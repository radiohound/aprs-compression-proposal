# What a new PPM model does to a mixed network

*2.1.0-dev.1: a proposal, not adopted; it needs F4MLV's agreement. Tables only; the diagram in the style of [`img/lacf-frame-layout.svg`](img/lacf-frame-layout.svg) (stations as boxes, one arrow per row: green decodes, red dropped, amber the sender's choice or a pass-through) is still to be drawn.*

**In one sentence:** a new PPM model never breaks old stations by itself. Old stations are cut off only from frames a sender **chooses** to encode with the new model; relays that have "Relay without the model" pass those frames on; old iGates cannot gate them. Plain frames and older models work everywhere, forever.

"Old" means a station that holds model N; "new" means one that holds N and N+1. The model is the low five bits of `V` ([spec 11](../spec/11-native-ppm-frame-byte-plan.md)).

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
| old, with it (firmware from the release that adds it) | LACF, model N+1 | passes it on unchecked; does not show or use it |
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

A change to the **front** of the frame (header, addresses, path, RXT block, position block) is a different matter: it is a MAJOR change with a new frame type byte (`3C FF 03`). Old firmware drops it within the first three bytes, and a relay cannot forward it, because it does not know where anything is. A model change touches only the content field, which is why a relay can still pass it on.

## Why a relay may forward what it cannot check

LoRa's own CRC catches damage on every hop. What the content CRC adds is protection against upstream bugs (for example a frame cut before transmission). A relay that cannot decode the content cannot check that CRC, so it forwards the frame without showing, uploading or using it, and every station that does decode it checks the CRC first. The frame may travel one extra hop, but it can never be shown or gated as wrong data.
