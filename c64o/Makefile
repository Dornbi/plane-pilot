OSCAR64_INCLUDE=../../../oscar64-main/include

PPILOT_PROGRAM = ppilot
PPILOT_SRC = \
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
		mem.h \
		mem.cc \
		model.h \
		model.cc \
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
		spritedef.h \
		spritedef.cc \
		sprites.h \
		sprites.cc \
		vec.h \
		vec.cc \
		vec_asm.cc \
		vec_lut.cc \
		vic.h \
		world.h \
		world.cc

POLYDEMO_PROGRAM = polydemo
POLYDEMO_SRC = \
		benchmark.h \
		benchmark.cc \
		bool.h \
		cia.h \
		fmath.h \
		fmath.cc \
		gfx.h \
		gfx.cc \
		keys.h \
		mem.h \
		mem.cc \
		print.h \
		print.cc \
		poly.h \
		poly.cc \
		vec.h \
		vec_lut.cc \
		world.h \
		world.cc \
		polydemo.cc

VECDEMO_PROGRAM = vecdemo
VECDEMO_SRC = 	benchmark.h \
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
OPTFLAGS = -O2 -Op -Oa -Oi -Oz -Op

all: $(PPILOT_PROGRAM).prg $(POLYDEMO_PROGRAM).prg $(VECDEMO_PROGRAM).prg $(VECTEST_PROGRAM).prg

$(PPILOT_PROGRAM).prg: $(PPILOT_SRC)
	$(CC) $(CFLAGS) $(OPTFLAGS) -D__DEBUG_HORIZON__ -D__DEBUG_MODEL__ -o=$(PPILOT_PROGRAM).prg ppilot.cc

$(POLYDEMO_PROGRAM).prg: $(POLYDEMO_SRC)
	$(CC) $(CFLAGS) $(OPTFLAGS) -D__DEBUG_CYCLES__ -D__DEBUG_POLY__  polydemo.cc

$(VECDEMO_PROGRAM).prg: $(VECDEMO_SRC)
	$(CC) $(CFLAGS) $(OPTFLAGS) -D__DEBUG_CYCLES__ vecdemo.cc

$(VECTEST_PROGRAM).prg: $(VECTEST_SRC)
	$(CC) $(CFLAGS) $(OPTFLAGS) -D__DEBUG_CYCLES__ vectest.cc

clean:
	rm -f $(PPILOT_PROGRAM).prg $(POLYDEMO_PROGRAM).prg $(VECDEMO_PROGRAM).prg $(VECTEST_PROGRAM).prg *.asm *.dbj *.int *.lbl *.map
