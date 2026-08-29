//============================================================================
//  Floppy over the real shared DMA channel.
//
//  next_floppy has no memory master: its sectors are moved by the SCSI
//  channel inside next_scsi, switched over by the controller's external
//  control register.  The floppy's own bench replaces that channel with
//  an obliging model, so nothing has ever exercised the pair together -
//  and the channel is where a driver's programming meets the transfer:
//  it has next and limit registers, it completes when next reaches
//  limit, and it stalls when it runs out.
//
//  This drives the pair the way a driver does: program the channel over
//  a buffer, ask for a whole track, and require every sector to land in
//  memory in order.
//============================================================================

`timescale 1ns/1ps

module tb_next_flpdma;

reg clk = 0;
always #5 clk = ~clk;

reg reset = 1;

// floppy registers
reg        f_sel = 0;
reg  [3:0] f_addr = 0;
reg        f_we = 0;
reg  [1:0] f_be = 0;
reg [15:0] f_wdata = 0;
wire [15:0] f_rdata;

// SCSI/channel registers
reg        s_esp = 0, s_csr = 0, s_sptr = 0, s_ptr = 0, s_ini = 0;
reg  [5:0] s_addr = 0;
reg        s_we = 0;
reg  [1:0] s_be = 0;
reg [15:0] s_wdata = 0;
wire [15:0] s_rdata;

wire        flp_select, int_floppy;
wire        flp_req, flp_wr, flp_bwe, flp_done;
wire [10:0] flp_len;
wire  [9:0] flp_addr;
wire  [7:0] flp_bwdata, flp_bq;

wire        m_req, m_we, m_ack;
wire [23:0] m_addr;
wire  [3:0] m_be;
wire [31:0] m_din;
reg  [31:0] m_dout;

wire [31:0] fsd_lba;
wire        fsd_rd, fsd_wr;
reg         fsd_ack = 0;
reg   [8:0] fsd_buff_addr = 0;
reg   [7:0] fsd_buff_dout = 0;
wire  [7:0] fsd_buff_din;
reg         fsd_buff_wr = 0;
reg         fimg_mounted = 0;
reg  [63:0] fimg_size = 0;

next_floppy #(.CLK_HZ(1000000)) floppy
(
	.clk(clk), .reset(reset),
	.sel(f_sel), .addr(f_addr), .we(f_we), .be(f_be),
	.wdata(f_wdata), .rdata(f_rdata),
	.int_floppy(int_floppy), .flp_select(flp_select), .mo_gpo(1'b0),
	.buf_addr(flp_addr), .buf_we(flp_bwe), .buf_wdata(flp_bwdata),
	.buf_q(flp_bq), .buf_len(flp_len),
	.dma_req(flp_req), .dma_wr(flp_wr), .dma_done(flp_done),
	.img_mounted(fimg_mounted), .img_readonly(1'b0), .img_size(fimg_size),
	.sd_lba(fsd_lba), .sd_rd(fsd_rd), .sd_wr(fsd_wr), .sd_ack(fsd_ack),
	.sd_buff_addr(fsd_buff_addr), .sd_buff_dout(fsd_buff_dout),
	.sd_buff_din(fsd_buff_din), .sd_buff_wr(fsd_buff_wr)
);

next_scsi #(.CLK_HZ(1000000)) scsi
(
	.clk(clk), .reset(reset),
	.sel_esp(s_esp), .sel_csr(s_csr), .sel_sptr(s_sptr),
	.sel_ptr(s_ptr), .sel_ini(s_ini),
	.addr(s_addr), .we(s_we), .be(s_be), .wdata(s_wdata), .rdata(s_rdata),
	.m_req(m_req), .m_we(m_we), .m_addr(m_addr), .m_be(m_be),
	.m_din(m_din), .m_dout(m_dout), .m_ack(m_ack),
	.flp_select(flp_select), .flp_req(flp_req), .flp_wr(flp_wr),
	.flp_len(flp_len), .flp_addr(flp_addr), .flp_bwe(flp_bwe),
	.flp_bwdata(flp_bwdata), .flp_bq(flp_bq), .flp_done(flp_done),
	.int_scsi(), .int_scsi_dma(),
	.img_mounted(1'b0), .img_readonly(1'b0), .img_size(64'd0),
	.sd_lba(), .sd_rd(), .sd_wr(), .sd_ack(1'b0),
	.sd_buff_addr(9'd0), .sd_buff_dout(8'd0), .sd_buff_din(),
	.sd_buff_wr(1'b0)
);

//----------------------------------------------------------------------------
// memory the channel writes into
//----------------------------------------------------------------------------

reg [31:0] ram [0:16383];
reg        ack_r = 0;
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

//----------------------------------------------------------------------------
// the floppy image
//----------------------------------------------------------------------------

localparam BLOCKS = 2880;
reg [7:0] disk [0:BLOCKS*512-1];

function [7:0] pat;
	input [31:0] blk;
	input [31:0] off;
	begin
		pat = blk[7:0] ^ off[7:0] ^ {blk[10:8], 5'd0};
	end
endfunction

integer di;
initial for (di = 0; di < BLOCKS*512; di = di + 1) disk[di] = pat(di/512, di%512);

reg sd_rd_act = 0;
integer sd_reads = 0;

always @(posedge clk) begin
	if (fsd_rd && !fsd_ack && !sd_rd_act) begin
		fsd_ack <= 1; sd_rd_act <= 1; fsd_buff_addr <= 0; fsd_buff_wr <= 0;
		sd_reads = sd_reads + 1;
	end
	else if (fsd_ack && sd_rd_act) begin
		if (!fsd_buff_wr) begin
			fsd_buff_dout <= disk[{fsd_lba[11:0], 9'd0} + {23'd0, fsd_buff_addr}];
			fsd_buff_wr <= 1;
		end
		else begin
			fsd_buff_wr <= 0;
			if (fsd_buff_addr == 9'd511) begin fsd_ack <= 0; sd_rd_act <= 0; end
			else fsd_buff_addr <= fsd_buff_addr + 1'd1;
		end
	end
end

//----------------------------------------------------------------------------
// register access
//----------------------------------------------------------------------------

task fwr;
	input [3:0] a;
	input [7:0] v;
	begin
		@(posedge clk);
		f_sel <= 1; f_addr <= a; f_we <= 1;
		f_be <= a[0] ? 2'b01 : 2'b10;
		f_wdata <= a[0] ? {8'h00, v} : {v, 8'h00};
		@(posedge clk);
		f_sel <= 0; f_we <= 0;
	end
endtask

task frd;
	input [3:0] a;
	output [7:0] v;
	begin
		@(posedge clk);
		f_sel <= 1; f_addr <= a; f_we <= 0;
		f_be <= a[0] ? 2'b01 : 2'b10;
		@(posedge clk);
		v = a[0] ? f_rdata[7:0] : f_rdata[15:8];
		f_sel <= 0;
		@(posedge clk);
	end
endtask

task ptr_wr32;
	input [5:0] a;
	input [31:0] v;
	begin
		@(posedge clk);
		s_ptr <= 1; s_addr <= a; s_we <= 1; s_be <= 2'b11; s_wdata <= v[31:16];
		@(posedge clk);
		s_addr <= a + 6'd2; s_wdata <= v[15:0];
		@(posedge clk);
		s_ptr <= 0; s_we <= 0;
	end
endtask

task csr_cmd;
	input [7:0] v;
	begin
		@(posedge clk);
		s_csr <= 1; s_addr <= 6'h00; s_we <= 1; s_be <= 2'b11;
		s_wdata <= {8'h00, v};
		@(posedge clk);
		s_csr <= 0; s_we <= 0;
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

localparam BUF = 32'h00002000;

integer i, s, bad;
reg [7:0] v;
integer waited;

initial begin
	repeat (10) @(posedge clk);
	reset = 0;
	repeat (10) @(posedge clk);

	fimg_size = BLOCKS*512;
	fimg_mounted = 1;
	@(posedge clk);
	fimg_mounted = 0;
	repeat (20) @(posedge clk);

	// Ask for a sector BEFORE handing the channel over.  The drive will
	// hold its request up while it waits; if the channel only watches
	// for an edge, that request is consumed here and never seen again.
	fwr(4'h2, 8'h1C);            // motor 0 on, channel still on the ESP
	fwr(4'h5, 8'h46);
	fwr(4'h5, 8'h00); fwr(4'h5, 8'h00); fwr(4'h5, 8'h00);
	fwr(4'h5, 8'h01); fwr(4'h5, 8'h02); fwr(4'h5, 8'h01);
	fwr(4'h5, 8'h1B); fwr(4'h5, 8'hFF);
	repeat (3000) @(posedge clk);          // let the request go up and sit

	ptr_wr32(6'h10, BUF);
	ptr_wr32(6'h14, BUF + 32'd512);
	csr_cmd(8'h11);
	fwr(4'h8, 8'h40);            // only now switch the channel over
	@(posedge clk);
	check(flp_select, "the channel is switched to the floppy");

	waited = 0;
	while (!int_floppy && waited < 2000000) begin
		@(posedge clk);
		waited = waited + 1;
	end
	check(int_floppy, "a request raised before selection is still served");
	bad = 0;
	for (i = 0; i < 512; i = i + 1)
		if (ram_byte(BUF + i) !== pat(0, i)) bad = bad + 1;
	check(bad == 0, "that deferred sector arrives intact");
	for (i = 0; i < 7; i = i + 1) frd(4'h5, v);   // drain the result

	// program the channel over a whole track, the way a driver does
	ptr_wr32(6'h10, BUF);
	ptr_wr32(6'h14, BUF + 32'd18*512);
	csr_cmd(8'h11);              // RESET | SETENABLE

	// read the whole track
	fwr(4'h5, 8'h46);
	fwr(4'h5, 8'h00);            // drive 0, head 0
	fwr(4'h5, 8'h00);            // C
	fwr(4'h5, 8'h00);            // H
	fwr(4'h5, 8'h01);            // R
	fwr(4'h5, 8'h02);            // N
	fwr(4'h5, 8'd18);            // EOT
	fwr(4'h5, 8'h1B);
	fwr(4'h5, 8'hFF);

	// the transfer runs on its own: wait for the completion interrupt
	waited = 0;
	while (!int_floppy && waited < 4000000) begin
		@(posedge clk);
		waited = waited + 1;
	end
	$display("  sectors fetched from the image: %0d, waited %0d cycles",
	         sd_reads, waited);
	check(int_floppy, "the track transfer completes");

	bad = 0;
	for (s = 0; s < 18; s = s + 1)
		for (i = 0; i < 512; i = i + 1)
			if (ram_byte(BUF + s*512 + i) !== pat(s, i)) begin
				if (bad < 4)
					$display("  sector %0d byte %0d: got %02x want %02x",
					         s + 1, i, ram_byte(BUF + s*512 + i), pat(s, i));
				bad = bad + 1;
			end
	check(bad == 0, "every sector of the track reached memory in order");

	if (errors == 0) $display("ALL PASS");
	else             $display("%0d FAILURES", errors);
	$finish;
end

endmodule
