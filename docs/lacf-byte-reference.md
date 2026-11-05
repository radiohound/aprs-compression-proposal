# LACF byte and bit reference

What every byte and bit in the [frame picture](lacf-frame-layout.md) means and what it does. LACF (LoRa APRS Compact Frame, [spec 11](../spec/11-native-ppm-frame-byte-plan.md)) is a draft in early development and still being reviewed for change, so a value here can change. The spec is the authority; where this sheet and the spec differ, the spec wins. The bytes of the examples come from the reference code (`tools/hb`).

**Notation.** Bits are numbered 7 (the most significant) down to 0. Bytes are written in hex (`3C`). `0b` marks binary. "Big endian" means the most significant byte is sent first; the CRC is the one field sent low byte first. A receiver that meets a value marked "dropped" throws the whole frame away and never shows it as text.

## 1. The frame, in the order sent

| Part | Size | Needed |
|---|---|---|
| [Marker](#2-marker) | 3 B | always |
| [Header](#3-header-byte) | 1 B | always |
| [Source](#4-address-elements-source-destination-and-path) | 5 B typical | always |
| [Destination](#42-the-destination-byte) | 1 B (or 1 + 5 B) | always |
| [Path elements](#4-address-elements-source-destination-and-path) | 0 to 15, 1 B or 5 B each | as the header says |
| [Position block](#5-position-block-kind-3-only) | 9 B typical | kind 3 only |
| [Content field](#6-content-field) | V, L, CRC-16, payload | always |
| [RXT block](#7-rxt-block-added-by-relays) | 4 B per hop + 1 | only if header bit 3 is set |

The whole frame is at most 255 bytes. A packet that does not fit is sent as an ordinary plain frame.

## 2. Marker

| Byte | Value | Meaning |
|---|---|---|
| 1 | `3C` | the character `<`, the start of every LoRa APRS frame |
| 2 | `FF` | the second byte of the LoRa APRS prefix |
| 3 | `02` | the frame type. `01` = plain text (every ordinary LoRa APRS frame), `02` = the native frame, which is LACF, `03` = a capability frame (suggested, not defined), `80` to `FF` = the earlier whole-text compressed frames |

Firmware that does not know type `02` ignores the frame, and a receiver drops any `3C FF` type it does not know instead of showing it as text. That is why a plain-text station does not see junk.

## 3. Header byte

One byte, bit by bit:

| Bits | Name | Values | What it does |
|---|---|---|---|
| 7-4 | path count | 0 to 15 | how many path elements follow the destination |
| 3 | RXT flag | 0 = none, 1 = an RXT block ends the frame | a relay sets it when it appends its measurement |
| 2 | reserved | must be 0 | a frame with this bit set is dropped |
| 1 | form | `1` = LACF (PPM-coded content); `0` = plain text, with a CRC | kinds 0 (plain) and 1 (plain with a position block) are the plain form; 2 and 3 are LACF |
| 0 | position block | `1` = a position block follows the path and the content is only the comment; `0` = none, the content is the whole information field | |

Examples: `23` = `0010 0011`: 2 path elements, no RXT block, kind 3. After a relay hop the header is `2B` = `0010 1011`: the same, with the RXT flag set.

The path count and the RXT flag are the two header bits a relay changes. The CRC does not cover them, so a relay does not recompute it.

## 4. Address elements (source, destination and path)

Source, destination escape and path elements are all *address elements*. The first bits of the first byte tell which of three forms it is.

### 4.1 The three forms

| Form | First byte | What follows | Used for |
|---|---|---|---|
| **WIDE** | `0 s nnn NNN` | nothing (1 byte in all) | `WIDEn` and `WIDEn-N`, where n is 1 to 7 and N is 1 to n |
| **Callsign** | `1 0 s xxxxx` | 4 bytes of packed callsign, then 1 extra byte only when xxxxx is 31 | a callsign of 1 to 6 characters A-Z and 0-9, with an optional SSID and star |
| **Text** | `1 1 lllll` | l bytes of text (1 to 31) | anything else: lower case, longer names, tactical names |

In every form `s` is the used mark: `s = 1` means the element had a `*` (the packet has already used it).

**WIDE byte `0 s nnn NNN`**: bit 7 is 0; bit 6 is `s`; bits 5-3 are `n`; bits 2-0 are `N` (0 when there is no `-N`). `09` = `0 0 001 001` = `WIDE1-1`, `12` = `0 0 010 010` = `WIDE2-2`. A byte with n = 0, or N greater than n, is dropped, and so is any path element that reads as a `WIDEn-N` breaking the APRS rule (`WIDE1-2`, `WIDE2-3`, `WIDE8-1`): such packets are not carried and not repeated.

**Callsign type byte `1 0 s xxxxx`**: bits 7-6 are `10`; bit 5 is `s`; bits 4-0 are the SSID, 0 to 30. The value 31 means the real SSID follows as one extra byte after the callsign (it must be 31 or more, else dropped). `89` = `1 0 0 01001` = SSID 9. `A1` = `1 0 1 00001` = SSID 1 and used: `N0RLY-1*`.

**The four callsign bytes** hold the six characters (padded with spaces) as one big-endian number in base 40. The alphabet has 37 symbols: space = 0, `A` to `Z` = 1 to 26, `0` to `9` = 27 to 36 (37 to 39 are not used and are dropped). The first character is the most significant digit. Example: `N0CALL` is N = 14, 0 = 27, C = 3, A = 1, L = 12, L = 12, so 14 x 40^5 + 27 x 40^4 + 3 x 40^3 + 1 x 40^2 + 12 x 40 + 12 = 1,502,914,092 = `59 94 A6 2C`. A value of 40^6 or more is dropped, as is an empty callsign or one with a space inside it.

**Text element `1 1 lllll`**: bits 7-6 are `11`; bits 4-0 are the length l (1 to 31; 0 is dropped), then l bytes of text. A text element may not contain `:`, `,`, `>` or a zero byte (dropped), because the rebuilt packet text would split in the wrong place. The used mark is just a `*` inside the text.

### 4.2 The destination byte

The destination is one byte, `ff iiiiii`:

| Bits | Name | What it does |
|---|---|---|
| 7-6 | `ff`, reserved | a sender sets them to 0 and a receiver ignores them, so a later version can give them a meaning; a relay copies them unchanged |
| 5-0 | `iiiiii`, the code | 0 to 61 name the 62 most common destinations (device or software ids) of the table version; 62 is reserved (dropped); 63 is the escape: an address element follows (so 6 bytes in all) |

Code 0 of table version 0 is `APLRG1` (the LoRa APRS iGate), so the byte `00` means `APLRG1`. A destination that is not in the list is sent as `3F` followed by an address element. A receiver also accepts the escape form for a destination that is in the list.

### 4.3 The source and the path

The **source** is one address element (5 bytes for an ordinary callsign). The **path** is as many address elements as the header's path count says. In the example: source `89 59 94 A6 2C` is `N0CALL-9`; path `09` is `WIDE1-1` and `12` is `WIDE2-2`. After the relay hop the first path element is `A1 59 A3 92 E8`, the relay's callsign `N0RLY-1*` (used), which is 4 bytes longer than the `09` it replaced.

## 5. Position block (kind 3 only)

When the information field starts with a position, the encoder sends the position exactly, instead of as text. The block is, in order: a flags byte, an optional timestamp, the position, the table and symbol characters, and optional course/speed and altitude.

### 5.1 The flags byte

| Bits | Name | What it does |
|---|---|---|
| 1-0 | data type | 0 = `!`, 1 = `=`, 2 = `/`, 3 = `@` (the APRS data type indicator) |
| 2 | timestamp | 1 = a 3-byte timestamp follows the flags (only for `/` and `@`) |
| 3 | compressed | 1 = a compressed position (7 bytes), 0 = an uncompressed one (6 bytes) |
| 4 | course/speed | 1 = 3 bytes of course and speed follow the symbol (uncompressed positions only; not together with bit 3) |
| 5 | altitude | 1 = 4 bytes of altitude follow |
| 6 | compressed, no cs | with bit 3: the compressed `c s` bytes were both spaces, so only the `T` byte follows (1 byte instead of 3) |
| 7 | reserved | must be 0 (dropped) |

The example's flags byte is `00`: data type `!`, no timestamp, uncompressed, no course/speed, no altitude.

### 5.2 The parts, in order

| Part | Size | How it is coded |
|---|---|---|
| Timestamp | 3 B | the six digits as a number times 4, plus 0, 1 or 2 for `z`, `h`, `/` (3 is dropped) |
| Uncompressed position | 6 B | (latitude in hundredths of a minute x 2, plus 1 for south) x 2^22 + (longitude in hundredths of a minute x 2, plus 1 for west) |
| Compressed position | 7 B | the four base-91 latitude characters as a number x 2^27 + the four longitude characters |
| Table and symbol | 2 B | the APRS symbol table character and the symbol character, as they are |
| Compressed `T` or `c s T` | 1 B or 3 B | the compressed position's extra bytes |
| Course and speed | 3 B | course x 1024 + speed, each 0 to 999 |
| Altitude | 4 B | 1 byte: where in the comment the `/A=` was; 3 bytes: the altitude in feet as a 24-bit two's complement number (-99999 to 999999) |

**The example, decoded.** The block is `00 | 01 B5 F8 16 55 EB | 2F | 3E`:

- Flags `00` as above.
- `01 B5 F8 16 55 EB` as one number is 1,881,062,921,707. Divided by 2^22 it gives a latitude field of 448,480: that is 224,240 hundredths of a minute with the south bit 0, so **37 degrees 22.40 minutes North**. The remainder is a longitude field of 1,463,787: 731,893 hundredths of a minute with the west bit 1, so **121 degrees 58.93 minutes West**.
- `2F` is the table character `/`; `3E` is the symbol character `>`.

Together they give the text `!3722.40N/12158.93W>`. The remaining text of the packet, the comment `LoRa APRS iGate on the hill`, is in the content field.

A receiver drops a block that no encoder could have produced: a timestamp above 999999, a latitude above 90 degrees, a longitude above 180 degrees, a course or speed above 999, an altitude out of range, or an altitude offset beyond the comment.

## 6. Content field

The content field carries the text: the whole information field in kinds 0 and 2, or the comment that is left after the position block in kinds 1 and 3. It starts with the version byte `V`.

### 6.1 V, the version byte

| Bits | Name | Values | What it does |
|---|---|---|---|
| 7-3 | `vvvvv`, protocol version | 0 to 31 (2 is the only one defined; the test builds that sent `01`, `02`, `C1`, `C2` are not held and are dropped) | names the frame layout and the content rules the sender used. A receiver that does not hold it drops the frame |
| 2-0 | `ttt`, table version | 0 to 7 (0 is the only one defined) | names the destination list and the pair of LACF tables, revised together. A receiver that does not hold it drops the frame |

Whether the content is LACF or plain is in the header (bit 1 of the kind), not in `V`.

### 6.2 LACF content (kinds 2 and 3)

| Part | Size | What it is |
|---|---|---|
| V | 1 B | as above |
| L | 1 B | the number of text characters coded, 1 to 255 (0 is dropped) |
| CRC-16 | 2 B | a check on the text and addresses, sent **low byte first** |
| PPM payload | n B | the text, coded with the table version; the length n is not sent, the receiver works it out |

**The CRC** is CRC-16/X-25 (polynomial 0x1021 reflected, initial value 0xFFFF, result inverted). It is computed over, in this order: the header byte AND 0x07 (the reserved bit and the kind bits), V, the source as text, `>`, the destination as text, `:`, and the information field. It does not cover the path or the path count and RXT flag, which relays change, so a relay never recomputes it. A receiver that has the wrong table set, or a damaged source, destination or V, fails it.

In the example `B1 A6` is the CRC `0xA6B1`, `10` is V (protocol version 2, table version 0) and `1B` is L = 27, the length of `LoRa APRS iGate on the hill`.

**The payload** is the range-coder output of spec 01, which codes each character by how likely it is after the three before it, using the table version. The sender ends the payload in a fixed way (the "deterministic ending", on average 0.25 B more) so that a receiver can check it: it decodes, codes the text again, and **drops the frame unless the result matches the payload byte for byte**. A cut or damaged payload almost never survives that. The example's payload is the 10 bytes `93 AF 55 0A 8E 18 E2 07 49 5B`, which stand for the 27 characters.

### 6.3 Plain content (kinds 0 and 1)

When PPM would not make the text shorter (the rule is that PPM must satisfy 4 + n < 3 + L), the sender uses plain content: `V`, then the 2-byte CRC (the same CRC-16 over the same input, low byte first), then the text itself, up to the RXT block or the end of the frame. There is no L. The CRC is what rejects a plain frame that was cut before transmission (protocol version 2; before it, plain text was protected only by the LoRa CRC).

### 6.4 What may not be in the text

A zero byte anywhere in the decoded text is dropped. The CRC covers the whole text, not just up to a zero byte.

## 7. RXT block (added by relays)

RXT carries what each relay heard. A relay appends it at the end of the frame, after the content, and sets header bit 3; it never touches the content. RXT was created by Jon, N7UV; the layout in this frame is described in spec 11.

```
tuple 1 | tuple 2 | ... | tuple k | mask        (4k + 1 bytes; the last byte of the frame is the mask)
```

### 7.1 The tuple (4 bytes, one per relay)

| Byte | Field | Range | What it holds |
|---|---|---|---|
| 1 | RSSI | 0 to 89 | the relay's received signal strength: -130 dBm up to -41 dBm in 1 dB steps |
| 2 | SNR | 0 to 88 | the signal to noise ratio: -24 dB up to +20 dB in 0.5 dB steps |
| 3 | frequency offset | 0 to 89 | how far the sender's frequency was from the relay's, roughly -2500 to +2500 Hz, on a non-linear scale (the middle of the range is near 0 Hz) |
| 4 | time to hop | 0 to 89 | the time from the end of reception to the end of the relay's own transmission, on an exponential scale based on the symbol time; it includes queueing, channel access and the time on air |

The ranges are the RXT characters minus 33, as defined in F4MLV's RXT description (`moricef/LoRa_APRS_iGate`, branch `rxt`). A tuple byte above the maximum for its field is dropped. The example tuple `32 14 2D 0A` is the test vector (RSSI 50, SNR 20, frequency offset 45, time to hop 10), which would be about -80 dBm and -14 dB; it is not a real measurement.

### 7.2 The mask byte

| Bit | Name | What it does |
|---|---|---|
| 7 | marker | always 1. A tuple byte is never 0x80 or more, so after a cut inside the block the last byte can never pass for a mask |
| 6-0 | path elements | bit j = 1 means path element j (0 is the first element after the destination) has a tuple. Tuples are in ascending order of j |

The mask can only name path elements 0 to 6, so at most 7 elements carry a tuple. A mask with bits set for elements that do not exist, a mask without the marker, or fewer tuples than the mask names, is dropped. In the example the mask `81` = `1000 0001`: the marker and bit 0, so one tuple, belonging to path element 0 (the relay `N0RLY-1*`). Each relay adds 4 bytes, and the first relay also adds the mask byte, so a relayed frame grows by 5 bytes for the first relay and 4 for each further one.

## 8. The two example frames, byte by byte

The 35-byte frame and the 44-byte frame after one relay hop are shown byte by byte, with the same colours as the picture, in [lacf-frame-layout.md](lacf-frame-layout.md).

## 9. What a receiver drops

A receiver drops the frame, silently, for any of: a marker it does not know; header bit 2 set; a reserved destination code (62); a protocol or table version it does not hold; an element that no encoder could produce (see section 4); a position block that no encoder could produce (section 5); an RXT tuple or mask that is impossible (section 7); a PPM payload that does not re-encode to the same bytes; or a CRC that does not match. It never shows such a frame as text and never passes it on.

## Credit

LACF is developed together with Fabrice Morel, F4MLV (moricef): it combines his native LoRa APRS frame and code with K6ATV's PPM compression. RXT was created by Jon, N7UV.
