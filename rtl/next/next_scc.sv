//============================================================================
//  SCC serial controller (AMD Z8530H), register-level model
//
//  0x02018000  Control B      0x02018002  Data B
//  0x02018001  Control A      0x02018003  Data A
//  (also reached through the BMAP IO mirror at 0x02118000, which is how
//  the boot ROM addresses it)
//
//  Modeled on Previous src/scc.c, which is what the boot ROM system
//  test is known to pass against:
//  - one register pointer shared by both channels (as on the real 8530):
//    a control write with pointer 0 sets it from bits 2:0 plus the
//    point-high command ((val & 0x38) == 8), any other control access
//    resets it
//  - control reads return RR0 (status), RR1 = 0x06, RR3/RR10 = 0, and
//    the stored WR12/WR13/WR15 for the pointer values scc.c serves
//  - WR9 decodes the master interrupt reset commands (0x40 reset B,
//    0x80 reset A, 0xC0 hard reset)
//  - reset leaves RR0 = 0x44 (TX buffer empty, TX underrun)
//  - a data write stores the byte in a one-byte buffer and raises
//    RR0 RX-available on that channel (scc.c models the local loopback
//    the ROM test uses this way); a data read returns the buffer
//
//  No baud timing, interrupts or DMA yet; see docs/PORTING.md.
//  Registers are discrete (not arrays) so the combinational read muxes
//  simulate correctly everywhere.
//============================================================================

module next_scc
(
	input         clk,
	input         reset,

	input         sel,
	input         addr1,         // cpu_addr[1]: 0 = control, 1 = data
	input         we,
	input   [1:0] be,            // [1] = even byte (channel B), [0] = odd (A)
	input  [15:0] wdata,
	output [15:0] rdata
);

localparam RR0_RXAVAIL = 8'h01;
localparam RR0_TXEMPTY = 8'h04;

reg [3:0] ptr;                   // shared register pointer
reg [7:0] wr12_a, wr12_b;
reg [7:0] wr13_a, wr13_b;
reg [7:0] wr15_a, wr15_b;
reg [7:0] wr2;                   // interrupt vector (shared)
reg [7:0] rr0_a, rr0_b;
reg [7:0] scc_buf;

// control read data mux, scc_control_read() in scc.c
wire [7:0] ctrl_a =
	(ptr == 4'd0)  ? rr0_a :
	(ptr == 4'd1)  ? 8'h06 :
	(ptr == 4'd2)  ? wr2 :
	(ptr == 4'd12) ? wr12_a :
	(ptr == 4'd13) ? wr13_a :
	(ptr == 4'd15) ? wr15_a : 8'h00;

wire [7:0] ctrl_b =
	(ptr == 4'd0)  ? rr0_b :
	(ptr == 4'd1)  ? 8'h06 :
	(ptr == 4'd2)  ? wr2 :
	(ptr == 4'd12) ? wr12_b :
	(ptr == 4'd13) ? wr13_b :
	(ptr == 4'd15) ? wr15_b : 8'h00;

// byte lanes: even byte = channel B, odd byte = channel A
assign rdata = addr1 ? {scc_buf, scc_buf} : {ctrl_b, ctrl_a};

wire ctrl_we = sel & we & !addr1;
wire ctrl_rd = sel & ~we & !addr1;
wire data_we = sel & we & addr1;

// the ROM accesses the SCC one byte at a time; for a word write the
// channel B byte is decoded (B is the lower address)
wire       ch_is_b   = be[1];
wire [7:0] ctrl_val  = ch_is_b ? wdata[15:8] : wdata[7:0];

wire [3:0] ptr_next  = {(ctrl_val & 8'h38) == 8'h08, ctrl_val[2:0]};

always @(posedge clk) begin
	if (reset) begin
		ptr <= 0;
		wr2 <= 0;
		wr12_a <= 0; wr12_b <= 0;
		wr13_a <= 0; wr13_b <= 0;
		wr15_a <= 8'hF8; wr15_b <= 8'hF8;
		rr0_a <= 8'h44; rr0_b <= 8'h44;
		scc_buf <= 0;
	end
	else begin
		if (ctrl_we) begin
			if (ptr == 0) begin
				ptr <= ptr_next;
				// commands in bits 7:6 do nothing here
				// (scc_write_init_command is empty in scc.c)
			end
			else begin
				case (ptr)
					4'd1: begin
						// scc_write_mode(): DMA request mode raises RX
						if ((ctrl_val & 8'hC0) == 8'hC0) begin
							if (ch_is_b) rr0_b <= rr0_b | RR0_RXAVAIL;
							else         rr0_a <= rr0_a | RR0_RXAVAIL;
						end
					end
					4'd9: begin
						// master interrupt reset commands
						case (ctrl_val[7:6])
							2'd1: rr0_b <= (rr0_b & ~8'hC7) | 8'h44;
							2'd2: rr0_a <= (rr0_a & ~8'hC7) | 8'h44;
							2'd3: begin
								rr0_a <= (rr0_a & ~8'hC7) | 8'h44;
								rr0_b <= (rr0_b & ~8'hC7) | 8'h44;
							end
							default: ;
						endcase
					end
					4'd2:  wr2 <= ctrl_val;
					4'd12: if (ch_is_b) wr12_b <= ctrl_val; else wr12_a <= ctrl_val;
					4'd13: if (ch_is_b) wr13_b <= ctrl_val; else wr13_a <= ctrl_val;
					4'd15: if (ch_is_b) wr15_b <= ctrl_val; else wr15_a <= ctrl_val;
					default: ;
				endcase
				ptr <= 0;
			end
		end
		else if (ctrl_rd) begin
			// reading a control port resets the register pointer
			ptr <= 0;
		end
		else if (data_we) begin
			// scc_data_write(): buffer the byte, raise loopback RX
			if (ch_is_b) begin
				scc_buf <= wdata[15:8];
				rr0_b <= RR0_TXEMPTY | RR0_RXAVAIL;
			end
			else begin
				scc_buf <= wdata[7:0];
				rr0_a <= RR0_TXEMPTY | RR0_RXAVAIL;
			end
		end
	end
end

endmodule
