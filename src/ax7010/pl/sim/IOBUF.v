// Behavioural stand-in for the Xilinx IOBUF primitive (simulation only).
`timescale 1ns / 1ps
module IOBUF (
    output wire O,
    inout  wire IO,
    input  wire I,
    input  wire T
);
    assign IO = T ? 1'bz : I;
    assign O = IO;
endmodule
