# TFAF1 wasm driver package

This package contains the `gen1` freestanding C driver for TFAF1, TFAF
generation 1. A future TFAF2 driver would be generation 2 and released
separately.

## Files

- `lib/tfaf1-wasm.a` is the LLVM LTO static archive. It contains the core
  reader and the optional OpenWAC HAL adapter as separate archive members.
- `include/tfaf1.h` declares the core reader API.
- `include/tfaf1_hal.h` declares the OpenWAC HAL adapter API.
- `docs/TFAF1.md` specifies the on-disk format.
- `LICENSE` contains the project license.
- `SHA256SUMS.txt` contains the archive's SHA-256 checksum.
- `BUILD-METADATA.json` records the target triple, LTO flags, compiler and
  archiver versions, archive size, and archive checksum.

The archive is not a standalone wasm program. Link it into the consumer's wasm
module using a compatible Clang/LLVM toolchain with LTO enabled. Applications
using only the core reader need `tfaf1.h` and provide a block-read callback.
Applications calling the HAL adapter also need `tfaf1_hal.h`; the resulting
module uses the OpenWAC `hal` imports provided by its host.

The static archive contains separate object members, so an application that
uses only the core API does not need to call the HAL adapter. If rebuilding the
adapter from source, the OpenWAC HAL declarations in `gen1/dependencies/hal.h`
are needed in addition to the public headers.
