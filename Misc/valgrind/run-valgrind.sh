#!/usr/bin/env bash

set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/../.." && pwd)"
artifacts_dir="$repo_root/artifacts"
binary="$repo_root/Quake/quakespasm-valgrind"
supp_file="$script_dir/valgrind.supp"
client_cfg_dir="$repo_root/Quake/id1"
client_cfg_name="ci_valgrind_client_$$.cfg"
client_cfg_path="$client_cfg_dir/$client_cfg_name"
server_cfg_name="ci_valgrind_server_$$.cfg"
server_cfg_path=""
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
  rm -f "$client_cfg_path"
  if [ -n "$server_cfg_path" ]; then
    rm -f "$server_cfg_path"
  fi
  if [ -n "$server_pid" ]; then
    kill "$server_pid" 2>/dev/null || true
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

# Prepare scripted client commands without touching autoexec.cfg. Dedicated
# servers also exec autoexec.cfg at startup, so a shared autoexec can make the
# server run client-only commands while it is still starting under Valgrind.
mkdir -p "$client_cfg_dir"
connect_command="connect la.quakeone.com:26002"
if [ -f "$server_dir/progs.dat" ]; then
  connect_command="connect 127.0.0.1:$server_port"
fi
cat > "$client_cfg_path" <<EOF
map start
wait 30
$connect_command
wait 300
disconnect
quit
EOF

# Optionally launch a local server using crmod7 if the progs.dat is present.
if [ -f "$server_dir/progs.dat" ]; then
  echo "Starting local server with -game $server_mod on port $server_port"
  server_cfg_path="$server_dir/$server_cfg_name"
  cat > "$server_cfg_path" <<'EOF'
wait 300
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
      +map start \
      +sv_public 0 \
      +exec "$server_cfg_name" \
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
      +map start \
      +sv_public 0 \
      +exec "$server_cfg_name" \
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
  -heapsize 256000 \
  -zone 1024 \
  +exec "$client_cfg_name"
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
