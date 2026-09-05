//============================================================================
//  Ethernet bridge test: the real next_enet_dma and next_enet_bridge
//  against a DDR3 mailbox model plus a scripted ARM daemon:
//
//  - the guest transmits a frame with loopback disabled
//    (TXMODE_DIS_LOOP set): it must land in the DDR3 TX slot with the
//    right length and byte order, and TX_WPTR must advance
//  - the "ARM" writes a frame addressed to the station MAC into the
//    RX slot and advances RX_WPTR: the frame must be delivered to
//    guest memory through the RX DMA channel with RXSTAT_PKT_OK, and
//    RX_RPTR must advance
//  - a frame for another address must be filtered out
//  - the guest MAC must be published to the mailbox MAC slot
//============================================================================

`timescale 1ns/1ps

module tb_next_bridge;

reg clk = 0;
always #5 clk = ~clk;

reg reset = 1;
reg bridge_enable = 0;
reg cable_connected = 1;

// register access to the enet module
reg         sel = 0;
reg  [14:0] addr = 0;
reg         we = 0;
reg   [1:0] be = 0;
reg  [15:0] wdata = 0;
wire [15:0] rdata;

// guest RAM master port of the enet module
wire        m_req, m_we, m_ack;
wire [29:0] m_addr;
wire  [3:0] m_be;
wire [31:0] m_din;
reg  [31:0] m_dout;
wire        m_addr_valid = (m_addr[29:24] == 6'd1);
wire        m_err = m_req && !m_addr_valid;

// bridge streaming
wire        btx_req, btx_rd, btx_ack, btx_done;
wire [10:0] btx_len, btx_addr;
wire  [7:0] btx_q;
wire        brx_start, brx_valid, brx_ready;
wire [10:0] brx_len;
wire  [7:0] brx_data;
wire [47:0] enet_mac;

// bridge DDR3 port
wire        eb_req, eb_we, eb_ack;
wire [28:0] eb_addr;
wire [63:0] eb_wdata;
wire [63:0] eb_rdata;

next_enet_dma #(.CLK_HZ(1000000)) enet
	(
		.clk(clk), .reset(reset),
		.sel(sel), .addr(addr), .we(we), .be(be),
		.wdata(wdata), .rdata(rdata),
		.m_req(m_req), .m_we(m_we), .m_addr(m_addr), .m_be(m_be),
		.m_din(m_din), .m_dout(m_dout), .m_ack(m_ack), .m_err(m_err),
	.tpe_select(1'b0), .cable_connected(cable_connected),
	.btx_req(btx_req), .btx_len(btx_len), .btx_addr(btx_addr),
	.btx_rd(btx_rd), .btx_q(btx_q), .btx_ack(btx_ack), .btx_done(btx_done),
	.brx_start(brx_start), .brx_len(brx_len), .brx_valid(brx_valid),
	.brx_data(brx_data), .brx_ready(brx_ready), .enet_mac(enet_mac),
	.int_en_tx(), .int_en_rx(), .int_en_tx_dma(), .int_en_rx_dma()
);

next_enet_bridge #(.CLK_HZ(1000000)) bridge
(
	.clk(clk), .reset(reset), .enable(bridge_enable),
	.btx_req(btx_req), .btx_len(btx_len), .btx_addr(btx_addr),
	.btx_rd(btx_rd), .btx_q(btx_q), .btx_ack(btx_ack), .btx_done(btx_done),
	.brx_start(brx_start), .brx_len(brx_len), .brx_valid(brx_valid),
	.brx_data(brx_data), .brx_ready(brx_ready),
	.guest_mac(enet_mac),
	.m_req(eb_req), .m_we(eb_we), .m_addr(eb_addr),
	.m_wdata(eb_wdata), .m_rdata(eb_rdata), .m_ack(eb_ack)
);

// guest RAM model (32-bit port)
reg [31:0] ram [0:16383];
reg        ack_r;
assign m_ack = ack_r;
always @(posedge clk) begin
	if (reset) ack_r <= 0;
	else if (!m_req || !m_addr_valid) ack_r <= 0;
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

// DDR3 mailbox model: 64 KB window at word base 0x03FE0000
reg [63:0] ddr [0:8191];
reg        eb_ack_r;
reg [63:0] eb_rdata_r;
assign eb_ack = eb_ack_r;
assign eb_rdata = eb_rdata_r;
wire [12:0] eb_idx = eb_addr[12:0];
always @(posedge clk) begin
	if (reset) eb_ack_r <= 0;
	else if (!eb_req) eb_ack_r <= 0;
	else if (!eb_ack_r) begin
		if (eb_we) ddr[eb_idx] <= eb_wdata;
		else eb_rdata_r <= ddr[eb_idx];
		eb_ack_r <= 1;
	end
end

function [7:0] ddr_byte;
	input [15:0] a;      // byte offset within the window
	reg [63:0] w;
	begin
		w = ddr[a[15:3]];
		ddr_byte = w[{a[2:0], 3'b000} +: 8];
	end
endfunction

task ddr_set_byte;
	input [15:0] a;
	input [7:0] v;
	begin
		ddr[a[15:3]][{a[2:0], 3'b000} +: 8] = v;
	end
endtask

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

integer errors = 0;

task check;
	input cond;
	input [511:0] name;
	begin
		if (cond) $display("PASS: %0s", name);
		else begin $display("FAIL: %0s", name); errors = errors + 1; end
	end
endtask

integer i;
reg [7:0] v;
reg ok;
reg [63:0] w64;

localparam PKT_LEN = 100;
localparam TX_BASE = 32'h04000100;
localparam RX_BASE = 32'h04001000;

// window byte offsets
localparam MB_MAGIC  = 16'h0000;
localparam MB_TXWPTR = 16'h0008;
localparam MB_RXWPTR = 16'h0010;
localparam MB_RXRPTR = 16'h0018;
localparam MB_MAC    = 16'h0020;
localparam MB_TXSLOT = 16'h0800;
localparam MB_RXSLOT = 16'h2800;

initial begin
	for (i = 0; i < 16384; i = i + 1) ram[i] = 32'h00000000;
	for (i = 0; i < 8192; i = i + 1) ddr[i] = 64'h0;
	// DDR survives a core reset.  Start with a valid-looking stale
	// mailbox to prove Network=Off neither polls nor consumes it.
	ddr[MB_MAGIC/8]  = 64'h4E58544554483031;
	ddr[MB_TXWPTR/8] = 64'h1122334455667788;
	ddr[MB_RXWPTR/8] = 64'h0102030405060708;
	ddr[MB_RXRPTR/8] = 64'h8877665544332211;

	// transmit frame: some destination, payload pattern
	for (i = 0; i < PKT_LEN; i = i + 1)
		ram_set_byte(TX_BASE + i, i[7:0] ^ 8'hc3);

	repeat (10) @(posedge clk);
	reset = 0;
	repeat (30) @(posedge clk);

	check(!eb_req, "Network Off mailbox is idle");
	check(ddr[MB_MAGIC/8] == 0 && ddr[MB_TXWPTR/8] == 0 &&
	      ddr[MB_RXWPTR/8] == 0 && ddr[MB_RXRPTR/8] == 0,
	      "Network Off invalidates stale ring");

	bridge_enable = 1;
	repeat (80) @(posedge clk);
	check(ddr[MB_MAGIC/8] == 64'h4E58544554483031, "enable publishes mailbox magic");
	check(ddr[MB_TXWPTR/8] == 0 && ddr[MB_RXWPTR/8] == 0 &&
	      ddr[MB_RXRPTR/8] == 0, "enable clears stale pointers");

	// station MAC and modes; loopback DISABLED (DIS_LOOP set)
	wr8(15'h6008, 8'h00); wr8(15'h6009, 8'h00); wr8(15'h600a, 8'h0f);
	wr8(15'h600b, 8'h01); wr8(15'h600c, 8'h02); wr8(15'h600d, 8'h03);
	wr8(15'h6004, 8'h02);        // TXMODE_DIS_LOOP: real wire
	wr8(15'h6005, 8'h02);        // RX normal match
	wr8(15'h6006, 8'h80);
	wr8(15'h6006, 8'h00);        // start

	repeat (40) @(posedge clk);
	w64 = ddr[MB_MAC/8];
	check(w64[63] && w64[47:0] == 48'h00000f010203, "guest MAC published");

	// transmit through the EN_TX channel (ROM programming style)
	wr32(15'h0110, 32'h00380000);
	wr32(15'h4310, TX_BASE);
	wr32(15'h4114, (TX_BASE + PKT_LEN + 32'd15) | 32'h80000000);
	wr32(15'h0110, 32'h00050000);

	repeat (8000) @(posedge clk);

	check(ddr[MB_TXWPTR/8] == 64'd1, "TX_WPTR advanced");
	check(ddr[MB_TXSLOT/8] == {53'd0, 11'd100}, "TX slot header length 100");
	ok = 1;
	for (i = 0; i < PKT_LEN; i = i + 1)
		if (ddr_byte(MB_TXSLOT + 8 + i) != (i[7:0] ^ 8'hc3)) ok = 0;
	check(ok, "TX frame bytes in the slot");
	// receive: arm the RX DMA, then the ARM model delivers a frame
	wr32(15'h0150, 32'h00380000);
	wr32(15'h4350, RX_BASE);
	wr32(15'h4140, RX_BASE);
	wr32(15'h4154, RX_BASE + 32'd104);
	wr32(15'h4144, RX_BASE + 32'd104);
	wr32(15'h0150, 32'h00050000);

	// frame for the station MAC
	ddr_set_byte(MB_RXSLOT + 8 + 0, 8'h00);
	ddr_set_byte(MB_RXSLOT + 8 + 1, 8'h00);
	ddr_set_byte(MB_RXSLOT + 8 + 2, 8'h0f);
	ddr_set_byte(MB_RXSLOT + 8 + 3, 8'h01);
	ddr_set_byte(MB_RXSLOT + 8 + 4, 8'h02);
	ddr_set_byte(MB_RXSLOT + 8 + 5, 8'h03);
	for (i = 6; i < PKT_LEN; i = i + 1)
		ddr_set_byte(MB_RXSLOT + 8 + i, i[7:0] ^ 8'h77);
	ddr[MB_RXSLOT/8] = {53'd0, 11'd100};
	ddr[MB_RXWPTR/8] = 64'd1;

	repeat (12000) @(posedge clk);

	check(ddr[MB_RXRPTR/8] == 64'd1, "RX_RPTR advanced");
	rd8(15'h6002, v);
	check(v[7], "RXSTAT_PKT_OK after bridge delivery");
	ok = 1;
	for (i = 6; i < PKT_LEN; i = i + 1)
		if (ram_byte(RX_BASE + i) != (i[7:0] ^ 8'h77)) ok = 0;
	check(ok, "frame delivered to guest memory");

	// a frame for a different station must be filtered
	wr8(15'h6002, 8'h8f);        // clear rx status
	wr32(15'h0150, 32'h00380000);
	wr32(15'h4350, RX_BASE + 32'h800);
	wr32(15'h4154, RX_BASE + 32'h800 + 32'd104);
	wr32(15'h0150, 32'h00050000);

	ddr_set_byte(MB_RXSLOT + 16'h800 + 8 + 0, 8'haa);
	for (i = 1; i < PKT_LEN; i = i + 1)
		ddr_set_byte(MB_RXSLOT + 16'h800 + 8 + i, i[7:0]);
	// note: dst aa:...  is multicast bit clear? 0xaa bit0=0 unicast, wrong addr
	ddr[(MB_RXSLOT + 16'h800)/8] = {53'd0, 11'd100};
	ddr[MB_RXWPTR/8] = 64'd2;

	repeat (12000) @(posedge clk);

	check(ddr[MB_RXRPTR/8] == 64'd2, "RX_RPTR advanced past filtered frame");
	rd8(15'h6002, v);
	check(!v[7], "mismatched frame filtered (no PKT_OK)");

	// with loopback enabled the machine is off the wire: a bridge frame
	// must be consumed from the ring but never delivered (the boot ROM's
	// self-test phases depend on this on a live LAN)
	wr8(15'h6002, 8'h8f);
	wr8(15'h6004, 8'h00);        // loopback on (DIS_LOOP clear)
	wr32(15'h0150, 32'h00380000);
	wr32(15'h4350, RX_BASE + 32'h1000);
	wr32(15'h4154, RX_BASE + 32'h1000 + 32'd104);
	wr32(15'h0150, 32'h00050000);
	ddr_set_byte(MB_RXSLOT + 16'h1000 + 8 + 0, 8'hff);
	for (i = 1; i < 6; i = i + 1) ddr_set_byte(MB_RXSLOT + 16'h1000 + 8 + i, 8'hff);
	for (i = 6; i < PKT_LEN; i = i + 1) ddr_set_byte(MB_RXSLOT + 16'h1000 + 8 + i, i[7:0]);
	ddr[(MB_RXSLOT + 16'h1000)/8] = {53'd0, 11'd100};
	ddr[MB_RXWPTR/8] = 64'd3;

	repeat (12000) @(posedge clk);

	check(ddr[MB_RXRPTR/8] == 64'd3, "ring consumed while in loopback");
	rd8(15'h6002, v);
	check(!v[7], "no delivery while in loopback (wire disconnected)");

	// Top-level "Ethernet cable = Disconnected" uses the same effective
	// connected signal as Network=Off: the mailbox is disabled, BMAP reports
	// no link, and external-wire transmits complete with 16 collisions.
	cable_connected = 0;
	bridge_enable = 0;
	repeat (80) @(posedge clk);
	check(ddr[MB_MAGIC/8] == 0, "cable disconnected invalidates mailbox");

	wr8(15'h6000, 8'h0f);        // clear sticky TX error bits
	wr8(15'h6004, 8'h02);        // TXMODE_DIS_LOOP: external wire
	wr32(15'h0110, 32'h00380000);
	wr32(15'h4310, TX_BASE);
	wr32(15'h4114, (TX_BASE + PKT_LEN + 32'd15) | 32'h80000000);
	wr32(15'h0110, 32'h00050000);
	repeat (8000) @(posedge clk);
	rd8(15'h6000, v);
	check(v[1] && !v[5] && !btx_req,
	      "disconnected cable reports 16 collisions and no bridge transmit");
	check(ddr[MB_TXWPTR/8] == 0,
	      "disconnected cable publishes no TX frame");

	cable_connected = 1;
	bridge_enable = 1;
	repeat (80) @(posedge clk);
	check(ddr[MB_MAGIC/8] == 64'h4E58544554483031 &&
	      ddr[MB_TXWPTR/8] == 0 && ddr[MB_RXWPTR/8] == 0 &&
	      ddr[MB_RXRPTR/8] == 0, "cable reconnect starts an empty ring");

	// Turning networking off invalidates the mailbox and then becomes
	// completely quiescent.  Re-enabling starts a fresh empty generation.
	bridge_enable = 0;
	repeat (80) @(posedge clk);
	check(!eb_req && ddr[MB_MAGIC/8] == 0,
	      "disable invalidates and quiesces");
	bridge_enable = 1;
	repeat (80) @(posedge clk);
	check(ddr[MB_MAGIC/8] == 64'h4E58544554483031 &&
	      ddr[MB_TXWPTR/8] == 0 && ddr[MB_RXWPTR/8] == 0 &&
	      ddr[MB_RXRPTR/8] == 0, "re-enable starts an empty ring");

	if (errors == 0) $display("ALL PASS");
	else             $display("%0d FAILURES", errors);
	$finish;
end

endmodule
