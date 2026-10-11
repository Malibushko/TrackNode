using System.Buffers.Binary;
using System.IO;

namespace TrackNode.DevTools;

public sealed record MeasurementStatus(byte Flags, uint Sequence, uint AgeMs, ulong GnssSequence)
{
    public bool SensorValid => (Flags & 1) != 0;
    public static MeasurementStatus Decode(ReadOnlySpan<byte> bytes)
    {
        Measurements.Check(bytes, 18);
        return new(bytes[1], BinaryPrimitives.ReadUInt32LittleEndian(bytes[2..]),
            BinaryPrimitives.ReadUInt32LittleEndian(bytes[6..]), BinaryPrimitives.ReadUInt64LittleEndian(bytes[10..]));
    }
}

public sealed record GnssMeasurement(byte Flags, double Latitude, double Longitude, double Speed, uint Utc, uint AgeMs)
{
    public bool Present => (Flags & 2) != 0;
    public bool PositionValid => (Flags & 4) != 0;
    public bool Fresh => (Flags & 32) != 0;
    public static GnssMeasurement Decode(ReadOnlySpan<byte> bytes)
    {
        Measurements.Check(bytes, 20);
        var lat = BinaryPrimitives.ReadInt32LittleEndian(bytes[2..]) / 1e7;
        var lon = BinaryPrimitives.ReadInt32LittleEndian(bytes[6..]) / 1e7;
        if ((bytes[1] & 1) != 0 || ((bytes[1] & 4) != 0 && (Math.Abs(lat) > 90 || Math.Abs(lon) > 180)))
            throw new InvalidDataException("Invalid GNSS flags or coordinates.");
        return new(bytes[1], lat, lon, BinaryPrimitives.ReadUInt16LittleEndian(bytes[10..]) / 100.0,
            BinaryPrimitives.ReadUInt32LittleEndian(bytes[12..]), BinaryPrimitives.ReadUInt32LittleEndian(bytes[16..]));
    }
}

public static class Measurements
{
    public static readonly Guid ServiceUuid = new("0000181a-0000-1000-8000-00805f9b34fb");
    public static readonly Guid TemperatureUuid = new("00002a6e-0000-1000-8000-00805f9b34fb");
    public static readonly Guid PressureUuid = new("00002a6d-0000-1000-8000-00805f9b34fb");
    public static readonly Guid GnssUuid = new("e0910004-7e83-4c9c-a89e-bfe03c940001");
    public static readonly Guid StatusUuid = new("e0910005-7e83-4c9c-a89e-bfe03c940001");

    public static double Temperature(ReadOnlySpan<byte> bytes)
    {
        if (bytes.Length != 2) throw new InvalidDataException("Invalid Temperature length.");
        var value = BinaryPrimitives.ReadInt16LittleEndian(bytes);
        if (value < -27315) throw new InvalidDataException("Invalid Temperature value.");
        return value / 100.0;
    }

    public static double Pressure(ReadOnlySpan<byte> bytes)
    {
        if (bytes.Length != 4) throw new InvalidDataException("Invalid Pressure length.");
        return BinaryPrimitives.ReadUInt32LittleEndian(bytes) / 1000.0;
    }

    internal static void Check(ReadOnlySpan<byte> bytes, int size)
    {
        if (bytes.Length != size || bytes[0] != 1 || (bytes[1] & 192) != 0 ||
            ((bytes[1] & 2) == 0 && (bytes[1] & 60) != 0))
            throw new InvalidDataException("Unsupported or malformed measurement schema.");
    }
}
