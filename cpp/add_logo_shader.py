"""Add the embedded logo clip shader to the bundled ShaderToy export."""

import json
from pathlib import Path

path = Path(__file__).parent / "data" / "shaders.json"
data = json.loads(path.read_text(encoding="utf-8"))
data["shaders"] = [s for s in data["shaders"] if s["info"]["id"] != "N11B01"]
data["shaders"].append({
    "ver": "0.1",
    "info": {
        "id": "N11B01",
        "name": "Nano11Bravo Logo Spin",
        "description": "The Nano11Bravo rotating logo, rendered by a GLSL image pass from embedded frames and accompanied by its original clip audio.",
        "tags": ["logo", "video", "audio"],
    },
    "renderpass": [{
        "name": "Image",
        "type": "image",
        "inputs": [
            {"id": "logo-spin-frames", "filepath": "embedded:logo-spin-frames",
             "type": "video", "channel": 0,
             "sampler": {"filter": "linear", "wrap": "clamp"}},
            {"id": "logo-spin-audio", "filepath": "embedded:logo-spin-audio",
             "type": "music", "channel": 1,
             "sampler": {"filter": "linear", "wrap": "clamp"}},
        ],
        "outputs": [{"id": "N11B01", "channel": 0}],
        "code": """void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
    vec2 uv = (fragCoord - 0.5 * iResolution.xy) / min(iResolution.x, iResolution.y) + 0.5;
    vec3 color = vec3(0.0);
    if (all(greaterThanEqual(uv, vec2(0.0))) && all(lessThanEqual(uv, vec2(1.0))))
        color = texture(iChannel0, uv).rgb;
    fragColor = vec4(color, 1.0);
}""",
    }],
})
data["numShaders"] = len(data["shaders"])
path.write_text(json.dumps(data, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
print(f"Bundled {data['numShaders']} shaders")
