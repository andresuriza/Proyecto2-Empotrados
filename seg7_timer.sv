module seg7_timer (
    input  logic       clk,
    input  logic       reset_n,
    input  logic       sync_reset, // Added to reset from HPS
    output logic [6:0] hex0, // SS units
    output logic [6:0] hex1, // SS tens
    output logic [6:0] hex2, // MM units
    output logic [6:0] hex3  // MM tens
);

    // 50 MHz clock = 50,000,000 ticks per second
    localparam CLOCK_FREQ = 50_000_000;
    
    logic [25:0] tick_counter;
    logic [3:0]  ss_units;
    logic [3:0]  ss_tens;
    logic [3:0]  mm_units;
    logic [3:0]  mm_tens;
    
    always_ff @(posedge clk or negedge reset_n) begin
        if (!reset_n) begin
            tick_counter <= 0;
            ss_units <= 0;
            ss_tens  <= 0;
            mm_units <= 0;
            mm_tens  <= 0;
        end else if (sync_reset) begin
            tick_counter <= 0;
            ss_units <= 0;
            ss_tens  <= 0;
            mm_units <= 0;
            mm_tens  <= 0;
        end else begin
            if (tick_counter == CLOCK_FREQ - 1) begin
                tick_counter <= 0;
                
                if (ss_units == 9) begin
                    ss_units <= 0;
                    if (ss_tens == 5) begin
                        ss_tens <= 0;
                        if (mm_units == 9) begin
                            mm_units <= 0;
                            if (mm_tens == 5) begin
                                mm_tens <= 0; // Max out at 59:59 and wrap
                            end else begin
                                mm_tens <= mm_tens + 1;
                            end
                        end else begin
                            mm_units <= mm_units + 1;
                        end
                    end else begin
                        ss_tens <= ss_tens + 1;
                    end
                end else begin
                    ss_units <= ss_units + 1;
                end
            end else begin
                tick_counter <= tick_counter + 1;
            end
        end
    end

    // 7-Segment Decoder logic (active low typical for DE1-SoC)
    function logic [6:0] decode_7seg(input logic [3:0] val);
        case (val)
            4'h0: decode_7seg = 7'b1000000;
            4'h1: decode_7seg = 7'b1111001;
            4'h2: decode_7seg = 7'b0100100;
            4'h3: decode_7seg = 7'b0110000;
            4'h4: decode_7seg = 7'b0011001;
            4'h5: decode_7seg = 7'b0010010;
            4'h6: decode_7seg = 7'b0000010;
            4'h7: decode_7seg = 7'b1111000;
            4'h8: decode_7seg = 7'b0000000;
            4'h9: decode_7seg = 7'b0011000;
            default: decode_7seg = 7'b1111111; // Blank
        endcase
    endfunction

    assign hex0 = decode_7seg(ss_units);
    assign hex1 = decode_7seg(ss_tens);
    assign hex2 = decode_7seg(mm_units);
    assign hex3 = decode_7seg(mm_tens);

endmodule