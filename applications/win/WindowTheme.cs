using System.Runtime.InteropServices;
using System.Windows;
using System.Windows.Interop;

namespace TrackNode.DevTools;

internal static class WindowTheme
{
    private const int UseImmersiveDarkMode = 20;
    private const int BorderColor = 34;
    private const int CaptionColor = 35;
    private const int TextColor = 36;

    public static void Apply(Window window)
    {
        var handle = new WindowInteropHelper(window).Handle;
        SetAttribute(handle, UseImmersiveDarkMode, 1);
        if (OperatingSystem.IsWindowsVersionAtLeast(10, 0, 22000))
        {
            SetAttribute(handle, CaptionColor, ColorRef(0x10, 0x14, 0x19));
            SetAttribute(handle, TextColor, ColorRef(0xE7, 0xED, 0xF4));
            SetAttribute(handle, BorderColor, ColorRef(0x2B, 0x35, 0x42));
        }
    }

    private static int ColorRef(byte red, byte green, byte blue) => red | (green << 8) | (blue << 16);

    private static void SetAttribute(IntPtr handle, int attribute, int value)
    {
        _ = DwmSetWindowAttribute(handle, attribute, ref value, sizeof(int));
    }

    [DllImport("dwmapi.dll", ExactSpelling = true)]
    private static extern int DwmSetWindowAttribute(IntPtr window, int attribute, ref int value, int size);
}
