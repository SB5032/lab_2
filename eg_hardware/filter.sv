
module filter(
input logic clk,
input logic reset,
input logic [15:0] input_signal,
output logic [31:0] filtered_signal);	

	parameter word_width = 16;
	parameter      order =17;
	wire signed [15:0] coef [0:16];
	reg signed [15:0] delay_pipeline[0:16];          
	assign coef[0] = 208;
	assign coef[1] = 339;
	assign coef[2] = 703;
	assign coef[3] = 1296;
	assign coef[4] = 2055;
	assign coef[5] = 2864;
	assign coef[6] = 3585;
	assign coef[7] = 4083;
    assign coef[8]= 4260;
	assign coef[9] = 4083;
	assign coef[10] = 3585;
	assign coef[11] = 2864;
	assign coef[12] = 2055;
	assign coef[13] = 1296;
	assign coef[14] = 703;
	assign coef[15] = 339;
	assign coef[16] = 208;

	reg signed [31:0]product[0:16];
	reg signed [31:0]sum;
	reg signed [15:0]data_in_buf;
	reg signed [31:0]data_out;

	always_ff@(posedge clk or posedge reset)
	begin 
	  if (reset)
	   begin
	    data_in_buf<=0;
	   end
	  else begin
	    data_in_buf<=input_signal;
	   end
	end

	always_ff@(posedge clk or posedge reset)
	  begin 
	    if(reset) begin
		delay_pipeline[0]<=0;
		delay_pipeline[1]<=0;
		delay_pipeline[2]<=0;
		delay_pipeline[3]<=0;
		delay_pipeline[4]<=0;
		delay_pipeline[5]<=0;
		delay_pipeline[6]<=0;
		delay_pipeline[7]<=0;
		delay_pipeline[8]<=0;
		delay_pipeline[9]<=0;
		delay_pipeline[10]<=0;
		delay_pipeline[11]<=0;
		delay_pipeline[12]<=0;
		delay_pipeline[13]<=0;
		delay_pipeline[14]<=0;
		delay_pipeline[15]<=0;
		delay_pipeline[16]<=0;
		
	      end
	      else begin
	      delay_pipeline[0]<=data_in_buf;
	      delay_pipeline[1]<=delay_pipeline[0];
	      delay_pipeline[2]<=delay_pipeline[1];
	      delay_pipeline[3]<=delay_pipeline[2];
	      delay_pipeline[4]<=delay_pipeline[3];
	      delay_pipeline[5]<=delay_pipeline[4];
	      delay_pipeline[6]<=delay_pipeline[5];
	      delay_pipeline[7]<=delay_pipeline[6];
	      delay_pipeline[8]<=delay_pipeline[7];
	      delay_pipeline[9]<=delay_pipeline[8];
	      delay_pipeline[10]<=delay_pipeline[9];
	      delay_pipeline[11]<=delay_pipeline[10];
	      delay_pipeline[12]<=delay_pipeline[11];
	      delay_pipeline[13]<=delay_pipeline[12];
	      delay_pipeline[14]<=delay_pipeline[13];
	      delay_pipeline[15]<=delay_pipeline[14];
	      delay_pipeline[16]<=delay_pipeline[15];
	      end 
	    end

	always_ff@(posedge clk or posedge reset)
	begin
	  if (reset)begin
	  product[0]<=0;
	  product[1]<=0;
	  product[2]<=0;
	  product[3]<=0;
	  product[4]<=0;
	  product[5]<=0;
	  product[6]<=0;
	  product[7]<=0;
	  product[8]<=0;
	  product[9]<=0;
	  product[10]<=0;
	  product[11]<=0;
	  product[12]<=0;
	  product[13]<=0;
	  product[14]<=0;
	  product[15]<=0;
	  product[16]<=0;
	 
	  end
	  else begin
	  product[0]<=coef[0]*delay_pipeline[0];
	  product[1]<=coef[1]*delay_pipeline[1];
	  product[2]<=coef[2]*delay_pipeline[2];
	  product[3]<=coef[3]*delay_pipeline[3];
	  product[4]<=coef[4]*delay_pipeline[4];
	  product[5]<=coef[5]*delay_pipeline[5];
	  product[6]<=coef[6]*delay_pipeline[6];
	  product[7]<=coef[7]*delay_pipeline[7];
	  product[8]<=coef[8]*delay_pipeline[8];
	  product[9]<=coef[9]*delay_pipeline[9];
	  product[10]<=coef[10]*delay_pipeline[10];
	  product[11]<=coef[11]*delay_pipeline[11];
	  product[12]<=coef[12]*delay_pipeline[12];
	  product[13]<=coef[13]*delay_pipeline[13];
	  product[14]<=coef[14]*delay_pipeline[14];
	  product[15]<=coef[15]*delay_pipeline[15];
	  product[16]<=coef[16]*delay_pipeline[16];
	  end
	end

	always_ff@(posedge clk or posedge reset)
	begin
	  if(reset)
	  begin
	  sum<=0;
	  end
	  else begin
	  	sum<=product[0]+product[1]+product[2]+product[3]+product[4]+product[5]+product[6]+product[7]+product[8]+product[9]+product[10]+product[11]+product[12]+product[13]+product[14]+product[15]+product[16];
	  end
	end
	
	always_ff@(posedge clk or posedge reset)
	begin
	  if(reset)
	  begin
	  data_out<=0;
	  end
	  else begin
	 filtered_signal = sum;
	  end
	end
endmodule
	 
