using System.Globalization;
using System.Windows;
using System.Windows.Input;
using System.Windows.Media;

namespace TrackNode.DevTools;

public sealed class HistoryChart : FrameworkElement
{
    private IReadOnlyList<HistoryChartPoint> points = [];
    private Point? pointer;
    public bool IsPressure { get; set; }
    private Brush Accent => IsPressure ? Brushes.SandyBrown : Brushes.Turquoise;
    private string Unit => IsPressure ? "hPa" : "°C";
    private double? Value(HistoryChartPoint point) => IsPressure ? point.PressureHpa : point.Temperature;

    public void SetPoints(IReadOnlyList<HistoryChartPoint> value)
    {
        points = value;
        pointer = null;
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
        var plot = new Rect(64, 24, Math.Max(1, ActualWidth - 80), Math.Max(1, ActualHeight - 54));
        var valid = points.Where(point => Value(point).HasValue).ToArray();
        if (valid.Length == 0)
        {
            Label(drawing, "No measurements yet · connect and Sync history, or open a saved JSON", plot.Left, plot.Top + plot.Height / 2);
            return;
        }
        var low = valid.Min(point => Value(point)!.Value);
        var high = valid.Max(point => Value(point)!.Value);
        var padding = Math.Max((high - low) * .12, IsPressure ? .5 : .3);
        low -= padding;
        high += padding;
        var first = points.Min(point => point.Time);
        var last = points.Max(point => point.Time);
        var seconds = Math.Max(1, (last - first).TotalSeconds);
        double X(HistoryChartPoint point) => plot.Left + (point.Time - first).TotalSeconds / seconds * plot.Width;
        double Y(double value) => plot.Bottom - (value - low) / (high - low) * plot.Height;
        var grid = new Pen(new SolidColorBrush(Color.FromRgb(43, 53, 66)), 1);
        var intervals = Math.Clamp((int)(plot.Height / 28), 1, 4);
        for (int i = 0; i <= intervals; ++i)
        {
            var y = plot.Top + plot.Height * i / intervals;
            drawing.DrawLine(grid, new Point(plot.Left, y), new Point(plot.Right, y));
            Label(drawing, (high - (high - low) * i / intervals).ToString("F1", CultureInfo.InvariantCulture), 0, y - 8);
        }
        for (int i = 0; i <= 2; ++i)
        {
            var time = first.AddSeconds((last - first).TotalSeconds * i / 2).ToLocalTime();
            var label = time.ToString(last - first >= TimeSpan.FromDays(1) ? "dd MMM HH:mm" : "HH:mm:ss");
            Label(drawing, label, plot.Left + plot.Width * i / 2, plot.Bottom + 12, i / 2.0);
        }
        var line = new Pen(Accent, 2);
        Point? previous = null;
        foreach (var point in points)
        {
            if (Value(point) is not double value) { previous = null; continue; }
            var position = new Point(X(point), Y(value));
            if (previous.HasValue) drawing.DrawLine(line, previous.Value, position);
            drawing.DrawEllipse(Accent, null, position, 2.5, 2.5);
            previous = position;
        }
        if (pointer is Point mouse && plot.Contains(mouse))
        {
            var selected = valid.MinBy(point => Math.Abs(X(point) - mouse.X))!;
            var x = X(selected);
            drawing.DrawLine(grid, new Point(x, plot.Top), new Point(x, plot.Bottom));
            drawing.DrawEllipse(Accent, new Pen(Brushes.White, 1), new Point(x, Y(Value(selected)!.Value)), 5, 5);
            Label(drawing, $"{selected.Time.ToLocalTime():dd MMM HH:mm:ss} · {Value(selected):F2} {Unit} · #{selected.Id}", plot.Right, 0, 1, Accent);
        }
        else Label(drawing, $"{valid.Length} points · {valid.Min(point => Value(point)):F2}–{valid.Max(point => Value(point)):F2} {Unit}", plot.Right, 0, 1, Accent);
    }

    private void Label(DrawingContext drawing, string text, double x, double y, double alignment = 0, Brush? brush = null)
    {
        var formatted = new FormattedText(text, CultureInfo.CurrentCulture, FlowDirection.LeftToRight,
            new Typeface("Segoe UI"), 11, brush ?? new SolidColorBrush(Color.FromRgb(146, 161, 179)), VisualTreeHelper.GetDpi(this).PixelsPerDip);
        drawing.DrawText(formatted, new Point(x - formatted.Width * alignment, y));
    }
}
