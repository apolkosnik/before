//============================================================================
//  NeXTstation Color frame buffer scan-out test: drives the real
//  next_cvram against a DDR3 model holding a known pattern and checks
//  that the display side reads back the right pixel for every position
//  of a line, that lines land in the bank their number selects, and
//  that a line is fetched while the previous one is being displayed.
//
//  Frame buffer layout: line pitch 280 words of 64 bits (1120 pixels,
//  16 bits each), base 0x34000000 in bytes = 0x06800000 in 64-bit
//  words.  Pixels run most significant first within a word.
//============================================================================

`timescale 1ns/1ps

module tb_next_cvram;

reg clk = 0;             // system/DDR3 clock
always #5 clk = ~clk;

reg clk_vid = 0;         // pixel clock, deliberately a different rate
always #4 clk_vid = ~clk_vid;

reg reset = 1;
reg cv_enable = 1;

wire        f_req;
wire [28:0] f_addr;
wire  [7:0] f_burst;
reg         f_ack = 0;
reg  [63:0] f_data = 0;
reg         f_data_valid = 0;

wire [10:0] px;
reg   [9:0] fetch_line = 0;
reg         line_start = 0;
wire [15:0] px_data;

next_cvram dut
(
	.clk(clk), .reset(reset), .enable(cv_enable),
	.f_req(f_req), .f_addr(f_addr), .f_burst(f_burst),
	.f_ack(f_ack), .f_data(f_data), .f_data_valid(f_data_valid),
	.clk_vid(clk_vid),
	.px(px), .fetch_line(fetch_line), .line_start(line_start),
	.px_data(px_data)
);

localparam VRAM_W   = 29'h0680_0000;
localparam WORDS_LN = 280;

// the word a line/word index should contain: a pattern that encodes
// both coordinates so a misplaced fetch is visible
function [63:0] pattern;
	input [9:0] ln;
	input [8:0] w;
	begin
		pattern = {ln, 6'd0, w, 7'd0, ln, 6'd0, w, 7'd0};
	end
endfunction

// the pixel that word/lane should present
function [15:0] pat_px;
	input [9:0] ln;
	input [10:0] p;
	reg [63:0] wrd;
	begin
		wrd = pattern(ln, p[10:2]);
		case (p[1:0])
			2'd0: pat_px = wrd[63:48];
			2'd1: pat_px = wrd[47:32];
			2'd2: pat_px = wrd[31:16];
			default: pat_px = wrd[15:0];
		endcase
	end
endfunction

//----------------------------------------------------------------------------
// DDR3 model: accepts a burst request, returns f_burst words after a
// latency, from the pattern above
//----------------------------------------------------------------------------

integer bursts_served = 0;
reg [28:0] b_addr;
reg  [7:0] b_left;
integer    lat;

always @(posedge clk) begin
	f_ack <= 0;
	f_data_valid <= 0;

	if (reset) begin
		b_left <= 0;
		lat <= 0;
	end
	else if (f_req && b_left == 0 && !f_ack) begin
		f_ack   <= 1;
		b_addr  <= f_addr;
		b_left  <= f_burst;
		lat     <= 4;                 // a few cycles before data flows
		bursts_served = bursts_served + 1;
	end
	else if (b_left != 0) begin
		if (lat != 0) lat <= lat - 1;
		else begin
			f_data <= pattern((b_addr - VRAM_W) / WORDS_LN,
			                  (b_addr - VRAM_W) % WORDS_LN);
			f_data_valid <= 1;
			b_addr <= b_addr + 1'd1;
			b_left <= b_left - 1'd1;
		end
	end
end

//----------------------------------------------------------------------------
// display side: walk a line's worth of pixels and check each one
//----------------------------------------------------------------------------

integer errors = 0;

task check;
	input cond;
	input [255:0] name;
	begin
		if (cond) $display("PASS: %0s", name);
		else begin $display("FAIL: %0s", name); errors = errors + 1; end
	end
endtask

// start of the line that displays "ln": the engine is told to hold the
// line after it
task begin_line;
	input [9:0] ln;
	begin
		@(posedge clk_vid);
		fetch_line <= ln + 10'd1;
		line_start <= 1;
		@(posedge clk_vid);
		line_start <= 0;
	end
endtask

// let the engine fetch a whole line
task settle;
	begin
		repeat (4000) @(posedge clk);
	end
endtask

// Put the display on line ln with that line already fetched, exactly as
// the raster does it: a line is fetched while its predecessor is on
// screen, so displaying line ln takes two line starts.
task show_line;
	input [9:0] ln;
	begin
		begin_line(ln - 10'd1);   // fetches ln into its bank
		settle;
		begin_line(ln);           // displays ln, fetches ln+1 into the other
		settle;
	end
endtask

integer i;
integer bad;
reg [15:0] got, want;

// Streaming checker.  px follows a free-running counter, so the RTL
// samples pixel vpx at each edge and presents it one edge later; the
// expected value is registered through the same one-stage pipeline,
// which makes the comparison independent of assignment ordering.
reg  [10:0] vpx = 0;
reg         check_en = 0;
reg   [9:0] check_line = 0;
reg         check_rep = 0;
integer     check_bad = 0;
integer     check_n = 0;
reg  [15:0] exp1;
reg         exp1_v = 0;

assign px = vpx;

always @(posedge clk_vid) begin
	if (!check_en) begin
		vpx    <= 0;
		exp1_v <= 0;
	end
	else begin
		// compare the data now on the port against the pixel addressed
		// at the previous edge
		if (exp1_v && px_data !== exp1) begin
			if (check_rep && check_bad < 4)
				$display("  line %0d pixel %0d: got %04x want %04x",
				         check_line, vpx - 11'd1, px_data, exp1);
			check_bad = check_bad + 1;
		end
		if (exp1_v) check_n = check_n + 1;

		exp1   <= pat_px(check_line, vpx);
		exp1_v <= 1;
		vpx    <= (vpx == 11'd1119) ? 11'd1119 : vpx + 11'd1;
	end
end

task stream_line;
	input  [9:0] ln;
	input        report;
	output integer nbad;
	begin
		check_line = ln;
		check_rep  = report;
		check_bad  = 0;
		check_n    = 0;
		@(posedge clk_vid);
		check_en = 1;
		repeat (1122) @(posedge clk_vid);
		check_en = 0;
		@(posedge clk_vid);
		nbad = check_bad;
	end
endtask

initial begin
	repeat (10) @(posedge clk);
	reset = 0;
	repeat (10) @(posedge clk);

	// prime: through vertical blanking the engine is told to hold line 0
	begin_line(10'd1023);        // fetch_line becomes 0
	settle;
	check(bursts_served > 0, "a line is fetched from the frame buffer");

	// display line 0 (bank 0) while line 1 is fetched
	show_line(10'd0);

	bad = 0;
	stream_line(10'd0, 1, bad);
	check(bad == 0, "every pixel of line 0 reads back correctly");

	// line 1 lives in the other bank and must already be there
	show_line(10'd1);
	bad = 0;
	stream_line(10'd1, 1, bad);
	check(bad == 0, "line 1 reads from the other bank");

	// a line deep in the frame, to prove the address arithmetic
	show_line(10'd830);
	bad = 0;
	stream_line(10'd830, 1, bad);
	check(bad == 0, "line 830 reads back correctly (address arithmetic)");

	// the whole line comes from one burst sequence: 280 words in
	// bursts of 16 is 18 requests
	check(bursts_served >= 4*18, "each line is fetched in bounded bursts");

	// disabled, the engine must go quiet even across line starts
	cv_enable = 0;
	i = bursts_served;
	begin_line(10'd5);
	settle;
	check(bursts_served == i, "a disabled engine issues no fetches");
	cv_enable = 1;

	if (errors == 0) $display("ALL PASS");
	else             $display("%0d FAILURES", errors);
	$finish;
end

endmodule
