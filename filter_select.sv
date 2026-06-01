module filter_select (
    input  logic       clk,
    input  logic       reset_n,
    input  logic [2:0] sw_in,
    output logic [1:0] filter_out
);

    typedef enum logic { ARMED, LOCKED } state_t;
    state_t state, next_state;
    
    logic [1:0] next_filter;
    
    always_ff @(posedge clk or negedge reset_n) begin
        if (!reset_n) begin
            state <= ARMED;
            filter_out <= 2'b00;
        end else begin
            state <= next_state;
            filter_out <= next_filter;
        end
    end
    
    always_comb begin
        next_state = state;
        next_filter = filter_out;
        
        case (state)
            ARMED: begin
                if (sw_in != 3'b000) begin
                    next_state = LOCKED;
                    // Latch-on-first behavior with priority (0 > 1 > 2)
                    if (sw_in[0])      next_filter = 2'd1; // Bass
                    else if (sw_in[1]) next_filter = 2'd2; // Treble
                    else if (sw_in[2]) next_filter = 2'd3; // Vocal
                end else begin
                    next_filter = 2'd0; // No filter
                end
            end
            
            LOCKED: begin
                // All switches must return to zero to arm for a new selection
                if (sw_in == 3'b000) begin
                    next_state = ARMED;
                    next_filter = 2'd0;
                end
            end
        endcase
    end

endmodule