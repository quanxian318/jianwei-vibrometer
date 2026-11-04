`timescale 1ns/1ps
// Temporary static voltage diagnostic only. No WS2812 serial transmitter.
module panel_probe(input wire clk, input wire resetn,
 input wire [31:0] command, output wire [31:0] status,
 input wire k1, enc_sw, enc_a, enc_b, output wire ws_data);
 (* ASYNC_REG="TRUE" *) reg [3:0] sync1=4'hf, sync2=4'hf;
 always @(posedge clk) begin
  if(!resetn) begin sync1<=4'hf;sync2<=4'hf;end
  else begin sync1<={enc_b,enc_a,enc_sw,k1};sync2<=sync1;end
 end
 assign ws_data=resetn && command[29] && command[30];
 assign status={8'ha7,16'b0,command[30],command[29],ws_data,1'b0,sync2};
endmodule
