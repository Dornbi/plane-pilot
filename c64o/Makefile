OSCAR64_INCLUDE=../../../oscar64-main/include

# Which flight model to build (docs/flight.md 1). The angle-of-attack model is
# the better aeroplane and costs bytes; the arcade model is what has always
# shipped. Both are in flight.cc behind __FLIGHT_AOA__ and both are covered by
# the host suite - `make -C c64o/test test-both`.
#
#   make prg               the arcade model
#   make prg FLIGHT_AOA=1  the angle-of-attack model
FLIGHT_AOA ?= 0
ifeq ($(FLIGHT_AOA),1)
FLIGHT_MODEL = -D__FLIGHT_AOA__
endif

# A stamp holding the model the last build used, so that switching the flag
# rebuilds. Without it make sees the same sources and hands back the binary it
# already had - which is a silent wrong answer, and the worst kind for a flag
# whose whole purpose is comparing the two.
FLIGHT_STAMP = .flight_model
$(shell [ "$$(cat $(FLIGHT_STAMP) 2>/dev/null)" = "$(FLIGHT_AOA)" ] || \
	echo "$(FLIGHT_AOA)" > $(FLIGHT_STAMP))

PPILOT_PROGRAM = ppilot
# The same game with the angle-of-attack flight model (docs/flight.md 1). Same
# sources, one define, and its own name so both can exist at once and be flown
# against each other.
PPILOTA_PROGRAM = ppilota
PPILOT_SRC = \
		bcd.cc \
		bcd.h \
		benchmark.h \
		benchmark.cc \
		bool.h \
		box.h \
		box.cc \
		boxdefs.h \
		boxdefs.cc \
		chardefs.h \
		chardefs.cc \
		cia.h \
		cpu.h \
		cpu.cc \
		clouddef.h \
		clouddef.cc \
		clouds.h \
		clouds.cc \
		color.h \
		fmath.h \
		fmath.cc \
		gfx.h \
		gfx.cc \
		gfx_chars.bin \
		help.h \
		help.cc \
		keys.h \
		keys.cc \
		mem.h \
		mem.cc \
		map.h \
		map.cc \
		mapdefs.h \
		mapdefs.cc \
		menu.h \
		menu.cc \
		flight.h \
		flight.cc \
		mission.h \
		mission.cc \
		music.h \
		music.cc \
		musicdef.h \
		musicdef.cc \
		msg.h \
		msg.cc \
		panel.h \
		panel.cc \
		panel.koa \
		ppilot.cc \
		poly.h \
		poly.cc \
		render.h \
		render.cc \
		roll.h \
		roll.cc \
		roll_asm.cc \
		print.h \
		print.cc \
		screen.h \
		screen.cc \
		sid.h \
		sim.h \
		sim.cc \
		sound.h \
		sound.cc \
		spritedef.h \
		spritedef.cc \
		spritedef.bin \
		sprites.h \
		sprites.cc \
		title.h \
		title.cc \
		titledef.h \
		titledef.bin \
		vec.h \
		vec.cc \
		vec_asm.cc \
		vec_lut.cc \
		vic.h \
		view.h \
		view.cc \
		world.h \
		world.cc \
		world_map.cc

POLYDEMO_PROGRAM = polydemo
POLYDEMO_SRC = \
		bcd.cc \
		bcd.h \
		benchmark.h \
		benchmark.cc \
		bool.h \
		cia.h \
		cpu.h \
		cpu.cc \
		fmath.h \
		fmath.cc \
		gfx.h \
		gfx.cc \
		gfx_chars.bin \
		keys.h \
		keys.cc \
		mem.h \
		mem.cc \
		print.h \
		print.cc \
		poly.h \
		poly.cc \
		polydemo.cc \
		sid.h \
		sound.h \
		sound.cc \
		vec.h \
		vec_asm.cc \
		vec_lut.cc \
		world.h \
		world.cc \
		world_map.cc

VECDEMO_PROGRAM = vecdemo
VECDEMO_SRC = 	benchmark.h \
		bcd.cc \
		bcd.h \
		benchmark.cc \
		bool.h \
		print.h \
		print.cc \
		vec.h \
		vec.cc \
		vec_asm.cc \
		vec_lut.cc \
		vecdemo.cc

VECTEST_PROGRAM = vectest
VECTEST_SRC = 	benchmark.h \
		bcd.cc \
		bcd.h \
		benchmark.cc \
		bool.h \
		print.h \
		print.cc \
		vec.h \
		vec.cc \
		vec_asm.cc \
		vec_lut.cc \
		vectest.cc

CC = oscar64
# -g is not optional here: it emits the .asm and .map that ZPCHECK reads.
CFLAGS = -ii=${OSCAR64_INCLUDE} -D__OSCAR64__ -g -xz
# -Oo runs the outliner over everything; the per-frame render and math files
# opt back out with #pragma optimize(nooutline), since there a JSR/RTS per
# extracted sequence costs more than the bytes are worth.
OPTFLAGS = -O2 -Op -Oa -Oi -Oz -Oo

# Clouds, on by default in both ppilot and ppilotd. Build with CLOUDS= to leave
# them out: world.cc then never includes clouds.h, so the #pragma compile chain
# that pulls in clouds.cc and clouddef.cc is never entered and neither the code
# nor the hash tables reach the binary. Useful for measuring what they cost,
# and for bisecting anything that only misbehaves with sprites on screen.
CLOUDS ?= -D__ENABLE_CLOUDS__

# Sound and the debug view. Both on by default: ppilot.prg is one binary again,
# so the V key and the D/Z/X keys are all in it. They were two binaries for a
# while - the debug build was the one with the room for the benchmark counters -
# and the flags stayed because the merged build still has to fit. Build with
# SOUND= or DEBUG= to leave one out and measure what it costs, the same dial
# CLOUDS= is.
SOUND ?= -D__ENABLE_SOUND__
DEBUG ?= -D__ENABLE_DEBUG__


# mem.h widens the zeropage region below oscar64's default, into space the
# compiler's own spilled temporaries grow into. Nothing in oscar64 checks that,
# and an overrun corrupts globals silently, so every link is followed by this.
PYTHON ?= python3
ZPCHECK = $(PYTHON) ../tools/check_zeropage.py
# Raster handlers run on top of the main line, so they must not touch oscar64's
# runtime zero page. Nothing in the compiler checks that and the failure mode is
# a scrambled screen with no obvious cause, so every link is followed by this
# too. See the header of the script.
IRQCHECK = $(PYTHON) ../tools/check_irq_zp.py
MATHCHECK = $(PYTHON) ../tools/check_mul_div.py
# Until mem_init() banks the ROM out, $A000-$BFFF reads BASIC and $E000-$FFFF
# reads KERNAL, while writes to the same addresses reach the RAM underneath. A
# global the linker happens to place there is therefore written correctly and
# read back as a ROM byte by anything that runs before mem_init(). It cost a
# frozen simulation once; see the header of the script.
ROMCHECK = $(PYTHON) ../tools/check_rom_window.py
RAMANALYSIS = $(PYTHON) ../tools/analyze_ram.py

# Report the size of a built .prg. A .prg starts with a two-byte little-endian
# load address, so the image the C64 actually holds is two bytes shorter than
# the file, and the last byte used follows from the start plus that image.
# Read from the file rather than assuming $0801, so the range stays right if
# the start address ever moves. $(1) is the path to the .prg.
define prg_size
	@if [ ! -f "$(1)" ]; then \
		echo "$(1): missing — build failed?" >&2; exit 1; \
	fi; \
	bytes=$$(wc -c < "$(1)" | tr -d ' '); \
	lo=$$(od -An -tu1 -N1 -j0 "$(1)" | tr -d ' '); \
	hi=$$(od -An -tu1 -N1 -j1 "$(1)" | tr -d ' '); \
	start=$$(( hi * 256 + lo )); \
	image=$$(( bytes - 2 )); \
	end=$$(( start + image - 1 )); \
	printf '%-14s %6d bytes on disk, %6d in memory at $$%04X-$$%04X\n' \
		"$(1)" "$$bytes" "$$image" "$$start" "$$end"
endef

all: $(PPILOT_PROGRAM).prg $(PPILOTA_PROGRAM).prg $(POLYDEMO_PROGRAM).prg $(VECDEMO_PROGRAM).prg $(VECTEST_PROGRAM).prg test
	@echo ""
	$(call prg_size,$(PPILOT_PROGRAM).prg)
	$(call prg_size,$(PPILOTA_PROGRAM).prg)

# Standalone, so the size can be checked without provoking a build. Reports
# whatever is on disk now; run `make` first if that might be stale.
size:
	$(call prg_size,$(PPILOT_PROGRAM).prg)
	$(call prg_size,$(PPILOTA_PROGRAM).prg)

ram:
	$(RAMANALYSIS) $(PPILOT_PROGRAM).map

$(PPILOT_PROGRAM).prg: $(PPILOT_SRC) $(FLIGHT_STAMP)
	$(CC) $(CFLAGS) $(OPTFLAGS) $(SOUND) $(DEBUG) $(CLOUDS) $(FLIGHT_MODEL) -D__MAX_RAM__ -o=$(PPILOT_PROGRAM).prg ppilot.cc
	@$(ZPCHECK) $(PPILOT_PROGRAM).asm
	@$(IRQCHECK) $(PPILOT_PROGRAM).asm
	@$(MATHCHECK) $(PPILOT_PROGRAM).asm
	@$(ROMCHECK) $(PPILOT_PROGRAM).asm

# Built from the same ppilot.cc, so it stays in step with the game by
# construction. It does not depend on FLIGHT_STAMP: its model is fixed by its
# own name, and the stamp exists for the case where FLIGHT_AOA moves ppilot.prg
# from one model to the other under a name that does not change.
$(PPILOTA_PROGRAM).prg: $(PPILOT_SRC)
	$(CC) $(CFLAGS) $(OPTFLAGS) $(SOUND) $(DEBUG) $(CLOUDS) -D__FLIGHT_AOA__ -D__MAX_RAM__ -o=$(PPILOTA_PROGRAM).prg ppilot.cc
	@$(ZPCHECK) $(PPILOTA_PROGRAM).asm
	@$(IRQCHECK) $(PPILOTA_PROGRAM).asm
	@$(MATHCHECK) $(PPILOTA_PROGRAM).asm
	@$(ROMCHECK) $(PPILOTA_PROGRAM).asm

$(POLYDEMO_PROGRAM).prg: $(POLYDEMO_SRC)
	$(CC) $(CFLAGS) $(OPTFLAGS) -D__DEBUG_CYCLES__ -D__DEBUG_POLY__ -D__MAX_RAM__ polydemo.cc
	@$(ZPCHECK) $(POLYDEMO_PROGRAM).asm
	@$(IRQCHECK) $(POLYDEMO_PROGRAM).asm
	@$(MATHCHECK) $(POLYDEMO_PROGRAM).asm
	@$(ROMCHECK) $(POLYDEMO_PROGRAM).asm

$(VECDEMO_PROGRAM).prg: $(VECDEMO_SRC)
	$(CC) $(CFLAGS) $(OPTFLAGS) -D__DEBUG_CYCLES__ vecdemo.cc
	@$(ZPCHECK) $(VECDEMO_PROGRAM).asm
	@$(MATHCHECK) $(VECDEMO_PROGRAM).asm

$(VECTEST_PROGRAM).prg: $(VECTEST_SRC)
	$(CC) $(CFLAGS) $(OPTFLAGS) -D__DEBUG_CYCLES__ vectest.cc
	@$(ZPCHECK) $(VECTEST_PROGRAM).asm

# Re-run the checks on whatever is already built, without provoking a build.
zpcheck:
	$(ZPCHECK) $(PPILOT_PROGRAM).asm $(PPILOTA_PROGRAM).asm \
		$(POLYDEMO_PROGRAM).asm $(VECDEMO_PROGRAM).asm $(VECTEST_PROGRAM).asm
	$(IRQCHECK) $(PPILOT_PROGRAM).asm $(PPILOTA_PROGRAM).asm \
		$(POLYDEMO_PROGRAM).asm

mathcheck:
	$(MATHCHECK) $(PPILOT_PROGRAM).asm $(PPILOTA_PROGRAM).asm \
		$(POLYDEMO_PROGRAM).asm $(VECDEMO_PROGRAM).asm

romcheck:
	$(ROMCHECK) $(PPILOT_PROGRAM).asm $(PPILOTA_PROGRAM).asm \
		$(POLYDEMO_PROGRAM).asm

test flight_test msg_test:
	$(MAKE) -C test $@

clean:
	rm -rf $(PPILOT_PROGRAM).prg $(PPILOTA_PROGRAM).prg $(POLYDEMO_PROGRAM).prg $(VECDEMO_PROGRAM).prg $(VECTEST_PROGRAM).prg *.asm *.dbj *.int *.lbl *.map $(FLIGHT_STAMP)
	$(MAKE) -C test clean

.PHONY: all size ram zpcheck mathcheck romcheck test flight_test msg_test clean

# ZPCHECK runs after the link, so a program that fails it already exists on
# disk. Without this, the next make would find the .prg newer than its sources,
# skip the rule, and ship a binary whose check never passed.
.DELETE_ON_ERROR:
