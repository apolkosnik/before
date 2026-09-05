// Run the unmodified NeXT Mach 3.3 FPSP from an externally supplied kernel.
// No disk, DMA, MMU translations, or OS scheduling: isolate CPU/FPU handling.
`timescale 1ns/1ps
module tb_next_fpsp;
    reg clk = 0, nreset = 0;
    always #5 clk = ~clk;
    wire [31:0] addr;
    wire [15:0] wdata;
    wire nwr, nuds, nlds;
    wire [1:0] busstate;
    reg ready = 0;
    reg [15:0] rdata = 0;
    wire mapped = addr < 32'h10000 ||
                  (addr >= 32'h04000000 && addr < 32'h04100000);
    wire berr = nreset && busstate != 2'b01 && !mapped;
    ap040_tg68k_compat dut (
        .clk(clk), .nreset(nreset),
        .clkena_in(busstate == 2'b01 || ready || berr),
        .cache_allow_all(1'b1), .cache_snoop_stb(1'b0),
        .cache_snoop_addr(32'd0), .cache_z2_ena(1'b0),
        .cache_z3_base0(5'd0), .cache_z3_ena0(1'b0),
        .cache_z3_base1(4'd0), .cache_z3_ena1(1'b0),
        .data_in(rdata), .ipl(3'b111), .ipl_autovector(1'b1), .berr(berr),
        .addr_out(addr), .data_write(wdata), .nwr(nwr), .nuds(nuds),
        .nlds(nlds), .busstate(busstate),
        .walker_ack(1'b0), .walker_data(32'd0), .walker_berr(1'b0),
        .cache_data(16'd0), .cache_ack(1'b0)
    );
    reg [15:0] lowmem [0:32767];
    reg [15:0] kernel [0:524287];
    string progfile, kernelfile;
    integer stage = 0, cycle = 0, wait_cycles = 0, latency = 0;
    integer i;
    reg [15:0] stack_fill = 0;
    reg [7:0] old_state = 0;
    reg [31:0] old_pc = 0;
    always @(posedge clk) begin
        cycle <= cycle + 1;
        old_state <= dut.core.state;
        old_pc <= dut.core.pc_i;
        if (nreset && dut.core.state == 8'd34 && old_state != 8'd34) begin
            $display("EXC stage=%0d vec=%0d pc=%08x spc=%08x sr=%04x",
                     stage, dut.core.exc_vec, dut.core.pc_i,
                     dut.core.exc_spc, dut.core.sr);
            $display("  frame busy=%b prepared=%b cmd=%04x et=%024x flags=%b",
                     dut.core.fpu_fstate_busy, dut.core.fpu_fstate_unimp,
                     dut.core.fpu_fstate_cmd1, dut.core.fpu_fstate_et,
                     dut.core.fpu_fstate_flags);
        end
        if ($test$plusargs("trace") && nreset && old_pc != dut.core.pc_i)
            $display("PC %08x stage=%0d", dut.core.pc_i, stage);
        if (!nreset || busstate == 2'b01) begin
            ready <= 0;
            wait_cycles <= latency;
        end else if (!ready && mapped) begin
            if (wait_cycles != 0) wait_cycles <= wait_cycles - 1;
            else begin
                ready <= 1;
                if (addr < 32'h10000) begin
                    rdata <= lowmem[addr[15:1]];
                    if (!nwr) begin
                        if (!nuds) lowmem[addr[15:1]][15:8] <= wdata[15:8];
                        if (!nlds) lowmem[addr[15:1]][7:0] <= wdata[7:0];
                        if (addr == 32'hf100) begin
                            stage <= int'(wdata);
                            $display("STAGE %0d", wdata);
                        end
                        if (addr == 32'hf102) begin
                            $display("D1=%08x FP0=%x/%04x/%016x ISP=%08x",
                                     dut.core.regfile.dreg[1], dut.core.g_fpu.fpu.fr_s[0],
                                     dut.core.g_fpu.fpu.fr_e[0], dut.core.g_fpu.fpu.fr_m[0],
                                     dut.core.regfile.isp);
                            $display("FP1=%x/%04x/%016x", dut.core.g_fpu.fpu.fr_s[1],
                                     dut.core.g_fpu.fpu.fr_e[1], dut.core.g_fpu.fpu.fr_m[1]);
                            $display("packed=%04x%04x %04x%04x %04x%04x",
                                     lowmem['h1800], lowmem['h1801], lowmem['h1802],
                                     lowmem['h1803], lowmem['h1804], lowmem['h1805]);
                            if (wdata != 16'h600d)
                                $fatal(1, "FAIL stage=%0d pc=%08x", stage, dut.core.pc_i);
                            $display("ALL PASS: real NeXT FPSP (%0d cycles, latency %0d)", cycle, latency);
                            $finish;
                        end
                    end
                end else begin
                    rdata <= kernel[addr[19:1]];
                    if (!nwr) begin
                        if (!nuds) kernel[addr[19:1]][15:8] <= wdata[15:8];
                        if (!nlds) kernel[addr[19:1]][7:0] <= wdata[7:0];
                    end
                end
            end
        end
        if (berr) $fatal(1, "Unmapped bus access %08x pc=%08x stage=%0d", addr, dut.core.pc_i, stage);
        if (cycle > 10000000) $fatal(1, "Timeout pc=%08x stage=%0d", dut.core.pc_i, stage);
    end
    initial begin
        if (!$value$plusargs("prog=%s", progfile) ||
            !$value$plusargs("kernel=%s", kernelfile))
            $fatal(1, "Use +prog=<hex16> +kernel=<sdmach hex16>");
        if ($value$plusargs("latency=%d", latency)) begin end
        if ($value$plusargs("stackfill=%h", stack_fill)) begin end
        for (i = 0; i < 32768; i = i + 1) lowmem[i] = 0;
        for (i = 'h6000; i < 'h7000; i = i + 1) lowmem[i] = stack_fill;
        for (i = 0; i < 524288; i = i + 1) kernel[i] = 0;
        $readmemh(progfile, lowmem);
        $readmemh(kernelfile, kernel);
        repeat (10) @(negedge clk);
        nreset = 1;
    end
endmodule
