using TcpNoiseClicker;
using Xunit;

namespace TcpNoiseClicker.Tests;

public sealed class FastSoundSchedulerTests
{
    [Fact]
    public void StartsAreSpacedEvenWhenPreviousSoundHasFinished()
    {
        var time = new ManualTime();
        var voices = new List<TestVoice>();
        using var scheduler = new FastSoundScheduler(_ => NewVoice(voices), time);
        Assert.True(scheduler.TryStart(new TestSound()));
        voices[0].Finish();
        Assert.Equal(1, voices[0].DisposeCalls);

        time.Advance(74);
        Assert.False(scheduler.TryStart(new TestSound()));
        Assert.Equal(TimeSpan.FromMilliseconds(1), scheduler.StartDelay);
        time.Advance(1);
        Assert.True(scheduler.TryStart(new TestSound()));
    }

    [Fact]
    public void FourLongSoundsOverlapAndSaturationDoesNotCreateMoreVoices()
    {
        var time = new ManualTime();
        var voices = new List<TestVoice>();
        using var scheduler = new FastSoundScheduler(_ => NewVoice(voices), time);
        for (var i = 0; i < FastSoundScheduler.MaxConcurrentSounds; i++)
        {
            Assert.True(scheduler.TryStart(new TestSound()));
            time.Advance(FastSoundScheduler.FastSoundStartIntervalMs);
        }
        Assert.True(scheduler.IsFull);
        Assert.All(voices, voice => Assert.False(voice.Completion.IsCompleted));
        for (var i = 0; i < 1000; i++)
            Assert.False(scheduler.TryStart(new TestSound()));
        Assert.Equal(4, voices.Count);

        voices[0].Finish();
        Assert.False(scheduler.IsFull);
        Assert.True(scheduler.TryStart(new TestSound()));
        Assert.Equal(5, voices.Count);
    }

    [Fact]
    public void StopAllDisposesLongVoicesWithoutWaitingForNaturalCompletion()
    {
        var time = new ManualTime();
        var voices = new List<TestVoice>();
        using var scheduler = new FastSoundScheduler(_ => NewVoice(voices), time);
        for (var i = 0; i < 4; i++)
        {
            Assert.True(scheduler.TryStart(new TestSound()));
            time.Advance(75);
        }
        scheduler.StopAll();
        scheduler.StopAll();
        Assert.All(voices, voice => Assert.Equal(1, voice.DisposeCalls));
        Assert.False(scheduler.IsFull);
        scheduler.Dispose();
        Assert.Throws<ObjectDisposedException>(() => scheduler.TryStart(new TestSound()));
    }

    [Fact]
    public void ImmediatelyCompletedVoiceIsReleased()
    {
        var voice = new TestVoice();
        voice.Finish();
        using var scheduler = new FastSoundScheduler(_ => voice);
        Assert.True(scheduler.TryStart(new TestSound()));
        Assert.Equal(1, voice.DisposeCalls);
    }

    [Fact]
    public void FailedStartupConsumesNeitherSlotNorLaunchInterval()
    {
        var fail = true;
        var voice = new TestVoice();
        using var scheduler = new FastSoundScheduler(_ => fail ? throw new InvalidDataException() : voice);
        Assert.Throws<InvalidDataException>(() => scheduler.TryStart(new TestSound()));
        Assert.Equal(TimeSpan.Zero, scheduler.StartDelay);
        fail = false;
        Assert.True(scheduler.TryStart(new TestSound()));
    }

    [Fact]
    public void FallbackBeepAlsoCountsTowardTheStartInterval()
    {
        var time = new ManualTime();
        using var scheduler = new FastSoundScheduler(_ => new TestVoice(), time);
        scheduler.RecordFallbackStart();
        Assert.False(scheduler.TryStart(new TestSound()));
        Assert.Equal(TimeSpan.FromMilliseconds(75), scheduler.StartDelay);
        time.Advance(75);
        Assert.True(scheduler.TryStart(new TestSound()));
    }

    private static TestVoice NewVoice(List<TestVoice> voices)
    {
        var voice = new TestVoice();
        voices.Add(voice);
        return voice;
    }

    private sealed class ManualTime : TimeProvider
    {
        private long _milliseconds;
        public override long TimestampFrequency => 1000;
        public override long GetTimestamp() => _milliseconds;
        public void Advance(int milliseconds) => _milliseconds += milliseconds;
    }
}

internal sealed class TestVoice : IActiveSound
{
    private readonly TaskCompletionSource _completion = new();
    public Task Completion => _completion.Task;
    public int DisposeCalls { get; private set; }
    public Action? Disposed { get; init; }
    public void Finish() => _completion.TrySetResult();
    public void Dispose()
    {
        DisposeCalls++;
        Disposed?.Invoke();
        Finish();
    }
}

internal sealed class TestSound(Action? play = null) : IEventSound
{
    public void PlaySync() => play?.Invoke();
    public void Dispose() { }
}
