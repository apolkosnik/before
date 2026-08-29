//============================================================================
// DDRAM arbiter regression: port-B read data is valid for only the Avalon
// DOUT_READY cycle.  The req/ack client samples it later, after ownership has
// returned to A, so the arbiter must retain the completed word.
//============================================================================

`timescale 1ns/1ps

module tb_next_ddram_arb;

reg clk = 0;
always #5 clk = ~clk;

reg reset = 1;

reg         a_rd = 0, a_we = 0;
reg  [28:0] a_addr = 0;
reg  [63:0] a_din = 0;
reg   [7:0] a_be = 8'hff, a_burst = 1;
wire        a_busy;
wire [63:0] a_dout;
wire        a_dout_ready;

reg         b_req = 0, b_we = 0;
reg  [28:0] b_addr = 0;
reg  [63:0] b_wdata = 0;
wire [63:0] b_rdata;
wire        b_ack;

reg         ddr_busy = 0;
reg  [63:0] ddr_dout = 0;
reg         ddr_ready = 0;
wire  [7:0] ddr_burst;
wire [28:0] ddr_addr;
wire [63:0] ddr_din;
wire  [7:0] ddr_be;
wire        ddr_rd, ddr_we;

next_ddram_arb dut
(
	.clk(clk), .reset(reset),
	.a_rd(a_rd), .a_we(a_we), .a_addr(a_addr), .a_din(a_din),
	.a_be(a_be), .a_burst(a_burst), .a_busy(a_busy),
	.a_dout(a_dout), .a_dout_ready(a_dout_ready),
	.b_req(b_req), .b_we(b_we), .b_addr(b_addr), .b_wdata(b_wdata),
	.b_rdata(b_rdata), .b_ack(b_ack),
	.DDRAM_BUSY(ddr_busy), .DDRAM_BURSTCNT(ddr_burst),
	.DDRAM_ADDR(ddr_addr), .DDRAM_DOUT(ddr_dout),
	.DDRAM_DOUT_READY(ddr_ready), .DDRAM_RD(ddr_rd),
	.DDRAM_DIN(ddr_din), .DDRAM_BE(ddr_be), .DDRAM_WE(ddr_we)
);

localparam [63:0] B_WORD = 64'h0123456789abcdef;
localparam [63:0] A_WORD = 64'hfedcba9876543210;
localparam [63:0] POISON = 64'hdeadbeefbad0cafe;

reg pending = 0;
reg [63:0] pending_data;

// One-cycle DOUT_READY model.  DOUT is deliberately poisoned immediately
// afterward; holding it indefinitely would mask the real interface bug.
always @(posedge clk) begin
	ddr_ready <= 0;
	ddr_dout <= POISON;
	if (reset) begin
		ddr_busy <= 0;
		pending <= 0;
	end
	else begin
		if (pending) begin
			ddr_busy <= 0;
			ddr_ready <= 1;
			ddr_dout <= pending_data;
			pending <= 0;
		end
		else if (ddr_rd && !ddr_busy) begin
			ddr_busy <= 1;
			pending <= 1;
			pending_data <= (ddr_addr == 29'h03fe0002) ? B_WORD : A_WORD;
		end
	end
end

integer errors = 0;
task check;
	input cond;
	input [511:0] name;
	begin
		if (cond) $display("PASS: %0s", name);
		else begin $display("FAIL: %0s", name); errors = errors + 1; end
	end
endtask

integer n;
initial begin
	repeat (5) @(posedge clk);
	reset = 0;

	// Port B read: its client sees ack after the raw DDR word has expired.
	@(posedge clk);
	b_addr <= 29'h03fe0002;
	b_req <= 1;
	n = 0;
	while (!b_ack && n < 20) begin @(posedge clk); n = n + 1; end
	check(b_ack, "port B read acknowledged");
	check(b_rdata == B_WORD, "port B retained ready-cycle data");
	b_req <= 0;

	// Let A replace the raw DDR output, then prove B's completed value is
	// still stable for the bridge return state.
	@(posedge clk);
	a_addr <= 29'h0600000;
	a_rd <= 1;
	n = 0;
	while (!a_dout_ready && n < 20) begin @(posedge clk); n = n + 1; end
	check(a_dout_ready && a_dout == A_WORD, "port A read still passes through");
	a_rd <= 0;
	repeat (3) @(posedge clk);
	check(b_rdata == B_WORD, "port B data survives later traffic");

	if (errors == 0) $display("ALL PASS");
	else $display("%0d FAILURES", errors);
	$finish;
end

endmodule
