//============================================================================
//  Magneto-optical drive controller (Fujitsu MB600310 "OSP") with its
//  DMA channel and the ECC buffer engine
//
//  OSP registers 0x02012000-0x02012016, disk DMA channel CSR
//  0x02000050, pointers 0x02004050-0x0201405C, init 0x02004250.
//
//  Modeled on Previous src/mo.c and the disk channel parts of
//  src/dma.c.  Implemented: the register set, the formatter command
//  register with reset and the standalone ECC commands, and the ECC
//  buffer engine in the MOCSR2_ECC_DIS ("disabled", buffer only) mode
//  the boot ROM ECC system test uses: ECC Write fills the 1024 or 1296
//  byte buffer from memory through the disk DMA channel, ECC Read
//  drains it back, MOINT_ECC_DONE is raised when a sequence completes.
//  Data passes through unchanged; the Reed-Solomon encoder that would
//  compute the 272 parity bytes is a TODO (docs/PORTING.md), so the
//  parity area reads back as buffer content.
//
//  No drive is attached: disk operations are a TODO.
//============================================================================

module next_mo #(parameter CLK_HZ = 100000000)
(
	input         clk,
	input         reset,

	// register access
	input         sel_osp,       // 0x12000-0x1201F
	input         sel_csr,       // 0x00050-0x00053
	input         sel_ptr,       // 0x04050-0x0405F
	input         sel_ini,       // 0x04250-0x04253
	input   [4:0] addr,          // byte offset within the block
	input         we,
	input   [1:0] be,
	input  [15:0] wdata,
	output [15:0] rdata,

	// RAM master port (32-bit, level req / level ack)
	output reg        m_req,
	output reg        m_we,
	output reg [23:0] m_addr,
	output reg  [3:0] m_be,
	output reg [31:0] m_din,
	input      [31:0] m_dout,
	input             m_ack,

	output        int_disk,      // OSP interrupt level
	output        int_disk_dma,  // DMA channel complete level

	// Two optical drives, as the reference carries (MO_MAX_DRIVES).
	// An image holds the encoded 1296 byte sectors the formatter reads,
	// so it is not a multiple of the 512 byte block the SD card speaks.
	input   [1:0] img_mounted,
	input         img_readonly,
	input  [63:0] img_size,
	output        sd_unit,
	output [31:0] sd_lba,
	output reg    sd_rd,
	output reg    sd_wr,
	input         sd_ack,
	input   [8:0] sd_buff_addr,
	input   [7:0] sd_buff_dout,
	output  [7:0] sd_buff_din,
	input         sd_buff_wr
);

localparam MOINT_ECC_DONE   = 8'h08;
localparam MOINT_OPER_COMPL = 8'h04;
localparam MOCSR2_ECC_CMP   = 8'h02;
localparam MOCSR2_ECC_BLOCKS= 8'h10;
localparam MOCSR2_ECC_MODE  = 8'h20;
localparam MOCSR2_ECC_DIS   = 8'h40;

localparam FMT_ECC_READ  = 8'h80;
localparam FMT_ECC_WRITE = 8'h40;
localparam FMT_RD_STAT   = 8'h20;
localparam FMT_ID_READ   = 8'h10;
localparam FMT_VERIFY    = 8'h08;
localparam FMT_ERASE     = 8'h04;
localparam FMT_READ      = 8'h02;
localparam FMT_WRITE     = 8'h01;

// formatter modes
localparam [2:0] FM_IDLE = 3'd0, FM_READ_ID = 3'd1, FM_READ = 3'd2,
                 FM_WRITE = 3'd3, FM_ERASE = 3'd4, FM_VERIFY = 3'd5;

// the disk turns one sector every SECTOR_IO_DELAY microseconds
localparam [20:0] SECTOR_IO_DELAY = 21'd1250;
localparam MOINIT_ID_CMP_TRK = 8'h20;

localparam MO_SEC_PER_TRACK = 5'd16;
localparam MO_TRACK_OFFSET  = 16'd4096;
localparam MO_TRACK_LIMIT   = 16'd15723;   // 19819 - 4096

// drive commands
localparam [15:0] DRV_REC = 16'h1000, DRV_RDS = 16'h2000, DRV_RCA = 16'h2200,
                  DRV_RES = 16'h2800, DRV_RHS = 16'h2A00, DRV_RGC = 16'h3000,
                  DRV_RVI = 16'h3F00, DRV_SRH = 16'h4100, DRV_SVH = 16'h4200,
                  DRV_SWH = 16'h4300, DRV_SEH = 16'h4400, DRV_SFH = 16'h4500,
                  DRV_RID = 16'h5000, DRV_SPM = 16'h5200, DRV_STM = 16'h5300,
                  DRV_LC  = 16'h5400, DRV_ULC = 16'h5500, DRV_EC  = 16'h5600,
                  DRV_SOO = 16'h5900, DRV_SOF = 16'h5A00;

// disk status bits, as the drive returns them
localparam [15:0] DS_INSERT = 16'h0004, DS_RESET = 16'h0008,
                  DS_SEEK   = 16'h0010, DS_CMD   = 16'h0020,
                  DS_STOPPED= 16'h0200, DS_WP    = 16'h2000,
                  DS_EMPTY  = 16'h4000, DS_BUSY  = 16'h8000;
localparam [15:0] VI_VERSION = 16'h0880;

// heads
localparam [2:0] NO_HEAD = 3'd0, READ_HEAD = 3'd1, WRITE_HEAD = 3'd2,
                 ERASE_HEAD = 3'd3, VERIFY_HEAD = 3'd4, RF_HEAD = 3'd5;

localparam MOINT_CMD_COMPL = 8'h01;
localparam MOINT_ATTN      = 8'h02;
localparam MOINT_TIMEOUT   = 8'h10;
localparam MOCSR2_DRIVE_SEL = 8'h01;
localparam MOCSR2_SECT_TIMER= 8'h80;

localparam SECT_DATA = 12'd1024;   // MO_SECTORSIZE_DATA
localparam SECT_DISK = 12'd1296;   // MO_SECTORSIZE_DISK

//----------------------------------------------------------------------------
// OSP registers
//----------------------------------------------------------------------------

reg [7:0] tracknumh, tracknuml;
reg [3:0] sector_num;
reg [3:0] sector_incr;
reg [7:0] sector_cnt;
reg [7:0] intstatus, intmask;
reg [7:0] csr2, csr1;
reg [7:0] csrh, csrl;
reg [7:0] err_stat, ecc_cnt;
reg [7:0] r_init, r_format, r_mark;
reg [7:0] flag0, flag1, flag2, flag3, flag4, flag5, flag6;

//----------------------------------------------------------------------------
// The drives.  Everything the OSP asks about a drive is per drive, and
// the selected one comes from CSR2's drive select bit.
//----------------------------------------------------------------------------
// The drives are fitted to the machine; the cartridges come and go.
// The reference keeps them apart - connected is configuration, set at
// init along with complete and a cleared attention, while inserted is
// whether there is a cartridge in the drive.  A connected drive with
// no cartridge still answers, and says it is empty; an unconnected one
// does not answer at all, which is how a machine with no optical
// drive looks.  NeXTSTEP probes od0 and od1, so both are fitted.
wire [1:0] drv_conn = 2'b11;     // both drives are fitted
reg  [1:0] drv_ins = 0;          // a cartridge is in the drive
reg  [1:0] drv_wp = 0;           // mounted read only
reg  [1:0] drv_spinning = 0;
reg  [1:0] drv_spiraling = 0;
reg  [1:0] drv_attn = 0;
reg  [1:0] drv_compl = 0;
reg [15:0] head_pos [0:1];       // current track
reg  [3:0] ho_pos [0:1];         // high order seek, applied by the next seek
reg  [1:0] drv_seeking = 0;      // a seeking drive presents no sectors
reg  [2:0] drv_head [0:1];
reg [31:0] drv_bytes [0:1];      // image size, for the sector bound
// The disk's side of the ECC buffer.  A sector is 1296 bytes and the
// card speaks 512, so a sector is a run of three or four blocks with
// the wanted window somewhere inside it.
reg  [10:0] dsk_addr = 0;
reg   [7:0] dsk_wdata = 0;
reg         dsk_we = 0;
reg         dsk_active = 0;
reg  [31:0] dsk_lba_r = 0;
reg   [2:0] dsk_blk = 0;
reg   [2:0] dsk_nblk = 0;
reg   [8:0] dsk_skip = 0;
localparam [2:0] D_IDLE = 3'd0, D_RGO = 3'd1, D_RACK = 3'd2,
                 D_WGO = 3'd3, D_WACK = 3'd4, D_NEXT = 3'd5;
reg   [2:0] dst = D_IDLE;
reg         dsk_is_wr = 0;       // the run puts a sector back
reg         dsk_erase = 0;       // ... as 0xFF rather than the buffer

// A sector covers its first and last block only in part, so putting one
// back means reading each block, replacing the window inside it, and
// writing it whole again.
reg   [7:0] stage [0:511];
reg   [7:0] stage_q;
reg         win_q;
wire [11:0] wgidx = {1'd0, dsk_blk, 8'd0, 1'd0} + {3'd0, sd_buff_addr};
wire        wgin  = (wgidx >= {3'd0, dsk_skip}) &&
                    ((wgidx - {3'd0, dsk_skip}) < SECT_DISK);
always @(posedge clk) begin
	// dsk_active spans the whole run, gaps included, and the host's
	// buffer bus is shared: without the ack, another device's sector
	// lands in the staging buffer and the write-back carries it into
	// the cartridge.
	if (dsk_active && sd_buff_wr && sd_ack) stage[sd_buff_addr] <= sd_buff_dout;
	stage_q <= stage[sd_buff_addr];
	win_q   <= wgin;
end
reg         sec_tick = 0;
assign sd_lba = dsk_lba_r;
// what the card takes back: the sector's own bytes inside the window,
// the block's original bytes outside it
assign sd_buff_din = win_q ? (dsk_erase ? 8'hFF : ecc_q) : stage_q;

reg  [3:0] sec_offset [0:1];     // sector under the head
reg [20:0] sec_timer = 0;
reg  [2:0] fmt_mode = FM_IDLE;
reg  [7:0] sector_counter = 0;
reg        write_timing = 0;

reg [15:0] drv_cmd = 0;
reg        drv_cmd_pend = 0;
reg [20:0] drv_dly = 0;          // microseconds until the command completes
reg        drv_busy = 0;
localparam [20:0] CMD_DELAY = 21'd40;
localparam [20:0] SPINUP_DELAY = 21'd1600000;   // 1.6 s, as the reference

wire       dnum = csr2[0];       // MOCSR2_DRIVE_SEL
assign     sd_unit = dnum;

wire [15:0] cur_track = head_pos[dnum];
wire  [2:0] cur_head  = drv_head[dnum];

// The status word the drive returns: empty until an image is mounted,
// stopped until the motor is started.
// The status the drive returns is sticky: faults accumulate in it
// until the attention is reset, and the live bits are added on top.
reg [15:0] dstat_v [0:1];
reg        attn_pend = 0;        // this command ends with an attention
wire [15:0] drv_dstat = dstat_v[dnum] |
                        (drv_ins[dnum] ? DS_INSERT : DS_EMPTY) |
                        (drv_wp[dnum] ? DS_WP : 16'h0000) |
                        (drv_spinning[dnum] ? 16'h0000 : DS_STOPPED);

// Two guards, as the reference has them.  Anything that moves the
// head or the spiral needs the disk turning: refusing it is an
// unimplemented command, DS_CMD, not an empty drive.  Only the motor
// and the eject need a cartridge as such.  Both answer with a
// completion and an attention, and neither does the work - carrying on
// regardless walks the head to a track that was never asked for, and
// the driver reads that back as the drive's position.
wire cmd_needs_spin  = (drv_cmd[15:12] == 4'h0)                  ||
                       ((drv_cmd & 16'hFFF0) == 16'hA000)        ||
                       ((drv_cmd & 16'hFF00) == 16'h5100)        ||
                       (drv_cmd == DRV_REC)                      ||
                       (drv_cmd[15:12] == 4'h4)                  ||
                       (drv_cmd == DRV_SOO) || (drv_cmd == DRV_SOF);
wire cmd_needs_media = (drv_cmd == DRV_SPM) || (drv_cmd == DRV_STM) ||
                       (drv_cmd == DRV_EC);

wire [7:0] intstatus_eff = (intstatus & ~(MOINT_CMD_COMPL | MOINT_ATTN)) |
                           (drv_compl[dnum] ? MOINT_CMD_COMPL : 8'h00) |
                           (drv_attn[dnum]  ? MOINT_ATTN      : 8'h00);
assign int_disk = |(intstatus_eff & intmask);

//----------------------------------------------------------------------------
// disk DMA channel
//----------------------------------------------------------------------------

reg  [7:0] d_csr;
reg [31:0] d_next, d_limit, d_start, d_stop;

assign int_disk_dma = d_csr[3];

//----------------------------------------------------------------------------
// register read mux
//----------------------------------------------------------------------------

`define MO_READ(a) ( \
	((a) == 5'h00) ? tracknumh : \
	((a) == 5'h01) ? tracknuml : \
	((a) == 5'h02) ? {4'd0, sector_num} : \
	((a) == 5'h03) ? sector_cnt : \
	((a) == 5'h04) ? intstatus_eff : \
	((a) == 5'h05) ? intmask : \
	((a) == 5'h06) ? csr2 : \
	((a) == 5'h07) ? csr1 : \
	((a) == 5'h08) ? csrh : \
	((a) == 5'h09) ? csrl : \
	((a) == 5'h0A) ? err_stat : \
	((a) == 5'h0B) ? ecc_cnt : \
	((a) == 5'h10) ? flag0 : \
	((a) == 5'h11) ? flag1 : \
	((a) == 5'h12) ? flag2 : \
	((a) == 5'h13) ? flag3 : \
	((a) == 5'h14) ? flag4 : \
	((a) == 5'h15) ? flag5 : \
	((a) == 5'h16) ? flag6 : 8'h00 )

wire [31:0] ptr_q = (addr[3:2] == 2'd0) ? d_next :
                    (addr[3:2] == 2'd1) ? d_limit :
                    (addr[3:2] == 2'd2) ? d_start : d_stop;

assign rdata = sel_osp ? {`MO_READ({addr[4:1], 1'b0}), `MO_READ({addr[4:1], 1'b1})} :
               sel_csr ? (addr[1] ? 16'h0000 : {d_csr, 8'h00}) :
               sel_ptr ? (addr[1] ? ptr_q[15:0] : ptr_q[31:16]) :
               sel_ini ? (addr[1] ? d_next[15:0] : d_next[31:16]) : 16'h0000;

//----------------------------------------------------------------------------
// ECC buffer and engine
//----------------------------------------------------------------------------

reg  [7:0] eccbuf [0:1295];
reg [11:0] ecc_size, ecc_limit;
reg [11:0] drain_pos;

localparam ECC_DONE = 3'd0, ECC_FILL = 3'd1, ECC_ECCING = 3'd2,
           ECC_DRAIN = 3'd3, ECC_RS = 3'd4;
reg  [2:0] ecc_state;
reg        ecc_is_read;          // current mode: 1 = ECC read (drain)
reg        ecc_repeat;

localparam M_IDLE = 3'd0, M_REQ = 3'd1, M_WAIT = 3'd2, M_PRE = 3'd3;
reg  [2:0] mst;

// Reed-Solomon codec sharing the ECC buffer
reg   [7:0] ecc_q;
wire        rs_done, rs_fail;
wire  [7:0] rs_count;
wire [10:0] rs_addr;
wire  [7:0] rs_wdata;
wire        rs_we;
reg         rs_start_enc, rs_start_dec;
wire        rs_active = (ecc_state == ECC_RS);

next_rs rs
(
	.clk(clk),
	.reset(reset),
	.start_encode(rs_start_enc),
	.start_decode(rs_start_dec),
	.done(rs_done),
	.fail(rs_fail),
	.err_count(rs_count),
	.b_addr(rs_addr),
	.b_rdata(ecc_q),
	.b_wdata(rs_wdata),
	.b_we(rs_we)
);

// single buffer port, muxed between the RS codec and the fill/drain
// engine; read data is registered (one cycle of latency)
reg  [10:0] mo_addr;
reg   [7:0] mo_wdata;
reg         mo_we;

// While a block goes back to the card the buffer must answer in step
// with the byte being asked for: registering the address as well as
// the data would put it two cycles behind, and zeroes go out instead.
wire [10:0] dsk_wr_addr = wgidx[10:0] - {2'd0, dsk_skip};
wire [10:0] ecc_addr = (dsk_active && dsk_is_wr) ? dsk_wr_addr :
                       dsk_active ? dsk_addr : rs_active ? rs_addr : mo_addr;
wire  [7:0] ecc_wd   = dsk_active ? dsk_wdata : rs_active ? rs_wdata : mo_wdata;
wire        ecc_we   = dsk_active ? dsk_we    : rs_active ? rs_we    : mo_we;

always @(posedge clk) begin
	if (ecc_we) eccbuf[ecc_addr] <= ecc_wd;
	ecc_q <= eccbuf[ecc_addr];
end

// engine step delay, roughly the ECC_DELAY pacing in mo.c
// The ECC engine paces itself in 50 us steps; the drive's own timings
// are in microseconds, so it counts its own.
localparam TICK_US = (CLK_HZ / 1000000) > 1 ? (CLK_HZ / 1000000) : 1;
reg [$clog2(TICK_US+1)-1:0] uscnt;
wire us_tick = (uscnt == TICK_US-1);

localparam TICK = CLK_HZ / 1000000 * 50;    // 50 us
reg [$clog2(TICK)-1:0] tickcnt;
wire tick = (tickcnt == TICK-1);

integer i;

task automatic osp_int;
	input [7:0] bits;
	begin
		intstatus <= intstatus | bits;
	end
endtask

// dma_interrupt(CHANNEL_DISK): complete only when next reached limit
task automatic dma_done_disk;
	begin
		if (d_next == d_limit) begin
			d_csr[3] <= 1;
			if (d_csr[1]) begin
				d_next <= d_start;
				d_limit <= d_stop;
				d_csr[1] <= 0;
			end
			else d_csr[0] <= 0;
		end
	end
endtask

// fmt_sector_done(): step the formatter's address and count the
// sector off; the operation completes when the count runs out
task automatic fmt_sector_done;
	reg [15:0] track;
	reg  [4:0] snum;
	begin
		track = {tracknumh, tracknuml};
		snum  = {1'b0, sector_num} + {1'b0, sector_incr};
		track = track + {15'd0, snum[4]};
		sector_num <= snum[3:0];
		tracknumh  <= track[15:8];
		tracknuml  <= track[7:0];
		sector_counter <= sector_counter - 1'd1;
		if (sector_counter == 8'd1) begin
			fmt_mode <= FM_IDLE;
			intstatus <= intstatus | MOINT_OPER_COMPL;
		end
	end
endtask

// Start the run of card blocks holding one 1296 byte sector.  A track
// outside the disk is a formatter timeout, not an access.
task automatic dsk_start;
	input [23:0] sid;
	input        is_wr;
	input        is_erase;
	reg [31:0] lsec;
	reg [31:0] boff;
	reg [11:0] span;
	begin
		if (sid[23:8] < MO_TRACK_OFFSET ||
		    sid[23:8] >= (MO_TRACK_OFFSET + MO_TRACK_LIMIT)) begin
			fmt_mode <= FM_IDLE;
			intstatus <= intstatus | MOINT_TIMEOUT;
		end
		else begin
			lsec = (({16'd0, sid[23:8]} - {16'd0, MO_TRACK_OFFSET}) << 4)
			     + {28'd0, sid[3:0]};
			boff = (lsec << 10) + (lsec << 8) + (lsec << 4);   // 1296 each
			span = {3'd0, boff[8:0]} + SECT_DISK + 12'd511;
			dsk_lba_r  <= boff[31:9];
			dsk_skip   <= boff[8:0];
			dsk_blk    <= 0;
			dsk_nblk   <= span[11:9];
			dsk_active <= 1;
			dsk_is_wr  <= is_wr;
			dsk_erase  <= is_erase;
			dst        <= D_RGO;
		end
	end
endtask

task automatic ecc_sequence_done;
	begin
		ecc_state <= ECC_DONE;
		if (csr2 & MOCSR2_ECC_DIS) osp_int(MOINT_ECC_DONE);
	end
endtask

wire [7:0] csr_or = (be[1] ? wdata[15:8] : 8'h00) | (be[0] ? wdata[7:0] : 8'h00);

always @(posedge clk) begin
	if (reset) begin
		tracknumh <= 0; tracknuml <= 0;
		sector_num <= 0; sector_incr <= 0; sector_cnt <= 0;
		intstatus <= 0; intmask <= 0;
		csr2 <= 0; csr1 <= 0; csrh <= 0; csrl <= 0;
		err_stat <= 0; ecc_cnt <= 0;
		r_init <= 0; r_format <= 0; r_mark <= 0;
		flag0 <= 0; flag1 <= 0; flag2 <= 0; flag3 <= 0;
		flag4 <= 0; flag5 <= 0; flag6 <= 0;
		d_csr <= 0;
		d_next <= 0; d_limit <= 0; d_start <= 0; d_stop <= 0;
		ecc_state <= ECC_DONE;
		ecc_size <= 0; ecc_limit <= 0; drain_pos <= 0;
		ecc_is_read <= 0; ecc_repeat <= 0;
		mst <= M_IDLE;
		m_req <= 0;
		mo_we <= 0;
		// The cartridge is not ejected by a reset.  dev_reset here
		// carries the 68040's RESET instruction, which the ROM runs
		// during start-up, so clearing the media state here takes the
		// disk out of the drive before the machine has finished
		// testing itself - and every boot then finds an empty drive.
		// Only what the controller owns is reset.
		dstat_v[0] <= 0; dstat_v[1] <= 0; attn_pend <= 0;
		drv_spinning <= 0; drv_spiraling <= 0;
		drv_attn <= 0; drv_compl <= 2'b11;   // MO_Init: complete, no attn
		head_pos[0] <= 0; head_pos[1] <= 0;
		drv_head[0] <= NO_HEAD; drv_head[1] <= NO_HEAD;
		drv_cmd_pend <= 0; drv_busy <= 0; drv_dly <= 0;
		ho_pos[0] <= 0; ho_pos[1] <= 0; drv_seeking <= 0;
		sd_rd <= 0; sd_wr <= 0;
		sec_offset[0] <= 0; sec_offset[1] <= 0;
		sec_timer <= 0; sec_tick <= 0;
		fmt_mode <= FM_IDLE; sector_counter <= 0; write_timing <= 0;
		dst <= D_IDLE; dsk_active <= 0; dsk_we <= 0;
		dsk_is_wr <= 0; dsk_erase <= 0;
		dsk_lba_r <= 0; dsk_blk <= 0; dsk_nblk <= 0; dsk_skip <= 0;
		rs_start_enc <= 0;
		rs_start_dec <= 0;
		tickcnt <= 0;
		uscnt <= 0;
	end
	else begin
		tickcnt <= tick ? 1'd0 : tickcnt + 1'd1;
		uscnt <= us_tick ? 1'd0 : uscnt + 1'd1;
		dsk_we <= 0;

		// place the streamed byte if it falls inside the sector window
		if (dsk_active && !dsk_is_wr && sd_buff_wr && sd_ack) begin : place
			reg [11:0] gidx;
			gidx = {1'd0, dsk_blk, 8'd0, 1'd0} + {3'd0, sd_buff_addr};
			if (gidx >= {3'd0, dsk_skip} &&
			    (gidx - {3'd0, dsk_skip}) < SECT_DISK) begin
				dsk_addr  <= (gidx - {3'd0, dsk_skip});
				dsk_wdata <= sd_buff_dout;
				dsk_we    <= 1;
			end
		end
		mo_we <= 0;
		rs_start_enc <= 0;
		rs_start_dec <= 0;

		//------------------------------------------------------------
		// OSP register writes
		//------------------------------------------------------------
		if (sel_osp & we) begin : osp_wr
			reg [4:0] a;
			reg [7:0] v;
			integer k;
			for (k = 0; k < 2; k = k + 1) begin
				if (k == 0 ? be[1] : be[0]) begin
					a = {addr[4:1], k[0]};
					v = k[0] ? wdata[7:0] : wdata[15:8];
					case (a)
						5'h00: tracknumh <= v;
						5'h01: tracknuml <= v;
						5'h02: begin sector_num <= v[3:0]; sector_incr <= v[7:4]; end
						5'h03: sector_cnt <= v;
						5'h04: intstatus <= intstatus & ~v;
						5'h05: intmask <= v;
						5'h06: csr2 <= v;
						5'h07: begin
							// mo_formatter_cmd()
							csr1 <= v;
							if (v == 8'h00) begin
								fmt_mode <= FM_IDLE;
								ecc_state <= ECC_DONE;
								ecc_cnt <= 0;
								err_stat <= 0;
							end
							else begin
								if (v & FMT_ECC_READ) begin
									if (ecc_state == ECC_DONE) begin
										ecc_is_read <= 1;
										ecc_repeat <= |(csr2 & MOCSR2_ECC_BLOCKS);
										ecc_state <= ECC_ECCING;
									end
								end
								if (v & FMT_ECC_WRITE) begin
									if (ecc_state == ECC_DONE) begin
										ecc_is_read <= 0;
										ecc_repeat <= |(csr2 & MOCSR2_ECC_BLOCKS);
										ecc_size <= 0;
										ecc_limit <= SECT_DATA;
										ecc_state <= ECC_FILL;
									end
								end
								if (v & FMT_RD_STAT) begin
									csrh <= drv_dstat[15:8];
									csrl <= drv_dstat[7:0];
								end
								if (v & FMT_ID_READ) fmt_mode <= FM_READ_ID;
								if (v & FMT_VERIFY) begin
									fmt_mode <= FM_VERIFY;
									sector_counter <= sector_cnt;
								end
								if (v & FMT_ERASE) begin
									fmt_mode <= FM_ERASE;
									sector_counter <= sector_cnt;
								end
								if (v & FMT_READ) begin
									fmt_mode <= FM_READ;
									sector_counter <= sector_cnt;
								end
								if (v & FMT_WRITE) begin
									write_timing <= 0;
									fmt_mode <= FM_WRITE;
									sector_counter <= sector_cnt;
								end
							end
						end
						5'h08: csrh <= v;
						5'h09: begin
							// MO_CSR_L_Write: the low byte completes the
							// command word and runs it
							csrl <= v;
							drv_cmd <= {csrh, v};
							drv_cmd_pend <= 1;
						end
						5'h0C: r_init <= v;
						5'h0D: r_format <= v;
						5'h0E: r_mark <= v;
						5'h10: flag0 <= v;
						5'h11: flag1 <= v;
						5'h12: flag2 <= v;
						5'h13: flag3 <= v;
						5'h14: flag4 <= v;
						5'h15: flag5 <= v;
						5'h16: flag6 <= v;
						default: ;
					endcase
				end
			end
		end

		//------------------------------------------------------------
		// a cartridge going in or out is the drive's business, and it
		// gets the OSP's attention
		//------------------------------------------------------------
		if (img_mounted[0]) begin
			drv_ins[0]   <= (img_size != 0);
			drv_wp[0]    <= img_readonly;
			drv_bytes[0] <= img_size[31:0];
			drv_attn[0]  <= 1;
		end
		if (img_mounted[1]) begin
			drv_ins[1]   <= (img_size != 0);
			drv_wp[1]    <= img_readonly;
			drv_bytes[1] <= img_size[31:0];
			drv_attn[1]  <= 1;
		end

		//------------------------------------------------------------
		// mo_drive_cmd(): a command to an absent drive is not answered
		// at all - no completion, which is how the driver learns the
		// slot is empty
		//------------------------------------------------------------
		if (drv_cmd_pend) begin
			drv_cmd_pend <= 0;
			if (drv_conn[dnum]) begin
				drv_compl[dnum] <= 0;
				drv_dly  <= CMD_DELAY;
				drv_busy <= 1;
			end
			if (drv_conn[dnum] && cmd_needs_media && !drv_ins[dnum]) begin
				dstat_v[dnum] <= dstat_v[dnum] | DS_EMPTY;
				attn_pend <= 1;
			end
			else if (drv_conn[dnum] && cmd_needs_spin &&
			         !drv_spinning[dnum]) begin
				// mo_unimplemented_cmd(): the head cannot be moved
				// against a disk that is not turning
				dstat_v[dnum] <= dstat_v[dnum] | DS_CMD;
				attn_pend <= 1;
			end
			else if (drv_conn[dnum]) begin
				casez (drv_cmd)
				16'b1010_0000_0000_????:            // high order seek:
					// it only arms the top bits; the next seek applies them
					ho_pos[dnum] <= drv_cmd[3:0];
				16'b0101_0001_????_????: begin      // relative jump
					case (drv_cmd[6:4])
					3'd1: drv_head[dnum] <= READ_HEAD;
					3'd2: drv_head[dnum] <= VERIFY_HEAD;
					3'd3: drv_head[dnum] <= WRITE_HEAD;
					3'd4: drv_head[dnum] <= ERASE_HEAD;
					default: ;
					endcase
					head_pos[dnum] <= cur_track +
					                  {{12{drv_cmd[3]}}, drv_cmd[3:0]};
					sec_offset[dnum] <= 0;
				end
				16'b0000_????_????_????: begin      // seek
					head_pos[dnum] <= {ho_pos[dnum], drv_cmd[11:0]};
					drv_seeking[dnum] <= 1;
				end
				default: begin
					case (drv_cmd)
					DRV_REC: begin
						head_pos[dnum] <= 0;
						sec_offset[dnum] <= 0;
						drv_seeking[dnum] <= 1;
					end
					DRV_RDS: begin csrh <= drv_dstat[15:8]; csrl <= drv_dstat[7:0]; end
					DRV_RCA: begin csrh <= cur_track[15:8]; csrl <= cur_track[7:0]; end
					DRV_RES: begin csrh <= 8'h00; csrl <= 8'h00; end
					DRV_RHS: begin csrh <= 8'h00; csrl <= 8'h00; end
					DRV_RGC: begin csrh <= 8'h00; csrl <= 8'h00; end
					DRV_RVI: begin csrh <= VI_VERSION[15:8]; csrl <= VI_VERSION[7:0]; end
					DRV_SRH: drv_head[dnum] <= READ_HEAD;
					DRV_SVH: drv_head[dnum] <= VERIFY_HEAD;
					DRV_SWH: drv_head[dnum] <= WRITE_HEAD;
					DRV_SEH: drv_head[dnum] <= ERASE_HEAD;
					DRV_SFH: drv_head[dnum] <= RF_HEAD;
					DRV_RID: begin      // reset attention and status
						drv_attn[dnum] <= 0;
						dstat_v[dnum]  <= 0;
					end
					DRV_SPM: drv_spinning[dnum] <= 0;
					DRV_STM: begin
						// spinning a cartridge up takes real time,
						// and the driver waits for the completion
						drv_spinning[dnum] <= 1;
						drv_dly <= SPINUP_DELAY;
					end
					DRV_SOO: drv_spiraling[dnum] <= 1;
					DRV_SOF: drv_spiraling[dnum] <= 0;
					DRV_EC:  begin
						drv_ins[dnum] <= 0;
						drv_spinning[dnum] <= 0;
						drv_attn[dnum] <= 1;
					end
					default: ;   // LC, ULC and the rest: accepted
					endcase
				end
				endcase
			end
		end

		// The drive reports completion a while after it is asked.
		// This runs after the command block, so its countdown must
		// stand aside on the cycle a command loads a new delay -
		// otherwise the decrement overwrites the load and the command
		// inherits whatever was left of the last one.  Invisible while
		// every command took the same time; a 1.6 second spin-up left
		// the next command counting down from the rest of it.
		if (drv_busy && us_tick && !drv_cmd_pend) begin
			if (drv_dly == 0) begin
				drv_busy <= 0;
				drv_compl[dnum] <= 1;
				drv_seeking[dnum] <= 0;
				if (attn_pend) begin
					drv_attn[dnum] <= 1;
					attn_pend <= 0;
				end
			end
			else drv_dly <= drv_dly - 1'd1;
		end

		// DMA CSR command (OR of byte lanes, like DMA_CSR_Write)
		if (sel_csr & we & (csr_or != 0)) begin
			if (csr_or[4]) d_csr <= d_csr & ~8'b00001011;   // RESET
			if (csr_or[1]) d_csr[1] <= 1;                   // SETSUPDATE
			if (csr_or[0]) d_csr[0] <= 1;                   // SETENABLE
			if (csr_or[3]) d_csr[3] <= 0;                   // CLRCOMPLETE
		end

		if (sel_ptr & we) begin
			case (addr[3:2])
				2'd0: begin if (!addr[1]) begin if (be[1]) d_next[31:24] <= wdata[15:8]; if (be[0]) d_next[23:16] <= wdata[7:0]; end else begin if (be[1]) d_next[15:8] <= wdata[15:8]; if (be[0]) d_next[7:0] <= wdata[7:0]; end end
				2'd1: begin if (!addr[1]) begin if (be[1]) d_limit[31:24] <= wdata[15:8]; if (be[0]) d_limit[23:16] <= wdata[7:0]; end else begin if (be[1]) d_limit[15:8] <= wdata[15:8]; if (be[0]) d_limit[7:0] <= wdata[7:0]; end end
				2'd2: begin if (!addr[1]) begin if (be[1]) d_start[31:24] <= wdata[15:8]; if (be[0]) d_start[23:16] <= wdata[7:0]; end else begin if (be[1]) d_start[15:8] <= wdata[15:8]; if (be[0]) d_start[7:0] <= wdata[7:0]; end end
				2'd3: begin if (!addr[1]) begin if (be[1]) d_stop[31:24] <= wdata[15:8]; if (be[0]) d_stop[23:16] <= wdata[7:0]; end else begin if (be[1]) d_stop[15:8] <= wdata[15:8]; if (be[0]) d_stop[7:0] <= wdata[7:0]; end end
			endcase
		end
		// DMA_Init_Write: a write to the init register loads next
		if (sel_ini & we) begin
			if (!addr[1]) begin if (be[1]) d_next[31:24] <= wdata[15:8]; if (be[0]) d_next[23:16] <= wdata[7:0]; end
			else begin if (be[1]) d_next[15:8] <= wdata[15:8]; if (be[0]) d_next[7:0] <= wdata[7:0]; end
		end

		//------------------------------------------------------------
		// The disk turns.  One sector passes under the head every
		// SECTOR_IO_DELAY, and the selected drive presents its sector
		// to the formatter (mo_spiraling_operation).
		//------------------------------------------------------------
		if (us_tick) begin
			if (sec_timer == 0) begin
				sec_timer <= SECTOR_IO_DELAY;
				sec_tick <= 1;
			end
			else sec_timer <= sec_timer - 1'd1;
		end

		if (sec_tick) begin : spiral
			reg [23:0] sector_id;
			reg [23:0] fmt_id;
			reg        match;
			sec_tick <= 0;
			sector_id = {cur_track, 4'd0, sec_offset[dnum]};
			fmt_id    = {tracknumh, tracknuml, 4'd0, sector_num};
			match = (r_init & MOINIT_ID_CMP_TRK)
			      ? (sector_id[23:8] == fmt_id[23:8])
			      : (sector_id == fmt_id);

			if (drv_ins[dnum] && drv_spinning[dnum] &&
			    drv_spiraling[dnum] && !drv_seeking[dnum] &&
			    (dst == D_IDLE) && (ecc_state == ECC_DONE)) begin
				case (fmt_mode)
				FM_READ_ID: begin
					tracknumh  <= sector_id[23:16];
					tracknuml  <= sector_id[15:8];
					sector_num <= sector_id[3:0];
					fmt_mode   <= FM_IDLE;
					osp_int(MOINT_OPER_COMPL);
				end
				FM_READ: if (match) begin
					// fetch the sector into the buffer; the drain to
					// memory follows when the run completes
					dsk_start(sector_id, 1'b0, 1'b0);
				end
				FM_WRITE: begin
					// The first sector under the head only fills the
					// buffer - there is nothing to put back yet.  A
					// write to a protected cartridge is refused.
					if (match && write_timing) begin
						if (drv_wp[dnum]) begin
							fmt_mode <= FM_IDLE;
							intstatus <= intstatus | MOINT_TIMEOUT;
						end
						else dsk_start(sector_id, 1'b1, 1'b0);
					end
					else if (!write_timing && d_csr[0] && d_next < d_limit) begin
						// the reference's note that the first sector
						// must miss is about pre-filling the buffer:
						// fill it once here, and again after each
						// sector is committed.  Re-arming it on every
						// miss instead drains the channel dry and
						// stalls before a matching sector ever comes.
						ecc_size  <= 0;
						ecc_limit <= SECT_DATA;
						ecc_is_read <= 0;
						ecc_repeat <= |(csr2 & MOCSR2_ECC_BLOCKS);
						ecc_state <= ECC_FILL;
						write_timing <= 1;
					end
					else write_timing <= 1;
				end
				FM_ERASE: if (match) begin
					if (drv_wp[dnum]) begin
						fmt_mode <= FM_IDLE;
						intstatus <= intstatus | MOINT_TIMEOUT;
					end
					else dsk_start(sector_id, 1'b1, 1'b1);
				end
				FM_VERIFY: if (match) dsk_start(sector_id, 1'b0, 1'b0);
				default: ;
				endcase
			end

			// keep spiraling regardless of what the formatter wanted
			if (drv_spiraling[0] && !drv_seeking[0]) begin
				if (sec_offset[0] == MO_SEC_PER_TRACK[3:0] - 1'd1) begin
					sec_offset[0] <= 0;
					head_pos[0] <= head_pos[0] + 1'd1;
				end
				else sec_offset[0] <= sec_offset[0] + 1'd1;
			end
			if (drv_spiraling[1] && !drv_seeking[1]) begin
				if (sec_offset[1] == MO_SEC_PER_TRACK[3:0] - 1'd1) begin
					sec_offset[1] <= 0;
					head_pos[1] <= head_pos[1] + 1'd1;
				end
				else sec_offset[1] <= sec_offset[1] + 1'd1;
			end
		end

		//------------------------------------------------------------
		// the SD run that fills the buffer with one 1296 byte sector
		//------------------------------------------------------------
		case (dst)
		D_IDLE: ;
		D_RGO: begin
			sd_rd <= 1;
			if (sd_ack) begin
				sd_rd <= 0;
				dst <= D_RACK;
			end
		end
		D_RACK: begin
			if (!sd_ack) dst <= dsk_is_wr ? D_WGO : D_NEXT;
		end
		D_WGO: begin
			sd_wr <= 1;
			if (sd_ack) begin
				sd_wr <= 0;
				dst <= D_WACK;
			end
		end
		D_WACK: begin
			if (!sd_ack) dst <= D_NEXT;
		end
		D_NEXT: begin
			if (dsk_blk + 1'd1 >= dsk_nblk) begin
				dsk_active <= 0;
				dsk_erase  <= 0;
				dst <= D_IDLE;
				if (dsk_is_wr) begin
					// the sector is on the disk; step on and start
					// gathering the next one from memory (ecc_write)
					fmt_sector_done;
					if (!dsk_erase) begin
						ecc_size  <= 0;
						ecc_limit <= SECT_DATA;
						ecc_is_read <= 0;
						ecc_repeat <= |(csr2 & MOCSR2_ECC_BLOCKS);
						ecc_state <= ECC_FILL;
					end
				end
				else begin
					// the whole sector is in the buffer: hand it to the
					// ECC engine, which drains it to memory (ecc_read)
					ecc_size <= 0;
					ecc_limit <= SECT_DISK;
					ecc_is_read <= 1;
					ecc_repeat <= |(csr2 & MOCSR2_ECC_BLOCKS);
					ecc_state <= ECC_ECCING;
					fmt_sector_done;
				end
			end
			else begin
				dsk_blk <= dsk_blk + 1'd1;
				dsk_lba_r <= dsk_lba_r + 1'd1;
				dst <= D_RGO;
			end
		end
		default: dst <= D_IDLE;
		endcase


		//------------------------------------------------------------
		// ECC engine
		//------------------------------------------------------------
		case (ecc_state)
		ECC_FILL: begin
			// dma_mo_read_memory: memory to buffer, 32-bit words
			if (csr2 & MOCSR2_ECC_MODE) ecc_limit <= SECT_DISK;
			if (ecc_size >= ecc_limit) begin
				dma_done_disk;
				ecc_state <= ECC_ECCING;
				mst <= M_IDLE;
				m_req <= 0;
			end
			else if (d_csr[0] && d_next < d_limit) begin
				case (mst)
				// The 50 us step paces the standalone ECC commands.  A
				// sector driven by the drive cannot afford it: at a byte
				// per step a sector takes forty sector times, and the
				// head is tracks away before it could be committed.
				M_IDLE: if (tick || fmt_mode != FM_IDLE) mst <= M_REQ;
				M_REQ: begin
					m_req <= 1;
					m_we <= 0;
					m_be <= 4'hF;
					m_addr <= d_next[25:2];
					mst <= M_WAIT;
				end
				M_WAIT: if (m_ack) begin
					m_req <= 0;
					mo_addr <= ecc_size[10:0];
					mo_wdata <=
						(d_next[1:0] == 2'd0) ? m_dout[31:24] :
						(d_next[1:0] == 2'd1) ? m_dout[23:16] :
						(d_next[1:0] == 2'd2) ? m_dout[15:8] : m_dout[7:0];
					mo_we <= 1;
					ecc_size <= ecc_size + 12'd1;
					d_next <= d_next + 32'd1;
					mst <= M_REQ;
				end
				default: mst <= M_IDLE;
				endcase
			end
			// else: starving, wait for the CPU to enable the channel
		end

		ECC_ECCING: begin
			if (ecc_is_read) begin
				// ecc_encode(): with ECC_DIS and ECC_MODE both set the
				// encoder is bypassed and the buffer drains as it is;
				// otherwise the codec builds the 1296 byte sector
				if ((csr2 & MOCSR2_ECC_DIS) && (csr2 & MOCSR2_ECC_MODE)) begin
					drain_pos <= 0;
					ecc_state <= ECC_DRAIN;
					mst <= M_IDLE;
				end
				else begin
					rs_start_enc <= 1;
					ecc_state <= ECC_RS;
				end
			end
			else begin
				// ecc_decode(): with ECC_DIS and no ECC_MODE the decoder
				// is bypassed; otherwise the codec corrects the sector
				if ((csr2 & MOCSR2_ECC_DIS) && !(csr2 & MOCSR2_ECC_MODE)) begin
					ecc_sequence_done;
				end
				else begin
					rs_start_dec <= 1;
					ecc_state <= ECC_RS;
				end
			end
		end

		ECC_RS: begin
			if (rs_done) begin
				if (ecc_is_read) begin
					// encoded: 1024 -> 1296, drain follows
					ecc_size <= SECT_DISK;
					ecc_limit <= SECT_DISK;
					drain_pos <= 0;
					ecc_state <= ECC_DRAIN;
					mst <= M_IDLE;
				end
				else begin
					// decoded: 1296 -> 1024
					ecc_size <= SECT_DATA;
					ecc_limit <= SECT_DATA;
					if (rs_fail) begin
						err_stat <= 8'h01;              // ERRSTAT_ECC
						osp_int(8'h80);                 // MOINT_DATA_ERR
						ecc_state <= ECC_DONE;
					end
					else begin
						if (ecc_cnt == 0) ecc_cnt <= rs_count;
						ecc_sequence_done;
					end
				end
			end
		end

		ECC_DRAIN: begin
			// dma_mo_write_memory: buffer to memory
			if (drain_pos >= ecc_size) begin
				dma_done_disk;
				ecc_sequence_done;
				mst <= M_IDLE;
				m_req <= 0;
			end
			else if (d_csr[0] && d_next < d_limit) begin
				case (mst)
				M_IDLE: if (tick) begin
					mo_addr <= drain_pos[10:0];
					mst <= M_PRE;
				end
				M_PRE: mst <= M_REQ;    // buffer read data settles
				M_REQ: begin
					m_req <= 1;
					m_we <= 1;
					m_be <= 4'b1000 >> d_next[1:0];
					m_addr <= d_next[25:2];
					m_din <= {4{ecc_q}};
					mst <= M_WAIT;
				end
				M_WAIT: if (m_ack) begin
					m_req <= 0;
					drain_pos <= drain_pos + 12'd1;
					d_next <= d_next + 32'd1;
					mo_addr <= drain_pos[10:0] + 11'd1;
					mst <= M_PRE;
				end
				default: mst <= M_IDLE;
				endcase
			end
		end

		default: ;  // ECC_DONE: idle
		endcase
	end
end

endmodule
