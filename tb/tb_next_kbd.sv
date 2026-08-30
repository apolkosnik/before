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

reg         sel_kms = 0;
reg   [3:0] addr = 0;
reg         we = 0;
reg   [1:0] be = 0;
reg  [15:0] wdata = 0;
wire [15:0] rdata;
wire        int_keymouse;

next_kms_snd #(.CLK_HZ(1000000)) dut
(
	.clk(clk), .reset(reset),
	.ps2_key(ps2),
	.sel_kms(sel_kms),
	.sel_csr(1'b0), .sel_sptr(1'b0), .sel_ptr(1'b0), .sel_ini(1'b0),
	.addr(addr), .we(we), .be(be), .wdata(wdata), .rdata(rdata),
	.m_req(), .m_we(), .m_addr(), .m_be(), .m_din(),
	.m_dout(32'd0), .m_ack(1'b0),
	.int_snd_ovrun(), .int_snd_out_dma(),
	.int_keymouse(int_keymouse)
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

	// shift modifier travels in the event
	key(1, 0, 8'h12);            // left shift down
	rd_km_data(d);
	check(d == 32'h1000_8200, "modifier event: left shift, keycode 0");
	key(1, 0, 8'h15);            // 'q' down with shift held
	rd_km_data(d);
	check(d == 32'h1000_8242, "shifted key: mod byte 0x02, keycode 0x42");
	key(0, 0, 8'h12);            // shift up
	rd_km_data(d);

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

	if (errors == 0) $display("ALL PASS");
	else             $display("%0d FAILURES", errors);
	$finish;
end

endmodule
