

module sprite_draw(input logic clk,
                    input logic reset,
                    input logic start,
                    input logic [4:0] row_in_sprite, // the row needed to be drawn in the sprite
                    input logic [9:0] sprite_column, // where the sprite is located
                    input logic [4:0] img_num,
                    output logic wren,
                    output logic [9:0] pixel_hcount, // where the pixel goes on the row
                    output logic [15:0] data, // pixel data
                    output logic finish
);

  logic [15:0] sprite_rom_address;
  sprite_rom(sprite_rom_address, clk, data); 

  logic [5:0] col;
  assign wren = (!finish) && (col > 0) && (data[0] == 0); // write only not transparent 

  always_ff @(posedge clk) begin
    if (reset) begin 
      finish <= 1;
    end else if (start) begin
      // img_num * 1024 (# of pixels per img) + row_in_sprite * 32 (# of pixels per row)
      sprite_rom_address <= (img_num << 10) + (row_in_sprite << 5);
      col <= 0;
      finish <= 0;
      pixel_hcount <= 0;
    end else if (!finish) begin
      // get pixel data from rom
      if (col < 32 && pixel_hcount < 639) begin
        col <= col + 1;
        sprite_rom_address <= sprite_rom_address + 1;
      end else
        finish <= 1;

      // output linebuffer address to draw
      if (col == 0) begin // wait 1 cycle to sync with data_tile_draw 
        pixel_hcount <= sprite_column;  
      end else if (pixel_hcount < 639) begin
        pixel_hcount <= pixel_hcount + 1;
      end
    end
  end


  
endmodule
