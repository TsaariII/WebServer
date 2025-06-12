#!/usr/bin/env python3
import socket

HOST = '127.0.0.2'
HOST_NAME = 'dads.com'
HOST_NAME2 = 'dads.fi'
HOST_NAME3 = 'dads.hu'
PORT = 8004

request1 = (
    f"GET /images HTTP/1.1\r\n"
    f"Host:{HOST_NAME}\r\n"
    f"Connection: close\r\n"
    f"\r\n"
)

request2 = (
    f"GET /images HTTP/1.1\r\n"
    f"Host: {HOST_NAME2}\r\n"
    f"Connection: close\r\n"
    f"\r\n"
)

request3 = (
    f"GET /images HTTP/1.1\r\n"
    f"Host:     {HOST_NAME3}\r\n"
    f"Connection: close\r\n"
    f"\r\n"
)

def run():
    print("\n\n\n---FIRST TEST with dads.com---")
    response_data = b""
    with socket.create_connection((HOST, PORT)) as sock:
        sock.sendall(request1.encode())
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

    print("\n\n\n---2ND TEST with dads.fi---")
    response_data = b""
    with socket.create_connection((HOST, PORT)) as sock:
        sock.sendall(request2.encode())
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

    print("\n\n\n---3RD TEST with dads.hu---")
    response_data = b""
    with socket.create_connection((HOST, PORT)) as sock:
        sock.sendall(request3.encode())
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
