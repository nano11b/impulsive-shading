"""Embed generated PNG previews into the executable."""

from pathlib import Path
import sys


def main() -> None:
    source = Path(sys.argv[1])
    destination = Path(sys.argv[2])
    destination.parent.mkdir(parents=True, exist_ok=True)
    with destination.open("w", encoding="ascii", newline="\n") as output:
        output.write("#pragma once\n#include <string>\n\n")
        output.write("inline std::string embeddedPreview(const std::string& id) {\n")
        for path in sorted(source.glob("*.png")):
            data = path.read_bytes()
            output.write(f'    if (id == "{path.stem}") {{\n')
            output.write("        static constexpr const char* chunks[] = {\n")
            chunks = [data[offset:offset + 8192] for offset in range(0, len(data), 8192)]
            for chunk in chunks:
                output.write('            "')
                output.write("".join(f"\\x{byte:02x}" for byte in chunk))
                output.write('",\n')
            output.write("        };\n")
            output.write(f"        std::string result; result.reserve({len(data)});\n")
            for index, chunk in enumerate(chunks):
                output.write(f"        result.append(chunks[{index}], {len(chunk)});\n")
            output.write("        return result;\n    }\n")
        output.write("    return {};\n}\n")


if __name__ == "__main__":
    main()
