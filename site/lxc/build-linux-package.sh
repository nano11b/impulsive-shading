#!/bin/sh
set -eu
source_root=$1
output=$2
script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
stage=$(mktemp -d)
trap 'rm -rf "$stage"' EXIT
mkdir -p "$stage/data"
cp "$source_root/out/Impulsive Shading" "$stage/Impulsive Shading"
cp "$source_root/cpp/data/logo_spin_frames.bin" "$stage/data/"
cp "$source_root/cpp/data/logo_spin_audio.mp3" "$stage/data/"
cp "$script_dir/Run-Benchmark-Linux.sh" "$stage/Run-Benchmark.sh"
cp "$script_dir/Update-Benchmark-Linux.sh" "$stage/Update-Benchmark.sh"
cp "$script_dir/README-Linux.txt" "$stage/README.txt"
printf '%s\n' "${GITHUB_REF_NAME:-local}" > "$stage/version.txt"
chmod 0755 "$stage/Impulsive Shading" "$stage/Run-Benchmark.sh" "$stage/Update-Benchmark.sh"
tar -czf "$output" -C "$stage" .
sha256sum "$output" | cut -d ' ' -f 1 > "$(dirname -- "$output")/linux-download.sha256"
echo "Built $output"
