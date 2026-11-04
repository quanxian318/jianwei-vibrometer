`timescale 1ns/1ps
module tb_panel_probe;
 reg clk=0;always #5 clk=~clk;
 reg resetn=0;reg[31:0] command=0;
 reg k1=1,enc_sw=1,enc_a=1,enc_b=1;
 wire[31:0] status;wire ws_data;
 panel_probe dut(clk,resetn,command,status,k1,enc_sw,enc_a,enc_b,ws_data);
 integer pulses=0,ones=0,hi=0;reg prev=0;reg monitor_enable=1;
 always @(posedge clk) begin
  if(ws_data && monitor_enable)hi=hi+1;
  if(prev&&!ws_data && monitor_enable)begin
   pulses=pulses+1;
   if(hi==64)ones=ones+1;
   else if(hi!=32)$fatal(1,"Incorrect WS2812 high width %d",hi);
   hi=0;
  end
  prev=ws_data;
 end
 initial begin
  #100;resetn=1;#100;
  command=32'h87080000; // all five LEDs: RGB(8,0,0)
  #600000;
  if(pulses!=120||ones!=5)$fatal(1,"Incorrect serial frame %d %d",pulses,ones);
  if(status[31:24]!=8'ha6 || status[4] || status[23:8]!=1)$fatal(1,"Signature, frame counter or busy incorrect");
  k1=0;enc_sw=0;#100;
  if(status[1:0]!=0)$fatal(1,"Pressed inputs not captured");
  k1=1;enc_sw=1;#100;
  enc_a=0;#150000;enc_b=0;#150000;enc_a=1;#150000;enc_b=1;#150000;
  if(status[3:2]!=2'b11)$fatal(1,"A/B raw input state %h",status);
  enc_b=0;#150000;enc_a=0;#150000;enc_b=1;#150000;enc_a=1;#150000;
  if(status[3:2]!=2'b11)$fatal(1,"A/B raw input state %h",status);
  command=32'h07000000;#600000;
  if(pulses!=240||ones!=5)$fatal(1,"Black frame incorrect");
  if(status[23:8]!=2)$fatal(1,"Frame counter incorrect");
  monitor_enable=0;command=32'h67000000;#100;
  if(!ws_data || status[7:5]!=3'b111)$fatal(1,"Static high mode incorrect");
  #100000;
  if(status[23:8]!=2)$fatal(1,"Static mode unexpectedly sent a frame");
  command=32'h27000000;#100;
  if(ws_data || status[7:5]!=3'b010)$fatal(1,"Static low mode incorrect");
  command=32'h07000000;#100;
  if(ws_data || status[6])$fatal(1,"Cannot return to serial mode");
  $display("PANEL_STATIC_PASS: GRB waveform, frame count, raw input, steady high/low, return to serial");
  $finish;
 end
endmodule
