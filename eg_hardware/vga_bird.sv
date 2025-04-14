/*
 * Avalon memory-mapped peripheral that generates VGA 
 * Sprite generation method heavily inspired from touhou project from spring 2022 by Xinye Jiang and Po-Cheng Liu.
 * Modified to handle bram rom sprites and thus reducing memory usage from loading very many sprites. 
 * Alex Yu, Riona Westphal
 * Columbia University
 */
`include "./bram.sv"
module vga_bird(
    input logic         clk,
    input logic         reset,
    input logic [31:0]  writedata,
    input logic         write,
    input logic         chipselect,
    input logic [15:0]  address,
                
    //vga ports
    output logic [7:0]  VGA_R, VGA_G, VGA_B,
    output logic        VGA_CLK, VGA_HS, VGA_VS, VGA_BLANK_n,
    output logic        VGA_SYNC_n
    );

    // Display
    logic [10:0]        hcount;
    logic [9:0]         vcount;

    vga_counters counters(.clk50(clk), .*);

    //avalon bus address data mapping

    // display up to 16 falling notes on screen
    // writedata note packet
    // |  31:26  |      25:20      |      19:10       |      9:0       | 
    // |  index  |  note_id 6 bits |   y cord 10 bits | x-cord 10 bits | 
    // |  0-63   |      0-63       |      0-639       |     0-479      |
    // ex:   32h'1a004101 => en = 1, id = 10, type = 0100, y-cord = 256.

    //store up to 16 notes. each note is built with 32 8x8 pixel sprites
    logic           sprites_write;
    logic [5:0]     sprites_write_address;
    logic [5:0]     sprites_read_address;
    logic [9:0]     sprites_x_cord;
    logic [9:0]     sprites_y_cord;
    logic [5:0]     sprites_n_value;
    logic [9:0]     x, y;
    logic [5:0]     n;

    twoportbram #(
        .RAM_WIDTH(10),
        .RAM_ADDR_BITS(6),
        .RAM_WORDS(7'h40)
    ) sprites_x (
        .clk(clk),
        .ra(sprites_read_address), 
        .wa(sprites_write_address),
        .write(sprites_write),
        .data_in(sprites_x_cord),
        .data_out(x)
    );
    twoportbram #(
        .RAM_WIDTH(10),
        .RAM_ADDR_BITS(6),
        .RAM_WORDS(7'h40)
    ) sprites_y (
        .clk(clk),
        .ra(sprites_read_address), 
        .wa(sprites_write_address),
        .write(sprites_write),
        .data_in(sprites_y_cord),
        .data_out(y)
    );
    twoportbram #(
        .RAM_WIDTH(6),
        .RAM_ADDR_BITS(6),
        .RAM_WORDS(7'h40)
    ) sprites_n (
        .clk(clk),
        .ra(sprites_read_address), 
        .wa(sprites_write_address),
        .write(sprites_write),
        .data_in(sprites_n_value),
        .data_out(n)
    );

    logic [15:0]        score;
    logic [15:0]        combo;

   // logic [31:0]        menu;    // | 1 bit   | 8 bits |   8 bits  |s
                                 // | display | item   | submenu # |
    logic [31:0]        gamedata;
    logic               reset_sw;

    always_ff @(posedge clk) begin
        if (reset) begin
            score  <=  16'h0;
	        combo  <=  16'h0;

        // data from avalon bus writes a piece of data to bram
        end else if (chipselect && write) begin
            case (address)
                4'h3 : begin
                    score <= writedata[15:0];
     		        combo <= writedata[31:16];
                end
                
                4'h4 : begin
                    gamedata <= writedata;
                    reset_sw <= writedata[0];
                end
                4'h5 : begin                     
                    sprites_x_cord          <= writedata[9:0];
                    sprites_y_cord          <= writedata[19:10];
                    sprites_n_value         <= writedata[25:20];
                    sprites_write_address   <= writedata[31:26];
                    sprites_write           <= 1;
                end
            endcase
        end else if (sprites_write) begin
            sprites_write <= 0;
        end
    end

    logic [9:0]         line;
    logic [3:0]         pattern;
    assign line = (vcount >= y) ? (vcount - y) : 10'd32;
    // logic [5:0]         length;
    // logic [5:0]         n;
    // sprites in sprites.sv are ordered from least prioety to highest priorety

	sprites sprites0(
        .n_sprite (n),
        .clk (clk),
        .pixel (pixel),
        .line (line),
        .color_code (pattern)
        );


    // logic [9:0] x, y;
    logic [639:0][3:0] buf_e;
    logic [639:0][3:0] buf_o;
    logic [3:0] state;
    logic [8:0] sprite_index;
    logic [5:0] pixel;
    logic [9:0] xposition;
    logic done;
    assign sprites_read_address = sprite_index[5:0];
    assign xposition = x + {4'd0, pixel};
    //paint basic background
    logic [3:0] bg_color;
    always_ff @(posedge clk) begin
        if      (vcount == 10'd0)   bg_color <= 4'hc;
    end

    always_ff @(posedge clk) begin
        if(reset) begin
            buf_e <= {640{4'hb}}; // some red
            buf_o <= {640{4'hb}}; // some red
            state <= 0;
            sprite_index <= 0;
            pixel <= 0;
            done <= 1;
        end else begin
            if (vcount[0]) begin     // output buffer_odd, edit buffer_even
                if ((hcount[10:1] > 640) && (vcount < 10'd480)) begin
                    for (int i = 0; i < 480; i++)
                        buf_o[i] <= bg_color;
                    for (int i = 480; i < 640; i++)
                        buf_o[i] <= 4'h0;
                end
            end else begin         // output buffer_even, edit buffer_odd 
                if ((hcount[10:1] > 640) && (vcount < 10'd480)) begin
                    for (int i = 0; i < 480; i++)
                        buf_e[i] <= bg_color;
                    for (int i = 480; i < 640; i++)
                        buf_e[i] <= 4'h0;
                end
            end

            if (hcount == 11'd1) begin
                done <= 0;  // 0/1
                state <= 4'd0; // 0-2
                sprite_index <= 0; // 0-127
                pixel <= 0; // 0-31
            end
            if(~done)begin
                case(state)
                    4'd0 : begin
                        // n, x, y ready next clk cycle
                        pixel <= 0;
                        state <= 4'd1;
                    end
                    4'd1 : begin
                        if ((n == 0) || (line >= 32)) begin // not in this line orr empty sprite
                            pixel <= 6'd0;
                            state <= 4'd0;
                            if (sprite_index < 9'd63) 	// check if there are more sprites to check (existance of 32 potential sprites)
                                sprite_index <= sprite_index + 8'd1;
                            else
                                done <= 1;
                        end else begin
                            state <= 4'd2;
                        end
                    end
                    4'd2 : begin
                        if (vcount[0]) begin     	// output buffer_odd, edit buffer_even
                            if (pattern != 4'h0) begin
                                if (xposition < 10'd640)
                                    buf_e[xposition] <= pattern;
                            end
                        end else begin             	// output buffer_even, edit buffer_odd
                            if (pattern != 4'h0) begin
                                if (xposition < 10'd640)
                                    buf_o[xposition] <= pattern;
                            end
                        end
							// repeat writing state pixel is 0-30 or under
                        if (pixel < 6'd31) begin
                            pixel <= pixel + 1;
                            state <= state;
                        end else begin 			// on pixel == 31 (32 pixel)
                            pixel <= 6'd0;
                            state <= 4'd0;
                            if (sprite_index < 9'd63) 	// repeat on sprite number 0-62
                                sprite_index <= sprite_index + 8'd1;
                            else                      	// stop on 63
                                done <= 1;
                        end
                    end
                    default:;
                endcase
            end
        end
    end
    
    //logic [3:0] color_code_out;
    logic [23:0] color_out;
	sprite_color_pallete colors(
		.color_code_o (buf_o[hcount[10:1]][3:0]),
        .color_code_e (buf_e[hcount[10:1]][3:0]),
        .select (vcount[0]),
		.color (color_out)	
	);

   always_comb begin
        {VGA_R, VGA_G, VGA_B} = 24'h0;
        if (VGA_BLANK_n) begin
            if (vcount > 10'd1 && vcount < 10'd480) {VGA_R, VGA_G, VGA_B} = color_out;
        end
    end


endmodule

module vga_counters(
    input logic             clk50, reset,
    output logic [10:0]     hcount,  // hcount[10:1] is pixel column
    output logic [9:0]      vcount,  // vcount[9:0] is pixel row
    output logic            VGA_CLK, VGA_HS, VGA_VS, VGA_BLANK_n, VGA_SYNC_n);

/*
 * 640 X 480 VGA timing for a 50 MHz clock: one pixel every other cycle
 * 
 * HCOUNT 1599 0             1279       1599 0
 *             _______________              ________
 * ___________|    Video      |____________|  Video
 * 
 * 
 * |SYNC| BP |<-- HACTIVE -->|FP|SYNC| BP |<-- HACTIVE
 *       _______________________      _____________
 * |____|       VGA_HS          |____|
 */
// Parameters for hcount
    parameter 
          HACTIVE      = 11'd 1280,
          HFRONT_PORCH = 11'd 32,
          HSYNC        = 11'd 192,
          HBACK_PORCH  = 11'd 96,   
          HTOTAL       = HACTIVE + HFRONT_PORCH + HSYNC +
          HBACK_PORCH; // 1600

          // Parameters for vcount
          parameter 
          VACTIVE      = 10'd 480,
          VFRONT_PORCH = 10'd 10,
          VSYNC        = 10'd 2,
          VBACK_PORCH  = 10'd 33,
          VTOTAL       = VACTIVE + VFRONT_PORCH + VSYNC +
          VBACK_PORCH; // 525

          logic endOfLine;

    always_ff @(posedge clk50 or posedge reset)
        if (reset)          hcount <= 0;
        else if (endOfLine) hcount <= 0;
        else               hcount <= hcount + 11'd 1;

    assign endOfLine = hcount == HTOTAL - 1;

    logic endOfField;

    always_ff @(posedge clk50 or posedge reset)
        if (reset)          vcount <= 0;
        else if (endOfLine)
            if (endOfField)   vcount <= 0;
            else              vcount <= vcount + 10'd 1;

    assign endOfField = vcount == VTOTAL - 1;

    // Horizontal sync: from 0x520 to 0x5DF (0x57F)
    // 101 0010 0000 to 101 1101 1111
    assign VGA_HS = !( (hcount[10:8] == 3'b101) &
                    !(hcount[7:5] == 3'b111));
    assign VGA_VS = !( vcount[9:1] == (VACTIVE + VFRONT_PORCH) / 2);

    assign VGA_SYNC_n = 1'b0; // For putting sync on the green signal; unused

    // Horizontal active: 0 to 1279     Vertical active: 0 to 479
    // 101 0000 0000  1280           01 1110 0000  480
    // 110 0011 1111  1599           10 0000 1100  524
    assign VGA_BLANK_n =     !( hcount[10] & (hcount[9] | hcount[8]) ) &
                            !( vcount[9] | (vcount[8:5] == 4'b1111) );

        /* VGA_CLK is 25 MHz
         *             __    __    __
         * clk50    __|  |__|  |__|
         *        
         *             _____       __
         * hcount[0]__|     |_____|
         */
    assign VGA_CLK = hcount[0]; // 25 MHz clock: rising edge sensitive

endmodule
