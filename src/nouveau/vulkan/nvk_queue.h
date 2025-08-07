/*
 * Copyright © 2022 Collabora Ltd. and Red Hat Inc.
 * SPDX-License-Identifier: MIT
 */
#ifndef NVK_QUEUE_H
#define NVK_QUEUE_H 1

#include "nvk_mem_stream.h"

#include "vk_queue.h"
#include "nvkmd/nvkmd.h"

struct nouveau_ws_bo;
struct nouveau_ws_context;
struct novueau_ws_push;
struct nv_push;
struct nvk_device;
struct nvkmd_mem;
struct nvkmd_ctx;

struct nvk_queue_state {
   struct {
      uint32_t alloc_count;
   } images;

   struct {
      uint32_t alloc_count;
   } samplers;

   struct {
      struct nvkmd_mem *mem;
      uint32_t bytes_per_warp;
      uint32_t bytes_per_tpc;
   } slm;
};

struct nvk_queue {
   struct vk_queue vk;

   enum nvkmd_engines engines;

   struct nvkmd_ctx *bind_ctx;
   struct nvkmd_ctx *exec_ctx;

   /* Memory stream to use for anything we need to push that isn't part of a
    * command buffer.
    */
   struct nvk_mem_stream push_stream;

   struct nvk_queue_state state;

   /* CB0 for all draw commands on this queue */
   struct nvkmd_mem *draw_cb0;
};

static inline struct nvk_device *
nvk_queue_device(struct nvk_queue *queue)
{
   return (struct nvk_device *)queue->vk.base.device;
}

static inline enum nvkmd_engines
nvk_queue_engines_from_queue_flags(VkQueueFlags queue_flags)
{
   enum nvkmd_engines engines = 0;
   if (queue_flags & VK_QUEUE_GRAPHICS_BIT) {
      engines |= NVKMD_ENGINE_3D;
      /* We rely on compute shaders for queries */
      engines |= NVKMD_ENGINE_COMPUTE;
   }
   if (queue_flags & VK_QUEUE_COMPUTE_BIT) {
      engines |= NVKMD_ENGINE_COMPUTE;
      /* We currently rely on 3D engine MMEs for indirect dispatch */
      engines |= NVKMD_ENGINE_3D;
   }
   if (queue_flags & VK_QUEUE_TRANSFER_BIT)
      engines |= NVKMD_ENGINE_COPY;

   return engines;
}

VkResult nvk_queue_create(struct nvk_device *dev,
                          const VkDeviceQueueCreateInfo *pCreateInfo,
                          uint32_t index_in_family);

void nvk_queue_destroy(struct nvk_device *dev, struct nvk_queue *queue);

VkResult nvk_push_draw_state_init(struct nvk_queue *queue,
                                  struct nv_push *p);

VkResult nvk_push_dispatch_state_init(struct nvk_queue *queue,
                                      struct nv_push *p);

#endif
