#!/bin/sh
set -eu
translation_repo=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$translation_repo"
mkdir -p .codex-build/chat-translate/tmp
export TMPDIR="$translation_repo/.codex-build/chat-translate/tmp"
translation_python=${QSSM_TRANSLATE_PYTHON:-.codex-build/chat-translate/venv/bin/python}
"$translation_python" -m unittest discover -s Misc/chat_translate -p test_service.py -v
"$translation_python" Misc/chat_translate/test_client.py "$@"
python3 Misc/stress/test_console_completion.py
python3 Misc/stress/test_utf8_to_quake.py
