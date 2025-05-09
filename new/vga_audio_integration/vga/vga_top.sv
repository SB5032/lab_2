/*
 * Avalon memory-mapped peripheral that generates VGA
 *
 * Stephen A. Edwards
 * Columbia University
 */

module vga_top(input logic        clk,
	        input logic 	   reset,
		input logic [31:0]  writedata,
		input logic 	   write,
		input 		   chipselect,
		input logic [4:0]  address, //KV2446

		output logic [7:0] VGA_R, VGA_G, VGA_B,
		output logic 	   VGA_CLK, VGA_HS, VGA_VS,
		                   VGA_BLANK_n,
		output logic 	   VGA_SYNC_n);

   logic [10:0]	   hcount;
   logic [9:0]     vcount;
	
   vga_counters counters(.clk50(clk), .*);
    
    // line buffer
    logic	[5:0]  address_tile_display;
	  logic	[9:0]  address_pixel_display;
	  logic	[5:0]  address_tile_draw;
	  logic	[9:0]  address_pixel_draw;

	  logic	[255:0]  data_tile_display;
    logic	[15:0]  data_pixel_display;
    logic	[255:0]  data_tile_draw;
    logic	[15:0]  data_pixel_draw;
	  
    
	  logic	  wren_tile_display;
	  logic	  wren_pixel_display;
    logic	  wren_tile_draw;
	  logic	  wren_pixel_draw;

	  logic	[255:0]  q_tile_display;
    logic	[15:0]  q_pixel_display;
    logic	[255:0]  q_tile_draw;
    logic	[15:0]  q_pixel_draw;

    logic switch;

    linebuffer(.*);
    
    // tile loader
    logic tile_start;
    logic tile_finish;
    logic tile_write;
    assign tile_write = (chipselect && write && (address == 0)); // address = 0: write tile
    // low 19 bit of writedata: row(5b), column(6b), tile image number(8b)
    tile_loader(clk, reset, tile_start, tile_write, writedata[18:0], vcount, address_tile_draw, data_tile_draw, tile_finish);

    // sprite loader
    logic sprite_start;
    logic sprite_finish;
    logic sprite_write;
    assign sprite_write = (chipselect && write && (address >= 1)); // address >= 1: write sprite
    sprite_loader(clk, reset, sprite_start, sprite_write, address, writedata[24:0], vcount, address_pixel_draw, data_pixel_draw, sprite_finish, wren_pixel_draw);
    //logic [4:0] row_in_sprite;
    //assign row_in_sprite = (vcount+1)%525%32;
    //sprite_draw(clk, reset, sprite_start, row_in_sprite, 10'd16, 5'd0, wren_pixel_draw, address_pixel_draw, data_pixel_draw, sprite_finish);

    logic drawing_sprite;

   // draw line buffer
   always_ff @(posedge clk) begin
      if (reset) begin
        wren_tile_display <= 0;
        wren_pixel_display <= 0;
        wren_tile_draw <= 0;
        tile_start <= 0;
        sprite_start <= 0;
        drawing_sprite <= 0;
        
        tile_start <= 0;
      // only draw active lines
      end else if (vcount < 479 || vcount == 524) begin
        // start the tile loader to write 40 tiles
        if(hcount == 0) begin          
          tile_start <= 1;
          wren_tile_draw <= 1;
          drawing_sprite <= 0;
        end else begin
          tile_start <= 0;
        end

        // wait for tile loader to finish
        // careful, setting start=1 takes 1 cycle and resetting tile_finish takes another
        // so tile start becomes 1 at hcount=1 and tile_finish becomes 0 at hcount=2
        if(hcount > 1 && tile_finish && (drawing_sprite == 0)) begin
          wren_tile_draw <= 0;
          // tile draw done, start sprite
          sprite_start <= 1;
          drawing_sprite <= 1;
        end 
        // pull sprite_start back to 0 since it only needs 1 cycle pulse
        if (drawing_sprite) begin
          sprite_start <= 0;
        end

      end 
    end


   // output
   always_comb begin 
      if (hcount[10:1] < 639) 
        address_pixel_display = hcount[10:1] + 1; // account for memory delay
      else
        address_pixel_display = 0; 
      
      if(hcount == 1598 && (vcount < 479 || vcount == 524)) // 2 cycles early: 1 cycle for switch, another for reading memory
        switch = 1;
      else
        switch = 0;
      
	    //{VGA_R, VGA_G, VGA_B} = {q_pixel_display[15:11]<<3, q_pixel_display[10:6]<<3, q_pixel_display[4:0]<<3};
      // why the one-line assignment does not work?
      VGA_R = q_pixel_display[15:11] << 3;
      VGA_G = q_pixel_display[10:6] << 3;
      VGA_B = q_pixel_display[5:1] << 3;
   end
	       
endmodule

module vga_counters(
 input logic 	     clk50, reset,
 output logic [10:0] hcount,  // hcount[10:1] is pixel column
 output logic [9:0]  vcount,  // vcount[9:0] is pixel row
 output logic 	     VGA_CLK, VGA_HS, VGA_VS, VGA_BLANK_n, VGA_SYNC_n);

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
   parameter HACTIVE      = 11'd 1280,
             HFRONT_PORCH = 11'd 32,
             HSYNC        = 11'd 192,
             HBACK_PORCH  = 11'd 96,   
             HTOTAL       = HACTIVE + HFRONT_PORCH + HSYNC +
                            HBACK_PORCH; // 1600
   
   // Parameters for vcount
   parameter VACTIVE      = 10'd 480,
             VFRONT_PORCH = 10'd 10,
             VSYNC        = 10'd 2,
             VBACK_PORCH  = 10'd 33,
             VTOTAL       = VACTIVE + VFRONT_PORCH + VSYNC +
                            VBACK_PORCH; // 525

   logic endOfLine;
   
   always_ff @(posedge clk50 or posedge reset)
     if (reset)          hcount <= 0;
     else if (endOfLine) hcount <= 0;
     else  	         hcount <= hcount + 11'd 1;

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
   // 101 0000 0000  1280	       01 1110 0000  480
   // 110 0011 1111  1599	       10 0000 1100  524
   assign VGA_BLANK_n = !( hcount[10] & (hcount[9] | hcount[8]) ) &
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
