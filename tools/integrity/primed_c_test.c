/* Check C on primed frames: does decode + re-encode + compare catch a decode made with the WRONG reference?
 *   cc -std=c99 -O2 -I reference tools/integrity/primed_c_test.c model/aprs_ppm_model_v1.c -o build/primed_c_test
 *   build/primed_c_test texts.txt        (one APRS text per line, oldest first; real texts are not in this repository)
 * For each packet with an earlier packet of the same station: encode it primed against the true reference (the previous packet), then decode it
 * against other wrong references (earlier packets of that station, and the true reference with 1 or 2 characters changed), with the reference
 * check byte IGNORED so every wrong reference gets a chance (the 8-bit reference check is a separate filter). Each wrong decode is judged by
 *   S  station id byte      T  text check byte (CRC-8 0x1D)      C  re-encode the decoded text against the same wrong reference and compare the payload bytes
 * and the table says how many wrong texts each combination lets through. */
#include "../../reference/aprs_ppm.c"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static size_t raw_decode(const uint8_t *ref, size_t rl, const uint8_t *f, size_t fl, uint8_t *text)
{
    decoder_t d; uint16_t freq[256]; size_t i, n = f[6];
    dec_init(&d, f + 7, fl - 7);
    for (i = 0; i < n; i++) {
        uint32_t total = build_freq(text, i, freq, ref, rl), v = dec_target(&d, total), cum = 0; int s = 0;
        while (s < 255 && cum + freq[s] <= v) { cum += freq[s]; s++; }
        dec_consume(&d, cum, freq[s]); text[i] = (uint8_t)s;
    }
    return n;
}

static long n_cases, n_same, n_wrong, w_S, w_T, w_C, w_ST, w_SC, w_STC, w_TC, n_intact, n_intact_C;
static void judge(const uint8_t *ref, size_t rl, const uint8_t *frame, size_t fl, const uint8_t *text, size_t tl)
{
    uint8_t back[300] = {0}, again[400]; size_t bl = raw_decode(ref, rl, frame, fl, back), al;
    int S, T, C;
    n_cases++;
    if (bl == tl && !memcmp(back, text, tl)) { n_same++; return; }          /* wrong reference, right text anyway: harmless */
    n_wrong++;
    S = aprs_ppm_station_id(back, bl) == frame[3];
    T = aprs_ppm_text_check(back, bl) == frame[5];
    al = aprs_ppm_encode_primed(ref, rl, back, bl, again, sizeof again);
    C = al == fl && !memcmp(again + 7, frame + 7, fl - 7);
    w_S += S; w_T += T; w_C += C; w_ST += S && T; w_SC += S && C; w_TC += T && C; w_STC += S && T && C;
}

int main(int argc, char **argv)
{
    static char lines[20000][400]; static size_t len[20000]; int n = 0, i, j; FILE *f; uint32_t rng = 777;
    if (argc < 2 || !(f = fopen(argv[1], "r"))) { fprintf(stderr, "usage: primed_c_test texts.txt\n"); return 2; }
    while (n < 20000 && fgets(lines[n], sizeof lines[n], f)) { len[n] = strcspn(lines[n], "\r\n"); lines[n][len[n]] = 0; if (len[n] > 0) n++; }
    fclose(f);
    for (i = 1; i < n; i++) {
        int prev[8], np = 0; size_t sl = strcspn(lines[i], ">"); uint8_t frame[400];
        for (j = i - 1; j >= 0 && np < 8; j--) if (strcspn(lines[j], ">") == sl && !strncmp(lines[j], lines[i], sl) && strcmp(lines[j], lines[i])) prev[np++] = j;
        if (!np) continue;
        {   /* true reference = the previous packet of the station; the frame must exist */
            size_t fl = aprs_ppm_encode_primed((uint8_t *)lines[prev[0]], len[prev[0]], (uint8_t *)lines[i], len[i], frame, sizeof frame);
            uint8_t back[300], again[400];
            if (!fl) continue;
            n_intact++;
            { size_t bl = aprs_ppm_decode_primed((uint8_t *)lines[prev[0]], len[prev[0]], frame, fl, back, sizeof back);
              size_t al = aprs_ppm_encode_primed((uint8_t *)lines[prev[0]], len[prev[0]], back, bl, again, sizeof again);
              if (bl == len[i] && al == fl && !memcmp(again, frame, fl)) n_intact_C++; }
            for (j = 1; j < np; j++) judge((uint8_t *)lines[prev[j]], len[prev[j]], frame, fl, (uint8_t *)lines[i], len[i]);
            for (j = 0; j < 6; j++) {                                   /* the true reference with 1 or 2 characters changed */
                uint8_t m[400]; size_t r = len[prev[0]];
                memcpy(m, lines[prev[0]], r);
                rng = rng * 1664525u + 1013904223u; m[(rng >> 8) % r] = (uint8_t)"0123456789"[(rng >> 20) % 10];
                if (j & 1) { rng = rng * 1664525u + 1013904223u; m[(rng >> 8) % r] = (uint8_t)"abcdefghij0123"[(rng >> 20) % 14]; }
                if (!memcmp(m, lines[prev[0]], r)) continue;
                judge(m, r, frame, fl, (uint8_t *)lines[i], len[i]);
            }
        }
    }
    printf("intact frames (true reference): %ld, accepted by C: %ld\n", n_intact, n_intact_C);
    printf("wrong-reference decodes: %ld; %ld came out as the right text anyway; %ld came out WRONG\n", n_cases, n_same, n_wrong);
    printf("of the %ld wrong texts, with the reference check byte ignored, passed:\n", n_wrong);
    printf("  S station id only        %ld\n  T text check only        %ld\n  C re-encode only         %ld\n", w_S, w_T, w_C);
    printf("  S+T (today)              %ld\n  S+C                      %ld\n  T+C                      %ld\n  S+T+C                    %ld\n", w_ST, w_SC, w_TC, w_STC);
    return 0;
}
