//============================================================================
//  BMAP ethernet cable/select test.
//
//  Register D reports the external transceiver state: connected twisted-pair
//  sets BMAP_TPE_ILBC, disconnected sets BMAP_HEARTBEAT.  Writes to the TPE
//  select bits still choose twisted-pair/thinwire independently of link.
//============================================================================

`timescale 1ns/1ps

module tb_next_bmap;

reg clk = 0;
always #5 clk = ~clk;

reg reset = 1;
reg sel = 0;
reg we = 0;
reg [5:0] addr = 0;
reg [1:0] be = 0;
reg [15:0] wdata = 0;
reg tpe_link = 0;
wire [15:0] rdata;
wire tpe_select;

next_bmap dut
(
	.clk(clk),
	.reset(reset),
	.sel(sel),
	.addr(addr),
	.we(we),
	.be(be),
	.wdata(wdata),
	.rdata(rdata),
	.tpe_link(tpe_link),
	.tpe_select(tpe_select)
);

integer errors = 0;
reg [15:0] v;

task check;
	input cond;
	input [511:0] name;
	begin
		if (cond) $display("PASS: %0s", name);
		else begin $display("FAIL: %0s", name); errors = errors + 1; end
	end
endtask

task wr16;
	input [5:0] a;
	input [15:0] v_in;
	begin
		@(posedge clk);
		sel <= 1; we <= 1; addr <= a; be <= 2'b11; wdata <= v_in;
		@(posedge clk);
		sel <= 0; we <= 0;
	end
endtask

task rd16;
	input [5:0] a;
	output [15:0] v_out;
	begin
		@(posedge clk);
		sel <= 1; we <= 0; addr <= a; be <= 2'b11;
		@(posedge clk);
		v_out = rdata;
		sel <= 0;
	end
endtask

initial begin
	repeat (4) @(posedge clk);
	reset = 0;
	repeat (4) @(posedge clk);

	// Register D high half contains TPE_RXSEL, HEARTBEAT and TPE_ILBC.
	rd16(6'h34, v);
	check(v[13] && !v[12],
	      "disconnected cable reports heartbeat and no TPE ILBC");

	tpe_link = 1;
	repeat (2) @(posedge clk);
	rd16(6'h34, v);
	check(!v[13] && v[12],
	      "connected twisted-pair reports TPE ILBC and no heartbeat");

	wr16(6'h34, 16'h9000);
	@(posedge clk);
	check(tpe_select, "writing both TPE bits selects twisted pair");

	wr16(6'h34, 16'h0000);
	@(posedge clk);
	check(!tpe_select, "clearing both TPE bits selects thinwire");

	tpe_link = 0;
	repeat (2) @(posedge clk);
	rd16(6'h34, v);
	check(v[13] && !v[12],
	      "dropping cable switches readback to disconnected");

	if (errors == 0) $display("ALL PASS");
	else             $display("%0d FAILURES", errors);
	$finish;
end

endmodule
