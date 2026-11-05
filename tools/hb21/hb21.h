/* Reference implementation of the frame of spec 11 (hybrid B): 3C FF 03 | header | source | destination | path... | [position block] | content field | [RXT block].
 * Kinds 2 and 3 only (a frame of kind 0 or 1 is dropped). Written from spec/11. */
#ifndef HB21_H
#define HB21_H
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif
#define HB21_MAX_TUPLES 7
#define HB21_SPEC_VERSION "2.1.0-dev.1"   /* semantic version of spec 11: this file implements the proposal 2.1.0-dev.1 (not adopted); V = c ss mmmmm; MINOR = backward-compatible addition, PATCH = wording only */
typedef struct { int n; uint8_t idx[HB21_MAX_TUPLES]; uint8_t t[HB21_MAX_TUPLES][4]; } hb21_rxt_t;     /* tuples in ascending path-element index */
typedef struct { int kind, cc, ver, npath; size_t frame_len, content_len, payload_len, front_len, path_start, content_start; hb21_rxt_t rxt; int has_rxt; int has_cap; uint8_t cap[4]; } hb21_info_t;   /* ver = the model version (V bits 4-0) */

/* Reasons for a dropped frame (negative return of hb21_decode). */
enum { HB21_ERR_PREFIX = -1, HB21_ERR_KIND = -2, HB21_ERR_RESERVED = -3, HB21_ERR_FRONT = -4, HB21_ERR_RXT = -5, HB21_ERR_VERSION = -6, HB21_ERR_CONTENT = -7,
       HB21_ERR_PAYLOAD = -8, HB21_ERR_CRC = -9, HB21_ERR_SIZE = -10, HB21_ERR_RANGE = -11, HB21_ERR_CODE_RESERVED = -12, HB21_ERR_POSITION = -13 };
const char *hb21_reason(int code);

int  hb21_set_list(const char *const *names, int n);   /* the same list given as an array (for firmware without files); returns n or -1 */
int  hb21_load_list(const char *path);       /* the destination list, set 1 (tools/hybrid/tables/tocall_set1.txt); returns the number of entries, or -1 */
uint16_t hb21_crc16(const uint8_t *p, size_t n);   /* CRC-16/X-25, check value of "123456789" is 0x906E */

/* Encode a TNC2 packet ("SRC>DST,PATH:INFO"); returns the frame length, or 0 if it cannot be encoded (the caller then sends a plain frame). rxt may be NULL. */
size_t hb21_encode(const char *tnc2, size_t len, const hb21_rxt_t *rxt, uint8_t *out, size_t cap, hb21_info_t *info);
/* The same with four capability bytes (V bit 7 is then set and the bytes follow V); capb may be NULL. */
size_t hb21_encode_cap(const char *tnc2, size_t len, const hb21_rxt_t *rxt, const uint8_t *capb, uint8_t *out, size_t cap, hb21_info_t *info);
/* The capability bytes: f = { role, model, sf, bw, cr, power, preset }; hb21_cap_pack returns 0 if a field is out of range. */
int  hb21_cap_pack(const int f[7], uint8_t out[4]);
void hb21_cap_parse(const uint8_t b[4], int f[7]);

/* 0 if the path of the TNC2 packet holds a WIDEn-N that breaks the APRS rule (n 1 to 7, N 1 to n), otherwise 1. Such a packet is dropped: hb21_encode refuses it, and the caller
 * must NOT fall back to sending it as plain text or relaying it on RF (spec 11, "Non-compliant WIDE paths"). */
int  hb21_path_ok(const char *tnc2, size_t len);

/* Receiver (spec 11 "Receiver"): returns the TNC2 length (> 0) or a negative HB21_ERR_* (the frame is dropped). */
int hb21_decode(const uint8_t *fr, size_t n, char *tnc2, size_t cap, hb21_info_t *info);

/* Relay (spec 11 "Relay"): the frame must already be accepted by hb21_decode. Rewrites the path (WIDE1-1 as first unused hop becomes relay_call*; WIDE2-2 becomes
 * relay_call*,WIDE2-1), copies the position block and the content field byte for byte, and appends the relay's tuple. Returns the new length, or 0. */
size_t hb21_relay(const uint8_t *fr, size_t n, const char *relay_call, const uint8_t tuple[4], uint8_t *out, size_t cap);
/* "Relay without the model": the same, but the frame need not be accepted by hb21_decode: only the structural checks are made (no model, no content CRC), so a frame whose
 * model or destination code the relay does not hold is forwarded, content field byte for byte. The relay must not show, upload or use such a frame. */
size_t hb21_relay_passthrough(const uint8_t *fr, size_t n, const char *relay_call, const uint8_t tuple[4], uint8_t *out, size_t cap);

/* The same relay with the new path chosen by the caller, for a digipeater with its own rules: new_tnc2 is the received packet with the path as the relay sends it (it
 * holds relay_call + "*" as the element the relay used); source, destination and information field must be those of the received frame. Copies the source and the
 * destination exactly as received, the position block and the content byte for byte, keeps the tuples of the earlier hops (each moves with its path element) and
 * appends the relay's tuple at its element, unless the frame would then be longer than 255 bytes: it is then forwarded without the relay's tuple (the earlier tuples stay).
 * Returns 0 if the frame must not be relayed: it holds a tuple on the relay's element or a later one, the element is not in the new path, or it does not fit even without
 * the relay's tuple. */
size_t hb21_relay_to(const uint8_t *fr, size_t n, const char *new_tnc2, size_t new_len, const char *relay_call, const uint8_t tuple[4], uint8_t *out, size_t cap);
#ifdef __cplusplus
}
#endif
#endif
