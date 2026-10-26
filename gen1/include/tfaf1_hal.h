#ifndef TFAF1_HAL_H
#define TFAF1_HAL_H
#include "tfaf1.h"

typedef struct {
  uint8_t uuid[16];
  uint32_t block_size;
  uint32_t block_count;
  int32_t raw_status;
} tfaf_hal_device;

tfaf_status tfaf1_hal_open(tfaf_hal_device *device, const uint8_t uuid[16]);
tfaf_status tfaf1_hal_select_root(tfaf_hal_device *device,
                                    uint32_t max_devices);
tfaf_reader tfaf1_hal_reader(tfaf_hal_device *device);

#endif
