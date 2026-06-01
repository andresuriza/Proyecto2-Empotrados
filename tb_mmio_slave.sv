`timescale 1ns/1ps

module tb_mmio_slave();

    // Clock and Reset
    logic        clk;
    logic        reset_n;

    // Avalon-MM Signals
    logic [5:0]  avl_address;
    logic        avl_read;
    logic [31:0] avl_readdata;
    logic        avl_write;
    logic [31:0] avl_writedata;

    // Conduit Signals
    logic        timer_reset;
    logic [1:0]  active_filter;
    logic [3:0]  key;
    logic [2:0]  sw;
    logic        irq;

    // Instantiate the DUT (Device Under Test)
    mmio_slave dut (
        .clk(clk),
        .reset_n(reset_n),
        .avl_address(avl_address),
        .avl_read(avl_read),
        .avl_readdata(avl_readdata),
        .avl_write(avl_write),
        .avl_writedata(avl_writedata),
        .timer_reset(timer_reset),
        .active_filter(active_filter),
        .key(key),
        .sw(sw),
        .irq(irq)
    );

    // Clock Generation (50 MHz)
    always #10 clk = ~clk;

    // Avalon-MM Write Task
    task avalon_write(input logic [5:0] addr, input logic [31:0] data);
        begin
            @(posedge clk);
            avl_address <= addr;
            avl_writedata <= data;
            avl_write <= 1'b1;
            @(posedge clk);
            avl_write <= 1'b0;
        end
    endtask

    // Avalon-MM Read Task
    task avalon_read(input logic [5:0] addr);
        begin
            @(posedge clk);
            avl_address <= addr;
            avl_read <= 1'b1;
            @(posedge clk);
            avl_read <= 1'b0;
            @(posedge clk); // wait for readdata pipeline
            $display("Time=%0t | READ from 0x%0h: 0x%0h", $time, addr, avl_readdata);
        end
    endtask

    // Test Sequence
    initial begin
        // Initialize signals
        clk = 0;
        reset_n = 0;
        avl_address = 0;
        avl_read = 0;
        avl_write = 0;
        avl_writedata = 0;
        active_filter = 2'b00;
        key = 4'b1111; // Active low buttons, initial unpressed
        sw = 3'b000;

        // Apply Reset
        #50 reset_n = 1'b1;
        $display("Time=%0t | Out of Reset", $time);

        // 1. Write to Play State Register (Offset 0x30 = Word 12 -> 48 decimal)
        #20;
        $display("Time=%0t | Writing Play State info into 0x30", $time);
        avalon_write(6'h30, 32'hDEADBEEF);
        
        // Read back Play State Register
        avalon_read(6'h30);

        // 2. Test Timer Reset (Offset 0x00 = Word 0 -> 0 decimal)
        #20;
        $display("Time=%0t | Pulsing Timer Reset via 0x00", $time);
        avalon_write(6'h00, 32'h0000_0001);
        @(posedge clk); // timer_reset is combinatorial/pulse usually, let's observe it

        // 3. Test Filter Reading (Offset 0x20 = Word 8 -> 32 decimal)
        #20;
        active_filter = 2'b10; // Change external hardware input
        $display("Time=%0t | Changed hardware filter switch. Reading from HPS...", $time);
        avalon_read(6'h20);

        // 4. Test Key Edge Detection and IRQs (Offset 0x10 = Word 4 -> 16 decimal)
        #20;
        // Enable IRQ for KEY0 (let's assume bit 8 handles KEY0 IRQ)
        avalon_write(6'h10, 32'h0000_0100); 

        // Press KEY0 (active low)
        #20 key[0] = 1'b0;
        #20 key[0] = 1'b1;
        
        #40;
        if (irq) $display("Time=%0t | IRQ Succesfully asserted!", $time);
        else     $display("Time=%0t | ERROR: IRQ Not Asserted!", $time);

        // Read to clear IRQ (Offset 0x10)
        avalon_read(6'h10);
        #20;
        if (!irq) $display("Time=%0t | IRQ successfully cleared!", $time);
        else      $display("Time=%0t | ERROR: IRQ still asserted!", $time);

        #100;
        $display("Time=%0t | Simulation Complete.", $time);
        $stop;
    end

endmodule