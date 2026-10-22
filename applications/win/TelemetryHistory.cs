using System.Buffers.Binary;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Security.Cryptography;
using System.Text.Json;

namespace TrackNode.DevTools;

public sealed record HistorySnapshot(string DeviceId, ulong Epoch, uint UptimeSeconds,
    ulong First, ulong Last, int Count, ulong Overwritten, DateTimeOffset PhoneUtc, double RoundTripSeconds)
{
    public static HistorySnapshot Parse(IReadOnlyList<string> lines, DateTimeOffset phoneUtc, double roundTripSeconds)
    {
        if (lines.Count != 1) throw new InvalidDataException("Invalid history metadata.");
        var fields = lines[0].Split(' ');
        if (fields.Length != 10 || fields[0] != "H" || fields[1] != "O" || fields[2] != "2" ||
            fields[3].Length != 24 || !fields[3].All(Uri.IsHexDigit))
            throw new InvalidDataException("Unsupported or invalid history metadata.");
        var epoch = HistoryWire.Hex(fields[4]);
        var uptime = HistoryWire.Hex(fields[5], 8);
        var first = HistoryWire.Hex(fields[6]);
        var last = HistoryWire.Hex(fields[7]);
        var count = HistoryWire.Hex(fields[8], 8);
        var overwritten = HistoryWire.Hex(fields[9]);
        var emptyBounds = count == 0 && first == 0 && last == 0;
        const int MaxFlashJournalRecords = 258_048;
        var populatedBounds = count is > 0 and <= MaxFlashJournalRecords && first != 0 && first <= last && last - first + 1 >= count;
        if (epoch == 0 || !(emptyBounds || populatedBounds))
            throw new InvalidDataException("Invalid history bounds.");
        return new(fields[3].ToUpperInvariant(), epoch, (uint)uptime, first, last, (int)count, overwritten, phoneUtc, roundTripSeconds);
    }
}

public sealed record HistoryRecord(ulong Id, string Raw, uint UptimeSeconds, uint PositionUptimeSeconds,
    bool EnvironmentValid, bool PositionValid, double? Temperature, uint? PressurePa, double? Latitude, double? Longitude)
{
    public static HistoryRecord Parse(ulong id, string raw)
    {
        if (raw.Length != 62 || !raw.All(Uri.IsHexDigit)) throw new InvalidDataException("Invalid packed history sample.");
        var bytes = Convert.FromHexString(raw);
        if (id == 0 || BinaryPrimitives.ReadUInt64LittleEndian(bytes) != id)
            throw new InvalidDataException("History sample ID mismatch.");
        var seconds = BinaryPrimitives.ReadUInt32LittleEndian(bytes.AsSpan(8));
        var positionSeconds = BinaryPrimitives.ReadUInt32LittleEndian(bytes.AsSpan(12));
        var flags = bytes[16];
        if ((flags & ~3) != 0) throw new InvalidDataException("Unknown history validity flags.");
        var environmentValid = (flags & 1) != 0;
        var positionValid = (flags & 2) != 0;
        var pressure = BinaryPrimitives.ReadUInt32LittleEndian(bytes.AsSpan(19));
        if (pressure > 200000 || (!environmentValid && bytes.AsSpan(17, 6).ContainsAnyExcept((byte)0)))
            throw new InvalidDataException("Invalid environment history payload.");
        var latitude = BinaryPrimitives.ReadInt32LittleEndian(bytes.AsSpan(23)) / 1e7;
        var longitude = BinaryPrimitives.ReadInt32LittleEndian(bytes.AsSpan(27)) / 1e7;
        if (Math.Abs(latitude) > 90 || Math.Abs(longitude) > 180 ||
            (!positionValid && (positionSeconds != 0 || bytes.AsSpan(23, 8).ContainsAnyExcept((byte)0))))
            throw new InvalidDataException("Invalid position history payload.");
        return new(id, raw.ToUpperInvariant(), seconds, positionSeconds, environmentValid, positionValid,
            environmentValid ? BinaryPrimitives.ReadInt16LittleEndian(bytes.AsSpan(17)) / 100.0 : null,
            environmentValid ? pressure : null, positionValid ? latitude : null, positionValid ? longitude : null);
    }
}

public sealed record HistoryPage(ulong AfterSampleId, ulong LastSampleId, IReadOnlyList<HistoryRecord> Records)
{
    public static HistoryPage Parse(IReadOnlyList<string> lines, ulong cursor, HistorySnapshot snapshot)
    {
        if (lines.Count < 3) throw new InvalidDataException("Incomplete history page.");
        var header = lines[0].Split(' ');
        var footer = lines[^1].Split(' ');
        if (header.Length != 5 || header[0] != "H" || header[1] != "B" ||
            footer.Length != 3 || footer[0] != "H" || footer[1] != "E")
            throw new InvalidDataException("Invalid history page envelope.");
        var after = HistoryWire.Hex(header[2]);
        var last = HistoryWire.Hex(header[3]);
        var count = HistoryWire.Hex(header[4], 8);
        if (after != cursor || last <= after || last > snapshot.Last || count is 0 or > 8 ||
            HistoryWire.Hex(footer[2]) != last || (ulong)lines.Count != count + 2)
            throw new InvalidDataException("History page has wrong bounds or record count.");
        var records = new List<HistoryRecord>();
        var previousId = after;
        for (int index = 1; index < lines.Count - 1; index++)
        {
            var fields = lines[index].Split(' ');
            if (fields.Length != 4 || fields[0] != "H" || fields[1] != "D")
                throw new InvalidDataException("Invalid history sample envelope.");
            var id = HistoryWire.Hex(fields[2]);
            if (id <= previousId || id < snapshot.First || id > last)
                throw new InvalidDataException("History samples are out of order.");
            var record = HistoryRecord.Parse(id, fields[3]);
            if (HistoryWire.SameSession(id, snapshot) &&
                (record.UptimeSeconds > snapshot.UptimeSeconds || record.PositionUptimeSeconds > snapshot.UptimeSeconds))
                throw new InvalidDataException("History timestamp exceeds its snapshot.");
            records.Add(record);
            previousId = id;
        }
        if (previousId != last || (after == 0 && records[0].Id != snapshot.First))
            throw new InvalidDataException("History page is missing samples.");
        return new(after, last, records);
    }
}

public static class HistoryWire
{
    public const int MinimalPacketSize = 20;

    public static ulong Hex(string value, int length = 16)
    {
        if (value.Length != length || !ulong.TryParse(value, NumberStyles.AllowHexSpecifier,
            CultureInfo.InvariantCulture, out var result)) throw new InvalidDataException("Invalid history integer.");
        return result;
    }

    public static string Command(char operation, ulong value) => $"H {operation} {value:X16}";

    public static bool SameSession(ulong sampleId, HistorySnapshot snapshot)
    {
        var legacySingleSession = (snapshot.First >> 32) == 0 && (snapshot.Last >> 32) == 0;
        return (sampleId >> 32) == snapshot.Epoch || (legacySingleSession && sampleId <= uint.MaxValue);
    }
}

public sealed record SavedHistoryRecord(HistoryRecord Record, DateTimeOffset? EstimatedUtc, DateTimeOffset? EstimatedPositionUtc);
public sealed class SavedHistory
{
    public string DeviceId { get; set; } = "";
    public ulong Epoch { get; set; }
    public List<HistorySnapshot> Anchors { get; set; } = [];
    public SortedDictionary<ulong, SavedHistoryRecord> Records { get; set; } = [];
}

public sealed class HistoryStore(string directory)
{
    public string SavePage(HistorySnapshot snapshot, HistoryPage page)
    {
        Directory.CreateDirectory(directory);
        var path = Path.Combine(directory, $"{snapshot.DeviceId}.json");
        var saved = File.Exists(path) ? JsonSerializer.Deserialize<SavedHistory>(File.ReadAllText(path)) :
            new SavedHistory { DeviceId = snapshot.DeviceId, Epoch = snapshot.Epoch };
        if (saved is null || saved.DeviceId != snapshot.DeviceId)
            throw new InvalidDataException("Local history identity mismatch.");
        saved.Epoch = snapshot.Epoch;
        foreach (var record in page.Records)
        {
            if (saved.Records.TryGetValue(record.Id, out var existing))
            {
                if (existing.Record.Raw != record.Raw) throw new InvalidDataException("Conflicting history record identity.");
                continue;
            }
            var sameSession = HistoryWire.SameSession(record.Id, snapshot);
            DateTimeOffset? utc = sameSession ? snapshot.PhoneUtc.AddSeconds((long)record.UptimeSeconds - snapshot.UptimeSeconds) : null;
            DateTimeOffset? positionUtc = sameSession && record.PositionValid ?
                snapshot.PhoneUtc.AddSeconds((long)record.PositionUptimeSeconds - snapshot.UptimeSeconds) : null;
            saved.Records.Add(record.Id, new(record, utc, positionUtc));
        }
        if (!saved.Anchors.Contains(snapshot)) saved.Anchors.Add(snapshot);
        var temporary = path + "." + Guid.NewGuid().ToString("N") + ".tmp";
        try
        {
            using (var file = new FileStream(temporary, FileMode.CreateNew, FileAccess.Write, FileShare.None,
                4096, FileOptions.WriteThrough))
            {
                JsonSerializer.Serialize(file, saved, new JsonSerializerOptions { WriteIndented = true });
                file.Flush(flushToDisk: true);
            }
            if (File.Exists(path)) File.Replace(temporary, path, null);
            else File.Move(temporary, path);
            return path;
        }
        finally
        {
            if (File.Exists(temporary)) File.Delete(temporary);
        }
    }
}

public sealed record HistorySyncResult(int Records, ulong Overwritten, string? Path);

public static class HistorySync
{
    public static async Task<HistorySyncResult> RunAsync(Func<string, Task<IReadOnlyList<string>>> send,
        Func<HistorySnapshot, HistoryPage, Task<string>> save, Action<int, int>? progress = null)
    {
        ulong nonce;
        do { nonce = BinaryPrimitives.ReadUInt64LittleEndian(RandomNumberGenerator.GetBytes(8)); } while (nonce == 0);
        var startedUtc = DateTimeOffset.UtcNow;
        var started = Stopwatch.GetTimestamp();
        var metadata = await send(HistoryWire.Command('O', nonce));
        var elapsed = Stopwatch.GetElapsedTime(started).TotalSeconds;
        var snapshot = HistorySnapshot.Parse(metadata, startedUtc.AddSeconds(elapsed / 2), elapsed);
        ulong cursor = 0;
        var count = 0;
        string? path = null;
        while (count < snapshot.Count)
        {
            var lines = await send(HistoryWire.Command('R', cursor));
            var page = HistoryPage.Parse(lines, cursor, snapshot);
            var wouldExceedSnapshot = count + page.Records.Count > snapshot.Count;
            var completesSnapshot = count + page.Records.Count == snapshot.Count;
            if (wouldExceedSnapshot || completesSnapshot != (page.LastSampleId == snapshot.Last))
                throw new InvalidDataException("History snapshot count mismatch.");
            path = await save(snapshot, page);
            var acknowledgement = await send(HistoryWire.Command('A', page.LastSampleId));
            if (acknowledgement.Count != 1 || acknowledgement[0] != $"H A {page.LastSampleId:X16}")
                throw new InvalidDataException("History ACK was not confirmed. Saved data can be synchronized again.");
            cursor = page.LastSampleId;
            count += page.Records.Count;
            progress?.Invoke(count, snapshot.Count);
        }
        return new(count, snapshot.Overwritten, path);
    }
}
