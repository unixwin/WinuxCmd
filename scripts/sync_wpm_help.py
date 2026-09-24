"""Sync the wpm custom_help entry in i18n_batch.py from src/commands/wpm.cpp.

Extracts the concatenated C++ string literal of print_usage()'s `help`
variable and rewrites the MANUAL_MESSAGES entry "command.wpm.custom_help".
"""

import re
import sys
from pathlib import Path

ROOT = Path(r"D:\repo\unixwin-winuxcmd")
CPP = ROOT / "src" / "commands" / "wpm.cpp"
PY = ROOT / "scripts" / "i18n_batch.py"

cpp_text = CPP.read_text(encoding="utf-8")

# Locate the help string inside print_usage()
start = cpp_text.index("auto print_usage() -> int {")
seg = cpp_text[start:]
m = re.search(r"const std::string help =\n(.*?);\n", seg, re.S)
if not m:
    sys.exit("help literal not found")
literal_block = m.group(1)

# Extract each C++ string literal piece and unescape
pieces = re.findall(r'"((?:[^"\\]|\\.)*)"', literal_block)


def unescape(s: str) -> str:
    return (
        s.replace('\\"', '"')
        .replace("\\\\", "\\")
        .replace("\\n", "\n")
        .replace("\\t", "\t")
    )


text = "".join(unescape(p) for p in pieces)

# Build the Python literal: one repr per line, implicit concatenation
lines = text.split("\n")
parts = []
for i, line in enumerate(lines):
    suffix = "\\n" if i < len(lines) - 1 else ""
    value = line + suffix
    parts.append("        %s" % repr(value))
py_literal = "(\n" + "\n".join(parts) + "\n    )"

py_text = PY.read_text(encoding="utf-8")
key = '    "command.wpm.custom_help": '
begin = py_text.index(key)
end = py_text.index("    ),", begin) + len("    ),")
new_entry = key + py_literal + ","
py_text = py_text[:begin] + new_entry + py_text[end:]
PY.write_text(py_text, encoding="utf-8")

print("custom_help synced:", len(lines), "lines,", len(text), "chars")
print("--- first 3 / last 3 ---")
for l in lines[:3]:
    print(repr(l))
for l in lines[-3:]:
    print(repr(l))
