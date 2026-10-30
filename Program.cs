using OpenTK.Mathematics;
using OpenTK.Windowing.Common;
using OpenTK.Windowing.Desktop;

namespace ShaderToySharp;

internal static class Program
{
    private static void Main(string[] args)
    {
        var validateOnly = args.Contains("--validate", StringComparer.OrdinalIgnoreCase);
        var shaderArgument = args.FirstOrDefault(argument => !argument.StartsWith("--", StringComparison.Ordinal));
        var shaderPath = shaderArgument is not null
            ? Path.GetFullPath(shaderArgument)
            : Path.Combine(AppContext.BaseDirectory, "Shaders", "default.frag");

        if (!File.Exists(shaderPath))
        {
            Console.Error.WriteLine($"Shader file not found: {shaderPath}");
            Environment.ExitCode = 1;
            return;
        }

        var nativeSettings = new NativeWindowSettings
        {
            ClientSize = new Vector2i(1280, 720),
            Title = $"ShaderToy Sharp — {Path.GetFileName(shaderPath)}",
            APIVersion = new Version(3, 3),
            Profile = ContextProfile.Core,
            Flags = ContextFlags.ForwardCompatible,
            StartVisible = !validateOnly
        };

        using var window = new ShaderToyWindow(
            GameWindowSettings.Default, nativeSettings, shaderPath, validateOnly);
        window.Run();
    }
}
