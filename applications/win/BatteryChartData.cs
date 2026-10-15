using System.Globalization;
using System.Text;

namespace TrackNode.DevTools;

public sealed record BatteryChartPoint(DateTimeOffset Time, ushort? Millivolts, byte? Level, bool? UsbPresent,
    byte? Quality = null, ushort? Raw = null, ushort? ReferenceRaw = null, ushort? SupplyMv = null, ushort? MinimumRaw = null, ushort? MaximumRaw = null);

public sealed class BatteryChartData
{
    public const int Capacity = 10000;
    private readonly List<BatteryChartPoint> points = [];
    public IReadOnlyList<BatteryChartPoint> Points => points;

    public bool Add(BatteryMeasurement measurement, DateTimeOffset received)
    {
        if (!measurement.Valid) return Break(received);
        var time = received.AddMilliseconds(-measurement.AgeMs);
        var previous = points.LastOrDefault();
        if (previous is not null && time - previous.Time < TimeSpan.FromSeconds(2)) return false;
        points.Add(new(time, measurement.BatteryMv, measurement.Level, measurement.UsbPresent,
            measurement.Quality, measurement.Raw, measurement.ReferenceRaw, measurement.SupplyMv, measurement.MinimumRaw, measurement.MaximumRaw));
        Trim();
        return true;
    }

    public bool Break(DateTimeOffset time)
    {
        if (points.Count == 0 || points[^1].Millivolts is null || time <= points[^1].Time) return false;
        points.Add(new(time, null, null, null));
        Trim();
        return true;
    }

    public void Clear() => points.Clear();

    public IReadOnlyList<BatteryChartPoint> Select(TimeSpan? range)
    {
        if (range is null || points.Count == 0) return points.ToArray();
        var start = points[^1].Time - range.Value;
        return points.Where(point => point.Time >= start).ToArray();
    }

    public string Export()
    {
        var text = new StringBuilder("timestamp,battery_mv_filtered,level_percent_estimate,usb_present,quality,battery_raw,vrefint_raw,supply_mv,min_raw,max_raw\n");
        foreach (var point in points)
            text.Append(CultureInfo.InvariantCulture, $"{point.Time:O},{point.Millivolts},{point.Level},{(point.UsbPresent.HasValue ? point.UsbPresent.Value ? "1" : "0" : "")},{point.Quality},{point.Raw},{point.ReferenceRaw},{point.SupplyMv},{point.MinimumRaw},{point.MaximumRaw}\n");
        return text.ToString();
    }

    private void Trim()
    {
        if (points.Count > Capacity) points.RemoveRange(0, points.Count - Capacity);
    }
}
