# CAN Upload Mock Server

This folder contains a small FastAPI HTTP server to develop and test upload behavior against a local endpoint.

## What it does

- Starts an HTTP server.
- Accepts `POST` multipart uploads on:
  - `/edit` (matches the OpenInverter web interface form action)
  - `/upload` (generic alias)
- Saves uploaded files to:
  - `C:\\Users\\<your-user>\\Desktop\\CAN Upload`
- Advertises itself via Bonjour (mDNS) as:
  - `http://can-upload.local:8000/` by default

## Setup

1. Open a terminal in `tools/can-upload-mock`.
2. Create and activate a Python virtual environment (optional but recommended).
3. Install dependencies:

```powershell
pip install -r requirements.txt
```

## Run

```powershell
python server.py
```

When running, the server listens on `0.0.0.0:8000` by default and prints the Bonjour URL.

## Test upload with curl

```powershell
curl -X POST http://127.0.0.1:8000/edit -F "updatefile=@README.md"
```

You can also target the Bonjour hostname if Bonjour/mDNS is available on the client machine:

```powershell
curl -X POST http://can-upload.local:8000/edit -F "updatefile=@README.md"
```

## Config (optional)

- `CAN_UPLOAD_PORT`: server port (default `8000`)
- `CAN_UPLOAD_HOST`: bind host (default `0.0.0.0`)
- `CAN_UPLOAD_BONJOUR_NAME`: Bonjour name prefix (default `can-upload`)
- `CAN_UPLOAD_ENABLE_BONJOUR`: `1/0` to enable or disable mDNS advertisement (default `1`)
- `CAN_UPLOAD_DIR`: override save directory (default Desktop `CAN Upload` folder)

Example:

```powershell
$env:CAN_UPLOAD_BONJOUR_NAME="can-grabber-upload"
$env:CAN_UPLOAD_PORT="8080"
python server.py
```

If you run from environments where mDNS registration blocks (for example some embedded or sandboxed shells), run with:

```powershell
$env:CAN_UPLOAD_ENABLE_BONJOUR="0"
python server.py
```
