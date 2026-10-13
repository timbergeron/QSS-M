#!/bin/sh
# Use the current host's SDL3, ignoring .d files from retired temporary prefixes.
set -eu
translation_repo=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$translation_repo"
mkdir -p .codex-build/chat-translate/tmp
python3 -c 'from pathlib import Path; source = Path("Quake/Makefile").read_text(); Path(".codex-build/chat-translate/Makefile").write_text(source.replace("sinclude $(OBJS:.o=.d)", "# Use this hosts current dependency installation."))'
TMPDIR="$translation_repo/.codex-build/chat-translate/tmp" make -C Quake -f "$translation_repo/.codex-build/chat-translate/Makefile" "$@"
