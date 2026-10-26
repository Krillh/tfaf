# Tiny Floppy Archive Format (TFAF)

TFAF is a simple, read-only archive format for archival disks, floppies, and
other block devices. It stores a flat list of named files as contiguous data
starting on block boundaries. An archive is written as a complete image and is
not designed for in-place updates. It is the official archive format of
[OpenWAC](https://github.com/Krillh/openwac).

The format prioritizes a small, straightforward layout. It does not define
directories, compression, checksums, timestamps, permissions, or recovery from
interrupted writes.

## Specification

See the [TFAF v1 specification](docs/TFAFv1.md) for the format requirements,
validation rules, and image layout.

## Driver

The freestanding C11 read-only driver is in [v1](v1/README.md). It
supports mount-time validation, flat entry enumeration, exact-name lookup, and
offset-based file reads. Its optional HAL adapter supports OpenWAC disk devices.
