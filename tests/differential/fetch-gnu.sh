#!/usr/bin/env bash
set -euo pipefail

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
META="$ROOT/versions.json"
DEST="${1:-$ROOT/.cache/coreutils-9.7}"

version=$(sed -n 's/.*"version": "\([^"]*\)".*/\1/p' "$META" | head -1)
url=$(sed -n 's/.*"source": "\([^"]*\)".*/\1/p' "$META" | head -1)
if [ -z "$version" ] || [ -z "$url" ]; then
  echo "invalid oracle metadata: $META" >&2
  exit 2
fi
if [ -d "$DEST/tests" ] && [ -d "$DEST/src" ]; then
  printf '%s\n' "$DEST"
  exit 0
fi
archive="${TMPDIR:-/tmp}/coreutils-${version}.tar.xz"
mkdir -p "$(dirname "$DEST")"
if [ ! -f "$archive" ]; then
  curl.exe -L --fail --silent --show-error "$url" -o "$archive"
fi
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
tar -xJf "$archive" -C "$tmp"
mv "$tmp/coreutils-${version}" "$DEST"
printf '%s\n' "$DEST"
