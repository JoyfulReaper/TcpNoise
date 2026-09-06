sqlite3 -header -box tcpnoise.db 'SELECT port, ip_version, connection_count FROM port_activity ORDER BY connection_count DESC, port, ip_version'

echo "Hall of shame"
sqlite3 -header -box tcpnoise.db \
'SELECT address, seen_count, first_seen_utc, last_seen_utc
 FROM seen_ip
 ORDER BY seen_count DESC
 LIMIT 1;'

echo "10 most seen"
sqlite3 -header -box tcpnoise.db \
'SELECT address, seen_count, first_seen_utc, last_seen_utc
 FROM seen_ip
 ORDER BY seen_count DESC
 LIMIT 10;'

echo "10 most recently seen"
sqlite3 -header -box tcpnoise.db \
'SELECT address, seen_count, first_seen_utc, last_seen_utc
 FROM seen_ip
 ORDER BY last_seen_utc DESC
 LIMIT 10;'

echo "Any IPv6?"
grep IPv6 *.log | tail
