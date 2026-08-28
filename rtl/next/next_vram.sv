//============================================================================
//  NeXT monochrome VRAM, 256 KB (0x0B000000, mask 0x0003FFFF)
//
//  Port A: CPU, 16 bits with byte lanes.  Registered address, registered
//  output (q_a is valid one cycle after the address is applied).
//  Port B: video scan-out, read only, registered address and output.
//
//  Built from two byte-wide dual-clock true dual-port RAMs (dpram_dc)
//  so each byte array maps to a single M10K set: port A carries the CPU
//  read/write in the system clock domain, port B the scan-out read in
//  the video clock domain.
//
//  Byte order: the even (lower) address byte is in bits [15:8], matching
//  the CPU data bus lanes (nUDS = even byte).
//============================================================================

module next_vram
(
	// CPU port (system clock domain)
	input         clk_a,
	input  [16:0] a_addr,        // word address
	input   [1:0] a_we,          // {even byte, odd byte} write lanes
	input  [15:0] a_din,
	output [15:0] a_q,

	// video port (video clock domain)
	input         clk_b,
	input  [16:0] b_addr,
	output [15:0] b_q
);

dpram_dc #(17, 8) mem_hi
(
	.clock_a(clk_a),
	.address_a(a_addr),
	.data_a(a_din[15:8]),
	.wren_a(a_we[1]),
	.q_a(a_q[15:8]),
	.clock_b(clk_b),
	.address_b(b_addr),
	.data_b(8'd0),
	.wren_b(1'b0),
	.q_b(b_q[15:8])
);

dpram_dc #(17, 8) mem_lo
(
	.clock_a(clk_a),
	.address_a(a_addr),
	.data_a(a_din[7:0]),
	.wren_a(a_we[0]),
	.q_a(a_q[7:0]),
	.clock_b(clk_b),
	.address_b(b_addr),
	.data_b(8'd0),
	.wren_b(1'b0),
	.q_b(b_q[7:0])
);

endmodule
