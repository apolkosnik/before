; Reproduce Workspace's floatFor:default: path with the exact Improv libc.
; The bench maps user libc data separately from supervisor kernel text.
; MMU and caches stay disabled: this is a CPU/FPSP test, not an OS boot.
        org     0
        dc.l    $e000,start
        rept    254
        dc.l    fail
        endr
start:
        move.w  #$2700,sr
        move.l  #$04080118,($2c).w
        move.l  #$04080350,($dc).w
        move.b  #1,($04098770).l
        fmove.l #0,fpcr
        movea.l #$d000,a0
        move    a0,usp
        move.w  #0,sr
        moveq   #3,d7
again:
        move.w  #30,($f100).l
        ; Observed stale value; each sscanf("0","%f") must replace it.
        move.l  #$42dd5233,d0
        fmove.s d0,fp0
        move.l  #$deadbeef,($3000).l
        pea     ($3000).l
        pea     fmt(pc)
        pea     zero(pc)
        jsr     ($05002fde).l
        adda.w  #12,sp
        cmp.l   #1,d0
        bne     fail
        tst.l   ($3000).l
        bne     fail
        dbra    d7,again
        cmpa.l  #$d000,sp
        bne     fail
        move.w  #$600d,($f102).l
        bra.s   *
fail:
        move.w  #$bad0,($f102).l
        bra.s   *
zero:   dc.b    "0",0
fmt:    dc.b    "%f",0
        even
