#!/usr/bin/env python3
import http.client
import os
import mimetypes
import uuid



def check_response(response, expected_status, expected_in_body=""):
    status_ok = response.status == expected_status
    body = response.read().decode()
    body_ok = expected_in_body in body
    print(f"-> Status: {response.status} {'✓' if status_ok else '✗'}")
    print(f"-> Body contains '{expected_in_body}': {'✓' if status_ok else '✗'}")
    return status_ok and body_ok

def static_file(path, expected_type, HOST, PORT):
    conn = http.client.HTTPConnection(HOST, PORT)
    conn.request("GET", path)
    response = conn.getresponse()
    type = response.getheader("Content-Type")
    print(f"GET {path}: {response.status} {response.reason}")
    print(f"-> Content-Type: {type}")
    print(response.read().decode())
    conn.close()
    return type == expected_type

def send_get(path, HOST, PORT):
    conn = http.client.HTTPConnection(HOST, PORT)
    conn.request("GET", path)
    response = conn.getresponse()
    print(f"GET {path}: {response.status} {response.reason}")
    print(response.read().decode())
    conn.close()

def send_post(path, content, content_type, HOST, PORT):
    conn = http.client.HTTPConnection(HOST, PORT)
    headers = {
        "Content-Type": content_type,
        "Content-Length": str(len(content))
    }
    conn.request("POST", path, body=content, headers=headers)
    response = conn.getresponse()
    print(f"POST {path}: {response.status} {response.reason}")
    print(response.read().decode())
    conn.close()

def send_cgi_post(path, data, type, HOST, PORT):
    conn = http.client.HTTPConnection(HOST, PORT)
    headers = {
        "Content-Type": type,
        "Content-Length": str(len(data))
    }
    conn.request("POST", path, body=data, headers=headers)
    response = conn.getresponse()
    print(f"POST {path}: {response.status} {response.reason}")
    print(response.read().decode())
    conn.close()

def send_multipart(path, filename, content, HOST, PORT):
    boundary = uuid.uuid4().hex
    conn = http.client.HTTPConnection(HOST, PORT)
    type = mimetypes.guess_type(filename)[0] or 'application/octet-stream'
    lines = [
        f"--{boundary}",
        f'Content-Disposition: form-data; name="file"; filename="{filename}"',
        f"Content-Type: {type}",
        "",
        content,
        f"--{boundary}--",
        "",
    ]
    body = "\r\n".join(lines).encode("utf-8")
    headers = {
        "Content-Type": f"multipart/form-data; boundary={boundary}",
        "Content-Length": str(len(body))
    }
    conn.request("POST", path, body=body, headers=headers)
    response = conn.getresponse()
    print(f"POST {path}: {response.status} {response.reason}")
    print(response.read().decode())
    conn.close()

def send_delete(path, HOST, PORT):
    conn = http.client.HTTPConnection(HOST, PORT)
    conn.request("DELETE", path)
    response = conn.getresponse()
    print(f"DELETE {path}: {response.status} {response.reason}")
    print(response.read().decode())
    conn.close()

# Example usage
if __name__ == "__main__":
    HOST = "127.0.0.2"
    PORT = 8004
    HOST2 = "127.0.0.2"
    PORT2 = 8004

    print("\tTEST 1")
    print("\tGET")
    # send_get("/images/", HOST, PORT2)
    # send_get("/cgi/echo.py", HOST, PORT)
    send_get("/cgi/GETpy.py", HOST2, PORT2)
    print("\tGET DONE\n")
    # print("\tPOST")
    # send_post("/images/uploads/test.txt", "This is a test upload.", "text/plain", HOST, PORT2)
    # file_name = "test_upload.txt"
    # file_content = "This is content of the test file."
    # send_multipart("/images/uploads/", file_name, file_content, HOST, PORT2)
    # form_data = "name=NikolaiTest&lang=Python"
    # send_cgi_post("/cgi/echo_post.py", form_data, "application/x-www-form-urlencoded", HOST2, PORT2)
    # print("\tPOST DONE\n")
    # print("\tStatic file")
    # static_file("/index.html", "text/html", HOST, PORT2)
    # print("\tStatic file DONE\n")
    # print("\tGET");
    # send_get("/images/uploads/test_upload.txt", HOST, PORT2)
    # print("\tGET DONE\n")
    # print("\tDELETE")
    # send_delete("/images/uploads/test.txt", HOST, PORT2)
    # send_delete("/images/uploads/test_upload.txt", HOST, PORT2)
    # print("\tDELETE DONE\n")

    # print("\tTEST 2")
    # print("\tGET")
    # send_get("/index.html", HOST2, PORT2)
    # send_get("/cgi/echo.py", HOST2, PORT2)
    # print("\tGET DONE\n")
    # print("\tPOST")
    # send_post("/uploads/test.txt", "This is a test upload.", "text/plain", HOST2, PORT2)
    # send_multipart("/images/uploads/", file_name, file_content, HOST2, PORT2)
    # send_cgi_post("/cgi/echo_post.py", form_data, "application/x-www-form-urlencoded", HOST2, PORT2)
    # print("\tPOST DONE\n")
    # print("\tStatic file")
    # static_file("/index.html", "text/html", HOST2, PORT2)
    # print("\tStatic file DONE\n")
    # print("\tGET")
    # send_get("/images/uploads/test_upload.txt", HOST2, PORT2)
    # send_get("/images/uploads/test.txt", HOST2, PORT2)
    # send_get("/images/uploads/this", HOST2, PORT2)
    # print("\tGET DONE\n")
    # print("\tDELETE")
    # send_delete("/images/uploads/test_upload.txt", HOST2, PORT2)
    # send_delete("/images/uploads/test.txt", HOST2, PORT2)
    # print("\tDELETE DONE")

    #send_get("/uploads/test_upload.txt")  # Check if deleted
    #send_get("uploads/test.txt")
    #"application/x-www-form-urlencoded"