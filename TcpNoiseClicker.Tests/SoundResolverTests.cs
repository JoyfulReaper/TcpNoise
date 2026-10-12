using TcpNoiseClicker;
using Xunit;

namespace TcpNoiseClicker.Tests;

public sealed class SoundResolverTests
{
    [Theory]
    [InlineData(22, 4, 2, "click.wav")]
    [InlineData(22, 6, 2, "ipv6.wav")]
    [InlineData(22, 4, 1, "new.wav")]
    [InlineData(420, 4, 2, "bong.wav")]
    [InlineData(42069, 6, 1, "bong.wav")]
    public void SensorAllWinsOverPortOverridesAndEverySemantic(int port, int ipVersion, ulong seen, string semantic)
    {
        var loader = new FakeLoader("click.wav", semantic, "sounds/dn42/all.wav",
            $"sounds/dn42/ports/{port}.wav", $"sounds/ports/{port}.wav", $"sounds/dn42/{semantic}");
        using var resolver = loader.CreateResolver();
        loader.Attempts.Clear();

        Assert.True(resolver.TryPlay(Event(sensor: "dn42", port: (ushort)port, ipVersion: ipVersion, seen: seen)));
        Assert.Equal("sounds/dn42/all.wav", Assert.Single(loader.Played));
        Assert.Equal("sounds/dn42/all.wav", Assert.Single(loader.Attempts));
    }

    [Fact]
    public void SensorAllIsCachedAcrossPortsAndDisposedOnce()
    {
        var loader = new FakeLoader("click.wav", "sounds/dn42/all.wav");
        using var resolver = loader.CreateResolver();
        foreach (var port in new ushort[] { 22, 80, 420 })
            Assert.True(resolver.TryPlay(Event(sensor: "dn42", port: port, ipVersion: 6, seen: 1)));

        Assert.Equal(1, loader.Attempts.Count(path => path == "sounds/dn42/all.wav"));
        var sound = Assert.Single(loader.Created, sound => sound.Path == "sounds/dn42/all.wav");
        Assert.Equal(3, sound.PlayCalls);
        resolver.Dispose();
        resolver.Dispose();
        Assert.All(loader.Created, player => Assert.Equal(1, player.DisposeCalls));
    }

    [Fact]
    public void SensorAllAppliesOnlyToItsOwnSensor()
    {
        var loader = new FakeLoader("click.wav", "sounds/dn42/all.wav", "sounds/ygg/all.wav",
            "sounds/clanker/ports/22.wav");
        using var resolver = loader.CreateResolver();

        Assert.True(resolver.TryPlay(Event(sensor: "dn42")));
        Assert.True(resolver.TryPlay(Event(sensor: "ygg")));
        Assert.True(resolver.TryPlay(Event(sensor: "clanker")));
        Assert.Equal(new[] { "sounds/dn42/all.wav", "sounds/ygg/all.wav", "sounds/clanker/ports/22.wav" }, loader.Played);
    }

    [Theory]
    [InlineData(false)]
    [InlineData(true)]
    public void BadSensorAllFallsThroughAndFailureIsCached(bool failDuringPlayback)
    {
        var loader = new FakeLoader("click.wav", "sounds/dn42/all.wav", "sounds/dn42/ports/22.wav");
        loader.Fail("sounds/dn42/all.wav", failDuringPlayback);
        using var resolver = loader.CreateResolver();

        for (var i = 0; i < 2; i++)
            Assert.True(resolver.TryPlay(Event(sensor: "dn42", ipVersion: 6, seen: 1)));

        Assert.Equal(new[] { "sounds/dn42/ports/22.wav", "sounds/dn42/ports/22.wav" }, loader.Played);
        Assert.Equal(1, loader.Attempts.Count(path => path == "sounds/dn42/all.wav"));
        if (failDuringPlayback)
        {
            var badSound = Assert.Single(loader.Created, sound => sound.Path == "sounds/dn42/all.wav");
            Assert.Equal(1, badSound.PlayCalls);
            Assert.Equal(1, badSound.DisposeCalls);
        }
    }

    [Theory]
    [InlineData(420, 6, 1, "bong.wav")]
    [InlineData(42069, 4, 1, "bong.wav")]
    [InlineData(22, 6, 1, "ipv6.wav")]
    [InlineData(22, 6, 2, "ipv6.wav")]
    [InlineData(22, 4, 1, "new.wav")]
    [InlineData(22, 4, 2, "click.wav")]
    public void NoOverrideDirectoriesPreservesSemanticPrecedence(int port, int ipVersion, ulong seen, string expected)
    {
        var loader = new FakeLoader("click.wav", "new.wav", "ipv6.wav", "bong.wav");
        using var resolver = loader.CreateResolver();

        var connection = Event(port: (ushort)port, ipVersion: ipVersion, seen: seen);
        Assert.Equal(expected, SoundResolver.GetSemanticSound(connection));
        Assert.True(resolver.TryPlay(connection));
        Assert.Equal(expected, Assert.Single(loader.Played));
    }

    [Theory]
    [InlineData(22, 4, 2, "click.wav")]
    [InlineData(22, 4, 1, "new.wav")]
    [InlineData(22, 6, 1, "ipv6.wav")]
    [InlineData(420, 6, 1, "bong.wav")]
    public void NodeSemanticWinsOverGlobalWhenPortOverridesAreMissing(int port, int ipVersion, ulong seen, string semantic)
    {
        var nodePath = "sounds/clanker/" + semantic;
        var loader = new FakeLoader("click.wav", semantic, nodePath);
        using var resolver = loader.CreateResolver();

        Assert.True(resolver.TryPlay(Event(port: (ushort)port, ipVersion: ipVersion, seen: seen)));
        Assert.Equal(nodePath, Assert.Single(loader.Played));
    }

    [Theory]
    [InlineData("unknown")]
    [InlineData("clanker")]
    public void MissingNodeFolderOrIndividualSemanticFallsThroughToGlobal(string sensor)
    {
        var loader = new FakeLoader("click.wav", "ipv6.wav", "sounds/clanker/new.wav");
        using var resolver = loader.CreateResolver();

        Assert.True(resolver.TryPlay(Event(sensor: sensor, ipVersion: 6)));
        Assert.Equal("ipv6.wav", Assert.Single(loader.Played));
    }

    [Theory]
    [InlineData(false)]
    [InlineData(true)]
    public void BadNodeSemanticFallsThroughToGlobal(bool failDuringPlayback)
    {
        var loader = new FakeLoader("click.wav", "ipv6.wav", "sounds/clanker/ipv6.wav");
        loader.Fail("sounds/clanker/ipv6.wav", failDuringPlayback);
        using var resolver = loader.CreateResolver();

        Assert.True(resolver.TryPlay(Event(ipVersion: 6)));
        Assert.Equal("ipv6.wav", Assert.Single(loader.Played));
    }

    [Theory]
    [InlineData(22, 4, 2)]
    [InlineData(22, 6, 2)]
    [InlineData(22, 4, 1)]
    [InlineData(420, 6, 1)]
    [InlineData(42069, 4, 1)]
    public void GlobalPortWinsOverEverySemantic(int port, int ipVersion, ulong seen)
    {
        var portPath = $"sounds/ports/{port}.wav";
        var loader = new FakeLoader("click.wav", "new.wav", "ipv6.wav", "bong.wav", portPath,
            "sounds/clanker/click.wav", "sounds/clanker/ipv6.wav", "sounds/clanker/bong.wav");
        using var resolver = loader.CreateResolver();

        Assert.True(resolver.TryPlay(Event(port: (ushort)port, ipVersion: ipVersion, seen: seen)));
        Assert.Equal(portPath, Assert.Single(loader.Played));
    }

    [Theory]
    [InlineData(false)]
    [InlineData(true)]
    public void NodePortWorksAndWinsOverGlobalPort(bool hasGlobalPort)
    {
        var loader = new FakeLoader("click.wav", "sounds/clanker/ports/22.wav");
        if (hasGlobalPort)
            loader.Available.Add("sounds/ports/22.wav");
        using var resolver = loader.CreateResolver();

        Assert.True(resolver.TryPlay(Event(ipVersion: 6, seen: 1)));
        Assert.Equal("sounds/clanker/ports/22.wav", Assert.Single(loader.Played));
    }

    [Theory]
    [InlineData(false)]
    [InlineData(true)]
    public void BadPortOverridesFallThroughInOrder(bool failDuringPlayback)
    {
        var loader = new FakeLoader("click.wav", "ipv6.wav", "sounds/clanker/ports/22.wav",
            "sounds/ports/22.wav", "sounds/clanker/ipv6.wav");
        loader.Fail("sounds/clanker/ports/22.wav", failDuringPlayback);
        using var resolver = loader.CreateResolver();

        Assert.True(resolver.TryPlay(Event(ipVersion: 6)));
        Assert.Equal("sounds/ports/22.wav", loader.Played.Last());

        loader.Fail("sounds/ports/22.wav", failDuringPlayback);
        Assert.True(resolver.TryPlay(Event(ipVersion: 6)));
        Assert.Equal("sounds/clanker/ipv6.wav", loader.Played.Last());
    }

    [Theory]
    [InlineData("new.wav", 22, 4, 1, false)]
    [InlineData("ipv6.wav", 22, 6, 2, false)]
    [InlineData("bong.wav", 420, 6, 1, false)]
    [InlineData("new.wav", 22, 4, 1, true)]
    [InlineData("ipv6.wav", 22, 6, 2, true)]
    [InlineData("bong.wav", 420, 6, 1, true)]
    public void FailedOptionalGlobalFallsDirectlyToClick(string semantic, int port, int ipVersion, ulong seen, bool playback)
    {
        var loader = new FakeLoader("click.wav", "new.wav", "ipv6.wav", "bong.wav");
        loader.Fail(semantic, playback);
        using var resolver = loader.CreateResolver();

        Assert.True(resolver.TryPlay(Event(port: (ushort)port, ipVersion: ipVersion, seen: seen)));
        Assert.Equal("click.wav", Assert.Single(loader.Played));
    }

    [Fact]
    public void MissingOptionalGlobalFallsToClick()
    {
        var loader = new FakeLoader("click.wav", "sounds/clanker/click.wav", "new.wav");
        using var resolver = loader.CreateResolver();
        // Fallback is the global click, even if other node/global semantics exist.
        Assert.True(resolver.TryPlay(Event(ipVersion: 6)));
        Assert.Equal("click.wav", Assert.Single(loader.Played));
    }

    [Fact]
    public void FailuresTraverseTheEntireCandidateChainInOrder()
    {
        var loader = new FakeLoader("click.wav", "ipv6.wav", "sounds/clanker/ports/22.wav",
            "sounds/ports/22.wav", "sounds/clanker/ipv6.wav", "sounds/clanker/all.wav");
        foreach (var path in loader.Available.Where(path => path != "click.wav"))
            loader.Fail(path, playback: true);
        using var resolver = loader.CreateResolver();
        Assert.True(resolver.TryPlay(Event(ipVersion: 6)));
        Assert.Equal(new[] { "sounds/clanker/all.wav", "sounds/clanker/ports/22.wav", "sounds/ports/22.wav",
            "sounds/clanker/ipv6.wav", "ipv6.wav", "click.wav" }, loader.Created
            .Where(sound => sound.PlayCalls > 0).OrderBy(sound => sound.PlayOrder).Select(sound => sound.Path));
        Assert.Equal("click.wav", Assert.Single(loader.Played));
    }

    public static TheoryData<string?> UnsafeSensors => new()
    {
        null, "", " ", ".", "..", "../foo", "..\\foo", "C:\\something", "/tmp/foo",
        "\\\\server\\share", "a/b", "a\\b", "a:b", "a\0b", "a\nb", "a?b", "a*b", "a|b",
        "a\"b", "a<b", "a>b", "clanker.", "clanker ", " clanker", "CON", "con.wav",
        "NUL", "AUX", "PRN", "COM1", "LPT9", "COM¹", "ports", "PORTS", new string('a', 256)
    };

    [Theory]
    [MemberData(nameof(UnsafeSensors))]
    public void UnsafeSensorNeverProducesNodeLookupButStillUsesGlobalPort(string? sensor)
    {
        var loader = new FakeLoader("click.wav", "sounds/ports/22.wav");
        using var resolver = loader.CreateResolver();
        loader.Attempts.Clear();

        Assert.True(resolver.TryPlay(Event(sensor: sensor!)));
        Assert.Equal("sounds/ports/22.wav", Assert.Single(loader.Attempts));
        Assert.Equal("sounds/ports/22.wav", Assert.Single(loader.Played));
    }

    [Theory]
    [InlineData("overlay-dn42")]
    [InlineData("node.example")]
    [InlineData("node_1")]
    public void SafeSensorNamesUseLiteralDirectory(string sensor)
    {
        var path = $"sounds/{sensor}/click.wav";
        var loader = new FakeLoader("click.wav", path);
        using var resolver = loader.CreateResolver();
        Assert.True(resolver.TryPlay(Event(sensor: sensor)));
        Assert.Equal(path, Assert.Single(loader.Played));
    }

    [Fact]
    public void NullPayloadUsesOnlyGlobalClick()
    {
        var loader = new FakeLoader("click.wav");
        using var resolver = loader.CreateResolver();
        loader.Attempts.Clear();
        Assert.True(resolver.TryPlay(null));
        Assert.Equal("click.wav", Assert.Single(loader.Played));
        Assert.Empty(loader.Attempts);
    }

    [Fact]
    public void LoadedSoundsAndMissingOrBadLookupsAreCached()
    {
        var loader = new FakeLoader("click.wav", "sounds/clanker/ipv6.wav", "sounds/ports/22.wav");
        loader.Fail("sounds/ports/22.wav", playback: false);
        using var resolver = loader.CreateResolver();
        for (var i = 0; i < 3; i++)
            Assert.True(resolver.TryPlay(Event(ipVersion: 6)));

        Assert.All(loader.Attempts.GroupBy(path => path), group => Assert.Single(group));
        Assert.Contains("sounds/clanker/all.wav", loader.Attempts);
        Assert.Contains("sounds/clanker/ports/22.wav", loader.Attempts);
        Assert.Contains("sounds/ports/22.wav", loader.Attempts);
        Assert.Equal(3, loader.Played.Count);
    }

    [Fact]
    public void CacheDistinguishesSensorPortAndSemantic()
    {
        var loader = new FakeLoader("click.wav", "sounds/clanker/ports/22.wav",
            "sounds/scopecreep/ports/22.wav", "sounds/clanker/ports/80.wav",
            "sounds/clanker/ipv6.wav", "sounds/clanker/new.wav");
        using var resolver = loader.CreateResolver();
        resolver.TryPlay(Event());
        resolver.TryPlay(Event(sensor: "scopecreep"));
        resolver.TryPlay(Event(port: 80));
        resolver.TryPlay(Event(port: 443, ipVersion: 6));
        resolver.TryPlay(Event(port: 443, seen: 1));
        Assert.Equal(new[] { "sounds/clanker/ports/22.wav", "sounds/scopecreep/ports/22.wav",
            "sounds/clanker/ports/80.wav", "sounds/clanker/ipv6.wav", "sounds/clanker/new.wav" }, loader.Played);
    }

    [Fact]
    public void PlaybackFailuresAreRetiredAndAllPlayersDisposedExactlyOnce()
    {
        var loader = new FakeLoader("click.wav", "new.wav", "ipv6.wav", "bong.wav",
            "sounds/clanker/ports/22.wav", "sounds/ports/22.wav", "sounds/clanker/ipv6.wav");
        loader.Fail("sounds/clanker/ports/22.wav", playback: true);
        loader.Fail("sounds/ports/22.wav", playback: true);
        var resolver = loader.CreateResolver();
        Assert.True(resolver.TryPlay(Event(ipVersion: 6)));
        Assert.True(resolver.TryPlay(Event(ipVersion: 6)));
        Assert.All(loader.Created.Where(sound => sound.FailsPlayback), sound => Assert.Equal(1, sound.PlayCalls));
        resolver.Dispose();
        resolver.Dispose();
        Assert.All(loader.Created, sound => Assert.Equal(1, sound.DisposeCalls));
        Assert.Throws<ObjectDisposedException>(() => resolver.TryPlay(Event()));
    }

    [Fact]
    public void MissingRequiredClickStillFailsAtStartup()
    {
        var loader = new FakeLoader();
        Assert.Throws<FileNotFoundException>(() => loader.CreateResolver());
    }

    [Fact]
    public void RequiredClickPlaybackFailureReturnsFalseForTrayBeep()
    {
        var loader = new FakeLoader("click.wav");
        loader.Fail("click.wav", playback: true);
        using var resolver = loader.CreateResolver();
        Assert.False(resolver.TryPlay(Event()));
        Assert.False(resolver.TryPlay(Event()));
        Assert.Equal(1, Assert.Single(loader.Created).PlayCalls);
    }

    private static TcpNoisePayload Event(string sensor = "clanker", ushort port = 22, int ipVersion = 4, ulong seen = 2)
        => new(sensor, 1, port, ipVersion, "192.0.2.1", 1234, seen, null, false, 0, "");

    private sealed class FakeLoader(params string[] available)
    {
        private readonly string _baseDirectory = Path.GetFullPath(Path.Combine(Path.GetTempPath(), "TcpNoiseResolverTests"));
        private readonly HashSet<string> _loadFailures = new(StringComparer.OrdinalIgnoreCase);
        private readonly HashSet<string> _playFailures = new(StringComparer.OrdinalIgnoreCase);
        public HashSet<string> Available { get; } = new(available, StringComparer.OrdinalIgnoreCase);
        public List<string> Attempts { get; } = [];
        public List<string> Played { get; } = [];
        public List<FakeSound> Created { get; } = [];

        public SoundResolver CreateResolver() => new(_baseDirectory, Load);

        public void Fail(string path, bool playback)
        {
            (playback ? _playFailures : _loadFailures).Add(path);
            // Allow a test to invalidate a sound already loaded successfully.
            foreach (var sound in Created.Where(sound => sound.Path == path))
                sound.FailsPlayback = true;
        }

        private IEventSound Load(string path)
        {
            var relative = Path.GetRelativePath(_baseDirectory, path).Replace('\\', '/');
            Attempts.Add(relative);
            if (!Available.Contains(relative))
                throw new FileNotFoundException();
            if (_loadFailures.Contains(relative))
                throw new InvalidDataException("Bad WAV");
            var sound = new FakeSound(relative, Played) { FailsPlayback = _playFailures.Contains(relative) };
            Created.Add(sound);
            return sound;
        }
    }

    private sealed class FakeSound(string path, List<string> played) : IEventSound
    {
        public string Path { get; } = path;
        public bool FailsPlayback { get; set; }
        public int PlayCalls { get; private set; }
        public int DisposeCalls { get; private set; }
        public int PlayOrder { get; private set; }

        public void PlaySync()
        {
            PlayCalls++;
            PlayOrder = Interlocked.Increment(ref _playSequence);
            if (FailsPlayback)
                throw new InvalidDataException("Unplayable WAV");
            played.Add(Path);
        }

        public void Dispose() => DisposeCalls++;

        private static int _playSequence;
    }
}
