using NATS.Client.Core;
using NATS.Net;
using System.Media;

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
                if (_muted)
                    continue;

                PlayClick();
            }
        }
        catch (OperationCanceledException)
            when (cancellationToken.IsCancellationRequested)
        {
        }
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