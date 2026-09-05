//============================================================================
//  KMS (keyboard/mouse/sound station) interface and the sound-out DMA
//  channel
//
//  KMS registers 0x0200E000-0x0200E00F, sound out DMA channel CSR
//  0x02000040, saved pointers 0x02004030-0x0200403C, pointers
//  0x02004040-0x0200404C, init 0x02004240.
//
//  Modeled on Previous src/kms.c, src/snd.c and the sound channel parts
//  of src/dma.c:
//  - the four KMS status/control bytes with their write-one-to-clear
//    and enable-latch semantics
//  - the command/data pair: a write to the data register executes the
//    command in the command byte (KMS_Data_Write -> KMS_command);
//    implemented commands are the sound out enable/disable family
//    (KMSCMD_SND_OUT with SIO_ENABLE), the volume/control accesses as
//    no-ops, and reset
//  - the sound out engine: while enabled, a pending DMA buffer is
//    consumed from memory (the samples go nowhere yet, there is no
//    audio path), then the channel completes (COMPLETE, chain reload or
//    disable, INT_SND_OUT_DMA) after roughly len/4 microseconds, the
//    pacing snd.c uses; with no buffer pending, the underrun status
//    bits are raised with INT_SOUND_OVRUN, as kms_sndout_underrun()
//
//  - keyboard input: MiSTer ps2_key events are translated to NeXT
//    keycodes (the mapping of Previous src/keymap.c), modifier state is
//    tracked (control, shifts, commands, alts, shift-lock), and events
//    are posted to the keyboard/mouse data register with
//    KBD_RECEIVED/KBD_INT and INT_KEYMOUSE, honoring the device poll
//    mask (KMSCMD_KBD_RECV) and the device address; the device
//    register protocol (KMSCMD_KMREG: reset, set address, register
//    reads) answers with kms_response() exactly as kms.c
//
//  Mouse input and the real audio output path are TODO
//  (docs/PORTING.md).
//============================================================================

module next_kms_snd #(parameter CLK_HZ = 100000000)
(
	input         clk,
	input         reset,

	// register access
	input  [10:0] ps2_key,       // MiSTer keyboard event stream

	input         sel_kms,       // 0x0E000-0x0E00F
	input         sel_csr,       // 0x00040-0x00043
	input         sel_sptr,      // 0x04030-0x0403F (saved)
	input         sel_ptr,       // 0x04040-0x0404F
	input         sel_ini,       // 0x04240-0x04243
	input   [3:0] addr,
	input         we,
	input   [1:0] be,
	input  [15:0] wdata,
	output [15:0] rdata,

	// RAM master port
	output reg        m_req,
	output reg        m_we,
	output reg [29:0] m_addr,
	output reg  [3:0] m_be,
	output reg [31:0] m_din,
	input      [31:0] m_dout,
	input             m_ack,
	input             m_err,

	output        int_snd_ovrun,   // INT_SOUND_OVRUN level
	output        int_snd_out_dma, // channel complete level
	output        int_keymouse     // INT_KEYMOUSE level
);

localparam SNDOUT_DMA_ENABLE   = 8'h80, SNDOUT_DMA_REQUEST = 8'h40,
           SNDOUT_DMA_UNDERRUN = 8'h20,
           SNDIN_DMA_ENABLE    = 8'h08, SNDIN_DMA_REQUEST  = 8'h04,
           SNDIN_DMA_OVERRUN   = 8'h02;
localparam KBD_INT = 8'h80, KBD_RECEIVED = 8'h40, KBD_OVERRUN = 8'h20,
           NMI_RECEIVED = 8'h10, KMS_INT = 8'h08, KMS_RECEIVED = 8'h04,
           KMS_OVERRUN = 8'h02;
localparam KMS_ENABLE = 8'h02, TX_LOOP = 8'h01;

//----------------------------------------------------------------------------
// KMS registers
//----------------------------------------------------------------------------

reg [7:0] st_snd, st_km, st_tx, st_cmd;
reg [31:0] kms_data;

// keyboard/mouse side
reg [31:0] km_data;
reg  [3:0] km_address;           // stored pre-masked with 0x0E
reg [31:0] km_dev_msk;
reg  [6:0] mods;
reg        capslock;

assign int_keymouse = st_km[7];  // KBD_INT

// device enabled when one of the six poll-mask nibbles carries the
// device address (kms_device_enabled in kms.c)
wire [3:0] dev_addr = {1'b0, km_address[3:1]};
wire kbd_enabled =
	((km_dev_msk[31:28] == dev_addr) && (km_dev_msk[31:28] != 4'hF)) ||
	((km_dev_msk[27:24] == dev_addr) && (km_dev_msk[27:24] != 4'hF)) ||
	((km_dev_msk[23:20] == dev_addr) && (km_dev_msk[23:20] != 4'hF)) ||
	((km_dev_msk[19:16] == dev_addr) && (km_dev_msk[19:16] != 4'hF)) ||
	((km_dev_msk[15:12] == dev_addr) && (km_dev_msk[15:12] != 4'hF)) ||
	((km_dev_msk[11:8]  == dev_addr) && (km_dev_msk[11:8]  != 4'hF));

reg       sndout_active;
reg       snd_underrun;

assign int_snd_ovrun = snd_underrun;

//----------------------------------------------------------------------------
// sound out DMA channel
//----------------------------------------------------------------------------

reg  [7:0] s_csr;
reg [31:0] s_next, s_limit, s_start, s_stop;
reg [31:0] s_snext, s_slimit, s_sstart, s_sstop;

assign int_snd_out_dma = s_csr[3];

//----------------------------------------------------------------------------
// read mux
//----------------------------------------------------------------------------

`define KMS_READ(a) ( \
	((a) == 4'h0) ? st_snd : \
	((a) == 4'h1) ? st_km : \
	((a) == 4'h2) ? st_tx : \
	((a) == 4'h3) ? st_cmd : \
	((a) == 4'h4) ? kms_data[31:24] : \
	((a) == 4'h5) ? kms_data[23:16] : \
	((a) == 4'h6) ? kms_data[15:8] : \
	((a) == 4'h7) ? kms_data[7:0] : \
	((a) == 4'h8) ? km_data[31:24] : \
	((a) == 4'h9) ? km_data[23:16] : \
	((a) == 4'hA) ? km_data[15:8] : \
	((a) == 4'hB) ? km_data[7:0] : 8'h00 )

wire [31:0] sptr_q = (addr[3:2] == 2'd0) ? s_snext :
                     (addr[3:2] == 2'd1) ? s_slimit :
                     (addr[3:2] == 2'd2) ? s_sstart : s_sstop;
wire [31:0] ptr_q  = (addr[3:2] == 2'd0) ? s_next :
                     (addr[3:2] == 2'd1) ? s_limit :
                     (addr[3:2] == 2'd2) ? s_start : s_stop;

assign rdata = sel_kms  ? {`KMS_READ({addr[3:1], 1'b0}), `KMS_READ({addr[3:1], 1'b1})} :
               sel_csr  ? (addr[1] ? 16'h0000 : {s_csr, 8'h00}) :
               sel_sptr ? (addr[1] ? sptr_q[15:0] : sptr_q[31:16]) :
               sel_ptr  ? (addr[1] ? ptr_q[15:0] : ptr_q[31:16]) :
               sel_ini  ? (addr[1] ? s_next[15:0] : s_next[31:16]) : 16'h0000;

//----------------------------------------------------------------------------
// engine
//----------------------------------------------------------------------------

localparam US_DIV = CLK_HZ / 1000000;
reg [$clog2(US_DIV)-1:0] uspresc;
wire us_tick = (uspresc == US_DIV-1);

localparam E_IDLE = 3'd0, E_RD = 3'd1, E_ACK = 3'd2, E_PACE = 3'd3;
reg  [2:0] est;
reg [17:0] pace;                 // microseconds until the completion intr
reg [15:0] poll;                 // polling interval while idle

wire [7:0] csr_or = (be[1] ? wdata[15:8] : 8'h00) | (be[0] ? wdata[7:0] : 8'h00);

// Sound DMA memory faults are DMA channel errors in Previous.  Do not let
// high/virtual pointers wrap into the low 64 MB RAM window.
task automatic dma_bus_exception;
	begin
		s_csr[0] <= 0;
		s_csr[3] <= 1;
		s_csr[4] <= 1;
		m_req <= 0;
		est <= E_IDLE;
	end
endtask

//----------------------------------------------------------------------------
// PS/2 set-2 to NeXT keycode translation (Keymap_GetKeyFromScancode in
// Previous src/keymap.c, keys reachable from a PC keyboard)
//----------------------------------------------------------------------------

function automatic [6:0] next_key;
	input       ext;
	input [7:0] c;
	begin
		next_key = 7'h00;
		if (!ext) case (c)
			8'h76: next_key = 7'h49;   // escape
			8'h16: next_key = 7'h4a;   // 1
			8'h1E: next_key = 7'h4b;   // 2
			8'h26: next_key = 7'h4c;   // 3
			8'h25: next_key = 7'h4d;   // 4
			8'h2E: next_key = 7'h50;   // 5
			8'h36: next_key = 7'h4f;   // 6
			8'h3D: next_key = 7'h4e;   // 7
			8'h3E: next_key = 7'h1e;   // 8
			8'h46: next_key = 7'h1f;   // 9
			8'h45: next_key = 7'h20;   // 0
			8'h4E: next_key = 7'h1d;   // minus
			8'h55: next_key = 7'h1c;   // equals
			8'h66: next_key = 7'h1b;   // backspace
			8'h0D: next_key = 7'h41;   // tab
			8'h15: next_key = 7'h42;   // q
			8'h1D: next_key = 7'h43;   // w
			8'h24: next_key = 7'h44;   // e
			8'h2D: next_key = 7'h45;   // r
			8'h2C: next_key = 7'h48;   // t
			8'h35: next_key = 7'h47;   // y
			8'h3C: next_key = 7'h46;   // u
			8'h43: next_key = 7'h06;   // i
			8'h44: next_key = 7'h07;   // o
			8'h4D: next_key = 7'h08;   // p
			8'h54: next_key = 7'h05;   // left bracket
			8'h5B: next_key = 7'h04;   // right bracket
			8'h5D: next_key = 7'h03;   // backslash
			8'h1C: next_key = 7'h39;   // a
			8'h1B: next_key = 7'h3a;   // s
			8'h23: next_key = 7'h3b;   // d
			8'h2B: next_key = 7'h3c;   // f
			8'h34: next_key = 7'h3d;   // g
			8'h33: next_key = 7'h40;   // h
			8'h3B: next_key = 7'h3f;   // j
			8'h42: next_key = 7'h3e;   // k
			8'h4B: next_key = 7'h2d;   // l
			8'h4C: next_key = 7'h2c;   // semicolon
			8'h52: next_key = 7'h2b;   // apostrophe
			8'h5A: next_key = 7'h2a;   // return
			8'h1A: next_key = 7'h31;   // z
			8'h22: next_key = 7'h32;   // x
			8'h21: next_key = 7'h33;   // c
			8'h2A: next_key = 7'h34;   // v
			8'h32: next_key = 7'h35;   // b
			8'h31: next_key = 7'h37;   // n
			8'h3A: next_key = 7'h36;   // m
			8'h41: next_key = 7'h2e;   // comma
			8'h49: next_key = 7'h2f;   // period
			8'h4A: next_key = 7'h30;   // slash
			8'h29: next_key = 7'h38;   // space
			8'h0E: next_key = 7'h26;   // backquote
			8'h77: next_key = 7'h26;   // num lock -> backquote
			8'h6C: next_key = 7'h21;   // kp 7
			8'h75: next_key = 7'h22;   // kp 8
			8'h7D: next_key = 7'h23;   // kp 9
			8'h7B: next_key = 7'h24;   // kp minus
			8'h6B: next_key = 7'h12;   // kp 4
			8'h73: next_key = 7'h18;   // kp 5
			8'h74: next_key = 7'h13;   // kp 6
			8'h79: next_key = 7'h15;   // kp plus
			8'h69: next_key = 7'h11;   // kp 1
			8'h72: next_key = 7'h17;   // kp 2
			8'h7A: next_key = 7'h14;   // kp 3
			8'h70: next_key = 7'h0b;   // kp 0
			8'h71: next_key = 7'h0c;   // kp period
			8'h7C: next_key = 7'h25;   // kp asterisk
			8'h05: next_key = 7'h01;   // F1 -> brightness down
			8'h06: next_key = 7'h19;   // F2 -> brightness up
			8'h03: next_key = 7'h02;   // F5 -> sound down
			8'h0B: next_key = 7'h1a;   // F6 -> sound up
			default: ;
		endcase
		else case (c)
			8'h4A: next_key = 7'h28;   // kp slash
			8'h5A: next_key = 7'h0d;   // kp enter
			8'h6B: next_key = 7'h09;   // left
			8'h74: next_key = 7'h10;   // right
			8'h75: next_key = 7'h16;   // up
			8'h72: next_key = 7'h0f;   // down
			8'h69: next_key = 7'h02;   // end -> sound down
			8'h6C: next_key = 7'h1a;   // home -> sound up
			8'h7A: next_key = 7'h01;   // page down -> brightness down
			8'h7D: next_key = 7'h19;   // page up -> brightness up
			default: ;
		endcase
	end
endfunction

reg ps2_toggle_d;
wire ps2_event = (ps2_key[10] != ps2_toggle_d);
wire ps2_make = ps2_key[9];
wire ps2_ext = ps2_key[8];
wire [7:0] ps2_code = ps2_key[7:0];

// modifier bit affected by this scancode, 0 if none
// (bit0 control, 1 lshift, 2 rshift, 3 lcmd, 4 rcmd, 5 lalt, 6 ralt;
// PC: windows keys = command, alt = alt, as in keymap.c unswapped)
function automatic [6:0] mod_bit;
	input       ext;
	input [7:0] c;
	begin
		mod_bit = 7'd0;
		if (c == 8'h14) mod_bit = 7'h01;              // control (both)
		else if (!ext && c == 8'h12) mod_bit = 7'h02; // left shift
		else if (!ext && c == 8'h59) mod_bit = 7'h04; // right shift
		else if (ext && c == 8'h1F) mod_bit = 7'h08;  // left win -> lcmd
		else if (ext && c == 8'h27) mod_bit = 7'h10;  // right win -> rcmd
		else if (!ext && c == 8'h11) mod_bit = 7'h20; // left alt
		else if (ext && c == 8'h11) mod_bit = 7'h40;  // right alt
	end
endfunction

// kms_interrupt() in kms.c
task automatic kms_interrupt;
	begin
		st_cmd <= 8'hC6;                    // KMSCMD_KBD_RECV
		if (st_km[6]) st_km[5] <= 1;        // overrun if still pending
		st_km[7] <= 1;                      // KBD_INT
		st_km[6] <= 1;                      // KBD_RECEIVED
	end
endtask

// kms_response() in kms.c: probes answer "no response / invalid";
// the address is passed in because a set-address command responds with
// the address it just set
task automatic kms_response;
	input [3:0] a;
	begin
		km_data <= {4'b0111, a, 24'd0};
		kms_interrupt;
	end
endtask

// KMS command execution, KMS_command() in kms.c
task automatic kms_command;
	input [7:0] cmd;
	begin
		if (cmd == 8'hC6) begin
			// KMSCMD_KBD_RECV: device poll mask
			km_dev_msk <= {kms_data[31:8], wdata[7:0]};
		end
		else if (cmd == 8'hC5) begin : kmreg
			// KMSCMD_KMREG, access_km_reg(): the data long is already
			// assembled except its lowest byte, which is in this write
			reg [7:0] reg_addr, reg_data;
			reg_addr = kms_data[31:24];
			reg_data = kms_data[23:16];
			if (reg_addr == 8'hEF) begin
				km_address <= {reg_data[3:1], 1'b0};
				kms_response({reg_data[3:1], 1'b0});
			end
			else begin
				// reset (0x0F), reads and writes all answer the same
				kms_response(km_address);
			end
		end
		else if ((cmd & 8'hC7) == 8'h07) begin
			// sound out
			if (cmd & 8'h08) begin       // SIO_ENABLE
				sndout_active <= 1;
			end
			else begin
				sndout_active <= 0;
				st_snd <= st_snd & ~(SNDOUT_DMA_UNDERRUN|SNDOUT_DMA_REQUEST);
				snd_underrun <= 0;
			end
		end
		// 0xC4/0xC2 volume control, 0xC7 analog sound out, 0xFF reset:
		// nothing to do yet
	end
endtask

always @(posedge clk) begin
	if (reset) begin
		st_snd <= 0; st_km <= 0; st_tx <= 0; st_cmd <= 0;
		kms_data <= 0;
		km_data <= 0;
		km_address <= 0;
		km_dev_msk <= 0;
		mods <= 0;
		capslock <= 0;
		ps2_toggle_d <= 0;
		sndout_active <= 0;
		snd_underrun <= 0;
		s_csr <= 0;
		s_next <= 0; s_limit <= 0; s_start <= 0; s_stop <= 0;
		s_snext <= 0; s_slimit <= 0; s_sstart <= 0; s_sstop <= 0;
		est <= E_IDLE;
		pace <= 0;
		poll <= 0;
		uspresc <= 0;
		m_req <= 0;
	end
	else begin
		uspresc <= us_tick ? 1'd0 : uspresc + 1'd1;

		//------------------------------------------------------------
		// keyboard events
		//------------------------------------------------------------
		ps2_toggle_d <= ps2_key[10];
		if (ps2_event) begin : kbd_ev
			reg [6:0] mb, nmods;
			reg [6:0] kc;
			mb = mod_bit(ps2_ext, ps2_code);
			nmods = ps2_make ? (mods | mb) : (mods & ~mb);
			mods <= nmods;
			if (!ps2_ext && ps2_code == 8'h58 && ps2_make)
				capslock <= ~capslock;      // caps lock adds left shift
			kc = next_key(ps2_ext, ps2_code);
			if ((kc != 0 || mb != 0) && kbd_enabled) begin
				// kms_keydown()/kms_keyup()
				km_data <= {4'b0001, km_address, 8'd0,
				            1'b1, nmods | (capslock ? 7'h02 : 7'h00),
				            !ps2_make, kc};
				kms_interrupt;
			end
		end

		//------------------------------------------------------------
		// register reads with side effects
		//------------------------------------------------------------
		if (sel_kms & ~we & (addr[3:1] == 3'd4)) begin
			// KMS_KM_Data_Read: consume the event
			st_km[7] <= 0;                  // KBD_INT
			st_km[6] <= 0;                  // KBD_RECEIVED
		end

		//------------------------------------------------------------
		// register writes
		//------------------------------------------------------------
		if (sel_kms & we) begin : kms_wr
			reg [3:0] a;
			reg [7:0] v;
			integer k;
			for (k = 0; k < 2; k = k + 1) begin
				if (k == 0 ? be[1] : be[0]) begin
					a = {addr[3:1], k[0]};
					v = k[0] ? wdata[7:0] : wdata[15:8];
					case (a)
						4'h0: begin
							// KMS_Ctrl_Snd_Write
							st_snd <= (st_snd & ~(SNDOUT_DMA_ENABLE|SNDIN_DMA_ENABLE))
							        | (v & (SNDOUT_DMA_ENABLE|SNDIN_DMA_ENABLE));
							if ((v & SNDOUT_DMA_UNDERRUN) && !sndout_active) begin
								st_snd <= ((st_snd & ~(SNDOUT_DMA_ENABLE|SNDIN_DMA_ENABLE))
								        | (v & (SNDOUT_DMA_ENABLE|SNDIN_DMA_ENABLE)))
								        & ~(SNDOUT_DMA_UNDERRUN|SNDOUT_DMA_REQUEST);
								snd_underrun <= 0;
							end
						end
						4'h1: begin
							// KMS_Ctrl_KM_Write: write-one-to-clear groups
							if (v & KBD_OVERRUN) st_km <= st_km & ~(KBD_RECEIVED|KBD_OVERRUN|KBD_INT);
							if (v & NMI_RECEIVED) st_km <= st_km & ~NMI_RECEIVED;
							if (v & KMS_OVERRUN) st_km <= st_km & ~(KMS_RECEIVED|KMS_OVERRUN|KMS_INT);
						end
						4'h2: st_tx <= (st_tx & ~(KMS_ENABLE|TX_LOOP)) | (v & (KMS_ENABLE|TX_LOOP));
						4'h3: st_cmd <= v;
						4'h4: kms_data[31:24] <= v;
						4'h5: kms_data[23:16] <= v;
						4'h6: kms_data[15:8] <= v;
						4'h7: begin
							kms_data[7:0] <= v;
							// KMS_Data_Write executes the pending command
							kms_command(st_cmd);
						end
						4'h8, 4'h9, 4'hA, 4'hB: ;   // km_data is read only
						default: ;
					endcase
				end
			end
		end

		if (sel_csr & we & (csr_or != 0)) begin
			if (csr_or[4]) s_csr <= s_csr & ~8'b00001011;
			if (csr_or[1]) s_csr[1] <= 1;
			if (csr_or[0]) s_csr[0] <= 1;
			if (csr_or[3]) s_csr[3] <= 0;
		end

		if (sel_sptr & we) begin
			case (addr[3:2])
				2'd0: begin if (!addr[1]) begin if (be[1]) s_snext[31:24] <= wdata[15:8]; if (be[0]) s_snext[23:16] <= wdata[7:0]; end else begin if (be[1]) s_snext[15:8] <= wdata[15:8]; if (be[0]) s_snext[7:0] <= wdata[7:0]; end end
				2'd1: begin if (!addr[1]) begin if (be[1]) s_slimit[31:24] <= wdata[15:8]; if (be[0]) s_slimit[23:16] <= wdata[7:0]; end else begin if (be[1]) s_slimit[15:8] <= wdata[15:8]; if (be[0]) s_slimit[7:0] <= wdata[7:0]; end end
				2'd2: begin if (!addr[1]) begin if (be[1]) s_sstart[31:24] <= wdata[15:8]; if (be[0]) s_sstart[23:16] <= wdata[7:0]; end else begin if (be[1]) s_sstart[15:8] <= wdata[15:8]; if (be[0]) s_sstart[7:0] <= wdata[7:0]; end end
				2'd3: begin if (!addr[1]) begin if (be[1]) s_sstop[31:24] <= wdata[15:8]; if (be[0]) s_sstop[23:16] <= wdata[7:0]; end else begin if (be[1]) s_sstop[15:8] <= wdata[15:8]; if (be[0]) s_sstop[7:0] <= wdata[7:0]; end end
			endcase
		end
		if (sel_ptr & we) begin
			case (addr[3:2])
				2'd0: begin if (!addr[1]) begin if (be[1]) s_next[31:24] <= wdata[15:8]; if (be[0]) s_next[23:16] <= wdata[7:0]; end else begin if (be[1]) s_next[15:8] <= wdata[15:8]; if (be[0]) s_next[7:0] <= wdata[7:0]; end end
				2'd1: begin if (!addr[1]) begin if (be[1]) s_limit[31:24] <= wdata[15:8]; if (be[0]) s_limit[23:16] <= wdata[7:0]; end else begin if (be[1]) s_limit[15:8] <= wdata[15:8]; if (be[0]) s_limit[7:0] <= wdata[7:0]; end end
				2'd2: begin if (!addr[1]) begin if (be[1]) s_start[31:24] <= wdata[15:8]; if (be[0]) s_start[23:16] <= wdata[7:0]; end else begin if (be[1]) s_start[15:8] <= wdata[15:8]; if (be[0]) s_start[7:0] <= wdata[7:0]; end end
				2'd3: begin if (!addr[1]) begin if (be[1]) s_stop[31:24] <= wdata[15:8]; if (be[0]) s_stop[23:16] <= wdata[7:0]; end else begin if (be[1]) s_stop[15:8] <= wdata[15:8]; if (be[0]) s_stop[7:0] <= wdata[7:0]; end end
			endcase
		end
		// DMA_Init_Write: a write to the init register loads next
		if (sel_ini & we) begin
			if (!addr[1]) begin if (be[1]) s_next[31:24] <= wdata[15:8]; if (be[0]) s_next[23:16] <= wdata[7:0]; end
			else begin if (be[1]) s_next[15:8] <= wdata[15:8]; if (be[0]) s_next[7:0] <= wdata[7:0]; end
		end

		//------------------------------------------------------------
		// sound out engine, SND_Out_Handler() in snd.c
		//------------------------------------------------------------
		case (est)
		E_IDLE: begin
			if (sndout_active && us_tick) begin
				if (poll != 0) poll <= poll - 1'd1;
				else if (s_csr[0]) begin
					if (s_next < s_limit) begin
						pace <= {2'd0, (s_limit[17:0] - s_next[17:0])} >> 2;
						est <= E_RD;
					end
					else begin
						// nothing to play: underrun
						st_snd <= st_snd | SNDOUT_DMA_UNDERRUN | SNDOUT_DMA_REQUEST;
						snd_underrun <= 1;
						poll <= 16'd100;
					end
				end
				else begin
					st_snd <= st_snd | SNDOUT_DMA_UNDERRUN | SNDOUT_DMA_REQUEST;
					snd_underrun <= 1;
					poll <= 16'd100;
				end
			end
		end

			E_RD: begin
				if (s_next >= s_limit) est <= E_PACE;
				else begin
					m_req <= 1;
					m_we <= 0;
					m_be <= 4'hF;
					m_addr <= s_next[31:2];
					est <= E_ACK;
				end
			end

			E_ACK: if (m_err) begin
				dma_bus_exception;
			end
			else if (m_ack) begin
				m_req <= 0;
				// samples are consumed; no audio output path yet
				s_next <= (s_next | 32'd3) + 32'd1;   // whole words
				est <= E_RD;
			end

		E_PACE: begin
			// interrupt after roughly len/4 microseconds
			if (us_tick) begin
				if (pace != 0) pace <= pace - 1'd1;
				else begin
					// dma_sndout_intr -> dma_interrupt(CHANNEL_SOUNDOUT)
					if (s_csr[0] && s_next == s_limit) begin
						s_csr[3] <= 1;
						if (s_csr[1]) begin
							s_next <= s_start;
							s_limit <= s_stop;
							s_csr[1] <= 0;
						end
						else s_csr[0] <= 0;
					end
					est <= E_IDLE;
				end
			end
		end

		default: est <= E_IDLE;
		endcase
	end
end

endmodule
