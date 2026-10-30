# Impulsive Shading

An OpenGL app for browsing and running your ShaderToy shaders. The earlier C# host for individual `.frag` files is documented below.

## C++ app with bundled shaders

The C++ app embeds the 21 shaders from `cpp/data/shaders.json` into the executable at build time. You can launch it without a JSON file. It supports `Common`, `Image`, Buffer A-D, and `Sound` passes, including feedback buffers, plus the standard time, resolution, mouse, date, and channel uniforms. It also supplies ShaderToy's keyboard texture.

Build on Windows with Visual Studio 2022 Build Tools and CMake. The first configure downloads GLFW, Glad, nlohmann/json, stb, Dear ImGui, and miniaudio; an internet connection is needed for that step.

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --target ImpulsiveShading
```

Run the app:

```powershell
& ".\build\Release\Impulsive Shading.exe"
```

The app opens with a searchable menu containing preview images for all 21 bundled shaders. Click a shader to run it. Select a monitor, full screen or windowed mode, and a resolution, then click **Apply display settings**. **VSync** is on by default, so FPS normally follows the display refresh rate; turn it off in the menu to allow higher FPS. The display choices, VSync, last shader, and time speed are saved in `%APPDATA%\Impulsive Shading\settings.json`. The FPS counter stays at the bottom right.

Press **M** or **Escape** to open or close the menu. **Left** and **Right** switch shaders, **R** recompiles, **P** pauses or resumes, and **F12** saves a PNG of the shader to `Pictures\Impulsive Shading`. The menu also has pause, screenshot, and time speed controls. `--shader 73yXR1` selects an initial shader ID; a name fragment also works. To compile and render one hidden frame for every bundled shader, add `--validate-all`. Use `--validate` to check only the selected shader. You can still load another export with `--json "C:\path\to\shaders.json"`.

Image textures referenced by the shaders are separate assets and are not contained in the JSON export. For the bundled shaders, the app searches next to the executable, for example `build/Release/media/a/example.png`. `--assets C:\path\to\asset-root` changes that root. Missing images use deterministic procedural textures; these are visual substitutes for the unavailable originals. Music channels use a generated track unless the referenced MP3 is placed under the asset root. Shaders with a `Sound` pass synthesize and play their own stereo audio automatically; the menu's **Play audio** checkbox controls playback. Microphone capture has been removed. The export's one microphone-input visualizer reads the regular music signal instead.

**Nano11Bravo Logo Spin** uses a GLSL image pass to display frames from `logo-spin.mp4`. Its 145 video frames and MP3 audio are embedded in the Windows executable, so the MP4 is not needed at runtime. The animation loops at 24 FPS and follows the playback controls. To regenerate the media after changing the clip, run `python cpp/pack_logo_frames.py "C:\path\to\logo-spin.mp4" cpp/data`, then rebuild and refresh previews.

To replace the bundled shaders, update `cpp/data/shaders.json` and rebuild. To refresh the preview images after shader changes, run `& ".\build\Release\Impulsive Shading.exe" --generate-previews "cpp\data\previews"` and rebuild again to embed the new PNGs.

The C# app below remains available for individual `.frag` files.

## GPU benchmarks

The C++ app can benchmark shaders on the computer where it runs. It uses a visible 1280 × 720 window, disables VSync and the menu, warms up the renderer, then times complete frames including GPU completion. Reports include average FPS, median and 95th percentile frame time, GPU, and OpenGL driver.

```powershell
& ".\build\Release\Impulsive Shading.exe" --benchmark-all --benchmark "build\benchmark-all.json" --benchmark-seconds 3 --benchmark-warmup 1
```

The benchmark website stores reports and lets you search by shader or filter by GPU. The local development launcher uses `IMPULSIVE_MARKS_TOKEN` for authenticated uploads; use `-SkipUpload` to keep a report local.

```powershell
& ".\run-benchmark.ps1" -All
```

To save a local report without uploading, use `& ".\run-benchmark.ps1" -All -SkipUpload`. Use `-Shader 73yXR1` for one shader; `-Seconds` and `-Warmup` set the measurement durations. The launcher requires the Release executable.

The public website is [marks.seven-labs.com](https://marks.seven-labs.com). Its server source is in `site/lxc/`.

The public [download page](https://marks.seven-labs.com/download) serves a Windows ZIP containing the app, its Visual C++ runtime DLLs, and a double-click launcher. The launcher benchmarks all 21 shaders and submits a complete report to `/api/community-reports` without a private token. Community submissions are limited to five per hour per client; the results page labels them as self-reported. Run `& ".\site\lxc\build-download.ps1"` after rebuilding the Release executable to refresh the ZIP and its SHA-256 checksum.

The same page also offers a Linux x86-64 archive built on Ubuntu 22.04 for that release and newer versions. It includes the executable, logo media, and a shell launcher. Extract it and run `./Run-Benchmark.sh`; use `./Run-Benchmark.sh --local` to keep the report on your machine. The build requires X11 or XWayland and OpenGL 3.3. Build the Linux executable on Ubuntu 22.04 with `cmake -S . -B out -G Ninja -DCMAKE_BUILD_TYPE=Release` and `cmake --build out --target ImpulsiveShading`, then run `site/lxc/build-linux-package.sh <source-root> <output.tar.gz>` to create the archive and checksum.

### GitHub releases and updates

Publishing a GitHub release tagged `vX.Y.Z` builds Windows and Ubuntu 22.04 downloads through `.github/workflows/release.yml`. Extract a release archive once, then start future runs through `Start Benchmark.cmd` on Windows or `./Run-Benchmark.sh` on Linux. Those launchers check GitHub Releases and install a newer release into the extracted folder before running. They verify the downloaded archive against its published SHA-256 file. Set `IMPULSIVE_SKIP_UPDATE=1` to run the installed version without checking. Launching the executable directly bypasses the update check.

## C# host: Run it

```powershell
dotnet run
```

Pass another shader file to render it:

```powershell
dotnet run -- "C:\path\to\shader.frag"
```

The fragment file should expose ShaderToy's usual entry point:

```glsl
void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
    vec2 uv = fragCoord / iResolution.xy;
    fragColor = vec4(uv, 0.5 + 0.5 * sin(iTime), 1.0);
}
```

Press **R** to recompile the file after editing it. Press **Escape** to quit. Compiler errors are written to the terminal, and a failed reload leaves the previous shader running.

Compile and render one invisible frame to validate a shader without opening a window:

```powershell
dotnet run -- --validate "C:\path\to\shader.frag"
```

## Supported inputs in the C# host

- `iResolution`, `iTime`, `iTimeDelta`, `iFrame`, and `iFrameRate`
- `iMouse` with ShaderToy-style bottom-left coordinates and click origin
- `iDate` and `iSampleRate`
- `iChannel0` through `iChannel3`, `iChannelTime`, and `iChannelResolution`

Texture channels currently use a 1×1 black fallback texture. Loading images, videos, cubemaps, sound, and multipass buffers is not implemented yet.
