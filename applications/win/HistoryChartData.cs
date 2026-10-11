using System.IO;
using System.Text.Json;

namespace TrackNode.DevTools;

public sealed record HistoryChartPoint(ulong Id, DateTimeOffset Time, double? Temperature, double? PressureHpa);

public static class HistoryChartData
{
    public static SavedHistory Load(string path)
    {
        var saved = JsonSerializer.Deserialize<SavedHistory>(File.ReadAllText(path));
        if (saved is null || saved.DeviceId.Length != 24 || !saved.DeviceId.All(Uri.IsHexDigit) || saved.Epoch == 0 ||
            saved.Records is null || saved.Anchors is null)
            throw new InvalidDataException("Invalid saved history identity.");
        foreach (var (id, item) in saved.Records)
        {
            if (item is null || item.Record is null || id != item.Record.Id ||
                HistoryRecord.Parse(id, item.Record.Raw) != item.Record)
                throw new InvalidDataException("Invalid saved history record.");
        }
        return saved;
    }

    public static IReadOnlyList<HistoryChartPoint> Select(SavedHistory saved, int limit)
    {
        var records = limit > 0 ? saved.Records.Values.TakeLast(limit) : saved.Records.Values;
        return records.Select(item => new HistoryChartPoint(item.Record.Id, item.EstimatedUtc,
            item.Record.EnvironmentValid ? item.Record.Temperature : null,
            item.Record.EnvironmentValid && item.Record.PressurePa.HasValue ? item.Record.PressurePa.Value / 100.0 : null)).ToArray();
    }
}
