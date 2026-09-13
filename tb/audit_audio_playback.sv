// Diagnostic audit probes; FAIL means required behavior is missing in the RTL.
// Kept separate from run_tests.sh so all independent findings are printed.
`timescale 1ns/1ps
module audit_audio_playback;
reg clk=0, reset=1;
always #5 clk=~clk;
reg sel_kms=0, sel_csr=0, sel_ptr=0, we=0;
reg [3:0] addr=0;
reg [1:0] be=0;
reg [15:0] wdata=0;
wire [15:0] rdata;
wire req, active_in, irq;
wire [29:0] mem_addr;
reg address_pattern=0;
reg memory_error=0;
wire [31:0] mem_data=address_pattern ? {16'h1000+{8'd0,mem_addr[7:0]},16'h2000+{8'd0,mem_addr[7:0]}} : 32'h1234abcd;
reg auto_ack=0, manual_ack=0;
reg ack=0;
wire signed [15:0] left, right;
integer failures=0;
reg collect_tail=0,wrote_sample=0;
integer tail_frames=0;
always @(posedge clk)wrote_sample<=dut.output_processing.valid[2] && !dut.output_processing.reset;
always @(negedge clk)if(collect_tail && wrote_sample && left!=0)begin
 if(left!=16'h1000+tail_frames || right!=16'h2000+tail_frames)
  $fatal(1,"short sound lost/reordered an accepted stereo frame");
 tail_frames++;
end
next_kms_snd #(.CLK_HZ(1000000),.CLK_REAL_HZ(1000000)) dut(
 .clk(clk),.reset(reset),.ps2_key(11'd0),.ps2_mouse(25'd0),
 .sel_kms(sel_kms),.sel_csr(sel_csr),.sel_ptr(sel_ptr),.sel_sptr(1'b0),.sel_ini(1'b0),
 .addr(addr),.we(we),.be(be),.wdata(wdata),.rdata(rdata),
 .m_req(req),.m_we(),.m_addr(mem_addr),.m_be(),.m_din(),.m_dout(mem_data),
 .m_ack(ack|manual_ack),.m_err(memory_error),.int_snd_ovrun(),.int_snd_out_dma(irq),.int_keymouse(),
 .sndin_active(active_in),.sndin_clear(),.sndin_request(1'b0),.sndin_overrun(1'b0),
 .audio_l(left),.audio_r(right));
always @(posedge clk) begin
 if(reset || !auto_ack || !req || ack) ack<=0;
 else ack<=1;
end
task check(input bit ok, input string message);
 if(ok) $display("PASS: %s",message);
 else begin failures++; $display("FAIL: %s",message); end
endtask
task wr(input integer which,input [3:0] a,input [1:0] lanes,input [15:0] value);
 @(negedge clk); sel_kms=(which==0);sel_csr=(which==1);sel_ptr=(which==2);
 addr=a;be=lanes;wdata=value;we=1;
 @(negedge clk);sel_kms=0;sel_csr=0;sel_ptr=0;we=0;
endtask
task bytewr(input [3:0] a,input [7:0] value);
 wr(0,a,a[0]?2'b01:2'b10,a[0]?{8'd0,value}:{value,8'd0});
endtask
task cmd(input [7:0] value,input [31:0] data);
 bytewr(3,value);wr(0,4,3,data[31:16]);wr(0,6,3,data[15:0]);
endtask
task ptr(input [3:0] a,input [31:0] value);
 wr(2,a,3,value[31:16]);wr(2,a+2,3,value[15:0]);
endtask
task csr(input [7:0] value);wr(1,0,3,{8'd0,value});endtask
task init;
 @(negedge clk);reset=1;auto_ack=0;manual_ack=0;address_pattern=0;memory_error=0;
 repeat(5)@(negedge clk);reset=0;
 bytewr(2,2); // KMS_ENABLE
endtask
task start(input [7:0] mode,input [31:0] limit_addr);
 ptr(0,32'h04002000);ptr(4,limit_addr);csr(8'h11);bytewr(0,8'h80);cmd(mode,0);
endtask
task pulse_ack;
 @(negedge clk);manual_ack=1;@(negedge clk);manual_ack=0;
endtask
task output_sample;
 do @(posedge clk);while(!dut.sample_tick);
 repeat(4)@(negedge clk); // arithmetic pipeline latency
endtask
integer normal_count,repeat_count,zero_count,count;
integer i,j,k,expected_index;
reg [10:0] attenuation;
initial begin
 // Source consumption, measured after identical warmup in each output mode.
 for(j=0;j<3;j++)begin
  init();auto_ack=1;start(j==0?8'h0f:j==1?8'h1f:8'h3f,32'h04008000);
  repeat(1000)@(negedge clk);count=0;
  repeat(10000)begin @(posedge clk);if(dut.af_pop)count++;end
  @(negedge clk);
  case(j)0:normal_count=count;1:repeat_count=count;2:zero_count=count;endcase
 end
 $display("MEASURE: source frames / 10 ms: normal=%0d repeat=%0d zero=%0d",normal_count,repeat_count,zero_count);
 check(normal_count==441,"44.1 kHz stereo source rate");
 check(repeat_count>=220 && repeat_count<=221,"22.05 kHz repeat mode consumes half as many source frames");
 check(zero_count>=220 && zero_count<=221,"22.05 kHz zero-fill mode consumes half as many source frames");

 // Distinct frames verify repeat versus zero insertion, including the final
 // half-sample after DMA completion has disabled the channel.
 for(j=0;j<3;j++)begin
  init();auto_ack=1;address_pattern=1;
  start(j==0?8'h0f:j==1?8'h1f:8'h3f,32'h04002010);
  do output_sample();while(left!=16'h1000);
  for(k=0;k<(j==0?4:8);k++)begin
   if(k!=0)output_sample();
   expected_index=j==0?k:k/2;
   if(j==2 && k%2==1)check(left==0 && right==0,"zero-fill output slot");
   else check(left==16'h1000+expected_index && right==16'h2000+expected_index,"ordered stereo output frame");
  end
  output_sample();check(left==0 && right==0,"silence after final output slot");
 end

 // DMA completion can precede audible output: stop/reset must drain the tail.
 init();auto_ack=1;address_pattern=1;tail_frames=0;collect_tail=1;
 start(8'h0f,32'h04002010);wait(irq);
 cmd(8'h07,0);csr(8'h10);repeat(400)@(negedge clk);
 collect_tail=0;
 check(tail_frames==4 && left==0 && right==0,"ordinary stop and DMA reset preserve a short sound's final queued frames");

 // Legacy serial volume interface: 111 prefix, both channels, attenuation 43 (mute).
 init();auto_ack=1;start(8'h0f,32'h04008000);repeat(1000)@(negedge clk);
 attenuation={3'b111,2'b11,6'd43};
 cmd(8'hc4,32'h01000000);cmd(8'hc4,0);
 for(i=10;i>=0;i--)begin
  cmd(8'hc4,attenuation[i]?32'h02000000:0);
  cmd(8'hc4,attenuation[i]?32'h06000000:32'h04000000);
 end
 cmd(8'hc4,32'h01000000);repeat(1000)@(negedge clk);
 $display("MEASURE: after maximum attenuation L=%04x R=%04x",left,right);
 check(left==0 && right==0,"guest serial volume command mutes both channels");
 cmd(8'hc2,32'hc0000000);repeat(100)@(negedge clk);
 check(left==16'h1234 && right==16'habcd,"direct volume command restores both channels");
 cmd(8'hc2,32'h43000000);repeat(100)@(negedge clk);
 check(left>=2335 && left<=2337 && right==16'habcd,"direct volume independently attenuates the left channel by 6 dB");
 cmd(8'hc4,32'h10000000);repeat(100)@(negedge clk);
 check(left==0 && right==0,"GPO mute reaches the output processing pipeline");
 cmd(8'hc4,0);cmd(8'hc2,32'hc0000000);repeat(100)@(negedge clk);
 check(left==16'h1234 && right==16'habcd,"clearing GPO mute restores output");

 // CPU can reset/reprogram while a DMA request waits for the RAM arbiter.
 init();start(8'h0f,32'h04002004);wait(req);csr(8'h10);
 check(!irq,"RESET initially clears completion");
 pulse_ack();
 $display("MEASURE: delayed response after RESET: CSR=%02x next=%08x FIFO=%0d",dut.s_csr,dut.s_next,dut.af_cnt);
 check(!irq && dut.s_next==32'h04002000 && dut.af_cnt==0,"aborted DMA response cannot restore completion, advance next, or enqueue audio");
 init();start(8'h0f,32'h04002004);wait(req);csr(8'h10);
 ptr(0,32'h04003000);ptr(4,32'h04003100);pulse_ack();
 $display("MEASURE: reprogrammed next after old response=%08x",dut.s_next);
 check(dut.s_next==32'h04003000,"old DMA response cannot advance a newly programmed descriptor");

 // Reset and completion/error on the SAME edge must have reset priority.
 for(j=0;j<2;j++)begin
  init();start(8'h0f,32'h04002004);wait(req);
  @(negedge clk);sel_csr=1;addr=0;we=1;be=3;wdata=16'h0010;
  manual_ack=(j==0);memory_error=(j==1);
  @(negedge clk);sel_csr=0;we=0;manual_ack=0;memory_error=0;
  check(dut.s_csr==0 && dut.s_next==32'h04002000 && !req && dut.af_cnt==0,"RESET wins over simultaneous ACK or error");
 end

 // Valid KMS reset command must stop recording and playback.
 init();auto_ack=1;start(8'h0f,32'h04008000);cmd(8'h0b,0);
 cmd(8'hff,32'hffffffff);repeat(4)@(negedge clk);
 $display("MEASURE: after KMS RESET input_active=%0d output_active=%0d tx=%02x",active_in,dut.sndout_active,dut.st_tx);
 check(!active_in && !dut.sndout_active && dut.st_tx==0,"KMS reset stops both audio directions and resets interface status");
 init();start(8'h0f,32'h04002004);wait(req);
 cmd(8'hff,32'hffffff00);
 check(dut.sndout_active,"invalid KMS reset data is ignored");
 cmd(8'hff,32'hffffffff);
 check(req && dut.s_csr[0] && dut.s_next==32'h04002000,"KMS reset preserves DMA registers and holds the pending request");
 pulse_ack();check(!irq && dut.af_cnt==0 && dut.s_next==32'h04002000,"KMS reset discards the pending output response");
 bytewr(2,2);auto_ack=1;cmd(8'h0f,0);repeat(100)@(negedge clk);
 check(irq && dut.s_next==32'h04002004,"playback restarts from its preserved descriptor");
 $display("AUDIT: %0d required-behavior checks failed",failures);
 $finish;
end
initial begin #10000000;$fatal(1,"audit watchdog");end
endmodule
