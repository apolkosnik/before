`timescale 1ns/1ps
module tb_next_exception_mailbox;
reg clk = 0, reset = 1;
always #5 clk = ~clk;
reg event_valid = 0;
reg [511:0] event_data = 0;
reg b_req = 0, b_we = 0;
reg [28:0] b_addr = 0;
reg [63:0] b_wdata = 0;
wire b_ack, m_req, m_we, m_ack;
wire [28:0] m_addr;
wire [63:0] b_rdata, m_wdata, m_rdata;
wire [28:0] ddr_addr;
wire [63:0] ddr_din;
wire ddr_rd, ddr_we;
reg stall = 0, ddr_ready = 0;
reg [63:0] ddr_dout = 0;
integer cycle = 0, writes = 0;
wire ddr_busy = stall || cycle % 7 < 3;
reg pending = 0;
reg [63:0] pending_data;
reg [63:0] ram [0:8191];
localparam [28:0] BASE = 29'h03fe0a00;
localparam [63:0] MAGIC = 64'h4e58544449414731;

next_exception_mailbox dut (.*);
next_ddram_arb arb (
	.clk(clk), .reset(reset),
	.a_rd(1'b0), .a_we(1'b0), .a_addr(29'd0), .a_din(64'd0),
	.a_be(8'hff), .a_burst(8'd1),
	.b_req(m_req), .b_we(m_we), .b_addr(m_addr), .b_wdata(m_wdata),
	.b_rdata(m_rdata), .b_ack(m_ack),
	.DDRAM_BUSY(ddr_busy), .DDRAM_ADDR(ddr_addr), .DDRAM_DIN(ddr_din),
	.DDRAM_RD(ddr_rd), .DDRAM_WE(ddr_we),
	.DDRAM_DOUT(ddr_dout), .DDRAM_DOUT_READY(ddr_ready)
);
always @(posedge clk) begin
	cycle <= cycle + 1;
	ddr_ready <= 0;
	ddr_dout <= 64'hdeadbeefdeadbeef;
	if (reset) pending <= 0;
	else begin
		if (pending) begin
			ddr_ready <= 1;
			ddr_dout <= pending_data;
			pending <= 0;
		end
		if (ddr_rd && !ddr_busy) begin
			pending <= 1;
			pending_data <= ram[ddr_addr[12:0]];
		end
		if (ddr_we && !ddr_busy) begin
			ram[ddr_addr[12:0]] <= ddr_din;
			if (ddr_addr >= BASE && ddr_addr < BASE + 10) writes <= writes + 1;
			else if (ddr_addr != 29'h03fe0001) $fatal(1, "write outside allocated mailbox");
		end
	end
end

task check(input bit condition, input string name);
	if (!condition) $fatal(1, "%s", name);
endtask
task wait_word(input integer idx, input [63:0] value);
	integer n;
	begin
		n = 0;
		while (ram[idx] !== value && n < 2000) begin @(negedge clk); n = n + 1; end
		check(n < 2000, "mailbox timeout");
	end
endtask
task pulse_event(input [511:0] data);
	begin
		@(negedge clk); event_data = data; event_valid = 1;
		@(negedge clk); event_valid = 0; event_data = ~data;
	end
endtask
task bridge_xfer(input bit wr, input [63:0] data);
	integer n;
	begin
		@(negedge clk);
		b_req = 1; b_we = wr; b_addr = 29'h03fe0001; b_wdata = data;
		n = 0;
		while (!b_ack && n < 2000) begin @(negedge clk); n = n + 1; end
		check(n < 2000, "bridge transaction timeout");
		if (!wr) check(b_rdata == data, "bridge read data/ownership");
		@(negedge clk); b_req = 0;
		repeat (8) @(negedge clk);
	end
endtask

integer i;
reg [511:0] sample_data;
initial begin
	for (i = 0; i < 8192; i = i + 1) ram[i] = 64'hfeedfacefeedface;
	for (i = 0; i < 8; i = i + 1) sample_data[i*64 +: 64] = 64'h1234000056780000 + i;
	repeat (5) @(negedge clk); reset = 0;
	wait_word('ha00, MAGIC);
	check(ram['ha01] == 0 && writes == 3, "startup invalidates stale status before magic");
	bridge_xfer(1, 64'h0102030405060708);
	// Event during an outstanding bridge read and a long DDR stall.
	stall = 1;
	fork
		bridge_xfer(0, 64'h0102030405060708);
		begin
			repeat (20) @(negedge clk);
			pulse_event(sample_data);
			repeat (20) @(negedge clk);
			check(ram['ha01] == 0, "no premature valid while DDR stalled");
			stall = 0;
		end
	join
	wait_word('ha01, 1);
	for (i = 0; i < 8; i = i + 1)
		check(ram['ha02+i] == sample_data[i*64 +: 64], "payload ordering/first event retention");
	check(writes == 12, "no duplicated writes");
	pulse_event(~sample_data);
	bridge_xfer(1, 64'habcd);
	bridge_xfer(0, 64'habcd);
	repeat (100) @(negedge clk);
	check(writes == 12, "record immutable after later events/bridge traffic");
	// Reset clears old validity; capture during initialization without any
	// Ethernet requests proves that the network-off path still publishes.
	reset = 1; repeat (5) @(negedge clk); reset = 0;
	pulse_event(~sample_data);
	wait_word('ha01, 0);
	wait_word('ha01, 1);
	for (i = 0; i < 8; i = i + 1)
		check(ram['ha02+i] == ~sample_data[i*64 +: 64], "reset/rearm without network");
	check(writes == 24, "reset publishes exactly one new record");
	// Reset halfway through a third publication, while the arbiter is
	// stalled on a payload write. The following boot must discard it.
	reset = 1; repeat (5) @(negedge clk); reset = 0;
	wait_word('ha01, 0);
	wait_word('ha00, MAGIC);
	pulse_event(sample_data);
	wait (writes >= 29);
	@(negedge clk); stall = 1;
	repeat (10) @(negedge clk);
	check(ram['ha01] == 0, "partial payload was not marked complete");
	reset = 1; repeat (5) @(negedge clk); reset = 0; stall = 0;
	repeat (100) @(negedge clk);
	check(ram['ha00] == MAGIC && ram['ha01] == 0, "reset invalidates interrupted publication");
	pulse_event(~sample_data);
	wait_word('ha01, 1);
	for (i = 0; i < 8; i = i + 1)
		check(ram['ha02+i] == ~sample_data[i*64 +: 64], "new boot replaces partial payload");
	$display("ALL PASS"); $finish;
end
initial begin #200000; $fatal(1, "global timeout"); end
endmodule
