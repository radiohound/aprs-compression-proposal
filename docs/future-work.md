# Possible later steps around 3.0.0

*A short list of things that may follow if 3.0.0 is adopted. None of it is part of the 3.0.0-dev.1 proposal, none of it is decided or built, and none of it changes a byte of the frame ([spec 12](../spec/12-lacf-3.0-dev-byte-plan.md)). It is written down so the choices made for 3.0.0 do not close these doors by accident. K6ATV, 7 November 2026.*

## 1. Firmware segmentation, for possible over-the-air updates

**The idea.** Split a device's flash so that the pieces that change at different speeds can be replaced separately: the application, the PPM model tables, and later perhaps the codec itself. A new model (32 are possible in `V`) could then reach a device as a small signed update instead of a full firmware image, and an old hilltop digipeater could learn a new model without a visit with a USB cable.

**Why it is mentioned with 3.0.0.** The partition table of an ESP32 cannot be changed over the air: a new one needs a USB flash. 3.0.0 already asks every device to be reflashed once (a new frame type). If the layout is going to change at all, doing it in that same visit costs one trip; doing it later costs a second round, including the devices that are hardest to reach.

**What would have to be decided first.** How many model sets a device holds (spec 12 says a firmware keeps every released model, which a split layout may change to the newest and the previous); a size limit for one model set, so the smallest board still fits; the header of a model partition (type, version, length, hash, signature); which key signs updates and how it is rotated; and a small, versioned interface between the application and the codec. How a model or an update reaches a device over the air is a separate question and is not covered here.

**Where it stands.** Nothing is implemented, and the partition layout waits until the on-air tests are done and the specs are concrete.

## 2. A library for LACF

**The idea.** Today the codec (`hb3.c`, and `hb.c` for 2.0.0) is copied into each firmware by hand, so copies can drift, and anyone who wants LACF in their own device has to read the spec and the reference line by line. A small library would carry the frame code, the CRC, the PPM payload coding, relay (with and without the model), the capability bytes and the test vectors, behind a stable interface of about ten functions.

**Shape.** A C99 core with no malloc and no operating-system dependency; the caller supplies the work memory (today the codec uses about 10 KB of stack, which is why the firmware runs it in a 16 KB task); the model tables are passed in by pointer so they can later live in a flash partition (see 1); explicit sizes and error codes. The library version would follow the frame type: 3.x is type 03 and includes the 2.0.0 decoder. Its own repository, tagged 0.x until the spec is adopted, MIT licensed, with the vectors and a test runner included.

**Why not now.** An interface frozen before the specs settle would have to be broken again. The order would be: finish the on-air tests, refactor the reference behind the new interface (the vectors stay the check), replace the copies in the firmwares, publish a 0.x tag. Ownership, credit and review would be agreed with F4MLV first.

## 3. Where the firmware work lives

The iGate and tracker firmware used for the LACF tests began as forks of CA2RXU's LoRa APRS iGate and Tracker, and the Raspberry Pi software has its own repository. The LACF branches have since grown away from that code: a second frame decoder, a relay without the model, capability bytes and a neighbour table, and a different layout of the source (a codec library, a worker task). Small, separable changes have been sent to the upstream repositories as pull requests and will keep being sent where they stand alone.

**What could reasonably be asked of upstream.** Not to adopt the LACF changes as a whole, which would be a lot to ask of a project with its own direction and users. The smaller request is the library (2):

* **The library first.** One tagged, MIT-licensed dependency, with its vectors and tests, that can sit behind a build option that is off by default, so users of the stock firmware see nothing unless they turn it on. MIT code can be included in GPL-3 firmware.
* **Then, only if upstream is willing, a few clearly marked hook points** where the firmware calls the library: where a received frame is decoded, where a packet is digipeated, where a frame is sent. The rest of the LACF firmware (the settings page, the neighbour table, the capability schedule) stays outside upstream unless upstream wants it.
* **If upstream would rather not carry even that,** the LACF firmware remains a separate line kept up to date with upstream. Because the codec would then be the shared library and not a copy, what has to be kept in sync is small.

This is a question for CA2RXU before it is a plan: it affects his project and his users, and the answer may well be "the library yes, the hooks not yet" or no. The code stays under its original licence with the original credit either way. No decision has been made, and nothing here is needed for 3.0.0.

## What 3.0.0 has to keep open for these

* The model version field has room (32 models); a firmware may hold more than one model.
* The type byte is the generation, so a library or a split firmware can carry both decoders.
* A relay without the model works on a frame it cannot decode, so a device that has not received a new model yet can still pass the frame on.

## 4. A crossband bridge announcement

**The idea.** A gateway that brings VHF AFSK packets (for example from 144.390 MHz) onto LoRa could announce that it does. This is not a frequency ID: the frequency ID and the SF, BW and CR fields describe a station's LoRa receive profile (F4MLV, 9 November 2026), and AFSK has none of them. It would be a separate item, for example a flag in the spare capability bits, with its own definition: the direction, the band, and what happens to the original packet's signal data. A crossband relay writes 93 (not measured) for what it cannot measure on the VHF hop.

**Where it stands.** Not part of the proposal and not decided. 144.390 MHz was removed from the frequency IDs.
