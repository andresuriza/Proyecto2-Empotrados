// ============================================================================
//  vga_timing.sv  —  VGA 640x480 @ 60 Hz timing generator
// ----------------------------------------------------------------------------
//  Part of the VGA text-display controller for the DE1-SoC (Cyclone V) audio
//  player. Generates horizontal/vertical counters, HSYNC/VSYNC, the active-
//  video strobe and the current pixel coordinates.
//
//  CLOCKING
//    Single 50 MHz domain (CLOCK_50). A 1-bit divider produces a 25 MHz pixel
//    enable (pix_en, asserted every other clock) so the whole renderer lives in
//    one clock domain — no clock-domain crossing. The divider bit is also
//    exported as a 25 MHz divided clock (vga_clk_out) intended for the VGA DAC
//    clock pin (VGA_CLK on the ADV7123).
//
//    25 MHz from /2 of 50 MHz gives 800*525*25e6/(800*525) = 59.52 Hz refresh
//    — within tolerance of every DE1-SoC monitor. For an exact 25.175 MHz dot
//    clock (59.94 Hz) use an Altera PLL instead of the /2 divider; a commented
//    template is provided at the bottom of this file.
//
//  TIMING (exact values, do not change)
//    Horizontal: visible 640, front 16, sync 96, back 48, total 800  (HSYNC low)
//    Vertical  : visible 480, front 10, sync  2, back 33, total 525  (VSYNC low)
//
//  RESET: synchronous, active-high (per project spec / Platform Designer reset
//  sink). NOTE: this differs from the repo's existing active-low async reset_n
//  (mmio_slave.sv, seg7_timer.sv); an integrating wrapper can invert as needed.
// ============================================================================

module vga_timing (
    input  logic        clk,            // 50 MHz reference clock
    input  logic        reset,          // synchronous, active-high

    output logic        pix_en,         // 25 MHz pixel-clock enable (1 of every 2 clks)
    output logic        vga_clk_out,    // 25 MHz divided clock for the DAC pin

    output logic [9:0]  pixel_x,        // 0..639 valid only while 'active'
    output logic [9:0]  pixel_y,        // 0..479 valid only while 'active'
    output logic        hsync,          // active LOW
    output logic        vsync,          // active LOW
    output logic        active          // high during 640x480 visible region
);

    // ── Horizontal timing constants (in pixels) ──────────────────────────────
    localparam int H_VISIBLE = 640;
    localparam int H_FRONT   = 16;
    localparam int H_SYNC    = 96;
    localparam int H_BACK    = 48;
    localparam int H_TOTAL   = H_VISIBLE + H_FRONT + H_SYNC + H_BACK; // 800

    localparam int H_SYNC_START = H_VISIBLE + H_FRONT;                // 656
    localparam int H_SYNC_END   = H_SYNC_START + H_SYNC;              // 752

    // ── Vertical timing constants (in lines) ─────────────────────────────────
    localparam int V_VISIBLE = 480;
    localparam int V_FRONT   = 10;
    localparam int V_SYNC    = 2;
    localparam int V_BACK    = 33;
    localparam int V_TOTAL   = V_VISIBLE + V_FRONT + V_SYNC + V_BACK; // 525

    localparam int V_SYNC_START = V_VISIBLE + V_FRONT;                // 490
    localparam int V_SYNC_END   = V_SYNC_START + V_SYNC;              // 492

    // ── 25 MHz pixel-clock enable (÷2 of 50 MHz) ─────────────────────────────
    // clk_div toggles every clock; pix_en is high on the cycles where a new
    // pixel is produced. vga_clk_out is the divided clock for the DAC pin.
    logic clk_div;

    always_ff @(posedge clk) begin
        if (reset) clk_div <= 1'b0;
        else       clk_div <= ~clk_div;
    end

    assign pix_en      = clk_div;       // advance the pixel pipeline on these cycles
    assign vga_clk_out = clk_div;       // 25 MHz square wave to VGA_CLK

    // ── Horizontal / vertical counters (advance only on pix_en) ──────────────
    logic [9:0] hcount;
    logic [9:0] vcount;

    always_ff @(posedge clk) begin
        if (reset) begin
            hcount <= 10'd0;
            vcount <= 10'd0;
        end else if (pix_en) begin
            if (hcount == H_TOTAL - 1) begin
                hcount <= 10'd0;
                if (vcount == V_TOTAL - 1)
                    vcount <= 10'd0;
                else
                    vcount <= vcount + 10'd1;
            end else begin
                hcount <= hcount + 10'd1;
            end
        end
    end

    // ── Sync / active outputs (combinational from the counters) ──────────────
    assign hsync   = ~((hcount >= H_SYNC_START) && (hcount < H_SYNC_END)); // active LOW
    assign vsync   = ~((vcount >= V_SYNC_START) && (vcount < V_SYNC_END)); // active LOW
    assign active  = (hcount < H_VISIBLE) && (vcount < V_VISIBLE);

    assign pixel_x = hcount;            // meaningful only while 'active'
    assign pixel_y = vcount;

    // ========================================================================
    //  OPTIONAL: exact 25.175 MHz dot clock via PLL (59.94 Hz, true VGA).
    //  Replace the ÷2 divider above with a PLL-generated pixel clock and run
    //  the counters on that clock directly (pix_en tied high). Example:
    //
    //    logic pixel_clk;                 // 25.175 MHz from PLL
    //    vga_pll u_pll (
    //        .refclk   (clk),             // 50 MHz in
    //        .rst      (reset),
    //        .outclk_0 (pixel_clk)        // 25.175 MHz out
    //    );
    //    // then: always_ff @(posedge pixel_clk) ... ; assign pix_en = 1'b1;
    //    //       assign vga_clk_out = pixel_clk;
    //
    //  Generate 'vga_pll' with the Altera PLL IP (ALTPLL/Cyclone V PLL) in the
    //  Quartus IP Catalog. Kept commented to avoid an unused PLL in this build.
    // ========================================================================

endmodule
