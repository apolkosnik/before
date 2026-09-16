`timescale 1ns/1ps
// Execute real RTE/NOP/BRA/TRAP instructions through the TG68K adapter.
module tb_next_exception_cpu #(parameter DIAGNOSTIC = 1);
reg clk = 0, nreset = 0;
always #5 clk = ~clk;
wire [31:0] addr;
wire [15:0] data_out;
wire [1:0] busstate;
wire nuds, nlds, halted, event_valid;
wire [511:0] event_data;
reg mem_ready = 0;
reg [15:0] mem [0:32767];
integer delay_count = 0, wait_mode = 0, events = 0;
reg [511:0] record_data;
wire ce = busstate == 2'b01 || mem_ready;

ap040_tg68k_compat #(.AP040_HAS_FPU(0), .AP040_DEBUG_EXCEPTIONS(DIAGNOSTIC)) dut (
	.clk(clk), .nreset(nreset), .tick_in(1'b1), .clkena_in(ce),
	.cache_allow_all(1'b1), .cache_snoop_stb(1'b0), .cache_snoop_addr(32'd0),
	.cache_z2_ena(1'b0), .cache_z3_base0(5'd0), .cache_z3_ena0(1'b0),
	.cache_z3_base1(4'd0), .cache_z3_ena1(1'b0),
	.data_in(mem[addr[15:1]]), .ipl(3'b111), .ipl_autovector(1'b1), .berr(1'b0),
	.addr_out(addr), .data_write(data_out), .nuds(nuds), .nlds(nlds),
	.busstate(busstate), .walker_ack(1'b0), .walker_data(32'd0),
	.walker_berr(1'b0), .cache_data(16'd0), .cache_ack(1'b0),
	.debug_halted(halted), .debug_exception_valid(event_valid),
	.debug_exception(event_data)
);

always @(posedge clk) begin
	mem_ready <= 0;
	if (!nreset) begin
		delay_count <= 0;
		events <= 0;
	end else begin
		if (busstate != 2'b01 && !mem_ready) begin
			if (delay_count >= wait_mode) begin
				mem_ready <= 1;
				delay_count <= 0;
			end else delay_count <= delay_count + 1;
		end
		if (mem_ready && busstate == 2'b11) begin
			if (!nuds) mem[addr[15:1]][15:8] <= data_out[15:8];
			if (!nlds) mem[addr[15:1]][7:0] <= data_out[7:0];
		end
		if (event_valid) begin
			events <= events + 1;
			record_data <= event_data;
			$display("event vec=%0d PC=%08x address=%08x SR=%04x IR=%04x",
				event_data[455:448], event_data[31:0], event_data[63:32],
				event_data[79:64], event_data[95:80]);
		end
	end
end

task check(input bit condition, input string name);
	if (!condition) $fatal(1, "%s", name);
endtask

integer i, ph, wm;
reg [15:0] restored_sr, opcode;
reg [7:0] vector_num;
reg [31:0] stacked_pc;
initial begin
	for (wm = 0; wm < 2; wm = wm + 1) begin
		wait_mode = wm * 5;
		for (ph = 0; ph < 5; ph = ph + 1) begin
			@(negedge clk); nreset = 0;
			repeat (8) @(negedge clk);
			for (i = 0; i < 32768; i = i + 1) mem[i] = 0;
			mem[0] = 0; mem[1] = 16'h1000; // ISP
			mem[2] = 0; mem[3] = 16'h0200; // reset PC
			mem[9*2+1] = 16'h0600;
			mem[47*2+1] = 16'h0600;
			mem[36*2+1] = 16'h0600;
			// LEA $3000,A0; MOVE A0,USP; MOVEQ #17/34/51,D0/D1/D2; RTE
			mem['h100] = 16'h41f9; mem['h101] = 0; mem['h102] = 16'h3000;
			mem['h103] = 16'h4e60; mem['h104] = 16'h7011;
			mem['h105] = 16'h7222; mem['h106] = 16'h7433;
			mem['h107] = 16'h4e73;
			restored_sr = ph == 0 ? 16'h8000 : ph == 2 ? 16'ha000 :
				ph == 4 ? 16'h4000 : 16'h0000;
			opcode = ph == 1 ? 16'h4e4f : ph == 3 ? 16'h4e44 :
				ph == 4 ? 16'h6002 : 16'h4e71;
			vector_num = ph == 1 ? 8'd47 : 8'd9;
			stacked_pc = ph == 4 ? 32'h304 : 32'h302;
			mem['h800] = restored_sr;
			mem['h801] = 0; mem['h802] = 16'h0300; mem['h803] = 0;
			mem['h180] = opcode; mem['h181] = 16'h4e71;
			mem['h182] = 16'h4e71; mem['h183] = 16'h60fe;
			mem['h300] = 16'h60fe; // handler loops in supervisor mode
			nreset = 1;
			repeat (5000) @(negedge clk);
			check(!halted, "CPU unexpectedly halted");
			if (!DIAGNOSTIC) begin
				check(events == 0 && event_data == 0, "disabled diagnostic outputs must be zero");
			end
			else if (ph == 2 || ph == 3) check(events == 0, "supervisor trace/syscall must not trigger");
			else begin
				check(events == 1, "expected exactly one qualified exception event");
				check(record_data[455:448] == vector_num, "vector");
				check(record_data[31:0] == stacked_pc, "stacked PC");
				check(record_data[79:64] == restored_sr, "pre-entry SR, not supervisor SR");
				check(record_data[95:80] == opcode, "opcode");
				if (ph != 1) check(record_data[63:32] == 32'h300, "trace source address");
				check(record_data[159:128] == 32'h3000, "user stack pointer");
				check(record_data[127:96] == 32'h300, "last RTE target PC");
				check(record_data[447:192] == 0, "reserved payload words");
				check(record_data[479:464] == restored_sr && record_data[480], "last RTE SR/valid");
				check(record_data[511:496] == 2, "record version");
			end
			$display("PASS phase=%0d waits=%0d", ph, wait_mode);
		end
	end
	$display("ALL PASS"); $finish;
end
endmodule
