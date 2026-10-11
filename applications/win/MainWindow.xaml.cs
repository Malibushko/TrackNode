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
    public string GnssStatus { get; private set; } = "—";
    public string GnssDetails { get; private set; } = "Awaiting data";
    public string SampleAge => lastSample == default ? "No telemetry received" :
        $"Telemetry {Math.Max(0, (int)(DateTimeOffset.UtcNow - lastSample).TotalSeconds)} s ago" + (!Connected ? " · disconnected" : "");
    public string Footer { get; private set; } = "Commands are never retried automatically. Journal retains the latest 2,000 events. ↑/↓ recalls commands.";
    public bool ShowTelemetry { get; set; }
    public bool ShowFirmware { get; set; } = true;
    private bool Connected => demo ? demoConnected : transport.Ready;
    public bool CanScan => !busy && !Connected && !demo;
    public bool CanConnect => !busy && !Connected && DeviceList?.SelectedItem is NearbyDevice && !demo;
    public bool CanDisconnect => !busy && Connected;
    public bool CanOperate => !busy && Connected;
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
            if (Sample.TryParse(line, out var sample))
            {
                Temperature = sample!.Temperature.ToString("F2", CultureInfo.InvariantCulture);
                Pressure = (sample.Pressure / 100).ToString("F1", CultureInfo.InvariantCulture);
                GnssStatus = sample.PositionValid ? "Fix" : "No fix";
                GnssDetails = sample.PositionValid
                    ? FormattableString.Invariant($"{sample.Latitude:F5}, {sample.Longitude:F5} · age {sample.GnssAgeMs / 1000.0:F1} s")
                    : sample.GnssSequence is null ? "No GNSS snapshot" : $"seq {sample.GnssSequence} · age {sample.GnssAgeMs / 1000} s";
                lastSample = DateTimeOffset.UtcNow;
            }
            else Add("ERROR", "Malformed telemetry: " + line);
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

    private void UpdateReadout(string line)
    {
        if (line.StartsWith("GNSS seq=", StringComparison.Ordinal))
        {
            GnssStatus = Regex.IsMatch(line, @"(?:^| )pos=1(?: |$)") ? "Fix" : "No fix";
            GnssDetails = line[5..];
        }
        var voltage = Regex.Match(line, @"^supply_mv=(\d+)");
        if (voltage.Success && double.TryParse(voltage.Groups[1].Value, out var mv)) Voltage = (mv / 1000).ToString("F3", CultureInfo.InvariantCulture);
        var temperature = Regex.Match(line, @"^Temperature = (-?[\d.]+)");
        if (temperature.Success) Temperature = temperature.Groups[1].Value;
        var pressure = Regex.Match(line, @"^Pressure = ([\d.]+)");
        if (pressure.Success && double.TryParse(pressure.Groups[1].Value, NumberStyles.Float, CultureInfo.InvariantCulture, out var pa)) Pressure = (pa / 100).ToString("F1", CultureInfo.InvariantCulture);
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
                "SET LOG_STREAM ON" => "OK LOG_STREAM ON",
                "SET LOG_STREAM OFF" => "OK LOG_STREAM OFF",
                _ => "ERR DEMO_COMMAND_NOT_SIMULATED"
            };
            ReceiveLine(response);
            return [response];
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
            TimerTick(null, EventArgs.Empty);
            ReceiveLine("L,1040,GNSS lookup started");
            Add("SYSTEM", "Replies and firmware logs appear independently of telemetry.");
        }
        Refresh();
        var argv = Environment.GetCommandLineArgs();
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
