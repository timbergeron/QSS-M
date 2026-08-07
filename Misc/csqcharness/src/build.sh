#!/bin/sh
# Build csprogs.dat + menu.dat for the rotpic sample.
#
#   ./build.sh /path/to/fteqcc
#
# Run this from the sample's src/ directory.  The two qs*extensions.qc headers
# come from the engine itself:
#
#   pr_dumpplatform -Tcs   -O qscsextensions
#   pr_dumpplatform -Tmenu -O qsmenuextensions
#
# (run those from the console with -game rotpic active; they land in
# rotpic/src/).  Note the engine's argv parser swallows anything starting with
# '-', so these cannot be passed as +pr_dumpplatform on the command line --
# put them in a cfg and +exec it.
set -e

FTEQCC=${1:-fteqcc}

"$FTEQCC" -srcfile csprogs.src
"$FTEQCC" -srcfile menu.src
