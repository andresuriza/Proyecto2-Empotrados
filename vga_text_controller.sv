// ============================================================================
//  vga_text_controller.sv  —  TOP: Avalon-MM slave + VGA text renderer
// ----------------------------------------------------------------------------
//  Custom VGA text-display controller for the DE1-SoC (Cyclone V), to be added
//  as an Avalon-MM component in Platform Designer. The processor (Nios II / ARM
//  HPS) writes already-formatted text — song title/artist/album/time, the track
//  indicator ("3 / 10") and the filter status ("FILTER: LOWPASS") — into the
//  character buffer. The hardware ONLY renders the buffer to a 640x480@60 VGA
//  screen; it parses nothing. Software owns the entire screen layout.
//
//  ────────────────────────────────────────────────────────────────────────
//  SOFTWARE-VISIBLE INTERFACE  (read this, SW team)
//  ────────────────────────────────────────────────────────────────────────
//  The slave is a PURE TEXT BUFFER — there are no control/status registers.
//
//    * Word-addressed slave (Platform Designer: addressUnits = WORDS).
//      avl_address is the CELL INDEX directly: 12 bits, range 0..2399.
//          cell_index = row * 80 + col      (col 0..79, row 0..29)
//      Byte offset (if your driver uses byte addresses) = cell_index * 4.
//      The region rounds up to 4096 words = 16 KB of address space.
//
//    * One 16-bit character cell per 32-bit word:
//
//          bit 15 14 13 12 | 11 10  9  8 |  7  6  5  4  3  2  1  0
//               \--- bg ---/  \--- fg ---/  \-------- ASCII --------/
//
//          [ 7:0]  printable ASCII code (0x20..0x7E); other codes -> blank
//          [11:8]  foreground palette index (0..15)
//          [15:12] background palette index (0..15)
//          [31:16] unused — reads back 0, ignored on write
//
//    * 16-colour palette index -> colour (classic VGA/CGA):
//          0 black     4 red       8  dark gray    12 bright red
//          1 blue      5 magenta   9  bright blue   13 bright magenta
//          2 green     6 brown     10 bright green  14 yellow
//          3 cyan      7 lt gray   11 bright cyan   15 white
//
//    * byteenable[1:0] gate the two bytes of the cell (write just the ASCII
//      byte, or just the attribute byte, if desired). byteenable[3:2] ignored.
//    * Reads have a fixed latency of 1 clock; no waitrequest.
//
//  Example: put 'A' (0x41) white-on-blue at row 2, col 5:
//      cell = {4'h1 /*bg blue*/, 4'hF /*fg white*/, 8'h41};  // 0x1F41
//      write cell to address (2*80 + 5) = 165.
//  ────────────────────────────────────────────────────────────────────────
//
//  RESET: synchronous, active-high (Platform Designer reset sink default).
//  CLOCK: single 50 MHz domain; an internal /2 enable drives the 25 MHz pixel
//  pipeline and the VGA_CLK output (no clock-domain crossing).
// ============================================================================

module vga_text_controller (
    // ── Clock & reset ────────────────────────────────────────────────────────
    input  logic        clk,            // 50 MHz (CLOCK_50)
    input  logic        reset,          // synchronous, active-high

    // ── Avalon-MM slave (text buffer) ────────────────────────────────────────
    input  logic [11:0] avl_address,    // cell index (word-addressed)
    input  logic        avl_read,
    output logic [31:0] avl_readdata,
    input  logic        avl_write,
    input  logic [31:0] avl_writedata,
    input  logic [3:0]  avl_byteenable,

    // ── VGA conduit (to ADV7123 DAC + HSYNC/VSYNC) ───────────────────────────
    output logic [7:0]  vga_r,
    output logic [7:0]  vga_g,
    output logic [7:0]  vga_b,
    output logic        vga_hs,         // active LOW
    output logic        vga_vs,         // active LOW
    output logic        vga_blank_n,    // active video (high in visible region)
    output logic        vga_sync_n,     // composite sync — unused, tied low
    output logic        vga_clk         // 25 MHz pixel clock to DAC
);

    // ── VGA timing generator ─────────────────────────────────────────────────
    logic        pix_en;                // unused at top (pipeline runs every clk)
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

    // ── Character buffer (true dual-port M10K) ───────────────────────────────
    // Port A : Avalon side.  Port B : renderer side.
    logic [11:0] b_addr;
    logic [15:0] b_rdata;
    logic [15:0] a_rdata;

    text_buffer u_buffer (
        .clk      (clk),
        // Port A — Avalon
        .a_addr   (avl_address),
        .a_we     (avl_write),
        .a_byteen (avl_byteenable[1:0]),
        .a_wdata  (avl_writedata[15:0]),
        .a_rdata  (a_rdata),
        // Port B — renderer
        .b_addr   (b_addr),
        .b_rdata  (b_rdata)
    );

    // Avalon read data: cell in the low half-word, upper half reads 0.
    // RAM read latency is 1 clock -> matches a fixed-latency-1 Avalon slave.
    assign avl_readdata = {16'h0000, a_rdata};

    // ── Font ROM ─────────────────────────────────────────────────────────────
    logic [11:0] font_addr;
    logic [7:0]  font_row;

    font_rom u_font (
        .clk  (clk),
        .addr (font_addr),
        .data (font_row)
    );

    // ── Renderer : coords -> cell -> glyph pixel -> RGB ──────────────────────
    char_renderer u_render (
        .clk         (clk),
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
