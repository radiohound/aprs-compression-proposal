/* Relay edge cases found by a review: a relay must not build a frame that its own decoder rejects, and must not repeat what it already repeated.
 * Build: see the Makefile target hb3-edge. Every check prints FAIL on a miss; the exit status is the number of misses. */
#include "hb3.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails = 0;
#define CHECK(name, cond) do { if (!(cond)) { printf("FAIL: %s\n", name); fails++; } } while (0)

static size_t unhex(const char *h, uint8_t *f) { size_t n = 0; unsigned x; while (h[0] && h[1]) { sscanf(h, "%2x", &x); f[n++] = (uint8_t)x; h += 2; } return n; }

int main(void)
{
    uint8_t f[300], o[300], tup[4] = { 50, 20, 45, 10 }; char t[700]; size_t n, r; hb3_info_t in;
    hb3_load_list("tools/hybrid/tables/tocall_set1.txt");

    /* 1. an earlier tuple whose element lands at the eighth place or later of the new path: it is left out (the mask has seven bits), the frame still decodes */
    n = unhex("3cff032b874755da4c3aa159a392e8121003ef9998938a5c6b04d0580043492b8bb3efb93ded6dd7e191f0ea2de72e8b4775915a30c4735cd6a3aa32142d0a81", f);
    r = hb3_decode(f, n, t, sizeof t, &in);
    CHECK("edge 1: the vector decodes", r > 0 && in.has_rxt && in.rxt.n >= 1);
    { char *c = strchr(t, ':'), *g = strchr(t, '>'); char src[50], dst[50], nt[900]; const char *cm = strchr(g, ',');
      memcpy(src, t, (size_t)(g - t)); src[g - t] = 0; memcpy(dst, g + 1, (size_t)(cm - g - 1)); dst[cm - g - 1] = 0;
      snprintf(nt, sizeof nt, "%s>%s,X0*,X1*,X2*,X3*,X4*,X5*,X6*,N0RLY-1*,N0RLY-2*%s", src, dst, c);
      r = hb3_relay_to(f, n, nt, strlen(nt), "N0RLY-2", tup, o, sizeof o);
      CHECK("edge 1: relayed or refused, never a frame the decoder rejects", r == 0 || hb3_decode(o, r, t, sizeof t, 0) > 0);
      r = hb3_relay_to_passthrough(f, n, nt, strlen(nt), "N0RLY-2", tup, o, sizeof o);
      CHECK("edge 1: pass-through the same", r == 0 || hb3_decode(o, r, t, sizeof t, 0) > 0); }

    /* 2. a frame that has already been through this relay is not repeated again */
    n = hb3_encode("K0TEST-7>APLRG1,N0RLY-1*,WIDE2-1:hello world", 44, NULL, f, sizeof f, NULL);
    r = hb3_relay(f, n, "N0RLY-1", tup, o, sizeof o);
    CHECK("edge 2: not repeated by the same call again", r == 0);
    n = hb3_encode("K0TEST-7>APLRG1,WIDE1-1:hello world", 35, NULL, f, sizeof f, NULL);
    r = hb3_relay(f, n, "N0RLY-1", tup, o, sizeof o);
    CHECK("edge 2: the first relay still works", r > 0 && hb3_decode(o, r, t, sizeof t, &in) > 0 && in.rxt.n == 1 && in.rxt.idx[0] == 0);

    /* 3. a tuple byte of 0x80 or more, and a non-compliant WIDE path chosen by the caller, are refused */
    { uint8_t bad[4] = { 50, 200, 45, 10 };
      CHECK("edge 3: tuple byte >= 0x80 refused", hb3_relay(f, n, "N0RLY-1", bad, o, sizeof o) == 0); }
    { const char *nt = "K0TEST-7>APLRG1,N0RLY-1*,WIDE2-3:hello world";
      CHECK("edge 3: non-compliant WIDE in the caller's path refused", hb3_relay_to(f, n, nt, strlen(nt), "N0RLY-1", tup, o, sizeof o) == 0);
      CHECK("edge 3: pass-through the same", hb3_relay_to_passthrough(f, n, nt, strlen(nt), "N0RLY-1", tup, o, sizeof o) == 0); }

    /* 4. relay without the model forwards nothing that a full receiver drops for a reason other than a model or a destination code it does not hold:
     * every rejection vector must fail hb3_decode_front, except those whose stated reason is exactly that */
    { FILE *fp = fopen("test/vectors_hb3_reject_v1.txt", "r"); static char line[4000]; int seen = 0, kept = 0;
      CHECK("edge 4: the rejection vectors can be read", fp != NULL);
      while (fp && fgets(line, sizeof line, fp)) {
          char *sp; uint8_t fr[300]; size_t fn; int ok_unread;
          if (line[0] == '#' || line[0] == '\n') continue;
          sp = strchr(line, ' '); if (!sp) continue; *sp = 0;
          fn = unhex(line, fr); seen++;
          /* the three things a relay without the model cannot see: a model or destination code it lacks, and a NUL inside PPM-coded text (the text is not there to look at) */
          ok_unread = strstr(sp + 1, "is not held") != NULL || strstr(sp + 1, "is not assigned") != NULL || strstr(sp + 1, "PPM text holds a NUL") != NULL;
          r = (size_t)(hb3_decode_front(fr, fn, t, sizeof t, 0) > 0);
          if (!ok_unread && r) { printf("FAIL: edge 4: a full receiver drops this and the relay without the model keeps it: %s\n", sp + 1); fails++; }
          if (ok_unread && r) kept++;
      }
      if (fp) fclose(fp);
      CHECK("edge 4: the vectors for a missing model are still relayable", kept >= 4);
      CHECK("edge 4: enough vectors were checked", seen > 40); }

    if (!fails) printf("hb3 edge cases: all pass\n");
    return fails;
}
