using Windows.Devices.Bluetooth;
using Windows.Devices.Bluetooth.Advertisement;
using Windows.Devices.Bluetooth.GenericAttributeProfile;
using Windows.Storage.Streams;
using Windows.Devices.Enumeration;
using System.IO;

namespace TrackNode.DevTools;

public sealed record NearbyDevice(ulong Address, string Name, short Rssi)
{
    public string AddressText => string.Join(":", Enumerable.Range(0, 6).Reverse().Select(i => ((Address >> (8 * i)) & 255).ToString("X2")));
    public string Description => $"{Name}  ·  {Rssi} dBm\n{AddressText}";
}

public sealed class BleTransport : IDisposable
{
    public static readonly Guid ServiceUuid = new("e0910001-7e83-4c9c-a89e-bfe03c940001");
    private static readonly Guid RxUuid = new("e0910002-7e83-4c9c-a89e-bfe03c940001");
    private static readonly Guid TxUuid = new("e0910003-7e83-4c9c-a89e-bfe03c940001");
    private BluetoothLEAdvertisementWatcher? watcher;
    private BluetoothLEDevice? device;
    private GattDeviceService? service;
    private GattDeviceService? environmentalService;
    private GattDeviceService? batteryService;
    private readonly System.Collections.Concurrent.ConcurrentDictionary<GattCharacteristic, int> measurements = new();
    private GattSession? session;
    private GattCharacteristic? rx;
    private GattCharacteristic? tx;
    private int generation;
    private int scanGeneration;
    private volatile bool ready;
    public int Generation => Volatile.Read(ref generation);
    public int MaxPayload => Math.Min(128, Math.Max(0, (session?.MaxPduSize ?? 23) - 3));
    public bool Ready => ready;
    public event Action<NearbyDevice>? Discovered;
    public event Action<string>? ScanStopped;
    public event Action<byte[], int>? Received;
    public event Action<Guid, byte[], int>? MeasurementReceived;
    public event Action<string, int>? LinkChanged;

    public void StartScan()
    {
        StopScan();
        var current = ++scanGeneration;
        watcher = new() { ScanningMode = BluetoothLEScanningMode.Active };
        watcher.Received += (_, args) =>
        {
            if (current != scanGeneration) return;
            var name = args.Advertisement.LocalName;
            if (name.StartsWith("TrackNode", StringComparison.OrdinalIgnoreCase) || args.Advertisement.ServiceUuids.Contains(ServiceUuid))
                Discovered?.Invoke(new(args.BluetoothAddress, string.IsNullOrEmpty(name) ? "TrackNode" : name, args.RawSignalStrengthInDBm));
        };
        watcher.Stopped += (_, args) =>
        {
            if (current == scanGeneration) ScanStopped?.Invoke(args.Error.ToString());
        };
        watcher.Start();
    }

    public void StopScan()
    {
        ++scanGeneration;
        watcher?.Stop();
        watcher = null;
    }

    public async Task ConnectAsync(ulong address)
    {
        Disconnect();
        var connecting = Generation;
        try
        {
            device = await BluetoothLEDevice.FromBluetoothAddressAsync(address);
            if (device is null) throw new IOException("Windows could not open the device. Check Bluetooth and permissions.");
            if (!device.DeviceInformation.Pairing.IsPaired)
            {
                LinkChanged?.Invoke("Pairing · hold KEY for 5 seconds to open the 60-second window", connecting);
                var pairing = await device.DeviceInformation.Pairing.PairAsync(DevicePairingProtectionLevel.Encryption);
                if (connecting != Generation) throw new IOException("Connection changed during pairing.");
                if (pairing.Status is not (DevicePairingResultStatus.Paired or DevicePairingResultStatus.AlreadyPaired))
                    throw new IOException($"Pairing failed: {pairing.Status}. Hold KEY for 5 seconds and reconnect within 60 seconds.");
            }
            var services = await device.GetGattServicesForUuidAsync(ServiceUuid, BluetoothCacheMode.Uncached);
            if (services.Status != GattCommunicationStatus.Success || services.Services.Count == 0)
                throw new IOException($"TrackNode service discovery: {services.Status}");
            service = services.Services[0];
            foreach (var unused in services.Services.Skip(1)) unused.Dispose();
            session = await GattSession.FromDeviceIdAsync(device.BluetoothDeviceId);
            if (session is null) throw new IOException("Windows could not create a GATT session.");
            session.MaintainConnection = true;
            var characteristics = await service.GetCharacteristicsAsync(BluetoothCacheMode.Uncached);
            if (characteristics.Status != GattCommunicationStatus.Success)
                throw new IOException($"Characteristic discovery: {characteristics.Status}");
            rx = characteristics.Characteristics.SingleOrDefault(c => c.Uuid == RxUuid);
            tx = characteristics.Characteristics.SingleOrDefault(c => c.Uuid == TxUuid);
            if (rx is null || tx is null || !rx.CharacteristicProperties.HasFlag(GattCharacteristicProperties.Write) ||
                !tx.CharacteristicProperties.HasFlag(GattCharacteristicProperties.Notify))
                throw new IOException("Required RX Write / TX Notify characteristics are missing.");
            tx.ValueChanged += OnReceived;
            device.ConnectionStatusChanged += OnConnectionChanged;
            session.MaxPduSizeChanged += OnMtuChanged;
            var subscribed = await tx.WriteClientCharacteristicConfigurationDescriptorAsync(GattClientCharacteristicConfigurationDescriptorValue.Notify);
            if (subscribed != GattCommunicationStatus.Success) throw new IOException($"Subscribe failed: {subscribed}");
            var environmental = await device.GetGattServicesForUuidAsync(Measurements.ServiceUuid, BluetoothCacheMode.Uncached);
            if (environmental.Status != GattCommunicationStatus.Success || environmental.Services.Count == 0)
                throw new IOException("Environmental Sensing service is missing. Update both firmware images.");
            environmentalService = environmental.Services[0];
            foreach (var unused in environmental.Services.Skip(1)) unused.Dispose();
            var sensorCharacteristics = await environmentalService.GetCharacteristicsAsync(BluetoothCacheMode.Uncached);
            if (sensorCharacteristics.Status != GattCommunicationStatus.Success)
                throw new IOException($"Sensor discovery: {sensorCharacteristics.Status}");
            var all = characteristics.Characteristics.Concat(sensorCharacteristics.Characteristics).ToList();
            foreach (var uuid in new[] { Measurements.StatusUuid, Measurements.TemperatureUuid, Measurements.PressureUuid, Measurements.GnssUuid })
            {
                var characteristic = all.SingleOrDefault(c => c.Uuid == uuid);
                if (characteristic is null || !characteristic.CharacteristicProperties.HasFlag(GattCharacteristicProperties.Read) ||
                    !characteristic.CharacteristicProperties.HasFlag(GattCharacteristicProperties.Notify))
                    throw new IOException($"Required Read / Notify characteristic {uuid} is missing.");
                measurements.TryAdd(characteristic, connecting);
                characteristic.ValueChanged += OnMeasurement;
                var result = await characteristic.WriteClientCharacteristicConfigurationDescriptorAsync(GattClientCharacteristicConfigurationDescriptorValue.Notify);
                if (result != GattCommunicationStatus.Success) throw new IOException($"Measurement subscription: {result}");
            }
            var batteryServices = await device.GetGattServicesForUuidAsync(BatteryCharacteristics.ServiceUuid, BluetoothCacheMode.Uncached);
            if (connecting != Generation) throw new IOException("Connection changed during battery discovery.");
            if (batteryServices.Status == GattCommunicationStatus.Success && batteryServices.Services.Count > 0)
            {
                batteryService = batteryServices.Services[0];
                foreach (var unused in batteryServices.Services.Skip(1)) unused.Dispose();
                var batteryCharacteristics = await batteryService.GetCharacteristicsAsync(BluetoothCacheMode.Uncached);
                if (connecting != Generation) throw new IOException("Connection changed during battery discovery.");
                if (batteryCharacteristics.Status != GattCommunicationStatus.Success)
                    throw new IOException($"Battery discovery: {batteryCharacteristics.Status}");
                all.AddRange(batteryCharacteristics.Characteristics);
            }
            foreach (var uuid in new[] { BatteryCharacteristics.LevelUuid, BatteryCharacteristics.StatusUuid })
            {
                var characteristic = all.SingleOrDefault(c => c.Uuid == uuid);
                if (characteristic is null) continue;
                if (!characteristic.CharacteristicProperties.HasFlag(GattCharacteristicProperties.Read) ||
                    !characteristic.CharacteristicProperties.HasFlag(GattCharacteristicProperties.Notify))
                    throw new IOException($"Battery Read / Notify characteristic {uuid} is malformed.");
                measurements.TryAdd(characteristic, connecting);
                characteristic.ValueChanged += OnMeasurement;
                var result = await characteristic.WriteClientCharacteristicConfigurationDescriptorAsync(GattClientCharacteristicConfigurationDescriptorValue.Notify);
                if (result != GattCommunicationStatus.Success) throw new IOException($"Battery subscription: {result}");
            }
            foreach (var characteristic in measurements.Keys)
            {
                var result = await characteristic.ReadValueAsync(BluetoothCacheMode.Uncached);
                if (connecting != Generation) throw new IOException("Connection changed during measurement discovery.");
                if (result.Status == GattCommunicationStatus.Success)
                    MeasurementReceived?.Invoke(characteristic.Uuid, ReadBytes(result.Value), connecting);
                else if (!IsUnavailableSensorReading(result, characteristic.Uuid))
                    throw new IOException($"Measurement read: {result.Status}, ATT error {result.ProtocolError}");
            }
            var connectionChangedDuringSubscription = connecting != Generation || device is null ||
                device.ConnectionStatus != BluetoothConnectionStatus.Connected;
            if (connectionChangedDuringSubscription)
                throw new IOException("Device disconnected during subscription.");
            ready = true;
            LinkChanged?.Invoke($"Subscribed · MTU {session.MaxPduSize} · payload {MaxPayload} B", generation);
        }
        catch
        {
            Disconnect();
            throw;
        }
    }

    public async Task WriteAsync(byte[] data)
    {
        var current = generation;
        var characteristic = rx;
        if (!Ready || characteristic is null) throw new IOException("Connect and subscribe first.");
        using var writer = new DataWriter();
        writer.WriteBytes(data);
        var result = await characteristic.WriteValueWithResultAsync(writer.DetachBuffer(), GattWriteOption.WriteWithResponse);
        var connectionChangedWhileSending = current != generation || !Ready;
        if (connectionChangedWhileSending) throw new IOException("Connection changed while sending.");
        if (result.Status != GattCommunicationStatus.Success)
            throw new IOException($"GATT Write failed: {result.Status}, ATT error {result.ProtocolError?.ToString() ?? "none"}. Command outcome may be unknown.");
    }

    private void OnReceived(GattCharacteristic sender, GattValueChangedEventArgs args)
    {
        var current = Generation;
        if (sender != tx) return;
        using var reader = DataReader.FromBuffer(args.CharacteristicValue);
        var bytes = new byte[reader.UnconsumedBufferLength];
        reader.ReadBytes(bytes);
        Received?.Invoke(bytes, current);
    }

    private static byte[] ReadBytes(IBuffer buffer)
    {
        using var reader = DataReader.FromBuffer(buffer);
        var bytes = new byte[reader.UnconsumedBufferLength];
        reader.ReadBytes(bytes);
        return bytes;
    }

    private static bool IsUnavailableSensorReading(GattReadResult result, Guid characteristic)
    {
        var sensorCharacteristic = characteristic == Measurements.TemperatureUuid ||
            characteristic == Measurements.PressureUuid || characteristic == BatteryCharacteristics.LevelUuid;
        return result.Status == GattCommunicationStatus.ProtocolError && result.ProtocolError == 0x0e &&
            sensorCharacteristic;
    }

    private void OnMeasurement(GattCharacteristic sender, GattValueChangedEventArgs args)
    {
        var measurementFromCurrentConnection = measurements.TryGetValue(sender, out var current) && current == Generation;
        if (measurementFromCurrentConnection)
            MeasurementReceived?.Invoke(sender.Uuid, ReadBytes(args.CharacteristicValue), current);
    }

    private void OnConnectionChanged(BluetoothLEDevice sender, object args)
    {
        var current = Generation;
        if (sender != device) return;
        if (sender.ConnectionStatus == BluetoothConnectionStatus.Disconnected)
        {
            ready = false;
            if (session is not null) session.MaintainConnection = false;
            LinkChanged?.Invoke("Disconnected by device", current);
        }
    }

    private void OnMtuChanged(GattSession sender, object args)
    {
        var mtuFromCurrentSession = sender == session && Ready;
        if (mtuFromCurrentSession) LinkChanged?.Invoke($"Subscribed · MTU {sender.MaxPduSize} · payload {MaxPayload} B", generation);
    }

    public void Disconnect()
    {
        ready = false;
        Interlocked.Increment(ref generation);
        if (tx is not null) tx.ValueChanged -= OnReceived;
        foreach (var characteristic in measurements.Keys) characteristic.ValueChanged -= OnMeasurement;
        measurements.Clear();
        if (device is not null) device.ConnectionStatusChanged -= OnConnectionChanged;
        if (session is not null)
        {
            session.MaxPduSizeChanged -= OnMtuChanged;
            session.MaintainConnection = false;
            session.Dispose();
        }
        service?.Dispose();
        environmentalService?.Dispose();
        batteryService?.Dispose();
        device?.Dispose();
        rx = null;
        tx = null;
        session = null;
        service = null;
        environmentalService = null;
        batteryService = null;
        device = null;
    }

    public void Dispose()
    {
        StopScan();
        Disconnect();
    }
}
