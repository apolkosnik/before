`timescale 1ns/1ps
module tb_next_snd_in;
reg clk=0, reset=1;
always #5 clk=~clk;
reg [14:0] addr=0;
reg sel=0,we=0;
reg [1:0] be=3;
reg [15:0] wd=0;
wire kms=sel && addr[14:4]==11'he00;
wire csr=sel && addr[14:2]==13'h20;
wire sptr=sel && addr[14:4]==11'h407;
wire ptr=sel && addr[14:4]==11'h408;
wire ini=sel && addr[14:2]==13'h10a0;
wire [15:0] krd,drd;
wire active,clear_status,request_status,overrun,irq,ovirq;
wire req,mem_we;
wire [29:0] ma;
wire [31:0] md;
wire [3:0] mb;
reg ack=0;
wire err=req && ma[29:24]!=6'd1;
reg hold_ack=0;
reg signed [15:0] pcm=0;
next_snd_in #(.CLK_REAL_HZ(1000000)) dut(
 .clk(clk),.reset(reset),.active(active),.clear_status(clear_status),.audio_in(pcm),
 .request_status(request_status),.overrun(overrun),
 .sel_csr(csr),.sel_sptr(sptr),.sel_ptr(ptr),.sel_ini(ini),
 .addr(addr[3:0]),.we(we),.be(be),.wdata(wd),.rdata(drd),.int_dma(irq),
 .m_req(req),.m_we(mem_we),.m_addr(ma),.m_be(mb),.m_din(md),.m_ack(ack),.m_err(err));
next_kms_snd #(.CLK_HZ(1000000)) control(
 .clk(clk),.reset(reset),.ps2_key(11'd0),.ps2_mouse(25'd0),
 .sel_kms(kms),.sel_csr(1'b0),.sel_sptr(1'b0),.sel_ptr(1'b0),.sel_ini(1'b0),
 .addr(addr[3:0]),.we(we),.be(be),.wdata(wd),.rdata(krd),
 .m_req(),.m_we(),.m_addr(),.m_be(),.m_din(),.m_dout(32'd0),.m_ack(1'b0),.m_err(1'b0),
 .sndin_active(active),.sndin_clear(clear_status),.sndin_request(request_status),.sndin_overrun(overrun),
 .int_snd_ovrun(ovirq),.int_snd_out_dma(),.int_keymouse(),.int_power(),.audio_l(),.audio_r());
reg [31:0] ram[0:4095];
integer writes=0,delay_count=0;
always @(posedge clk) begin
 if(reset || !req) begin ack<=0;delay_count<=0;end
 else if(!ack && !err && !hold_ack) begin
  if(delay_count==4) begin
   if(!mem_we || mb!=15) $fatal(1,"bad recording RAM transaction");
   ram[ma[11:0]]<=md;writes<=writes+1;ack<=1;
  end else delay_count<=delay_count+1;
 end
end
task wr(input [14:0] a,input [15:0] v,input [1:0] lanes=3);
 begin
  @(negedge clk);sel=1;we=1;addr=a;wd=v;be=lanes;
  @(negedge clk);sel=0;we=0;
 end
endtask
task put(input [14:0] a,input [31:0] v);
 begin wr(a,v[31:16]);wr(a+15'd2,v[15:0]);end
endtask
task get(input [14:0] a,output [31:0] v);
 begin
  @(negedge clk);sel=1;we=0;addr=a;
  @(negedge clk);v[31:16]=drd;addr=a+15'd2;
  @(negedge clk);v[15:0]=drd;sel=0;
 end
endtask
task command(input [7:0] v);
 begin wr(15'he003,{8'd0,v},1);put(15'he004,0);end
endtask
task arm(input [31:0] start,input [31:0] limit,input [7:0] c=8'h05);
 begin put(15'h4280,start);put(15'h4084,limit);put(15'h80,{8'd0,c,16'd0});end
endtask
task check(input bit ok,input string description);
 begin if(!ok)$fatal(1,"FAIL: %s",description);else $display("PASS: %s",description);end
endtask
task wait_irq;
 integer n;
 begin n=0;while(!irq && n<100000)begin @(negedge clk);n=n+1;end
  check(irq,"recording completes within its sample budget");end
endtask
task next_sample;
 begin
  @(negedge clk);
  while(!dut.sample_tick)@(negedge clk);
  @(negedge clk);
 end
endtask
integer i,before_writes;
bit exact;
reg [31:0] value,held_data;
reg [29:0] held_addr;
initial begin
 for(i=0;i<4096;i=i+1)ram[i]=32'h12345678;
 repeat(10)@(negedge clk);reset=0;
 // The failed kernel had a 128-byte input descriptor and needed a
 // nonzero CSR even after KMS disabled the codec for dma_abort.
 arm(32'h04002000,32'h04002080);
 repeat(200)@(negedge clk);get(15'h80,value);
 check(value==32'h01000000 && writes==0,"armed input is visible before the codec starts");
 command(8'h0b);
 repeat(200)@(negedge clk);
 check(writes==0,"recording is paced; no word before four codec samples");
 wait_irq;
 get(15'h4080,value);
 check(value==32'h04002080 && writes==32,"128-byte window completes at its exact limit");
 get(15'h4074,value);check(value==32'h04002080,"saved limit reports captured bytes");
 exact=1;
 for(i=2048;i<2080;i=i+1)if(ram[i]!=32'hffffffff)exact=0;
 check(exact,"all 128 mu-law silence bytes reach guest RAM exactly");
 put(15'h80,32'h00080000);get(15'h80,value);
 check(value==32'h08000000 && irq,"late ACK retains a stopped completion");
 command(8'h03);get(15'h80,value);
 check(value!=0 && !active,"dma_abort poll terminates after recording stops");
 put(15'h80,32'h00100000);get(15'h80,value);
 check(value==0 && !irq && !ovirq,"RESET retires the stopped input channel");

 // Stop before completion, then restart with another buffer.
 arm(32'h04002100,32'h04002200);command(8'h0b);
 repeat(1000)@(negedge clk);command(8'h03);
 repeat(20)@(negedge clk);before_writes=writes;
 repeat(1000)@(negedge clk);get(15'h80,value);
 check(writes==before_writes && value==32'h01000000,"stop preserves ENABLE for abort without new samples");
 put(15'h80,32'h00100000);

 // Natural chaining and a refill ACK arriving after both windows stop.
 put(15'h4088,32'h04002300);put(15'h408c,32'h04002310);
 arm(32'h04002200,32'h04002210,8'h07);command(8'h0b);wait_irq;
 get(15'h80,value);check(value==32'h09000000,"chained completion keeps ENABLE");
 repeat(2500)@(negedge clk);
 put(15'h4088,32'h04002400);put(15'h408c,32'h04002410);
 put(15'h80,32'h000a0000);get(15'h80,value);
 check(value==32'h0a000000,"late refill cannot erase stopped input completion");
 command(8'h03);put(15'h80,32'h00100000);

 // RESET during a held memory request; a late response must not alter
 // the replacement descriptor or resurrect COMPLETE.
 arm(32'h04002500,32'h04002504);hold_ack=1;command(8'h0b);
 wait(req);@(negedge clk);held_data=md;held_addr=ma;
 command(8'h03);put(15'h80,32'h00100000);put(15'h4280,32'h04002600);
 repeat(20)@(negedge clk);
 check(req && ma==held_addr && md==held_data,"pending RAM request stays stable through reset");
 hold_ack=0;repeat(20)@(negedge clk);get(15'h4080,value);
 check(value==32'h04002600 && !irq,"late response cannot resurrect aborted input");

	// Data encoding endpoints through the actual register/DMA path.
 pcm=32767;arm(32'h04002700,32'h04002704);command(8'h0b);wait_irq;
 check(ram[2496]==32'h80808080,"positive PCM clips to mu-law 80");
 command(8'h03);put(15'h80,32'h00100000);
 pcm=-32768;arm(32'h04002704,32'h04002708);command(8'h0b);wait_irq;
 check(ram[2497]==0,"negative PCM endpoint handles signed magnitude overflow");
 command(8'h03);put(15'h80,32'h00100000);

 // Distinct samples prove chronological byte packing, not just a
 // constant-fill path. Reference mu-law bytes: 0, +1000, -1000, +32767.
 pcm=0;arm(32'h04002708,32'h0400270c);command(8'h0b);
 next_sample;pcm=1000;next_sample;pcm=-1000;next_sample;pcm=32767;
 wait_irq;check(ram[2498]==32'hffce4e80,"four distinct codec samples keep oldest-first byte order");
 command(8'h03);put(15'h80,32'h00100000);

 // Complete a window on the very clock of CLRCOMPLETE. This is a
 // new event, whether chaining kept ENABLE set or the channel stopped.
 for(i=0;i<2;i=i+1)begin
  put(15'h4088,32'h04002820);put(15'h408c,32'h04002824);
  arm(32'h04002800,32'h04002804,i ? 8'h07 : 8'h05);command(8'h0b);
  wait(ack);@(negedge clk);sel=1;we=1;addr=15'h80;wd=8;be=3;
  @(negedge clk);sel=0;we=0;get(15'h80,value);
  check(value==(i ? 32'h09000000 : 32'h08000000) && irq,
        "new input completion survives ACK on the same clock");
  command(8'h03);put(15'h80,32'h00100000);
 end
 arm(32'h04002840,32'h04002844);command(8'h0b);
 wait(ack);@(negedge clk);sel=1;we=1;addr=15'h80;wd=16'h10;be=3;
 @(negedge clk);sel=0;we=0;get(15'h80,value);
 check(value==0 && !irq,"RESET wins over same-clock input completion");
 command(8'h03);

 arm(32'h14002000,32'h14002004);command(8'h0b);wait_irq;
 get(15'h80,value);check(value==32'h18000000,"invalid high pointer faults without a low-RAM alias");
 command(8'h03);put(15'h80,32'h00100000);
 arm(32'h04002001,32'h04002004);get(15'h80,value);
 check(value==32'h18000000,"unaligned input window completes as a bus exception");
 put(15'h80,32'h00100000);

 // Both overrun sources share one interrupt: acknowledging input must
 // not clear an outstanding output underrun.
 command(8'h0f);command(8'h0b);repeat(1000)@(negedge clk);
 check(ovirq && overrun,"empty recording channel raises input overrun");
 command(8'h03);repeat(10)@(negedge clk);
 check(!overrun && ovirq,"input stop preserves output underrun interrupt");
 command(8'h07);repeat(10)@(negedge clk);check(!ovirq,"both sound interrupt sources clear independently");
 $display("ALL PASS");$finish;
end
initial begin #10000000;$fatal(1,"recording bench timeout");end
endmodule
