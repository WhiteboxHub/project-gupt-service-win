package main

import (
	"sync"
	"time"
)

// SessionState represents the state of a session
type SessionState string

const (
	StateWaiting   SessionState = "waiting"   // Host waiting for client
	StateConnected SessionState = "connected" // Client connected
	StateBusy      SessionState = "busy"      // Host busy with another client
	StateOffline   SessionState = "offline"   // Host offline
)

// HostInfo represents information about a host
type HostInfo struct {
	SessionID   string       `json:"session_id"`
	HostName    string       `json:"host_name"`
	HostIP      string       `json:"host_ip"`
	TCPPort     int          `json:"tcp_port"`
	UDPPort     int          `json:"udp_port"`
	State       SessionState `json:"state"`
	Resolution  string       `json:"resolution"`
	Password    bool         `json:"password_required"`
	LastSeen    time.Time    `json:"last_seen"`
	CreatedAt   time.Time    `json:"created_at"`
	ClientID    string       `json:"client_id,omitempty"`
	ClientIP    string       `json:"client_ip,omitempty"`
	Description string       `json:"description,omitempty"`
}

// SessionManager manages all active sessions
type SessionManager struct {
	mu       sync.RWMutex
	sessions map[string]*HostInfo
	timeout  time.Duration
}

// NewSessionManager creates a new session manager
func NewSessionManager(timeout time.Duration) *SessionManager {
	sm := &SessionManager{
		sessions: make(map[string]*HostInfo),
		timeout:  timeout,
	}

	// Start cleanup goroutine
	go sm.cleanupLoop()

	return sm
}

// RegisterHost registers or updates a host
func (sm *SessionManager) RegisterHost(info *HostInfo) {
	sm.mu.Lock()
	defer sm.mu.Unlock()

	if existing, ok := sm.sessions[info.SessionID]; ok {
		// Update existing session
		existing.HostName = info.HostName
		existing.HostIP = info.HostIP
		existing.TCPPort = info.TCPPort
		existing.UDPPort = info.UDPPort
		existing.Resolution = info.Resolution
		existing.Password = info.Password
		existing.Description = info.Description
		existing.LastSeen = time.Now()

		// Don't change state if already connected
		if existing.State != StateConnected {
			existing.State = StateWaiting
		}
	} else {
		// Create new session
		info.State = StateWaiting
		info.CreatedAt = time.Now()
		info.LastSeen = time.Now()
		sm.sessions[info.SessionID] = info
	}
}

// UpdateHostHeartbeat updates the last seen time for a host
func (sm *SessionManager) UpdateHostHeartbeat(sessionID string) bool {
	sm.mu.Lock()
	defer sm.mu.Unlock()

	if session, ok := sm.sessions[sessionID]; ok {
		session.LastSeen = time.Now()
		return true
	}
	return false
}

// ConnectClient connects a client to a host
func (sm *SessionManager) ConnectClient(sessionID, clientID, clientIP string) (*HostInfo, error) {
	sm.mu.Lock()
	defer sm.mu.Unlock()

	session, ok := sm.sessions[sessionID]
	if !ok {
		return nil, &SessionError{Code: "session_not_found", Message: "Session not found"}
	}

	if session.State == StateConnected {
		return nil, &SessionError{Code: "session_busy", Message: "Host is busy with another client"}
	}

	if session.State == StateOffline {
		return nil, &SessionError{Code: "host_offline", Message: "Host is offline"}
	}

	// Connect client
	session.ClientID = clientID
	session.ClientIP = clientIP
	session.State = StateConnected
	session.LastSeen = time.Now()

	return session, nil
}

// DisconnectClient disconnects a client from a host
func (sm *SessionManager) DisconnectClient(sessionID string) bool {
	sm.mu.Lock()
	defer sm.mu.Unlock()

	if session, ok := sm.sessions[sessionID]; ok {
		session.ClientID = ""
		session.ClientIP = ""
		session.State = StateWaiting
		return true
	}
	return false
}

// GetHost retrieves a host by session ID
func (sm *SessionManager) GetHost(sessionID string) (*HostInfo, bool) {
	sm.mu.RLock()
	defer sm.mu.RUnlock()

	session, ok := sm.sessions[sessionID]
	if !ok {
		return nil, false
	}

	// Return a copy to avoid race conditions
	copy := *session
	return &copy, true
}

// ListHosts returns all active hosts
func (sm *SessionManager) ListHosts() []*HostInfo {
	sm.mu.RLock()
	defer sm.mu.RUnlock()

	hosts := make([]*HostInfo, 0, len(sm.sessions))
	for _, session := range sm.sessions {
		if session.State != StateOffline {
			// Return a copy
			copy := *session
			hosts = append(hosts, &copy)
		}
	}

	return hosts
}

// RemoveHost removes a host from the session manager
func (sm *SessionManager) RemoveHost(sessionID string) bool {
	sm.mu.Lock()
	defer sm.mu.Unlock()

	if _, ok := sm.sessions[sessionID]; ok {
		delete(sm.sessions, sessionID)
		return true
	}
	return false
}

// cleanupLoop periodically removes timed-out sessions
func (sm *SessionManager) cleanupLoop() {
	ticker := time.NewTicker(30 * time.Second)
	defer ticker.Stop()

	for range ticker.C {
		sm.cleanup()
	}
}

// cleanup removes timed-out sessions
func (sm *SessionManager) cleanup() {
	sm.mu.Lock()
	defer sm.mu.Unlock()

	now := time.Now()
	for sessionID, session := range sm.sessions {
		if now.Sub(session.LastSeen) > sm.timeout {
			delete(sm.sessions, sessionID)
		}
	}
}

// GetStats returns session statistics
func (sm *SessionManager) GetStats() map[string]interface{} {
	sm.mu.RLock()
	defer sm.mu.RUnlock()

	waiting := 0
	connected := 0
	busy := 0

	for _, session := range sm.sessions {
		switch session.State {
		case StateWaiting:
			waiting++
		case StateConnected:
			connected++
		case StateBusy:
			busy++
		}
	}

	return map[string]interface{}{
		"total_sessions": len(sm.sessions),
		"waiting":        waiting,
		"connected":      connected,
		"busy":           busy,
	}
}

// SessionError represents a session-related error
type SessionError struct {
	Code    string `json:"code"`
	Message string `json:"message"`
}

func (e *SessionError) Error() string {
	return e.Message
}
