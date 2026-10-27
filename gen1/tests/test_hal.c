#include "test_support.h"
#include "tfaf1_hal.h"
#include "../dependencies/hal.h"

#include <string.h>

#define FAKE_MAX_DEVICES 4
#define FAKE_OP_ROOT 0x00
#define FAKE_OP_BLOCK_SIZE 0x10
#define FAKE_OP_BLOCK_COUNT 0x11
#define FAKE_OP_READ_BLOCK 0x20

typedef struct {
  uint8_t uuid[16];
  uint32_t interface_id;
  uint32_t block_size;
  uint32_t block_count;
  uint8_t is_root;
} fake_hal_device;

static fake_hal_device fake_devices[FAKE_MAX_DEVICES];
static int32_t fake_count;
static int32_t fake_count_status;
static int32_t fake_info_status;
static int32_t fake_call_operation;
static int32_t fake_call_status;
static int32_t fake_short_operation;
static uint8_t fake_short_root;
static test_disk fake_disk;

static void put_le32(uint8_t *destination, uint32_t value) {
  destination[0] = (uint8_t)value;
  destination[1] = (uint8_t)(value >> 8);
  destination[2] = (uint8_t)(value >> 16);
  destination[3] = (uint8_t)(value >> 24);
}

static uint32_t get_le32(const uint8_t *source) {
  return (uint32_t)source[0] | ((uint32_t)source[1] << 8) |
         ((uint32_t)source[2] << 16) | ((uint32_t)source[3] << 24);
}

static void fake_hal_reset(void) {
  memset(fake_devices, 0, sizeof(fake_devices));
  fake_count = 0;
  fake_count_status = 0;
  fake_info_status = 0;
  fake_call_operation = -1;
  fake_call_status = 0;
  fake_short_operation = -1;
  fake_short_root = 0;
  test_disk_init(&fake_disk, 64, 8);
  test_disk_finish_table(&fake_disk);
}

static void fake_hal_add(uint32_t interface_id, uint8_t root,
                         uint8_t uuid_seed) {
  fake_hal_device *device = &fake_devices[fake_count++];
  device->interface_id = interface_id;
  device->block_size = fake_disk.block_size;
  device->block_count = fake_disk.block_count;
  device->is_root = root;
  for (size_t i = 0; i < sizeof(device->uuid); ++i)
    device->uuid[i] = (uint8_t)(uuid_seed + i);
}

int32_t hal_device_count(void) {
  return fake_count_status ? fake_count_status : fake_count;
}

int32_t hal_device_info(int32_t index, void *output, int32_t output_capacity) {
  if (fake_info_status)
    return fake_info_status;
  if (index < 0 || index >= fake_count || !output || output_capacity < 20)
    return -7003;
  put_le32((uint8_t *)output, fake_devices[index].interface_id);
  memcpy((uint8_t *)output + 4, fake_devices[index].uuid, 16);
  return 0;
}

int32_t hal_device_call(const uint8_t uuid[16], int32_t operation,
                        const void *arguments, int32_t arguments_len,
                        void *response, int32_t response_capacity) {
  fake_hal_device *device = 0;
  for (int32_t i = 0; i < fake_count; ++i) {
    if (memcmp(uuid, fake_devices[i].uuid, 16) == 0) {
      device = &fake_devices[i];
      break;
    }
  }
  if (!device)
    return -7004;
  if (operation == fake_call_operation)
    return fake_call_status;
  if (operation == FAKE_OP_ROOT) {
    if (!response || response_capacity < 1)
      return -7005;
    *(uint8_t *)response = fake_short_root ? fake_short_root : device->is_root;
    return 1;
  }
  if (operation == FAKE_OP_BLOCK_SIZE || operation == FAKE_OP_BLOCK_COUNT) {
    if (!response || response_capacity < 4)
      return -7006;
    if (fake_short_operation == operation)
      return 3;
    put_le32((uint8_t *)response,
             operation == FAKE_OP_BLOCK_SIZE ? device->block_size
                                             : device->block_count);
    return 4;
  }
  if (operation == FAKE_OP_READ_BLOCK) {
    uint32_t block;
    if (!arguments || arguments_len != 4 || !response ||
        response_capacity < (int32_t)device->block_size)
      return -7007;
    block = get_le32((const uint8_t *)arguments);
    if (fake_disk.fail_block == (int32_t)block)
      return fake_disk.fail_status;
    if (block >= fake_disk.block_count)
      return -7008;
    if (fake_short_operation == operation)
      return (int32_t)device->block_size - 1;
    memcpy(response, fake_disk.bytes + (size_t)block * device->block_size,
           device->block_size);
    return (int32_t)device->block_size;
  }
  return -7009;
}

int test_hal_open_and_reader(void) {
  tfaf_hal_device device;
  tfaf_reader reader;
  tfaf_volume volume;
  uint8_t workspace[TEST_MAX_BLOCK_SIZE];
  int32_t raw_status = 0;

  fake_hal_reset();
  fake_hal_add(HAL_INTERFACE_DISK, 1, 0x20);
  TEST_STATUS(tfaf1_hal_open(&device, fake_devices[0].uuid), TFAF_OK);
  TEST_ASSERT(device.block_size == 64 && device.block_count == 8);
  reader = tfaf1_hal_reader(&device);
  tfaf1_init(&volume);
  TEST_STATUS(tfaf1_mount(&volume, &reader, workspace, sizeof(workspace), 0),
              TFAF_OK);
  tfaf1_unmount(&volume);

  fake_disk.fail_block = 0;
  fake_disk.fail_status = -8123;
  TEST_ASSERT(reader.read(reader.context, 0, workspace, sizeof(workspace),
                          &raw_status) != 0);
  TEST_ASSERT(raw_status == -8123 && device.raw_status == -8123);

  fake_hal_reset();
  fake_hal_add(HAL_INTERFACE_DISK, 0, 0x30);
  fake_short_operation = FAKE_OP_BLOCK_SIZE;
  TEST_STATUS(tfaf1_hal_open(&device, fake_devices[0].uuid), TFAF_EIO);
  TEST_ASSERT(device.raw_status < 0);

  fake_hal_reset();
  fake_hal_add(HAL_INTERFACE_DISK, 0, 0x40);
  fake_devices[0].block_size = 15;
  TEST_STATUS(tfaf1_hal_open(&device, fake_devices[0].uuid), TFAF_ECORRUPT);
  TEST_ASSERT(device.block_size == 0 && device.block_count == 0);

  fake_hal_reset();
  fake_hal_add(HAL_INTERFACE_DISK, 0, 0x50);
  fake_call_operation = FAKE_OP_BLOCK_COUNT;
  fake_call_status = -8124;
  TEST_STATUS(tfaf1_hal_open(&device, fake_devices[0].uuid), TFAF_EIO);
  TEST_ASSERT(device.raw_status == -8124 && device.block_size == 0);
  return 1;
}

int test_hal_root_selection(void) {
  tfaf_hal_device device;
  uint8_t invalid_root = 2;

  fake_hal_reset();
  TEST_STATUS(tfaf1_hal_select_root(&device, 4), TFAF_ENOTFOUND);
  TEST_STATUS(tfaf1_hal_select_root(&device, 0), TFAF_EINVAL);

  fake_hal_reset();
  fake_count_status = -8333;
  TEST_STATUS(tfaf1_hal_select_root(&device, 4), TFAF_EIO);
  TEST_ASSERT(device.raw_status == -8333);

  fake_hal_reset();
  fake_count = 5;
  TEST_STATUS(tfaf1_hal_select_root(&device, 4), TFAF_ERESOURCE);

  fake_hal_reset();
  fake_hal_add(0x0200u, 1, 0x10);
  TEST_STATUS(tfaf1_hal_select_root(&device, 4), TFAF_ENOTFOUND);

  fake_hal_reset();
  fake_hal_add(HAL_INTERFACE_DISK, 1, 0x10);
  fake_hal_add(HAL_INTERFACE_DISK, 1, 0x40);
  TEST_STATUS(tfaf1_hal_select_root(&device, 4), TFAF_ECORRUPT);

  fake_hal_reset();
  fake_hal_add(HAL_INTERFACE_DISK, 1, 0x10);
  fake_short_root = invalid_root;
  TEST_STATUS(tfaf1_hal_select_root(&device, 4), TFAF_EIO);
  TEST_ASSERT(device.raw_status < 0);

  fake_hal_reset();
  fake_hal_add(HAL_INTERFACE_DISK, 1, 0x10);
  fake_info_status = -8444;
  TEST_STATUS(tfaf1_hal_select_root(&device, 4), TFAF_EIO);
  TEST_ASSERT(device.raw_status == -8444);

  fake_hal_reset();
  fake_hal_add(HAL_INTERFACE_DISK, 1, 0x70);
  TEST_STATUS(tfaf1_hal_select_root(&device, 4), TFAF_OK);
  TEST_ASSERT(memcmp(device.uuid, fake_devices[0].uuid, 16) == 0);
  return 1;
}
