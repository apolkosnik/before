//============================================================================
//  SCSI controller (NCR53C90A "ESP"), register-level model
//
//  0x02014000  transfer count low   (write: staging, read: counter)
//  0x02014001  transfer count high
//  0x02014002  FIFO (16 deep)
//  0x02014003  command
//  0x02014004  status / select bus id
//  0x02014005  interrupt status / select timeout
//  0x02014006  sequence step / sync period
//  0x02014007  FIFO flags / sync offset
//  0x02014008  configuration
//  0x02014009  - / clock conversion
//  0x0201400a  - / test
//  0x0201400b  configuration 2 (53C90A)
//  0x02014020  DMA control     0x02014021  DMA FIFO status
//
//  Modeled on Previous src/esp.c.  Implemented: the FIFO with its flags
//  register, the dual write/count registers with counter load on DMA
//  commands (0 loads 0x10000), the NOP/FLUSH/RESET commands, hard reset
//  through the DMA control register ESPCTRL_RESET bit, and plain
//  storage for the setup registers.  This covers the boot ROM SCSI
//  system test (FIFO fill/drain with flag counts) and the extended
//  transfer count and configuration tests.
//
//  No SCSI bus, selection, transfer or interrupt generation yet: those
//  need the DMA engine and a disk backend (see docs/PORTING.md).
//============================================================================

module next_esp
(
	input         clk,
	input         reset,

	input         sel,
	input   [5:0] addr,          // byte offset within 0x02014000-0x0201403F
	input         we,
	input   [1:0] be,            // [1] = even byte, [0] = odd byte
	input  [15:0] wdata,
	output [15:0] rdata,

	output        int_scsi       // level, for INT_SCSI
);

localparam STAT_VGC = 8'h08, STAT_TC = 8'h10, STAT_PE = 8'h20,
           STAT_GE  = 8'h40, STAT_INT = 8'h80;

reg  [7:0] fifo [0:15];
reg  [7:0] fifo_head;            // mirror of fifo[0] for the read mux
reg  [4:0] fifoflags;

reg  [7:0] wr_tcl, wr_tch;       // write staging (not changed by reset)
reg [16:0] counter;

reg  [7:0] command0;
reg  [7:0] status;
reg  [7:0] intstatus;
reg  [7:0] seqstep;
reg  [7:0] syncperiod, syncoffset;
reg  [7:0] configuration, conf2;
reg  [7:0] clockconv;
reg  [7:0] selectbusid, selecttimeout;
reg  [7:0] dma_control, dma_status;
reg        mode_dma;

assign int_scsi = dma_control[5] & status[7];   // ESPCTRL_ENABLE_INT & STAT_INT

// register read mux (byte offsets; each 16-bit word carries two regs).
// Plain nested ternaries over scalar registers: functions or array reads
// in combinational paths break sensitivity in some simulators.
wire [5:0] a_even = {addr[5:1], 1'b0};
wire [5:0] a_odd  = {addr[5:1], 1'b1};

`define ESP_READ(a) ( \
	((a) == 6'h00) ? counter[7:0] : \
	((a) == 6'h01) ? counter[15:8] : \
	((a) == 6'h02) ? fifo_head : \
	((a) == 6'h03) ? command0 : \
	((a) == 6'h04) ? (status & 8'hF8) : \
	((a) == 6'h05) ? intstatus : \
	((a) == 6'h06) ? seqstep : \
	((a) == 6'h07) ? {3'd0, fifoflags} : \
	((a) == 6'h08) ? configuration : \
	((a) == 6'h0B) ? conf2 : \
	((a) == 6'h20) ? dma_control : \
	((a) == 6'h21) ? dma_status : 8'h00 )

assign rdata = {`ESP_READ(a_even), `ESP_READ(a_odd)};

integer i;

task automatic fifo_clear;
	begin
		for (i = 0; i < 16; i = i + 1) fifo[i] <= 8'h00;
		fifo_head <= 8'h00;
		fifoflags <= 0;
	end
endtask

task automatic fifo_pop;
	begin
		if (fifoflags != 0) begin
			for (i = 0; i < 15; i = i + 1) fifo[i] <= fifo[i+1];
			fifo[15] <= 8'h00;
			fifo_head <= fifo[1];
			fifoflags <= fifoflags - 1'd1;
		end
	end
endtask

task automatic fifo_push;
	input [7:0] v;
	begin
		if (fifoflags == 5'd16) begin
			fifo[15] <= v;               // overflow overwrites the top
			status[6] <= 1'b1;           // STAT_GE
		end
		else begin
			fifo[fifoflags[3:0]] <= v;
			if (fifoflags == 0) fifo_head <= v;
			fifoflags <= fifoflags + 1'd1;
		end
	end
endtask

task automatic hard_reset;
	begin
		// esp_reset_hard() + esp_reset_soft() in esp.c
		clockconv <= 8'h02;
		configuration <= configuration & 8'h07;
		fifo_clear;
		syncperiod <= 8'h05;
		syncoffset <= 8'h00;
		status <= status & ~(STAT_INT|STAT_VGC|STAT_PE|STAT_GE|STAT_TC);
		intstatus <= 8'h00;
		mode_dma <= 0;
		counter <= 0;
		seqstep <= 8'h00;
		command0 <= 8'h00;
	end
endtask

task automatic reg_write;
	input [5:0] a;
	input [7:0] v;
	begin
		case (a)
			6'h00: wr_tcl <= v;
			6'h01: wr_tch <= v;
			6'h02: fifo_push(v);
			6'h03: begin
				// esp_command_write()/esp_start_command(), reduced to the
				// command set the ROM uses; the counter loads on every
				// DMA command without touching the staging registers
				command0 <= v;
				if (v[7]) begin
					counter <= ({wr_tch, wr_tcl} == 16'd0) ? 17'h10000
					                                       : {1'b0, wr_tch, wr_tcl};
					status[4] <= 1'b0;   // clear STAT_TC
					mode_dma <= 1;
				end
				case (v & 8'h7F)
					7'h00: ;             // NOP
					7'h01: fifo_clear;   // flush FIFO
					7'h02: hard_reset;   // reset chip
					default: ;           // not implemented yet
				endcase
			end
			6'h04: selectbusid <= v;
			6'h05: selecttimeout <= v;
			6'h06: syncperiod <= v;
			6'h07: syncoffset <= v;
			6'h08: configuration <= v;
			6'h09: clockconv <= v;
			6'h0A: ;                     // test register
			6'h20: begin
				dma_control <= v;
				if (v[1]) hard_reset;    // ESPCTRL_RESET
			end
			6'h21: dma_status <= v;
			default: ;
		endcase
	end
endtask

always @(posedge clk) begin
	if (reset) begin
		fifo_clear;
		wr_tcl <= 0; wr_tch <= 0;
		counter <= 0;
		command0 <= 0;
		status <= 0;
		intstatus <= 0;
		seqstep <= 0;
		syncperiod <= 8'h05;
		syncoffset <= 0;
		configuration <= 0;
		conf2 <= 0;
		clockconv <= 8'h02;
		selectbusid <= 0;
		selecttimeout <= 0;
		dma_control <= 0;
		dma_status <= 0;
		mode_dma <= 0;
	end
	else if (sel) begin
		if (we) begin
			if (be[1]) reg_write(a_even, wdata[15:8]);
			if (be[0]) reg_write(a_odd, wdata[7:0]);
		end
		else begin
			// read side effects
			if ((be[1] && a_even == 6'h02) || (be[0] && a_odd == 6'h02))
				fifo_pop;
			if (((be[1] && a_even == 6'h05) || (be[0] && a_odd == 6'h05)) && status[7]) begin
				intstatus <= 8'h00;
				status <= status & ~(STAT_INT|STAT_VGC|STAT_PE|STAT_GE);
			end
		end
	end
end

endmodule
