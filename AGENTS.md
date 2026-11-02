# Project Guidelines

## ESP32-S3 Firmware Flashing

On Linux, if the ESP32 USB JTAG/serial device is visible in `lsusb` but no `/dev/ttyACM*` port exists, load the CDC ACM driver with `sudo modprobe cdc_acm`. Verify the port appears and is accessible before running `idf.py flash`; only report success after the flash command completes.
