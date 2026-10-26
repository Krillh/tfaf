#ifndef TFAFV1_H
#define TFAFV1_H
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  TFAF_OK = 0,
  TFAF_EINVAL,
  TFAF_ENOSPACE,
  TFAF_EFORMAT,
  TFAF_ECORRUPT,
  TFAF_ENOTFOUND,
  TFAF_ERESOURCE,
  TFAF_EIO,
  TFAF_EEND
} tfaf_status;

typedef int (*tfaf_read_block_fn)(void *context, uint32_t block,
                                  uint8_t *destination, size_t capacity,
                                  int32_t *raw_status);
typedef struct {
  tfaf_read_block_fn read;
  void *context;
  uint32_t block_size;
  uint32_t block_count;
} tfaf_reader;

typedef struct {
  tfaf_reader reader;
  uint8_t *cache;
  size_t cache_size;
  uint32_t cached_block;
  uint32_t entry_count;
  uint32_t contents_block;
  uint64_t table_end;
  uint64_t work_limit;
  uint64_t work;
  int32_t raw_io_status;
  uint32_t _cookie;
  uint32_t _generation;
  uint8_t active;
  uint8_t cache_valid;
} tfaf_volume;

typedef struct {
  uint32_t index;
  uint32_t size;
  uint32_t first_block;
  uint16_t name_length;
  uint64_t _table_offset;
  uint8_t _owner[sizeof(void *) + sizeof(uint32_t)];
} tfaf_file;

typedef struct {
  uint32_t next_index;
  uint64_t _table_offset;
  uint8_t _owner[sizeof(void *) + sizeof(uint32_t)];
} tfaf_cursor;
/* Call init once before mount. Workspace must hold at least one full block.
 * Handles are owned by one mount and expire on unmount. */
size_t tfafv1_workspace_size(uint32_t block_size);
void tfafv1_init(tfaf_volume *volume);
tfaf_status tfafv1_mount(tfaf_volume *volume, const tfaf_reader *reader,
                         void *workspace, size_t workspace_size,
                         uint64_t work_limit);
void tfafv1_unmount(tfaf_volume *volume);
tfaf_status tfafv1_validate(tfaf_volume *volume, uint64_t work_limit);

/* Entries are flat and ordered as stored. TFAF has no directories. */
tfaf_status tfafv1_entry_at(tfaf_volume *volume, uint32_t index,
                            tfaf_file *file);
tfaf_status tfafv1_entries_begin(tfaf_volume *volume, tfaf_cursor *cursor);
tfaf_status tfafv1_entries_next(tfaf_volume *volume, tfaf_cursor *cursor,
                                tfaf_file *file);
tfaf_status tfafv1_lookup(tfaf_volume *volume, const uint8_t *name,
                          size_t name_length, tfaf_file *file);
/* Copies the name and appends NUL; name_length excludes the terminator. */
tfaf_status tfafv1_file_name(tfaf_volume *volume, const tfaf_file *file,
                             uint8_t *destination, size_t capacity,
                             size_t *name_length);
/* Reads up to length bytes. EOF succeeds with zero bytes; partial I/O reports
 * bytes_read together with TFAF_EIO. */
tfaf_status tfafv1_pread(tfaf_volume *volume, const tfaf_file *file,
                          uint64_t offset, void *destination, size_t length,
                          size_t *bytes_read);

#ifdef __cplusplus
}
#endif
#endif



