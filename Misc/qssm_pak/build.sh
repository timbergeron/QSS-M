#!/usr/bin/env bash
set -euo pipefail

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_root=$(CDPATH= cd -- "$script_dir/../.." && pwd)
output_pak=${1:-"$repo_root/Quake/qssm.pak"}

# Git supplies a stable file list and keeps the packaging scripts out of the pak.
files=()
while IFS= read -r -d '' file; do
  case "$file" in
    *.sh) continue ;;
  esac
  files+=("${file#Misc/qssm_pak/}")
done < <(git -C "$repo_root" ls-files -z -- Misc/qssm_pak)

if [ "${#files[@]}" -eq 0 ]; then
  echo "No qssm.pak source files found" >&2
  exit 1
fi

cd "$script_dir"
bash createpak.sh "$output_pak" "${files[@]}"
