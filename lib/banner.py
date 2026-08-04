"""Banners stamped into every generated file.

Both chardefs and boxdefs exist twice: once as Python (lib/chardefs.py,
lib/boxdefs.py, used by the reference renderer) and once as C for the C64
build (c64o/chardefs.*, c64o/boxdefs.*). All six are written by
generate_all.py, so they carry a notice pointing back at the generator.
"""

_LINES = [
    "GENERATED FILE - DO NOT EDIT.",
    "",
    "Regenerate with ./generate_all.sh from the repository root.",
    "Produced by generate_all.py via {producer}.",
]


def c_banner(producer: str) -> str:
    """Returns the generated-file notice as a C comment block."""
    rule = "// " + "-" * 74 + "\n"
    body = "".join(
        ("//\n" if not line else f"// {line.format(producer=producer)}\n")
        for line in _LINES)
    return rule + body + rule + "\n"


def py_banner(producer: str) -> str:
    """Returns the generated-file notice as a Python comment block."""
    rule = "# " + "-" * 74 + "\n"
    body = "".join(
        ("#\n" if not line else f"# {line.format(producer=producer)}\n")
        for line in _LINES)
    return rule + body + rule + "\n"
