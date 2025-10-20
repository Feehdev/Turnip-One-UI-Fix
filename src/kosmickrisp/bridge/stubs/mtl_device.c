/*
 * Copyright 2025 LunarG, Inc.
 * Copyright 2025 Google LLC
 * SPDX-License-Identifier: MIT
 */

#include "mtl_device.h"

/* Device creation */
mtl_device *
mtl_device_create(void)
{
   return NULL;
}

/* Device operations */
void
mtl_start_gpu_capture(mtl_device *mtl_dev_handle)
{
}

void
mtl_stop_gpu_capture(void)
{
}

/* Device feature query */
void
mtl_device_get_name(mtl_device *dev, char buffer[256])
{
}

void
mtl_device_get_architecture_name(mtl_device *dev, char buffer[256])
{
}

uint64_t
mtl_device_get_peer_group_id(mtl_device *dev)
{
   return 0u;
}

uint32_t
mtl_device_get_peer_index(mtl_device *dev)
{
   return 0u;
}

uint64_t
mtl_device_get_registry_id(mtl_device *dev)
{
   return 0u;
}

struct mtl_size
mtl_device_max_threads_per_threadgroup(mtl_device *dev)
{
   return (struct mtl_size){};
}

/* Resource queries */
void
mtl_heap_buffer_size_and_align_with_length(mtl_device *device, uint64_t *size_B,
                                           uint64_t *align_B)
{
}

void
mtl_heap_texture_size_and_align_with_descriptor(mtl_device *device,
                                                struct kk_image_layout *layout)
{
}
