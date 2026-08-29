//============================================================================
//  NeXT system control registers and RTC/NVRAM
//
//  SCR1 (read only)          0x0200c000..3
//  Slot ID (read only)       0x0200c800..3
//  SCR2 (read/write)         0x0200d000..3
//
//  Modeled on Previous src/sysReg.c and src/rtcnvram.c.
//
//  SCR1 for a 25MHz NeXTcube 68040 with 100ns memory is 0x00012052
//  (SCR1_CUBE in sysReg.c: dma rev 1, cpu type 2 = NeXTcube, board rev 0,
//  vmem speed 0x40 | mem speed 0x10, cpu speed 2 = 25MHz).
//
//  SCR2 byte 2 implements the bit serial interface to the MC68HC68T1
//  real time clock chip (SCR2_RTDATA 0x04, SCR2_RTCLK 0x02, SCR2_RTCE
//  0x01), following oldrtc_interface_io() in rtcnvram.c: 8 address bits
//  (bit 7 = write, bit 5 = clock regs), then 8 data bits, MSB first,
//  advanced on each falling edge of RTCLK while RTCE is high.  The
//  address auto-increments for burst access (0x9F wraps to 0x00, 0xB2
//  wraps to 0x20).
//
//  NVRAM power-on content is the nvram_default[] image from rtcnvram.c,
//  which carries a valid checksum in bytes 30/31.
//============================================================================

module next_scr #(parameter CLK_HZ = 100000000)
(
	input         clk,
	input         reset,

	// register access
	input         sel,
	input   [1:0] reg_id,        // 0 = SCR1, 1 = SID, 2 = SCR2
	input         addr1,         // addr[1]: word within the register
	input         we,
	input   [1:0] be,            // {even byte, odd byte} lanes
	input  [15:0] wdata,
	output [15:0] rdata,

	// SCR1 value (machine id), default 25MHz Cube 040
	input  [31:0] scr1,

	// boot device menu: 0 = Auto (disk when an image is mounted, else
	// the ROM default order), 1 = Disk, 2 = Network, 3 = ROM Default.
	// Loaded into the NVRAM boot command on reset.
	input   [1:0] boot_sel,
	input         disk_mounted,

	output        timer_ipl7,    // SCR2 byte 2 bit 7
	output        led,           // SCR2 byte 3 bit 0
	output        rom_overlay,   // SCR2 byte 3 bit 7 (not used by decode)

	// soft interrupt levels (INT_SOFT1/INT_SOFT2), level signals
	output        softint1,
	output        softint2
);

reg [7:0] scr2_0, scr2_1, scr2_2, scr2_3;

assign timer_ipl7 = scr2_2[7];
assign led        = scr2_3[0];
assign rom_overlay= scr2_3[7];
assign softint1   = scr2_0[0];
assign softint2   = scr2_0[1];

//----------------------------------------------------------------------------
// MC68HC68T1 RTC and NVRAM
//----------------------------------------------------------------------------

reg  [7:0] nvram [0:31];
reg  [7:0] rtc_addr;
reg  [4:0] rtc_phase;            // 0..16, matches 'phase' in rtcnvram.c
reg  [7:0] rtc_val;

reg  [7:0] clkctrl;              // reg 0x31
reg  [7:0] intctrl;              // reg 0x32

// time of day, BCD (date part is static, see docs/PORTING.md)
reg  [7:0] t_sec, t_min, t_hour;
reg  [7:0] t_wday, t_mday, t_month, t_year;

// one second tick
localparam integer SEC_DIV = CLK_HZ;
reg [$clog2(SEC_DIV)-1:0] sec_presc;
wire sec_tick = (sec_presc == SEC_DIV-1);

function [7:0] bcd_inc;
	input [7:0] v;
	bcd_inc = (v[3:0] == 4'd9) ? {v[7:4] + 4'd1, 4'd0} : {v[7:4], v[3:0] + 4'd1};
endfunction

// clock register read, rtc_get_clock() in rtcnvram.c
function [7:0] clock_get;
	input [7:0] a;
	case (a[6:0])
		7'h20: clock_get = t_sec;
		7'h21: clock_get = t_min;
		7'h22: clock_get = t_hour;
		7'h23: clock_get = t_wday;
		7'h24: clock_get = t_mday;
		7'h25: clock_get = t_month;
		7'h26: clock_get = t_year;
		7'h30: clock_get = 8'h00;      // status
		7'h31: clock_get = clkctrl;
		7'h32: clock_get = intctrl;
		default: clock_get = 8'h00;
	endcase
endfunction

// serial engine state advance happens on SCR2 byte 2 writes below

//----------------------------------------------------------------------------
// register access
//----------------------------------------------------------------------------

wire [15:0] scr1_hi = scr1[31:16];
wire [15:0] scr1_lo = scr1[15:0];

assign rdata = (reg_id == 2'd0) ? (addr1 ? scr1_lo : scr1_hi) :
               (reg_id == 2'd1) ? 16'h0000 :                     // slot ID
               (addr1 ? {scr2_2, scr2_3} : {scr2_0, scr2_1});

wire       scr2_we    = sel && we && (reg_id == 2'd2);
wire [7:0] w_scr2_2   = wdata[15:8];

// falling edge of RTCLK with RTCE high, sampled from the written value as
// in Previous SCR2_Write2 (old RTCLK=1, new RTCLK=0)
wire rtc_step = scr2_we && addr1 && be[1] && w_scr2_2[0] && scr2_2[1] && !w_scr2_2[1];
wire rtc_bit_in = w_scr2_2[2];

wire [4:0] next_phase = rtc_phase + 5'd1;
wire       rtc_is_write = rtc_addr[7];
wire       rtc_is_clock = rtc_addr[5];
wire [7:0] rtc_load = rtc_is_clock ? clock_get(rtc_addr) : nvram[rtc_addr[4:0]];

// output bit for read transfers: bit (16 - phase) of the value
wire [7:0] rtc_val_cur = (next_phase == 5'd9 && !rtc_is_write) ? rtc_load : rtc_val;
wire [3:0] rtc_bit_idx = 5'd16 - next_phase;
wire       rtc_bit_out = rtc_is_write ? rtc_bit_in : rtc_val_cur[rtc_bit_idx[2:0]];

integer i;

// effective boot device: 1 = SCSI disk ("sd"), 2 = ethernet ("en"),
// 3 = empty boot command (the ROM walks its device table, network
// first).  Auto picks the disk exactly when an image is mounted.
wire [1:0] bootdev = (boot_sel == 2'd0) ? (disk_mounted ? 2'd1 : 2'd3)
                                        : boot_sel;

// nvram_default[] from Previous rtcnvram.c with the boot command from
// the OSD (nvram_init() semantics) and the matching checksum: 16-bit
// one's-complement sum over bytes 0-29, complemented, at bytes 30/31
function automatic [7:0] nv_init;
	input [4:0] i;
	input [1:0] dev;
	begin
		case (i)
			5'd0:  nv_init = 8'h94;
			5'd1:  nv_init = 8'h0F;
			5'd2:  nv_init = 8'h40;
			5'd14: nv_init = 8'h4B;
			5'd18: nv_init = (dev == 2'd1) ? "s" :
			                 (dev == 2'd2) ? "e" : 8'h00;
			5'd19: nv_init = (dev == 2'd1) ? "d" :
			                 (dev == 2'd2) ? "n" : 8'h00;
			5'd30: nv_init = (dev == 2'd1) ? 8'h6D :
			                 (dev == 2'd2) ? 8'h7B : 8'hE0;
			5'd31: nv_init = (dev == 2'd1) ? 8'h8B :
			                 (dev == 2'd2) ? 8'h81 : 8'hEF;
			default: nv_init = 8'h00;
		endcase
	end
endfunction

always @(posedge clk) begin
	if (reset) begin
		for (i = 0; i < 32; i = i + 1) nvram[i] <= nv_init(i[4:0], bootdev);
		scr2_0 <= 8'h00;
		scr2_1 <= 8'h00;
		scr2_2 <= 8'h00;   // non-turbo reset values, SCR_Reset() in sysReg.c
		scr2_3 <= 8'h00;
		rtc_phase <= 0;
		rtc_addr <= 0;
		rtc_val <= 0;
		clkctrl <= 8'h00;
		intctrl <= 8'h00;
		t_sec <= 8'h00; t_min <= 8'h00; t_hour <= 8'h00;
		t_wday <= 8'h01; t_mday <= 8'h01; t_month <= 8'h01; t_year <= 8'h00;
		sec_presc <= 0;
	end
	else begin
		// time of day
		sec_presc <= sec_tick ? 1'd0 : sec_presc + 1'd1;
		if (sec_tick) begin
			if (t_sec == 8'h59) begin
				t_sec <= 0;
				if (t_min == 8'h59) begin
					t_min <= 0;
					t_hour <= (t_hour == 8'h23) ? 8'h00 : bcd_inc(t_hour);
				end
				else t_min <= bcd_inc(t_min);
			end
			else t_sec <= bcd_inc(t_sec);
		end

		// SCR2 writes
		if (scr2_we) begin
			if (!addr1) begin
				if (be[1]) scr2_0 <= wdata[15:8];
				if (be[0]) scr2_1 <= wdata[7:0];
			end
			else begin
				if (be[1]) scr2_2 <= w_scr2_2;
				if (be[0]) scr2_3 <= wdata[7:0];
			end
		end

		// RTC serial interface, oldrtc_interface_io() in rtcnvram.c
		if (scr2_we && addr1 && be[1] && !w_scr2_2[0]) begin
			// RTCE low resets the interface
			rtc_phase <= 0;
			rtc_addr <= 0;
		end
		else if (rtc_step) begin
			if (next_phase <= 5'd8) begin
				rtc_addr <= {rtc_addr[6:0], rtc_bit_in};
				rtc_phase <= next_phase;
			end
			else begin
				if (rtc_is_write) rtc_val <= {rtc_val_cur[6:0], rtc_bit_in};
				else              rtc_val <= rtc_val_cur;

				// reflect the interface data bit in SCR2 byte 2 readback
				scr2_2 <= {w_scr2_2[7:3], rtc_bit_out, w_scr2_2[1:0]};

				if (next_phase == 5'd16) begin
					if (rtc_is_write) begin
						if (rtc_is_clock) begin
							case (rtc_addr[6:0])
								7'h20: t_sec  <= {rtc_val_cur[6:0], rtc_bit_in};
								7'h21: t_min  <= {rtc_val_cur[6:0], rtc_bit_in};
								7'h22: t_hour <= {rtc_val_cur[6:0], rtc_bit_in};
								7'h24: t_mday <= {rtc_val_cur[6:0], rtc_bit_in};
								7'h25: t_month<= {rtc_val_cur[6:0], rtc_bit_in};
								7'h26: t_year <= {rtc_val_cur[6:0], rtc_bit_in};
								7'h31: clkctrl<= {rtc_val_cur[6:0], rtc_bit_in};
								7'h32: intctrl<= {rtc_val_cur[6:0], rtc_bit_in};
								default: ;
							endcase
						end
						else nvram[rtc_addr[4:0]] <= {rtc_val_cur[6:0], rtc_bit_in};
					end
					// address auto-increment with the wrap rules from
					// oldrtc_interface_io()
					case (rtc_addr)
						8'h9F:   rtc_addr <= 8'h00;
						8'hB2:   rtc_addr <= 8'h20;
						default: rtc_addr <= rtc_addr + 8'd1;
					endcase
					rtc_phase <= 5'd8;
				end
				else rtc_phase <= next_phase;
			end
		end
	end
end

endmodule
