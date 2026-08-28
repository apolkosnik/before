//============================================================================
//  True dual-port RAM, synthesis-friendly replacement for the AP68040
//  primitives/dpram.v (same ports and parameters).
//
//  The AP68040 README says to substitute a vendor-mappable RAM in the
//  host project: its own plain model (single always block, old-data
//  read-during-write on both ports) does not map to Cyclone V M10K true
//  dual port and falls into registers.  This is the standard Intel true
//  dual-port inference template: per-port write-first, one always block
//  per port.  The Minimig-AGA integration of the same CPU runs with
//  these semantics on real hardware.
//
//  Used by the testbenches as well, so simulation and synthesis see the
//  same read-during-write behavior.
//============================================================================

module dpram #(parameter AW = 8, parameter DW = 8) (
	input clock,
	input [AW-1:0] address_a,
	input [DW-1:0] data_a,
	input wren_a,
	output reg [DW-1:0] q_a,
	input [AW-1:0] address_b,
	input [DW-1:0] data_b,
	input wren_b,
	output reg [DW-1:0] q_b
);
	reg [DW-1:0] mem [0:(1<<AW)-1];

	always @(posedge clock) begin
		if (wren_a) begin
			mem[address_a] <= data_a;
			q_a <= data_a;
		end
		else q_a <= mem[address_a];
	end

	always @(posedge clock) begin
		if (wren_b) begin
			mem[address_b] <= data_b;
			q_b <= data_b;
		end
		else q_b <= mem[address_b];
	end
endmodule

//----------------------------------------------------------------------------
// Dual-clock variant: port A and port B each have their own clock.
// Same inference template; used where a memory crosses clock domains
// (the VRAM scan-out port runs in the video clock domain).
//----------------------------------------------------------------------------

module dpram_dc #(parameter AW = 8, parameter DW = 8) (
	input clock_a,
	input [AW-1:0] address_a,
	input [DW-1:0] data_a,
	input wren_a,
	output reg [DW-1:0] q_a,
	input clock_b,
	input [AW-1:0] address_b,
	input [DW-1:0] data_b,
	input wren_b,
	output reg [DW-1:0] q_b
);
	reg [DW-1:0] mem [0:(1<<AW)-1];

	always @(posedge clock_a) begin
		if (wren_a) begin
			mem[address_a] <= data_a;
			q_a <= data_a;
		end
		else q_a <= mem[address_a];
	end

	always @(posedge clock_b) begin
		if (wren_b) begin
			mem[address_b] <= data_b;
			q_b <= data_b;
		end
		else q_b <= mem[address_b];
	end
endmodule
