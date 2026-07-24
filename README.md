# VsDB
A lightweight database visualization

## Saved PostgreSQL connections

After a connection succeeds, VsDB keeps its host, port, user, database, SSL mode,
and timeout in the local application settings. On Windows, the password is stored
separately in Windows Credential Manager and is never written to the settings file.

Saved databases are restored in the left Connections panel on the next launch.
Double-click one to reconnect with its saved credentials. If reconnecting fails,
VsDB reopens the connection form with the previous values so the password, URL,
port, user, or database can be corrected.
