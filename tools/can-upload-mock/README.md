# CAN Upload Mock Server

This folder contains a FastAPI upload backend used to develop and test ESP32 auto-upload behavior.

## Features implemented

- `POST /edit` and `POST /upload` multipart endpoints
- chunked stream endpoints:
  - `POST /edit-chunked`
  - `POST /upload-chunked`
  - `POST /edit` also auto-detects raw chunked mode when:
    - `X-Upload-Mode: chunked` or `raw`, or
    - `Content-Type: application/octet-stream`
- Web UI at `GET /ui`:
  - shows all local uploaded files
  - OneDrive setup menu
  - manual upload controls
- API auth token support
  - `X-Api-Token: <token>` or `Authorization: Bearer <token>`
- Upload validation
  - max size limit
  - extension allowlist
  - MIME type allowlist
- Metadata ingestion and validation
  - required: `bus_id`, `start_ms`, `end_ms`
  - optional: `flags`, `source`, `device_id`
- Idempotency support
  - `X-Idempotency-Key` header (or `idempotency_key` form field)
- Duplicate detection
  - hash + filename + metadata key (`bus_id/start_ms/end_ms`)
- Atomic file write (temp file + fsync + rename)
- Structured JSON responses with stable codes
- Health endpoints
  - `GET /healthz`
  - `GET /readyz`
- Retry-friendly responses
  - `429` with `Retry-After` (rate limiting)
  - optional forced `503` with `Retry-After`
- Per-client rate limiting
- Upload audit log (`upload_audit.jsonl` in upload directory)
- Bonjour (mDNS) advertisement (best effort, non-fatal)

## OneDrive Web Workflow

Open:
- `http://<PC-LAN-IP>:8000/ui`

Menu workflow in UI:
1. Save OneDrive config (`client_id`, `tenant_id`, target folder, enable auto-upload).
2. Start device login.
3. Open the shown verification URL, enter user code, approve access.
4. Finalize login with device code.
5. Enable automatic upload and verify file sync status in file table.

Notes:
- Uses Microsoft Graph Device Code flow.
- Stores OneDrive config and tokens in upload directory file:
  - `onedrive_config.json`
- Automatic OneDrive upload runs after successful `/edit` uploads.

## Setup

1. Open terminal in `tools/can-upload-mock`
2. Install dependencies:

```powershell
pip install -r requirements.txt
```

## Run

```powershell
python server.py
```

Default base URL:
- `http://can-upload.local:8000/` (Bonjour name)
- `http://127.0.0.1:8000/` (local loopback)

On startup the server now also prints:
- `LAN upload URL: http://<PC-LAN-IP>:8000/edit`

And `/` reports:
- `lan_upload_url`
- `bonjour_self_check` (`ok` or `failed`)
- `bonjour_error` (if registration failed)

## ESP32 integration changes required

The ESP32 uploader should send requests in this format:

1. Endpoint:
- `POST <upload_url>` where default is `http://can-upload.local:8000/edit`

2. Multipart form data:
- file field: keep existing field name (e.g. `updatefile`) and attach log file bytes
- required metadata fields as text form fields:
  - `bus_id` (integer)
  - `start_ms` (integer)
  - `end_ms` (integer)
- optional metadata fields:
  - `flags`, `source`, `device_id`

3. Headers:
- `X-Idempotency-Key: <stable-unique-key-per-file>` recommended
  - Example key basis: `bus_id + start_ms + end_ms + filename + filesize`
- If auth token is enabled on server:
  - `X-Api-Token: <token>`

4. Retry logic:
- On `429` or `503`, read `Retry-After` header and back off accordingly
- Keep exponential backoff + jitter on transport errors/timeouts

5. Success/duplicate handling:
- Treat both as non-failure:
  - `201` + code `UPLOAD_ACCEPTED`
  - `200` + code `DUPLICATE_CONTENT` or `DUPLICATE_IDEMPOTENCY_KEY`
- Mark file as uploaded in ESP32 metadata for these success-like responses

6. Error handling:
- Do not mark uploaded for:
  - `400`, `401`, `413`, `415`, `500`
- Log returned JSON `code` and `message` for diagnostics

## Response contract

Success shape:

```json
{
  "status": "ok",
  "code": "UPLOAD_ACCEPTED",
  "message": "upload stored successfully",
  "data": { "...": "..." }
}
```

Error shape:

```json
{
  "status": "error",
  "code": "RATE_LIMITED",
  "message": "too many requests from client",
  "data": {}
}
```

## Quick test

```powershell
curl -X POST http://127.0.0.1:8000/edit `
  -F "updatefile=@README.md" `
  -F "bus_id=0" `
  -F "start_ms=1000" `
  -F "end_ms=2000" `
  -H "X-Idempotency-Key: demo-1"
```

Chunked/raw stream test (no multipart parser path):

```powershell
curl -X POST http://127.0.0.1:8000/edit-chunked `
  -H "Content-Type: application/octet-stream" `
  -H "X-Filename: test.bin" `
  -H "X-Bus-Id: 0" `
  -H "X-Start-Ms: 1000" `
  -H "X-End-Ms: 2000" `
  --data-binary "@README.md"
```

## Config (environment variables)

- `CAN_UPLOAD_PORT` default `8000`
- `CAN_UPLOAD_HOST` default `0.0.0.0`
- `CAN_UPLOAD_BONJOUR_NAME` default `can-upload`
- `CAN_UPLOAD_ENABLE_BONJOUR` default `1`
- `CAN_UPLOAD_DIR` default Desktop `CAN Upload`
- `CAN_UPLOAD_API_TOKEN` optional auth token
- `CAN_UPLOAD_MAX_BYTES` default `268435456` (256 MiB)
- `CAN_UPLOAD_ALLOWED_EXTENSIONS` default `*` (comma separated, e.g. `.csv,.log,.bin`)
- `CAN_UPLOAD_ALLOWED_MIME_TYPES` default `*` (comma separated)
- `CAN_UPLOAD_RATE_LIMIT_WINDOW_SEC` default `60`
- `CAN_UPLOAD_RATE_LIMIT_MAX_REQUESTS` default `30`
- `CAN_UPLOAD_FORCE_503` default `0` (set `1` to simulate temporary outage)
- `CAN_UPLOAD_FAIL_RETRY_AFTER_SEC` default `10`
- `CAN_UPLOAD_IDEMPOTENCY_TTL_SEC` default `86400`
- `CAN_UPLOAD_AUDIT_LOG_FILE` default `upload_audit.jsonl`
- `CAN_UPLOAD_ONEDRIVE_CONFIG_FILE` default `onedrive_config.json`
- `CAN_UPLOAD_ONEDRIVE_SCOPE` default `offline_access Files.ReadWrite`
- `CAN_UPLOAD_DDNS_ENABLE` default `0` (set `1` to enable DynDNS updates on server start)
- `CAN_UPLOAD_DDNS_PROVIDER` default `noip` (currently only `noip` supported)
- `CAN_UPLOAD_DDNS_HOSTNAME` No-IP hostname to update (e.g. `nobbyfix.ddns.net`)
- `CAN_UPLOAD_DDNS_USERNAME` No-IP account username
- `CAN_UPLOAD_DDNS_PASSWORD` No-IP account password
- `CAN_UPLOAD_DDNS_UPDATE_URL` default `https://dynupdate.no-ip.com/nic/update`
- `CAN_UPLOAD_DDNS_INTERVAL_SEC` default `600` (periodic refresh; set `0` for one-shot update at startup only)
- `CAN_UPLOAD_DDNS_USER_AGENT` default `can-upload-mock/1.0`
- `CAN_UPLOAD_DDNS_MYIP` optional explicit public IP (normally leave empty)

## Example runtime configs

Disable Bonjour (for restricted shells):

```powershell
$env:CAN_UPLOAD_ENABLE_BONJOUR="0"
python server.py
```

Enable auth + strict allowlist:

```powershell
$env:CAN_UPLOAD_API_TOKEN="my-secret-token"
$env:CAN_UPLOAD_ALLOWED_EXTENSIONS=".log,.csv,.bin"
$env:CAN_UPLOAD_ALLOWED_MIME_TYPES="text/plain,application/octet-stream,text/csv"
python server.py
```

Enable No-IP DynDNS auto update (startup + periodic refresh):

```powershell
$env:CAN_UPLOAD_DDNS_ENABLE="1"
$env:CAN_UPLOAD_DDNS_PROVIDER="noip"
$env:CAN_UPLOAD_DDNS_HOSTNAME="nobbyfix.ddns.net"
$env:CAN_UPLOAD_DDNS_USERNAME="<your-noip-username>"
$env:CAN_UPLOAD_DDNS_PASSWORD="<your-noip-password>"
$env:CAN_UPLOAD_DDNS_INTERVAL_SEC="600"
python server.py
```
