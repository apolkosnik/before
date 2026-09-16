`timescale 1ns/1ps
module tb_next_exception_trigger;
reg clk = 0, reset = 1, event_valid = 0;
always #5 clk = ~clk;
reg [511:0] event_data;
wire capture_valid;
wire [511:0] capture_data;
integer captures = 0;
reg [511:0] result;
next_exception_trigger dut (.*);
always @(posedge clk) begin
	if (reset) captures <= 0;
	else if (capture_valid) begin captures <= captures+1; result <= capture_data; end
end
function [511:0] packet(input [1:0] cls, input [1:0] reason,
                       input [31:0] root, frame, pc);
	packet = {16'd3,cls,reason,12'd0,16'd6,4'd0,4'd2,8'd9,256'd0,
		root,32'h03ffffbc,frame,16'h4e71,16'h8000,32'h10e65f20,pc};
endfunction
task send(input [1:0] cls, input [1:0] reason, input [31:0] root, frame, pc);
	@(negedge clk); event_valid = 1; event_data = packet(cls,reason,root,frame,pc);
	@(negedge clk); event_valid = 0;
endtask
task rearm;
	@(negedge clk); reset = 1; repeat (3) @(negedge clk); reset = 0;
endtask
task expect_result(input [3:0] status, input [31:0] pc);
	repeat (150) @(negedge clk);
	if (captures != 1 || result[483:480] != status || result[31:0] != pc)
		$fatal(1,"result captures=%d match=%d PC=%08x expected match=%d PC=%08x",
			captures,result[483:480],result[31:0],status,pc);
	if (capture_data !== result) $fatal(1,"complete output must remain held after capture pulse");
endtask
integer i;
initial begin
	rearm;
	// Same frame in another address space must not replace our candidate.
	send(0,0,'h4000000,'h1000,'h1234);
	send(0,0,'h5000000,'h1000,'h5678);
	send(1,0,'h5000000,'h1000,0);
	if (captures) $fatal(1,"raw trace or return published");
	send(2,1,'h4000000,'h1000,'h040573bc);
	expect_result(1,'h1234);
	send(2,1,'h5000000,'h1000,'h040573bc);
	expect_result(1,'h1234); // immutable after first delivery
	rearm;
	send(0,0,1,2,'h1234); send(1,0,1,2,0); send(2,1,1,2,'h040573bc);
	expect_result(2,0); // newest return tombstone beats older exception
	rearm;
	send(0,0,1,2,'h1234); send(1,0,1,2,0); send(0,0,1,2,'h5678);
	send(2,1,1,2,'h040573bc); expect_result(1,'h5678); // reuse, new candidate wins
	rearm;
	send(0,0,1,2,'h1234);
	for(i=0;i<64;i=i+1) send(1,0,3,4+i,0);
	send(2,1,1,2,'h040573bc); expect_result(3,0); // explicit history eviction
	rearm;
	send(0,0,1,2,'h1234);
	for(i=0;i<63;i=i+1) send(1,0,3,4+i,0);
	send(2,2,1,2,'h04056b94);
	// Events arriving during the sequential search must not alter history.
	send(0,0,1,2,'hdead); send(1,0,1,2,0);
	expect_result(1,'h1234); // oldest retained entry, frozen during search
	rearm;
	send(0,0,1,2,'h1234); send(2,1,1,3,'h040573bc); expect_result(3,0); // wrong frame
	rearm;
	send(0,0,1,2,'h1234); send(2,3,1,0,'h04007d6c); expect_result(4,0);
	rearm;
	send(2,1,1,2,'h040573bc); expect_result(3,0); // no uninitialized RAM match
	rearm;
	send(0,0,1,2,'h1234); send(2,1,1,2,'h040573bc); rearm;
	repeat(150) @(negedge clk);
	if(captures) $fatal(1,"reset did not cancel search");
	$display("ALL PASS"); $finish;
end
initial begin #100000; $fatal(1,"timeout"); end
endmodule
