

module sprite_active(input logic clk,
                      input logic reset,
                      input logic write_sprite,
                      input logic [4:0] sprite_number_write,
                      input logic [4:0] sprite_number, //kv2446
                      input logic [24:0] sprite_register,
                      input logic write_vcount,
                      input logic [9:0] actual_vcount, // the line being drawn
                      output logic [4:0] row_in_sprite, // the row needed to be drawn in the sprite
                      output logic [9:0] sprite_column, // where the sprite is located
                      output logic [4:0] img_num,
                      output logic is_active, // input sprite is indeed, active
                      output logic finish // finish the sprite
);


  // sprite array access
  logic [31:0][24:0] sprite_array;
  // indexing: 24 is active, 23-15 is v/row, 14-5 is h/col 4-0 is image number
  

  always_ff @(posedge clk) begin
    if(reset) begin
      finish <= 1;
      is_active <= 0;
    end else if (write_vcount) begin
      finish <= 0;
      if (actual_vcount < 480) begin
        // check corresponding sprite register
        if (sprite_array[sprite_number][24] == 1) begin
          // if this sprite is active at somewhere on the screen
          if (actual_vcount >= sprite_array[sprite_number][23:15] && 
              actual_vcount - sprite_array[sprite_number][23:15] < 32) begin
            // is within range, output sprite
            row_in_sprite <= (actual_vcount - sprite_array[sprite_number][23:15]);
            sprite_column <= sprite_array[sprite_number][14:5];
            img_num <= sprite_array[sprite_number][4:0];
            is_active <= 1;
            finish <= 1;
          end else begin
            // is not within range
            is_active <= 0;
            finish <= 1;
          end
        end else begin
          // sprite not active 
          is_active <= 0;
          finish <= 1;
        end
      end else if (actual_vcount >= 480) begin
        is_active <= 0;
        finish <= 1; // inactive lines, nothing to do
      end
    end
  end

  // for when needing to change sprite_register value
  // -1 because incoming number because sprite register base is base + 1
  always_ff @(posedge clk) begin
    if (write_sprite) begin
      sprite_array[sprite_number_write - 1][24:0] <= sprite_register[24:0];
    end
  end

  
endmodule
