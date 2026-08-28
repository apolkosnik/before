//============================================================================
//  Reed-Solomon codec for the MO controller ECC buffer
//
//  Cross-interleaved RS(36,32) over GF(2^8) (polynomial 0x11d),
//  generator (x-1)(x-2)(x-4)(x-8) = x^4 + 0x0F x^3 + 0x36 x^2 +
//  0x78 x + 0x40, a straight port of Previous src/rs.c (verified
//  against it with shared golden vectors, see tb/tb_next_rs.sv).
//
//  encode: spreads the 1024 data bytes into 32 rows of 36, encodes the
//  32 column codewords, then the 36 row codewords (1024 -> 1296 bytes).
//  decode: corrects up to 2 byte errors per codeword, rows first, then
//  columns, then compacts back to 1024 bytes.  err_count is the total
//  corrected bytes.  fail is raised when a column codeword is
//  uncorrectable (rs_decode returning -1); row failures are skipped,
//  as in rs_decode().
//
//  Byte-serial FSM: one buffer access per cycle through the b_* port,
//  one cycle of read latency.  All GF arithmetic is combinational
//  shift-and-xor logic; inversions run through a 13-step sequential
//  Fermat chain to keep logic depth down.
//============================================================================

module next_rs
(
	input             clk,
	input             reset,

	input             start_encode,
	input             start_decode,
	output reg        done,          // one-cycle pulse
	output reg        fail,
	output reg  [7:0] err_count,

	// buffer port: b_addr/b_we/b_wdata are sampled every cycle, b_rdata
	// is valid the cycle after the address was presented
	output reg [10:0] b_addr,
	input       [7:0] b_rdata,
	output reg  [7:0] b_wdata,
	output reg        b_we
);

//----------------------------------------------------------------------------
// GF(2^8) primitives, polynomial 0x11d
//----------------------------------------------------------------------------

function automatic [7:0] gmul;
	input [7:0] a;
	input [7:0] b;
	reg [7:0] p, aa;
	integer k;
	begin
		p = 8'h00;
		aa = a;
		for (k = 0; k < 8; k = k + 1) begin
			if (b[k]) p = p ^ aa;
			aa = {aa[6:0], 1'b0} ^ (aa[7] ? 8'h1d : 8'h00);
		end
		gmul = p;
	end
endfunction

// generator coefficients: x^4 + G3 x^3 + G2 x^2 + G1 x + G0
localparam [7:0] G3 = 8'h0F, G2 = 8'h36, G1 = 8'h78, G0 = 8'h40;
localparam [7:0] ALPHA_INV = 8'h8e;      // 1/2 in this field

// one LFSR step of ecc_block(): r' = t_rem[r>>24] ^ (r<<8)
function automatic [31:0] lfsr_step;
	input [31:0] r;
	reg [7:0] t;
	begin
		t = r[31:24];
		lfsr_step = {r[23:0], 8'h00} ^
		            {gmul(t, G3), gmul(t, G2), gmul(t, G1), gmul(t, G0)};
	end
endfunction

//----------------------------------------------------------------------------
// state
//----------------------------------------------------------------------------

localparam S_IDLE     = 6'd0,
           S_SPR_RD   = 6'd1,  S_SPR_CAP  = 6'd2,  S_SPR_WAIT = 6'd32,
           S_ENC_CW   = 6'd3,  S_ENC_RD   = 6'd4,  S_ENC_ACC  = 6'd5,
           S_ENC_FIN  = 6'd6,  S_ENC_WR   = 6'd7,
           S_DEC_CW   = 6'd8,  S_CHK_RD   = 6'd9,  S_CHK_ACC  = 6'd10,
           S_CHK_EVAL = 6'd11,
           S_SYN_RD   = 6'd12, S_SYN_ACC  = 6'd13,
           S_BM_DELTA = 6'd14, S_BM_INV   = 6'd15, S_BM_APPLY = 6'd16,
           S_CLASSIFY = 6'd17,
           S_SE_INV   = 6'd18, S_SE_LOG   = 6'd19,
           S_ROOT     = 6'd20,
           S_APOW     = 6'd21,
           S_DIV1_INV = 6'd22, S_DIV2_INV = 6'd23,
           S_FIX1_RD  = 6'd24, S_FIX1_WR  = 6'd25,
           S_FIX2_RD  = 6'd26, S_FIX2_WR  = 6'd27,
           S_CW_NEXT  = 6'd28,
           S_CMP_RD   = 6'd29, S_CMP_CAP  = 6'd30, S_CMP_WAIT = 6'd33,
           S_FIX2_WAIT = 6'd34,
           S_DONE     = 6'd31;

reg [5:0] st;
reg       cols;                  // current pass: 0 = rows, 1 = columns

reg [5:0] cw;                    // codeword index within the pass
reg [5:0] idx;                   // byte index within a codeword
reg [5:0] row, col;              // spread/compact indices

reg [31:0] lfsr;
reg [31:0] ref_ecc;

reg [7:0] syn1, syn2, syn3, syn4;
reg [7:0] p1, p2, p3;            // alpha^i, alpha^2i, alpha^3i

reg [7:0] sg0, sg1, sg2, sg3, sg4;
reg [7:0] om0, om1, om2, om3, om4;
reg [7:0] ta0, ta1, ta2, ta3, ta4;
reg [7:0] ga0, ga1, ga2, ga3, ga4;
reg [2:0] bm_l;
reg [2:0] bm_d;
reg       bm_b;
reg [7:0] bm_delta;
reg       bm_method_a;

// sequential Fermat inverse: 6 x (square, multiply) + final square
reg [7:0] inv_x, inv_t;
reg [3:0] inv_step;

reg [8:0] root_k;
reg [7:0] root_q;                // alpha^-k
reg [7:0] q_e1, q2_e1;
reg [7:0] q_e2, q2_e2;
reg [5:0] epos1, epos2;
reg       have_root1;
reg       two_errors;

reg [8:0] log_k;
reg [7:0] log_p;
reg [7:0] log_x;

reg [7:0] ap1, ap2;              // alpha^epos1, alpha^epos2
reg [5:0] ap_cnt;

reg [7:0] e1_num, e2_num;        // omega(1/r) * alpha^epos values
reg [7:0] d1_val, d2_val;
reg [7:0] err1, err2;
reg       cw_fail;

// codeword byte address: off + step*k
function automatic [10:0] cwa;
	input       c;               // pass: 0 rows, 1 columns
	input [5:0] w;               // codeword
	input [5:0] k;               // byte
	begin
		if (c) cwa = {5'd0, w} + 11'd36 * k;    // column: off=cw, step=36
		else   cwa = 11'd36 * w + {5'd0, k};    // row: off=36*cw, step=1
	end
endfunction

integer i;

always @(posedge clk) begin
	done <= 0;
	b_we <= 0;

	if (reset) begin
		st <= S_IDLE;
		fail <= 0;
		err_count <= 0;
	end
	else begin
		case (st)
		S_IDLE: begin
			if (start_encode) begin
				fail <= 0;
				err_count <= 0;
				// spread rows 31..1, bytes 31..0 (descending, memmove
				// semantics for the overlapping low rows)
				row <= 6'd31;
				col <= 6'd31;
				st <= S_SPR_RD;
			end
			else if (start_decode) begin
				fail <= 0;
				err_count <= 0;
				cols <= 0;
				cw <= 0;
				st <= S_DEC_CW;
			end
		end

		//------------------------------------------------------------
		// encode: spread
		//------------------------------------------------------------
		S_SPR_RD: begin
			// issue the read; the previous write commits this cycle
			b_addr <= 11'd32 * row + {5'd0, col};
			st <= S_SPR_WAIT;
		end
		S_SPR_WAIT: st <= S_SPR_CAP;
		S_SPR_CAP: begin
			// b_rdata = buf[32*row+col]; write it to 36*row+col
			b_addr <= 11'd36 * row + {5'd0, col};
			b_wdata <= b_rdata;
			b_we <= 1;
			if (col != 0) begin
				col <= col - 1'd1;
				st <= S_SPR_RD;
			end
			else if (row != 1) begin
				row <= row - 1'd1;
				col <= 6'd31;
				st <= S_SPR_RD;
			end
			else begin
				cols <= 1;               // columns first
				cw <= 0;
				st <= S_ENC_CW;
			end
		end

		//------------------------------------------------------------
		// encode: one codeword (32 data bytes in, 4 parity out)
		//------------------------------------------------------------
		S_ENC_CW: begin
			lfsr <= 0;
			idx <= 0;
			b_addr <= cwa(cols, cw, 6'd0);
			st <= S_ENC_RD;
		end
		S_ENC_RD: st <= S_ENC_ACC;
		S_ENC_ACC: begin
			// ecc_block(): bytes 0-3 load, bytes 4-31 step-and-add
			if (idx < 6'd4) lfsr <= {lfsr[23:0], b_rdata};
			else            lfsr <= lfsr_step(lfsr) ^ {24'd0, b_rdata};
			if (idx == 6'd31) begin
				idx <= 0;
				st <= S_ENC_FIN;
			end
			else begin
				idx <= idx + 1'd1;
				b_addr <= cwa(cols, cw, idx + 1'd1);
				st <= S_ENC_RD;
			end
		end
		S_ENC_FIN: begin
			// four empty steps finish the remainder
			lfsr <= lfsr_step(lfsr);
			if (idx == 6'd3) begin
				idx <= 6'd32;
				st <= S_ENC_WR;
			end
			else idx <= idx + 1'd1;
		end
		S_ENC_WR: begin
			b_addr <= cwa(cols, cw, idx);
			b_wdata <= (idx == 6'd32) ? lfsr[31:24] :
			           (idx == 6'd33) ? lfsr[23:16] :
			           (idx == 6'd34) ? lfsr[15:8] : lfsr[7:0];
			b_we <= 1;
			if (idx == 6'd35) begin
				if (cols) begin
					if (cw == 6'd31) begin
						cols <= 0;       // rows pass follows
						cw <= 0;
						st <= S_ENC_CW;
					end
					else begin
						cw <= cw + 1'd1;
						st <= S_ENC_CW;
					end
				end
				else begin
					if (cw == 6'd35) st <= S_DONE;
					else begin
						cw <= cw + 1'd1;
						st <= S_ENC_CW;
					end
				end
			end
			else idx <= idx + 1'd1;
		end

		//------------------------------------------------------------
		// decode: ecc check pass over one codeword
		//------------------------------------------------------------
		S_DEC_CW: begin
			lfsr <= 0;
			ref_ecc <= 0;
			idx <= 0;
			cw_fail <= 0;
			two_errors <= 0;
			b_addr <= cwa(cols, cw, 6'd0);
			st <= S_CHK_RD;
		end
		S_CHK_RD: st <= S_CHK_ACC;
		S_CHK_ACC: begin
			if (idx < 6'd4)       lfsr <= {lfsr[23:0], b_rdata};
			else if (idx < 6'd32) lfsr <= lfsr_step(lfsr) ^ {24'd0, b_rdata};
			else                  ref_ecc <= {ref_ecc[23:0], b_rdata};
			if (idx == 6'd35) st <= S_CHK_EVAL;
			else begin
				idx <= idx + 1'd1;
				b_addr <= cwa(cols, cw, idx + 1'd1);
				st <= S_CHK_RD;
			end
		end
		S_CHK_EVAL: begin : chkeval
			reg [31:0] e;
			e = lfsr_step(lfsr_step(lfsr_step(lfsr_step(lfsr))));
			if (e == ref_ecc) st <= S_CW_NEXT;       // clean codeword
			else begin
				syn1 <= 0; syn2 <= 0; syn3 <= 0; syn4 <= 0;
				p1 <= 8'h01; p2 <= 8'h01; p3 <= 8'h01;
				idx <= 0;
				b_addr <= cwa(cols, cw, 6'd35);
				st <= S_SYN_RD;
			end
		end

		//------------------------------------------------------------
		// decode: syndromes (i = 0..35 over byte (35-i))
		//------------------------------------------------------------
		S_SYN_RD: st <= S_SYN_ACC;
		S_SYN_ACC: begin
			if (b_rdata != 0) begin
				syn1 <= syn1 ^ b_rdata;
				syn2 <= syn2 ^ gmul(b_rdata, p1);
				syn3 <= syn3 ^ gmul(b_rdata, p2);
				syn4 <= syn4 ^ gmul(b_rdata, p3);
			end
			p1 <= gmul(p1, 8'h02);
			p2 <= gmul(p2, 8'h04);
			p3 <= gmul(p3, 8'h08);
			if (idx == 6'd35) begin
				sg0 <= 1; sg1 <= 0; sg2 <= 0; sg3 <= 0; sg4 <= 0;
				om0 <= 1; om1 <= 0; om2 <= 0; om3 <= 0; om4 <= 0;
				ta0 <= 1; ta1 <= 0; ta2 <= 0; ta3 <= 0; ta4 <= 0;
				ga0 <= 0; ga1 <= 0; ga2 <= 0; ga3 <= 0; ga4 <= 0;
				bm_d <= 0;
				bm_b <= 0;
				bm_l <= 1;
				st <= S_BM_DELTA;
			end
			else begin
				idx <= idx + 1'd1;
				b_addr <= cwa(cols, cw, 6'd35 - (idx + 1'd1));
				st <= S_SYN_RD;
			end
		end

		//------------------------------------------------------------
		// decode: Berlekamp-Massey, 4 iterations
		//------------------------------------------------------------
		S_BM_DELTA: begin : bmdelta
			reg [7:0] s0, s1v, s2v, s3v, s4v;
			reg [7:0] delta;
			s0 = 8'h01; s1v = syn1; s2v = syn2; s3v = syn3; s4v = syn4;
			delta = gmul(sg0, (bm_l == 3'd1) ? s1v :
			                  (bm_l == 3'd2) ? s2v :
			                  (bm_l == 3'd3) ? s3v : s4v);
			delta = delta ^ gmul(sg1, (bm_l == 3'd1) ? s0 :
			                          (bm_l == 3'd2) ? s1v :
			                          (bm_l == 3'd3) ? s2v : s3v);
			if (bm_l >= 3'd2)
				delta = delta ^ gmul(sg2, (bm_l == 3'd2) ? s0 :
				                          (bm_l == 3'd3) ? s1v : s2v);
			if (bm_l >= 3'd3)
				delta = delta ^ gmul(sg3, (bm_l == 3'd3) ? s0 : s1v);
			if (bm_l == 3'd4)
				delta = delta ^ gmul(sg4, s0);
			bm_delta <= delta;
			// method selection, as in rs_decode_string()
			if (delta == 0 || {1'b0, bm_d} > ((bm_l + 1'd1) >> 1) ||
			    ({1'b0, bm_d} == ((bm_l + 1'd1) >> 1) && bm_l[0] && !bm_b)) begin
				bm_method_a <= 1;
				st <= S_BM_APPLY;
			end
			else begin
				bm_method_a <= 0;
				inv_x <= delta;
				inv_t <= delta;
				inv_step <= 0;
				st <= S_BM_INV;
			end
		end

		S_BM_INV: begin
			// inv_t: 6 x (square, multiply by x) then a final square
			if (inv_step < 4'd12) begin
				if (inv_step[0]) inv_t <= gmul(inv_t, inv_x);
				else             inv_t <= gmul(inv_t, inv_t);
				inv_step <= inv_step + 1'd1;
			end
			else begin
				inv_t <= gmul(inv_t, inv_t);
				st <= S_BM_APPLY;
			end
		end

		S_BM_APPLY: begin : bmapply
			reg [7:0] osg [0:4];
			reg [7:0] oom [0:4];
			reg [7:0] ota [0:4];
			reg [7:0] oga [0:4];
			reg [7:0] nsg [0:4];
			reg [7:0] nom [0:4];
			reg [7:0] nta [0:4];
			reg [7:0] nga [0:4];
			osg[0]=sg0; osg[1]=sg1; osg[2]=sg2; osg[3]=sg3; osg[4]=sg4;
			oom[0]=om0; oom[1]=om1; oom[2]=om2; oom[3]=om3; oom[4]=om4;
			ota[0]=ta0; ota[1]=ta1; ota[2]=ta2; ota[3]=ta3; ota[4]=ta4;
			oga[0]=ga0; oga[1]=ga1; oga[2]=ga2; oga[3]=ga3; oga[4]=ga4;
			for (i = 0; i < 5; i = i + 1) begin
				nsg[i]=osg[i]; nom[i]=oom[i]; nta[i]=ota[i]; nga[i]=oga[i];
			end
			if (bm_method_a) begin
				// tau/gamma multiplied by x, then sigma/omega corrected
				for (i = 4; i > 0; i = i - 1) begin
					nta[i] = ota[i-1];
					nga[i] = oga[i-1];
				end
				nta[0] = 0;
				nga[0] = 0;
				if (bm_delta != 0) begin
					for (i = 1; i < 5; i = i + 1) begin
						nsg[i] = osg[i] ^ gmul(nta[i], bm_delta);
						nom[i] = oom[i] ^ gmul(nga[i], bm_delta);
					end
				end
			end
			else begin
				bm_d <= bm_l - bm_d;
				bm_b <= !bm_b;
				// all reads use the pre-iteration values (the C loop
				// runs downward and touches each index once)
				for (i = 4; i > 0; i = i - 1) begin
					nsg[i] = osg[i] ^ gmul(ota[i-1], bm_delta);
					nom[i] = oom[i] ^ gmul(oga[i-1], bm_delta);
					nta[i-1] = gmul(osg[i-1], inv_t);
					nga[i-1] = gmul(oom[i-1], inv_t);
				end
			end
			sg0<=nsg[0]; sg1<=nsg[1]; sg2<=nsg[2]; sg3<=nsg[3]; sg4<=nsg[4];
			om0<=nom[0]; om1<=nom[1]; om2<=nom[2]; om3<=nom[3]; om4<=nom[4];
			ta0<=nta[0]; ta1<=nta[1]; ta2<=nta[2]; ta3<=nta[3]; ta4<=nta[4];
			ga0<=nga[0]; ga1<=nga[1]; ga2<=nga[2]; ga3<=nga[3]; ga4<=nga[4];
			if (bm_l == 3'd4) st <= S_CLASSIFY;
			else begin
				bm_l <= bm_l + 1'd1;
				st <= S_BM_DELTA;
			end
		end

		//------------------------------------------------------------
		// decode: locate and fix
		//------------------------------------------------------------
		S_CLASSIFY: begin
			if (sg3 != 0 || sg4 != 0) begin
				cw_fail <= 1;
				st <= S_CW_NEXT;
			end
			else if (sg2 != 0 && sg0 != 0) begin
				two_errors <= 1;
				root_k <= 0;
				root_q <= 8'h01;
				have_root1 <= 0;
				st <= S_ROOT;
			end
			else if (sg1 != 0 && sg0 != 0) begin
				// single error: epos = log(sigma1/sigma0)
				inv_x <= sg0;
				inv_t <= sg0;
				inv_step <= 0;
				st <= S_SE_INV;
			end
			else begin
				cw_fail <= 1;
				st <= S_CW_NEXT;
			end
		end

		S_SE_INV: begin
			if (inv_step < 4'd12) begin
				if (inv_step[0]) inv_t <= gmul(inv_t, inv_x);
				else             inv_t <= gmul(inv_t, inv_t);
				inv_step <= inv_step + 1'd1;
			end
			else begin
				log_x <= gmul(sg1, gmul(inv_t, inv_t));
				log_k <= 0;
				log_p <= 8'h01;
				st <= S_SE_LOG;
			end
		end
		S_SE_LOG: begin
			if (log_p == log_x) begin
				if (log_k > 9'd35) begin
					cw_fail <= 1;
					st <= S_CW_NEXT;
				end
				else begin
					epos1 <= log_k[5:0];
					err1 <= sg1 ^ om1;
					b_addr <= cwa(cols, cw, 6'd35 - log_k[5:0]);
					st <= S_FIX1_RD;
				end
			end
			else if (log_k == 9'd255) begin
				cw_fail <= 1;
				st <= S_CW_NEXT;
			end
			else begin
				log_k <= log_k + 1'd1;
				log_p <= gmul(log_p, 8'h02);
			end
		end

		S_ROOT: begin : root
			reg [7:0] res;
			res = sg0 ^ gmul(sg1, root_q) ^ gmul(sg2, gmul(root_q, root_q));
			if (res == 0 && !have_root1) begin
				if (root_k > 9'd35 || root_k == 9'd255) begin
					cw_fail <= 1;
					st <= S_CW_NEXT;
				end
				else begin
					epos1 <= root_k[5:0];
					q_e1 <= root_q;
					q2_e1 <= gmul(root_q, root_q);
					have_root1 <= 1;
					root_k <= root_k + 1'd1;
					root_q <= gmul(root_q, ALPHA_INV);
				end
			end
			else if (res == 0 && have_root1) begin
				if (root_k > 9'd35) begin
					cw_fail <= 1;
					st <= S_CW_NEXT;
				end
				else begin
					epos2 <= root_k[5:0];
					q_e2 <= root_q;
					q2_e2 <= gmul(root_q, root_q);
					ap1 <= 8'h01;
					ap2 <= 8'h01;
					ap_cnt <= 0;
					st <= S_APOW;
				end
			end
			else if (root_k == 9'd255) begin
				cw_fail <= 1;
				st <= S_CW_NEXT;
			end
			else begin
				root_k <= root_k + 1'd1;
				root_q <= gmul(root_q, ALPHA_INV);
			end
		end

		S_APOW: begin
			// alpha^epos1 and alpha^epos2 by repeated doubling
			if (ap_cnt < epos1) ap1 <= gmul(ap1, 8'h02);
			if (ap_cnt < epos2) ap2 <= gmul(ap2, 8'h02);
			if (ap_cnt >= epos1 && ap_cnt >= epos2) begin : errnum
				reg [7:0] e1, e2;
				e1 = om0 ^ gmul(om1, q_e1) ^ gmul(om2, q2_e1);
				e2 = om0 ^ gmul(om1, q_e2) ^ gmul(om2, q2_e2);
				e1_num <= gmul(e1, ap1);
				e2_num <= gmul(e2, ap2);
				// divisors: 1 ^ alpha^(epos2-epos1) and 1 ^ alpha^(epos1-epos2)
				d1_val <= 8'h01 ^ gmul(ap2, q_e1);
				d2_val <= 8'h01 ^ gmul(ap1, q_e2);
				inv_x <= 8'h01 ^ gmul(ap2, q_e1);
				inv_t <= 8'h01 ^ gmul(ap2, q_e1);
				inv_step <= 0;
				st <= S_DIV1_INV;
			end
			else ap_cnt <= ap_cnt + 1'd1;
		end

		S_DIV1_INV: begin
			if (inv_step < 4'd12) begin
				if (inv_step[0]) inv_t <= gmul(inv_t, inv_x);
				else             inv_t <= gmul(inv_t, inv_t);
				inv_step <= inv_step + 1'd1;
			end
			else begin
				err1 <= gmul(e1_num, gmul(inv_t, inv_t));
				inv_x <= d2_val;
				inv_t <= d2_val;
				inv_step <= 0;
				st <= S_DIV2_INV;
			end
		end
		S_DIV2_INV: begin
			if (inv_step < 4'd12) begin
				if (inv_step[0]) inv_t <= gmul(inv_t, inv_x);
				else             inv_t <= gmul(inv_t, inv_t);
				inv_step <= inv_step + 1'd1;
			end
			else begin
				err2 <= gmul(e2_num, gmul(inv_t, inv_t));
				b_addr <= cwa(cols, cw, 6'd35 - epos1);
				st <= S_FIX1_RD;
			end
		end

		S_FIX1_RD: st <= S_FIX1_WR;
		S_FIX1_WR: begin
			b_addr <= cwa(cols, cw, 6'd35 - epos1);
			b_wdata <= b_rdata ^ err1;
			b_we <= 1;
			err_count <= err_count + 8'd1;
			if (two_errors) begin
				st <= S_FIX2_RD;
			end
			else st <= S_CW_NEXT;
		end
		S_FIX2_RD: begin
			b_addr <= cwa(cols, cw, 6'd35 - epos2);
			st <= S_FIX2_WAIT;
		end
		S_FIX2_WAIT: st <= S_FIX2_WR;
		S_FIX2_WR: begin
			b_addr <= cwa(cols, cw, 6'd35 - epos2);
			b_wdata <= b_rdata ^ err2;
			b_we <= 1;
			err_count <= err_count + 8'd1;
			st <= S_CW_NEXT;
		end

		S_CW_NEXT: begin
			if (cw_fail && cols) fail <= 1;   // uncorrectable column
			cw_fail <= 0;
			if (cols ? (cw == 6'd31) : (cw == 6'd35)) begin
				if (!cols) begin
					cols <= 1;
					cw <= 0;
					st <= S_DEC_CW;
				end
				else begin
					// compact rows 1..31 back to 32-byte pitch
					row <= 6'd1;
					col <= 6'd0;
					st <= S_CMP_RD;
				end
			end
			else begin
				cw <= cw + 1'd1;
				st <= S_DEC_CW;
			end
		end

		//------------------------------------------------------------
		// decode: compact
		//------------------------------------------------------------
		S_CMP_RD: begin
			b_addr <= 11'd36 * row + {5'd0, col};
			st <= S_CMP_WAIT;
		end
		S_CMP_WAIT: st <= S_CMP_CAP;
		S_CMP_CAP: begin
			b_addr <= 11'd32 * row + {5'd0, col};
			b_wdata <= b_rdata;
			b_we <= 1;
			if (col != 6'd31) begin
				col <= col + 1'd1;
				st <= S_CMP_RD;
			end
			else if (row != 6'd31) begin
				row <= row + 1'd1;
				col <= 0;
				st <= S_CMP_RD;
			end
			else st <= S_DONE;
		end

		S_DONE: begin
			done <= 1;
			st <= S_IDLE;
		end

		default: st <= S_IDLE;
		endcase
	end
end

endmodule
