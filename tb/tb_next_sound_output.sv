`timescale 1ns/1ps
module tb_next_sound_output;
reg clk=0,reset=1,strobe=0,mute=0,deemphasis=0;
always #5 clk=~clk;
reg [31:0] frame=0;
reg [5:0] att_l=0,att_r=0;
wire signed [15:0] left,right;
next_sound_output dut(.clk(clk),.reset(reset),.sample_strobe(strobe),.frame(frame),
 .mute(mute),.deemphasis(deemphasis),.attenuation_l(att_l),.attenuation_r(att_r),
 .audio_l(left),.audio_r(right));
task sample(input integer l,input integer r);
 @(negedge clk);frame={16'(l),16'(r)};strobe=1;
 @(negedge clk);strobe=0;repeat(4)@(negedge clk);
endtask
function automatic integer rounded(input real value);
 integer n;
 begin
  n=$rtoi(value+(value<0?-0.5:0.5));
  return n>32767?32767:n< -32768?-32768:n;
 end
endfunction
task check(input integer l,input integer r,input integer tolerance,input string message);
 if(left<l-tolerance || left>l+tolerance || right<r-tolerance || right>r+tolerance)
  $fatal(1,"%s: expected %0d/%0d got %0d/%0d",message,l,r,left,right);
endtask
integer a,n,l,r;
real gain,li=0,ri=0,lo=0,ro=0,y_l,y_r;
initial begin
 repeat(5)@(negedge clk);reset=0;
 sample(-32768,32767);check(-32768,32767,0,"unity signed endpoints");
 for(a=0;a<44;a++)begin
  att_l=6'(a);att_r=6'(43-a);
  for(n=0;n<50;n++)begin
   l=$signed(16'($urandom));r=$signed(16'($urandom));
   sample(l,r);
   check(rounded(l*(a==43?0.0:10.0**(-a/10.0))),
         rounded(r*(a==0?0.0:10.0**(-(43-a)/10.0))),1,"independent 2 dB channel attenuation");
  end
 end
 $display("PASS: all attenuation settings match floating-point reference with <=1-code error");
 att_l=0;att_r=0;mute=1;sample(-32768,32767);check(0,0,0,"mute");mute=0;
 @(negedge clk);reset=1;repeat(4)@(negedge clk);reset=0;deemphasis=1;
 for(n=0;n<1024;n++)begin
  l=n==0?32767:0;
  r=$rtoi(16000.0*$sin(6.283185307179586*10000.0*n/44100.0));
  y_l=l*0.45995451989513153-li*0.08782333709141937+lo*0.6278688171962878;
  y_r=r*0.45995451989513153-ri*0.08782333709141937+ro*0.6278688171962878;
  sample(l,r);check(rounded(y_l),rounded(y_r),2,"de-emphasis impulse and 10 kHz tone");
  li=l;ri=r;lo=y_l;ro=y_r;
 end
 $display("PASS: de-emphasis matches Previous coefficients with <=2-code error");
 // Flush while a sample is in the arithmetic pipeline.
 @(negedge clk);frame=32'h7fff8000;strobe=1;
 @(negedge clk);strobe=0;reset=1;
 repeat(4)@(negedge clk);reset=0;repeat(8)@(negedge clk);
 check(0,0,0,"reset flushes pending arithmetic");
 $display("ALL PASS");$finish;
end
endmodule
