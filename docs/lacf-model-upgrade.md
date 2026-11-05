# What a new PPM model does to a mixed network

*2.1.0-dev.1 (frame type `3C FF 03`, [spec 12](../spec/12-lacf-2.1-dev-byte-plan.md)): a proposal, not adopted; it needs F4MLV's agreement. Tables only; a diagram (stations as boxes, one arrow per row: green decodes, red dropped, amber the sender's choice or a pass-through) is still to be drawn.*

**In one sentence:** a new PPM model never breaks old stations by itself. Old stations are cut off only from frames a sender **chooses** to encode with the new model; relays that have "Relay without the model" pass those frames on; old iGates cannot gate them. Plain frames and older models work everywhere, forever.

"Old" means a station that holds model N; "new" means one that holds N and N+1. The model is the low five bits of `V` ([spec 12](../spec/12-lacf-2.1-dev-byte-plan.md)). The 2.0.0 frame (type `02`, spec 11) is a different frame type: a station that holds both decoders reads either, and a 2.0.0-only station never sees a type `03` frame.

## A. Direct reception

| Sender | Frame sent | Old receiver | New receiver |
|---|---|---|---|
| old | plain | decodes | decodes |
| old | LACF, model N | decodes | decodes |
| new | plain | decodes (the model bits are ignored for plain content) | decodes |
| new | LACF, model N | decodes | decodes |
| new | LACF, model N+1 | **drops** (cleanly, never misreads) | decodes |

The only broken arrow is new to old with model N+1, and it is the sender's choice. It would be broken both ways only if a firmware dropped model N, which the spec forbids (firmware keeps every released model).

## B. Relays (digipeaters)

| Relay | Frame received | Result |
|---|---|---|
| old, without "Relay without the model" | LACF, model N+1 | drops it: a wall |
| old, with it (firmware from the release that adds it) | LACF, model N+1 | checks the CRC (it is over the bytes as sent), then passes it on; does not show or use it |
| new | any | checks and relays |

A model N+1 frame crosses a digipeater that has the pass-through and is decoded by any new station downstream. **Only firmware that already has the pass-through when the first new model ships benefits.** Dire Wolf cannot do it (it re-encodes every frame through AX.25).

## C. iGates (RF to APRS-IS)

| iGate | Frame received | Result |
|---|---|---|
| old | LACF, model N+1 | cannot decode, so cannot gate: the position does not reach APRS-IS through this iGate |
| new | LACF, model N+1 | gates it |

This is the most visible effect of a rollout: gaps on aprs.fi wherever only old iGates hear a new-model sender. That is why iGates upgrade first.

## D. APRS-IS to RF, and messages

A new iGate transmitting to RF follows the sender rule (model N or plain while old stations may be listening). Replies from old stations always work.

## What changes the frame itself

A change to the **front** of the frame (header, addresses, path, RXT block, position block), or to what the destination byte or `V` mean, is a MAJOR change with a new frame type byte. That is what type `03` is, relative to the 2.0.0 type `02`. Old firmware drops a type it does not know within the first three bytes, and a relay cannot forward it, because it does not know where anything is. A model change touches only the content field, which is why a relay can still pass it on.

## What a relay without the model can and cannot check

The CRC-16 is taken over the bytes as sent (the kind bits, `V`, the capability bytes, the source and destination bytes, the position block, `L` and the payload), not over the decoded text, so **a relay that does not hold the model can check it**. It refuses a frame damaged on the channel or cut before transmission, as any station would.

What it cannot check is that the payload is a canonical encoding under the model, and that the text is sensible. That protects against a sender that stamps the wrong model or encodes wrongly, and only a station that holds the model can check it. Such a frame would cross every such relay until a station with the model drops it, so it can travel several hops (the path limits how many; WIDEn-N allows one or two in normal use). It is never shown or gated as wrong data, because every station that decodes it makes the check. A CRC-16 detects errors with a 1 in 65,536 chance of missing one; it is error detection and not an absolute guarantee, and LoRa's own CRC checks every hop as well.
