namespace TcpNoiseClicker;

internal sealed record HistoryEntry(
    DateTime ReceivedAt,
    TcpNoisePayload Event);