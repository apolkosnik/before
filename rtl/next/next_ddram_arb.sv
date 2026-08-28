//============================================================================
//  DDRAM port arbiter: main RAM adapter (port A, pass-through with the
//  MiSTer DDRAM handshake) and the ethernet bridge mailbox (port B,
//  simple req/ack), serialized onto the single DDRAM interface.
//
//  Both masters issue single-beat operations.  Port A has priority;
//  while port B owns the bus, port A sees BUSY and holds its request,
//  which is exactly the DDRAM contract next_ddram already follows.
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

	// port B: bridge mailbox (req/ack, full-word ops)
	input             b_req,
	input             b_we,
	input      [28:0] b_addr,
	input      [63:0] b_wdata,
	output     [63:0] b_rdata,
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

localparam G_A = 2'd0, G_B_ISSUE = 2'd1, G_B_READ = 2'd2;

reg [1:0] gst;
reg       a_read_pending;

// port A owns the bus by default; B gets it only when A is idle
wire a_active = a_rd | a_we | a_read_pending;
wire b_owns   = (gst != G_A);

assign DDRAM_ADDR     = b_owns ? b_addr : a_addr;
assign DDRAM_DIN      = b_owns ? b_wdata : a_din;
assign DDRAM_BE       = b_owns ? 8'hFF : a_be;
assign DDRAM_BURSTCNT = b_owns ? 8'd1 : a_burst;
assign DDRAM_RD       = b_owns ? (gst == G_B_ISSUE && !b_we) : a_rd;
assign DDRAM_WE       = b_owns ? (gst == G_B_ISSUE &&  b_we) : a_we;

assign a_busy         = b_owns | DDRAM_BUSY;
assign a_dout         = DDRAM_DOUT;
assign a_dout_ready   = DDRAM_DOUT_READY & !b_owns;
assign b_rdata        = DDRAM_DOUT;

always @(posedge clk) begin
	b_ack <= 0;

	if (reset) begin
		gst <= G_A;
		a_read_pending <= 0;
	end
	else begin
		// track port A's outstanding read so B never steals the bus
		// between acceptance and data return
		if (!b_owns) begin
			if (a_rd && !DDRAM_BUSY) a_read_pending <= 1;
			if (DDRAM_DOUT_READY) a_read_pending <= 0;
		end

		case (gst)
		G_A: begin
			if (b_req && !b_ack && !a_active) gst <= G_B_ISSUE;
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
				b_ack <= 1;
				gst <= G_A;
			end
		end
		default: gst <= G_A;
		endcase
	end
end

endmodule
