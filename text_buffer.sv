// ============================================================================
//  text_buffer.sv  —  True dual-port character RAM (inferred M10K)
// ----------------------------------------------------------------------------
//  80x30 = 2400 celdas de caracter para la pantalla VGA. El procesador escribe
//  celdas por el Puerto A (lado Avalon); el renderer lee por el Puerto B. Ambos
//  puertos con lectura registrada -> Quartus infiere M10K (true dual-port: un
//  puerto read+write y un puerto read-only). Profundidad 4096 (addr 12-bit);
//  solo se usan 0..2399.
//
//  ramstyle = "M10K, no_rw_check": fuerza M10K y trata el read-during-write a la
//  misma direccion como DON'T-CARE. Es NECESARIO para inferir aqui: el Puerto A
//  hace escrituras PARCIALES byte-enable (mem[a_addr][7:0]/[15:8]) y a la vez lee
//  la palabra completa; sin no_rw_check Quartus no encuentra un modo M10K con
//  semantica RDW definida y cae a registros (el blow-up de LABs que no cabia).
//  Seguro aqui: lectura y escritura Avalon son mutuamente exclusivas por
//  transaccion, asi que el dato RDW nunca se consume.
//
//  CELL BIT LAYOUT (16 bits): [7:0]=ASCII  [11:8]=fg  [15:12]=bg
//  ADDRESSING: addr = fila*80 + col  (0..2399).
//  RESET: ninguno (M10K no tiene reset de contenido); el procesador limpia la
//  pantalla al boot. No agregar reset aqui o se rompe la inferencia M10K.
// ============================================================================

module text_buffer #(
    parameter int ADDR_W = 12,          // 4096 words
    parameter int DATA_W = 16           // bits por celda
) (
    input  logic                clk,

    // ── Port A : Avalon-MM write/read (procesador) ───────────────────────────
    input  logic [ADDR_W-1:0]   a_addr,
    input  logic                a_we,
    input  logic [1:0]          a_byteen,   // [0]=byte bajo, [1]=byte alto
    input  logic [DATA_W-1:0]   a_wdata,
    output logic [DATA_W-1:0]   a_rdata,    // lectura registrada

    // ── Port B : renderer read-only ──────────────────────────────────────────
    input  logic [ADDR_W-1:0]   b_addr,
    output logic [DATA_W-1:0]   b_rdata     // lectura registrada
);

    (* ramstyle = "M10K, no_rw_check" *)
    logic [DATA_W-1:0] mem [0:(1<<ADDR_W)-1];

    // ── Port A : escritura byte-enable + lectura registrada (sin reset) ──────
    always_ff @(posedge clk) begin
        if (a_we) begin
            if (a_byteen[0]) mem[a_addr][7:0]  <= a_wdata[7:0];
            if (a_byteen[1]) mem[a_addr][15:8] <= a_wdata[15:8];
        end
        a_rdata <= mem[a_addr];     // RDW = don't-care (ver no_rw_check)
    end

    // ── Port B : read-only registrado (sin reset) ────────────────────────────
    always_ff @(posedge clk) begin
        b_rdata <= mem[b_addr];
    end

endmodule
