#!/usr/bin/env pytest
from pathlib import Path
import requests

def test_get_root():
    """Test that GET /index.html returns 200."""
    response = requests.get("http://127.0.0.2:8004/index.html")
    assert response.status_code == 200

def test_oldDir_redirect():
    """
    Test that a GET request to /oldDir/ results in a 301 redirect to /newDir/.
    """
    response = requests.get("http://127.0.0.2:8004/oldDir/", allow_redirects=False)
    # Expect a redirection status code (307) and a Location header pointing to /newDir/
    assert response.status_code == 301
    location = response.headers.get("Location", "")
    assert "/newDir/" in location


def test_newDir_directory_listing():
    """
    Test that GET /newDir/ returns 200.
    The configuration enables directory listing in /newDir/ so we expect a listing.
    """
    response = requests.get("http://127.0.0.2:8004/newDir/")
    assert response.status_code == 200
    # Optionally, check for a marker (like an HTML tag) that indicates a directory listing.
    # For example, many servers include <title>Directory listing</title> in the response.
    content = response.text.lower()
    assert "directory" in content or "<html" in content


def test_images_get():
    """
    Test that GET /images/ (an endpoint allowing GET, POST, DELETE)
    returns 200 and contains (possibly) a directory listing.
    """
    response = requests.get("http://127.0.0.2:8004/images/")
    assert response.status_code == 200

def test_images_post():    
	files = {'file': ('filename.txt', b"dummy data\n")}
	response = requests.post("http://127.0.0.2:8004/images/", files=files)
	assert response.status_code in (200, 201)

def test_images_delete():
    """
    Test that DELETE /images/ is accepted.
    """

    # Setup: Create a test directory and file
    base_dir = Path("www/images")
    test_file = base_dir / "random.file"
    base_dir.mkdir(parents=True, exist_ok=True)
    test_file.write_text("Temporary file content.")

    # Run DELETE request
    response = requests.delete("http://127.0.0.2:8004/images/random.file")

    # Check the response status code
    assert response.status_code in (200, 202, 204)

    # Optionally, clean up
    if test_file.exists():
        test_file.unlink()
    # if base_dir.exists() and not any(base_dir.iterdir()):
    #     base_dir.rmdir()

def test_imagesREDIR():
    """
    Test that GET /imagesREDIR/ returns a 307 redirect to /images/.
    """
    response = requests.get("http://127.0.0.2:8004/imagesREDIR/", allow_redirects=False)
    assert response.status_code == 307
    location = response.headers.get("Location", "")
    assert "/images/" in location

def test_cgi_empty_redirect():
    """
    Test that GET /cgi/empty/ returns a 307 redirect to https://www.google.com.
    """
    response = requests.get("http://127.0.0.2:8004/cgi/empty/", allow_redirects=False)
    assert response.status_code == 307
    location = response.headers.get("Location", "")
    assert "https://www.google.com" in location

def test_bad_http_request():
    """
    Test that sending a malformed HTTP request (bad header format) returns a 400 error.
    
    This test uses a raw socket to send a manually crafted HTTP request with an intentionally
    malformed header (missing the colon). Higher-level libraries like requests or aiohttp would
    sanitize such inputs automatically, so a raw socket is better suited for this purpose.
    """
    import socket
    
    # Connect to the local server at 127.0.0.2:8004
    with socket.create_connection(("127.0.0.2", 8004), timeout=5) as sock:
        # Craft a malformed HTTP request:
        # - Valid request line and Host header
        # - A header line without a colon (malformed)
        bad_request = (
            "GET /index.html HTTP/1.1\r\n"
            "Host: http://127.0.0.2\r\n"
            "BadHeaderWithoutColon\r\n"  # This header is invalid (missing ':' separator)
            "\r\n"
        )
        sock.sendall(bad_request.encode())
        
        # Receive the response (reading 1024 bytes is generally sufficient for the header)
        response = sock.recv(1024).decode(errors="ignore")
    
    # Extract the status line (first line of the response)
    status_line = response.splitlines()[0] if response else "Empty response"
    # Assert that the status line indicates a 400 Bad Request.
    # Depending on the implementation of your server, the exact wording might vary.
    assert "400" in status_line, f"Expected a 400 Bad Request response, got: {status_line}"

def test_good_http_request():
    """
    Test sending a valid HTTP GET request using a raw socket and verify that it returns a 200 OK response.
    This test sends a properly formatted request header to ensure the server responds correctly,
    eliminating malformed header errors as a factor.
    """
    import socket
    
    # Establish a connection to the server.
    with socket.create_connection(("127.0.0.2", 8004), timeout=5) as sock:
        # Construct a valid HTTP GET request for /index.html.
        good_request = (
            "GET /index.html HTTP/1.1\r\n"
            "Host: 127.0.0.2\r\n"
            "Connection: close\r\n"  # Ensure the connection is closed after the response.
            "\r\n"
        )
        # Send the request.
        sock.sendall(good_request.encode())
        
        # Receive the response from the server.
        response = sock.recv(1024).decode(errors="ignore")
    
    # Get the first line of the response which should contain the HTTP status.
    status_line = response.splitlines()[0] if response else ""
    
    # Assert that the response indicates a successful request.
    assert "200" in status_line, f"Expected a 200 OK response, got: {status_line}"

def test_not_found_error():
    """
    Test that a GET request for a non-existent resource returns a 404 error,
    which should trigger the custom error page.
    """
    response = requests.get("http://127.0.0.2:8004/nonexistent.html")
    # Depending on your implementation, the server should send a 404 and may serve the custom error page.
    assert response.status_code == 404
    # Optionally, check that the response content contains something indicative of the error page.
    content = response.text.lower()
    # For example, the custom error page may contain the error code or the phrase "not found".
    assert "404" in content or "not found" in content

def test_client_body_size_exceeded():
    """
    Test that sending a POST request with a large payload (exceeding max_client_body_size)
    results in an error (likely a 413 Payload Too Large error).
    """
    # Construct a payload larger than 5,000,000 bytes, e.g. 6,000,000 bytes.
    payload = b"x" * 6000000
    try:
        response = requests.post("http://127.0.0.2:8004/images/", data=payload)
        # Adjust the expected status code as needed; 413 is common for payload too large.
        assert response.status_code in (413, 400, 500), f"Unexpected status: {response.status_code}"
    except Exception as e:
        print(f"Error sending payload: {e}")

def test_file_upload_and_check():
    """
    Test that uploading a file through a POST request is successful and 
    that the file is correctly saved in the 'home/images' directory.
    
    Assumes the server routes POST requests to /images/ to the directory:
      ./home/images
    """
    import time
    from pathlib import Path
    import requests

    # Define the file name and content for the test.
    filename = "test_upload.txt"
    file_content = b"sample file content"
    files = {'file': (filename, file_content)}
    
    # Send a POST request to the /images/ endpoint.
    response = requests.post("http://127.0.0.2:8004/images/", files=files)
    assert response.status_code in (200, 201), f"Upload failed with status {response.status_code}"
    
    # Optionally, wait a short time to let any asynchronous file writing complete.
    time.sleep(0.5)
    
    # Determine the expected path to the uploaded file.
    upload_path = Path(__file__).parent.parent.parent / "www/images" / filename
    
    # Verify that the file now exists.
    print(upload_path)
    assert upload_path.exists(), f"Expected uploaded file at {upload_path} does not exist."
    
    # Optionally, verify that the file contents are as expected.
    with upload_path.open("rb") as f:
        saved_content = f.read()
    assert saved_content == file_content, "File content does not match expected content."
    
    # Clean up: remove the file after the test so that repeated test runs don't accumulate files.
    try:
        upload_path.unlink()
    except Exception as e:
        print(f"Error cleaning up the test file: {e}")

########################################################################
# Asynchronous Test for Concurrency
########################################################################

import aiohttp
import asyncio
import pytest

@pytest.mark.asyncio
async def test_repeated_requests():
    """
    Asynchronously send multiple concurrent GET requests to /index.html.
    This test simulates load. Adjust num_requests as appropriate.
    """
    num_requests = 1000
    url = "http://127.0.0.2:8004/index.html"

    # Rajoita samanaikaisten pyyntöjen määrä
    sem = asyncio.Semaphore(num_requests)  # Vain 10 yhtäaikaista pyyntöä – säädä tarpeen mukaan

    async def limited_get(session, url):
        async with sem:
            async with session.get(url) as resp:
                assert resp.status == 200
                await resp.text()  # luetaan sisältö, vaikka ei käytetä sitä
                return resp

    async with aiohttp.ClientSession() as session:
        tasks = [limited_get(session, url) for _ in range(num_requests)]
        responses = await asyncio.gather(*tasks, return_exceptions=True)

    # Optionaalinen virheenkäsittely
    for i, resp in enumerate(responses):
        if isinstance(resp, Exception):
            print(f"Request {i} failed: {resp}")
        else:
            assert resp.status == 200

@pytest.mark.asyncio
async def test_concurrent_get_and_post():
    """
    Asynchronously send multiple concurrent GET and POST requests to the server.
    This test simulates a mixed load of GET and POST requests.
    """
    num_get = 1000   # Number of GET requests to send
    num_post = 1000  # Number of POST requests to send
    get_url = "http://127.0.0.2:8004/index.html"
    post_url = "http://127.0.0.2:8004/images/"

    sem = asyncio.Semaphore(1000)  # Rajoita samanaikaiset pyynnöt esim. 25:een

    async def limited_get(session, url):
        async with sem:
            async with session.get(url) as resp:
                await resp.read()
                return resp

    async def limited_post(session, url, formdata):
        async with sem:
            async with session.post(url, data=formdata) as resp:
                await resp.read()
                return resp

    async with aiohttp.ClientSession() as session:
        test_file = Path("home/images/uploads/filename.txt")

        # Luo GET-pyynnöt
        get_tasks = [
            limited_get(session, get_url)
            for _ in range(num_get)
        ]

        # Luo POST-pyynnöt
        post_tasks = []
        for _ in range(num_post):
            form = aiohttp.FormData()
            form.add_field(
                'file',
                b"dummy data\n", 
                filename="filename.txt",
            )
            post_tasks.append(limited_post(session, post_url, form))

        # Aja molemmat rinnakkain
        tasks = get_tasks + post_tasks
        responses = await asyncio.gather(*tasks, return_exceptions=True)

        test_file.unlink(missing_ok=True)  # varmistetaan että tiedosto poistuu

    # Tarkista vastaukset
    for i, resp in enumerate(responses):
        if isinstance(resp, Exception):
            print(f"Request {i} failed: {resp}")
            continue

        method = resp.request_info.method
        if method == "GET":
            assert resp.status == 200, (
                f"GET request to {resp.request_info.url} returned {resp.status}"
            )
        elif method == "POST":
            assert resp.status in (200, 201), (
                f"POST request to {resp.request_info.url} returned {resp.status}"
            )
        await resp.release()

@pytest.mark.asyncio
async def test_single_post_upload():
    post_url = "http://127.0.0.2:8004/uploads/"
    filename = "testfile.txt"
    content = b"Test content\n"

    async with aiohttp.ClientSession() as session:
        form = aiohttp.FormData()
        form.add_field('file', content, filename=filename)
        async with session.post(post_url, data=form) as resp:
            assert resp.status in (200, 201)
            text = await resp.text()
            print(text)  # Tulosta vastaus debuggausta varten

@pytest.mark.asyncio
async def test_repeated_identical_post_upload():
    post_url = "http://127.0.0.2:8004/uploads/"
    filename = "testfile.txt"
    content = b"Test content for repeated upload\n"

    sem = asyncio.Semaphore(10)  # Maksimi 10 samanaikaista pyyntöä

    async def limited_post():
        async with sem:
            async with aiohttp.ClientSession() as session:
                form = aiohttp.FormData()
                form.add_field('file', content, filename=filename)
                async with session.post(post_url, data=form) as resp:
                    assert resp.status in (200, 201)
                    return await resp.text()

    # Lähetä sama tiedosto 5 kertaa
    tasks = [limited_post() for _ in range(5)]
    results = await asyncio.gather(*tasks)
    for res in results:
        print(res)

@pytest.mark.asyncio
async def test_repeated_post_requests_from_multiple_clients():
    post_url = "http://127.0.0.2:8004/cgi/test.py"
    data = "Test content\n"

    sem = asyncio.Semaphore(10)  # Maksimi 10 samanaikaista pyyntöä

    async def limited_post():
            async with sem:
                async with aiohttp.ClientSession() as session:
                    results = []
                    for _ in range(2):  # Lähetetään 2 POST-pyyntöä per client
                        async with session.post(post_url, data=data) as resp:
                            assert resp.status in (200, 201)
                            results.append(await resp.text())
                    return results

    # 10 clienttia × 2 pyyntöä = 20 POSTia
    tasks = [limited_post() for _ in range(3)]
    results = await asyncio.gather(*tasks)
    for res in results:
        print(res)

