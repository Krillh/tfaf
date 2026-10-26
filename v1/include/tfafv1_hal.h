#ifndef TFAFV1_HAL_H
#define TFAFV1_HAL_H
#include "tfafv1.h"

typedef struct {
  uint8_t uuid[16];
  uint32_t block_size;
  uint32_t block_count;
  int32_t raw_status;
} tfaf_hal_device;

tfaf_status tfafv1_hal_open(tfaf_hal_device *device, const uint8_t uuid[16]);
tfaf_status tfafv1_hal_select_root(tfaf_hal_device *device,
                                    uint32_t max_devices);
tfaf_reader tfafv1_hal_reader(tfaf_hal_device *device);

#endif
