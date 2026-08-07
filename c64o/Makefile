OSCAR64_INCLUDE=../../../oscar64-main/include

PPILOT_PROGRAM = ppilot
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
		sim.h \
		sim.cc \
		spritedef.h \
		spritedef.cc \
		spritedef.bin \
		sprites.h \
		sprites.cc \
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

# mem.h widens the zeropage region below oscar64's default, into space the
# compiler's own spilled temporaries grow into. Nothing in oscar64 checks that,
# and an overrun corrupts globals silently, so every link is followed by this.
PYTHON ?= python3
ZPCHECK = $(PYTHON) ../tools/check_zeropage.py

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

all: $(PPILOT_PROGRAM).prg $(POLYDEMO_PROGRAM).prg $(VECDEMO_PROGRAM).prg $(VECTEST_PROGRAM).prg test
	@echo ""
	$(call prg_size,$(PPILOT_PROGRAM).prg)

# Standalone, so the size can be checked without provoking a build. Reports
# whatever is on disk now; run `make` first if that might be stale.
size:
	$(call prg_size,$(PPILOT_PROGRAM).prg)

$(PPILOT_PROGRAM).prg: $(PPILOT_SRC)
	$(CC) $(CFLAGS) $(OPTFLAGS) -D__DEBUG_VIEW__ -D__DEBUG_MODEL__ -D__MAX_RAM__ -o=$(PPILOT_PROGRAM).prg ppilot.cc
	@$(ZPCHECK) $(PPILOT_PROGRAM).asm

$(POLYDEMO_PROGRAM).prg: $(POLYDEMO_SRC)
	$(CC) $(CFLAGS) $(OPTFLAGS) -D__DEBUG_CYCLES__ -D__DEBUG_POLY__ -D__MAX_RAM__ polydemo.cc
	@$(ZPCHECK) $(POLYDEMO_PROGRAM).asm

$(VECDEMO_PROGRAM).prg: $(VECDEMO_SRC)
	$(CC) $(CFLAGS) $(OPTFLAGS) -D__DEBUG_CYCLES__ vecdemo.cc
	@$(ZPCHECK) $(VECDEMO_PROGRAM).asm

$(VECTEST_PROGRAM).prg: $(VECTEST_SRC)
	$(CC) $(CFLAGS) $(OPTFLAGS) -D__DEBUG_CYCLES__ vectest.cc
	@$(ZPCHECK) $(VECTEST_PROGRAM).asm

# Re-run the check on whatever is already built, without provoking a build.
zpcheck:
	$(ZPCHECK) $(PPILOT_PROGRAM).asm $(POLYDEMO_PROGRAM).asm \
		$(VECDEMO_PROGRAM).asm $(VECTEST_PROGRAM).asm

test flight_test msg_test:
	$(MAKE) -C test $@

clean:
	rm -rf $(PPILOT_PROGRAM).prg $(POLYDEMO_PROGRAM).prg $(VECDEMO_PROGRAM).prg $(VECTEST_PROGRAM).prg *.asm *.dbj *.int *.lbl *.map
	$(MAKE) -C test clean

.PHONY: all size zpcheck test flight_test msg_test clean

# ZPCHECK runs after the link, so a program that fails it already exists on
# disk. Without this, the next make would find the .prg newer than its sources,
# skip the rule, and ship a binary whose check never passed.
.DELETE_ON_ERROR:
