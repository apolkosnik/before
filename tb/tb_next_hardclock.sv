//============================================================================
//  NeXT hardclock and interrupt controller test
//
//  Drives the real next_timer and next_intc through their register
//  interfaces exactly as the CPU bus would:
//    - program a 100 microsecond period, enable, expect INT_TIMER
//      (bit 29) after 100 us +/- 1 us and IPL 6 once unmasked
//    - CSR read releases the interrupt
//    - the timer refires periodically
//    - SCR2 TIMERIPL7 promotes INT_TIMER to IPL 7
//============================================================================

`timescale 1ns/1ps

module tb_next_hardclock;

reg clk = 0;
always #5 clk = ~clk;   // 100 MHz

reg reset = 1;

// timer
reg         t_sel = 0;
reg   [2:0] t_addr = 0;
reg         t_we = 0;
reg   [1:0] t_be = 0;
reg  [15:0] t_wdata = 0;
wire [15:0] t_rdata;
reg         t_rd = 0;
wire        timer_set, timer_clr;

next_timer #(.CLK_HZ(100000000)) timer
(
	.clk(clk), .reset(reset),
	.sel(t_sel), .addr(t_addr), .we(t_we), .be(t_be),
	.wdata(t_wdata), .rdata(t_rdata), .rd(t_rd),
	.int_set(timer_set), .int_clr(timer_clr)
);

// intc
reg         i_sel = 0;
reg         i_mask = 0;
reg         i_addr1 = 0;
reg         i_we = 0;
reg   [1:0] i_be = 0;
reg  [15:0] i_wdata = 0;
wire [15:0] i_rdata;
wire  [2:0] ipl;
reg         timer_ipl7 = 0;

wire [31:0] int_set = {2'b00, timer_set, 29'd0};
wire [31:0] int_clr = {2'b00, timer_clr, 29'd0};

next_intc intc
(
	.clk(clk), .reset(reset),
	.int_set(int_set), .int_clr(int_clr),
	.timer_ipl7(timer_ipl7),
	.sel(i_sel), .reg_mask(i_mask), .addr1(i_addr1),
	.we(i_we), .be(i_be), .wdata(i_wdata), .rdata(i_rdata),
	.ipl(ipl)
);

task timer_write;
	input [2:0] addr;
	input [1:0] be;
	input [15:0] data;
	begin
		@(posedge clk);
		t_sel <= 1; t_addr <= addr; t_we <= 1; t_be <= be; t_wdata <= data;
		@(posedge clk);
		t_sel <= 0; t_we <= 0;
	end
endtask

task timer_read_csr;
	begin
		@(posedge clk);
		t_sel <= 1; t_addr <= 3'd4; t_we <= 0; t_rd <= 1;
		@(posedge clk);
		t_sel <= 0; t_rd <= 0;
	end
endtask

task intc_write_mask;
	input [31:0] mask;
	begin
		@(posedge clk);
		i_sel <= 1; i_mask <= 1; i_addr1 <= 0; i_we <= 1; i_be <= 2'b11;
		i_wdata <= mask[31:16];
		@(posedge clk);
		i_addr1 <= 1; i_wdata <= mask[15:0];
		@(posedge clk);
		i_sel <= 0; i_we <= 0;
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

integer t_us = 0;
integer set_time1 = -1, set_time2 = -1;
always @(posedge clk) begin
	t_us = t_us + 1;
	if (timer_set) begin
		if (set_time1 < 0) set_time1 = t_us;
		else if (set_time2 < 0) set_time2 = t_us;
	end
end

initial begin
	if ($test$plusargs("dump")) begin
		$dumpfile("build/tb_next_hardclock.vcd");
		$dumpvars(0, tb_next_hardclock);
	end

	repeat (10) @(posedge clk);
	reset = 0;
	repeat (10) @(posedge clk);

	// unmask INT_TIMER
	intc_write_mask(32'h20000000);
	repeat (2) @(posedge clk);
	check(intc.mask == 32'hA0027640,
	      "non-turbo interrupt mask forces the Previous non-maskable sources");

	// latch = 100 us: bytes 0 (high) and 1 (low) then CSR latch+enable
	timer_write(3'd0, 2'b11, {8'h00, 8'd100});
	timer_write(3'd4, 2'b10, {8'hC0, 8'h00});   // enable | latch

	// wait 150 us: first fire lands near 100 us, mid-period afterwards
	repeat (15000) @(posedge clk);

	check(set_time1 > 0, "timer fired");
	if (set_time1 > 0)
		check(set_time1 >= 9800 && set_time1 <= 10300,
		      "first fire close to 100 us");
	check(ipl == 3'd6, "INT_TIMER at IPL 6");

	// CSR read releases the interrupt
	timer_read_csr;
	repeat (4) @(posedge clk);
	check(ipl == 3'd0, "CSR read released INT_TIMER");

	// wait for the refire
	repeat (20000) @(posedge clk);
	check(set_time2 > set_time1, "timer refired");
	if (set_time2 > set_time1)
		check((set_time2 - set_time1) >= 9900 && (set_time2 - set_time1) <= 10100,
		      "refire period close to 100 us");

	// TIMERIPL7
	timer_ipl7 = 1;
	repeat (4) @(posedge clk);
	check(ipl == 3'd7, "TIMERIPL7 promotes INT_TIMER to IPL 7");

	if (errors == 0) $display("ALL PASS");
	else             $display("%0d FAILURES", errors);
	$finish;
end

endmodule
