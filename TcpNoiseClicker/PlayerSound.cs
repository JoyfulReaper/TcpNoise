using System.Media;

namespace TcpNoiseClicker;

internal sealed class PlayerSound : IEventSound, IFastEventSound
{
    private readonly SoundPlayer _player;
    private readonly Lazy<byte[]> _fastWaveData;

    private PlayerSound(SoundPlayer player, string path)
    {
        _player = player;
        _fastWaveData = new(() => File.ReadAllBytes(path));
    }

    public static IEventSound Load(string path)
    {
        var player = new SoundPlayer(path);
        try
        {
            player.Load();
            return new PlayerSound(player, path);
        }
        catch
        {
            player.Dispose();
            throw;
        }
    }

    public void PlaySync() => _player.PlaySync();

    public IActiveSound StartPlayback() => WavePlayback.Start(_fastWaveData.Value);

    public static void StopSerialPlayback()
    {
        using var player = new SoundPlayer();
        player.Stop();
    }

    public void Dispose() => _player.Dispose();
}
