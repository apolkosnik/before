`timescale 1ns/1ps
module tb_next_audio_adc;
reg clk=0,reset=1;
always #5 clk=~clk;
wire [3:0] adc_bus;
wire signed [15:0] pcm;
reg [11:0] level=2048,shift=0;
assign adc_bus[2]=shift[11];
// ADC model: conversion captures the analog level, MSB first on SPI.
always @(posedge adc_bus[0]) shift=level;
always @(negedge adc_bus[3]) shift={shift[10:0],1'b0};
next_audio_adc #(.CLK_REAL_HZ(8000000)) dut(
 .clk(clk),.reset(reset),.ADC_BUS(adc_bus),.audio_in(pcm));
task check(input bit ok,input string message);
 begin if(!ok)$fatal(1,"FAIL: %s (PCM=%0d)",message,pcm);else $display("PASS: %s",message);end
endtask
initial begin
 repeat(10)@(negedge clk);reset=0;
 repeat(10000)@(negedge clk);
 check(dut.raw==2048,"LTC2308 reads the serial 12-bit sample");
 check(pcm==0,"constant ADC bias produces silence");
 // Allow the 2.5 ms FIR group delay plus its step response.
 level=2304;repeat(28000)@(negedge clk);
 check(pcm>1500 && pcm<2100,"positive ADC amplitude survives averaging and DC removal");
 level=1792;repeat(28000)@(negedge clk);
 check(pcm< -1500 && pcm> -2400,"negative ADC amplitude remains signed");
 reset=1;repeat(10)@(negedge clk);level=3000;reset=0;
 repeat(10000)@(negedge clk);
 check(pcm==0,"reset reacquires a different input bias without a DC recording");
 $display("ALL PASS");$finish;
end
endmodule
