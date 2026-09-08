namespace TcpNoiseClicker;

internal sealed record TcpNoiseEventEnvelope(
    TcpNoisePayload Payload);

internal sealed record TcpNoisePayload(
    string Sensor,
    ulong ConnectionNumber,
    ushort ListenPort,
    int IpVersion,
    string RemoteAddress,
    ushort RemotePort,
    ulong SeenCount,
    string? Banner,
    bool BannerSent,
    int PayloadLength,
    string Payload);