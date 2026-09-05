# Host build of the tests. g++, not oscar64. host_vec.cc checks
# that the inline vec_fastmul8p8 in vec.h matches the 6502 routine it stands
# in for; without that the results below say nothing about the real target.
HOST_CC = g++

# The flight model is a build-time choice (docs/flight.md 1). FLIGHT_AOA=1
# selects the angle-of-attack model, anything else the arcade one, and the
# suite has to pass either way - flight_test.cc carries both sets of
# expectations because for several cases they are opposites.
#
#   make test               the arcade model, which is what ships
#   make test FLIGHT_AOA=1  the angle-of-attack model
#   make test-both          one after the other
FLIGHT_AOA ?= 0
ifeq ($(FLIGHT_AOA),1)
FLIGHT_MODEL = -D__FLIGHT_AOA__
endif

HOST_CFLAGS = -O2 -std=c++17 -Wall -Wno-unknown-pragmas -I.. $(FLIGHT_MODEL)

# --- The 16-bit narrowing check -------------------------------------------
#
# oscar64's int is 16 bits and the host's is 32, so an intermediate that wraps
# on the 6510 does not wrap here and a host test can pass against arithmetic the
# target never did. int16.h has the measurements; check_narrowing.py counts the
# places the divergence could hide and fails when the count moves.
#
# Negation on its own is excluded. -x of an int16_t gives the same answer at
# both widths - including at -32768, where it is its own negative - so the 50-odd
# sites clang reports for it are noise. It is the shift or add *after* a negation
# that overflows, and that is reported on its own.
#
# clang-only flags, so they are probed rather than assumed: on a compiler that
# does not take them the check prints a line and passes, because the alternative
# is a test suite that will not build on Linux.
NARROW_FLAGS = -Wimplicit-int-conversion -Wno-implicit-int-conversion-on-negation
# One baseline per flight model: the two code paths have different narrowing
# sites, and a single ratchet cannot tell "the other model was built" from "a
# new narrowing appeared", which is the only thing it exists to catch.
ifeq ($(FLIGHT_AOA),1)
NARROW_BASELINE = narrowing-aoa.baseline
else
NARROW_BASELINE = narrowing.baseline
endif
NARROW_CHECK = python3 ../../tools/check_narrowing.py
# -Werror is load bearing: clang only *warns* on an unknown -W option, so
# without it the probe passes on a compiler that ignores the flags and the scan
# then reports zero narrowings - which the ratchet reads as every site having
# been fixed. gcc errors on its own, but the probe has to hold for both.
NARROW_SUPPORTED = $(HOST_CC) $(NARROW_FLAGS) -Werror -fsyntax-only -x c++ /dev/null

# --- The on-target suite ---------------------------------------------------
#
# target_test.cc is built by oscar64 and run in VICE, because the cases in it
# have no host equivalent: they are the ones where the 6510's 16-bit int is the
# thing under test. See its header, and int16.h for why the host cannot stand in.
#
# Both tools are optional. Without oscar64 or x64sc the target rules print a
# line and pass, so the other nine suites still run on a machine that has
# neither. TARGET_TEST= turns it off outright.
TARGET_TEST ?= target-test
OSCAR64 ?= oscar64
# Mirrors OSCAR64_INCLUDE in c64o/Makefile, one directory deeper.
OSCAR64_INCLUDE ?= ../../../../oscar64-main/include
# -Oz matches the game build; the rest is what c64o/Makefile passes, minus the
# flags that only make sense for a program with a display.
TARGET_CFLAGS = -ii=$(OSCAR64_INCLUDE) -D__OSCAR64__ -g -O2 -Oz $(FLIGHT_MODEL)

FLIGHT_TEST_SRC = flight_test.cc host_vec.cc ../flight.cc ../mission.cc ../world_map.cc ../vec.cc \
		../vec_lut.cc ../fmath.cc ../msg.cc
FLIGHT_TEST_HDRS = ../flight.h ../mission.h ../vec.h ../fmath.h ../msg.h

MSG_TEST_SRC = msg_test.cc ../msg.cc
MSG_TEST_HDRS = ../msg.h

MAP_TEST_SRC = map_test.cc ../mission.cc ../world_map.cc
MAP_TEST_HDRS = ../mission.h ../world.h

# sound_test links sound.cc alone. The flight globals it reads are defined in
# the test itself rather than pulled in from flight.cc, which keeps this a test
# of the register mapping and lets it set input combinations directly.
SOUND_TEST_SRC = sound_test.cc ../sound.cc
SOUND_TEST_HDRS = ../sound.h ../sid.h ../flight.h

# mul_test links nothing: it replays the arithmetic of the four call sites that
# were converted from mul16/mul16by8 to vec_fastmul8p8 against the products they
# replaced. Copies of the expressions rather than the real functions, since
# render.cc and box.cc do not build on the host, so it has to be kept in step
# with them by hand - the comment at the top of mul_test.cc says why it earns
# that cost.
# music_test links music.cc and the generated musicdef.cc. sound_volume is
# defined by the test itself rather than pulled in from sound.cc, which keeps
# this a test of the player and lets it set the V-key setting directly.
MUSIC_TEST_SRC = music_test.cc ../music.cc ../musicdef.cc
MUSIC_TEST_HDRS = ../music.h ../musicdef.h ../sound.h ../sid.h

MUL_TEST_SRC = mul_test.cc
MUL_TEST_HDRS = ../vec.h

# sprites_test #includes ../sprites.cc rather than linking it: the band
# handlers are `inline`, which is how oscar64 folds them into the raster IRQs,
# and means there is no out-of-line copy for the host linker to find. That is
# also why sprites.cc is a header dependency here. vic.h and mem.h point their
# register and screen RAM addresses at arrays in the test when __OSCAR64__ is
# not defined, the same way sid.h already does for the SID.
SPRITES_TEST_SRC = sprites_test.cc ../spritedef.cc
SPRITES_TEST_HDRS = ../sprites.h ../sprites.cc ../spritedef.h ../vic.h \
		../mem.h ../msg.h ../view.h ../color.h ../roll.h

# poly_test includes ../poly.cc rather than linking it: the projection and
# both clippers are static, and it is the projection that is under test. The
# unused-function warning is for _poly_trace_edge_dda, which poly.cc keeps as
# the alternative to the bresenham tracer.
POLY_TEST_SRC = poly_test.cc ../vec.cc ../vec_lut.cc ../fmath.cc
POLY_TEST_HDRS = ../poly.h ../poly.cc ../vec.h ../mem.h ../chardefs.h

CPU_TEST_SRC = cpu_test.cc
CPU_TEST_HDRS = ../cpu.h ../cpu.cc ../cia.h

all: test

flight_test: $(FLIGHT_TEST_SRC) $(FLIGHT_TEST_HDRS)
	$(HOST_CC) $(HOST_CFLAGS) -o flight_test $(FLIGHT_TEST_SRC)

msg_test: $(MSG_TEST_SRC) $(MSG_TEST_HDRS)
	$(HOST_CC) $(HOST_CFLAGS) -o msg_test $(MSG_TEST_SRC)

map_test: $(MAP_TEST_SRC) $(MAP_TEST_HDRS)
	$(HOST_CC) $(HOST_CFLAGS) -o map_test $(MAP_TEST_SRC)

sound_test: $(SOUND_TEST_SRC) $(SOUND_TEST_HDRS)
	$(HOST_CC) $(HOST_CFLAGS) -D__ENABLE_SOUND__ -o sound_test $(SOUND_TEST_SRC)

music_test: $(MUSIC_TEST_SRC) $(MUSIC_TEST_HDRS)
	$(HOST_CC) $(HOST_CFLAGS) -D__ENABLE_SOUND__ -o music_test $(MUSIC_TEST_SRC)

mul_test: $(MUL_TEST_SRC) $(MUL_TEST_HDRS)
	$(HOST_CC) $(HOST_CFLAGS) -o mul_test $(MUL_TEST_SRC)

sprites_test: $(SPRITES_TEST_SRC) $(SPRITES_TEST_HDRS)
	$(HOST_CC) $(HOST_CFLAGS) -o sprites_test $(SPRITES_TEST_SRC)

cpu_test: $(CPU_TEST_SRC) $(CPU_TEST_HDRS)
	$(HOST_CC) $(HOST_CFLAGS) -o cpu_test $(CPU_TEST_SRC)

poly_test: $(POLY_TEST_SRC) $(POLY_TEST_HDRS)
	$(HOST_CC) $(HOST_CFLAGS) -Wno-unused-function -o poly_test $(POLY_TEST_SRC)

# Every suite's sources, for the syntax-only narrowing pass. Kept as one list
# rather than reusing the per-suite variables because the pass does not link and
# does not care which binary a file belongs to - only that it is compiled once
# with the same flags the real build uses.
NARROW_TUS = "$(FLIGHT_TEST_SRC)" "$(SOUND_TEST_SRC)" "$(MUSIC_TEST_SRC)" \
		"$(MUL_TEST_SRC)" "$(SPRITES_TEST_SRC)" "$(CPU_TEST_SRC)" \
		"$(POLY_TEST_SRC)" "$(MSG_TEST_SRC)" "$(MAP_TEST_SRC)"

# Collect the warnings from every translation unit into one stream. -Wno-error
# is implicit: this pass never fails the compile, only the count comparison.
define narrow_scan
	for tu in $(NARROW_TUS); do \
		$(HOST_CC) $(HOST_CFLAGS) -Wno-unused-function $(NARROW_FLAGS) \
			-D__ENABLE_SOUND__ -fsyntax-only $$tu 2>&1 || true; \
	done
endef

target_test.prg: target_test.cc int16.h ../vec.h ../vec.cc ../vec_lut.cc
	$(OSCAR64) $(TARGET_CFLAGS) -o=target_test.prg target_test.cc

target-test:
	@if ! command -v $(OSCAR64) >/dev/null 2>&1; then \
		echo "target_test: $(OSCAR64) not found - skipped (the 16-bit cases were not run)"; \
	else \
		$(MAKE) --no-print-directory target_test.prg && \
		../../tools/run_target_test.sh target_test.prg; \
	fi

narrowing:
	@if ! $(NARROW_SUPPORTED) >/dev/null 2>&1; then \
		echo "narrowing: $(HOST_CC) does not take $(NARROW_FLAGS) - skipped"; \
	else \
		{ $(narrow_scan) ; } | $(NARROW_CHECK) $(NARROW_BASELINE); \
	fi

narrowing-update:
	@if ! $(NARROW_SUPPORTED) >/dev/null 2>&1; then \
		echo "narrowing: $(HOST_CC) does not take $(NARROW_FLAGS) - cannot update"; \
		exit 1; \
	fi
	@{ $(narrow_scan) ; } | $(NARROW_CHECK) --update $(NARROW_BASELINE)

test: flight_test msg_test map_test sound_test music_test mul_test sprites_test cpu_test poly_test narrowing $(TARGET_TEST)
	@./flight_test > output.txt && ./msg_test >> output.txt && ./map_test >> output.txt && ./sound_test >> output.txt && ./music_test >> output.txt && ./mul_test >> output.txt && ./sprites_test >> output.txt && ./cpu_test >> output.txt && ./poly_test >> output.txt && echo "ALL TESTS PASSED (flight_test, msg_test, map_test, sound_test, music_test, mul_test, sprites_test, cpu_test, poly_test) -> test/output.txt"

# Both models, from clean, because the object files are not model-tagged.
test-both:
	$(MAKE) clean && $(MAKE) test FLIGHT_AOA=0
	$(MAKE) clean && $(MAKE) test FLIGHT_AOA=1

clean:
	rm -rf flight_test msg_test map_test sound_test music_test mul_test sprites_test cpu_test poly_test output.txt *.dSYM
	rm -f target_test.prg target_test.asm target_test.lbl target_test.map target_test.int target_test.dbj

.PHONY: all test test-both narrowing narrowing-update target-test flight_test msg_test map_test sound_test music_test mul_test sprites_test poly_test clean
