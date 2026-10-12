using System.Media;
using System.Threading.Channels;

namespace TcpNoiseClicker;

internal sealed class SoundPlaybackWorker : IDisposable
{
    private readonly SoundResolver _sounds;
    private readonly FastSoundScheduler _fastSounds;
    private readonly Channel<TcpNoisePayload?> _queue;
    private readonly SemaphoreSlim _wakeUp = new(0, 1);
    private readonly Action _beep;
    private readonly Action _stopSerialPlayback;
    private volatile bool _fastMode;

    public SoundPlaybackWorker(SoundResolver sounds, int queueCapacity,
        FastSoundScheduler? fastSounds = null, Action? beep = null, Action? stopSerialPlayback = null)
    {
        _sounds = sounds;
        _fastSounds = fastSounds ?? new FastSoundScheduler();
        _beep = beep ?? (() => SystemSounds.Beep.Play());
        _stopSerialPlayback = stopSerialPlayback ?? PlayerSound.StopSerialPlayback;
        _queue = Channel.CreateBounded<TcpNoisePayload?>(new BoundedChannelOptions(queueCapacity)
        {
            SingleReader = true,
            SingleWriter = true,
            FullMode = BoundedChannelFullMode.Wait
        });
    }

    public void SetFastMode(bool enabled)
    {
        _fastMode = enabled;
        WakeUp();
    }

    public bool TryEnqueue(TcpNoisePayload? connection)
    {
        if (!_queue.Writer.TryWrite(connection))
            return false;
        WakeUp();
        return true;
    }

    private void WakeUp()
    {
        if (_wakeUp.CurrentCount != 0)
            return;
        try { _wakeUp.Release(); }
        catch (SemaphoreFullException) { }
    }

    public async Task RunAsync(CancellationToken cancellationToken)
    {
        using var workerFinished = new CancellationTokenSource();
        var stopSerialTask = StopSerialOnCancellationAsync(cancellationToken, workerFinished.Token);
        try
        {
            while (true)
            {
                await _wakeUp.WaitAsync(cancellationToken);
                do
                {
                    cancellationToken.ThrowIfCancellationRequested();
                    if (!_fastMode)
                        _fastSounds.StopAll();
                    if (!_queue.Reader.TryRead(out var connection))
                        break;

                    if (_fastMode)
                    {
                        if (_fastSounds.IsFull)
                            continue;
                        // Space launches to keep scanner bursts audible; never truncate a WAV.
                        await Task.Delay(_fastSounds.StartDelay, cancellationToken);
                    }

                    cancellationToken.ThrowIfCancellationRequested();
                    bool played;
                    if (_fastMode)
                        played = _sounds.TryPlayFast(connection, sound => _fastSounds.TryStart(sound));
                    else
                    {
                        _fastSounds.StopAll();
                        played = _sounds.TryPlay(connection);
                    }

                    if (!played)
                    {
                        try
                        {
                            _beep();
                            if (_fastMode)
                                _fastSounds.RecordFallbackStart();
                        }
                        catch { }
                    }
                } while (true);
            }
        }
        catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
        {
        }
        finally
        {
            workerFinished.Cancel();
            await stopSerialTask;
            _fastSounds.StopAll();
            _queue.Writer.TryComplete();
        }
    }

    private async Task StopSerialOnCancellationAsync(CancellationToken shutdown, CancellationToken workerFinished)
    {
        using var waiting = CancellationTokenSource.CreateLinkedTokenSource(shutdown, workerFinished);
        try
        {
            await Task.Delay(Timeout.Infinite, waiting.Token).ConfigureAwait(false);
        }
        catch (OperationCanceledException)
        {
        }

        // Repeat the stop to cover cancellation racing with entry into PlaySync.
        try
        {
            while (shutdown.IsCancellationRequested && !workerFinished.IsCancellationRequested)
            {
                try { _stopSerialPlayback(); }
                catch { }
                await Task.Delay(25, workerFinished).ConfigureAwait(false);
            }
        }
        catch (OperationCanceledException) when (workerFinished.IsCancellationRequested)
        {
        }
    }

    public void Dispose()
    {
        _fastSounds.Dispose();
        _wakeUp.Dispose();
    }
}
