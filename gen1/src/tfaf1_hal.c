#include "tfaf1_hal.h"
#include "../dependencies/hal.h"

#define OP_ROOT 0x00
#define OP_BLOCK_SIZE 0x10
#define OP_BLOCK_COUNT 0x11
#define OP_READ_BLOCK 0x20
#define SHORT_RESPONSE (-10001)

static uint32_t read_le32(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
         ((uint32_t)p[3] << 24);
}
static void write_le32(uint8_t *p, uint32_t x) {
  p[0] = (uint8_t)x;
  p[1] = (uint8_t)(x >> 8);
  p[2] = (uint8_t)(x >> 16);
  p[3] = (uint8_t)(x >> 24);
}
static int call_exact(tfaf_hal_device *d, int32_t operation, const void *args,
                      int32_t args_size, void *response,
                      int32_t response_capacity, int32_t expected) {
  static uint8_t empty;
  int32_t result = hal_device_call(d->uuid, operation,
                                   args ? args : &empty, args_size,
                                   response ? response : &empty,
                                   response_capacity);
  d->raw_status = result;
  if (result < 0)
    return -1;
  if (result != expected) {
    d->raw_status = SHORT_RESPONSE;
    return -1;
  }
  return 0;
}
tfaf_status tfaf1_hal_open(tfaf_hal_device *d, const uint8_t uuid[16]) {
  uint8_t response[4];
  if (!d || !uuid)
    return TFAF_EINVAL;
  for (size_t i = 0; i < 16; ++i)
    d->uuid[i] = uuid[i];
  d->block_size = d->block_count = 0;
  d->raw_status = 0;
  if (call_exact(d, OP_BLOCK_SIZE, 0, 0, response, 4, 4))
    return TFAF_EIO;
  d->block_size = read_le32(response);
  if (call_exact(d, OP_BLOCK_COUNT, 0, 0, response, 4, 4)) {
    d->block_size = 0;
    return TFAF_EIO;
  }
  d->block_count = read_le32(response);
  if (d->block_size < 16 || d->block_size > 65535 || !d->block_count ||
      d->block_count > 65535) {
    d->block_size = d->block_count = 0;
    return TFAF_ECORRUPT;
  }
  return TFAF_OK;
}
static int read_one(void *context, uint32_t block, uint8_t *destination,
                    size_t capacity, int32_t *raw_status) {
  tfaf_hal_device *d = (tfaf_hal_device *)context;
  uint8_t argument[4];
  static uint8_t empty;
  int32_t result;
  if (!d || !destination || block >= d->block_count ||
      capacity < d->block_size || d->block_size > 2147483647u) {
    if (raw_status)
      *raw_status = SHORT_RESPONSE;
    if (d)
      d->raw_status = SHORT_RESPONSE;
    return -1;
  }
  write_le32(argument, block);
  result = hal_device_call(d->uuid, OP_READ_BLOCK, argument, 4,
                           destination ? destination : &empty,
                           (int32_t)d->block_size);
  d->raw_status = result;
  if (raw_status)
    *raw_status = result;
  if (result < 0)
    return -1;
  if ((uint32_t)result != d->block_size) {
    d->raw_status = SHORT_RESPONSE;
    if (raw_status)
      *raw_status = SHORT_RESPONSE;
    return -1;
  }
  return 0;
}
tfaf_reader tfaf1_hal_reader(tfaf_hal_device *d) {
  tfaf_reader reader;
  reader.read = read_one;
  reader.context = d;
  reader.block_size = d ? d->block_size : 0;
  reader.block_count = d ? d->block_count : 0;
  return reader;
}
tfaf_status tfaf1_hal_select_root(tfaf_hal_device *out, uint32_t max_devices) {
  int32_t count;
  uint32_t roots = 0;
  uint8_t selected[16];
  if (!out || !max_devices)
    return TFAF_EINVAL;
  for (size_t i = 0; i < sizeof(*out); ++i)
    ((uint8_t *)out)[i] = 0;
  count = hal_device_count();
  if (count < 0) {
    out->raw_status = count;
    return TFAF_EIO;
  }
  if ((uint32_t)count > max_devices)
    return TFAF_ERESOURCE;
  for (int32_t i = 0; i < count; ++i) {
    uint8_t info[20], root = 0;
    int32_t result = hal_device_info(i, info, (int32_t)sizeof(info));
    if (result != 0) {
      out->raw_status = result;
      return TFAF_EIO;
    }
    if (read_le32(info) != 0x0100u)
      continue;
    static uint8_t empty;
    result = hal_device_call(info + 4, OP_ROOT, &empty, 0, &root, 1);
    if (result < 0) {
      out->raw_status = result;
      return TFAF_EIO;
    }
    if (result != 1 || root > 1) {
      out->raw_status = SHORT_RESPONSE;
      return TFAF_EIO;
    }
    if (!root)
      continue;
    if (++roots > 1)
      return TFAF_ECORRUPT;
    for (size_t k = 0; k < sizeof(selected); ++k)
      selected[k] = info[4 + k];
  }
  if (!roots)
    return TFAF_ENOTFOUND;
  return tfaf1_hal_open(out, selected);
}

