#!/usr/bin/env python3
"""
Minimal HTTP server for GD-ML-Bot visualizer.
Serves state.json and visualizer.html from G:/gd-ml-bot/
Access: http://localhost:8888
"""
import http.server
import os

PORT = 8888
ROOT = r"G:\gd-ml-bot"

class Handler(http.server.SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=ROOT, **kwargs)

    def log_message(self, fmt, *args):
        pass  # Silent - no log spam

    def end_headers(self):
        # Allow local file access, disable caching for state.json
        self.send_header("Access-Control-Allow-Origin", "*")
        if self.path.startswith("/state.json"):
            self.send_header("Cache-Control", "no-store, no-cache, must-revalidate")
            self.send_header("Pragma", "no-cache")
        super().end_headers()

if __name__ == "__main__":
    os.chdir(ROOT)
    with http.server.HTTPServer(("localhost", PORT), Handler) as httpd:
        print(f"[GD-Visualizer] Server running at http://localhost:{PORT}")
        print(f"[GD-Visualizer] Serving files from {ROOT}")
        print(f"[GD-Visualizer] Open http://localhost:{PORT}/visualizer.html in your browser")
        print(f"[GD-Visualizer] Press Ctrl+C to stop")
        try:
            httpd.serve_forever()
        except KeyboardInterrupt:
            print("\n[GD-Visualizer] Server stopped.")
