# Corpus hermeticity waivers

Every waiver applied via `ingest.sh --waive` lands here as one row.
Audit periodically; tighten policy by closing waivers that are no
longer justified.

| Date | Bucket | Set | Script | Category | Reason |
|------|--------|-----|--------|----------|--------|
| 2026-05-31 | posix | autoconf | config.guess | filesys | /proc/cpuinfo and similar are Linux-only system probes that fall back to uname/getconf on macOS/BSD; the script is designed to be portable across platforms by guarding /proc reads with file-existence checks. |
| 2026-05-31 | posix | autoconf | gendocs.sh | network | wget calls are gated on a CLI flag that the scorecard's --help-style invocation never triggers. |
