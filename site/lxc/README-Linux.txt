IMPULSIVE SHADING BENCHMARK FOR LINUX
====================================

This x86-64 build targets Ubuntu 22.04 or newer (glibc 2.35+), with
X11 or XWayland, OpenGL 3.3, and curl for submitting results.

Extract the archive, open a terminal in the extracted folder, then run:

  ./Run-Benchmark.sh

Keep the benchmark window visible until all 21 shaders finish. The full
run takes about 1-2 minutes. It saves benchmark-result.json beside the
executable and submits the result to https://marks.seven-labs.com.

To keep results local without submitting:

  ./Run-Benchmark.sh --local

The archive includes the logo animation's video and audio data. The report
contains GPU model and driver, resolution, run time, and shader performance.
It does not include your name or username. Community results are
self-reported and rate-limited.
