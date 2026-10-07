#!/bin/sh
# usage: build.sh <name>  -> builds current working tree into bin-<name>
D="$(cd "$(dirname "$0")" && pwd -W)"
R="$(cd "$(dirname "$0")/../.." && pwd -W)"
"/c/Program Files/Microsoft Visual Studio/2022/Community/MSBuild/Current/Bin/MSBuild.exe" "$R/Windows/VisualStudio/quakespasm.vcxproj" -m -t:Build -p:Configuration=Release -p:Platform=x64 "-p:SolutionDir=$R/Windows/VisualStudio/" "-p:OutDir=$D/bin-$1/" "-p:IntDir=$D/obj/" -v:minimal -nologo > "$D/build-$1.log" 2>&1
rc=$?
grep -E "error|warning C4(013|047|133)" "$D/build-$1.log" | head -20
tail -2 "$D/build-$1.log"
exit $rc
