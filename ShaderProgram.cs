using OpenTK.Graphics.OpenGL4;

namespace ShaderToySharp;

internal sealed class ShaderProgram : IDisposable
{
    public int Handle { get; }

    public ShaderProgram(string vertexSource, string fragmentSource)
    {
        var vertex = Compile(ShaderType.VertexShader, vertexSource);
        var fragment = 0;

        try
        {
            fragment = Compile(ShaderType.FragmentShader, fragmentSource);
            Handle = GL.CreateProgram();
            GL.AttachShader(Handle, vertex);
            GL.AttachShader(Handle, fragment);
            GL.LinkProgram(Handle);
            GL.GetProgram(Handle, GetProgramParameterName.LinkStatus, out var linked);
            if (linked == 0)
            {
                throw new InvalidOperationException($"Shader link failed:\n{GL.GetProgramInfoLog(Handle)}");
            }
        }
        catch
        {
            if (Handle != 0)
            {
                GL.DeleteProgram(Handle);
            }

            throw;
        }
        finally
        {
            GL.DeleteShader(vertex);
            if (fragment != 0)
            {
                GL.DeleteShader(fragment);
            }
        }
    }

    public int Uniform(string name) => GL.GetUniformLocation(Handle, name);

    public void Dispose() => GL.DeleteProgram(Handle);

    private static int Compile(ShaderType type, string source)
    {
        var shader = GL.CreateShader(type);
        GL.ShaderSource(shader, source);
        GL.CompileShader(shader);
        GL.GetShader(shader, ShaderParameter.CompileStatus, out var compiled);
        if (compiled != 0)
        {
            return shader;
        }

        var log = GL.GetShaderInfoLog(shader);
        GL.DeleteShader(shader);
        throw new InvalidOperationException($"{type} compilation failed:\n{log}");
    }
}
