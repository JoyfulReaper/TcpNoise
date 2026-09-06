sqlite3 -header -box tcpnoise.db 'SELECT port, ip_version, connection_count FROM port_activity ORDER BY connection_count DESC, port, ip_version'

echo "Any IPv6?"
grep IPv6 *.log | tail
