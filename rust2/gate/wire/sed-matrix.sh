#!/usr/bin/env bash
# sed wiring-wave argument matrix (#1488, see rust2/REPLACEMENT-RECIPE.md).
# Usage: sed-matrix.sh OUTDIR gnu                  (under WSL: GNU sed 4.9)
#        sed-matrix.sh OUTDIR exe <winuxcmd.exe>   (under Git Bash: product)
# Emits ledger.tsv: case|rc|out_sha|err_sha|tree_sha plus per-case .out/.err
# files, so the same table can be run for the pre-wiring baseline exe, the
# wired product exe and the GNU oracle, then joined with compare.sh.
#
# Every case runs in its own fresh scratch directory: the setup snippet
# materializes the fixture, the command runs with stdin </dev/null unless
# the stdin column carries bytes, and the post-run tree hash covers the
# -i/-e write-file file-system effects (relative paths only).
set -u
OUT="$1"; MODE="$2"
EXE="${3:-}"
mkdir -p "$OUT"
LEDGER="$OUT/ledger.tsv"
: > "$LEDGER"
SCRATCH="$OUT/scratch"
rm -rf "$SCRATCH"

tree_sha() {
  # sorted relative paths + content hashes, excluding stdout/stderr captures
  ( cd "$1" && find . -type f ! -name 'case.out' ! -name 'case.err' -print0 \
      | sort -z | xargs -0 -r sha256sum ) | sha256sum | cut -d' ' -f1
}

run_case() {
  local id="$1" envspec="$2" stdin_data="$3" setup="$4"; shift 4
  local dir="$SCRATCH/$id"
  rm -rf "$dir"; mkdir -p "$dir"
  ( cd "$dir" && eval "$setup" ) >/dev/null 2>&1
  local o="$OUT/$id.out" e="$OUT/$id.err" rc
  local cmd
  if [ "$MODE" = gnu ]; then
    cmd=(/usr/bin/sed)
  else
    cmd=("$EXE" sed)
  fi
  if [ -n "$stdin_data" ]; then
    ( cd "$dir" && printf '%s' "$stdin_data" | env $envspec "${cmd[@]}" "$@" ) >"$o" 2>"$e"
  else
    ( cd "$dir" && env $envspec "${cmd[@]}" "$@" </dev/null ) >"$o" 2>"$e"
  fi
  rc=$?
  printf '%s|%s|%s|%s|%s\n' "$id" "$rc" \
    "$(sha256sum "$o" | cut -d' ' -f1)" "$(sha256sum "$e" | cut -d' ' -f1)" \
    "$(tree_sha "$dir")" >>"$LEDGER"
}

# id|env|stdin|setup|args (':' joins argv; @EMPTY@ is an empty argument)
CASES=$(cat <<'TABLE'
m01|||printf 'one\ntwo\nthree\nfour\nfive\n' > f|p:f
m02|||printf 'one\ntwo\nthree\nfour\nfive\n' > f|-n:2p:f
m03|||printf 'one\ntwo\nthree\nfour\nfive\n' > f|-n:2,4p:f
m04|||printf 'one\ntwo\nthree\nfour\nfive\n' > f|-n:\,t,p:f
m05|||printf 'one\ntwo\nthree\nfour\nfive\n' > f|-n:1h;2x;p:f
m06|||printf 'one\ntwo\nthree\n' > f|-n:1h;2G;p:f
m07|||printf 'one\ntwo\nthree\n' > f|-n:1h;2H;3x;p:f
m08|||printf 'one\ntwo\nthree\nfour\nfive\n' > f|-e:1p:-e:s/t/T/:-e:$p:f
m09|||printf 'one\ntwo\nthree\nfour\nfive\n' > f;printf '2d\ns/e/E/g\n' > s.sed|-f:s.sed:f
m10|||printf 'a\nb\nc\n' > f|-n:2q:f
m11|||printf 'ab\ncd\n' > f|-i:-e:s/a/A/:f
m12|||printf 'ab\ncd\n' > f|-i.bak:-e:s/c/C/:f
m13|||printf 'x1\nx2\n' > f1;printf 'y1\ny2\n' > f2|-s:-n:$p:f1:f2
m14|||printf 'a\0b\0' > f|-z:-n:$=:f
m15|||printf 'abcabc\n' > f|-E:s/(b)(c)/\2\1/g:f
m16|||printf 'abcdefghij\n' > f|-l:4:-n:l:f
m17|||printf '1\n2\n3\n' > f|-n:N;s/\n/-/:f
m18|||printf 'abc\n' > f|y/abc/xyz/:f
m19|||printf 'a\nb\nc\nd\n' > f|2b;s/^/>/;T:f
m20|||printf 'one\ntwo\n' > f|s/e/E/w /dev/stdout:f
m21|||printf 'one\ntwo\n' > f;printf 'READ\n' > r.txt|r r.txt:f
m22|||printf 'one\ntwo\n' > f|w out.txt:f
m23|||printf 'one\ntwo\n' > f|-u:s/o/0/:f
m24|||printf 'a+b\n' > f|--posix:s/a+b/X/:f
m25|||one|s/o/0/
m26|||one|s/o/0/:-
m27|||printf 'one\ntwo\n' > f|-n:$p:f
m28|||printf 'a\nb\nc\n' > f|-n:1,2{p;d};3p:f
TABLE
)

# Field layout read on fd 3 so a subject reading stdin cannot swallow rows.
while IFS='|' read -r id envspec stdin_data setup argspec <&3; do
  IFS=':' read -r -a args <<<"${argspec:-}"
  if [ "${#args[@]}" -eq 1 ] && [ -z "${args[0]}" ]; then args=(); fi
  # @EMPTY@ -> a real empty argument
  for i in "${!args[@]}"; do
    [ "${args[$i]}" = "@EMPTY@" ] && args[$i]=""
  done
  run_case "$id" "$envspec" "$stdin_data" "$setup" "${args[@]+"${args[@]}"}"
done 3<<ROWS
$CASES
ROWS

echo "ledger at $LEDGER"
