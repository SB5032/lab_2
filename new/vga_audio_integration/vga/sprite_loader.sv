module sprite_loader(input logic clk,
                   input logic reset,
                   input logic start,
                   input logic write,
                   input logic [3:0] sprite_register_number,
                   input logic [24:0] writedata,
                   input logic [9:0] vcount,
                   output logic [9:0] address_pixel_draw, 
                   output logic [15:0] data_pixel_draw,
                   output logic finish,
                   output logic wren_pixel_draw
);
    logic sprite_active_start;
    logic [3:0] sprite_number;
    logic [9:0] actual_vcount; 
    logic [4:0] row_in_sprite;
    logic [9:0] sprite_column;
    logic [4:0] img_num;
    logic is_active;
    logic sprite_active_finish;
    logic checking;
    logic checked;

    sprite_active(clk, reset, write, sprite_register_number, sprite_number, writedata, sprite_active_start, actual_vcount, row_in_sprite, sprite_column, img_num, is_active, sprite_active_finish);


    logic sprite_draw_start;
    logic sprite_draw_finish;
    logic drawing;

    sprite_draw(clk, reset, sprite_draw_start, row_in_sprite, sprite_column, img_num, wren_pixel_draw, address_pixel_draw, data_pixel_draw, sprite_draw_finish); 
    

    always_ff @(posedge clk) begin
        if (reset) begin
            finish <= 1;
            drawing <= 0;
            checked <= 0;
            checking <= 0;
            sprite_active_start <= 0;
            sprite_draw_start <= 0;
        end else if (start) begin
            sprite_number <= 0;
            finish <= 0;
            checked <= 0;
            checking <= 0;
            drawing <= 0;
            // calculate actual vcount
            if (vcount < 479) begin
                actual_vcount <= vcount + 1;
            end else if (vcount >= 479 && vcount < 524) begin
                finish <= 1; // inactive lines, nothing to draw      
            end else if (vcount == 524) begin 
                actual_vcount <= 0; // draw the first line
            end
        end else if (!finish) begin
          if (sprite_number < 16) begin //12) begin
                // check sprite active
                if (!checked) begin
                    if (!checking) begin
                        sprite_active_start <= 1;
                        checking <= 1;
                    end else begin
                        sprite_active_start <= 0;
                        if (sprite_active_finish) begin
                            checking <= 0;
                            checked <= 1;
                        end
                    end
                end else begin
                    // start drawing if active
                    if (is_active) begin 
                        if (!drawing) begin
                            drawing <= 1;
                            sprite_draw_start <= 1;
                        end else begin
                            sprite_draw_start <= 0;
                        end         
                        
                        // sprite_draw_start = 0 happens in the same cycle as sprite_draw_finish = 0
                        // need to check sprite_draw_start = 0, otherwise will detect old finish value that haven't been set to 0
                        if (drawing && (sprite_draw_start == 0) && sprite_draw_finish) begin
                            drawing <= 0;
                            sprite_number <= sprite_number + 1;
                            checked <= 0;
                        end
                    // skip if not active
                    end else begin
                        sprite_number <= sprite_number + 1;
                        checked <= 0;
                    end
                end
            end else begin
                finish <= 1;
            end
        end
    end

endmodule
