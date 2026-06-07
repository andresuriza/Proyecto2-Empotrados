// ============================================================================
//  text_buffer.sv  —  Simple dual-port character RAM (inferred M10K)
// ----------------------------------------------------------------------------
//  Holds the 80x30 = 2400 character cells rendered to the VGA screen. The
//  processor WRITES cells through Port A (the Avalon-MM side); the renderer
//  READS cells through Port B. Es un display de SOLO ESCRITURA: el software
//  escribe el texto y nunca necesita leerlo de vuelta, asi que el puerto A es
//  write-only y a_rdata = 0. Eso lo vuelve un SIMPLE dual-port (1 write + 1
//  read), que Quartus infiere de forma CONFIABLE como M10K block RAM.
//
//  (La version anterior era true dual-port con byte-enable + read-back en el
//   mismo puerto; esa combinacion NO infirio M10K y cayo a logica -> 65536
//   flip-flops -> no cabia en el FPGA.)
//
//  CELL BIT LAYOUT (16 bits):
//     bit 15 14 13 12 | 11 10  9  8 |  7  6  5  4  3  2  1  0
//          \--- bg ---/  \--- fg ---/  \-------- ASCII --------/
//
//  ADDRESSING: addr = cell index = row*80 + col  (0..2399).
//  RESET: none. El procesador limpia/inicializa la pantalla al boot.
// ============================================================================

module text_buffer #(
    parameter int ADDR_W = 12,          // 4096 words
    parameter int DATA_W = 16           // bits por celda
) (
    input  logic                clk,

    // ── Port A : write-only (procesador / Avalon) ────────────────────────────
    input  logic [ADDR_W-1:0]   a_addr,
    input  logic                a_we,
    input  logic [1:0]          a_byteen,   // [0]=byte bajo, [1]=byte alto
    input  logic [DATA_W-1:0]   a_wdata,
    output logic [DATA_W-1:0]   a_rdata,    // readback NO usado -> 0

    // ── Port B : read-only (renderer) ────────────────────────────────────────
    input  logic [ADDR_W-1:0]   b_addr,
    output logic [DATA_W-1:0]   b_rdata     // lectura registrada
);

<<<<<<< HEAD
    // Simple dual-port (1 write + 1 read) -> inferencia M10K confiable.
    // no_rw_check: lectura/escritura simultanea a la misma direccion = don't care
    // (en un display de texto no importa), ayuda a la inferencia del block RAM.
    (* ramstyle = "M10K, no_rw_check" *)
    logic [DATA_W-1:0] mem [0:(1<<ADDR_W)-1];

    // ── Port A : escritura byte-enable (sin read-back) ───────────────────────
=======
    // Shared memory array — inferred as a Cyclone V M10K true dual-port RAM.
    //
    //  ramstyle = "M10K"        : force the array into M10K block RAM (not LABs).
    //  no_rw_check              : tell the Fitter NOT to build read-during-write
    //                             bypass logic and to treat same-address RDW data
    //                             as DON'T-CARE. This is REQUIRED for inference
    //                             here: Port A does byte-enabled PARTIAL writes
    //                             (mem[a_addr][7:0]/[15:8]) while also reading the
    //                             FULL word from the same port/cycle. Without
    //                             no_rw_check, Quartus cannot find an M10K mode
    //                             with defined RDW semantics for a partially-
    //                             written word and falls back to registers
    //                             (the ~98 Kbit-of-flip-flops / LAB blow-up).
    //                             Safe here: Avalon read and write are mutually
    //                             exclusive per transaction, so the RDW result is
    //                             never consumed.
    //
    //  IMPORTANT: the memory array and its read-data registers have NO reset.
    //  M10K block RAM has no reset on its contents or output register; any reset
    //  branch touching mem[]/a_rdata/b_rdata would block block-RAM inference.
    (* ramstyle = "M10K, no_rw_check" *)
    logic [DATA_W-1:0] mem [0:(1<<ADDR_W)-1];

    // ── Port A : byte-enabled write + registered read-back (no reset) ────────
>>>>>>> arreglo-vga
    always_ff @(posedge clk) begin
        if (a_we) begin
            if (a_byteen[0]) mem[a_addr][7:0]  <= a_wdata[7:0];
            if (a_byteen[1]) mem[a_addr][15:8] <= a_wdata[15:8];
        end
<<<<<<< HEAD
    end

    // ── Port B : lectura registrada (renderer) ───────────────────────────────
=======
        a_rdata <= mem[a_addr];         // RDW data is don't-care (see no_rw_check)
    end

    // ── Port B : read-only, registered (no reset) ────────────────────────────
>>>>>>> arreglo-vga
    always_ff @(posedge clk) begin
        b_rdata <= mem[b_addr];
    end

    // El puerto A es de solo escritura; el readback no se usa.
    assign a_rdata = '0;

endmodule
