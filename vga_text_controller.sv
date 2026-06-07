// ============================================================================
//  vga_text_controller.sv  —  SIMPLE memory-free VGA test controller
// ----------------------------------------------------------------------------
//  DE1-SoC (Cyclone V 5CSEMA5F31). Avalon-MM slave + VGA conduit, instantiated
//  in Platform Designer as component "vga_text_controller".
//
//  This replaces the earlier text-mode design (font ROM + char buffer), whose
//  on-chip memories would not infer as M10K on this device and overflowed the
//  LABs. This version uses NO memory at all — pixel colour is purely
//  combinational from two control registers and the h/v counters, so it fits
//  trivially and lets software prove the VGA chain end-to-end.
//
//  The MODULE NAME, the Avalon-MM slave interface (ports/widths), the clock and
//  the (synchronous, active-high) reset, and the VGA conduit are UNCHANGED from
//  the original so the existing hps.qsys / vga_text_controller_hw.tcl
//  instantiation still matches. Only the internals changed.
//
//  ────────────────────────────────────────────────────────────────────────
//  SOFTWARE-VISIBLE REGISTER MAP  (Avalon slave 'avl', addressUnits = WORDS)
//  ────────────────────────────────────────────────────────────────────────
//  Base address (ARM, behind HPS-to-FPGA Lightweight bridge) = 0xFF210000.
//  avl_address is a WORD index, so word N is at byte offset N*4.
//
//    word 0  (byte 0x0)  BG_COLOR : [23:0] = {R[7:0], G[7:0], B[7:0]}
//                                   background / solid colour. [31:24] ignored.
//    word 1  (byte 0x4)  MODE     : [1:0]
//                                     0 = solid background (BG_COLOR)
//                                     1 = 8 vertical colour bars (VGA test bars)
//                                     2 = checkerboard (pixel_x[5] ^ pixel_y[5])
//                                     3 = (reserved) -> solid background
//    all other words read back 0 and ignore writes.
//
//  Reads have a fixed latency of 1 clock (no waitrequest). RGB is forced to 0
//  outside the visible region (blanking).
// ============================================================================

module vga_text_controller (
    // ── Clock & reset ────────────────────────────────────────────────────────
    input  logic        clk,            // 50 MHz (clk_0.clk)
    input  logic        reset,          // synchronous, active-high (clk_0.clk_reset)

    // ── Avalon-MM slave 'avl' (word-addressed) ───────────────────────────────
    input  logic [11:0] avl_address,    // word index
    input  logic        avl_read,
    output logic [31:0] avl_readdata,
    input  logic        avl_write,
    input  logic [31:0] avl_writedata,
    input  logic [3:0]  avl_byteenable, // present in the interface; control regs
                                        // are written as full 32-bit words

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

    // ── Register addresses (word indices) ────────────────────────────────────
    localparam logic [11:0] REG_BG_COLOR = 12'd0;   // byte offset 0x0
    localparam logic [11:0] REG_MODE     = 12'd1;   // byte offset 0x4

    // ── VGA timing generator (unchanged module) ──────────────────────────────
    logic        pix_en;                // 25 MHz enable (used inside vga_timing)
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

    // ── Control registers ────────────────────────────────────────────────────
    logic [23:0] bg_color;
    logic [1:0]  mode;

    always_ff @(posedge clk) begin
        if (reset) begin
            bg_color <= 24'hFFFFFF;   // DIAGNOSTIC: power-up bright white
            mode     <= 2'd1;         // DIAGNOSTIC: power-up colour bars (no Avalon write needed)
        end else if (avl_write) begin
            case (avl_address)
                REG_BG_COLOR: bg_color <= avl_writedata[23:0];
                REG_MODE:     mode     <= avl_writedata[1:0];
                default:      ; // no other writable registers
            endcase
        end
    end

    // ── Avalon read (registered, latency 1) ──────────────────────────────────
    always_ff @(posedge clk) begin
        if (reset) begin
            avl_readdata <= 32'h0;
        end else if (avl_read) begin
            case (avl_address)
                REG_BG_COLOR: avl_readdata <= {8'h00, bg_color};
                REG_MODE:     avl_readdata <= {30'h0, mode};
                default:      avl_readdata <= 32'h0;
            endcase
        end
    end

    // ── Combinational pixel colour ───────────────────────────────────────────
    //  8-colour table for the test bars (full 8-bit-per-channel).
    function automatic logic [23:0] bar_color(input logic [2:0] idx);
        unique case (idx)
            3'd0: bar_color = 24'hFF_FF_FF; // white
            3'd1: bar_color = 24'hFF_FF_00; // yellow
            3'd2: bar_color = 24'h00_FF_FF; // cyan
            3'd3: bar_color = 24'h00_FF_00; // green
            3'd4: bar_color = 24'hFF_00_FF; // magenta
            3'd5: bar_color = 24'hFF_00_00; // red
            3'd6: bar_color = 24'h00_00_FF; // blue
            3'd7: bar_color = 24'h00_00_00; // black
        endcase
    endfunction

    logic [2:0]  bar;
    logic        checker;
    logic [23:0] color;

    assign bar     = (pixel_x / 10'd80);   // 640/8 = 80 px per bar -> 0..7
    assign checker = pixel_x[5] ^ pixel_y[5];

    always_comb begin
        unique case (mode)
            2'd0:    color = bg_color;                          // solid
            2'd1:    color = bar_color(bar);                    // colour bars
            2'd2:    color = checker ? 24'hFF_FF_FF : 24'h00_00_00; // checkerboard
            default: color = bg_color;                          // reserved -> solid
        endcase
    end

    // ── Registered VGA outputs (RGB + sync aligned, blanking forces RGB=0) ────
    always_ff @(posedge clk) begin
        if (reset) begin
            vga_r       <= 8'h0;
            vga_g       <= 8'h0;
            vga_b       <= 8'h0;
            vga_hs      <= 1'b1;          // sync idle level (active LOW)
            vga_vs      <= 1'b1;
            vga_blank_n <= 1'b0;
        end else begin
            vga_r       <= active ? color[23:16] : 8'h0;
            vga_g       <= active ? color[15:8]  : 8'h0;
            vga_b       <= active ? color[7:0]   : 8'h0;
            vga_hs      <= hsync;
            vga_vs      <= vsync;
            vga_blank_n <= active;
        end
    end

    // ADV7123 composite sync-on-green is not used in RGB mode -> tie low.
    assign vga_sync_n = 1'b0;

endmodule
