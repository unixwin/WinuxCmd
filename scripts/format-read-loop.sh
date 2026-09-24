#!/usr/bin/env sh
set -e

case "$0" in
  */*) script_dir=${0%/*} ;;
  *) script_dir=. ;;
esac

start_dir=$(pwd)
cd "$script_dir"
script_dir=$(pwd)
cd "$start_dir"

if [ $# -gt 0 ]; then
  root_dir=$1
else
  cd "$script_dir/.."
  root_dir=$(pwd)
  cd "$start_dir"
fi
suffixes=${SUFFIXES:-".h .cc .cpp .hpp .cxx .hxx .C .cppm"}

find_clang_format() {
  if [ -n "${CLANG_FORMAT:-}" ]; then
    if [ -x "$CLANG_FORMAT" ] || command -v "$CLANG_FORMAT" >/dev/null 2>&1; then
      printf '%s\n' "$CLANG_FORMAT"
      return 0
    fi
  fi

  for name in clang-format clang-format.exe; do
    if command -v "$name" >/dev/null 2>&1; then
      printf '%s\n' "$name"
      return 0
    fi
  done

  return 1
}

has_source_suffix() {
  file=$1
  for suffix in $suffixes; do
    case "$file" in
      *"$suffix") return 0 ;;
    esac
  done
  return 1
}

clang_format=$(find_clang_format) || {
  printf '%s\n' "Error: clang-format not found. Install it, add it to PATH, or set CLANG_FORMAT." >&2
  exit 1
}

cd "$root_dir"

if ! git rev-parse --show-toplevel >/dev/null 2>&1; then
  printf '%s\n' "Error: format-read-loop.sh must run inside the WinuxCmd git checkout." >&2
  exit 1
fi

printf 'Using %s\n' "$("$clang_format" --version)"
printf '%s\n' "Searching for files..."

count=0
git_dir=$(git rev-parse --git-dir)
file_list="$git_dir/winuxcmd-format-$$.lst"
trap 'rm -f "$file_list"' EXIT
git ls-files -co --exclude-standard > "$file_list"

while IFS= read -r file; do
  case "$file" in
    third_party/* | */third_party/* | third-party/* | */third-party/*) continue ;;
    src/utils/json.hpp) continue ;;
  esac

  if ! has_source_suffix "$file"; then
    continue
  fi

  if [ ! -f "$file" ]; then
    continue
  fi

  count=$((count + 1))
  printf 'Formatting: %s\n' "$file"
  "$clang_format" -i -style=file "$file"
done < "$file_list"

if [ "$count" -eq 0 ]; then
  printf '%s\n' "No files found that need formatting"
else
  printf 'Formatting complete: %s files\n' "$count"
fi
