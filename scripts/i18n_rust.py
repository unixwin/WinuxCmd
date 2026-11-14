"""Extract explicit Rust messages without scanning comments, data, or tests.

Supported calls are tr!(ctx, "key", "template", ...) and qualified
i18n::text(ctx, "key", FALLBACK). Fallbacks are Rust string literals,
same-module string constants, or concat! of those forms. Production module
edges are followed from crate roots; cfg(test) modules are not traversed.
Unsupported source indirection fails extraction rather than losing messages.

Keys shared with the C++ catalogs are special: `winux::i18n::format` uses
std::format positioning (`{}`), the Rust `tr!` fallback names the same
fields, and one English catalog entry must serve both. POSITIONAL_TO_NAMED
is the explicit migration annotation for exactly those keys; the merge
accepts the two spellings only when the literal text and the field
count/order match, and keeps the positioned C++ text as the canonical
catalog entry. A conflicting fallback without an annotation still fails.
"""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
import re
from string import Formatter


# Migration annotation: catalog keys whose C++ call sites render positioned
# text while the Rust call site names the same fields. Value = the Rust
# field names in placeholder order. Add a key here only after migrating it;
# merging an annotated key verifies the positioned and named spellings are
# the same message (same literal shape, same field count/order), and any
# other C++/Rust fallback disagreement stays a hard extraction failure.
POSITIONAL_TO_NAMED: dict[str, tuple[str, ...]] = {
    "common.try_help": ("prog",),
    "common.error.option_no_argument": ("option",),
    "common.error.extra_operand": ("operand",),
    "common.error.missing_after": ("operand",),
}


@dataclass(frozen=True)
class Token:
    kind: str
    text: str
    children: tuple[Token, ...] = ()


def _decode_string(value: str) -> str:
    result = []
    i = 0
    escapes = {"n": "\n", "r": "\r", "t": "\t", "0": "\0",
               "\\": "\\", '"': '"', "'": "'"}
    while i < len(value):
        if value[i] != "\\":
            result.append(value[i])
            i += 1
            continue
        i += 1
        if i == len(value):
            raise ValueError("unterminated Rust string escape")
        ch = value[i]
        i += 1
        if ch in escapes:
            result.append(escapes[ch])
        elif ch == "x":
            digits = value[i:i + 2]
            if not re.fullmatch(r"[0-9a-fA-F]{2}", digits) or int(digits, 16) > 127:
                raise ValueError("invalid Rust str hex escape")
            result.append(chr(int(digits, 16)))
            i += 2
        elif ch == "u" and value[i:i + 1] == "{":
            end = value.find("}", i + 1)
            digits = value[i + 1:end].replace("_", "")
            if end < 0 or not re.fullmatch(r"[0-9a-fA-F]{1,6}", digits):
                raise ValueError("invalid Rust Unicode escape")
            scalar = int(digits, 16)
            if scalar > 0x10FFFF or 0xD800 <= scalar <= 0xDFFF:
                raise ValueError("invalid Rust Unicode scalar")
            result.append(chr(scalar))
            i = end + 1
        elif ch == "\n":
            while i < len(value) and value[i].isspace():
                i += 1
        else:
            raise ValueError(f"unsupported Rust string escape: \\{ch}")
    return "".join(result)


def tokenize(source: str) -> tuple[Token, ...]:
    i = 0

    def parse(closing=None):
        nonlocal i
        tokens = []
        while i < len(source):
            ch = source[i]
            if ch.isspace():
                i += 1
                continue
            if source.startswith("//", i):
                end = source.find("\n", i)
                i = len(source) if end < 0 else end + 1
                continue
            if source.startswith("/*", i):
                depth = 1
                i += 2
                while i < len(source) and depth:
                    if source.startswith("/*", i):
                        depth += 1
                        i += 2
                    elif source.startswith("*/", i):
                        depth -= 1
                        i += 2
                    else:
                        i += 1
                if depth:
                    raise ValueError("unterminated Rust block comment")
                continue
            raw = re.match(r"(br|cr|r)(#{0,255})\"", source[i:])
            if raw:
                start = i + raw.end()
                terminator = '"' + raw[2]
                end = source.find(terminator, start)
                if end < 0:
                    raise ValueError("unterminated Rust raw string")
                kind = "string" if raw[1] == "r" else "other"
                tokens.append(Token(kind, source[start:end]))
                i = end + len(terminator)
                continue
            prefix = ch in "bc" and source[i + 1:i + 2] == '"'
            if ch == '"' or prefix:
                start = i + (2 if prefix else 1)
                end = start
                while end < len(source) and source[end] != '"':
                    end += 2 if source[end] == "\\" else 1
                if end >= len(source):
                    raise ValueError("unterminated Rust string")
                body = source[start:end]
                tokens.append(Token("other" if prefix else "string",
                                    body if prefix else _decode_string(body)))
                i = end + 1
                continue
            char = re.match(r"'(?:\\(?:u\{[0-9a-fA-F_]+\}|x[0-9a-fA-F]{2}|.)|[^'\\\n])'",
                            source[i:])
            if char:
                tokens.append(Token("other", char[0]))
                i += char.end()
                continue
            if ch in "([{":
                i += 1
                tokens.append(Token("group", ch, tuple(parse(dict(zip("([{", ")]}"))[ch]))))
                continue
            if ch in ")]}":
                if ch != closing:
                    raise ValueError("unbalanced Rust source delimiters")
                i += 1
                return tokens
            name = re.match(r"[A-Za-z_][A-Za-z0-9_]*", source[i:])
            if name:
                tokens.append(Token("ident", name[0]))
                i += name.end()
            else:
                tokens.append(Token("punct", ch))
                i += 1
        if closing is not None:
            raise ValueError("unclosed Rust source delimiter")
        return tokens

    return tuple(parse())


def _arguments(tokens):
    result = [[]]
    for token in tokens:
        if token.text == "," and token.kind == "punct":
            result.append([])
        else:
            result[-1].append(token)
    if result and not result[-1]:
        result.pop()
    return result


def _cfg(tokens):
    if len(tokens) == 1 and tokens[0].text == "test":
        return False
    if len(tokens) == 2 and tokens[1].kind == "group":
        op = tokens[0].text
        values = [_cfg(arg) for arg in _arguments(tokens[1].children)]
        if op == "all":
            return False if False in values else (True if all(v is True for v in values) else None)
        if op == "any":
            return True if True in values else (False if all(v is False for v in values) else None)
        if op == "not" and len(values) == 1:
            return None if values[0] is None else not values[0]
    return None


def _skip_item(tokens, start):
    angle = 0
    for i in range(start, len(tokens)):
        token = tokens[i]
        if token.kind == "punct":
            if token.text == "<":
                angle += 1
            elif token.text == ">":
                angle = max(0, angle - 1)
            elif token.text == ";" and angle == 0:
                return i + 1
        if token.kind == "group" and token.text == "{" and angle == 0:
            return i + 1
    raise ValueError("unsupported cfg-disabled Rust item")


def _production(tokens):
    result = []
    i = 0
    while i < len(tokens):
        inner = tokens[i].text == "#" and i + 1 < len(tokens) and tokens[i + 1].text == "!"
        attribute = i + (2 if inner else 1)
        if (tokens[i].text == "#" and attribute < len(tokens)
                and tokens[attribute].kind == "group" and tokens[attribute].text == "["):
            attr = tokens[attribute].children
            if attr and attr[0].text == "path":
                raise ValueError("Rust #[path] modules need explicit extraction support")
            disabled = bool(attr and attr[0].text == "test")
            if (len(attr) == 2 and attr[0].text == "cfg"
                    and attr[1].kind == "group"):
                disabled = _cfg(attr[1].children) is False
            if disabled and inner:
                return ()
            i = _skip_item(tokens, attribute + 1) if disabled else attribute + 1
        else:
            result.append(tokens[i])
            i += 1
    return tuple(result)


def _constants(tokens, inherited):
    result = dict(inherited)
    local = {}
    for i, token in enumerate(tokens[:-2]):
        if (token.text != "const" or tokens[i + 1].kind != "ident"
                or tokens[i + 2].text != ":"):
            continue
        j = i + 3
        while j < len(tokens) and tokens[j].kind != "group" and tokens[j].text not in ("=", ";"):
            j += 1
        if j == len(tokens) or tokens[j].text != "=":
            continue
        end = j + 1
        while end < len(tokens) and tokens[end].text != ";":
            end += 1
        if end < len(tokens):
            local.setdefault(tokens[i + 1].text, []).append(tokens[j + 1:end])
    result.update(local)
    return result


def _literal(tokens, constants, resolving=()):
    if len(tokens) == 1:
        token = tokens[0]
        if token.kind == "string":
            return token.text
        if token.kind == "ident" and token.text in constants:
            if token.text in resolving:
                raise ValueError("cyclic Rust I18N constant")
            values = {_literal(choice, constants, (*resolving, token.text))
                      for choice in constants[token.text]}
            if len(values) != 1:
                raise ValueError(f"ambiguous Rust I18N constant: {token.text}")
            return values.pop()
    if (len(tokens) == 3 and tokens[0].text == "concat" and tokens[1].text == "!"
            and tokens[2].kind == "group"):
        return "".join(_literal(arg, constants, resolving)
                       for arg in _arguments(tokens[2].children))
    raise ValueError("Rust I18N key/fallback must resolve to a static string")


def _message_call(tokens, i):
    if (tokens[i].text == "tr" and i + 2 < len(tokens) and tokens[i + 1].text == "!"
            and tokens[i + 2].kind == "group"):
        return tokens[i + 2], True, 3
    if (tokens[i].text == "i18n" and i + 4 < len(tokens)
            and [t.text for t in tokens[i + 1:i + 4]] == [":", ":", "text"]
            and tokens[i + 4].kind == "group" and tokens[i + 4].text == "("):
        return tokens[i + 4], False, 5
    return None


def _contains_message(tokens):
    return any(_message_call(tokens, i)
               or (token.kind == "group" and _contains_message(token.children))
               for i, token in enumerate(tokens))


def extract_rust_messages(crates_root: Path) -> dict[str, str]:
    messages = {}
    visited = set()

    def record(arguments, constants, formatted):
        if len(arguments) < 3 or len(arguments[1]) != 1 or arguments[1][0].kind != "string":
            raise ValueError("Rust I18N calls require a literal stable key")
        key = arguments[1][0].text
        if not re.fullmatch(r"[a-z0-9_]+(?:\.[a-z0-9_]+)+", key):
            raise ValueError(f"invalid Rust I18N key: {key}")
        fallback = _literal(arguments[2], constants)
        if formatted:
            fields = set()
            for _, field, spec, conversion in Formatter().parse(fallback):
                if field is None:
                    continue
                if not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", field) or spec or conversion:
                    raise ValueError(f"unsupported Rust I18N placeholder: {key}")
                fields.add(field)
            bindings = []
            for argument in arguments[3:]:
                if len(argument) < 3 or argument[0].kind != "ident" or argument[1].text != "=":
                    raise ValueError(f"invalid Rust I18N binding: {key}")
                bindings.append(argument[0].text)
            if fields != set(bindings) or len(bindings) != len(set(bindings)):
                raise ValueError(f"Rust I18N binding mismatch: {key}")
        if key in messages and messages[key] != fallback:
            raise ValueError(f"conflicting Rust I18N fallbacks: {key}")
        messages[key] = fallback

    def walk(tokens, module_dir, constants):
        tokens = _production(tokens)
        constants = _constants(tokens, constants)
        i = 0
        while i < len(tokens):
            token = tokens[i]
            if (token.text == "macro_rules" and i + 3 < len(tokens)
                    and tokens[i + 3].kind == "group"):
                body = tokens[i + 3].children
                # Message-bearing wrappers need deliberate support, not a
                # guessed expansion with potentially different literals.
                if _contains_message(body):
                    raise ValueError("I18N calls inside macro_rules are unsupported")
                i += 4
                continue
            if token.text == "include" and i + 1 < len(tokens) and tokens[i + 1].text == "!":
                raise ValueError("Rust include! needs explicit extraction support")
            if token.text == "mod" and i + 2 < len(tokens) and tokens[i + 1].kind == "ident":
                name, body = tokens[i + 1].text, tokens[i + 2]
                if body.kind == "group" and body.text == "{":
                    walk(body.children, module_dir / name, {})
                    i += 3
                    continue
                if body.text == ";":
                    candidates = [module_dir / (name + ".rs"), module_dir / name / "mod.rs"]
                    existing = [p for p in candidates if p.is_file()]
                    if len(existing) != 1:
                        raise ValueError(f"missing or ambiguous Rust module: {module_dir / name}")
                    visit(existing[0], module_dir / name)
                    i += 3
                    continue
            call = _message_call(tokens, i)
            if call:
                body, formatted, consumed = call
                record(_arguments(body.children), constants, formatted)
                i += consumed
                continue
            if token.kind == "group":
                walk(token.children, module_dir, constants)
            i += 1

    def visit(path, module_dir):
        path = path.resolve()
        if path in visited:
            return
        visited.add(path)
        try:
            walk(tokenize(path.read_text(encoding="utf-8")), module_dir, {})
        except ValueError as error:
            raise ValueError(f"{path}: {error}") from error

    for crate in sorted(crates_root.glob("*")):
        if not crate.is_dir():
            continue
        manifest = crate / "Cargo.toml"
        if not manifest.is_file():
            continue
        section = ""
        for line in manifest.read_text(encoding="utf-8").splitlines():
            line = line.strip()
            if line.startswith("["):
                section = line.split("#", 1)[0].strip()
            if section in ("[lib]", "[[bin]]") and re.match(r"path\s*=", line):
                match = re.fullmatch(r"""path\s*=\s*["'](src/(?:lib|main)\.rs)["']\s*(?:#.*)?""", line)
                if not match:
                    raise ValueError(f"{manifest}: custom target paths need extraction support")
        roots = [crate / "src/lib.rs", crate / "src/main.rs"]
        roots += sorted((crate / "src/bin").glob("*.rs"))
        roots += sorted((crate / "src/bin").glob("*/main.rs"))
        for root in roots:
            if root.is_file():
                visit(root, root.parent)
    return dict(sorted(messages.items()))


def _template_shape(text: str, named: bool):
    """Split a fallback into literal segments and placeholder fields.

    Returns None for anything the shared-catalog equivalence rule cannot
    reason about. The C++ side must be positioned (`{}` only); the Rust side
    must use plain named fields. Escaped `{{`/`}}` stay literal.
    """
    literals: list[str] = []
    fields: list[str] = []
    literal: list[str] = []
    i = 0
    while i < len(text):
        ch = text[i]
        if ch == "{":
            if text.startswith("{{", i):
                literal.append("{")
                i += 2
                continue
            end = text.find("}", i + 1)
            if end < 0:
                return None
            field = text[i + 1:end]
            if named:
                if not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", field):
                    return None
            elif field != "":
                return None
            literals.append("".join(literal))
            literal = []
            fields.append(field)
            i = end + 1
            continue
        if ch == "}":
            if text.startswith("}}", i):
                literal.append("}")
                i += 2
                continue
            return None
        literal.append(ch)
        i += 1
    literals.append("".join(literal))
    return literals, fields


def _same_message(positioned: str, named: str, fields: tuple[str, ...]) -> bool:
    left = _template_shape(positioned, named=False)
    right = _template_shape(named, named=True)
    if left is None or right is None:
        return False
    if left[0] != right[0]:
        return False
    return (len(left[1]) == len(fields) and len(right[1]) == len(fields)
            and tuple(right[1]) == fields)


def merge_rust_messages(messages: dict[str, str],
                        rust_messages: dict[str, str]) -> dict[str, str]:
    """Merge Rust fallbacks into the English catalog in place.

    Identical fallbacks collapse. A differing fallback needs a
    POSITIONAL_TO_NAMED annotation AND the same literal shape with the
    annotated named fields in order; the catalog keeps the positioned C++
    text so `winux::i18n::format` stays valid. Everything else is the same
    hard "conflicting C++/Rust I18N fallbacks" failure as before.
    """
    for key, fallback in rust_messages.items():
        if key not in messages:
            messages[key] = fallback
            continue
        if messages[key] == fallback:
            continue
        fields = POSITIONAL_TO_NAMED.get(key)
        if fields is None:
            raise ValueError(f"conflicting C++/Rust I18N fallbacks: {key}")
        if not _same_message(messages[key], fallback, fields):
            raise ValueError(
                f"conflicting C++/Rust I18N fallbacks: {key} "
                f"(positioned {messages[key]!r} vs named {fallback!r})"
            )
    return messages
