#!/usr/bin/env bash

set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/../.." && pwd)"
artifacts_dir="$repo_root/artifacts"
binary="$repo_root/Quake/quakespasm-valgrind"
supp_file="$script_dir/valgrind.supp"
autoexec_dir="$repo_root/Quake/id1"
autoexec_path="$autoexec_dir/autoexec.cfg"
autoexec_backup=""
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
rm -f "$combined_log" "$stdin_stub"
printf "\n" > "$stdin_stub"

if [ ! -x "$binary" ]; then
  echo "Missing valgrind binary at $binary; run build-linux-valgrind.sh first." >&2
  exit 1
fi

# Prepare a tiny autoexec so shareware builds still run scripted commands.
mkdir -p "$autoexec_dir"

if [ -f "$autoexec_path" ]; then
  autoexec_backup="$(mktemp)"
  cp "$autoexec_path" "$autoexec_backup"
fi

cleanup() {
  rm -f "$autoexec_path"
  if [ -n "$autoexec_backup" ] && [ -f "$autoexec_backup" ]; then
    mv "$autoexec_backup" "$autoexec_path"
  fi
  if [ -n "$server_pid" ]; then
    kill "$server_pid" 2>/dev/null || true
  fi
}
trap cleanup EXIT

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

# Optionally launch a local server using crmod7 if the progs.dat is present.
if [ -f "$server_dir/progs.dat" ]; then
  echo "Starting local server with -game $server_mod on port $server_port"
  touch "$server_valgrind_log" "$server_stdout_log"
  if ! command -v valgrind >/dev/null 2>&1; then
    echo "Warning: valgrind not found; running server without it" >&2
    run_with_timeout "$server_timeout" "$binary" <"$stdin_stub" \
      -basedir "$repo_root/Quake" \
      -game "$server_mod" \
      -dedicated 1 \
      -port "$server_port" \
      +map start \
      +sv_public 0 \
      >"$server_stdout_log" 2>&1 &
  else
    run_with_timeout "$server_timeout" valgrind <"$stdin_stub" \
      --tool=memcheck \
      --leak-check=full \
      --show-leak-kinds=definite \
      --track-origins=yes \
      --suppressions="$supp_file" \
      --error-exitcode=1 \
      --log-file="$server_valgrind_log" \
      "$binary" \
      -basedir "$repo_root/Quake" \
      -game "$server_mod" \
      -dedicated 1 \
      -port "$server_port" \
      +map start \
      +sv_public 0 \
      >"$server_stdout_log" 2>&1 &
  fi
  server_pid=$!
else
  echo "Skipping local server: $server_dir/progs.dat not found"
fi

# Write autoexec after server decision so we can include/exclude local connect.
cat > "$autoexec_path" <<'EOF'
map start
wait 30
connect la.quakeone.com:26002
wait 300
disconnect
quit
EOF

set +e
run_with_timeout "$client_timeout" xvfb-run -a valgrind \
  --tool=memcheck \
  --leak-check=full \
  --show-leak-kinds=definite \
  --track-origins=yes \
  --suppressions="$supp_file" \
  --error-exitcode=1 \
  --log-file="$artifacts_dir/valgrind.log" \
  ./quakespasm-valgrind \
  -basedir "$repo_root/Quake" \
  -heapsize 256000 \
  -zone 1024 \
  +exec autoexec.cfg
client_status=$?

if [ -n "$server_pid" ]; then
  wait "$server_pid"
  server_status=$?
  server_pid=""
fi

set -e

if is_timeout_status "$client_status"; then
  echo "Valgrind run timed out after $client_timeout" >&2
  exit 124
elif [ "$client_status" -ne 0 ]; then
  echo "Valgrind reported errors (exit $client_status); see $artifacts_dir/valgrind.log" >&2
fi

if is_timeout_status "$server_status"; then
  echo "Server valgrind timed out after $server_timeout" >&2
elif [ "$server_status" -ne 0 ]; then
  echo "Server valgrind reported errors (exit $server_status); see $server_valgrind_log" >&2
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

exit 0
