module vga_text_controller (
    // ── Clock & reset ────────────────────────────────────────────────────────
    input  logic        clk,            // 50 MHz (clk_0.clk)
    input  logic        reset,          // synchronous, active-high (clk_0.clk_reset)

    // ── Avalon-MM slave 'avl' (word-addressed) ───────────────────────────────
    input  logic [11:0] avl_address,    // cell index (word index)
    input  logic        avl_read,
    output logic [31:0] avl_readdata,
    input  logic        avl_write,
    input  logic [31:0] avl_writedata,
    input  logic [3:0]  avl_byteenable, // present in interface; cells written as full words

    // ── VGA conduit 'vga' (to ADV7123 DAC + HSYNC/VSYNC) ─────────────────────
    output logic [7:0]  vga_r,
    output logic [7:0]  vga_g,
    output logic [7:0]  vga_b,
    output logic        vga_hs,         // active LOW
    output logic        vga_vs,         // active LOW
    output logic        vga_blank_n,    // high in visible region
    output logic        vga_sync_n,     // composite sync — unused, tied low
    output logic        vga_clk         // 25 MHz pixel clock to DAC
);

    // ── Text geometry ────────────────────────────────────────────────────────
    localparam int COLS  = 40;
    localparam int ROWS  = 8;
    localparam int CELLS = COLS * ROWS;     // 320

    // ── VGA timing generator (unchanged proven module) ───────────────────────
    logic        pix_en;
    logic [9:0]  pixel_x, pixel_y;
    logic        active, hsync, vsync;

    vga_timing u_timing (
        .clk         (clk),
        .reset       (reset),
        .pix_en      (pix_en),
        .vga_clk_out (vga_clk),
        .pixel_x     (pixel_x),
        .pixel_y     (pixel_y),
        .hsync       (hsync),
        .vsync       (vsync),
        .active      (active)
    );

    // ── Character buffer (altsyncram simple dual-port, M10K) ──────────────────
    logic [8:0]  a_addr, b_addr;
    logic        a_we;
    logic [15:0] a_wdata, b_rdata;

    assign a_addr  = avl_address[8:0];
    assign a_wdata = avl_writedata[15:0];
    assign a_we    = avl_write && (avl_address < CELLS);

    text_buffer u_buffer (
        .clk     (clk),
        .a_addr  (a_addr),
        .a_we    (a_we),
        .a_wdata (a_wdata),
        .b_addr  (b_addr),
        .b_rdata (b_rdata)
    );

    // Avalon read: write-only buffer -> return 0, registered (latency 1).
    always_ff @(posedge clk) begin
        if (reset)         avl_readdata <= 32'h0;
        else if (avl_read) avl_readdata <= 32'h0;
    end

    // ── Font ROM (altsyncram ROM, M10K, init from font.mif) ──────────────────
    logic [11:0] font_addr;
    logic [7:0]  font_row;

    font_rom u_font (
        .clk  (clk),
        .addr (font_addr),
        .data (font_row)
    );

    // ── Renderer: coords -> cell -> glyph pixel -> RGB ───────────────────────
    char_renderer #(.COLS(COLS), .ROWS(ROWS)) u_render (
        .clk         (clk),
        .reset       (reset),
        .pixel_x     (pixel_x),
        .pixel_y     (pixel_y),
        .active      (active),
        .hsync       (hsync),
        .vsync       (vsync),
        .cell_addr   (b_addr),
        .cell_data   (b_rdata),
        .font_addr   (font_addr),
        .font_row    (font_row),
        .vga_r       (vga_r),
        .vga_g       (vga_g),
        .vga_b       (vga_b),
        .vga_hs      (vga_hs),
        .vga_vs      (vga_vs),
        .vga_blank_n (vga_blank_n)
    );

    // ADV7123 composite sync-on-green is not used in RGB mode -> tie low.
    assign vga_sync_n = 1'b0;

endmodule

module text_buffer (
    input  logic        clk,
    input  logic [8:0]  a_addr,         // write address (cell index)
    input  logic        a_we,
    input  logic [15:0] a_wdata,
    input  logic [8:0]  b_addr,         // read address (cell index)
    output logic [15:0] b_rdata         // valid 1 clock after b_addr
);

    altsyncram #(
        .operation_mode                     ("DUAL_PORT"),
        .width_a                            (16),
        .widthad_a                          (9),
        .numwords_a                         (512),
        .width_b                            (16),
        .widthad_b                          (9),
        .numwords_b                         (512),
        .address_reg_b                      ("CLOCK0"),
        .outdata_reg_b                      ("UNREGISTERED"),
        .read_during_write_mode_mixed_ports ("DONT_CARE"),
        .ram_block_type                     ("M10K"),
        .intended_device_family             ("Cyclone V"),
        .power_up_uninitialized             ("FALSE"),
        .clock_enable_input_a               ("BYPASS"),
        .clock_enable_input_b               ("BYPASS"),
        .clock_enable_output_b              ("BYPASS")
    ) u_ram (
        .clock0        (clk),
        // Port A : write
        .wren_a        (a_we),
        .address_a     (a_addr),
        .data_a        (a_wdata),
        // Port B : read
        .address_b     (b_addr),
        .q_b           (b_rdata),
        // Unused ports tied off
        .aclr0         (1'b0),
        .aclr1         (1'b0),
        .addressstall_a(1'b0),
        .addressstall_b(1'b0),
        .byteena_a     (1'b1),
        .byteena_b     (1'b1),
        .clock1        (1'b1),
        .clocken0      (1'b1),
        .clocken1      (1'b1),
        .clocken2      (1'b1),
        .clocken3      (1'b1),
        .data_b        ({16{1'b1}}),
        .eccstatus     (),
        .q_a           (),
        .rden_a        (1'b1),
        .rden_b        (1'b1),
        .wren_b        (1'b0)
    );

endmodule

module font_rom (
    input  logic        clk,
    input  logic [11:0] addr,           // {ascii, glyph_y}
    output logic [7:0]  data            // valid 1 clock after addr
);

    altsyncram #(
        .operation_mode         ("ROM"),
        .width_a                (8),
        .widthad_a              (12),
        .numwords_a             (4096),
        .address_reg_a          ("CLOCK0"),
        .outdata_reg_a          ("UNREGISTERED"),
        .init_file              ("font.mif"),
        .lpm_hint               ("ENABLE_RUNTIME_MOD=NO"),
        .ram_block_type         ("M10K"),
        .intended_device_family ("Cyclone V"),
        .clock_enable_input_a   ("BYPASS"),
        .clock_enable_output_a  ("BYPASS")
    ) u_rom (
        .clock0        (clk),
        .address_a     (addr),
        .q_a           (data),
        // Unused ports tied off
        .aclr0         (1'b0),
        .aclr1         (1'b0),
        .addressstall_a(1'b0),
        .byteena_a     (1'b1),
        .clock1        (1'b1),
        .clocken0      (1'b1),
        .clocken1      (1'b1),
        .clocken2      (1'b1),
        .clocken3      (1'b1),
        .data_a        (8'hFF),
        .eccstatus     (),
        .rden_a        (1'b1),
        .wren_a        (1'b0)
    );

endmodule

module char_renderer #(
    parameter int COLS = 40,
    parameter int ROWS = 8
) (
    input  logic        clk,
    input  logic        reset,

    // From vga_timing (live)
    input  logic [9:0]  pixel_x,
    input  logic [9:0]  pixel_y,
    input  logic        active,
    input  logic        hsync,          // active LOW
    input  logic        vsync,          // active LOW

    // text_buffer Port B (read-only)
    output logic [8:0]  cell_addr,
    input  logic [15:0] cell_data,      // valid T+1

    // font_rom
    output logic [11:0] font_addr,
    input  logic [7:0]  font_row,       // valid T+2

    // VGA pixel outputs (registered, T+3)
    output logic [7:0]  vga_r,
    output logic [7:0]  vga_g,
    output logic [7:0]  vga_b,
    output logic        vga_hs,
    output logic        vga_vs,
    output logic        vga_blank_n
);

    // ── Stage T : address generation from live pixel coordinates ─────────────
    logic [6:0] col;                    // pixel_x / 8  (0..79)
    logic [4:0] row;                    // pixel_y / 16 (0..29)
    logic       in_region;

    assign col       = pixel_x[9:3];
    assign row       = pixel_y[9:4];
    assign in_region = (col < COLS) && (row < ROWS);
    assign cell_addr = (row * COLS + col);   // 0..319

    // ── Stage T+1 ────────────────────────────────────────────────────────────
    logic [2:0] gx_d1;
    logic [3:0] gy_d1;
    logic       inr_d1, act_d1, hs_d1, vs_d1;

    always_ff @(posedge clk) begin
        gx_d1  <= pixel_x[2:0];
        gy_d1  <= pixel_y[3:0];
        inr_d1 <= in_region;
        act_d1 <= active;
        hs_d1  <= hsync;
        vs_d1  <= vsync;
    end

    // cell_data valid this cycle -> form font address {ascii, glyph_y}
    assign font_addr = {cell_data[7:0], gy_d1};

    // ── Stage T+2 ────────────────────────────────────────────────────────────
    logic [3:0] fg_d2, bg_d2;
    logic [2:0] gx_d2;
    logic       inr_d2, act_d2, hs_d2, vs_d2;

    always_ff @(posedge clk) begin
        fg_d2  <= cell_data[11:8];
        bg_d2  <= cell_data[15:12];
        gx_d2  <= gx_d1;
        inr_d2 <= inr_d1;
        act_d2 <= act_d1;
        hs_d2  <= hs_d1;
        vs_d2  <= vs_d1;
    end

    // ── Stage T+2 combinational : glyph pixel + palette ──────────────────────
    logic        pixel_on;
    logic [3:0]  color_idx;
    logic [23:0] rgb;

    assign pixel_on  = font_row[3'd7 - gx_d2];        // bit 7 = leftmost
    assign color_idx = pixel_on ? fg_d2 : bg_d2;

    function automatic logic [23:0] palette(input logic [3:0] idx);
        unique case (idx)
            4'h0: palette = 24'h00_00_00; // black
            4'h1: palette = 24'h00_00_AA; // blue
            4'h2: palette = 24'h00_AA_00; // green
            4'h3: palette = 24'h00_AA_AA; // cyan
            4'h4: palette = 24'hAA_00_00; // red
            4'h5: palette = 24'hAA_00_AA; // magenta
            4'h6: palette = 24'hAA_55_00; // brown
            4'h7: palette = 24'hAA_AA_AA; // light gray
            4'h8: palette = 24'h55_55_55; // dark gray
            4'h9: palette = 24'h55_55_FF; // bright blue
            4'hA: palette = 24'h55_FF_55; // bright green
            4'hB: palette = 24'h55_FF_FF; // bright cyan
            4'hC: palette = 24'hFF_55_55; // bright red
            4'hD: palette = 24'hFF_55_FF; // bright magenta
            4'hE: palette = 24'hFF_FF_55; // yellow
            4'hF: palette = 24'hFF_FF_FF; // white
        endcase
    endfunction

    assign rgb = (act_d2 && inr_d2) ? palette(color_idx) : 24'h00_00_00;

    // ── Stage T+3 : output registers (RGB and sync aligned) ──────────────────
    always_ff @(posedge clk) begin
        if (reset) begin
            vga_r       <= 8'h0;
            vga_g       <= 8'h0;
            vga_b       <= 8'h0;
            vga_hs      <= 1'b1;
            vga_vs      <= 1'b1;
            vga_blank_n <= 1'b0;
        end else begin
            vga_r       <= rgb[23:16];
            vga_g       <= rgb[15:8];
            vga_b       <= rgb[7:0];
            vga_hs      <= hs_d2;
            vga_vs      <= vs_d2;
            vga_blank_n <= act_d2;
        end
    end

endmodule
