using System.Buffers.Binary;
using System.IO;

namespace TrackNode.DevTools;

public sealed record BatteryMeasurement(byte Flags, byte Level, ushort BatteryMv, ushort SupplyMv,
    ushort Raw, ushort ReferenceRaw, ushort MinimumRaw, ushort MaximumRaw, uint AgeMs)
{
    public bool Valid => (Flags & 1) != 0 && AgeMs < 15000;
    public bool UsbPresent => (Flags & 2) != 0;
    public bool Low => (Flags & 4) != 0;

    public static BatteryMeasurement Decode(ReadOnlySpan<byte> bytes)
    {
        if (bytes.Length != 20 || bytes[0] != 1 || bytes[3] != 0 || (bytes[1] & ~7) != 0 || bytes[2] > 100)
            throw new InvalidDataException("Unsupported or malformed battery snapshot.");
        var value = new BatteryMeasurement(bytes[1], bytes[2], Read(bytes, 4), Read(bytes, 6), Read(bytes, 8), Read(bytes, 10),
            Read(bytes, 12), Read(bytes, 14), BinaryPrimitives.ReadUInt32LittleEndian(bytes[16..]));
        var malformedReading = (value.Flags & 1) != 0 &&
            (value.BatteryMv is < 2500 or > 4500 || value.SupplyMv is < 1800 or > 3600 ||
             value.MinimumRaw == 0 || value.MinimumRaw > value.Raw || value.Raw > value.MaximumRaw ||
             value.MaximumRaw >= 4095 || value.ReferenceRaw is 0 or >= 4095);
        var malformedUnavailable = (value.Flags & 1) == 0 &&
            (value.Level != 0 || value.BatteryMv != 0 || value.SupplyMv != 0 || value.Raw != 0 ||
             value.ReferenceRaw != 0 || value.MinimumRaw != 0 || value.MaximumRaw != 0);
        if (malformedReading || malformedUnavailable)
            throw new InvalidDataException("Invalid battery values.");
        return value;
    }

    public static byte DecodeLevel(ReadOnlySpan<byte> bytes)
    {
        if (bytes.Length != 1 || bytes[0] > 100) throw new InvalidDataException("Invalid Battery Level.");
        return bytes[0];
    }

    private static ushort Read(ReadOnlySpan<byte> bytes, int offset) =>
        BinaryPrimitives.ReadUInt16LittleEndian(bytes.Slice(offset, 2));
}

public static class BatteryCharacteristics
{
    public static readonly Guid ServiceUuid = new("0000180f-0000-1000-8000-00805f9b34fb");
    public static readonly Guid LevelUuid = new("00002a19-0000-1000-8000-00805f9b34fb");
    public static readonly Guid StatusUuid = new("e0910006-7e83-4c9c-a89e-bfe03c940001");
}
