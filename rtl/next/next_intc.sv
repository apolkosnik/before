//============================================================================
//  NeXT interrupt controller
//
//  Interrupt status register at 0x02007000, interrupt mask register at
//  0x02007800.  Bit assignments and level mapping follow Previous
//  src/includes/sysReg.h and get_interrupt_level() in src/sysReg.c:
//
//    INT_L7_MASK 0xC0000000   INT_L6_MASK 0x3FFC0000
//    INT_L5_MASK 0x00038000   INT_L4_MASK 0x00004000
//    INT_L3_MASK 0x00003FFC   INT_L2_MASK 0x00000002
//    INT_L1_MASK 0x00000001
//
//  INT_TIMER (bit 29) is promoted to level 7 when SCR2 byte 2 bit 7
//  (SCR2_TIMERIPL7) is set, as in get_interrupt_level().
//
//  Devices request/release interrupts through the int_set/int_clr pulse
//  vectors (set wins over clear in the same cycle, matching the order in
//  Previous set_interrupt()).  The CPU may also write the status register
//  directly (IntRegStatWrite in sysReg.c replaces the register content).
//============================================================================

module next_intc
(
	input         clk,
	input         reset,

	// device request/release (one-cycle pulses)
	input  [31:0] int_set,
	input  [31:0] int_clr,

	// SCR2 byte 2 bit 7: timer interrupt at IPL 7
	input         timer_ipl7,

	// register access, one 16-bit half per cycle
	input         sel,           // access to this module
	input         reg_mask,      // 0 = status (0x7000), 1 = mask (0x7800)
	input         addr1,         // addr[1]: 0 = high half, 1 = low half
	input         we,
	input   [1:0] be,            // {upper byte, lower byte} lane enables
	input  [15:0] wdata,
	output [15:0] rdata,

	output  [2:0] ipl            // active high; CPU ipl pins are active low
);

reg [31:0] stat;
reg [31:0] mask;

wire [31:0] w32 = reg_mask ? mask : stat;
assign rdata = addr1 ? w32[15:0] : w32[31:16];

wire [15:0] wmerged_hi = {be[1] ? wdata[15:8] : w32[31:24], be[0] ? wdata[7:0] : w32[23:16]};
wire [15:0] wmerged_lo = {be[1] ? wdata[15:8] : w32[15:8],  be[0] ? wdata[7:0] : w32[7:0]};

always @(posedge clk) begin
	if (reset) begin
		stat <= 32'd0;
		mask <= 32'd0;
	end
	else begin
		// device release then request, then CPU write overrides
		stat <= (stat & ~int_clr) | int_set;

		if (sel & we) begin
			if (reg_mask) begin
				if (addr1) mask[15:0]  <= wmerged_lo;
				else       mask[31:16] <= wmerged_hi;
			end
			else begin
				if (addr1) stat[15:0]  <= wmerged_lo;
				else       stat[31:16] <= wmerged_hi;
			end
		end
	end
end

// level encoder, from get_interrupt_level() in Previous src/sysReg.c
wire [31:0] pend = stat & mask;

assign ipl = (pend & 32'hC0000000)              ? 3'd7 :
             (pend[29] & timer_ipl7)            ? 3'd7 :
             (pend & 32'h3FFC0000)              ? 3'd6 :
             (pend & 32'h00038000)              ? 3'd5 :
             (pend & 32'h00004000)              ? 3'd4 :
             (pend & 32'h00003FFC)              ? 3'd3 :
             (pend & 32'h00000002)              ? 3'd2 :
             (pend & 32'h00000001)              ? 3'd1 : 3'd0;

endmodule
