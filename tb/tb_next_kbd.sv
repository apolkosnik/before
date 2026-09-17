//============================================================================
//  KMS keyboard test: drives the real next_kms_snd with MiSTer ps2_key
//  events and the KMS command protocol the ROM uses:
//    - KMSCMD_KBD_RECV programs the device poll mask; with the
//      keyboard's address absent, events are dropped
//    - with the keyboard polled, a key press posts an event to the
//      keyboard/mouse data register in the kms.c layout (device
//      master, valid, modifiers, up/down, NeXT keycode) with
//      KBD_RECEIVED/KBD_INT and the INT_KEYMOUSE level
//    - reading the data register consumes the event and releases the
//      interrupt; a second event before the read sets KBD_OVERRUN
//    - modifiers travel in the event byte (shift held during a press)
//    - KMSCMD_KMREG set-address and reset answer with kms_response()
//============================================================================

`timescale 1ns/1ps

module tb_next_kbd;

reg clk = 0;
always #5 clk = ~clk;

reg reset = 1;

reg  [10:0] ps2 = 0;
reg  [24:0] ps2m = 0;

reg         sel_kms = 0;
reg   [3:0] addr = 0;
reg         we = 0;
reg   [1:0] be = 0;
reg  [15:0] wdata = 0;
wire [15:0] rdata;
wire        int_keymouse, int_power;

next_kms_snd #(.CLK_HZ(1000000)) dut
(
	.clk(clk), .reset(reset),
	.ps2_key(ps2),
	.ps2_mouse(ps2m),
	.sel_kms(sel_kms),
	.sel_csr(1'b0), .sel_sptr(1'b0), .sel_ptr(1'b0), .sel_ini(1'b0),
	.addr(addr), .we(we), .be(be), .wdata(wdata), .rdata(rdata),
	.m_req(), .m_we(), .m_addr(), .m_be(), .m_din(),
	.m_dout(32'd0), .m_ack(1'b0), .m_err(1'b0),
	.int_snd_ovrun(), .int_snd_out_dma(),
	.int_keymouse(int_keymouse),
	.int_power(int_power),
	.sndin_active(), .sndin_clear(), .sndin_request(1'b0), .sndin_overrun(1'b0),
	.audio_l(), .audio_r()
);

task kms_wr8;
	input [3:0] a;
	input [7:0] v;
	begin
		@(posedge clk);
		sel_kms <= 1; addr <= a; we <= 1;
		be <= a[0] ? 2'b01 : 2'b10;
		wdata <= a[0] ? {8'h00, v} : {v, 8'h00};
		@(posedge clk);
		sel_kms <= 0; we <= 0;
	end
endtask

task kms_rd8;
	input [3:0] a;
	output [7:0] v;
	begin
		@(posedge clk);
		sel_kms <= 1; addr <= a; we <= 0;
		be <= a[0] ? 2'b01 : 2'b10;
		@(posedge clk);
		v = a[0] ? rdata[7:0] : rdata[15:8];
		sel_kms <= 0;
	end
endtask

task kms_cmd;
	input [7:0] cmd;
	input [31:0] data;
	begin
		kms_wr8(4'h3, cmd);
		kms_wr8(4'h4, data[31:24]);
		kms_wr8(4'h5, data[23:16]);
		kms_wr8(4'h6, data[15:8]);
		kms_wr8(4'h7, data[7:0]);
	end
endtask

task rd_km_data;
	output [31:0] v;
	begin
		kms_rd8(4'h8, v[31:24]);
		kms_rd8(4'h9, v[23:16]);
		kms_rd8(4'hA, v[15:8]);
		kms_rd8(4'hB, v[7:0]);
	end
endtask

task key;
	input make;
	input ext;
	input [7:0] code;
	begin
		@(posedge clk);
		ps2 <= {~ps2[10], make, ext, code};
		repeat (4) @(posedge clk);
	end
endtask

// one MiSTer mouse packet.  dx/dy are 9-bit signed; dy uses the PS/2
// convention (positive = up), which the device negates to NeXT down.
task mouse;
	input signed [8:0] dx;
	input signed [8:0] dy;
	input left;
	input right;
	begin
		@(posedge clk);
		ps2m <= {~ps2m[24], dy[7:0], dx[7:0],
		         {2'b00, dy[8], dx[8], 1'b1, 1'b0, right, left}};
		repeat (4) @(posedge clk);
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

reg [31:0] d;
reg [7:0] v;

initial begin
	repeat (10) @(posedge clk);
	reset = 0;
	repeat (10) @(posedge clk);

	// disable all poll slots (0xF nibbles): a key event is dropped
	// (a zero mask counts as device 0 polled, as in kms.c)
	kms_cmd(8'hC6, 32'hFFFFFFF2);
	key(1, 0, 8'h1C);            // 'a' down
	check(!int_keymouse, "event dropped while keyboard not polled");
	key(1, 0, 8'h09);            // F10 does not require keyboard polling
	check(int_power && !int_keymouse, "F10 asserts separate power request while unpolled");
	key(1, 0, 8'h09);            // repeated make, not a toggle
	check(int_power && !int_keymouse, "F10 repeat keeps power asserted without KMS event");
	key(1, 1, 8'h71); key(0, 1, 8'h71); // forward Delete is never power
	check(int_power && !int_keymouse, "Delete cannot release held F10 or post a KMS event");
	key(0, 0, 8'h09);
	check(!int_power && !int_keymouse, "F10 release clears power while unpolled");

	// poll the keyboard (device address 0 in the first mask nibble)
	kms_cmd(8'hC6, 32'h0FFFFFF2);
	key(1, 0, 8'h1C);            // 'a' down
	check(int_keymouse, "INT_KEYMOUSE raised on key down");
	kms_rd8(4'h1, v);
	check(v[7] && v[6], "KBD_INT and KBD_RECEIVED set");
	rd_km_data(d);
	check(d == 32'h1000_8039, "event: master, valid, keycode 0x39 down");
	check(!int_keymouse, "interrupt released by the data read");

	key(0, 0, 8'h1C);            // 'a' up
	rd_km_data(d);
	check(d == 32'h1000_80B9, "key up event carries the up bit");

	key(1, 0, 8'h61); rd_km_data(d);
	check(d == 32'h1000_8003, "ISO extra backslash maps to NeXT backslash");
	key(0, 0, 8'h61); rd_km_data(d);
	check(d == 32'h1000_8083, "ISO extra backslash release");
	key(1, 0, 8'h0F); rd_km_data(d);
	check(d == 32'h1000_8027, "keypad equals is distinct from main equals");
	key(0, 0, 8'h0F); rd_km_data(d);
	check(d == 32'h1000_80A7, "keypad equals release");
	key(1, 0, 8'h66); rd_km_data(d);
	check(d == 32'h1000_801B && !int_power, "Backspace remains NeXT Delete, not power");
	key(0, 0, 8'h66); rd_km_data(d);
	key(1, 1, 8'h71); key(0, 1, 8'h71);
	check(!int_keymouse && !int_power, "forward Delete deliberately remains unassigned");
	key(1, 0, 8'h09);
	check(int_power && !int_keymouse, "F10 is not delivered as an ordinary polled key");
	key(0, 0, 8'h09);
	check(!int_power && !int_keymouse, "polled F10 release has no KMS event");

	// The arrow keys arrive as extended scancodes, and share their
	// second byte with the keypad digits: 0x75 is kp 8 without the
	// E0 prefix and up arrow with it.  Reading the prefix wrongly
	// makes the arrows type numbers, or nothing at all.
	key(1, 1, 8'h6B);            // left
	rd_km_data(d);
	check(d == 32'h1000_8009, "left arrow is keycode 0x09");
	key(1, 1, 8'h74);            // right
	rd_km_data(d);
	check(d == 32'h1000_8010, "right arrow is keycode 0x10");
	key(1, 1, 8'h75);            // up
	rd_km_data(d);
	check(d == 32'h1000_8016, "up arrow is keycode 0x16");
	key(1, 1, 8'h72);            // down
	rd_km_data(d);
	check(d == 32'h1000_800F, "down arrow is keycode 0x0f");
	key(0, 1, 8'h72);            // down, released
	rd_km_data(d);
	check(d == 32'h1000_808F, "arrow release carries the up bit");

	// the same codes without the prefix are the keypad digits
	key(1, 0, 8'h75);            // kp 8
	rd_km_data(d);
	check(d == 32'h1000_8022, "kp 8 without the prefix is keycode 0x22");
	key(0, 0, 8'h75);
	rd_km_data(d);

	// shift modifier travels in the event
	key(1, 0, 8'h12);            // left shift down
	rd_km_data(d);
	check(d == 32'h1000_8200, "modifier event: left shift, keycode 0");
	key(1, 0, 8'h15);            // 'q' down with shift held
	rd_km_data(d);
	check(d == 32'h1000_8242, "shifted key: mod byte 0x02, keycode 0x42");
	key(0, 0, 8'h12);            // shift up
	rd_km_data(d);

	// Independent Control state, in both release orders.
	key(1, 0, 8'h14); rd_km_data(d);
	key(1, 1, 8'h14); rd_km_data(d);
	key(0, 0, 8'h14); rd_km_data(d);
	check(d == 32'h1000_8180, "left CTRL release preserves held right CTRL");
	key(1, 0, 8'h1C); rd_km_data(d);
	check(d == 32'h1000_8139, "following key retains held right CTRL");
	key(0, 1, 8'h14); rd_km_data(d);
	check(d == 32'h1000_8080, "last CTRL release clears control");
	key(1, 1, 8'h14); rd_km_data(d);
	key(1, 0, 8'h14); rd_km_data(d);
	key(0, 1, 8'h14); rd_km_data(d);
	check(d == 32'h1000_8180, "right CTRL release preserves held left CTRL");
	key(0, 0, 8'h14); rd_km_data(d);
	check(d == 32'h1000_8080, "both Control keys now released");

	// Caps Lock sends the new modifier immediately, without a keycode.
	key(1, 0, 8'h58);
	check(int_keymouse, "Caps Lock make posts an immediate modifier event");
	rd_km_data(d);
	check(d == 32'h1000_8200, "Caps Lock make carries newly enabled shift lock");
	key(1, 0, 8'h58); rd_km_data(d);
	check(d == 32'h1000_8200, "Caps Lock repeat does not toggle the lock");
	key(0, 0, 8'h58); rd_km_data(d);
	check(d == 32'h1000_8280, "Caps Lock release keeps shift lock active");
	key(1, 0, 8'h1C); rd_km_data(d);
	check(d == 32'h1000_8239, "shift lock is present on ordinary keys");
	key(1, 0, 8'h12); rd_km_data(d); // physical shift stays down
	key(1, 0, 8'h58); rd_km_data(d);
	check(d == 32'h1000_8200, "turning Caps Lock off preserves physical left Shift");
	key(0, 0, 8'h58); rd_km_data(d);
	key(0, 0, 8'h12); rd_km_data(d);
	check(d == 32'h1000_8080, "Shift clears after Caps Lock off and physical release");
	key(1, 0, 8'h58); rd_km_data(d); // lock on again
	key(0, 0, 8'h58); rd_km_data(d);
	key(1, 0, 8'h58);
	check(int_keymouse, "Caps Lock off posts an immediate modifier event");
	rd_km_data(d);
	check(d == 32'h1000_8000, "Caps Lock off event contains the new clear modifier");
	key(0, 0, 8'h58); rd_km_data(d);
	check(d == 32'h1000_8080, "Caps Lock release after lock off");

	// overrun: two events without a read
	key(1, 0, 8'h32);
	key(0, 0, 8'h32);
	kms_rd8(4'h1, v);
	check(v[5], "KBD_OVERRUN on the second unread event");
	kms_wr8(4'h1, 8'h20);        // clear the overrun group
	kms_rd8(4'h1, v);
	check(!v[7] && !v[6] && !v[5], "overrun write clears the group");

	// KMREG set-address answers with the probe response
	kms_cmd(8'hC5, 32'hEF020000);
	rd_km_data(d);
	check(d[30] && d[29] && d[28], "kms_response: no-response/invalid");
	check(d[27:24] == 4'h2, "response carries the new address");

	//----------------------------------------------------------------
	// Mouse: the KMS second device.  Reset the address to 0 so the
	// keyboard is device 0 and the mouse device 1.
	//----------------------------------------------------------------
	kms_cmd(8'hC5, 32'hEF000000);   // address 0
	rd_km_data(d);                   // consume the probe response

	// poll only the keyboard (device 0): a mouse packet is dropped
	kms_cmd(8'hC6, 32'h0FFFFFF2);
	mouse(9'd5, 9'd0, 1'b0, 1'b0);
	check(!int_keymouse, "mouse dropped while the mouse is not polled");

	// poll the mouse too (device 1 in the second nibble)
	kms_cmd(8'hC6, 32'h01FFFFF2);
	mouse(9'd5, 9'd0, 1'b0, 1'b0);   // move right 5
	check(int_keymouse, "INT_KEYMOUSE raised on mouse move");
	rd_km_data(d);
	check(d == 32'h010001F7,
	      "mouse right 5: device 1, x field 0x7B, both buttons up");
	check(!int_keymouse, "mouse interrupt released by the data read");

	mouse(-9'sd3, 9'd0, 1'b0, 1'b0); // move left 3
	rd_km_data(d);
	check(d == 32'h01000107, "mouse left 3: x field 0x03 (bare magnitude)");

	mouse(9'd0, 9'sd4, 1'b0, 1'b0);  // PS/2 dy +4 = up -> NeXT y field 4
	rd_km_data(d);
	check(d == 32'h01000901, "mouse up 4: y field 0x04");

	mouse(9'd0, 9'd0, 1'b1, 1'b0);   // left button down, no motion
	rd_km_data(d);
	check(d == 32'h01000100, "mouse left button down clears the left-up bit");

	mouse(9'd0, 9'd0, 1'b1, 1'b1);   // both buttons down
	rd_km_data(d);
	check(d == 32'h01000000, "mouse both buttons down clears both up bits");

	mouse(9'sd200, 9'd0, 1'b0, 1'b0); // large right delta clamps to 0x3F
	rd_km_data(d);
	check(d[7:1] == 7'h41,
	      "mouse right clamps to 0x3F: x field (0x40-0x3F)|0x40 = 0x41");

	// KMS reset clears modifier tracking, not the separate held power key.
	key(1, 1, 8'h14); rd_km_data(d);
	key(1, 0, 8'h58); rd_km_data(d);
	key(1, 0, 8'h09);
	kms_cmd(8'hFF, 32'hFFFFFFFF);
	@(negedge clk); // observe reset state after the write clock has settled
	check(int_power && !int_keymouse, "KMS reset leaves held power request asserted");
	check(dut.mods == 0 && dut.ctrl_down == 0 && !dut.capslock && !dut.caps_down,
	      "KMS reset clears all modifier tracking");
	key(0, 0, 8'h09);
	check(!int_power, "F10 release still works after KMS reset");
	key(1, 0, 8'h09);
	@(negedge clk); reset = 1;
	repeat (4) @(negedge clk);
	check(!int_power, "device reset clears a held power request");
	reset = 0;
	key(0, 0, 8'h09);
	check(!int_power, "release after device reset cannot reassert power");

	if (errors == 0) $display("ALL PASS");
	else             $display("%0d FAILURES", errors);
	$finish;
end

endmodule
