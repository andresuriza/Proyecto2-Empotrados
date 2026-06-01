module mmio_slave (
    input  logic        clk,
    input  logic        reset_n,

    // Avalon-MM Slave Interface
    // Assuming base address is 0xFF200010, the offsets we care about are 0x00, 0x10, 0x20, 0x30.
    // 6-bit address gives 64 bytes of space. Word address = avl_address[5:2].
    input  logic [5:0]  avl_address,
    input  logic        avl_read,
    output logic [31:0] avl_readdata,
    input  logic        avl_write,
    input  logic [31:0] avl_writedata,

    // Conduits to Top-Level Modules
    // seg7_timer interface
    output logic        timer_reset,
    // filter_select interface
    input  logic [1:0]  active_filter,
    // Buttons & Switches
    input  logic [3:0]  key,
    input  logic [2:0]  sw,
    
    // IRQ out (optional, for keys)
    output logic        irq
);

    // Register map (Offsets relative to this slave's base 0xFF200010):
    // 0x00 (Word 0): 7-seg time display registers (Write 1 to clear/reset)
    // 0x10 (Word 4): Button/switch status (Read) | IRQ Enable (Write)
    // 0x20 (Word 8): Filter select readback (Read)
    // 0x30 (Word 12): Playback state register (Read/Write)

    logic [31:0] play_state_reg;
    logic [3:0]  irq_enable;
    logic [3:0]  key_edge;
    logic [3:0]  key_last;
    logic        timer_rst_reg;

    // Edge detection for interrupts
    always_ff @(posedge clk or negedge reset_n) begin
        if (!reset_n) begin
            key_last <= 4'b0000;
            key_edge <= 4'b0000;
        end else begin
            key_last <= key;
            // Generate edge: if pressed (assuming active low KEY, so ~key & key_last is a press)
            // Or active high? DE1-SoC KEYs are usually active low.
            key_edge <= (~key & key_last) | (key_edge & ~timer_rst_reg); // Simplistic way, clear on read?
            // Actually, let's clear key_edge when read from Avalon
            if (avl_read && (avl_address[5:2] == 4'd4)) begin
                key_edge <= 4'b0000; // clear on read
            end else begin
                key_edge <= (~key & key_last) | key_edge;
            end
        end
    end

    // IRQ Generation
    assign irq = |(key_edge & irq_enable);
    assign timer_reset = timer_rst_reg;

    // Avalon Read
    always_ff @(posedge clk or negedge reset_n) begin
        if (!reset_n) begin
            avl_readdata <= 32'h0;
        end else if (avl_read) begin
            case (avl_address[5:2])
                4'd0: avl_readdata <= 32'h0; // timer read (dummy)
                4'd4: avl_readdata <= {16'h0, 4'h0, irq_enable, 1'b0, sw, key}; // Status 
                4'd8: avl_readdata <= {30'h0, active_filter};
                4'd12: avl_readdata <= play_state_reg;
                default: avl_readdata <= 32'h0;
            endcase
        end
    end

    // Avalon Write
    always_ff @(posedge clk or negedge reset_n) begin
        if (!reset_n) begin
            play_state_reg <= 32'h0;
            irq_enable <= 4'h0;
            timer_rst_reg <= 1'b0;
        end else begin
            timer_rst_reg <= 1'b0; // Default to pulse
            
            if (avl_write) begin
                case (avl_address[5:2])
                    4'd0: timer_rst_reg <= avl_writedata[0]; // Pulse 1 to reset timer
                    4'd4: irq_enable <= avl_writedata[11:8]; // Assume IRQ enables are at bits 11:8
                    4'd8: ; // Filter is read only
                    4'd12: play_state_reg <= avl_writedata; // HPS writes play/pause state
                endcase
            end
        end
    end

endmodule