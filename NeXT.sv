//============================================================================
//
//  NeXT for MiSTer  --  NeXTcube 68040
//
//  Based on the MiSTer core template, the Previous emulator
//  (reference/previous) as the hardware reference, and the AP68040
//  CPU core (rtl/AP68040).
//
//  This program is free software; you can redistribute it and/or modify it
//  under the terms of the GNU General Public License as published by the Free
//  Software Foundation; either version 2 of the License, or (at your option)
//  any later version.
//
//  This program is distributed in the hope that it will be useful, but WITHOUT
//  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
//  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
//  more details.
//
//  You should have received a copy of the GNU General Public License along
//  with this program; if not, write to the Free Software Foundation, Inc.,
//  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
//
//============================================================================

module emu
(
	`include "sys/emu_ports.vh"
);

///////// Default values for ports not used in this core /////////

assign ADC_BUS  = 'Z;
assign USER_OUT = '1;
assign {UART_RTS, UART_TXD, UART_DTR} = 0;
assign {SD_SCK, SD_MOSI, SD_CS} = 'Z;
assign {SDRAM_DQ, SDRAM_A, SDRAM_BA, SDRAM_CLK, SDRAM_CKE, SDRAM_DQML, SDRAM_DQMH, SDRAM_nWE, SDRAM_nCAS, SDRAM_nRAS, SDRAM_nCS} = 'Z;

assign VGA_SL = 0;
assign VGA_F1 = 0;
assign VGA_SCALER  = 0;
assign VGA_DISABLE = 0;
assign HDMI_FREEZE = 0;
assign HDMI_BLACKOUT = 0;
assign HDMI_BOB_DEINT = 0;

assign AUDIO_S = 0;
assign AUDIO_L = 0;
assign AUDIO_R = 0;
assign AUDIO_MIX = 0;

assign LED_DISK = 0;
assign LED_POWER = 0;
assign BUTTONS = 0;

//////////////////////////////////////////////////////////////////

// 1120 x 832 is close to 4:3
wire [1:0] ar = status[122:121];

assign VIDEO_ARX = (!ar) ? 12'd4 : (ar - 1'd1);
assign VIDEO_ARY = (!ar) ? 12'd3 : 12'd0;

`include "build_id.v"
localparam CONF_STR = {
	"NeXT;;",
	"F1,BINROM,Boot ROM;",
	"S0,VHDIMG,SCSI Disk;",
	"S1,IMGIMAFLPVFD,Floppy;",
	"-;",
	"O[122:121],Aspect ratio,Original,Full Screen,[ARC1],[ARC2];",
	"O[54:52],Network,Off,eth0,eth1,macvlan,tap0;",
	"O[57:55],Boot device,Auto,Disk,Floppy,Network,ROM Default;",
	"-;",
	"T[0],Reset;",
	"R[0],Reset and close OSD;",
	"v,0;",
	"V,v",`BUILD_DATE
};

wire forced_scandoubler;
wire   [1:0] buttons;
wire [127:0] status;
wire  [10:0] ps2_key;

wire        ioctl_download;
wire [15:0] ioctl_index;
wire        ioctl_wr;
wire [26:0] ioctl_addr;
wire  [7:0] ioctl_dout;

wire  [1:0] img_mounted_v, sd_ack_v;
wire        img_mounted   = img_mounted_v[0];
wire        fimg_mounted  = img_mounted_v[1];
wire        sd_ack        = sd_ack_v[0];
wire        fsd_ack       = sd_ack_v[1];
wire [31:0] fsd_lba;
wire        fsd_rd, fsd_wr, fsd_buff_wr;
wire  [7:0] fsd_buff_din;
wire        img_readonly;
wire [63:0] img_size;
wire [31:0] sd_lba;
wire        sd_rd, sd_wr;
wire [13:0] sd_buff_addr;
wire  [7:0] sd_buff_dout, sd_buff_din;
wire        sd_buff_wr;

hps_io #(.CONF_STR(CONF_STR), .VDNUM(2)) hps_io
(
	.clk_sys(clk_sys),
	.HPS_BUS(HPS_BUS),
	.EXT_BUS(),
	.gamma_bus(),

	.forced_scandoubler(forced_scandoubler),

	.buttons(buttons),
	.status(status),
	.status_menumask(0),

	.ioctl_download(ioctl_download),
	.ioctl_index(ioctl_index),
	.ioctl_wr(ioctl_wr),
	.ioctl_addr(ioctl_addr),
	.ioctl_dout(ioctl_dout),

	.img_mounted(img_mounted_v),
	.img_readonly(img_readonly),
	.img_size(img_size),
	.sd_lba('{sd_lba, fsd_lba}),
	.sd_rd({fsd_rd, sd_rd}),
	.sd_wr({fsd_wr, sd_wr}),
	.sd_ack(sd_ack_v),
	.sd_buff_addr(sd_buff_addr),
	.sd_buff_dout(sd_buff_dout),
	.sd_buff_din('{sd_buff_din, fsd_buff_din}),
	.sd_buff_wr(sd_buff_wr),

	.ps2_key(ps2_key)
);

///////////////////////   CLOCKS   ///////////////////////////////

// The AP68040 closes timing around 30 MHz on this device (the proven
// Minimig-AGA integration runs it at 28.7 MHz), so the system runs at
// 28 MHz and the 1600x912 pixel timing gets its own 100 MHz clock.
wire clk_sys;   // 28 MHz: CPU, devices, DDR3
wire clk_vid;   // 100 MHz: pixel clock
pll pll
(
	.refclk(CLK_50M),
	.rst(0),
	.outclk_0(clk_sys),
	.outclk_1(clk_vid)
);

// hold the machine in reset until a boot ROM has been loaded
wire rom_download = ioctl_download && (ioctl_index[5:0] <= 6'd1);
reg  rom_loaded = 0;
always @(posedge clk_sys) if (rom_download) rom_loaded <= 1;

wire reset = RESET | status[0] | buttons[1] | rom_download | ~rom_loaded;

///////////////////////   SYSTEM    //////////////////////////////

wire        hsync, vsync, hblank, vblank;
wire  [7:0] gray;
wire        led;

wire        ram_req, ram_we, ram_ack;
wire  [3:0] ram_be;
wire [23:0] ram_addr;
wire [31:0] ram_din, ram_dout;

wire        btx_req, btx_rd, btx_ack, btx_done;
wire [10:0] btx_len, btx_addr;
wire  [7:0] btx_q;
wire        brx_start, brx_valid, brx_ready;
wire [10:0] brx_len;
wire  [7:0] brx_data;
wire [47:0] enet_mac;

// CLK_HZ sets the machine's microsecond tick at 50 clocks: with the
// 32 MHz system clock this is a virtual microsecond (the machine runs
// at 64 percent of real time, uniformly), which satisfies the boot
// ROM's CPU-speed calibration invariant (see CPU_PACE_* in
// next_system.sv).  Pacing is off: 32 MHz is already below the
// calibrated speed.
next_system #(
	.CLK_HZ(50000000),
	.CPU_PACE_NUM(2),
	.CPU_PACE_DEN(2),
	.CLK_REAL_HZ(28000000)    // the real clk_sys, so the clock keeps time
) system
(
	.clk(clk_sys),
	.clk_vid(clk_vid),
	.reset(reset),

	.ps2_key(ps2_key),
	.boot_sel(status[57:55]),

	.fimg_mounted(fimg_mounted),
	.fimg_readonly(img_readonly),
	.fimg_size(img_size),
	.fsd_lba(fsd_lba),
	.fsd_rd(fsd_rd),
	.fsd_wr(fsd_wr),
	.fsd_ack(fsd_ack),
	.fsd_buff_addr(sd_buff_addr[8:0]),
	.fsd_buff_dout(sd_buff_dout),
	.fsd_buff_din(fsd_buff_din),
	.fsd_buff_wr(sd_buff_wr),

	.img_mounted(img_mounted),
	.img_readonly(img_readonly),
	.img_size(img_size),
	.sd_lba(sd_lba),
	.sd_rd(sd_rd),
	.sd_wr(sd_wr),
	.sd_ack(sd_ack),
	.sd_buff_addr(sd_buff_addr[8:0]),
	.sd_buff_dout(sd_buff_dout),
	.sd_buff_din(sd_buff_din),
	.sd_buff_wr(sd_buff_wr),

	.rom_wr(ioctl_wr & rom_download),
	.rom_waddr(ioctl_addr[16:0]),
	.rom_wdata(ioctl_dout),

	.hsync(hsync),
	.vsync(vsync),
	.hblank(hblank),
	.vblank(vblank),
	.gray(gray),

	.ram_req(ram_req),
	.ram_we(ram_we),
	.ram_be(ram_be),
	.ram_addr(ram_addr),
	.ram_din(ram_din),
	.ram_dout(ram_dout),
	.ram_ack(ram_ack),

	.led(led),

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

	.dbg_pc(),
	.dbg_halted(),
	.dbg_ipl()
);

assign LED_USER = led;

///////////////////////   DDR3 RAM   /////////////////////////////

assign DDRAM_CLK = clk_sys;

// guest RAM and the ethernet bridge mailbox share the DDRAM port
wire        ga_rd, ga_we, ga_busy, ga_dout_ready;
wire [28:0] ga_addr;
wire [63:0] ga_din, ga_dout;
wire  [7:0] ga_be, ga_burst;

wire        eb_req, eb_we, eb_ack;
wire [28:0] eb_addr;
wire [63:0] eb_wdata, eb_rdata;

next_ddram ddram
(
	.clk(clk_sys),
	.reset(reset),

	.ram_req(ram_req),
	.ram_we(ram_we),
	.ram_be(ram_be),
	.ram_addr(ram_addr),
	.ram_din(ram_din),
	.ram_dout(ram_dout),
	.ram_ack(ram_ack),

	.DDRAM_BUSY(ga_busy),
	.DDRAM_BURSTCNT(ga_burst),
	.DDRAM_ADDR(ga_addr),
	.DDRAM_DOUT(ga_dout),
	.DDRAM_DOUT_READY(ga_dout_ready),
	.DDRAM_RD(ga_rd),
	.DDRAM_DIN(ga_din),
	.DDRAM_BE(ga_be),
	.DDRAM_WE(ga_we)
);

next_ddram_arb ddram_arb
(
	.clk(clk_sys),
	.reset(reset),

	.a_rd(ga_rd),
	.a_we(ga_we),
	.a_addr(ga_addr),
	.a_din(ga_din),
	.a_be(ga_be),
	.a_burst(ga_burst),
	.a_busy(ga_busy),
	.a_dout(ga_dout),
	.a_dout_ready(ga_dout_ready),

	.b_req(eb_req),
	.b_we(eb_we),
	.b_addr(eb_addr),
	.b_wdata(eb_wdata),
	.b_rdata(eb_rdata),
	.b_ack(eb_ack),

	.DDRAM_BUSY(DDRAM_BUSY),
	.DDRAM_BURSTCNT(DDRAM_BURSTCNT),
	.DDRAM_ADDR(DDRAM_ADDR),
	.DDRAM_DOUT(DDRAM_DOUT),
	.DDRAM_DOUT_READY(DDRAM_DOUT_READY),
	.DDRAM_RD(DDRAM_RD),
	.DDRAM_DIN(DDRAM_DIN),
	.DDRAM_BE(DDRAM_BE),
	.DDRAM_WE(DDRAM_WE)
);

next_enet_bridge #(.CLK_HZ(32000000)) enet_bridge
(
	.clk(clk_sys),
	.reset(reset),
	.enable(|status[54:52]),

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

	.guest_mac(enet_mac),

	.m_req(eb_req),
	.m_we(eb_we),
	.m_addr(eb_addr),
	.m_wdata(eb_wdata),
	.m_rdata(eb_rdata),
	.m_ack(eb_ack)
);

///////////////////////   VIDEO    ///////////////////////////////

assign CLK_VIDEO = clk_vid;
assign CE_PIXEL  = 1;

assign VGA_DE = ~(hblank | vblank);
assign VGA_HS = hsync;
assign VGA_VS = vsync;
assign VGA_R  = gray;
assign VGA_G  = gray;
assign VGA_B  = gray;

endmodule
