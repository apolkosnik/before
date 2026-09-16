// Diagnostic-only, first-event latch and DDR mailbox publisher.
// HPS physical 0x1ff05000 (Avalon 64-bit word 0x03fe0a00), outside
// Ethernet's header and rings [0x1ff00000, 0x1ff04800).  No guest RAM writes.
// +0: magic NXTDIAG1, +8: captured (0/1), +16..79: eight payload words.
// Reset invalidates the old magic, clears captured, then publishes magic.
// Payload is immutable after the first event; captured=1 is written LAST.
// This is independent of the Ethernet enable and never backpressures CPU
// execution.  Only the low-priority DDR mailbox port is shared.
module next_exception_mailbox #(
	// The unhandled-trap selector already holds its complete output until
	// reset. Avoid a second wide payload register in that configuration.
	// Default remains a self-contained first-event snapshot for other taps.
	parameter INPUT_HELD = 0
) (
	input clk, reset,
	input event_valid,
	input [511:0] event_data,
	input b_req, b_we,
	input [28:0] b_addr,
	input [63:0] b_wdata,
	output [63:0] b_rdata,
	output b_ack,
	output m_req, m_we,
	output [28:0] m_addr,
	output [63:0] m_wdata,
	input [63:0] m_rdata,
	input m_ack
);
localparam [28:0] BASE = 29'h03fe0a00;
localparam [63:0] MAGIC = 64'h4e58544449414731;
localparam [1:0] IDLE = 0, BRIDGE = 1, DIAG = 2, GAP = 3;
reg [1:0] state;
reg [3:0] step;
reg captured;
wire [511:0] saved;
generate if (INPUT_HELD) begin : g_held_input
	assign saved = event_data;
end else begin : g_snapshot
	reg [511:0] snapshot;
	always @(posedge clk)
		if (!reset && event_valid && !captured) snapshot <= event_data;
	assign saved = snapshot;
end endgenerate

wire diag_pending = step < 4'd3 || (captured && step < 4'd12);
wire [2:0] payload_index = step[2:0] - 3'd3;
wire [28:0] diag_addr = (step == 0 || step == 2) ? BASE :
	(step == 1 || step == 11) ? BASE + 29'd1 :
	BASE + 29'd2 + {26'd0, payload_index};
wire [63:0] diag_data = step < 2 ? 64'd0 : step == 2 ? MAGIC :
	step == 11 ? 64'd1 : saved[{payload_index, 6'd0} +: 64];

assign m_req = !reset && ((state == BRIDGE && b_req) || state == DIAG);
assign m_we = state == DIAG || b_we;
assign m_addr = state == DIAG ? diag_addr : b_addr;
assign m_wdata = state == DIAG ? diag_data : b_wdata;
assign b_ack = !reset && state == BRIDGE && m_ack;
assign b_rdata = m_rdata;

always @(posedge clk) begin
	if (reset) begin
		state <= IDLE;
		step <= 0;
		captured <= 0;
	end else begin
		if (event_valid && !captured) begin
			captured <= 1;
		end
		case (state)
			IDLE: begin
				if (diag_pending) state <= DIAG;
				else if (b_req) state <= BRIDGE;
			end
			BRIDGE: if (m_ack) state <= GAP;
			DIAG: if (m_ack) begin
				step <= step + 1'd1;
				state <= GAP;
			end
			// Let a level req/ack transaction retire before changing owner.
			GAP: if (!m_ack) state <= IDLE;
		endcase
	end
end
endmodule
