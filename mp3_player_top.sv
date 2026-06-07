module mp3_player_top (
    // ── Clocks ─────────────────────────────────────
    input  logic        CLOCK_50,

    // ── HPS DDR3 ───────────────────────────────────
    output logic [12:0] HPS_DDR3_ADDR,
    output logic [2:0]  HPS_DDR3_BA,
    output logic        HPS_DDR3_CAS_N,
    output logic        HPS_DDR3_CKE,
    output logic        HPS_DDR3_CK_N,
    output logic        HPS_DDR3_CK_P,
    output logic        HPS_DDR3_CS_N,
    output logic        HPS_DDR3_DM,
    inout  wire  [7:0]  HPS_DDR3_DQ,
    inout  wire         HPS_DDR3_DQS_N,
    inout  wire         HPS_DDR3_DQS_P,
    output logic        HPS_DDR3_ODT,
    output logic        HPS_DDR3_RAS_N,
    output logic        HPS_DDR3_RESET_N,
    input  logic        HPS_DDR3_RZQ,
    output logic        HPS_DDR3_WE_N,

    // ── HPS SD Card ────────────────────────────────
    output logic        HPS_SD_CLK,
    inout  wire         HPS_SD_CMD,
    inout  wire  [3:0]  HPS_SD_DATA,

    // ── HPS I2C ────────────────────────────────────
    inout  wire         HPS_I2C1_SCLK,
    inout  wire         HPS_I2C1_SDAT,

    // ── FPGA I2C (audio codec config) ─────────────
    inout  wire         FPGA_I2C_SDAT,
    output logic        FPGA_I2C_SCLK,

    // ── HPS UART ───────────────────────────────────
    input  logic        HPS_UART_RX,
    output logic        HPS_UART_TX,
    input  logic        HPS_UART_CTS,
    output logic        HPS_UART_RTS,

    // ── HPS USB ────────────────────────────────────
    input  logic        HPS_USB_CLKOUT,
    inout  wire  [7:0]  HPS_USB_DATA,
    input  logic        HPS_USB_DIR,
    input  logic        HPS_USB_NXT,
    output logic        HPS_USB_STP,

    // ── Audio (WM8731) ─────────────────────────────
    input  logic        AUD_ADCDAT,
    inout  wire         AUD_BCLK,
    inout  wire         AUD_DACLRCK,
    inout  wire         AUD_ADCLRCK,
    output logic        AUD_DACDAT,
    output logic        AUD_XCK,

    // ── VGA ────────────────────────────────────────
    output logic [7:0]  VGA_R,
    output logic [7:0]  VGA_G,
    output logic [7:0]  VGA_B,
    output logic        VGA_HS,
    output logic        VGA_VS,
    output logic        VGA_BLANK_N,
    output logic        VGA_SYNC_N,
    output logic        VGA_CLK,

    // ── 7-Segment Displays ─────────────────────────
    output logic [6:0]  HEX0,
    output logic [6:0]  HEX1,
    output logic [6:0]  HEX2,
    output logic [6:0]  HEX3,
    output logic [6:0]  HEX4,
    output logic [6:0]  HEX5,

    // ── Buttons & Switches ─────────────────────────
    input  logic [3:0]  KEY,
    input  logic [9:0]  SW
);

// ── VGA: manejada por el vga_text_controller dentro del qsys ──
// El conduit exportado se conecta a los pines en la instancia hps u0 (mas abajo).

// ── PLL: genera 12.288 MHz para el codec WM8731 ─────────────
logic pll_locked;

audio_pll pll_inst (
    .inclk0 (CLOCK_50),
    .c0     (AUD_XCK),
    .locked (pll_locked)
);

// ── Internal signals ────────────────────────────────
logic        sys_timer_ext;           // sys_timer timeout pulse (unused at top)
logic        h2f_mpu_evento;
logic [1:0]  h2f_mpu_standbywfe;
logic [1:0]  h2f_mpu_standbywfi;

logic [27:0] test_lo;
logic [13:0] test_hi;

assign test_lo = 28'h0000000;
assign test_hi = 14'h0000;

// ── HPS + NIOS II subsystem ────────────────────────
hps u0 (
    // Clock & Reset
    .clk_clk                            (CLOCK_50),
    .reset_reset_n                      (1'b1),

    // DDR3
    .memory_mem_a                       (HPS_DDR3_ADDR),
    .memory_mem_ba                      (HPS_DDR3_BA),
    .memory_mem_ck                      (HPS_DDR3_CK_P),
    .memory_mem_ck_n                    (HPS_DDR3_CK_N),
    .memory_mem_cke                     (HPS_DDR3_CKE),
    .memory_mem_cs_n                    (HPS_DDR3_CS_N),
    .memory_mem_ras_n                   (HPS_DDR3_RAS_N),
    .memory_mem_cas_n                   (HPS_DDR3_CAS_N),
    .memory_mem_we_n                    (HPS_DDR3_WE_N),
    .memory_mem_reset_n                 (HPS_DDR3_RESET_N),
    .memory_mem_dq                      (HPS_DDR3_DQ),
    .memory_mem_dqs                     (HPS_DDR3_DQS_P),
    .memory_mem_dqs_n                   (HPS_DDR3_DQS_N),
    .memory_mem_odt                     (HPS_DDR3_ODT),
    .memory_mem_dm                      (HPS_DDR3_DM),
    .memory_oct_rzqin                   (HPS_DDR3_RZQ),

    // SD Card
    .hps_io_hps_io_sdio_inst_CMD        (HPS_SD_CMD),
    .hps_io_hps_io_sdio_inst_D0         (HPS_SD_DATA[0]),
    .hps_io_hps_io_sdio_inst_D1         (HPS_SD_DATA[1]),
    .hps_io_hps_io_sdio_inst_CLK        (HPS_SD_CLK),
    .hps_io_hps_io_sdio_inst_D2         (HPS_SD_DATA[2]),
    .hps_io_hps_io_sdio_inst_D3         (HPS_SD_DATA[3]),

    // USB
    .hps_io_hps_io_usb1_inst_D0         (HPS_USB_DATA[0]),
    .hps_io_hps_io_usb1_inst_D1         (HPS_USB_DATA[1]),
    .hps_io_hps_io_usb1_inst_D2         (HPS_USB_DATA[2]),
    .hps_io_hps_io_usb1_inst_D3         (HPS_USB_DATA[3]),
    .hps_io_hps_io_usb1_inst_D4         (HPS_USB_DATA[4]),
    .hps_io_hps_io_usb1_inst_D5         (HPS_USB_DATA[5]),
    .hps_io_hps_io_usb1_inst_D6         (HPS_USB_DATA[6]),
    .hps_io_hps_io_usb1_inst_D7         (HPS_USB_DATA[7]),
    .hps_io_hps_io_usb1_inst_CLK        (HPS_USB_CLKOUT),
    .hps_io_hps_io_usb1_inst_STP        (HPS_USB_STP),
    .hps_io_hps_io_usb1_inst_DIR        (HPS_USB_DIR),
    .hps_io_hps_io_usb1_inst_NXT        (HPS_USB_NXT),

    // UART
    .hps_io_hps_io_uart0_inst_RX        (HPS_UART_RX),
    .hps_io_hps_io_uart0_inst_TX        (HPS_UART_TX),
    .hps_io_hps_io_uart0_inst_CTS       (HPS_UART_CTS),
    .hps_io_hps_io_uart0_inst_RTS       (HPS_UART_RTS),

    // HPS I2C
    .hps_io_hps_io_i2c0_inst_SDA        (HPS_I2C1_SDAT),
    .hps_io_hps_io_i2c0_inst_SCL        (HPS_I2C1_SCLK),

    // Audio data (WM8731 serial)
    .audio_0_external_interface_ADCDAT  (AUD_ADCDAT),
    .audio_0_external_interface_ADCLRCK (AUD_ADCLRCK),
    .audio_0_external_interface_BCLK    (AUD_BCLK),
    .audio_0_external_interface_DACDAT  (AUD_DACDAT),
    .audio_0_external_interface_DACLRCK (AUD_DACLRCK),

    // Audio codec I2C config (via FPGA fabric)
    .audio_config_SDAT                  (FPGA_I2C_SDAT),
    .audio_config_SCLK                  (FPGA_I2C_SCLK),

    // PIO — botones (activo-bajo, invierten en software de NIOS)
    .pio_key_export                     (KEY),

    // PIO — switches (solo 3 bits para filtro)
    .pio_sw_export                      (SW[2:0]),

    // PIO — displays 7-seg HEX0–HEX3
    // Layout [27:21]=HEX3 [20:14]=HEX2 [13:7]=HEX1 [6:0]=HEX0
    .pio_hex_lo_export                  ({HEX3, HEX2, HEX1, HEX0}),

    // // PIO — displays 7-seg HEX4–HEX5
    // // Layout [13:7]=HEX5 [6:0]=HEX4
    .pio_hex_hi_export                  ({HEX5, HEX4}),

    // MPU events (no usados en este diseño)
    .h2f_mpu_events_eventi              (1'b0),
    .h2f_mpu_events_evento              (h2f_mpu_evento),
    .h2f_mpu_events_standbywfe          (h2f_mpu_standbywfe),
    .h2f_mpu_events_standbywfi          (h2f_mpu_standbywfi),

    // sys_timer timeout pulse (manejado internamente por NIOS via IRQ)
    .sys_timer_ext_export               (sys_timer_ext),

    // VGA (conduit del vga_text_controller)
    .vga_r                              (VGA_R),
    .vga_g                              (VGA_G),
    .vga_b                              (VGA_B),
    .vga_hs                             (VGA_HS),
    .vga_vs                             (VGA_VS),
    .vga_blank_n                        (VGA_BLANK_N),
    .vga_sync_n                         (VGA_SYNC_N),
    .vga_clk_pixel                      (VGA_CLK)
);





endmodule
