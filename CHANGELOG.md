# Changelog

LACF (spec 11) uses semantic versions: MAJOR = wire format version (the low six bits of `V`), MINOR = backward-compatible addition, PATCH = wording only. See "Versions" in `spec/11-native-ppm-frame-byte-plan.md`. The current number is in the file `VERSION`.

## Unreleased - 3.0.0-dev.1, text revised 9 November 2026 after F4MLV's review (wording only; the bytes are unchanged)

F4MLV reviewed PR #3 on 9 November and accepted the architecture of type `3C FF 03` but not yet the draft bytes. This revision changes the text of [spec 12](spec/12-lacf-3.0-dev-byte-plan.md) and the byte reference only: the `03` typo; type `02` returns to his native draft 0, and **LACF 2.0.0 is marked historical** (retired as a type 02 format; spec 11, the byte reference and `README.md` carry a banner; spec, codec, vectors and the `v2.0.0` tag stay as the record, and `VERSION` still names that last tagged release); four capability bytes everywhere; the integrity wording (the CRC over the bytes as sent cannot show that two ends used the same table, and model-number uniqueness is normative); the destination bit is named `r`; the placeholder destination names are withdrawn; the RXT text extension is removed; capability roles 3, 4 and 6 are held back; a relay without the model is limited to two causes with one list of checks and a relay no longer converts `01` to `03`; the capability preset is the frequency ID, without country names, and 144.390 AFSK is removed. A stale comment in `tools/hb3/hb3.h` is corrected. **Still to do for the next dev tag (these change the bytes or the code):** the `3C FF 03` prefix in the CRC, the capability layout (one exact model with a plain-only value, power with 0 and negative dBm, frequency IDs renumbered), `WIDE1-2` kept as a text element, `*` rejected in a source or destination, the initial destination list, the model-retention policy, the training script fixes, the limits table and the vectors.

## Unreleased - 3.0.0-dev.1, 5 November 2026 (a proposal: not adopted, every change needs F4MLV's agreement)

**A separate frame type, `3C FF 03`, described in [spec 12](spec/12-lacf-3.0-dev-byte-plan.md).** The 2.0.0 frame (`3C FF 02`, spec 11) is unchanged and, since 9 November 2026, historical: its spec, byte reference, picture, codec (`tools/hb`) and vectors are as they were. The proposal has its own codec (`tools/hb3`, C and Python), vectors (`test/vectors_hb3_*.txt`), byte reference, picture and layout page. A device can hold both decoders (they share the PPM tables). `VERSION`, the firmware (iGate, tracker, Pi software), the README notice and the firmware page are still 2.0.0.

1. **`V = c ss mmmmm`** instead of `vvvvv ttt`: a capability flag, two spare bits and a 5-bit model version (32 models). The frame type is the generation. Plain frames ignore the model bits.
2. **Destination byte `c iiiiiii`:** 128 fixed, append-only codes (0 to 125 listed, 126 reserved, 127 the escape), no longer ranked and not versioned by `V`; one reserved bit.
3. **The CRC is over the bytes as sent** (the kind bits, `V`, the capability bytes, the source and destination bytes, the position block, `L` and the payload or text), not over the decoded text, so that a relay that does not hold the model can check it. No extra bytes.
4. **RXT:** 93 (`~`) means "not measured"; 90 to 127 other than 93 are unknown and the frame is kept; 0x80 or more still drops it.
5. **Relay without the model:** a relay that does not hold a frame's model checks the structure, the CRC and every content rule that needs no model (plain content is read like any receiver reads it; LACF content is held to the size rule and the altitude offset), forwards the frame byte for byte, adds its own tuple, and does not show or use it. In this draft it forwards nothing that a full receiver would drop for a reason other than a missing model or destination code (a draft behaviour to test; whether a relay without the model should check that much is open, see item 9). The spec states what it cannot check (the inside of PPM-coded text).
6. **Capability bytes:** four bytes after `V` when `c` is set: role (3 bits, with digipeater and iGate "late" tiers), model decoded (5), SF (3), BW (2), CR (2), **power in whole dBm (6 bits, 1 to 63)**, preset (5; 433.775, 439.9125 UK, 434.855 Poland), six spare bits. Sent about every 8th beacon. (The first draft had a 4-bit power field of odd values only, which could not give 20 or 22 dBm; F4MLV pointed that out.)
7. Measured with the C reference on 3,000 generated packets: every cut, every appended-bytes case and every single-bit flip is rejected exactly as with the 2.0.0 CRC (outside the path, which no CRC covers). The C and Python references agree on every vector file.
8. Decided not to: use destination code 126 for a packed `APL` plus three characters (only about 0.5% of real LoRa APRS packets would use it); a separate capability frame.
9. Still open: how much a relay without the model should check (the structure and the CRC only, or every rule that needs no model, as this draft does), placeholder names in the destination list, the frequency-offset range (to be discussed with Jon), how a sender chooses a model, how a receiver carries a missing RXT value to its display, the UK and Poland presets.
10. Possible later steps, not part of the proposal and not decided: firmware segmentation for over-the-air updates, a library for LACF, and where the firmware work lives. See [docs/future-work.md](docs/future-work.md).

## 2.0.0 - 2 November 2026 (protocol version 2, table version 0)

**Breaking.** Three changes to the bytes:

1. **Plain content has a CRC-16:** `V | CRC-16 (2 bytes, low first) | text`. The CRC input is the same as for LACF content (header kind bits, `V`, source, `>`, destination, `:`, information field), so a relay still never recomputes it.
2. **The form is in the header:** kind bit 1 = LACF (1) or plain (0), kind bit 0 = position block. Kinds 0 and 1 are plain (without and with a position block), 2 and 3 are LACF. Header bit 2 stays reserved. The two content codings are no longer in `V`.
3. **`V` is `vvvvv ttt`:** five bits of protocol version (2) and three bits of table version (0). The table version names the destination list and the LACF tables, revised together; the files keep their old names (`tocall_set1.txt`) but are table version 0.

* Why: F4MLV's RF test of 2 November 2026 (38 cases, commit `500c4c63`) showed that a plain frame cut by one byte before transmission was accepted and relayed with different text (cases B2, B6). Plain content had only the LoRa CRC, which is computed after the cut.
* Size rule: LACF is used when `4 + n < 3 + L` (was `4 + n < 1 + L`). Plain frames are 2 bytes longer. On 1,000 real RF packets: 11.7% are plain, +0.23 bytes per packet on average (55.89 to 56.12), +17 ms per packet at SF12 and +0.5 ms at SF7. 88.3% of packets are unchanged.
* Plain content meets the 1 in 65,536 target and does not beat it: 271 of 20,000,000 plain frames with 1 to 8 random bytes appended were accepted (1 in 73,801). LACF content also has the re-encode check. CRC-16 kept (a CRC-24 on plain would cost one more byte per plain frame).
* Test builds before this version sent `V` as `cc vvvvvv` (`01`, `02`, `C1`, `C2`). Those values read as protocol versions 0 and 24 and are dropped.
* The LACF payload, the tables, the destination list and the RXT block are unchanged.
* On the air, 2 November 2026 (LoRa32 iGate, T-Deck Plus tracker, hill Pi with Dire Wolf; 433 MHz, SF12): every exchange decoded with `V = 0x10` and a valid CRC, in both directions, with no frame dropped: T-Deck to LoRa32 (plain form, kind 1), LoRa32 to T-Deck and back through a relay with an RXT tuple (LACF form, kind 3), the hill to both ESP32 boards (LACF, kind 3, 39 B instead of 57 B) and a plain text frame re-encoded by the LoRa32 digipeater (kind 1). One site, a few metres apart, no range or error-rate measurement; truncated and modified frames were not sent over RF.
* Documentation: the frame diagram and the docs describe plain and LACF content, the four kinds, `V = 10` (protocol 2, table 0) and "table version".
* Tests: C and Python references agree (0 disagreements on 3,000 packets and 143,224 damaged frames); every cut of every plain frame is rejected; F4MLV's cases A, B and C replay as expected (software replay, not RF).

## 1.0.0 - 1 November 2026 (wire format version 1, test builds only)

First byte plan: native frame `3C FF 02`, PPM-coded content with CRC-16, plain fallback without a check, RXT block at the end.
