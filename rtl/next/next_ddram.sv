//============================================================================
//  Main RAM in DDR3 through the MiSTer DDRAM interface
//
//  Serves the 32-bit ram_* port of next_system from the HPS DDR3 at byte
//  base 0x30000000 (the core-usable window), single-beat bursts.
//
//  Byte order: the ram_* port carries big-endian 68k words (bit 31 down
//  to bit 0 = ascending byte address), the DDR3 is little endian, so
//  bytes are swapped within each 32-bit half.
//============================================================================

module next_ddram
(
	input             clk,
	input             reset,

	// ram port (level req, level ack: ack holds until req drops)
	input             ram_req,
	input             ram_we,
	input       [3:0] ram_be,      // [3] = MSB = lowest byte address
	input      [23:0] ram_addr,    // 32-bit word index (64 MB)
	input      [31:0] ram_din,
	output reg [31:0] ram_dout,
	output reg        ram_ack,

	// MiSTer DDRAM interface
	input             DDRAM_BUSY,
	output      [7:0] DDRAM_BURSTCNT,
	output reg [28:0] DDRAM_ADDR,
	input      [63:0] DDRAM_DOUT,
	input             DDRAM_DOUT_READY,
	output reg        DDRAM_RD,
	output reg [63:0] DDRAM_DIN,
	output reg  [7:0] DDRAM_BE,
	output reg        DDRAM_WE
);

assign DDRAM_BURSTCNT = 8'd1;

function [31:0] bswap;
	input [31:0] x;
	bswap = {x[7:0], x[15:8], x[23:16], x[31:24]};
endfunction

wire [3:0] be_rev = {ram_be[0], ram_be[1], ram_be[2], ram_be[3]};

reg half;      // which 32-bit half of the 64-bit word
reg busy;

always @(posedge clk) begin
	if (reset) begin
		DDRAM_RD <= 0;
		DDRAM_WE <= 0;
		ram_ack  <= 0;
		busy     <= 0;
	end
	else begin
		if (!DDRAM_BUSY) begin
			if (DDRAM_WE) begin
				DDRAM_WE <= 0;
				ram_ack  <= 1;      // write is done once accepted
				busy     <= 0;
			end
			if (DDRAM_RD) DDRAM_RD <= 0;
		end

		if (busy && DDRAM_DOUT_READY) begin
			ram_dout <= bswap(half ? DDRAM_DOUT[63:32] : DDRAM_DOUT[31:0]);
			ram_ack  <= 1;
			busy     <= 0;
		end

		if (!ram_req) ram_ack <= 0;
		else if (!ram_ack && !busy && !DDRAM_RD && !DDRAM_WE) begin
			DDRAM_ADDR <= {5'b00110, 1'b0, ram_addr[23:1]};
			half       <= ram_addr[0];
			if (ram_we) begin
				DDRAM_DIN <= {2{bswap(ram_din)}};
				DDRAM_BE  <= ram_addr[0] ? {be_rev, 4'b0000} : {4'b0000, be_rev};
				DDRAM_WE  <= 1;
			end
			else begin
				DDRAM_BE  <= 8'hFF;
				DDRAM_RD  <= 1;
				busy      <= 1;
			end
		end
	end
end

endmodule
