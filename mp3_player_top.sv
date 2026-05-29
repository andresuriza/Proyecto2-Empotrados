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

    // ── HPS I2C (WM8731 config) ────────────────────
    inout  wire         HPS_I2C1_SCLK,
    inout  wire         HPS_I2C1_SDAT,

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

    // ── Buttons & Switches ─────────────────────────
    input  logic [3:0]  KEY,
    input  logic [9:0]  SW
);

// HPS reset output → drives FPGA logic reset
logic hps_fpga_reset_n;

// Filter select from switch priority encoder
logic [1:0] active_filter;

// 7-seg decoded values (from your timer block)
logic [6:0] hex0_val, hex1_val, hex2_val, hex3_val;

hps u0 (
    .memory_mem_a                       (HPS_DDR3_ADDR),                       //                     memory.mem_a
    .memory_mem_ba                      (HPS_DDR3_BA),                      //                           .mem_ba
    .memory_mem_ck                      (HPS_DDR3_CK_P),                      //                           .mem_ck
    .memory_mem_ck_n                    (HPS_DDR3_CK_N),                    //                           .mem_ck_n
    .memory_mem_cke                     (HPS_DDR3_CKE),                     //                           .mem_cke
    .memory_mem_cs_n                    (HPS_DDR3_CS_N),                    //                           .mem_cs_n
    .memory_mem_ras_n                   (HPS_DDR3_RAS_N),                   //                           .mem_ras_n
    .memory_mem_cas_n                   (HPS_DDR3_CAS_N),                   //                           .mem_cas_n
    .memory_mem_we_n                    (HPS_DDR3_WE_N),                    //                           .mem_we_n
    .memory_mem_reset_n                 (HPS_DDR3_RESET_N),                 //                           .mem_reset_n
    .memory_mem_dq                      (HPS_DDR3_DQ),                      //                           .mem_dq
    .memory_mem_dqs                     (HPS_DDR3_DQS_P),                     //                           .mem_dqs
    .memory_mem_dqs_n                   (HPS_DDR3_DQS_N),                  //                           .mem_dqs_n
    .memory_mem_odt                     (HPS_DDR3_ODT),                     //                           .mem_odt
    .memory_mem_dm                      (HPS_DDR3_DM),                      //                           .mem_dm
    .memory_oct_rzqin                   (HPS_DDR3_RZQ),                   //                           .oct_rzqin
    .hps_io_hps_io_sdio_inst_CMD        (HPS_SD_CMD),        //                     hps_io.hps_io_sdio_inst_CMD
    .hps_io_hps_io_sdio_inst_D0         (HPS_SD_DATA[0]),         //                           .hps_io_sdio_inst_D0
    .hps_io_hps_io_sdio_inst_D1         (HPS_SD_DATA[1]),         //                           .hps_io_sdio_inst_D1
    .hps_io_hps_io_sdio_inst_CLK        (HPS_SD_CLK),        //                           .hps_io_sdio_inst_CLK
    .hps_io_hps_io_sdio_inst_D2         (HPS_SD_DATA[2]),         //                           .hps_io_sdio_inst_D2
    .hps_io_hps_io_sdio_inst_D3         (HPS_SD_DATA[3]),         //                           .hps_io_sdio_inst_D3

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

    .hps_io_hps_io_uart0_inst_RX        (HPS_UART_RX),        //                           
    .hps_io_hps_io_uart0_inst_TX        (HPS_UART_TX),        //                           
    .hps_io_hps_io_uart0_inst_CTS       (HPS_UART_CTS),       //                           
    .hps_io_hps_io_uart0_inst_RTS       (HPS_UART_RTS),       //                           
    .hps_io_hps_io_i2c0_inst_SDA        (HPS_I2C1_SDAT),        //                           .hps_io_i2c0_inst_SDA
    .hps_io_hps_io_i2c0_inst_SCL        (HPS_I2C1_SCLK),        //                           .hps_io_i2c0_inst_SCL
    .hpf_reset_reset_n                  (hps_fpga_reset_n),                  //                  hpf_reset.reset_n
    .reset_reset_n                      (1'b1),                      //                      reset.reset_n
    .clk_clk                            (CLOCK_50),                            //                        clk.clk
    .audio_0_external_interface_ADCDAT  (AUD_ADCDAT),  // audio_0_external_interface.ADCDAT
    .audio_0_external_interface_ADCLRCK (AUD_ADCLRCK), //                           .ADCLRCK
    .audio_0_external_interface_BCLK    (AUD_BCLK),    //                           .BCLK
    .audio_0_external_interface_DACDAT  (AUD_DACDAT),  //                           .DACDAT
    .audio_0_external_interface_DACLRCK (AUD_DACLRCK), //                           .DACLRCK
    
    // ── Avalon-MM Custom Slave Bridge ─────────────
    .avalon_mm_master_waitrequest       (1'b0),
    .avalon_mm_master_readdata          (avl_readdata),
    .avalon_mm_master_readdatavalid     (avl_readdatavalid),
    .avalon_mm_master_burstcount        (),
    .avalon_mm_master_writedata         (avl_writedata),
    .avalon_mm_master_address           (avl_addr),
    .avalon_mm_master_write             (avl_write),
    .avalon_mm_master_read              (avl_read),
    .avalon_mm_master_byteenable        (),
    .avalon_mm_master_debugaccess       ()
);

logic avl_readdatavalid;
always_ff @(posedge CLOCK_50 or negedge hps_fpga_reset_n) begin
    if (!hps_fpga_reset_n)
        avl_readdatavalid <= 1'b0;
    else
        avl_readdatavalid <= avl_read;
end

// ── Switch priority encoder ─────────────────────
filter_select u_filter (
    .clk        (CLOCK_50),
    .reset_n    (hps_fpga_reset_n),
    .sw_in      (SW[2:0]),
    .filter_out (active_filter)
);

logic timer_sync_rst;

// ── 7-segment time display ──────────────────────
seg7_timer u_timer (
    .clk        (CLOCK_50),
    .reset_n    (hps_fpga_reset_n),
    .sync_reset (timer_sync_rst),
    .hex0       (HEX0),
    .hex1       (HEX1),
    .hex2       (HEX2),
    .hex3       (HEX3)
);

// ── Custom MMIO Slave ───────────────────────────
// These signals will tie to the Avalon Pipeline Bridge
// once you export it from Qsys
logic [9:0]  avl_addr;
logic        avl_read, avl_write;
logic [31:0] avl_writedata, avl_readdata;
logic        avl_waitrequest; // mmio_slave handles 1-cycle reads, can tie avl_waitrequest to 0 in Qsys if needed

mmio_slave u_mmio (
    .clk           (CLOCK_50),
    .reset_n       (hps_fpga_reset_n),
    .avl_address   (avl_addr[5:0]), // Downsizing 10-bit address bus to 6-bit internally
    .avl_read      (avl_read),
    .avl_readdata  (avl_readdata),
    .avl_write     (avl_write),
    .avl_writedata (avl_writedata),
    
    .timer_reset   (timer_sync_rst),
    .active_filter (active_filter),
    .key           (KEY),
    .sw            (SW[2:0]),
    .irq           () // Can tie to f2h_irq0 if exported
);

// ── Unused displays ────
assign HEX4 = 7'h7F;  // blank
assign HEX5 = 7'h7F;  // blank

endmodule