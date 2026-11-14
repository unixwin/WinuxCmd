#!/usr/bin/env bash
# Materialize the pinned GNU master oracle (coreutils + gnulib) for the
# `GNU master differential` workflow.
#
# Why this script exists
# ----------------------
# The oracle job used to run `git submodule update --init --depth=1` against
# gnulib's canonical host (git.savannah.gnu.org). That host intermittently
# answers an HTTP 500 in the middle of the packfile
#
#   error: RPC failed; HTTP 500 curl 22 The requested URL returned error: 500
#   fatal: Fetched in submodule path 'gnulib', but it did not contain <sha>
#
# which reds the oracle leg and skips `compare`, so a PR gets no differential
# signal at all. The failure is transport-only: the same commit is available
# from other transports.
#
# Source order (fixed contract, see DOCS/gate-oracle-policy.md)
# ------------------------------------------------------------
#   coreutils: GitHub mirror -> savannah upstream
#   gnulib:    savannah upstream -> GitHub mirror -> cgit snapshot
#              -> vendored archive under rust2/gate/vendor/
#
# Every source is *verified against the anchor* before it is used:
#   * a git worktree must resolve to exactly the anchored commit;
#   * an archive must match the anchored sha256 (vendored) and/or reproduce
#     the anchored content fingerprint of the tree (mirror snapshot).
# A source that cannot prove the revision is rejected, never silently
# accepted. That is the guard against a stale mirror serving an older tree
# and producing a green oracle record for a different revision - the same
# "stale result overwrites the fresh one" class that invalidated gate01
# (DOCS/2.0-HANDOVER.md, "零、续作检查点"). A mismatch rejects that source
# outright (no retry, no acceptance); if every source is rejected the job
# fails rather than falling back to "whatever the mirror had".
#
# Retry policy (written here on purpose, not implicit in the workflow)
# -------------------------------------------------------------------
#   * RETRY_ATTEMPTS=2 per source, backoff 5 s then 20 s, then move on;
#   * per-attempt hard ceiling ATTEMPT_TIMEOUT=180 s (git transfers also abort
#     after 60 s below 1 kB/s, so a stalled savannah connection cannot eat the
#     whole job);
#   * a 600 s budget covers all network sources, so a bad upstream day falls
#     through to the vendored archive instead of exhausting the job timeout;
#   * transport errors are retried, revision mismatches are NOT retried (a
#     stale mirror is not a transient error).
#
# Output
# ------
# The script materializes DEST (default $RUNNER_TEMP/coreutils) and writes
# <parent of DEST>/gnu-master-sources.json recording which source and which
# revisions were used. On success it prints `export GNULIB_SRCDIR=...` on
# stdout so the caller can `eval` it; all diagnostics go to stderr.
set -euo pipefail

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
DEST=${1:-${RUNNER_TEMP:-/tmp}/coreutils}
ANCHOR=${GNULIB_ANCHOR:-"$ROOT/rust2/gate/vendor/gnulib.anchor.json"}
PROVENANCE=${PROVENANCE:-"$(dirname -- "$DEST")/gnu-master-sources.json"}

RETRY_ATTEMPTS=${RETRY_ATTEMPTS:-2}
RETRY_BACKOFF_SECONDS=${RETRY_BACKOFF_SECONDS:-"5 20"}
ATTEMPT_TIMEOUT=${ATTEMPT_TIMEOUT:-180}
SOURCE_BUDGET_SECONDS=${SOURCE_BUDGET_SECONDS:-600}
GIT_LOW_SPEED_LIMIT=${GIT_LOW_SPEED_LIMIT:-1000}
GIT_LOW_SPEED_TIME=${GIT_LOW_SPEED_TIME:-60}
STARTED_AT=$(date +%s)

COREUTILS_SOURCES=(
  "github-mirror:https://github.com/coreutils/coreutils.git"
  "savannah-upstream:https://git.savannah.gnu.org/git/coreutils.git"
)
GNULIB_SOURCES=(
  "savannah-upstream:https://git.savannah.gnu.org/git/gnulib.git"
  "github-mirror:https://github.com/coreutils/gnulib.git"
  "cgit-snapshot:https://git.savannah.gnu.org/cgit/gnulib.git/snapshot/gnulib-REVISION.tar.gz"
  "vendored-archive:VENDORED"
)

log() { printf '[gnu-oracle] %s\n' "$*" >&2; }
die() { log "ERROR: $*"; exit 1; }

[ -f "$ANCHOR" ] || die "anchor file not found: $ANCHOR"
command -v python3 >/dev/null 2>&1 || die "python3 is required (tree fingerprint)"

# The anchor is the single source of truth for both revisions and for the
# vendored bytes. Nothing in this script hardcodes a sha.
eval "$(python3 - "$ANCHOR" <<'PY'
import json, sys
a = json.load(open(sys.argv[1], encoding="utf-8"))
for key in ("coreutils_revision", "gnulib_revision", "vendored_archive",
            "vendored_sha256", "tree_sha256"):
    if not a.get(key):
        sys.exit(f"anchor {sys.argv[1]} is missing {key}")
print("COREUTILS_REVISION=%s" % a["coreutils_revision"])
print("GNULIB_REVISION=%s" % a["gnulib_revision"])
print("VENDORED_ARCHIVE=%s" % a["vendored_archive"])
print("VENDORED_SHA256=%s" % a["vendored_sha256"])
print("TREE_SHA256=%s" % a["tree_sha256"])
PY
)"
GNULIB_DIR="$DEST/gnulib"
SCRATCH=$(mktemp -d)
trap 'rm -rf "$SCRATCH"' EXIT

tree_fingerprint() {
  python3 - "$1" <<'PY'
import hashlib, os, sys
root = sys.argv[1]
rows = []
for base, dirs, files in os.walk(root):
    dirs[:] = sorted(d for d in dirs if d != ".git")
    for name in sorted(files):
        path = os.path.join(base, name)
        rel = os.path.relpath(path, root).replace(os.sep, "/")
        if os.path.islink(path):
            rows.append("%s\tlink\t%s" % (rel, os.readlink(path)))
            continue
        if not os.path.isfile(path):
            continue
        h = hashlib.sha256()
        with open(path, "rb") as fh:
            for chunk in iter(lambda: fh.read(1 << 20), b""):
                h.update(chunk)
        rows.append("%s\t%d\t%s" % (rel, os.path.getsize(path), h.hexdigest()))
rows.sort()
digest = hashlib.sha256("\n".join(rows).encode()).hexdigest()
print(digest)
PY
}

verify_tree() { # dir, label
  local actual
  actual=$(tree_fingerprint "$1")
  if [ "$actual" != "$TREE_SHA256" ]; then
    log "$2: tree fingerprint mismatch (expected $TREE_SHA256, got $actual)"
    return 1
  fi
  log "$2: tree fingerprint ok ($actual)"
}

verify_git_revision() { # dir, expected, label
  local actual
  actual=$(git -C "$1" rev-parse HEAD 2>/dev/null) || {
    log "$3: no git revision in $1"; return 1; }
  if [ "$actual" != "$2" ]; then
    # A stale mirror is not a transient error: retrying it is pointless and
    # accepting it would anchor the oracle to the wrong tree.
    log "$3: revision mismatch (expected $2, got $actual) - rejecting source"
    return 2
  fi
  log "$3: revision verified ($actual)"
}

git_retry() { # label, function
  local label=$1 fn=$2 attempt delay backoff elapsed
  backoff=($RETRY_BACKOFF_SECONDS)
  for attempt in $(seq 1 "$RETRY_ATTEMPTS"); do
    if [ "${SRC_LOCAL:-0}" != 1 ]; then
      elapsed=$(( $(date +%s) - STARTED_AT ))
      if [ "$elapsed" -ge "$SOURCE_BUDGET_SECONDS" ]; then
        log "$label: network source budget ${SOURCE_BUDGET_SECONDS}s exhausted; not starting another attempt"
        return 1
      fi
    fi
    log "$label: attempt $attempt/$RETRY_ATTEMPTS"
    local rc=0
    "$fn" || rc=$?
    if [ "$rc" -eq 0 ]; then return 0; fi
    if [ "$rc" -eq 2 ]; then
      log "$label: source rejected (stale/mismatched); not retrying"
      return 1
    fi
    if [ "$attempt" -lt "$RETRY_ATTEMPTS" ]; then
      delay=${backoff[$((attempt - 1))]:-20}
      log "$label: attempt $attempt failed; retrying in ${delay}s"
      sleep "$delay"
    fi
  done
  return 1
}

fetch_coreutils_git() {
  rm -rf "$DEST"
  mkdir -p "$DEST"
  git -C "$DEST" init -q
  git -C "$DEST" remote add origin "$SRC_URL"
  # No --depth: coreutils derives its version from `git describe`, so the
  # tag history must be present or configure reports UNKNOWN. Fetching the
  # single revision without depth still walks its ancestry; --tags makes the
  # v* tags available for describe.
  timeout -k 15 "$ATTEMPT_TIMEOUT" git -c http.version=HTTP/1.1 \
    -c http.lowSpeedLimit="$GIT_LOW_SPEED_LIMIT" \
    -c http.lowSpeedTime="$GIT_LOW_SPEED_TIME" \
    -C "$DEST" fetch --tags origin "$COREUTILS_REVISION" || return 1
  git -C "$DEST" checkout -q --detach FETCH_HEAD || return 1
  verify_git_revision "$DEST" "$COREUTILS_REVISION" "coreutils/$SRC_LABEL" || return $?
}

fetch_gnulib_git() {
  rm -rf "$GNULIB_DIR"
  mkdir -p "$GNULIB_DIR"
  git -C "$GNULIB_DIR" init -q
  git -C "$GNULIB_DIR" remote add origin "$SRC_URL"
  if ! timeout -k 15 "$ATTEMPT_TIMEOUT" git -c http.version=HTTP/1.1 \
        -c http.lowSpeedLimit="$GIT_LOW_SPEED_LIMIT" \
        -c http.lowSpeedTime="$GIT_LOW_SPEED_TIME" \
        -C "$GNULIB_DIR" fetch --no-tags --depth=1 origin "$GNULIB_REVISION"; then
    # Some servers refuse fetch-by-sha; retry the whole history once before
    # giving up on this source. Still bounded by ATTEMPT_TIMEOUT.
    timeout -k 15 "$ATTEMPT_TIMEOUT" git -c http.version=HTTP/1.1 \
      -c http.lowSpeedLimit="$GIT_LOW_SPEED_LIMIT" \
      -c http.lowSpeedTime="$GIT_LOW_SPEED_TIME" \
      -C "$GNULIB_DIR" fetch --no-tags origin \
      '+refs/heads/*:refs/remotes/origin/*' || return 1
  fi
  git -C "$GNULIB_DIR" checkout -q --detach FETCH_HEAD 2>/dev/null ||
    git -C "$GNULIB_DIR" checkout -q --detach "$GNULIB_REVISION" || return 1
  verify_git_revision "$GNULIB_DIR" "$GNULIB_REVISION" "gnulib/$SRC_LABEL" || return $?
  # A git checkout carries .git, the fingerprint ignores it; verify the tree
  # too so a mirror that rewrote history under the same sha is caught.
  verify_tree "$GNULIB_DIR" "gnulib/$SRC_LABEL" || return 1
}

fetch_gnulib_archive() { # url-or-file, kind
  local src=$1 kind=$2 archive="$SCRATCH/gnulib.tar.gz"
  rm -f "$archive"
  case "$kind" in
    url)
      timeout -k 15 "$ATTEMPT_TIMEOUT" curl -fsSL --connect-timeout 20 \
        --max-time "$ATTEMPT_TIMEOUT" -o "$archive" "$src" || return 1
      ;;
    file)
      [ -f "$src" ] || { log "vendored archive missing: $src"; return 1; }
      cp "$src" "$archive"
      ;;
  esac
  echo "$VENDORED_SHA256  $archive" | sha256sum -c - >/dev/null 2>&1 || {
    # The vendored bytes have an anchored digest; a mirror snapshot does not,
    # so only the vendored source can be rejected here.
    if [ "$kind" = file ]; then
      log "vendored archive sha256 mismatch - the anchor and the payload disagree"
      return 2
    fi
    log "$src: archive digest differs from the vendored payload (expected for a different transport); verifying the tree instead"
  }
  rm -rf "$GNULIB_DIR"
  mkdir -p "$GNULIB_DIR"
  tar -xzf "$archive" -C "$GNULIB_DIR" --strip-components=1 || return 1
  [ -f "$GNULIB_DIR/gnulib-tool" ] || { log "$src: archive has no gnulib-tool"; return 1; }
  verify_tree "$GNULIB_DIR" "gnulib/$SRC_LABEL" || return $?
}

try_sources() { # name, array-name, function
  local name=$1 array_name=$2 fn=$3 entry
  local -n sources=$array_name
  for entry in "${sources[@]}"; do
    SRC_LABEL=${entry%%:*}
    SRC_URL=${entry#*:}
    SRC_LOCAL=0
    case "$name:$SRC_LABEL" in
      gnulib:cgit-snapshot)
        SRC_URL=${SRC_URL/REVISION/$GNULIB_REVISION}
        ;;
      gnulib:vendored-archive)
        SRC_URL="$ROOT/$VENDORED_ARCHIVE"
        SRC_LOCAL=1
        ;;
    esac
    log "trying $name source $SRC_LABEL ($SRC_URL)"
    if [ "$name" = coreutils ]; then
      git_retry "$name/$SRC_LABEL" "$fn" && {
        printf '%s\n' "$SRC_LABEL" > "$SCRATCH/coreutils-source"; return 0; }
    else
      git_retry "$name/$SRC_LABEL" "$fn" && {
        printf '%s\n' "$SRC_LABEL" > "$SCRATCH/gnulib-source"; return 0; }
    fi
  done
  return 1
}

log "coreutils revision: $COREUTILS_REVISION"
log "gnulib revision:    $GNULIB_REVISION"

if try_sources coreutils COREUTILS_SOURCES fetch_coreutils_git; then
  COREUTILS_SOURCE=$(cat "$SCRATCH/coreutils-source")
else
  die "could not fetch coreutils $COREUTILS_REVISION from any configured source"
fi

# One entry point for the three gnulib transports; dispatch by URL shape so
# try_sources stays transport-agnostic.
fetch_gnulib_source() {
  case "$SRC_URL" in
    /*) fetch_gnulib_archive "$SRC_URL" file ;;
    *.tar.gz) fetch_gnulib_archive "$SRC_URL" url ;;
    *) fetch_gnulib_git ;;
  esac
}

if try_sources gnulib GNULIB_SOURCES fetch_gnulib_source; then
  GNULIB_SOURCE=$(cat "$SCRATCH/gnulib-source")
else
  die "could not fetch gnulib $GNULIB_REVISION from any configured source"
fi

python3 - "$PROVENANCE" <<PY
import json, sys
json.dump({
    "schema": 1,
    "coreutils_revision": "$COREUTILS_REVISION",
    "gnulib_revision": "$GNULIB_REVISION",
    "coreutils_source": "$COREUTILS_SOURCE",
    "gnulib_source": "$GNULIB_SOURCE",
    "anchor": "$ANCHOR",
    "vendored_archive": "$VENDORED_ARCHIVE",
    "vendored_sha256": "$VENDORED_SHA256",
    "tree_sha256": "$TREE_SHA256",
    "retry": {"attempts": $RETRY_ATTEMPTS, "backoff_seconds": "${RETRY_BACKOFF_SECONDS}",
              "attempt_timeout_seconds": $ATTEMPT_TIMEOUT,
              "network_source_budget_seconds": $SOURCE_BUDGET_SECONDS},
}, open(sys.argv[1], "w", encoding="utf-8"), indent=2, sort_keys=True)
PY
log "provenance: $PROVENANCE ($COREUTILS_SOURCE / $GNULIB_SOURCE)"

printf "export GNULIB_SRCDIR='%s'\n" "$GNULIB_DIR"
printf "export COREUTILS_REVISION='%s'\n" "$COREUTILS_REVISION"
printf "export GNULIB_REVISION='%s'\n" "$GNULIB_REVISION"
