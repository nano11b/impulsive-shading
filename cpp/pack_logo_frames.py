"""Pack a video as JPEG frames plus an MP3 for the embedded logo shader.

Requires ffmpeg and ffprobe when regenerating the media. The C++ build only
needs the generated .bin and .mp3 files.
"""

import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile


def run(*command: str) -> None:
    subprocess.run(command, check=True)


def main() -> None:
    source = Path(sys.argv[1]).resolve()
    output = Path(sys.argv[2]).resolve()
    output.mkdir(parents=True, exist_ok=True)
    probe = subprocess.run(
        ["ffprobe", "-v", "error", "-select_streams", "v:0", "-show_entries",
         "stream=width,height,r_frame_rate", "-of", "json", str(source)],
        check=True, capture_output=True, text=True)
    stream = json.loads(probe.stdout)["streams"][0]
    fps_numerator, fps_denominator = map(int, stream["r_frame_rate"].split("/"))
    with tempfile.TemporaryDirectory() as temporary:
        pattern = str(Path(temporary) / "%04d.jpg")
        run("ffmpeg", "-hide_banner", "-loglevel", "error", "-i", str(source),
            "-map", "0:v:0", "-fps_mode", "passthrough", "-q:v", "4", pattern, "-y")
        frames = [path.read_bytes() for path in sorted(Path(temporary).glob("*.jpg"))]
    if not frames:
        raise RuntimeError("No video frames were decoded")
    offsets = [0]
    for frame in frames:
        offsets.append(offsets[-1] + len(frame))
    with (output / "logo_spin_frames.bin").open("wb") as packed:
        packed.write(struct.pack("<4sIIIII", b"LSF1", stream["width"], stream["height"],
                                 fps_numerator, fps_denominator, len(frames)))
        packed.write(struct.pack(f"<{len(offsets)}I", *offsets))
        for frame in frames:
            packed.write(frame)
    run("ffmpeg", "-hide_banner", "-loglevel", "error", "-i", str(source),
        "-map", "0:a:0", "-vn", "-codec:a", "libmp3lame", "-q:a", "4",
        str(output / "logo_spin_audio.mp3"), "-y")
    print(f"Packed {len(frames)} frames at {fps_numerator}/{fps_denominator} fps")


if __name__ == "__main__":
    main()
