//============================================================================
//  NeXT video timing test
//
//  Runs the real next_video against the real next_vram and verifies the
//  scan parameters against the module contract (1120x832 visible in a
//  1600x912 frame at 100 MHz) and the pixel pipeline against a known
//  VRAM pattern:
//    - line period 1600 cycles, frame period 912 lines
//    - 1120 active pixels per line, 832 active lines per frame
//    - one vbl pulse per frame, at the start of vertical blank
//    - 2bpp decode: byte 0x1B = pixels 0,1,2,3 = 0xFF,0xAA,0x55,0x00
//    - line pitch 288 bytes (word 144 starts line 1)
//============================================================================

`timescale 1ns/1ps

module tb_next_video;

reg clk = 0;
always #5 clk = ~clk;

wire        hsync, vsync, hblank, vblank, vbl;
wire  [7:0] gray;
wire [16:0] scan_addr;
wire [15:0] scan_q;

next_video dut
(
	.clk(clk),
	.hsync(hsync), .vsync(vsync), .hblank(hblank), .vblank(vblank),
	.gray(gray), .vbl(vbl),
	.vram_addr(scan_addr), .vram_q(scan_q)
);

next_vram vram
(
	.clk_a(clk),
	.a_addr(17'd0), .a_we(2'b00), .a_din(16'd0), .a_q(),
	.clk_b(clk),
	.b_addr(scan_addr), .b_q(scan_q)
);

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
initial begin
	// pattern: line 0 word 0 = 0x1BE4, line 1 word 0 = 0xE41B
	// 0x1B = 00 01 10 11 -> pixels white, light, dark, black
	for (i = 0; i < 131072; i = i + 1) begin
		vram.mem_hi.mem[i] = 8'h00;
		vram.mem_lo.mem[i] = 8'h00;
	end
	vram.mem_hi.mem[0]   = 8'h1B;
	vram.mem_lo.mem[0]   = 8'hE4;
	vram.mem_hi.mem[144] = 8'hE4;
	vram.mem_lo.mem[144] = 8'h1B;
end

// measurements
integer hs_period = 0, hs_last = -1;
integer line_pixels = 0;
integer vbl_period = 0, vbl_last = -1;
integer act_pixels, act_lines;
integer vbl_count = 0;
integer t = 0;
reg hs_d = 0, hb_d = 1, vb_d = 1;

reg [7:0] frame_px [0:7];
reg [7:0] line1_px [0:7];
integer fpx = 0, l1px = 0;
integer lines_since_frame = 0;
reg counting = 0;

always @(posedge clk) begin
	t = t + 1;
	hs_d <= hsync;
	hb_d <= hblank;
	vb_d <= vblank;

	if (hsync && !hs_d) begin
		if (hs_last >= 0) hs_period = t - hs_last;
		hs_last = t;
	end

	if (vbl) begin
		if (vbl_last >= 0) vbl_period = t - vbl_last;
		vbl_last = t;
		vbl_count = vbl_count + 1;
		act_lines = lines_since_frame;
		lines_since_frame = 0;
		counting = 1;
	end

	// count active pixels per line, capture the count at end of line
	if (!hblank && !vblank) begin
		act_pixels = (hb_d || vb_d) ? 1 : act_pixels + 1;
		if (hb_d && !vb_d) lines_since_frame = lines_since_frame + 1;
		if (vb_d) lines_since_frame = 1;
	end
	else if (hblank && !hb_d && !vblank) line_pixels = act_pixels;

	// capture the first 8 pixels of the frame and of line 1
	if (counting && !hblank && !vblank) begin
		if (vb_d) fpx = 0;
		if (fpx < 8 && lines_since_frame == 1) begin
			frame_px[fpx] <= gray;
			fpx = fpx + 1;
		end
		if (hb_d && lines_since_frame == 2) l1px = 0;
		if (l1px < 8 && lines_since_frame == 2) begin
			line1_px[l1px] <= gray;
			l1px = l1px + 1;
		end
	end
end

initial begin
	if ($test$plusargs("dump")) begin
		$dumpfile("build/tb_next_video.vcd");
		$dumpvars(0, tb_next_video);
	end

	// a bit more than 2 frames
	repeat (3100000) @(posedge clk);

	$display("");
	$display("=== tb_next_video results ===");
	$display("hsync period    = %0d cycles (expect 1600)", hs_period);
	$display("frame period    = %0d cycles (expect 1459200)", vbl_period);
	$display("active pixels   = %0d (expect 1120)", line_pixels);
	$display("active lines    = %0d (expect 832)", act_lines);
	$display("frame px 0..7   = %02x %02x %02x %02x %02x %02x %02x %02x",
	         frame_px[0],frame_px[1],frame_px[2],frame_px[3],
	         frame_px[4],frame_px[5],frame_px[6],frame_px[7]);
	$display("line1 px 0..7   = %02x %02x %02x %02x %02x %02x %02x %02x",
	         line1_px[0],line1_px[1],line1_px[2],line1_px[3],
	         line1_px[4],line1_px[5],line1_px[6],line1_px[7]);

	check(hs_period == 1600, "line period is 1600 cycles (62.5 kHz)");
	check(vbl_period == 1459200, "frame period is 912 lines (68.5 Hz)");
	check(line_pixels == 1120, "1120 active pixels per line");
	check(act_lines == 832, "832 active lines per frame");
	check(vbl_count >= 2, "vbl pulses seen");
	// 0x1BE4: pixels 00 01 10 11 11 10 01 00
	check(frame_px[0] == 8'hFF && frame_px[1] == 8'hAA &&
	      frame_px[2] == 8'h55 && frame_px[3] == 8'h00 &&
	      frame_px[4] == 8'h00 && frame_px[5] == 8'h55 &&
	      frame_px[6] == 8'hAA && frame_px[7] == 8'hFF,
	      "2bpp decode of word 0 (0x1BE4)");
	// 0xE41B: pixels 11 10 01 00 00 01 10 11
	check(line1_px[0] == 8'h00 && line1_px[1] == 8'h55 &&
	      line1_px[2] == 8'hAA && line1_px[3] == 8'hFF &&
	      line1_px[4] == 8'hFF && line1_px[5] == 8'hAA &&
	      line1_px[6] == 8'h55 && line1_px[7] == 8'h00,
	      "line pitch 288 bytes (word 144 starts line 1)");

	if (errors == 0) $display("ALL PASS");
	else             $display("%0d FAILURES", errors);
	$finish;
end

endmodule
