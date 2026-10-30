IMPULSIVE SHADING BENCHMARK
===========================

Requirements: Windows 64-bit, a GPU with OpenGL 3.3, and an internet
connection to submit results.
The Visual C++ runtime DLLs needed by the app are included in this folder.

1. Extract the entire ZIP to a folder.
2. Double-click "Start Benchmark.cmd".
3. Leave the benchmark window visible until the run finishes.
4. View submitted results at https://marks.seven-labs.com

The full run measures all 21 bundled shaders. It takes about 1-2 minutes.
Results are also saved in benchmark-result.json beside the executable.

The report contains GPU renderer/vendor, OpenGL version, resolution, run
time, and shader performance. It does not include your name or username.

For a local-only run, open PowerShell in this folder and run:
  & '.\Run Benchmark.ps1' -SkipUpload

Community submissions are rate-limited. If upload fails, keep the JSON
file and try again later.
