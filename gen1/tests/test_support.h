#ifndef TFAF1_TEST_SUPPORT_H
#define TFAF1_TEST_SUPPORT_H

#include "tfaf1.h"

#include <stddef.h>
#include <stdint.h>

#define TEST_MAX_BLOCK_SIZE 256u
#define TEST_MAX_BLOCK_COUNT 64u

typedef struct {
  uint8_t bytes[TEST_MAX_BLOCK_SIZE * TEST_MAX_BLOCK_COUNT];
  uint32_t block_size;
  uint32_t block_count;
  uint32_t reads;
  int32_t fail_block;
  int32_t fail_status;
  uint64_t table_end;
  uint16_t entry_count;
} test_disk;

typedef int (*test_fn)(void);
typedef struct {
  const char *name;
  test_fn run;
} test_case;

int test_failure(const char *file, int line, const char *expression);
int test_status_failure(const char *file, int line, const char *expression,
                        tfaf_status actual, tfaf_status expected);

#define TEST_ASSERT(condition)                                                  \
  do {                                                                          \
    if (!(condition))                                                           \
      return test_failure(__FILE__, __LINE__, #condition);                      \
  } while (0)

#define TEST_STATUS(expression, expected_status)                               \
  do {                                                                          \
    tfaf_status test_actual_status = (expression);                             \
    tfaf_status test_expected_status = (expected_status);                       \
    if (test_actual_status != test_expected_status)                             \
      return test_status_failure(__FILE__, __LINE__, #expression,               \
                                 test_actual_status, test_expected_status);      \
  } while (0)

void test_disk_init(test_disk *disk, uint32_t block_size,
                    uint32_t block_count);
size_t test_disk_add_entry(test_disk *disk, uint32_t size, uint16_t first_block,
                           const uint8_t *name, uint16_t name_length);
uint32_t test_disk_finish_table(test_disk *disk);
void test_disk_set_entry_first(test_disk *disk, size_t entry_offset,
                               uint16_t first_block);
void test_disk_set_entry_size(test_disk *disk, size_t entry_offset,
                              uint32_t size);
void test_disk_set_entry_name_length(test_disk *disk, size_t entry_offset,
                                     uint16_t name_length);
tfaf_reader test_disk_reader(test_disk *disk);
int test_read_block(void *context, uint32_t block, uint8_t *destination,
                    size_t capacity, int32_t *raw_status);
tfaf_status test_mount(test_disk *disk, tfaf_volume *volume, void *workspace,
                       size_t workspace_size, uint64_t work_limit);

int test_workspace_and_empty_image(void);
int test_table_iteration_lookup_and_reading(void);
int test_odd_block_size_and_name_edges(void);
int test_empty_files_and_gaps(void);
int test_header_rejections(void);
int test_entry_and_name_rejections(void);
int test_allocation_ranges(void);
int test_interface_handles_and_lifecycle(void);
int test_io_errors_and_partial_progress(void);
int test_validation_work_limit(void);

int test_hal_open_and_reader(void);
int test_hal_root_selection(void);

#endif
