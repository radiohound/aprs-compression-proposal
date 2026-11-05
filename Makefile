CC ?= cc
CFLAGS ?= -std=c99 -O2 -Wall -Wextra -Werror -Ireference
SRC = reference/aprs_ppm.c model/aprs_ppm_model_v1.c

build/ppm_tool: test/ppm_tool.c $(SRC) reference/aprs_ppm.h reference/aprs_ppm_model.h
	@mkdir -p build
	$(CC) $(CFLAGS) test/ppm_tool.c $(SRC) -o $@

test: build/ppm_tool
	build/ppm_tool check test/vectors_v1.txt
	build/ppm_tool check-primed test/vectors_primed_v1.txt
	build/ppm_tool collide test/vectors_primed_v1.txt

build/trunc_ref: tools/integrity/trunc_ref.c $(SRC) reference/aprs_ppm.h reference/aprs_ppm_model.h
	@mkdir -p build
	$(CC) $(CFLAGS) tools/integrity/trunc_ref.c model/aprs_ppm_model_v1.c -o $@

build/primed_c_test: tools/integrity/primed_c_test.c $(SRC) reference/aprs_ppm.h reference/aprs_ppm_model.h
	@mkdir -p build
	$(CC) $(CFLAGS) tools/integrity/primed_c_test.c model/aprs_ppm_model_v1.c -o $@

# truncation and corruption of full frames with check C, on fabricated packets (see tools/integrity/README.txt)
integrity: build/trunc_ref
	build/trunc_ref

build/lacf_ending: tools/integrity/lacf_ending.c $(SRC) reference/aprs_ppm.h reference/aprs_ppm_model.h
	@mkdir -p build
	$(CC) $(CFLAGS) tools/integrity/lacf_ending.c model/aprs_ppm_model_v1.c -o $@

# the LACF ending and the receiver's re-encode check (spec/11, PPM payload rules), checked against its vectors and on fabricated packets
lacf-ending: build/lacf_ending
	build/lacf_ending check test/vectors_lacf_v1.txt
	build/lacf_ending

build/fast_freq_test: tools/integrity/fast_freq_test.c tools/integrity/fast_freq.h $(SRC) reference/aprs_ppm.h reference/aprs_ppm_model.h
	@mkdir -p build
	$(CC) $(CFLAGS) tools/integrity/fast_freq_test.c model/aprs_ppm_model_v1.c -o $@

# the faster build_freq (tools/integrity/fast_freq.h) must equal the reference's output; needs a text file: make fast-freq TEXTS=texts.txt (fabricated: test/synthetic_packets.txt)
fast-freq: build/fast_freq_test
	build/fast_freq_test $(or $(TEXTS),test/synthetic_packets.txt)

lacf-vectors: build/lacf_ending
	cat test/synthetic_packets.txt test/lacf_edge_packets.txt | build/lacf_ending gen > test/vectors_lacf_v1.txt

vectors: build/ppm_tool
	build/ppm_tool gen < test/synthetic_packets.txt > test/vectors_v1.txt
	build/ppm_tool gen-primed < test/synthetic_primed_pairs.txt > test/vectors_primed_v1.txt

# spec 11 reference frame (tools/hb): build, vectors, fault injection on generated packets
build/hb_test: tools/hb/hb.c tools/hb/hb.h tools/hb/hbi.c tools/hb/hbc.c tools/hb/hb_payload.h tools/hb/hb_test.c $(SRC)
	@mkdir -p build
	$(CC) $(CFLAGS) -c tools/hb/hbi.c -o build/hbi.o
	$(CC) $(CFLAGS) -c tools/hb/hbc.c -o build/hbc.o
	$(CC) $(CFLAGS) -Itools/hb -c tools/hb/hb.c -o build/hb.o
	$(CC) $(CFLAGS) -Itools/hb tools/hb/hb_test.c build/hb.o build/hbi.o build/hbc.o -o $@

hb-test: build/hb_test
	build/hb_test check test/vectors_hb_v1.txt
	build/hb_test relaycheck test/vectors_hb_relay_v1.txt
	build/hb_test relaycheck test/vectors_hb_relay_chain_v1.txt
	build/hb_test relaycheck test/vectors_hb_relay_edge_v1.txt
	build/hb_test relaytocheck test/vectors_hb_relay_v1.txt
	build/hb_test relaytocheck test/vectors_hb_relay_chain_v1.txt
	build/hb_test relaytocheck test/vectors_hb_relay_edge_v1.txt
	@n=$$(grep -v '^#' test/vectors_hb_reject_v1.txt | awk '{print $$1}' | build/hb_test recv | grep -vc '^-$$'); echo "RXT rejection vectors: $$n accepted (must be 0)"; test "$$n" = 0
	python3 tools/hb/gen_packets.py 3000 > build/hb_packets.txt
	build/hb_test props build/hb_packets.txt

hb-vectors: build/hb_test
	python3 tools/hb/gen_packets.py 500 > test/hb_packets.txt
	grep -v '^#' test/hb_edge_packets.txt >> test/hb_packets.txt
	build/hb_test gen < test/hb_packets.txt > test/vectors_hb_v1.txt
	python3 tools/hb/gen_reject.py > test/vectors_hb_reject_v1.txt
	( echo "# Spec 11 relay vectors, first hop: <received frame hex> <relayed frame hex> <relayed TNC2 hex>. Relay N0RLY-1, tuple bytes 50 20 45 10, rules in spec 11 Conformance."; build/hb_test relaygen < test/hb_packets.txt ) > test/vectors_hb_relay_v1.txt
	( echo "# Spec 11 relay vectors, second and later hops (the received frame already carries k tuples): <received frame hex> <relayed frame hex> <relayed TNC2 hex>. Relay N0RLY-(k+1), tuple bytes 50+k 20+k 45+k 10+k, rules in spec 11 Conformance."; build/hb_test relaychain < test/hb_packets.txt ) > test/vectors_hb_relay_chain_v1.txt
	python3 tools/hb/gen_relay_edge.py > test/vectors_hb_relay_edge_v1.txt

# spec 12, the proposal 2.1.0-dev.1 (frame type 3C FF 03; the 2.0.0 frame above is untouched): build, vectors, fault injection on generated packets
build/hb21_test: tools/hb21/hb21.c tools/hb21/hb21.h tools/hb/hbi.c tools/hb/hbc.c tools/hb/hb_payload.h tools/hb21/hb21_test.c $(SRC)
	@mkdir -p build
	$(CC) $(CFLAGS) -c tools/hb/hbi.c -o build/hbi.o
	$(CC) $(CFLAGS) -c tools/hb/hbc.c -o build/hbc.o
	$(CC) $(CFLAGS) -Itools/hb21 -c tools/hb21/hb21.c -o build/hb21.o
	$(CC) $(CFLAGS) -Itools/hb21 tools/hb21/hb21_test.c build/hb21.o build/hbi.o build/hbc.o -o $@

hb21-test: build/hb21_test
	build/hb21_test check test/vectors_hb21_v1.txt
	build/hb21_test relaycheck test/vectors_hb21_relay_v1.txt
	build/hb21_test relaycheck test/vectors_hb21_relay_chain_v1.txt
	build/hb21_test relaycheck test/vectors_hb21_relay_edge_v1.txt
	build/hb21_test relaytocheck test/vectors_hb21_relay_v1.txt
	build/hb21_test relaytocheck test/vectors_hb21_relay_chain_v1.txt
	build/hb21_test relaytocheck test/vectors_hb21_relay_edge_v1.txt
	build/hb21_test capcheck test/vectors_hb21_cap_v1.txt
	build/hb21_test passcheck test/vectors_hb21_passthrough_v1.txt
	@n=$$(grep -v '^#' test/vectors_hb21_reject_v1.txt | awk '{print $$1}' | build/hb21_test recv | grep -vc '^-$$'); echo "rejection vectors: $$n accepted (must be 0)"; test "$$n" = 0
	python3 tools/hb/gen_packets.py 3000 > build/hb_packets.txt
	build/hb21_test props build/hb_packets.txt

hb21-vectors: build/hb21_test
	python3 tools/hb/gen_packets.py 500 > test/hb_packets.txt
	grep -v '^#' test/hb_edge_packets.txt >> test/hb_packets.txt
	build/hb21_test gen < test/hb_packets.txt > test/vectors_hb21_v1.txt
	( echo "# Spec 12 relay vectors, first hop: <received frame hex> <relayed frame hex> <relayed TNC2 hex>. Relay N0RLY-1, tuple bytes 50 20 45 10."; build/hb21_test relaygen < test/hb_packets.txt ) > test/vectors_hb21_relay_v1.txt
	( echo "# Spec 12 relay vectors, second and later hops (the received frame already carries k tuples): <received frame hex> <relayed frame hex> <relayed TNC2 hex>. Relay N0RLY-(k+1), tuple bytes 50+k 20+k 45+k 10+k."; build/hb21_test relaychain < test/hb_packets.txt ) > test/vectors_hb21_relay_chain_v1.txt
	python3 tools/hb21/gen_relay_edge.py > test/vectors_hb21_relay_edge_v1.txt
	python3 tools/hb21/gen_reject.py > test/vectors_hb21_reject_v1.txt
	python3 tools/hb21/gen_cap.py < test/hb_packets.txt > test/vectors_hb21_cap_v1.txt
	python3 tools/hb21/gen_passthrough.py < test/hb_packets.txt > test/vectors_hb21_passthrough_v1.txt

clean:
	rm -rf build
.PHONY: test vectors clean integrity lacf-ending lacf-vectors fast-freq hb-test hb-vectors hb21-test hb21-vectors
