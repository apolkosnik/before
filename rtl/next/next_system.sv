//============================================================================
//  NeXT system glue: AP68040 CPU, address decode, devices
//
//  Target machine: NeXTcube 68040 25MHz, monochrome (SCR1 0x00012052),
//  the machine Previous calls NEXT_CUBE040 (non-turbo).
//
//  Memory map, from Previous src/cpu/memory.c:
//    0x00000000-0x0001FFFF  boot ROM (128 KB)
//    0x01000000-0x0101FFFF  boot ROM mirror (reset vectors point here)
//    0x02000000-0x0201FFFF  device space
//    0x020C0000-0x020CFFFF  BMAP chip (also aliased at 0x820C0000)
//    0x02100000-0x0211FFFF  device space mirror (BMAP access path)
//    0x04000000-0x07FFFFFF  main RAM, 4 banks (served by the ram_* port)
//    0x0B000000-0x0B03FFFF  VRAM
//    0x0C000000-0x0F03FFFF  VRAM mirrors for memory write functions 0-3
//    0x10000000-0x1FFFFFFF  RAM mirrors for memory write functions 0-3
//    everything else        bus error (empty NeXTbus slots and so on)
//
//  The MWF mirrors are plain accesses for now (MWF0 = copy); the raster
//  op write functions are a TODO documented in docs/PORTING.md.
//
//  The CPU bus follows the TG68K-shaped protocol of ap040_tg68k_compat
//  (see rtl/AP68040/tb/tb_ap040_program.v): the host pulses mem_ready for
//  one cycle to complete an access, clkena_in = idle | mem_ready | berr.
//============================================================================

module next_system #(
	parameter CLK_HZ      = 100000000,
	// Internal (cached) CPU cycles advance NUM of every DEN clocks
	// (bus cycles are never gated).  The boot ROM's delay() is a
	// calibrated DBF loop, self-checked by the POST event counter test
	// against a 899..1100 us window for delay(1000): 6.25 loop
	// iterations per microsecond TICK of this module.  The core runs a
	// cached DBF-taken in ~8 clocks (measured in full-POST simulation),
	// so the calibration invariant is
	//     (CLK_HZ / 1e6) * (CPU_PACE_NUM / CPU_PACE_DEN) = 50
	// clocks per microsecond tick.  CLK_HZ defines the microsecond of
	// every timer in this system, so it may be a VIRTUAL microsecond:
	// the FPGA build runs the 32 MHz clock with CLK_HZ = 50 MHz and
	// pacing off, making the machine uniformly 64 percent of real time
	// but internally consistent with the calibration.  The defaults
	// below (100 MHz, 1/2) satisfy the same invariant.
	parameter CPU_PACE_NUM = 1,
	parameter CPU_PACE_DEN = 2,
	// physical clock rate, for the battery backed time of day (CLK_HZ
	// is the virtual rate the CPU calibration is built on)
	parameter CLK_REAL_HZ = CLK_HZ,
	parameter ROM_INIT_EN = 0,
	parameter ROM_INIT    = "rom.hex"
)
(
	input         clk,            // system clock: CPU, devices, RAM
	input  [10:0] ps2_key,       // MiSTer keyboard events (to the KMS)

	// boot device menu (to the NVRAM boot command, see next_scr)
	input   [2:0] boot_sel,

	// host time of day (hps_io RTC port)
	input  [64:0] hps_rtc,

	// floppy image (MiSTer SD block interface, slot 1)
	input         fimg_mounted,
	input         fimg_readonly,
	input  [63:0] fimg_size,
	output [31:0] fsd_lba,
	output        fsd_rd,
	output        fsd_wr,
	input         fsd_ack,
	input   [8:0] fsd_buff_addr,
	input   [7:0] fsd_buff_dout,
	output  [7:0] fsd_buff_din,
	input         fsd_buff_wr,

	// SCSI disk image (MiSTer SD block interface)
	input         img_mounted,
	input         img_readonly,
	input  [63:0] img_size,
	output [31:0] sd_lba,
	output        sd_rd,
	output        sd_wr,
	input         sd_ack,
	input   [8:0] sd_buff_addr,
	input   [7:0] sd_buff_dout,
	output  [7:0] sd_buff_din,
	input         sd_buff_wr,
	input         clk_vid,        // video clock: scan-out (100 MHz for
	                              // the 1600x912 pixel timing)
	input         reset,          // active high, synchronous

	// boot ROM download (byte stream)
	input         rom_wr,
	input  [16:0] rom_waddr,
	input   [7:0] rom_wdata,

	// video output (one pixel per clk)
	output        hsync,
	output        vsync,
	output        hblank,
	output        vblank,
	output  [7:0] gray,

	// main RAM port, 32 bit, 64 MB (level req, level ack; see NeXT.sv
	// and tb/ for the two implementations)
	output reg        ram_req,
	output reg        ram_we,
	output reg  [3:0] ram_be,     // [3] = MSB byte lane
	output reg [23:0] ram_addr,   // 32-bit word index
	output reg [31:0] ram_din,
	input      [31:0] ram_dout,
	input             ram_ack,

	output        led,

	// ethernet frame bridge (next_enet_bridge in the emu top)
	output        btx_req,
	output [10:0] btx_len,
	input  [10:0] btx_addr,
	input         btx_rd,
	output  [7:0] btx_q,
	output        btx_ack,
	input         btx_done,
	input         brx_start,
	input  [10:0] brx_len,
	input         brx_valid,
	input   [7:0] brx_data,
	output        brx_ready,
	output [47:0] enet_mac,

	// debug
	output [31:0] dbg_pc,
	output        dbg_halted,
	output  [2:0] dbg_ipl
);

//----------------------------------------------------------------------------
// CPU
//----------------------------------------------------------------------------

wire [15:0] cpu_din;
wire [31:0] cpu_addr;
wire [15:0] cpu_dout;
wire        nwr, nuds, nlds;
wire  [1:0] busstate;
wire        longword;
wire        nresetout;
wire  [2:0] fc;

wire        walker_req, walker_we;
wire [31:0] walker_addr, walker_wdat;
reg         walker_ack;
reg  [31:0] walker_data;
reg         walker_berr;

reg         mem_ready;
reg         berr_hold;

// snoop pulse for DMA writes into main RAM (one per acknowledged write)
reg        dma_snoop_stb;
reg [31:0] dma_snoop_addr;

// ethernet DMA master port (driven by next_enet_dma below)
wire        en_m_req, en_m_we;
wire [23:0] en_m_addr;
wire  [3:0] en_m_be;
wire [31:0] en_m_din;
wire        en_m_ack;
wire        int_en_tx, int_en_rx, int_en_tx_dma, int_en_rx_dma;

// MO/ECC DMA master port (driven by next_mo below)
wire        mo_m_req, mo_m_we;
wire [23:0] mo_m_addr;
wire  [3:0] mo_m_be;
wire [31:0] mo_m_din;
wire        mo_m_ack;
wire        int_disk, int_disk_dma;

// SCSI DMA master port (driven by next_scsi below)
wire        sc_m_req, sc_m_we;
wire [23:0] sc_m_addr;
wire  [3:0] sc_m_be;
wire [31:0] sc_m_din;
wire        sc_m_ack;

// KMS/sound DMA master port (driven by next_kms_snd below)
wire        sn_m_req, sn_m_we;
wire [23:0] sn_m_addr;
wire  [3:0] sn_m_be;
wire [31:0] sn_m_din;
wire        sn_m_ack;
wire        int_snd_ovrun, int_snd_out_dma, int_keymouse;

wire  [2:0] ipl_level;

// fractional pacing of internal cycles (see CPU_PACE_* above)
reg [$clog2(CPU_PACE_DEN)-1:0] pace_acc = 0;
wire pace = pace_acc < CPU_PACE_NUM;
always @(posedge clk)
	pace_acc <= (pace_acc == CPU_PACE_DEN-1) ? 1'd0 : pace_acc + 1'd1;

wire clkena = ((busstate == 2'b01) & pace) | mem_ready | berr_hold;

wire [255:0] debug_status;
assign dbg_pc  = debug_status[31:0];
assign dbg_ipl = ipl_level;

ap040_tg68k_compat #(
	.AP040_HAS_MMU(1),
	.AP040_HAS_FPU(1),
	.AP040_ENABLE_CACHE(1)
) cpu
(
	.clk(clk),
	.nreset(~reset),
	.clkena_in(clkena),

	// Cacheability follows the real 68040 model: everything is a cache
	// candidate, gated by the CACR enables and the MMU cache-inhibit
	// attributes (the boot ROM runs with the instruction cache only;
	// the OS marks device pages noncacheable through the MMU).  The
	// Amiga-specific window inputs stay off.  DMA writes to main RAM
	// are snooped so the data cache never holds stale lines.
	.cache_allow_all(1'b1),
	.cache_snoop_stb(dma_snoop_stb),
	.cache_snoop_addr(dma_snoop_addr),
	.cache_z2_ena(1'b0),
	.cache_z3_base0(5'd0),
	.cache_z3_ena0(1'b0),
	.cache_z3_base1(4'd0),
	.cache_z3_ena1(1'b0),

	.data_in(cpu_din),
	.ipl(~ipl_level),
	.ipl_autovector(1'b1),
	.berr(berr_hold),

	.addr_out(cpu_addr),
	.data_write(cpu_dout),
	.nwr(nwr),
	.nuds(nuds),
	.nlds(nlds),
	.busstate(busstate),
	.longword(longword),
	.nresetout(nresetout),
	.fc(fc),

	.mmu_addr_log(),
	.mmu_addr_phys(),
	.mmu_cache_inhibit(),

	.walker_req(walker_req),
	.walker_we(walker_we),
	.walker_addr(walker_addr),
	.walker_wdat(walker_wdat),
	.walker_ack(walker_ack),
	.walker_data(walker_data),
	.walker_berr(walker_berr),

	.cache_req(),
	.cache_addr(),
	.cache_data(16'd0),
	.cache_ack(1'b0),
	.cache_burst(),
	.cache_burst_len(),
	.cache_ramaddr(),

	.cacr_out(),
	.vbr_out(),
	.debug_busy(),
	.debug_fault(),
	.debug_halted(dbg_halted),
	.debug_status(debug_status),
	.debug_status2()
);

// devices also see the RESET instruction
wire dev_reset = reset | ~nresetout;

//----------------------------------------------------------------------------
// address decode
//----------------------------------------------------------------------------

wire is_write = (busstate == 2'b11);
wire [1:0] lanes = {~nuds, ~nlds};

wire d_rom  = (cpu_addr[31:17] == 15'h0000) || (cpu_addr[31:17] == {8'h01, 7'd0});
wire d_io   = (cpu_addr[31:24] == 8'h02) &&
              ((cpu_addr[23:17] == 7'd0) || (cpu_addr[23:17] == 7'd8));
wire d_bmap = (cpu_addr[31:16] == 16'h020C) || (cpu_addr[31:16] == 16'h820C);
wire d_ram  = (cpu_addr[31:26] == 6'b000001) ||   // 0x04-0x07
              (cpu_addr[31:28] == 4'h1);          // 0x10-0x1F MWF mirrors
wire d_vram = (cpu_addr[31:24] == 8'h0B) ||
              (cpu_addr[31:26] == 6'b000011);     // 0x0C-0x0F MWF mirrors

wire d_any  = d_rom | d_io | d_bmap | d_ram | d_vram;

//----------------------------------------------------------------------------
// bus cycle state machine
//----------------------------------------------------------------------------

localparam S_IDLE = 2'd0, S_INT = 2'd1, S_RAM = 2'd2, S_RAM_E = 2'd3;

reg  [1:0] state;
localparam G_ENET = 2'd0, G_MO = 2'd1, G_SND = 2'd2, G_SCSI = 2'd3;
reg  [1:0] dma_grant;
reg        sel_rom, sel_vram, sel_io, sel_bmap;
reg [15:0] cyc_rdata;

wire cpu_req = (busstate != 2'b01) && !mem_ready && !berr_hold;

// walker service (the core never runs walker and CPU bus cycles at the
// same time, see tb_ap040_program.v)
wire walker_is_ram = (walker_addr[31:26] == 6'b000001) || (walker_addr[31:28] == 4'h1);
reg  walker_armed;
reg  walker_busy;

// internal read data mux (valid in cycle S_INT)
wire [15:0] rom_q, vram_q, io_rdata, bmap_rdata;
wire        bmap_tpe_select;

assign cpu_din = cyc_rdata;

assign en_m_ack = (state == S_RAM_E) && (dma_grant == G_ENET) && ram_ack;
assign mo_m_ack = (state == S_RAM_E) && (dma_grant == G_MO) && ram_ack;
assign sn_m_ack = (state == S_RAM_E) && (dma_grant == G_SND) && ram_ack;
assign sc_m_ack = (state == S_RAM_E) && (dma_grant == G_SCSI) && ram_ack;

always @(posedge clk) begin
	mem_ready  <= 0;
	walker_ack <= 0;
	walker_berr<= 0;

	dma_snoop_stb <= 0;
	if (reset) begin
		state <= S_IDLE;
		berr_hold <= 0;
		{sel_rom, sel_vram, sel_io, sel_bmap} <= 0;
		ram_req <= 0;
		walker_armed <= 1;
		walker_busy <= 0;
	end
	else begin
		if (berr_hold && busstate == 2'b01) berr_hold <= 0;
		if (!walker_req) walker_armed <= 1;

		case (state)
		S_IDLE: begin
			{sel_rom, sel_vram, sel_io, sel_bmap} <= 0;
			if (walker_req && walker_armed) begin
				walker_armed <= 0;
				if (!walker_is_ram) walker_berr <= 1;
				else begin
					walker_busy <= 1;
					ram_req  <= 1;
					ram_we   <= walker_we;
					ram_be   <= 4'b1111;
					ram_addr <= walker_addr[25:2];
					ram_din  <= walker_wdat;
					state    <= S_RAM;
				end
			end
			else if (en_m_req && !cpu_req && !berr_hold) begin
				ram_req  <= 1;
				ram_we   <= en_m_we;
				ram_be   <= en_m_be;
				ram_addr <= en_m_addr;
				ram_din  <= en_m_din;
				dma_grant <= G_ENET;
				state    <= S_RAM_E;
			end
			else if (mo_m_req && !cpu_req && !berr_hold) begin
				ram_req  <= 1;
				ram_we   <= mo_m_we;
				ram_be   <= mo_m_be;
				ram_addr <= mo_m_addr;
				ram_din  <= mo_m_din;
				dma_grant <= G_MO;
				state    <= S_RAM_E;
			end
			else if (sn_m_req && !cpu_req && !berr_hold) begin
				ram_req  <= 1;
				ram_we   <= sn_m_we;
				ram_be   <= sn_m_be;
				ram_addr <= sn_m_addr;
				ram_din  <= sn_m_din;
				dma_grant <= G_SND;
				state    <= S_RAM_E;
			end
			else if (sc_m_req && !cpu_req && !berr_hold) begin
				ram_req  <= 1;
				ram_we   <= sc_m_we;
				ram_be   <= sc_m_be;
				ram_addr <= sc_m_addr;
				ram_din  <= sc_m_din;
				dma_grant <= G_SCSI;
				state    <= S_RAM_E;
			end
			else if (cpu_req && !berr_hold) begin
				if (!d_any) berr_hold <= 1;
				else if (d_ram) begin
					ram_req  <= 1;
					ram_we   <= is_write;
					ram_be   <= cpu_addr[1] ? {2'b00, lanes} : {lanes, 2'b00};
					ram_addr <= cpu_addr[25:2];
					ram_din  <= {cpu_dout, cpu_dout};
					state    <= S_RAM;
				end
				else begin
					sel_rom  <= d_rom;
					sel_vram <= d_vram;
					sel_io   <= d_io;
					sel_bmap <= d_bmap;
					state    <= S_INT;
				end
			end
		end

		S_INT: begin
			cyc_rdata <= sel_rom  ? rom_q :
			             sel_vram ? vram_q :
			             sel_bmap ? bmap_rdata : io_rdata;
			mem_ready <= 1;
			{sel_rom, sel_vram, sel_io, sel_bmap} <= 0;
			state <= S_IDLE;
		end

		S_RAM: begin
			if (ram_ack) begin
				ram_req <= 0;
				if (walker_busy) begin
					walker_busy <= 0;
					walker_ack  <= 1;
					walker_data <= ram_dout;
				end
				else begin
					cyc_rdata <= cpu_addr[1] ? ram_dout[15:0] : ram_dout[31:16];
					mem_ready <= 1;
				end
				state <= S_IDLE;
			end
		end

		S_RAM_E: begin
			if (ram_ack) begin
				ram_req <= 0;
				state <= S_IDLE;
				if (ram_we) begin
					dma_snoop_stb <= 1;
					dma_snoop_addr <= {6'b000001, ram_addr, 2'b00};
				end
			end
		end

		default: state <= S_IDLE;
		endcase
	end
end

//----------------------------------------------------------------------------
// boot ROM
//----------------------------------------------------------------------------

next_rom #(.ROM_INIT_EN(ROM_INIT_EN), .ROM_INIT(ROM_INIT)) rom
(
	.clk(clk),
	.a_addr(cpu_addr[16:1]),
	.a_q(rom_q),
	.wr(rom_wr),
	.w_addr(rom_waddr),
	.w_din(rom_wdata)
);

//----------------------------------------------------------------------------
// VRAM and video
//----------------------------------------------------------------------------

wire [16:0] scan_addr;
wire [15:0] scan_q;

next_vram vram
(
	.clk_a(clk),
	.a_addr(cpu_addr[17:1]),
	.a_we({2{sel_vram & is_write}} & lanes),
	.a_din(cpu_dout),
	.a_q(vram_q),
	.clk_b(clk_vid),
	.b_addr(scan_addr),
	.b_q(scan_q)
);

next_video video
(
	.clk(clk_vid),
	.hsync(hsync),
	.vsync(vsync),
	.hblank(hblank),
	.vblank(vblank),
	.gray(gray),
	.vbl(),
	.vram_addr(scan_addr),
	.vram_q(scan_q)
);

// vertical blank into the system clock domain: synchronize the level,
// pulse on the rising edge
reg [2:0] vblank_sync;
always @(posedge clk) vblank_sync <= {vblank_sync[1:0], vblank};
wire vbl = vblank_sync[1] & ~vblank_sync[2];

//----------------------------------------------------------------------------
// device space
//----------------------------------------------------------------------------

wire [16:0] io_off = cpu_addr[16:0];

wire io_enet  = sel_io && !io_off[16] && (
                (io_off[15:4]  == 12'h600)  ||                  // 0x6000-0x600f MB8795
                (io_off[15:2]  == 14'h044)  ||                  // 0x0110 EN_TX CSR
                (io_off[15:2]  == 14'h054)  ||                  // 0x0150 EN_RX CSR
                (io_off[15:5]  == 11'h208)  ||                  // 0x4100-0x411f
                (io_off[15:5]  == 11'h20A)  ||                  // 0x4140-0x415f
                (io_off[15:2]  == 14'h10C4) ||                  // 0x4310
                (io_off[15:2]  == 14'h10D4));                   // 0x4350
wire io_mo_osp = sel_io && (io_off[16:5] == 12'h900);           // 0x12000-0x1201f
wire io_mo_csr = sel_io && (io_off[16:2] == 15'h0014);          // 0x00050-0x00053
wire io_mo_ptr = sel_io && (io_off[16:4] == 13'h405);           // 0x04050-0x0405f
wire io_mo_ini = sel_io && (io_off[16:2] == 15'h1094);          // 0x04250-0x04253
wire io_mo     = io_mo_osp | io_mo_csr | io_mo_ptr | io_mo_ini;
wire io_kms    = sel_io && (io_off[16:4] == 13'hE00);           // 0x0e000-0x0e00f
wire io_sn_csr = sel_io && (io_off[16:2] == 15'h0010);          // 0x00040-0x00043
wire io_sn_sptr= sel_io && (io_off[16:4] == 13'h403);           // 0x04030-0x0403f
wire io_sn_ptr = sel_io && (io_off[16:4] == 13'h404);           // 0x04040-0x0404f
wire io_sn_ini = sel_io && (io_off[16:2] == 15'h1090);          // 0x04240-0x04243
wire io_snd    = io_kms | io_sn_csr | io_sn_sptr | io_sn_ptr | io_sn_ini;
wire io_sc_csr = sel_io && (io_off[16:2] == 15'h0004);          // 0x00010-0x00013
wire io_sc_sptr= sel_io && (io_off[16:4] == 13'h400);           // 0x04000-0x0400f
wire io_sc_ptr = sel_io && (io_off[16:4] == 13'h401);           // 0x04010-0x0401f
wire io_sc_ini = sel_io && (io_off[16:2] == 15'h1084);          // 0x04210-0x04213
wire io_scsi   = io_sc_csr | io_sc_sptr | io_sc_ptr | io_sc_ini;
wire io_dma   = sel_io && (io_off[16:12] < 5'h05) && !io_enet && !io_mo && !io_snd && !io_scsi; // 0x00000-0x04FFF
wire io_intc  = sel_io && (io_off[16:12] == 5'h07);             // 0x07000-0x07FFF
wire io_scr1  = sel_io && (io_off[16:11] == 6'h18);             // 0x0c000-0x0c7ff
wire io_sid   = sel_io && (io_off[16:11] == 6'h19);             // 0x0c800-0x0cfff
wire io_scr2  = sel_io && (io_off[16:11] == 6'h1A);             // 0x0d000-0x0d7ff
wire io_timer = sel_io && (io_off[16:12] == 5'h16);             // 0x16000-0x16fff
wire io_scc   = sel_io && (io_off[16:3]  == 14'h3000);          // 0x18000-0x18007
wire io_esp   = sel_io && (io_off[16:6]  == 11'h500);           // 0x14000-0x1403f
wire io_flp   = sel_io && (io_off[16:4]  == 13'h1410);          // 0x14100-0x1410f
wire io_evt   = sel_io && (io_off[16:12] == 5'h1a);             // 0x1a000-0x1afff
wire io_scr   = io_scr1 | io_sid | io_scr2;

wire [15:0] scr_rdata, intc_rdata, timer_rdata, dma_rdata, scc_rdata, esp_rdata, enet_rdata, mo_rdata, snd_rdata;
wire [15:0] flp_rdata;

// event counter: 20-bit microsecond counter, latched when its first byte
// is read, reset by a write (System_Timer_Read/Write in Previous
// src/sysReg.c: value is (host_time_us() - offset) & 0xFFFFF)
localparam US_DIV = CLK_HZ / 1000000;
reg [$clog2(US_DIV)-1:0] us_presc;
wire us_tick = (us_presc == US_DIV-1);
reg [19:0] us_counter;
reg [19:0] evt_latch;

always @(posedge clk) begin
	if (reset) begin
		us_presc <= 0;
		us_counter <= 0;
		evt_latch <= 0;
	end
	else begin
		us_presc <= us_tick ? 1'd0 : us_presc + 1'd1;
		if (us_tick) us_counter <= us_counter + 1'd1;
		if (io_evt && !is_write && !cpu_addr[1]) evt_latch <= us_counter;
		if (io_evt && is_write) us_counter <= 0;
	end
end

wire [15:0] evt_rdata = cpu_addr[1] ? evt_latch[15:0] : {12'd0, us_counter[19:16]};

assign io_rdata = io_enet  ? enet_rdata :
                  io_mo    ? mo_rdata :
                  io_snd   ? snd_rdata :
                  io_scsi  ? esp_rdata :
                  io_dma   ? dma_rdata :
                  io_intc  ? intc_rdata :
                  io_scr   ? scr_rdata :
                  io_timer ? timer_rdata :
                  io_scc   ? scc_rdata :
                  io_esp   ? esp_rdata :
                  io_flp   ? flp_rdata :
                  io_evt   ? evt_rdata : 16'h0000;

// system control registers and RTC
wire timer_ipl7, softint1, softint2;

// survives reset: the mount pulse fires once at OSD time, usually
// before the user resets into the new configuration
reg disk_mounted = 0;
always @(posedge clk) if (img_mounted) disk_mounted <= (img_size != 0);

reg floppy_mounted = 0;
always @(posedge clk) if (fimg_mounted) floppy_mounted <= (fimg_size != 0);

next_scr #(.CLK_HZ(CLK_HZ), .CLK_REAL_HZ(CLK_REAL_HZ)) scr
(
	.clk(clk),
	.reset(dev_reset),
	.sel(io_scr),
	.reg_id(io_scr1 ? 2'd0 : io_sid ? 2'd1 : 2'd2),
	.addr1(cpu_addr[1]),
	.we(is_write),
	.be(lanes),
	.wdata(cpu_dout),
	.rdata(scr_rdata),
	.scr1(32'h00012052),         // 25MHz NeXTcube 68040, 100ns memory
	.boot_sel(boot_sel),
	.disk_mounted(disk_mounted),
	.floppy_mounted(floppy_mounted),
	.hps_rtc(hps_rtc),
	.timer_ipl7(timer_ipl7),
	.led(led),
	.rom_overlay(),
	.softint1(softint1),
	.softint2(softint2)
);

// hardclock
wire timer_set, timer_clr;

next_timer #(.CLK_HZ(CLK_HZ)) timer
(
	.clk(clk),
	.reset(dev_reset),
	.sel(io_timer),
	.addr(cpu_addr[2:0]),
	.we(is_write),
	.be(lanes),
	.wdata(cpu_dout),
	.rdata(timer_rdata),
	.rd(~is_write),
	.int_set(timer_set),
	.int_clr(timer_clr)
);

// DMA stub with the frame interrupt path
wire vid_int_set, vid_int_clr;

next_dma_stub dma
(
	.clk(clk),
	.reset(dev_reset),
	.sel(io_dma),
	.addr(io_off[14:0]),
	.we(is_write),
	.be(lanes),
	.wdata(cpu_dout),
	.rdata(dma_rdata),
	.vbl(vbl),
	.vid_int_set(vid_int_set),
	.vid_int_clr(vid_int_clr)
);

next_enet_dma #(.CLK_HZ(CLK_HZ)) enet
(
	.clk(clk),
	.reset(dev_reset),
	.sel(io_enet),
	.addr(io_off[14:0]),
	.we(is_write),
	.be(lanes),
	.wdata(cpu_dout),
	.rdata(enet_rdata),
	.m_req(en_m_req),
	.m_we(en_m_we),
	.m_addr(en_m_addr),
	.m_be(en_m_be),
	.m_din(en_m_din),
	.m_dout(ram_dout),
	.m_ack(en_m_ack),
	.tpe_select(bmap_tpe_select),
	.btx_req(btx_req),
	.btx_len(btx_len),
	.btx_addr(btx_addr),
	.btx_rd(btx_rd),
	.btx_q(btx_q),
	.btx_ack(btx_ack),
	.btx_done(btx_done),
	.brx_start(brx_start),
	.brx_len(brx_len),
	.brx_valid(brx_valid),
	.brx_data(brx_data),
	.brx_ready(brx_ready),
	.enet_mac(enet_mac),
	.int_en_tx(int_en_tx),
	.int_en_rx(int_en_rx),
	.int_en_tx_dma(int_en_tx_dma),
	.int_en_rx_dma(int_en_rx_dma)
);

// MO drive controller with the ECC buffer engine (a RAM bus master)
next_mo #(.CLK_HZ(CLK_HZ)) mo
(
	.clk(clk),
	.reset(dev_reset),
	.sel_osp(io_mo_osp),
	.sel_csr(io_mo_csr),
	.sel_ptr(io_mo_ptr),
	.sel_ini(io_mo_ini),
	.addr(io_off[4:0]),
	.we(is_write),
	.be(lanes),
	.wdata(cpu_dout),
	.rdata(mo_rdata),
	.m_req(mo_m_req),
	.m_we(mo_m_we),
	.m_addr(mo_m_addr),
	.m_be(mo_m_be),
	.m_din(mo_m_din),
	.m_dout(ram_dout),
	.m_ack(mo_m_ack),
	.int_disk(int_disk),
	.int_disk_dma(int_disk_dma)
);

// KMS and sound out DMA (a RAM bus master)
next_kms_snd #(.CLK_HZ(CLK_HZ)) kms_snd
(
	.clk(clk),
	.reset(dev_reset),
	.ps2_key(ps2_key),
	.sel_kms(io_kms),
	.sel_csr(io_sn_csr),
	.sel_sptr(io_sn_sptr),
	.sel_ptr(io_sn_ptr),
	.sel_ini(io_sn_ini),
	.addr(io_off[3:0]),
	.we(is_write),
	.be(lanes),
	.wdata(cpu_dout),
	.rdata(snd_rdata),
	.m_req(sn_m_req),
	.m_we(sn_m_we),
	.m_addr(sn_m_addr),
	.m_be(sn_m_be),
	.m_din(sn_m_din),
	.m_dout(ram_dout),
	.m_ack(sn_m_ack),
	.int_snd_ovrun(int_snd_ovrun),
	.int_snd_out_dma(int_snd_out_dma),
	.int_keymouse(int_keymouse)
);

// Floppy drive: an 82077 whose sector data rides the SCSI DMA channel
wire        int_floppy, flp_select;
wire        flp_req, flp_wr, flp_bwe, flp_done;
wire [10:0] flp_len;
wire  [9:0] flp_addr;
wire  [7:0] flp_bwdata, flp_bq;

next_floppy #(.CLK_HZ(CLK_HZ)) floppy
(
	.clk(clk),
	.reset(dev_reset),
	.sel(io_flp),
	.addr(io_off[3:0]),
	.we(is_write),
	.be(lanes),
	.wdata(cpu_dout),
	.rdata(flp_rdata),
	.int_floppy(int_floppy),
	.flp_select(flp_select),
	.mo_gpo(1'b0),
	.buf_addr(flp_addr),
	.buf_we(flp_bwe),
	.buf_wdata(flp_bwdata),
	.buf_q(flp_bq),
	.buf_len(flp_len),
	.dma_req(flp_req),
	.dma_wr(flp_wr),
	.dma_done(flp_done),
	.img_mounted(fimg_mounted),
	.img_readonly(fimg_readonly),
	.img_size(fimg_size),
	.sd_lba(fsd_lba),
	.sd_rd(fsd_rd),
	.sd_wr(fsd_wr),
	.sd_ack(fsd_ack),
	.sd_buff_addr(fsd_buff_addr),
	.sd_buff_dout(fsd_buff_dout),
	.sd_buff_din(fsd_buff_din),
	.sd_buff_wr(fsd_buff_wr)
);

// SCSI controller, disk target and DMA channel
wire esp_int_scsi, int_scsi_dma;

next_scsi #(.CLK_HZ(CLK_HZ)) scsi
(
	.clk(clk),
	.reset(dev_reset),
	.sel_esp(io_esp),
	.sel_csr(io_sc_csr),
	.sel_sptr(io_sc_sptr),
	.sel_ptr(io_sc_ptr),
	.sel_ini(io_sc_ini),
	.addr(io_off[5:0]),
	.we(is_write),
	.be(lanes),
	.wdata(cpu_dout),
	.rdata(esp_rdata),
	.m_req(sc_m_req),
	.m_we(sc_m_we),
	.m_addr(sc_m_addr),
	.m_be(sc_m_be),
	.m_din(sc_m_din),
	.m_dout(ram_dout),
	.m_ack(sc_m_ack),
	.flp_select(flp_select),
	.flp_req(flp_req),
	.flp_wr(flp_wr),
	.flp_len(flp_len),
	.flp_addr(flp_addr),
	.flp_bwe(flp_bwe),
	.flp_bwdata(flp_bwdata),
	.flp_bq(flp_bq),
	.flp_done(flp_done),
	.int_scsi(esp_int_scsi),
	.int_scsi_dma(int_scsi_dma),
	.img_mounted(img_mounted),
	.img_readonly(img_readonly),
	.img_size(img_size),
	.sd_lba(sd_lba),
	.sd_rd(sd_rd),
	.sd_wr(sd_wr),
	.sd_ack(sd_ack),
	.sd_buff_addr(sd_buff_addr),
	.sd_buff_dout(sd_buff_dout),
	.sd_buff_din(sd_buff_din),
	.sd_buff_wr(sd_buff_wr)
);

// SCC serial controller
next_scc scc_dev
(
	.clk(clk),
	.reset(dev_reset),
	.sel(io_scc),
	.addr1(cpu_addr[1]),
	.we(is_write),
	.be(lanes),
	.wdata(cpu_dout),
	.rdata(scc_rdata)
);

// BMAP
next_bmap bmap
(
	.clk(clk),
	.reset(dev_reset),
	.sel(sel_bmap),
	.addr(cpu_addr[5:0]),
	.we(is_write),
	.be(lanes),
	.wdata(cpu_dout),
	.rdata(bmap_rdata),
	.tpe_select(bmap_tpe_select)
);

// interrupt controller
reg soft1_d, soft2_d, scsi_d, flp_d;
reg entx_d, enrx_d, entxd_d, enrxd_d, disk_d, diskd_d, sndo_d, sndd_d, km_d;
reg scsid_d;
always @(posedge clk) begin
	soft1_d <= softint1;
	soft2_d <= softint2;
	scsi_d  <= esp_int_scsi;
	flp_d   <= int_floppy;
	scsid_d <= int_scsi_dma;
	entx_d  <= int_en_tx;
	enrx_d  <= int_en_rx;
	entxd_d <= int_en_tx_dma;
	enrxd_d <= int_en_rx_dma;
	disk_d  <= int_disk;
	diskd_d <= int_disk_dma;
	sndo_d  <= int_snd_ovrun;
	sndd_d  <= int_snd_out_dma;
	km_d    <= int_keymouse;
end

wire [31:0] int_set =
	(32'd1  & {31'd0,  softint1 & ~soft1_d}) |
	({31'd0, softint2 & ~soft2_d} << 1) |
	({31'd0, int_keymouse & ~km_d} << 3) |
	({31'd0, vid_int_set} << 5) |
	({31'd0, int_floppy & ~flp_d} << 7) |
	({31'd0, int_snd_ovrun & ~sndo_d} << 8) |
	({31'd0, int_en_rx & ~enrx_d} << 9) |
	({31'd0, int_en_tx & ~entx_d} << 10) |
	({31'd0, esp_int_scsi & ~scsi_d} << 12) |
	({31'd0, int_disk & ~disk_d} << 13) |
	({31'd0, int_snd_out_dma & ~sndd_d} << 23) |
	({31'd0, int_disk_dma & ~diskd_d} << 25) |
	({31'd0, int_scsi_dma & ~scsid_d} << 26) |
	({31'd0, int_en_rx_dma & ~enrxd_d} << 27) |
	({31'd0, int_en_tx_dma & ~entxd_d} << 28) |
	({31'd0, timer_set} << 29);

wire [31:0] int_clr =
	({31'd0, ~softint1 & soft1_d}) |
	({31'd0, ~softint2 & soft2_d} << 1) |
	({31'd0, ~int_keymouse & km_d} << 3) |
	({31'd0, vid_int_clr} << 5) |
	({31'd0, ~int_floppy & flp_d} << 7) |
	({31'd0, ~int_snd_ovrun & sndo_d} << 8) |
	({31'd0, ~int_en_rx & enrx_d} << 9) |
	({31'd0, ~int_en_tx & entx_d} << 10) |
	({31'd0, ~esp_int_scsi & scsi_d} << 12) |
	({31'd0, ~int_disk & disk_d} << 13) |
	({31'd0, ~int_snd_out_dma & sndd_d} << 23) |
	({31'd0, ~int_disk_dma & diskd_d} << 25) |
	({31'd0, ~int_scsi_dma & scsid_d} << 26) |
	({31'd0, ~int_en_rx_dma & enrxd_d} << 27) |
	({31'd0, ~int_en_tx_dma & entxd_d} << 28) |
	({31'd0, timer_clr} << 29);

next_intc intc
(
	.clk(clk),
	.reset(dev_reset),
	.int_set(int_set),
	.int_clr(int_clr),
	.timer_ipl7(timer_ipl7),
	.sel(io_intc),
	.reg_mask(io_off[11]),
	.addr1(cpu_addr[1]),
	.we(is_write),
	.be(lanes),
	.wdata(cpu_dout),
	.rdata(intc_rdata),
	.ipl(ipl_level)
);

endmodule
