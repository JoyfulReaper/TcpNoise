namespace TcpNoiseClicker;

internal sealed class HistoryForm : Form
{
    private readonly DataGridView _grid;

    public HistoryForm(IReadOnlyList<HistoryEntry> history)
    {
        Text = "tcpnoise history";

        Width = 1200;
        Height = 600;
        StartPosition = FormStartPosition.CenterScreen;

        _grid = new DataGridView
        {
            Dock = DockStyle.Fill,
            ReadOnly = true,
            AllowUserToAddRows = false,
            AllowUserToDeleteRows = false,
            AllowUserToResizeRows = false,
            RowHeadersVisible = false,
            SelectionMode = DataGridViewSelectionMode.FullRowSelect,
            MultiSelect = false,
            AutoGenerateColumns = false,
            AutoSizeRowsMode = DataGridViewAutoSizeRowsMode.AllCells
        };

        _grid.Columns.Add(new DataGridViewTextBoxColumn
        {
            Name = "Time",
            HeaderText = "Time",
            Width = 90
        });

        _grid.Columns.Add(new DataGridViewTextBoxColumn
        {
            Name = "Sensor",
            HeaderText = "Sensor",
            Width = 110
        });

        _grid.Columns.Add(new DataGridViewTextBoxColumn
        {
            Name = "Remote",
            HeaderText = "Remote",
            Width = 240
        });

        _grid.Columns.Add(new DataGridViewTextBoxColumn
        {
            Name = "ListenPort",
            HeaderText = "Port",
            Width = 70
        });

        _grid.Columns.Add(new DataGridViewTextBoxColumn
        {
            Name = "IpVersion",
            HeaderText = "IP",
            Width = 50
        });

        _grid.Columns.Add(new DataGridViewTextBoxColumn
        {
            Name = "Seen",
            HeaderText = "Seen",
            Width = 65
        });

        _grid.Columns.Add(new DataGridViewTextBoxColumn
        {
            Name = "Banner",
            HeaderText = "Banner",
            Width = 280,
            DefaultCellStyle =
            {
                WrapMode = DataGridViewTriState.True
            }
        });

        _grid.Columns.Add(new DataGridViewTextBoxColumn
        {
            Name = "Payload",
            HeaderText = "Payload",
            AutoSizeMode = DataGridViewAutoSizeColumnMode.Fill,
            MinimumWidth = 250,
            DefaultCellStyle =
            {
                WrapMode = DataGridViewTriState.True
            }
        });

        Controls.Add(_grid);

        RefreshHistory(history);
    }

    public void RefreshHistory(
        IReadOnlyList<HistoryEntry> history)
    {
        _grid.Rows.Clear();

        foreach (var entry in history)
        {
            var tcpEvent = entry.Event;

            var banner = tcpEvent.Banner switch
            {
                null => "<none>",
                _ when tcpEvent.BannerSent =>
                    tcpEvent.Banner,
                _ =>
                    $"<send failed> {tcpEvent.Banner}"
            };

            var payload = tcpEvent.PayloadLength > 0
                ? tcpEvent.Payload
                : "<none>";

            _grid.Rows.Add(
                entry.ReceivedAt.ToString("HH:mm:ss"),
                tcpEvent.Sensor,
                $"{tcpEvent.RemoteAddress}:{tcpEvent.RemotePort}",
                tcpEvent.ListenPort,
                $"v{tcpEvent.IpVersion}",
                tcpEvent.SeenCount,
                banner,
                payload);
        }
    }
}