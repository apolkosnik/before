// Passive Mach 2.0 mk-94 unhandled-trap selector. No CPU backpressure.
// Store candidate/return events in block RAM, then search newest-first for
// the exact (URP, exception-frame address) at kernel exception delivery.
// A matching return is a tombstone: never resurrect an older reused frame.
// Eviction or missing context is published explicitly, not guessed.
module next_exception_trigger (
	input clk, reset,
	input event_valid,
	input [511:0] event_data,
	output reg capture_valid,
	output [511:0] capture_data
);
localparam [1:0] IDLE = 0, WAIT_READ = 1, COMPARE = 2, DONE = 3;
reg [1:0] state;
(* ramstyle = "M10K" *) reg [255:0] history [0:63];
reg [255:0] read_data;
reg [5:0] wr_index, rd_index;
reg [6:0] count, remaining;
reg [255:0] trigger_record;
reg [3:0] match_status;
// The RAM read address freezes on completion. Reuse its registered output
// instead of duplicating the selected payload in scarce logic registers.
wire [255:0] selected_record = match_status == 1 ? read_data :
	{64'd0, trigger_record[191:96], 96'd0};
wire [255:0] compact = {event_data[511:448], event_data[191:0]};
wire [1:0] event_class = event_data[495:494];
wire supported = event_data[511:496] == 16'd3;
wire matching = read_data[191:160] == trigger_record[191:160] &&
	read_data[127:96] == trigger_record[127:96];

// Synchronous RAM read, with no array reset so Quartus can infer M10Ks.
always @(posedge clk) begin
	read_data <= history[rd_index];
	if (!reset && state == IDLE && event_valid && supported && !event_class[1])
		history[wr_index] <= compact;
end

always @(posedge clk) begin
	if (reset) begin
		state <= IDLE;
		wr_index <= 0;
		rd_index <= 0;
		count <= 0;
		remaining <= 0;
		capture_valid <= 0;
		match_status <= 0;
	end else begin
		capture_valid <= 0;
		case (state)
			IDLE: if (event_valid && supported) begin
				if (!event_class[1]) begin
					wr_index <= wr_index + 1'b1;
					if (count != 7'd64) count <= count + 1'b1;
				end else if (event_class == 2'd2) begin
					trigger_record <= compact;
					rd_index <= wr_index - 1'b1;
					remaining <= count;
					if (compact[237:236] == 2'd3 || count == 0) begin
						match_status <= compact[237:236] == 2'd3 ? 4'd4 : 4'd3;
						capture_valid <= 1;
						state <= DONE;
					end else state <= WAIT_READ;
				end
			end
			WAIT_READ: state <= COMPARE;
			COMPARE: begin
				if (matching) begin
					match_status <= read_data[239:238] == 2'd0 ? 4'd1 : 4'd2;
					capture_valid <= 1;
					state <= DONE;
				end else if (remaining == 1) begin
					match_status <= 4'd3;
					capture_valid <= 1;
					state <= DONE;
				end else begin
					rd_index <= rd_index - 1'b1;
					remaining <= remaining - 1'b1;
					state <= WAIT_READ;
				end
			end
			DONE: ; // Preserve the first delivery, ignoring later traffic.
		endcase
	end
end

// Final class 3, delivery reason, explicit match status, kernel D2 flags,
// original frame format/vector. Word 3 adds delivery kernel SP / PC.
assign capture_data = {
	16'd3, 2'd3, trigger_record[237:236], 8'd0, match_status,
	trigger_record[223:208], 4'd0, selected_record[203:192],
	192'd0, trigger_record[63:0], selected_record[191:0]
};
endmodule
