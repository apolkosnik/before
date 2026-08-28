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
	parameter ROM_INIT_EN = 0,
	parameter ROM_INIT    = "rom.hex"
)
(
	input         clk,            // system clock: CPU, devices, RAM
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

wire  [2:0] ipl_level;

wire clkena = (busstate == 2'b01) | mem_ready | berr_hold;

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

	// no cacheable windows declared yet: the core runs uncached
	.cache_allow_all(1'b0),
	.cache_snoop_stb(1'b0),
	.cache_snoop_addr(32'd0),
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

localparam S_IDLE = 2'd0, S_INT = 2'd1, S_RAM = 2'd2;

reg  [1:0] state;
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

assign cpu_din = cyc_rdata;

always @(posedge clk) begin
	mem_ready  <= 0;
	walker_ack <= 0;
	walker_berr<= 0;

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

wire io_dma   = sel_io && (io_off[16:12] < 5'h05);              // 0x00000-0x04FFF
wire io_intc  = sel_io && (io_off[16:12] == 5'h07);             // 0x07000-0x07FFF
wire io_scr1  = sel_io && (io_off[16:11] == 6'h18);             // 0x0c000-0x0c7ff
wire io_sid   = sel_io && (io_off[16:11] == 6'h19);             // 0x0c800-0x0cfff
wire io_scr2  = sel_io && (io_off[16:11] == 6'h1A);             // 0x0d000-0x0d7ff
wire io_timer = sel_io && (io_off[16:12] == 5'h16);             // 0x16000-0x16fff
wire io_evt   = sel_io && (io_off[16:12] == 5'h1a);             // 0x1a000-0x1afff
wire io_scr   = io_scr1 | io_sid | io_scr2;

wire [15:0] scr_rdata, intc_rdata, timer_rdata, dma_rdata;

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

assign io_rdata = io_dma   ? dma_rdata :
                  io_intc  ? intc_rdata :
                  io_scr   ? scr_rdata :
                  io_timer ? timer_rdata :
                  io_evt   ? evt_rdata : 16'h0000;

// system control registers and RTC
wire timer_ipl7, softint1, softint2;

next_scr #(.CLK_HZ(CLK_HZ)) scr
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
	.rdata(bmap_rdata)
);

// interrupt controller
reg soft1_d, soft2_d;
always @(posedge clk) begin
	soft1_d <= softint1;
	soft2_d <= softint2;
end

wire [31:0] int_set =
	(32'd1  & {31'd0,  softint1 & ~soft1_d}) |
	({31'd0, softint2 & ~soft2_d} << 1) |
	({31'd0, vid_int_set} << 5) |
	({31'd0, timer_set} << 29);

wire [31:0] int_clr =
	({31'd0, ~softint1 & soft1_d}) |
	({31'd0, ~softint2 & soft2_d} << 1) |
	({31'd0, vid_int_clr} << 5) |
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
