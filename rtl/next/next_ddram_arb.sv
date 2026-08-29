//============================================================================
//  DDRAM port arbiter: main RAM adapter (port A, pass-through with the
//  MiSTer DDRAM handshake) and the ethernet bridge mailbox (port B,
//  simple req/ack), serialized onto the single DDRAM interface.
//
//  Ports A and B issue single-beat operations.  Port V is the colour
//  frame buffer scan-out: burst reads that must not be starved, because
//  a late line shows as a torn display, so it is served ahead of A and
//  B once its request is up.  Its bursts are short (16 words) and only
//  needed while a colour machine is displaying, so the interruption to
//  the CPU is bounded.
//
//  While another port owns the bus, port A sees BUSY and holds its
//  request, which is exactly the DDRAM contract next_ddram follows.
//============================================================================

module next_ddram_arb
(
	input             clk,
	input             reset,

	// port A: next_ddram (DDRAM-shaped)
	input             a_rd,
	input             a_we,
	input      [28:0] a_addr,
	input      [63:0] a_din,
	input       [7:0] a_be,
	input       [7:0] a_burst,
	output            a_busy,
	output     [63:0] a_dout,
	output            a_dout_ready,

	// port V: colour frame buffer scan-out (burst reads)
	input             v_req,
	input      [28:0] v_addr,
	input       [7:0] v_burst,
	output            v_ack,
	output     [63:0] v_data,
	output            v_data_valid,

	// port B: bridge mailbox (req/ack, full-word ops)
	input             b_req,
	input             b_we,
	input      [28:0] b_addr,
	input      [63:0] b_wdata,
	output reg [63:0] b_rdata,
	output reg        b_ack,

	// DDRAM
	input             DDRAM_BUSY,
	output      [7:0] DDRAM_BURSTCNT,
	output     [28:0] DDRAM_ADDR,
	input      [63:0] DDRAM_DOUT,
	input             DDRAM_DOUT_READY,
	output            DDRAM_RD,
	output     [63:0] DDRAM_DIN,
	output      [7:0] DDRAM_BE,
	output            DDRAM_WE
);

localparam G_A = 3'd0, G_B_ISSUE = 3'd1, G_B_READ = 3'd2,
           G_V_ISSUE = 3'd3, G_V_READ = 3'd4;

reg [2:0] gst;
reg       a_read_pending;
reg [7:0] v_left;          // words still expected from the current burst

// port A owns the bus by default; the others take it only when A is idle
wire a_active = a_rd | a_we | a_read_pending;
wire b_owns   = (gst == G_B_ISSUE) || (gst == G_B_READ);
wire v_owns   = (gst == G_V_ISSUE) || (gst == G_V_READ);
wire busy_own = b_owns | v_owns;

assign DDRAM_ADDR     = v_owns ? v_addr  : b_owns ? b_addr  : a_addr;
assign DDRAM_DIN      = b_owns ? b_wdata : a_din;
assign DDRAM_BE       = busy_own ? 8'hFF : a_be;
assign DDRAM_BURSTCNT = v_owns ? v_burst : b_owns ? 8'd1 : a_burst;
assign DDRAM_RD       = v_owns ? (gst == G_V_ISSUE) :
                        b_owns ? (gst == G_B_ISSUE && !b_we) : a_rd;
assign DDRAM_WE       = b_owns ? (gst == G_B_ISSUE &&  b_we) : a_we;

assign a_busy         = busy_own | DDRAM_BUSY;
assign a_dout         = DDRAM_DOUT;
assign a_dout_ready   = DDRAM_DOUT_READY & !busy_own;

assign v_ack          = (gst == G_V_ISSUE) && !DDRAM_BUSY;
assign v_data         = DDRAM_DOUT;
assign v_data_valid   = (gst == G_V_READ) && DDRAM_DOUT_READY;

always @(posedge clk) begin
	b_ack <= 0;

	if (reset) begin
		gst <= G_A;
		a_read_pending <= 0;
		v_left <= 0;
		b_rdata <= 0;
	end
	else begin
		// track port A's outstanding read so B never steals the bus
		// between acceptance and data return
		if (!busy_own) begin
			if (a_rd && !DDRAM_BUSY) a_read_pending <= 1;
			if (DDRAM_DOUT_READY) a_read_pending <= 0;
		end

		case (gst)
		G_A: begin
			// the display comes first: a starved scan-out tears the
			// picture, while the CPU only waits
			if (v_req && !a_active) gst <= G_V_ISSUE;
			else if (b_req && !b_ack && !a_active) gst <= G_B_ISSUE;
		end
		G_V_ISSUE: begin
			if (!DDRAM_BUSY) begin
				v_left <= v_burst;
				gst    <= G_V_READ;
			end
		end
		G_V_READ: begin
			if (DDRAM_DOUT_READY) begin
				v_left <= v_left - 1'd1;
				if (v_left == 8'd1) gst <= G_A;
			end
		end
		G_B_ISSUE: begin
			if (!DDRAM_BUSY) begin
				if (b_we) begin
					b_ack <= 1;
					gst <= G_A;
				end
				else gst <= G_B_READ;
			end
		end
		G_B_READ: begin
			if (DDRAM_DOUT_READY) begin
				// Avalon only guarantees DDRAM_DOUT in the ready cycle.
				// Port B observes the registered ack after ownership has
				// returned to A, so retain the word explicitly.
				b_rdata <= DDRAM_DOUT;
				b_ack <= 1;
				gst <= G_A;
			end
		end
		default: gst <= G_A;
		endcase
	end
end

endmodule
