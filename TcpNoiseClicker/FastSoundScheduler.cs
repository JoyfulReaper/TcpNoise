namespace TcpNoiseClicker;

internal interface IActiveSound : IDisposable
{
    Task Completion { get; }
}

internal interface IFastEventSound
{
    IActiveSound StartPlayback();
}

internal sealed class FastSoundScheduler : IDisposable
{
    // Scanner bursts can overlap without restarting a long notification WAV.
    public const int FastSoundStartIntervalMs = 75;
    public const int MaxConcurrentSounds = 4;

    private readonly object _gate = new();
    private readonly HashSet<IActiveSound> _active = [];
    private readonly Func<IEventSound, IActiveSound> _startPlayback;
    private readonly TimeProvider _time;
    private long? _lastStart;
    private bool _disposed;

    public FastSoundScheduler(Func<IEventSound, IActiveSound>? startPlayback = null, TimeProvider? time = null)
    {
        _startPlayback = startPlayback ?? (sound => ((IFastEventSound)sound).StartPlayback());
        _time = time ?? TimeProvider.System;
    }

    public bool IsFull
    {
        get { lock (_gate) return _active.Count >= MaxConcurrentSounds; }
    }

    public TimeSpan StartDelay
    {
        get
        {
            lock (_gate)
            {
                if (_lastStart is not { } timestamp)
                    return TimeSpan.Zero;
                var delay = TimeSpan.FromMilliseconds(FastSoundStartIntervalMs) - _time.GetElapsedTime(timestamp);
                return delay > TimeSpan.Zero ? delay : TimeSpan.Zero;
            }
        }
    }

    public bool TryStart(IEventSound sound)
    {
        IActiveSound playback;
        lock (_gate)
        {
            ObjectDisposedException.ThrowIf(_disposed, this);
            // Bound device handles and voices; saturated events are dropped, not queued again.
            if (_active.Count >= MaxConcurrentSounds || StartDelay > TimeSpan.Zero)
                return false;
            playback = _startPlayback(sound);
            _lastStart = _time.GetTimestamp();
            _active.Add(playback);
        }

        _ = playback.Completion.ContinueWith(
            _ => Release(playback), CancellationToken.None,
            TaskContinuationOptions.ExecuteSynchronously, TaskScheduler.Default);
        return true;
    }

    private void Release(IActiveSound playback)
    {
        lock (_gate)
        {
            if (!_active.Contains(playback))
                return;
            DisposePlayback(playback);
            _active.Remove(playback);
        }
    }

    public void RecordFallbackStart()
    {
        lock (_gate)
            _lastStart = _time.GetTimestamp();
    }

    private static void DisposePlayback(IActiveSound playback)
    {
        try { playback.Dispose(); }
        catch { }
    }

    public void StopAll()
    {
        IActiveSound[] active;
        lock (_gate)
        {
            active = _active.ToArray();
            _active.Clear();
        }
        foreach (var playback in active)
            DisposePlayback(playback);
    }

    public void Dispose()
    {
        lock (_gate)
            _disposed = true;
        StopAll();
    }
}
