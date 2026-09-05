//============================================================================
//  NeXT BMAP chip, 16 x 32-bit registers at 0x020C0000 (mask 0x3F,
//  decoded over a 64 KB window as in Previous memory.c)
//
//  Modeled on Previous src/bmap.c: plain read/write registers, except
//  register 0xD where bit 29 (BMAP_HEARTBEAT) reads as 1 when no
//  twisted-pair ethernet transceiver is connected, and bit 28
//  (BMAP_TPE_ILBC) reads as 1 when the external twisted-pair link is
//  present.
//============================================================================

module next_bmap
(
	input         clk,
	input         reset,

	input         sel,
	input   [5:0] addr,          // byte address within the register file
	input         we,
	input   [1:0] be,
	input  [15:0] wdata,
	output [15:0] rdata,

	input         tpe_link,

	// bmap_tpe_select in bmap.c: set when both TPE bits (31 and 28) of
	// register 0xD are written set, cleared when both are written clear
	output reg    tpe_select
);

reg [31:0] regs [0:15];

wire [3:0] ridx = addr[5:2];

wire [31:0] rval_raw = regs[ridx];
wire [31:0] rval_d = (rval_raw & ~32'h30000000) |
                     (tpe_link ? 32'h10000000 : 32'h20000000);
wire [31:0] rval = (ridx == 4'hD) ? rval_d : rval_raw;

assign rdata = addr[1] ? rval[15:0] : rval[31:16];

integer i;

always @(posedge clk) begin
	if (reset) begin
		for (i = 0; i < 16; i = i + 1) regs[i] <= 32'd0;
		tpe_select <= 0;
	end
	else if (sel & we) begin
		if (!addr[1]) begin
			if (be[1]) regs[ridx][31:24] <= wdata[15:8];
			if (be[0]) regs[ridx][23:16] <= wdata[7:0];
			// TPE bits live in the top byte of register 0xD
			if (be[1] && ridx == 4'hD) begin
				if ((wdata[15:8] & 8'h90) == 8'h90) tpe_select <= 1;
				else if ((wdata[15:8] & 8'h90) == 8'h00) tpe_select <= 0;
			end
		end
		else begin
			if (be[1]) regs[ridx][15:8] <= wdata[15:8];
			if (be[0]) regs[ridx][7:0]  <= wdata[7:0];
		end
	end
end

endmodule
