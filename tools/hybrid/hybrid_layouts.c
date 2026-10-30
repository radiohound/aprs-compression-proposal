/* Size comparison of three frame layouts, measured with the real reference codec (reference/aprs_ppm.c) and a model table.
 *
 *   hybrid_layouts TSV [N...]     N = full frame every Nth transmission per station (1 = full frames only); default 2
 *
 * Layouts (see README.md):
 *   0 whole packet   PPM of "SRC>DST[,PATH]:INFO"                       (APRS-PPM v2.0.0)
 *   1 hybrid A       native path block + PPM of "SRC>DST:INFO"            (the relay edits the path block, copies the PPM bytes)
 *   2 hybrid B       native source, destination code, path block + PPM of ":INFO"
 * Run each layout with a model trained for it (tools/hybrid/train_layout_model.py --mode whole|nopath|info).
 *
 * Environment (all optional):
 *   LAYOUTS=mask   bit 0 whole, bit 1 hybrid A, bit 2 hybrid B (default 7 = all)
 *   PACKPATH=0     old path cost (2 + text length) instead of packed callsign elements
 *   NATIVE=1       cost source, destination and path exactly as moricef's native encoder does (moricef/LoRa_APRS_iGate,
 *                  branch rxt, src/native_aprs.cpp, encElement): no path code table, path block = 1 header byte + its elements, destination as an element (DSTCODE=1 keeps the code table)
 *   DUMPCOST=1     print "source destination path" native bytes for each row and exit (to check against the real encoder)
 *   NOHOP=1        skip the "after one relay hop" pass
 *   RXT=R          each of R relays adds one 4-byte RXT tuple, plus one mask byte once (relayed frames only; default 0)
 *   RXTFULL=1      add the RXT bytes to full frames only, not to primed frames
 *
 * Every frame is decoded by a receiver holding only what it received and compared with the original ("decode failures" must be 0).
 * Bytes are counted from the frame the codec produces (it includes the 3-byte radio prefix) plus the native bytes of the layout.
 * Plain frames are the text plus the 3-byte prefix; a text the codec declines to shrink goes out as a plain frame. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>
#include <stdint.h>
#include "aprs_ppm.h"

#define MAXROWS 400000
typedef struct { char src[16]; int rf; char kind; char path[96]; uint8_t text[256]; int tl; } Row;
static Row *rows; static int nrows;

static const char *CODES[] = { "", "WIDE1-1", "WIDE1-1,WIDE2-1", "WIDE2-1", "WIDE2-2", "WIDE1-1,WIDE2-2", "TCPIP*", NULL };
static const char *DCODES[] = { "APLRG1","APLRT1","APLWS2","APLRFD","APLC13","APLOX1","APLT00","APLRFT","APLHM0","APLETK","APLC12","APLG01","APLHI0","APL",NULL };
static int dst_code(const char *d){ for(int i=0;DCODES[i];i++) if(!strcmp(DCODES[i],d)) return i; return -1; }
/* A callsign packs into 5 bytes when its base is 1 to 6 letters or digits and its SSID is absent or a number 0 to 15 (moricef's encoder keeps other SSIDs as text). */
static int call_packable(const char *s, int l) {
    int b = 0; while (b < l && s[b] != '-') b++;
    if (b < 1 || b > 6) return 0;
    for (int i = 0; i < b; i++) { char c = s[i]; if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))) return 0; }
    if (b == l) return 1;
    int v = 0, n = l - b - 1; if (n < 1 || n > 2) return 0;
    for (int i = b + 1; i < l; i++) { if (s[i] < '0' || s[i] > '9') return 0; v = v * 10 + s[i] - '0'; }
    return v <= 15;
}
static int src_packable(const char *s, int l) { return call_packable(s, l); }
/* Native path block for a path outside the code table: one header byte, then per element 1 byte for WIDEn-N, 5 bytes for a packable callsign
   (the used mark * is one of its bits), otherwise 1 + the element's text length. PACKPATH=0 restores the old 2 + text length. */
static int PACKPATH = 1;
static int path_native(const char *p) {
    if (!PACKPATH) return 2 + (int)strlen(p);
    int tot = 1; const char *e = p;
    while (*e) {
        const char *c = strchr(e, ','); int l = c ? (int)(c - e) : (int)strlen(e);
        if (l >= 7 && !strncmp(e, "WIDE", 4) && e[4] >= '1' && e[4] <= '7' && e[5] == '-' && l == 7) tot += 1;
        else { int cl = l; if (cl && e[cl - 1] == '*') cl--; tot += call_packable(e, cl) ? 5 : 1 + l; }
        e += l; if (*e == ',') e++;
    }
    return tot;
}
static int path_code(const char *p) { for (int i = 0; CODES[i]; i++) if (!strcmp(CODES[i], p)) return i; return -1; }

/* NATIVE=1: one address element as moricef's encoder writes it (encElement in moricef/LoRa_APRS_iGate, branch rxt, src/native_aprs.cpp).
   WIDEn[-N][*] with n, N 1..7: 1 byte. 1 to 6 of A-Z0-9, optional -SSID 1..255 without a leading zero, optional *: 5 bytes,
   6 when the SSID is above 30. Anything else: 1 + text length, up to 31 characters. Returns -1 for an element it cannot carry. */
static int NATIVE = 0, DSTCODE = 0;
static int elem_native(const char *e, int l) {
    int i = 5;
    if (l >= 5 && !strncmp(e, "WIDE", 4) && e[4] >= '1' && e[4] <= '7') {
        if (i < l && e[i] == '-') i = (i + 1 < l && e[i + 1] >= '1' && e[i + 1] <= '7') ? i + 2 : -1;
        if (i > 0 && i < l && e[i] == '*') i++;
        if (i == l) return 1;
    }
    i = 0; while (i < l && i < 6 && ((e[i] >= 'A' && e[i] <= 'Z') || (e[i] >= '0' && e[i] <= '9'))) i++;
    if (i > 0) {
        int ssid = 0, ok = 1;
        if (i < l && e[i] == '-') {
            int j = i + 1; while (j < l && j < i + 4 && e[j] >= '0' && e[j] <= '9') { ssid = ssid * 10 + e[j] - '0'; j++; }
            if (j == i + 1 || e[i + 1] == '0' || ssid > 255) ok = 0; else i = j;
        }
        if (ok && i < l && e[i] == '*') i++;
        if (ok && i == l) return ssid > 30 ? 6 : 5;
    }
    return (l >= 1 && l <= 31) ? 1 + l : -1;
}
/* path block: one header byte (element count), then each element. -1 if an element cannot be carried or there are more than 15. */
static int path_block_native(const char *p) {
    int tot = 1, n = 0; const char *e = p;
    while (*e) {
        const char *c = strchr(e, ','); int l = c ? (int)(c - e) : (int)strlen(e);
        int b = elem_native(e, l); if (b < 0 || ++n > 15) return -1;
        tot += b; e += l; if (*e == ',') e++;
    }
    return tot;
}


/* ---- byte-saving variants measured on hybrid B (all off by default; they need NATIVE=1) ----
   TOCALL=1   destination from a table of the most frequent destinations (1 byte), a one-wildcard registry pattern (2 bytes), Mic-E (6 bytes: escape + 5 byte element; MICE_DST=4 gives the old estimate)
              or an escape byte + the native element; TOCALLFILE names the table (tocall_table.py, built from the training stations only)
   POSNATIVE=1  position packets go in the native position block of draft 0 (flags, optional timestamp, position, symbol, course/speed, altitude);
              PPM codes only the comment. Reference for PACKPOS.
   PACKPOS=1  as POSNATIVE, with latitude, longitude, direction (5 degrees) and speed (to 255 kn) in one 7-byte integer (6 bytes without course/speed). Lossy.
   PACKPOS=2  the same with Mic-E's resolution: direction in 1 degree steps (0 unknown, 1 to 360) and speed 0 to 999 kn in 1 kn steps, 8 bytes (6 without course/speed).
   MICE2NATIVE=1  models a new-format tracker that sends a native position instead of Mic-E (needs NATIVE=1 and PACKPOS=2 or POSNATIVE=1): the Mic-E latitude, longitude, speed,
              course, symbol and 4-bit message status (3 bits plus a custom/standard flag) become a native position; the rest of the information field goes to PPM; the
              destination costs 1 byte (the tracker's own tocall). With PACKPOS=2 the status is folded into the packed integer (v*16 + status): no extra byte. With POSNATIVE=1
              (lossless draft-0 block) a status byte is added, except when the status equals MICE_DEF (default -1: always). Value-lossless, not text-lossless (the text is no longer Mic-E).
              Ambiguous positions (K, L, Z in the latitude) and invalid packets stay text and are counted.
   MICE_KEEPDST=1 charge the Mic-E destination as before instead of 1 byte (isolates the position)
   MICE_DEF=n status value that needs no status byte in the lossless block (0..15; bits + 8 if custom), default -1
   WSPRCALL=1 callsigns (source, relay elements) as one 32-bit number, 4 bytes: WSPRSTAR=1 adds one byte per path that has a used mark (*), which a
              4-byte number cannot carry.
   PLAINTEXT=1  text that PPM cannot shrink goes in the native frame as it is (no ':' lead byte); with a position block and an empty comment, no text at all.
   HOPCALL=C  the simulated relay's callsign in the "after one relay hop" pass (default DIGI-10; use one that packs, such as KD6DGI-10). */
static int TOCALL = 0, PACKPOS = 0, POSNATIVE = 0, WSPRCALL = 0, WSPRSTAR = 0, PLAINTEXT = 0, MICE2NATIVE = 0, MICE_DEF = -1;
static int micedump = 0, MICE_DST = 6;   /* Mic-E destination outside the table: escape byte + 5 byte element (measured by moricef, 29 Oct 2026; the first estimate was 4) */
static long mice_seen, mice_conv, mice_amb, mice_inv, mice_rtfail, mice_exact_fail, mice_stat[16];
static const char *HOPCALL = "DIGI-10";
static FILE *dumpf;

#define MAXE 256
static char tc_exact[MAXE][16]; static int tc_n; static char tc_pat[MAXE][8]; static int tc_np;
static void tocall_load(const char *fn) {
    FILE *f = fopen(fn, "r"); char line[64]; if (!f) { perror(fn); exit(1); }
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = 0;
        if (line[0] == 'E' && tc_n < 192) strncpy(tc_exact[tc_n++], line + 2, 15);
        else if (line[0] == 'P' && tc_np < MAXE) strncpy(tc_pat[tc_np++], line + 2, 7);
    }
    fclose(f);
}
static long tc_class[4];   /* 1-byte table entry, 2-byte registry pattern, Mic-E, escape */
static int tocall_cost(const char *dst, int dl, const char *info) {
    for (int i = 0; i < tc_n; i++) if (!strcmp(tc_exact[i], dst)) { tc_class[0]++; return 1; }
    if (dl == 6) for (int i = 0; i < tc_np; i++) {
        int w = -1, ok = 1;
        for (int k = 0; k < 6 && ok; k++) {
            char p = tc_pat[i][k];
            if (p == '?') { if (w >= 0 || !isalnum((unsigned char)dst[k])) ok = 0; else w = k; }
            else if (p == 'n') { if (w >= 0 || dst[k] < '0' || dst[k] > '9') ok = 0; else w = k; }
            else if (p != dst[k]) ok = 0;
        }
        if (ok && w >= 0) { tc_class[1]++; return 2; }
    }
    if (info[0] == '`' || info[0] == '\'') { tc_class[2]++; return MICE_DST; }
    int e = elem_native(dst, dl); if (e < 0) return -1;
    tc_class[3]++; return 1 + e;
}

/* WSPR-style callsign: the base padded to 6 characters with its digit third, base 37*36*10*27^3, times 16 plus a numeric SSID 0..15. 4 bytes. */
static long wsp_ok, wsp_total, wsr_ok, wsr_total;   /* source callsigns, relay callsign elements */
static int wspr_encode(const char *e, int l, uint32_t *code) {
    int b = 0; while (b < l && e[b] != '-') b++;
    int ssid = 0;
    if (b < l) { int n = l - b - 1; if (n < 1 || n > 2 || e[b + 1] == '0') return 0;
        for (int i = b + 1; i < l; i++) { if (e[i] < '0' || e[i] > '9') return 0; ssid = ssid * 10 + e[i] - '0'; }
        if (ssid > 15) return 0; }
    if (b < 3 || b > 6) return 0;
    char c[7]; memset(c, ' ', 6); c[6] = 0;
    if (e[2] >= '0' && e[2] <= '9') memcpy(c, e, b);                 /* K6ATV: digit second means left pad */
    else if (e[1] >= '0' && e[1] <= '9' && b <= 5) memcpy(c + 1, e, b);
    else return 0;
    if (!(c[2] >= '0' && c[2] <= '9')) return 0;
    for (int i = 0; i < 6; i++) { char x = c[i];
        int up = (x >= 'A' && x <= 'Z'), dg = (x >= '0' && x <= '9'), sp = (x == ' ');
        if (i == 0 && !(up || dg || sp)) return 0; if (i == 1 && !(up || dg)) return 0;
        if (i == 2 && !dg) return 0; if (i >= 3 && !(up || sp)) return 0; }
    #define V37(x) ((x) == ' ' ? 0 : (x) >= 'A' ? (x) - 'A' + 11 : (x) - '0' + 1)
    #define V36(x) ((x) >= 'A' ? (x) - 'A' + 10 : (x) - '0')
    #define V27(x) ((x) == ' ' ? 0 : (x) - 'A' + 1)
    uint64_t n = ((((uint64_t)V37(c[0]) * 36 + V36(c[1])) * 10 + (c[2] - '0')) * 27 + V27(c[3])) * 27 + V27(c[4]);
    n = n * 27 + V27(c[5]);
    uint64_t v = n * 16 + ssid;
    if (v >= 4194840960ULL) return 0;
    *code = (uint32_t)v; return 1;
}
static int wspr_decode(uint32_t code, char *out) {          /* inverse, to prove the number carries the callsign */
    uint64_t v = code; int ssid = (int)(v % 16); uint64_t n = v / 16; char c[7]; c[6] = 0;
    for (int i = 5; i >= 3; i--) { int d = (int)(n % 27); n /= 27; c[i] = d ? 'A' + d - 1 : ' '; }
    c[2] = '0' + (int)(n % 10); n /= 10; int d = (int)(n % 36); n /= 36; c[1] = d < 10 ? '0' + d : 'A' + d - 10;
    d = (int)n; c[0] = d == 0 ? ' ' : d < 11 ? '0' + d - 1 : 'A' + d - 11;
    int a = 0, z = 5; while (c[a] == ' ') a++; while (z > a && c[z] == ' ') z--;
    int k = 0; for (int i = a; i <= z; i++) out[k++] = c[i];
    if (ssid) k += sprintf(out + k, "-%d", ssid);
    out[k] = 0; return k;
}
/* cost of one address element with WSPR callsigns on; star = used mark */
static long wsp_fails;
static int elem_wspr(const char *e, int l, int *star) {
    int cl = l; *star = 0; if (cl && e[cl - 1] == '*') { cl--; *star = 1; }
    uint32_t code; char back[16];
    if (!(l >= 5 && !strncmp(e, "WIDE", 4)) && wspr_encode(e, cl, &code)) {
        wspr_decode(code, back); if (strlen(back) != (size_t)cl || strncmp(back, e, cl)) wsp_fails++;
        return 4; }
    *star = 0; return elem_native(e, l);
}
static int path_block_wspr(const char *p) {
    int tot = 1, n = 0, anystar = 0; const char *e = p;
    while (*e) {
        const char *c = strchr(e, ','); int l = c ? (int)(c - e) : (int)strlen(e); int st;
        int b = elem_wspr(e, l, &st); if (b < 0 || ++n > 15) return -1;
        int iswide = (l >= 5 && !strncmp(e, "WIDE", 4)), cl = l - (l && e[l - 1] == '*');
        if (!iswide && strncmp(e, "TCPIP", 5) && (elem_native(e, l) == 5 || elem_native(e, l) == 6) ) { wsr_total++; if (b == 4) wsr_ok++; }
        if (e[l - 1] == '*') anystar = 1;
        tot += b; e += l; if (*e == ',') e++;
    }
    return tot + (WSPRSTAR && anystar ? 1 : 0);
}

/* position packets */
typedef struct { char dt; int has_ts; char ts[8]; int comp, has_cs, cs_alt, cs_none; long lat_idx, lon_idx; int course, speed;
    char tbl, code, T, cs0, cs1; char yx[9]; char ext[8]; int has_alt, alt_off; char altstr[8]; char comment[260]; int mice, mstat; } Pos;
static int b91(const char *s, int n, long *v) { *v = 0; for (int i = 0; i < n; i++) { if (s[i] < 33 || s[i] > 123) return 0; *v = *v * 91 + s[i] - 33; } return 1; }
static int parse_pos(const char *info, Pos *P) {
    memset(P, 0, sizeof *P); char d = info[0]; if (!(d == '!' || d == '=' || d == '/' || d == '@')) return 0;
    P->dt = d; const char *q = info + 1; size_t left = strlen(q);
    if (d == '/' || d == '@') { if (left < 7) return 0;
        for (int i = 0; i < 6; i++) if (q[i] < '0' || q[i] > '9') return 0;
        if (q[6] != 'z' && q[6] != 'h' && q[6] != '/') return 0;
        memcpy(P->ts, q, 7); P->has_ts = 1; q += 7; left -= 7; }
    #define DIG(x) ((x) >= '0' && (x) <= '9')
    if (DIG(q[0])) {                                    /* uncompressed: 19 characters */
        if (left < 19) return 0;
        for (int i = 0; i < 4; i++) if (!DIG(q[i])) return 0;
        if (q[4] != '.' || !DIG(q[5]) || !DIG(q[6]) || (q[7] != 'N' && q[7] != 'S')) return 0;
        for (int i = 9; i < 14; i++) if (!DIG(q[i])) return 0;
        if (q[14] != '.' || !DIG(q[15]) || !DIG(q[16]) || (q[17] != 'E' && q[17] != 'W')) return 0;
        int la = (q[0]-'0')*10 + q[1]-'0', lm = (q[2]-'0')*10 + q[3]-'0', lh = (q[5]-'0')*10 + q[6]-'0';
        int lo = (q[9]-'0')*100 + (q[10]-'0')*10 + q[11]-'0', om = (q[12]-'0')*10 + q[13]-'0', oh = (q[15]-'0')*10 + q[16]-'0';
        if (la > 90 || lm > 59 || lo > 180 || om > 59) return 0;
        long lat_h = la * 6000L + lm * 100 + lh, lon_h = lo * 6000L + om * 100 + oh;
        P->lat_idx = 90 * 6000L + (q[7] == 'S' ? -lat_h : lat_h);
        P->lon_idx = (180 * 6000L + (q[17] == 'W' ? -lon_h : lon_h)) % 2160000L;
        P->tbl = q[8]; P->code = q[18]; q += 19; left -= 19;
        if (left >= 7 && DIG(q[0]) && DIG(q[1]) && DIG(q[2]) && q[3] == '/' && DIG(q[4]) && DIG(q[5]) && DIG(q[6])) {
            P->has_cs = 1; P->course = (q[0]-'0')*100 + (q[1]-'0')*10 + q[2]-'0'; P->speed = (q[4]-'0')*100 + (q[5]-'0')*10 + q[6]-'0';
            memcpy(P->ext, q, 7); q += 7; left -= 7;
            if (P->course > 360) return 0; }
    } else {                                            /* compressed: table, 4 + 4 base 91, code, c, s, T */
        if (left < 13) return 0; long y, x;
        if (!b91(q + 1, 4, &y) || !b91(q + 5, 4, &x)) return 0;
        P->comp = 1; P->tbl = q[0]; P->code = q[9]; P->cs0 = q[10]; P->cs1 = q[11]; P->T = q[12]; memcpy(P->yx, q + 1, 8);
        double lat = 90.0 - y / 380926.0, lon = -180.0 + x / 190463.0;
        P->lat_idx = lround((lat + 90) * 6000); P->lon_idx = lround((lon + 180) * 6000) % 2160000L;
        if (P->lat_idx > 1080000L) return 0;
        if (P->cs0 == ' ') P->cs_none = 1;
        else if (((P->T - 33) & 0x18) == 0x10 || P->cs0 == '{') P->cs_alt = 1;
        else if (P->cs0 >= '!' && P->cs0 <= 'z' && P->cs1 >= '!') { P->has_cs = 1; P->course = (P->cs0 - 33) * 4; P->speed = (int)lround(pow(1.08, P->cs1 - 33) - 1); }
        else return 0;
        q += 13; left -= 13; }
    strncpy(P->comment, q, 259);
    char *a = strstr(P->comment, "/A=");
    if (a) { int neg = a[3] == '-'; int nd = neg ? 5 : 6, ok = 1; for (int i = 0; i < nd; i++) if (!DIG(a[3 + neg + i])) ok = 0;
        if (ok && (int)(a - P->comment) <= 255) { P->has_alt = 1; P->alt_off = (int)(a - P->comment); memcpy(P->altstr, a + 3, neg + nd); P->altstr[neg + nd] = 0;
            memmove(a, a + 3 + neg + nd, strlen(a + 3 + neg + nd) + 1); } }
    return 1;
}

/* Mic-E (APRS101 ch. 10): latitude, message bits, N/S, longitude offset and E/W in the 6 destination characters; longitude, speed, course, symbol in info bytes 1..8.
   Returns 1 decoded into P (an uncompressed position with course/speed; P->mstat = 3 message bits + 8 if custom), 0 not Mic-E, -1 Mic-E kept as text (-2 ambiguous position). */
static int mice_digit(char ch, int *cust, int *stdb) {
    *cust = *stdb = 0;
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'A' && ch <= 'J') { *cust = 1; return ch - 'A'; }
    if (ch >= 'P' && ch <= 'Y') { *stdb = 1; return ch - 'P'; }
    return -1;                                    /* K, L, Z: a space = position ambiguity */
}
static int parse_mice(const char *dst, int dl, const char *info, Pos *P) {
    unsigned char c0 = (unsigned char)info[0];
    if (!(c0 == '`' || c0 == '\'' || c0 == 0x1C || c0 == 0x1D)) return 0;
    if (!strncmp(dst, "APL", 3)) return 0;                                   /* LoRa device ids are tocalls whatever the comment starts with */
    for (int i = 0; i < tc_n; i++) if (!strcmp(tc_exact[i], dst)) return 0;   /* a destination the table knows is a tocall (an APL... packet whose comment starts with ` or ' is not Mic-E) */
    memset(P, 0, sizeof *P);
    if (dl < 6 || strlen(info) < 9) return -1;
    if (dl > 6) { if (dst[6] != '-' || dl > 9) return -1; for (int i = 7; i < dl; i++) if (dst[i] < '0' || dst[i] > '9') return -1; }   /* -SSID after the 6 latitude characters: the path code, not part of the position */
    for (int i = 0; i < 6; i++) if (!((dst[i] >= '0' && dst[i] <= '9') || (dst[i] >= 'A' && dst[i] <= 'L') || (dst[i] >= 'P' && dst[i] <= 'Z'))) return -1;
    int dv[6], cu[6], sd[6];
    for (int i = 0; i < 6; i++) { dv[i] = mice_digit(dst[i], &cu[i], &sd[i]); if (dv[i] < 0) return -2; }
    for (int i = 3; i < 6; i++) if (cu[i]) return -1;                      /* characters 4 to 6 carry flags only from the P..Z set */
    int anyc = cu[0] | cu[1] | cu[2], anys = sd[0] | sd[1] | sd[2];
    if (anyc && anys) return -1;                                           /* custom and standard mixed */
    int bits = ((cu[0] | sd[0]) << 2) | ((cu[1] | sd[1]) << 1) | (cu[2] | sd[2]);
    int north = sd[3], off = sd[4], west = sd[5], mm = dv[2] * 10 + dv[3];
    long lat_h = (long)(dv[0] * 10 + dv[1]) * 6000 + mm * 100 + dv[4] * 10 + dv[5];
    if (mm >= 60 || lat_h > 90 * 6000L) return -1;
    const unsigned char *b = (const unsigned char *)info;
    for (int i = 1; i <= 6; i++) if (b[i] < 28 || b[i] > 127) return -1;
    if (b[7] < 33 || b[7] > 126) return -1;
    int lo = b[1] - 28; if (off) lo += 100; if (lo >= 190 && lo <= 199) lo -= 190; else if (lo >= 180 && lo <= 189) lo -= 80;
    int m = b[2] - 28; if (m >= 60) m -= 60; int h = b[3] - 28;
    int sp = b[4] - 28, dc = b[5] - 28, se = b[6] - 28, speed = sp * 10 + dc / 10, course = (dc % 10) * 100 + se;
    if (speed >= 800) speed -= 800; if (course >= 400) course -= 400;
    if (lo > 180 || m > 59 || h > 99 || course > 360) return -1;
    long lon_h = (long)lo * 6000 + m * 100 + h;
    P->dt = '!'; P->comp = 0; P->lat_idx = 90 * 6000L + (north ? lat_h : -lat_h);
    P->lon_idx = (180 * 6000L + (west ? -lon_h : lon_h)) % 2160000L; if (P->lon_idx < 0) P->lon_idx += 2160000L;
    P->has_cs = (speed > 0 || course > 0); P->course = course; P->speed = speed;
    P->tbl = (char)b[8]; P->code = (char)b[7];
    strncpy(P->comment, info + 9, 259);
    P->mice = 1; P->mstat = bits + ((anyc && bits) ? 8 : 0);
    return 1;
}

/* Mic-E encoder: the 6 destination characters and info bytes 1..8 from decoded values (the inverse of parse_mice). Used by SELFTEST and to check every real packet:
   a Mic-E packet whose characters are not what this writes back used another, equivalent encoding. */
static void mice_build(const Pos *P, char *dst6, unsigned char *b) {
    long la = P->lat_idx - 90 * 6000L, lh = la < 0 ? -la : la; int north = la >= 0, d[6], msg = P->mstat & 7, cust = (P->mstat >> 3) & 1;
    d[0] = (int)(lh / 6000) / 10; d[1] = (int)(lh / 6000) % 10; d[2] = (int)((lh % 6000) / 100) / 10; d[3] = (int)((lh % 6000) / 100) % 10; d[4] = (int)(lh % 100) / 10; d[5] = (int)(lh % 100) % 10;
    for (int i = 0; i < 3; i++) dst6[i] = (char)(((msg >> (2 - i)) & 1) ? (cust ? 'A' : 'P') + d[i] : '0' + d[i]);
    long lo = P->lon_idx - 180 * 6000L; int west = lo < 0; long lw = lo < 0 ? -lo : lo; int deg = (int)(lw / 6000), mn = (int)((lw % 6000) / 100), hu = (int)(lw % 100), off, b1;
    if (deg <= 9) { off = 1; b1 = deg + 118; } else if (deg <= 99) { off = 0; b1 = deg + 28; } else if (deg <= 109) { off = 1; b1 = deg + 8; } else { off = 1; b1 = deg - 72; }
    dst6[3] = (char)(north ? 'P' + d[3] : '0' + d[3]); dst6[4] = (char)(off ? 'P' + d[4] : '0' + d[4]); dst6[5] = (char)(west ? 'P' + d[5] : '0' + d[5]); dst6[6] = 0;
    int spd = P->has_cs ? P->speed : 0, crs = P->has_cs ? P->course : 0;
    b[1] = (unsigned char)b1; b[2] = (unsigned char)(mn < 10 ? mn + 88 : mn + 28); b[3] = (unsigned char)(hu + 28);
    /* the usual form: speed under 200 kn is written with +800 (SP 80..99), course with +400 (hundreds digit 4..7); the decoder subtracts them again */
    b[4] = (unsigned char)((spd < 200 ? spd / 10 + 80 : spd / 10) + 28); b[5] = (unsigned char)((spd % 10) * 10 + crs / 100 + 4 + 28); b[6] = (unsigned char)(crs % 100 + 28);
    b[7] = (unsigned char)P->code; b[8] = (unsigned char)P->tbl;
}
/* the packed integer for a Mic-E position (the 8 byte PACKPOS=2 layout times 16 for the status), decoded again; counts mismatches */
static void mice_check(const Pos *P) {
    uint64_t base = (uint64_t)P->lat_idx * 2160000ULL + P->lon_idx, v; int dir = P->has_cs ? P->course : 0, spd = P->has_cs ? (P->speed > 999 ? 999 : P->speed) : 0;
    v = P->has_cs ? (((base * 361 + dir) * 1000 + spd) * 16 + P->mstat) : base * 16 + P->mstat;
    uint64_t w = v; int st = (int)(w % 16); w /= 16; int ds = 0, dd = 0;
    if (P->has_cs) { ds = (int)(w % 1000); w /= 1000; dd = (int)(w % 361); w /= 361; }
    long lo = (long)(w % 2160000ULL), la = (long)(w / 2160000ULL);
    if (st != P->mstat || la != P->lat_idx || lo != P->lon_idx || ds != spd || dd != dir) mice_rtfail++;
    mice_stat[P->mstat & 15]++;
}
static int pos_native_cost(const Pos *P, int packed) {
    if (P->mice) {                                                /* flags byte, symbol (2), then the packed integer (status folded in) or the lossless block plus a status byte */
        if (packed) return 1 + 2 + (P->has_cs ? 8 : 6);
        return 1 + 2 + 6 + (P->has_cs ? 3 : 0) + ((MICE_DEF >= 0 && P->mstat == MICE_DEF) ? 0 : 1);
    }
    int c = 1 + (P->has_ts ? 3 : 0) + 2 + (P->has_alt ? 4 : 0);
    int pk = P->has_cs ? (PACKPOS == 2 ? 8 : 7) : 6;
    if (!P->comp) c += packed ? pk : 6 + (P->has_cs ? 3 : 0);
    else c += packed ? pk + 1 + (P->cs_alt ? 2 : 0) : 7 + (P->cs_none ? 1 : 3);
    return c;
}
static long cs_course_chg, cs_speed_chg;
static long pos_n, pos_cs, pos_comp, pos_diff, pos_diff_pos, pos_diff_course, pos_diff_speed, pos_roundtrip_fail;
#define DMUL (PACKPOS == 2 ? 361ULL : 73ULL)     /* direction values: 0 unknown, then 1 to 72 (5 degrees) or 1 to 360 (1 degree) */
#define SMUL (PACKPOS == 2 ? 1000ULL : 256ULL)   /* speed values: 0 to 255 or 0 to 999 kn */
#define DSTEP (PACKPOS == 2 ? 1 : 5)
static int dir_of(int course) { if (PACKPOS == 2) return course > 360 ? 360 : course; int r = (course + 2) / 5; return course == 0 ? 0 : (r ? r : 1); }   /* 0 unknown, 1..72 (72 = north) */
static int speed_of(int s) { int m = (int)SMUL - 1; return s > m ? m : s; }
/* the packed integer, its decoding, and the rebuilt text; counts what changes */
static void pos_pack_check(const Pos *P, const char *info) {
    pos_n++; if (P->comp) pos_comp++; if (P->has_cs) pos_cs++;
    int dir = P->has_cs ? dir_of(P->course) : 0, spd = P->has_cs ? speed_of(P->speed) : 0;
    uint64_t v = P->has_cs ? (((uint64_t)P->lat_idx * 2160000ULL + P->lon_idx) * DMUL + dir) * SMUL + spd : (uint64_t)P->lat_idx * 2160000ULL + P->lon_idx;
    /* decode */
    uint64_t w = v; int ds = 0, dd = 0; if (P->has_cs) { ds = (int)(w % SMUL); w /= SMUL; dd = (int)(w % DMUL); w /= DMUL; }
    long lo = (long)(w % 2160000ULL), la = (long)(w / 2160000ULL);
    if (la != P->lat_idx || lo != P->lon_idx || ds != spd || dd != dir) pos_roundtrip_fail++;
    /* rebuilt text */
    char out[400]; int k = 0; out[k++] = P->dt; if (P->has_ts) { memcpy(out + k, P->ts, 7); k += 7; }
    int cdir = dd == 0 ? 0 : dd * DSTEP;
    if (!P->comp) {
        long H = la - 90 * 6000L; int s = H < 0; if (s) H = -H;
        k += sprintf(out + k, "%02ld%02ld.%02ld%c%c", H / 6000, (H % 6000) / 100, H % 100, s ? 'S' : 'N', P->tbl);
        long L = lo - 180 * 6000L; int w2 = L < 0; if (w2) L = -L;
        k += sprintf(out + k, "%03ld%02ld.%02ld%c%c", L / 6000, (L % 6000) / 100, L % 100, w2 ? 'W' : 'E', P->code);
        if (P->has_cs) { k += sprintf(out + k, "%03d/%03d", cdir, ds); if (cdir != P->course) cs_course_chg++; if (ds != P->speed) cs_speed_chg++; }
    } else {
        double lat = la / 6000.0 - 90, lon = lo / 6000.0 - 180;
        long y = (long)floor(380926.0 * (90 - lat)), x = (long)floor(190463.0 * (180 + lon));
        char yx[9]; for (int i = 3; i >= 0; i--) { yx[i] = 33 + y % 91; y /= 91; yx[4 + i] = 33 + x % 91; x /= 91; }
        out[k++] = P->tbl; memcpy(out + k, yx, 8); k += 8; out[k++] = P->code;
        if (P->has_cs) { out[k] = 33 + (int)lround(cdir / 4.0); out[k + 1] = 33 + (int)lround(log(ds + 1.0) / log(1.08)); if (out[k] != P->cs0) cs_course_chg++; if (out[k + 1] != P->cs1) cs_speed_chg++; k += 2; } else { out[k++] = P->cs0; out[k++] = P->cs1; }
        out[k++] = P->T;
    }
    out[k] = 0;
    char com[300]; strcpy(com, P->comment);
    if (P->has_alt) { char tmp[300]; int n = (int)strlen(P->altstr); memcpy(tmp, com, P->alt_off); memcpy(tmp + P->alt_off, "/A=", 3); memcpy(tmp + P->alt_off + 3, P->altstr, n); strcpy(tmp + P->alt_off + 3 + n, com + P->alt_off); strcpy(com, tmp); }
    strcat(out, com);
    if (strcmp(out, info)) {
        pos_diff++;
        if (P->comp) pos_diff_pos++;
        else if (P->has_cs && (cdir != P->course)) pos_diff_course++;
        else if (P->has_cs && P->speed > (int)SMUL - 1) pos_diff_speed++;
        else pos_diff_pos++;
    }
}

/* LoRa airtime (s), explicit header, CRC, CR 4/5, 8-symbol preamble, low-data-rate-optimise at SF11/12 */
static double airtime2(int bytes, int sf) {
    double tsym = pow(2, sf) / 125000.0; int de = sf >= 11;
    double x = ceil((8.0 * bytes - 4 * sf + 28 + 16) / (4.0 * (sf - 2 * de)));
    if (x < 0) x = 0;
    return (8 + 4.25) * tsym + (8 + x * 5) * tsym;
}

/* per-station sender and receiver state: last full/plain text, count of transmissions */
typedef struct { char key[16]; int used; uint8_t stx[256]; int stl; int seq; uint8_t srx[256]; int srl; } St;
#define HT 65536
static St *tab;
static St *get(const char *k) { unsigned h = 5381; for (const char *p = k; *p; p++) h = h * 33 + (unsigned char)*p; h %= HT;
    while (tab[h].used && strcmp(tab[h].key, k)) h = (h + 1) % HT;
    if (!tab[h].used) { tab[h].used = 1; strncpy(tab[h].key, k, 15); } return &tab[h]; }

/* groups */
enum { G_ALL, G_RF, G_STAT, G_MOV, G_HOP, NG };
static const char *GN[NG] = { "all LoRa packets (held-out stations)", "heard over RF (qAR/qAO)", "stationary stations", "moving stations", "RF, path starts WIDE1-1 (hop subset)" };
typedef struct { long n; double plain, bytes, ap12, ac12, ap7, ac7; long primed, full, plainfr; } Acc;

/* relayed text: WIDE1-1 replaced by "DIGI*" (fill-in digipeater), rest unchanged. returns 1 if applicable */
static int hop(const Row *r, char *outpath) {
    if (!r->rf || strncmp(r->path, "WIDE1-1", 7)) return 0;
    snprintf(outpath, 96, "%s*%s", HOPCALL, r->path + 7);
    return 1;
}
static int in_group(const Row *r, int g, int hopped) {
    switch (g) { case G_ALL: return 1; case G_RF: return r->rf; case G_STAT: return r->kind == 'S'; case G_MOV: return r->kind == 'M';
                 case G_HOP: return hopped; } return 0; }

/* returns frame bytes; layout 0 = whole packet, 1 = path outside. extra = path block bytes (layout 1). Verifies decode. */
static long fails, escapes, pathrows, unencodable; static int RXTH = 0, RXTFULL = 0; /* relays that each append a 4-byte tuple; +1 mask byte once */
static int send_one(int layout, int N, const Row *r, int hopped, const char *hp, int *kindout) {
    uint8_t t[300]; int tl; int extra = 0; char dst[64];
    const char *path = hopped ? hp : r->path;
    /* build the text this station-hop transmits */
    const char *gt = strchr((char *)r->text, '>'); const char *colon = strchr((char *)r->text, ':');
    int srcl = (int)(gt - (char *)r->text); const char *dstp = gt + 1;
    const char *c = dstp; while (*c && *c != ',' && *c != ':') c++;
    int dl = (int)(c - dstp); memcpy(dst, dstp, dl); dst[dl] = 0;
    const char *info = colon + 1;
    int pcode = path_code(r->path); pathrows++;
    int pathextra = hopped ? 6 : (pcode >= 0 ? 1 : path_native(path)); if (!hopped && pcode < 0) escapes++;
    if (NATIVE) {                       /* a row the native encoder cannot carry goes out as a plain frame in every layout, so all layouts cover the same packets */
        int sx = elem_native((const char *)r->text, srcl), dx = elem_native(dst, dl); pathextra = path_block_native(path);
        if (sx < 0 || dx < 0 || pathextra < 0) { unencodable++; *kindout = 2; return 3 + r->tl; }
    }
    int bempty = 0, posb = 0;
    if (layout == 0) { tl = snprintf((char *)t, 300, "%.*s>%s%s%s:%s", srcl, r->text, dst, *path ? "," : "", path, info); }
    else if (layout == 1) { tl = snprintf((char *)t, 300, "%.*s>%s:%s", srcl, r->text, dst, info); extra = pathextra; }
    else {
        const char *body = info; Pos P;
        int micedone = 0;
        if (MICE2NATIVE) {
            int mr = parse_mice(dst, dl, info, &P);
            if (!hopped && mr != 0) { mice_seen++; if (mr == 1) mice_conv++; else if (mr == -2) mice_amb++; else mice_inv++; }
            if (mr == 1 && micedump && !hopped && layout == 2) fprintf(stderr, "%d %ld %ld %d %d\n", P.mstat, P.lat_idx, P.lon_idx, P.speed, P.course);
            if (mr == 1) { body = P.comment; posb = pos_native_cost(&P, PACKPOS == 2); micedone = 1; if (!hopped) { mice_check(&P); char d6[8]; unsigned char bb[9]; mice_build(&P, d6, bb); if (strncmp(d6, dst, 6) || memcmp(bb + 1, info + 1, 8)) { mice_exact_fail++; if (getenv("MICEDIFF") && mice_exact_fail <= 6) { fprintf(stderr, "orig %s ", dst); for (int q = 1; q <= 8; q++) fprintf(stderr, "%02x ", (unsigned char)info[q]); fprintf(stderr, "| rebuilt %s ", d6); for (int q = 1; q <= 8; q++) fprintf(stderr, "%02x ", bb[q]); fprintf(stderr, "| speed %d course %d\n", P.speed, P.course); } } } bempty = !*body; }
        }
        if (!micedone && (PACKPOS || POSNATIVE) && parse_pos(info, &P)) { body = P.comment; posb = pos_native_cost(&P, PACKPOS); if (!hopped) { if (PACKPOS) pos_pack_check(&P, info); else pos_n++; }
                                                                               bempty = !*body; }
        tl = snprintf((char *)t, 300, ":%s", body);               /* leading ':' so the model sees a familiar start */
        int dc = dst_code(dst);
        if (NATIVE) {
            int sb, db, pb = pathextra;
            if (WSPRCALL) {
                uint32_t code; int sl = srcl; wsp_total++;
                if (wspr_encode((const char *)r->text, sl, &code)) { char back[16]; wspr_decode(code, back); if (strlen(back) != (size_t)sl || strncmp(back, (const char *)r->text, sl)) wsp_fails++; sb = 4; wsp_ok++; }
                else sb = elem_native((const char *)r->text, srcl);
                pb = path_block_wspr(path);
            } else sb = elem_native((const char *)r->text, srcl);
            if (micedone && !getenv("MICE_KEEPDST")) db = 1;   /* a native-position tracker sends its own tocall, a table hit (MICE_KEEPDST=1 keeps the Mic-E destination cost, to see the position alone) */
            else if (TOCALL) db = tocall_cost(dst, dl, info); else db = DSTCODE && dc >= 0 ? 1 : elem_native(dst, dl);
            if (sb < 0 || db < 0 || pb < 0) { unencodable++; *kindout = 2; return 3 + r->tl; }
            extra = sb + db + pb + posb;
        } else
        extra = (src_packable(r->text, srcl) ? 5 : 1 + srcl) + (dc >= 0 ? 1 : 1 + (int)strlen(dst)) + pathextra; }
    if (dumpf && layout == 2) { if (!bempty) fprintf(dumpf, "%s\n", (char *)t); return -1; }
    if (bempty) { *kindout = 2; return 3 + extra + ((hopped && RXTH > 0) ? 4 * RXTH + 1 : 0); }     /* position block and no comment: no text at all */
    if (tl > 255) return -1;
    St *s = get(r->src);
    uint8_t f[400], back[400]; int fl = 0, kind = 0; size_t n;
    int wantfull = (s->stl == 0) || (N <= 1) || (s->seq % N == 0);
    s->seq++;
    if (!wantfull) {
        n = aprs_ppm_encode_primed(s->stx, s->stl, t, tl, f, sizeof f);
        if (n) { fl = (int)n; kind = 1; } else wantfull = 1;
    }
    if (wantfull) {
        n = aprs_ppm_encode_frame(t, tl, f, sizeof f);
        if (n) { fl = (int)n; kind = 0; } else { memcpy(f, "\x3c\xff\x01", 3); memcpy(f + 3, t, tl); fl = 3 + tl; kind = 2; }
        memcpy(s->stx, t, tl); s->stl = tl;                     /* a full or plain frame becomes the reference */
    }
    /* receiver */
    if (kind == 2) { memcpy(back, f + 3, tl); n = tl; }
    else if (kind == 0) n = aprs_ppm_decode_frame(f, fl, back, sizeof back);
    else n = aprs_ppm_decode_primed(s->srx, s->srl, f, fl, back, sizeof back);
    if (n != (size_t)tl || memcmp(back, t, tl)) { fails++; return -1; }
    if (kind != 1) { memcpy(s->srx, back, n); s->srl = (int)n; }
    /* path block round trip (layout 1): code table must give the original path back */
    if (layout == 1 && !hopped && !NATIVE) { int code = path_code(r->path); if (code >= 0 && strcmp(CODES[code], r->path)) fails++; }
    *kindout = kind;
    if (layout == 2 && PLAINTEXT && kind == 2) fl -= 1;           /* the native frame carries the text as it is, without the ':' lead byte */
    if (layout == 2 && kind == 1) extra -= 1;                    /* station id not needed: source is readable */
    if (hopped && RXTH > 0 && (!RXTFULL || kind != 1)) extra += 4 * RXTH + 1;               /* RXT: one tuple per relay + mask byte */
    return fl + extra;
}

int main(int argc, char **argv) {
    if (getenv("PACKPATH")) PACKPATH = atoi(getenv("PACKPATH"));
    if (getenv("RXTFULL")) RXTFULL = atoi(getenv("RXTFULL"));
    if (getenv("RXT")) RXTH = atoi(getenv("RXT"));
    if (getenv("NATIVE")) NATIVE = atoi(getenv("NATIVE"));
    if (getenv("TOCALL")) TOCALL = atoi(getenv("TOCALL"));
    if (getenv("PACKPOS")) PACKPOS = atoi(getenv("PACKPOS"));
    if (getenv("MICE2NATIVE")) MICE2NATIVE = atoi(getenv("MICE2NATIVE"));
    if (getenv("MICE_DEF")) MICE_DEF = atoi(getenv("MICE_DEF"));
    micedump = getenv("MICEDUMP") != NULL; if (getenv("MICE_DST")) MICE_DST = atoi(getenv("MICE_DST"));
    if (getenv("POSNATIVE")) POSNATIVE = atoi(getenv("POSNATIVE"));
    if (getenv("WSPRCALL")) WSPRCALL = atoi(getenv("WSPRCALL"));
    if (getenv("WSPRSTAR")) WSPRSTAR = atoi(getenv("WSPRSTAR"));
    if (getenv("PLAINTEXT")) PLAINTEXT = atoi(getenv("PLAINTEXT"));
    if (getenv("HOPCALL")) HOPCALL = getenv("HOPCALL");
    if ((TOCALL || PACKPOS || POSNATIVE || WSPRCALL || MICE2NATIVE) && !NATIVE) { fprintf(stderr, "TOCALL, PACKPOS, POSNATIVE and WSPRCALL need NATIVE=1\n"); return 1; }
    if (TOCALL) { if (!getenv("TOCALLFILE")) { fprintf(stderr, "TOCALL=1 needs TOCALLFILE (tocall_table.py)\n"); return 1; } tocall_load(getenv("TOCALLFILE")); }
    if (getenv("DUMPTEXT")) dumpf = stdout;
    if (getenv("DSTCODE")) DSTCODE = atoi(getenv("DSTCODE"));
    FILE *f = fopen(argv[1], "r"); char line[1024]; rows = calloc(MAXROWS, sizeof(Row)); tab = calloc(HT, sizeof(St));
    while (fgets(line, sizeof line, f) && nrows < MAXROWS) {
        char *p[5]; char *q = line; int i;
        for (i = 0; i < 5; i++) { p[i] = q; q = strchr(q, '\t'); if (!q) break; *q++ = 0; if (i == 3) { p[4] = q; q = strchr(q, '\n'); if (q) *q = 0; i = 4; break; } }
        if (i < 4) continue;
        Row *r = &rows[nrows++]; strncpy(r->src, p[0], 15); r->rf = p[1][0] == '1'; r->kind = p[2][0]; strncpy(r->path, p[3], 95);
        r->tl = (int)strlen(p[4]); memcpy(r->text, p[4], r->tl + 1);
    }
    fclose(f);
    if (getenv("DUMPCOST")) {
        for (int i = 0; i < nrows; i++) {
            const char *t = (const char *)rows[i].text, *gt = strchr(t, '>'), *d = gt + 1, *c = d;
            while (*c && *c != ',' && *c != ':') c++;
            printf("%d %d %d\n", elem_native(t, (int)(gt - t)), elem_native(d, (int)(c - d)), path_block_native(rows[i].path));
        }
        return 0;
    }
    if (dumpf) { char hp[96] = ""; for (int i = 0; i < nrows; i++) { int k; send_one(2, 1, &rows[i], 0, hp, &k); } return 0; }   /* DUMPTEXT=1: the text hybrid B would hand to PPM */
    if (getenv("SELFTEST")) {                                    /* one packed position, decoded again */
        Pos P; const char *ex = "=3725.12N/12158.34W>123/045/A=000321 test"; parse_pos(ex, &P);
        int dir = dir_of(P.course), spd = speed_of(P.speed);
        uint64_t v = (((uint64_t)P.lat_idx * 2160000ULL + P.lon_idx) * DMUL + dir) * SMUL + spd, w = v;
        int ds = (int)(w % SMUL); w /= SMUL; int dd = (int)(w % DMUL); w /= DMUL; long lo = (long)(w % 2160000ULL), la = (long)(w / 2160000ULL);
        printf("sample %s\n  lat_idx %ld lon_idx %ld dir_idx %d speed %d  v = %llu (%d bytes, max %llu)\n  decoded: lat %.6f lon %.6f course %d speed %d kn\n",
               ex, P.lat_idx, P.lon_idx, dir, spd, (unsigned long long)v, PACKPOS == 2 ? 8 : 7, (unsigned long long)(1080001ULL * 2160000ULL * DMUL * SMUL - 1),
               la / 6000.0 - 90, lo / 6000.0 - 180, dd * DSTEP, ds);
        { /* Mic-E: every message status, standard and custom, written and decoded again (value 0 is Emergency) */
            int bad = 0, n = 0; Pos Q; memset(&Q, 0, sizeof Q); Q.lat_idx = 90 * 6000L + 3348 * 100L + 25; Q.lon_idx = 180 * 6000L - (112 * 6000L + 7 * 100 + 46); Q.has_cs = 1; Q.speed = 25; Q.course = 357; Q.code = '>'; Q.tbl = '/';
            for (int stv = 0; stv < 16; stv++) { if (stv > 8 && (stv & 7) == 0) continue; if (stv == 8) continue; Q.mstat = stv; char d6[8]; unsigned char bb[9]; bb[0] = '`'; mice_build(&Q, d6, bb);
                char info[16]; info[0] = '`'; memcpy(info + 1, bb + 1, 8); info[9] = 0; Pos R; int r = parse_mice(d6, 6, info, &R); n++;
                if (r != 1 || R.mstat != stv || R.lat_idx != Q.lat_idx || R.lon_idx != Q.lon_idx || R.speed != 25 || R.course != 357) { bad++; printf("  Mic-E status %d: FAILED (decode %d)\n", stv, r); } }
            printf("Mic-E self-test: %d status values (0 = Emergency, 1..7 standard bits, 9..15 custom) written and decoded again, %d failures; sample dest %s\n", n, bad, "S32U6T"); }
        uint32_t code; char back[16]; wspr_encode("K6ATV-9", 7, &code); wspr_decode(code, back); printf("  callsign K6ATV-9 -> %u -> %s\n", code, back);
        return 0; }
    printf("%d packets\n", nrows);
    int Ns[8], nN = 0; for (int i = 2; i < argc; i++) Ns[nN++] = atoi(argv[i]); if (!nN) Ns[nN++] = 2;
    int lmask = getenv("LAYOUTS") ? atoi(getenv("LAYOUTS")) : 7, hpmax = getenv("NOHOP") ? 1 : 2;
    for (int hp_on = 0; hp_on < hpmax; hp_on++) {
      for (int ni = 0; ni < nN; ni++) for (int layout = 0; layout < 3; layout++) { if (!(lmask >> layout & 1)) continue;
        Acc a[NG]; memset(a, 0, sizeof a); memset(tab, 0, sizeof(St) * HT); fails = 0; escapes = 0; pathrows = 0; unencodable = 0; memset(tc_class, 0, sizeof tc_class); pos_n = pos_cs = pos_comp = pos_diff = pos_diff_pos = pos_diff_course = pos_diff_speed = pos_roundtrip_fail = cs_course_chg = cs_speed_chg = 0; mice_seen = mice_conv = mice_amb = mice_inv = mice_rtfail = mice_exact_fail = 0; memset(mice_stat, 0, sizeof mice_stat); wsp_ok = wsp_total = wsr_ok = wsr_total = wsp_fails = 0;
        for (int i = 0; i < nrows; i++) {
            Row *r = &rows[i]; char hpth[96]; int hopped = hp_on ? hop(r, hpth) : 0; int kind = 0;
            int fl = send_one(layout, Ns[ni], r, hopped, hpth, &kind);
            if (fl < 0) continue;
            for (int g = 0; g < NG; g++) if (in_group(r, g, hopped)) {
                int plain = 3 + r->tl + 0;
                a[g].n++; a[g].plain += plain; a[g].bytes += fl; a[g].ap12 += airtime2(plain, 12); a[g].ac12 += airtime2(fl, 12);
                a[g].ap7 += airtime2(plain, 7); a[g].ac7 += airtime2(fl, 7);
                if (kind == 1) a[g].primed++; else if (kind == 0) a[g].full++; else a[g].plainfr++;
            }
        }
        printf("\n%s, N=%d, %s   (decode failures: %ld%s)\n", layout == 2 ? "HYBRID B (native src,dst,path; PPM info)" : layout ? "HYBRID A (native path; PPM src,dst,info)" : "WHOLE PACKET ", Ns[ni], hp_on ? "after one relay hop (where applicable)" : "heard direct", fails,
               layout ? "" : "");
        for (int g = 0; g < NG; g++) { if (hp_on == 0 && g == G_HOP) continue; if (!a[g].n) continue;
            printf("  %-40s n=%6ld  avg %5.1f B (plain %5.1f)  saved %5.1f%%  SF12 airtime saved %5.1f%%  SF7 %5.1f%%  primed %4.1f%%\n",
                GN[g], a[g].n, a[g].bytes / a[g].n, a[g].plain / a[g].n, 100 * (1 - a[g].bytes / a[g].plain),
                100 * (1 - a[g].ac12 / a[g].ap12), 100 * (1 - a[g].ac7 / a[g].ap7), 100.0 * a[g].primed / a[g].n); }
        if (layout == 1) printf("  path block: %ld rows, %ld needed the escape (%.1f%%)\n", pathrows, escapes, 100.0 * escapes / (pathrows ? pathrows : 1));
        if (NATIVE && unencodable) printf("  sent as plain frames: %ld rows the native encoder cannot carry\n", unencodable);
        if (layout == 2 && TOCALL) { long t = tc_class[0] + tc_class[1] + tc_class[2] + tc_class[3]; if (t) printf("  destination: table (1 B) %.1f%%, registry pattern (2 B) %.1f%%, Mic-E (%d B) %.1f%%, escape %.1f%%   (%ld packets)\n", 100.0 * tc_class[0] / t, 100.0 * tc_class[1] / t, MICE_DST, 100.0 * tc_class[2] / t, 100.0 * tc_class[3] / t, t); }
        if (layout == 2 && (PACKPOS || POSNATIVE) && pos_n) { printf("  position block: %ld packets carried natively (%ld compressed, %ld with course/speed)\n", pos_n, pos_comp, pos_cs);
            if (PACKPOS) printf("  packed position: rebuilt text differs for %ld (%.1f%%): compressed re-encoding %ld, course rounded %ld, speed above 255 kn %ld; integer round trip failures %ld; of %ld with course/speed, course token changed %ld, speed token changed %ld\n", pos_diff, 100.0 * pos_diff / pos_n, pos_diff_pos, pos_diff_course, pos_diff_speed, pos_roundtrip_fail, pos_cs, cs_course_chg, cs_speed_chg); }
        if (layout == 2 && MICE2NATIVE) { printf("  Mic-E: %ld seen, %ld sent as a native position, %ld left as text (ambiguous position %ld, invalid or mixed %ld); packed integer round trip failures %ld; rebuilding the 6 destination and 8 information characters from the values differs for %ld\n    status values (bits, +8 custom):", mice_seen, mice_conv, mice_amb + mice_inv, mice_amb, mice_inv, mice_rtfail, mice_exact_fail);
            for (int i = 0; i < 16; i++) if (mice_stat[i]) printf(" %d:%ld", i, mice_stat[i]); printf("\n"); }
        if (layout == 2 && WSPRCALL && wsp_total) printf("  callsigns packed into 4 bytes: source %.1f%% of %ld, relay elements %.1f%% of %ld; round trip failures %ld\n", 100.0 * wsp_ok / wsp_total, wsp_total, wsr_total ? 100.0 * wsr_ok / wsr_total : 0.0, wsr_total, wsp_fails);
      }
    }
    return 0;
}
