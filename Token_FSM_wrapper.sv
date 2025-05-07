//-----------------------------------------------------------------------------
// File:        Token_FSM_wrapper.sv
// Author:      Kamala Vennela Vasireddy
//-----------------------------------------------------------------------------
// Description:
//   A wrapper for the Token_FSM.sv core module. This wrapper encapsulates
//   the thermal-emergency detection and dynamic token‐allocation logic, 
//   computing by max_tokens_act based on overrun conditions and tile activity.
//   It instantiates Token_FSM with both the CSR token value and the computed
//   active token count for use by the FSM and its integrated divider unit.
// Parameters:
//   OVERRUN_THRESHOLD   = number of consecutive cycles above usage limit
//   PERCENT_THRESHOLD   = percent of max_tokens that triggers an emergency
//-----------------------------------------------------------------------------


module Token_FSM_wrapper #(
    parameter integer OVERRUN_THRESHOLD = 15,
    parameter integer PERCENT_THRESHOLD = 90
)(
    input  logic        clock,                  // NoC clock
    input  logic        reset,                  // Active high, synchronous reset
    input  logic        packet_in,              // Received packet flag
    input  logic [31:0] packet_in_val,          // Received packet value
    output logic        packet_out,             // Sending packet flag
    output logic [31:0] packet_out_val,         // Sent packet value
    input  logic        packet_out_ready,       // FSM input: NoC ready to accept a packet
    input  logic        enable,                 // FSM turned on
    output logic [4:0]  packet_out_addr,        // Sent packet address
    input  logic        activity,               // Activity flag from the tile
    input  logic [5:0]  max_tokens,             // Raw CSR value
    input  logic [7:0]  token_counter_override, // Override token counter
    output logic [6:0]  tokens_next,            // Next token counter value
    input  logic [4:0]  packet_in_addr,         // Received packet address
    input  logic [11:0] refresh_rate_min,       // Min refresh rate
    input  logic [11:0] refresh_rate_max,       // Max refresh rate
    input  logic [4:0]  random_rate,            // Random exchange rate
    input  logic [17:0] LUT_write,              // LUT write control
    output logic [7:0]  LUT_read,               // LUT read value
    output logic [7:0]  freq_target,            // Frequency target output
    input  logic [19:0] neighbors_ID,           // Neighbor IDs
    input  logic [31:0] PM_network              // Power-management network IDs
);

    // auto‐compute width for our counter
    localparam integer OVERRUN_WIDTH = $clog2(OVERRUN_THRESHOLD + 1);

    // Internal state
    logic [OVERRUN_WIDTH-1:0] overrun_counter;
    logic                     pull_back;
    logic [5:0]               max_tokens_local;
    logic [5:0]               max_tokens_act;
    logic                     overrun_emergency;

    // Combinational logic
    always_comb begin
        // active token budget
        max_tokens_act = pull_back
                         ? max_tokens_local
                         : (activity ? max_tokens : 6'd0);

		// Compute active token allowance
        if (!activity || max_tokens == 6'd0)
            overrun_emergency = 1'b0;
        else
            overrun_emergency = enable &&
                ((tokens_next * 100) > (max_tokens * PERCENT_THRESHOLD));
    end

    // Sequential logic for counter and pull-back
    always_ff @(posedge clock or posedge reset) begin
        if (reset) begin
            overrun_counter  <= '0;
            pull_back        <= 1'b0;
            max_tokens_local <= max_tokens;
        end else begin
            // count/emergency window
            if (enable && overrun_emergency && (overrun_counter < OVERRUN_THRESHOLD))
                overrun_counter <= overrun_counter + 1;
            else if (enable)
                overrun_counter <= '0;

            // assert or release pull-back
            if (enable) begin
                pull_back <= (overrun_counter >= OVERRUN_THRESHOLD)
                             ? 1'b1
                             : (overrun_emergency ? pull_back : 1'b0);
            end

            // Halve budget when pulled back
            max_tokens_local <= (pull_back && enable && (tokens_next != 7'd0))
                                ? (max_tokens >> 1)
                                : max_tokens;
        end
    end

    // Instantiate core FSM with both CSR value and active token ports
    Token_FSM u_fsm (
        .clock                  (clock),
        .reset                  (reset),
        .packet_in              (packet_in),
        .packet_in_val          (packet_in_val),
        .packet_out             (packet_out),
        .packet_out_val         (packet_out_val),
        .packet_out_ready       (packet_out_ready),
        .enable                 (enable),
        .packet_out_addr        (packet_out_addr),
        .activity               (activity),
        .max_tokens             (max_tokens),      // CSR value
        .max_tokens_act         (max_tokens_act),  // Computed active count
        .token_counter_override (token_counter_override),
        .tokens_next            (tokens_next),
        .packet_in_addr         (packet_in_addr),
        .refresh_rate_min       (refresh_rate_min),
        .refresh_rate_max       (refresh_rate_max),
        .random_rate            (random_rate),
        .LUT_write              (LUT_write),
        .LUT_read               (LUT_read),
        .freq_target            (freq_target),
        .neighbors_ID           (neighbors_ID),
        .PM_network             (PM_network)
    );

endmodule
