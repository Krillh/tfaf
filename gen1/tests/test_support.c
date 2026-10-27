#include "test_support.h"

#include <stdio.h>
#include <string.h>

#define TEST_MAGIC 0x2cb8858eu
#define TEST_FORMAT 0xfaf01000u

static void put16(uint8_t *destination, uint16_t value) {
  destination[0] = (uint8_t)value;
  destination[1] = (uint8_t)(value >> 8);
}

static void put32(uint8_t *destination, uint32_t value) {
  destination[0] = (uint8_t)value;
  destination[1] = (uint8_t)(value >> 8);
  destination[2] = (uint8_t)(value >> 16);
  destination[3] = (uint8_t)(value >> 24);
}

int test_failure(const char *file, int line, const char *expression) {
  fprintf(stderr, "    assertion failed at %s:%d: %s\n", file, line,
          expression);
  return 0;
}

int test_status_failure(const char *file, int line, const char *expression,
                        tfaf_status actual, tfaf_status expected) {
  fprintf(stderr,
          "    status mismatch at %s:%d: %s returned %d, expected %d\n", file,
          line, expression, (int)actual, (int)expected);
  return 0;
}

void test_disk_init(test_disk *disk, uint32_t block_size,
                    uint32_t block_count) {
  memset(disk, 0, sizeof(*disk));
  disk->block_size = block_size;
  disk->block_count = block_count;
  disk->fail_block = -1;
  disk->fail_status = -7001;
  disk->table_end = 16;
  put32(disk->bytes, TEST_MAGIC);
  put32(disk->bytes + 4, TEST_FORMAT);
  put16(disk->bytes + 8, (uint16_t)block_size);
  put16(disk->bytes + 10, (uint16_t)block_count);
}

size_t test_disk_add_entry(test_disk *disk, uint32_t size, uint16_t first_block,
                           const uint8_t *name, uint16_t name_length) {
  size_t offset = (size_t)disk->table_end;
  uint8_t *entry = disk->bytes + offset;
  put32(entry, size);
  put16(entry + 4, first_block);
  put16(entry + 6, name_length);
  if (name_length)
    memcpy(entry + 8, name, name_length);
  disk->table_end += 8u + name_length;
  ++disk->entry_count;
  put16(disk->bytes + 12, disk->entry_count);
  return offset;
}

uint32_t test_disk_finish_table(test_disk *disk) {
  uint32_t contents = (uint32_t)((disk->table_end + disk->block_size - 1u) /
                                 disk->block_size);
  put16(disk->bytes + 14, (uint16_t)contents);
  return contents;
}

void test_disk_set_entry_first(test_disk *disk, size_t entry_offset,
                               uint16_t first_block) {
  put16(disk->bytes + entry_offset + 4, first_block);
}

void test_disk_set_entry_size(test_disk *disk, size_t entry_offset,
                              uint32_t size) {
  put32(disk->bytes + entry_offset, size);
}

void test_disk_set_entry_name_length(test_disk *disk, size_t entry_offset,
                                     uint16_t name_length) {
  put16(disk->bytes + entry_offset + 6, name_length);
}

tfaf_reader test_disk_reader(test_disk *disk) {
  tfaf_reader reader;
  reader.read = test_read_block;
  reader.context = disk;
  reader.block_size = disk->block_size;
  reader.block_count = disk->block_count;
  return reader;
}

tfaf_status test_mount(test_disk *disk, tfaf_volume *volume, void *workspace,
                       size_t workspace_size, uint64_t work_limit) {
  tfaf_reader reader = test_disk_reader(disk);
  tfaf1_init(volume);
  return tfaf1_mount(volume, &reader, workspace, workspace_size, work_limit);
}

int test_read_block(void *context, uint32_t block, uint8_t *destination,
                    size_t capacity, int32_t *raw_status) {
  test_disk *disk = (test_disk *)context;
  if (!disk || !destination || block >= disk->block_count ||
      block >= TEST_MAX_BLOCK_COUNT || capacity < disk->block_size) {
    if (raw_status)
      *raw_status = -7002;
    return -1;
  }
  ++disk->reads;
  if ((int32_t)block == disk->fail_block) {
    if (raw_status)
      *raw_status = disk->fail_status;
    return -1;
  }
  memcpy(destination, disk->bytes + (size_t)block * disk->block_size,
         disk->block_size);
  if (raw_status)
    *raw_status = 0;
  return 0;
}
