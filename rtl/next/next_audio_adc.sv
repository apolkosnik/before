// MiSTer ADC channel 0 -> signed mono PCM at 8012 Hz.
// 321-tap Q19 FIR: passband 0..3400 Hz, stopband 4006..32048 Hz,
// <0.09 dB ripple, >65 dB stopband rejection. One serial MAC computes
// each decimated sample; the 512-word ring allows new ADC samples to
// arrive while the filter reads the snapshot. History is padded with
// zero after reset, avoiding a resettable array of FPGA registers.
module next_audio_adc #(
    parameter CLK_REAL_HZ = 28000000
)(
    input clk, reset,
    inout [3:0] ADC_BUS,
    output reg signed [15:0] audio_in
);
wire [11:0] raw;
wire raw_sync;
ltc2308 #(.NUM_CH(1), .ADC_RATE(8012*8), .CLK_RATE(CLK_REAL_HZ),
          .SCK_DIV(2), .CONV_WAIT_NS(2000), .LONG_CONVST(1)) adc (
    .clk(clk), .reset(reset), .ADC_BUS(ADC_BUS), .dout(raw), .dout_sync(raw_sync)
);
reg sync_d;
reg [1:0] warmup;
reg have_bias;
reg signed [24:0] bias;
wire signed [24:0] raw_fixed = $signed({1'b0,raw,12'd0});
wire signed [24:0] bias_error = raw_fixed-bias;
wire signed [12:0] centered = $signed({1'b0,raw})-$signed({1'b0,bias[23:12]});
reg [2:0] count;
reg [8:0] write_pos, filled, base, available, tap;
reg signed [12:0] history [0:511];
reg busy, read_valid, read_last, read_present, product_valid, product_last;
reg signed [12:0] sample_q;
reg signed [17:0] coefficient_q;
reg signed [30:0] product;
reg signed [39:0] accumulator;
wire signed [39:0] sum = accumulator + {{9{product[30]}},product};
reg audio_valid;

function automatic signed [17:0] coefficient(input [8:0] index);
    begin
        case(index)
// BEGIN GENERATED FIR COEFFICIENTS (tb/design_audio_filter.py)
        9'd0: coefficient = -18'sd31;
        9'd1: coefficient = -18'sd196;
        9'd2: coefficient = -18'sd122;
        9'd3: coefficient = -18'sd173;
        9'd4: coefficient = -18'sd194;
        9'd5: coefficient = -18'sd209;
        9'd6: coefficient = -18'sd210;
        9'd7: coefficient = -18'sd194;
        9'd8: coefficient = -18'sd163;
        9'd9: coefficient = -18'sd117;
        9'd10: coefficient = -18'sd60;
        9'd11: coefficient = 18'sd2;
        9'd12: coefficient = 18'sd65;
        9'd13: coefficient = 18'sd120;
        9'd14: coefficient = 18'sd162;
        9'd15: coefficient = 18'sd184;
        9'd16: coefficient = 18'sd183;
        9'd17: coefficient = 18'sd157;
        9'd18: coefficient = 18'sd110;
        9'd19: coefficient = 18'sd45;
        9'd20: coefficient = -18'sd30;
        9'd21: coefficient = -18'sd106;
        9'd22: coefficient = -18'sd174;
        9'd23: coefficient = -18'sd223;
        9'd24: coefficient = -18'sd247;
        9'd25: coefficient = -18'sd240;
        9'd26: coefficient = -18'sd201;
        9'd27: coefficient = -18'sd133;
        9'd28: coefficient = -18'sd43;
        9'd29: coefficient = 18'sd60;
        9'd30: coefficient = 18'sd161;
        9'd31: coefficient = 18'sd248;
        9'd32: coefficient = 18'sd309;
        9'd33: coefficient = 18'sd334;
        9'd34: coefficient = 18'sd316;
        9'd35: coefficient = 18'sd256;
        9'd36: coefficient = 18'sd159;
        9'd37: coefficient = 18'sd34;
        9'd38: coefficient = -18'sd103;
        9'd39: coefficient = -18'sd236;
        9'd40: coefficient = -18'sd347;
        9'd41: coefficient = -18'sd419;
        9'd42: coefficient = -18'sd441;
        9'd43: coefficient = -18'sd408;
        9'd44: coefficient = -18'sd318;
        9'd45: coefficient = -18'sd182;
        9'd46: coefficient = -18'sd13;
        9'd47: coefficient = 18'sd168;
        9'd48: coefficient = 18'sd338;
        9'd49: coefficient = 18'sd474;
        9'd50: coefficient = 18'sd557;
        9'd51: coefficient = 18'sd571;
        9'd52: coefficient = 18'sd512;
        9'd53: coefficient = 18'sd382;
        9'd54: coefficient = 18'sd195;
        9'd55: coefficient = -18'sd29;
        9'd56: coefficient = -18'sd262;
        9'd57: coefficient = -18'sd474;
        9'd58: coefficient = -18'sd637;
        9'd59: coefficient = -18'sd725;
        9'd60: coefficient = -18'sd724;
        9'd61: coefficient = -18'sd628;
        9'd62: coefficient = -18'sd444;
        9'd63: coefficient = -18'sd192;
        9'd64: coefficient = 18'sd99;
        9'd65: coefficient = 18'sd394;
        9'd66: coefficient = 18'sd653;
        9'd67: coefficient = 18'sd841;
        9'd68: coefficient = 18'sd930;
        9'd69: coefficient = 18'sd901;
        9'd70: coefficient = 18'sd753;
        9'd71: coefficient = 18'sd499;
        9'd72: coefficient = 18'sd165;
        9'd73: coefficient = -18'sd208;
        9'd74: coefficient = -18'sd574;
        9'd75: coefficient = -18'sd884;
        9'd76: coefficient = -18'sd1095;
        9'd77: coefficient = -18'sd1174;
        9'd78: coefficient = -18'sd1104;
        9'd79: coefficient = -18'sd885;
        9'd80: coefficient = -18'sd539;
        9'd81: coefficient = -18'sd104;
        9'd82: coefficient = 18'sd368;
        9'd83: coefficient = 18'sd816;
        9'd84: coefficient = 18'sd1181;
        9'd85: coefficient = 18'sd1410;
        9'd86: coefficient = 18'sd1466;
        9'd87: coefficient = 18'sd1334;
        9'd88: coefficient = 18'sd1020;
        9'd89: coefficient = 18'sd555;
        9'd90: coefficient = -18'sd6;
        9'd91: coefficient = -18'sd596;
        9'd92: coefficient = -18'sd1139;
        9'd93: coefficient = -18'sd1561;
        9'd94: coefficient = -18'sd1801;
        9'd95: coefficient = -18'sd1817;
        9'd96: coefficient = -18'sd1595;
        9'd97: coefficient = -18'sd1153;
        9'd98: coefficient = -18'sd535;
        9'd99: coefficient = 18'sd185;
        9'd100: coefficient = 18'sd919;
        9'd101: coefficient = 18'sd1573;
        9'd102: coefficient = 18'sd2055;
        9'd103: coefficient = 18'sd2293;
        9'd104: coefficient = 18'sd2243;
        9'd105: coefficient = 18'sd1896;
        9'd106: coefficient = 18'sd1280;
        9'd107: coefficient = 18'sd462;
        9'd108: coefficient = -18'sd462;
        9'd109: coefficient = -18'sd1377;
        9'd110: coefficient = -18'sd2163;
        9'd111: coefficient = -18'sd2710;
        9'd112: coefficient = -18'sd2931;
        9'd113: coefficient = -18'sd2779;
        9'd114: coefficient = -18'sd2252;
        9'd115: coefficient = -18'sd1398;
        9'd116: coefficient = -18'sd308;
        9'd117: coefficient = 18'sd889;
        9'd118: coefficient = 18'sd2043;
        9'd119: coefficient = 18'sd2998;
        9'd120: coefficient = 18'sd3619;
        9'd121: coefficient = 18'sd3801;
        9'd122: coefficient = 18'sd3493;
        9'd123: coefficient = 18'sd2702;
        9'd124: coefficient = 18'sd1501;
        9'd125: coefficient = 18'sd18;
        9'd126: coefficient = -18'sd1571;
        9'd127: coefficient = -18'sd3066;
        9'd128: coefficient = -18'sd4263;
        9'd129: coefficient = -18'sd4984;
        9'd130: coefficient = -18'sd5097;
        9'd131: coefficient = -18'sd4542;
        9'd132: coefficient = -18'sd3336;
        9'd133: coefficient = -18'sd1586;
        9'd134: coefficient = 18'sd527;
        9'd135: coefficient = 18'sd2756;
        9'd136: coefficient = 18'sd4820;
        9'd137: coefficient = 18'sd6434;
        9'd138: coefficient = 18'sd7342;
        9'd139: coefficient = 18'sd7355;
        9'd140: coefficient = 18'sd6377;
        9'd141: coefficient = 18'sd4428;
        9'd142: coefficient = 18'sd1649;
        9'd143: coefficient = -18'sd1698;
        9'd144: coefficient = -18'sd5250;
        9'd145: coefficient = -18'sd8577;
        9'd146: coefficient = -18'sd11216;
        9'd147: coefficient = -18'sd12724;
        9'd148: coefficient = -18'sd12721;
        9'd149: coefficient = -18'sd10937;
        9'd150: coefficient = -18'sd7246;
        9'd151: coefficient = -18'sd1688;
        9'd152: coefficient = 18'sd5522;
        9'd153: coefficient = 18'sd14005;
        9'd154: coefficient = 18'sd23244;
        9'd155: coefficient = 18'sd32626;
        9'd156: coefficient = 18'sd41492;
        9'd157: coefficient = 18'sd49197;
        9'd158: coefficient = 18'sd55163;
        9'd159: coefficient = 18'sd58938;
        9'd160: coefficient = 18'sd60224;
// END GENERATED FIR COEFFICIENTS
        default: coefficient = 0;
        endcase
    end
endfunction

function automatic signed [15:0] pcm_clip(input signed [39:0] value);
    reg signed [40:0] magnitude, rounded;
    begin
        // Q19 filter gain and x8 PCM scaling: shift 16, round, saturate.
        magnitude = value < 0 ? -{value[39],value} : {value[39],value};
        rounded = (magnitude + 41'sd32768) >>> 16;
        if (value < 0) rounded = -rounded;
        if (rounded > 32767) pcm_clip = 16'sh7fff;
        else if (rounded < -32768) pcm_clip = 16'sh8000;
        else pcm_clip = rounded[15:0];
    end
endfunction

// Synchronous reads infer FPGA RAM. Valid flags suppress unfilled entries.
always @(posedge clk) if (busy) begin
    sample_q <= history[base-tap];
    coefficient_q <= coefficient(tap > 160 ? 9'd320-tap : tap);
end
always @(posedge clk) begin
    audio_valid <= 0;
    if (reset) begin
        sync_d <= 0; warmup <= 0; have_bias <= 0; bias <= 0; count <= 0;
        write_pos <= 0; filled <= 0; base <= 0; available <= 0; tap <= 0;
        busy <= 0; read_valid <= 0; read_last <= 0; read_present <= 0;
        product_valid <= 0; product_last <= 0; product <= 0;
        accumulator <= 0; audio_in <= 0;
    end else begin
        sync_d <= raw_sync;
        read_valid <= busy;
        product_valid <= read_valid;
        if (busy) begin
            read_last <= (tap == 320);
            read_present <= (tap < available);
            if (tap == 320) busy <= 0;
            else tap <= tap + 1'd1;
        end
        if (read_valid) begin
            product <= read_present ? sample_q * coefficient_q : 31'sd0;
            product_last <= read_last;
        end
        if (product_valid) begin
            accumulator <= sum;
            if (product_last) begin audio_in <= pcm_clip(sum); audio_valid <= 1; end
        end
        if (sync_d != raw_sync) begin
            // The toggle starts a conversion; raw is the preceding result.
            if (warmup != 2) warmup <= warmup + 1'd1;
            else if (!have_bias) begin bias <= raw_fixed; have_bias <= 1; end
            else begin
                bias <= bias + (bias_error >>> 10);
                history[write_pos] <= centered;
                write_pos <= write_pos + 1'd1;
                if (filled < 321) filled <= filled + 1'd1;
                count <= count + 1'd1;
                if (count == 7) begin
                    base <= write_pos;
                    available <= filled < 321 ? filled + 1'd1 : filled;
                    tap <= 0; accumulator <= 0; busy <= 1;
                end
            end
        end
    end
end
endmodule
