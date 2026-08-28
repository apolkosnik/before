//============================================================================
//  NeXT boot ROM, 128 KB (0x00000000 and mirror 0x01000000, mask 0x1FFFF)
//
//  Port A: CPU read.  Registered address, registered output.
//  Port B: byte-wide write from the HPS file download (ioctl), assembling
//  big-endian byte pairs: even file offset = bits [15:8].
//
//  For simulation the image can be preloaded with $readmemh from
//  INIT_FILE (one 4-digit hex word per line, big-endian).
//============================================================================

module next_rom #(parameter ROM_INIT_EN = 0, parameter ROM_INIT = "rom.hex")
(
	input         clk,

	// CPU read port
	input  [15:0] a_addr,        // word address
	output [15:0] a_q,

	// byte write port (ioctl download)
	input         wr,
	input  [16:0] w_addr,        // byte address
	input   [7:0] w_din
);

reg [15:0] mem [0:65535];

reg [15:0] qa;
assign a_q = qa;

always @(posedge clk) qa <= mem[a_addr];

reg [7:0] evenbyte;
always @(posedge clk) begin
	if (wr) begin
		if (!w_addr[0]) evenbyte <= w_din;
		else            mem[w_addr[16:1]] <= {evenbyte, w_din};
	end
end

generate if (ROM_INIT_EN) begin : g_init
	initial $readmemh(ROM_INIT, mem);
end endgenerate

endmodule
