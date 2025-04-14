
/*
 * Avalon memory-mapped peripheral that handles audio inputs
 * Utilizes a audio driver that handles Altera driver files.
 * Functions to both store 1.5 seconds of 48khz audio data into BRAM for the Avalon bus to read out
 * Also can send to software the readout the result of the detector moudule.
 * Columbia University
 */

`include "global_variables.sv"
`include "./AudioCodecDrivers/audio_driver.sv"
`include "audio_rom.v"

//`define RAM_ADDR_BITS 5'd16
//`define RAM_WORDS 16'd48000


module hex7seg(input logic  [3:0] a,
               output logic [6:0] y);

   /* Replace this comment and the code below it with your solution */
    always_comb
        case (a)        //      gfe_dcba
            4'h0:        y = 7'b100_0000;
            4'h1:        y = 7'b111_1001;
            4'h2:        y = 7'b010_0100;
            4'h3:        y = 7'b011_0000;
            4'h4:        y = 7'b001_1001;
            4'h5:        y = 7'b001_0010;
            4'h6:        y = 7'b000_0010;
            4'h7:        y = 7'b111_1000;
            4'h8:        y = 7'b000_0000;
            4'h9:        y = 7'b001_0000;
            4'hA:        y = 7'b000_1000;
            4'hB:        y = 7'b000_0011;
            4'hC:        y = 7'b100_0110;
            4'hD:        y = 7'b010_0001;
            4'hE:        y = 7'b000_0110;
            4'hF:        y = 7'b000_1110;
            default:     y = 7'b111_1111;
        endcase
endmodule

module audio_control( 
       
        // 7-segment LED displays; HEX0 is rightmost
        output logic [6:0]        HEX0, HEX1, HEX2, HEX3, HEX4, HEX5, 
        
        //Audio pin assignments
        //Used because Professor Scott Hauck and Kyle Gagner
        output logic              FPGA_I2C_SCLK,
        inout                     FPGA_I2C_SDAT,
        output logic              AUD_XCK,
        input logic               AUD_ADCLRCK,
        input logic               AUD_DACLRCK,
        input logic               AUD_BCLK,
        input logic               AUD_ADCDAT,
        output logic              AUD_DACDAT,
        
        //Driver IO ports
        input logic               clk,
        input logic               reset,
        input logic [31:0]        writedata,
        input logic               write,
        input logic               read,
        input                     chipselect,
        input logic [15:0]        address,
        output logic [31:0]       readdata,     

        
        
        //filter control
        output logic [15:0]       raw_data,
	input  logic [31:0]       filtered_signal,
        //button 
	input logic [3:0]  KEY
     
        );
    //Audio Controller
    reg [15:0]      dac_left_in;
    reg [15:0]      dac_right_in;
    wire [15:0]     adc_left_out;
    wire [15:0]     adc_right_out;
    wire advance;

    // wire advance;
    
    //Device drivers from Altera modified by Professor Scott Hauck and Kyle Gagner in Verilog
    audio_driver aDriver(
        .CLOCK_50(clk), 
        .reset(reset), 
        .dac_left(dac_left_in), 
        .dac_right(dac_right_in), 
        .adc_left(adc_left_out), 
        .adc_right(adc_right_out), 
        .advance(advance), 
        .FPGA_I2C_SCLK(FPGA_I2C_SCLK), 
        .FPGA_I2C_SDAT(FPGA_I2C_SDAT), 
        .AUD_XCK(AUD_XCK), 
        .AUD_DACLRCK(AUD_DACLRCK), 
        .AUD_ADCLRCK(AUD_ADCLRCK), 
        .AUD_BCLK(AUD_BCLK), 
        .AUD_ADCDAT(AUD_ADCDAT), 
        .AUD_DACDAT(AUD_DACDAT)
        );
 

    //Instantiate hex decoders
    logic [23:0]    hexout_buffer;
    hex7seg h5( .a(buffer[3:0]),.y(HEX5) ), // left digit
            h4( .a(KEY[3:0]),.y(HEX4) ), 
            h3( .a(sound),.y(HEX3) ), 
            h2( .a(play),.y(HEX2) ),
            h1( .a(KEY[3:0]),.y(HEX1) ),
            h0( .a(buffer[3:0]),.y(HEX0) );    

   
  // Debounce variables
wire [15:0] buffer;
    always_comb begin
        //audioInMono = (adc_right_out>>1) + (adc_left_out>>1);
    
         
       buffer=adc_right_out;
      

	
    end
wire [15:0] sound_address;
 
reg [15:0] sound_data;
wire [1:0] sound;

wire play;
wire [1:0] prev_sound;  
audio_rom(sound_address, clk, sound_data);
 


	
   
  
      always_ff @(posedge clk) begin : IOcalls
           if (advance) begin
    raw_data <= buffer;
    
    // Detect if KEY has changed
    if (sound != prev_sound) begin
    if (play==0)begin
        case (sound)
            0:begin
	   dac_left_in <= 16'd0;
           dac_right_in <= 16'd0;
           play<=0;
        end
               
            1: begin
                sound_address <= 16'd0;
                play<=1;
                
            end
            2: begin
                sound_address <= 16'd18232;
                play<=1;
            end
            // Add more cases if needed
        endcase
    end else begin
          sound_address <= sound_address + 1'b1;
  end
    end else begin
      if((sound_address!=16'd18231)&&(sound_address!=16'd46494))begin
        sound_address <= sound_address + 1'b1;
      end
    end

    // Update the previous KEY value
    prev_sound <= sound;
    if((sound_address==16'd18231)||(sound_address==16'd46494))begin
       dac_left_in <= 16'd0;
        dac_right_in <= 16'd0;
        play<=0;
     end   
    if ((sound == 0)&& (play==0))begin
	dac_left_in <= 16'd0;
        dac_right_in <= 16'd0;
        
    end else begin
	dac_left_in <= sound_data;
        dac_right_in <= sound_data;
        
    end
end
       
        if (chipselect && read) begin
            case (address)
                16'h0000 : begin
			readdata[31:0] <=  filtered_signal;
                  

                end
		16'h0001 : begin
			readdata[31:0] <=  {28'b0000000000000000000000000000,KEY};
                  
                  
		    
                end
            endcase
	    
        end
	if (chipselect && write) begin
            case (address)
                16'h0002 : begin
			sound<=writedata[1:0];
                  

                end
		
            endcase
	    
        end
      
    end
   logic [31:0]    driverReading = 31'd0;
    wire sampleBeingTaken;
    assign sampleBeingTaken = driverReading[0];
    
    //Map timer(Sample) counter output
    parameter readOutSize = 16'hffff;
    //Sample inputs/Audio passthrough

endmodule
