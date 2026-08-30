//============================================================================
//  NeXT core boot test
//
//  Loads the real NeXTcube 68040 boot ROM (Rev 2.5 v66, from the Previous
//  submodule) into the real next_system with the real AP68040 core and
//  lets it execute from reset.  No mock logic: the only thing modeled
//  here is the main RAM (64 MB behind the ram_* port, as the DDR3 is on
//  hardware).
//
//  Milestones checked:
//    1. the CPU fetches the reset vectors from ROM addresses 0 and 4
//    2. the PC reaches the reset vector target 0x0100001E
//    3. the ROM reads SCR1 and receives 0x00012052 (machine id)
//    4. the ROM writes SCR2 (LED/RTC activity)
//    5. the CPU never halts (double fault)
//
//  All IO-space accesses are logged; the log is printed on failure and
//  with +iotrace on success too.
//============================================================================

`timescale 1ns/1ps

module tb_next_boot;

reg clk = 0;
reg reset = 1;

always #5 clk = ~clk;   // 100 MHz

// ram port
wire        ram_req, ram_we;
wire  [3:0] ram_be;
wire [23:0] ram_addr;
wire [31:0] ram_din;
wire [31:0] ram_dout;   // driven by next_ddram
wire        ram_ack;

wire        hsync, vsync, hblank, vblank;
wire  [7:0] gray;
wire        led;
wire [31:0] dbg_pc;
wire        dbg_halted;
wire  [2:0] dbg_ipl;

// exactly the FPGA parameterization: virtual microsecond of 50 clocks,
// no pacing (the physical simulation clock rate is immaterial, the
// clock ratios are what the ROM's calibration checks measure)
next_system #(
	.CLK_HZ(50000000),
	.CPU_PACE_NUM(2),
	.CPU_PACE_DEN(2),
	.ROM_INIT_EN(1),
	.ROM_INIT("build/rom.hex")
) dut
(
	.clk(clk),
	.clk_vid(clk),   // both domains on one clock in simulation
	.reset(reset),
	.ps2_key(11'd0),
	.boot_sel(bootfd ? 3'd2 : bootsd ? 3'd1 : 3'd0),
	.fimg_mounted({1'b0, fimg_mounted}), .fsd_unit(), .fimg_readonly(1'b0),
	.fimg_size(bootfd ? 64'd1474560 : 64'd0),
	.fsd_lba(fsd_lba), .fsd_rd(fsd_rd), .fsd_wr(fsd_wr), .fsd_ack(fsd_ack),
	.fsd_buff_addr(fsd_buff_addr), .fsd_buff_dout(fsd_buff_dout),
	.fsd_buff_din(fsd_buff_din), .fsd_buff_wr(fsd_buff_wr),
	.img_mounted({5'b00000, img_mounted}),
	.sd_unit(),
	.img_readonly(1'b0),
	.img_size(img_bytes),
	.sd_lba(sd_lba),
	.sd_rd(sd_rd),
	.sd_wr(sd_wr),
	.sd_ack(sd_ack),
	.sd_buff_addr(sd_buff_addr),
	.sd_buff_dout(sd_buff_dout),
	.sd_buff_din(sd_buff_din),
	.sd_buff_wr(sd_buff_wr),

	.rom_wr(1'b0),
	.rom_waddr(17'd0),
	.rom_wdata(8'd0),

	.hsync(hsync), .vsync(vsync), .hblank(hblank), .vblank(vblank),
	.gray(gray),

	.ram_req(ram_req),
	.ram_we(ram_we),
	.ram_be(ram_be),
	.ram_addr(ram_addr),
	.ram_din(ram_din),
	.ram_dout(ram_dout),
	.ram_ack(ram_ack),

	.led(led),

	.btx_req(btx_req), .btx_len(btx_len), .btx_addr(btx_addr), .btx_rd(btx_rd),
	.btx_q(btx_q), .btx_ack(btx_ack), .btx_done(btx_done),
	.brx_start(brx_start), .brx_len(brx_len), .brx_valid(brx_valid),
	.brx_data(brx_data), .brx_ready(brx_ready), .enet_mac(enet_mac),

	.dbg_pc(dbg_pc),
	.dbg_halted(dbg_halted),
	.dbg_ipl(dbg_ipl)
);

//----------------------------------------------------------------------------
// Main RAM model with the level req / level ack protocol.
//
// A 4 MB SIMM set in bank 0, banks 1-3 empty: a machine configuration a
// real cube supports, and small enough that the ROM's full-memory clear
// pass stays fast in simulation.  A 4 MB bank aliases through its 16 MB
// decode window (address lines above the SIMM size are not connected),
// which is what the ROM's sizing probe detects.  Reads from the empty
// banks return the address (open bus), writes are dropped, matching
// mem_ram_empty_*() in Previous src/cpu/memory.c.
//----------------------------------------------------------------------------

// Main memory, 64 MB, as the 32-bit words the machine sees.  The DDR3
// model below reads and writes it in 64-bit pairs through next_ddram's
// byte lanes, so the adapter's lane and endian handling is exercised.
reg [31:0] ram_mem [0:16777215];
integer    ram_init_i;
initial for (ram_init_i = 0; ram_init_i < 16777216; ram_init_i = ram_init_i + 1)
	ram_mem[ram_init_i] = 32'h00000000;

//----------------------------------------------------------------------------
// The real DDR3 path: next_ddram and next_ddram_arb, wired as the emu
// top wires them.  The bench used to hang its own RAM model straight on
// next_system's port, so the entire DDR3 chain - the adapter, the
// arbiter, its colour and mailbox ports, its read latch - had never
// executed a single instruction of POST.
//----------------------------------------------------------------------------

wire        ga_rd, ga_we, ga_busy, ga_dout_ready;
wire [28:0] ga_addr;
wire [63:0] ga_din, ga_dout;
wire  [7:0] ga_be, ga_burst;

wire        eb_req, eb_we, eb_ack;
wire [28:0] eb_addr;
wire [63:0] eb_wdata, eb_rdata;

wire        dr_busy, dr_dout_ready, dr_rd, dr_we;
wire [28:0] dr_addr;
wire [63:0] dr_dout, dr_din;
wire  [7:0] dr_be, dr_burst;

next_ddram ddram
(
	.clk(clk), .reset(reset),
	.ram_req(ram_req), .ram_we(ram_we), .ram_be(ram_be),
	.ram_addr(ram_addr), .ram_din(ram_din),
	.ram_dout(ram_dout), .ram_ack(ram_ack),
	.DDRAM_BUSY(ga_busy), .DDRAM_BURSTCNT(ga_burst), .DDRAM_ADDR(ga_addr),
	.DDRAM_DOUT(ga_dout), .DDRAM_DOUT_READY(ga_dout_ready),
	.DDRAM_RD(ga_rd), .DDRAM_DIN(ga_din), .DDRAM_BE(ga_be), .DDRAM_WE(ga_we)
);

next_ddram_arb ddram_arb
(
	.clk(clk), .reset(reset),
	.a_rd(ga_rd), .a_we(ga_we), .a_addr(ga_addr), .a_din(ga_din),
	.a_be(ga_be), .a_burst(ga_burst), .a_busy(ga_busy),
	.a_dout(ga_dout), .a_dout_ready(ga_dout_ready),
	.b_req(eb_req), .b_we(eb_we), .b_addr(eb_addr),
	.b_wdata(eb_wdata), .b_rdata(eb_rdata), .b_ack(eb_ack),
	.DDRAM_BUSY(dr_busy), .DDRAM_BURSTCNT(dr_burst), .DDRAM_ADDR(dr_addr),
	.DDRAM_DOUT(dr_dout), .DDRAM_DOUT_READY(dr_dout_ready),
	.DDRAM_RD(dr_rd), .DDRAM_DIN(dr_din), .DDRAM_BE(dr_be), .DDRAM_WE(dr_we)
);

//----------------------------------------------------------------------------
// The ethernet mailbox bridge, on the arbiter's port B, against a
// mailbox the host never wrote (the DDR3 model starts those words as
// rubbish).  With the network off it must read none of it: reading is
// how a stale ring pointer becomes frames the guest never asked for.
//----------------------------------------------------------------------------

wire        btx_req, btx_rd, btx_ack, btx_done;
wire [10:0] btx_len, btx_addr;
wire  [7:0] btx_q;
wire        brx_start, brx_valid, brx_ready;
wire [10:0] brx_len;
wire  [7:0] brx_data;
wire [47:0] enet_mac;

reg         net_enable = 1;

next_enet_bridge #(.CLK_HZ(50000000)) bridge
(
	.clk(clk), .reset(reset), .enable(net_enable),
	.btx_req(btx_req), .btx_len(btx_len), .btx_addr(btx_addr),
	.btx_rd(btx_rd), .btx_q(btx_q), .btx_ack(btx_ack), .btx_done(btx_done),
	.brx_start(brx_start), .brx_len(brx_len), .brx_valid(brx_valid),
	.brx_data(brx_data), .brx_ready(brx_ready),
	.guest_mac(enet_mac),
	.m_req(eb_req), .m_we(eb_we), .m_addr(eb_addr), .m_wdata(eb_wdata),
	.m_rdata(eb_rdata), .m_ack(eb_ack)
);

integer injected = 0;
always @(posedge clk) if (!reset && brx_start) injected = injected + 1;

// DDR3 model: throttles with BUSY, answers reads after a latency, and
// returns bursts back to back.  A memory that always accepts instantly
// is the memory that hides handshake bugs.  Each window has its own
// storage so an access landing in the wrong one is visible.
localparam [28:0] W_RAM  = 29'h0600_0000;   // byte 0x30000000
localparam [28:0] W_VRAM = 29'h0680_0000;   // byte 0x34000000
localparam [28:0] W_MBOX = 29'h03FE_0000;   // byte 0x1FF00000

reg [63:0] vram64 [0:262143];
reg [63:0] mbox   [0:2047];
integer    mb_i;
initial begin
	for (mb_i = 0; mb_i < 2048; mb_i = mb_i + 1)
		mbox[mb_i] = {32'hDEADBEEF, mb_i[15:0], 16'hFACE};
	for (mb_i = 0; mb_i < 262144; mb_i = mb_i + 1) vram64[mb_i] = 64'd0;
end

integer mbox_reads = 0, mbox_writes = 0, stray_ddr = 0;

wire in_ram  = (dr_addr[28:23] == W_RAM[28:23]);
wire in_vram = (dr_addr[28:18] == W_VRAM[28:18]);
wire in_mbox = (dr_addr[28:11] == W_MBOX[28:11]);

function [31:0] bsw; input [31:0] x; bsw = {x[7:0], x[15:8], x[23:16], x[31:24]}; endfunction

reg  [7:0] d3_left = 0;
reg [28:0] d3_addr = 0;
reg  [5:0] d3_lat = 0;
reg [31:0] d3_lfsr = 32'h1234_5678;
reg        d3_busy_r = 0;
reg        d3_dv = 0;
reg [63:0] d3_dout = 0;

assign dr_busy       = d3_busy_r;
assign dr_dout_ready = d3_dv;
assign dr_dout       = d3_dout;

always @(posedge clk) begin
	d3_lfsr <= {d3_lfsr[30:0], d3_lfsr[31] ^ d3_lfsr[21] ^ d3_lfsr[1] ^ d3_lfsr[0]};
	d3_dv   <= 0;

	if (reset) begin
		d3_left   <= 0;
		d3_lat    <= 0;
		d3_busy_r <= 0;
	end
	else begin
		d3_busy_r <= (d3_left != 0) || (d3_lfsr[3:0] == 4'd0);

		if (d3_left == 0 && !d3_busy_r) begin
			if (dr_we) begin
				if (in_ram) begin
					if (dr_be[0]) ram_mem[{dr_addr[22:0], 1'b0}][31:24] <= dr_din[7:0];
					if (dr_be[1]) ram_mem[{dr_addr[22:0], 1'b0}][23:16] <= dr_din[15:8];
					if (dr_be[2]) ram_mem[{dr_addr[22:0], 1'b0}][15:8]  <= dr_din[23:16];
					if (dr_be[3]) ram_mem[{dr_addr[22:0], 1'b0}][7:0]   <= dr_din[31:24];
					if (dr_be[4]) ram_mem[{dr_addr[22:0], 1'b1}][31:24] <= dr_din[39:32];
					if (dr_be[5]) ram_mem[{dr_addr[22:0], 1'b1}][23:16] <= dr_din[47:40];
					if (dr_be[6]) ram_mem[{dr_addr[22:0], 1'b1}][15:8]  <= dr_din[55:48];
					if (dr_be[7]) ram_mem[{dr_addr[22:0], 1'b1}][7:0]   <= dr_din[63:56];
				end
				else if (in_vram) vram64[dr_addr[17:0]] <= dr_din;
				else if (in_mbox) begin
					mbox[dr_addr[10:0]] <= dr_din;
					mbox_writes = mbox_writes + 1;
				end
				else stray_ddr = stray_ddr + 1;
			end
			else if (dr_rd) begin
				d3_addr <= dr_addr;
				d3_left <= dr_burst;
				d3_lat  <= 6'd12;
				if (in_mbox) mbox_reads = mbox_reads + 1;
				else if (!in_ram && !in_vram) stray_ddr = stray_ddr + 1;
			end
		end
		else if (d3_left != 0) begin
			if (d3_lat != 0) d3_lat <= d3_lat - 1'd1;
			else begin
				d3_dout <= (d3_addr[28:23] == W_RAM[28:23])
				           ? {bsw(ram_mem[{d3_addr[22:0], 1'b1}]),
				              bsw(ram_mem[{d3_addr[22:0], 1'b0}])} :
				           (d3_addr[28:18] == W_VRAM[28:18]) ? vram64[d3_addr[17:0]] :
				           (d3_addr[28:11] == W_MBOX[28:11]) ? mbox[d3_addr[10:0]] :
				                                               64'hBADD_BADD_BADD_BADD;
				d3_dv   <= 1;
				d3_addr <= d3_addr + 1'd1;
				d3_left <= d3_left - 1'd1;
			end
		end
	end
end


//----------------------------------------------------------------------------
// bus monitor
//----------------------------------------------------------------------------

wire [31:0] cpu_addr  = dut.cpu_addr;
wire  [1:0] busstate  = dut.busstate;
wire        mem_ready = dut.mem_ready;
wire [15:0] cpu_din   = dut.cpu_din;
wire [15:0] cpu_dout  = dut.cpu_dout;
wire        berr_hold = dut.berr_hold;

// latch the address of the cycle being completed: mem_ready comes two
// cycles after dispatch, while the CPU still holds addr_out
integer access_count = 0;
reg [31:0] first_addr [0:3];

reg        seen_vec_lo = 0;      // read at 0x0
reg        seen_vec_pc = 0;      // read at 0x4
reg        seen_entry  = 0;      // pc reached 0x0100001E
reg        seen_scr1   = 0;      // SCR1 read observed
reg        scr1_ok     = 0;      // ... with correct data
reg        seen_scr2_wr= 0;
integer    berr_count  = 0;

// IO access log
integer iolog_n = 0;
reg [63:0] iolog [0:1023];       // {we, addr[30:0], data[15:0], pc[15:0]}

wire is_io_cyc = (cpu_addr[31:24] == 8'h02);
wire is_write  = (busstate == 2'b11);

always @(posedge clk) begin
	if (!reset) begin
		if (mem_ready) begin
			if (access_count < 4) begin
				first_addr[access_count] <= cpu_addr;
				access_count = access_count + 1;
			end
			if (cpu_addr == 32'h00000000 && !is_write) seen_vec_lo <= 1;
			if (cpu_addr == 32'h00000004 && !is_write) seen_vec_pc <= 1;

			if ((cpu_addr[31:17] == 15'h0100 || cpu_addr[31:17] == 15'h0108) &&
			    (cpu_addr[16:0] >= 17'h0c000 && cpu_addr[16:0] < 17'h0c004) && !is_write) begin
				seen_scr1 <= 1;
				if (!cpu_addr[1] && cpu_din == 16'h0001) scr1_ok <= 1;
				if ( cpu_addr[1] && cpu_din == 16'h2052) scr1_ok <= 1;
			end

			if ((cpu_addr[31:24] == 8'h02) &&
			    (cpu_addr[16:12] == 5'h0d) && is_write) seen_scr2_wr <= 1;

			if (is_io_cyc) begin
				iolog[iolog_n % 1024] <= {is_write, cpu_addr[30:0], is_write ? cpu_dout : cpu_din, dbg_pc[15:0]};
				iolog_n = iolog_n + 1;
			end
		end
		if (berr_hold && busstate != 2'b01) ;
		if (dbg_pc == 32'h0100001E) seen_entry <= 1;
	end
end

// count distinct bus error events and log the faulting addresses
reg berr_d;
reg [31:0] berr_log [0:63];
reg [31:0] berr_pc  [0:63];
always @(posedge clk) begin
	berr_d <= berr_hold;
	if (berr_hold && !berr_d) begin
		if (berr_count < 64) begin
			berr_log[berr_count] <= cpu_addr;
			berr_pc[berr_count]  <= dbg_pc;
		end
		berr_count = berr_count + 1;
	end
end

time delay_t0 = 0;

// POST sub-test tracing: entry PCs and the return-value check PCs of
// the system test master at 0x469c (see ROMV66 listing), logging D0
wire [31:0] dbg_d0 = dut.cpu.core.regfile.dbg_d0;
task post_trace;
	input [31:0] pc;
	begin
		case (pc)
			32'h010031b4: $display("[%0t] POST: FPU test entry", $time);
			32'h010031fa: $display("[%0t] POST: SCC test entry", $time);
			32'h0100337e: $display("[%0t] POST: SCSI test entry", $time);
			32'h01003548: $display("[%0t] POST: Enet test entry", $time);
			32'h0100381a: $display("[%0t] POST: ECC test entry", $time);
			32'h01003b1a: $display("[%0t] POST: RTC test entry", $time);
			32'h01003b98: $display("[%0t] POST: Timer test entry", $time);
			32'h01003c7e: $display("[%0t] POST: Event counter test entry", $time);
			32'h01003f74: $display("[%0t] POST: Sound out test entry", $time);
			32'h01003430: $display("[%0t] POST: Extended SCSI test entry", $time);
			32'h010046f2: $display("[%0t] POST: FPU test returned d0=%08x", $time, dbg_d0);
			32'h0100471c: $display("[%0t] POST: SCC test returned d0=%08x", $time, dbg_d0);
			32'h01004746: $display("[%0t] POST: SCSI test returned d0=%08x", $time, dbg_d0);
			32'h01004764: $display("[%0t] POST: Enet test returned d0=%08x", $time, dbg_d0);
			32'h0100479c: $display("[%0t] POST: ECC test returned d0=%08x", $time, dbg_d0);
			32'h010047c6: $display("[%0t] POST: RTC test returned d0=%08x", $time, dbg_d0);
			32'h010047f0: $display("[%0t] POST: Timer test returned d0=%08x", $time, dbg_d0);
			32'h0100481a: $display("[%0t] POST: Event counter test returned d0=%08x", $time, dbg_d0);
			32'h01004852: $display("[%0t] POST: Sound out test returned d0=%08x", $time, dbg_d0);
			32'h01004884: $display("[%0t] POST: Extended SCSI test returned d0=%08x", $time, dbg_d0);
			32'h01001308: begin
				$display("[%0t] POST: SYSTEM TEST FAILED, error code %02x", $time, dbg_d0[7:0]);
				$display("  enet: est=%0d stopped=%b tx_mode=%02x tx_status=%02x rx_status=%02x",
				         dut.enet.est, dut.enet.en_stopped, dut.enet.tx_mode,
				         dut.enet.tx_status, dut.enet.rx_status);
				$display("  enet: tx_csr=%02x tx_next=%08x tx_limit=%08x tx_len=%0d",
				         dut.enet.tx_csr, dut.enet.tx_next, dut.enet.tx_limit, dut.enet.tx_len);
				$display("  enet: rx_csr=%02x rx_next=%08x rx_limit=%08x rx_len=%0d rx_slimit=%08x",
				         dut.enet.rx_csr, dut.enet.rx_next, dut.enet.rx_limit, dut.enet.rx_len,
				         dut.enet.rx_slimit);
				dump_state;
			end
			32'h01001330: $display("[%0t] POST: system test passed path", $time);
			32'h01003d78: $display("[%0t] POST: event counter measured delay(1000) = %0d us", $time, dbg_d0);
			32'h010024d2: delay_t0 = $time;   // delay() body entry (past the arg<=3 early out)
			32'h010024fc: if (delay_t0 != 0) begin
				$display("[%0t] POST: delay() took %0d ns", $time, ($time - delay_t0) / 1000);
				delay_t0 = 0;
			end
			default: ;
		endcase
	end
endtask

// heartbeat for long runs
integer hb_cycles = 0;
always @(posedge clk) begin
	hb_cycles = hb_cycles + 1;
	if (hb_cycles % 10000000 == 0)
		$display("[%0t] heartbeat: pc=%08x", $time, dbg_pc);
end

// dedicated ethernet trace: every access to the enet register ranges,
// and the engine state transitions
wire [16:0] en_off = cpu_addr[16:0];
wire is_enet_cyc = (cpu_addr[31:24] == 8'h02) &&
	((en_off[15:4]  == 12'h600)  ||
	 (en_off[15:2]  == 14'h044)  ||
	 (en_off[15:2]  == 14'h054)  ||
	 (en_off[15:5]  == 11'h208)  ||
	 (en_off[15:5]  == 11'h20A)  ||
	 (en_off[15:2]  == 14'h10C4) ||
	 (en_off[15:2]  == 14'h10D4));

always @(posedge clk) begin
	if (!reset && mem_ready && is_enet_cyc && $test$plusargs("entrace"))
		$display("[%0t] EN %s %08x data=%04x pc=%08x",
		         $time, is_write ? "WR" : "RD", cpu_addr,
		         is_write ? cpu_dout : cpu_din, dbg_pc);
end

reg [2:0] en_est_prev;
always @(posedge clk) begin
	en_est_prev <= dut.enet.est;
	if (!reset && dut.enet.est != en_est_prev && $test$plusargs("entrace"))
		$display("[%0t] EN est %0d -> %0d (tx_len=%0d rx_len=%0d rx_pos=%0d tx_next=%08x rx_next=%08x)",
		         $time, en_est_prev, dut.enet.est, dut.enet.tx_len,
		         dut.enet.rx_len, dut.enet.rx_pos, dut.enet.tx_next, dut.enet.rx_next);
end

// PC trace ring buffer
reg [31:0] pc_ring [0:63];
integer pc_ring_n = 0;
reg [31:0] pc_prev = 0;
always @(posedge clk) begin
	if (dbg_pc != pc_prev) begin
		pc_prev <= dbg_pc;
		pc_ring[pc_ring_n % 64] <= dbg_pc;
		pc_ring_n = pc_ring_n + 1;
		post_trace(dbg_pc);
	end
end

task dump_state;
	integer i, k;
	begin
		$display("--- state dump ---");
		$display("pc=%08x halted=%b ipl=%0d berr_events=%0d", dbg_pc, dbg_halted, dbg_ipl, berr_count);
		$display("last PCs:");
		k = (pc_ring_n > 64) ? 64 : pc_ring_n;
		for (i = 0; i < k; i = i + 1)
			$display("  %08x", pc_ring[(pc_ring_n - k + i) % 64]);
		$display("bus errors:");
		for (i = 0; i < berr_count && i < 64; i = i + 1)
			$display("  addr=%08x pc=%08x", berr_log[i], berr_pc[i]);
		$display("IO accesses (last %0d of %0d):", (iolog_n > 1024) ? 1024 : iolog_n, iolog_n);
		k = (iolog_n > 1024) ? 1024 : iolog_n;
		for (i = 0; i < k; i = i + 1)
			$display("  %s %08x data=%04x pc=xxxx%04x",
			         iolog[(iolog_n - k + i) % 1024][63] ? "WR" : "RD",
			         {1'b0, iolog[(iolog_n - k + i) % 1024][62:32]},
			         iolog[(iolog_n - k + i) % 1024][31:16],
			         iolog[(iolog_n - k + i) % 1024][15:0]);
	end
endtask

integer errors = 0;
longint run_cycles;
reg     halt_run = 0;

task check;
	input cond;
	input [639:0] name;
	begin
		if (cond) $display("PASS: %0s", name);
		else begin
			$display("FAIL: %0s", name);
			errors = errors + 1;
		end
	end
endtask

//----------------------------------------------------------------------------
// +bootsd: a 2048 block disk image on the SD model and the boot device
// menu at Disk, to drive the ROM's SCSI boot path ("sd" boot command).
// The image carries a byte pattern, not a real filesystem: the pass
// criteria are that the ROM selects the disk, the target serves sector
// reads and the DMA channel lands them in memory.
//----------------------------------------------------------------------------


reg        bootsd = 0;

//----------------------------------------------------------------------------
// +bootfd: a 1.44 MB floppy on the second image slot with the boot
// device set to Floppy, so the ROM's own floppy driver runs - which is
// the only way to see what it programs into the shared DMA channel.
//----------------------------------------------------------------------------

reg         bootfd = 0;
reg         fimg_mounted = 0;
wire [31:0] fsd_lba;
wire        fsd_rd, fsd_wr;
reg         fsd_ack = 0;
reg   [8:0] fsd_buff_addr = 0;
reg   [7:0] fsd_buff_dout = 0;
wire  [7:0] fsd_buff_din;
reg         fsd_buff_wr = 0;

reg  [7:0] fdisk [0:2880*512-1];
integer    fdi;
initial for (fdi = 0; fdi < 2880*512; fdi = fdi + 1)
	fdisk[fdi] = fdi[7:0] ^ {fdi[16:9], 1'b0};

reg fsd_act = 0;
integer fsd_reads = 0;
always @(posedge clk) begin
	if (fsd_rd && !fsd_ack && !fsd_act) begin
		fsd_ack <= 1; fsd_act <= 1; fsd_buff_addr <= 0; fsd_buff_wr <= 0;
		fsd_reads = fsd_reads + 1;
		if (fsd_reads < 40)
			$display("[%0t] FD: read lba %0d", $time, fsd_lba);
	end
	else if (fsd_ack && fsd_act) begin
		if (!fsd_buff_wr) begin
			fsd_buff_dout <= fdisk[{fsd_lba[11:0], 9'd0} + {23'd0, fsd_buff_addr}];
			fsd_buff_wr <= 1;
		end
		else begin
			fsd_buff_wr <= 0;
			if (fsd_buff_addr == 9'd511) begin fsd_ack <= 0; fsd_act <= 0; end
			else fsd_buff_addr <= fsd_buff_addr + 1'd1;
		end
	end
end

// every register the ROM's floppy driver touches, and the controller's
// answer: this is the conversation the "DMA buf overflow" ends
integer fdreg_n = 0;
always @(posedge clk) if (bootfd && !reset && dut.io_flp && dut.state == 2'd1) begin
	if (fdreg_n < 120) begin
		if (is_write)
			$display("[%0t] FDREG wr +%0d = %04x", $time, dut.io_off[3:0],
			         cpu_dout);
		else
			$display("[%0t] FDREG rd +%0d -> %04x", $time, dut.io_off[3:0],
			         cpu_din);
	end
	fdreg_n = fdreg_n + 1;
end

// what the ROM programs into the shared channel, and what the transfer
// does with it: this is the accounting the ROM checks afterwards
reg [31:0] dnext_d = 0, dlimit_d = 0;
reg  [7:0] dcsr_d = 0;
always @(posedge clk) if (bootfd && !reset) begin
	if (dut.scsi.d_next != dnext_d && dut.scsi.flp_active)
		dnext_d <= dut.scsi.d_next;
	if (dut.scsi.d_limit != dlimit_d) begin
		dlimit_d <= dut.scsi.d_limit;
		$display("[%0t] FD chan: next %08x limit %08x csr %02x", $time,
		         dut.scsi.d_next, dut.scsi.d_limit, dut.scsi.d_csr);
	end
	if (dut.scsi.d_csr != dcsr_d) begin
		dcsr_d <= dut.scsi.d_csr;
		$display("[%0t] FD csr: %02x  next %08x limit %08x", $time,
		         dut.scsi.d_csr, dut.scsi.d_next, dut.scsi.d_limit);
	end
end

reg        img_mounted = 0;
wire [31:0] sd_lba;
wire        sd_rd, sd_wr;
reg         sd_ack = 0;
reg   [8:0] sd_buff_addr = 0;
reg   [7:0] sd_buff_dout = 0;
wire  [7:0] sd_buff_din;
reg         sd_buff_wr = 0;

reg  [7:0] disk [0:2048*512-1];

// +img=<path>: a raw disk image file backs the SD model instead of
// the built-in pattern (copy the image first, writes go back to it)
string     img_path;
integer    img_fd = 0;
integer    img_blk = 0;
reg [63:0] img_bytes = 64'd1048576;
reg  [7:0] fbuf [0:511];
integer    fr;

initial begin
	if ($value$plusargs("img=%s", img_path)) begin
		img_fd = $fopen(img_path, "rb+");
		if (img_fd == 0) begin
			$display("cannot open %0s", img_path);
			$finish;
		end
		// size: +imgblk=<512-byte blocks> is authoritative (robust across
		// simulators); fall back to seek/tell
		if ($value$plusargs("imgblk=%d", img_blk))
			img_bytes = {32'd0, img_blk} * 64'd512;
		else begin
			fr = $fseek(img_fd, 0, 2);
			img_bytes = $ftell(img_fd);
		end
		fr = $fseek(img_fd, 0, 0);
		// confirm the data path: read sector 0 and show the disklabel
		fr = $fread(fbuf, img_fd);
		$display("BOOT: disk image %0s, %0d bytes; label %02x %02x %02x %02x '%c%c%c%c'",
		         img_path, img_bytes,
		         fbuf[0], fbuf[1], fbuf[2], fbuf[3],
		         fbuf[0], fbuf[1], fbuf[2], fbuf[3]);
	end
end

function [7:0] dpat;
	input [31:0] blk;
	input [31:0] off;
	begin
		dpat = blk[7:0] ^ off[7:0] ^ {off[10:8], 5'd0};
	end
endfunction

integer sdi;
initial for (sdi = 0; sdi < 2048*512; sdi = sdi + 1)
	disk[sdi] = dpat(sdi / 512, sdi % 512);

reg sd_rd_act = 0, sd_wr_act = 0, sd_rphase = 0, img_flush = 0;
integer sd_reads = 0;
reg sd_lba0 = 0;

always @(posedge clk) begin
	if (sd_rd && !sd_ack) begin
		sd_ack <= 1;
		sd_rd_act <= 1;
		sd_buff_addr <= 0;
		sd_buff_wr <= 0;
		sd_reads = sd_reads + 1;
		if (sd_lba == 0) sd_lba0 <= 1;
		if (img_fd != 0) begin
			fr = $fseek(img_fd, {sd_lba, 9'd0}, 0);
			fr = $fread(fbuf, img_fd);
		end
		if (sd_reads < 200 || (sd_reads % 256) == 0)
			$display("[%0t] BOOT: SD read lba %0d", $time, sd_lba);
	end
	else if (sd_ack && sd_rd_act) begin
		if (!sd_buff_wr) begin
			sd_buff_dout <= (img_fd != 0)
			              ? fbuf[sd_buff_addr]
			              : disk[{sd_lba[10:0], 9'd0} + {23'd0, sd_buff_addr}];
			sd_buff_wr <= 1;
		end
		else begin
			sd_buff_wr <= 0;
			if (sd_buff_addr == 9'd511) begin
				sd_ack <= 0;
				sd_rd_act <= 0;
			end
			else sd_buff_addr <= sd_buff_addr + 1'd1;
		end
	end
	else if (sd_wr && !sd_ack) begin
		sd_ack <= 1;
		sd_wr_act <= 1;
		sd_buff_addr <= 0;
		sd_rphase <= 0;
		$display("[%0t] BOOT: SD write lba %0d", $time, sd_lba);
	end
	else if (sd_ack && sd_wr_act) begin
		if (sd_rphase) begin
			if (img_fd != 0) fbuf[sd_buff_addr] <= sd_buff_din;
			else disk[{sd_lba[10:0], 9'd0} + {23'd0, sd_buff_addr}] <= sd_buff_din;
			sd_rphase <= 0;
			if (sd_buff_addr == 9'd511) begin
				sd_ack <= 0;
				sd_wr_act <= 0;
				if (img_fd != 0) img_flush <= 1;
			end
			else sd_buff_addr <= sd_buff_addr + 1'd1;
		end
		else sd_rphase <= 1;
	end
	if (img_flush) begin : wrback
		integer k;
		img_flush <= 0;
		fr = $fseek(img_fd, {sd_lba, 9'd0}, 0);
		for (k = 0; k < 512; k = k + 1) $fwrite(img_fd, "%c", fbuf[k]);
		$fflush(img_fd);
	end
end

// +walktrace: every MMU table-walker transaction, and every access
// error the walker reports.  With a real disk image this is what turns
// a kernel "MMU invalid descriptor" panic into the exact descriptor
// address and value that produced it.
reg wtrace = 0;
initial wtrace = $test$plusargs("walktrace");

// a small ring of the most recent walker transactions, so when a walk
// faults we can print the whole descriptor chain that led there
reg [95:0] wring [0:31];      // {we, addr[31:0], data[31:0]} loosely packed
integer    wrp = 0;
reg        w_ack_d = 0;

localparam W_FLT_ST = 4'd9;
reg [3:0]  wst_d = 0;
integer    faults_seen = 0;

task dump_walk_ring;
	integer k, idx;
	begin
		$display("  --- last walker transactions ---");
		for (k = 31; k >= 0; k = k - 1) begin
			idx = (wrp - k + 64) % 32;
			if (wring[idx] !== 96'bx)
				$display("    %s %08x %s %08x",
				         wring[idx][64] ? "wr" : "rd",
				         wring[idx][63:32],
				         wring[idx][64] ? "=" : "->",
				         wring[idx][31:0]);
		end
	end
endtask

always @(posedge clk) if (!reset) begin
	// record every completed walker transaction
	if (dut.walker_req && dut.walker_ack) begin
		wring[wrp % 32] <= {31'd0, dut.walker_we, dut.walker_addr,
		                    dut.walker_we ? dut.walker_wdat : dut.walker_data};
		wrp <= wrp + 1;
		if (wtrace &&
		    (dut.walker_addr[31:24] == 8'h04 || dut.walker_addr[31:24] == 8'h05 ||
		     dut.walker_addr[31:24] == 8'h06 || dut.walker_addr[31:24] == 8'h07))
			// only near the tables the kernel actually uses
			;
	end

	// the MMU walker just latched an invalid descriptor: report the
	// faulting VA, the descriptor chain, and freeze a screen
	wst_d <= dut.cpu.mmu.wst;
	if (dut.cpu.mmu.wst == W_FLT_ST && wst_d != W_FLT_ST && !dut.cpu.mmu.w_pt) begin : mmufault
		reg [31:0] da;
		reg [31:0] memword;
		faults_seen = faults_seen + 1;
		da = dut.cpu.mmu.w_desc_addr;
		// what the tb RAM actually holds at the faulting descriptor
		// address (walker reads via next_system's [25:2] word index)
		memword = ram_mem[da[25:2]];
		$display("[%0t] MMU FAULT #%0d: VA=%08x super=%b instr=%b",
		         $time, faults_seen, dut.cpu.mmu.w_la, dut.cpu.mmu.w_super,
		         dut.cpu.mmu.f_bank);
		$display("    faulting descriptor: value=%08x read-from=%08x  mem[@]=%08x  %s",
		         dut.cpu.mmu.w_desc, da, memword,
		         (dut.cpu.mmu.w_desc === memword) ? "(walker read matches memory)"
		                                          : "(MISMATCH: walker read != memory)");
		$display("    srp=%08x urp=%08x tc=%08x",
		         dut.cpu.mmu.srp, dut.cpu.mmu.urp, dut.cpu.mmu.tc);
		if (dut.cpu.mmu.w_la[31:24] == 8'h3c) begin
			dump_walk_ring;
			$display("[%0t] BOOT: the panic fault reproduced", $time);
			fb_dump;
			halt_run <= 1;
		end
	end
	if (dut.walker_req && dut.walker_berr)
		$display("[%0t] WALK BERR %08x", $time, dut.walker_addr);
end

// ESP activity trace: selection commands and DMA writes to memory
reg saw_esp_sel = 0;
integer scsi_dma_writes = 0;
reg [7:0] esp_cmd_d = 0;
always @(posedge clk) if (!reset && bootsd) begin
	if (dut.scsi.command0 != esp_cmd_d) begin
		esp_cmd_d <= dut.scsi.command0;
		if ((dut.scsi.command0 & 8'h7F) == 8'h41 ||
		    (dut.scsi.command0 & 8'h7F) == 8'h42) begin
			if (!saw_esp_sel)
				$display("[%0t] BOOT: ESP select command %02x, target %0d",
				         $time, dut.scsi.command0, dut.scsi.selectbusid[2:0]);
			saw_esp_sel <= 1;
		end
	end
	if (dut.sc_m_req && dut.sc_m_we && dut.sc_m_ack)
		scsi_dma_writes = scsi_dma_writes + 1;
end

// 1120x832 2bpp NeXT gray to PGM: 0 = white, 3 = black, line pitch
// 288 bytes (1120/4 active plus 8 pad), even address byte in mem_hi
task fb_dump;
	integer fd, y, xb, p;
	reg [7:0] b;
	begin
		fd = $fopen("build/fb.pgm", "wb");
		$fwrite(fd, "P5\n1120 832\n255\n");
		for (y = 0; y < 832; y = y + 1) begin
			for (xb = 0; xb < 280; xb = xb + 1) begin : row
				integer ba;
				ba = y * 288 + xb;
				b = ba[0] ? dut.vram.mem_lo.mem[ba >> 1]
				          : dut.vram.mem_hi.mem[ba >> 1];
				for (p = 3; p >= 0; p = p - 1)
					$fwrite(fd, "%c", 8'd255 - {6'd0, b[2*p +: 2]} * 8'd85);
			end
		end
		$fclose(fd);
		$display("BOOT: framebuffer written to build/fb.pgm");
	end
endtask

initial begin
	if ($test$plusargs("dump")) begin
		$dumpfile("build/tb_next_boot.vcd");
		$dumpvars(0, tb_next_boot);
	end

	if ($test$plusargs("bootfd")) bootfd = 1;
	if ($test$plusargs("netoff")) net_enable = 0;

	if ($test$plusargs("bootsd")) begin
		bootsd = 1;
		@(posedge clk);
		img_mounted <= 1;
		@(posedge clk);
		img_mounted <= 0;
	end


	repeat (20) @(posedge clk);
	reset = 0;

	// the medium arrives once the machine is running, the way the OSD
	// delivers it: a mount pulse during reset is simply not seen
	if (bootfd) begin
		repeat (20) @(posedge clk);
		fimg_mounted <= 1;
		@(posedge clk);
		fimg_mounted <= 0;
	end

	// run length: +cycles=<n> for a small count, or +mcycles=<n> for
	// n million cycles (avoids the 32-bit cap of %d for long boots)
	begin : runlen
		integer mc;
		if ($value$plusargs("mcycles=%d", mc))
			run_cycles = mc * 1000000;
		else if (!$value$plusargs("cycles=%d", run_cycles))
			run_cycles = 300000;
	end
	while (run_cycles > 0 && !halt_run) begin
		@(posedge clk);
		run_cycles = run_cycles - 1;
	end

	$display("");
	$display("=== tb_next_boot results ===");
	$display("first accesses: %08x %08x %08x %08x",
	         first_addr[0], first_addr[1], first_addr[2], first_addr[3]);

	check(seen_vec_lo,  "reset SP fetched from ROM address 0");
	check(seen_vec_pc,  "reset PC fetched from ROM address 4");
	check(seen_entry,   "PC reached reset vector target 0x0100001E");
	check(seen_scr1,    "ROM read SCR1");
	check(scr1_ok,      "SCR1 returned machine id 0x00012052");
	check(seen_scr2_wr, "ROM wrote SCR2");
	check(!dbg_halted,  "CPU not halted");

	$display("mailbox: %0d reads, %0d writes, %0d frames pushed at the guest",
	         mbox_reads, mbox_writes, injected);
	if (!net_enable) begin
		// The bridge may invalidate the mailbox once so the host daemon
		// sees no magic, but it must never READ it: reading is how a
		// stale ring pointer left in DDR3 turns into frames the guest
		// never asked for.
		check(mbox_reads == 0,
		      "network off: the bridge reads no mailbox state");
		check(mbox_writes <= 8,
		      "network off: at most a one-off invalidation is written");
		check(injected == 0,
		      "network off: no frame reaches the guest");
	end
	else begin
		check(injected == 0,
		      "a stale mailbox injects no frames into the guest");
	end


	if (bootfd) begin
		$display("floppy register accesses: %0d, sectors fetched: %0d",
		         fdreg_n, fsd_reads);
		fb_dump;
	end

	if (bootsd) begin
		$display("SD reads: %0d, SCSI DMA words to memory: %0d",
		         sd_reads, scsi_dma_writes);
		check(saw_esp_sel, "boot: ROM selected the SCSI disk");
		check(sd_lba0, "boot: sector 0 fetched from the SD image");
		check(scsi_dma_writes >= 128, "boot: a full sector reached memory by DMA");
		fb_dump;
	end

	if (errors != 0 || $test$plusargs("iotrace")) dump_state;

	if (errors == 0) $display("ALL PASS");
	else             $display("%0d FAILURES", errors);
	$finish;
end

endmodule
