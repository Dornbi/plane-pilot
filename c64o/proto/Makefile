# Host build of the AoA flight model prototype. Nothing here is in the game
# build - c64o/Makefile does not list it, and it links the shipping flight.cc
# rather than modifying it.
#
#   make run          the full set of sweeps
#   make run S=level  one section: wing level turns stall takeoff glide dive induced

HOST_CC = g++
# The prototype links the shipping flight.cc for its comparison column, and
# wants the angle-of-attack build of it.
HOST_CFLAGS = -O2 -std=c++17 -Wall -Wno-unknown-pragmas -I.. -D__FLIGHT_AOA__

PROTO_SRC = aoa_proto.cc aoa.cc \
		../flight.cc ../mission.cc ../world_map.cc ../vec.cc ../vec_lut.cc \
		../fmath.cc ../msg.cc
PROTO_HDRS = aoa.h ../flight.h ../mission.h ../vec.h ../fmath.h ../msg.h

S =

all: aoa_proto

aoa_proto: $(PROTO_SRC) $(PROTO_HDRS)
	$(HOST_CC) $(HOST_CFLAGS) -o aoa_proto $(PROTO_SRC)

run: aoa_proto
	./aoa_proto $(S)

clean:
	rm -f cycles_probe.prg cycles_probe.asm cycles_probe.map cycles_probe.lbl cycles_probe.int cycles_probe.dbj cycles_probe.dump.bin size_probe.prg size_probe.asm size_probe.map size_probe.lbl size_probe.int size_probe.dbj
	rm -rf aoa_proto *.dSYM

.PHONY: all run size cycles clean

# --- Target code size ------------------------------------------------------
#
# The model built for the 6510, so "what would this cost to land" is a
# measurement. Skips itself where oscar64 is not installed, the way
# test/Makefile does.
OSCAR64 ?= oscar64
OSCAR64_INCLUDE ?= ../../../../oscar64-main/include
# -D__FLIGHT_AOA__ because cycles_probe.cc times the angle-of-attack model;
# without it flight.cc compiles the arcade one and the probe measures that.
TARGET_CFLAGS = -ii=$(OSCAR64_INCLUDE) -D__OSCAR64__ -g -O2 -Oz -i=.. -D__FLIGHT_AOA__

size:
	@if ! command -v $(OSCAR64) >/dev/null 2>&1; then \
		echo "size: $(OSCAR64) not found - skipped"; \
	else \
		$(OSCAR64) $(TARGET_CFLAGS) -o=size_probe.prg size_probe.cc >/dev/null && \
		echo "AoA model, built for the 6510:" && \
		grep -E "^[0-9a-f]{4} \([0-9a-f]{4}\) : (aoa_advance|aoa_cl16|aoa_input)," size_probe.map && \
		echo "The shipping model, from the game build:" && \
		grep -E "^[0-9a-f]{4} \([0-9a-f]{4}\) : (flight_advance|flight_input)," ../ppilot.map; \
	fi

# --- Cycles on a real 6510 ---------------------------------------------------
#
# What one flight_advance() costs, straight and level and in a turn. Needs
# oscar64 and x64sc; skips itself without either, like test/Makefile does.
cycles:
	@if ! command -v $(OSCAR64) >/dev/null 2>&1; then \
		echo "cycles: $(OSCAR64) not found - skipped"; \
	else \
		$(OSCAR64) $(TARGET_CFLAGS) -D__MAX_RAM__ -o=cycles_probe.prg cycles_probe.cc >/dev/null && \
		for sym in g_level_cycles g_turn_cycles g_ortho_cycles g_level_stall g_level_alpha g_turn_leftz_end g_turn_stall; do \
			printf '%-16s ' "$$sym"; \
			../../tools/vice_dump.sh cycles_probe.prg @spin $$sym 2 \
				cycles_probe.dump.bin 40000000 | tail -1; \
		done; \
	fi
