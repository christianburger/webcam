package main

import (
	"encoding/base64"
	"encoding/json"
	"fmt"
	"log"
	"net/http"
	"os"
	"strconv"
	"time"
)

const boundary = "frame"

// 1x1 white JPEG
const tinyJpegB64 = "/9j/4AAQSkZJRgABAQAAAQABAAD/2wCEAAkGBxAQEBUQEBAVFRUVFRUVFRUVFRUVFRUVFRUWFhUVFRUYHSggGBolGxUVITEhJSkrLi4uFx8zODMsNygtLisBCgoKDg0OFQ8QFS0dFR0tKy0tLS0tKy0tLS0tLS0tLS0tLS0tLS0tLS0tLS0tLS0tLS0tLS0tLS0tLS0tLf/AABEIAAEAAQMBIgACEQEDEQH/xAAbAAEAAgMBAQAAAAAAAAAAAAAABQYBAwQCB//EADUQAAIBAwIDBQgBBQAAAAAAAAECAwAEEQUSITFBBhMiUWFxgZEUMkKhscHR8DJCUv/EABkBAQADAQEAAAAAAAAAAAAAAAABAgMEBf/EACQRAQEAAgICAgMBAAAAAAAAAAABAhEDIRIxBEETIlEiMmFx/9oADAMBAAIRAxEAPwD8nREQBERAEREAREQBERAEREAREQBERAEREAREQH/2Q=="

func main() {
	port := envInt("PORT", 8080)
	mux := http.NewServeMux()

	mux.HandleFunc("/", func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Content-Type", "text/html; charset=utf-8")
		fmt.Fprintf(w, "<html><body><h2>Mock ESP32 origin is up</h2><ul><li><a href='/status'>/status</a></li><li><a href='/capture'>/capture</a></li><li><a href='/stream'>/stream</a></li><li><a href='/hardware'>/hardware</a></li></ul></body></html>")
	})

	mux.HandleFunc("/status", func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Content-Type", "application/json")
		_ = json.NewEncoder(w).Encode(map[string]any{
			"ok":   true,
			"ts":   time.Now().UTC().Format(time.RFC3339),
			"led":  false,
			"sw":   false,
			"heap": 123456,
		})
	})

	mux.HandleFunc("/hardware", func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Content-Type", "application/json")
		_ = json.NewEncoder(w).Encode(map[string]any{
			"camera": true,
			"pan":    true,
			"tilt":   true,
		})
	})

	mux.HandleFunc("/capture", func(w http.ResponseWriter, r *http.Request) {
		jpg, _ := base64.StdEncoding.DecodeString(tinyJpegB64)
		w.Header().Set("Content-Type", "image/jpeg")
		w.WriteHeader(http.StatusOK)
		_, _ = w.Write(jpg)
	})

	mux.HandleFunc("/stream", func(w http.ResponseWriter, r *http.Request) {
		jpg, _ := base64.StdEncoding.DecodeString(tinyJpegB64)
		w.Header().Set("Content-Type", "multipart/x-mixed-replace; boundary="+boundary)
		w.Header().Set("Cache-Control", "no-cache")
		flusher, ok := w.(http.Flusher)
		if !ok {
			http.Error(w, "streaming unsupported", http.StatusInternalServerError)
			return
		}

		t := time.NewTicker(1 * time.Second)
		defer t.Stop()
		for {
			select {
			case <-r.Context().Done():
				return
			case <-t.C:
				_, _ = fmt.Fprintf(w, "--%s\r\nContent-Type: image/jpeg\r\nContent-Length: %d\r\n\r\n", boundary, len(jpg))
				_, _ = w.Write(jpg)
				_, _ = w.Write([]byte("\r\n"))
				flusher.Flush()
			}
		}
	})

	addr := fmt.Sprintf(":%d", port)
	log.Printf("mock origin listening on %s", addr)
	log.Fatal(http.ListenAndServe(addr, mux))
}

func envInt(key string, def int) int {
	v := os.Getenv(key)
	if v == "" {
		return def
	}
	n, err := strconv.Atoi(v)
	if err != nil {
		return def
	}
	return n
}
