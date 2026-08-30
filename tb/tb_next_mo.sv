//============================================================================
//  MO/ECC test: replays the full boot ROM ECC system test flow (the
//  routine at ROM offset 0x381a) against the real next_mo with its
//  Reed-Solomon codec:
//    1. ECC Write fill of 1024 pattern bytes (csr2 = ECC_DIS)
//    2. ECC Read drain of the encoded 1296 byte sector
//    3. corrupt bytes in memory with the ROM's exact pattern
//    4. ECC Write fill of the corrupted sector with decode
//       (csr2 = ECC_DIS|ECC_MODE), expect ecc_cnt = 0x24 (36)
//    5. ECC Read passthrough drain of the corrected 1024 bytes,
//       compare with the original pattern
//============================================================================

`timescale 1ns/1ps

module tb_next_mo;

reg clk = 0;
always #5 clk = ~clk;

reg reset = 1;

reg         sel_osp = 0, sel_csr = 0, sel_ptr = 0, sel_ini = 0;
reg   [4:0] addr = 0;
reg         we = 0;
reg   [1:0] be = 0;
reg  [15:0] wdata = 0;
wire [15:0] rdata;

wire        m_req, m_we, m_ack;
wire [23:0] m_addr;
wire  [3:0] m_be;
wire [31:0] m_din;
reg  [31:0] m_dout;

wire int_disk, int_disk_dma;

// short tick for simulation: 50 "us" = 50 cycles
next_mo #(.CLK_HZ(1000000)) dut
(
	.clk(clk), .reset(reset),
	.sel_osp(sel_osp), .sel_csr(sel_csr), .sel_ptr(sel_ptr), .sel_ini(sel_ini),
	.addr(addr), .we(we), .be(be), .wdata(wdata), .rdata(rdata),
	.m_req(m_req), .m_we(m_we), .m_addr(m_addr), .m_be(m_be),
	.m_din(m_din), .m_dout(m_dout), .m_ack(m_ack),
	.int_disk(int_disk), .int_disk_dma(int_disk_dma)
);

localparam SRC  = 32'h04002000;
localparam DST  = 32'h04003000;
localparam DST2 = 32'h04004000;

// RAM model
reg [31:0] ram [0:16383];
reg        ack_r;
assign m_ack = ack_r;

always @(posedge clk) begin
	if (reset) ack_r <= 0;
	else if (!m_req) ack_r <= 0;
	else if (!ack_r) begin
		if (m_we) begin
			if (m_be[3]) ram[m_addr[13:0]][31:24] <= m_din[31:24];
			if (m_be[2]) ram[m_addr[13:0]][23:16] <= m_din[23:16];
			if (m_be[1]) ram[m_addr[13:0]][15:8]  <= m_din[15:8];
			if (m_be[0]) ram[m_addr[13:0]][7:0]   <= m_din[7:0];
		end
		else m_dout <= ram[m_addr[13:0]];
		ack_r <= 1;
	end
end

function [7:0] ram_byte;
	input [31:0] a;
	reg [31:0] w;
	begin
		w = ram[a[15:2]];
		ram_byte = (a[1:0] == 0) ? w[31:24] :
		           (a[1:0] == 1) ? w[23:16] :
		           (a[1:0] == 2) ? w[15:8] : w[7:0];
	end
endfunction

task ram_set_byte;
	input [31:0] a;
	input [7:0] v;
	begin
		case (a[1:0])
			0: ram[a[15:2]][31:24] = v;
			1: ram[a[15:2]][23:16] = v;
			2: ram[a[15:2]][15:8]  = v;
			3: ram[a[15:2]][7:0]   = v;
		endcase
	end
endtask

// the corruption pattern of the ROM ECC test (raises exactly 36
// correctable byte errors)
task ram_corrupt;
	integer k;
	reg [7:0] b;
	begin
		ram_set_byte(DST + 'h001, ~ram_byte(DST + 'h001));
		ram_set_byte(DST + 'h032, ram_byte(DST + 'h032) + 8'd1);
		ram_set_byte(DST + 'h064, (~ram_byte(DST + 'h064)) + 8'h9c);
		ram_set_byte(DST + 'h0c8, ram_byte(DST + 'h0c8) + 8'hff);
		ram_set_byte(DST + 'h309, ~ram_byte(DST + 'h309));
		ram_set_byte(DST + 'h37a, ram_byte(DST + 'h37a) + 8'h17);
		ram_set_byte(DST + 'h457, ram_byte(DST + 'h456) + 8'h27);
		ram_set_byte(DST + 'h50a, ram_byte(DST + 'h50a) + 8'h16);
		for (k = 0; k < 32; k = k + 1) begin
			b = ~ram_byte(DST + 'h215 + k);
			ram_set_byte(DST + 'h215 + k, (b % 8'h2f) + 8'h25);
		end
	end
endtask

task osp_wr8;
	input [4:0] a;
	input [7:0] v;
	begin
		@(posedge clk);
		sel_osp <= 1; addr <= a; we <= 1;
		be <= a[0] ? 2'b01 : 2'b10;
		wdata <= a[0] ? {8'h00, v} : {v, 8'h00};
		@(posedge clk);
		sel_osp <= 0; we <= 0;
	end
endtask

task osp_rd8;
	input [4:0] a;
	output [7:0] v;
	begin
		@(posedge clk);
		sel_osp <= 1; addr <= a; we <= 0;
		be <= a[0] ? 2'b01 : 2'b10;
		@(posedge clk);
		v = a[0] ? rdata[7:0] : rdata[15:8];
		sel_osp <= 0;
	end
endtask

task ptr_wr32;
	input [4:0] a;
	input [31:0] v;
	begin
		@(posedge clk);
		sel_ptr <= 1; addr <= a; we <= 1; be <= 2'b11; wdata <= v[31:16];
		@(posedge clk);
		addr <= a + 5'd2; wdata <= v[15:0];
		@(posedge clk);
		sel_ptr <= 0; we <= 0;
	end
endtask

task csr_cmd;
	input [7:0] v;
	begin
		@(posedge clk);
		sel_csr <= 1; addr <= 5'h00; we <= 1; be <= 2'b11; wdata <= {8'h00, v};
		@(posedge clk);
		addr <= 5'h02; wdata <= 16'h0000;
		@(posedge clk);
		sel_csr <= 0; we <= 0;
	end
endtask

integer errors = 0;

task check;
	input cond;
	input [639:0] name;
	begin
		if (cond) $display("PASS: %0s", name);
		else begin $display("FAIL: %0s", name); errors = errors + 1; end
	end
endtask

integer i;
reg [7:0] v;
reg ok;


initial begin
	for (i = 0; i < 16384; i = i + 1) ram[i] = 32'h00000000;
	for (i = 0; i < 1024; i = i + 1) begin
		case (i[1:0])
			0: ram[(SRC[15:2]) + i/4][31:24] = i[7:0];
			1: ram[(SRC[15:2]) + i/4][23:16] = i[7:0];
			2: ram[(SRC[15:2]) + i/4][15:8]  = i[7:0];
			3: ram[(SRC[15:2]) + i/4][7:0]   = i[7:0];
		endcase
	end

	repeat (10) @(posedge clk);
	reset = 0;
	repeat (10) @(posedge clk);

	// formatter reset
	osp_wr8(5'h07, 8'h00);
	// ECC_DIS mode: standalone buffer operation
	osp_wr8(5'h06, 8'h40);
	// interrupt mask: ECC_DONE
	osp_wr8(5'h05, 8'h08);

	// fill: channel at the source, enable, ECC Write command
	ptr_wr32(5'h00, SRC);
	ptr_wr32(5'h04, SRC + 32'd1024);
	csr_cmd(8'h11);              // RESET | SETENABLE
	osp_wr8(5'h07, 8'h40);       // FMT_ECC_WRITE

	repeat (80000) @(posedge clk);

	osp_rd8(5'h04, v);
	check(v[3], "MOINT_ECC_DONE after ECC write (fill)");
	check(int_disk, "INT_DISK raised (masked in)");
	osp_wr8(5'h04, 8'h08);       // clear
	repeat (2) @(posedge clk);
	check(!int_disk, "INT_DISK released after status clear");

	// drain: channel at the destination, ECC Read encodes 1296 bytes
	ptr_wr32(5'h00, DST);
	ptr_wr32(5'h04, DST + 32'd1296);
	csr_cmd(8'h11);
	osp_wr8(5'h07, 8'h80);       // FMT_ECC_READ

	repeat (140000) @(posedge clk);

	osp_rd8(5'h04, v);
	check(v[3], "MOINT_ECC_DONE after ECC read (encode and drain)");

	// the data bytes sit in the interleaved sector layout: byte i of
	// the input is at 36*(i/32) + (i%32)
	ok = 1;
	for (i = 0; i < 1024; i = i + 1)
		if (ram_byte(DST + (i/32)*36 + (i%32)) != i[7:0]) ok = 0;
	check(ok, "encoded sector carries the data in interleaved layout");

	// corrupt the sector in memory with the ROM test's exact pattern
	ram_corrupt;

	// decode: fill the corrupted 1296 bytes with ECC_MODE set
	osp_wr8(5'h04, 8'hfc);       // clear interrupt status
	osp_wr8(5'h06, 8'h60);       // ECC_DIS | ECC_MODE
	osp_wr8(5'h07, 8'h00);       // formatter reset (clears ecc_cnt)
	ptr_wr32(5'h00, DST);
	ptr_wr32(5'h04, DST + 32'd1296);
	csr_cmd(8'h11);
	osp_wr8(5'h07, 8'h40);       // FMT_ECC_WRITE (fill + decode)

	repeat (200000) @(posedge clk);

	osp_rd8(5'h04, v);
	check(v[3], "MOINT_ECC_DONE after decode");
	osp_rd8(5'h0b, v);
	$display("ecc_cnt = %0d (ROM expects 36)", v);
	check(v == 8'h24, "ecc_cnt reads 0x24 corrected errors");
	osp_rd8(5'h0a, v);
	check(v == 8'h00, "no ERRSTAT_ECC");

	// passthrough drain of the corrected data
	osp_wr8(5'h04, 8'hfc);
	ptr_wr32(5'h00, DST2);
	ptr_wr32(5'h04, DST2 + 32'd1024);
	csr_cmd(8'h11);
	osp_wr8(5'h07, 8'h80);       // FMT_ECC_READ (passthrough, ECC_MODE)

	repeat (120000) @(posedge clk);

	osp_rd8(5'h04, v);
	check(v[3], "MOINT_ECC_DONE after passthrough drain");
	ok = 1;
	for (i = 0; i < 1024; i = i + 1)
		if (ram_byte(DST2 + i) != i[7:0]) ok = 0;
	check(ok, "corrected data matches the original pattern");

	if (errors == 0) $display("ALL PASS");
	else             $display("%0d FAILURES", errors);
	$finish;
end

endmodule
