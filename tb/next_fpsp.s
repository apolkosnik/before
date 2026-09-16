; Probe actual FPSP handlers in NeXT Mach 3.3 or Improv Mach 2.0 mk-94.
; Externally supplied sdmach is loaded at $04000000, not included here.
        ifd     USERMODE
RUNSP   equ     $d000
        else
RUNSP   equ     $e000
        endif
        org     0
        dc.l    $e000,start
        rept    254
        dc.l    fail
        endr
start:
        move.w  #$2700,sr
        ifd     IMPROV
        move.l  #$04080118,($2c).w ; old non-Turbo unimp_hd
        move.l  #$04080350,($dc).w ; old unsupported-data handler
        move.b  #1,($04098770).l
        else
        move.l  #$04008946,($2c).w ; F-line glue -> fpsp_unimp
        move.l  #$0400895c,($dc).w ; unsupported data -> fpsp_unsupp
        move.b  #1,($040b6348).l   ; 68040 glue selector
        endif
        move.l  #$80008000,d0
        movec   d0,cacr
        moveq   #0,d0
        fmove.l d0,fpcr
        ifd     USERMODE
        movea.l #RUNSP,a0
        move    a0,usp
        move.w  #0,sr
        endif

        ifd     IMPROV_TRACE_ONLY
        bra     probe_one
        endif
        ifd     IMPROV_PACKED_ONLY
        bra     probe_packed
        endif
        move.w  #1,($f100).l
        fmove.l d0,fp0
        fsin.x  fp0
        fmove.l fp0,d1
        tst.l   d1
        bne     fail
        cmpa.l  #RUNSP,sp
        bne     fail

        move.w  #10,($f100).l
        move.l  #$3fc00000,d0
        fmove.s d0,fp0
        fintrz.x fp0
        fmove.l fp0,d1
        cmpi.l  #1,d1
        bne     fail
        cmpa.l  #RUNSP,sp
        bne     fail

        move.w  #11,($f100).l
        move.l  #$3fc00000,d0
        fmove.s d0,fp0
        fint.x  fp0
        fmove.l fp0,d1
        cmpi.l  #2,d1
        bne     fail
        cmpa.l  #RUNSP,sp
        bne     fail

probe_one:
        move.w  #12,($f100).l
        moveq   #1,d0
        fmove.l d0,fp0
        fsin.x  fp0
        fmove.l fp0,d1
        cmpi.l  #1,d1
        bne     fail
        cmpa.l  #RUNSP,sp
        bne     fail

        ifd     IMPROV_TRACE_ONLY
        bra     done
        endif

probe_packed:
        move.w  #2,($f100).l
        moveq   #1,d0
        fmove.l d0,fp0
        lea     ($3000).w,a0
        fmove.p fp0,(a0)+
        cmpa.l  #$300c,a0
        bne     fail
        cmpa.l  #RUNSP,sp
        bne     fail
        cmpi.l  #1,($3000).w
        bne     fail
        tst.l   ($3004).w
        bne     fail
        tst.l   ($3008).w
        bne     fail

        ifd     IMPROV
        ifnd    CHECK_IMPROV_PACKED
        ; The frame-revision regression ends here for the old kernel.
        ; BUSY-frame execution is implemented, but the exact old kernel
        ; still prepares an incorrect nonzero packed operand. Keep opt-in.
        bra     done
        endif
        endif

        move.w  #3,($f100).l
        lea     ($3000).w,a0
        fmove.p (a0)+,fp1
        fmove.l fp1,d1
        cmpi.l  #1,d1
        bne     fail
        cmpa.l  #$300c,a0
        bne     fail
        cmpa.l  #RUNSP,sp
        bne     fail

        ; Exact opcode and FP0 value recovered from the hardware panic:
        ; libsys_s.B.shlib at $05054296 uses a dynamic k-factor in D0.
        move.w  #4,($f100).l
        lea     sample(pc),a0
        fmovem.x (a0),fp0
        lea     ($3000).w,a0
        moveq   #-1,d0
        dc.w    $f210,$7c00      ; fmove.p fp0,(a0){d0}
        cmpa.l  #RUNSP,sp
        bne     fail
        cmpi.l  #$40010001,($3000).w ; rounded to one decimal place: 0.1
        bne     fail
        tst.l   ($3004).w
        bne     fail
        tst.l   ($3008).w
        bne     fail

        move.w  #5,($f100).l
        fint.p  ($3000).w,fp2    ; packed operand, FPSP-emulated opcode
        fmove.l fp2,d1
        tst.l   d1
        bne     fail
        cmpa.l  #RUNSP,sp
        bne     fail

        ifnd    IMPROV
        ; Exercise normalized ET15 from the unchanged NeXT FPSP without
        ; its FP1 scratch-register bug masking the restore result.
        move.w  #13,($f100).l
        fmove.l #1,fp4
        fadd.p  ($3000).w,fp4    ; 1.0 + 0.1; FP4 survives decbin
        fmove.d fp4,($3050).w
        cmpi.l  #$3ff19999,($3050).w
        bne     fail
        cmpi.l  #$9999999a,($3054).w
        bne     fail
        cmpa.l  #RUNSP,sp
        bne     fail
        endif

        ; Separate operand-preparation issue in the exact kernel: restored
        ; FPTEMP is not the expected 1.0. Keep an opt-in replay reproducer.
        ifd     CHECK_REPLAY
        move.w  #6,($f100).l
        fadd.p  ($3000).w,fp1    ; 1.0 + 0.1, dyadic packed operand
        fmove.d fp1,($3040).w
        cmpi.l  #$3ff19999,($3040).w
        bne     fail
        cmpi.l  #$9999999a,($3044).w
        bne     fail
        cmpa.l  #RUNSP,sp
        bne     fail
        endif

done:
        move.w  #$600d,($f102).l
        bra.s   *
fail:
        move.w  #$bad0,($f102).l
        bra.s   *
sample:
        dc.l    $3ffc0000,$8103c800,$00000000
