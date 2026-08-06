# Plane Pilot — data generation and build entry points.
#
# The Python tools live in tools/ and the C64 sources in c64o/.
# Every target can be run from any directory; the tools anchor their own
# paths to the repo root.

PYTHON ?= python3

# Canonical flags for the generators. These are not defaults inside the
# scripts, so they belong here rather than in anyone's shell history.
CHARDEFS_FLAGS = --include-alternates --proportional-dither --min-box-width=4 --debug
RENDER_CENTERS = 160,100;160,96;164,100

.PHONY: help data chardefs gfx-chars sprites render demo prg test clean

help:
	@echo "Data generation:"
	@echo "  make data       - regenerate all generated C64 data (chardefs, gfx-chars, sprites)"
	@echo "  make chardefs   - chardefs/boxdefs for Python and C, plus all reference frames"
	@echo "  make gfx-chars  - c64o/gfx_chars.bin"
	@echo "  make sprites    - c64o/spritedef.{bin,h,cc} and lib/spritedef.py"
	@echo ""
	@echo "Preview and build:"
	@echo "  make render     - render all roll angles to out/rendered_frames"
	@echo "  make demo       - interactive roll/pitch demo (needs pygame)"
	@echo "  make prg        - build the C64 binaries via c64o/Makefile (needs oscar64)"
	@echo ""
	@echo "  make test       - run the Python test suite"
	@echo "  make clean      - remove out/"

# --- Data generation -------------------------------------------------------

data: chardefs gfx-chars sprites

chardefs:
	$(PYTHON) tools/generate_all.py $(CHARDEFS_FLAGS)

gfx-chars:
	$(PYTHON) tools/generate_gfx_chars.py

sprites:
	$(PYTHON) tools/generate_sprites.py

# --- Preview and build -----------------------------------------------------

render:
	$(PYTHON) tools/render_all.py --centers "$(RENDER_CENTERS)" --debug

demo:
	$(PYTHON) tools/flight_demo.py

prg:
	$(MAKE) -C c64o

# --- Housekeeping ----------------------------------------------------------

test:
	$(PYTHON) -m pytest tests -q

clean:
	rm -rf out
