#!/bin/bash
# Script to regenerate chardefs.py with consistent settings
python3 generate_all.py --include-alternates --proportional-dither --min-box-width=4 --debug
