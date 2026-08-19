# Host build of the tests. g++, not oscar64. host_vec.cc checks
# that the inline vec_fastmul8p8 in vec.h matches the 6502 routine it stands
# in for; without that the results below say nothing about the real target.
HOST_CC = g++
HOST_CFLAGS = -O2 -std=c++17 -Wall -Wno-unknown-pragmas -I..

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

test: flight_test msg_test map_test sound_test music_test mul_test sprites_test cpu_test
	@./flight_test > output.txt && ./msg_test >> output.txt && ./map_test >> output.txt && ./sound_test >> output.txt && ./music_test >> output.txt && ./mul_test >> output.txt && ./sprites_test >> output.txt && ./cpu_test >> output.txt && echo "ALL TESTS PASSED (flight_test, msg_test, map_test, sound_test, music_test, mul_test, sprites_test, cpu_test) -> test/output.txt"

clean:
	rm -rf flight_test msg_test map_test sound_test music_test mul_test sprites_test cpu_test output.txt *.dSYM

.PHONY: all test flight_test msg_test map_test sound_test music_test mul_test sprites_test clean
