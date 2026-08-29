//============================================================================
//  Ethernet controller (Fujitsu MB8795) with its two DMA channels
//
//  MB8795 registers 0x02006000-0x0200600F, DMA channel registers for
//  Ethernet Transmit (CSR 0x02000110, pointers 0x02004100-0x0200411C,
//  init 0x02004310) and Ethernet Receive (CSR 0x02000150, pointers
//  0x02004140-0x0200415C, init 0x02004350).
//
//  Modeled on Previous src/ethernet.c and the ethernet parts of
//  src/dma.c.  Implemented: the register set, the CSR command protocol
//  (the 68040 command byte placement is handled by ORing the byte lanes
//  of the 32-bit write, exactly like DMA_CSR_Write in dma.c), the DMA
//  chaining bits (SUPDATE reloads next/limit from start/stop), and a
//  transfer engine that performs local loopback: when the transmitter
//  is started with TXMODE_DIS_LOOP clear, the packet is read from
//  memory through the EN_TX channel (next up to limit, EN_EOP flag in
//  the limit register ends the packet), padded to the 60 byte minimum,
//  a 4 byte place-holder CRC is appended, and it is delivered back
//  through the EN_RX channel with RXSTAT_PKT_OK and TXSTAT_TX_RECVD
//  raised.  This is the path the boot ROM ethernet system test
//  exercises.  The engine ticks every 500 microseconds, the enet_io
//  rate Previous uses for the hardware test.
//
//  Real network connectivity is a TODO (docs/PORTING.md).
//============================================================================

module next_enet_dma #(parameter CLK_HZ = 100000000)
(
	input         clk,
	input         reset,

	// register access
	input         sel,
	input  [14:0] addr,          // io_off[14:0]
	input         we,
	input   [1:0] be,
	input  [15:0] wdata,
	output [15:0] rdata,

	// RAM master port (32-bit, level req / level ack)
	output reg        m_req,
	output reg        m_we,
	output reg [23:0] m_addr,
	output reg  [3:0] m_be,
	output reg [31:0] m_din,
	input      [31:0] m_dout,
	input             m_ack,

	// BMAP twisted-pair select (disables the loopback path, see
	// enet_state() in ethernet.c)
	input         tpe_select,

	// frame bridge (next_enet_bridge): when the guest runs with the
	// loopback disabled, transmitted frames stream out here and
	// received frames stream in, taking the place of the wire
	output reg        btx_req,       // a transmitted frame is ready
	output reg [10:0] btx_len,
	input      [10:0] btx_addr,      // byte fetch within the frame
	input             btx_rd,
	output reg  [7:0] btx_q,
	output reg        btx_ack,
	input             btx_done,      // bridge finished with the frame

	input             brx_start,     // bridge starts delivering a frame
	input      [10:0] brx_len,
	input             brx_valid,     // one byte per cycle while high
	input       [7:0] brx_data,
	output            brx_ready,     // engine can accept the stream
	output     [47:0] enet_mac,      // station address (NodeID registers)

	// interrupt levels
	output        int_en_tx,     // MB8795 transmitter
	output        int_en_rx,     // MB8795 receiver
	output        int_en_tx_dma, // EN_TX channel complete
	output        int_en_rx_dma  // EN_RX channel complete
);

//----------------------------------------------------------------------------
// MB8795 registers
//----------------------------------------------------------------------------

localparam TXSTAT_READY    = 8'h80, TXSTAT_NET_BUSY = 8'h40,
           TXSTAT_TX_RECVD = 8'h20, TXSTAT_UNDERFLOW = 8'h08;
localparam RXSTAT_PKT_OK   = 8'h80, RXSTAT_SHORT_PKT = 8'h08,
           RXSTAT_OVERFLOW = 8'h01;
localparam TXMODE_DIS_LOOP = 8'h02;
localparam RXMODE_ENA_SHORT= 8'h08;

reg [7:0] tx_status, tx_mask, rx_status, rx_mask;
reg [7:0] tx_mode, rx_mode;
reg       en_stopped;
reg [7:0] mac0, mac1, mac2, mac3, mac4, mac5;

assign enet_mac = {mac0, mac1, mac2, mac3, mac4, mac5};

assign int_en_tx = |(tx_status & tx_mask & 8'h2F);
assign int_en_rx = |(rx_status & rx_mask & 8'h8F);

//----------------------------------------------------------------------------
// DMA channel registers (EN_TX and EN_RX)
//----------------------------------------------------------------------------

localparam CSR_ENABLE = 0, CSR_SUPDATE = 1, CSR_COMPLETE = 3, CSR_BUSEXC = 4;

reg  [7:0] tx_csr, rx_csr;
reg [31:0] tx_next, tx_limit, tx_start, tx_stop;
reg [31:0] tx_snext, tx_slimit, tx_sstart, tx_sstop;
reg [31:0] rx_next, rx_limit, rx_start, rx_stop;
reg [31:0] rx_snext, rx_slimit, rx_sstart, rx_sstop;

assign int_en_tx_dma = tx_csr[CSR_COMPLETE];
assign int_en_rx_dma = rx_csr[CSR_COMPLETE];

// EN_EOP 0x80000000, EN_BOP 0x40000000
wire [31:0] tx_limit_addr = tx_limit & 32'h3FFFFFFF;

//----------------------------------------------------------------------------
// register read mux
//----------------------------------------------------------------------------

wire is_mb    = (addr[14:4] == 11'h600);       // 0x6000-0x600F
wire is_txcsr = (addr[14:2] == 13'h044);       // 0x0110-0x0113
wire is_rxcsr = (addr[14:2] == 13'h054);       // 0x0150-0x0153
wire is_txptr = (addr[14:5] == 10'h208);       // 0x4100-0x411F
wire is_rxptr = (addr[14:5] == 10'h20A);       // 0x4140-0x415F
wire is_txini = (addr[14:2] == 13'h10C4);      // 0x4310-0x4313
wire is_rxini = (addr[14:2] == 13'h10D4);      // 0x4350-0x4353

// MB8795 byte read
`define MB_READ(a) ( \
	((a) == 4'h0) ? tx_status : \
	((a) == 4'h1) ? (tx_mask & 8'hAF) : \
	((a) == 4'h2) ? rx_status : \
	((a) == 4'h3) ? (rx_mask & 8'h9F) : \
	((a) == 4'h4) ? tx_mode : \
	((a) == 4'h5) ? rx_mode : \
	((a) == 4'h8) ? mac0 : \
	((a) == 4'h9) ? mac1 : \
	((a) == 4'hA) ? mac2 : \
	((a) == 4'hB) ? mac3 : \
	((a) == 4'hC) ? mac4 : \
	((a) == 4'hD) ? mac5 : 8'h00 )

// pointer register long read (word offset addr[4:2] within the group)
wire [31:0] tx_ptr_q =
	(addr[4:2] == 3'd0) ? tx_snext :
	(addr[4:2] == 3'd1) ? tx_slimit :
	(addr[4:2] == 3'd2) ? tx_sstart :
	(addr[4:2] == 3'd3) ? tx_sstop :
	(addr[4:2] == 3'd4) ? tx_next :
	(addr[4:2] == 3'd5) ? tx_limit :
	(addr[4:2] == 3'd6) ? tx_start : tx_stop;

wire [31:0] rx_ptr_q =
	(addr[4:2] == 3'd0) ? rx_snext :
	(addr[4:2] == 3'd1) ? rx_slimit :
	(addr[4:2] == 3'd2) ? rx_sstart :
	(addr[4:2] == 3'd3) ? rx_sstop :
	(addr[4:2] == 3'd4) ? rx_next :
	(addr[4:2] == 3'd5) ? rx_limit :
	(addr[4:2] == 3'd6) ? rx_start : rx_stop;

wire [31:0] long_q = is_txcsr ? {tx_csr, 24'h000000} :
                     is_rxcsr ? {rx_csr, 24'h000000} :
                     is_txptr ? tx_ptr_q :
                     is_rxptr ? rx_ptr_q :
                     is_txini ? tx_next :
                     is_rxini ? rx_next : 32'h00000000;

assign rdata = is_mb ? {`MB_READ({addr[3:1], 1'b0}), `MB_READ({addr[3:1], 1'b1})} :
               (addr[1] ? long_q[15:0] : long_q[31:16]);

//----------------------------------------------------------------------------
// packet buffer (single, loopback only)
//----------------------------------------------------------------------------

reg  [7:0] pkt [0:2047];

// single-port discipline so the buffer maps to block RAM: exactly one
// write site and one registered read site, addresses muxed by state
reg        pw_we;
reg [10:0] pw_addr;
reg  [7:0] pw_data;
reg  [7:0] pkt_q;
reg        btx_pending;

reg [11:0] tx_len;               // bytes gathered from memory
reg [11:0] rx_len;               // bytes pending delivery to memory
reg [11:0] rx_pos;

// destination address of the packet being gathered, captured on the fly
// so the buffer array keeps a single read port
reg  [7:0] dst0, dst1, dst2, dst3, dst4, dst5;

// enet_packet_for_me() in ethernet.c (non-turbo)
wire match_me = (dst0 == mac0) && (dst1 == mac1) && (dst2 == mac2) &&
                (dst3 == mac3) && (dst4 == mac4) &&
                ((dst5 == mac5) || rx_mode[4]);          // RXMODE_ADDR_SIZE
wire match_bc = &{dst0, dst1, dst2, dst3, dst4, dst5};
wire match_mc = dst0[0];
wire match_lmc = dst0[0] && ((dst0 & 8'hFE) == mac0) &&
                 (dst1 == mac1) && (dst2 == mac2);
wire pkt_for_me =
	(rx_mode[1:0] == 2'd0) ? 1'b0 :                      // RX_NOPACKETS
	(rx_mode[1:0] == 2'd1) ? (match_bc | match_me | match_lmc) :
	(rx_mode[1:0] == 2'd2) ? (match_bc | match_me | match_mc) :
	1'b1;                                                // RX_PROMISCUOUS

//----------------------------------------------------------------------------
// engine
//----------------------------------------------------------------------------

localparam US_TICK = CLK_HZ / 1000000 * 500;   // 500 us
reg [$clog2(US_TICK)-1:0] tickcnt;
wire tick = (tickcnt == US_TICK-1);

localparam E_IDLE   = 4'd0,
           E_TX_RD  = 4'd1,     // issue memory read for one byte
           E_TX_ACK = 4'd2,
           E_TX_END = 4'd3,
           E_RX_WR  = 4'd4,
           E_RX_ACK = 4'd5,
           E_RX_END = 4'd6,
           E_BTX    = 4'd7,     // frame parked for the bridge to fetch
           E_BRX    = 4'd8,
           E_RX_PRE = 4'd9;     // buffer read data settles     // frame streaming in from the bridge

reg [3:0] est;
reg [10:0] brx_pos;

assign brx_ready = (est == E_IDLE) && (rx_len == 0) && !en_stopped;

wire loopback = !tpe_select && !(tx_mode & TXMODE_DIS_LOOP);

// helper: byte lane select for a byte address
function [3:0] be_of;
	input [1:0] b;
	be_of = 4'b1000 >> b;
endfunction

integer i;

// register writes and engine share the always block so the DMA registers
// have one driver

wire mb_we  = sel & we & is_mb;
wire [7:0] mb_wv_e = wdata[15:8];
wire [7:0] mb_wv_o = wdata[7:0];

task automatic mb_write;
	input [3:0] a;
	input [7:0] v;
	begin
		case (a)
			4'h0: begin
				tx_status <= tx_status & ~(v & 8'h0F);
			end
			4'h1: tx_mask <= v;
			4'h2: rx_status <= rx_status & ~(v & 8'h8F);
			4'h3: rx_mask <= v;
			4'h4: tx_mode <= v;
			4'h5: rx_mode <= v;
			4'h6: begin
				// enet_reset(): bit 7 stops and readies, clear starts
				if (v[7]) begin
					en_stopped <= 1;
					tx_status <= TXSTAT_READY;
					tx_len <= 0;
					rx_len <= 0;
				end
				else en_stopped <= 0;
			end
			4'h8: mac0 <= v;
			4'h9: mac1 <= v;
			4'hA: mac2 <= v;
			4'hB: mac3 <= v;
			4'hC: mac4 <= v;
			4'hD: mac5 <= v;
			default: ;
		endcase
	end
endtask

// CSR command, DMA_CSR_Write() in dma.c: command byte is the OR of all
// byte lanes of the long
task automatic csr_cmd_tx;
	input [7:0] cmd;
	begin
		if (cmd[4]) tx_csr <= tx_csr & ~8'b00001011;         // RESET
		if (cmd[1]) tx_csr[CSR_SUPDATE] <= 1;                // SETSUPDATE
		if (cmd[0]) tx_csr[CSR_ENABLE] <= 1;                 // SETENABLE
		if (cmd[3]) tx_csr[CSR_COMPLETE] <= 0;               // CLRCOMPLETE
	end
endtask

task automatic csr_cmd_rx;
	input [7:0] cmd;
	begin
		if (cmd[4]) rx_csr <= rx_csr & ~8'b00001011;
		if (cmd[1]) rx_csr[CSR_SUPDATE] <= 1;
		if (cmd[0]) rx_csr[CSR_ENABLE] <= 1;
		if (cmd[3]) rx_csr[CSR_COMPLETE] <= 0;
	end
endtask

// dma_enet_interrupt(): complete, chain reload or disable
task automatic dma_done_tx;
	begin
		tx_csr[CSR_COMPLETE] <= 1;
		if (tx_csr[CSR_SUPDATE]) begin
			tx_next <= tx_start;
			tx_limit <= tx_stop;
			tx_csr[CSR_SUPDATE] <= 0;
		end
		else tx_csr[CSR_ENABLE] <= 0;
	end
endtask

task automatic dma_done_rx;
	begin
		rx_csr[CSR_COMPLETE] <= 1;
		if (rx_csr[CSR_SUPDATE]) begin
			rx_next <= rx_start;
			rx_limit <= rx_stop;
			rx_csr[CSR_SUPDATE] <= 0;
		end
		else rx_csr[CSR_ENABLE] <= 0;
	end
endtask

wire [7:0] csr_or = (be[1] ? wdata[15:8] : 8'h00) | (be[0] ? wdata[7:0] : 8'h00);

wire [10:0] pkt_raddr = (est == E_BTX) ? btx_addr : rx_pos[10:0];

always @(posedge clk) begin
	if (pw_we) pkt[pw_addr] <= pw_data;
	pkt_q <= pkt[pkt_raddr];
end

always @(posedge clk) begin
	pw_we <= 0;
	if (reset) begin
		tx_status <= TXSTAT_READY;
		tx_mask <= 0; rx_status <= 0; rx_mask <= 0;
		tx_mode <= 0; rx_mode <= 0;
		en_stopped <= 1;
		mac0 <= 0; mac1 <= 0; mac2 <= 0; mac3 <= 0; mac4 <= 0; mac5 <= 0;
		dst0 <= 0; dst1 <= 0; dst2 <= 0; dst3 <= 0; dst4 <= 0; dst5 <= 0;
		tx_csr <= 0; rx_csr <= 0;
		tx_next <= 0; tx_limit <= 0; tx_start <= 0; tx_stop <= 0;
		tx_snext <= 0; tx_slimit <= 0; tx_sstart <= 0; tx_sstop <= 0;
		rx_next <= 0; rx_limit <= 0; rx_start <= 0; rx_stop <= 0;
		rx_snext <= 0; rx_slimit <= 0; rx_sstart <= 0; rx_sstop <= 0;
		tx_len <= 0; rx_len <= 0; rx_pos <= 0;
		brx_pos <= 0;
		btx_req <= 0; btx_len <= 0; btx_q <= 0; btx_ack <= 0;
		btx_pending <= 0;
		est <= E_IDLE;
		m_req <= 0;
		tickcnt <= 0;
	end
	else begin
		tickcnt <= tick ? 1'd0 : tickcnt + 1'd1;

		//------------------------------------------------------------
		// CPU register writes
		//------------------------------------------------------------
		if (mb_we) begin
			if (be[1]) mb_write({addr[3:1], 1'b0}, mb_wv_e);
			if (be[0]) mb_write({addr[3:1], 1'b1}, mb_wv_o);
		end

		if (sel & we & is_txcsr & (csr_or != 0)) csr_cmd_tx(csr_or);
		if (sel & we & is_rxcsr & (csr_or != 0)) csr_cmd_rx(csr_or);

		if (sel & we & is_txptr) begin
			case (addr[4:2])
				3'd0: begin if (!addr[1]) begin if (be[1]) tx_snext[31:24] <= wdata[15:8]; if (be[0]) tx_snext[23:16] <= wdata[7:0]; end else begin if (be[1]) tx_snext[15:8] <= wdata[15:8]; if (be[0]) tx_snext[7:0] <= wdata[7:0]; end end
				3'd1: begin if (!addr[1]) begin if (be[1]) tx_slimit[31:24] <= wdata[15:8]; if (be[0]) tx_slimit[23:16] <= wdata[7:0]; end else begin if (be[1]) tx_slimit[15:8] <= wdata[15:8]; if (be[0]) tx_slimit[7:0] <= wdata[7:0]; end end
				3'd2: begin if (!addr[1]) begin if (be[1]) tx_sstart[31:24] <= wdata[15:8]; if (be[0]) tx_sstart[23:16] <= wdata[7:0]; end else begin if (be[1]) tx_sstart[15:8] <= wdata[15:8]; if (be[0]) tx_sstart[7:0] <= wdata[7:0]; end end
				3'd3: begin if (!addr[1]) begin if (be[1]) tx_sstop[31:24] <= wdata[15:8]; if (be[0]) tx_sstop[23:16] <= wdata[7:0]; end else begin if (be[1]) tx_sstop[15:8] <= wdata[15:8]; if (be[0]) tx_sstop[7:0] <= wdata[7:0]; end end
				3'd4: begin if (!addr[1]) begin if (be[1]) tx_next[31:24] <= wdata[15:8]; if (be[0]) tx_next[23:16] <= wdata[7:0]; end else begin if (be[1]) tx_next[15:8] <= wdata[15:8]; if (be[0]) tx_next[7:0] <= wdata[7:0]; end end
				3'd5: begin if (!addr[1]) begin if (be[1]) tx_limit[31:24] <= wdata[15:8]; if (be[0]) tx_limit[23:16] <= wdata[7:0]; end else begin if (be[1]) tx_limit[15:8] <= wdata[15:8]; if (be[0]) tx_limit[7:0] <= wdata[7:0]; end end
				3'd6: begin if (!addr[1]) begin if (be[1]) tx_start[31:24] <= wdata[15:8]; if (be[0]) tx_start[23:16] <= wdata[7:0]; end else begin if (be[1]) tx_start[15:8] <= wdata[15:8]; if (be[0]) tx_start[7:0] <= wdata[7:0]; end end
				3'd7: begin if (!addr[1]) begin if (be[1]) tx_stop[31:24] <= wdata[15:8]; if (be[0]) tx_stop[23:16] <= wdata[7:0]; end else begin if (be[1]) tx_stop[15:8] <= wdata[15:8]; if (be[0]) tx_stop[7:0] <= wdata[7:0]; end end
			endcase
		end
		if (sel & we & is_rxptr) begin
			case (addr[4:2])
				3'd0: begin if (!addr[1]) begin if (be[1]) rx_snext[31:24] <= wdata[15:8]; if (be[0]) rx_snext[23:16] <= wdata[7:0]; end else begin if (be[1]) rx_snext[15:8] <= wdata[15:8]; if (be[0]) rx_snext[7:0] <= wdata[7:0]; end end
				3'd1: begin if (!addr[1]) begin if (be[1]) rx_slimit[31:24] <= wdata[15:8]; if (be[0]) rx_slimit[23:16] <= wdata[7:0]; end else begin if (be[1]) rx_slimit[15:8] <= wdata[15:8]; if (be[0]) rx_slimit[7:0] <= wdata[7:0]; end end
				3'd2: begin if (!addr[1]) begin if (be[1]) rx_sstart[31:24] <= wdata[15:8]; if (be[0]) rx_sstart[23:16] <= wdata[7:0]; end else begin if (be[1]) rx_sstart[15:8] <= wdata[15:8]; if (be[0]) rx_sstart[7:0] <= wdata[7:0]; end end
				3'd3: begin if (!addr[1]) begin if (be[1]) rx_sstop[31:24] <= wdata[15:8]; if (be[0]) rx_sstop[23:16] <= wdata[7:0]; end else begin if (be[1]) rx_sstop[15:8] <= wdata[15:8]; if (be[0]) rx_sstop[7:0] <= wdata[7:0]; end end
				3'd4: begin if (!addr[1]) begin if (be[1]) rx_next[31:24] <= wdata[15:8]; if (be[0]) rx_next[23:16] <= wdata[7:0]; end else begin if (be[1]) rx_next[15:8] <= wdata[15:8]; if (be[0]) rx_next[7:0] <= wdata[7:0]; end end
				3'd5: begin if (!addr[1]) begin if (be[1]) rx_limit[31:24] <= wdata[15:8]; if (be[0]) rx_limit[23:16] <= wdata[7:0]; end else begin if (be[1]) rx_limit[15:8] <= wdata[15:8]; if (be[0]) rx_limit[7:0] <= wdata[7:0]; end end
				3'd6: begin if (!addr[1]) begin if (be[1]) rx_start[31:24] <= wdata[15:8]; if (be[0]) rx_start[23:16] <= wdata[7:0]; end else begin if (be[1]) rx_start[15:8] <= wdata[15:8]; if (be[0]) rx_start[7:0] <= wdata[7:0]; end end
				3'd7: begin if (!addr[1]) begin if (be[1]) rx_stop[31:24] <= wdata[15:8]; if (be[0]) rx_stop[23:16] <= wdata[7:0]; end else begin if (be[1]) rx_stop[15:8] <= wdata[15:8]; if (be[0]) rx_stop[7:0] <= wdata[7:0]; end end
			endcase
		end
		// DMA_Init_Write in dma.c: a write to the init register loads the
		// channel's next pointer
		if (sel & we & is_txini) begin
			if (!addr[1]) begin if (be[1]) tx_next[31:24] <= wdata[15:8]; if (be[0]) tx_next[23:16] <= wdata[7:0]; end
			else begin if (be[1]) tx_next[15:8] <= wdata[15:8]; if (be[0]) tx_next[7:0] <= wdata[7:0]; end
		end
		if (sel & we & is_rxini) begin
			if (!addr[1]) begin if (be[1]) rx_next[31:24] <= wdata[15:8]; if (be[0]) rx_next[23:16] <= wdata[7:0]; end
			else begin if (be[1]) rx_next[15:8] <= wdata[15:8]; if (be[0]) rx_next[7:0] <= wdata[7:0]; end
		end

		//------------------------------------------------------------
		// transfer engine
		//------------------------------------------------------------
		case (est)
		E_IDLE: if (brx_start && brx_ready) begin
			brx_pos <= 0;
			est <= E_BRX;
		end
		else if (tick && !en_stopped) begin
			if (rx_len != 0) begin
				// deliver the pending packet, enet_io receive path
				if (!rx_csr[CSR_ENABLE]) begin
					// receiver overflow, DMA disabled
					rx_status <= (rx_status | RXSTAT_OVERFLOW) & ~RXSTAT_PKT_OK;
					rx_len <= 0;
					tx_status <= tx_status & ~TXSTAT_NET_BUSY;
				end
				else begin
					rx_status <= rx_status & ~RXSTAT_PKT_OK;
					est <= E_RX_PRE;
				end
			end
			else if (tx_status[7] && tx_csr[CSR_ENABLE]) begin
				// transmit: gather bytes from memory (the frame goes to
				// the loopback path or to the bridge at E_TX_END)
				est <= E_TX_RD;
			end
		end

		E_TX_RD: begin
			if (tx_next >= tx_limit_addr) est <= E_TX_END;
			else begin
				m_req <= 1;
				m_we <= 0;
				m_be <= 4'hF;
				m_addr <= tx_next[25:2];
				est <= E_TX_ACK;
			end
		end

		E_TX_ACK: if (m_ack) begin : tx_ack
			reg [7:0] b;
			m_req <= 0;
			b = (tx_next[1:0] == 2'd0) ? m_dout[31:24] :
			    (tx_next[1:0] == 2'd1) ? m_dout[23:16] :
			    (tx_next[1:0] == 2'd2) ? m_dout[15:8] : m_dout[7:0];
			if (tx_len < 12'd2047) begin
				pw_we <= 1;
				pw_addr <= tx_len[10:0];
				pw_data <= b;
				tx_len <= tx_len + 1'd1;
			end
			case (tx_len)
				12'd0: dst0 <= b;
				12'd1: dst1 <= b;
				12'd2: dst2 <= b;
				12'd3: dst3 <= b;
				12'd4: dst4 <= b;
				12'd5: dst5 <= b;
				default: ;
			endcase
			tx_next <= tx_next + 1'd1;
			est <= E_TX_RD;
		end

		E_TX_END: begin : tx_end
			reg [11:0] eff_len;
			dma_done_tx;
			if (tx_limit[31]) begin
				// EN_EOP: packet complete.  The channel gathers 15 bytes
				// past the packet end (the host programs limit + 15, the
				// ISP discards them; enet_io does size -= 15).  Loop the
				// packet back with the short-packet pad and a 4 byte
				// place-holder CRC.
				eff_len = (tx_len > 12'd15) ? tx_len - 12'd15 : tx_len;
				tx_status <= tx_status & ~TXSTAT_TX_RECVD;
				if (loopback) begin
					if (pkt_for_me) begin
						// enet_receive(): the address filter accepts it
						tx_status <= (tx_status | TXSTAT_NET_BUSY) & ~TXSTAT_TX_RECVD;
						rx_len <= ((eff_len < 12'd60) ? 12'd60 : eff_len) + 12'd4;
						rx_pos <= 0;
					end
					tx_len <= 0;
					est <= E_IDLE;
				end
				else begin
					// hand the frame to the bridge (the wire side)
					btx_req <= 1;
					btx_len <= eff_len[10:0];
					est <= E_BTX;
				end
			end
			else est <= E_IDLE;
		end

		E_BTX: begin
			// serve the bridge's byte fetches out of the packet buffer;
			// the read data settles one cycle after the address applies
			btx_ack <= 0;
			if (btx_pending) begin
				btx_q <= pkt_q;
				btx_ack <= 1;
				btx_pending <= 0;
			end
			else if (btx_rd) btx_pending <= 1;
			if (btx_done) begin
				btx_req <= 0;
				btx_pending <= 0;
				tx_len <= 0;
				est <= E_IDLE;
			end
		end

		E_BRX: begin
			// frame streaming in from the bridge into the packet buffer
			if (brx_valid && brx_pos < 11'd2047) begin
				pw_we <= 1;
				pw_addr <= brx_pos;
				pw_data <= brx_data;
				case (brx_pos)
					11'd0: dst0 <= brx_data;
					11'd1: dst1 <= brx_data;
					11'd2: dst2 <= brx_data;
					11'd3: dst3 <= brx_data;
					11'd4: dst4 <= brx_data;
					11'd5: dst5 <= brx_data;
					default: ;
				endcase
				brx_pos <= brx_pos + 1'd1;
			end
			if (brx_pos >= brx_len) begin
				// enet_receive(): filter and queue for RX DMA delivery
				if (pkt_for_me) begin
					tx_status <= tx_status | TXSTAT_NET_BUSY;
					rx_len <= (({1'b0, brx_pos} < 12'd60) ? 12'd60 : {1'b0, brx_pos}) + 12'd4;
					rx_pos <= 0;
				end
				est <= E_IDLE;
			end
		end

		E_RX_WR: begin
			// packet done takes precedence: when the packet ends exactly
			// at the window limit (the ROM sizes its ring slots to the
			// frame length) this must be a completed packet, not a full
			// buffer (dma_enet_write_memory checks size == 0 first)
			if (rx_pos >= rx_len) est <= E_RX_END;
			else if (rx_next >= rx_limit) begin
				// buffer full before packet done: chain to the next tick
				dma_done_rx;
				est <= E_IDLE;
			end
			else begin
				m_req <= 1;
				m_we <= 1;
				m_be <= be_of(rx_next[1:0]);
				m_addr <= rx_next[25:2];
				m_din <= {4{pkt_q}};
				est <= E_RX_ACK;
			end
		end

		E_RX_ACK: if (m_ack) begin
			m_req <= 0;
			rx_next <= rx_next + 1'd1;
			rx_pos <= rx_pos + 1'd1;
			est <= E_RX_PRE;
		end

		E_RX_PRE: est <= E_RX_WR;   // pkt_q settles for the new rx_pos

		E_RX_END: begin
			// packet delivered completely; the EN_BOP marker lands in the
			// final address the ISR reads back (dma_enet_write_memory)
			rx_slimit <= rx_next | 32'h40000000;
			dma_done_rx;
			rx_status <= rx_status | RXSTAT_PKT_OK;
			tx_status <= (tx_status | TXSTAT_TX_RECVD) & ~TXSTAT_NET_BUSY;
			rx_len <= 0;
			rx_pos <= 0;
			est <= E_IDLE;
		end

		default: est <= E_IDLE;
		endcase
	end
end

endmodule
