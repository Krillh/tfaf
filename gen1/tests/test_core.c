#include "test_support.h"

#include <string.h>

#define MAGIC_OFFSET 0u
#define FORMAT_OFFSET 4u
#define BLOCK_SIZE_OFFSET 8u
#define BLOCK_COUNT_OFFSET 10u
#define ENTRY_COUNT_OFFSET 12u
#define CONTENTS_BLOCK_OFFSET 14u

static void put16(uint8_t *destination, uint16_t value) {
  destination[0] = (uint8_t)value;
  destination[1] = (uint8_t)(value >> 8);
}

int test_workspace_and_empty_image(void) {
  test_disk disk;
  tfaf_volume volume;
  tfaf_reader reader;
  tfaf_cursor cursor;
  tfaf_file file;
  uint8_t workspace[TEST_MAX_BLOCK_SIZE];

  test_disk_init(&disk, 16, 1);
  put16(disk.bytes + CONTENTS_BLOCK_OFFSET, 1);
  reader = test_disk_reader(&disk);
  TEST_ASSERT(tfaf1_workspace_size(16) == 16);
  tfaf1_init(&volume);
  TEST_STATUS(tfaf1_mount(&volume, &reader, workspace, 15, 0), TFAF_ENOSPACE);
  TEST_STATUS(tfaf1_mount(&volume, &reader, workspace, 16, 0), TFAF_OK);
  TEST_STATUS(tfaf1_entries_begin(&volume, &cursor), TFAF_OK);
  TEST_STATUS(tfaf1_entries_next(&volume, &cursor, &file), TFAF_EEND);
  TEST_STATUS(tfaf1_entry_at(&volume, 0, &file), TFAF_EEND);
  TEST_STATUS(tfaf1_lookup(&volume, (const uint8_t *)"missing", 7, &file),
              TFAF_ENOTFOUND);
  tfaf1_unmount(&volume);
  TEST_STATUS(tfaf1_entries_begin(&volume, &cursor), TFAF_EINVAL);
  return 1;
}

int test_table_iteration_lookup_and_reading(void) {
  static const uint8_t first_name[] = "Name with spaces and punctuation!";
  static const uint8_t second_name[] = "CaseSensitiveName";
  static const uint8_t third_name[] = "path/name";
  test_disk disk;
  tfaf_volume volume;
  tfaf_cursor cursor;
  tfaf_file file;
  uint8_t workspace[TEST_MAX_BLOCK_SIZE];
  size_t entry_offsets[3];
  uint32_t contents;
  size_t bytes_read;
  uint8_t output[80];
  uint8_t copied_name[64];
  size_t copied_length;

  test_disk_init(&disk, 64, 16);
  entry_offsets[0] = test_disk_add_entry(
      &disk, 70, 0, first_name, (uint16_t)(sizeof(first_name) - 1));
  entry_offsets[1] = test_disk_add_entry(
      &disk, 64, 0, second_name, (uint16_t)(sizeof(second_name) - 1));
  entry_offsets[2] = test_disk_add_entry(
      &disk, 0, 16, third_name, (uint16_t)(sizeof(third_name) - 1));
  contents = test_disk_finish_table(&disk);
  TEST_ASSERT(disk.table_end > disk.block_size);
  TEST_ASSERT(disk.table_end % disk.block_size != 0);
  // Physical placement deliberately differs from table order: the second
  // entry's data starts first, and the first entry has a gap before it.
  test_disk_set_entry_first(&disk, entry_offsets[0], (uint16_t)(contents + 3));
  test_disk_set_entry_first(&disk, entry_offsets[1], (uint16_t)contents);

  for (size_t i = 0; i < 64; ++i)
    disk.bytes[(size_t)(contents + 3) * 64 + i] = (uint8_t)i;
  for (size_t i = 0; i < 6; ++i)
    disk.bytes[(size_t)(contents + 4) * 64 + i] = (uint8_t)(64 + i);
  memset(disk.bytes + (size_t)(contents + 4) * 64 + 6, 0xe7, 58);
  memset(disk.bytes + (size_t)contents * 64, 0xa5, 64);

  TEST_STATUS(test_mount(&disk, &volume, workspace, disk.block_size, 0),
              TFAF_OK);
  TEST_STATUS(tfaf1_entries_begin(&volume, &cursor), TFAF_OK);
  for (uint32_t i = 0; i < 3; ++i) {
    TEST_STATUS(tfaf1_entries_next(&volume, &cursor, &file), TFAF_OK);
    TEST_ASSERT(file.index == i);
    TEST_STATUS(tfaf1_file_name(&volume, &file, copied_name,
                                sizeof(copied_name), &copied_length),
                TFAF_OK);
    TEST_ASSERT(copied_name[copied_length] == 0);
    TEST_ASSERT(copied_length == file.name_length);
  }
  TEST_STATUS(tfaf1_entries_next(&volume, &cursor, &file), TFAF_EEND);
  TEST_STATUS(tfaf1_entry_at(&volume, 3, &file), TFAF_EEND);

  TEST_STATUS(tfaf1_lookup(&volume, first_name, sizeof(first_name) - 1, &file),
              TFAF_OK);
  TEST_ASSERT(file.index == 0 && file.size == 70);
  TEST_STATUS(tfaf1_file_name(&volume, &file, copied_name,
                              sizeof(first_name) - 1, &copied_length),
              TFAF_ENOSPACE);
  TEST_ASSERT(copied_length == 0);
  TEST_STATUS(tfaf1_file_name(&volume, &file, copied_name,
                              sizeof(first_name), &copied_length),
              TFAF_OK);
  TEST_ASSERT(copied_length == sizeof(first_name) - 1);
  TEST_ASSERT(memcmp(copied_name, first_name, copied_length) == 0);

  TEST_STATUS(tfaf1_lookup(&volume, (const uint8_t *)"casesensitivename", 17,
                            &file),
              TFAF_ENOTFOUND);
  TEST_STATUS(tfaf1_lookup(&volume, second_name, sizeof(second_name) - 1, &file),
              TFAF_OK);
  TEST_ASSERT(file.index == 1);
  TEST_STATUS(tfaf1_lookup(&volume, third_name, sizeof(third_name) - 1, &file),
              TFAF_OK);
  TEST_ASSERT(file.index == 2);

  TEST_STATUS(tfaf1_lookup(&volume, first_name, sizeof(first_name) - 1, &file),
              TFAF_OK);
  memset(output, 0, sizeof(output));
  TEST_STATUS(tfaf1_pread(&volume, &file, 60, output, 30, &bytes_read), TFAF_OK);
  TEST_ASSERT(bytes_read == 10);
  for (size_t i = 0; i < bytes_read; ++i)
    TEST_ASSERT(output[i] == (uint8_t)(60 + i));
  TEST_STATUS(tfaf1_pread(&volume, &file, 70, output, 10, &bytes_read), TFAF_OK);
  TEST_ASSERT(bytes_read == 0);
  TEST_STATUS(tfaf1_pread(&volume, &file, 100, output, 10, &bytes_read), TFAF_OK);
  TEST_ASSERT(bytes_read == 0);
  TEST_STATUS(tfaf1_pread(&volume, &file, 0, 0, 0, &bytes_read), TFAF_OK);
  TEST_ASSERT(bytes_read == 0);

  tfaf1_unmount(&volume);
  return 1;
}

int test_empty_files_and_gaps(void) {
  static const uint8_t empty_name[] = "empty";
  static const uint8_t data_name[] = "data";
  test_disk disk;
  tfaf_volume volume;
  tfaf_file file;
  uint8_t workspace[TEST_MAX_BLOCK_SIZE];
  size_t empty_entry, data_entry, bytes_read;
  uint32_t contents;
  uint8_t output[4] = {0};

  test_disk_init(&disk, 32, 12);
  empty_entry = test_disk_add_entry(&disk, 0, 0, empty_name,
                                    (uint16_t)(sizeof(empty_name) - 1));
  data_entry = test_disk_add_entry(&disk, 1, 0, data_name,
                                   (uint16_t)(sizeof(data_name) - 1));
  contents = test_disk_finish_table(&disk);
  test_disk_set_entry_first(&disk, empty_entry, 12);
  test_disk_set_entry_first(&disk, data_entry, (uint16_t)(contents + 3));
  disk.bytes[(size_t)(contents + 3) * 32] = 0x5a;

  TEST_STATUS(test_mount(&disk, &volume, workspace, disk.block_size, 0),
              TFAF_OK);
  TEST_STATUS(tfaf1_lookup(&volume, empty_name, sizeof(empty_name) - 1, &file),
              TFAF_OK);
  TEST_ASSERT(file.size == 0 && file.first_block == 12);
  TEST_STATUS(tfaf1_pread(&volume, &file, 0, output, sizeof(output), &bytes_read),
              TFAF_OK);
  TEST_ASSERT(bytes_read == 0);
  TEST_STATUS(tfaf1_lookup(&volume, data_name, sizeof(data_name) - 1, &file),
              TFAF_OK);
  TEST_STATUS(tfaf1_pread(&volume, &file, 0, output, sizeof(output), &bytes_read),
              TFAF_OK);
  TEST_ASSERT(bytes_read == 1 && output[0] == 0x5a);
  tfaf1_unmount(&volume);
  return 1;
}

int test_odd_block_size_and_name_edges(void) {
  static const uint8_t edge_name[] = {0x20, 0x7e};
  test_disk disk;
  tfaf_volume volume;
  tfaf_file file;
  uint8_t workspace[TEST_MAX_BLOCK_SIZE];
  uint8_t output[8] = {0};
  uint8_t name_output[4] = {0};
  size_t entry, bytes_read, name_length;
  uint32_t contents;

  test_disk_init(&disk, 65, 8);
  entry = test_disk_add_entry(&disk, 66, 0, edge_name, sizeof(edge_name));
  contents = test_disk_finish_table(&disk);
  test_disk_set_entry_first(&disk, entry, (uint16_t)contents);
  for (size_t i = 0; i < 65; ++i)
    disk.bytes[(size_t)contents * 65 + i] = (uint8_t)i;
  disk.bytes[(size_t)(contents + 1) * 65] = 65;

  TEST_STATUS(test_mount(&disk, &volume, workspace, 65, 0), TFAF_OK);
  TEST_STATUS(tfaf1_lookup(&volume, edge_name, sizeof(edge_name), &file),
              TFAF_OK);
  TEST_STATUS(tfaf1_file_name(&volume, &file, name_output,
                              sizeof(name_output), &name_length),
              TFAF_OK);
  TEST_ASSERT(name_length == sizeof(edge_name));
  TEST_ASSERT(name_output[0] == 0x20 && name_output[1] == 0x7e &&
              name_output[2] == 0);
  TEST_STATUS(tfaf1_pread(&volume, &file, 63, output, 4, &bytes_read), TFAF_OK);
  TEST_ASSERT(bytes_read == 3);
  TEST_ASSERT(output[0] == 63 && output[1] == 64 && output[2] == 65);
  tfaf1_unmount(&volume);
  return 1;
}

int test_header_rejections(void) {
  test_disk disk;
  tfaf_volume volume;
  tfaf_reader reader;
  uint8_t workspace[TEST_MAX_BLOCK_SIZE];

  test_disk_init(&disk, 32, 8);
  test_disk_finish_table(&disk);
  reader = test_disk_reader(&disk);
  tfaf1_init(&volume);
  disk.bytes[MAGIC_OFFSET] ^= 1;
  TEST_STATUS(tfaf1_mount(&volume, &reader, workspace, sizeof(workspace), 0),
              TFAF_EFORMAT);

  test_disk_init(&disk, 32, 8);
  test_disk_finish_table(&disk);
  reader = test_disk_reader(&disk);
  tfaf1_init(&volume);
  disk.bytes[FORMAT_OFFSET] ^= 1;
  TEST_STATUS(tfaf1_mount(&volume, &reader, workspace, sizeof(workspace), 0),
              TFAF_EFORMAT);

  test_disk_init(&disk, 32, 8);
  test_disk_finish_table(&disk);
  reader = test_disk_reader(&disk);
  tfaf1_init(&volume);
  put16(disk.bytes + BLOCK_SIZE_OFFSET, 31);
  TEST_STATUS(tfaf1_mount(&volume, &reader, workspace, sizeof(workspace), 0),
              TFAF_ECORRUPT);

  test_disk_init(&disk, 32, 8);
  test_disk_finish_table(&disk);
  reader = test_disk_reader(&disk);
  tfaf1_init(&volume);
  put16(disk.bytes + BLOCK_COUNT_OFFSET, 7);
  TEST_STATUS(tfaf1_mount(&volume, &reader, workspace, sizeof(workspace), 0),
              TFAF_ECORRUPT);

  test_disk_init(&disk, 32, 8);
  test_disk_finish_table(&disk);
  reader = test_disk_reader(&disk);
  reader.block_size = 15;
  tfaf1_init(&volume);
  TEST_STATUS(tfaf1_mount(&volume, &reader, workspace, sizeof(workspace), 0),
              TFAF_ECORRUPT);

  test_disk_init(&disk, 32, 8);
  test_disk_finish_table(&disk);
  reader = test_disk_reader(&disk);
  reader.block_size = 65536;
  tfaf1_init(&volume);
  TEST_STATUS(tfaf1_mount(&volume, &reader, workspace, sizeof(workspace), 0),
              TFAF_ECORRUPT);

  test_disk_init(&disk, 32, 8);
  test_disk_finish_table(&disk);
  reader = test_disk_reader(&disk);
  reader.block_count = 0;
  tfaf1_init(&volume);
  TEST_STATUS(tfaf1_mount(&volume, &reader, workspace, sizeof(workspace), 0),
              TFAF_ECORRUPT);

  test_disk_init(&disk, 32, 8);
  test_disk_finish_table(&disk);
  reader = test_disk_reader(&disk);
  reader.block_count = 65536;
  tfaf1_init(&volume);
  TEST_STATUS(tfaf1_mount(&volume, &reader, workspace, sizeof(workspace), 0),
              TFAF_ECORRUPT);

  test_disk_init(&disk, 32, 8);
  test_disk_finish_table(&disk);
  reader = test_disk_reader(&disk);
  tfaf1_init(&volume);
  put16(disk.bytes + CONTENTS_BLOCK_OFFSET, 9);
  TEST_STATUS(tfaf1_mount(&volume, &reader, workspace, sizeof(workspace), 0),
              TFAF_ECORRUPT);

  test_disk_init(&disk, 32, 8);
  test_disk_finish_table(&disk);
  reader = test_disk_reader(&disk);
  tfaf1_init(&volume);
  put16(disk.bytes + CONTENTS_BLOCK_OFFSET, 0);
  TEST_STATUS(tfaf1_mount(&volume, &reader, workspace, sizeof(workspace), 0),
              TFAF_ECORRUPT);

  test_disk_init(&disk, 16, 65535);
  test_disk_finish_table(&disk);
  put16(disk.bytes + CONTENTS_BLOCK_OFFSET, 65535);
  reader = test_disk_reader(&disk);
  tfaf1_init(&volume);
  TEST_STATUS(tfaf1_mount(&volume, &reader, workspace, sizeof(workspace), 0),
              TFAF_OK);
  tfaf1_unmount(&volume);

  test_disk_init(&disk, 32, 8);
  {
    static const uint8_t long_name[] = "this name crosses the table boundary";
    test_disk_add_entry(&disk, 0, 0, long_name,
                        (uint16_t)(sizeof(long_name) - 1));
  }
  reader = test_disk_reader(&disk);
  tfaf1_init(&volume);
  put16(disk.bytes + CONTENTS_BLOCK_OFFSET, 1);
  TEST_STATUS(tfaf1_mount(&volume, &reader, workspace, sizeof(workspace), 0),
              TFAF_ECORRUPT);
  return 1;
}

int test_entry_and_name_rejections(void) {
  static const uint8_t name[] = "x";
  test_disk disk;
  tfaf_volume volume;
  tfaf_reader reader;
  uint8_t workspace[TEST_MAX_BLOCK_SIZE];
  size_t entry;
  uint32_t contents;

  test_disk_init(&disk, 64, 16);
  entry = test_disk_add_entry(&disk, 1, 0, name, 1);
  contents = test_disk_finish_table(&disk);
  test_disk_set_entry_first(&disk, entry, (uint16_t)contents);
  reader = test_disk_reader(&disk);
  tfaf1_init(&volume);
  test_disk_set_entry_name_length(&disk, entry, 0);
  TEST_STATUS(tfaf1_mount(&volume, &reader, workspace, sizeof(workspace), 0),
              TFAF_ECORRUPT);

  test_disk_init(&disk, 64, 16);
  entry = test_disk_add_entry(&disk, 1, 0, name, 1);
  contents = test_disk_finish_table(&disk);
  test_disk_set_entry_first(&disk, entry, (uint16_t)contents);
  disk.bytes[entry + 8] = 0x1f;
  reader = test_disk_reader(&disk);
  tfaf1_init(&volume);
  TEST_STATUS(tfaf1_mount(&volume, &reader, workspace, sizeof(workspace), 0),
              TFAF_ECORRUPT);

  test_disk_init(&disk, 64, 16);
  entry = test_disk_add_entry(&disk, 1, 0, name, 1);
  contents = test_disk_finish_table(&disk);
  test_disk_set_entry_first(&disk, entry, (uint16_t)contents);
  disk.bytes[entry + 8] = 0x7f;
  reader = test_disk_reader(&disk);
  tfaf1_init(&volume);
  TEST_STATUS(tfaf1_mount(&volume, &reader, workspace, sizeof(workspace), 0),
              TFAF_ECORRUPT);

  test_disk_init(&disk, 64, 16);
  entry = test_disk_add_entry(&disk, 1, 0, name, 1);
  contents = test_disk_finish_table(&disk);
  test_disk_set_entry_first(&disk, entry, (uint16_t)contents);
  reader = test_disk_reader(&disk);
  tfaf1_init(&volume);
  test_disk_set_entry_name_length(&disk, entry, 100);
  TEST_STATUS(tfaf1_mount(&volume, &reader, workspace, sizeof(workspace), 0),
              TFAF_ECORRUPT);

  test_disk_init(&disk, 64, 16);
  entry = test_disk_add_entry(&disk, 1, 0, name, 1);
  contents = test_disk_finish_table(&disk);
  test_disk_set_entry_first(&disk, entry, (uint16_t)contents);
  reader = test_disk_reader(&disk);
  tfaf1_init(&volume);
  put16(disk.bytes + ENTRY_COUNT_OFFSET, 2);
  TEST_STATUS(tfaf1_mount(&volume, &reader, workspace, sizeof(workspace), 0),
              TFAF_ECORRUPT);

  test_disk_init(&disk, 64, 16);
  {
    static const uint8_t duplicate[] = "same";
    size_t first = test_disk_add_entry(&disk, 0, 0, duplicate, 4);
    size_t second = test_disk_add_entry(&disk, 0, 0, duplicate, 4);
    uint32_t first_data = test_disk_finish_table(&disk);
    test_disk_set_entry_first(&disk, first, (uint16_t)first_data);
    test_disk_set_entry_first(&disk, second, (uint16_t)first_data);
  }
  contents = (uint16_t)disk.bytes[14] | ((uint16_t)disk.bytes[15] << 8);
  reader = test_disk_reader(&disk);
  tfaf1_init(&volume);
  TEST_STATUS(tfaf1_mount(&volume, &reader, workspace, sizeof(workspace), 0),
              TFAF_ECORRUPT);

  test_disk_init(&disk, 64, 16);
  {
    static const uint8_t upper[] = "Name";
    static const uint8_t lower[] = "name";
    size_t first = test_disk_add_entry(&disk, 0, 0, upper, 4);
    size_t second = test_disk_add_entry(&disk, 0, 0, lower, 4);
    uint32_t first_data = test_disk_finish_table(&disk);
    test_disk_set_entry_first(&disk, first, (uint16_t)first_data);
    test_disk_set_entry_first(&disk, second, (uint16_t)first_data);
  }
  contents = (uint16_t)disk.bytes[14] | ((uint16_t)disk.bytes[15] << 8);
  (void)contents;
  reader = test_disk_reader(&disk);
  tfaf1_init(&volume);
  TEST_STATUS(tfaf1_mount(&volume, &reader, workspace, sizeof(workspace), 0),
              TFAF_OK);
  tfaf1_unmount(&volume);
  return 1;
}

int test_allocation_ranges(void) {
  static const uint8_t a[] = "a";
  static const uint8_t b[] = "b";
  test_disk disk;
  tfaf_volume volume;
  tfaf_reader reader;
  uint8_t workspace[TEST_MAX_BLOCK_SIZE];
  size_t first, second;
  uint32_t contents;

  test_disk_init(&disk, 64, 16);
  first = test_disk_add_entry(&disk, 65, 0, a, 1);
  second = test_disk_add_entry(&disk, 64, 0, b, 1);
  contents = test_disk_finish_table(&disk);
  TEST_ASSERT(contents == 1);
  test_disk_set_entry_first(&disk, first, 1);
  test_disk_set_entry_first(&disk, second, 2);
  reader = test_disk_reader(&disk);
  tfaf1_init(&volume);
  TEST_STATUS(tfaf1_mount(&volume, &reader, workspace, sizeof(workspace), 0),
              TFAF_ECORRUPT);

  test_disk_set_entry_first(&disk, second, 3);
  tfaf1_init(&volume);
  TEST_STATUS(tfaf1_mount(&volume, &reader, workspace, sizeof(workspace), 0),
              TFAF_OK);
  tfaf1_unmount(&volume);

  test_disk_set_entry_first(&disk, second, 2);
  test_disk_set_entry_size(&disk, first, 0);
  test_disk_set_entry_size(&disk, second, 64);
  tfaf1_init(&volume);
  TEST_STATUS(tfaf1_mount(&volume, &reader, workspace, sizeof(workspace), 0),
              TFAF_OK);
  tfaf1_unmount(&volume);

  test_disk_set_entry_size(&disk, first, 1);
  test_disk_set_entry_first(&disk, first, 0);
  tfaf1_init(&volume);
  TEST_STATUS(tfaf1_mount(&volume, &reader, workspace, sizeof(workspace), 0),
              TFAF_ECORRUPT);

  test_disk_set_entry_first(&disk, first, 1);
  test_disk_set_entry_first(&disk, second, 16);
  tfaf1_init(&volume);
  TEST_STATUS(tfaf1_mount(&volume, &reader, workspace, sizeof(workspace), 0),
              TFAF_ECORRUPT);

  test_disk_set_entry_first(&disk, second, 1);
  test_disk_set_entry_size(&disk, first, 0xffffffffu);
  tfaf1_init(&volume);
  TEST_STATUS(tfaf1_mount(&volume, &reader, workspace, sizeof(workspace), 0),
              TFAF_ECORRUPT);
  return 1;
}

int test_interface_handles_and_lifecycle(void) {
  static const uint8_t file_name[] = "file";
  test_disk disk_a, disk_b;
  tfaf_volume volume_a, volume_b, uninitialized = {0};
  tfaf_reader reader_a, reader_b;
  tfaf_file file, copied;
  tfaf_cursor cursor;
  uint8_t workspace_a[TEST_MAX_BLOCK_SIZE];
  uint8_t workspace_b[TEST_MAX_BLOCK_SIZE];
  uint8_t output[8];
  size_t bytes_read;
  size_t entry;
  uint32_t contents;

  test_disk_init(&disk_a, 64, 8);
  reader_a.read = test_read_block;
  reader_a.context = &disk_a;
  reader_a.block_size = 64;
  reader_a.block_count = 8;
  TEST_STATUS(tfaf1_mount(0, &reader_a, workspace_a, sizeof(workspace_a), 0),
              TFAF_EINVAL);
  TEST_STATUS(tfaf1_mount(&uninitialized, &reader_a, workspace_a,
                          sizeof(workspace_a), 0),
              TFAF_EINVAL);

  test_disk_init(&disk_a, 64, 8);
  entry = test_disk_add_entry(&disk_a, 4, 0, file_name, 4);
  contents = test_disk_finish_table(&disk_a);
  test_disk_set_entry_first(&disk_a, entry, (uint16_t)contents);
  memcpy(disk_a.bytes + (size_t)contents * 64, "data", 4);
  test_disk_init(&disk_b, 64, 8);
  entry = test_disk_add_entry(&disk_b, 4, 0, file_name, 4);
  contents = test_disk_finish_table(&disk_b);
  test_disk_set_entry_first(&disk_b, entry, (uint16_t)contents);
  memcpy(disk_b.bytes + (size_t)contents * 64, "data", 4);
  reader_a = test_disk_reader(&disk_a);
  reader_b = test_disk_reader(&disk_b);
  tfaf1_init(&volume_a);
  tfaf1_init(&volume_b);
  TEST_STATUS(tfaf1_mount(&volume_a, &reader_a, workspace_a,
                          sizeof(workspace_a), 0),
              TFAF_OK);
  TEST_STATUS(tfaf1_mount(&volume_b, &reader_b, workspace_b,
                          sizeof(workspace_b), 0),
              TFAF_OK);
  TEST_STATUS(tfaf1_lookup(&volume_a, file_name, 4, &file), TFAF_OK);
  TEST_STATUS(tfaf1_entry_at(&volume_a, 0, 0), TFAF_EINVAL);
  TEST_STATUS(tfaf1_entries_begin(&volume_a, 0), TFAF_EINVAL);
  TEST_STATUS(tfaf1_entries_next(&volume_a, &cursor, 0), TFAF_EINVAL);
  TEST_STATUS(tfaf1_lookup(&volume_a, file_name, 4, 0), TFAF_EINVAL);
  TEST_STATUS(tfaf1_lookup(&volume_a, 0, 4, &copied), TFAF_EINVAL);
  TEST_STATUS(tfaf1_lookup(&volume_a, 0, 0, &copied), TFAF_EINVAL);
  TEST_STATUS(tfaf1_file_name(&volume_a, &file, 0, sizeof(output), &bytes_read),
              TFAF_EINVAL);
  TEST_STATUS(tfaf1_file_name(&volume_a, &file, output, sizeof(output), 0),
              TFAF_EINVAL);
  TEST_STATUS(tfaf1_pread(&volume_a, 0, 0, output, sizeof(output), &bytes_read),
              TFAF_EINVAL);
  TEST_STATUS(tfaf1_pread(&volume_a, &file, 0, output, sizeof(output), 0),
              TFAF_EINVAL);
  TEST_STATUS(tfaf1_pread(&volume_b, &file, 0, output, sizeof(output),
                          &bytes_read),
              TFAF_EINVAL);

  copied = file;
  copied.first_block++;
  TEST_STATUS(tfaf1_pread(&volume_a, &copied, 0, output, sizeof(output),
                          &bytes_read),
              TFAF_EINVAL);
  TEST_STATUS(tfaf1_entries_begin(&volume_a, &cursor), TFAF_OK);
  TEST_STATUS(tfaf1_entries_next(&volume_b, &cursor, &copied), TFAF_EINVAL);
  TEST_STATUS(tfaf1_pread(&volume_a, &file, 0, 0, 1, &bytes_read), TFAF_EINVAL);
  TEST_STATUS(tfaf1_lookup(&volume_a, 0, 1, &copied), TFAF_EINVAL);
  {
    const uint8_t control_name[] = { 'x', 0x7f };
    TEST_STATUS(tfaf1_lookup(&volume_a, control_name, sizeof(control_name),
                             &copied),
                TFAF_EINVAL);
  }

  tfaf1_unmount(&volume_a);
  TEST_STATUS(tfaf1_pread(&volume_a, &file, 0, output, sizeof(output),
                          &bytes_read),
              TFAF_EINVAL);
  TEST_STATUS(tfaf1_mount(&volume_a, &reader_a, workspace_a,
                          sizeof(workspace_a), 0),
              TFAF_OK);
  TEST_STATUS(tfaf1_pread(&volume_a, &file, 0, output, sizeof(output),
                          &bytes_read),
              TFAF_EINVAL);
  tfaf1_unmount(&volume_a);
  tfaf1_unmount(&volume_b);
  return 1;
}

int test_io_errors_and_partial_progress(void) {
  static const uint8_t name[] = "stream";
  test_disk disk;
  tfaf_volume volume;
  tfaf_reader reader;
  uint8_t workspace[TEST_MAX_BLOCK_SIZE];
  uint8_t output[128];
  size_t entry, bytes_read;
  uint32_t contents;

  test_disk_init(&disk, 64, 12);
  disk.fail_block = 0;
  disk.fail_status = -1234;
  reader = test_disk_reader(&disk);
  tfaf1_init(&volume);
  TEST_STATUS(tfaf1_mount(&volume, &reader, workspace, sizeof(workspace), 0),
              TFAF_EIO);
  TEST_ASSERT(volume.raw_io_status == -1234);

  test_disk_init(&disk, 64, 12);
  entry = test_disk_add_entry(&disk, 128, 0, name, sizeof(name) - 1);
  contents = test_disk_finish_table(&disk);
  test_disk_set_entry_first(&disk, entry, (uint16_t)contents);
  memset(disk.bytes + (size_t)contents * 64, 0x31, 64);
  memset(disk.bytes + (size_t)(contents + 1) * 64, 0x52, 64);
  reader = test_disk_reader(&disk);
  TEST_STATUS(test_mount(&disk, &volume, workspace, disk.block_size, 0),
              TFAF_OK);

  {
    tfaf_file file;
    TEST_STATUS(tfaf1_lookup(&volume, name, sizeof(name) - 1, &file), TFAF_OK);
    disk.fail_block = (int32_t)(contents + 1);
    disk.fail_status = -9876;
    memset(output, 0, sizeof(output));
    TEST_STATUS(tfaf1_pread(&volume, &file, 0, output, sizeof(output),
                            &bytes_read),
                TFAF_EIO);
    TEST_ASSERT(bytes_read == 64);
    for (size_t i = 0; i < 64; ++i)
      TEST_ASSERT(output[i] == 0x31);
    TEST_ASSERT(volume.raw_io_status == -9876);
  }
  tfaf1_unmount(&volume);
  return 1;
}

int test_validation_work_limit(void) {
  static const uint8_t name[] = "bounded-work";
  test_disk disk;
  tfaf_volume volume;
  tfaf_reader reader;
  uint8_t workspace[TEST_MAX_BLOCK_SIZE];
  size_t entry;
  uint32_t contents;

  test_disk_init(&disk, 64, 12);
  entry = test_disk_add_entry(&disk, 0, 0, name, sizeof(name) - 1);
  contents = test_disk_finish_table(&disk);
  test_disk_set_entry_first(&disk, entry, (uint16_t)contents);
  reader = test_disk_reader(&disk);
  tfaf1_init(&volume);
  TEST_STATUS(tfaf1_mount(&volume, &reader, workspace, sizeof(workspace), 1),
              TFAF_ERESOURCE);

  TEST_STATUS(test_mount(&disk, &volume, workspace, disk.block_size, 0),
              TFAF_OK);
  TEST_STATUS(tfaf1_validate(&volume, 1), TFAF_ERESOURCE);
  TEST_STATUS(tfaf1_validate(&volume, 0), TFAF_OK);
  tfaf1_unmount(&volume);
  return 1;
}
