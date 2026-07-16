# Boot and Network Diagnostics

**Status:** Diagnostic toolkit
**Target:** ESP32 serial output and HTTP endpoints

This directory contains host-side tools for repeated boot capture, serial log
collection, and HTTP reachability checks. It has no PlatformIO environment.

- `tools/serial_boot_capture.py` captures one boot session.
- `tools/http_reachability_probe.py` checks response completeness and headers.
- `tools/boot_probe_loop.py` combines repeated boot and HTTP probing.

Historical boot-loop directories, serial boot captures, and Wi-Fi restore logs
are preserved in `results/raw/`. A diagnostic run should record serial port,
baud rate, target address, probe paths, number of cycles, and a final summary.
