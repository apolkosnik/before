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
//    - the boot device menu loads the NVRAM boot command on reset
//      ("sd"/"en"/empty per selection, Auto follows the mounted disk)
//      with the matching checksum
//    - burst read auto-increments the address
//    - a write to NVRAM byte 5 reads back
//    - SCR1 reads the machine id
//============================================================================

`timescale 1ns/1ps

module tb_next_rtc;

reg clk = 0;
always #5 clk = ~clk;

reg reset = 1;

reg   [2:0] boot_sel = 0;
reg         disk_mounted = 0;
reg         floppy_mounted = 0;
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
	.clk(clk), .reset(reset),
	.sel(sel), .reg_id(reg_id), .addr1(addr1), .we(we), .be(be),
	.wdata(wdata), .rdata(rdata),
	.scr1(32'h00012052),
	.boot_sel(boot_sel), .disk_mounted(disk_mounted),
	.floppy_mounted(floppy_mounted),
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
reg [7:0] c18, c19;
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

// reset with a menu selection, then read boot command and checksum
task boot_variant;
	input [2:0] bsel;
	input mounted;
	begin
		boot_sel = bsel;
		disk_mounted = mounted;
		reset = 1;
		repeat (5) @(posedge clk);
		reset = 0;
		repeat (5) @(posedge clk);
		rtc_send_byte(8'h12);
		rtc_recv_byte(c18);
		rtc_recv_byte(c19);
		rtc_stop;
		rtc_send_byte(8'h1E);
		rtc_recv_byte(b30);
		rtc_recv_byte(b31);
		rtc_stop;
	end
endtask

initial begin
	if ($test$plusargs("dump")) begin
		$dumpfile("build/tb_next_rtc.vcd");
		$dumpvars(0, tb_next_rtc);
	end

	repeat (10) @(posedge clk);
	reset = 0;
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

	// The chip is battery backed, and the reset that reaches it carries
	// the CPU's RESET instruction, which the ROM and the system software
	// both execute while starting up.  A guest's byte must still be
	// there afterwards.
	reset = 1;
	repeat (5) @(posedge clk);
	reset = 0;
	repeat (5) @(posedge clk);
	rtc_send_byte(8'h05);
	rtc_recv_byte(wb);
	rtc_stop;
	check(wb == 8'h5A, "an NVRAM byte survives a reset");


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

	// boot device menu variants
	boot_variant(3'd0, 1'b0);    // Auto, no disk: empty command
	check(c18 == 8'h00 && c19 == 8'h00, "Auto without disk: empty boot command");
	check(b30 == 8'hE0 && b31 == 8'hEF, "Auto without disk: checksum");

	boot_variant(3'd0, 1'b1);    // Auto with a disk: "sd"
	check(c18 == "s" && c19 == "d", "Auto with disk: boot command sd");
	check(b30 == 8'h6D && b31 == 8'h8B, "Auto with disk: checksum");

	boot_variant(3'd3, 1'b1);    // Network: "en"
	check(c18 == "e" && c19 == "n", "Network: boot command en");
	check(b30 == 8'h7B && b31 == 8'h81, "Network: checksum");

	boot_variant(3'd4, 1'b1);    // ROM Default: empty even with a disk
	check(c18 == 8'h00 && c19 == 8'h00, "ROM Default: empty boot command");

	boot_variant(3'd1, 1'b0);    // Disk forced, even without an image
	check(c18 == "s" && c19 == "d", "Disk: boot command sd");

	// Floppy, and Auto falling through to a mounted floppy
	boot_variant(3'd2, 1'b0);
	check(c18 == "f" && c19 == "d", "Floppy: boot command fd");
	check(b30 == 8'h7A && b31 == 8'h8B, "Floppy: checksum");
	floppy_mounted = 1;
	boot_variant(3'd0, 1'b0);    // Auto: no disk, but a floppy
	check(c18 == "f" && c19 == "d", "Auto with only a floppy: boot command fd");
	floppy_mounted = 0;

	if (errors == 0) $display("ALL PASS");
	else             $display("%0d FAILURES", errors);
	$finish;
end

endmodule
