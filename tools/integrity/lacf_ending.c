/* The LACF ending and the receiver's re-encode check (spec/11 payload rules), written from the spec text, on this repository's reference codec, and tested.
 *   make lacf-ending      or      build/lacf_ending [texts.txt]
 * With no argument it uses 3000 fabricated packets. The frame mark is not decided yet (spec/11, "Open"); this test keeps 3C FF 81 so the frames line up
 * with the reference codec's, and changes only the ending. */
#include "../../reference/aprs_ppm.c"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lacf_core.h"
static long lacf_disagree;                                               /* the one pass receiver against the two pass one, on every call */
static size_t checked_receive(const uint8_t *fr, size_t fl, uint8_t *text, size_t *payload, int allow_tail)
{
    uint8_t t2[300]; size_t p2 = 0, p1 = 0, a = lacf_receive(fr, fl, text, &p1, allow_tail), b = lacf_receive_two_pass(fr, fl, t2, &p2, allow_tail);
    if (a != b || (a && (memcmp(text, t2, a) || p1 != p2))) lacf_disagree++;
    if (payload) *payload = p1;
    return a;
}
#define lacf_receive checked_receive

static uint32_t rng = 31102026;
static uint32_t rnd(void) { rng = rng * 1664525u + 1013904223u; return rng >> 8; }
static int synth(char *line, int i)
{
    static const char *cm[] = {"LoRa Tracker", "iGate hill", "balloon", "T-Deck", "", "Bat 3.9V", "QRV 433.775"};
    int lat = 3200 + rnd() % 400, lon = 11000 + rnd() % 500;
    return snprintf(line, 300, "N0CALL-%d>APLRT1%s:!%02d%02d.%02dN/%03d%02d.%02dW>%s %d.%dV", i % 12, (rnd() & 1) ? ",WIDE1-1" : "", lat / 100, lat % 100,
                    (int)(rnd() % 100), lon / 100, lon % 100, (int)(rnd() % 100), cm[rnd() % 7], 3 + (int)(rnd() % 2), (int)(rnd() % 10));
}

/* gen: packets on stdin -> "<frame hex or -> <text hex>" on stdout (fabricated packets only). check FILE: every vector must encode to its frame and be accepted by the receiver. */
static void hexout(const uint8_t *p, size_t n) { size_t i; for (i = 0; i < n; i++) printf("%02x", p[i]); }
static int vectors(int check, const char *path)
{
    char line[1000]; FILE *f = check ? fopen(path, "r") : stdin; int bad = 0, cnt = 0;
    if (!f) { perror(path); return 2; }
    if (!check) printf("# Test vectors for the PPM payload rules of spec/11 on a whole-text TEST frame (3C FF 81, 6 byte header; not a format): model v1 table, deterministic ending. Format: <frame hex or -> <text hex>\n");
    while (fgets(line, sizeof line, f)) {
        uint8_t text[400], fr[400], want[400], back[300]; size_t tl = 0, fl, wl = 0, pay; char *sp;
        if (line[0] == '#' || line[0] == '\n') continue;
        if (!check) {
            tl = strcspn(line, "\r\n"); memcpy(text, line, tl); fl = lacf_encode(text, tl, fr, sizeof fr);
            if (fl) hexout(fr, fl); else fputs("-", stdout);
            fputc(' ', stdout); hexout(text, tl); fputc('\n', stdout);
            continue;
        }
        sp = strchr(line, ' '); if (!sp) continue; *sp++ = 0;
        { size_t i; for (i = 0; sp[i] && sp[i] != '\n' && sp[i + 1]; i += 2) { unsigned x; sscanf(sp + i, "%2x", &x); text[tl++] = (uint8_t)x; } }
        { size_t i; if (strcmp(line, "-")) for (i = 0; line[i] && line[i + 1]; i += 2) { unsigned x; sscanf(line + i, "%2x", &x); want[wl++] = (uint8_t)x; } }
        fl = lacf_encode(text, tl, fr, sizeof fr); cnt++;
        if (fl != wl || (fl && memcmp(fr, want, fl))) { bad++; printf("encode mismatch on vector %d\n", cnt); continue; }
        if (fl && (lacf_receive(fr, fl, back, &pay, 0) != tl || memcmp(back, text, tl))) { bad++; printf("receiver rejects vector %d\n", cnt); }
    }
    if (check) { fclose(f); printf("%d LACF vectors, %d failed\n", cnt, bad); }
    return bad != 0;
}

/* stress TEXTS TRIALS: damage every frame TRIALS times (1, 2, 3, 8 flipped bits, random payload; no tail) and count wrong texts accepted by the re-encode check alone (C, CRC-16 switched off),
 * by the CRC-16 alone (the decoder produced text and the 16 bit check matched), and by both together (the shipping receiver). The damage is in the payload, after the LoRa CRC has been passed. */
static int stress(const char *path, int trials)
{
    static char lines[20000][400]; static size_t len[20000]; int n = 0, i, j, k, t; FILE *f = fopen(path, "r");
    long tries[5] = {0}, dec[5] = {0}, wc[5] = {0}, w16[5] = {0}, wboth[5] = {0}; static const int flips[] = {1, 2, 3, 8, 0};
    if (!f) { perror(path); return 2; }
    while (n < 20000 && fgets(lines[n], sizeof lines[n], f)) { len[n] = strcspn(lines[n], "\r\n"); lines[n][len[n]] = 0; if (len[n]) n++; }
    fclose(f);
    for (i = 0; i < n; i++) {
        uint8_t fr[400], big[400], back[300]; size_t fl = lacf_encode((uint8_t *)lines[i], len[i], fr, sizeof fr), pay;
        if (fl <= LACF_HDR) continue;
        for (j = 0; j < 5; j++) for (t = 0; t < trials; t++) {
            size_t tl2; int wrong;
            memcpy(big, fr, fl);
            if (j < 4) for (k = 0; k < flips[j]; k++) { size_t p = LACF_HDR + rnd() % (fl - LACF_HDR); big[p] ^= (uint8_t)(1u << (rnd() % 8)); }
            else for (k = LACF_HDR; (size_t)k < fl; k++) big[k] = (uint8_t)rnd();
            if (!memcmp(big, fr, fl)) continue;
            tries[j]++;
            tl2 = lacf_decode_raw(big, fl, back);
            wrong = tl2 && (tl2 != len[i] || memcmp(back, lines[i], tl2));
            if (!wrong) continue;
            dec[j]++;
            lacf_crc_on = 0; wc[j] += lacf_receive(big, fl, back, &pay, 0) != 0; lacf_crc_on = 1;
            w16[j] += lacf_crc16(back, tl2) == (uint16_t)(big[4] | (big[5] << 8));
            wboth[j] += lacf_receive(big, fl, back, &pay, 0) != 0;
        }
    }
    printf("%d texts x %d trials per damage type. Wrong text accepted (the LoRa CRC has already passed):\n", n, trials);
    printf("%-26s %9s %9s %24s %20s %22s\n", "damage", "trials", "decoded", "re-encode check (C) alone", "CRC-16 alone", "C + CRC-16 (shipping)");
    for (j = 0; j < 5; j++) printf("%-26s %9ld %9ld %14ld (1 in %-5ld) %12ld (1 in %-6ld) %10ld\n", j < 4 ? (j == 0 ? "1 random bit flipped" : j == 1 ? "2 random bits flipped" : j == 2 ? "3 random bits flipped" : "8 random bits flipped") : "payload replaced by random", tries[j], dec[j], wc[j], wc[j] ? tries[j] / wc[j] : 0, w16[j], w16[j] ? dec[j] / w16[j] : 0, wboth[j]);
    return 0;
}

/* recv: lines "N <frame hex>" (no relay tail allowed) or "T <frame hex>" (a tail allowed) on stdin; prints the accepted text as hex, or "-" for a dropped frame.
 * Used to compare this receiver with the independent Python one (tools/integrity/lacf_py) on the same damaged frames. */
static int recv_mode(void)
{
    static char line[2000]; uint8_t fr[400], text[300]; size_t fl, tl, pay;
    while (fgets(line, sizeof line, stdin)) {
        char *h = line + 2; size_t i;
        if (line[0] != 'N' && line[0] != 'T') continue;
        for (fl = 0; h[2 * fl] && h[2 * fl + 1] && h[2 * fl] != '\n' && fl < sizeof fr; fl++) { unsigned x; sscanf(h + 2 * fl, "%2x", &x); fr[fl] = (uint8_t)x; }
        tl = lacf_receive(fr, fl, text, &pay, line[0] == 'T');
        if (!tl) puts("-"); else { for (i = 0; i < tl; i++) printf("%02x", text[i]); putchar('\n'); }
    }
    return 0;
}

int main(int argc, char **argv)
{
    if (getenv("LACF_NOCRC")) lacf_crc_on = 0;                                  /* measure the re-encode check alone */
    if (argc > 1 && !strcmp(argv[1], "recv")) return recv_mode();
    if (argc > 3 && !strcmp(argv[1], "stress")) return stress(argv[2], atoi(argv[3]));
    if (argc > 1 && !strcmp(argv[1], "gen")) return vectors(0, NULL);
    if (argc > 2 && !strcmp(argv[1], "check")) return vectors(1, argv[2]);
    static char lines[300000][400]; static size_t len[300000]; int n = 0, i, j, k, bad = 0; FILE *f = NULL;
    long made = 0, sum_cur = 0, sum_new = 0, indep_ok = 0, indep_n = 0, minimal_ok = 0, minimal_n = 0, same = 0;
    long rx_ok = 0, rxt_ok = 0, cut_n[2][8] = {{0}}, cut_wrong[2][8] = {{0}}, fl_n[5] = {0}, fl_wrong[5] = {0};
    static const int cuts[] = {1, 2, 3, 4, 6, 8, 12, 16}, flips[] = {1, 2, 3, 8, 0};
    if (argc > 1) { f = fopen(argv[1], "r"); if (!f) { perror(argv[1]); return 2; } }
    if (f) { while (n < 300000 && fgets(lines[n], sizeof lines[n], f)) { len[n] = strcspn(lines[n], "\r\n"); lines[n][len[n]] = 0; if (len[n]) n++; } fclose(f); }
    else for (n = 0; n < 3000; n++) len[n] = (size_t)synth(lines[n], n);
    for (i = 0; i < n; i++) {
        uint8_t fr[400], cur[400], big[400], back[300]; size_t fl = lacf_encode((uint8_t *)lines[i], len[i], fr, sizeof fr), cl, pay;
        if (!fl) continue;
        made++;
        cl = aprs_ppm_encode_frame((uint8_t *)lines[i], len[i], cur, sizeof cur);
        if (cl) { sum_cur += cl; sum_new += fl; }
        { uint8_t fr2[400]; same += lacf_encode((uint8_t *)lines[i], len[i], fr2, sizeof fr2) == fl && !memcmp(fr, fr2, fl); }      /* deterministic */
        for (k = 0; k <= 8; k++) {                                              /* any continuation decodes to the same text: nothing, random, all 0x00, all 0xFF */
            int mode;
            for (mode = 0; mode < 3; mode++) {
                size_t tl2;
                memcpy(big, fr, fl); for (j = 0; j < k; j++) big[fl + j] = mode == 0 ? (uint8_t)rnd() : mode == 1 ? 0x00 : 0xFF;
                tl2 = lacf_decode_raw(big, fl + k, back); indep_n++;
                if (tl2 == len[i] && !memcmp(back, lines[i], tl2)) indep_ok++;
            }
        }
        if (fl > LACF_HDR) {                                                    /* minimal: with the last byte dropped, all-0x00 or all-0xFF continuation must fail */
            int fail = 0, mode;
            for (mode = 0; mode < 2; mode++) {
                size_t tl2; memcpy(big, fr, fl - 1); for (j = 0; j < 8; j++) big[fl - 1 + j] = mode ? 0xFF : 0x00;
                tl2 = lacf_decode_raw(big, fl - 1 + 8, back);
                if (tl2 != len[i] || memcmp(back, lines[i], tl2)) fail = 1;
            }
            minimal_n++; minimal_ok += fail;
        }
        if (lacf_receive(fr, fl, back, &pay, 0) == len[i] && !memcmp(back, lines[i], len[i]) && pay == fl - LACF_HDR) rx_ok++;
        { uint8_t t6[6] = {0xA5, 0x00, 0xFF, 0x5A, 0x12, 0x34};                 /* a 6 byte relay tail: the receiver must find the payload end by itself */
          memcpy(big, fr, fl); memcpy(big + fl, t6, 6);
          if (lacf_receive(big, fl + 6, back, &pay, 1) == len[i] && !memcmp(back, lines[i], len[i]) && pay == fl - LACF_HDR) rxt_ok++; }
        for (j = 0; j < 8; j++) {
            int tail;
            for (tail = 0; tail < 2; tail++) {
                size_t total = tail ? fl + 6 : fl, tl2;
                if (tail) { memcpy(big, fr, fl); for (k = 0; k < 6; k++) big[fl + k] = (uint8_t)rnd(); } else memcpy(big, fr, fl);
                if (total <= (size_t)cuts[j] + LACF_HDR) continue;
                if (tail && (size_t)cuts[j] <= 6) continue;                      /* a cut that only removes tail leaves the payload intact: not damage */
                cut_n[tail][j]++;
                tl2 = lacf_receive(big, total - cuts[j], back, &pay, tail);
                if (tl2 && (tl2 != len[i] || memcmp(back, lines[i], tl2))) cut_wrong[tail][j]++;
            }
        }
        for (j = 0; j < 5; j++) {                                               /* bit flips (1, 2, 3, 8 bits) and a payload of random bytes (j == 4), no tail */
            size_t tl2;
            memcpy(big, fr, fl);
            if (j < 4) for (k = 0; k < flips[j]; k++) { size_t p = LACF_HDR + rnd() % (fl - LACF_HDR); big[p] ^= (uint8_t)(1u << (rnd() % 8)); }
            else for (k = LACF_HDR; (size_t)k < fl; k++) big[k] = (uint8_t)rnd();
            if (fl > LACF_HDR && !memcmp(big, fr, fl)) continue;
            fl_n[j]++;
            tl2 = lacf_receive(big, fl, back, &pay, 0);
            if (tl2 && (tl2 != len[i] || memcmp(back, lines[i], tl2))) fl_wrong[j]++;
        }
    }
    printf("%d texts (%s); LACF frames made: %ld; deterministic (same bytes on a second encode): %ld\n", n, f ? "from file" : "fabricated", made, same);
    printf("average frame bytes: current ending %.2f, LACF ending %.2f (%+.2f)\n", (double)sum_cur / made, (double)sum_new / made, (double)(sum_new - sum_cur) / made);
    printf("any continuation decodes to the same text (0 to 8 bytes: random, 0x00, 0xFF): %ld of %ld\n", indep_ok, indep_n);
    printf("minimal (with the last byte dropped, 0x00 or 0xFF after it fails): %ld of %ld\n", minimal_ok, minimal_n);
    printf("receiver accepts intact frame with the right text and payload length: no tail %ld of %ld, 6 byte tail %ld of %ld\n\n", rx_ok, made, rxt_ok, made);
    printf("%-28s %-34s %-34s\n", "damage (LoRa CRC still valid)", "no tail: wrong accepted", "6 byte tail: wrong accepted");
    for (j = 0; j < 8; j++) {
        printf("cut off %2d bytes              %6ld of %-6ld (%.3f%%)          ", cuts[j], cut_wrong[0][j], cut_n[0][j], cut_n[0][j] ? 100.0 * cut_wrong[0][j] / cut_n[0][j] : 0.0);
        if (cut_n[1][j]) printf("%6ld of %-6ld (%.3f%%)\n", cut_wrong[1][j], cut_n[1][j], 100.0 * cut_wrong[1][j] / cut_n[1][j]); else printf("n/a (only the tail is cut)\n");
        bad += cut_wrong[0][j] != 0 || cut_wrong[1][j] != 0;
    }
    for (j = 0; j < 5; j++) printf("%-28s %6ld of %-6ld (%.3f%%)\n", j < 4 ? (j == 0 ? "1 random bit flipped" : j == 1 ? "2 random bits flipped" : j == 2 ? "3 random bits flipped" : "8 random bits flipped") : "payload replaced by random", fl_wrong[j], fl_n[j], fl_n[j] ? 100.0 * fl_wrong[j] / fl_n[j] : 0.0);
    printf("\none pass receiver against the two pass one: %ld disagreements\n", lacf_disagree);
    return (lacf_disagree == 0 && same == made && indep_ok == indep_n && rx_ok == made && rxt_ok == made && !bad) ? 0 : 1;
}
