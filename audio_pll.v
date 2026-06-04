// PLL: 50 MHz -> 12.295 MHz (~12.288 MHz, error < 0.06%)
// Cyclone V (5CSEMA5F31C6)
module audio_pll (
    input  wire inclk0,
    output wire c0,
    output wire locked
);

altpll #(
    .intended_device_family ("Cyclone V"),
    .lpm_type               ("altpll"),
    .operation_mode         ("NORMAL"),
    .compensate_clock       ("CLK0"),
    .inclk0_input_frequency (20000),
    .clk0_multiply_by       (15),
    .clk0_divide_by         (61),
    .clk0_duty_cycle        (50),
    .clk0_phase_shift       ("0")
) pll_inst (
    .inclk   ({1'b0, inclk0}),
    .clk     ({5'b00000, c0}),
    .locked  (locked),
    .areset  (1'b0),
    .pfdena  (1'b1),
    .clkena  (6'b111111),
    .extclkena (4'b1111),
    .fbin    (1'b1),
    .clkswitch  (1'b0),
    .scanclk    (1'b0),
    .scanclkena (1'b0),
    .scandata   (1'b0),
    .activeclock  (),
    .clkbad       (),
    .clkloss      (),
    .enable0      (),
    .enable1      (),
    .extclk       (),
    .fbout        (),
    .scandataout  (),
    .scandone     (),
    .sclkout0     (),
    .sclkout1     ()
);

endmodule
