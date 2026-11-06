/* Rate at which a plain-content frame with 1 to 8 random bytes appended is accepted (the CRC-16 predicts 1 in 65,536).
 * Build from the repository root after `make build/hb_test`:  cc -O2 -std=c99 -Itools/hb -Ireference tools/hb/rawrate.c build/hb.o build/hbi.o build/hbc.o -o build/rawrate
 * Run:  build/rawrate build/hb_packets.txt 20000000   (2 Nov 2026: 271 of 20,000,000 accepted, 1 in 73,801) */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include "hb.h"
static uint64_t st = 88172645463325252ULL;
static uint32_t rnd(void) { st ^= st << 13; st ^= st >> 7; st ^= st << 17; return (uint32_t)(st >> 11); }
int main(int argc, char **argv) {
    (void)argc;
    FILE *f = fopen(argv[1], "r"); char line[1000]; static uint8_t frames[4000][256]; static size_t fl[4000]; int nf = 0;
    long trials = atol(argv[2]), acc = 0, t; char tn[900];
    if (hb_load_list("tools/hybrid/tables/tocall_set1.txt") != 62) return 2;
    while (fgets(line, sizeof line, f) && nf < 4000) {
        size_t l = strlen(line); hb_info_t in; size_t w;
        while (l && (line[l-1] == '\n' || line[l-1] == '\r')) line[--l] = 0;
        if (!l) continue;
        w = hb_encode(line, l, NULL, frames[nf], 256, &in);
        if (w && in.cc == 1) fl[nf++] = w;
    }
    printf("%d raw frames\n", nf);
    for (t = 0; t < trials; t++) {
        uint8_t b[300]; int i = (int)(rnd() % (uint32_t)nf); int k = 1 + (int)(rnd() % 8), q;
        memcpy(b, frames[i], fl[i]);
        for (q = 0; q < k; q++) b[fl[i] + q] = (uint8_t)rnd();
        if (hb_decode(b, fl[i] + k, tn, sizeof tn, NULL) > 0) acc++;
    }
    printf("%ld trials, %ld accepted, 1 in %.0f (CRC-16 predicts 1 in 65536)\n", trials, acc, acc ? (double)trials / acc : 0.0);
    return 0;
}
