# Changelog

LACF (spec 11) uses semantic versions: MAJOR = wire format version (the low six bits of `V`), MINOR = backward-compatible addition, PATCH = wording only. See "Versions" in `spec/11-native-ppm-frame-byte-plan.md`. The current number is in the file `VERSION`.

## Unreleased - 2.1.0-dev.1, 4 November 2026 (a proposal in the documents only; nothing implemented, not adopted, every change needs F4MLV's agreement)

Documentation only: spec 11, the byte reference, spec 02 and the new `docs/lacf-model-upgrade.md` describe the proposal. `VERSION`, the reference code, the test vectors, the worked example and the pictures are still 2.0.0 and change when the proposal is implemented.

1. **`V = c ss mmmmm`** instead of `vvvvv ttt`: a capability flag, two spare bits and a 5-bit model version (32 models). The protocol version leaves `V`: a change to the front of the frame is a MAJOR change and gets a new frame type byte. Plain frames ignore the model bits.
2. **Destination byte `c iiiiiii`:** 128 fixed, append-only codes (0 to 125 listed, 126 reserved, 127 the escape), no longer ranked and no longer versioned by `V`; one reserved bit.
3. **RXT:** 93 (`~`) means "not measured"; 90 to 127 other than 93 are unknown and the frame is kept; 0x80 or more still drops it.
4. **Relay without the model:** a relay that does not hold a frame's model forwards it byte for byte, adds its own tuple and does not show or use it (measured in the Python reference: 1,792 of 1,792, byte for byte identical to a relay that holds the model).
5. **Capability bytes:** three bytes after `V` when `c` is set: role (3 bits, with digipeater and iGate "late" tiers), model decoded (5), SF (3), BW (2), CR (2), power (4), preset (5; 433.775, 439.9125 UK, 434.855 Poland). Sent about every 8th beacon.
6. **Decided not to:** use destination code 126 for a packed `APL` plus three characters (only about 0.5% of real LoRa APRS packets would use it); put a capability frame `3C FF 03` on the air.
7. Still open: placeholder names in the destination list, the frequency-offset range, how a sender chooses a model.

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
