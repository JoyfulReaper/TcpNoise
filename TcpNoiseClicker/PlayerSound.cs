using System.Media;

namespace TcpNoiseClicker;

internal sealed class PlayerSound : IEventSound
{
    private readonly SoundPlayer _player;

    private PlayerSound(SoundPlayer player) => _player = player;

    public static IEventSound Load(string path)
    {
        var player = new SoundPlayer(path);
        try
        {
            player.Load();
            return new PlayerSound(player);
        }
        catch
        {
            player.Dispose();
            throw;
        }
    }

    public void PlaySync() => _player.PlaySync();

    public void Dispose() => _player.Dispose();
}
