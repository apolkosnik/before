//============================================================================
//  NeXT BMAP chip, 16 x 32-bit registers at 0x020C0000 (mask 0x3F,
//  decoded over a 64 KB window as in Previous memory.c)
//
//  Modeled on Previous src/bmap.c: plain read/write registers, except
//  register 0xD where bit 29 (BMAP_HEARTBEAT) reads as 1 when no
//  twisted-pair ethernet transceiver is connected (this core has no
//  ethernet, so the heartbeat bit is always set on read).
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
	output [15:0] rdata
);

reg [31:0] regs [0:15];

wire [3:0] ridx = addr[5:2];

wire [31:0] rval_raw = regs[ridx];
wire [31:0] rval = (ridx == 4'hD) ? (rval_raw | 32'h20000000) : rval_raw;

assign rdata = addr[1] ? rval[15:0] : rval[31:16];

integer i;

always @(posedge clk) begin
	if (reset) begin
		for (i = 0; i < 16; i = i + 1) regs[i] <= 32'd0;
	end
	else if (sel & we) begin
		if (!addr[1]) begin
			if (be[1]) regs[ridx][31:24] <= wdata[15:8];
			if (be[0]) regs[ridx][23:16] <= wdata[7:0];
		end
		else begin
			if (be[1]) regs[ridx][15:8] <= wdata[15:8];
			if (be[0]) regs[ridx][7:0]  <= wdata[7:0];
		end
	end
end

endmodule
