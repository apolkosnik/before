// Exercise the core's ADC timing with worst-budget Tco/Tsu/board propagation
// and the datasheet's maximum SDO enable/data-valid delays.
`timescale 1ns/1ps
module tb_next_adc_timing;
reg clk=0,reset=1;
always #17.857143 clk=~clk;
wire [3:0] bus;
wire [11:0] raw;
wire sync;
ltc2308 #(.NUM_CH(1),.ADC_RATE(64096),.CLK_RATE(28000000),
 .SCK_DIV(2),.CONV_WAIT_NS(2000),.LONG_CONVST(1)) dut(
 .clk(clk),.reset(reset),.ADC_BUS(bus),.dout(raw),.dout_sync(sync));
// 18 ns root-clock-referenced Tco/Tsu + 2 ns board in each direction.
// The model uses ideal local clocks; each delay represents the corresponding
// effective I/O requirement INCLUDING the clock insertion term from TimeQuest.
wire sck,convst,sdi;
assign #20 sck=bus[3];
assign #20 convst=bus[0];
assign #20 sdi=bus[1];
reg sdo=0;
assign #20 bus[2]=sdo;
reg [11:0] conversion_word=0,shift=0;
reg [5:0] configuration=0;
integer count=0,bit_index=0;
realtime began=0,last_sck=0,last_rise=0;
always @(sdi) if(!reset && last_rise>0 && $realtime-last_rise<2.5)
 $fatal(1,"SDI hold budget violated");
always @(posedge convst) if(!reset)begin
 began=$realtime;
 conversion_word=12'((count*73)^12'ha5b);bit_index=0;
end
always @(negedge convst) if(!reset && began>0)begin
 if($realtime-began<1600)$fatal(1,"CONVST fell before maximum conversion time");
 shift=conversion_word;
 #15;sdo=shift[11];
end
always @(negedge sck) if(!reset && began>0)begin
 last_sck=$realtime;
 shift={shift[10:0],1'b0};
 #12.5;sdo=shift[11];
end
always @(posedge sck) if(!reset)begin
 last_rise=$realtime;
 if($realtime-last_sck<70 && bit_index!=0)$fatal(1,"short SCK half-period");
 if(bit_index<6)configuration={configuration[4:0],sdi};
 if(bit_index==5 && configuration!=6'b100010)$fatal(1,"incorrect ADC command");
 bit_index++;
end
always @(negedge clk) if(!reset && bit_index==12)begin
 if(raw!==conversion_word)$fatal(1,"delayed ADC data mismatch expected=%03x got=%03x",conversion_word,raw);
 bit_index=13;count++;
 if(count==100)begin
  $display("PASS: 100 ADC conversions with maximum propagation/data-valid delays");
  $display("ALL PASS");$finish;
 end
end
initial begin repeat(5)@(negedge clk);reset=0;#2000000;$fatal(1,"ADC timing watchdog");end
endmodule
