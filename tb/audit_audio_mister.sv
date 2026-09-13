// Exercise the actual MiSTer audio processing and I2S pins with asynchronous
// 44.1 kHz stereo input. Also expose SPDIF channel-status rate selection.
`timescale 1ns/1ps
module audit_audio_mister;
reg clk=0,core_clk=0,reset=1,rate96=0;
always #5 clk=~clk;
always #4.388571 core_clk=~core_clk; // 28 MHz relative to the audio clock's 24.576 MHz
reg [15:0] core_l=0,core_r=0;
integer acc=0,source_samples=0;
always @(posedge core_clk)begin
 if(reset)begin acc=0;source_samples=0;core_l<=0;core_r<=0;end
 else begin
  acc+=44100;
  if(acc>=28000000)begin
   acc-=28000000;
   core_l<=16'($rtoi(4096.0*$sin(6.283185307179586*1000.0*source_samples/44100.0)));
   core_r<=16'($rtoi(2048.0*$sin(6.283185307179586*2000.0*source_samples/44100.0)));
   source_samples++;
  end
 end
end
wire bclk,lrclk,data,spdif,dac_l,dac_r;
audio_out dut(.clk(clk),.reset(reset),.sample_rate(rate96),
 .flt_rate(32'd7056000),.cx(40'd4258969),.cx0(8'd3),.cx1(8'd3),.cx2(8'd1),
 .cy0(-24'd6216759),.cy1(24'd6143386),.cy2(-24'd2023767),
 .att(5'd0),.boost(2'd0),.mix(2'd0),.is_signed(1'b1),.core_l(core_l),.core_r(core_r),
 .alsa_l(16'd0),.alsa_r(16'd0),.i2s_bclk(bclk),.i2s_lrclk(lrclk),.i2s_data(data),
 .spdif(spdif),.dac_l(dac_l),.dac_r(dac_r));
reg previous_lr=1,checking=0;
reg [15:0] serial_word=0,expected_l=0,expected_r=0;
reg [15:0] before_l=0,before_r=0,pending_l=0,pending_r=0;
always @(negedge clk)begin before_l=dut.al;before_r=dut.ar;end
// The falling word-select edge latches both source channels. Preserve the
// pre-edge values, including when the audio mixer also changes that cycle.
always @(negedge lrclk)begin pending_l=before_l;pending_r=before_r;end
integer frames=0,serial_checks=0;
always @(posedge bclk)begin
 if(reset)begin previous_lr=1;serial_word=0;checking=0;end
 else begin
  serial_word={serial_word[14:0],data};
  if(lrclk!=previous_lr)begin
   if(checking)begin
    if(serial_word!==(previous_lr?expected_r:expected_l))
     $fatal(1,"I2S mismatch channel=%0d got=%04x expected=%04x",previous_lr,serial_word,previous_lr?expected_r:expected_l);
    serial_checks++;
   end
   if(!lrclk)begin
    frames++;expected_l=pending_l;expected_r=pending_r;checking=1;
   end
   serial_word=0;previous_lr=lrclk;
  end
 end
end
task next_sample;
 do @(posedge clk);while(!dut.sample_ce);
 #1;
endtask
integer fd,n,before_frames,delta,failures=0;
reg [3:0] spdif_rate;
initial begin
 repeat(10)@(negedge clk);reset=0;
 // MiSTer's startup mute lasts about 342 ms at either output rate.
 repeat(9000000)@(negedge clk);
 for(n=0;n<2;n++)begin
  @(negedge clk);rate96=(n==1);
  repeat(10000)@(negedge clk);
  before_frames=frames;repeat(245760)@(negedge clk);delta=frames-before_frames;
  $display("MEASURE: MiSTer %0d kHz: %0d stereo I2S frames / 10 ms",rate96?96:48,delta);
  if(delta!=(rate96?960:480))$fatal(1,"incorrect I2S output rate");
  fd=$fopen($sformatf("tb/build/audio_audit/mister_%0d.csv",rate96?96:48),"w");
  if(!fd)$fatal(1,"cannot open output CSV");
  repeat(4096)begin next_sample();$fdisplay(fd,"%0d,%0d",$signed(dut.al),$signed(dut.ar));end
  $fclose(fd);
  // Inspect each channel-status rate bit at its subframe load point.
  spdif_rate=0;
  for(integer bit_index=24;bit_index<28;bit_index++)begin
   do @(negedge clk);while(!(dut.toslink.load_subframe_q && dut.toslink.subframe_count_q==2*bit_index));
   spdif_rate[bit_index-24]=dut.toslink.channel_status_bit_r;
  end
  $display("MEASURE: SPDIF %0d kHz output reports frequency field 0x%01x",rate96?96:48,spdif_rate);
  if(spdif_rate!=(rate96?4'ha:4'h2))begin failures++;$display("FAIL: SPDIF frequency metadata matches output rate");end
 end
 $display("PASS: %0d I2S channel words match the serializer's stereo input",serial_checks);
 $display("AUDIT: %0d MiSTer output metadata checks failed",failures);$finish;
end
initial begin #1000000000;$fatal(1,"audit watchdog");end
endmodule
