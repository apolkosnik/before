//============================================================================
//  Ethernet frame bridge to the ARM daemon (the "wire" behind the
//  MB8795 when the guest disables loopback)
//
//  Modeled on the A2065 support in Minimig-AGA_MiSTer and Main_MiSTer
//  (rtl/A2065/a2065_ddr3_mailbox.v and support/minimig/minimig_a2065*):
//  a DDR3 shared-memory mailbox that an ARM daemon polls from Main's
//  poll loop.  Unlike the A2065 (whole LANCE emulated on the ARM), the
//  NeXT MB8795 and its DMA channels live in the fabric and pass the
//  boot ROM's hardware tests there, so only ethernet FRAMES cross:
//
//  DDR3 byte layout (ARM physical 0x1FF00000, the window Main maps
//  with shmem_map; 64-bit-word Avalon address 0x03FE0000):
//    +0x0000  MAGIC     "NXTETH01" (FPGA writes at reset release)
//    +0x0008  TX_WPTR   FPGA increments per transmitted frame
//    +0x0010  RX_WPTR   ARM increments per received frame
//    +0x0018  RX_RPTR   FPGA increments per consumed frame
//    +0x0020  GUEST_MAC bit63 = valid, bits[47:0] = station address
//                       (FPGA republishes on NodeID writes)
//    +0x0800  TX slots: 4 x 2048 bytes
//    +0x2800  RX slots: 4 x 2048 bytes
//    slot: u64 header, bits[10:0] = length; frame bytes from +8,
//    byte i of the frame at byte offset 8+i (little-endian words)
//
//  Both rings are 4 deep; the FPGA never blocks on the ARM (stale TX
//  frames are overwritten, as the A2065 command ring does), and the
//  ARM respects RX_RPTR so received frames are never overwritten
//  before delivery.
//============================================================================

module next_enet_bridge #(parameter CLK_HZ = 100000000)
(
	input             clk,
	input             reset,

	// frame streaming to/from next_enet_dma
	input             btx_req,
	input      [10:0] btx_len,
	output reg [10:0] btx_addr,
	output reg        btx_rd,
	input       [7:0] btx_q,
	input             btx_ack,
	output reg        btx_done,

	output reg        brx_start,
	output reg [10:0] brx_len,
	output reg        brx_valid,
	output reg  [7:0] brx_data,
	input             brx_ready,

	// station address to publish (from the MB8795 NodeID registers)
	input      [47:0] guest_mac,

	// DDRAM access (through the arbiter in the emu top)
	output reg        m_req,
	output reg        m_we,
	output reg [28:0] m_addr,       // 64-bit word address
	output reg [63:0] m_wdata,
	input      [63:0] m_rdata,
	input             m_ack
);

localparam [28:0] BASE      = 29'h03FE0000;   // byte 0x1FF00000
localparam [28:0] A_MAGIC   = BASE + 29'h000;
localparam [28:0] A_TXWPTR  = BASE + 29'h001;
localparam [28:0] A_RXWPTR  = BASE + 29'h002;
localparam [28:0] A_RXRPTR  = BASE + 29'h003;
localparam [28:0] A_MAC     = BASE + 29'h004;
localparam [28:0] A_TXSLOT  = BASE + 29'h100;  // byte +0x800
localparam [28:0] A_RXSLOT  = BASE + 29'h500;  // byte +0x2800
localparam [28:0] SLOT_STRIDE = 29'h100;       // 2048 bytes

localparam [63:0] MAGIC = 64'h4E58544554483031;  // "NXTETH01"

// poll the RX write pointer roughly every 200 microseconds
localparam POLL = CLK_HZ / 1000000 * 200;
reg [$clog2(POLL)-1:0] pollcnt;
wire poll_tick = (pollcnt == POLL-1);

localparam S_INIT_MAGIC = 5'd0,
           S_INIT_MAC   = 5'd1,
           S_IDLE       = 5'd2,
           // transmit: enet -> DDR3
           S_TX_FETCH   = 5'd3,
           S_TX_PACK    = 5'd4,
           S_TX_WRITE   = 5'd5,
           S_TX_HDR     = 5'd6,
           S_TX_WPTR    = 5'd7,
           // receive: DDR3 -> enet
           S_RX_POLL    = 5'd8,
           S_RX_HDR     = 5'd9,
           S_RX_READ    = 5'd10,
           S_RX_STREAM  = 5'd11,
           S_RX_RPTR    = 5'd12,
           S_MAC        = 5'd13,
           S_WAIT       = 5'd14;

reg [4:0] st, ret;

reg [63:0] tx_wptr, rx_wptr, rx_rptr;
reg [10:0] pos;                  // byte position within the frame
reg  [2:0] lane;
reg [63:0] pack;
reg [10:0] cur_len;
reg [47:0] mac_last;
reg        mac_dirty;

// one DDR3 op with return state
task automatic ddr_op;
	input        we;
	input [28:0] a;
	input [63:0] d;
	input  [4:0] nxt;
	begin
		m_req <= 1;
		m_we <= we;
		m_addr <= a;
		m_wdata <= d;
		ret <= nxt;
		st <= S_WAIT;
	end
endtask

wire [28:0] tx_slot_base = A_TXSLOT + SLOT_STRIDE * tx_wptr[1:0];
wire [28:0] rx_slot_base = A_RXSLOT + SLOT_STRIDE * rx_rptr[1:0];

always @(posedge clk) begin
	btx_rd <= 0;
	btx_done <= 0;
	brx_start <= 0;
	brx_valid <= 0;

	if (reset) begin
		st <= S_INIT_MAGIC;
		m_req <= 0;
		tx_wptr <= 0;
		rx_wptr <= 0;
		rx_rptr <= 0;
		pollcnt <= 0;
		mac_last <= 0;
		mac_dirty <= 0;
		btx_addr <= 0;
	end
	else begin
		pollcnt <= poll_tick ? 1'd0 : pollcnt + 1'd1;
		if (guest_mac != mac_last) mac_dirty <= 1;

		case (st)
		S_INIT_MAGIC: ddr_op(1, A_MAGIC, MAGIC, S_INIT_MAC);
		S_INIT_MAC: begin
			mac_last <= guest_mac;
			mac_dirty <= 0;
			ddr_op(1, A_MAC, {1'b1, 15'd0, guest_mac}, S_IDLE);
		end

		S_IDLE: begin
			if (btx_req) begin
				pos <= 0;
				lane <= 0;
				pack <= 0;
				cur_len <= btx_len;
				btx_addr <= 0;
				btx_rd <= 1;
				st <= S_TX_FETCH;
			end
			else if (poll_tick) begin
				ddr_op(0, A_RXWPTR, 64'd0, S_RX_POLL);
			end
			else if (mac_dirty) st <= S_INIT_MAC;
		end

		//------------------------------------------------------------
		// transmit
		//------------------------------------------------------------
		S_TX_FETCH: if (btx_ack) begin
			// place byte pos into little-endian lane pos[2:0]
			pack[{lane, 3'b000} +: 8] <= btx_q;
			if (pos + 11'd1 >= cur_len || lane == 3'd7) st <= S_TX_WRITE;
			else begin
				pos <= pos + 1'd1;
				lane <= lane + 1'd1;
				btx_addr <= pos + 1'd1;
				btx_rd <= 1;
			end
		end
		S_TX_WRITE: begin
			ddr_op(1, tx_slot_base + 29'd1 + {23'd0, pos[10:3]}, pack,
			       (pos + 11'd1 >= cur_len) ? S_TX_HDR : S_TX_PACK);
		end
		S_TX_PACK: begin
			pos <= pos + 1'd1;
			lane <= 0;
			pack <= 0;
			btx_addr <= pos + 1'd1;
			btx_rd <= 1;
			st <= S_TX_FETCH;
		end
		S_TX_HDR: ddr_op(1, tx_slot_base, {53'd0, cur_len}, S_TX_WPTR);
		S_TX_WPTR: begin
			tx_wptr <= tx_wptr + 1'd1;
			btx_done <= 1;
			ddr_op(1, A_TXWPTR, tx_wptr + 1'd1, S_IDLE);
		end

		//------------------------------------------------------------
		// receive
		//------------------------------------------------------------
		S_RX_POLL: begin
			rx_wptr <= m_rdata;
			if (m_rdata != rx_rptr && brx_ready)
				ddr_op(0, rx_slot_base, 64'd0, S_RX_HDR);
			else st <= S_IDLE;
		end
		S_RX_HDR: begin
			cur_len <= m_rdata[10:0];
			pos <= 0;
			if (m_rdata[10:0] == 0 || m_rdata[10:0] > 11'd1600) begin
				// nonsense length: drop the slot
				st <= S_RX_RPTR;
			end
			else begin
				brx_start <= 1;
				brx_len <= m_rdata[10:0];
				ddr_op(0, rx_slot_base + 29'd1, 64'd0, S_RX_READ);
			end
		end
		S_RX_READ: begin
			pack <= m_rdata;
			lane <= 0;
			st <= S_RX_STREAM;
		end
		S_RX_STREAM: begin
			brx_valid <= 1;
			brx_data <= pack[{lane, 3'b000} +: 8];
			if (pos + 11'd1 >= cur_len) st <= S_RX_RPTR;
			else if (lane == 3'd7)
				ddr_op(0, rx_slot_base + 29'd1 + {23'd0, pos[10:3]} + 29'd1, 64'd0, S_RX_READ);
			else lane <= lane + 1'd1;
			pos <= pos + 1'd1;
		end
		S_RX_RPTR: begin
			rx_rptr <= rx_rptr + 1'd1;
			ddr_op(1, A_RXRPTR, rx_rptr + 1'd1, S_IDLE);
		end

		S_WAIT: if (m_ack) begin
			m_req <= 0;
			st <= ret;
		end

		default: st <= S_IDLE;
		endcase
	end
end

endmodule
