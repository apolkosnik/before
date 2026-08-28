//============================================================================
//  NeXT internal hardclock timer
//
//  Registers at 0x02016000 (byte 0, latch high), 0x02016001 (byte 1,
//  latch low) and 0x02016004 (CSR).  Behavior follows the Hardclock*
//  handlers in Previous src/sysReg.c:
//
//    CSR bit 7 (0x80) enable
//    CSR bit 6 (0x40) latch: transfers {reg0,reg1} into the period latch,
//                     self clearing
//
//  The period is in microseconds.  While enabled with a nonzero latch the
//  timer raises INT_TIMER every 'latch' microseconds.  Reading the CSR
//  releases INT_TIMER (HardclockReadCSR calls
//  set_interrupt(INT_TIMER,RELEASE_INT)).
//============================================================================

module next_timer #(parameter CLK_HZ = 100000000)
(
	input         clk,
	input         reset,

	// register access
	input         sel,
	input   [2:0] addr,          // byte address 0x0/0x1/0x4 within block
	input         we,
	input   [1:0] be,            // {even byte, odd byte} lanes
	input  [15:0] wdata,
	output [15:0] rdata,
	input         rd,            // qualified read strobe (for CSR side effect)

	output reg    int_set,       // one-cycle pulse: raise INT_TIMER
	output reg    int_clr        // one-cycle pulse: release INT_TIMER
);

localparam US_DIV = CLK_HZ / 1000000;

reg  [7:0] hc0, hc1;            // write staging registers
reg  [7:0] csr;
reg [15:0] latch_val;

// microsecond prescaler
reg [$clog2(US_DIV)-1:0] presc;
wire us_tick = (presc == US_DIV-1);

reg [15:0] us_count;

// word address 0x02016000: bytes 0 (even) and 1 (odd)
// word address 0x02016004: byte 4 (even)
assign rdata = (addr[2]) ? {csr, 8'h00} : {latch_val[15:8], latch_val[7:0]};

always @(posedge clk) begin
	int_set <= 0;
	int_clr <= 0;

	if (reset) begin
		hc0 <= 0; hc1 <= 0; csr <= 0;
		latch_val <= 0;
		presc <= 0;
		us_count <= 0;
	end
	else begin
		presc <= us_tick ? 1'd0 : presc + 1'd1;

		if (us_tick && csr[7] && latch_val != 0) begin
			if (us_count >= latch_val-1) begin
				us_count <= 0;
				int_set <= 1;
			end
			else us_count <= us_count + 1'd1;
		end

		if (sel & we) begin
			if (!addr[2]) begin
				if (be[1]) hc0 <= wdata[15:8];
				if (be[0]) hc1 <= wdata[7:0];
			end
			else if (be[1]) begin
				// HardclockWriteCSR: bit 6 loads the latch and self-clears
				csr <= wdata[15:8] & 8'hBF;
				if (wdata[14]) begin
					latch_val <= {hc0, hc1};
					us_count <= 0;
				end
			end
		end

		// HardclockReadCSR releases INT_TIMER
		if (sel & rd & addr[2]) int_clr <= 1;
	end
end

endmodule
