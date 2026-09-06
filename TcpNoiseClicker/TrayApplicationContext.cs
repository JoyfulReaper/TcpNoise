using NATS.Client.Core;
using NATS.Net;
using System.Media;
using System.Text.Json;

namespace TcpNoiseClicker;

internal sealed class TrayApplicationContext : ApplicationContext
{
    private const string NatsUrl = "nats://10.99.0.1:4222";
    private const string Subject = "tcpnoise.connection";

    private readonly CancellationTokenSource _cancellation = new();
    private readonly NotifyIcon _notifyIcon;
    private readonly SoundPlayer _clickPlayer;
    private readonly ToolStripMenuItem _muteMenuItem;
    private readonly Task _listenerTask;

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

    public TrayApplicationContext()
    {
        _clickPlayer = new SoundPlayer(
            Path.Combine(AppContext.BaseDirectory, "click.wav"));

        try
        {
            _clickPlayer.Load();
        }
        catch (Exception ex)
        {
            MessageBox.Show(
                ex.ToString(),
                "Failed to load click.wav",
                MessageBoxButtons.OK,
                MessageBoxIcon.Error);
        }

        _muteMenuItem = new ToolStripMenuItem("Mute");
        _muteMenuItem.Click += ToggleMute;

        var exitMenuItem = new ToolStripMenuItem("Exit");
        exitMenuItem.Click += Exit;

        var menu = new ContextMenuStrip();
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

        _listenerTask = Task.Run(
            () => ListenAsync(_cancellation.Token));
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
                    connectionEvent = JsonSerializer.Deserialize<TcpNoiseEventEnvelope>(
                        message.Data,
                        JsonOptions);
                }
                catch (JsonException)
                {
                    // Keep clicking even if a future event schema fails to deserialize.
                }

                if (connectionEvent is not null)
                {
                    RecordEvent(connectionEvent.Payload);
                }

                if (!_muted)
                {
                    PlayClick();
                }
            }
        }
        catch (OperationCanceledException)
            when (cancellationToken.IsCancellationRequested)
        {
        }
    }

    private void RecordEvent(TcpNoisePayload connection)
    {
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
                var address = Truncate(_lastRemoteAddress, 25);

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

    private static string Truncate(string value, int maxLength)
    {
        if (value.Length <= maxLength)
        {
            return value;
        }

        return value[..(maxLength - 1)] + "…";
    }

    private void PlayClick()
    {
        try
        {
            _clickPlayer.Play();
        }
        catch
        {
            SystemSounds.Beep.Play();
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
            await _listenerTask;
        }
        catch (OperationCanceledException)
        {
        }

        _clickPlayer.Dispose();
        _notifyIcon.Dispose();
        _cancellation.Dispose();

        ExitThread();
    }
}