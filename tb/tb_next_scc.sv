//============================================================================
//  SCC test: replays the exact boot ROM SCC test sequence (routine at
//  ROM offset 0x31fa, see ROMV66 listing) against the real next_scc:
//
//    read Control A
//    WR9 pointer, then 0xCA (hard reset)
//    20-byte init table (from ROM offset 0x15344) to Control A, then B
//    for each channel: select RR0, poll TX empty (bit 2), write 0x40 to
//    the data port, poll RX available (bit 0), read the data port back
//    and compare with 0x40
//
//  Also checks the point-high pointer arithmetic (writing 0x09 must
//  reach WR9) via the WR12/WR13 readback path.
//============================================================================

`timescale 1ns/1ps

module tb_next_scc;

reg clk = 0;
always #5 clk = ~clk;

reg reset = 1;

reg         sel = 0;
reg         addr1 = 0;
reg         we = 0;
reg   [1:0] be = 0;
reg  [15:0] wdata = 0;
wire [15:0] rdata;

next_scc dut
(
	.clk(clk), .reset(reset),
	.sel(sel), .addr1(addr1), .we(we), .be(be),
	.wdata(wdata), .rdata(rdata)
);

// byte write: a = byte address within the 4-byte block (0=ctrlB, 1=ctrlA,
// 2=dataB, 3=dataA)
task wr8;
	input [1:0] a;
	input [7:0] v;
	begin
		@(posedge clk);
		sel <= 1; addr1 <= a[1]; we <= 1;
		be <= a[0] ? 2'b01 : 2'b10;
		wdata <= a[0] ? {8'h00, v} : {v, 8'h00};
		@(posedge clk);
		sel <= 0; we <= 0;
	end
endtask

task rd8;
	input [1:0] a;
	output [7:0] v;
	begin
		@(posedge clk);
		sel <= 1; addr1 <= a[1]; we <= 0;
		be <= a[0] ? 2'b01 : 2'b10;
		@(posedge clk);
		v = a[0] ? rdata[7:0] : rdata[15:8];
		sel <= 0;
	end
endtask

// init table from ROM offset 0x15344
reg [7:0] init_tab [0:19];
initial begin
	init_tab[0]=8'h04;  init_tab[1]=8'h44;
	init_tab[2]=8'h0b;  init_tab[3]=8'h50;
	init_tab[4]=8'h0e;  init_tab[5]=8'h10;
	init_tab[6]=8'h0c;  init_tab[7]=8'h0a;
	init_tab[8]=8'h0d;  init_tab[9]=8'h00;
	init_tab[10]=8'h0e; init_tab[11]=8'h11;
	init_tab[12]=8'h0a; init_tab[13]=8'h00;
	init_tab[14]=8'h03; init_tab[15]=8'hc1;
	init_tab[16]=8'h05; init_tab[17]=8'hea;
	init_tab[18]=8'h0f; init_tab[19]=8'h00;
end

integer errors = 0;

task check;
	input cond;
	input [255:0] name;
	begin
		if (cond) $display("PASS: %0s", name);
		else begin $display("FAIL: %0s", name); errors = errors + 1; end
	end
endtask

// one channel loopback pass, ctrl/data byte addresses as arguments;
// returns the ROM's error code (0 = pass)
task loopback;
	input [1:0] ctrl;
	input [1:0] data;
	output integer code;
	integer n;
	reg [7:0] v;
	begin
		code = 0;
		// poll TX empty
		n = 0; v = 0;
		while (n <= 4999 && !(v & 8'h04)) begin
			wr8(ctrl, 8'h00);
			rd8(ctrl, v);
			n = n + 1;
		end
		if (!(v & 8'h04)) code = 1;
		else begin
			wr8(data, 8'h40);
			// poll RX available
			n = 0; v = 0;
			while (n <= 4999 && !(v & 8'h01)) begin
				wr8(ctrl, 8'h00);
				rd8(ctrl, v);
				n = n + 1;
			end
			if (!(v & 8'h01)) code = 2;
			else begin
				rd8(data, v);
				if (v != 8'h40) code = 3;
			end
		end
	end
endtask

integer i, code_a, code_b;
reg [7:0] v;

initial begin
	repeat (10) @(posedge clk);
	reset = 0;
	repeat (10) @(posedge clk);

	// ROM sequence: dummy read, WR9 hard reset
	rd8(2'd1, v);
	wr8(2'd1, 8'h09);
	wr8(2'd1, 8'hca);

	// init table to channel A then channel B
	for (i = 0; i < 20; i = i + 1) wr8(2'd1, init_tab[i]);
	rd8(2'd0, v);
	for (i = 0; i < 20; i = i + 1) wr8(2'd0, init_tab[i]);
	rd8(2'd1, v);

	// channel A loopback (ctrl 1, data 3), then B (ctrl 0, data 2)
	loopback(2'd1, 2'd3, code_a);
	check(code_a == 0, "channel A loopback (ROM error code 0)");
	if (code_a != 0) $display("  channel A error code %0d", code_a);

	loopback(2'd0, 2'd2, code_b);
	check(code_b == 0, "channel B loopback (ROM error code 0)");
	if (code_b != 0) $display("  channel B error code %0d", code_b + 4);

	// pointer arithmetic: point-high reaches WR12/WR13 storage
	wr8(2'd1, 8'h0c); wr8(2'd1, 8'h5a);
	wr8(2'd1, 8'h0c); rd8(2'd1, v);
	check(v == 8'h5a, "WR12 write/readback through the pointer");

	wr8(2'd1, 8'h09);           // 0x09 = point high + reg 1 -> WR9
	wr8(2'd1, 8'h80);           // reset channel A
	wr8(2'd1, 8'h00); rd8(2'd1, v);
	check((v & 8'h04) != 0, "RR0 TX empty set after channel reset");

	if (errors == 0) $display("ALL PASS");
	else             $display("%0d FAILURES", errors);
	$finish;
end

endmodule
