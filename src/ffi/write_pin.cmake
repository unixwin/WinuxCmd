# write_pin.cmake — invoked POST_BUILD with -DDLL=<staged dll path>
# -DOUT=<pin file path>.
#
# Writes the DLL's SHA-256 as bare lowercase hex (no filename, no
# trailing newline). Layout ruling (ffi-route, 2026-10-28): the
# per-version winuxcore.sha256 next to the DLL is the loader's pinned
# hash source; the package-root SHA256SUMS stays the zip-level manifest
# and is written by the packaging step, not here.
file(SHA256 "${DLL}" _winuxcore_hash)
file(WRITE "${OUT}" "${_winuxcore_hash}")
