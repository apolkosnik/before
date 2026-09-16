`timescale 1ns/1ps
// Real CPU instructions exercise benign AST -> RTE -> later fatal trace,
// TRAP #15 delivery, opcode/privilege guards, and SIGTRAP exit fallback.
module tb_next_unhandled_cpu #(parameter DIAGNOSTIC = 2);
reg clk = 0, nreset = 0;
always #5 clk = ~clk;
wire [31:0] addr;
wire [15:0] data_out;
wire [1:0] busstate;
wire nuds, nlds, halted, event_valid, capture_valid;
wire [511:0] event_data, capture_data;
reg mem_ready = 0;
reg [15:0] mem [0:32767];
integer delay_count = 0, wait_mode = 0, captures = 0, candidates = 0, returns = 0;
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
	.debug_halted(halted), .debug_exception_valid(event_valid), .debug_exception(event_data)
);
next_exception_trigger trigger (
	.clk(clk), .reset(!nreset), .event_valid(event_valid), .event_data(event_data),
	.capture_valid(capture_valid), .capture_data(capture_data)
);
wire mb_req, mb_we;
wire [28:0] mb_addr;
wire [63:0] mb_data;
reg mb_ack = 0;
reg [63:0] mailbox [0:9];
next_exception_mailbox #(.INPUT_HELD(1)) publisher (
	.clk(clk), .reset(!nreset), .event_valid(capture_valid), .event_data(capture_data),
	.b_req(1'b0), .b_we(1'b0), .b_addr(29'd0), .b_wdata(64'd0),
	.m_req(mb_req), .m_we(mb_we), .m_addr(mb_addr), .m_wdata(mb_data),
	.m_rdata(64'd0), .m_ack(mb_ack)
);
always @(posedge clk) begin
	mb_ack <= nreset && mb_req;
	if (nreset && mb_req && !mb_ack) begin
		if (!mb_we || mb_addr < 29'h03fe0a00 || mb_addr >= 29'h03fe0a0a)
			$fatal(1,"unexpected publisher transaction");
		mailbox[mb_addr-29'h03fe0a00] <= mb_data;
	end
end
always @(posedge clk) begin
	mem_ready <= 0;
	if (!nreset) begin
		delay_count <= 0; captures <= 0; candidates <= 0; returns <= 0;
	end else begin
		if (busstate != 2'b01 && !mem_ready) begin
			if (delay_count >= wait_mode) begin mem_ready <= 1; delay_count <= 0; end
			else delay_count <= delay_count + 1;
		end
		if (mem_ready && busstate == 2'b11) begin
			if (!nuds) mem[addr[15:1]][15:8] <= data_out[15:8];
			if (!nlds) mem[addr[15:1]][7:0] <= data_out[7:0];
		end
		if (event_valid) begin
			$display("raw class=%d reason=%d frame=%08x pc=%08x", event_data[495:494],
				event_data[493:492], event_data[127:96], event_data[31:0]);
			if (event_data[495:494] == 0) candidates <= candidates + 1;
			if (event_data[495:494] == 1) begin
				returns <= returns + 1;
				if (returns == 0 && event_data[127:96] != 32'h1000)
					$fatal(1, "RTE key must be pre-pop frame address");
			end
		end
		if (capture_valid) begin captures <= captures + 1; record_data <= capture_data; end
	end
end
task check(input bit condition, input string name);
	if (!condition) $fatal(1, "%s", name);
endtask
task word_at(input integer address, input [15:0] value);
	mem[(address & 'hffff)/2] = value;
endtask
task jmp_at(input integer address, input [31:0] target);
	word_at(address, 'h4ef9); word_at(address+2, target[31:16]); word_at(address+4,target[15:0]);
endtask
integer i, ph, wm;
initial begin
	for (wm = 0; wm < 2; wm = wm + 1) begin
		wait_mode = wm * 5;
		for (ph = 0; ph < 7; ph = ph + 1) begin
			@(negedge clk); nreset = 0; repeat (8) @(negedge clk);
			for (i = 0; i < 32768; i = i + 1) mem[i] = 0;
			word_at(2, 'h1000); word_at(6, 'h200);
			word_at(9*4+2, 'h600); word_at(47*4+2,'h600); word_at(36*4+2,'h800);
			// LEA $3000,A0; MOVE A0,USP; RTE.
			word_at('h200,'h41f9); word_at('h204,'h3000); word_at('h206,'h4e60); word_at('h208,'h4e73);
			word_at('h1000, (ph == 0 || ph == 2 || ph == 6) ? 'h8000 : 0);
			word_at('h1004,'h300);
			// Benign trace, then request tracing through a syscall, then NOP.
			word_at('h300,'h4e71);
			word_at('h302,'h23fc); word_at('h306,6); word_at('h30a,'h2000);
			word_at('h30c,'h4e44); word_at('h30e,'h4e71); word_at('h310,'h60fe);
			word_at('h2002,4);
			// Trace handler: LEA -$44(SP),A4; MOVE.L $2000,D2; JMP decision.
			word_at('h600,'h49ef); word_at('h602,'hffbc);
			word_at('h604,'h2439); word_at('h608,'h2000); jmp_at('h60a,32'h040573b6);
			// Exact kernel decision: BTST #1,D2; BEQ epilogue; CLR.L -(SP).
			word_at('h73b6,'h0802); word_at('h73b8,1); word_at('h73ba,'h6714);
			word_at('h73bc,'h42a7); word_at('h73be,'h60fe);
			// Stub epilogue clears T and returns the actual CPU trace frame.
			word_at('h73d0,'h0257); word_at('h73d2,'h7fff); word_at('h73d4,'h4e73);
			word_at('h800,'h0057); word_at('h802,'h8000); word_at('h804,'h4e73);
			if (ph == 1) begin
				word_at('h300,'h4e4f);
				word_at('h604,'h7406); jmp_at('h606,32'h04056b94);
				word_at('h6b94,'h61ff); word_at('h6b96,0); word_at('h6b98,6); word_at('h6b9c,'h60fe);
			end
			if (ph == 2) begin
				jmp_at('h60a,32'h040573bc); word_at('h73bc,'h4e71); // wrong opcode
			end
			if (ph == 3) begin
				word_at('h1002,'h0405); word_at('h1004,'h73bc); // user-mode same PC/opcode
			end
			if (ph == 4 || ph == 5) begin
				word_at('h200, ph == 4 ? 'h7405 : 'h7406);
				jmp_at('h202,32'h04007d6c);
				word_at('h7d6c,'h2f02); word_at('h7d6e,'h60fe);
			end
			if (ph == 6) begin
				// Real kernel frame compaction: old frame FFC -> format-0
				// frame 1000. A subsequent delivery using the OLD key must
				// see its tombstone, not the earlier trace candidate.
				word_at('h302,'h4e44); word_at('h304,'h60fe);
				word_at('h600,'h0257); word_at('h602,'h7fff);
				word_at('h604,'h9efc); word_at('h606,'h44); jmp_at('h608,32'h04002126);
				word_at('hffa,4); // saved-state stack adjustment
				word_at('h2126,'h204f); word_at('h2128,'hd0ef); word_at('h212a,'h42);
				word_at('h212c,'h216f); word_at('h212e,'h46); word_at('h2130,'h46);
				word_at('h2132,'h316f); word_at('h2134,'h44); word_at('h2136,'h44);
				word_at('h2138,'h4268); word_at('h213a,'h4a);
				word_at('h213c,'h2f48); word_at('h213e,'h3c);
				word_at('h2140,'h4cd7); word_at('h2142,'hffff);
				word_at('h2144,'hdefc); word_at('h2146,'h44); word_at('h2148,'h4e73);
				word_at('h800,'h49f9); word_at('h802,0); word_at('h804,'hfb8);
				jmp_at('h806,32'h040573bc);
			end
			nreset = 1;
			repeat (15000) @(negedge clk);
			check(!halted,"unexpected halt");
			if (!DIAGNOSTIC || ph == 2 || ph == 3 || ph == 5)
				check(captures == 0,"ordinary/wrong opcode/user PC/wrong signal must not trigger");
			else begin
				check(captures == 1,"exactly one delivery record");
				check(mailbox[0] == 64'h4e58544449414731 && mailbox[1] == 1,"mailbox publication completed");
				for (i = 0; i < 8; i = i+1)
					check(mailbox[i+2] == record_data[i*64 +: 64],"held-input payload matches captured record");
				check(record_data[511:496] == 3 && record_data[495:494] == 3,"final version/class");
				if (ph == 6) begin
					check(record_data[483:480] == 2 && record_data[31:0] == 0,
					      "compacted frame retires original key before RTE");
				end else if (ph == 4) begin
					check(record_data[493:492] == 3 && record_data[483:480] == 4,"signal fallback explicit");
					check(record_data[31:0] == 0,"fallback must not invent user PC");
				end else begin
					check(record_data[483:480] == 1,"matched outstanding frame");
					check(record_data[31:0] == (ph == 0 ? 'h310 : 'h302),"correct later user PC");
					check(record_data[127:96] == (ph == 0 ? 'hffc : 'h1000),"frame key");
					check(record_data[455:448] == (ph == 0 ? 9 : 47),"original vector");
					check(record_data[223:192] == (ph == 0 ? 'h040573bc : 'h04056b94),"kernel trigger PC");
					if (ph == 0) begin
						check(candidates == 2 && returns == 3,"benign trace consumed and retired before fatal trace");
						check(record_data[479:464] == 6,"AST plus preexisting trace flags retained");
						check(record_data[79:64] == 'h8000 && record_data[95:80] == 'h4e71,"original SR/opcode");
					end
				end
				$display("DELIVERY_PACKET=%0128h", record_data);
			end
			$display("PASS unhandled phase=%0d waits=%0d diagnostic=%0d",ph,wait_mode,DIAGNOSTIC);
		end
	end
	$display("ALL PASS"); $finish;
end
endmodule
