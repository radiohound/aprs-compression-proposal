/* Tests and tools for the spec 11 reference frame (tools/hb/hb.c).  Run from the repository root.
 *   hb_test gen  < packets.txt          TNC2 packets -> "<frame hex> <tnc2 hex>" vectors ("-" for a packet that cannot be encoded)
 *   hb_test check VECTORS               every vector must encode to its frame and decode to its packet
 *   hb_test recv                        lines "<frame hex>" -> the accepted TNC2 as hex, or "-" (cross-check against another implementation)
 *   hb_test relaychain < packets.txt    the same for the second and later hops (frames that already carry tuples): "<received> <relayed> <relayed tnc2>"
 *   hb_test relaycheck FILE             check a relay vector file against hb_relay
 *   hb_test relaytocheck FILE           the same file through hb_relay_to, with the relayed path taken from the file
 *   hb_test relaygen  < packets.txt     for each packet with an unused WIDE1-1 or WIDE2-2: "<frame hex> <relayed frame hex> <relayed tnc2 hex>"
 *   hb_test props packets.txt           cuts, appended bytes, bit flips, RXT, relay: what is accepted that should not be
 *   hb_test time packets.txt            encode and decode time on this machine */
#include "hb.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void hexout(const uint8_t *p, size_t n) { size_t i; for (i = 0; i < n; i++) printf("%02x", p[i]); }
static size_t unhex(const char *h, uint8_t *out, size_t cap) { size_t n = 0; while (h[0] && h[1] && h[0] != '\n' && h[0] != ' ' && n < cap) { unsigned x; sscanf(h, "%2x", &x); out[n++] = (uint8_t)x; h += 2; } return n; }
static uint32_t rng = 1100311;
static uint32_t rnd(void) { rng = rng * 1664525u + 1013904223u; return rng >> 8; }
static const uint8_t TUP[4] = { 50, 20, 45, 10 };

static size_t read_lines(const char *path, char (*lines)[700], size_t max)
{
    FILE *f = path ? fopen(path, "r") : stdin; size_t n = 0;
    if (!f) { perror(path); exit(2); }
    while (n < max && fgets(lines[n], 700, f)) { size_t l = strcspn(lines[n], "\r\n"); lines[n][l] = 0; if (l && lines[n][0] != '#') n++; }
    if (path) fclose(f);
    return n;
}

int main(int argc, char **argv)
{
    const char *list = getenv("HB_LIST") ? getenv("HB_LIST") : "tools/hybrid/tables/tocall_set1.txt";
    static char lines[20000][700]; size_t n, i; uint8_t fr[400]; char tn[800];
    if (hb_load_list(list) != 62) { fprintf(stderr, "cannot load the destination list %s (run from the repository root)\n", list); return 2; }
    if (hb_crc16((const uint8_t *)"123456789", 9) != 0x906E) { fprintf(stderr, "CRC check value wrong\n"); return 2; }
    if (argc < 2) { fprintf(stderr, "usage: hb_test gen|check|recv|relaygen|props|time [file]\n"); return 2; }

    if (!strcmp(argv[1], "gen")) {
        n = read_lines(NULL, lines, 20000);
        printf("# Spec 11 test vectors: <frame hex> <TNC2 packet hex>. Protocol version 2, table version 0 (V = 00010 000), destination list set 1. '-' = not encodable (a plain frame is sent).\n");
        for (i = 0; i < n; i++) { size_t w = hb_encode(lines[i], strlen(lines[i]), NULL, fr, sizeof fr, NULL); if (w) hexout(fr, w); else fputs("-", stdout); putchar(' '); hexout((uint8_t *)lines[i], strlen(lines[i])); putchar('\n'); }
        return 0;
    }
    if (!strcmp(argv[1], "check") && argc > 2) {
        FILE *f = fopen(argv[2], "r"); char l[2000]; int cnt = 0, bad = 0, sk = 0;
        if (!f) { perror(argv[2]); return 2; }
        while (fgets(l, sizeof l, f)) {
            char *sp; uint8_t want[400], txt[800]; size_t wl, tl; hb_info_t in; size_t w;
            if (l[0] == '#' || l[0] == '\n') continue;
            sp = strchr(l, ' '); if (!sp) continue; *sp++ = 0;
            tl = unhex(sp, txt, sizeof txt); txt[tl] = 0; cnt++;
            if (!strcmp(l, "-")) { if (hb_encode((char *)txt, tl, NULL, fr, sizeof fr, NULL)) { bad++; printf("vector %d: encodes but the vector says it cannot\n", cnt); } sk++; continue; }
            wl = unhex(l, want, sizeof want);
            w = hb_encode((char *)txt, tl, NULL, fr, sizeof fr, &in);
            if (w != wl || memcmp(fr, want, wl)) { bad++; printf("vector %d: encode mismatch\n", cnt); continue; }
            { int r = hb_decode(want, wl, tn, sizeof tn, &in); if (r != (int)tl || memcmp(tn, txt, tl)) { bad++; printf("vector %d: decode %s\n", cnt, r < 0 ? hb_reason(r) : "mismatch"); } }
        }
        fclose(f);
        printf("%d vectors (%d not encodable), %d failed\n", cnt, sk, bad);
        return bad != 0;
    }
    if (!strcmp(argv[1], "recv")) {
        char l[2000];
        while (fgets(l, sizeof l, stdin)) { uint8_t b[400]; size_t bl = unhex(l, b, sizeof b); int r = hb_decode(b, bl, tn, sizeof tn, NULL); if (r > 0) hexout((uint8_t *)tn, (size_t)r); else fputs("-", stdout); putchar('\n'); }
        return 0;
    }
    if (!strcmp(argv[1], "relaytocheck")) {
        /* the same vectors through hb_relay_to: the new path is the one the vector lists (column 3, the relayed packet). For a vector that must not be relayed (column 2 is '-') the
         * path is made here by the same rule as hb_relay's (first unused WIDE1-1, WIDE2-1 or WIDE2-2); if that does not apply the vector is not run. */
        FILE *f = fopen(argv[2], "r"); char l[3000]; int cnt = 0, bad = 0, skipped = 0;
        while (f && fgets(l, sizeof l, f)) {
            uint8_t a[400], b[400], out[400], t4[4]; char c[40], nt[800], *p1, *p2; size_t al, bl, r, nl; hb_info_t in; int d, k;
            if (l[0] == '#' || l[0] == '\n') continue;
            p1 = strchr(l, ' '); p2 = p1 ? strchr(p1 + 1, ' ') : NULL; if (!p2) { bad++; continue; }
            al = unhex(l, a, sizeof a); bl = unhex(p1 + 1, b, sizeof b); cnt++;
            if (hb_decode(a, al, tn, sizeof tn, &in) <= 0) { bad++; continue; }
            k = in.has_rxt ? in.rxt.n : 0; snprintf(c, sizeof c, "N0RLY-%d", k + 1); { int q; for (q = 0; q < 4; q++) t4[q] = (uint8_t)(TUP[q] + k); }
            if (p1[1] == '-') {
                char *comma = strchr(tn, ','), *colon = strchr(tn, ':'), *u; size_t head;
                if (!comma || !colon) { skipped++; continue; }
                u = comma + 1; while (u < colon) { char *e = strchr(u, ','); if (!e || e > colon) e = colon; if (e[-1] != '*') break; u = e + 1; }
                if (u >= colon) { skipped++; continue; }
                head = (size_t)(u - tn);
                if (!strncmp(u, "WIDE1-1", 7) && (u[7] == ',' || u[7] == ':')) nl = (size_t)snprintf(nt, sizeof nt, "%.*s%s*%s", (int)head, tn, c, u + 7);
                else if (!strncmp(u, "WIDE2-1", 7) && (u[7] == ',' || u[7] == ':')) nl = (size_t)snprintf(nt, sizeof nt, "%.*s%s*%s", (int)head, tn, c, u + 7);
                else if (!strncmp(u, "WIDE2-2", 7) && (u[7] == ',' || u[7] == ':')) nl = (size_t)snprintf(nt, sizeof nt, "%.*s%s*,WIDE2-1%s", (int)head, tn, c, u + 7);
                else { skipped++; continue; }
                r = hb_relay_to(a, al, nt, nl, c, t4, out, sizeof out);
                if (r) { bad++; printf("relay-to vector %d: relayed a frame that must not be relayed\n", cnt); }
                continue;
            }
            { char *p3 = strchr(p2 + 1, ' '); size_t tl; uint8_t tb[800]; (void)p3;
              tl = unhex(p2 + 1, tb, sizeof tb); if (tl == 0 || tl >= sizeof nt) { bad++; continue; } memcpy(nt, tb, tl); nt[tl] = 0; nl = tl; }
            r = hb_relay_to(a, al, nt, nl, c, t4, out, sizeof out);
            d = r ? hb_decode(out, r, tn, sizeof tn, NULL) : -1;
            if (r != bl || memcmp(out, b, bl) || d <= 0) { bad++; printf("relay-to vector %d: mismatch\n", cnt); }
        }
        if (f) fclose(f);
        printf("%d relay-to vectors, %d failed (%d not run)\n", cnt, bad, skipped);
        return bad != 0 || !f;
    }
    if (!strcmp(argv[1], "relaychain") || !strcmp(argv[1], "relaycheck")) {
        /* relay vectors: the relay is N0RLY-(k+1) with tuple 50+k 20+k 45+k 10+k, k = tuples already in the frame (spec 11 Conformance) */
        int chain = !strcmp(argv[1], "relaychain");
        if (!chain) {
            FILE *f = fopen(argv[2], "r"); char l[3000]; int cnt = 0, bad = 0;
            while (f && fgets(l, sizeof l, f)) {
                uint8_t a[400], b[400], out[400], t4[4]; char c[40], *p1, *p2; size_t al, bl, r; hb_info_t in; int d;
                if (l[0] == '#' || l[0] == '\n') continue;
                p1 = strchr(l, ' '); p2 = p1 ? strchr(p1 + 1, ' ') : NULL; if (!p2) { bad++; continue; }
                al = unhex(l, a, sizeof a); bl = unhex(p1 + 1, b, sizeof b); cnt++;
                if (hb_decode(a, al, tn, sizeof tn, &in) <= 0) { bad++; continue; }
                { int k = in.has_rxt ? in.rxt.n : 0; int q; snprintf(c, sizeof c, "N0RLY-%d", k + 1); for (q = 0; q < 4; q++) t4[q] = (uint8_t)(TUP[q] + k); }
                r = hb_relay(a, al, c, t4, out, sizeof out);
                if (p1[1] == '-') { if (r) { bad++; printf("relay vector %d: relayed a frame that must not be relayed\n", cnt); } continue; }     /* '-': the relay must refuse */
                d = r ? hb_decode(out, r, tn, sizeof tn, NULL) : -1;
                if (r != bl || memcmp(out, b, bl) || d <= 0) { bad++; printf("relay vector %d: mismatch\n", cnt); }
            }
            if (f) fclose(f);
            printf("%d relay vectors, %d failed\n", cnt, bad);
            return bad != 0 || !f;
        }
        n = read_lines(NULL, lines, 20000);
        for (i = 0; i < n; i++) {
            size_t w = hb_encode(lines[i], strlen(lines[i]), NULL, fr, sizeof fr, NULL); int k;
            if (!w) continue;
            for (k = 0; k < 7; k++) {
                uint8_t out[400], t4[4]; char c[40]; size_t r; int q, d;
                snprintf(c, sizeof c, "N0RLY-%d", k + 1); for (q = 0; q < 4; q++) t4[q] = (uint8_t)(TUP[q] + k);
                r = hb_relay(fr, w, c, t4, out, sizeof out); if (!r) break;
                d = hb_decode(out, r, tn, sizeof tn, NULL);
                if (k >= 1) { hexout(fr, w); putchar(' '); hexout(out, r); putchar(' '); if (d > 0) hexout((uint8_t *)tn, (size_t)d); else fputs("-", stdout); putchar('\n'); }
                memcpy(fr, out, r); w = r;
            }
        }
        return 0;
    }
    if (!strcmp(argv[1], "relaygen")) {
        n = read_lines(NULL, lines, 20000);
        for (i = 0; i < n; i++) {
            size_t w = hb_encode(lines[i], strlen(lines[i]), NULL, fr, sizeof fr, NULL), r; uint8_t out[400]; int d;
            if (!w) continue;
            r = hb_relay(fr, w, "N0RLY-1", TUP, out, sizeof out);
            if (!r) continue;
            d = hb_decode(out, r, tn, sizeof tn, NULL);
            hexout(fr, w); putchar(' '); hexout(out, r); putchar(' '); if (d > 0) hexout((uint8_t *)tn, (size_t)d); else fputs("-", stdout); putchar('\n');
        }
        return 0;
    }
    if (!strcmp(argv[1], "props") && argc > 2) {
        /* counters by content type: [0] PPM content (cc = 0), [1] plain content (cc = 1) */
        long frames[2] = {0}, intact_ok = 0, cut_n[2] = {0}, cut_acc[2] = {0}, app_n[2] = {0}, app_acc[2] = {0}, flip_n[2] = {0}, flip_acc[2] = {0}, flip_wrong[2] = {0};
        long reg_n[2][6] = {{0}}, reg_wrong[2][6] = {{0}}, rx_n = 0, rx_ok = 0, rxcut_n = 0, rxcut_acc = 0, rl_n = 0, rl_ok = 0, rl_copy = 0;
        static const char *regname[6] = { "prefix and header byte", "source and destination", "path", "position block", "content: V, L, CRC", "content: payload or text" };
        n = read_lines(argv[2], lines, 20000);
        for (i = 0; i < n; i++) {
            size_t w = hb_encode(lines[i], strlen(lines[i]), NULL, fr, sizeof fr, NULL), k; hb_info_t in; int d, c;
            if (!w) continue;
            d = hb_decode(fr, w, tn, sizeof tn, &in);
            if (d == (int)strlen(lines[i]) && !memcmp(tn, lines[i], (size_t)d)) intact_ok++;
            c = in.cc; frames[c]++;
            for (k = 1; k < w; k++) { cut_n[c]++; if (hb_decode(fr, w - k, tn, sizeof tn, NULL) > 0) cut_acc[c]++; }
            for (k = 1; k <= 8; k++) { uint8_t b[400]; size_t q; memcpy(b, fr, w); for (q = 0; q < k; q++) b[w + q] = (uint8_t)rnd(); app_n[c]++; if (hb_decode(b, w + k, tn, sizeof tn, NULL) > 0) app_acc[c]++; }
            { size_t bit; for (bit = 0; bit < w * 8; bit++) {
                uint8_t b[400]; size_t byte = bit / 8; int reg, dd; memcpy(b, fr, w); b[byte] ^= (uint8_t)(1u << (bit % 8)); flip_n[c]++;
                reg = byte < 4 ? 0 : byte < in.path_start ? 1 : byte < in.front_len ? 2 : byte < in.content_start ? 3 : (c == 0 && byte < in.content_start + 4) ? 4 : 5;
                reg_n[c][reg]++;
                dd = hb_decode(b, w, tn, sizeof tn, NULL);
                if (dd > 0) { flip_acc[c]++; if (dd != (int)strlen(lines[i]) || memcmp(tn, lines[i], (size_t)dd)) { flip_wrong[c]++; reg_wrong[c][reg]++; } }
            } }
            { hb_rxt_t rx; uint8_t b[400]; size_t w2; int np = 0; const char *p = strchr(lines[i], '>'); const char *cp = strchr(lines[i], ':');
              for (; p && p < cp; p++) if (*p == ',') np++;
              if (np >= 1) {
                  memset(&rx, 0, sizeof rx); rx.n = 1; rx.idx[0] = 0; memcpy(rx.t[0], TUP, 4); rx_n++;
                  w2 = hb_encode(lines[i], strlen(lines[i]), &rx, b, sizeof b, &in);
                  if (w2) { d = hb_decode(b, w2, tn, sizeof tn, &in); if (d > 0 && in.has_rxt && in.rxt.n == 1 && !memcmp(in.rxt.t[0], TUP, 4)) rx_ok++;
                            for (k = 1; k < w2; k++) { size_t keep = w2 - k; (void)keep; } }
              } }
            { uint8_t out[400]; size_t r = hb_relay(fr, w, "N0RLY-1", TUP, out, sizeof out);
              if (r) { rl_n++; d = hb_decode(out, r, tn, sizeof tn, &in);
                       if (d > 0 && in.has_rxt) { size_t ca = 0; rl_ok++; while (ca < w && ca < r && fr[w - 1 - ca] == out[r - 1 - 5 - ca]) ca++; if (ca >= in.content_len) rl_copy++; } } }
        }
        printf("%ld frames, %ld with PPM content and %ld with plain content; intact frames decoded to the original: %ld\n", frames[0] + frames[1], frames[0], frames[1], intact_ok);
        { int kk;
        for (kk = 0; kk < 2; kk++) {
            int rg;
            size_t k = (size_t)kk;
            printf("\n== %s content (%ld frames)\n", k ? "PLAIN" : "PPM", frames[k]);
            printf("every cut (1 byte up to all but one): %ld cuts, %ld accepted\n", cut_n[k], cut_acc[k]);
            printf("1 to 8 random bytes appended: %ld frames, %ld accepted\n", app_n[k], app_acc[k]);
            printf("every single-bit flip: %ld flips, %ld accepted, %ld of those with a different packet\n", flip_n[k], flip_acc[k], flip_wrong[k]);
            for (rg = 0; rg < 6; rg++) printf("   flips in %-26s accepted with a different packet: %ld of %ld\n", regname[rg], reg_wrong[k][rg], reg_n[k][rg]);
        } }
        printf("\nframes with one RXT tuple: %ld, decoded with the tuple intact: %ld\n", rx_n, rx_ok);
        printf("relayed frames: %ld, accepted by the receiver with an RXT block: %ld, content bytes copied unchanged: %ld\n", rl_n, rl_ok, rl_copy);
        (void)rxcut_n; (void)rxcut_acc;
        return 0;
    }
    if (!strcmp(argv[1], "time") && argc > 2) {
        clock_t t0; size_t reps = 20, r; double te, td; size_t okn = 0; static uint8_t frs[3000][260]; static size_t fl[3000];
        n = read_lines(argv[2], lines, 3000);
        for (i = 0; i < n; i++) { fl[i] = hb_encode(lines[i], strlen(lines[i]), NULL, frs[i], 260, NULL); if (fl[i]) okn++; }
        t0 = clock(); for (r = 0; r < reps; r++) for (i = 0; i < n; i++) hb_encode(lines[i], strlen(lines[i]), NULL, fr, sizeof fr, NULL); te = (double)(clock() - t0) / CLOCKS_PER_SEC;
        t0 = clock(); for (r = 0; r < reps; r++) for (i = 0; i < n; i++) if (fl[i]) hb_decode(frs[i], fl[i], tn, sizeof tn, NULL); td = (double)(clock() - t0) / CLOCKS_PER_SEC;
        printf("%zu packets (%zu encodable): encode %.1f us, decode %.1f us per packet on this machine\n", n, okn, te * 1e6 / (double)(reps * n), td * 1e6 / (double)(reps * okn));
        return 0;
    }
    fprintf(stderr, "unknown mode\n"); return 2;
}
