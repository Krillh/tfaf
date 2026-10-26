# TFAF1: Tiny Floppy Archive Format

TFAF1 is TFAF generation 1. A future TFAF2 would be generation 2. This
document defines TFAF1, currently the only defined generation.

TFAF is a small, read-only archive layout for block devices whose contents are
written as a complete image and then retained until the device is wiped. It is
intended for archival disks and floppies, but works with any block device that
can store the resulting image. TFAF has no directories, timestamps, permissions,
compression, checksums, or update protocol. It stores a flat list of named files.

## Conventions and validity

- Integers are unsigned and little-endian. All offsets are zero-based.
- `B` is the block size in bytes and `D` is the block count. Byte ranges are
  written `[start, end)`; the end is excluded.
- The format has no runtime block-size or disk-size defaults. Read block 0
  using the device's reported geometry. The recorded `B` and `D` must match
  that geometry exactly.
- A completed archive must satisfy every required rule below. Reject invalid
  images; TFAF defines no repair or crash-recovery behavior.
- Use checked arithmetic when calculating offsets, lengths, and block counts.
  Overflow, truncation, or wrapping must not make an invalid range valid.

## Block 0 header

The first 16 bytes of block 0 are the header. The header uses the
OpenWAC-standard magic and the TFAF format identifier.

| Offset | Bytes | Field |
| ---: | ---: | --- |
| 0 | 4 | Magic: `0x2CB8858E` |
| 4 | 4 | Format identifier: `0xFAF01000` |
| 8 | 2 | Block size `B` in bytes |
| 10 | 2 | Block count `D` |
| 12 | 2 | Number of file entries `E` |
| 14 | 2 | First contents block `C` |

`B` must be at least 16. `D` must be nonzero. Since `D` is a 16-bit field,
block indexes range from 0 through `D - 1`, and `D` is at most 65,535. The
recorded image size is `B * D` bytes. A device may impose additional geometry
limits. `C` must be no greater than `D`.

The file table begins at byte offset 16, immediately after the header. It
contains exactly `E` entries with no padding between entries. The table may
span multiple blocks and has no block-alignment requirement. It must end at or
before byte offset `C * B`; therefore the complete header and file table fit
before the contents area. `C` may equal `D` when the archive has no file data.
All file contents start at block-aligned offsets, as specified below.

## File entries

Each entry has this layout. All multibyte fields use the integer conventions
above.

| Relative offset | Bytes | Field |
| ---: | ---: | --- |
| 0 | 4 | Logical file size in bytes |
| 4 | 2 | First data block |
| 6 | 2 | Name length in bytes, `L` |
| 8 | `L` | Name bytes |

`L` must be nonzero. Names are case-sensitive printable ASCII bytes `0x20`
through `0x7E`; NUL termination is not used. A name may contain spaces or
punctuation. Names must be unique across the archive, compared byte-for-byte.
Names are not paths: `/` and `\\` are ordinary name characters.

The table is parsed in order, using each entry's `L` to locate the next entry.
The table end is the byte immediately following the last name. For `E = 0`,
the table end is byte 16. Reject an entry whose name length or bytes extend
beyond `C * B`.

The 32-bit logical size is the exact number of file bytes. File data occupies
whole blocks, with no per-file padding in the logical contents. For each file:

```text
block_count = ceil(logical_size / B)
data_start = first_data_block * B
data_end = data_start + block_count * B
```

The entry's first data block must be at least `C` and less than or equal to
`D`. `data_end` must be no greater than `D * B`. For a nonempty file,
`first_data_block < D`. A zero-length file owns no data blocks; its
`first_data_block` may be any value from `C` through `D` and is ignored when
reading the file.

Files are stored contiguously, each starting at a block boundary. Physical
blocks after the last logical byte in a file's final block are padding and
are not part of the file. Each nonempty file's allocated block range must be
disjoint from every other file's range. Gaps between files are permitted and
ignored. Bytes in unused blocks and padding have no defined value.

## Reading files

To read a file, find its entry by exact name, then return exactly its logical
size bytes beginning at `first_data_block * B`. Do not return final-block
padding. There is no required ordering of entries or file data; readers must
use the entry fields rather than infer placement from table order.

## Creating an image

An image creator chooses `B`, `D`, and a file order, writes block 0 and the
entries, then places each nonempty file in a nonoverlapping contiguous run of
blocks at or after `C`. `C` must be large enough to cover the complete table.
The creator records the exact logical size and data start for each file.
Reserved bytes, table-to-content padding, inter-file gaps, and final-block
padding may be filled with any bytes.

TFAF does not prescribe a write order, atomicity, durability, or behavior after
a failed write. The image is valid only after all required bytes have been
written and the resulting completed image satisfies this specification.
