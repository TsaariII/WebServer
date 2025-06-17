#!/usr/bin/env python3
import socket

def send_raw_request(raw_request):
    with socket.create_connection(("127.0.0.2", 8004)) as sock:
        print(f"--- Sending ---\n{raw_request}")
        sock.sendall(raw_request.encode())
        response = sock.recv(4096)
        print("--- Response ---")
        try:
            print(response.decode('utf-8'))
        except UnicodeDecodeError as e:
            print(f"[!] Decode error: {e}")
            print("[!] Raw bytes:")
            print(response)
        print("\n====================\n")


# Valid GET
valid_get = (
    "GET / HTTP/1.1\r\n"
    "Host: tests.com\r\n"
    "\r\n"
)

# GET with query
get_query = (
    "GET /product.html?item=Laptop&color=Green HTTP/1.1\r\n"
    "Host: tests.com\r\n"
    "\r\n"
)

# GET with headers
get_with_headers = (
    "GET /api/data HTTP/1.1\r\n"
    "Host: tests.com\r\n"
    "User-Agent: testClient\r\n"
    "Accept: application/json\r\n"
    "\r\n"
)

# GET with space in path (invalid)
invalid_space_path = (
    "GET /my folder/file.html HTTP/1.1\r\n"
    "Host: tests.com\r\n"
    "\r\n"
)

# GET with lowercase method (invalid)
lowercase_get = (
    "get / HTTP/1.1\r\n"
    "Host: tests.com\r\n"
    "\r\n"
)

# GET with body (undefined behavior)
get_with_body = (
    "GET /api HTTP/1.1\r\n"
    "Host: tests.com\r\n"
    "Content-Length: 15\r\n"
    "\r\n"
    "body-content-here"
)

# Run them all
for req in [valid_get, get_query, get_with_headers, invalid_space_path]:#, lowercase_get, get_with_body]:
    send_raw_request(req)