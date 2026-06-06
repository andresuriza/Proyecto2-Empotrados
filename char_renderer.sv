// ============================================================================
//  char_renderer.sv  —  pixel coords -> cell -> font pixel -> RGB
// ----------------------------------------------------------------------------
//  Turns the VGA timing's current pixel coordinate into an on-screen colour by
//  looking up the character cell, fetching the glyph row from the font ROM and
//  selecting foreground vs background colour from the 16-colour palette.
//
//  PIPELINE (3 clk latency, runs every clk; pixel coords change every 2 clks)
//    T  : derive col/row/glyph_x/glyph_y from live timing; present text_buffer
//         Port-B read address (cell_index = row*80 + col).
//    T+1: cell word valid -> ascii/fg/bg; present font_rom address {ascii,gy}.
//    T+2: font row valid -> select pixel, index palette (combinational).
//    T+3: register RGB out. HSYNC/VSYNC/BLANK are delayed by the same 3 clks
//         via the pipeline registers so they line up with the emitted pixel.
//  Because the pixel coordinate is held stable for the full 2-clk pixel period,
//  the output is likewise held stable across the period the DAC samples it.
//
//  The 16-colour palette (classic VGA/CGA) is expanded to the DE1-SoC ADV7123
//  DAC's 8 bits per channel.
// ============================================================================

module char_renderer (
    input  logic        clk,

    // From vga_timing (live, registered there)
    input  logic [9:0]  pixel_x,
    input  logic [9:0]  pixel_y,
    input  logic        active,
    input  logic        hsync,          // active LOW
    input  logic        vsync,          // active LOW

    // text_buffer Port B (read-only)
    output logic [11:0] cell_addr,      // -> text_buffer.b_addr
    input  logic [15:0] cell_data,      // <- text_buffer.b_rdata (valid T+1)

    // font_rom
    output logic [11:0] font_addr,      // -> font_rom.addr
    input  logic [7:0]  font_row,       // <- font_rom.data (valid T+2)

    // VGA pixel outputs (registered, T+3)
    output logic [7:0]  vga_r,
    output logic [7:0]  vga_g,
    output logic [7:0]  vga_b,
    output logic        vga_hs,         // active LOW
    output logic        vga_vs,         // active LOW
    output logic        vga_blank_n     // high in visible region
);

    localparam int COLS = 80;

    // ── Stage T : address generation from live pixel coordinates ─────────────
    logic [6:0] col;                    // pixel_x / 8  (0..79 in visible area)
    logic [4:0] row;                    // pixel_y / 16 (0..29 in visible area)

    assign col = pixel_x[9:3];
    assign row = pixel_y[9:4];
    assign cell_addr = (row * COLS) + col;   // cell index 0..2399

    // ── Stage T+1 registers (align with cell_data) ───────────────────────────
    logic [2:0] gx_s1;
    logic [3:0] gy_s1;
    logic       act_s1, hs_s1, vs_s1;

    always_ff @(posedge clk) begin
        gx_s1  <= pixel_x[2:0];
        gy_s1  <= pixel_y[3:0];
        act_s1 <= active;
        hs_s1  <= hsync;
        vs_s1  <= vsync;
    end

    // Font lookup address: char*16 + row-in-glyph. cell_data valid this cycle.
    assign font_addr = {cell_data[7:0], gy_s1};

    // ── Stage T+2 registers (align with font_row) ────────────────────────────
    logic [3:0] fg_s2, bg_s2;
    logic [2:0] gx_s2;
    logic       act_s2, hs_s2, vs_s2;

    always_ff @(posedge clk) begin
        fg_s2  <= cell_data[11:8];
        bg_s2  <= cell_data[15:12];
        gx_s2  <= gx_s1;
        act_s2 <= act_s1;
        hs_s2  <= hs_s1;
        vs_s2  <= vs_s1;
    end

    // ── Stage T+2 combinational : pixel select + palette lookup ──────────────
    logic        pixel_on;
    logic [3:0]  color_idx;
    logic [23:0] rgb;

    // MSB of the font row is the leftmost pixel on screen.
    assign pixel_on  = font_row[3'd7 - gx_s2];
    assign color_idx = pixel_on ? fg_s2 : bg_s2;

    // 16-colour VGA/CGA palette -> {R[7:0], G[7:0], B[7:0]}
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

    // Outside the visible region the DAC must see black (blanking).
    assign rgb = act_s2 ? palette(color_idx) : 24'h00_00_00;

    // ── Stage T+3 : output registers (RGB and sync aligned) ──────────────────
    always_ff @(posedge clk) begin
        vga_r       <= rgb[23:16];
        vga_g       <= rgb[15:8];
        vga_b       <= rgb[7:0];
        vga_hs      <= hs_s2;
        vga_vs      <= vs_s2;
        vga_blank_n <= act_s2;
    end

endmodule
