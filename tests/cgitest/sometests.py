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
    base_dir = Path("home/images")
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

def main():
    test_get_root()
    test_oldDir_redirect()
    test_newDir_directory_listing()
    test_images_get()
    test_images_post()
    test_images_delete()
    test_imagesREDIR()

if __name__ == "__main__":
    main()