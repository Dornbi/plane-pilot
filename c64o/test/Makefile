# The flight test is built by the parent Makefile, which is the single source
# of truth for the source list and flags. This is here so that `make` and
# `make test` also work from inside test/.

test:
	$(MAKE) -C .. test

flight_test:
	$(MAKE) -C .. flight_test

clean:
	$(MAKE) -C .. clean

.PHONY: test flight_test clean
