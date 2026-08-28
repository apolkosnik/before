//============================================================================
//  KMS / sound out test: drives the real next_kms_snd the way the boot
//  ROM sound test does: program the sound out DMA channel, enable the
//  DMA via the KMS command/data pair (KMSCMD_SND_OUT | SIO_ENABLE),
//  expect the buffer to be consumed and the channel to complete with
//  INT_SND_OUT_DMA; once the buffer is exhausted with no chain, expect
//  the underrun status bits and INT_SOUND_OVRUN, released by the
//  control write and the sound out disable command.
//============================================================================

`timescale 1ns/1ps

module tb_next_snd;

reg clk = 0;
always #5 clk = ~clk;

reg reset = 1;

reg         sel_kms = 0, sel_csr = 0, sel_sptr = 0, sel_ptr = 0, sel_ini = 0;
reg   [3:0] addr = 0;
reg         we = 0;
reg   [1:0] be = 0;
reg  [15:0] wdata = 0;
wire [15:0] rdata;

wire        m_req, m_we, m_ack;
wire [23:0] m_addr;
wire  [3:0] m_be;
wire [31:0] m_din;
reg  [31:0] m_dout;

wire int_snd_ovrun, int_snd_out_dma;

// 1 MHz "clock" so a microsecond is one cycle
next_kms_snd #(.CLK_HZ(1000000)) dut
(
	.clk(clk), .reset(reset),
	.sel_kms(sel_kms), .sel_csr(sel_csr), .sel_sptr(sel_sptr),
	.sel_ptr(sel_ptr), .sel_ini(sel_ini),
	.addr(addr), .we(we), .be(be), .wdata(wdata), .rdata(rdata),
	.m_req(m_req), .m_we(m_we), .m_addr(m_addr), .m_be(m_be),
	.m_din(m_din), .m_dout(m_dout), .m_ack(m_ack),
	.int_snd_ovrun(int_snd_ovrun), .int_snd_out_dma(int_snd_out_dma)
);

// simple RAM ack
reg ack_r;
assign m_ack = ack_r;
always @(posedge clk) begin
	if (reset) ack_r <= 0;
	else if (!m_req) ack_r <= 0;
	else if (!ack_r) begin
		m_dout <= 32'h55aa1234;
		ack_r <= 1;
	end
end

task kms_wr8;
	input [3:0] a;
	input [7:0] v;
	begin
		@(posedge clk);
		sel_kms <= 1; addr <= a; we <= 1;
		be <= a[0] ? 2'b01 : 2'b10;
		wdata <= a[0] ? {8'h00, v} : {v, 8'h00};
		@(posedge clk);
		sel_kms <= 0; we <= 0;
	end
endtask

task kms_rd8;
	input [3:0] a;
	output [7:0] v;
	begin
		@(posedge clk);
		sel_kms <= 1; addr <= a; we <= 0;
		be <= a[0] ? 2'b01 : 2'b10;
		@(posedge clk);
		v = a[0] ? rdata[7:0] : rdata[15:8];
		sel_kms <= 0;
	end
endtask

// KMS command with data, like the ROM helper at 0x3e44
task kms_cmd;
	input [7:0] cmd;
	input [31:0] data;
	begin
		kms_wr8(4'h3, cmd);
		kms_wr8(4'h4, data[31:24]);
		kms_wr8(4'h5, data[23:16]);
		kms_wr8(4'h6, data[15:8]);
		kms_wr8(4'h7, data[7:0]);
	end
endtask

task ptr_wr32;
	input [3:0] a;
	input [31:0] v;
	begin
		@(posedge clk);
		sel_ptr <= 1; addr <= a; we <= 1; be <= 2'b11; wdata <= v[31:16];
		@(posedge clk);
		addr <= a + 4'd2; wdata <= v[15:0];
		@(posedge clk);
		sel_ptr <= 0; we <= 0;
	end
endtask

task csr_cmd;
	input [7:0] v;
	begin
		@(posedge clk);
		sel_csr <= 1; addr <= 4'h0; we <= 1; be <= 2'b11; wdata <= {8'h00, v};
		@(posedge clk);
		addr <= 4'h2; wdata <= 16'h0000;
		@(posedge clk);
		sel_csr <= 0; we <= 0;
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

reg [7:0] v;

initial begin
	repeat (10) @(posedge clk);
	reset = 0;
	repeat (10) @(posedge clk);

	// TX status polls as idle
	kms_rd8(4'h2, v);
	check(v == 8'h00, "KMS TX status reads idle");

	// program the channel: 256 byte buffer
	ptr_wr32(4'h0, 32'h04002000);
	ptr_wr32(4'h4, 32'h04002100);
	csr_cmd(8'h11);              // RESET | SETENABLE

	// sound out enable through the command/data pair
	kms_wr8(4'h0, 8'h80);        // SNDOUT_DMA_ENABLE
	kms_cmd(8'h0F, 32'h0);       // KMSCMD_SND_OUT | SIO_ENABLE

	// buffer consumption plus the pace delay (64 us) plus polling
	repeat (3000) @(posedge clk);

	check(int_snd_out_dma, "channel complete raised INT_SND_OUT_DMA");
	@(posedge clk);
	csr_cmd(8'h08);              // DMA_CLRCOMPLETE
	repeat (2) @(posedge clk);
	check(!int_snd_out_dma, "CLRCOMPLETE releases the channel interrupt");

	// with no more buffers the engine underruns
	repeat (300) @(posedge clk);
	kms_rd8(4'h0, v);
	check(v[5] && v[6], "underrun and request status bits set");
	check(int_snd_ovrun, "INT_SOUND_OVRUN raised");

	// sound out disable clears the underrun state
	kms_cmd(8'h07, 32'h0);       // KMSCMD_SND_OUT without SIO_ENABLE
	repeat (2) @(posedge clk);
	kms_rd8(4'h0, v);
	check(!v[5] && !v[6], "disable clears underrun and request");
	check(!int_snd_ovrun, "INT_SOUND_OVRUN released");

	if (errors == 0) $display("ALL PASS");
	else             $display("%0d FAILURES", errors);
	$finish;
end

endmodule
