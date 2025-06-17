#!/usr/bin/env python3

import socket

HOST = '127.0.0.2'
PORT = 8004
PATH = '/cgi/test.py'

# Generate ~512KB body (large enough to not fit in one write)
body = "A" * (10 * 1)
request = (
    f"POST {PATH} HTTP/1.1\r\n"
    f"Host: {HOST}\r\n"
    f"Content-Length: {len(body)}\r\n"
    f"Content-Type: text/plain\r\n"
    f"Connection: keep-alive\r\n"
    f"\r\n"
    f"{body}"
)

def run():
    with socket.create_connection((HOST, PORT)) as sock:
        sock.sendall(request.encode())
        response_data = b""
        total = 0
        while True:
            chunk = sock.recv(8192)
            if not chunk:
                break
            total += len(chunk)
            response_data += chunk
            print(f"📦 Received {len(chunk)} bytes, total: {total}")
        print(f"\n✅ Final total received: {total / 1024:.1f} KB")
        print("\n🔽 Response:")
        print(response_data.decode(errors="replace"))

if __name__ == "__main__":
    run()
