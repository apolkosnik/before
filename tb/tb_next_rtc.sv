//============================================================================
//  NeXT RTC/NVRAM serial interface test
//
//  Bit-bangs the MC68HC68T1 serial protocol through SCR2 byte 2 of the
//  real next_scr module, the way the boot ROM does: RTCE high, address
//  byte then data byte(s) MSB first, data sampled by the chip on the
//  falling edge of RTCLK.
//
//  Checks against the nvram_default[] content from Previous rtcnvram.c:
//    - NVRAM byte 0 reads 0x94, byte 1 reads 0x0F
//    - checksum bytes 30/31 carry the one's-complement sum
//    - a user/configuration reset applies the selected boot policy while
//      a CPU RESET leaves the battery-backed NVRAM alone
//    - Auto is resolved from the mounted HDD/floppy state at that reset
//    - CD-ROM probe commands cover scan-order units 0 through 3
//    - applying a boot policy preserves unrelated bytes and recomputes
//      the checksum from their live values
//    - burst read auto-increments the address
//    - a write to NVRAM byte 5 reads back
//    - SCR1 reads the machine id
//============================================================================

`timescale 1ns/1ps

module tb_next_rtc;

reg clk = 0;
always #5 clk = ~clk;

reg reset = 1;
reg config_reset = 1;

reg   [2:0] boot_sel = 0;
reg         floppy_mounted = 0;
reg   [2:0] sd_lower_mounted = 0;
reg         sel = 0;
reg   [1:0] reg_id = 0;
reg         addr1 = 0;
reg         we = 0;
reg   [1:0] be = 0;
reg  [15:0] wdata = 0;
wire [15:0] rdata;

// CLK_HZ of 1000 puts a clock second within reach of a simulation
next_scr #(.CLK_HZ(1000)) dut
(
	.clk(clk), .reset(reset), .config_reset(config_reset),
	.sel(sel), .reg_id(reg_id), .addr1(addr1), .we(we), .be(be),
	.wdata(wdata), .rdata(rdata),
	.scr1(32'h00012052),
	.boot_sel(boot_sel), .floppy_mounted(floppy_mounted),
	.sd_lower_mounted(sd_lower_mounted),
	.timer_ipl7(), .led(), .rom_overlay(),
	.softint1(), .softint2()
);

// one bus write to SCR2 byte 2
task scr2_byte2_write;
	input [7:0] val;
	begin
		@(posedge clk);
		sel <= 1; reg_id <= 2'd2; addr1 <= 1; we <= 1; be <= 2'b10;
		wdata <= {val, 8'h00};
		@(posedge clk);
		sel <= 0; we <= 0;
	end
endtask

// one bus read of SCR2 word 1 (bytes 2 and 3)
task scr2_word1_read;
	output [15:0] val;
	begin
		@(posedge clk);
		sel <= 1; reg_id <= 2'd2; addr1 <= 1; we <= 0; be <= 2'b11;
		@(posedge clk);
		val = rdata;
		sel <= 0;
	end
endtask

localparam RTCE = 8'h01, RTCLK = 8'h02, RTDATA = 8'h04;

// clock one bit into the interface, return the interface data bit
task rtc_bit;
	input  b_in;
	output b_out;
	reg [15:0] r;
	begin
		scr2_byte2_write(RTCE | RTCLK | (b_in ? RTDATA : 8'h00));
		scr2_byte2_write(RTCE |         (b_in ? RTDATA : 8'h00));
		scr2_word1_read(r);
		b_out = r[10];   // byte 2 bit 2 (RTDATA)
	end
endtask

task rtc_send_byte;
	input [7:0] v;
	integer i;
	reg dummy;
	begin
		for (i = 7; i >= 0; i = i - 1) rtc_bit(v[i], dummy);
	end
endtask

task rtc_recv_byte;
	output [7:0] v;
	integer i;
	reg b;
	begin
		for (i = 7; i >= 0; i = i - 1) begin
			rtc_bit(1'b0, b);
			v[i] = b;
		end
	end
endtask

task rtc_stop;
	begin
		scr2_byte2_write(8'h00);   // RTCE low resets the interface
	end
endtask

integer errors = 0;

task check;
	input cond;
	input [639:0] name;
	begin
		if (cond) $display("PASS: %0s", name);
		else begin $display("FAIL: %0s", name); errors = errors + 1; end
	end
endtask

reg [7:0] b0, b1, b30, b31, wb;
reg [7:0] boot_cmd [0:11];
reg [7:0] r_sec, r_min, r_hour, r_wday, r_mday, r_month, r_year;
reg [15:0] w;


// read the whole clock through the serial interface
task read_clock;
	begin
		rtc_send_byte(8'h20);
		rtc_recv_byte(r_sec);
		rtc_recv_byte(r_min);
		rtc_recv_byte(r_hour);
		rtc_recv_byte(r_wday);
		rtc_recv_byte(r_mday);
		rtc_recv_byte(r_month);
		rtc_recv_byte(r_year);
		rtc_stop;
	end
endtask

// write the whole clock the way the guest does
task write_clock;
	input [7:0] sec, min, hour, mday, month, year;
	begin
		rtc_send_byte(8'ha0); rtc_send_byte(sec);   rtc_stop;
		rtc_send_byte(8'ha1); rtc_send_byte(min);   rtc_stop;
		rtc_send_byte(8'ha2); rtc_send_byte(hour);  rtc_stop;
		rtc_send_byte(8'ha4); rtc_send_byte(mday);  rtc_stop;
		rtc_send_byte(8'ha5); rtc_send_byte(month); rtc_stop;
		rtc_send_byte(8'ha6); rtc_send_byte(year);  rtc_stop;
	end
endtask

// Read all 12 boot-command bytes and the stored checksum.  Reading the
// complete field catches stale suffixes when a long CD command is replaced
// by a short command.
task read_boot_state;
	integer j;
	begin
		rtc_send_byte(8'h12);
		for (j = 0; j < 12; j = j + 1) rtc_recv_byte(boot_cmd[j]);
		rtc_stop;
		rtc_send_byte(8'h1E);
		rtc_recv_byte(b30);
		rtc_recv_byte(b31);
		rtc_stop;
	end
endtask

task write_nvram_byte;
	input [4:0] a;
	input [7:0] v;
	begin
		rtc_send_byte({3'b100, a});
		rtc_send_byte(v);
		rtc_stop;
	end
endtask

task read_nvram_byte;
	input  [4:0] a;
	output [7:0] v;
	begin
		rtc_send_byte({3'b000, a});
		rtc_recv_byte(v);
		rtc_stop;
	end
endtask

// A CPU RESET resets the RTC serial engine but must not apply OSD policy.
task cpu_reset;
	begin
		reset = 1;
		repeat (5) @(posedge clk);
		reset = 0;
		repeat (5) @(posedge clk);
	end
endtask

// A user/configuration reset both resets devices and applies the boot policy.
task user_reset;
	begin
		reset = 1;
		config_reset = 1;
		repeat (5) @(posedge clk);
		reset = 0;
		config_reset = 0;
		repeat (5) @(posedge clk);
	end
endtask

task boot_variant;
	input [2:0] bsel;
	input [2:0] hdds;
	input floppy;
	begin
		boot_sel = bsel;
		sd_lower_mounted = hdds;
		floppy_mounted = floppy;
		user_reset;
		read_boot_state;
	end
endtask

task check_short_command;
	input [7:0] c0;
	input [7:0] c1;
	input [639:0] name;
	begin
		check(boot_cmd[0] == c0 && boot_cmd[1] == c1 &&
		      boot_cmd[2] == 0 && boot_cmd[3] == 0 &&
		      boot_cmd[4] == 0 && boot_cmd[5] == 0 &&
		      boot_cmd[6] == 0 && boot_cmd[7] == 0 &&
		      boot_cmd[8] == 0 && boot_cmd[9] == 0 &&
		      boot_cmd[10] == 0 && boot_cmd[11] == 0, name);
	end
endtask

task check_cd_command;
	input [1:0] unit_no;
	input [7:0] checksum_lo;
	begin
		$display("CD-ROM scan-order unit %0d", unit_no);
		check(boot_cmd[0] == "s" && boot_cmd[1] == "d" &&
		      boot_cmd[2] == "(" && boot_cmd[3] == ("0" + {6'd0, unit_no}) &&
		      boot_cmd[4] == "," && boot_cmd[5] == "0" &&
		      boot_cmd[6] == "," && boot_cmd[7] == "0" &&
		      boot_cmd[8] == ")" && boot_cmd[9] == 0 &&
		      boot_cmd[10] == 0 && boot_cmd[11] == 0,
		      "CD-ROM command is sd(N,0,0) with a cleared suffix");
		check(b30 == 8'hC3 && b31 == checksum_lo,
		      "CD-ROM command checksum matches its scan-order unit");
	end
endtask

initial begin
	if ($test$plusargs("dump")) begin
		$dumpfile("build/tb_next_rtc.vcd");
		$dumpvars(0, tb_next_rtc);
	end

	repeat (10) @(posedge clk);
	reset = 0;
	config_reset = 0;
	repeat (10) @(posedge clk);

	// SCR1 through the register interface
	@(posedge clk);
	sel <= 1; reg_id <= 2'd0; addr1 <= 0; we <= 0; be <= 2'b11;
	@(posedge clk);
	w[15:0] = rdata; sel <= 0;
	check(w == 16'h0001, "SCR1 high word is 0x0001");
	@(posedge clk);
	sel <= 1; reg_id <= 2'd0; addr1 <= 1; we <= 0;
	@(posedge clk);
	w[15:0] = rdata; sel <= 0;
	check(w == 16'h2052, "SCR1 low word is 0x2052");

	// burst read NVRAM bytes 0 and 1 (address auto-increment)
	rtc_send_byte(8'h00);
	rtc_recv_byte(b0);
	rtc_recv_byte(b1);
	rtc_stop;
	$display("nvram[0]=%02x nvram[1]=%02x", b0, b1);
	check(b0 == 8'h94, "NVRAM byte 0 is 0x94");
	check(b1 == 8'h0F, "NVRAM byte 1 is 0x0F (auto-increment works)");

	// checksum bytes
	rtc_send_byte(8'h1E);
	rtc_recv_byte(b30);
	rtc_recv_byte(b31);
	rtc_stop;
	$display("nvram[30]=%02x nvram[31]=%02x", b30, b31);
	check(b30 == 8'hE0 && b31 == 8'hEF, "NVRAM checksum bytes are 0xE0 0xEF");

	// write NVRAM byte 5 and read it back
	rtc_send_byte(8'h85);          // write, address 5
	rtc_send_byte(8'h5A);
	rtc_stop;
	rtc_send_byte(8'h05);
	rtc_recv_byte(wb);
	rtc_stop;
	$display("nvram[5]=%02x after write", wb);
	check(wb == 8'h5A, "NVRAM write/readback");

	// The chip is battery backed.  A CPU RESET instruction resets the
	// serial interface, but must not alter a guest-written NVRAM byte.
	cpu_reset;
	read_nvram_byte(5'd5, wb);
	check(wb == 8'h5A, "an NVRAM byte survives a CPU reset");


	// The ROM's clock test waits up to 1100 ms for the seconds register
	// to change, and the reset it competes with carries the CPU's RESET
	// instruction.  A clock restarted by that never ticks: POST 91.
	rtc_send_byte(8'h20);
	rtc_recv_byte(r_sec);
	rtc_stop;
	repeat (400) @(posedge clk);
	reset = 1;                    // a RESET instruction lands mid-second
	repeat (5) @(posedge clk);
	reset = 0;
	repeat (900) @(posedge clk);  // past one clock second in total
	rtc_send_byte(8'h20);
	rtc_recv_byte(r_min);
	rtc_stop;
	$display("seconds %02x -> %02x across a reset", r_sec, r_min);
	check(r_min != r_sec, "the seconds keep counting across a reset");

	// Applying a policy must derive its checksum from the live NVRAM, not
	// a fixed per-command constant.  Byte 5 contributes as the low byte of
	// a checksum word: 6D8B - 005A = 6D31 for the "sd" command.
	boot_sel = 3'd1;
	repeat (10) @(posedge clk);
	read_boot_state;
	check_short_command(8'h00, 8'h00,
	                    "changing the OSD selection does not immediately rewrite NVRAM");
	user_reset;
	read_boot_state;
	check_short_command("s", "d", "user reset applies the Disk command");
	check(b30 == 8'h6D && b31 == 8'h31,
	      "boot checksum includes a preserved guest NVRAM byte");
	read_nvram_byte(5'd5, wb);
	check(wb == 8'h5A, "user reset preserves unrelated NVRAM bytes");

	// Restore the unrelated byte so the remaining checks can use the
	// reference command checksums directly.
	write_nvram_byte(5'd5, 8'h00);
	user_reset;
	read_boot_state;
	check(b30 == 8'h6D && b31 == 8'h8B,
	      "an unchanged policy is reapplied and its checksum recomputed");

	// Simulate a guest replacing the boot command and leaving a stale
	// suffix.  A CPU RESET preserves it; the next user reset reapplies the
	// selected policy and clears all twelve command bytes first.
	write_nvram_byte(5'd18, "e");
	write_nvram_byte(5'd19, "n");
	write_nvram_byte(5'd20, "X");
	cpu_reset;
	read_boot_state;
	check(boot_cmd[0] == "e" && boot_cmd[1] == "n" && boot_cmd[2] == "X",
	      "CPU reset preserves a guest-modified boot command");
	user_reset;
	read_boot_state;
	check_short_command("s", "d",
	                    "user reset restores the selected command and clears its suffix");
	check(b30 == 8'h6D && b31 == 8'h8B,
	      "user reset restores the selected command checksum");

	// Auto is sampled only on a user reset.  Hot media changes and a CPU
	// RESET do not silently replace the guest's battery-backed command.
	boot_variant(3'd0, 3'b000, 1'b0);
	check_short_command(8'h00, 8'h00, "Auto without media: empty boot command");
	check(b30 == 8'hE0 && b31 == 8'hEF, "Auto without media: checksum");
	sd_lower_mounted = 3'b001;
	repeat (10) @(posedge clk);
	cpu_reset;
	read_boot_state;
	check_short_command(8'h00, 8'h00,
	                    "hot HDD insertion and CPU reset leave Auto command unchanged");
	user_reset;
	read_boot_state;
	check_short_command("s", "d", "user reset resolves Auto to a mounted HDD");
	check(b30 == 8'h6D && b31 == 8'h8B, "Auto with HDD: checksum");

	sd_lower_mounted = 3'b000;
	floppy_mounted = 1;
	repeat (10) @(posedge clk);
	read_boot_state;
	check_short_command("s", "d", "hot media change does not immediately rerun Auto");
	user_reset;
	read_boot_state;
	check_short_command("f", "d", "user reset resolves Auto to a mounted floppy");
	check(b30 == 8'h7A && b31 == 8'h8B, "Auto with only a floppy: checksum");

	// A hot OSD change is policy, not an immediate NVRAM write.
	boot_sel = 3'd3;
	repeat (10) @(posedge clk);
	read_boot_state;
	check_short_command("f", "d", "hot OSD change leaves the current command intact");
	user_reset;
	read_boot_state;
	check_short_command("e", "n", "user reset applies the Network command");
	check(b30 == 8'h7B && b31 == 8'h81, "Network: checksum");

	boot_variant(3'd4, 3'b111, 1'b1); // ROM Default ignores mounted media
	check_short_command(8'h00, 8'h00, "ROM Default: empty boot command");
	check(b30 == 8'hE0 && b31 == 8'hEF, "ROM Default: checksum");

	boot_variant(3'd1, 3'b000, 1'b0); // Disk forced without an image
	check_short_command("s", "d", "Disk: boot command sd");
	check(b30 == 8'h6D && b31 == 8'h8B, "Disk: checksum");

	boot_variant(3'd2, 3'b000, 1'b0); // Floppy forced without an image
	check_short_command("f", "d", "Floppy: boot command fd");
	check(b30 == 8'h7A && b31 == 8'h8B, "Floppy: checksum");

	// Optical.  nvram_init() in the reference spells it od, alongside
	// sd, fd and en.
	boot_variant(3'd5, 3'b000, 1'b0);
	check_short_command("o", "d", "Optical: boot command od");
	check(b30 == 8'h71 && b31 == 8'h8B, "Optical: checksum");

	// CD-ROM is a qualified SCSI probe command.  Its unit is the count of
	// mounted HDD targets below target 3.  Cover every representable count,
	// including a hot count change that must wait for user reset.
	boot_variant(3'd6, 3'b000, 1'b0);
	check_cd_command(2'd0, 8'hFA);
	sd_lower_mounted = 3'b001;
	repeat (10) @(posedge clk);
	read_boot_state;
	check_cd_command(2'd0, 8'hFA);
	user_reset;
	read_boot_state;
	check_cd_command(2'd1, 8'hF9);
	boot_variant(3'd6, 3'b011, 1'b0);
	check_cd_command(2'd2, 8'hF8);
	boot_variant(3'd6, 3'b111, 1'b0);
	check_cd_command(2'd3, 8'hF7);

	// Replacing the longest command must clear its tail.
	boot_variant(3'd1, 3'b000, 1'b0);
	check_short_command("s", "d", "short command clears the previous CD-ROM suffix");

	if (errors == 0) $display("ALL PASS");
	else             $display("%0d FAILURES", errors);
	$finish;
end

endmodule
