# Output normalization layer for the differential runner (issue #1127 P3).
#
# runner.sh pipes BOTH sides of a comparison through the same filter
# (CR stripping in the runner, then this sed program) as its last
# comparison tier: a case whose outputs differ only where a rule here
# masks counts as PASS. A rule may therefore mask only text that is
# genuinely run-varying - a value that changes between two runs of the
# SAME program against the same inputs - and must leave every
# deterministic part of the output comparing exactly. Rules are added
# per observed corpus shape, never speculatively, and each rule below
# names the divergence it masks and why masking it cannot hide a real
# one. If a proposed mask would also match deterministic content, the
# difference belongs in whitelist-<platform>.txt with a reason instead.
#
# Deliberately NOT masked here:
# - Calendar dates. A wrong date is a real divergence; only the
#   time-of-day of a timestamp is run noise (both sides execute within
#   the same run, so the date a run-varying timestamp carries agrees
#   unless the value itself is wrong at day granularity).
# - Epoch seconds and other bare numbers. Ten-digit integers are data
#   throughout this corpus (sizes, offsets, od/hexdump output); masking
#   them would hide real differences. A case that prints a live clock
#   reading belongs in EXEMPT.md, not behind a mask.
# - Quote style and localized wording. Locale sensitivity is handled by
#   pinning: runner.sh runs both sides under ORACLE_LOCALE (C by
#   default). Canonicalizing U+2018/U+2019 or message text here would
#   silently reclassify the explained quoting differences that
#   whitelist.txt records with byte-level evidence.
#
# --- dd -----------------------------------------------------------------
#
# GNU dd ends its stderr summary with ", <elapsed> s, <rate>" - values
# that change on every run and can never match byte-for-byte. The mask
# drops everything after "bytes copied," (the elapsed/rate tail and the
# optional human-readable size in parentheses), leaving the
# deterministic part of the summary - the "<full>+<partial> records
# in/out" record counts (printed identically by WinuxCmd since #1707)
# and the byte counts - to be compared exactly. The record lines and
# the leading byte count are NOT masked, so a genuine counting or
# formatting divergence still differs.
#
# "bytes?" covers GNU's singular "1 byte copied" as well as the plural.
s/^([0-9]+ bytes?)( \([^)]*\))? copied, .*/\1 copied/
#
# --- Timestamps ---------------------------------------------------------
#
# A file a case's setup creates is created twice - once in each side's
# work directory, a moment apart - so any rendering of its mtime
# (stat %y/%z, stat's default block) carries a different time-of-day on
# each side, down to the nanoseconds, while the calendar date agrees.
# Masking the time-of-day (with its fractional part and numeric zone)
# makes such cases comparable at all; the date still compares, so a
# wrong-day or wrong-zone-at-day-granularity result still differs.
s/([0-9]{4}-[0-9]{2}-[0-9]{2})[ T][0-9]{2}:[0-9]{2}:[0-9]{2}(\.[0-9]+)?( [+-][0-9]{4}|Z)?/\1 <TIME>/
#
# date(1) with no format prints the current time in ctime form
# ("Sat Oct 31 19:32:11 CST 2026"): the two sides run a moment apart,
# so the time-of-day is run noise while weekday, date, zone name and
# year are the comparable content.
s/^((Mon|Tue|Wed|Thu|Fri|Sat|Sun) (Jan|Feb|Mar|Apr|May|Jun|Jul|Aug|Sep|Oct|Nov|Dec) [ 0-9][0-9] )[0-9]{2}:[0-9]{2}:[0-9]{2}/\1<TIME>/
