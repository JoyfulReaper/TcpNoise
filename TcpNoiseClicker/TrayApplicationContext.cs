using NATS.Client.Core;
using NATS.Net;
using System.Media;
using System.Text.Json;
using System.Threading.Channels;

namespace TcpNoiseClicker;

internal sealed class TrayApplicationContext : ApplicationContext
{
    private const string NatsUrl = "nats://10.99.0.1:4222";
    private const string Subject = "tcpnoise.connection";

    private readonly CancellationTokenSource _cancellation = new();
    private readonly NotifyIcon _notifyIcon;
    private readonly ToolStripMenuItem _muteMenuItem;
    private readonly Task _listenerTask;

    private readonly SoundPlayer _clickPlayer;
    private readonly SoundPlayer? _newPlayer;
    private readonly SoundPlayer? _ipv6Player;
    private readonly SoundPlayer? _bongPlayer;

    private readonly Dictionary<ushort, SoundPlayer> _portSounds = new();

    private volatile bool _muted;

    private static readonly JsonSerializerOptions JsonOptions = new()
    {
        PropertyNameCaseInsensitive = true
    };

    private readonly object _statsLock = new();
    private readonly System.Windows.Forms.Timer _tooltipTimer;

    private DateOnly _statsDate = DateOnly.FromDateTime(DateTime.Now);
    private int _clicksToday;
    private string? _lastRemoteAddress;
    private ushort _lastListenPort;

    private const int SoundQueueCapacity = 32;
    private readonly Channel<TcpNoisePayload?> _soundQueue;
    private readonly Task _soundTask;

    private const int HistoryCapacity = 50;

    private readonly List<HistoryEntry> _history = [];
    private readonly object _historyLock = new();

    private HistoryForm? _historyForm;

    public TrayApplicationContext()
    {
        _clickPlayer = LoadRequiredSound("click.wav");

        _newPlayer = TryLoadSound("new.wav");
        _ipv6Player = TryLoadSound("ipv6.wav");
        _bongPlayer = TryLoadSound("bong.wav");

        var historyMenuItem = new ToolStripMenuItem("Show history");
        historyMenuItem.Click += ShowHistory;

        _muteMenuItem = new ToolStripMenuItem("Mute");
        _muteMenuItem.Click += ToggleMute;

        var exitMenuItem = new ToolStripMenuItem("Exit");
        exitMenuItem.Click += Exit;

        var menu = new ContextMenuStrip();
        menu.Items.Add(historyMenuItem);
        menu.Items.Add(_muteMenuItem);
        menu.Items.Add(new ToolStripSeparator());
        menu.Items.Add(exitMenuItem);

        _notifyIcon = new NotifyIcon
        {
            Icon = SystemIcons.Information,
            Text = "tcpnoise clicker",
            ContextMenuStrip = menu,
            Visible = true
        };

        _tooltipTimer = new System.Windows.Forms.Timer
        {
            Interval = 500
        };

        _tooltipTimer.Tick += (_, _) => RefreshTooltip();
        _tooltipTimer.Start();

        _soundQueue = Channel.CreateBounded<TcpNoisePayload?>(
            new BoundedChannelOptions(SoundQueueCapacity)
            {
                SingleReader = true,
                SingleWriter = true,
                FullMode = BoundedChannelFullMode.Wait
            });

        _soundTask = Task.Run(
            () => PlaySoundsAsync(_cancellation.Token));

        _listenerTask = Task.Run(
            () => ListenAsync(_cancellation.Token));
    }

    private void ShowHistory(object? sender, EventArgs e)
    {
        if (_historyForm is { IsDisposed: false })
        {
            _historyForm.RefreshHistory(GetHistorySnapshot());
            _historyForm.Show();
            _historyForm.Activate();
            return;
        }

        _historyForm = new HistoryForm(GetHistorySnapshot());
        _historyForm.Show();
    }

    private async Task ListenAsync(CancellationToken cancellationToken)
    {
        var options = NatsOpts.Default with
        {
            Url = NatsUrl,
            Name = "tcpnoise-clicker",
            RetryOnInitialConnect = true
        };

        await using var nats = new NatsClient(options);

        try
        {
            await foreach (var message in nats.SubscribeAsync<string>(
                Subject,
                cancellationToken: cancellationToken))
            {
                TcpNoiseEventEnvelope? connectionEvent = null;

                try
                {
                    connectionEvent =
                        JsonSerializer.Deserialize<TcpNoiseEventEnvelope>(message.Data, JsonOptions);
                }
                catch (JsonException)
                {
                    // Keep making noise even if a future event schema
                    // fails to deserialize.
                }

                if (connectionEvent is not null)
                {
                    RecordEvent(connectionEvent.Payload);
                }

                if (!_muted)
                {
                    _soundQueue.Writer.TryWrite(connectionEvent?.Payload);
                }
            }
        }
        catch (OperationCanceledException)
            when (cancellationToken.IsCancellationRequested)
        {
        }
    }

    private async Task PlaySoundsAsync(CancellationToken cancellationToken)
    {
        try
        {
            await foreach (var connection in _soundQueue.Reader.ReadAllAsync(cancellationToken))
            {
                PlaySound(connection);
            }
        }
        catch (OperationCanceledException)
            when (cancellationToken.IsCancellationRequested)
        {
        }
    }

    private IReadOnlyList<HistoryEntry> GetHistorySnapshot()
    {
        lock (_historyLock)
        {
            return _history.ToList();
        }
    }

    private void RecordEvent(TcpNoisePayload connection)
    {
        lock (_historyLock)
        {
            lock (_historyLock)
            {
                _history.Insert(
                    0,
                    new HistoryEntry(DateTime.Now, connection));

                if (_history.Count > HistoryCapacity)
                {
                    _history.RemoveAt(_history.Count - 1);
                }
            }

            if (_historyForm is { IsDisposed: false })
            {
                _historyForm.BeginInvoke(() =>
                {
                    if (!_historyForm.IsDisposed)
                    {
                        _historyForm.RefreshHistory(
                            GetHistorySnapshot());
                    }
                });
            }
        }

        lock (_statsLock)
        {
            var today = DateOnly.FromDateTime(DateTime.Now);

            if (_statsDate != today)
            {
                _statsDate = today;
                _clicksToday = 0;
            }

            _clicksToday++;
            _lastRemoteAddress = connection.RemoteAddress;
            _lastListenPort = connection.ListenPort;
        }
    }

    private void RefreshTooltip()
    {
        string text;

        lock (_statsLock)
        {
            if (_lastRemoteAddress is null)
            {
                text = $"{_clicksToday} clicks today";
            }
            else
            {
                var address = TruncateAddress(
                    _lastRemoteAddress,
                    25);

                text =
                    $"{_clicksToday} today | " +
                    $"{address} → {_lastListenPort}";
            }
        }

        // NotifyIcon.Text has historically had a rather stupidly small limit.
        if (text.Length > 63)
        {
            text = text[..63];
        }

        _notifyIcon.Text = text;
    }

    private static string TruncateAddress(
        string value,
        int maxLength)
    {
        if (value.Length <= maxLength)
        {
            return value;
        }

        const string ellipsis = "…";

        var remaining = maxLength - ellipsis.Length;
        var startLength = remaining / 2;
        var endLength = remaining - startLength;

        return value[..startLength]
            + ellipsis
            + value[^endLength..];
    }

    private void PlaySound(TcpNoisePayload? connection)
    {
        var player = GetSound(connection);

        try
        {
            player.PlaySync();
        }
        catch
        {
            SystemSounds.Beep.Play();
        }
    }

    private SoundPlayer GetSound(TcpNoisePayload? connection)
    {
        if (connection is null)
        {
            return _clickPlayer;
        }

        if (connection.ListenPort is 420 or 42069)
        {
            return _bongPlayer ?? _clickPlayer;
        }

        if (connection.IpVersion == 6)
        {
            return _ipv6Player ?? _clickPlayer;
        }

        if (connection.SeenCount == 1)
        {
            return _newPlayer ?? _clickPlayer;
        }

        // Ready for port-specific sounds later.
        if (_portSounds.TryGetValue(
            connection.ListenPort,
            out var portPlayer))
        {
            return portPlayer;
        }

        return _clickPlayer;
    }

    private static SoundPlayer LoadRequiredSound(string fileName)
    {
        var path = Path.Combine(AppContext.BaseDirectory, fileName);
        var player = new SoundPlayer(path);

        try
        {
            player.Load();
            return player;
        }
        catch (Exception ex)
        {
            player.Dispose();

            MessageBox.Show(
                ex.ToString(),
                $"Failed to load {fileName}",
                MessageBoxButtons.OK,
                MessageBoxIcon.Error);

            throw;
        }
    }

    private static SoundPlayer? TryLoadSound(string fileName)
    {
        var path = Path.Combine(AppContext.BaseDirectory, fileName);

        if (!File.Exists(path))
        {
            return null;
        }

        var player = new SoundPlayer(path);

        try
        {
            player.Load();
            return player;
        }
        catch
        {
            player.Dispose();
            return null;
        }
    }

    private void ToggleMute(object? sender, EventArgs e)
    {
        _muted = !_muted;
        _muteMenuItem.Checked = _muted;
    }

    private async void Exit(object? sender, EventArgs e)
    {
        _tooltipTimer.Stop();
        _tooltipTimer.Dispose();

        _notifyIcon.Visible = false;
        _cancellation.Cancel();

        try
        {
            await Task.WhenAll(_listenerTask, _soundTask);
        }
        catch (OperationCanceledException)
        {
        }

        try
        {
            await _listenerTask;
        }
        catch (OperationCanceledException)
        {
        }

        foreach (var player in _portSounds.Values)
        {
            player.Dispose();
        }

        _bongPlayer?.Dispose();
        _ipv6Player?.Dispose();
        _newPlayer?.Dispose();
        _clickPlayer.Dispose();

        _notifyIcon.Dispose();
        _cancellation.Dispose();

        ExitThread();
    }
}