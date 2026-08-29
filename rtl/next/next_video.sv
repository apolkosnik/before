//============================================================================
//  NeXT MegaPixel display, monochrome 2 bit grayscale
//
//  1120 x 832 visible, 2 bits per pixel, 4 pixels per byte, line pitch
//  288 bytes (1120/4 active + 8 pad, see fast_screen.c in Previous:
//  pitch = (1120 + 32) / 4 for non-turbo).  Pixel values: 0 = white,
//  1 = light gray, 2 = dark gray, 3 = black.
//
//  Timing here is 1600 x 912 total at a 100 MHz pixel clock, giving a
//  62.5 kHz line rate and a 68.5 Hz frame rate (the real monitor runs at
//  68.3 Hz).  The frame interrupt (INT_VIDEO through the video DMA
//  channel) fires at the start of vertical blank.
//
//  The scan-out reads the VRAM through a dedicated 16-bit read port
//  (8 pixels per word).  The port has a registered address and a
//  registered output, so the fetch runs 3 cycles ahead of the shift
//  register load.
//============================================================================

module next_video
(
	input         clk,           // 100 MHz, one pixel per clock

	output reg    hsync,
	output reg    vsync,
	output reg    hblank,
	output reg    vblank,
	output  [7:0] gray,          // pixel luminance (monochrome machine)

	// colour machine: RGBA4444 pixel from the DDR3 line buffer
	input         color_mode,
	input  [15:0] cpx_data,      // pixel under cpx, two clocks late
	output [10:0] cpx,           // pixel within the line being fetched
	output  [9:0] fetch_line,    // line the frame buffer should hold next
	output reg    line_start,    // one pulse per line
	output  [7:0] r, g, b,       // colour output

	output reg    vbl,           // one-cycle pulse at start of vertical blank

	// VRAM scan port (registered address, registered q, 1 word = 8 pixels)
	output reg [16:0] vram_addr,
	input      [15:0] vram_q
);

localparam H_ACT   = 1120;
localparam H_TOTAL = 1600;
localparam HS_BEG  = 1216;
localparam HS_END  = 1360;

localparam V_ACT   = 832;
localparam V_TOTAL = 912;
localparam VS_BEG  = 869;
localparam VS_END  = 872;

localparam PITCH_W = 144;        // words per line (288 bytes)

reg [10:0] hcnt = 0;
reg  [9:0] vcnt = 0;

// coordinates k cycles ahead, with wrap
function [21:0] ahead;           // {v[9:0], h[11:0]}
	input [10:0] h;
	input  [9:0] v;
	input  [1:0] k;
	reg [11:0] hh;
	reg  [9:0] vv;
	begin
		hh = h + k;
		vv = v;
		if (hh >= H_TOTAL) begin
			hh = hh - H_TOTAL;
			vv = (v == V_TOTAL-1) ? 10'd0 : v + 10'd1;
		end
		ahead = {vv, hh};
	end
endfunction

wire [21:0] a1 = ahead(hcnt, vcnt, 2'd1);
wire [21:0] a3 = ahead(hcnt, vcnt, 2'd3);
wire [11:0] h1 = a1[11:0];
wire  [9:0] v1 = a1[21:12];
wire [11:0] h3 = a3[11:0];
wire  [9:0] v3 = a3[21:12];

// load the shifter when the NEXT cycle starts an active 8-pixel group;
// present the address for it 2 cycles before that
wire load_now  = (v1 < V_ACT) && (h1 < H_ACT) && (h1[2:0] == 3'd0);
wire addr_now  = (v3 < V_ACT) && (h3 < H_ACT) && (h3[2:0] == 3'd0);

reg [15:0] sr;
wire active = (vcnt < V_ACT) && (hcnt < H_ACT);

// 0 = white .. 3 = black
wire [1:0] px = sr[15:14];
assign gray = {4{~px}};

//----------------------------------------------------------------------------
// colour path: the line buffer is addressed two clocks ahead so its
// registered output lines up with the pixel being displayed
//----------------------------------------------------------------------------

wire [21:0] a2 = ahead(hcnt, vcnt, 2'd2);
wire [11:0] h2 = a2[11:0];
assign cpx = h2[10:0];

// the line the frame buffer engine should hold next: the one after the
// line being displayed, and line 0 through vertical blanking so the
// first visible line is always primed
wire [9:0] next_line = (vcnt >= V_ACT-1) ? 10'd0 : vcnt + 10'd1;
assign fetch_line = next_line;

// NeXT colour is 16 bit RGBA 4:4:4:4 (fast_screen.c: r = col & 0xF000),
// each nibble replicated to eight bits
wire [3:0] cr = cpx_data[15:12];
wire [3:0] cg = cpx_data[11:8];
wire [3:0] cb = cpx_data[7:4];

assign r = color_mode ? {cr, cr} : gray;
assign g = color_mode ? {cg, cg} : gray;
assign b = color_mode ? {cb, cb} : gray;

always @(posedge clk) begin
	if (hcnt == H_TOTAL-1) begin
		hcnt <= 0;
		vcnt <= (vcnt == V_TOTAL-1) ? 10'd0 : vcnt + 10'd1;
	end
	else hcnt <= hcnt + 11'd1;

	if (addr_now) vram_addr <= v3 * PITCH_W + {6'd0, h3[10:3]};

	if (load_now)     sr <= vram_q;
	else if (active)  sr <= {sr[13:0], 2'b00};

	hsync  <= (h1 >= HS_BEG) && (h1 < HS_END);
	hblank <= (h1 >= H_ACT);
	vsync  <= (v1 >= VS_BEG) && (v1 < VS_END);
	vblank <= (v1 >= V_ACT);

	vbl <= (vcnt == V_ACT-1) && (hcnt == H_TOTAL-1);

	// one pulse at the end of each line, when next_line is already the
	// line the frame buffer must hold
	line_start <= (hcnt == H_TOTAL-1);
end

endmodule
