#!/usr/bin/env bash
set -u
set -o pipefail

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
CORPUS="${CORPUS:-$ROOT}"
ONLY=""
REPORT="$ROOT/report.md"
RESULTS_FILE=""
COMMANDS_FILE="$ROOT/commands.txt"
WINUXCMD_BIN="${WINUXCMD_BIN:-}"
WINUXCMD_EXE="${WINUXCMD_EXE:-winuxcmd.exe}"
GNU_BIN="${GNU_BIN:-}"
BASELINE="$ROOT/baseline.json"
WHITELIST="$ROOT/whitelist.txt"

while [ "$#" -gt 0 ]; do
  case "$1" in
    --corpus) CORPUS=$2; shift 2 ;;
    --report) REPORT=$2; shift 2 ;;
    --results) RESULTS_FILE=$2; shift 2 ;;
    --only) ONLY=$2; shift 2 ;;
    --baseline) BASELINE=$2; shift 2 ;;
    -h|--help) printf '%s\n' 'runner.sh [--corpus DIR] [--report FILE] [--only CMD]'; exit 0 ;;
    *) echo "unknown option: $1" >&2; exit 2 ;;
  esac
done

# Load the whitelist of known differential gaps (case path<TAB>reason).
# These cases are reported as KNOWN_DIFF instead of failing the run.
declare -A KNOWN_DIFFS
if [ -f "$WHITELIST" ]; then
  while IFS=$'\t' read -r case_path reason || [ -n "$case_path" ]; do
    [ -z "$case_path" ] && continue
    case "$case_path" in \#*) continue ;; esac
    KNOWN_DIFFS["$case_path"]="$reason"
  done < "$WHITELIST"
fi

# Platform of the GNU oracle runtime. A few GNU-visible behaviors are produced
# by the POSIX runtime rather than by coreutils itself: the diagnostic for a
# closed stdin, for example, is emitted by the Cygwin/MSYS2 read() shim
# ("failed to set file descriptor text/binary mode") before cat(1) ever reaches
# its own error path. Such cases cannot be compared against an MSYS2 oracle at
# any coreutils version. They declare `oracle: linux` and are SKIPped here,
# which keeps KNOWN_DIFF meaning "executed, results differ" instead of
# "could not be executed".
case "$(uname -s 2>/dev/null)" in
  Linux) ORACLE_PLATFORM=linux ;;
  MINGW*|MSYS*|CYGWIN*) ORACLE_PLATFORM=msys ;;
  *) ORACLE_PLATFORM=unknown ;;
esac

find_oracle() {
  if [ -n "$GNU_BIN" ] && [ -x "$GNU_BIN/$1" ]; then
    printf '%s/%s' "$GNU_BIN" "$1"
  elif [ -n "$GNU_BIN" ] && [ -x "$GNU_BIN/$1.exe" ]; then
    printf '%s/%s.exe' "$GNU_BIN" "$1"
  else
    command -v "$1" 2>/dev/null || command -v "$1.exe" 2>/dev/null || true
  fi
}

find_timeout() {
  if [ -n "$GNU_BIN" ] && [ -x "$GNU_BIN/timeout.exe" ]; then
    printf '%s/timeout.exe' "$GNU_BIN"
  elif [ -n "$GNU_BIN" ] && [ -x "$GNU_BIN/timeout" ]; then
    printf '%s/timeout' "$GNU_BIN"
  else
    command -v timeout 2>/dev/null || true
  fi
}

absolute_command() {
  local value=$1
  if [[ "$value" =~ ^[A-Za-z]:[\\/] ]]; then
    value=$(cygpath -u "$value")
  fi
  if [[ "$value" == */* ]]; then
    if [[ "$value" != /* ]]; then value="$PWD/$value"; fi
    (CDPATH= cd -- "$(dirname "$value")" && printf '%s/%s' "$PWD" "$(basename "$value")")
  else
    command -v "$value" 2>/dev/null || true
  fi
}

shell_quote() {
  printf "'%s'" "${1//\'/\'\\''}"
}

run_case() {
  local case_file=$1 work wdir gdir line block cmd args timeout setup stdin files case_oracle
  work=$(mktemp -d) || return 2
  wdir="$work/w"; gdir="$work/g"; mkdir -p "$wdir" "$gdir"
  cmd=""; args=""; timeout=10; setup=""; stdin=""; files=""; block=""; case_oracle=""
  while IFS= read -r line || [ -n "$line" ]; do
    if [ -n "$block" ]; then
      if [[ "$line" == "  "* ]]; then
        if [ "$block" = setup ]; then setup+="${line#  }\n"; else stdin+="${line#  }\n"; fi
        continue
      fi
      block=""
    fi
    case "$line" in
      cmd:*) cmd=${line#cmd: };;
      args:*) args=${line#args:}; args=${args# };;
      timeout:*) timeout=${line#timeout: };;
      files:*) files=${line#files: };;
      oracle:*) case_oracle=${line#oracle: }; case_oracle=${case_oracle# };;
      setup:\ \|) block=setup;;
      stdin:\ \|) block=stdin;;
    esac
  done < "$case_file"
  if [ -z "$cmd" ]; then
    rm -rf "$work"
    printf '%s\t%s\tSKIP\n' "${case_file#$ROOT/}" "$cmd"
    return 0
  fi
  if [ -n "$case_oracle" ] && [ "$case_oracle" != "$ORACLE_PLATFORM" ]; then
    rm -rf "$work"
    printf '%s\t%s\tSKIP\n' "${case_file#$ROOT/}" "$cmd"
    return 0
  fi
  [ -z "$setup" ] || {
    (cd "$wdir" && printf '%b' "$setup" | sh >/dev/null 2>&1)
    (cd "$gdir" && printf '%b' "$setup" | sh >/dev/null 2>&1)
  }
  local wout="$work/w.out" werr="$work/w.err" gout="$work/g.out" gerr="$work/g.err"
  local wrc grc wcmd gcmd bucket case_args timeout_cmd wname gname wuses_dispatcher
  wcmd="$WINUXCMD_EXE"
  wuses_dispatcher=false
  if [ -n "$WINUXCMD_BIN" ]; then
    if [ -x "$WINUXCMD_BIN/$cmd.exe" ]; then
      wcmd="$WINUXCMD_BIN/$cmd.exe"
    else
      wcmd="$WINUXCMD_BIN/$WINUXCMD_EXE"
      wuses_dispatcher=true
    fi
  fi
  gcmd=$(find_oracle "$cmd")
  if [ ! -x "$gcmd" ]; then
    rm -rf "$work"
    printf '%s\t%s\tSKIP\n' "${case_file#$ROOT/}" "$cmd"
    return 0
  fi
  wcmd=$(absolute_command "$wcmd")
  gcmd=$(absolute_command "$gcmd")
  if [ -z "$wcmd" ] || [ -z "$gcmd" ]; then
    rm -rf "$work"
    printf '%s\t%s\tSKIP\n' "${case_file#$ROOT/}" "$cmd"
    return 0
  fi
  timeout_cmd=$(find_timeout)
  if [ -z "$timeout_cmd" ]; then
    rm -rf "$work"
    printf '%s\t%s\tSKIP\n' "${case_file#$ROOT/}" "$cmd"
    return 0
  fi
  case_args=$args
  # Case arguments are intentionally shell-like and come from the checked-in
  # corpus. Keep setup and both command sides in the same isolated directory.
  # The checked-in case format intentionally uses shell quoting for args.
  # Evaluate only this trusted case field so quoted operands stay one argv.
  local wquoted gquoted
  wname=$(basename "$wcmd")
  local wext="${wname##*.}"
  if [ "$wext" = "$wname" ]; then
    wname=$cmd
  fi
  gname=$(basename "$gcmd")
  local gext="${gname##*.}"
  if [ "$gext" != "$gname" ]; then
    gname="$gname"
  else
    gname=$cmd
  fi
  wquoted=$(shell_quote "$wname")
  gquoted=$(shell_quote "$gname")
  local wcommand="exec $wquoted $case_args"
  if [ "$wuses_dispatcher" = true ]; then
    wcommand="exec $wquoted $(shell_quote "$cmd") $case_args"
  fi
  local gcommand="exec $gquoted $case_args"
  (cd "$wdir" && export PATH="$(dirname "$wcmd"):$PATH" WINUX_LANG=en && printf '%b' "$stdin" | MSYS2_ARG_CONV_EXCL='*' MSYS_NO_PATHCONV=1 \
    "$timeout_cmd" "$timeout" bash -c "$wcommand" >"$wout" 2>"$werr"); wrc=$?
  (cd "$gdir" && export PATH="$(dirname "$gcmd"):$PATH" && printf '%b' "$stdin" | MSYS2_ARG_CONV_EXCL='*' MSYS_NO_PATHCONV=1 \
    "$timeout_cmd" "$timeout" bash -c "$gcommand" >"$gout" 2>"$gerr"); grc=$?
  files_equal=true
  if [ -n "$files" ]; then
    IFS=',' read -ra expected_files <<< "$files"
    for expected in "${expected_files[@]}"; do
      if [ ! -f "$wdir/$expected" ] || [ ! -f "$gdir/$expected" ] ||
         ! cmp -s "$wdir/$expected" "$gdir/$expected"; then
        files_equal=false
        break
      fi
    done
  fi
  if [ "$wrc" -ne "$grc" ]; then
    bucket=EXIT_DIFF
  elif cmp -s "$wout" "$gout" && cmp -s "$werr" "$gerr" && [ "$files_equal" = true ]; then
    bucket=PASS
  elif cmp -s <(tr -d '\r' < "$wout") <(tr -d '\r' < "$gout") &&
       cmp -s <(tr -d '\r' < "$werr") <(tr -d '\r' < "$gerr") &&
       [ "$files_equal" = true ]; then
    bucket=CRLF_DIFF
  else
    bucket=OUT_DIFF
  fi
  if [ "$bucket" != PASS ] && [ -n "${KNOWN_DIFFS["${case_file#$ROOT/}"]+x}" ]; then
    bucket=KNOWN_DIFF
  fi
  printf '%s\t%s\t%s\n' "${case_file#$ROOT/}" "$cmd" "$bucket"
  rm -rf "$work"
}

oracle_cmd=$(find_oracle cat)
oracle=$({ "$oracle_cmd" --version 2>/dev/null || true; } | head -1 | tr -d '\r')
if [ -z "$oracle" ]; then
  echo "ERROR: GNU coreutils oracle not found" >&2
  exit 2
fi
timeout_cmd=$(find_timeout)
if [ -z "$timeout_cmd" ]; then
  echo "ERROR: GNU timeout oracle not found; set GNU_BIN to a GNU coreutils bin directory" >&2
  exit 2
fi
case_count=$(find "$CORPUS" -type f -name '*.case' | wc -l)
if [ "$case_count" -eq 0 ]; then
  echo "ERROR: differential corpus is empty: $CORPUS" >&2
  exit 2
fi
printf '# Differential report\n\nOracle: `%s`\n\n| Case | Result |\n|---|---|\n' "$oracle" > "$REPORT"
results=$(mktemp)
find "$CORPUS" -type f -name '*.case' | sort | while IFS= read -r file; do
  if [ -n "$ONLY" ] && ! awk -v wanted="$ONLY" '$0 == "cmd: " wanted { found=1 } END { exit !found }' "$file"; then
    continue
  fi
  run_case "$file"
done | tee "$results" >> "$REPORT"
if [ -n "$RESULTS_FILE" ]; then
  cp "$results" "$RESULTS_FILE"
fi
printf '\n## Summary\n\n' >> "$REPORT"
awk -F '\t' '{ total++; count[$3]++ } END { for (k in count) printf "- %s: %d/%d\n", k, count[k], total }' "$results" >> "$REPORT"
printf '\n## By command\n\n| Command | PASS | Executed | Rate |\n|---|---:|---:|---:|\n' >> "$REPORT"
awk -F '\t' '
  {
    command = $2;
    executed[command]++;
    if ($3 == "PASS") passed[command]++;
  }
  END {
    for (command in executed) {
      rate = executed[command] ? (100 * passed[command] / executed[command]) : 0;
      printf "| %s | %d | %d | %.1f%% |\n", command, passed[command] + 0,
             executed[command], rate;
    }
  }' "$results" | sort >> "$REPORT"
if [ -f "$COMMANDS_FILE" ]; then
  printf '\n## Command coverage\n\n' >> "$REPORT"
  awk -F '\t' '
    NR == FNR { expected[$1] = 1; next }
    { actual[$2] = 1 }
    END {
      covered = 0
      for (command in expected) {
        if (actual[command]) covered++
        else missing = missing (missing ? ", " : "") command
      }
      printf "Declared commands: %d\\nCovered commands: %d\\nMissing commands: %s\\n",
             length(expected), covered, (missing ? missing : "none")
    }' "$COMMANDS_FILE" "$results" >> "$REPORT"
fi
total=$(wc -l < "$results")
pass=$(awk -F '\t' '$3 == "PASS" { count++ } END { print count + 0 }' "$results")
skip=$(awk -F '\t' '$3 ~ /^SKIP/ { count++ } END { print count + 0 }' "$results")
known=$(awk -F '\t' '$3 == "KNOWN_DIFF" { count++ } END { print count + 0 }' "$results")
diffs=$((total - pass - skip - known))
printf '\nCases executed: %d\nPASS: %d\nKNOWN_DIFF: %d\nSKIP: %d\nDifferences: %d\n' "$total" "$pass" "$known" "$skip" "$diffs" >> "$REPORT"
if [ "$total" -eq 0 ]; then
  echo "ERROR: no cases selected (corpus=$CORPUS only=$ONLY)" >&2
  rm -f "$results"
  exit 2
fi
if [ "$diffs" -gt 0 ]; then
  echo "ERROR: $diffs unexpected differential case(s) did not match" >&2
  rm -f "$results"
  exit 1
fi
if [ -n "$BASELINE" ] && [ -s "$BASELINE" ] && [ -n "$RESULTS_FILE" ]; then
  python3 "$ROOT/compare-baseline.py" "$BASELINE" "$RESULTS_FILE" || exit 1
fi
rm -f "$results"
