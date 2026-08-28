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
reg  [31:0] ram_dout;
reg         ram_ack;

wire        hsync, vsync, hblank, vblank;
wire  [7:0] gray;
wire        led;
wire [31:0] dbg_pc;
wire        dbg_halted;
wire  [2:0] dbg_ipl;

next_system #(
	.CLK_HZ(100000000),
	.ROM_INIT_EN(1),
	.ROM_INIT("build/rom.hex")
) dut
(
	.clk(clk),
	.clk_vid(clk),   // both domains on one clock in simulation
	.reset(reset),

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

reg [31:0] ram_mem [0:1048575];   // 4 MB

wire        bank0     = (ram_addr[23:22] == 2'b00);
wire [19:0] ram_index = ram_addr[19:0];              // alias within the bank
wire [31:0] open_bus  = {6'b000001, ram_addr, 2'b00};

reg [1:0] ram_wait;
always @(posedge clk) begin
	if (reset) begin
		ram_ack <= 0;
		ram_wait <= 0;
	end
	else if (!ram_req) begin
		ram_ack <= 0;
		ram_wait <= 0;
	end
	else if (!ram_ack) begin
		// a few wait states, like the DDR3 on hardware
		if (ram_wait == 2'd3) begin
			if (ram_we) begin
				if (bank0) begin
					if (ram_be[3]) ram_mem[ram_index][31:24] <= ram_din[31:24];
					if (ram_be[2]) ram_mem[ram_index][23:16] <= ram_din[23:16];
					if (ram_be[1]) ram_mem[ram_index][15:8]  <= ram_din[15:8];
					if (ram_be[0]) ram_mem[ram_index][7:0]   <= ram_din[7:0];
				end
			end
			else ram_dout <= bank0 ? ram_mem[ram_index] : open_bus;
			ram_ack <= 1;
		end
		else ram_wait <= ram_wait + 1'd1;
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
integer run_cycles;

task check;
	input cond;
	input [255:0] name;
	begin
		if (cond) $display("PASS: %0s", name);
		else begin
			$display("FAIL: %0s", name);
			errors = errors + 1;
		end
	end
endtask

initial begin
	if ($test$plusargs("dump")) begin
		$dumpfile("build/tb_next_boot.vcd");
		$dumpvars(0, tb_next_boot);
	end

	repeat (20) @(posedge clk);
	reset = 0;

	// 3 ms of system time by default, more with +cycles=<n>
	if (!$value$plusargs("cycles=%d", run_cycles)) run_cycles = 300000;
	repeat (run_cycles) @(posedge clk);

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

	if (errors != 0 || $test$plusargs("iotrace")) dump_state;

	if (errors == 0) $display("ALL PASS");
	else             $display("%0d FAILURES", errors);
	$finish;
end

endmodule
