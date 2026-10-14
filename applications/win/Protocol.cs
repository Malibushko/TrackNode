using System.Globalization;
using System.IO;
using System.Text;
using System.Text.RegularExpressions;

namespace TrackNode.DevTools;

public sealed class LineFramer
{
    private readonly StringBuilder pending = new();
    private DateTimeOffset lastByte;
    public bool HasPartial => pending.Length > 0;

    public IReadOnlyList<string> Feed(ReadOnlySpan<byte> bytes, DateTimeOffset now)
    {
        var lines = new List<string>();
        foreach (var value in bytes)
        {
            if (value == '\n')
            {
                lines.Add(pending.ToString().TrimEnd('\r'));
                pending.Clear();
            }
            else
            {
                if (value > 127 || value == 0 || pending.Length >= 1024)
                    throw new InvalidDataException("Invalid or oversized ASCII stream. Reconnect to resynchronize.");
                pending.Append((char)value);
            }
        }
        lastByte = now;
        return lines;
    }

    public bool IsExpired(DateTimeOffset now) => HasPartial && now - lastByte > TimeSpan.FromSeconds(3);
    public void Reset() => pending.Clear();
}

public sealed record Sample(ulong TimestampMs, double Temperature, double Pressure,
    ulong? GnssSequence, ulong? GnssAgeMs, bool PositionValid, double? Latitude, double? Longitude,
    bool SpeedValid, double? Speed, bool TimeValid, long? UtcUnixSeconds)
{
    public static bool TryParse(string line, out Sample? sample)
    {
        sample = null;
        var fields = line.Split(',');
        if (fields.Length != 13 || fields[0] != "T") return false;
        var culture = CultureInfo.InvariantCulture;
        bool Number(int i, out double value) => double.TryParse(fields[i], NumberStyles.Float, culture, out value) && double.IsFinite(value);
        double? Optional(int i) => Number(i, out var value) ? value : null;
        if (!ulong.TryParse(fields[1], out var timestamp) || !Number(2, out var temperature) ||
            !Number(3, out var pressure) || !Flag(fields[6]) || !Flag(fields[9]) || !Flag(fields[11])) return false;
        ulong? sequence = ulong.TryParse(fields[4], out var seqValue) ? seqValue : null;
        ulong? age = ulong.TryParse(fields[5], out var ageValue) ? ageValue : null;
        if ((fields[4].Length > 0 && sequence is null) || (fields[5].Length > 0 && age is null)) return false;
        if (sequence.HasValue != age.HasValue) return false;
        var lat = Optional(7);
        var lon = Optional(8);
        var speed = Optional(10);
        long? utc = long.TryParse(fields[12], out var seconds) ? seconds : null;
        if ((fields[6] == "1" || fields[9] == "1" || fields[11] == "1") && sequence is null) return false;
        if (fields[6] == "1" && (lat is null || lon is null || lat < -90 || lat > 90 || lon < -180 || lon > 180)) return false;
        if (fields[9] == "1" && (speed is null || speed < 0)) return false;
        if (fields[11] == "1" && utc is null) return false;
        sample = new(timestamp, temperature, pressure, sequence, age, fields[6] == "1", lat, lon,
            fields[9] == "1", speed, fields[11] == "1", utc);
        return true;
    }

    private static bool Flag(string value) => value is "0" or "1";
}

public sealed class ReplyTracker(string command)
{
    private int remaining = -1;
    public List<string> Lines { get; } = [];

    public bool Accept(string line)
    {
        if (line.StartsWith("T,", StringComparison.Ordinal) || line.StartsWith("L,", StringComparison.Ordinal)) return false;
        Lines.Add(line);
        if (line.StartsWith("ERR ", StringComparison.Ordinal)) return true;
        if (command.StartsWith("H R ", StringComparison.Ordinal)) return line.StartsWith("H E ", StringComparison.Ordinal);
        if (command.Equals("GET BATTERY", StringComparison.OrdinalIgnoreCase) && remaining < 0 &&
            line.StartsWith("battery_mv=", StringComparison.Ordinal))
        {
            remaining = 1;
            return false;
        }
        if (command == "GET GNSS" && remaining < 0 && line.StartsWith("GNSS seq=", StringComparison.Ordinal))
        {
            remaining = new[] { "pos", "speed", "time_field" }.Count(field =>
                Regex.IsMatch(line, $@"(?:^| ){field}=1(?: |$)"));
            return remaining == 0;
        }
        if (remaining > 0) return --remaining == 0;
        return true;
    }
}

public static class CommandEncoding
{
    public static byte[] Encode(string text, int maxPayload)
    {
        if (string.IsNullOrWhiteSpace(text) || text.Any(c => c < 32 || c > 126))
            throw new ArgumentException("Command must contain one printable ASCII line.");
        var data = Encoding.ASCII.GetBytes(text);
        if (data.Length > Math.Min(64, maxPayload))
            throw new ArgumentException($"Command is {data.Length} bytes; current limit is {Math.Min(64, maxPayload)}. Wait for a larger MTU or use a shorter command.");
        return data;
    }
}
