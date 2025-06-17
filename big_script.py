#!/usr/bin/env python3

import socket

HOST = '127.0.0.2'
PORT = 8004
CGI_PATH = '/cgi/test.py'

# Create a large POST body (e.g. 1MB)
BODY_SIZE = 10 * 1024  # 1 MB
body = "0" * BODY_SIZE

# Construct raw HTTP POST request
request = (
    f"GET {CGI_PATH} HTTP/1.1\r\n"
    f"Host: {HOST}\r\n"
    f"Connection: close\r\n"
    f"\r\n\r\n\r\n"
)

def run():
    print(f"📤 Sending {len(body) / 1024:.1f} KB body to CGI script...")
    response_data = b""
    with socket.create_connection((HOST, PORT)) as sock:
        sock.sendall(request.encode())
        total = 0
        while True:
            chunk = sock.recv(8192)
            if not chunk:
                break
            response_data += chunk
            total += len(chunk)
            print(f"📦 Received {len(chunk)} bytes, total: {total}")
    
    print(f"\n✅ Final total received: {total / 1024:.1f} KB")
    print("\n🔽 Response:")
    print(response_data.decode(errors="replace"))

if __name__ == "__main__":
    run()
