module linebuffer(
	clk,
  reset,
	switch,
	address_tile_display,
	address_pixel_display,
	address_tile_draw,
	address_pixel_draw,

	data_tile_display,
	data_pixel_display,
  data_tile_draw,
  data_pixel_draw,

  wren_tile_display,
  wren_pixel_display,
  wren_tile_draw,
  wren_pixel_draw,

  q_tile_display,
  q_pixel_display,
  q_tile_draw,
  q_pixel_draw
);
	input	clk;
  input reset;
  input switch;	// switch drawing and displaying buffer
  input	[5:0]  address_tile_display;
	input	[9:0]  address_pixel_display;
	input	[5:0]  address_tile_draw;
	input	[9:0]  address_pixel_draw;

	input	[255:0]  data_tile_display;
  input	[15:0]  data_pixel_display;
  input	[255:0]  data_tile_draw;
  input	[15:0]  data_pixel_draw;
	
  
	input	  wren_tile_display;
	input	  wren_pixel_display;
  input	  wren_tile_draw;
	input	  wren_pixel_draw;

	output	[255:0]  q_tile_display;
  output	[15:0]  q_pixel_display;
  output	[255:0]  q_tile_draw;
  output	[15:0]  q_pixel_draw;

  wire [5:0] address_tile[1:0];
  wire [9:0] address_pixel[1:0];

  wire [255:0] data_tile[1:0];
  wire [16:0] data_pixel[1:0];

  wire wren_tile[1:0];
  wire wren_pixel[1:0];

  wire [255:0] q_tile[1:0];
  wire [16:0] q_pixel[1:0];

  logic display_index;
  logic draw_index;

  linebuffer_ram ram0(address_tile[0], address_pixel[0], clk, data_tile[0], data_pixel[0], wren_tile[0], wren_pixel[0], q_tile[0], q_pixel[0]);
  linebuffer_ram ram1(address_tile[1], address_pixel[1], clk, data_tile[1], data_pixel[1], wren_tile[1], wren_pixel[1], q_tile[1], q_pixel[1]);

  always_ff @(posedge clk) begin
    if(reset) begin
      draw_index <= 0;
      display_index <= 1;    
    end else if (switch) begin // currently only accept single-cycle pulse on switch
      draw_index <= display_index;
      display_index <= draw_index;    
    end
  end

  always_comb begin
      address_tile[display_index] = address_tile_display;
      address_pixel[display_index] = address_pixel_display;
      address_tile[draw_index] = address_tile_draw;
      address_pixel[draw_index] = address_pixel_draw;

      data_tile[display_index] = data_tile_display;
      data_pixel[display_index] = data_pixel_display;
      data_tile[draw_index] = data_tile_draw;
      data_pixel[draw_index] = data_pixel_draw;

      wren_tile[display_index] = wren_tile_display;
      wren_pixel[display_index] = wren_pixel_display;
      wren_tile[draw_index] = wren_tile_draw;
      wren_pixel[draw_index] = wren_pixel_draw;

      q_tile_display = q_tile[display_index];
      q_pixel_display = q_pixel[display_index];
      q_tile_draw = q_tile[draw_index];
      q_pixel_draw = q_pixel[draw_index];
  end
  
endmodule
