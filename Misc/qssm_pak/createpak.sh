#!/bin/bash
set -euo pipefail

# Usage check
if [ "$#" -lt 2 ]; then
    echo "Usage: $0 <output.pak> <input files...>"
    exit 1
fi

OUTPUT_PAK=$1
shift  # Remove the output file name from the arguments list

# Function to output a 32-bit unsigned int in little-endian format
little_endian_uint32() {
    printf "$(printf '\\x%02x' $(( $1 & 0xFF )) $(( ($1 >> 8) & 0xFF )) $(( ($1 >> 16) & 0xFF )) $(( ($1 >> 24) & 0xFF )))"
}

# Initialize directory offset and size
directory_offset=12  # Initial offset after the header
directory_size=0

# Calculate directory offset and size
for file in "$@"; do
    if [ ! -f "$file" ]; then
        echo "Error: File $file not found!"
        exit 1
    fi

    file_size=$(wc -c < "$file" | tr -d ' ')
    directory_offset=$((directory_offset + file_size))
    directory_size=$((directory_size + 64))  # Each directory entry is 64 bytes
done

# Write header (magic, directory offset, directory size)
{
    echo -n 'PACK'
    little_endian_uint32 $directory_offset
    little_endian_uint32 $directory_size
} > "$OUTPUT_PAK"

# Append file content
for file in "$@"; do
    cat "$file" >> "$OUTPUT_PAK"
done

# Write directory entries
file_offset=12  # Start after the header
for file in "$@"; do
    file_size=$(wc -c < "$file" | tr -d ' ')
    # Preserve the full path for the directory entry
    file_name_with_path=$(echo "$file" | sed 's|^\./||')  # Remove leading './' if present

    # Ensure the path + filename does not exceed 56 characters
    if [ "${#file_name_with_path}" -gt 56 ]; then
        echo "Error: Path + filename length exceeds 56 characters for $file"
        exit 1
    fi

    {
        echo -n "$file_name_with_path"
        # Add padding to the file name to make it 56 bytes
        for (( i=${#file_name_with_path}; i<56; i++ )); do echo -n -e "\x00"; done
        little_endian_uint32 $file_offset
        little_endian_uint32 $file_size
    } >> "$OUTPUT_PAK"

    file_offset=$((file_offset + file_size))
done

echo "Created $OUTPUT_PAK successfully."
