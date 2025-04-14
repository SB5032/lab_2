/*
 * Sprite ROM seletor and color decoders as well as syncronous ROM module
 * Alex Yu
 * Columbia University
 */
 
 module sprites(
	input logic [5:0]          	n_sprite,
	input logic [9:0]          	line,
	input logic [5:0]			pixel,
	input logic                	clk,
	output logic [3:0]			color_code);
	
	logic [9:0] spr_rom_addr ;

	assign spr_rom_addr = (line<<5) + pixel;
	
	logic [3:0] spr_rom_data [42:0];
	// sprites indevidually stored in roms

	// numbers
	rom_sync #(
        .WIDTH(4),
        .WORDS(1024),
        .INIT_F("./sprites/Sprite_rom/1.txt")
    ) num_1 (
	.clk(clk),
        .addr(spr_rom_addr),
        .data(spr_rom_data[6'd1])
    );
	rom_sync #(
        .WIDTH(4),
        .WORDS(1024),
        .INIT_F("./sprites/Sprite_rom/2.txt")
    ) num_2 (
	.clk(clk),
        .addr(spr_rom_addr),
        .data(spr_rom_data[6'd2])
    );
	rom_sync #(
        .WIDTH(4),
        .WORDS(1024),
        .INIT_F("./sprites/Sprite_rom/3.txt")
    ) num_3 (
	.clk(clk),
        .addr(spr_rom_addr),
        .data(spr_rom_data[6'd3])
    );
	rom_sync #(
        .WIDTH(4),
        .WORDS(1024),
        .INIT_F("./sprites/Sprite_rom/4.txt")
    ) num_4 (
	.clk(clk),
        .addr(spr_rom_addr),
        .data(spr_rom_data[6'd4])
    );
	rom_sync #(
        .WIDTH(4),
        .WORDS(1024),
        .INIT_F("./sprites/Sprite_rom/5.txt")
    ) num_5 (
	.clk(clk),
        .addr(spr_rom_addr),
        .data(spr_rom_data[6'd5])
    );
	rom_sync #(
        .WIDTH(4),
        .WORDS(1024),
        .INIT_F("./sprites/Sprite_rom/6.txt")
    ) num_6 (
	.clk(clk),
        .addr(spr_rom_addr),
        .data(spr_rom_data[6'd6])
    );
	rom_sync #(
        .WIDTH(4),
        .WORDS(1024),
        .INIT_F("./sprites/Sprite_rom/7.txt")
    ) num_7 (
	.clk(clk),
        .addr(spr_rom_addr),
        .data(spr_rom_data[6'd7])
    );
	rom_sync #(
        .WIDTH(4),
        .WORDS(1024),
        .INIT_F("./sprites/Sprite_rom/8.txt")
    ) num_8 (
	.clk(clk),
        .addr(spr_rom_addr),
        .data(spr_rom_data[6'd8])
    );
	rom_sync #(
        .WIDTH(4),
        .WORDS(1024),
        .INIT_F("./sprites/Sprite_rom/9.txt")
    ) num_9 (
	.clk(clk),
        .addr(spr_rom_addr),
        .data(spr_rom_data[6'd9])
    );
	rom_sync #(
        .WIDTH(4),
        .WORDS(1024),
        .INIT_F("./sprites/Sprite_rom/0.txt")
    ) num_10 (
	.clk(clk),
        .addr(spr_rom_addr),
        .data(spr_rom_data[6'd10])
    );

	// Letters 
	rom_sync #(
        .WIDTH(4),
        .WORDS(1024),
        .INIT_F("./sprites/Sprite_rom/B.txt")
    ) num_11 (
	.clk(clk),
        .addr(spr_rom_addr),
        .data(spr_rom_data[6'd11])
    );
	
	rom_sync #(
        .WIDTH(4),
        .WORDS(1024),
        .INIT_F("./sprites/Sprite_rom/C.txt")
    ) num_12 (
	.clk(clk),
        .addr(spr_rom_addr),
        .data(spr_rom_data[6'd12])
    );
	rom_sync #(
        .WIDTH(4),
        .WORDS(1024),
        .INIT_F("./sprites/Sprite_rom/E.txt")
    ) num_13 (
	.clk(clk),
        .addr(spr_rom_addr),
        .data(spr_rom_data[6'd13])
    );
	rom_sync #(
        .WIDTH(4),
        .WORDS(1024),
        .INIT_F("./sprites/Sprite_rom/M.txt")
    ) num_14 (
	.clk(clk),
        .addr(spr_rom_addr),
        .data(spr_rom_data[6'd14])
    );
	rom_sync #(
        .WIDTH(4),
        .WORDS(1024),
        .INIT_F("./sprites/Sprite_rom/O.txt")
    ) num_15 (
	.clk(clk),
        .addr(spr_rom_addr),
        .data(spr_rom_data[6'd15])
    );
	
	rom_sync #(
        .WIDTH(4),
        .WORDS(1024),
        .INIT_F("./sprites/Sprite_rom/R.txt")
    ) num_16 (
	.clk(clk),
        .addr(spr_rom_addr),
        .data(spr_rom_data[6'd16])
    );
	rom_sync #(
        .WIDTH(4),
        .WORDS(1024),
        .INIT_F("./sprites/Sprite_rom/S.txt")
    ) num_17(
	.clk(clk),
        .addr(spr_rom_addr),
        .data(spr_rom_data[6'd17])
    );
	// NOTE BLOCKS
	rom_sync #( //blue
        .WIDTH(4),
        .WORDS(1024),
        .INIT_F("./sprites/Sprite_rom/E_white_32.txt")
    ) num_18 (
	.clk(clk),
        .addr(spr_rom_addr),
        .data(spr_rom_data[6'd18])
    );
	rom_sync #(
        .WIDTH(4),
        .WORDS(1024),
        .INIT_F("./sprites/Sprite_rom/U_white_32.txt")
    ) num_19 (
	.clk(clk),
        .addr(spr_rom_addr),
        .data(spr_rom_data[6'd19])
    );
	rom_sync #( //orange
        .WIDTH(4),
        .WORDS(1024),
        .INIT_F("./sprites/Sprite_rom/M_white_32.txt")
    ) num_20 (
	.clk(clk),
        .addr(spr_rom_addr),
        .data(spr_rom_data[6'd20])
    );
	rom_sync #(
        .WIDTH(4),
        .WORDS(1024),
        .INIT_F("./sprites/Sprite_rom/V_white_32.txt")
    ) num_21 (
	.clk(clk),
        .addr(spr_rom_addr),
        .data(spr_rom_data[6'd21])
    );
	rom_sync #( //pink
        .WIDTH(4),
        .WORDS(1024),
        .INIT_F("./sprites/Sprite_rom/S_white_32.txt")
    ) num_22 (
	.clk(clk),
        .addr(spr_rom_addr),
        .data(spr_rom_data[6'd22])
    );
	rom_sync #(
        .WIDTH(4),
        .WORDS(1024),
        .INIT_F("./sprites/Sprite_rom/T_white_32.txt")
    ) num_23 (
	.clk(clk),
        .addr(spr_rom_addr),
        .data(spr_rom_data[6'd23])
    );
	rom_sync #(//purple
        .WIDTH(4),
        .WORDS(1024),
        .INIT_F("./sprites/Sprite_rom/I_white_32.txt")
    ) num_24 (
	.clk(clk),
        .addr(spr_rom_addr),
        .data(spr_rom_data[6'd24])
    );
	rom_sync #(
        .WIDTH(4),
        .WORDS(1024),
        .INIT_F("./sprites/Sprite_rom/N_white_32.txt")
    ) num_25 (
	.clk(clk),
        .addr(spr_rom_addr),
        .data(spr_rom_data[6'd25])
    );
	rom_sync #(//purple
        .WIDTH(4),
        .WORDS(1024),
        .INIT_F("./sprites/Sprite_rom/A.txt")
    ) num_26 (
	.clk(clk),
        .addr(spr_rom_addr),
        .data(spr_rom_data[6'd26])
    );
	rom_sync #(
        .WIDTH(4),
        .WORDS(1024),
        .INIT_F("./sprites/Sprite_rom/X.txt")
    ) num_27 (
	.clk(clk),
        .addr(spr_rom_addr),
        .data(spr_rom_data[6'd27])
    );
        rom_sync #(
        .WIDTH(4),
        .WORDS(1024),
        .INIT_F("./sprites/Sprite_rom/bird.txt")
    ) num_28 (
	.clk(clk),
        .addr(spr_rom_addr),
        .data(spr_rom_data[6'd28])
    );
	rom_sync #(
        .WIDTH(4),
        .WORDS(1024),
        .INIT_F("./sprites/Sprite_rom/pipe1.txt")
    ) num_29 (
	.clk(clk),
        .addr(spr_rom_addr),
        .data(spr_rom_data[6'd29])
    );
	rom_sync #(
        .WIDTH(4),
        .WORDS(1024),
        .INIT_F("./sprites/Sprite_rom/pipe2.txt")
    ) num_30 (
	.clk(clk),
        .addr(spr_rom_addr),
        .data(spr_rom_data[6'd30])
    );
	rom_sync #(
        .WIDTH(4),
        .WORDS(1024),
        .INIT_F("./sprites/Sprite_rom/G_white_32.txt")
    ) num_31 (
	.clk(clk),
        .addr(spr_rom_addr),
        .data(spr_rom_data[6'd31])
    );
	rom_sync #(
	.WIDTH(4),
	.WORDS(1024),
	.INIT_F("./sprites/Sprite_rom/bird_dead.txt")
    ) num_32 (
	.clk(clk),
	.addr(spr_rom_addr),
	.data(spr_rom_data[6'd32])
    );
	rom_sync #(
	.WIDTH(4),
	.WORDS(1024),
	.INIT_F("./sprites/Sprite_rom/A_w.txt")
    ) num_33 (
	.clk(clk),
	.addr(spr_rom_addr),
	.data(spr_rom_data[6'd33])
    );
	rom_sync #(
	.WIDTH(4),
	.WORDS(1024),
	.INIT_F("./sprites/Sprite_rom/R_w.txt")
    ) num_34 (
	.clk(clk),
	.addr(spr_rom_addr),
	.data(spr_rom_data[6'd34])
    );
	rom_sync #(
	.WIDTH(4),
	.WORDS(1024),
	.INIT_F("./sprites/Sprite_rom/Y.txt")
    ) num_35 (
	.clk(clk),
	.addr(spr_rom_addr),
	.data(spr_rom_data[6'd35])
    );
	rom_sync #(
	.WIDTH(4),
	.WORDS(1024),
	.INIT_F("./sprites/Sprite_rom/D.txt")
    ) num_36 (
	.clk(clk),
	.addr(spr_rom_addr),
	.data(spr_rom_data[6'd36])
    );
	rom_sync #(
	.WIDTH(4),
	.WORDS(1024),
	.INIT_F("./sprites/Sprite_rom/H.txt")
    ) num_37 (
	.clk(clk),
	.addr(spr_rom_addr),
	.data(spr_rom_data[6'd37])
    );
	rom_sync #(
	.WIDTH(4),
	.WORDS(1024),
	.INIT_F("./sprites/Sprite_rom/O_w.txt")
    ) num_38 (
	.clk(clk),
	.addr(spr_rom_addr),
	.data(spr_rom_data[6'd38])
    );

	always_comb begin
		case (n_sprite)
			// numbers
			6'd1  : color_code = spr_rom_data[6'd1];  // 1
			6'd2  : color_code = spr_rom_data[6'd2];  // 2 
			6'd3  : color_code = spr_rom_data[6'd3];  // 3
			6'd4  : color_code = spr_rom_data[6'd4];  // 4
			6'd5  : color_code = spr_rom_data[6'd5];  // 5
			6'd6  : color_code = spr_rom_data[6'd6];  // 6
			6'd7  : color_code = spr_rom_data[6'd7];  // 7
			6'd8  : color_code = spr_rom_data[6'd8];  // 8
			6'd9  : color_code = spr_rom_data[6'd9];  // 9
			6'd10 : color_code = spr_rom_data[6'd10]; // 10
			// letters 
			6'd11 : color_code = spr_rom_data[6'd11]; // B
			6'd12 : color_code = spr_rom_data[6'd12]; // C
			6'd13 : color_code = spr_rom_data[6'd13]; // E
			6'd14 : color_code = spr_rom_data[6'd14]; // M
			6'd15 : color_code = spr_rom_data[6'd15]; // O 
			6'd16 : color_code = spr_rom_data[6'd16]; // R
			6'd17 : color_code = spr_rom_data[6'd17]; // S
			// notes
			6'd18 : color_code = spr_rom_data[6'd18]; // E w
			6'd19 : color_code = spr_rom_data[6'd19]; // U w
			6'd20 : color_code = spr_rom_data[6'd20]; // M w
			6'd21 : color_code = spr_rom_data[6'd21]; // V w
			6'd22 : color_code = spr_rom_data[6'd22]; // S w
			6'd23 : color_code = spr_rom_data[6'd23]; // T w
			6'd24 : color_code = spr_rom_data[6'd24]; // I w
			6'd25 : color_code = spr_rom_data[6'd25]; // N w
			// A/X added later
			6'd26 : color_code = spr_rom_data[6'd26]; // A
			6'd27 : color_code = spr_rom_data[6'd27]; // X
			// 6'd28 : color_code = spr_rom_data[6'd28]; // BACKGROUND
			6'd28 : color_code = spr_rom_data[6'd28]; // BIRD
			6'd29 : color_code = spr_rom_data[6'd29]; // PIPE1
			6'd30 : color_code = spr_rom_data[6'd30]; // PIPE2
			6'd31 : color_code = spr_rom_data[6'd31]; // G w
			6'd32 : color_code = spr_rom_data[6'd32]; // dead bird
			6'd33 : color_code = spr_rom_data[6'd33]; // A_w
			6'd34 : color_code = spr_rom_data[6'd34]; // R_w
			6'd35 : color_code = spr_rom_data[6'd35]; // Y
			6'd36 : color_code = spr_rom_data[6'd36]; // D
			6'd37 : color_code = spr_rom_data[6'd37]; // H
			6'd38 : color_code = spr_rom_data[6'd38]; // O_w
			default : begin
				color_code <= 4'h0;
			end
		endcase
	end
endmodule

module sprite_color_pallete(
	input logic [3:0] 	color_code_o,
	input logic [3:0] 	color_code_e,
	input logic 		select,
	output logic [23:0]	color
	);
	logic [3:0] color_code;
	assign color_code = (select) ? color_code_o : color_code_e;
	always_comb begin
		case(color_code)
			//sprite colors
			4'h0 : color = 24'hFFFFFF;
			4'h1 : color = 24'hFFFFFF;
			4'h2 : color = 24'h646361;
			4'h3 : color = 24'h0a0808;
			4'h4 : color = 24'hfdc603;
			4'h5 : color = 24'h5f2a04;
			4'h6 : color = 24'hea7e02;
			4'h7 : color = 24'hdab6ff;
			4'h8 : color = 24'h101f06;
			4'h9 : color = 24'h7ed012;
			4'ha : color = 24'h0bad01;
			default : color = 24'h000000;
		endcase
	end
endmodule

module rom_sync #(
    parameter WIDTH=4,
    parameter WORDS=1024,
    parameter INIT_F="",
    parameter ADDRW=10
    ) (
    input wire logic clk,
    input wire logic [ADDRW-1:0] addr,
    output     logic [WIDTH-1:0] data
    );

    logic [WIDTH-1:0] memory [WORDS];

    initial begin
        if (INIT_F != 0) begin
            $display("Creating rom_sync from init file '%s'.", INIT_F);
            $readmemh(INIT_F, memory);
        end
    end

    always_ff @(posedge clk) begin
        data <= memory[addr];
    end
endmodule
