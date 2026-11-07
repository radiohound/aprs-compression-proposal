# LACF 3.0.0-dev.1 (proposal, frame type `3C FF 03`): a model version, capability bytes and a CRC over the bytes as sent (spec 12)

> **PROPOSAL, NOT ADOPTED. Needs F4MLV's agreement.** This is the proposed next format of LACF. It is a **separate frame type, `3C FF 03`**, and it does not replace the tested 2.0.0 format (`3C FF 03`, [spec 11](11-native-ppm-frame-byte-plan.md)), which stays as it is, with its own codec and vectors. A device can hold both decoders: the third byte of the frame, the type, says which one reads it, and both use the same PPM tables. Firmware that does not know a type drops the frame, so 2.0.0 stations never read these frames. The C reference ([`tools/hb3`](../tools/hb3/)), the Python reference (`tools/hb3/hb3_py`) and the test vectors (`test/vectors_hb3_*.txt`) implement this text. No firmware does, and nothing has been run on the air. Details will change; do not build deployed software from this text.
>
> *Last changed 5 November 2026. Version 3.0.0-dev.1 (dev tags while the format changes). What differs from 2.0.0 is marked *3.0.0-dev.1*.*
>
> **What differs from 2.0.0:** a model version in `V` instead of the protocol and table versions; a capability flag in `V` and four capability bytes; a destination byte with 128 fixed, append-only codes; a **CRC over the bytes as sent**, so that a relay that does not hold a frame's model can check it; RXT values that are "not measured" or unknown no longer drop a frame; and a relay without the model still forwards a frame.

| | |
|---|---|
| Version | **3.0.0-dev.1** (frame type `03`). The tested format is 2.0.0 (frame type `02`, spec 11). Check the number against `CHANGELOG.md` |
| Name | **LACF**, the **LoRa APRS Compact Frame**: the native frame `3C FF 03` with a PPM-coded content field (called hybrid B in `tools/hybrid/`) |
| Status | **A proposal written by K6ATV after F4MLV's comments on 2.0.0 (issue 7 of moricef/LoRa_APRS_Tracker), not adopted. Implemented in the C reference (`tools/hb3`) and the Python reference; no firmware implements it.** |
| Credit | **Co-developed with Fabrice Morel, F4MLV (moricef).** LACF is a hybrid of his native LoRa APRS frame and code and K6ATV's PPM compression, and the two are working on it together (issue 7 of moricef/LoRa_APRS_Tracker). RXT was created by Jon, N7UV (see `tools/hybrid/README.md`, Credit) |
| Builds on | the native frame and code of Fabrice Morel, F4MLV (moricef) (see History at the end); hybrid B of `tools/hybrid/` and its tables (`tools/hybrid/tables/`); the PPM payload rules below (ending, CRC-16, re-encode check) |
| Replaces | nothing: the 2.0.0 frame (type `02`) is unchanged and stays the tested baseline. This is a separate frame type |

## What this is

This is **LACF** (hybrid B of `tools/hybrid/`): the native part (source, destination, path, RXT) is structured so a relay can edit it; the PPM part is only the content, which a relay copies unchanged.
In the terms of the thread (issue 7, comments 5 to 17): hybrid B + destination table v2 + lossless position block, with the comment-only table when the position is in the native block. Relays update the path and append RXT without decompressing.

Agreed with F4MLV as the scope of the first experimental common format (comment 16, confirmed in 17): native source, destination and path; the versioned destination table; lossless positions; standalone PPM for the remaining content; extended callsigns and SSIDs, content and the link between each measurement and its relay preserved exactly; receivers drop any frame whose format, model or table version they do not know (append-only tables).
**Not in the first format:** primed mode (and so the 1-byte station id), rounded or packed positions (`PACKPOS`, an optional tracker setting later), WSPR-style callsigns, the power/backhaul questions (the capability bytes are proposed in 3.0.0-dev.1), Mic-E as a native position (Mic-E travels as text).

## Frame

    3C FF 03 | header | source | destination | path... | [position block] | content field | [RXT block]

A picture of the whole frame, with a worked example, is at the end of this section.

`3C FF 03`, the header byte, the address elements, the path and the position block are defined exactly in Conformance. The frame has two forms, **LACF** (PPM-coded content) and **plain** (the text itself), each with or without a native position block (the four kinds in the header), a destination table, a rule for when to use plain, and the RXT block at the end.

### Header byte

| Bits | Meaning |
|---|---|
| 7-4 | number of path elements, 0 to 15 |
| 3 | an RXT block ends the frame (it follows the content field) |
| 2 | reserved, must be 0 (a receiver drops a frame that sets it; kept free for a future change) |
| 1 | the form: **1 = LACF** (PPM-coded content), **0 = plain** (the text itself, with a CRC) |
| 0 | **1 = a native position block follows the path** (the content is then only the comment that remains), 0 = no position block (the content is the whole information field) |

The kind is therefore two independent bits: **form** (bit 1: plain or LACF) and **position block** (bit 0). Kind 0 is plain without a position block, 1 plain with one, 2 LACF without one, 3 LACF with one. F4MLV proposed the kind values for exactly this (comment 6: which coder was used). A receiver that does not know a frame MUST drop it and never show it as text.

### Content field (every kind)

The **content field** is everything after the position block (kinds 1 and 3) or the path (kinds 0 and 2) up to the RXT block. Its words: the **text** is the information field (kinds 0 and 2) or the comment (kinds 1 and 3) as characters; the **payload** is the PPM-coded bytes of that text; the **body** is the payload (LACF content) or the text (plain content). The first byte of the content field is `V`, present in **every** frame:

    V = c ss mmmmm                                                              (3.0.0-dev.1, proposed; in 2.0.0: V = vvvvv ttt)
    c       1 bit    1 = four capability bytes follow V (see "Capability bytes"), 0 = none
    ss      2 bits   spare: a sender sets 0, a receiver ignores them, a relay copies them
    mmmmm   5 bits   model version (this document: 0). It names the pair of LACF tables (info-native and comment-only) that coded the payload (32 versions). A receiver that does not hold the model cannot decode LACF content and drops the frame; a relay may still forward it ("Relay without the model"). Plain content uses no model: a sender sets mmmmm to 0, and a receiver accepts a plain frame whatever the model bits say

The protocol version is no longer a field of `V`: a change to the front of the frame is a MAJOR change and gets a new frame type byte (see "Versions"). The destination list is no longer versioned by `V` (see "Destination").

Whether the content is LACF or plain is **not** in `V`: it is bit 1 of the kind in the header.

LACF content (kinds 2 and 3):

    V        1 byte
    [cap]    3 bytes   only when V bit c is set ("Capability bytes")
    L        1 byte    number of text bytes coded, 1..255
    CRC      2 bytes   CRC-16/X-25 (below), low byte first
    payload  n bytes   range coder bytes of spec 01, ending as in "PPM payload rules" below

Plain content (kinds 0 and 1):

    V        1 byte
    [cap]    3 bytes   only when V bit c is set ("Capability bytes")
    CRC      2 bytes   CRC-16/X-25 (below), low byte first, over the same input as for PPM content
    text     the text itself, up to the RXT block or the end of the frame (no `L`: its length is what remains)

It is used when LACF would not shrink the text. **Protocol version 2 added the CRC to plain content** (F4MLV's RF test of 2 November 2026 showed that a plain frame cut before transmission was accepted and relayed as different text, because nothing but the LoRa CRC, which is computed after the cut, protected it).

* Kinds 0 and 2 carry the whole information field. Kinds 1 and 3 follow a position block and carry only the comment that remains.
* **What the CRC covers** (LACF and plain content; *3.0.0-dev.1: the bytes as sent, not the decoded text. In 2.0.0 it was over the decoded text, which a relay without the model cannot produce*). The CRC input is, in this order: the byte `header & 0x07` (the kind bits and the reserved bit, so the form and the position-block bit are covered); the byte `V`; the four capability bytes when `c` is set; the source and destination bytes exactly as sent (everything between the header and the path: the source element, the destination code byte, and the escape element when there is one); the position block (kinds 1 and 3); the byte `L` (LACF content only); and the payload (LACF content) or the text (plain content).
  It does not cover the path, the rest of the header (the path count and the RXT bit), the RXT block or the CRC field itself. A relay changes the path and the RXT block only, and copies everything else byte for byte, so it never recomputes the CRC. **Any station can check it without the model and without the destination list**, because it needs no decoding. A damaged source, destination, position block, `V`, capability byte, `L` or payload fails it.
* The payload length is not sent; the receiver derives it (Receiver, step 3).

**Models.** LACF kind 2 uses the *info-native* table and kind 3 the *comment-only* table, because a comment after a position looks different from a whole information field. The low five bits of `V` (the model version) name the pair; the destination list is no longer part of it (3.0.0-dev.1; in 2.0.0 the three bits of the table version named both). Model versions are allocated by this spec, append-only, starting at 0 (32 values). Firmware keeps every released model:

| `mmmmm` | Info-native table | Comment-only table | Status |
|---|---|---|---|
| 0 | `tools/hybrid/tables/model_hybridB_info_native.c` (sha256 `440a1ec2...dc270`, 77 KB) | `tools/hybrid/tables/model_hybridB_comment.c` (sha256 `d1262b44...eaf5`, 75 KB) | experimental: trained on 70% of the stations of one APRS-IS capture (callsign-hash buckets 3 to 9), 174,170 and 105,092 packets |

The tables' own header says "model version 2", which they share with the whole-packet and hybrid A tables of the same build; that number does not identify a layout, which is why the model version in `V` is allocated here. A deployed first format needs a frozen set trained on all stations and on several networks' data
(`train_layout_model.py --base-counts`, see the harness README), with a new `mmmmm`. The experimental set above must not be treated as the final one.

### Fallback to plain text

The encoder uses LACF content only when it is shorter than plain content: `4 + n < 3 + L` bytes (LACF: `V`, `L`, CRC and payload; plain: `V`, CRC and the text). Otherwise it sends plain content (the kind with the form bit clear: 0 for 2, 1 for 3), so the frame still uses the destination list and the RXT rules. The capability bytes, when `c` is set, are in both forms and are not part of this comparison.
**Decided 2 November 2026: plain is a form in the header** (kinds 0 and 1), not a coding inside `V`. The draft-0 frames that used kinds 0 and 1 had no `V` and no CRC, so a receiver of this plan drops them (the CRC cannot match). On our packets 15% of information fields do not shrink with the info-native table and 6% of comments with the comment-only table (about a third on the harness's data).
After a position block, an empty comment sends no text (kind 1, plain content with nothing after `V` and the CRC: 3 bytes). These rules are the harness's `PLAINTEXT=1`. Senders must follow the size rule, and a receiver drops PPM content the encoder would not have sent (Receiver, step 5), the same way it drops a payload that is not the canonical encoding; any receiver decodes every coding it holds.

### Destination (fixed, append-only list)

*3.0.0-dev.1, proposed, needs F4MLV's agreement. In 2.0.0 the byte was `ff iiiiii` with 62 ranked codes and a list versioned by `V`.*

The destination is coded in one byte, `c iiiiiii`:

* `c`, the top bit, is **reserved**. A sender sets it to 0, a receiver ignores it, and a relay copies it unchanged. It is the one ignorable flag kept in the frame for something not yet foreseen; whatever it comes to mean describes the originating station, not a relay. It is not used for capability: the capability bytes carry that.
* `iiiiiii`, the low seven bits, give 128 codes. **Codes 0 to 125 name destinations of the destination list. Code 126 is reserved** (a receiver drops a frame that uses it; decided: it is not used for a packed "`APL` plus three characters" form, because only about 0.5% of real LoRa APRS packets use an unlisted `APL` destination, which would save about 0.015 bytes per frame). **Code 127 is the escape:** after it comes an address element (5 bytes for an ordinary destination, so 6 in all; this is also the cost of a Mic-E destination, whose characters are the position, carried as a 5-byte element).
* **The codes are fixed and append-only.** Once a code is assigned it never changes meaning, and the list is no longer ranked: every direct code costs one byte wherever it sits, so ranking never saved air time. The list is not versioned in `V`. A receiver drops a frame whose code is not assigned in its copy of the list (it can tell locally); a relay forwards such a frame ("Relay without the model"). A sender that knows old receivers are around uses the escape for a destination added after the first release.
* A destination that has no direct code is not in the list: it escapes. There are no patterns and no second tier of 2-byte codes. In both measured captures registry patterns almost never fired (0 packets of 8,062 in Ariège and 2 of 14,305 in ours).

**The first list** (codes 0 to 61 of the first release; 2.0.0 called it table version 0) is the file [`tools/hybrid/tables/tocall_set1.txt`](../tools/hybrid/tables/tocall_set1.txt) (sha256 `0e076bfbe365edc040cc294b7b271495d6f2e3d76292be9cd3dae5c4e6bf7a35`): 62 destinations ranked on our packets plus the destinations reported from the French capture; `APU25N`, `BEACON` and other rare ones are not in it. It is experimental, like the tables. Codes 62 to 125 are unassigned until the first release.

Measured on our 14,305 real packets (73 distinct destinations): the 62 most common as direct codes cover 99.9% of packets (the first five, `APLRG1`, `APLRT1`, `APLWS2`, `APLG01`, `APLRFD`, cover 88.8%), a mean of 1.0038 bytes per packet, against 1.015 for the v2 list of the harness (85 exact entries, 28 patterns). That ranking is made on the same data it is tested on, so it is optimistic; a real list is ranked on training stations and checked on held-out ones.
The harness's v2 list ([`tools/hybrid/tables/tocall_table_v2.txt`](../tools/hybrid/tables/tocall_table_v2.txt), sha256 `e700581b...f59aff`, 85 exact destinations and 28 patterns) is the starting data, not the final list: its entries seen once or twice in the captures (`APU25N`, `BEACON`) would not earn a direct code, and the few `APL...` destinations seen here that it lacks (`APSTKI` 20 packets, `APLM2D` 6, `APLGMT` 5, `APLHT2` 4) would compete for the 62 codes.
Exact-entry coverage of the v2 list on the French capture (Ariège, 8,062 packets) is 85.1% with v1 and 97.3% with v2; on the Arizona sample (2,659 packets) 99.8% and 100%; Mic-E is 2.5% in Ariège and always escapes.

**The list is not part of `V`** (3.0.0-dev.1). A code names the same destination in every frame, whatever the model, so a receiver needs only its own copy of the list to read it. *Open, proposed: fill about half of the codes 62 to 125 with placeholder names for future LoRa APRS devices, as a published menu ("firmware that requests one of these tocalls gets one-byte compression on every LACF station"). The names must not be in the registry [aprsorg/aprs-deviceid](https://github.com/aprsorg/aprs-deviceid) and must never be transmitted by us unregistered; the first frozen list would hold them all.*

### RXT block (header bit 3 set)

    tuple 1 | tuple 2 | ... | tuple k | mask          the last byte of the frame is the mask

* A tuple is 4 bytes: RSSI, SNR, frequency offset, time to hop, each the RXT character minus 33, so each byte is 0 to 89 for a measurement (his encoder clamps every field to 90 values; SNR has 89 steps, 0 to 88). Values above 89 are described under "Not measured and unknown values" below.
* The mask has **bit 7 set** as a marker. Bit j (0 to 6) set means that path element j (0 is the first element after the destination) carries a measurement. Tuples are in ascending order of j. `k` is the number of mask bits 0 to 6 that are set.
* The block is `4k + 1` bytes. Bits at or above the path element count are not allowed. **A tuple byte of 0x80 or more is impossible** (it would look like a mask marker), and a receiver drops a frame that has one; a byte from 90 to 127 is *unknown* for its field and the frame is kept (3.0.0-dev.1, below). *In 2.0.0 a receiver dropped a frame with any tuple byte above its field's maximum (RSSI 89, SNR 88, frequency offset 89, time to hop 89), a check that random damage fails about 98% of the time; the proposal gives it up because RXT is advisory, and the LoRa CRC and the mask marker already protect it.*

The tuples have no check of their own beyond the LoRa CRC, which already covers the whole frame at the level of VHF APRS (about 1 in 65,536); a block CRC would be recomputed by every relay and so add nothing the LoRa CRC does not give per hop.
The marker makes the block safe against truncation: a tuple byte is never 0x80 or more, so after a cut inside the block the last byte can never be mistaken for a mask (tested below). It costs one path element: at most 7 elements can carry a tuple (mask bits 0 to 6), not 8.
This placement (end of frame, mask byte last) is K6ATV's proposal in issue 7 (comment 5): a relay appends without moving the content, whatever the content is, and the content (PPM payload or text) is never touched by RXT, which is already compact binary and would not shrink. The same placement applies to both kinds, so a relay needs one rule.

A relay builds its outgoing frame from the front: new header and path, then the position block and content (the content field) copied byte for byte, then the old tuples, its own tuple and the new mask. If there was no block it creates one (tuple, mask) and sets header bit 3.
A relayed frame grows by **5 bytes for the first relay** (4 for the tuple, 1 for the mask) and 4 for each further one.

**Not measured and unknown values (3.0.0-dev.1, proposed, needs F4MLV's agreement).**

* 0 to 89 are measurements, as above. **93 (`~`) means "not measured"** and is valid in any of the four fields. 0 cannot be used for that: it is a real reading in every field (-130 dBm, -24 dB, about -2,500 Hz, the shortest hop). 90 to 92 are reserved. A receiver treats every value from 90 to 127 other than 93 as *unknown* for that field and **keeps the frame**. In F4MLV's text trailer `{...}` each value is one character, value + 33, and his parser accepts only `!` to `z` (values 0 to 89); `{` and `}` are the trailer's own delimiters, so values 90 (`{`) and 92 (`}`) cannot be written in the text form at all, and 94 to 127 are binary only. The characters that can carry a value outside his range are `|` (91) and `~` (93); `~` is the one proposed for "not measured". Both are reserved for TNC use in the APRS 1.01 comment field, which does not matter on a LoRa text frame (the trailer is removed before a packet goes to APRS-IS), but F4MLV's parser must be extended to accept it. A byte of 0x80 or more is still impossible.
* A relay that measured nothing adds no tuple (no mask bit, and no block if it would be the first). A relay that measured some fields writes 93 in the others: a relay behind a serial KISS TNC cannot measure SNR or frequency offset, which is why this exists.
* **Frequency offset range (open).** For now a reading beyond the end of the scale is clamped to the end value (as the iGate and the tracker do). Values 90 to 92 stay reserved so that two values for "beyond the range" can be assigned later without breaking old receivers. See [`docs/rxt-frequency-offset-scale.md`](../docs/rxt-frequency-offset-scale.md).

### Capability bytes (V bit c set)

*3.0.0-dev.1, proposed, needs F4MLV's agreement.* When bit `c` of `V` is 1, exactly **four bytes** follow `V` (before `L` or the CRC). They say what the sending station is and what it can decode, so that its neighbours can keep a table of them. They are one 32-bit number, most significant byte first: the word of spec 02 with the model widened to 5 bits, the power given in whole dBm (6 bits), and the role widened to 3 bits.

| Bits (31 first) | Field | Size | Values |
|---|---|---|---|
| 31-29 | role | 3 | see below |
| 28-24 | model decoded | 5 | 0 = the station decodes plain content only; otherwise the newest model it decodes **plus one** (model 0, the first set, is sent as 1), so that 0 is not mistaken for the first model |
| 23-21 | SF | 3 | 0 to 7 = SF5 to SF12 |
| 20-19 | BW | 2 | 62.5 / 125 / 250 / 500 kHz |
| 18-17 | CR | 2 | 4/5, 4/6, 4/7, 4/8 |
| 16-11 | power | 6 | **the radio's configured output in whole dBm**: 0 = unknown, 1 to 63 = that many dBm (so 10, 12, 20, 22, 30, 33, 37 and 40 dBm are exact, 40 dBm being 10 W). The output of the module, not the power radiated: no antenna gain or cable loss |
| 10-6 | preset | 5 | 0 the channel the frame was heard on; 1 = 144.390 MHz AFSK; 2 = 433.775 MHz; 3 = 439.9125 MHz (UK); 4 = 434.855 MHz (Poland); 5 to 29 reserved; 30 and 31 reserved (no explicit frequency: the length is fixed) |
| 5-0 | spare | 6 | a sender sets 0, a receiver ignores them |

Six bits are spare, so a later addition may fit without a new frame type. A preset names a frequency only; the SF, BW and CR fields give the rest, and a preset is not a "default for the country" (a receiver cannot know the country, and a frame gated to APRS-IS is read far from where it was sent). The UK and Poland presets come from [lora-aprs.org/settings](https://lora-aprs.org/settings/) and are to be checked with local groups. *(The first draft of this block had a 4-bit power field with odd values only, 5 to 33 dBm, which could not give 20 or 22 dBm, the most common settings; F4MLV pointed that out on 5 November 2026.)*

| Role | Meaning |
|---|---|
| 0 | transmit only (decodes nothing; a station that sends LACF but decodes nothing must say so, so that it is not counted as a decoder) |
| 1 | listens (a tracker or client that receives and does not relay) |
| 2 | digipeater |
| 3 | digipeater late |
| 4 | iGate, no RF relay (receive-only) |
| 5 | iGate and digipeater |
| 6 | iGate and digipeater late |
| 7 | reserved |

"Late" is a tier, as in Meshtastic (`ROUTER` and `ROUTER_LATE`): a high site relays first and a late one waits and relays only if the packet has not already been repeated. The frame only says what the station is; how long a late station waits is behaviour, see the relay-priority draft in `aprs-smarts`.

* The bytes describe the **originating station**. A relay copies them unchanged, and they are part of the content CRC input (after `V`).
* They are **advisory**: a receiver ignores a value it does not know (role 7, a reserved preset) and never drops a frame for it. A frame with `c` set and fewer than four bytes after `V` is dropped.
* **Schedule (SHOULD):** every 8th transmission, at most once an hour and at least once every 6 hours, plus at start-up and whenever the station's settings change. Beacon rates differ (one minute for a moving tracker, 30 minutes or more for a fixed iGate), hence a count with time limits. The cost is 4 bytes on that frame: about 0.13 to 0.2 s at SF12 and BW125, about 1.3% of air time at every 8th. (A separate capability frame would cost 8% at every 8th and 16% at every 4th, which is why the bytes are carried in `V`.)
* **Neighbour table (SHOULD):** a receiver keeps, per callsign, the role, model, radio settings, power and when it was last heard, **and whether the bytes arrived direct** (a path with no used element) or through a relay: a block heard through a relay describes a station that may not hear you, and links are not symmetric. An entry expires after about 24 hours, so a reflashed or removed station does not hold others back. Until a neighbour's bytes have been heard, its capabilities are unknown, and unknown means the older model or plain.
* A **receive-only iGate never transmits**, so it never announces itself: it stays unknown to its neighbours.

### The frame as a picture

![The fields of a LACF frame, the header and V bytes, the RXT block, and a worked example: 35 bytes, and 44 bytes after one relay hop](../docs/img/lacf-3.0-frame-layout.svg)

The picture shows the fields in order, the header byte and `V` bit by bit, the RXT tuple, and one real packet as a LACF frame (35 bytes, hex) and after one relay hop (44 bytes). The example's 57% saving is for that one packet; real packets average about 44% (see "Size"). The same content as tables, with the example byte by byte, is in [`docs/lacf-3.0-frame-layout.md`](../docs/lacf-3.0-frame-layout.md), and every byte and bit is defined in [`docs/lacf-3.0-byte-reference.md`](../docs/lacf-3.0-byte-reference.md); the bytes come from the reference code, and `python3 tools/lacf3_diagram.py` redraws the picture.

## PPM payload rules (normative)

These apply to the `payload` of the content field. The model, the range coder and every probability are those of spec 01 with the table the kind and `V` select.

**Why.** A payload cut short before it is sent carries a valid LoRa CRC, because the sender computes the CRC after the cut, and a decoder that pads missing input with zeros turns the cut payload into plausible text.
Measured on 14,298 real full frames, the unmodified decoder accepted every frame cut by 1 to 6 bytes, with the right length and a wrong tail. The rules below make the end of the payload unambiguous and make the receiver prove that the payload is the one an encoder would have sent.

**Ending.** After the last text byte is encoded, the encoder holds `low` (up to 33 bits, with a carry pending in bit 32) and `range`. Then:

1. If `low >= 2^32`: add 1 to the payload so far (propagating the carry through the bytes already written) and set `low = low - 2^32`.
2. Let `hi = low + range - 1`.
3. For `nb = 1, 2, 3, 4` in turn: let `blk = 2^(8*(4-nb))` and `v = ceil(low / blk) * blk` (the first multiple of `blk` that is `>= low`). If `v + blk - 1 <= hi`, stop.
   (If a decoder reads `nb` bytes and then anything at all, the 32-bit value it sees is some number from `v` to `v + blk - 1`; the condition says that every such number lies inside `[low, hi]`.
   `nb = 0` can never succeed, because `range < 2^32`. Because `range >= 2^24` after the last normalisation, `nb` is in fact always 1 or 2; 3 and 4 are never reached.)
4. If `v >= 2^32`: add 1 to the payload so far and set `v = v - 2^32`.
5. Append the top `nb` bytes of `v` (most significant first).
6. **Do not strip trailing zero bytes.** A zero last byte is sent.

The result is the fewest bytes after which **any** following bytes decode to the same text (in all but rare binary texts: 1 in 23,709 adversarial binary texts ended one byte longer than needed, because the decoder clamps to the last symbol; correctness is unaffected, since every receiver compares against the same deterministic encoder). Spec 01's ending (the fewest bytes assuming zeros follow, then stripping zeros) is replaced for these frames; the new ending is on average 0.25 bytes longer.
The encoder is deterministic: the same text always gives the same bytes, and a second implementation MUST produce them exactly, because the receiver check depends on it.

**CRC-16.** CRC-16/X-25, the AX.25 frame check: polynomial 0x1021 reflected (0x8408), initial value 0xFFFF, final xor 0xFFFF; the CRC of the ASCII string `123456789` is 0x906E. It is sent low byte first.

**Receiver equivalence.** The receiver decodes `L` text bytes, checks the CRC-16, re-encodes the text with the rules above and requires the result to equal the payload exactly (same length, same bytes). Decoding and re-encoding use the same probabilities for the same character,
so a receiver MAY re-encode while it decodes (one pass over the model instead of two); it must give the same accept and drop answers as the two-pass form. The CRC is checked before the comparison, so a damaged frame is dropped as early as possible.

**Measured (3.0.0-dev.1, 4 November 2026, 3,000 generated packets: 1,577 with LACF content and 1,423 with plain content, C reference).** With the CRC over the bytes as sent: every cut of 1 byte up to all but one, 140,131 cuts, 0 accepted; 1 to 8 random bytes appended, 24,000 frames, 0 accepted; every single-bit flip, 1,145,048 flips: 0 accepted with a different packet in the prefix and header (0 of 96,000), in the source and destination (0 of 190,248), in the position block (0 of 193,312), in `V`, `L` and the CRC (0 of 50,464) and in the payload or text (0 of 533,280). Flips in the path are accepted, as in 2.0.0, because the CRC does not cover it. These counts are the same as for the 2.0.0 CRC. The random-damage measurement of 2.0.0 (1, 2, 3 and 8 flipped bits in a payload, 2.85 million cases) has not been repeated; a CRC over the transmitted bytes has the same 1 in 65,536 expectation for damage to those bytes. The C and Python references agree on every vector file.
Details and the other results are in [`docs/truncated-payloads-and-integrity.md`](../docs/truncated-payloads-and-integrity.md).

## Receiver (normative)

1. Drop the frame unless it starts `3C FF 03` and header bit 2 is clear. Kind bit 1 gives the form (1 LACF, 0 plain), kind bit 0 the position block.
2. Parse the addresses, the path and the position block (kinds 1 and 3), with the layouts and the strict rules of Conformance. Then read `V`. The spare bits `ss` are ignored. If `c` is set, four capability bytes follow `V` (they are advisory and never cause a drop, but the frame is dropped if fewer than four bytes remain). *(3.0.0-dev.1, proposed. In 2.0.0 the receiver dropped a frame whose protocol version or table version it did not hold; a 2.0.0 station never reads a type `03` frame.)*
3. If header bit 3 is set: the last byte is the mask. Drop the frame unless bit 7 is set, `k >= 1`, every set bit is below the path element count, and at least `4k + 1` bytes remain after the start of the content field's body, which is the byte after `V` (and after the capability bytes when `c` is set) and the two CRC bytes for plain content (kinds 0 and 1), and the byte after `V` (and the capability bytes), `L` and the two CRC bytes for LACF content (kinds 2 and 3). The RXT block is the last `4k + 1` bytes. **The frame is dropped if a tuple byte is 0x80 or more; a byte from 90 to 127 is unknown for its field and the frame is kept** (RXT, above). The payload (LACF content) or the plain text is everything between the start of the body and the block. The block never overlaps `V`, the capability bytes, `L` or the CRC.
   If bit 3 is clear, there is no block and the payload or text is everything after.
4. **Check the CRC-16** over the bytes as sent (Content field, What the CRC covers) and drop the frame unless it matches. This needs no model and no destination list, so every receiver and every relay makes this check first. For plain content (kinds 0 and 1) the text is everything between the CRC and the RXT block: there is nothing more to decode, and the model bits are ignored. For LACF content (kinds 2 and 3), drop the frame unless the model (the low five bits of `V`) is held; then decode `L` text bytes from the payload with the range decoder (the table chosen by the kind) and rebuild the information field. The decoder may read past the end of the payload as zero bytes, because the range coder reads ahead; acceptance never depends on those bytes, since step 5 compares the re-encoded payload with the one received. Drop the frame if the destination code is not assigned in the receiver's list.
5. (LACF content) Re-encode the `L` text bytes with the sender's rules ("PPM payload rules", Ending). Drop the frame unless the result is **exactly** the payload (same length, same bytes), and drop it if the content field (`4 + n` bytes, not counting the capability bytes) is not shorter than `3 + L`, because the encoder would have sent plain content. This is what shows that the payload is a canonical encoding under the model; the CRC cannot show it.
6. Only an accepted frame may be shown, uploaded, relayed, or kept for later use. The reasons for dropping a frame are not part of the format (implementations name them as they like).

Steps 4 and 5 may be done in one pass over the model ("PPM payload rules", Receiver equivalence). Because the payload lies exactly between the CRC and the RXT block, there is no tail to guess about; a damaged or cut frame changes where the payload ends and step 5 fails.

## Relay (draft)

A relay accepts a frame by the Receiver rules (or, for a frame whose model it does not hold, by "Relay without the model", step 8), which for PPM kinds decode the content once to check it (one pass; 1.4 to 4.2 ms on the boards measured), and then forwards it with its own path update and its own RXT measurement **without re-encoding the content**: the outgoing frame is built from the bytes it received. A relay that skips the check forwards damage it cannot see. The content is never converted: a Mic-E packet is forwarded as it was received (see Mic-E below).

1. **Rewrite the front.** Decode the header, addresses and path of the native part to the TNC2 path, apply the usual digipeater rule (a packet whose path holds a non-compliant `WIDEn-N` is not repeated at all, see Conformance, Non-compliant WIDE paths; for example `WIDE1-1` becomes the relay's callsign with the used mark; `WIDEn-N` loses one hop; the relay vectors use only the three cases of Conformance, and a deployed relay may apply its full rule), and encode the header, addresses and path again. This is cheap and needs no PPM decoding. The source and the destination are copied **as received, byte for byte**: that keeps the reserved bit `c` of the destination byte, which describes the originating station, and a listed destination that was sent in the escape form stays in the escape form
   (a Mic-E destination is its 6 latitude characters, so it is the escape followed by the 5-byte element). *The relay works on the TNC2 path. His iGate relay today decodes the whole native frame to TNC2, moves the measurements to their callsigns, adds its own tuple and encodes the whole frame again; for the PPM kinds the content field is copied instead of decoded and re-encoded, which his code does not do yet.*
2. **Copy the rest unchanged**, byte for byte: the position block and the content (a content field with its CRC-16 and payload, or plain text).
3. **Find the index of the relay's own path element** after step 1, set that bit in the mask, and insert the relay's tuple in ascending order of index. Tuples already in the block belong to used path elements, which come before the relay's own element, so for the cases of the relay vectors no mask bit moves; in general they keep their meaning only if their path elements keep their index; a relay that changes the number or order of elements before them must move the mask bits with them (his iGate moves measurements by full callsign).
   **A relay does not forward a frame that carries a tuple on its own path element or on a later one.** Tuples belong to used elements, which come before the first unused one, so no conforming relay made such a frame; it is dropped, not repaired. A receiver that is not a relay accepts the frame, as the mask is in range.
4. **Fill the tuple at transmit time.** RSSI, SNR and frequency offset are those measured when the frame was received. Time to hop runs from the end of reception to the end of the relay's own transmission, including queue and backoff, so it is computed just before transmission starts from the queue time plus the time on air, which the frame length fixes.
5. **A relay that cannot finish the tuple in time sends none** (a frame without a block is valid) instead of sending a wrong one. The same holds when the relay's own element is the eighth or later, which has no mask bit: the old tuples are kept and no new one is added (no block at all if there were none). A frame that would be longer than 255 bytes with the relay's own tuple is forwarded without it (the earlier tuples stay, and the header bit and mask follow what remains); a frame that is too long even without it is not relayed.
6. **Drop duplicates** on the decoded APRS content (source, destination and information field), as his iGate does; the path and RXT differ between copies and are ignored.
7. **An iGate** decodes the frame to TNC2 for APRS-IS, MQTT and TNC output, removing the RXT block, and uploads the same source, destination and information field a text frame would have given.
8. **Relay without the model** (3.0.0-dev.1, proposed, needs F4MLV's agreement). A relay that holds the frame's format but not its model (or not its destination code) does not drop the frame. It parses and checks everything that needs no model (header, addresses, path, position block, `V`, the RXT mask and tuples) **and the CRC-16, which is over the bytes as sent**, and drops the frame on any failure there. It then forwards it exactly as steps 1 to 5: the front rewritten, the position block and the whole content field (`V`, the capability bytes, `L`, the CRC and the payload) copied byte for byte, its own tuple added. It reads `V` only for the bit `c`, to find where the body starts, and never reads the model. Duplicates are detected on the received bytes of the source, the destination and the content field, which are identical in every copy, instead of on decoded content.
   **What such a relay cannot check, stated plainly.** It cannot decode the payload, so it cannot see that the payload is a canonical encoding under the model, nor that the text is sensible: that protects against a sender that stamps the wrong model or encodes wrongly, and only a station that holds the model can check it. A frame like that would cross every such relay until a station with the model drops it, so it can travel several hops (the path limits how many: WIDEn-N allows one or two in normal use). It is never shown or gated as wrong data, because every station that decodes it makes the check; the CRC detects damage with the 1 in 65,536 strength of any CRC-16, which is error detection and not an absolute guarantee, and LoRa's own CRC checks every hop as well. **Damage on the channel or a frame cut before transmission is caught by the relay itself**, since the CRC is over the bytes as sent. The relay does not show, upload or use a frame whose model it does not hold.
   **Only firmware that has this before a new model ships benefits:** a relay without it drops the frame, and so is a wall for new-model frames. Dire Wolf cannot do it (its AX.25 core re-encodes every frame). *Measured in the Python reference on 5 November 2026, with the 3,000 test packets encoded under a stand-in second model: an unmodified relay forwards none of them, the pass-through relay forwards all 1,792 that a relay can forward, each byte for byte identical to what a relay that holds the model produces, a downstream receiver decodes all 1,792 correctly, and a frame damaged before the relay is refused by the relay itself, every time. This is a software test, not an on-air one.*

**What a digipeater sends, by what it received.** There are two cases, and the content form follows the same size rule in both.

* **A LACF-family frame (`3C FF 03`, any kind) received:** the relay copies it as above. Plain stays plain and LACF stays LACF; the content and its CRC are not recomputed.
* **A plain text frame (`3C FF 01`) received, and the digipeater's transmit format is LACF:** the digipeater encodes the packet it is about to repeat (with its own path update) as a new `3C FF 03` frame, exactly as an origin would: LACF content (kind 2 or 3) when `4 + n < 3 + L`, otherwise plain content (kind 0 or 1), with a fresh CRC-16. It is not copied, because there is nothing to copy. The iGate firmware sends the converted frame without an RXT block; whether measurements that a text frame carries in its text trailer are kept as tuples is left to the implementation (the tracker firmware can keep them).
* **Plain text is the fallback only:** a digipeater with its transmit format set to LACF sends plain text only for a packet that cannot be encoded (for example a source that no address element can hold). A packet whose path holds a non-compliant `WIDEn-N` is not repeated in any form (Conformance, Non-compliant WIDE paths). A digipeater whose transmit format is not LACF sends plain text, and still accepts and relays LACF frames only if it implements this section.

**The 255-byte limit.** A LoRa frame holds at most 255 bytes, and receivers have no room for more, so a relay never sends a longer frame and adds no padding or margin to be sure of it: the length is checked exactly. The relayed frame is the received one plus the growth of the path (a used element is a callsign, longer than the `WIDEn-N` byte it replaces) plus the relay's tuple (4 bytes, and 1 more for the mask when the frame had no RXT block). The relay decides in this order:

1. the frame with the path change and its own tuple is at most 255 bytes: it is sent with the tuple;
2. otherwise, the frame with the path change alone is at most 255 bytes: it is sent without the relay's own tuple (the earlier tuples stay, and the header bit and mask follow what remains);
3. otherwise it is not relayed.

For a frame with no RXT block, relayed at `WIDE1-1` (the path grows by 4 bytes, the first tuple adds 5), as the reference relay in C and in Python gives it:

| Received | Relayed | Relay's own tuple |
|---|---|---|
| 246 bytes or fewer | 255 bytes or fewer (received + 9) | added |
| 247 to 251 bytes | 251 to 255 bytes (received + 4) | left out |
| 252 bytes or more | none | not relayed |

The two edge vectors at the end of [`test/vectors_hb3_relay_edge_v1.txt`](../test/vectors_hb3_relay_edge_v1.txt) are the middle and last rows (a 249-byte frame relayed as 253 bytes without a tuple, and a 253-byte frame that is not relayed). The numbers move with the path: `WIDE2-2` grows it by one element more, and a frame that already carries tuples adds 4 bytes for the relay's tuple, not 5, so the limits are found by the three rules above and not by the table.

### Mic-E

Mic-E is not decoded or converted by this plan. A Mic-E packet is carried as received: the destination as the escape plus the 5-byte element (6 bytes, since its characters are the position), and the information field as plain content (kind 0), or as a LACF content field (kind 2) only if that is shorter, which is unusual because the field is printable binary. Relays forward it exactly as above. Converting Mic-E to a native position
would save only 1 to 3 bytes per Mic-E packet, almost all of it in the destination, and would change the bytes of the packet; it is not worth that and is not part of this plan.
If the sender used the destination SSID as a path code, the relay applies the usual rule to it.

## Conformance (draft)

*The vectors are those of this proposal, in `test/vectors_hb3_*.txt`, regenerated by `make hb3-vectors`, with two files that 2.0.0 does not have:* [`test/vectors_hb3_cap_v1.txt`](../test/vectors_hb3_cap_v1.txt) (`<frame hex> <TNC2 packet hex> <capability bytes hex>`: the encoder with the four capability bytes produces the frame, the receiver accepts it and reads the bytes back) and [`test/vectors_hb3_passthrough_v1.txt`](../test/vectors_hb3_passthrough_v1.txt) (`<received frame hex> <pass-through relayed frame hex> <relay that decodes, hex or ->`: a frame whose model or destination code the relay does not hold is forwarded by "Relay without the model" and not by the relay that decodes; a line with `- -` is a damaged frame that no relay may forward). An implementation conforms if, for every line of [`test/vectors_hb3_v1.txt`](../test/vectors_hb3_v1.txt) (`<frame hex> <TNC2 packet hex>`, 554 fabricated packets, model version 0, which includes destination list set 1), the encoder produces exactly that frame from the packet and the receiver accepts the frame and returns exactly that packet;
for [`test/vectors_hb3_relay_v1.txt`](../test/vectors_hb3_relay_v1.txt) and [`test/vectors_hb3_relay_chain_v1.txt`](../test/vectors_hb3_relay_chain_v1.txt) (`<received frame> <relayed frame> <relayed packet>`; first hop, and second and later hops of frames that already carry tuples), the relay produces the relayed frame and the receiver accepts it and returns the relayed packet;
for [`test/vectors_hb3_relay_edge_v1.txt`](../test/vectors_hb3_relay_edge_v1.txt) (the same columns; a relayed frame of `-` means the relay must **not** relay that frame): the source and destination bytes copied as received, a listed destination in escape form, seven tuples already in the frame with the relay at the eighth element, tuples on unused elements, and a long packet (relayed without the relay's own tuple when the tuple would make it longer than 255 bytes, not relayed when it is too long even without it);
and every frame of [`test/vectors_hb3_reject_v1.txt`](../test/vectors_hb3_reject_v1.txt) (`<frame hex> <why>`: impossible RXT blocks, headers, versions and address elements, text elements holding `:`, `,`, `>` or NUL, and PPM text holding a NUL) is dropped. The edge cases of the list above (WIDE look-alikes, SSIDs 0 to 255, text elements with a star, empty and over-long elements, 15 and 16 path elements, frames at the 255-byte limit, malformed positions, altitude forms, the relay with an eighth element) are in [`test/hb_edge_packets.txt`](../test/hb_edge_packets.txt), part of the 554 vectors. A worked example follows this section. An independent Python implementation, written from this text before these rules were completed, and the C reference agree on all vectors and on 142,282 intact and damaged frames. The C reference is [`tools/hb3/`](../tools/hb3/) (`make hb3-test`); the fabricated packets come from `tools/hb/gen_packets.py`.
The frame bytes depend on these encoder choices, which are therefore part of the format:

* **TNC2 packet:** `SOURCE>DESTINATION[,PATH...]:INFORMATION`, split at the first `>`, the first `:` after it, and the commas between them; at most 15 path elements; the whole frame (including `3C FF 03`, any RXT block) must be at most 255 bytes and the information field at most 255 bytes, otherwise the packet is not encodable and a plain frame is sent. A packet is also not encodable when an element is empty (an empty source, destination or `,,`), a text element is longer than 31 bytes, or `>` or `:` is missing, or the source contains a `:` (the packet would be split at the wrong colon when it is rebuilt). A packet whose path holds a non-compliant `WIDEn-N` is not encodable either, but it is **not** sent as a plain frame: it is dropped (see Non-compliant WIDE paths below). The encoder sends the hybrid frame whenever the packet is encodable and does not compare its length with the plain frame. The encoder decodes what it built and uses the frame only if the packet comes back byte for byte.
* **Address elements:** `WIDEn` or `WIDEn-N` with `n` 1 to 7 and `N` 1 to `n`, optionally followed by `*`, is one byte `0 s nnn NNN` (`s` = the `*`, `N` = 0 when there is no `-N`). A callsign of 1 to 6 characters `A-Z0-9`, optionally `-SSID` (a decimal number 1 to 255 with no leading zero), optionally `*`, is `1 0 s xxxxx` (`xxxxx` the SSID, or 31 with the SSID in one extra byte after the callsign when it is above 30) followed by the 6 characters (padded with spaces) as four bytes, big endian, in base 40 over the alphabet space, `A` to `Z`, `0` to `9` (space is 0, `A` is 1, `0` is 27; the first character is the most significant digit; SSID 31 also uses the extra byte).
  A `WIDE` form is always the one-byte form when it matches exactly (`WIDE1-1` included). A path element shaped like a WIDE alias that does not follow the APRS rule (`WIDE1-2`, `WIDE2-3`, `WIDE8-1`, `WIDE1-0`) is not carried at all: the packet is dropped (see Non-compliant WIDE paths below). Anything else (lower case, `-0`, `-05`, longer names) is a text element `1 1 lllll` and its 1 to 31 bytes, including a trailing `*` as one of its bytes (the used mark has a bit only on WIDE and callsign elements).
  Tactical and other names that do not fit (longer than 6 characters, lower case, other characters) travel as text elements and are never rejected for their content.
  **A receiver drops a frame with an element that no encoder can produce (strict rule):** a `WIDE` byte with `n` = 0 or `N` above `n`, or a path element in any form (callsign or text element as well) that reads as a non-compliant `WIDEn-N`; a callsign element whose SSID extra byte is below 31, or whose four-byte value is 40^6 (4,096,000,000) or more or has a base-40 digit above 36 (the alphabet has 37 symbols), that is empty, or that has a space inside it; any element that runs past the end of the frame; a text element of length 0; a text element that holds `:`, `,` or `>` (the rebuilt TNC2 packet would split at the wrong place; an encoder cannot produce one, because it splits its input at those characters); a NUL byte (0x00) anywhere in the decoded text, which includes the text of a PPM payload, and the CRC-16 is taken over the whole text, not up to the NUL (an encoder is given a text string and cannot produce one). This covers the source, the destination escape and every path element, and it costs nothing because the receiver parses them anyway. The other characters of a text element are not checked, so the rule gives less protection where the path is text.
* **Destination:** a byte `0 iiiiiii` with the index when the destination text equals an entry of the destination list (exact match, no `*`; the encoder must use the code then, and never uses a code that is not assigned); otherwise the byte `0x7F` (code 127) and the destination as an address element. A receiver also accepts the escape form of a listed destination (the destination is under the content CRC as text). *(3.0.0-dev.1, proposed; in 2.0.0 the byte was `00 iiiiii` and the escape `0x3F`.)*
* **Kind:** the position bit (bit 0) is set when the information field starts with a position, otherwise clear; the form bit (bit 1) is set for LACF content and clear for plain (rule below). A position is: a data type indicator `!`, `=`, `/` or `@`; for `/` and `@` a timestamp of six digits and one of `z`, `h`, `/`; then an uncompressed position (`ddmm.hhN` or `S`, a table character, `dddmm.hhE` or `W`, a symbol character; 19 characters, digits only in the numbers) or a compressed one (a table character from `/ \ A-Z a-j`, four and four base-91 characters (each `!` to `{`, 33 to 123), a symbol character (any byte) and three characters `c s T` (any bytes); 13 characters). Course/speed follows an uncompressed position as `ddd/ddd` (seven characters, digits and a slash).
  If the position block built from it does not rebuild the information field exactly, kind 2 is used (for example `/A=-00000`, which would come back as `/A=000000`, or minutes that overflow such as `4999.99N`).
* **Position block:** a flags byte (bits 1-0 the data type indicator 0 `!`, 1 `=`, 2 `/`, 3 `@`; bit 2 timestamp; bit 3 compressed; bit 4 course/speed present, uncompressed only; bit 5 altitude present; bit 6 compressed with `c s` both exactly two spaces (0x20), so that only `T` follows; otherwise all three bytes are kept), then the timestamp as three bytes (the six digits as a number times 4, plus 0, 1 or 2 for `z`, `h`, `/`), then the position: uncompressed is six bytes holding (latitude in hundredths of a minute times 2, plus 1 for south) times 2^22 plus (longitude in hundredths of a minute times 2, plus 1 for west), then the table and symbol characters, then (bit 4) three bytes of course times 1024 plus speed when the 7 characters `ddd/ddd` follow the position;
  compressed is seven bytes holding the latitude's four base-91 characters as a number times 2^27 plus the longitude's, then the table and symbol characters, then the one byte `T` (bit 6) or the three bytes `c s T`. Altitude (bit 5): the first `/A=` that is followed by six digits, or by `-` and five digits (an earlier `/A=` followed by anything else does not count), within the first 255 bytes of the comment (the comment is shorter than that, so the limit never matters) is removed from it and stored as one byte (its offset, which is the same in the original and in the remaining comment) and three bytes (the altitude in feet, 24-bit two's complement). What remains of the comment is the text.
  **A receiver drops a position block that no encoder can produce (strict rule):** flags bit 7 set; bit 4 (course/speed) together with bit 3 (compressed); bit 6 without bit 3; a timestamp value above 999999 or with unit code 3, or a timestamp flag on a data type `!` or `=`; a latitude above 90 degrees or a longitude above 180 degrees (uncompressed, in hundredths of a minute: 540000 and 1080000); a compressed latitude or longitude of 91^4 or more; a course or a speed above 999; an altitude outside -99999 to 999999; an altitude offset beyond the comment.

* **Content:** the text is the information field (kind 2) or the comment that remains (kind 3), at most 255 bytes. LACF content is used when the payload (from the table: info-native for kind 2, comment-only for kind 3) satisfies `4 + n < 3 + L`; otherwise plain content (kind 0 or 1), and an empty text is plain content with nothing after `V` and the CRC.
* **Relay vectors:** the relay is `N0RLY-(k+1)` and its tuple is the bytes `50+k 20+k 45+k 10+k` (decimal), where `k` is the number of tuples already in the received frame; so the first hop is `N0RLY-1` with `50 20 45 10`, the second `N0RLY-2` with `51 21 46 11`. The relay takes the first path element that has no `*`: `WIDE1-1` becomes `N0RLY-n*`; `WIDE2-1` becomes `N0RLY-n*`; `WIDE2-2` becomes `N0RLY-n*,WIDE2-1`; with any other first unused element, or no path, the packet is not relayed and has no vector. The header gets the new path count and the RXT bit; the position block and the content field are copied byte for byte; the block is the old tuples, the relay's tuple at the index of its element, and the mask.

### Non-compliant WIDE paths

**The rule.** A path element shaped like a WIDE alias (`WIDE`, a number n, optionally `-` and a number N, optionally `*`) must follow the APRS rule: **n is 1 to 7 and N is 1 to n**. A packet whose path holds one that does not (for example `WIDE1-2`, `WIDE2-3`, `WIDE8-1`, `WIDE1-0`) is **dropped**:
* a LACF sender does not encode it;
* a gateway or digipeater does not repeat it on RF **in any form**, neither as a LACF frame nor as plain text;
* a LACF receiver drops a frame that carries one, whatever element form it is written in.

`WIDEn` without a `-N` (it is the used form of an alias) and names that merely begin with WIDE (`WIDER`) are not affected. The rule never changes a packet: a compliant packet is carried exactly, and a malformed one is refused, not corrected.

**Why.**
1. In APRS, N counts the hops still to go, counting down from n, so it cannot exceed n. A path with N above n cannot come from a correctly configured station or digipeater: it is a misconfiguration or damage.
2. The channel is slow and shared (a 57-byte beacon holds it for about 2.6 seconds at SF12). Repeating a malformed path spends that airtime on a packet whose hop count no digipeater can apply the same way: each one would count it down differently.
3. It keeps the one-byte WIDE element meaning one thing. Its three bits for n and three for N cannot hold more than 7, and a receiver can treat N above n as damage.
4. It keeps this format's receivers simple and strict, and it never produces a packet that was not on the air before.

**How often it happens.** In 14,305 real LoRa APRS packets there are 7,097 WIDE path elements: 7,025 follow the rule, 69 are a plain `WIDEn`, and 3 do not (all `WIDE1-2`), which is 0.02% of the packets.

**A difference from the native encoder.** F4MLV's native encoder writes `WIDE1-2` and `WIDE2-3` as a one-byte WIDE element, so it accepts such paths. Under this rule a LACF receiver drops that frame and a LACF relay does not repeat it. This is a decision of this plan, to be discussed with him.

**For implementers.** `hb3_path_ok()` (C) and `path_ok()` (Python) say whether a TNC2 packet's path is acceptable. A caller that gets "no" must drop the packet: it must not fall back to a plain frame and must not relay it. A relay that does not run this check cannot be reached by this plan. Test vectors: the edge packets `WIDE1-2`, `WIDE1-2*`, `WIDE01-1`, `WIDE10-1` and the older WIDE look-alikes in `test/hb_edge_packets.txt` (all not encodable), and three frames in `test/vectors_hb3_reject_v1.txt` that carry a non-compliant path in the callsign and text forms.

## Worked example

One packet, as the origin sends it and after two relay hops. All bytes are hexadecimal; the three frames are in the vector files' format and were produced by the reference code.

`N0CALL-9>APLRG1,WIDE1-1,WIDE2-2:!3722.40N/12158.93W>LoRa APRS iGate on the hill` (79 characters)

**The origin's frame, 35 bytes** (kind 3: position block, then the comment, PPM coded):

| Bytes | Field | Meaning |
|---|---|---|
| `3C FF 03` | prefix | native frame |
| `23` | header | `0010 0 0 11`: 2 path elements, no RXT block, bit 2 clear, kind 3 |
| `89 59 94 A6 2C` | source | callsign element: `1 0 0 01001` (SSID 9), then `N0CALL` in base 40 as `59 94 A6 2C` |
| `00` | destination | code 0: `APLRG1`, the first entry of destination list set 1 |
| `09` | path 0 | `WIDE1-1`: `0 0 001 001` |
| `12` | path 1 | `WIDE2-2`: `0 0 010 010` |
| `00` | position flags | `!` (data type 0), no timestamp, uncompressed, no course/speed, no altitude |
| `01 B5 F8 16 55 EB` | position | latitude 37°22.40' N as hundredths of a minute times 2 plus 0 (N), 22 bits, then longitude 121°58.93' W times 2 plus 1 (W), 22 bits (`(224240 × 2) << 22 | (731893 × 2 + 1)`) |
| `2F 3E` | symbol | table `/`, symbol `>` |
| `00` | `V` | `0 00 00000`: no capability bytes (`c` = 0), spare bits 0, model version 0 |
| `1B` | `L` | 27 characters of comment |
| `C3 EE` | CRC-16/X-25 | `0xEEC3`, low byte first, over the bytes as sent: `03 00` + `89 59 94 A6 2C 00` (source and destination) + the position block `00 01 B5 F8 16 55 EB 2F 3E` + `1B` (L) + the 10 payload bytes |
| `93 AF 55 0A 8E 18 E2 07 49 5B` | payload | the 27 comment characters in 10 bytes (the comment-only table, because the position is in the native block) |

The text is 79 bytes plus the 3-byte prefix; the frame is 35 bytes.

**After the first relay (`N0RLY-1`, tuple 50 20 45 10), 44 bytes:** `3CFF032B895994A62C00A159A392E8120001B5F81655EB2F3E001BC3EE93AF550A8E18E207495B32142D0A81`

* header `23` becomes `2B` (RXT bit set);
* path 0 `09` (`WIDE1-1`) becomes `A1 59 A3 92 E8`: callsign element, `1 0 1 00001`, the used mark `*` and SSID 1, then `N0RLY` in base 40 (the path is now `N0RLY-1*,WIDE2-2`; path 1, `12`, is unchanged);
* the position block and the content field (`00 ... 5B`) are copied byte for byte;
* the RXT block is added at the end: `32 14 2D 0A` is the tuple (RSSI 50, SNR 20, frequency offset 45, time to hop 10), then the mask `81` = marker bit 7 and bit 0: one tuple, for path element 0.

**After the second relay (`N0RLY-2`, tuple 51 21 46 11), 53 bytes:** `3CFF033B895994A62C00A159A392E8A259A392E8110001B5F81655EB2F3E001BC3EE93AF550A8E18E207495B32142D0A33152E0B83`

* header `2B` becomes `3B` (3 path elements);
* the first unused element is `WIDE2-2`: it becomes `A2 59 A3 92 E8` (`N0RLY-2*`) followed by `11` (`WIDE2-1`), so the path is `N0RLY-1*,N0RLY-2*,WIDE2-1`;
* the position block and content are still the same bytes;
* the RXT block keeps the first tuple, adds `33 15 2E 0B` (51 21 46 11) after it, and the mask becomes `83` = marker, bit 0 and bit 1: two tuples, for path elements 0 and 1.

Each receiver verifies the content once (CRC and re-encode); a relay copies it without decoding the text again.

## Versions

LACF uses **semantic version numbers**, so anyone can say which one they use and whether it is the latest.

| Part | Meaning |
|---|---|
| MAJOR | the generation of the frame layout, which is the frame type byte: `3C FF 02` is LACF 2.x (spec 11, tested) and `3C FF 03` is LACF 3.x (this proposal). A change to the front of the frame (header, addresses, path, RXT block, position block) or to what the destination byte or `V` mean is a MAJOR change and needs a new type byte. Old types stay defined and are decoded forever, and a device may hold several decoders. A type a receiver does not hold is dropped, never decoded wrongly |
| model version | the low five bits of `V` (32 values), starting at 0: the pair of LACF tables. A new model is breaking for a receiver that does not hold it (it drops the frame, never misreads), but it needs no new type byte, because it changes only the content field and a relay that has "Relay without the model" can still forward it. It is recorded in the changelog as its own line and in MINOR or PATCH as the change warrants. Firmware keeps every released model. (2.0.0 called the low three bits the table version, which also named the destination list.) |
| MINOR | a backward-compatible addition: something a MAJOR-equal receiver either ignores or drops cleanly, never misreads. Receivers ignore only bit `c` of the destination byte, the spare bits `ss` of `V`, the spare capability bits, RXT values 90 to 127 and the capability fields they do not know, and drop every other reserved value (header bit 2, destination codes 126 and unassigned ones, a model they do not hold), so a MINOR feature uses those or a frame that older stations drop. Adding a destination code is a MINOR. Changing the meaning of any existing byte is a MAJOR |
| PATCH | wording, examples and vectors only; no change to any byte |

* **Where the number lives:** this document (the Version row), `VERSION` and `CHANGELOG.md` at the repository root, `HB3_SPEC_VERSION` in `tools/hb3/hb3.h` and `SPEC_VERSION` in `tools/hb3/hb3_py/hb3.py`. Firmware should print the version it implements (for example `LACF 2.0.0`) in its startup text or about screen, with its own build number.
* **Am I up to date?** Compare your number with the newest entry of `CHANGELOG.md`. Frames from a different MAJOR are not accepted, so a station that is behind on MAJOR will see the newer stations as "dropped", not as garbage; the log line says `version or coding`.
* **MINOR and PATCH are not on the air:** `V` carries only the MAJOR, so a station cannot learn from received frames that it is behind on MINOR or PATCH; only the changelog and the number the firmware prints tell it. A later version could carry the full number in a beacon or capability frame.
* **There is no automatic check** over the air or on the network in this version.
* **History of the numbers:** 1.x was the test builds of 1 and 2 November 2026 (plain content without a CRC), never released. 2.0.0 adds the CRC-16 to plain content.

## Size

Figures for this combination, bytes saved against the plain frame including the 3-byte prefix, full frames only (N=1), no RXT. None of them is an RF measurement: they are harness results, the encoder run on captured packets, or estimates:

| Data | Combination | Harness result (without `V` and the CRC-16) | Current frame (with `V` and the CRC-16) |
|---|---|---|---|
| K6ATV APRS-IS capture, 93,145 held-out packets | hybrid B + tocall table + lossless position | 46.2% | **41.9% measured** (81.3 B to 47.2 B; 313 packets that cannot be encoded counted as plain frames) |
| same | + packed position (optional, not in this plan; no encoder) | 47.2% | about 43% estimated |
| F4MLV Ariège, 8,062 packets (comment 15) | table v2 + lossless position, matched tables | 39.3% | about 35% estimated |
| F4MLV Arizona, 2,659 packets (comment 15) | same | 43.8% | about 40% estimated |
| K6ATV, 14,305 real packets ([`docs/hybrid-savings-and-timing.md`](../docs/hybrid-savings-and-timing.md)) | hybrid B as in this plan | not run | **43.8% measured** (86.6 B to 48.7 B) |

"Measured" means the current reference encoder (`tools/hb3`) was run on the packets, with `V`, the CRC-16 and the deterministic ending, no RXT. "Estimated" is used where we do not have the packets or an encoder: the harness figure minus the extra bytes, divided by the mean plain frame of that set. The extra bytes are `V` (1), the CRC-16 (2) and the ending (0.25 on average), 3.25 in all (measured: 2.25 for the CRC-16 and the ending, plus 1 for `V`); using 3.0 or 3.5 bytes moves each estimate by about 0.3 points. The mean plain frame is 81.3 B for the first two rows (measured); 74.8 B for Ariège, worked out from F4MLV's own figures (0.613 bytes per packet is a 0.82-point change, comment 15); and 86.6 B for Arizona, assumed from our own 14,305 packets because his mean is not published. His own re-run with the current encoder would replace these estimates.

Dropping `V` (folding the version into the header) would give one byte back. The 50.2% measured earlier on the whole-text test frame is the whole-packet figure and is not the figure for this plan.

## Integrity: what is measured

The payload rules were tested first on a whole-text test frame (withdrawn as a format; the results concern the payload and carry over): cut frames, 0 wrongly accepted at every size; with the CRC-16 and the re-encode check together 0 of about 2.85 million damaged frames accepted. Those runs used the whole-packet table of spec 01 on whole texts. The same checks were repeated with the hybrid B tables on real texts
([`results-hybridB-payload.txt`](../tools/integrity/results-hybridB-payload.txt)): the info-native table on 14,305 information fields (12,166 smaller than plain) and the comment-only table on 11,367 comments after a position (10,704 smaller than plain; the position is stripped with regular expressions, so this is an approximation of the comment). For both tables the ending is deterministic and independent of what follows (328,482 of 328,482 and 289,008 of 289,008 decodes right with 0 to 8 following bytes),
the receiver accepts every intact frame with and without a 6-byte tail, **0 cut frames are wrongly accepted at any cut size (1 to 16 bytes)**, 0 damaged frames (1, 2, 3, 8 flipped bits, random payload) are accepted, and the one-pass receiver agrees with the two-pass one. The frame header of those tests is the withdrawn test frame's (6 bytes), not this plan's (4 bytes plus the native fields); the checks concern the payload.
Larger damage runs with the shipping combination (CRC-16 and re-encode check), 10 damaged copies per text and damage type ([`results-hybridB-stress.txt`](../tools/integrity/results-hybridB-stress.txt)): **0 wrongly accepted of about 608,000 trials (info-native table) and about 533,000 (comment-only table)**.
The re-encode check alone leaves 1 in 181 to 1 in 2,644 (info-native) and 1 in 104 to 1 in 204 (comment-only: short comments leave less for the check to catch), and the CRC-16 alone about 1 in 40,000 to 1 in 120,000, so the two are needed together; a 95% upper bound for 0 of 1.14 million is about 1 in 380,000.

**Target.** On one hop the undetected-error rate should be no worse than VHF APRS, whose AX.25 frame check is a CRC-16: about 1 in 65,536 for random damage. (VHF APRS is also protected per hop only: a digipeater recomputes the check.) The LoRa payload CRC is 16 bits (CCITT), covers every payload byte, header byte, address and tuple included, and gives that level for random channel damage on one hop. It is optional,
so a sender must enable it and a receiver must act on its status. It cannot see three things: a **payload cut before transmission** (the CRC is computed after the cut), a **decoder using the wrong table** (the frame arrives intact), and a **fault in a relay** after reception (the relay then sends with a fresh LoRa CRC).

**What protects each part of the frame:**

| Part | Channel noise on a hop | Cut before transmission | Wrong table, or a relay fault |
|---|---|---|---|
| Source, destination, `V`, the header's kind bits (PPM content) | LoRa CRC and the content CRC, end to end across relays | not applicable (the front is not cut) | the content CRC: a wrong destination list or a damaged source, destination or `V` fails it. The kind bits are in the CRC input too, so a flipped form or position bit fails it |
| `3C FF 03`, path, path count, RXT bit | LoRa CRC only | not applicable | not covered (relays change them) |
| PPM payload | LoRa CRC, then the re-encode check and the content CRC | the re-encode check rejects every cut | the re-encode check passes about 0.4% of wrong-table decodes (up to 2.1% for some table pairs), the content CRC alone all but 2 of 360,000, both together 0 of 360,000 |
| Information field (position block and comment) | LoRa CRC and the content CRC, end to end across relays | the content CRC | the content CRC |
| RXT tuples and mask | LoRa CRC only, plus the marker and range checks | the marked mask byte | not covered (see below) |
| Plain content (kinds 0 and 1) | LoRa CRC and the content CRC | the content CRC (protocol version 2; earlier test builds had none) | the content CRC |

So the plan meets the VHF level per hop for channel noise on every byte, and is stronger than VHF for the information field across relays. It is **not** end-to-end protection for the path, the path count or RXT tuples, and a kind-bit flip into kind 0 or 1 is not caught.

**How the combined rates are meant to be read.** The end-to-end figure for damage to the PPM payload is (the probability that damage passes the LoRa CRC, about 1 in 65,536) times (the share of those frames our checks accept, measured below). That product assumes the damage is random-looking, that the two checks are independent, and that the rate is per damaged frame, not per frame sent. Errors of 1 to 3 bits cannot pass a 16-bit LoRa CRC at all, so the rows for few-bit flips describe an event that does not occur on the air; they test the checks, not the channel. Cuts and wrong tables have no LoRa CRC factor.

* **Truncation of the PPM payload:** the re-encode check. Alone, with no CRC, 0 of about 230,000 cut frames per cut size were accepted (286,100 texts, [`results-hybridB-reencode-alone.txt`](../tools/integrity/results-hybridB-reencode-alone.txt)). A review that tried every cut point on about 300,000 adversarial texts found that a payload that is a proper prefix or extension of the canonical encoding can never be accepted, so cuts, appended bytes and joined frames are rejected by construction.
* **Wrong table, wrong model, a second implementation that differs:** the re-encode check alone catches about 99.6% (0.4% accepted over 12 ordered pairs of four tables; one changed count in a table, 1 in 155), the CRC-16 alone all but 2 of 360,000, and both together all of them (95% upper bound about 1 in 120,000). The **content CRC-16** is therefore kept: computed by the origin over the information field, never touched by relays. (An earlier draft said the re-encode check cannot see a wrong table; that figure, 46%, was for primed wrong references with the old ending.)
* **Random damage to the payload after the LoRa CRC has passed:** not zero. A review with 180 million random payload damages found 1 frame accepted with wrong text (5.6e-9), and 6 of 90.3 million trials that replaced one of the last three payload bytes (6.6e-8); the wrong text differs in the last 4 to 9 characters. The re-encode check alone passes 1 in 327 of the random damage and the CRC-16 alone 1 in 64,000. Our own earlier run, 0 of about 2.85 million, bounds the combined rate only to about 1 in 950,000.
* **`L`** is protected by the CRC only (see Receiver, step 4).
* **The RXT block is unauthenticated.** Only the LoRa CRC protects it, plus two cheap checks: the marked mask byte makes every cut inside the block fail, and the range check fails random bytes about 98% of the time, but a single-bit flip of a tuple byte stays in range 76% of the time. In a test block, 61% of single-bit flips were accepted with changed values, and a two-bit mask change that keeps the number of tuples (`0x83` to `0x85`) was accepted every time and silently moves the tuples to different path elements.
  A per-tuple check written by the relay that owns the tuple would add protection; a block CRC recomputed by every relay would not.

Spec 11 spends 2 bytes on the content CRC-16 and none on the RXT block. **Adopted after review:** the content CRC also covers the kind bits, `V`, source and destination (no extra bytes), and `V` is mandatory in every frame, so the destination list has a version.

**The RXT block** ([`tools/integrity/rxt_tail_test.py`](../tools/integrity/rxt_tail_test.py)), tested on 600 real packets carrying 0 to 3 tuples, with the rules above:

* **Cut frames, 1 to 16 bytes off the end: 0 of 21,564 accepted** with the marked mask byte ([`results-rxt-tail-marked.txt`](../tools/integrity/results-rxt-tail-marked.txt)).
  Without the marker (a plain mask in the last byte, [`results-rxt-tail-plain.txt`](../tools/integrity/results-rxt-tail-plain.txt)) 73 of the same cut frames were accepted with a wrong mask (cuts of 4 or 8 bytes inside the block, 17 to 34 of 600 for the cut sizes concerned), because a left-over tuple byte can look like a valid mask. That is why the marker is in the plan.
* **Damage inside the block** (1 or 3 flipped bits, 36,000 trials): the range check lowers the share accepted from 87% and 68% to 66% and 32% (one and three tuples; [`results-rxt-tail-marked-range.txt`](../tools/integrity/results-rxt-tail-marked-range.txt)). Flips of a few bits are already caught by the LoRa CRC; the range check matters for random-looking damage. A CRC-16 over the block (tested: 0 accepted, and every 1- to 3-bit flip detected) was **not adopted**: it would be recomputed by every relay and
  adds nothing the LoRa CRC does not give per hop ([`results-rxt-crc.txt`](../tools/integrity/results-rxt-crc.txt)).

## Receiver time (ESP32, measured)

One-pass receiver (decode, CRC-16, re-encode compare) with the hybrid B tables, fast `build_freq`, mean per frame over the texts PPM can shrink ([`results-esp32-timing.txt`](../tools/integrity/results-esp32-timing.txt)):

| Board | Information field (kind 2) | Comment after a position (kind 3) |
|---|---|---|
| LILYGO LoRa32 v2.1 (ESP32-PICO-D4) | 4.2 ms (3.5 ms with the key tables copied to RAM) | 2.0 ms (1.8 ms) |
| LILYGO T-Deck (ESP32-S3) | 2.8 ms (2.7 ms) | 1.4 ms (1.4 ms) |

The earlier whole-packet figures (6.3 ms and 5.0 ms) are higher because the text is longer. The re-encode check adds nothing measurable in the one-pass receiver; a two-pass receiver roughly doubles the time. These are codec-only test sketches on 48 fabricated packets (28 information fields and 18 comments shrink), without WiFi, not the iGate or tracker firmware.

## Not yet measured

Status of 2 November 2026. The format is in early development and is still being tested and reviewed for change; several items that this section used to list as open are now done, and the open list is shorter. What is open is listed last.

**Done since this section was first written**

* **The extended CRC input and kind 3 end to end.** The C reference (`tools/hb3/hb3.c`) and the independent Python implementation (`tools/hb3/hb3_py/hb3.py`) compute the CRC over the kind bits, `V`, source, destination and information field, pack `V` as `vvvvv ttt`, rebuild the position block and the comment-only table, and parse the destination and path. They agree on all of [`test/vectors_hb3_v1.txt`](../test/vectors_hb3_v1.txt) (554 packets, all four kinds), the relay vectors and the rejection vectors, and on 142,282 intact and damaged frames.
* **The RXT block with plain content (kinds 0 and 1).** About two thirds of the relay vectors are plain-content frames (197 of 298 first-hop and 129 of 199 later-hop), and the cut tests include them. **Closed in protocol version 2:** plain content has its own CRC-16, so a cut that removes the block and part of the text is rejected by the CRC (0 of every cut of every plain frame in 3,000 generated packets accepted, and F4MLV's cases B2 and B6 are rejected). Plain content has the CRC-16 and nothing more, so it meets the 1 in 65,536 target but does not beat it: in the stress test (11,384 plain frames with 1 to 8 random bytes appended) one was accepted, which is that rate. LACF content also has the re-encode check. The cost is 2 bytes per plain frame: 11.7% of 1,000 real RF packets fell back to plain, +0.23 bytes per packet on average (+17 ms at SF12).
* **The size of a native PPM frame with an encoder, `V` and the CRC included.** Measured on 14,305 real packets with the reference encoder: 86.6 bytes plain against 48.7 bytes, 43.8% smaller, and about 35% less airtime ([`docs/hybrid-savings-and-timing.md`](../docs/hybrid-savings-and-timing.md)). On the air, beacons were 29 against 50 bytes (iGate), 58 against 89 (T-Deck) and 39 against 57 (Dire Wolf) ([`docs/lacf-test-results-2026-11-01.md`](../docs/lacf-test-results-2026-11-01.md)).
* **An encoder and decoder with this plan.** They exist in test branches of the iGate (`feature/lacf`), the T-Deck tracker (`hybrid`) and Dire Wolf (`feature/lora-hybrid`), and have been run on the air between a LoRa32, a T-Deck and a hill Raspberry Pi; the tests continue.
* **The relay path in the reference code.** Path rewrite, RXT append and no PPM decode of the content, including locating the end of the position block without decoding it, are implemented in the C and Python references and covered by 298 + 199 + 8 relay vectors (the last eight are edge cases such as tuples on unused elements and packets near the 255-byte limit). `hb_relay_to` is the same relay with the new path chosen by the caller, for a digipeater with its own rules (the iGate and the tracker use it); it passes the same vectors, with the relayed path taken from the vector.

* **A relay in the iGate and the tracker, run on the air (2 November 2026).** Both digipeat an LACF frame as the Relay section says: the frame is built from the received bytes with the reference `hb_relay_to`, the earlier tuples are kept, and the relay's own tuple is added, with its time to hop filled in just before it transmits (queue, channel access and time on air included). The LoRa32 relayed T-Deck beacons of 58 and 25 bytes as 67 and 34 bytes; the T-Deck relayed a LoRa32 beacon of 29 bytes as 38 bytes: +9 bytes each, 4 for the longer path and 5 for the first tuple. Each board decoded the other's relay, and the values in the tuple matched what the relay had measured, within the resolution of each field (RSSI -81 dBm and -81 dBm, SNR 12.75 dB and 13.0 dB, time to hop 3,338 ms and 3,233 ms; the frequency offset, -3,324 Hz, read as the end of the scale, -2,500 Hz). Details in [`docs/lacf-test-results-2026-11-01.md`](../docs/lacf-test-results-2026-11-01.md), section 8. A frame that would be longer than 255 bytes with the relay's tuple goes out without it (host tests only so far).

**Still open**

* **A relay in Dire Wolf.** It counts the tuples of a frame it receives; the AX.25 frame cannot carry them, so it does not pass them on and does not relay LACF.
* **A chain of two relays over RF.** One relay hop has been run on the air in each direction (iGate to T-Deck and T-Deck to LoRa32), not one frame through two relays. The hill's Dire Wolf logged and decoded 5 of the 6 relays of the test (all at the predicted size; the sixth is not in its log), see the test results, section 8. F4MLV's own firmware does not implement this plan.
* **More conditions.** Other boards and spreading factors, a busy or weak channel, long runs. The tests so far are short runs on one LoRa32, one T-Deck and one Dire Wolf station.

## Upgrade path (3.0.0-dev.1, proposed, needs F4MLV's agreement)

How a mixed network behaves when a new model or new destination codes appear. A picture of each direction, as tables, is in [`docs/lacf-model-upgrade.md`](../docs/lacf-model-upgrade.md).

1. **Receivers first, senders last.** A release adds decoding (a new model, new destination codes) before any sender uses it by default: upgrade iGates and digipeaters before trackers default to a new model.
2. **Plain content is the floor:** always sendable and always accepted, whatever the model bits say. Standard LoRa APRS text keeps working through every LACF change.
3. **Old formats are decoded indefinitely.** No receiver drops a released model or destination code, and a MAJOR change keeps the old frame types defined.
4. **A sender uses the lowest frame type and the oldest model that carries the frame** (the escape for a destination added after the first release, or plain), when it knows, or cannot rule out, that stations listening do not hold the newer one.
5. **How a sender learns what its listeners hold** (capability bytes, a setting, release notes) is left to implementations and will be decided during development. The capability bytes cannot tell it about a receive-only iGate, which never transmits.
6. **A relay that has "Relay without the model" before the first new model ships passes new-model frames on; an old digipeater without it drops them, and an old iGate cannot gate them.** The most visible effect of a rollout is a gap on aprs.fi wherever only old iGates hear a new-model sender, which is why iGates upgrade first.
7. **Both formats on one device.** The 2.0.0 frame (type `02`) and this one (type `03`) are separate codecs read by the type byte, so a firmware can hold both, share the PPM tables, and decode either; flash is the only cost. A station that sends type `03` is not heard by a station that holds only the 2.0.0 decoder, so a sender that knows its listeners are 2.0.0 stations keeps sending type `02`, or plain text.
## Extending the format later

The information kind (header bits 1 and 0) is full now that all four kinds are used (form and position block), so a future need does not have to squeeze into this header. **A new header is a new frame type byte** (`3C FF xx`), with its own layout, defined when it is warranted. Receivers drop any `3C FF` type they do not know and never show it as text, so a new type is invisible to old firmware.
Types in use or suggested: `01` plain text, `02` the 2.0.0 native frame (spec 11, tested), `03` this proposal (spec 12), `80` to `FF` the whole-packet PPM frames of spec 01 and spec 08. Several different headers can therefore live side by side, each used where it fits.
Inside this header, the content field's version byte `V` is allocated append-only: its low five bits can name further model versions (32 in all); relays copy the content field without reading it, so adding a model version changes nothing for a relay that has "Relay without the model" (3.0.0-dev.1).

## Roles and profiles (to be defined)

Interoperability and a staged upgrade need a standard profile for each kind of station, so that firmware by different authors agrees on who must understand what. Three roles, with what each needs, as a starting point:

| Role | Originates | Must decode | Relays |
|---|---|---|---|
| **Tracker** | its own packets, as plain text, and optionally native or hybrid B (no RXT, path as configured) | plain text, native, hybrid B (messages and replies reach it) | no, in the code read |
| **Digipeater** | normally nothing of its own except beacons and telemetry | every format it relays | yes: path rewrite, its own RXT tuple, content copied (Relay, above) |
| **iGate** (RF to APRS-IS) | beacons, telemetry, and packets from APRS-IS to RF in the format its settings choose | every format, decoded to TNC2 with the RXT block removed before upload | optional: digipeating is a setting of an iGate, so **every iGate needs the encoder as well as the decoder** |

Rules that apply to every role: a plain text frame is always accepted and always sendable (the floor); an unknown `3C FF` type, kind or table version is dropped and never shown as text; the model version in `V` decides which tables a frame needs.
A **digipeater is a mode of an iGate (or of a tracker), not a separate profile**: the frame is the same and only the relay rules above apply, so nothing is needed for it beyond the iGate's decoder and encoder (a "relays hybrid B" capability bit may still be wanted).
Open: who defines and maintains the profiles; and how many profile levels there are. How a station advertises its role and what it decodes is proposed in 3.0.0-dev.1: the capability bytes (for example level 0 plain text, level 1 hybrid B).

## Firmware integration notes (from reading his tracker code)

*Dated 31 October 2026: these notes describe the code as it was read, before this plan was implemented. Since then the plan has been added in test branches of the iGate (`feature/lacf`), the tracker fork (`hybrid`) and Dire Wolf (`feature/lora-hybrid`; the branch named in the Dire Wolf note below is the one that was read). None of these adds RXT measurements or relays LACF frames yet. See "Not yet measured".*

His tracker (`moricef/LoRa_APRS_Tracker`, branch `devel`, `src/lora_utils.cpp` and `lib/native_aprs/`; the native codec there is a copy of his iGate's, with the relay functions left out) does this today:

* **Receive:** the radio is read as raw bytes and only when the radio library reports no CRC error. A native frame (`3C FF 03`) is decoded to TNC2, its RXT block turned into a v2 text trailer, and passed on as a `3C FF 01` text frame, so everything after that sees ordinary text. Any other frame is read as text up to the first zero byte. **It does not drop an unknown `3C FF xx` type in that file**, which this plan requires (a `3C FF 81` frame would be shown as garbage); the check belongs with the new type.
* **Transmit:** `txFormat` is 0 text, 1 native, 2 text and then native (two frames per packet, a transition mode). A packet that cannot be encoded natively goes as text; native frames are capped at 255 bytes. The tracker adds no RXT of its own.
* **Where hybrid B plugs in:** the receive step (a new branch for every kind of `3C FF 03`, producing the same TNC2 text) and the transmit step (a new `txFormat` value, falling back to native or text when PPM does not shrink the content). The setting is stored in the tracker's JSON configuration and, I did not check, in its web page.
* **Not read:** its digipeater code, if any, and its message handling, which should not depend on the frame format because everything after receive sees text.

### K6ATV's iGate fork (`LoRa_APRS_iGate`, branch `feature/order3-compression`, ESP32 LoRa32 v2.1; no RXT, no native frames)

* **Receive** (`readFrame` in `src/lora_utils.cpp`): a compressed (`3C FF 81`) or primed (`3C FF C1`) frame is decoded, only when the radio library reports no CRC error, and handed on as a `3C FF 01` text frame, so everything after receive sees text. An unknown `3C FF xx` type is dropped and never shown as text (the check his tracker `devel` lacks).
* **Digipeater** (`src/digi_utils.cpp`): it works entirely on the TNC2 **text** path: `buildPacket` rewrites it with string operations (`WIDE1-1` as the first hop becomes its callsign plus the used mark; `WIDE2-n` handling in mode 2; mode 3 repeats when its callsign is in the path; a cross-frequency mode; `NOGATE` is not repeated). It then puts the text packet in the output buffer, and the transmit step re-encodes the **whole** packet (compressed if `compressTx`).
  So today a relay decodes the whole packet and encodes it again. For hybrid B the same path rules would have to run on the path decoded from the native front, with the content copied, not decoded and re-encoded.
* **Duplicates:** `isIn25SegHashBuffer(sender, payload)` drops a repeat of the same sender and payload within 25 seconds. With hybrid B a relay could use the source from the native front and the content CRC-16 as the key, without decoding the content (for PPM kinds it has decoded it once anyway, to verify).
* **Settings:** `compressTx` (on or off) and `compressFullEvery`; no `txFormat`. A `txFormat` like his tracker's (text, native, hybrid B, or both) would replace the boolean.
* **Where hybrid B plugs in:** `readFrame` (native `3C FF 03` decode, every kind), the digipeater (path rewrite on the native front), `transmitText` (a new format), and the configuration. The iGate-to-APRS-IS side needs no change, since it uploads the decoded TNC2 text.

### K6ATV's Direwolf port (`~/direwolf`, branch `feature/lora-cad-live`; the hill Raspberry Pi test bench, not a deployment target)

* **Receive** (`rx` thread in `src/loraspi.c`): `lorac_rx` (`src/loracompress.c`) expands a `3C FF 81` or `3C FF C1` frame to its TNC2 text, or reports a compressed frame it could not decode, which is dropped. The text is then parsed into an AX.25 packet (`ax25_from_text`) and handed to Direwolf's core (`dlq_rec_frame`), where the digipeater, the APRS decoder and the IGate work on that AX.25 object. The settings are `LORACOMPRESSRX` (on by default) and `LORACOMPRESS n` (a full frame at least every n-th packet; 0 or 1 means no primed frames).
* **Transmit:** the core hands the driver an AX.25 packet, which becomes TNC2 text, and `lorac_tx` picks the smallest of primed, full compressed or plain.
* **A relay therefore decodes and re-encodes the whole frame.** For PPM content this is harmless: the encoder is deterministic, so the same text with the same table and the same plain-or-PPM decision gives **byte-identical content**, and a relay that re-encodes produces what a relay that copies would. It costs only CPU. What does not survive the trip through Direwolf's core is anything that is not in the AX.25 object: the RXT block (it would need a side channel from receive to transmit, as the iGate's `RxtState` is), and addresses outside AX.25's limits (SSIDs above 15 are rejected at parse, which the native address element allows).
* **K6ATV-10's configuration** (`/home/walter/direwolf.conf` on the hill Pi, read from a filtered paste, login line removed): one service runs the LoRa channel and a VHF channel (RTL-SDR on 144.390 MHz). LoRa is channel 10 on a 433 MHz SPI hat, 433.775 MHz, SF12, BW 125 kHz, CR 4/5, sync word 0x12, 22 dBm, boosted receive gain, with the events log. It is an **iGate and a beacon, not a digipeater**: every `DIGIPEAT` line is commented out, `IGSERVER` is `noam.aprs2.net`, `IGTXVIA` is commented out (nothing from APRS-IS goes to RF), and two `PBEACON` lines send its own beacon over LoRa and to APRS-IS every 30 minutes.
  There is no `LORACOMPRESS` line, so it sends plain frames; `LORACOMPRESSRX` is on by default. As configured today this node needs only the **decoder**, but digipeating is a setting in Direwolf as in the ESP32 iGate (the `DIGIPEAT` lines are only commented out), and it also sends its own beacons, so a complete iGate profile needs the **encoder** as well; it has no relay path to change until digipeating is switched on.
* **Where hybrid B plugs in:** the `rx` step (a native decoder in place of `lorac_rx`, producing the same TNC2 text) and the transmit step (a native encoder). RXT and the destination table need work in the core's packet path; the table could live in the driver, since the destination is a field of the TNC2 text.

## Open

* **3.0.0-dev.1 proposals (5 November 2026), all of them proposed and needing F4MLV's agreement:** a separate frame type `3C FF 03` (the 2.0.0 frame is unchanged); the destination byte `c iiiiiii` with 128 fixed, append-only codes (code 126 stays reserved); `V = c ss mmmmm` (the model version replaces the protocol and table versions); plain frames ignore the model bits; **a CRC over the bytes as sent**, which lets a relay without the model check it; RXT 93 = not measured and values 90 to 127 unknown, frame kept; a relay without the model forwards the frame; four capability bytes (role with digipeater and iGate tiers, model, SF, BW, CR, **power in whole dBm**, preset, spare bits) flagged by bit `c`. Still open: placeholder names in the destination list and a request to the registry; the frequency-offset range (to be discussed with Jon, N7UV); how a sender chooses a model; presets beyond 2 to 4 (the UK and Poland frequencies are to be checked with local groups); how a receiver carries a missing RXT value through its interfaces and display. Possible later steps that do not change the frame (firmware segmentation for over-the-air updates, a library, where the firmware work lives) are in [docs/future-work.md](../docs/future-work.md).

* **The destination byte needs no disambiguation.** This plan defines the destination field of all four kinds as the code byte (escape plus an address element), and there is nothing to tell apart. Plain content is kinds 0 and 1 (decided 2 November 2026).
* **Versions: proposed change (3.0.0-dev.1).** `V = c ss mmmmm` is mandatory in every frame (1 byte; a plain frame adds 2 for the CRC): the capability flag, two spare bits and the model version. The protocol version leaves `V` (the frame type is the generation) and the table version becomes the model version, because the destination list is no longer versioned. In 2.0.0 `V = vvvvv ttt`. The plain/LACF form is in the header (kind bit 1), not in `V`. Proposed, needs F4MLV's agreement.
* **Header gap: closed in protocol version 2.** The content CRC covers the kind bits, `V`, source and destination, and plain content has its own CRC-16, so a flipped form or position bit fails the CRC like any other damage (tested: every such flip is dropped). The path count and the RXT bit are still not covered, because relays change them.
* **Distribute destination-list updates over the air.** The list is small (62 destinations are about 310 bytes, two frames, about 18 s at SF12), so a station that sees an unknown list version could ask its nearest neighbour, or an iGate could fetch and verify it and pass it on. The PPM tables (about 150 KB, 600 full frames) cannot go this way. Needs a version field, a signature from a key built into the firmware, rate-limited requests, and old versions kept. Not designed.
* **Upgrade path:** written up above, under "Upgrade path" (proposed, needs F4MLV's agreement).
* **The reserved destination bit `c`: decided to stay reserved** (3.0.0-dev.1; in 2.0.0 two bits). All compressed LoRa will use this format until a later version; capability is carried by the capability bytes, not by this bit. Senders set it to 0, receivers ignore it, and a future version can give it a meaning.
* **Kind values 2 and 3, or a new frame type.** F4MLV suggested the unused kinds himself; the issue also floated `01` text, `02` native, `03` capability, `80` to `FF` PPM. Older firmware that treats unknown kinds as errors will drop these frames, which must be checked in its code.
* **Frozen tables.** The experimental set (`mmmmm = 0`) is trained on one capture. The first format needs a frozen set trained on several networks, and a decision on who maintains it (append-only).
* **RXT at the end.** K6ATV's choice: one rule for every frame (a relay appends without moving bytes, and RXT is never inside the content). His current firmware puts the block after the path, so this needs F4MLV's agreement and a matching change in his firmware; frames from his current builds would not interoperate for RXT. Also **seven path elements instead of eight**, because of the marker bit.
* **Jon N7UV's view** on anything that changes what the tuple carries (the plan keeps the four measurements unchanged), and **F6BVP's** on identification: hybrid B keeps the source callsign recoverable in every frame, as F4MLV prefers.
* **`V` costs one byte per frame** (the model version, the capability flag and two spare bits; in 2.0.0 the protocol and table versions).
* **Mic-E** is forwarded unchanged (decided: not worth converting). Open: whether the destination-SSID path rule applies to a Mic-E destination.
* **The relay procedure** above is a draft: path rewriting in native elements (the index of the relay's element, moving mask bits when elements change), and time to hop at transmit time, need F4MLV's firmware to confirm.

## History

This format combines K6ATV's PPM compression with the native frame that Fabrice Morel, F4MLV (moricef) described in `docs/LORA_APRS_NATIVE_FRAME_DRAFT0.md` (`moricef/LoRa_APRS_iGate`, branch `rxt`, called draft 0 in issue 7 of moricef/LoRa_APRS_Tracker). That frame defined the address elements, the path and the position block, which this plan keeps (Conformance fixes every byte), and information kinds 0 and 1 (plain and position text). This plan keeps the kind numbers and the position-block bit, and gives kinds 0 and 1 a `V` byte and a CRC-16 (draft-0 frames of kinds 0 and 1 have neither, so a receiver of this plan drops them). It differs from draft 0 in using kinds 2 and 3, a content field with a version byte and CRC-16, a destination table, and the RXT block at the end of the frame instead of after the path. A draft-0 decoder rejects any kind above 1, so it drops these frames. An earlier whole-text frame (`3C FF 81`) in this repository was withdrawn.
