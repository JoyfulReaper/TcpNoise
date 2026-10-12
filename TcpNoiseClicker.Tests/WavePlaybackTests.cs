using NAudio.Wave;
using TcpNoiseClicker;
using Xunit;

namespace TcpNoiseClicker.Tests;

public sealed class WavePlaybackTests
{
    [Fact]
    public void ConcurrentVoicesUseIndependentReadersAndStopBeforeTheWaveEnds()
    {
        var data = SilentWave();
        var firstOutput = new TestOutput();
        var secondOutput = new TestOutput();
        using var first = WavePlayback.Start(data, () => firstOutput);
        using var second = WavePlayback.Start(data, () => secondOutput);
        Assert.Equal(PlaybackState.Playing, firstOutput.PlaybackState);
        Assert.Equal(PlaybackState.Playing, secondOutput.PlaybackState);
        Assert.NotSame(firstOutput.Provider, secondOutput.Provider);
        Assert.False(first.Completion.IsCompleted);
        Assert.False(second.Completion.IsCompleted);
        first.Dispose();
        Assert.True(first.Completion.IsCompleted);
        Assert.False(second.Completion.IsCompleted);
        Assert.Equal(1, firstOutput.StopCalls);
        Assert.Equal(1, firstOutput.DisposeCalls);
        second.Dispose();
        Assert.Equal(1, secondOutput.DisposeCalls);
    }

    [Theory]
    [InlineData(false)]
    [InlineData(true)]
    public async Task CompletionOrDeviceErrorReleasesAudioResources(bool deviceError)
    {
        var output = new TestOutput();
        using var scheduler = new FastSoundScheduler(_ => WavePlayback.Start(SilentWave(), () => output));
        Assert.True(scheduler.TryStart(new TestSound()));
        output.Finish(deviceError ? new IOException("Device removed") : null);
        await output.Disposed.Task.WaitAsync(TimeSpan.FromSeconds(5));
        Assert.Equal(1, output.DisposeCalls);
        output.Finish();
        Assert.Equal(1, output.DisposeCalls);
    }

    [Theory]
    [InlineData(true)]
    [InlineData(false)]
    public void FailedDeviceInitializationOrStartDisposesResources(bool failInit)
    {
        var output = new TestOutput { FailInit = failInit, FailPlay = !failInit };
        Assert.Throws<InvalidOperationException>(() => WavePlayback.Start(SilentWave(), () => output));
        Assert.Equal(1, output.DisposeCalls);
    }

    [Fact]
    public void CorruptWaveFailsBeforeOpeningAnAudioDevice()
    {
        var deviceOpened = false;
        Assert.ThrowsAny<Exception>(() => WavePlayback.Start([1, 2, 3, 4], () =>
        {
            deviceOpened = true;
            return new TestOutput();
        }));
        Assert.False(deviceOpened);
    }

    private static byte[] SilentWave()
    {
        using var data = new MemoryStream();
        using (var writer = new WaveFileWriter(data, new WaveFormat(8000, 16, 1)))
            writer.Write(new byte[16000], 0, 16000);
        return data.ToArray();
    }

    private sealed class TestOutput : IWavePlayer
    {
        public event EventHandler<StoppedEventArgs>? PlaybackStopped;
        public IWaveProvider? Provider { get; private set; }
        public bool FailInit { get; init; }
        public bool FailPlay { get; init; }
        public int StopCalls { get; private set; }
        public int DisposeCalls { get; private set; }
        public TaskCompletionSource Disposed { get; } = new(TaskCreationOptions.RunContinuationsAsynchronously);
        public float Volume { get; set; } = 1;
        public PlaybackState PlaybackState { get; private set; }
        public WaveFormat OutputWaveFormat => Provider!.WaveFormat;

        public void Init(IWaveProvider waveProvider)
        {
            Provider = waveProvider;
            if (FailInit)
                throw new InvalidOperationException();
        }

        public void Play()
        {
            if (FailPlay)
                throw new InvalidOperationException();
            PlaybackState = PlaybackState.Playing;
        }

        public void Pause() => PlaybackState = PlaybackState.Paused;
        public void Stop()
        {
            StopCalls++;
            Finish();
        }
        public void Finish(Exception? error = null)
        {
            PlaybackState = PlaybackState.Stopped;
            PlaybackStopped?.Invoke(this, new StoppedEventArgs(error));
        }
        public void Dispose()
        {
            DisposeCalls++;
            Disposed.TrySetResult();
        }
    }
}
