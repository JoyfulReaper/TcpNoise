using System.Collections.Concurrent;
using TcpNoiseClicker;
using Xunit;

namespace TcpNoiseClicker.Tests;

public sealed class SoundPlaybackWorkerTests
{
    private static readonly TimeSpan TestTimeout = TimeSpan.FromSeconds(5);

    [Fact]
    public void QueueIsBoundedAndNeverWaitsForSpace()
    {
        using var resolver = Resolver(new TestSound());
        using var worker = new SoundPlaybackWorker(resolver, 32);
        for (var i = 0; i < 32; i++)
            Assert.True(worker.TryEnqueue(null));
        Assert.False(worker.TryEnqueue(null));
    }

    [Fact]
    public async Task DefaultModeIsSerialAndEnqueuingStaysResponsive()
    {
        var firstStarted = Signal();
        var secondStarted = Signal();
        using var release = new ManualResetEventSlim();
        var playCount = 0;
        var sound = new TestSound(() =>
        {
            if (Interlocked.Increment(ref playCount) == 1)
            {
                firstStarted.TrySetResult();
                release.Wait();
            }
            else
                secondStarted.TrySetResult();
        });
        using var resolver = Resolver(sound);
        using var worker = new SoundPlaybackWorker(resolver, 32,
            new FastSoundScheduler(_ => throw new InvalidOperationException("Fast backend used in normal mode")),
            beep: () => throw new InvalidOperationException("Unexpected fallback"), stopSerialPlayback: release.Set);
        using var shutdown = new CancellationTokenSource();
        var running = Task.Run(() => worker.RunAsync(shutdown.Token));
        try
        {
            Assert.True(worker.TryEnqueue(null));
            await firstStarted.Task.WaitAsync(TestTimeout);
            Assert.True(worker.TryEnqueue(null));
            Assert.False(secondStarted.Task.IsCompleted);
            release.Set();
            await secondStarted.Task.WaitAsync(TestTimeout);
        }
        finally
        {
            shutdown.Cancel();
            await running.WaitAsync(TestTimeout);
        }
        Assert.Equal(2, playCount);
    }

    [Fact]
    public async Task FastModeOverlapsAndDisablingItStopsVoicesBeforeNormalPlayback()
    {
        var voices = new ConcurrentQueue<TestVoice>();
        var twoStarted = Signal();
        var allStopped = Signal();
        var normalStarted = Signal();
        var disposed = 0;
        var normalSawStoppedVoices = false;
        using var resolver = Resolver(new TestSound(() =>
        {
            normalSawStoppedVoices = voices.All(voice => voice.DisposeCalls == 1);
            normalStarted.TrySetResult();
        }));
        var scheduler = new FastSoundScheduler(_ =>
        {
            var voice = new TestVoice { Disposed = () =>
            {
                if (Interlocked.Increment(ref disposed) == 2)
                    allStopped.TrySetResult();
            } };
            voices.Enqueue(voice);
            if (voices.Count == 2)
                twoStarted.TrySetResult();
            return voice;
        });
        using var worker = new SoundPlaybackWorker(resolver, 32, scheduler,
            beep: () => { }, stopSerialPlayback: () => { });
        using var shutdown = new CancellationTokenSource();
        worker.SetFastMode(true);
        var running = Task.Run(() => worker.RunAsync(shutdown.Token));
        try
        {
            worker.TryEnqueue(null);
            worker.TryEnqueue(null);
            await twoStarted.Task.WaitAsync(TestTimeout);
            Assert.All(voices, voice => Assert.False(voice.Completion.IsCompleted));
            Assert.False(normalStarted.Task.IsCompleted);

            // No new event is needed to apply OFF while the worker is otherwise idle.
            worker.SetFastMode(false);
            await allStopped.Task.WaitAsync(TestTimeout);
            worker.TryEnqueue(null);
            await normalStarted.Task.WaitAsync(TestTimeout);
            Assert.True(normalSawStoppedVoices);
        }
        finally
        {
            shutdown.Cancel();
            await running.WaitAsync(TestTimeout);
        }
    }

    [Fact]
    public async Task EnablingFastModeLetsCurrentSerialSoundFinish()
    {
        var normalStarted = Signal();
        var fastStarted = Signal();
        using var release = new ManualResetEventSlim();
        var serialCount = 0;
        using var resolver = Resolver(new TestSound(() =>
        {
            Interlocked.Increment(ref serialCount);
            normalStarted.TrySetResult();
            release.Wait();
        }));
        using var worker = new SoundPlaybackWorker(resolver, 32, new FastSoundScheduler(_ =>
        {
            fastStarted.TrySetResult();
            return new TestVoice();
        }), stopSerialPlayback: release.Set);
        using var shutdown = new CancellationTokenSource();
        var running = Task.Run(() => worker.RunAsync(shutdown.Token));
        try
        {
            worker.TryEnqueue(null);
            await normalStarted.Task.WaitAsync(TestTimeout);
            worker.SetFastMode(true);
            worker.TryEnqueue(null);
            Assert.False(fastStarted.Task.IsCompleted);
            release.Set();
            await fastStarted.Task.WaitAsync(TestTimeout);
            Assert.Equal(1, serialCount);
        }
        finally
        {
            shutdown.Cancel();
            await running.WaitAsync(TestTimeout);
        }
    }

    [Fact]
    public async Task ShutdownStopsLongFastSoundsAndCancelsPendingLaunchDelay()
    {
        var started = Signal();
        var voices = new ConcurrentQueue<TestVoice>();
        using var resolver = Resolver(new TestSound());
        using var worker = new SoundPlaybackWorker(resolver, 32, new FastSoundScheduler(_ =>
        {
            var voice = new TestVoice();
            voices.Enqueue(voice);
            started.TrySetResult();
            return voice;
        }), stopSerialPlayback: () => { });
        using var shutdown = new CancellationTokenSource();
        worker.SetFastMode(true);
        worker.TryEnqueue(null);
        worker.TryEnqueue(null);
        var running = Task.Run(() => worker.RunAsync(shutdown.Token));
        await started.Task.WaitAsync(TestTimeout);
        shutdown.Cancel();
        await running.WaitAsync(TestTimeout);
        Assert.Equal(1, Assert.Single(voices).DisposeCalls);
        Assert.False(worker.TryEnqueue(null));
    }

    [Fact]
    public async Task ShutdownStopsSerialPlaybackEvenWhenStartRacesWithCancellation()
    {
        var enteringPlayback = Signal();
        using var allowStart = new ManualResetEventSlim();
        using var finished = new ManualResetEventSlim();
        var stopCount = 0;
        var playing = false;
        using var resolver = Resolver(new TestSound(() =>
        {
            enteringPlayback.TrySetResult();
            allowStart.Wait();
            Volatile.Write(ref playing, true);
            finished.Wait();
        }));
        using var worker = new SoundPlaybackWorker(resolver, 32, stopSerialPlayback: () =>
        {
            if (Interlocked.Increment(ref stopCount) == 1)
                allowStart.Set();
            if (Volatile.Read(ref playing))
                finished.Set();
        });
        using var shutdown = new CancellationTokenSource();
        worker.TryEnqueue(null);
        var running = Task.Run(() => worker.RunAsync(shutdown.Token));
        await enteringPlayback.Task.WaitAsync(TestTimeout);
        shutdown.Cancel();
        await running.WaitAsync(TestTimeout);
        Assert.True(finished.IsSet);
    }

    private static TaskCompletionSource Signal() => new(TaskCreationOptions.RunContinuationsAsynchronously);

    private static SoundResolver Resolver(IEventSound click)
        => new(Path.GetTempPath(), path => Path.GetFileName(path) == "click.wav"
            ? click : throw new FileNotFoundException());
}
