# TFAF1 C driver

This is a small, freestanding C11, read-only driver for the TFAF1
specification in ../docs/TFAF1.md. The format core has no heap, filesystem,
logging, or HAL dependency. It uses a caller-owned block reader and one-block
cache. The optional HAL adapter uses the disk operations in dependencies/hal.h.

## Use the core

Initialize a volume, then mount it with device geometry, a block-read callback,
and a workspace of at least one block:

```c
tfaf_volume volume;
tfaf_file file;
uint8_t cache[BLOCK_SIZE];
uint8_t output[4096];
size_t bytes_read;

tfaf1_init(&volume);
tfaf_reader reader = { read_block, device, BLOCK_SIZE, BLOCK_COUNT };
tfaf_status status =
    tfaf1_mount(&volume, &reader, cache, sizeof cache, 0);
if (status != TFAF_OK) {
  /* Handle invalid media or I/O errors. */
} else {
  status = tfaf1_lookup(&volume, (const uint8_t *)"README", 6, &file);
  if (status == TFAF_OK) {
    status = tfaf1_pread(&volume, &file, 0, output, sizeof output,
                          &bytes_read);
  }
  tfaf1_unmount(&volume);
}
```

The callback returns zero only after filling one complete block. Pass 0 for
the validation work limit to use its one-million-step default. Mount validates
the header, table bounds, printable ASCII names, exact-name uniqueness, file
ranges, and overlap between file allocations. Validation uses bounded pairwise
scans. The default limit is roughly suitable for 1,000 small entries; long
shared name prefixes can hit it sooner. Larger archives can pass a higher limit
to mount, at the cost of more validation work.

Lookup takes an exact, case-sensitive counted name. tfaf1_file_name copies the
name to a caller buffer, appends NUL, and reports the name length excluding the
terminator. tfaf1_pread reads from an explicit offset, returns up to the
requested length, and reports the number copied. EOF is a successful zero-byte
read. The tfaf_cursor API supports one-pass iteration without allocating a
table. Handles and cursors belong to one mounted volume and expire on unmount.
Calls on a volume must be serialized; the driver assumes the image is not
changed externally while mounted.

## HAL adapter

tfaf1_hal_open reads geometry for a device UUID. tfaf1_hal_reader returns a
block reader suitable for tfaf1_mount. tfaf1_hal_select_root selects the
single root disk. The adapter preserves the last raw HAL status in
tfaf_hal_device.raw_status.

## Build

With Clang available on PATH, compile the freestanding core and HAL adapter:

```powershell
./build.ps1 -Target native
./build.ps1 -Target wasm
```

Objects are written to `out/native/` or `out/wasm/`. Applications that do not use
OpenWAC's HAL only need `src/tfaf1.c` and `include/tfaf1.h`.
