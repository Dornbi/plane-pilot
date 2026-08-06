"""Make the repo root importable so tests can `import lib...` and
`from tools.png2koa import ...` no matter which directory pytest runs from."""

import os
import sys

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
if REPO_ROOT not in sys.path:
    sys.path.insert(0, REPO_ROOT)
