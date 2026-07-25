# Host build of the tests. g++, not oscar64. host_vec.cc checks
# that the inline vec_fastmul8p8 in vec.h matches the 6502 routine it stands
# in for; without that the results below say nothing about the real target.
HOST_CC = g++
HOST_CFLAGS = -O2 -std=c++17 -Wall -Wno-unknown-pragmas -I..

FLIGHT_TEST_SRC = flight_test.cc host_vec.cc ../flight.cc ../mission.cc ../vec.cc \
		../vec_lut.cc ../fmath.cc
FLIGHT_TEST_HDRS = ../flight.h ../mission.h ../vec.h ../fmath.h

MSG_TEST_SRC = msg_test.cc ../msg.cc
MSG_TEST_HDRS = ../msg.h

all: test

flight_test: $(FLIGHT_TEST_SRC) $(FLIGHT_TEST_HDRS)
	$(HOST_CC) $(HOST_CFLAGS) -o flight_test $(FLIGHT_TEST_SRC)

msg_test: $(MSG_TEST_SRC) $(MSG_TEST_HDRS)
	$(HOST_CC) $(HOST_CFLAGS) -o msg_test $(MSG_TEST_SRC)

test: flight_test msg_test
	@./flight_test > output.txt && ./msg_test >> output.txt && echo "ALL TESTS PASSED (37 flight tests, msg_test) -> test/output.txt"

clean:
	rm -rf flight_test msg_test output.txt *.dSYM

.PHONY: all test flight_test msg_test clean
