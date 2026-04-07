package main

import (
	"flag"
	"fmt"
	"log"
	"net/http"
	"os"
	"os/signal"
	"syscall"
	"time"

	"github.com/gorilla/mux"
	"github.com/rs/cors"
)

const (
	DefaultPort    = 8080
	DefaultTimeout = 60 * time.Second
)

// Server represents the signaling server
type Server struct {
	port           int
	sessionManager *SessionManager
	apiServer      *APIServer
	httpServer     *http.Server
}

// NewServer creates a new signaling server
func NewServer(port int, timeout time.Duration) *Server {
	sessionManager := NewSessionManager(timeout)
	apiServer := NewAPIServer(sessionManager)

	return &Server{
		port:           port,
		sessionManager: sessionManager,
		apiServer:      apiServer,
	}
}

// Start starts the signaling server
func (s *Server) Start() error {
	// Create router
	router := mux.NewRouter()

	// Register API routes
	s.apiServer.RegisterRoutes(router)

	// Serve static files (web UI)
	router.PathPrefix("/").Handler(http.FileServer(http.Dir("./web")))

	// Setup CORS
	c := cors.New(cors.Options{
		AllowedOrigins:   []string{"*"},
		AllowedMethods:   []string{"GET", "POST", "PUT", "DELETE", "OPTIONS"},
		AllowedHeaders:   []string{"*"},
		AllowCredentials: true,
	})

	handler := c.Handler(router)

	// Create HTTP server
	s.httpServer = &http.Server{
		Addr:           fmt.Sprintf(":%d", s.port),
		Handler:        handler,
		ReadTimeout:    10 * time.Second,
		WriteTimeout:   10 * time.Second,
		MaxHeaderBytes: 1 << 20,
	}

	// Start server
	log.Printf("=== GuPT Signaling Server ===")
	log.Printf("Starting server on port %d", s.port)
	log.Printf("Session timeout: %v", s.sessionManager.timeout)
	log.Printf("Web UI: http://localhost:%d", s.port)
	log.Printf("API: http://localhost:%d/api", s.port)

	return s.httpServer.ListenAndServe()
}

// Stop stops the signaling server
func (s *Server) Stop() error {
	if s.httpServer != nil {
		log.Println("Shutting down server...")
		return s.httpServer.Close()
	}
	return nil
}

func main() {
	// Parse command-line flags
	port := flag.Int("port", DefaultPort, "Server port")
	timeout := flag.Duration("timeout", DefaultTimeout, "Session timeout duration")
	flag.Parse()

	// Create server
	server := NewServer(*port, *timeout)

	// Handle graceful shutdown
	sigChan := make(chan os.Signal, 1)
	signal.Notify(sigChan, os.Interrupt, syscall.SIGTERM)

	// Start server in goroutine
	go func() {
		if err := server.Start(); err != nil && err != http.ErrServerClosed {
			log.Fatalf("Server error: %v", err)
		}
	}()

	// Wait for interrupt signal
	<-sigChan
	log.Println("\nReceived interrupt signal")

	// Graceful shutdown
	if err := server.Stop(); err != nil {
		log.Printf("Error stopping server: %v", err)
	}

	log.Println("Server stopped")
}
