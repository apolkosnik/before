`timescale 1ns/1ps
// Mount-time geometry only: no SD/DMA transaction or guest disk writes.
module tb_next_scsi_geometry;
reg clk = 0;
always #5 clk = ~clk;
reg reset = 1;
reg [5:0] mounted = 0;
reg [63:0] bytes = 0;
reg readonly = 0;
reg [23:0] expected [0:5];
reg [31:0] expected_blocks [0:5];
reg [5:0] expected_present = 0, expected_ro = 0;
integer checks = 0;
next_scsi #(.CLK_HZ(1000000)) dut (
    .clk(clk), .reset(reset), .sel_esp(1'b0), .sel_csr(1'b0),
    .sel_sptr(1'b0), .sel_ptr(1'b0), .sel_ini(1'b0),
    .addr(6'd0), .we(1'b0), .be(2'd0), .wdata(16'd0),
    .m_dout(32'd0), .m_ack(1'b0), .m_err(1'b0),
    .flp_select(1'b0), .flp_req(1'b0), .flp_wr(1'b0),
    .flp_len(11'd0), .flp_bq(8'd0),
    .img_mounted(mounted), .img_size(bytes), .img_readonly(readonly),
    .sd_ack(1'b0), .sd_buff_addr(9'd0), .sd_buff_dout(8'd0), .sd_buff_wr(1'b0)
);
task mount_check(input [5:0] slots, input [63:0] size_bytes, input ro);
    reg [63:0] blocks, cylinders;
    integer slot;
    begin
        @(negedge clk);
        mounted = slots; bytes = size_bytes; readonly = ro;
        blocks = {32'd0, size_bytes[40:9]};
        cylinders = (blocks + 64'd127) / 64'd128;
        for (slot = 0; slot < 6; slot = slot + 1) if (slots[slot]) begin
            expected[slot] = cylinders[23:0];
            expected_blocks[slot] = blocks[31:0];
            expected_present[slot] = size_bytes != 0;
            expected_ro[slot] = ro;
        end
        @(posedge clk); #1;
        for (slot = 0; slot < 6; slot = slot + 1) begin
            if (dut.geo_cyl_v[slot] !== expected[slot] ||
                dut.img_blocks_v[slot] !== expected_blocks[slot] ||
                dut.disk_present_v[slot] !== expected_present[slot] ||
                dut.disk_ro_v[slot] !== expected_ro[slot])
                $fatal(1, "geometry mount %0d slot %0d size=%h got=%h expected=%h",
                       checks, slot, size_bytes, dut.geo_cyl_v[slot], expected[slot]);
        end
        checks = checks + 1;
    end
endtask
reg [31:0] rng = 32'h68040;
integer i;
initial begin
    mount_check(6'h3f, 0, 0);
    reset = 0;
    mount_check(6'h3f, 511, 0);          // partial blocks are discarded
    mount_check(6'h15, 512, 1);
    mount_check(6'h2a, 127*512, 0);
    mount_check(6'h3f, 128*512, 1);
    mount_check(6'h3f, 129*512+511, 0);
    mount_check(6'h3f, 64'h000000ffffffffff, 1); // quotient rounds through 24-bit wrap
    mount_check(6'h01, 64'h0000010000000000, 0);
    mount_check(6'h02, 64'h0000010000000200, 0);
    mount_check(6'h04, 64'h000001ffffffffff, 1);
    mount_check(6'h08, 64'h0000020000000000, 0); // discarded size bit 41
    mount_check(6'h3f, 64'hffffffffffffffff, 1);
    for (i = 0; i < 4096; i = i + 1) begin
        rng = (rng ^ (rng << 13));
        rng = (rng ^ (rng >> 17));
        rng = (rng ^ (rng << 5));
        // Consecutive mounts, simultaneous slots, same-slot replacements,
        // no-mount cycles, and mounts during CPU reset must all be safe.
        reset = (i % 19 == 0);
        mount_check(rng[5:0], {rng, rng ^ 32'h5a5aa5a5}, rng[6]);
    end
    mount_check(6'h3f, 0, 0);           // eject every slot
    mount_check(6'h00, 64'hffffffffffffffff, 1);
    $display("ALL PASS: %0d mount/geometry checks across six slots", checks);
    $finish;
end
endmodule
