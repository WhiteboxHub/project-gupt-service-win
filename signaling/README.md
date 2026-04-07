# GuPT Signaling Server

A Go-based signaling server for GuPT remote desktop system. Provides host discovery, session management, and NAT traversal coordination.

## Features

- ✅ **Host Registration** - Hosts register and advertise their availability
- ✅ **Client Discovery** - Clients can discover and connect to available hosts
- ✅ **Session Management** - Track active connections and states
- ✅ **Automatic Timeout** - Remove stale sessions automatically
- ✅ **REST API** - Full HTTP REST API for integration
- ✅ **Web UI** - Simple web interface for monitoring
- ✅ **CORS Support** - Cross-origin requests enabled
- ✅ **Statistics** - Real-time server and session statistics

## Quick Start

### Prerequisites

- Go 1.21 or higher
- Internet connection (for downloading dependencies)

### Installation

```bash
cd signaling
go mod download
go build -o gupt-signaling
```

### Run Server

```bash
./gupt-signaling --port 8080 --timeout 60s
```

### Access Web UI

Open your browser and navigate to:
```
http://localhost:8080
```

## Configuration

### Command-Line Options

```bash
./gupt-signaling [options]

Options:
  --port int          Server port (default: 8080)
  --timeout duration  Session timeout duration (default: 60s)
```

### Examples

```bash
# Run on port 3000 with 2-minute timeout
./gupt-signaling --port 3000 --timeout 120s

# Run with default settings
./gupt-signaling
```

## REST API

### Base URL

```
http://localhost:8080/api
```

### Endpoints

#### Host Endpoints

**Register Host**
```http
POST /api/host/register
Content-Type: application/json

{
  "session_id": "unique-session-id",
  "host_name": "My Desktop",
  "host_ip": "192.168.1.100",
  "tcp_port": 5900,
  "udp_port": 5901,
  "resolution": "1920x1080",
  "password_required": true,
  "description": "My main desktop"
}
```

Response:
```json
{
  "success": true,
  "message": "Host registered successfully",
  "data": {
    "session_id": "unique-session-id",
    "host_name": "My Desktop",
    "state": "waiting",
    ...
  }
}
```

**Send Heartbeat**
```http
POST /api/host/heartbeat
Content-Type: application/json

{
  "session_id": "unique-session-id"
}
```

**Disconnect Host**
```http
POST /api/host/disconnect
Content-Type: application/json

{
  "session_id": "unique-session-id"
}
```

#### Client Endpoints

**Connect to Host**
```http
POST /api/client/connect
Content-Type: application/json

{
  "session_id": "unique-session-id",
  "client_id": "client_123"
}
```

Response:
```json
{
  "success": true,
  "message": "Connected to host",
  "data": {
    "session_id": "unique-session-id",
    "host_ip": "192.168.1.100",
    "tcp_port": 5900,
    "udp_port": 5901,
    ...
  }
}
```

**Disconnect Client**
```http
POST /api/client/disconnect
Content-Type: application/json

{
  "session_id": "unique-session-id"
}
```

#### Query Endpoints

**List All Hosts**
```http
GET /api/hosts
```

Response:
```json
{
  "success": true,
  "data": [
    {
      "session_id": "session-1",
      "host_name": "Desktop 1",
      "state": "waiting",
      ...
    },
    {
      "session_id": "session-2",
      "host_name": "Desktop 2",
      "state": "connected",
      ...
    }
  ]
}
```

**Get Host Details**
```http
GET /api/host/{sessionId}
```

**Get Server Statistics**
```http
GET /api/stats
```

Response:
```json
{
  "success": true,
  "data": {
    "total_sessions": 5,
    "waiting": 3,
    "connected": 2,
    "busy": 0
  }
}
```

**Server Status**
```http
GET /api/status
```

Response:
```json
{
  "success": true,
  "data": {
    "status": "ok",
    "version": "1.0.0",
    "time": "2026-04-07T12:00:00Z"
  }
}
```

**Health Check**
```http
GET /health
```

Response: `OK` (200 status)

## Session States

- **waiting** - Host is registered and available for connections
- **connected** - Client is connected to the host
- **busy** - Host is busy with another client
- **offline** - Host has timed out or disconnected

## Architecture

```
┌─────────────────────────────────────────────────────────┐
│                  Signaling Server                        │
├─────────────────────────────────────────────────────────┤
│                                                           │
│  ┌─────────────────────────────────────────────┐        │
│  │           HTTP/REST API Server               │        │
│  │          (Port 8080, Gorilla Mux)            │        │
│  └─────────────────┬───────────────────────────┘        │
│                    │                                      │
│         ┌──────────┴──────────┐                         │
│         ▼                      ▼                         │
│  ┌─────────────┐      ┌──────────────┐                 │
│  │  API Server │      │  Static Web  │                 │
│  │  (Routes)   │      │  File Server │                 │
│  └──────┬──────┘      └──────────────┘                 │
│         │                                                │
│         ▼                                                │
│  ┌─────────────────────────────┐                       │
│  │     Session Manager         │                       │
│  │  (In-Memory Store + Mutex)  │                       │
│  └─────────────────────────────┘                       │
│         │                                                │
│         ├─ Register Host                                │
│         ├─ Update Heartbeat                             │
│         ├─ Connect Client                               │
│         ├─ List Hosts                                   │
│         └─ Cleanup Timeout Sessions                     │
│                                                           │
└─────────────────────────────────────────────────────────┘

Usage Flow:
1. Host registers with server (POST /api/host/register)
2. Host sends periodic heartbeats (POST /api/host/heartbeat)
3. Client queries available hosts (GET /api/hosts)
4. Client connects to host (POST /api/client/connect)
5. Server provides host connection details
6. Client connects directly to host (peer-to-peer)
```

## Integration with GuPT

### Host Application Integration

```cpp
// In host/main.cpp or dedicated signaling client

// Register with signaling server
void RegisterWithSignalingServer() {
    std::string sessionId = GenerateSessionId();
    std::string signalingUrl = "http://signaling.example.com:8080";

    // Register
    json payload = {
        {"session_id", sessionId},
        {"host_name", GetHostName()},
        {"host_ip", GetPublicIP()},
        {"tcp_port", 5900},
        {"udp_port", 5901},
        {"resolution", "1920x1080"},
        {"password_required", true}
    };

    HttpPost(signalingUrl + "/api/host/register", payload);

    // Send heartbeats every 30 seconds
    while (running) {
        std::this_thread::sleep_for(std::chrono::seconds(30));
        json heartbeat = {{"session_id", sessionId}};
        HttpPost(signalingUrl + "/api/host/heartbeat", heartbeat);
    }
}
```

### Client Application Integration

```cpp
// In client/main.cpp

// Discover and connect to host
void ConnectViaSignaling() {
    std::string signalingUrl = "http://signaling.example.com:8080";

    // List available hosts
    auto hosts = HttpGet(signalingUrl + "/api/hosts");

    // Display hosts to user
    DisplayHostList(hosts);

    // User selects host
    std::string selectedSessionId = GetUserSelection();

    // Connect to host
    json payload = {
        {"session_id", selectedSessionId},
        {"client_id", GenerateClientId()}
    };

    auto result = HttpPost(signalingUrl + "/api/client/connect", payload);

    // Extract host connection details
    std::string hostIp = result["data"]["host_ip"];
    int tcpPort = result["data"]["tcp_port"];

    // Connect directly to host
    ConnectToHost(hostIp, tcpPort);
}
```

## Deployment

### Development

```bash
go run *.go --port 8080
```

### Production

```bash
# Build
go build -o gupt-signaling

# Run with systemd (example)
sudo systemctl start gupt-signaling

# Run with Docker
docker build -t gupt-signaling .
docker run -p 8080:8080 gupt-signaling
```

### Environment Variables

```bash
export GUPT_PORT=8080
export GUPT_TIMEOUT=60s
./gupt-signaling
```

## Security Considerations

1. **No Authentication** - Current implementation has no authentication. Add API keys or JWT tokens for production.
2. **HTTPS** - Use HTTPS in production with TLS certificates.
3. **Rate Limiting** - Add rate limiting to prevent abuse.
4. **Firewall** - Configure firewall rules to restrict access.
5. **Input Validation** - All inputs are validated, but add additional checks as needed.

## Monitoring

### Logs

The server logs important events:
```
2026/04/07 12:00:00 === GuPT Signaling Server ===
2026/04/07 12:00:00 Starting server on port 8080
2026/04/07 12:00:00 Session timeout: 1m0s
2026/04/07 12:00:00 Web UI: http://localhost:8080
2026/04/07 12:00:00 API: http://localhost:8080/api
2026/04/07 12:00:05 Host registered: My Desktop (session-123)
2026/04/07 12:00:10 Client connected: client-456 -> session-123
```

### Statistics

Access real-time statistics:
```bash
curl http://localhost:8080/api/stats
```

### Health Check

Monitor server health:
```bash
curl http://localhost:8080/health
```

## Troubleshooting

### Server Won't Start

```bash
# Check if port is already in use
lsof -i :8080

# Try different port
./gupt-signaling --port 9000
```

### Sessions Timing Out Too Quickly

```bash
# Increase timeout duration
./gupt-signaling --timeout 300s
```

### CORS Issues

CORS is enabled by default for all origins. Modify `server.go` if you need to restrict origins:

```go
c := cors.New(cors.Options{
    AllowedOrigins: []string{"https://your-domain.com"},
    ...
})
```

## Future Enhancements

- [ ] WebSocket support for real-time updates
- [ ] Authentication and authorization
- [ ] Persistent storage (Redis, PostgreSQL)
- [ ] STUN/TURN server integration for NAT traversal
- [ ] ICE candidate exchange for WebRTC-style connections
- [ ] Metrics and Prometheus integration
- [ ] Docker container and Kubernetes deployment
- [ ] Load balancing for multiple instances

## License

MIT License

## Contributing

Contributions are welcome! Please open an issue or submit a pull request.

## Support

For issues and questions, please open an issue on GitHub.
