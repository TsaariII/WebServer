#!/usr/bin/env python3
import socket

HOST = '127.0.0.2'
HOST_NAME = 'dads.com'
PORT = 8004

request = (
    f"GET /images HTTP/1.1\r\n"
    f"Host:     {HOST_NAME}\r\n"
    f"Connection: close\r\n"
    f"\r\n"
)

#GET /images HTTP/1.1\r\nHost: 'dads.fi'\r\nConnection: close\r\n\r\n

def run():
    response_data = b""
    with socket.create_connection((HOST, PORT)) as sock:
        sock.sendall(request.encode())  # ✅ only once
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
