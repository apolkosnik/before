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
//    (including repeat/zero-fill modes), volume/control accesses, and reset
//  - the sound out engine: while enabled, a pending DMA buffer is
//    consumed from memory one 16-bit-stereo frame at a time (the frame
//    is {L,R}, big-endian, as snd.c reads it) into an audio FIFO that a
//    44.1 kHz sample tick drains to audio_l/r; the full FIFO stalls the
//    DMA so the whole channel runs at the playback rate, and the channel
//    completes (COMPLETE, chain reload or disable, INT_SND_OUT_DMA) when
//    the buffer is consumed; with no buffer pending, the underrun status
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
//    - mouse input: the MiSTer ps2_mouse packet is decoded to a NeXT
//      mouse report (7-bit clamped x/y deltas in the kms_mouse_move
//      encoding, plus the two button states) and posted at the mouse
//      device address, gated by the poll mask like the keyboard.
//============================================================================

module next_kms_snd #(
	parameter CLK_HZ = 100000000,
	// The real clk frequency, for the 44.1 kHz audio sample rate (which is
	// real time, unlike the virtual CLK_HZ microsecond used for pacing).
	parameter CLK_REAL_HZ = CLK_HZ
)
(
	input         clk,
	input         reset,

	// register access
	input  [10:0] ps2_key,       // MiSTer keyboard event stream
	input  [24:0] ps2_mouse,     // MiSTer mouse packet: [24] toggle strobe,
	                             // [23:16] dy, [15:8] dx, [7:0] PS/2 status
	                             // (bit0 L, bit1 R, bit4 Xsign, bit5 Ysign)

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
	output        int_keymouse,    // INT_KEYMOUSE level
	output reg    int_power,       // F10 held: separate INT_POWER, not a KMS event

	// Codec input DMA lives in next_snd_in; share the KMS control/status.
	output reg    sndin_active,
	output reg    sndin_clear,
	input         sndin_request,
	input         sndin_overrun,

	// signed 16-bit stereo audio out, driven at the NeXT's 44.1 kHz rate
	output wire signed [15:0] audio_l,
	output wire signed [15:0] audio_r
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
reg  [1:0] ctrl_down;           // retain each Control key independently
reg        caps_down;          // ignore typematic repeats when toggling Caps Lock

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

// The mouse is the keyboard's device address with the KM_MOUSE bit set
// (kms.c: addr = km_addr | KM_MOUSE); device 1 alongside the keyboard's 0
// when the address is the reset default.  Enabled the same way.
wire [3:0] dev_addr_mouse = dev_addr | 4'h1;
wire mouse_enabled =
	((km_dev_msk[31:28] == dev_addr_mouse) && (km_dev_msk[31:28] != 4'hF)) ||
	((km_dev_msk[27:24] == dev_addr_mouse) && (km_dev_msk[27:24] != 4'hF)) ||
	((km_dev_msk[23:20] == dev_addr_mouse) && (km_dev_msk[23:20] != 4'hF)) ||
	((km_dev_msk[19:16] == dev_addr_mouse) && (km_dev_msk[19:16] != 4'hF)) ||
	((km_dev_msk[15:12] == dev_addr_mouse) && (km_dev_msk[15:12] != 4'hF)) ||
	((km_dev_msk[11:8]  == dev_addr_mouse) && (km_dev_msk[11:8]  != 4'hF));

reg       sndout_active;
reg       snd_underrun;
reg [1:0] sndout_mode;
reg repeat_phase;
reg [31:0] repeat_frame;
reg dma_cancelled;
reg [5:0] attenuation_l, attenuation_r;
reg [4:0] volume_bits;
reg [10:0] volume_shift;
reg [7:0] gpo;

// A 16-bit write to the last two data bytes must include BOTH new bytes
// when executing the command (nonblocking updates have not landed yet).
wire kms_execute = sel_kms && we && addr[3:1] == 3'd3 && be[0];
wire [31:0] command_data = {kms_data[31:16],
                            be[1] ? wdata[15:8] : kms_data[15:8], wdata[7:0]};
wire kms_reset_command = kms_execute && st_cmd == 8'hff && command_data == 32'hffffffff;
wire output_command = kms_execute && (st_cmd & 8'hc7) == 8'h07;

assign int_snd_ovrun = snd_underrun | sndin_overrun;

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

wire [7:0] sound_status = (st_snd & 8'hf9) | {5'd0, sndin_request, sndin_overrun, 1'b0};
`define KMS_READ(a) ( \
	((a) == 4'h0) ? sound_status : \
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

localparam E_IDLE = 3'd0, E_RD = 3'd1, E_ACK = 3'd2;
reg  [2:0] est;
reg [15:0] poll;                 // polling interval while idle

// audio sample-rate tick (44.1 kHz) from the real clock, a fractional
// divider (add SR each clock, tick and subtract when it reaches the clock).
localparam AUDIO_SR = 44100;
reg  [31:0] sr_acc;
wire [32:0] sr_sum = {1'b0, sr_acc} + AUDIO_SR;
wire        sample_tick = (sr_sum >= CLK_REAL_HZ);

// stereo audio FIFO: the sound-out DMA fills it, the sample tick drains it
// into audio_l/r. A full FIFO stalls DMA; doubled modes consume one frame
// per two output ticks.
localparam AF_DEPTH = 256, AF_AW = 8;
reg [31:0]      afifo [0:AF_DEPTH-1];   // {L[15:0], R[15:0]} per frame
reg [AF_AW-1:0] af_wr, af_rd;
reg [AF_AW:0]   af_cnt;
wire af_full  = (af_cnt == AF_DEPTH);
wire af_empty = (af_cnt == 0);
wire af_push = (est == E_ACK) && m_ack && !m_err && !dma_cancelled && !output_cancel;
wire af_pop = sample_tick && !af_empty &&
              (!sndout_mode[0] || !repeat_phase) && !output_flush;
wire [31:0] output_frame = af_pop ? afifo[af_rd] :
    (sndout_mode[0] && repeat_phase && !sndout_mode[1]) ? repeat_frame : 32'd0;

wire [7:0] csr_or = (be[1] ? wdata[15:8] : 8'h00) | (be[0] ? wdata[7:0] : 8'h00);
wire csr_write = sel_csr && we && !addr[1];
wire dma_reset = csr_write && csr_or[4];
// DMA reset cancels in-flight reads, but the sound station must still
// drain frames it already accepted. KMS reset or a fresh start clears the
// station queue; ordinary stop allows the final buffered samples to play.
wire output_flush = kms_reset_command || (output_command && st_cmd[3] && !sndout_active);
wire output_cancel = dma_reset || output_flush;

next_sound_output output_processing (
    .clk(clk), .reset(reset || output_flush),
    .sample_strobe(sample_tick), .frame(output_frame),
    .mute(gpo[4]), .deemphasis(gpo[3]),
    .attenuation_l(attenuation_l), .attenuation_r(attenuation_r),
    .audio_l(audio_l), .audio_r(audio_r)
);

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
// Previous src/gui-sdl/sdlkeymap.c, non-ADB scancode map).
// Deliberate exception: only F10 requests power; forward Delete is ignored.
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
			8'h61: next_key = 7'h03;   // ISO extra backslash
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
			8'h0F: next_key = 7'h27;   // keypad equals
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
			8'h09: next_key = 7'h58;   // F10 -> separate power request
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

// mouse: decode the MiSTer PS/2 packet.  dx/dy are 9-bit two's complement
// (byte plus its sign bit from the status byte).  PS/2 dy is up-positive;
// the NeXT wants down-positive, so dy is negated.
reg         ps2_mouse_tgl_d;
wire        mouse_event = (ps2_mouse[24] != ps2_mouse_tgl_d);
wire signed [8:0] mouse_dx =  $signed({ps2_mouse[4], ps2_mouse[15:8]});
wire signed [8:0] mouse_dy = -$signed({ps2_mouse[5], ps2_mouse[23:16]});
wire        m_left  = ps2_mouse[0];
wire        m_right = ps2_mouse[1];
// NeXT mouse word: [15:9] y, [8] right-up, [7:1] x, [0] left-up
wire [15:0] mouse16 = {mouse_field(mouse_dy), ~m_right,
                       mouse_field(mouse_dx), ~m_left};

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

// One 7-bit NeXT mouse axis field from a signed delta, matching
// kms_mouse_move() in kms.c: magnitude is clamped to 0x3F; a negative
// delta (left / up) is the bare magnitude, a positive delta (right /
// down) is (0x40 - magnitude) | 0x40, and zero is zero.
function automatic [6:0] mouse_field;
	input signed [8:0] d;
	reg [8:0] absd;
	reg [6:0] mag;
	begin
		absd = d[8] ? (-d) : d;
		mag  = (absd > 9'd63) ? 7'h3F : absd[6:0];
		if (!d[8] && mag != 0) mouse_field = (7'h40 - mag) | 7'h40; // right/down
		else                   mouse_field = mag;                    // left/up/zero
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

task automatic set_volume;
    input [7:0] value;
    begin
        if (value[6]) attenuation_l <= value[5:0] > 43 ? 6'd43 : value[5:0];
        if (value[7]) attenuation_r <= value[5:0] > 43 ? 6'd43 : value[5:0];
    end
endtask

// KMS command execution, KMS_command() in kms.c
task automatic kms_command;
	input [7:0] cmd;
	input [31:0] data;
	begin
		if (cmd == 8'hC6) begin
			// KMSCMD_KBD_RECV: device poll mask
			km_dev_msk <= data;
		end
		else if (cmd == 8'hC5) begin : kmreg
			// KMSCMD_KMREG, access_km_reg(): the data long is already
			// assembled except its lowest byte, which is in this write
			reg [7:0] reg_addr, reg_data;
			reg_addr = data[31:24];
			reg_data = data[23:16];
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
				sndout_mode <= cmd[5:4];
				repeat_phase <= 0;
			end
			else begin
				sndout_active <= 0;
				st_snd <= st_snd & ~(SNDOUT_DMA_UNDERRUN|SNDOUT_DMA_REQUEST);
				snd_underrun <= 0;
			end
		end
		else if ((cmd & 8'hC7) == 8'h03) begin
			sndin_active <= cmd[3];
			if (!cmd[3]) sndin_clear <= 1;
		end
        else if (cmd == 8'hc4) begin
            gpo <= data[31:24];
            if (data[24]) begin
                if (volume_bits == 11 && volume_shift[10:8] == 3'b111)
                    set_volume(volume_shift[7:0]);
            end else if (gpo[0]) begin
                volume_bits <= 0;
                volume_shift <= 0;
            end else if (data[26] && !gpo[2]) begin
                volume_shift <= {volume_shift[9:0], data[25]};
                // Saturate so an overlong transaction can never wrap to 11.
                if (volume_bits != 31) volume_bits <= volume_bits + 1'd1;
            end
        end else if (cmd == 8'hc2) begin
            set_volume(data[31:24]);
            volume_bits <= 0;
            volume_shift <= 0;
        end
        // C7 direct output remains outside the DMA playback path.
	end
endtask

always @(posedge clk) begin
	sndin_clear <= 0;
	if (reset) begin
		st_snd <= 0; st_km <= 0; st_tx <= 0; st_cmd <= 0;
		kms_data <= 0;
		km_data <= 0;
		km_address <= 0;
		km_dev_msk <= 0;
		mods <= 0;
		capslock <= 0;
		ctrl_down <= 0;
		caps_down <= 0;
		int_power <= 0;
		// Consume the current host strobe during reset, rather than replaying
		// a stale held F10/key event when the device comes out of reset.
		ps2_toggle_d <= ps2_key[10];
		ps2_mouse_tgl_d <= 0;
		sndout_active <= 0;
        sndout_mode <= 0; repeat_phase <= 0; repeat_frame <= 0;
        dma_cancelled <= 0;
        attenuation_l <= 0; attenuation_r <= 0;
        volume_bits <= 0; volume_shift <= 0; gpo <= 0;
		sndin_active <= 0;
		snd_underrun <= 0;
		s_csr <= 0;
		s_next <= 0; s_limit <= 0; s_start <= 0; s_stop <= 0;
		s_snext <= 0; s_slimit <= 0; s_sstart <= 0; s_sstop <= 0;
		est <= E_IDLE;
		poll <= 0;
		uspresc <= 0;
		m_req <= 0;
		sr_acc <= 0;
		af_wr <= 0; af_rd <= 0; af_cnt <= 0;
		m_we <= 0; m_addr <= 0; m_be <= 0; m_din <= 0;
	end
	else begin
		uspresc <= us_tick ? 1'd0 : uspresc + 1'd1;

		//------------------------------------------------------------
		// audio: run the 44.1 kHz divider, drain one FIFO frame per tick
		// to audio_l/r (silence when the FIFO is empty), and accept the
		// DMA's pushed frame.  m_dout carries {L[15:0], R[15:0]}.
		//------------------------------------------------------------
		sr_acc <= sample_tick ? (sr_sum[31:0] - CLK_REAL_HZ) : sr_sum[31:0];
		if (sample_tick) begin
            if (!sndout_mode[0]) repeat_phase <= 0;
            else if (repeat_phase) repeat_phase <= 0;
            else if (af_pop) repeat_phase <= 1;
        end
        if (af_pop) begin
            repeat_frame <= afifo[af_rd];
            af_rd <= af_rd + 1'd1;
        end
		if (af_push) begin
			afifo[af_wr] <= m_dout;
			af_wr <= af_wr + 1'd1;
		end
		case ({af_push, af_pop})
			2'b10: af_cnt <= af_cnt + 1'd1;
			2'b01: af_cnt <= af_cnt - 1'd1;
			default: ;
		endcase

		//------------------------------------------------------------
		// keyboard events
		//------------------------------------------------------------
		ps2_toggle_d <= ps2_key[10];
		if (ps2_event) begin : kbd_ev
			reg [6:0] mb, nmods;
			reg [6:0] kc;
			reg [1:0] nctrl;
			reg ncaps, caps_event;
			mb = mod_bit(ps2_ext, ps2_code);
			nmods = ps2_make ? (mods | mb) : (mods & ~mb);
			nctrl = ctrl_down;
			if (ps2_code == 8'h14) begin
				nctrl[ps2_ext] = ps2_make;
				nmods[0] = |nctrl;
			end
			ctrl_down <= nctrl;
			mods <= nmods;
			caps_event = !ps2_ext && ps2_code == 8'h58;
			ncaps = capslock;
			if (caps_event) begin
				if (ps2_make && !caps_down) ncaps = ~capslock;
				caps_down <= ps2_make;
			end
			capslock <= ncaps;
			kc = next_key(ps2_ext, ps2_code);
			// Previous's non-turbo RTC power request goes directly to the
			// interrupt controller, independently of keyboard polling.
			if (kc == 7'h58) int_power <= ps2_make;
			else if ((kc != 0 || mb != 0 || caps_event) && kbd_enabled) begin
				// kms_keydown()/kms_keyup()
				km_data <= {4'b0001, km_address, 8'd0,
				            1'b1, nmods | (ncaps ? 7'h02 : 7'h00),
				            !ps2_make, kc};
				kms_interrupt;
			end
		end
		//------------------------------------------------------------
		// mouse events (kms_mouse_move / kms_mouse_button).  Deferred a
		// cycle behind a keyboard event so they never share km_data; the
		// toggle is consumed only when processed, so nothing is dropped.
		//------------------------------------------------------------
		else if (mouse_event) begin
			ps2_mouse_tgl_d <= ps2_mouse[24];
			if (mouse_enabled) begin
				km_data <= {4'b0000, km_address[3:1], 1'b1, 8'd0, mouse16};
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
							if (v[1] && !sndin_active) sndin_clear <= 1;
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
							kms_command(st_cmd, command_data);
						end
						4'h8, 4'h9, 4'hA, 4'hB: ;   // km_data is read only
						default: ;
					endcase
				end
			end
		end

		if (csr_write) begin
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
		// The DMA fetches one stereo frame at a time and hands it to the
		// audio FIFO. A full FIFO holds it off; source consumption follows
		// the selected output mode. Completion marks the final DMA fetch,
		// with up to one FIFO of samples still queued for playback.
		E_IDLE: begin
			if (sndout_active && !output_cancel) begin
				if (s_csr[0] && s_next < s_limit) begin
					if (!af_full) est <= E_RD;   // read when the FIFO has room
				end
				else if (af_empty && !repeat_phase && us_tick) begin
					if (poll != 0) poll <= poll - 1'd1;
					else begin
						st_snd <= st_snd | SNDOUT_DMA_UNDERRUN | SNDOUT_DMA_REQUEST;
						snd_underrun <= 1;
						poll <= 16'd100;
					end
				end
			end
		end

		E_RD: begin
			if (s_next >= s_limit || !s_csr[0] || !sndout_active || output_cancel) est <= E_IDLE;
			else begin
				m_req <= 1;
				m_we <= 0;
				m_be <= 4'hF;
				m_addr <= s_next[31:2];
				est <= E_ACK;
			end
		end

		E_ACK: if (m_ack || m_err) begin
			m_req <= 0;
			est <= E_IDLE;
			dma_cancelled <= 0;
			if (!dma_cancelled && !output_cancel) begin
				if (m_err) dma_bus_exception;
				else begin
					// The accepted stereo frame also enters the FIFO.
					if ((s_next | 32'd3) + 32'd1 >= s_limit) begin
						s_csr[3] <= 1;
						if (s_csr[1]) begin
							s_next <= s_start;
							s_limit <= s_stop;
							s_csr[1] <= 0;
						end else begin
							s_next <= (s_next | 32'd3) + 32'd1;
							s_csr[0] <= 0;
						end
					end else s_next <= (s_next | 32'd3) + 32'd1;
				end
			end
		end

		default: est <= E_IDLE;
		endcase
        if (output_flush) begin
            af_wr <= 0; af_rd <= 0; af_cnt <= 0;
            repeat_phase <= 0; repeat_frame <= 0;
        end
        if (output_cancel && m_req && !m_ack && !m_err) dma_cancelled <= 1;
        // Last assignment wins over simultaneous command/status activity.
        // KMS reset stops codec activity, but preserves DMA registers.
        if (kms_reset_command) begin
            st_snd <= 0; st_km <= 0; st_tx <= 0; st_cmd <= 0;
            kms_data <= 0; km_data <= 0; km_address <= 0; km_dev_msk <= 0;
            mods <= 0; capslock <= 0; ctrl_down <= 0; caps_down <= 0;
            // A KMS command reset does not release the separate power key.
            sndout_active <= 0; sndin_active <= 0; sndin_clear <= 1;
            snd_underrun <= 0; sndout_mode <= 0; poll <= 0;
        end
	end
end

endmodule

// 44.1 kHz codec output processing. Q12 filter history preserves fractional
// feedback; Q17 coefficients follow Previous snd_deemphasis_filter. Three
// pipeline stages keep the multipliers off the FIFO-to-pin timing path.
module next_sound_output (
    input clk, reset, sample_strobe,
    input [31:0] frame,
    input mute, deemphasis,
    input [5:0] attenuation_l, attenuation_r,
    output reg signed [15:0] audio_l, audio_r
);
function automatic [16:0] gain(input [5:0] attenuation);
    begin
        case (attenuation)
            6'd0: gain = 17'd65536;
            6'd1: gain = 17'd52057;
            6'd2: gain = 17'd41350;
            6'd3: gain = 17'd32846;
            6'd4: gain = 17'd26090;
            6'd5: gain = 17'd20724;
            6'd6: gain = 17'd16462;
            6'd7: gain = 17'd13076;
            6'd8: gain = 17'd10387;
            6'd9: gain = 17'd8250;
            6'd10: gain = 17'd6554;
            6'd11: gain = 17'd5206;
            6'd12: gain = 17'd4135;
            6'd13: gain = 17'd3285;
            6'd14: gain = 17'd2609;
            6'd15: gain = 17'd2072;
            6'd16: gain = 17'd1646;
            6'd17: gain = 17'd1308;
            6'd18: gain = 17'd1039;
            6'd19: gain = 17'd825;
            6'd20: gain = 17'd655;
            6'd21: gain = 17'd521;
            6'd22: gain = 17'd414;
            6'd23: gain = 17'd328;
            6'd24: gain = 17'd261;
            6'd25: gain = 17'd207;
            6'd26: gain = 17'd165;
            6'd27: gain = 17'd131;
            6'd28: gain = 17'd104;
            6'd29: gain = 17'd83;
            6'd30: gain = 17'd66;
            6'd31: gain = 17'd52;
            6'd32: gain = 17'd41;
            6'd33: gain = 17'd33;
            6'd34: gain = 17'd26;
            6'd35: gain = 17'd21;
            6'd36: gain = 17'd16;
            6'd37: gain = 17'd13;
            6'd38: gain = 17'd10;
            6'd39: gain = 17'd8;
            6'd40: gain = 17'd7;
            6'd41: gain = 17'd5;
            6'd42: gain = 17'd4;
            default: gain = 0; // 43 and above are mute
        endcase
    end
endfunction
reg [2:0] valid;
reg use_filter, muted;
reg [16:0] gain_l, gain_r;
reg signed [27:0] input_l, input_r, previous_l, previous_r;
reg signed [27:0] history_l, history_r, filtered_l, filtered_r;
reg signed [45:0] p0_l, p1_l, p2_l, p0_r, p1_r, p2_r;
reg signed [45:0] volume_l, volume_r;
wire signed [47:0] sum_l = {{2{p0_l[45]}},p0_l} + {{2{p1_l[45]}},p1_l} + {{2{p2_l[45]}},p2_l};
wire signed [47:0] sum_r = {{2{p0_r[45]}},p0_r} + {{2{p1_r[45]}},p1_r} + {{2{p2_r[45]}},p2_r};
function automatic signed [27:0] filter_clip(input signed [47:0] sum);
    reg signed [47:0] value;
    begin
        value = sum >>> 17;
        if (value > 48'sd134217727) filter_clip = 28'sh7ffffff;
        else if (value < -48'sd134217728) filter_clip = 28'sh8000000;
        else filter_clip = value[27:0];
    end
endfunction
function automatic signed [15:0] volume_clip(input signed [45:0] product);
    reg signed [46:0] value, magnitude;
    begin
        // Q12 PCM times Q16 gain. Round symmetrically, then saturate.
        magnitude = product < 0 ? -{product[45],product} : {product[45],product};
        value = (magnitude + 47'sd134217728) >>> 28;
        if (product < 0) value = -value;
        if (value > 32767) volume_clip = 16'sh7fff;
        else if (value < -32768) volume_clip = 16'sh8000;
        else volume_clip = value[15:0];
    end
endfunction
always @(posedge clk) begin
    if (reset) begin
        valid <= 0; use_filter <= 0; muted <= 0;
        gain_l <= 0; gain_r <= 0;
        input_l <= 0; input_r <= 0; previous_l <= 0; previous_r <= 0;
        history_l <= 0; history_r <= 0; filtered_l <= 0; filtered_r <= 0;
        p0_l <= 0; p1_l <= 0; p2_l <= 0; p0_r <= 0; p1_r <= 0; p2_r <= 0;
        volume_l <= 0; volume_r <= 0; audio_l <= 0; audio_r <= 0;
    end else begin
        valid <= {valid[1:0],sample_strobe};
        if (sample_strobe) begin
            use_filter <= deemphasis; muted <= mute;
            gain_l <= gain(attenuation_l); gain_r <= gain(attenuation_r);
            input_l <= {frame[31:16],12'd0}; input_r <= {frame[15:0],12'd0};
            p0_l <= $signed({frame[31:16],12'd0}) * 18'sd60287;
            p0_r <= $signed({frame[15:0],12'd0}) * 18'sd60287;
            p1_l <= previous_l * -18'sd11511; p1_r <= previous_r * -18'sd11511;
            p2_l <= history_l * 18'sd82296; p2_r <= history_r * 18'sd82296;
            previous_l <= deemphasis ? {frame[31:16],12'd0} : 28'd0;
            previous_r <= deemphasis ? {frame[15:0],12'd0} : 28'd0;
        end
        if (valid[0]) begin
            filtered_l <= use_filter ? filter_clip(sum_l) : input_l;
            filtered_r <= use_filter ? filter_clip(sum_r) : input_r;
            history_l <= use_filter ? filter_clip(sum_l) : 28'd0;
            history_r <= use_filter ? filter_clip(sum_r) : 28'd0;
        end
        if (valid[1]) begin
            volume_l <= filtered_l * $signed({1'b0,gain_l});
            volume_r <= filtered_r * $signed({1'b0,gain_r});
        end
        if (valid[2]) begin
            audio_l <= muted ? 16'sd0 : volume_clip(volume_l);
            audio_r <= muted ? 16'sd0 : volume_clip(volume_r);
        end
    end
end
endmodule
