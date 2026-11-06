/* Test-vector tool for the APRS-PPM reference codec.
 *   ppm_tool gen   < packets.txt > vectors.txt     one packet per line in; "<frame hex> <text hex>" out
 *   ppm_tool check   vectors.txt                   verify encode == frame and decode round-trips
 *   ppm_tool stats < packets.txt                   mean sizes
 * A frame of "-" means the codec declines (not smaller): the sender uses the plain frame. */
#include "aprs_ppm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void hex(const uint8_t *p, size_t n, FILE *f) { size_t i; for (i = 0; i < n; i++) fprintf(f, "%02x", p[i]); }

static int unhex(const char *s, uint8_t *out, size_t cap, size_t *n)
{
    size_t i = 0;
    while (s[0] && s[1] && s[0] != '\n' && s[0] != '\r') {
        unsigned v; char b[3] = { s[0], s[1], 0 };
        if (i >= cap || sscanf(b, "%2x", &v) != 1) return -1;
        out[i++] = (uint8_t)v; s += 2;
    }
    *n = i; return 0;
}

static void emit(const uint8_t *text, size_t n)
{
    uint8_t frame[300];
    size_t fl = aprs_ppm_encode_frame(text, n, frame, sizeof frame);
    if (fl) hex(frame, fl, stdout); else fputs("-", stdout);
    fputc(' ', stdout); hex(text, n, stdout); fputc('\n', stdout);
}

static int gen(void)
{
    char line[1024]; uint8_t buf[300]; size_t i;
    printf("# APRS-PPM test vectors, model version %u. Format: <frame hex or -> <text hex>\n", aprs_ppm_model_version());
    while (fgets(line, sizeof line, stdin)) {
        size_t n = strlen(line);
        while (n && (line[n - 1] == '\n' || line[n - 1] == '\r')) n--;
        if (n == 0 || line[0] == '#' || n > 255) continue;
        emit((const uint8_t *)line, n);
    }
    /* edge cases */
    buf[0] = 'A'; emit(buf, 1);
    memset(buf, 'A', 255); emit(buf, 255);
    memset(buf, 0, 40); emit(buf, 40);
    for (i = 0; i < 255; i++) buf[i] = (uint8_t)(i + 1);
    emit(buf, 255);
    for (i = 0; i < 100; i++) buf[i] = (uint8_t)((i * 37 + 11) & 0xFF);
    emit(buf, 100);
    memcpy(buf, "`c5Zl\x1c>/\"4}=", 12); emit(buf, 12);
    return 0;
}

static int check(const char *path)
{
    FILE *f = fopen(path, "r"); char line[2048]; int bad = 0, n = 0;
    if (!f) { perror(path); return 2; }
    while (fgets(line, sizeof line, f)) {
        uint8_t text[300], frame[300], enc[300], back[300]; size_t tl, fl = 0, el, bl; char *sp;
        if (line[0] == '#' || line[0] == '\n') continue;
        sp = strchr(line, ' ');
        if (!sp) { bad++; continue; }
        *sp = 0;
        if (unhex(sp + 1, text, sizeof text, &tl)) { bad++; continue; }
        if (strcmp(line, "-") != 0 && unhex(line, frame, sizeof frame, &fl)) { bad++; continue; }
        el = aprs_ppm_encode_frame(text, tl, enc, sizeof enc);
        n++;
        if (el != fl || (el && memcmp(enc, frame, el))) { printf("ENCODE MISMATCH on vector %d\n", n); bad++; continue; }
        if (fl) {
            bl = aprs_ppm_decode_frame(frame, fl, back, sizeof back);
            if (bl != tl || memcmp(back, text, tl)) { printf("DECODE MISMATCH on vector %d\n", n); bad++; }
        }
    }
    fclose(f);
    printf("%d vectors, %d failures\n", n, bad);
    return bad ? 1 : 0;
}

static int stats(void)
{
    char line[1024]; size_t n = 0, plain = 0, sent = 0;
    while (fgets(line, sizeof line, stdin)) {
        uint8_t frame[300]; size_t l = strlen(line), fl;
        while (l && (line[l - 1] == '\n' || line[l - 1] == '\r')) l--;
        if (l == 0 || l > 255) continue;
        fl = aprs_ppm_encode_frame((const uint8_t *)line, l, frame, sizeof frame);
        n++; plain += 3 + l; sent += fl ? fl : 3 + l;
    }
    if (n) printf("%zu packets: plain %.1f B -> %.1f B (%.0f%% saved)\n", n, (double)plain / n, (double)sent / n, 100.0 * (1.0 - (double)sent / plain));
    return 0;
}

static int hex_in(const char *s, uint8_t *out, size_t cap, size_t *n)
{
    return unhex(s, out, cap, n);
}

/* gen-primed: lines "reference<TAB>text" in; "<reference hex> <frame hex or -> <text hex>" out. */
static int gen_primed(void)
{
    char line[2048]; uint8_t frame[300];
    printf("# APRS-PPM primed test vectors, model version %u. Format: <reference hex> <frame hex or -> <text hex>\n", aprs_ppm_model_version());
    while (fgets(line, sizeof line, stdin)) {
        char *tab; size_t rl, tl, fl;
        if (line[0] == '#' || line[0] == '\n') continue;
        tab = strchr(line, '\t');
        if (!tab) continue;
        *tab = 0;
        rl = strlen(line); tl = strlen(tab + 1);
        while (tl && (tab[tl] == '\n' || tab[tl] == '\r')) tl--;
        fl = aprs_ppm_encode_primed((const uint8_t *)line, rl, (const uint8_t *)(tab + 1), tl, frame, sizeof frame);
        hex((const uint8_t *)line, rl, stdout); fputc(' ', stdout);
        if (fl) hex(frame, fl, stdout); else fputs("-", stdout);
        fputc(' ', stdout); hex((const uint8_t *)(tab + 1), tl, stdout); fputc('\n', stdout);
    }
    return 0;
}

/* check-primed: encode must reproduce the frame; decode with the reference must reproduce the text;
 * decode with a one-byte-changed reference must fail. */
static int check_primed(const char *path)
{
    FILE *f = fopen(path, "r"); char line[4096]; int bad = 0, n = 0, wrongref = 0;
    if (!f) { perror(path); return 2; }
    while (fgets(line, sizeof line, f)) {
        uint8_t ref[300], text[300], frame[400], enc[400], back[400]; size_t rl, tl, fl = 0, el, bl;
        char *a, *b;
        if (line[0] == '#' || line[0] == '\n') continue;
        a = strchr(line, ' '); if (!a) { bad++; continue; } *a++ = 0;
        b = strchr(a, ' '); if (!b) { bad++; continue; } *b++ = 0;
        if (hex_in(line, ref, sizeof ref, &rl) || hex_in(b, text, sizeof text, &tl)) { bad++; continue; }
        if (strcmp(a, "-") != 0 && hex_in(a, frame, sizeof frame, &fl)) { bad++; continue; }
        el = aprs_ppm_encode_primed(ref, rl, text, tl, enc, sizeof enc);
        n++;
        if (el != fl || (el && memcmp(enc, frame, el))) { printf("PRIMED ENCODE MISMATCH on vector %d\n", n); bad++; continue; }
        if (fl) {
            bl = aprs_ppm_decode_primed(ref, rl, frame, fl, back, sizeof back);
            if (bl != tl || memcmp(back, text, tl)) { printf("PRIMED DECODE MISMATCH on vector %d\n", n); bad++; continue; }
            ref[rl / 2] ^= 1;
            bl = aprs_ppm_decode_primed(ref, rl, frame, fl, back, sizeof back);
            if (bl != 0) { printf("WRONG REFERENCE NOT REJECTED on vector %d\n", n); bad++; } else wrongref++;
        }
    }
    fclose(f);
    printf("%d primed vectors, %d failures, %d wrong-reference frames rejected\n", n, bad, wrongref);
    return bad ? 1 : 0;
}

/* collide: the failure F4MLV reported. For each primed vector, build stale references for the same station that have
 * the same CRC-8 as the true reference (a receiver that missed an update and kept an old packet), decode the real
 * frame with each, and count how many decode to the wrong text without an error. Also cuts bytes off the end of each
 * frame and counts how many still decode to the wrong text. A fixed seed keeps the numbers the same on every run.
 * The text check is one CRC-8, so about 1 in 256 of the wrong decodes still gets through: that is a measured residual,
 * not zero, and the test fails only if it rises above 1% of cases. */
static uint32_t rng_state = 12345;
static uint32_t rnd(void) { rng_state = rng_state * 1664525u + 1013904223u; return rng_state >> 8; }

static int collide(const char *path)
{
    FILE *f = fopen(path, "r"); char line[4096]; int vectors = 0;
    long cases = 0, rejected = 0, right = 0, wrong = 0, cuts = 0, cut_ok = 0, cut_wrong = 0;
    if (!f) { perror(path); return 2; }
    while (fgets(line, sizeof line, f)) {
        uint8_t ref[300], text[300], frame[400], stale[300], back[400]; size_t rl, tl, fl = 0, k;
        uint8_t seen[20][300]; char *a, *b; int made = 0, j, dup; long tries = 0;
        if (line[0] == '#' || line[0] == '\n') continue;
        a = strchr(line, ' '); if (!a) continue; *a++ = 0;
        b = strchr(a, ' '); if (!b) continue; *b++ = 0;
        if (hex_in(line, ref, sizeof ref, &rl) || hex_in(b, text, sizeof text, &tl)) continue;
        if (!strcmp(a, "-") || hex_in(a, frame, sizeof frame, &fl)) continue;
        vectors++;
        while (made < 20 && tries++ < 200000) {
            size_t sl = rl, i = rnd() % sl;
            memcpy(stale, ref, rl);
            stale[i] = (uint8_t)"0123456789"[rnd() % 10];
            if (rnd() & 1) stale[rl - 1 - rnd() % (rl > 8 ? 8 : rl)] = (uint8_t)"abcdefghij0123"[rnd() % 14];
            if (!memcmp(stale, ref, rl) || aprs_ppm_crc8(stale, sl) != aprs_ppm_crc8(ref, rl)) continue;
            for (dup = 0, j = 0; j < made; j++) if (!memcmp(seen[j], stale, rl)) dup = 1;
            if (dup) continue;                      /* count each distinct stale reference once */
            memcpy(seen[made], stale, rl);
            made++; cases++;
            k = aprs_ppm_decode_primed(stale, sl, frame, fl, back, sizeof back);
            if (!k) rejected++; else if (k == tl && !memcmp(back, text, tl)) right++; else wrong++;
        }
        for (k = 1; k <= 3 && k + 7 < fl; k++) {
            size_t bl = aprs_ppm_decode_primed(ref, rl, frame, fl - k, back, sizeof back);
            cuts++;
            if (!bl) continue;
            if (bl == tl && !memcmp(back, text, tl)) cut_ok++; else cut_wrong++;
        }
    }
    fclose(f);
    printf("%d vectors: %ld stale-reference collisions: %ld rejected, %ld right text anyway, %ld WRONG text accepted\n",
           vectors, cases, rejected, right, wrong);
    printf("%ld frames cut by 1 to 3 bytes: %ld decoded to the wrong text\n", cuts, cut_wrong);
    (void)cut_ok;
    return (cases == 0 || wrong * 100 > cases) ? 1 : 0;
}

int main(int argc, char **argv)
{
    if (argc >= 2 && !strcmp(argv[1], "gen")) return gen();
    if (argc >= 3 && !strcmp(argv[1], "check")) return check(argv[2]);
    if (argc >= 2 && !strcmp(argv[1], "stats")) return stats();
    if (argc >= 2 && !strcmp(argv[1], "gen-primed")) return gen_primed();
    if (argc >= 3 && !strcmp(argv[1], "check-primed")) return check_primed(argv[2]);
    if (argc >= 3 && !strcmp(argv[1], "collide")) return collide(argv[2]);
    fprintf(stderr, "usage: ppm_tool gen|stats|gen-primed < packets.txt | ppm_tool check vectors.txt | ppm_tool check-primed vectors.txt\n");
    return 2;
}
