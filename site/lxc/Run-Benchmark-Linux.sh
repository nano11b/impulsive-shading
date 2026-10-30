#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
report="$root/benchmark-result.json"
site=${IMPULSIVE_MARKS_URL:-https://marks.seven-labs.com}

case "${1:-}" in
  "") upload=1 ;;
  --local) upload=0 ;;
  *) echo "Usage: $0 [--local]" >&2; exit 2 ;;
esac

echo "Running all 21 shaders at 1280 x 720 with VSync off. Keep the benchmark window visible."
if [ "${IMPULSIVE_SKIP_UPDATE:-0}" != 1 ] && [ -x "$root/Update-Benchmark.sh" ]; then
  "$root/Update-Benchmark.sh" || echo "Update failed; using installed version." >&2
fi
"$root/Impulsive Shading" --benchmark-all --benchmark "$report" --benchmark-seconds 3 --benchmark-warmup 1
if [ "$upload" -eq 0 ]; then
  echo "Results saved to $report"
  exit 0
fi
if ! command -v curl >/dev/null 2>&1; then
  echo "curl is required for submission. Results remain at $report" >&2
  exit 1
fi
if curl --fail --show-error --silent --max-time 30 --retry 2 \
    --header 'Content-Type: application/json' \
    --data-binary "@$report" "$site/api/community-reports" >/dev/null; then
  echo "Results submitted. View them at $site"
else
  echo "Upload failed. Results remain at $report" >&2
  exit 1
fi
