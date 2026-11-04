// Standalone front-panel diagnostic. No system power or load outputs.
// GPIO command: bit31 toggle commits; bits26:24 LED index (7=all);
// bits23:0 RGB. Status: A5 signature, quadrature count16, busy, raw AB/SW/K1.
`timescale 1ns/1ps
module panel_probe(input wire clk, input wire resetn,
 input wire [31:0] command, output wire [31:0] status,
 input wire k1, enc_sw, enc_a, enc_b, output wire ws_data);
 (* ASYNC_REG="TRUE" *) reg [3:0] sync1=4'hf, sync2=4'hf;
 reg [6:0] us_div=0;
 wire tick_us=(us_div==99);
 always @(posedge clk) begin
  if(!resetn) begin sync1<=4'hf;sync2<=4'hf;us_div<=0;end
  else begin
   sync1<={enc_b,enc_a,enc_sw,k1};sync2<=sync1;
   us_div<=tick_us?0:us_div+1'b1;
  end
 end
 // Raw synchronized A/B go to PS for decoding during diagnosis.
 reg [119:0] colours=0;
 reg toggle=0,start=0;
 wire busy;
 wire [23:0] grb={command[15:8],command[23:16],command[7:0]};
 integer i;
 always @(posedge clk) begin
  start<=0;
  if(!resetn) begin colours<=0;toggle<=0;end
  else if(command[31]!=toggle) begin
   toggle<=command[31];
   for(i=0;i<5;i=i+1)
    if(command[26:24]==7 || command[26:24]==i)
     colours[24*i+:24]<=grb;
   start<=1;
  end
 end
 wire serial_data;
 wire [15:0] frames;
 cc_ws2812 #(.CLK_HZ(100000000),.NLED(5)) leds(
  .clk(clk),.rst(!resetn),.start(start),.grb(colours),
  .reset_us(16'd350),.tick_us(tick_us),.dout(serial_data),
  .busy(busy),.pending(),.frames(frames));
 // bits29/30 select a steady diagnostic output for a multimeter.
 assign ws_data=resetn && (command[29] ? command[30] : serial_data);
 assign status={8'ha6,frames,command[30],command[29],ws_data,busy,sync2};
endmodule
