//============================================================================
//  NeXTstation Color frame buffer scan-out.
//
//  1120 x 832 at 16 bits per pixel is 1.8 MB, far more than the block
//  RAM on this device, so the frame buffer lives in DDR3 (its own 2 MB
//  window at byte 0x34000000, see next_ddram.sv) and the display is fed
//  from it one line at a time.
//
//  Two line buffers: while the raster reads one, the fetch engine fills
//  the other with the next line by 64-bit DDR3 bursts.  A line is 1120
//  pixels = 2240 bytes = 280 words of 64 bits; a colour slab's line
//  pitch is the visible width, with none of the padding the monochrome
//  MegaPixel has.
//
//  Which buffer holds a line is decided by the line number itself (line
//  N lives in bank N[0]), so the two clock domains need no handshake
//  about ownership and a missed or late fetch cannot leave the display
//  reading a buffer the engine is writing.  Lines are fetched one ahead;
//  through vertical blanking the engine fetches line 0, so the first
//  visible line is always primed.
//============================================================================

module next_cvram
(
	// fetch side (system clock, DDR3)
	input             clk,
	input             reset,
	// a monochrome machine must issue no fetch traffic at all: the
	// scan-out port outranks the CPU at the DDR3 arbiter and a line's
	// fetch is most of the 28 MHz port's bandwidth, so an ungated
	// engine starves the CPU out of booting
	input             enable,

	output reg        f_req,        // burst read request
	output reg [28:0] f_addr,       // 64-bit word address
	output reg  [7:0] f_burst,      // words in this burst
	input             f_ack,        // request accepted this cycle
	input      [63:0] f_data,
	input             f_data_valid,

	// display side (pixel clock)
	input             clk_vid,
	input      [10:0] px,           // pixel within the line (0..1119)
	input       [9:0] fetch_line,   // line the engine should hold next
	input             line_start,   // one pulse per line, on clk_vid
	output     [15:0] px_data       // pixel px, two clk_vid cycles late
);

localparam H_ACT    = 11'd1120;
localparam WORDS_LN = 9'd280;             // 1120 * 2 / 8
localparam BURST    = 8'd16;              // words per DDR3 burst
localparam VRAM_W   = 29'h0680_0000;      // 0x34000000 in 64-bit words

//----------------------------------------------------------------------------
// line buffers, on the dual-clock block RAM template used elsewhere in
// this core (an inferred array this wide would land in registers and
// not fit)
//----------------------------------------------------------------------------

reg  [8:0] w_idx;      // index the next received word belongs to
reg  [8:0] w_addr;     // index of the word currently being written
reg        w_bank;
reg [63:0] w_data;
reg        w_en;

wire [8:0] r_idx = px[10:2];
wire [63:0] q0, q1;

dpram_dc #(9, 64) lbuf0
(
	.clock_a(clk),     .address_a(w_addr), .data_a(w_data),
	.wren_a(w_en & ~w_bank), .q_a(),
	.clock_b(clk_vid), .address_b(r_idx), .data_b(64'd0),
	.wren_b(1'b0),     .q_b(q0)
);

dpram_dc #(9, 64) lbuf1
(
	.clock_a(clk),     .address_a(w_addr), .data_a(w_data),
	.wren_a(w_en & w_bank), .q_a(),
	.clock_b(clk_vid), .address_b(r_idx), .data_b(64'd0),
	.wren_b(1'b0),     .q_b(q1)
);

// The bank a line lives in is the line number's low bit, so the read
// side needs no handshake with the fetch engine.  q0/q1 are registered
// by the RAM, so the selects are delayed to match.
reg       r_bank = 0;      // bank of the line being displayed
reg       r_bank_d;
reg [1:0] r_sel_d;

always @(posedge clk_vid) begin
	if (line_start) r_bank <= ~fetch_line[0];   // displaying fetch_line-1
	r_bank_d <= r_bank;
	r_sel_d  <= px[1:0];
end

// four pixels per 64-bit word, most significant first: the frame buffer
// is big endian like the rest of the machine
wire [63:0] r_word = r_bank_d ? q1 : q0;
assign px_data = (r_sel_d == 2'd0) ? r_word[63:48] :
                 (r_sel_d == 2'd1) ? r_word[47:32] :
                 (r_sel_d == 2'd2) ? r_word[31:16] : r_word[15:0];

//----------------------------------------------------------------------------
// line-start and the wanted line, crossed into the fetch domain
//----------------------------------------------------------------------------

reg       ls_tog = 0;
reg [9:0] want_line = 0;

always @(posedge clk_vid) begin
	if (line_start) begin
		want_line <= fetch_line;
		ls_tog    <= ~ls_tog;
	end
end

reg [2:0] ls_sync;
reg [9:0] line_s0, line_s1;
always @(posedge clk) begin
	ls_sync <= {ls_sync[1:0], ls_tog};
	line_s0 <= want_line;
	line_s1 <= line_s0;
end
wire new_line = ls_sync[2] ^ ls_sync[1];

//----------------------------------------------------------------------------
// fetch engine
//----------------------------------------------------------------------------

localparam F_IDLE = 2'd0, F_REQ = 2'd1, F_DATA = 2'd2;
reg  [1:0] fst;
reg  [8:0] f_count;      // words still owed for this line
reg  [8:0] f_got;        // words received in the current burst
reg  [7:0] f_this;       // size of the current burst

// first word of a line: line * 280, in 64-bit words
wire [28:0] line_base = VRAM_W + ({19'd0, line_s1} * WORDS_LN);

task automatic start_line;
	begin
		f_addr  <= line_base;
		w_idx   <= 0;
		w_bank  <= line_s1[0];
		f_count <= WORDS_LN;
		fst     <= F_REQ;
	end
endtask

always @(posedge clk) begin
	if (reset) begin
		fst     <= F_IDLE;
		f_req   <= 0;
		f_addr  <= 0;
		f_burst <= 0;
		f_count <= 0;
		f_got   <= 0;
		f_this  <= 0;
		w_idx   <= 0;
		w_addr  <= 0;
		w_bank  <= 0;
		w_en    <= 0;
	end
	else begin
		w_en <= 0;

		if (!enable) begin
			f_req <= 0;
			fst   <= F_IDLE;
		end
		// a new line always restarts the engine: the display has moved
		// on and whatever remained of the previous line no longer matters
		else if (new_line) begin
			f_req <= 0;
			start_line;
		end
		else case (fst)
		F_IDLE: ;

		F_REQ: begin
			if (f_count == 0) fst <= F_IDLE;
			else begin
				f_this  <= (f_count >= {1'b0, BURST}) ? BURST : f_count[7:0];
				f_burst <= (f_count >= {1'b0, BURST}) ? BURST : f_count[7:0];
				f_req   <= 1;
				f_got   <= 0;
				if (f_req && f_ack) begin
					f_req <= 0;
					fst   <= F_DATA;
				end
			end
		end

		F_DATA: begin
			if (f_data_valid) begin
				w_data <= f_data;
				w_addr <= w_idx;      // travels with the data it belongs to
				w_en   <= 1;
				w_idx  <= w_idx + 1'd1;
				f_got  <= f_got + 1'd1;
				if (f_got + 1'd1 == {1'b0, f_this}) begin
					f_addr  <= f_addr + {21'd0, f_this};
					f_count <= f_count - {1'b0, f_this};
					fst     <= F_REQ;
				end
			end
		end

		default: fst <= F_IDLE;
		endcase
	end
end

endmodule
