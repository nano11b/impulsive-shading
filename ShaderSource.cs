using System.Text;

namespace ShaderToySharp;

internal static class ShaderSource
{
    public const string Vertex = """
        #version 330 core

        const vec2 vertices[3] = vec2[3](
            vec2(-1.0, -1.0),
            vec2( 3.0, -1.0),
            vec2(-1.0,  3.0)
        );

        void main()
        {
            gl_Position = vec4(vertices[gl_VertexID], 0.0, 1.0);
        }
        """;

    public static string BuildFragment(string shaderToySource)
    {
        var body = new StringBuilder();
        using var reader = new StringReader(shaderToySource);
        while (reader.ReadLine() is { } line)
        {
            // A desktop GLSL version is supplied by the host. ShaderToy exports may
            // contain their own version directive, which cannot appear mid-source.
            if (!line.TrimStart().StartsWith("#version", StringComparison.Ordinal))
            {
                body.AppendLine(line);
            }
        }

        return $$"""
            #version 330 core

            out vec4 _shaderToyColor;

            uniform vec3 iResolution;
            uniform float iTime;
            uniform float iTimeDelta;
            uniform float iFrameRate;
            uniform int iFrame;
            uniform vec4 iMouse;
            uniform vec4 iDate;
            uniform float iSampleRate;
            uniform float iChannelTime[4];
            uniform vec3 iChannelResolution[4];
            uniform sampler2D iChannel0;
            uniform sampler2D iChannel1;
            uniform sampler2D iChannel2;
            uniform sampler2D iChannel3;

            #define texture2D texture
            #define textureCube texture

            {{body}}

            void main()
            {
                mainImage(_shaderToyColor, gl_FragCoord.xy);
            }
            """;
    }
}
