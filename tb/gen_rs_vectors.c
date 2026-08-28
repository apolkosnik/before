/* Golden vector generator: drives the real Previous rs.c */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "stub.h"
void rs_encode(Uint8 *sector);
int rs_decode(Uint8 *sector);

static Uint8 sec[1296], enc[1296], cor[1296], dec[1296];

static void dump(FILE *f, Uint8 *b, int n) {
    for (int i = 0; i < n; i++) fprintf(f, "%02x\n", b[i]);
}

int main(void) {
    srand(12345);
    FILE *f = fopen("vectors.hex", "w");

    /* vector 1: fixed pattern like the POST uses (0,1,2,...) */
    for (int i = 0; i < 1024; i++) sec[i] = i & 0xff;
    memcpy(enc, sec, 1024);
    rs_encode(enc);
    dump(f, sec, 1024);          /* data in */
    dump(f, enc, 1296);          /* encoded out */

    /* corrupt with the POST-like pattern: scattered plus a 32 byte run */
    memcpy(cor, enc, 1296);
    cor[0x001] = ~cor[0x001];
    cor[0x032] += 1;
    cor[0x064] = (Uint8)(~cor[0x064] + 0x9c);
    cor[0x0c8] += 0xff;
    cor[0x309] = ~cor[0x309];
    cor[0x37a] += 0x17;
    cor[0x457] = cor[0x456] + 0x27;
    cor[0x50a] += 0x16;
    for (int i = 0; i < 32; i++) {
        Uint8 v = cor[0x215 + i];
        Uint32 q = (Uint32)((Uint8)~v) / 0x2f;
        (void)q;
        Uint32 r = (Uint32)((Uint8)~v) % 0x2f;
        cor[0x215 + i] = r + 0x25;
    }
    memcpy(dec, cor, 1296);
    int n = rs_decode(dec);
    printf("golden: corrected %d errors (POST expects 0x24 = 36)\n", n);
    dump(f, cor, 1296);          /* corrupted in */
    dump(f, dec, 1024);          /* decoded out */
    FILE *g = fopen("count.txt", "w");
    fprintf(g, "%d\n", n);
    fclose(g);

    /* vector 2: random data, random correctable corruption */
    for (int i = 0; i < 1024; i++) sec[i] = rand() & 0xff;
    memcpy(enc, sec, 1024);
    rs_encode(enc);
    dump(f, sec, 1024);
    dump(f, enc, 1296);
    memcpy(cor, enc, 1296);
    /* one error per row keeps everything correctable */
    for (int r = 0; r < 36; r += 2) cor[36*r/36*36 + (r*7)%36] ^= 0x5a + r;
    memcpy(dec, cor, 1296);
    n = rs_decode(dec);
    printf("golden: vector 2 corrected %d errors\n", n);
    dump(f, cor, 1296);
    dump(f, dec, 1024);
    FILE *h = fopen("count2.txt", "w");
    fprintf(h, "%d\n", n);
    fclose(h);

    fclose(f);
    return 0;
}
