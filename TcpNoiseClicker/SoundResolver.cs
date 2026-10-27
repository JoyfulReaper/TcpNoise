using System.Globalization;

namespace TcpNoiseClicker;

internal interface IEventSound : IDisposable
{
    void PlaySync();
}

// Used only by the single sound consumer, then disposed after that consumer stops.
internal sealed class SoundResolver : IDisposable
{
    private readonly string _baseDirectory;
    private readonly string _soundsDirectory;
    private readonly Func<string, IEventSound> _loadSound;
    private readonly Dictionary<string, IEventSound?> _cache = new(StringComparer.OrdinalIgnoreCase);
    private readonly HashSet<IEventSound> _ownedSounds = new(ReferenceEqualityComparer.Instance);
    private readonly HashSet<string> _fastPlaybackFailures = new(StringComparer.OrdinalIgnoreCase);
    private bool _disposed;

    public SoundResolver(string baseDirectory, Func<string, IEventSound> loadSound)
    {
        _baseDirectory = Path.GetFullPath(baseDirectory);
        _soundsDirectory = Path.Combine(_baseDirectory, "sounds");
        _loadSound = loadSound;

        // click.wav remains required; let the caller report a startup failure.
        var clickPath = GlobalPath("click.wav");
        var click = _loadSound(clickPath);
        _cache.Add(clickPath, click);
        _ownedSounds.Add(click);

        TryLoad(GlobalPath("new.wav"));
        TryLoad(GlobalPath("ipv6.wav"));
        TryLoad(GlobalPath("bong.wav"));
    }

    public static string GetSemanticSound(TcpNoisePayload? connection)
    {
        if (connection?.ListenPort is 420 or 42069)
            return "bong.wav";
        if (connection?.IpVersion == 6)
            return "ipv6.wav";
        if (connection?.SeenCount == 1)
            return "new.wav";
        return "click.wav";
    }

    // A successful Load does not guarantee PlaySync will accept the WAV.
    // Retire playback failures too, and continue through the same fallback chain.
    public bool TryPlay(TcpNoisePayload? connection)
        => TryPlay(connection, sound => sound.PlaySync(), null);

    public bool TryPlayFast(TcpNoisePayload? connection, Action<IEventSound> startPlayback)
        => TryPlay(connection, startPlayback, _fastPlaybackFailures);

    private bool TryPlay(TcpNoisePayload? connection, Action<IEventSound> play, HashSet<string>? playbackFailures)
    {
        ObjectDisposedException.ThrowIf(_disposed, this);
        var semanticSound = GetSemanticSound(connection);

        foreach (var path in GetCandidatePaths(connection, semanticSound))
        {
            if (playbackFailures?.Contains(path) == true)
                continue;
            var sound = TryLoad(path);
            if (sound is null)
                continue;

            try
            {
                play(sound);
                return true;
            }
            catch
            {
                if (playbackFailures is not null)
                {
                    // A format/device failure in fast mode must not disable normal playback.
                    playbackFailures.Add(path);
                }
                else
                {
                    _cache[path] = null;
                    Release(sound);
                }
            }
        }

        return false;
    }

    private IEnumerable<string> GetCandidatePaths(TcpNoisePayload? connection, string semanticSound)
    {
        string? nodeDirectory = null;
        if (connection is not null)
        {
            if (IsSafeSensor(connection.Sensor))
            {
                nodeDirectory = Path.Combine(_soundsDirectory, connection.Sensor);
                yield return Path.Combine(nodeDirectory, "all.wav");
            }

            var portFile = connection.ListenPort.ToString(CultureInfo.InvariantCulture) + ".wav";
            if (nodeDirectory is not null)
                yield return Path.Combine(nodeDirectory, "ports", portFile);
            yield return Path.Combine(_soundsDirectory, "ports", portFile);
        }

        if (nodeDirectory is not null)
            yield return Path.Combine(nodeDirectory, semanticSound);
        yield return GlobalPath(semanticSound);
        if (semanticSound != "click.wav")
            yield return GlobalPath("click.wav");
    }

    private static bool IsSafeSensor(string? sensor)
    {
        // Accept one literal Windows directory component, without normalization
        // that could alias an unsafe name to another sensor. "ports" is reserved.
        if (string.IsNullOrWhiteSpace(sensor) || sensor.Length > 255 ||
            sensor is "." or ".." || sensor != sensor.Trim() ||
            sensor.EndsWith('.') || sensor.Equals("ports", StringComparison.OrdinalIgnoreCase))
            return false;

        foreach (var character in sensor)
        {
            if (char.IsControl(character) || "<>:\"/\\|?*".Contains(character))
                return false;
        }

        // Windows device names are reserved even with an extension.
        var stem = sensor.Split('.')[0].TrimEnd(' ');
        if (stem.Equals("CON", StringComparison.OrdinalIgnoreCase) ||
            stem.Equals("PRN", StringComparison.OrdinalIgnoreCase) ||
            stem.Equals("AUX", StringComparison.OrdinalIgnoreCase) ||
            stem.Equals("NUL", StringComparison.OrdinalIgnoreCase))
            return false;

        if (stem.Length == 4 &&
            (stem.StartsWith("COM", StringComparison.OrdinalIgnoreCase) ||
             stem.StartsWith("LPT", StringComparison.OrdinalIgnoreCase)) &&
            "123456789¹²³".Contains(stem[3]))
            return false;

        return true;
    }

    private string GlobalPath(string fileName) => Path.Combine(_baseDirectory, fileName);

    private IEventSound? TryLoad(string path)
    {
        if (_cache.TryGetValue(path, out var cached))
            return cached;

        IEventSound? sound = null;
        try
        {
            sound = _loadSound(path);
            _ownedSounds.Add(sound);
        }
        catch
        {
            // Missing, inaccessible and invalid files all use the next candidate.
        }

        _cache.Add(path, sound);
        return sound;
    }

    private void Release(IEventSound sound)
    {
        if (!_ownedSounds.Remove(sound))
            return;
        try
        {
            sound.Dispose();
        }
        catch
        {
            // Cleanup of a failed optional sound must not stop the consumer.
        }
    }

    public void Dispose()
    {
        if (_disposed)
            return;
        _disposed = true;
        foreach (var sound in _ownedSounds.ToArray())
            Release(sound);
        _cache.Clear();
        _fastPlaybackFailures.Clear();
    }
}
