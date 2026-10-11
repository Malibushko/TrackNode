using System.Text.Json;
using TrackNode.DevTools;

public static class HistoryTests
{
    public static async Task RunAsync()
    {
        const string raw = "01000000000000000200000000000000012EFBA08601000000000000000000";
        string[] metadata = ["H O 2 000000010000000200000003 000000000000002A 00000003 0000000000000001 0000000000000001 00000001 0000000000000000"];
        string[] lines = ["H B 0000000000000000 0000000000000001 00000001", "H D 0000000000000001 " + raw,
            "H E 0000000000000001"];
        void Assert(bool condition)
        {
            if (!condition) throw new Exception("History check failed.");
        }
        var snapshot = HistorySnapshot.Parse(metadata, DateTimeOffset.UtcNow, .1);
        var page = HistoryPage.Parse(lines, 0, snapshot);
        Assert(page.Records[0].Temperature == -12.34 && page.Records[0].PressurePa == 100000);
        const string distantRaw = "2A000000000000000200000000000000012EFBA08601000000000000000000";
        var sparseSnapshot = snapshot with { Last = 42, Count = 2 };
        string[] sparseLines = ["H B 0000000000000000 000000000000002A 00000002", lines[1],
            "H D 000000000000002A " + distantRaw, "H E 000000000000002A"];
        Assert(HistoryPage.Parse(sparseLines, 0, sparseSnapshot).Records.Select(record => record.Id).SequenceEqual(new ulong[] { 1, 42 }));
        var positionRaw = "2A000000000000000200000001000000032EFBA086010000175BCA00D2496B";
        var position = HistoryRecord.Parse(42, positionRaw);
        Assert(position.PositionValid && position.PositionUptimeSeconds == 1 && position.Latitude == -90 && position.Longitude == 180);
        foreach (var operation in new[] { 'O', 'R', 'A' })
            Assert(CommandEncoding.Encode(HistoryWire.Command(operation, ulong.MaxValue), HistoryWire.MinimalPacketSize).Length == HistoryWire.MinimalPacketSize);
        var tracker = new ReplyTracker(HistoryWire.Command('R', 0));
        Assert(!tracker.Accept(lines[0]));
        Assert(!tracker.Accept("L,3,firmware log"));
        Assert(!tracker.Accept(lines[1]));
        Assert(tracker.Accept(lines[2]) && tracker.Lines.Count == 3);
        foreach (var broken in new[] {
            new[] { lines[0], lines[2] },
            new[] { lines[0], "H D 0000000000000002 " + raw, lines[2] },
            new[] { lines[0], lines[1], "H E 0000000000000002" },
            new[] { lines[0], "H D 0000000000000001 01000000000000000200000000000000052EFBA08601000000000000000000", lines[2] } })
        {
            try { HistoryPage.Parse(broken, 0, snapshot); throw new Exception("Accepted invalid page."); }
            catch (InvalidDataException) {}
        }
        var directory = Path.Combine(AppContext.BaseDirectory, "TrackNodeHistoryTest-" + Guid.NewGuid().ToString("N"));
        try
        {
            var store = new HistoryStore(directory);
            var savedBeforeAck = false;
            var commands = new List<string>();
            Task<IReadOnlyList<string>> Send(string command)
            {
                commands.Add(command);
                if (command.StartsWith("H O ")) return Task.FromResult<IReadOnlyList<string>>(metadata);
                if (command.StartsWith("H R ")) return Task.FromResult<IReadOnlyList<string>>(lines);
                Assert(savedBeforeAck && Directory.GetFiles(directory, "*.json").Length == 1);
                return Task.FromResult<IReadOnlyList<string>>(["H A 0000000000000001"]);
            }
            Task<string> Save(HistorySnapshot anchor, HistoryPage batch)
            {
                var path = store.SavePage(anchor, batch);
                savedBeforeAck = true;
                return Task.FromResult(path);
            }
            var first = await HistorySync.RunAsync(Send, Save);
            var second = await HistorySync.RunAsync(Send, Save);
            Assert(first.Records == 1 && second.Records == 1);
            var saved = JsonSerializer.Deserialize<SavedHistory>(File.ReadAllText(first.Path!))!;
            Assert(saved.Records.Count == 1 && saved.Anchors.Count == 2);
            var originalUtc = saved.Records[1].EstimatedUtc;
            store.SavePage(snapshot with { PhoneUtc = snapshot.PhoneUtc.AddHours(1) }, page);
            saved = JsonSerializer.Deserialize<SavedHistory>(File.ReadAllText(first.Path!))!;
            Assert(saved.Records[1].EstimatedUtc == originalUtc);
            var chartHistory = HistoryChartData.Load(first.Path!);
            var chartPoints = HistoryChartData.Select(chartHistory, 0);
            Assert(chartPoints.Count == 1 && chartPoints[0].PressureHpa == 1000 && chartPoints[0].Temperature == -12.34);
            Assert(chartPoints[0].Time == originalUtc && chartPoints[0].Id == 1);
            chartHistory.Records.Add(42, new(position with { EnvironmentValid = false }, originalUtc.AddHours(1), null));
            var lastPoint = HistoryChartData.Select(chartHistory, 1).Single();
            Assert(lastPoint.Id == 42 && lastPoint.Temperature is null && lastPoint.PressureHpa is null);
            Assert(HistoryChartData.Select(chartHistory, 0).Select(point => point.Id).SequenceEqual(new ulong[] { 1, 42 }));
            var invalidPath = Path.Combine(directory, "invalid.json");
            File.WriteAllText(invalidPath, JsonSerializer.Serialize(chartHistory));
            try { HistoryChartData.Load(invalidPath); throw new Exception("Accepted inconsistent stored history."); }
            catch (InvalidDataException) {}
            File.Delete(invalidPath);
            commands.Clear();
            try
            {
                await HistorySync.RunAsync(Send, (_, _) => throw new IOException("Disk unavailable"));
                throw new Exception("Ignored save failure.");
            }
            catch (IOException) { Assert(commands.Count == 2 && commands.All(command => !command.StartsWith("H A "))); }
            commands.Clear();
            async Task<IReadOnlyList<string>> LoseAck(string command)
            {
                if (command.StartsWith("H A ")) throw new IOException("Connection lost before ACK reply");
                return await Send(command);
            }
            try
            {
                await HistorySync.RunAsync(LoseAck, Save);
                throw new Exception("Ignored lost ACK.");
            }
            catch (IOException) {}
            await HistorySync.RunAsync(Send, Save);
            Assert(JsonSerializer.Deserialize<SavedHistory>(File.ReadAllText(first.Path!))!.Records.Count == 1);
            var conflicting = page with { Records = [HistoryRecord.Parse(1, "01000000000000000200000000000000012FFBA08601000000000000000000")] };
            try { store.SavePage(snapshot, conflicting); throw new Exception("Accepted conflicting ID."); }
            catch (InvalidDataException) {}
            Assert(Directory.GetFiles(directory, "*.tmp").Length == 0);
        }
        finally
        {
            if (Directory.Exists(directory))
            {
                foreach (var file in Directory.GetFiles(directory)) File.Delete(file);
                Directory.Delete(directory);
            }
        }
        Console.WriteLine("History checks passed: packed values, MTU20, page validation, durable save before ACK, replay deduplication and disk failure.");
    }
}
