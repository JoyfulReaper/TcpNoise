namespace TcpNoiseClicker;

internal sealed class EventDetailsForm : Form
{
    public EventDetailsForm(HistoryEntry entry)
    {
        var tcpEvent = entry.Event;

        Text = $"tcpnoise event #{tcpEvent.ConnectionNumber}";

        Width = 900;
        Height = 700;
        StartPosition = FormStartPosition.CenterParent;

        var root = new TableLayoutPanel
        {
            Dock = DockStyle.Fill,
            Padding = new Padding(12),
            ColumnCount = 2,
            RowCount = 0,
            AutoScroll = true
        };

        root.ColumnStyles.Add(
            new ColumnStyle(SizeType.AutoSize));

        root.ColumnStyles.Add(
            new ColumnStyle(SizeType.Percent, 100));

        AddField(
            root,
            "Time",
            entry.ReceivedAt.ToString("yyyy-MM-dd HH:mm:ss"));

        AddField(
            root,
            "Sensor",
            tcpEvent.Sensor);

        AddField(
            root,
            "Connection",
            tcpEvent.ConnectionNumber.ToString());

        AddField(
            root,
            "Remote",
            $"{tcpEvent.RemoteAddress}:{tcpEvent.RemotePort}");

        AddField(
            root,
            "Listen port",
            tcpEvent.ListenPort.ToString());

        AddField(
            root,
            "IP version",
            $"IPv{tcpEvent.IpVersion}");

        AddField(
            root,
            "Seen",
            tcpEvent.SeenCount.ToString());

        var bannerStatus = tcpEvent.Banner switch
        {
            null => "<none>",
            _ when tcpEvent.BannerSent =>
                tcpEvent.Banner,
            _ =>
                $"<send failed>\r\n{tcpEvent.Banner}"
        };

        AddMultilineField(
            root,
            "Banner",
            bannerStatus,
            90);

        var payload = tcpEvent.PayloadLength > 0
            ? tcpEvent.Payload
            : "<none>";

        AddMultilineField(
            root,
            $"Payload ({tcpEvent.PayloadLength} bytes)",
            payload,
            300);

        Controls.Add(root);
    }

    private static void AddField(
        TableLayoutPanel panel,
        string label,
        string value)
    {
        var labelControl = new Label
        {
            Text = label,
            AutoSize = true,
            Font = new Font(
                SystemFonts.DefaultFont,
                FontStyle.Bold),
            Margin = new Padding(3, 6, 12, 6)
        };

        var valueControl = new TextBox
        {
            Text = value,
            ReadOnly = true,
            BorderStyle = BorderStyle.None,
            Dock = DockStyle.Fill,
            Margin = new Padding(3, 6, 3, 6)
        };

        var row = panel.RowCount++;

        panel.RowStyles.Add(
            new RowStyle(SizeType.AutoSize));

        panel.Controls.Add(labelControl, 0, row);
        panel.Controls.Add(valueControl, 1, row);
    }

    private static void AddMultilineField(
        TableLayoutPanel panel,
        string label,
        string value,
        int height)
    {
        var labelControl = new Label
        {
            Text = label,
            AutoSize = true,
            Font = new Font(
                SystemFonts.DefaultFont,
                FontStyle.Bold),
            Margin = new Padding(3, 6, 12, 6)
        };

        var valueControl = new TextBox
        {
            Text = value,
            ReadOnly = true,
            Multiline = true,
            ScrollBars = ScrollBars.Both,
            WordWrap = false,
            Font = new Font(
                FontFamily.GenericMonospace,
                10),
            Dock = DockStyle.Fill,
            Height = height,
            Margin = new Padding(3, 6, 3, 6)
        };

        var row = panel.RowCount++;

        panel.RowStyles.Add(
            new RowStyle(SizeType.Absolute, height));

        panel.Controls.Add(labelControl, 0, row);
        panel.Controls.Add(valueControl, 1, row);
    }
}