package main

import (
	"encoding/json"
	"log"
	"net/http"
	"time"

	"github.com/gorilla/mux"
)

// APIServer handles HTTP requests
type APIServer struct {
	sessionManager *SessionManager
}

// NewAPIServer creates a new API server
func NewAPIServer(sm *SessionManager) *APIServer {
	return &APIServer{
		sessionManager: sm,
	}
}

// RegisterRoutes registers all API routes
func (api *APIServer) RegisterRoutes(router *mux.Router) {
	// Host endpoints
	router.HandleFunc("/api/host/register", api.handleHostRegister).Methods("POST")
	router.HandleFunc("/api/host/heartbeat", api.handleHostHeartbeat).Methods("POST")
	router.HandleFunc("/api/host/disconnect", api.handleHostDisconnect).Methods("POST")

	// Client endpoints
	router.HandleFunc("/api/client/connect", api.handleClientConnect).Methods("POST")
	router.HandleFunc("/api/client/disconnect", api.handleClientDisconnect).Methods("POST")

	// Query endpoints
	router.HandleFunc("/api/hosts", api.handleListHosts).Methods("GET")
	router.HandleFunc("/api/host/{sessionId}", api.handleGetHost).Methods("GET")

	// Status endpoint
	router.HandleFunc("/api/status", api.handleStatus).Methods("GET")
	router.HandleFunc("/api/stats", api.handleStats).Methods("GET")

	// Health check
	router.HandleFunc("/health", api.handleHealth).Methods("GET")
}

// Request/Response structures

type RegisterHostRequest struct {
	SessionID   string `json:"session_id"`
	HostName    string `json:"host_name"`
	HostIP      string `json:"host_ip"`
	TCPPort     int    `json:"tcp_port"`
	UDPPort     int    `json:"udp_port"`
	Resolution  string `json:"resolution"`
	Password    bool   `json:"password_required"`
	Description string `json:"description,omitempty"`
}

type HeartbeatRequest struct {
	SessionID string `json:"session_id"`
}

type ConnectClientRequest struct {
	SessionID string `json:"session_id"`
	ClientID  string `json:"client_id"`
}

type DisconnectRequest struct {
	SessionID string `json:"session_id"`
}

type APIResponse struct {
	Success bool        `json:"success"`
	Message string      `json:"message,omitempty"`
	Data    interface{} `json:"data,omitempty"`
	Error   string      `json:"error,omitempty"`
}

// Handler functions

func (api *APIServer) handleHostRegister(w http.ResponseWriter, r *http.Request) {
	var req RegisterHostRequest
	if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
		api.respondError(w, http.StatusBadRequest, "Invalid request body")
		return
	}

	// Validate required fields
	if req.SessionID == "" || req.HostName == "" {
		api.respondError(w, http.StatusBadRequest, "Missing required fields")
		return
	}

	// Use remote address if host IP not provided
	if req.HostIP == "" {
		req.HostIP = r.RemoteAddr
	}

	// Create host info
	hostInfo := &HostInfo{
		SessionID:   req.SessionID,
		HostName:    req.HostName,
		HostIP:      req.HostIP,
		TCPPort:     req.TCPPort,
		UDPPort:     req.UDPPort,
		Resolution:  req.Resolution,
		Password:    req.Password,
		Description: req.Description,
	}

	// Register host
	api.sessionManager.RegisterHost(hostInfo)

	log.Printf("Host registered: %s (%s)", req.HostName, req.SessionID)

	api.respondSuccess(w, "Host registered successfully", hostInfo)
}

func (api *APIServer) handleHostHeartbeat(w http.ResponseWriter, r *http.Request) {
	var req HeartbeatRequest
	if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
		api.respondError(w, http.StatusBadRequest, "Invalid request body")
		return
	}

	if req.SessionID == "" {
		api.respondError(w, http.StatusBadRequest, "Missing session_id")
		return
	}

	if !api.sessionManager.UpdateHostHeartbeat(req.SessionID) {
		api.respondError(w, http.StatusNotFound, "Session not found")
		return
	}

	api.respondSuccess(w, "Heartbeat updated", nil)
}

func (api *APIServer) handleHostDisconnect(w http.ResponseWriter, r *http.Request) {
	var req DisconnectRequest
	if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
		api.respondError(w, http.StatusBadRequest, "Invalid request body")
		return
	}

	if req.SessionID == "" {
		api.respondError(w, http.StatusBadRequest, "Missing session_id")
		return
	}

	if !api.sessionManager.RemoveHost(req.SessionID) {
		api.respondError(w, http.StatusNotFound, "Session not found")
		return
	}

	log.Printf("Host disconnected: %s", req.SessionID)

	api.respondSuccess(w, "Host disconnected", nil)
}

func (api *APIServer) handleClientConnect(w http.ResponseWriter, r *http.Request) {
	var req ConnectClientRequest
	if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
		api.respondError(w, http.StatusBadRequest, "Invalid request body")
		return
	}

	if req.SessionID == "" || req.ClientID == "" {
		api.respondError(w, http.StatusBadRequest, "Missing required fields")
		return
	}

	clientIP := r.RemoteAddr

	hostInfo, err := api.sessionManager.ConnectClient(req.SessionID, req.ClientID, clientIP)
	if err != nil {
		if sessionErr, ok := err.(*SessionError); ok {
			api.respondError(w, http.StatusBadRequest, sessionErr.Message)
		} else {
			api.respondError(w, http.StatusInternalServerError, err.Error())
		}
		return
	}

	log.Printf("Client connected: %s -> %s", req.ClientID, req.SessionID)

	api.respondSuccess(w, "Connected to host", hostInfo)
}

func (api *APIServer) handleClientDisconnect(w http.ResponseWriter, r *http.Request) {
	var req DisconnectRequest
	if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
		api.respondError(w, http.StatusBadRequest, "Invalid request body")
		return
	}

	if req.SessionID == "" {
		api.respondError(w, http.StatusBadRequest, "Missing session_id")
		return
	}

	if !api.sessionManager.DisconnectClient(req.SessionID) {
		api.respondError(w, http.StatusNotFound, "Session not found")
		return
	}

	log.Printf("Client disconnected from: %s", req.SessionID)

	api.respondSuccess(w, "Client disconnected", nil)
}

func (api *APIServer) handleListHosts(w http.ResponseWriter, r *http.Request) {
	hosts := api.sessionManager.ListHosts()
	api.respondSuccess(w, "", hosts)
}

func (api *APIServer) handleGetHost(w http.ResponseWriter, r *http.Request) {
	vars := mux.Vars(r)
	sessionID := vars["sessionId"]

	if sessionID == "" {
		api.respondError(w, http.StatusBadRequest, "Missing session_id")
		return
	}

	host, ok := api.sessionManager.GetHost(sessionID)
	if !ok {
		api.respondError(w, http.StatusNotFound, "Session not found")
		return
	}

	api.respondSuccess(w, "", host)
}

func (api *APIServer) handleStatus(w http.ResponseWriter, r *http.Request) {
	status := map[string]interface{}{
		"status":  "ok",
		"version": "1.0.0",
		"time":    time.Now().Format(time.RFC3339),
	}
	api.respondSuccess(w, "", status)
}

func (api *APIServer) handleStats(w http.ResponseWriter, r *http.Request) {
	stats := api.sessionManager.GetStats()
	api.respondSuccess(w, "", stats)
}

func (api *APIServer) handleHealth(w http.ResponseWriter, r *http.Request) {
	w.WriteHeader(http.StatusOK)
	w.Write([]byte("OK"))
}

// Helper functions

func (api *APIServer) respondSuccess(w http.ResponseWriter, message string, data interface{}) {
	response := APIResponse{
		Success: true,
		Message: message,
		Data:    data,
	}
	api.respondJSON(w, http.StatusOK, response)
}

func (api *APIServer) respondError(w http.ResponseWriter, status int, message string) {
	response := APIResponse{
		Success: false,
		Error:   message,
	}
	api.respondJSON(w, status, response)
}

func (api *APIServer) respondJSON(w http.ResponseWriter, status int, data interface{}) {
	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(status)
	json.NewEncoder(w).Encode(data)
}
