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

# The programs c64o/Makefile builds. `make release` publishes these from
# c64o/ (build output, gitignored) to bin/ (checked in, what README links to).
PROGRAMS = ppilot polydemo vecdemo vectest

.PHONY: help data chardefs gfx-chars sprites map-tiles map-tiles-draft map-preview render demo prg release test clean

help:
	@echo "Data generation:"
	@echo "  make data       - regenerate all generated C64 data (chardefs, gfx-chars, sprites, map-tiles)"
	@echo "  make chardefs   - chardefs/boxdefs for Python and C, plus all reference frames"
	@echo "  make gfx-chars  - c64o/gfx_chars.bin"
	@echo "  make sprites    - c64o/spritedef.{bin,h,cc} and lib/spritedef.py"
	@echo "  make map-tiles  - c64o/mapdefs.{cc,h} from gfx/ppilot_map_tiles.png"
	@echo ""
	@echo "Preview and build:"
	@echo "  make map-preview- render out/map_preview.png from the current map tiles"
	@echo "  make render     - render all roll angles to out/rendered_frames"
	@echo "  make demo       - interactive roll/pitch demo (needs pygame)"
	@echo "  make prg        - build the C64 binaries via c64o/Makefile (needs oscar64),"
	@echo "                    then report ppilot.prg's size and load range"
	@echo "  make release    - build, then publish the .prg files to bin/"
	@echo ""
	@echo "  make test       - run the Python test suite"
	@echo "  make clean      - remove out/ and the c64o/ build output"

# --- Data generation -------------------------------------------------------

data: chardefs gfx-chars sprites map-tiles

chardefs:
	$(PYTHON) tools/generate_all.py $(CHARDEFS_FLAGS)

gfx-chars:
	$(PYTHON) tools/generate_gfx_chars.py

sprites:
	$(PYTHON) tools/generate_sprites.py

# The tile sheet gfx/ppilot_map_tiles.png is the source of truth: edit it in
# GIMP, then run this. map-tiles-draft only lays down the first version and
# refuses to clobber an existing sheet without --force.
map-tiles:
	$(PYTHON) tools/generate_map_tiles.py

map-tiles-draft:
	$(PYTHON) tools/make_map_tiles_draft.py

# --- Preview and build -----------------------------------------------------

map-preview:
	$(PYTHON) tools/render_map_preview.py

render:
	$(PYTHON) tools/render_all.py --centers "$(RENDER_CENTERS)" --debug

demo:
	$(PYTHON) tools/flight_demo.py

# Report the size of a built .prg. A .prg starts with a two-byte little-endian
# load address, so the image the C64 actually holds is two bytes shorter than
# the file, and the end address follows from the start plus that image.
# $(1) is the path to the .prg.
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

prg:
	$(MAKE) -C c64o
	@echo ""
	$(call prg_size,c64o/ppilot.prg)

# Publish the freshly built binaries. bin/ is the only copy anyone downloads,
# so this is the step that keeps it from drifting behind c64o/.
release: prg
	@mkdir -p bin
	@for p in $(PROGRAMS); do \
		if [ ! -f "c64o/$$p.prg" ]; then \
			echo "release: c64o/$$p.prg missing — build failed?" >&2; exit 1; \
		fi; \
		if cmp -s "c64o/$$p.prg" "bin/$$p.prg"; then \
			echo "  unchanged  bin/$$p.prg"; \
		else \
			cp "c64o/$$p.prg" "bin/$$p.prg"; \
			echo "  published  bin/$$p.prg ($$(wc -c < bin/$$p.prg) bytes)"; \
		fi; \
	done
	@echo "Review with: git status bin/"

# --- Housekeeping ----------------------------------------------------------

test:
	$(PYTHON) -m pytest tests -q

clean:
	rm -rf out
	$(MAKE) -C c64o clean
	rm -f c64o/*.prg
