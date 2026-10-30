// ShaderToy fragment shaders define mainImage instead of main.
void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
    vec2 uv = (2.0 * fragCoord - iResolution.xy) / iResolution.y;
    float distanceFromCenter = length(uv);
    float wave = 0.5 + 0.5 * cos(10.0 * distanceFromCenter - 3.0 * iTime);
    vec3 color = 0.5 + 0.5 * cos(iTime + uv.xyx + vec3(0.0, 2.0, 4.0));
    color *= 0.35 + 0.65 * wave;
    fragColor = vec4(color, 1.0);
}
