`define BGM_BEGIN 18'h0
`define BGM_END 18'h1387f
module fpga_audio(input logic        clk,
	        input logic 	   reset,

    // input from the intel audio ip module
		input left_chan_ready,
		input right_chan_ready,

    // avalon slave
		input logic [7:0]  writedata,
		input logic 	   write,
		input 		   chipselect,
		input logic [3:0]  address,

		output logic [15:0] sample_data_l,
		output logic sample_valid_l,
		output logic [15:0] sample_data_r,
		output logic sample_valid_r);

  logic [17:0] sound_begin_addresses [5:0] = '{18'h13880, 18'h17700, 18'h1b580, 18'h1dc52, 18'h1fb92, 18'h21ad2};
  logic [17:0] sound_end_addresses [5:0] = '{18'h176ff, 18'h1b57f, 18'h1dc51, 18'h1fb91, 18'h21ad1, 18'h23fd3};

  logic [17:0] sound_address;
  logic [17:0] sound_end_address;
  logic [17:0] bgm_address;
  logic [7:0] sound_data;

  logic left_busy;
  logic right_busy;

  logic bgm_playing;
  logic sfx_playing;

  //assign sample_valid_l = bgm_playing || sfx_playing;
  //assign sample_valid_r = bgm_playing || sfx_playing;

  audio_rom(sound_address, clk, sound_data);

  // 8bit to 16bit
  assign sample_data_l = sound_data << 8;
  assign sample_data_r = sound_data << 8;

  always_ff @(posedge clk) begin
    if (reset) begin
      sample_valid_l <= 0;
      sample_valid_r <= 0;
      left_busy <= 0;
      right_busy <= 0;
      sound_address <= `BGM_BEGIN;
      bgm_address <= `BGM_BEGIN;
      sound_end_address <= `BGM_END;
      bgm_playing <= 0;
      sfx_playing <= 0;
    end else if (chipselect && write) begin
       case (address) // control bgm start/stop
	       0: begin 
          bgm_playing <= writedata;
          bgm_address <= `BGM_BEGIN;
          if(!sfx_playing) begin
            sound_address <= `BGM_BEGIN;
            sound_end_address <= `BGM_END;          
          end
         end
         1: begin // start playing sound effect
          sound_address <= sound_begin_addresses[writedata];
          sound_end_address <= sound_end_addresses[writedata];
          sfx_playing <= 1;
         end
       endcase
    end

    if (bgm_playing || sfx_playing) begin
      if (left_chan_ready == 1 && right_chan_ready == 1) begin // our fpga clock is much faster than sampling rate
        if(left_busy == 0 && right_busy == 0) begin // only feed data when audio is ready
          // current sound ends
          if(sound_address >= sound_end_address) begin
            // if sound effect ends, continue playing bgm
            if(sfx_playing) begin
              sound_address <= bgm_address;
              sound_end_address <= `BGM_END;
              sfx_playing <= 0;
            // repeat bgm
            end else begin
              bgm_address <= `BGM_BEGIN;
              sound_address <= `BGM_BEGIN;
              sound_end_address <= `BGM_END;
            end
          end else begin
            sound_address <= sound_address + 1;
            if(!sfx_playing)
              bgm_address <= bgm_address + 1;
          end // if(sound_address >= sound_end_address)
        end // if(left_busy == 0 && right_busy == 0)

        left_busy <= 1;
        right_busy <= 1;
        sample_valid_l <= 1;
        sample_valid_r <= 1;
      end else if (left_chan_ready == 0 && right_chan_ready == 0) begin // wait until ready becomes 0
        left_busy <= 0;
        right_busy <= 0;
        sample_valid_l <= 0;
        sample_valid_r <= 0;  
      end
    end else begin 
      sample_valid_l <= 0;
      sample_valid_r <= 0;
    end // if (left_chan_ready == 1 && right_chan_ready == 1)
  end // if (bgm_playing || sfx_playing)
	       
endmodule


