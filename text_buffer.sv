// ============================================================================
//  text_buffer.sv  —  True dual-port character RAM (inferred M10K)
// ----------------------------------------------------------------------------
//  Holds the 80x30 = 2400 character cells rendered to the VGA screen. The
//  processor writes cells through Port A (the Avalon-MM side); the renderer
//  reads cells through Port B. Both ports are registered-read so Quartus infers
//  Cyclone V M10K block RAM (a true dual-port RAM with one read+write port and
//  one read-only port). Depth is rounded up to 4096 so the address is a clean
//  12-bit word index; only 0..2399 are used.
//
//  CELL BIT LAYOUT (16 bits, fully used — no reserved bits)
//
//     bit 15 14 13 12 | 11 10  9  8 |  7  6  5  4  3  2  1  0
//          \--- bg ---/  \--- fg ---/  \-------- ASCII --------/
//
//     [ 7:0]  printable ASCII code (0x20..0x7E)
//     [11:8]  foreground palette index (16-color)
//     [15:12] background palette index (16-color)
//
//  ADDRESSING: addr = cell index = row*80 + col  (0..2399).
//
//  RESET: none. Block RAM contents are not reset (M10K has no reset port); the
//  processor is expected to clear/initialise the screen at boot. Avoiding a
//  reset here is what allows clean M10K inference.
// ============================================================================

module text_buffer #(
    parameter int ADDR_W = 12,          // 4096 words
    parameter int DATA_W = 16           // bits per cell
) (
    input  logic                clk,

    // ── Port A : Avalon-MM write/read side (processor) ───────────────────────
    input  logic [ADDR_W-1:0]   a_addr,
    input  logic                a_we,       // write enable
    input  logic [1:0]          a_byteen,   // [0]=low byte, [1]=high byte of cell
    input  logic [DATA_W-1:0]   a_wdata,
    output logic [DATA_W-1:0]   a_rdata,    // registered read-back

    // ── Port B : renderer read side (read-only) ──────────────────────────────
    input  logic [ADDR_W-1:0]   b_addr,
    output logic [DATA_W-1:0]   b_rdata     // registered read
);

    // Shared memory array — inferred as M10K true dual-port RAM.
    (* ramstyle = "M10K" *)
    logic [DATA_W-1:0] mem [0:(1<<ADDR_W)-1];

    // ── Port A : byte-enabled write, registered read-back ────────────────────
    always_ff @(posedge clk) begin
        if (a_we) begin
            if (a_byteen[0]) mem[a_addr][7:0]  <= a_wdata[7:0];
            if (a_byteen[1]) mem[a_addr][15:8] <= a_wdata[15:8];
        end
        a_rdata <= mem[a_addr];         // read-first behaviour is fine here
    end

    // ── Port B : read-only, registered ───────────────────────────────────────
    always_ff @(posedge clk) begin
        b_rdata <= mem[b_addr];
    end

endmodule
