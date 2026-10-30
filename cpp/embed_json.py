"""Generate small C++ string chunks from a ShaderToy JSON export."""

from pathlib import Path
import sys


def main() -> None:
    source = Path(sys.argv[1]).read_bytes()
    destination = Path(sys.argv[2])
    destination.parent.mkdir(parents=True, exist_ok=True)
    chunks = [source[index:index + 8192] for index in range(0, len(source), 8192)]
    with destination.open("w", encoding="ascii", newline="\n") as output:
        output.write("#pragma once\n#include <string>\n\n")
        output.write("inline std::string embeddedShadersJson() {\n")
        output.write("    static constexpr const char* chunks[] = {\n")
        for chunk in chunks:
            output.write('        "')
            output.write("".join(f"\\x{byte:02x}" for byte in chunk))
            output.write('",\n')
        output.write("    };\n")
        output.write(f"    std::string result;\n    result.reserve({len(source)});\n")
        output.write("    for (const char* chunk : chunks) result += chunk;\n")
        output.write("    return result;\n}\n")


if __name__ == "__main__":
    main()
