using System.Diagnostics;
using OpenTK.Graphics.OpenGL4;
using OpenTK.Mathematics;
using OpenTK.Windowing.Common;
using OpenTK.Windowing.Desktop;
using OpenTK.Windowing.GraphicsLibraryFramework;

namespace ShaderToySharp;

internal sealed class ShaderToyWindow : GameWindow
{
    private readonly string _shaderPath;
    private readonly bool _validateOnly;
    private readonly Stopwatch _clock = new();
    private ShaderProgram? _program;
    private int _vertexArray;
    private int _blackTexture;
    private int _frame;
    private double _lastFrameTime;
    private Vector2 _mousePosition;
    private Vector2 _clickOrigin;
    private bool _mouseDown;
    private bool _reloadWasDown;

    public ShaderToyWindow(
        GameWindowSettings gameWindowSettings,
        NativeWindowSettings nativeWindowSettings,
        string shaderPath,
        bool validateOnly)
        : base(gameWindowSettings, nativeWindowSettings)
    {
        _shaderPath = shaderPath;
        _validateOnly = validateOnly;
    }

    protected override void OnLoad()
    {
        base.OnLoad();
        GL.ClearColor(Color4.Black);
        _vertexArray = GL.GenVertexArray();
        GL.BindVertexArray(_vertexArray);
        _blackTexture = CreateBlackTexture();
        ReloadShader(throwOnFailure: true);
        _clock.Start();
    }

    protected override void OnRenderFrame(FrameEventArgs args)
    {
        base.OnRenderFrame(args);

        var now = _clock.Elapsed.TotalSeconds;
        var delta = _frame == 0 ? 0.0 : now - _lastFrameTime;
        _lastFrameTime = now;

        GL.Clear(ClearBufferMask.ColorBufferBit);
        if (_program is not null)
        {
            GL.UseProgram(_program.Handle);
            SetUniforms(_program, now, delta);
            GL.BindVertexArray(_vertexArray);
            GL.DrawArrays(PrimitiveType.Triangles, 0, 3);
        }

        SwapBuffers();
        _frame++;

        if (_validateOnly)
        {
            Console.WriteLine("Shader validation succeeded.");
            Close();
        }
    }

    protected override void OnUpdateFrame(FrameEventArgs args)
    {
        base.OnUpdateFrame(args);

        if (KeyboardState.IsKeyDown(Keys.Escape))
        {
            Close();
        }

        var reloadIsDown = KeyboardState.IsKeyDown(Keys.R);
        if (reloadIsDown && !_reloadWasDown)
        {
            ReloadShader(throwOnFailure: false);
        }

        _reloadWasDown = reloadIsDown;
    }

    protected override void OnResize(ResizeEventArgs args)
    {
        base.OnResize(args);
        GL.Viewport(0, 0, FramebufferSize.X, FramebufferSize.Y);
    }

    protected override void OnMouseMove(MouseMoveEventArgs args)
    {
        base.OnMouseMove(args);
        _mousePosition = ToShaderCoordinates(args.Position);
    }

    protected override void OnMouseDown(MouseButtonEventArgs args)
    {
        base.OnMouseDown(args);
        if (args.Button == MouseButton.Left)
        {
            _mouseDown = true;
            _mousePosition = ToShaderCoordinates(MousePosition);
            _clickOrigin = _mousePosition;
        }
    }

    protected override void OnMouseUp(MouseButtonEventArgs args)
    {
        base.OnMouseUp(args);
        if (args.Button == MouseButton.Left)
        {
            _mouseDown = false;
            _mousePosition = ToShaderCoordinates(MousePosition);
        }
    }

    protected override void OnUnload()
    {
        _program?.Dispose();
        GL.DeleteTexture(_blackTexture);
        GL.DeleteVertexArray(_vertexArray);
        base.OnUnload();
    }

    private void ReloadShader(bool throwOnFailure)
    {
        try
        {
            var source = File.ReadAllText(_shaderPath);
            var replacement = new ShaderProgram(ShaderSource.Vertex, ShaderSource.BuildFragment(source));
            var old = _program;
            _program = replacement;
            old?.Dispose();
            _frame = 0;
            _lastFrameTime = 0;
            Title = $"ShaderToy Sharp — {Path.GetFileName(_shaderPath)}";
            Console.WriteLine($"Loaded {_shaderPath}");
        }
        catch (Exception exception)
        {
            Console.Error.WriteLine(exception.Message);
            Title = $"ShaderToy Sharp — compile error — {Path.GetFileName(_shaderPath)}";
            if (throwOnFailure)
            {
                throw;
            }
        }
    }

    private void SetUniforms(ShaderProgram program, double time, double delta)
    {
        var width = FramebufferSize.X;
        var height = FramebufferSize.Y;
        Set(program, "iResolution", width, height, 1f);
        Set(program, "iTime", (float)time);
        Set(program, "iTimeDelta", (float)delta);
        Set(program, "iFrameRate", delta > 0 ? (float)(1.0 / delta) : 0f);
        Set(program, "iFrame", _frame);

        var clickSign = _mouseDown ? 1f : -1f;
        Set(program, "iMouse", _mousePosition.X, _mousePosition.Y,
            clickSign * _clickOrigin.X, clickSign * _clickOrigin.Y);

        var local = DateTime.Now;
        var seconds = (float)local.TimeOfDay.TotalSeconds;
        Set(program, "iDate", local.Year, local.Month, local.Day, seconds);
        Set(program, "iSampleRate", 44100f);

        for (var channel = 0; channel < 4; channel++)
        {
            GL.ActiveTexture(TextureUnit.Texture0 + channel);
            GL.BindTexture(TextureTarget.Texture2D, _blackTexture);
            Set(program, $"iChannel{channel}", channel);

            var resolution = program.Uniform($"iChannelResolution[{channel}]");
            if (resolution >= 0)
            {
                GL.Uniform3(resolution, 1f, 1f, 1f);
            }

            var channelTime = program.Uniform($"iChannelTime[{channel}]");
            if (channelTime >= 0)
            {
                GL.Uniform1(channelTime, (float)time);
            }
        }
    }

    private Vector2 ToShaderCoordinates(Vector2 position)
    {
        var scaleX = ClientSize.X > 0 ? (float)FramebufferSize.X / ClientSize.X : 1f;
        var scaleY = ClientSize.Y > 0 ? (float)FramebufferSize.Y / ClientSize.Y : 1f;
        return new Vector2(position.X * scaleX, (ClientSize.Y - position.Y) * scaleY);
    }

    private static int CreateBlackTexture()
    {
        var texture = GL.GenTexture();
        GL.BindTexture(TextureTarget.Texture2D, texture);
        var pixel = new byte[] { 0, 0, 0, 255 };
        GL.TexImage2D(TextureTarget.Texture2D, 0, PixelInternalFormat.Rgba8,
            1, 1, 0, PixelFormat.Rgba, PixelType.UnsignedByte, pixel);
        GL.TexParameter(TextureTarget.Texture2D, TextureParameterName.TextureMinFilter, (int)TextureMinFilter.Linear);
        GL.TexParameter(TextureTarget.Texture2D, TextureParameterName.TextureMagFilter, (int)TextureMagFilter.Linear);
        return texture;
    }

    private static void Set(ShaderProgram program, string name, float value)
    {
        var location = program.Uniform(name);
        if (location >= 0) GL.Uniform1(location, value);
    }

    private static void Set(ShaderProgram program, string name, int value)
    {
        var location = program.Uniform(name);
        if (location >= 0) GL.Uniform1(location, value);
    }

    private static void Set(ShaderProgram program, string name, float x, float y, float z)
    {
        var location = program.Uniform(name);
        if (location >= 0) GL.Uniform3(location, x, y, z);
    }

    private static void Set(ShaderProgram program, string name, float x, float y, float z, float w)
    {
        var location = program.Uniform(name);
        if (location >= 0) GL.Uniform4(location, x, y, z, w);
    }
}
