# TFAF1: Tiny Floppy Archive Format

TFAF1 is TFAF generation 1; a future TFAF2 would be generation 2. TFAF1 is a
simple, read-only archive format for archival disks, floppies, and other block
devices. It stores a flat list of named files as contiguous data starting on
block boundaries. An archive is written as a complete image and is not designed
for in-place updates. It is the official archive format of
[OpenWAC](https://github.com/Krillh/openwac).

The format prioritizes a small, straightforward layout. It does not define
directories, compression, checksums, timestamps, permissions, or recovery from
interrupted writes.

## Specification

See the [TFAF1 specification](docs/TFAF1.md) for the format requirements,
validation rules, and image layout.

## Driver

The freestanding C11 read-only driver is in [gen1](gen1/README.md). It
supports mount-time validation, flat entry enumeration, exact-name lookup, and
offset-based file reads. Its optional HAL adapter supports OpenWAC disk devices.

## Build

With Clang and `llvm-ar` on `PATH`, select the generation explicitly:

```powershell
./build-release.ps1 -Generation gen1
```

Pass `-ReleaseVersion 0.1.0-rc.1` when preparing a versioned prerelease; the
bundle metadata records both the semantic version and its generation-specific
tag (`tfaf1/v0.1.0-rc.1`).

The current `gen1` build creates `build/gen1/tfaf1-wasm.a` and packages the
archive, public headers, license, usage notes, format specification, and build
metadata under `release/tfaf-gen1-wasm/`. It also creates
`release/tfaf-gen1-wasm.zip`. Each generation has a separate build and release
path. Consumers need a compatible Clang/LLVM toolchain and must enable LTO at
the final link step.

## Tests

Run the TFAF1 host-side format and interface suite with:

```powershell
cd gen1
./test.ps1
```
