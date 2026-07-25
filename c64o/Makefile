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
CFLAGS = -ii=${OSCAR64_INCLUDE} -D__OSCAR64__ -g -xz
OPTFLAGS = -O2 -Op -Oa -Oi -Oz

all: $(PPILOT_PROGRAM).prg $(POLYDEMO_PROGRAM).prg $(VECDEMO_PROGRAM).prg $(VECTEST_PROGRAM).prg test

$(PPILOT_PROGRAM).prg: $(PPILOT_SRC)
	$(CC) $(CFLAGS) $(OPTFLAGS) -D__DEBUG_VIEW__ -D__DEBUG_MODEL__ -D__MAX_RAM__ -o=$(PPILOT_PROGRAM).prg ppilot.cc

$(POLYDEMO_PROGRAM).prg: $(POLYDEMO_SRC)
	$(CC) $(CFLAGS) $(OPTFLAGS) -D__DEBUG_CYCLES__ -D__DEBUG_POLY__ -D__MAX_RAM__ polydemo.cc

$(VECDEMO_PROGRAM).prg: $(VECDEMO_SRC)
	$(CC) $(CFLAGS) $(OPTFLAGS) -D__DEBUG_CYCLES__ vecdemo.cc

$(VECTEST_PROGRAM).prg: $(VECTEST_SRC)
	$(CC) $(CFLAGS) $(OPTFLAGS) -D__DEBUG_CYCLES__ vectest.cc

test: flight_test msg_test
	@./test/flight_test > test/output.txt && ./test/msg_test >> test/output.txt && echo "ALL TESTS PASSED (33 flight tests, msg_test) -> test/output.txt"

clean:
	rm -rf $(PPILOT_PROGRAM).prg $(POLYDEMO_PROGRAM).prg $(VECDEMO_PROGRAM).prg $(VECTEST_PROGRAM).prg test/flight_test test/msg_test test/output.txt test/*.dSYM *.asm *.dbj *.int *.lbl *.map

.PHONY: all test flight_test msg_test clean
