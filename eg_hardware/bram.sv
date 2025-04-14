/*
 * twoport Bram module from http://www.cs.columbia.edu/~sedwards/classes/2023/4840-spring/memory.pdf
 * default parameters lightly modified by Alex Yu
 * Columbia University
 */
 
 module twoportbram
    #(
        parameter                           RAM_WIDTH = 24,
        parameter                           RAM_ADDR_BITS = 16,
        parameter                           RAM_WORDS = 16'hffff
    ) (
    input logic                             clk,
    input logic [RAM_ADDR_BITS - 1:0]       ra, wa,
    input logic                             write,
    input logic [RAM_WIDTH - 1:0]           data_in,
    output logic [RAM_WIDTH - 1:0]          data_out
    );

    logic [RAM_WIDTH - 1:0] mem [RAM_WORDS - 1:0];

    always_ff @(posedge clk) begin
        if (write) mem[wa] <= data_in;
        data_out <= mem[ra];
    end
endmodule