# Proposal draft 02 - Capability announcement

> **Earlier format.** This draft describes the earlier whole-text frames (`3C FF 81`, `3C FF C1`), which are superseded for LoRa by LACF, [spec 11](11-native-ppm-frame-byte-plan.md), a draft in early development and still being reviewed for change.

> **For LACF 3.0.0-dev.1, the capability word is proposed to ride in the frame itself (a proposal, not adopted, needs F4MLV's agreement).** In [spec 12](12-lacf-3.0-dev-byte-plan.md) it is a block of four bytes after the `V` byte (frame type `3C FF 03`), flagged by bit 7 of `V`: this draft's word with the model field widened to 5 bits, the power in whole dBm (6 bits), the role widened to 3 bits (with digipeater and iGate "late" tiers) and the explicit-frequency presets (30 and 31) dropped so that the length is fixed. What still applies here: the VHF/AFSK column and the preset idea, and the text and APRS-IS forms below. A separate capability frame is no longer the plan for LoRa.

| | |
|---|---|
| Status | **Proposal draft. Not a standard. Not fully implemented or tested.** |
| Depends on | 01 Compressed frames |
| Credit | Fabrice Morel, F4MLV (moricef) and K6ATV are working together on the current format, LACF ([spec 11](11-native-ppm-frame-byte-plan.md)), a hybrid of his native LoRa APRS frame and code and this PPM compression. |
| Changes to existing APRS | None for the baseline method; the optional explicit marker and binary form are new frame/token types |

## Summary

A sender should use a compressed frame only when it has reason to believe the stations it wants to
reach can decode it, and should otherwise send plain APRS. This part says how a station learns that.

## Motivation

Compressed frames (01) are not valid APRS to a station that does not implement them. Without a way
to learn who can decode them, an upgraded station must either always send plain (no gain) or send
compressed and risk being unheard. Legacy stations, including their two-way messaging, must keep
working, so the default is plain.

## Baseline: listen-and-switch (no new syntax)

A station that can receive switches to compressed frames after it hears a compressed frame from an
iGate or digipeater (one that transmits the format almost certainly decodes it), and reverts to
plain after a period without hearing one. Messages to a station not known to be capable are sent
plain with the ordinary ack. This needs no change to any packet, so it is legacy-safe by construction.

Caveats:

* Hidden nodes: hearing an iGate does not prove it hears you.
* Several iGates, only some of which decode.
* Trackers that cannot afford to receive cannot use it.

Mitigation: send a plain frame every Nth transmission, and fall back to plain if no ack arrives for
a message.

## Optional: explicit marker

A field in an existing beacon or status packet saying which model versions and roles the station
supports. It needs a place in the text that existing software ignores or displays harmlessly, and
that is not yet found; `<` is already an APRS data-type identifier for station capabilities, so
reusing it needs care. Not proposed until a placement is tested against existing software.

A compact binary form is specified below.

## Binary form (proposal)

One 16-bit word, sent most significant byte first. It carries the station's role and compression
support as well as its radio settings, so one announcement answers both "can you decode me"
and "where do I find you".

    bits    LoRa                              VHF/UHF (AFSK/FSK)
    15-14   role                              role
    13-12   compression                       compression
    11-9    SF: 0-7 = SF5..SF12               reserved, send 0
    8-7     BW: 62.5 / 125 / 250 / 500 kHz    baud: 300 / 1200 / 9600 / reserved
    6-5     CR: 4/5 / 4/6 / 4/7 / 4/8         framing: AX.25 / FX.25 / IL2P / reserved
    4-0     preset                            preset

**Role** (bits 15-14): 0 = transmit only (a tracker that does not receive), 1 = listens (a tracker or
client that receives), 2 = digipeater, 3 = iGate.

**Compression** (bits 13-12): the highest model version from 01 that the station decodes. 0 = none,
1-3 = model v1-v3. Whether a station also *sends* compressed frames is not carried; a receiver
sees that by hearing one.

**Preset** (bits 4-0) names the channel, and the preset decides which column above applies:

    0       the channel this announcement was heard on (family = how it was received)
    1       144.390 MHz, AFSK. Fixed; never reassigned.
    2-29    table, to be defined (see open questions)
    30      explicit frequency follows, LoRa
    31      explicit frequency follows, AFSK/FSK

**Explicit frequency** (presets 30 and 31 only): 3 bytes, unsigned, most significant first, in
units of 500 Hz. That reaches 8.39 GHz and represents half-kHz channels such as 439.9125 MHz.

Two escapes rather than one, so a receiver always knows whether to read bits 11-5 as SF/BW/CR or as
baud/framing, even when the frequency is not in the table. On VHF the coding-rate bits carry the
framing instead, because a receiver cannot decode FX.25 or IL2P without knowing to expect it.

Preset 0 is only meaningful over RF. An iGate gating an announcement to APRS-IS, or a station whose
announcement is likely to be read there, should use a numbered preset or an explicit frequency.

SF5 and SF6 are included because SX126x-class radios (for example the STM32WLE5) support them, even
though SX127x radios use SF6 only in implicit-header mode and current LoRa APRS uses SF7-SF12.

### Examples

    1200-baud AX.25 iGate on 144.390                  C0 81              (2 bytes)
    listening tracker, decodes v1, SF7/125/4:5,
        same channel as heard                         54 80              (2 bytes)
    iGate, decodes v1, SF12/125/4:5 on 433.775 MHz    DE 9E 0D 3C DE     (5 bytes)
    9600-baud AX.25 iGate on 445.925 MHz              C1 1F 0D 9B CA     (5 bytes)

### Where it is carried

* **LoRa, binary.** A frame type of its own: `3C FF` + a type byte + the word (+ frequency). The type
  byte cannot be 0x01 (plain LoRa APRS) or 0x80-0xFF (compressed frames, 01). 0x02 is the obvious
  candidate. Not yet checked against what other LoRa APRS firmware does with third bytes other than
  0x01. Legacy receivers that check for exactly `3C FF 01` drop it, which is fine: they could not
  use it anyway.
* **APRS text** (VHF, APRS-IS, and plain LoRa frames). Raw bytes do not survive APRS-IS, so the word
  is written as printable characters, for example 4 hex digits in a station-capabilities (`<`)
  packet: `<IGATE,RF=DE9E0D3CDE`. Token name, and hex versus base-91 (3 characters for the word), are
  undecided.

## Compatibility with legacy stations

Nothing changes for a station that implements neither method. An upgraded iGate still gateways
compressed frames to APRS-IS as plain text, and sends plain to legacy stations.

## Security and regulatory considerations

None specific beyond those in 01. A capability marker, if added, is public information about the
station.

## Open issues

1. Whether listen-and-switch, the explicit marker, or both. Marker syntax and placement, if any.
2. Whether a digipeater may advertise on behalf of the iGate it feeds.
3. How long to remember a capable station.
4. The preset table (2-29): which channels, who maintains it, how it is versioned. A preset can also
   name an RX/TX pair for gateways that listen and answer on different channels.
5. RX and TX that differ and are not in the table: send the word twice (TX first, then RX)?
6. Transmit power is not in the word; an optional extra byte is one option.
7. Two bits of model version run out at v3. Either models change rarely, or 3 means "v3 or later,
   see my status".
7a. Several tables at once. Devices differ in flash: a Raspberry Pi or ESP32 can hold a large table
   (and several), a small microcontroller only a small one, and the table is chosen per link. A frame
   must use a table both ends hold, so a sender picks one the receiver announced. That means the
   announcement should be a list or set of tables held, not one "highest version". Compression is
   then set by the weaker end of a link (measured spread by table size: roughly 52% for the
   biggest to 42% for the smallest, from `docs/rationale.md`). Separate per-country tables looked
   unnecessary in the pilot data (`docs/per-country-estimate.md`).
8. Where the field lives so existing software ignores it: this part proposes a LoRa frame type and
   a `<` text token, neither tested against existing firmware.
9. The reasons to advertise radio settings at all are discussed in the phase 2 repo
   (`aprs-smarts`, `drafts/radio-parameters.md`).
10. Reach is a better signal than "I heard it"; see `drafts/reach-aware-repeating.md` in phase 2.
