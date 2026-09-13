// Serial ADC -> signed PCM -> mu-law -> recording DMA, plus tone measurements.
`timescale 1ns/1ps
module audit_audio_capture;
reg clk=0,reset=1,active=0;
always #5 clk=~clk;
wire [3:0] adc_bus;
wire signed [15:0] pcm;
reg [11:0] shift=0;
real frequency=1000.0;
integer conversion=0;
integer serial_bit=0;
reg [5:0] config_word=0;
assign adc_bus[2]=shift[11];
always @(posedge adc_bus[0])begin
 shift=12'(2048+$rtoi(512.0*$sin(6.283185307179586*frequency*conversion/64096.0)));
 conversion++;
 serial_bit=0;
end
always @(negedge adc_bus[3])shift={shift[10:0],1'b0};
always @(posedge adc_bus[3])begin
 if(serial_bit<6)begin
  config_word={config_word[4:0],adc_bus[1]};
  if(serial_bit==5 && config_word!==6'b100010)
   $fatal(1,"ADC configuration is not single-ended channel zero, unipolar, awake");
 end
 serial_bit++;
end
next_audio_adc #(.CLK_REAL_HZ(28000000)) adc(
 .clk(clk),.reset(reset),.ADC_BUS(adc_bus),.audio_in(pcm));
reg sel_csr=0,sel_ptr=0,we=0;
reg [3:0] addr=0;
reg [15:0] wdata=0;
wire req,irq;
wire [29:0] mem_addr;
wire [31:0] mem_data;
reg ack=0;
next_snd_in #(.CLK_REAL_HZ(28000000)) dma(
 .clk(clk),.reset(reset),.active(active),.clear_status(1'b0),.audio_in(pcm),
 .request_status(),.overrun(),.sel_csr(sel_csr),.sel_sptr(1'b0),.sel_ptr(sel_ptr),.sel_ini(1'b0),
 .addr(addr),.we(we),.be(2'b11),.wdata(wdata),.rdata(),.int_dma(irq),
 .m_req(req),.m_we(),.m_addr(mem_addr),.m_be(),.m_din(mem_data),.m_ack(ack),.m_err(1'b0));
// Independent integer mu-law reference (segment search by repeated shifting).
function automatic [7:0] ulaw(input integer sample);
 integer mag,exp,scan,mask;
 begin
  mask=sample<0?8'h7f:8'hff;
  mag=sample<0?-sample:sample;if(mag>32635)mag=32635;
  mag+=132;exp=0;scan=mag>>8;
  while(scan!=0)begin exp++;scan=scan>>1;end
  ulaw=8'(((exp<<4)|((mag>>(exp+3))&15))^mask);
 end
endfunction
integer captured=0,sample_number=0;
reg [31:0] expected=0;
always @(posedge clk)begin
 if(reset)begin ack<=0;captured=0;sample_number=0;expected=0;end
 else begin
  ack<=req && !ack;
  if(active && dma.sample_tick)begin
   expected={expected[23:0],ulaw(int'(pcm))};sample_number++;
  end
  if(req && ack)begin
   if(mem_data!==expected || mem_addr!==(30'h01000800+30'(captured)))
    $fatal(1,"ADC->DMA mismatch word=%0d address=%08x got=%08x expected=%08x",captured,mem_addr,mem_data,expected);
   captured++;
  end
 end
end
task wr(input bit csr,input [3:0] a,input [15:0] data);
 @(negedge clk);sel_csr=csr;sel_ptr=!csr;addr=a;wdata=data;we=1;
 @(negedge clk);sel_csr=0;sel_ptr=0;we=0;
endtask
task next_sample;
 do @(posedge clk);while(!adc.audio_valid);
 #1;
endtask
integer n,j,fd;
initial begin
 for(n=-32768;n<=32767;n++)
  if(dma.encode_ulaw(16'(n))!==ulaw(n))$fatal(1,"mu-law mismatch %0d",n);
 $display("PASS: all 65,536 signed PCM codes match independent mu-law reference");
 for(j=0;j<2;j++)begin
  @(negedge clk);reset=1;active=0;conversion=0;frequency=j==0?1000.0:5000.0;
  repeat(5)@(negedge clk);reset=0;
  wr(0,0,16'h0400);wr(0,2,16'h2000);wr(0,4,16'h0401);wr(0,6,16'h0000);wr(1,0,16'h0015);
  @(negedge clk);active=1;
  repeat(2000)next_sample();
  fd=$fopen($sformatf("tb/build/audio_audit/capture_%0d.csv",integer'(frequency)),"w");
  if(!fd)$fatal(1,"cannot open capture CSV");
  repeat(2048)begin next_sample();$fdisplay(fd,"%0d",pcm);end
  $fclose(fd);
  $display("PASS: %0d Hz serial ADC tone through PCM and %0d verified DMA words",integer'(frequency),captured);
 end
 $display("ALL CAPTURE CHECKS PASS");$finish;
end
initial begin #1000000000;$fatal(1,"audit watchdog");end
endmodule
