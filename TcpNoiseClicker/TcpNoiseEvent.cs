namespace TcpNoiseClicker;

internal sealed record TcpNoiseEventEnvelope(
    TcpNoisePayload Payload);

internal sealed record TcpNoisePayload(
    ulong ConnectionNumber,
    ushort ListenPort,
    int IpVersion,
    string RemoteAddress,
    ushort RemotePort,
    ulong SeenCount);