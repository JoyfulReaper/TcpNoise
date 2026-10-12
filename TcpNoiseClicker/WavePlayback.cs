using NAudio.Wave;

namespace TcpNoiseClicker;

internal sealed class WavePlayback : IActiveSound
{
    private readonly MemoryStream _data;
    private readonly WaveFileReader _reader;
    private readonly IWavePlayer _output;
    private readonly TaskCompletionSource _stopped = new(TaskCreationOptions.RunContinuationsAsynchronously);
    private int _disposed;

    private WavePlayback(MemoryStream data, WaveFileReader reader, IWavePlayer output)
    {
        _data = data;
        _reader = reader;
        _output = output;
        _output.PlaybackStopped += OnStopped;
    }

    public Task Completion => _stopped.Task;

    public static IActiveSound Start(byte[] waveData, Func<IWavePlayer>? createOutput = null)
    {
        var data = new MemoryStream(waveData, writable: false);
        WaveFileReader? reader = null;
        IWavePlayer? output = null;
        WavePlayback? playback = null;
        try
        {
            reader = new WaveFileReader(data);
            output = createOutput?.Invoke() ?? new WaveOutEvent { DesiredLatency = 50, NumberOfBuffers = 2 };
            output.Init(reader);
            playback = new WavePlayback(data, reader, output);
            output.Play();
            return playback;
        }
        catch
        {
            if (playback is not null)
                output!.PlaybackStopped -= playback.OnStopped;
            try { output?.Dispose(); }
            finally
            {
                reader?.Dispose();
                data.Dispose();
            }
            throw;
        }
    }

    private void OnStopped(object? sender, StoppedEventArgs e) => _stopped.TrySetResult();

    public void Dispose()
    {
        if (Interlocked.Exchange(ref _disposed, 1) != 0)
            return;

        try
        {
            // Reset the device, then wait for its worker to exit, not for the WAV duration.
            _output.Stop();
            _stopped.Task.GetAwaiter().GetResult();
        }
        finally
        {
            _output.PlaybackStopped -= OnStopped;
            try { _output.Dispose(); }
            finally
            {
                _reader.Dispose();
                _data.Dispose();
            }
        }
    }
}
