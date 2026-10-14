using System.Globalization;
using System.Windows;
using System.Windows.Input;
using System.Windows.Media;

namespace TrackNode.DevTools;

public sealed class BatteryChart : FrameworkElement
{
    private IReadOnlyList<BatteryChartPoint> points = [];
    private Point? pointer;

    public void SetPoints(IReadOnlyList<BatteryChartPoint> value)
    {
        points = value;
        InvalidateVisual();
    }

    protected override void OnMouseMove(MouseEventArgs args)
    {
        base.OnMouseMove(args);
        pointer = args.GetPosition(this);
        InvalidateVisual();
    }

    protected override void OnMouseLeave(MouseEventArgs args)
    {
        base.OnMouseLeave(args);
        pointer = null;
        InvalidateVisual();
    }

    protected override void OnRender(DrawingContext drawing)
    {
        base.OnRender(drawing);
        drawing.DrawRectangle(Brushes.Transparent, null, new Rect(RenderSize));
        var plot = new Rect(58, 30, Math.Max(1, ActualWidth - 108), Math.Max(1, ActualHeight - 65));
        var valid = points.Where(point => point.Millivolts.HasValue).ToArray();
        if (valid.Length == 0)
        {
            Label(drawing, "Connect to TrackNode to collect battery measurements", plot.Left, plot.Top + plot.Height / 2);
            return;
        }
        var low = valid.Min(point => point.Millivolts!.Value) / 1000.0;
        var high = valid.Max(point => point.Millivolts!.Value) / 1000.0;
        var padding = Math.Max((high - low) * .12, .01);
        low -= padding;
        high += padding;
        var first = points[0].Time;
        var last = points[^1].Time;
        var seconds = Math.Max(1, (last - first).TotalSeconds);
        double X(BatteryChartPoint point) => plot.Left + (point.Time - first).TotalSeconds / seconds * plot.Width;
        double Y(ushort mv) => plot.Bottom - (mv / 1000.0 - low) / (high - low) * plot.Height;
        double LevelY(byte level) => plot.Bottom - level / 100.0 * plot.Height;
        var grid = new Pen(new SolidColorBrush(Color.FromRgb(43, 53, 66)), 1);
        var usb = new SolidColorBrush(Color.FromArgb(38, 103, 232, 194));
        var voltage = new Pen(Brushes.Turquoise, 2);
        var levelLine = new Pen(Brushes.SandyBrown, 1.5) { DashStyle = DashStyles.Dash };
        for (int i = 1; i < points.Count; ++i)
        {
            var before = points[i - 1];
            var current = points[i];
            var continuous = before.Millivolts.HasValue && current.Millivolts.HasValue &&
                current.Time - before.Time < TimeSpan.FromSeconds(15);
            if (continuous && before.UsbPresent == true)
                drawing.DrawRectangle(usb, null, new Rect(X(before), plot.Top, Math.Max(0, X(current) - X(before)), plot.Height));
        }
        for (int i = 0; i <= 4; ++i)
        {
            var y = plot.Top + plot.Height * i / 4;
            drawing.DrawLine(grid, new Point(plot.Left, y), new Point(plot.Right, y));
            Label(drawing, (high - (high - low) * i / 4).ToString("F3", CultureInfo.InvariantCulture), plot.Left - 8, y - 8, 1);
            Label(drawing, $"{100 - i * 25}%", plot.Right + 8, y - 8, 0, Brushes.SandyBrown);
        }
        for (int i = 0; i <= 2; ++i)
            Label(drawing, first.AddSeconds((last - first).TotalSeconds * i / 2).ToLocalTime().ToString("HH:mm:ss"),
                plot.Left + plot.Width * i / 2, plot.Bottom + 12, i / 2.0);
        BatteryChartPoint? previous = null;
        foreach (var point in points)
        {
            if (point.Millivolts is not ushort mv) { previous = null; continue; }
            var position = new Point(X(point), Y(mv));
            if (previous?.Millivolts is ushort before && point.Time - previous.Time < TimeSpan.FromSeconds(15))
            {
                drawing.DrawLine(voltage, new Point(X(previous), Y(before)), position);
                if (previous.Level is byte beforeLevel && point.Level is byte currentLevel)
                    drawing.DrawLine(levelLine, new Point(X(previous), LevelY(beforeLevel)), new Point(X(point), LevelY(currentLevel)));
            }
            drawing.DrawEllipse(Brushes.Turquoise, null, position, 2, 2);
            previous = point;
        }
        if (pointer is Point mouse && plot.Contains(mouse))
        {
            var selected = valid.MinBy(point => Math.Abs(X(point) - mouse.X))!;
            drawing.DrawLine(grid, new Point(X(selected), plot.Top), new Point(X(selected), plot.Bottom));
            Label(drawing, $"{selected.Time.ToLocalTime():HH:mm:ss} · {selected.Millivolts} mV · ≈{selected.Level}% · USB {(selected.UsbPresent == true ? "present" : "absent")}", plot.Right, 0, 1);
        }
        else Label(drawing, $"{valid.Length} measurements · {valid.Min(p => p.Millivolts)}–{valid.Max(p => p.Millivolts)} mV", plot.Right, 0, 1);
    }

    private void Label(DrawingContext drawing, string text, double x, double y, double alignment = 0, Brush? brush = null)
    {
        var formatted = new FormattedText(text, CultureInfo.CurrentCulture, FlowDirection.LeftToRight,
            new Typeface("Segoe UI"), 11, brush ?? new SolidColorBrush(Color.FromRgb(146, 161, 179)), VisualTreeHelper.GetDpi(this).PixelsPerDip);
        drawing.DrawText(formatted, new Point(x - formatted.Width * alignment, y));
    }
}
