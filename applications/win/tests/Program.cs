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
Console.WriteLine($"{checks} protocol checks passed: fragmentation, expiry, bounds, absent GNSS, multi-line replies, MTU and ASCII validation.");
