#!/bin/sh
# Build the sample HUD (../csprogs.dat) and compare.py's reference progs (../ref.dat).
#
#   ./build.sh /path/to/fteqcc
#
# Run from this directory. qscsextensions.qc comes from the engine:
#   pr_dumpplatform -Tcs -O qscsextensions
# (put it in a cfg and +exec it; the engine's argv parser eats "-T...").
set -e
FTEQCC=${1:-fteqcc}
"$FTEQCC" -srcfile csprogs.src
"$FTEQCC" -srcfile ref.src
# compare.py's mission-pack scenarios: needs qsssextensions.qc (pr_dumpplatform -Tss -O qsssextensions)
if [ -f qsssextensions.qc ]; then
	"$FTEQCC" -srcfile chudss.src
fi
