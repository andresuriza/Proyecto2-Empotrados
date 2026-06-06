`timescale 1ns/1ps
// ============================================================================
//  tb_vga_timing.sv  —  self-checking testbench for vga_timing.sv
// ----------------------------------------------------------------------------
//  Verifies, against the exact 640x480@60 spec, that:
//    * the horizontal period is 800 pixel-times and vertical is 525 lines;
//    * the pixel counters advance monotonically and wrap correctly;
//    * HSYNC is active-LOW for 96 pixel-times starting at hcount 656;
//    * VSYNC is active-LOW for 2 lines starting at vcount 490;
//    * 'active' is high exactly over hcount 0..639 & vcount 0..479, i.e. the
//      visible region contains exactly 640*480 = 307200 pixels.
//
//  Drives a 50 MHz clock and an active-high synchronous reset, then scans one
//  complete frame pixel-by-pixel (sampling on the DUT's pix_en strobe) and
//  raises $error on any mismatch. Prints a PASS/FAIL summary and $stop.
// ============================================================================

module tb_vga_timing();

    // ── DUT I/O ──────────────────────────────────────────────────────────────
    logic        clk;
    logic        reset;
    logic        pix_en;
    logic        vga_clk_out;
    logic [9:0]  pixel_x, pixel_y;
    logic        hsync, vsync, active;

    vga_timing dut (
        .clk         (clk),
        .reset       (reset),
        .pix_en      (pix_en),
        .vga_clk_out (vga_clk_out),
        .pixel_x     (pixel_x),
        .pixel_y     (pixel_y),
        .hsync       (hsync),
        .vsync       (vsync),
        .active      (active)
    );

    // ── Expected timing constants (the spec we are checking against) ─────────
    localparam int H_TOTAL = 800,  H_ACT = 640, H_SYNC_S = 656, H_SYNC_W = 96;
    localparam int V_TOTAL = 525,  V_ACT = 480, V_SYNC_S = 490, V_SYNC_W = 2;

    // ── 50 MHz clock ─────────────────────────────────────────────────────────
    always #10 clk = ~clk;

    // ── Error bookkeeping ────────────────────────────────────────────────────
    int errors = 0;
    task automatic check(input bit cond, input string msg);
        if (!cond) begin
            errors++;
            $error("[t=%0t] CHECK FAILED: %s (x=%0d y=%0d hs=%b vs=%b act=%b)",
                   $time, msg, pixel_x, pixel_y, hsync, vsync, active);
        end
    endtask

    // Advance exactly one pixel: wait for a pix_en clock, then let combinational
    // outputs settle before sampling.
    task automatic pix_tick;
        begin
            @(posedge clk);
            while (!pix_en) @(posedge clk);
            #1;
        end
    endtask

    // ── Stimulus + checks ────────────────────────────────────────────────────
    int  prev_x, prev_y;
    int  active_pixels;
    int  hsync_low, vsync_low_lines;

    initial begin
        clk   = 1'b0;
        reset = 1'b1;
        repeat (4) @(posedge clk);
        reset = 1'b0;

        // Align to the top-left origin of a frame.
        pix_tick;
        while (!(pixel_x == 0 && pixel_y == 0)) pix_tick;

        // ── Scan exactly one full frame ──────────────────────────────────────
        prev_x = 0; prev_y = 0;
        active_pixels   = 0;
        vsync_low_lines = 0;

        for (int p = 0; p < H_TOTAL * V_TOTAL; p++) begin
            automatic bit exp_hs  = ~((pixel_x >= H_SYNC_S) && (pixel_x < H_SYNC_S + H_SYNC_W));
            automatic bit exp_vs  = ~((pixel_y >= V_SYNC_S) && (pixel_y < V_SYNC_S + V_SYNC_W));
            automatic bit exp_act = (pixel_x < H_ACT) && (pixel_y < V_ACT);

            // Sync polarity / position and active-video region.
            check(hsync  == exp_hs,  "HSYNC polarity/position");
            check(vsync  == exp_vs,  "VSYNC polarity/position");
            check(active == exp_act, "ACTIVE region");
            if (active) active_pixels++;

            // Per-line HSYNC-low width must be exactly 96 pixel-times.
            if (pixel_x == 0) hsync_low = 0;
            if (!hsync) hsync_low++;
            if (pixel_x == H_TOTAL - 1)
                check(hsync_low == H_SYNC_W, "HSYNC low width == 96");

            // Counter advance / wrap: proves H_TOTAL=800 and V_TOTAL=525.
            if (p > 0) begin
                if (prev_x == H_TOTAL - 1) begin
                    check(pixel_x == 0, "hcount wraps to 0 after 799");
                    if (prev_y == V_TOTAL - 1)
                        check(pixel_y == 0, "vcount wraps to 0 after 524");
                    else
                        check(pixel_y == prev_y + 1, "vcount increments on new line");
                end else begin
                    check(pixel_x == prev_x + 1, "hcount increments by 1");
                    check(pixel_y == prev_y,      "vcount stable within line");
                end
            end

            // Count lines in which VSYNC is asserted (low). VSYNC stays low
            // continuously across the sync lines, so count at each line start
            // (pixel_x == 0) rather than counting contiguous low blocks.
            if (pixel_x == 0 && !vsync) vsync_low_lines++;

            prev_x = pixel_x;
            prev_y = pixel_y;
            pix_tick;
        end

        // ── Whole-frame checks ───────────────────────────────────────────────
        check(active_pixels   == H_ACT * V_ACT, "active pixels == 640*480");
        check(vsync_low_lines == V_SYNC_W,       "VSYNC low spans 2 lines");

        // ── Summary ──────────────────────────────────────────────────────────
        $display("------------------------------------------------------------");
        $display(" tb_vga_timing: scanned 1 frame (%0d pixels)", H_TOTAL * V_TOTAL);
        $display("   active pixels   = %0d (expected %0d)", active_pixels, H_ACT * V_ACT);
        $display("   vsync low lines = %0d (expected %0d)", vsync_low_lines, V_SYNC_W);
        if (errors == 0)
            $display(" RESULT: PASS — all VGA timing checks passed.");
        else
            $display(" RESULT: FAIL — %0d check(s) failed.", errors);
        $display("------------------------------------------------------------");
        $stop;
    end

    // ── Watchdog ─────────────────────────────────────────────────────────────
    initial begin
        #50_000_000;   // 50 ms ≫ one frame (~16.7 ms)
        $error("TIMEOUT: testbench did not finish");
        $stop;
    end

endmodule
