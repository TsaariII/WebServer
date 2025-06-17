import socket

HOST = "127.0.0.2"  # or the IP of your server
PORT = 8004       # replace with your server's port

# Load the large header from file (as generated before)
with open("too_large_header_request.txt", "r") as f:
    request = f.read()

print(f"📤 Sending request of size {len(request)} bytes")

with socket.create_connection((HOST, PORT)) as sock:
    sock.sendall(request.encode())
    
    # Receive response
    response = b""
    while True:
        chunk = sock.recv(4096)
        if not chunk:
            break
        response += chunk

print("✅ Response from server:")
print(response.decode(errors="replace"))
