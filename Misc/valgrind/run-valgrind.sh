#!/usr/bin/env bash

set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/../.." && pwd)"
artifacts_dir="$repo_root/artifacts"
binary="$repo_root/Quake/quakespasm-valgrind"
supp_file="$script_dir/valgrind.supp"
client_game="ci_valgrind_client_$$"
client_dir="$repo_root/Quake/$client_game"
client_autoexec_path="$client_dir/autoexec.cfg"
server_autoexec_path=""
server_autoexec_backup=""
stdin_stub="$artifacts_dir/stdin.txt"
server_mod="crmod7"
server_dir="$repo_root/Quake/$server_mod"
server_pid=""
server_status=0
server_valgrind_log="$artifacts_dir/valgrind-server-local.log"
server_stdout_log="$artifacts_dir/server-local.log"
server_port=26001
combined_log="$artifacts_dir/valgrind-combined.log"
server_timeout=90s
client_timeout=120s
timeout_kill_after=15s

mkdir -p "$artifacts_dir"
rm -f "$combined_log" "$stdin_stub" "$artifacts_dir/valgrind.log" \
  "$server_valgrind_log" "$server_stdout_log"
printf "\n" > "$stdin_stub"

cleanup() {
  if [ -n "$server_pid" ]; then
    kill "$server_pid" 2>/dev/null || true
    wait "$server_pid" 2>/dev/null || true
  fi
  case "$client_dir" in
    "$repo_root/Quake/ci_valgrind_client_"*) rm -rf "$client_dir" ;;
  esac
  if [ -n "$server_autoexec_path" ]; then
    rm -f "$server_autoexec_path"
  fi
  if [ -n "$server_autoexec_backup" ] && [ -f "$server_autoexec_backup" ]; then
    mv "$server_autoexec_backup" "$server_autoexec_path"
  fi
}
trap cleanup EXIT

if [ ! -x "$binary" ]; then
  echo "Missing valgrind binary at $binary; run build-linux-valgrind.sh first." >&2
  exit 1
fi

run_with_timeout() {
  timeout --signal=TERM --kill-after="$timeout_kill_after" "$@"
}

is_timeout_status() {
  [ "$1" -eq 124 ] || [ "$1" -eq 137 ]
}

# Use headless drivers to avoid needing a display/audio device on CI.
export SDL_AUDIODRIVER=dummy
export QSS_NOSTDIN=1

cd "$repo_root/Quake"

# Drive the run through per-game autoexec files. In this tree +exec command-line
# commands can be delayed behind quake.rc/startdemos, which makes CI time out.
mkdir -p "$client_dir"
cat > "$client_autoexec_path" <<'EOF'
map start
wait
wait
wait
quit
EOF

# Optionally launch a local server using crmod7 if the progs.dat is present.
if [ -f "$server_dir/progs.dat" ]; then
  echo "Starting local server with -game $server_mod on port $server_port"
  server_autoexec_path="$server_dir/autoexec.cfg"
  if [ -f "$server_autoexec_path" ]; then
    server_autoexec_backup="$(mktemp)"
    cp "$server_autoexec_path" "$server_autoexec_backup"
  fi
  cat > "$server_autoexec_path" <<'EOF'
sv_public 0
map start
wait
wait
wait
quit
EOF
  touch "$server_valgrind_log" "$server_stdout_log"
  if ! command -v valgrind >/dev/null 2>&1; then
    echo "Warning: valgrind not found; running server without it" >&2
    run_with_timeout "$server_timeout" "$binary" <"$stdin_stub" \
      -basedir "$repo_root/Quake" \
      -game "$server_mod" \
      -dedicated 1 \
      -port "$server_port" \
      >"$server_stdout_log" 2>&1 &
  else
    run_with_timeout "$server_timeout" valgrind <"$stdin_stub" \
      --tool=memcheck \
      --leak-check=full \
      --show-leak-kinds=definite \
      --errors-for-leak-kinds=definite \
      --track-origins=yes \
      --suppressions="$supp_file" \
      --error-exitcode=1 \
      --log-file="$server_valgrind_log" \
      "$binary" \
      -basedir "$repo_root/Quake" \
      -game "$server_mod" \
      -dedicated 1 \
      -port "$server_port" \
      >"$server_stdout_log" 2>&1 &
  fi
  server_pid=$!
else
  echo "Skipping local server: $server_dir/progs.dat not found"
fi

set +e
run_with_timeout "$client_timeout" xvfb-run -a valgrind \
  --tool=memcheck \
  --leak-check=full \
  --show-leak-kinds=definite \
  --errors-for-leak-kinds=definite \
  --track-origins=yes \
  --suppressions="$supp_file" \
  --error-exitcode=1 \
  --log-file="$artifacts_dir/valgrind.log" \
  ./quakespasm-valgrind \
  -basedir "$repo_root/Quake" \
  -game "$client_game" \
  -heapsize 256000 \
  -zone 1024
client_status=$?

if [ -n "$server_pid" ]; then
  wait "$server_pid"
  server_status=$?
  server_pid=""
fi

set -e

exit_status=0
if is_timeout_status "$client_status"; then
  echo "Valgrind run timed out after $client_timeout" >&2
  exit_status=124
elif [ "$client_status" -ne 0 ]; then
  echo "Valgrind reported errors (exit $client_status); see $artifacts_dir/valgrind.log" >&2
  exit_status=1
fi

if is_timeout_status "$server_status"; then
  echo "Server valgrind timed out after $server_timeout" >&2
  if [ "$exit_status" -eq 0 ]; then
    exit_status=124
  fi
elif [ "$server_status" -ne 0 ]; then
  echo "Server valgrind reported errors (exit $server_status); see $server_valgrind_log" >&2
  if [ "$exit_status" -eq 0 ]; then
    exit_status=1
  fi
fi

if [ -f "$artifacts_dir/valgrind.log" ]; then
  echo
  echo "==== valgrind log (tail) ===="
  tail -n 200 "$artifacts_dir/valgrind.log"
  {
    echo "==== CLIENT VALGRIND LOG ===="
    cat "$artifacts_dir/valgrind.log"
    echo
  } >> "$combined_log"
fi

if [ -f "$server_valgrind_log" ]; then
  echo
  echo "==== server valgrind log (tail) ===="
  tail -n 200 "$server_valgrind_log"
  {
    echo "==== SERVER VALGRIND LOG ===="
    cat "$server_valgrind_log"
    echo
  } >> "$combined_log"
fi

if [ -f "$server_stdout_log" ]; then
  echo
  echo "==== server stdout (tail) ===="
  tail -n 200 "$server_stdout_log"
  {
    echo "==== SERVER STDOUT ===="
    cat "$server_stdout_log"
    echo
  } >> "$combined_log"
fi

exit "$exit_status"
