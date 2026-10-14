using System.Collections.ObjectModel;
using System.ComponentModel;
using System.Globalization;
using System.IO;
using System.Text;
using System.Text.RegularExpressions;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Data;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Threading;
using Microsoft.Win32;

namespace TrackNode.DevTools;

public sealed record JournalEntry(DateTimeOffset Timestamp, string Kind, string Text)
{
    public string Time => Timestamp.ToLocalTime().ToString("HH:mm:ss.fff");
    public string Color => Kind switch { "TX" => "#67E8C2", "LOG" => "#F5C77E", "ERROR" => "#FF8A94", "TLM" => "#82B9F2", _ => "#A9B7C9" };
}

public partial class MainWindow : Window, INotifyPropertyChanged
{
    private readonly BleTransport transport = new();
    private readonly LineFramer framer = new();
    private readonly DispatcherTimer timer = new() { Interval = TimeSpan.FromSeconds(1) };
    private readonly List<string> history = [];
    private readonly bool demo;
    private bool busy;
    private bool scanning;
    private bool closed;
    private bool demoConnected;
    private int historyIndex;
    private DateTimeOffset lastSample;
    private DateTimeOffset scanStarted;
    private ReplyTracker? replyTracker;
    private TaskCompletionSource<IReadOnlyList<string>>? pendingReply;
    private int demoTick;
    private double? typedTemperature;
    private double? typedPressure;
    private MeasurementStatus? measurementStatus;
    private DateTimeOffset statusReceived;
    private BatteryMeasurement? batteryStatus;
    private DateTimeOffset batteryReceived;
    private byte? batteryLevel;
    private DateTimeOffset batteryLevelReceived;
    private SavedHistory? chartHistory;
    private int chartLimit;
    private string? chartPath;
    private readonly BatteryChartData batteryChart = new();
    private TimeSpan? batteryRange = TimeSpan.FromMinutes(15);
    private ulong? batteryDevice;
    public string BatteryChartDetails { get; private set; } = "No battery measurements yet · hover to inspect · export before closing";
    private static string HistoryDirectory => Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "TrackNode", "History");
    public string ChartDetails { get; private set; } = "No saved history. Connect to the node and sync, or open a previous export.";
    public ObservableCollection<NearbyDevice> Devices { get; } = [];
    public ObservableCollection<JournalEntry> Journal { get; } = [];
    public ICollectionView JournalView { get; }
    public string ConnectionLabel { get; private set; } = "Disconnected";
    public string LinkDetails { get; private set; } = "Connect to begin";
    public string ScanDetails { get; private set; } = "Scan for advertising TrackNode devices.";
    public string ScanButtonLabel => scanning ? "Stop scanning" : "Scan devices";
    public string LogStreamLabel { get; private set; } = "Firmware logs: off";
    public string ModeLabel => demo ? "DEMO · simulated data" : "LIVE · Windows Bluetooth";
    public string Temperature { get; private set; } = "—";
    public string Pressure { get; private set; } = "—";
    public string Voltage { get; private set; } = "—";
    public string Battery { get; private set; } = "—";
    public string BatteryDetails { get; private set; } = "Awaiting battery data";
    public string GnssStatus { get; private set; } = "—";
    public string GnssDetails { get; private set; } = "Awaiting data";
    public string SampleAge => !demo && measurementStatus is not null ?
        $"Sample {measurementStatus.Sequence} · age {(measurementStatus.AgeMs / 1000.0 + Math.Max(0, (DateTimeOffset.UtcNow - statusReceived).TotalSeconds)):F1} s" +
        (!measurementStatus.SensorValid ? " · unavailable/stale" : "") + (!Connected ? " · disconnected" : "") :
        lastSample == default ? "No telemetry received" :
        $"Telemetry {Math.Max(0, (int)(DateTimeOffset.UtcNow - lastSample).TotalSeconds)} s ago" + (!Connected ? " · disconnected" : "");
    public string Footer { get; private set; } = "Commands are never retried automatically. Journal retains the latest 2,000 events. ↑/↓ recalls commands.";
    public bool ShowTelemetry { get; set; }
    public bool ShowFirmware { get; set; } = true;
    private bool Connected => demo ? demoConnected : transport.Ready;
    public bool CanScan => !busy && !Connected && !demo;
    public bool CanConnect => !busy && !Connected && DeviceList?.SelectedItem is NearbyDevice && !demo;
    public bool CanDisconnect => !busy && Connected;
    public bool CanOperate => !busy && Connected;
    public bool CanSyncHistory => CanOperate && !demo;
    public event PropertyChangedEventHandler? PropertyChanged;

    public MainWindow()
    {
        demo = Environment.GetCommandLineArgs().Contains("--demo") || Environment.GetCommandLineArgs().Contains("--snapshot");
        JournalView = CollectionViewSource.GetDefaultView(Journal);
        JournalView.Filter = item => item is JournalEntry entry && (entry.Kind != "TLM" || ShowTelemetry) && (entry.Kind != "LOG" || ShowFirmware);
        InitializeComponent();
        DataContext = this;
        transport.Discovered += found => OnUi(() =>
        {
            if (!scanning) return;
            var old = Devices.FirstOrDefault(item => item.Address == found.Address);
            if (old is not null)
            {
                var selected = Equals(DeviceList.SelectedItem, old);
                var index = Devices.IndexOf(old);
                Devices[index] = found;
                if (selected) DeviceList.SelectedItem = found;
            }
            else Devices.Add(found);
            ScanDetails = $"{Devices.Count} node(s) found. Select one to connect.";
            Refresh();
        });
        transport.ScanStopped += reason => OnUi(() =>
        {
            scanning = false;
            ScanDetails = $"Scan stopped: {reason}";
            Add("SYSTEM", ScanDetails);
            Refresh();
        });
        transport.Received += (data, generation) => OnUi(() =>
        {
            if (generation != transport.Generation) return;
            try
            {
                foreach (var line in framer.Feed(data, DateTimeOffset.UtcNow)) ReceiveLine(line);
            }
            catch (InvalidDataException error)
            {
                Add("ERROR", error.Message);
                Disconnect();
            }
        });
        transport.LinkChanged += (details, generation) => OnUi(() =>
        {
            if (generation != transport.Generation) return;
            if (!transport.Ready)
            {
                Add("SYSTEM", details);
                Disconnect();
                return;
            }
            LinkDetails = details;
            Refresh();
        });
        transport.MeasurementReceived += (uuid, bytes, generation) => OnUi(() =>
        {
            if (generation != transport.Generation) return;
            try { ReceiveMeasurement(uuid, bytes); }
            catch (InvalidDataException error)
            {
                Add("ERROR", error.Message);
                Disconnect();
            }
        });
        timer.Tick += TimerTick;
    }

    private void OnUi(Action action)
    {
        if (!closed) Dispatcher.BeginInvoke(() => { if (!closed) action(); });
    }

    private void MinimizeClicked(object sender, RoutedEventArgs args) => SystemCommands.MinimizeWindow(this);

    private void MaximizeClicked(object sender, RoutedEventArgs args)
    {
        if (WindowState == WindowState.Maximized) SystemCommands.RestoreWindow(this);
        else SystemCommands.MaximizeWindow(this);
    }

    private void CloseClicked(object sender, RoutedEventArgs args) => SystemCommands.CloseWindow(this);

    protected override void OnStateChanged(EventArgs args)
    {
        base.OnStateChanged(args);
        var label = WindowState == WindowState.Maximized ? "Restore" : "Maximize";
        MaximizeButton.ToolTip = label;
        System.Windows.Automation.AutomationProperties.SetName(MaximizeButton, label);
    }

    protected override void OnSourceInitialized(EventArgs args)
    {
        base.OnSourceInitialized(args);
        WindowTheme.Apply(this);
    }

    private void Refresh() => PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(null));

    private void Add(string kind, string text)
    {
        var entry = new JournalEntry(DateTimeOffset.UtcNow, kind, text);
        Journal.Add(entry);
        while (Journal.Count > 2000) Journal.RemoveAt(0);
        if (FollowLog?.IsChecked == true && JournalView.Contains(entry)) JournalList.ScrollIntoView(entry);
    }

    private void ReceiveLine(string line)
    {
        if (string.IsNullOrEmpty(line)) return;
        if (line.StartsWith("T,", StringComparison.Ordinal))
        {
            Add("TLM", line);
            if (Sample.TryParse(line, out var sample) && demo)
            {
                Temperature = sample!.Temperature.ToString("F2", CultureInfo.InvariantCulture);
                Pressure = (sample.Pressure / 100).ToString("F1", CultureInfo.InvariantCulture);
                GnssStatus = sample.PositionValid ? "Fix" : "No fix";
                GnssDetails = sample.PositionValid
                    ? FormattableString.Invariant($"{sample.Latitude:F5}, {sample.Longitude:F5} · age {sample.GnssAgeMs / 1000.0:F1} s")
                    : sample.GnssSequence is null ? "No GNSS snapshot" : $"seq {sample.GnssSequence} · age {sample.GnssAgeMs / 1000} s";
                lastSample = DateTimeOffset.UtcNow;
            }
            else if (!Sample.TryParse(line, out _)) Add("ERROR", "Malformed telemetry: " + line);
        }
        else if (line.StartsWith("L,", StringComparison.Ordinal))
        {
            var fields = line.Split(',', 3);
            if (fields.Length == 3 && ulong.TryParse(fields[1], out _)) Add("LOG", $"[{fields[1]} ms] {fields[2]}");
            else Add("ERROR", "Malformed firmware log: " + line);
        }
        else
        {
            Add(line.StartsWith("ERR ", StringComparison.Ordinal) ? "ERROR" : "RX", line);
            UpdateReadout(line);
            if (replyTracker?.Accept(line) == true) pendingReply?.TrySetResult(replyTracker.Lines.ToArray());
        }
        Refresh();
    }

    private void ReceiveMeasurement(Guid uuid, byte[] bytes)
    {
        if (uuid == BatteryCharacteristics.StatusUuid)
        {
            batteryStatus = BatteryMeasurement.Decode(bytes);
            batteryReceived = DateTimeOffset.UtcNow;
            if (batteryChart.Add(batteryStatus, batteryReceived)) UpdateBatteryChart();
            UpdateBatteryReadout();
            Add("TLM", batteryStatus.Valid
                ? $"Battery {batteryStatus.BatteryMv} mV (filtered) · ADC {batteryStatus.MinimumRaw}–{batteryStatus.MaximumRaw} · age {batteryStatus.AgeMs} ms"
                : $"Battery unavailable · age {batteryStatus.AgeMs} ms");
        }
        else if (uuid == BatteryCharacteristics.LevelUuid)
        {
            batteryLevel = BatteryMeasurement.DecodeLevel(bytes);
            batteryLevelReceived = DateTimeOffset.UtcNow;
            UpdateBatteryReadout();
        }
        else if (uuid == Measurements.TemperatureUuid) typedTemperature = Measurements.Temperature(bytes);
        else if (uuid == Measurements.PressureUuid) typedPressure = Measurements.Pressure(bytes);
        else if (uuid == Measurements.StatusUuid)
        {
            measurementStatus = MeasurementStatus.Decode(bytes);
            statusReceived = DateTimeOffset.UtcNow;
            Add("TLM", $"GATT sample {measurementStatus.Sequence} · age {measurementStatus.AgeMs} ms · flags 0x{measurementStatus.Flags:X2}");
        }
        else if (uuid == Measurements.GnssUuid)
        {
            var gnss = GnssMeasurement.Decode(bytes);
            GnssStatus = !gnss.Present ? "—" : !gnss.Fresh ? "Stale" : gnss.PositionValid ? "Fix" : "No fix";
            GnssDetails = !gnss.Present ? "No GNSS snapshot" : gnss.PositionValid
                ? FormattableString.Invariant($"{gnss.Latitude:F5}, {gnss.Longitude:F5} · age {gnss.AgeMs / 1000.0:F1} s")
                : FormattableString.Invariant($"No position · age {gnss.AgeMs / 1000.0:F1} s");
        }
        Temperature = measurementStatus?.SensorValid == true && typedTemperature.HasValue
            ? typedTemperature.Value.ToString("F2", CultureInfo.InvariantCulture) : "—";
        Pressure = measurementStatus?.SensorValid == true && typedPressure.HasValue
            ? typedPressure.Value.ToString("F1", CultureInfo.InvariantCulture) : "—";
        Refresh();
    }

    private void UpdateBatteryReadout()
    {
        if (batteryStatus is not null)
        {
            var age = batteryStatus.AgeMs / 1000.0 + Math.Max(0, (DateTimeOffset.UtcNow - batteryReceived).TotalSeconds);
            if (!batteryStatus.Valid || age >= 15)
            {
                Battery = "—";
                BatteryDetails = "Unavailable / stale";
                return;
            }
            Battery = (batteryStatus.BatteryMv / 1000.0).ToString("F3", CultureInfo.InvariantCulture) + " V";
            Voltage = (batteryStatus.SupplyMv / 1000.0).ToString("F3", CultureInfo.InvariantCulture);
            BatteryDetails = $"≈{batteryStatus.Level}% · {(batteryStatus.UsbPresent ? "USB present" : "Battery power")}\nADC {batteryStatus.MinimumRaw}–{batteryStatus.MaximumRaw} · {age:F1} s";
        }
        else if (batteryLevel.HasValue)
        {
            var stale = DateTimeOffset.UtcNow - batteryLevelReceived >= TimeSpan.FromSeconds(15);
            Battery = stale ? "—" : $"≈{batteryLevel}%";
            BatteryDetails = stale ? "Unavailable / stale" : "Battery Level · voltage unavailable";
        }
    }

    private void UpdateReadout(string line)
    {
        if (demo && line.StartsWith("GNSS seq=", StringComparison.Ordinal))
        {
            GnssStatus = Regex.IsMatch(line, @"(?:^| )pos=1(?: |$)") ? "Fix" : "No fix";
            GnssDetails = line[5..];
        }
        var voltage = Regex.Match(line, @"^supply_mv=(\d+)");
        if (voltage.Success && double.TryParse(voltage.Groups[1].Value, out var mv)) Voltage = (mv / 1000).ToString("F3", CultureInfo.InvariantCulture);
        var battery = Regex.Match(line, @"^battery_mv=(\d+) level_percent_estimate=(\d+) usb_present=([01]) low_battery=([01]) age=(\d+) ms$");
        if (battery.Success && ushort.TryParse(battery.Groups[1].Value, out var batteryMv) &&
            byte.TryParse(battery.Groups[2].Value, out var level) && level <= 100)
        {
            Battery = (batteryMv / 1000.0).ToString("F3", CultureInfo.InvariantCulture) + " V";
            BatteryDetails = $"≈{level}% · {(battery.Groups[3].Value == "1" ? "USB present" : "Battery power")} · command";
        }
        if (line == "ERR BATTERY_UNAVAILABLE")
        {
            Battery = "—";
            BatteryDetails = "Battery unavailable";
        }
        var temperature = Regex.Match(line, @"^Temperature = (-?[\d.]+)");
        if (temperature.Success && demo) Temperature = temperature.Groups[1].Value;
        var pressure = Regex.Match(line, @"^Pressure = ([\d.]+)");
        if (pressure.Success && demo && double.TryParse(pressure.Groups[1].Value, NumberStyles.Float, CultureInfo.InvariantCulture, out var pa)) Pressure = (pa / 100).ToString("F1", CultureInfo.InvariantCulture);
        if (line == "OK LOG_STREAM ON") LogStreamLabel = "Firmware logs: streaming";
        if (line == "OK LOG_STREAM OFF") LogStreamLabel = "Firmware logs: off";
    }

    private async Task<IReadOnlyList<string>> SendCommandAsync(string command)
    {
        var bytes = CommandEncoding.Encode(command, demo ? 128 : transport.MaxPayload);
        Add("TX", command);
        if (!history.Contains(command)) history.Add(command);
        if (history.Count > 100) history.RemoveAt(0);
        historyIndex = history.Count;
        if (demo)
        {
            var response = command switch
            {
                "PING" => "PONG",
                "GET TEMP" => "Temperature = 23.42 C (age=180 ms, seq=12)",
                "GET PRESSURE" => "Pressure = 100812.00 Pa (age=180 ms, seq=12)",
                "GET VOLTAGE" => "supply_mv=3298 low_voltage=0 vrefint_raw=1502 age=100 ms",
                "GET BATTERY" => "battery_mv=3981 level_percent_estimate=72 usb_present=0 low_battery=0 age=100 ms\nsupply_mv=3298 battery_raw=2471 vrefint_raw=1502 min_raw=2468 max_raw=2475",
                "SET LOG_STREAM ON" => "OK LOG_STREAM ON",
                "SET LOG_STREAM OFF" => "OK LOG_STREAM OFF",
                _ => "ERR DEMO_COMMAND_NOT_SIMULATED"
            };
            var lines = response.Split('\n');
            foreach (var line in lines) ReceiveLine(line);
            return lines;
        }
        replyTracker = new(command);
        pendingReply = new(TaskCreationOptions.RunContinuationsAsynchronously);
        try
        {
            await transport.WriteAsync(bytes);
            return await pendingReply.Task.WaitAsync(TimeSpan.FromSeconds(8));
        }
        catch (Exception error) when (error is not ArgumentException)
        {
            Add("ERROR", $"{error.Message} Result may be unknown; reconnect before another command.");
            Disconnect();
            throw;
        }
        finally
        {
            pendingReply = null;
            replyTracker = null;
        }
    }

    private async Task ExecuteAsync(string command)
    {
        if (!CanOperate) return;
        busy = true;
        Refresh();
        try { await SendCommandAsync(command.Trim()); }
        catch (Exception error) { Add("ERROR", error.Message); }
        finally { busy = false; Refresh(); }
    }

    private void ScanClicked(object sender, RoutedEventArgs args)
    {
        try
        {
            if (scanning) StopScan();
            else
            {
                Devices.Clear();
                scanning = true;
                scanStarted = DateTimeOffset.UtcNow;
                ScanDetails = "Scanning for 15 seconds…";
                transport.StartScan();
                Add("SYSTEM", "BLE scan started");
            }
        }
        catch (Exception error) { StopScan(); Add("ERROR", error.Message); }
        Refresh();
    }

    private void StopScan()
    {
        transport.StopScan();
        scanning = false;
        ScanDetails = Devices.Count == 0 ? "No TrackNode found. Check device power and Bluetooth." : $"{Devices.Count} node(s) found.";
        Refresh();
    }

    private void DeviceSelected(object sender, SelectionChangedEventArgs args) => Refresh();

    private async void ConnectClicked(object sender, RoutedEventArgs args)
    {
        if (DeviceList.SelectedItem is not NearbyDevice selected || !CanConnect) return;
        StopScan();
        busy = true;
        if (batteryDevice != selected.Address)
        {
            batteryChart.Clear();
            batteryDevice = selected.Address;
            UpdateBatteryChart();
        }
        ConnectionLabel = "Connecting…";
        LinkDetails = selected.AddressText;
        framer.Reset();
        Temperature = Pressure = Voltage = GnssStatus = "—";
        GnssDetails = "Awaiting data";
        lastSample = default;
        Refresh();
        try
        {
            await transport.ConnectAsync(selected.Address);
            if (closed) { transport.Dispose(); return; }
            ConnectionLabel = selected.Name;
            Add("SYSTEM", "Connected and subscribed: " + selected.AddressText);
            await SendCommandAsync("PING");
            var logs = await SendCommandAsync("SET LOG_STREAM ON");
            if (logs.Any(line => line.StartsWith("ERR ", StringComparison.Ordinal)))
                LogStreamLabel = "Logs unavailable: update STM32 firmware";
        }
        catch (Exception error) { Add("ERROR", error.Message); Disconnect(); }
        finally { busy = false; Refresh(); }
    }

    private void DisconnectClicked(object sender, RoutedEventArgs args) => Disconnect();

    private void Disconnect()
    {
        if (batteryChart.Break(DateTimeOffset.UtcNow)) UpdateBatteryChart();
        typedTemperature = null;
        batteryStatus = null;
        batteryLevel = null;
        Battery = "—";
        BatteryDetails = "Awaiting battery data";
        typedPressure = null;
        measurementStatus = null;
        transport.Disconnect();
        demoConnected = false;
        framer.Reset();
        pendingReply?.TrySetException(new IOException("Device disconnected."));
        ConnectionLabel = "Disconnected";
        LinkDetails = "Reconnect to resume";
        LogStreamLabel = "Firmware logs: off";
        Refresh();
    }

    private async void QuickCommand(object sender, RoutedEventArgs args)
    {
        if (sender is Button { Tag: string command }) await ExecuteAsync(command);
    }

    private async void ApplyPeriod(object sender, RoutedEventArgs args)
    {
        if (!uint.TryParse(PeriodInput.Text, out var period) || period < 100 || period > 60000)
        {
            Add("ERROR", "Sample period must be 100…60000 ms.");
            return;
        }
        await ExecuteAsync($"SET SAMPLE_PERIOD {period}");
    }

    private async void SendClicked(object sender, RoutedEventArgs args) => await ExecuteAsync(CommandInput.Text);

    private async void SyncHistoryClicked(object sender, RoutedEventArgs args)
    {
        if (!CanSyncHistory) return;
        busy = true;
        Refresh();
        try
        {
            var store = new HistoryStore(HistoryDirectory);
            var result = await HistorySync.RunAsync(SendCommandAsync,
                (snapshot, page) => Task.Run(() => store.SavePage(snapshot, page)),
                (count, total) => { Footer = $"History sync: {count}/{total} records saved and acknowledged."; Refresh(); });
            Footer = $"History: {result.Records} saved · {result.Overwritten} overwritten on device since boot.";
            Add("SYSTEM", Footer + (result.Path is null ? "" : " File: " + result.Path));
            if (result.Path is not null) LoadChartHistory(result.Path);
            WorkbenchTabs.SelectedIndex = 1;
        }
        catch (Exception exception)
        {
            Footer = "History sync stopped. Saved pages remain on this PC; unacknowledged data can be synchronized again.";
            Add("ERROR", exception.Message);
        }
        finally { busy = false; Refresh(); }
    }

    private void OpenHistoryClicked(object sender, RoutedEventArgs args)
    {
        var dialog = new OpenFileDialog { Filter = "TrackNode history (*.json)|*.json", InitialDirectory = Directory.Exists(HistoryDirectory) ? HistoryDirectory : "" };
        if (dialog.ShowDialog(this) == true) LoadChartHistory(dialog.FileName);
    }

    private void LoadChartHistory(string path)
    {
        try
        {
            var saved = HistoryChartData.Load(path);
            chartHistory = saved;
            chartPath = path;
            UpdateCharts();
        }
        catch (Exception error)
        {
            Add("ERROR", "Cannot open history: " + error.Message);
            Footer = "Could not open saved history: " + error.Message;
            Refresh();
        }
    }

    private void ChartRangeClicked(object sender, RoutedEventArgs args)
    {
        if (sender is Button { Tag: string value } && int.TryParse(value, out var limit))
        {
            chartLimit = limit;
            UpdateCharts();
        }
    }

    private void UpdateCharts()
    {
        if (chartHistory is null) return;
        var points = HistoryChartData.Select(chartHistory, chartLimit);
        TemperatureChart.SetPoints(points);
        PressureChart.SetPoints(points);
        var period = points.Count == 0 ? "empty" : $"{points.Min(point => point.Time).ToLocalTime():dd MMM HH:mm:ss} – {points.Max(point => point.Time).ToLocalTime():dd MMM HH:mm:ss}";
        var overwritten = chartHistory.Anchors.Count == 0 ? 0 : chartHistory.Anchors.Max(anchor => anchor.Overwritten);
        ChartDetails = $"{points.Count}/{chartHistory.Records.Count} records · {period} · {overwritten} overwritten on device\n{Path.GetFileName(chartPath)}";
        Refresh();
    }

    private async void CommandKeyDown(object sender, KeyEventArgs args)
    {
        if (args.Key == Key.Enter) { args.Handled = true; await ExecuteAsync(CommandInput.Text); }
        else if (args.Key is Key.Up or Key.Down && history.Count > 0)
        {
            historyIndex = Math.Clamp(historyIndex + (args.Key == Key.Up ? -1 : 1), 0, history.Count);
            CommandInput.Text = historyIndex == history.Count ? "" : history[historyIndex];
            CommandInput.CaretIndex = CommandInput.Text.Length;
            args.Handled = true;
        }
    }

    private void FilterChanged(object sender, RoutedEventArgs args) => JournalView?.Refresh();
    private void ClearLog(object sender, RoutedEventArgs args) => Journal.Clear();

    private void UpdateBatteryChart()
    {
        var points = batteryChart.Select(batteryRange);
        BatteryPlot.SetPoints(points);
        var count = batteryChart.Points.Count(point => point.Millivolts.HasValue);
        BatteryChartDetails = $"{count:N0} measurements collected · {(batteryRange is null ? "all" : $"last {batteryRange.Value.TotalMinutes:0} min")} · filtered voltage · gaps are not interpolated · export before closing";
    }

    private void BatteryRangeClicked(object sender, RoutedEventArgs args)
    {
        if (sender is Button { Tag: string tag } && int.TryParse(tag, out var minutes))
        {
            batteryRange = minutes == 0 ? null : TimeSpan.FromMinutes(minutes);
            UpdateBatteryChart();
            Refresh();
        }
    }

    private void ClearBatteryClicked(object sender, RoutedEventArgs args)
    {
        batteryChart.Clear();
        UpdateBatteryChart();
        Refresh();
    }

    private async void ExportBatteryClicked(object sender, RoutedEventArgs args)
    {
        var dialog = new SaveFileDialog { Filter = "Battery measurements (*.csv)|*.csv", FileName = $"tracknode-battery-{DateTime.Now:yyyyMMdd-HHmmss}.csv" };
        if (dialog.ShowDialog(this) != true) return;
        var csv = batteryChart.Export();
        try { await File.WriteAllTextAsync(dialog.FileName, csv, new UTF8Encoding(true)); Add("SYSTEM", "Battery chart exported: " + dialog.FileName); }
        catch (Exception error) { Add("ERROR", error.Message); }
    }

    private async void ExportLog(object sender, RoutedEventArgs args)
    {
        var dialog = new SaveFileDialog { Filter = "Tab-separated journal (*.tsv)|*.tsv", FileName = $"tracknode-{DateTime.Now:yyyyMMdd-HHmmss}.tsv" };
        if (dialog.ShowDialog(this) != true) return;
        var text = "timestamp\tkind\tmessage\n" + string.Join('\n', Journal.Select(entry => $"{entry.Timestamp:O}\t{entry.Kind}\t{entry.Text.Replace('\t', ' ').Replace('\r', ' ').Replace('\n', ' ')}"));
        try { await File.WriteAllTextAsync(dialog.FileName, text, new UTF8Encoding(true)); Add("SYSTEM", "Journal exported: " + dialog.FileName); }
        catch (Exception error) { Add("ERROR", error.Message); }
    }

    private void TimerTick(object? sender, EventArgs args)
    {
        UpdateBatteryReadout();
        if (batteryStatus is not null && DateTimeOffset.UtcNow - batteryReceived >= TimeSpan.FromSeconds(15) &&
            batteryChart.Break(DateTimeOffset.UtcNow)) UpdateBatteryChart();
        if (scanning && DateTimeOffset.UtcNow - scanStarted > TimeSpan.FromSeconds(15)) StopScan();
        if (Connected && framer.IsExpired(DateTimeOffset.UtcNow))
        {
            Add("ERROR", "Partial notification line expired. Reconnect to resynchronize.");
            Disconnect();
        }
        if (demo && demoConnected)
        {
            ++demoTick;
            ReceiveLine($"T,{demoTick * 1000},23.42,100812.00,0,0,0,,,0,,0,");
            if (demoTick % 4 == 0 && LogStreamLabel.EndsWith("streaming", StringComparison.Ordinal))
                ReceiveLine($"L,{demoTick * 1000},GNSS lookup in progress; waiting for a fix");
        }
        Refresh();
    }

    private async void WindowLoaded(object sender, RoutedEventArgs args)
    {
        timer.Start();
        Add("SYSTEM", demo ? "DEMO: no Bluetooth connection; all data below is simulated." : "Workbench ready. Scan and connect to TrackNode.");
        if (demo)
        {
            demoConnected = true;
            ConnectionLabel = "TrackNode · DEMO";
            LinkDetails = "Simulated subscription · MTU 131";
            Devices.Add(new(0xDA7A00001234, "TrackNode / demo", -48));
            DeviceList.SelectedIndex = 0;
            ScanDetails = "Simulated device. No radio operations.";
            ReceiveLine("PONG");
            ReceiveLine("OK LOG_STREAM ON");
            ReceiveLine("supply_mv=3298 low_voltage=0 vrefint_raw=1502 age=100 ms");
            ReceiveLine("battery_mv=3981 level_percent_estimate=72 usb_present=0 low_battery=0 age=100 ms");
            TimerTick(null, EventArgs.Empty);
            ReceiveLine("L,1040,GNSS lookup started");
            Add("SYSTEM", "Replies and firmware logs appear independently of telemetry.");
        }
        if (demo)
        {
            chartHistory = new SavedHistory { DeviceId = "DEMO", Epoch = 1 };
            var start = DateTimeOffset.Now.Date.AddHours(-4);
            for (int index = 0; index < 144; ++index)
            {
                var valid = index is < 65 or > 68;
                var record = new HistoryRecord((ulong)index + 1, "", (uint)index * 300, 0, valid, false,
                    valid ? 21.4 + 1.6 * Math.Sin(index / 22.0) : null,
                    valid ? (uint)(100800 + 170 * Math.Cos(index / 36.0)) : null, null, null);
                chartHistory.Records.Add(record.Id, new(record, new DateTimeOffset(start.AddMinutes(index * 5)), null));
            }
            chartPath = "DEMO · simulated overnight measurements";
            UpdateCharts();
            var batteryStart = DateTimeOffset.UtcNow.AddMinutes(-10);
            for (int index = 0; index < 120; ++index)
            {
                var mv = (ushort)(index < 80 ? 3850 + index * 2 : 4010 - (index - 80) / 2);
                batteryChart.Add(new BatteryMeasurement((byte)(index < 80 ? 3 : 1), (byte)(55 + Math.Min(index, 80) / 3),
                    mv, 3300, 2450, 1500, 2440, 2460, 0), batteryStart.AddSeconds(index * 5));
            }
            UpdateBatteryChart();
        }
        else if (Directory.Exists(HistoryDirectory))
        {
            var latest = Directory.EnumerateFiles(HistoryDirectory, "*.json").OrderByDescending(File.GetLastWriteTimeUtc).FirstOrDefault();
            if (latest is not null) LoadChartHistory(latest);
        }
        Refresh();
        var argv = Environment.GetCommandLineArgs();
        if (argv.Contains("--charts")) WorkbenchTabs.SelectedIndex = 2;
        if (argv.Contains("--battery-chart")) WorkbenchTabs.SelectedIndex = 1;
        var snapshotIndex = Array.IndexOf(argv, "--snapshot");
        if (snapshotIndex >= 0 && snapshotIndex + 1 < argv.Length)
        {
            await Dispatcher.InvokeAsync(() => { }, DispatcherPriority.ApplicationIdle);
            if (argv.Contains("--compact"))
            {
                Width = MinWidth;
                Height = MinHeight;
                await Dispatcher.InvokeAsync(() => { }, DispatcherPriority.ApplicationIdle);
            }
            UpdateLayout();
            var content = (FrameworkElement)Content;
            var bitmap = new RenderTargetBitmap((int)(content.ActualWidth + content.Margin.Left + content.Margin.Right),
                (int)(content.ActualHeight + content.Margin.Top + content.Margin.Bottom), 96, 96, PixelFormats.Pbgra32);
            bitmap.Render(this);
            var encoder = new PngBitmapEncoder();
            encoder.Frames.Add(BitmapFrame.Create(bitmap));
            using (var output = File.Create(argv[snapshotIndex + 1])) encoder.Save(output);
            Close();
        }
    }

    private void WindowClosing(object? sender, CancelEventArgs args)
    {
        closed = true;
        timer.Stop();
        Disconnect();
        transport.Dispose();
    }
}
