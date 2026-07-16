# Upload UI v5 Test Architecture

This page is the documentation entrypoint for the `sd_http_upload_ui_test_v5`
development harness.

The detailed source architecture currently lives in:

- [Upload UI Test v5 Architecture](architecture-v5.md)

## Scope

The v5 harness validates:

- SD test file creation
- Upload queue seeding
- HTTP `/status`, `/start`, `/abort`, `/reset_upload_state`, and bookkeeping
- Retry/backoff behavior
- Server contract validation
- Uploaded-state persistence
- Reachability probe and cache behavior
- UI responsiveness while upload is active

## Runtime View

```mermaid
flowchart TD
  Main[sd_http_upload_ui_test_v5 entrypoint] --> Storage[Test storage helpers]
  Main --> Uploader[upload_task and monitor_task]
  Main --> Server[HTTP server task]
  Main --> Serial[serial tester interface]
  Server --> Queue[upload queue]
  Serial --> Queue
  Queue --> Uploader
  Uploader --> SD[SD test files]
  Uploader --> Target[Mock upload server]
```

## Related Tools

- `experiments/upload-ui/tools/upload_ui_tester_v5.py`
- `experiments/upload-ui/docs/runbook.md`
- `experiments/upload-ui/tools/can-upload-mock/server.py`

## Evidence

Historical results are preserved under `../results/raw/` and summarized in the
repository-level [`IMPLEMENTATION_LOG.md`](../../../IMPLEMENTATION_LOG.md).
