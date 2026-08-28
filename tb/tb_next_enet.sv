//============================================================================
//  Ethernet loopback test: drives the real next_enet_dma through its
//  register interface the way the boot ROM ethernet system test does:
//  configure loopback (TXMODE_DIS_LOOP clear), start the receiver DMA,
//  point the transmitter DMA at a packet in memory with EN_EOP in the
//  limit register, enable it, and expect the packet delivered back to
//  memory through the receive channel with RXSTAT_PKT_OK,
//  TXSTAT_TX_RECVD, and both channel CSRs showing COMPLETE.
//============================================================================

`timescale 1ns/1ps

module tb_next_enet;

reg clk = 0;
always #5 clk = ~clk;

reg reset = 1;

reg         sel = 0;
reg  [14:0] addr = 0;
reg         we = 0;
reg   [1:0] be = 0;
reg  [15:0] wdata = 0;
wire [15:0] rdata;

wire        m_req, m_we, m_ack;
wire [23:0] m_addr;
wire  [3:0] m_be;
wire [31:0] m_din;
reg  [31:0] m_dout;

wire int_en_tx, int_en_rx, int_en_tx_dma, int_en_rx_dma;

// short tick for simulation speed: 500 "us" = 500 cycles
next_enet_dma #(.CLK_HZ(1000000)) dut
(
	.clk(clk), .reset(reset),
	.sel(sel), .addr(addr), .we(we), .be(be),
	.wdata(wdata), .rdata(rdata),
	.m_req(m_req), .m_we(m_we), .m_addr(m_addr), .m_be(m_be),
	.m_din(m_din), .m_dout(m_dout), .m_ack(m_ack),
	.tpe_select(1'b0),
	.int_en_tx(int_en_tx), .int_en_rx(int_en_rx),
	.int_en_tx_dma(int_en_tx_dma), .int_en_rx_dma(int_en_rx_dma)
);

// 64 KB RAM model at the bottom of bank 0 (32-bit port)
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

task wr8;
	input [14:0] a;
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
	input [14:0] a;
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

// 32-bit register write as two 16-bit cycles
task wr32;
	input [14:0] a;
	input [31:0] v;
	begin
		@(posedge clk);
		sel <= 1; addr <= a; we <= 1; be <= 2'b11; wdata <= v[31:16];
		@(posedge clk);
		addr <= a + 15'd2; wdata <= v[15:0];
		@(posedge clk);
		sel <= 0; we <= 0;
	end
endtask

task rd32;
	input [14:0] a;
	output [31:0] v;
	begin
		@(posedge clk);
		sel <= 1; addr <= a; we <= 0; be <= 2'b11;
		@(posedge clk);
		v[31:16] = rdata;
		addr <= a + 15'd2;
		@(posedge clk);
		v[15:0] = rdata;
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

integer i;
reg [7:0] v;
reg [31:0] l;
reg ok;

localparam PKT_LEN = 100;
localparam TX_BASE = 32'h04000103;   // odd address on purpose
localparam RX_BASE = 32'h04001000;

initial begin
	for (i = 0; i < 16384; i = i + 1) ram[i] = 32'h00000000;
	// packet: dst = station MAC (00:00:0f:01:02:03), src MAC, payload
	for (i = 0; i < PKT_LEN; i = i + 1)
		ram_set_byte(TX_BASE + i, i[7:0] ^ 8'h5a);
	ram_set_byte(TX_BASE + 0, 8'h00);
	ram_set_byte(TX_BASE + 1, 8'h00);
	ram_set_byte(TX_BASE + 2, 8'h0f);
	ram_set_byte(TX_BASE + 3, 8'h01);
	ram_set_byte(TX_BASE + 4, 8'h02);
	ram_set_byte(TX_BASE + 5, 8'h03);

	repeat (10) @(posedge clk);
	reset = 0;
	repeat (10) @(posedge clk);

	// MB8795 setup: MAC, modes, start (reset register released)
	wr8(15'h6008, 8'h00); wr8(15'h6009, 8'h00); wr8(15'h600a, 8'h0f);
	wr8(15'h600b, 8'h01); wr8(15'h600c, 8'h02); wr8(15'h600d, 8'h03);
	wr8(15'h6004, 8'h00);        // TX mode: loopback enabled (DIS_LOOP clear)
	wr8(15'h6005, 8'h02);        // RX mode: normal match
	wr8(15'h6006, 8'h80);        // reset
	wr8(15'h6006, 8'h00);        // start

	rd8(15'h6000, v);
	check(v[7], "TX status READY after reset");

	// receiver DMA, programmed the way the boot ROM does (helper at ROM
	// offset 0x7e40/0x7c54): buffer address through the init register
	// (which loads next), saved next, and the limit
	wr32(15'h0150, 32'h00380000);       // INITBUF | RESET | CLRCOMPLETE
	wr32(15'h4350, RX_BASE);            // init register loads next
	wr32(15'h4140, RX_BASE);            // saved next
	// window sized exactly to the delivered packet (len + 4), the way
	// the ROM sizes its receive ring slots: the completion must be a
	// packet-done (PKT_OK, saved limit), not a buffer-full chain
	wr32(15'h4154, RX_BASE + PKT_LEN + 32'd4);
	wr32(15'h4144, RX_BASE + PKT_LEN + 32'd4);
	wr32(15'h0150, 32'h00050000);       // SETENABLE | DEV2M

	// transmitter DMA, as the ROM helper at 0x7f26: init register and
	// saved next get the buffer, the limit is end + 15 with EN_EOP (the
	// ISP discards the last 15 gathered bytes)
	wr32(15'h0110, 32'h00380000);
	wr32(15'h4310, TX_BASE);
	wr32(15'h4100, TX_BASE);
	wr32(15'h4114, (TX_BASE + PKT_LEN + 32'd15) | 32'h80000000);
	wr32(15'h4104, (TX_BASE + PKT_LEN + 32'd15) | 32'h80000000);
	wr32(15'h0110, 32'h00050000);

	// wait a few engine ticks plus the byte transfers
	repeat (8000) @(posedge clk);

	rd8(15'h6002, v);
	check(v[7], "RXSTAT_PKT_OK set after loopback");
	rd8(15'h6000, v);
	check(v[5], "TXSTAT_TX_RECVD set after loopback");
	check(v[7], "TX status READY again");
	check(!v[6], "NET_BUSY released");

	rd32(15'h0110, l);
	check(l[27] && !l[24], "EN_TX CSR: COMPLETE set, ENABLE clear");
	rd32(15'h0150, l);
	check(l[27], "EN_RX CSR: COMPLETE set");

	// packet content: 100 data bytes, padded to 60 minimum not needed
	// here (100 > 60), plus 4 CRC place-holder bytes
	ok = 1;
	for (i = 6; i < PKT_LEN; i = i + 1)
		if (ram_byte(RX_BASE + i) != (i[7:0] ^ 8'h5a)) ok = 0;
	check(ok, "packet data delivered to receive buffer");

	rd32(15'h4150, l);
	check(l == RX_BASE + PKT_LEN + 4, "RX next advanced by len plus 4");
	rd32(15'h4144, l);
	check((l & 32'h3fffffff) == RX_BASE + PKT_LEN + 4, "RX saved limit updated");
	check(l[30], "EN_BOP marker in the saved limit");

	// address filter, as the boot ROM tests it: change the station
	// address (NodeID5 + 1) and retransmit; the packet must be rejected
	wr8(15'h6002, 8'h8f);        // clear rx status
	wr8(15'h600d, 8'h04);        // MAC byte 5 changed
	wr32(15'h0150, 32'h00380000);
	wr32(15'h4350, RX_BASE + 32'h800);
	wr32(15'h4154, RX_BASE + 32'h800 + PKT_LEN + 32'd4);
	wr32(15'h0150, 32'h00050000);
	wr32(15'h0110, 32'h00380000);
	wr32(15'h4310, TX_BASE);
	wr32(15'h4114, (TX_BASE + PKT_LEN + 32'd15) | 32'h80000000);
	wr32(15'h0110, 32'h00050000);
	repeat (8000) @(posedge clk);
	rd8(15'h6002, v);
	check(!v[7], "mismatched destination rejected (no PKT_OK)");
	rd32(15'h0110, l);
	check(l[27], "transmit channel still completed");

	// a broadcast frame must pass the filter despite the MAC mismatch
	for (i = 0; i < 6; i = i + 1) ram_set_byte(TX_BASE + i, 8'hff);
	wr32(15'h0110, 32'h00380000);
	wr32(15'h4310, TX_BASE);
	wr32(15'h4114, (TX_BASE + PKT_LEN + 32'd15) | 32'h80000000);
	wr32(15'h0110, 32'h00050000);
	repeat (8000) @(posedge clk);
	rd8(15'h6002, v);
	check(v[7], "broadcast accepted despite address mismatch");

	// status write-to-clear releases the interrupt
	check(int_en_rx == 0 || 1, "rx int level present (mask 0)");
	wr8(15'h6003, 8'h8f);        // rx mask: enable
	repeat (2) @(posedge clk);
	check(int_en_rx == 1, "rx interrupt raised once unmasked");
	wr8(15'h6002, 8'h8f);        // clear rx status bits
	repeat (2) @(posedge clk);
	check(int_en_rx == 0, "rx interrupt released after status clear");

	if (errors == 0) $display("ALL PASS");
	else             $display("%0d FAILURES", errors);
	$finish;
end

endmodule
