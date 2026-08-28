//============================================================================
//  ESP SCSI controller test: replays the boot ROM SCSI system test
//  (routine at ROM offset 0x337e) and the extended transfer count and
//  configuration checks (0x3430) against the real next_esp:
//
//    DMA control 0x82 (reset) then 0x80, chip reset + NOP commands,
//    push 0x00..0x04 into the FIFO, FIFO flags must read 5, pop two
//    bytes (0x00 then 0x01), flags must read 3.
//
//    Extended: 0x55/0x55 into the transfer count staging, DMA NOP
//    (0x80) loads the counter, reads back 0x5555; repeat with 0xAA;
//    configuration register write/readback.
//============================================================================

`timescale 1ns/1ps

module tb_next_esp;

reg clk = 0;
always #5 clk = ~clk;

reg reset = 1;

reg         sel = 0;
reg   [5:0] addr = 0;
reg         we = 0;
reg   [1:0] be = 0;
reg  [15:0] wdata = 0;
wire [15:0] rdata;

next_esp dut
(
	.clk(clk), .reset(reset),
	.sel(sel), .addr(addr), .we(we), .be(be),
	.wdata(wdata), .rdata(rdata),
	.int_scsi()
);

task wr8;
	input [5:0] a;
	input [7:0] v;
	begin
		@(posedge clk);
		sel <= 1; addr <= a; we <= 1;
		be <= a[0] ? 2'b01 : 2'b10;
		wdata <= a[0] ? {8'h00, v} : {v, 8'h00};
		@(posedge clk);
		sel <= 0; we <= 0;
	end
endtask

task rd8;
	input [5:0] a;
	output [7:0] v;
	begin
		@(posedge clk);
		sel <= 1; addr <= a; we <= 0;
		be <= a[0] ? 2'b01 : 2'b10;
		@(posedge clk);
		v = a[0] ? rdata[7:0] : rdata[15:8];
		sel <= 0;
	end
endtask

integer errors = 0;

task check;
	input cond;
	input [255:0] name;
	begin
		if (cond) $display("PASS: %0s", name);
		else begin $display("FAIL: %0s", name); errors = errors + 1; end
	end
endtask

reg [7:0] v, v2;

initial begin
	repeat (10) @(posedge clk);
	reset = 0;
	repeat (10) @(posedge clk);

	// ROM system test sequence
	wr8(6'h20, 8'h82);           // DMA ctrl: 20MHz clock + reset
	repeat (20) @(posedge clk);
	wr8(6'h20, 8'h80);           // release reset
	wr8(6'h03, 8'h02);           // command: reset chip
	wr8(6'h03, 8'h00);           // command: NOP
	wr8(6'h02, 8'h00);
	wr8(6'h02, 8'h01);
	wr8(6'h02, 8'h02);
	wr8(6'h02, 8'h03);
	wr8(6'h02, 8'h04);

	rd8(6'h07, v);
	check((v & 8'h1f) == 8'h05, "FIFO flags read 5 after five writes");

	rd8(6'h02, v);
	check(v == 8'h00, "first FIFO byte is 0x00");
	rd8(6'h02, v);
	check(v == 8'h01, "second FIFO byte is 0x01");

	rd8(6'h07, v);
	check((v & 8'h1f) == 8'h03, "FIFO flags read 3 after two reads");

	// extended test: transfer counter loads on DMA command
	wr8(6'h20, 8'h82);
	repeat (20) @(posedge clk);
	wr8(6'h20, 8'h80);
	wr8(6'h03, 8'h02);
	wr8(6'h03, 8'h00);
	wr8(6'h03, 8'h01);           // flush FIFO (ROM extended test does this)

	wr8(6'h00, 8'h55);
	wr8(6'h01, 8'h55);
	wr8(6'h03, 8'h80);           // DMA NOP: loads the counter
	rd8(6'h00, v);
	rd8(6'h01, v2);
	check({v2, v} == 16'h5555, "transfer counter loads 0x5555 on DMA command");

	wr8(6'h00, 8'haa);
	wr8(6'h01, 8'haa);
	wr8(6'h03, 8'h80);
	rd8(6'h00, v);
	rd8(6'h01, v2);
	check({v2, v} == 16'haaaa, "transfer counter loads 0xaaaa on DMA command");

	// configuration register readback
	wr8(6'h08, 8'h13);
	rd8(6'h08, v);
	check(v == 8'h13, "configuration register write/readback");

	// chip reset clears the FIFO and configuration test bits
	wr8(6'h02, 8'h7e);
	wr8(6'h03, 8'h02);
	rd8(6'h07, v);
	check((v & 8'h1f) == 8'h00, "chip reset clears the FIFO");
	rd8(6'h08, v);
	check(v == 8'h03, "chip reset clears configuration bits 7:3");

	if (errors == 0) $display("ALL PASS");
	else             $display("%0d FAILURES", errors);
	$finish;
end

endmodule
