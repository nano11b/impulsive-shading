#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo=https://github.com/nano11b/impulsive-shading/releases/latest/download
archive=Impulsive-Shading-Benchmark-Linux-x86_64.tar.gz
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
if ! curl --fail --location --silent --show-error --max-time 30 "$repo/version.txt" -o "$tmp/version.txt"; then
  echo "Update check unavailable; using installed version." >&2
  exit 0
fi
if [ -f "$root/version.txt" ] && [ "$(cat "$root/version.txt")" = "$(cat "$tmp/version.txt")" ]; then
  exit 0
fi
curl --fail --location --silent --show-error --max-time 180 "$repo/$archive" -o "$tmp/$archive"
curl --fail --location --silent --show-error --max-time 30 "$repo/$archive.sha256" -o "$tmp/$archive.sha256"
(cd "$tmp" && sha256sum -c "$archive.sha256")
mkdir "$tmp/unpacked"
tar -xzf "$tmp/$archive" -C "$tmp/unpacked"
for item in 'Impulsive Shading' 'Run-Benchmark.sh' 'Update-Benchmark.sh' 'README.txt' 'version.txt'; do
  [ -f "$tmp/unpacked/$item" ] || { echo "Incomplete update: $item" >&2; exit 1; }
done
mkdir -p "$root/data"
cp "$tmp/unpacked/data/logo_spin_frames.bin" "$root/data/"
cp "$tmp/unpacked/data/logo_spin_audio.mp3" "$root/data/"
cp "$tmp/unpacked/Impulsive Shading" "$root/Impulsive Shading.new"
chmod 755 "$root/Impulsive Shading.new"
mv -f "$root/Impulsive Shading.new" "$root/Impulsive Shading"
cp "$tmp/unpacked/Run-Benchmark.sh" "$root/Run-Benchmark.sh"
cp "$tmp/unpacked/Update-Benchmark.sh" "$root/Update-Benchmark.sh"
chmod 755 "$root/Run-Benchmark.sh" "$root/Update-Benchmark.sh"
cp "$tmp/unpacked/README.txt" "$root/README.txt"
cp "$tmp/version.txt" "$root/version.txt"
echo "Updated to $(cat "$root/version.txt")"
