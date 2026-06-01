module de1_soc_pintest (
    input  logic        CLOCK_50,
    input  logic [3:0]  KEY,
    input  logic [9:0]  SW,
    output logic [6:0]  HEX0,
    output logic [6:0]  HEX1,
    output logic [6:0]  HEX2,
    output logic [6:0]  HEX3,
    output logic [6:0]  HEX4,
    output logic [6:0]  HEX5,
    output logic [9:0]  LEDR
);

// Turn ALL segments ON (active low = 0 turns segment on)
assign HEX0 = 7'b0000000;
assign HEX1 = 7'b0000000;
assign HEX2 = 7'b0000000;
assign HEX3 = 7'b0000000;
assign HEX4 = 7'b0000000;
assign HEX5 = 7'b0000000;

// Mirror switches to LEDs
assign LEDR = SW;

endmodule