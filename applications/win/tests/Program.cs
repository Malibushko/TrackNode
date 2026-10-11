using System.Text;
using TrackNode.DevTools;

var checks = 0;
void Check(bool condition)
{
    ++checks;
    if (!condition) throw new Exception($"Check {checks} failed");
}
var now = DateTimeOffset.UtcNow;
var bytes = Encoding.ASCII.GetBytes("PONG\r\nL,123,a,b\r\nT,1,23.40,100000,,,0,,,0,,0,\r\n");
for (var split = 0; split <= bytes.Length; ++split)
{
    var framer = new LineFramer();
    var lines = framer.Feed(bytes.AsSpan(0, split), now).Concat(framer.Feed(bytes.AsSpan(split), now)).ToArray();
    Check(lines.Length == 3 && lines[0] == "PONG" && lines[1] == "L,123,a,b");
    Check(!framer.HasPartial);
}
var partial = new LineFramer();
partial.Feed("PON"u8, now);
Check(partial.IsExpired(now.AddSeconds(4)));
partial.Reset();
Check(partial.Feed("PONG\r\n"u8, now).Single() == "PONG");
foreach (var invalid in new[] { new byte[] { 0 }, new byte[] { 255 }, Enumerable.Repeat((byte)'X', 1025).ToArray() })
{
    try { new LineFramer().Feed(invalid, now); Check(false); }
    catch (InvalidDataException) { Check(true); }
}
Check(Sample.TryParse("T,1,23.40,100000,,,0,,,0,,0,", out var missing) && missing!.GnssSequence is null);
Check(Sample.TryParse("T,1,23.40,100000,12,150,1,50.1,30.2,1,0.5,1,1234", out var valid) && valid!.PositionValid);
foreach (var bad in new[] { "T,1,NaN,100000,,,0,,,0,,0,", "T,1,23,100000,12,150,1,,,0,,0,", "T,1,23,100000,12,150,2,,,0,,0," })
    Check(!Sample.TryParse(bad, out _));
var tracker = new ReplyTracker("GET GNSS");
Check(!tracker.Accept("L,3,log"));
Check(!tracker.Accept("GNSS seq=12 age=150 ms pos=1 speed=1 time_field=1"));
Check(!tracker.Accept("GNSS lat=50.1 lon=30.2"));
Check(!tracker.Accept("T,1,23.40,100000,,,0,,,0,,0,"));
Check(!tracker.Accept("GNSS speed=0.5 m/s"));
Check(tracker.Accept("GNSS utc_unix_s=1234"));
Check(tracker.Lines.Count == 4);
Check(new ReplyTracker("GET GNSS").Accept("GNSS seq=12 age=150 ms pos=0 speed=0 time_field=0"));
Check(new ReplyTracker("PING").Accept("ERR COMMAND_QUEUE_FULL"));
Check(CommandEncoding.Encode("SET LOG_STREAM ON", 20).Length == 17);
foreach (var command in new[] { "SET SAMPLE_PERIOD 60000", "PING\nPING", "", "ПИНГ" })
{
    try { CommandEncoding.Encode(command, 20); Check(false); }
    catch (ArgumentException) { Check(true); }
}
Check(Measurements.Temperature(new byte[] { 0x83, 0xff }) == -1.25);
Check(Measurements.Pressure(new byte[] { 0x40, 0x42, 0x0f, 0 }) == 1000);
var statusBytes = new byte[18];
statusBytes[0] = 1;
statusBytes[1] = 1;
System.Buffers.Binary.BinaryPrimitives.WriteUInt32LittleEndian(statusBytes.AsSpan(2), 42);
System.Buffers.Binary.BinaryPrimitives.WriteUInt64LittleEndian(statusBytes.AsSpan(10), ulong.MaxValue);
var status = MeasurementStatus.Decode(statusBytes);
Check(status.SensorValid && status.Sequence == 42 && status.GnssSequence == ulong.MaxValue);
var gnssBytes = new byte[20];
gnssBytes[0] = 1;
gnssBytes[1] = 38;
System.Buffers.Binary.BinaryPrimitives.WriteInt32LittleEndian(gnssBytes.AsSpan(2), -900000000);
System.Buffers.Binary.BinaryPrimitives.WriteInt32LittleEndian(gnssBytes.AsSpan(6), 1800000000);
var gnss = GnssMeasurement.Decode(gnssBytes);
Check(gnss.Fresh && gnss.Present && gnss.PositionValid && gnss.Latitude == -90 && gnss.Longitude == 180);
gnssBytes[1] = 2;
Check(!GnssMeasurement.Decode(gnssBytes).PositionValid);
gnssBytes[1] = 0;
Check(!GnssMeasurement.Decode(gnssBytes).Present);
foreach (var invalid in new[] { new byte[17], new byte[19], Enumerable.Repeat((byte)255, 18).ToArray() })
{
    try { MeasurementStatus.Decode(invalid); Check(false); }
    catch (InvalidDataException) { Check(true); }
}
try { Measurements.Temperature(new byte[] { 0, 0x80 }); Check(false); }
catch (InvalidDataException) { Check(true); }
gnssBytes[1] = 4;
try { GnssMeasurement.Decode(gnssBytes); Check(false); }
catch (InvalidDataException) { Check(true); }
await HistoryTests.RunAsync();
Console.WriteLine($"{checks} protocol checks passed: text framing, replies, typed values, schema, bounds and absent GNSS.");
