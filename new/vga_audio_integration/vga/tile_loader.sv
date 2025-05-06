/*module fake_tile_array(
	address_a,
	address_b,
	clock,
	data_a,
	data_b,
	wren_a,
	wren_b,
	q_a,
	q_b);

	input	[10:0]  address_a;
	input	[10:0]  address_b;
	input	  clock;
	input	[7:0]  data_a;
	input	[7:0]  data_b;
	input	  wren_a;
	input	  wren_b;
	output	[7:0]  q_a;
	output	[7:0]  q_b;

  always_ff @(posedge clock) begin
    if (address_a < 40 || address_a >= 1160 || address_a%40 == 0 || address_a%40 == 39)
      q_a <= 1;
    else
      q_a <= 0;
  end

endmodule*/

module tile_loader(input logic clk,
                   input logic reset,
                   input logic start,
                   input logic write,
                   input logic [18:0] writedata,
                   input logic [9:0] vcount,
                   output logic [5:0] address_tile_draw, // = column of a tile
                   output logic [255:0] data_tile_draw,
                   output logic finish
                   // output logic wren_tile_draw, // should I add this?
);

  logic [5:0] col;

  logic [10:0] tile_array_address_read;
  logic [10:0] tile_array_address_write;
  assign tile_array_address_write = writedata[18:14]*40 + writedata[13:8];

  logic [9:0] actual_vcount; // the next line to draw
  logic [7:0] tile_img_num; 
  logic [11:0] tile_rom_address; // adjusted according to the actual rom size

  // row = actual_vcount / 16 = actual_vcount >> 4
  // address of the beginning of that row = row * 40
  assign tile_array_address_read = (actual_vcount >> 4)*40 + col; 

  tile_array(.address_a(tile_array_address_read), // port a for read
           .address_b(tile_array_address_write), // port b for write
           .clock(clk),
           .data_b(writedata[7:0]), 
           .wren_a(1'b0),
           .wren_b(write), 
           .q_a(tile_img_num));

  // 16 rows (words) per image, vcount%16 is the row (word) in the tile
  // tile_img_num * 16 + actual_vcount % 16
  assign tile_rom_address = (tile_img_num << 4) + actual_vcount[3:0]; // + has higher priority than << 

  tile_rom(tile_rom_address, clk, data_tile_draw);

  always_ff @(posedge clk) begin
    if(reset) begin
      //wren <= 0;
      finish <= 1;
    end else if (start) begin
      finish <= 0;
      col <= 0; 
      // calculate actual vcount
      if (vcount < 479) begin
        actual_vcount <= vcount + 1;
      end else if (vcount >= 479 && vcount < 524) begin
        finish <= 1; // inactive lines, nothing to draw      
      end else if (vcount == 524) begin 
        actual_vcount <= 0; // draw the first line
      end
    end else if (!finish) begin
      if (col < 39)
        col <= col+1;  
      if (address_tile_draw == 38) 
        finish <= 1;
        //wren_tile_draw <= 0;  
    end
  end

  // output linebuffer address to draw
  always_ff @(posedge clk) begin
    if (col <= 1) begin // wait 2 cycles to sync with data_tile_draw //why wait 3 cycle works, but 2 does not?
      address_tile_draw <= 0;  
      //wren_tile_draw <= 1;  
    end else if (!finish) begin
      address_tile_draw <= address_tile_draw + 1;
    end
  end

endmodule
