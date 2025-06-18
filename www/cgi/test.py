#!/usr/bin/env python3
import os, sys, time
print("Content-Type: text/plain\n")
print(f"Method: {os.environ.get('REQUEST_METHOD')}")
print(f"Query: {os.environ.get('QUERY_STRING', '')}")
print(f"Content-Length: {os.environ.get('CONTENT_LENGTH')}")
print(f"Content-Type: {os.environ.get('CONTENT_TYPE')}")
print("\r\n\r\n")
body = sys.stdin.read();
print(f"{body}");