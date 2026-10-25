/* SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 * SPDX-License-Identifier: MIT
 */

#ifndef FSA_VK_PRIVATE_H
#define FSA_VK_PRIVATE_H

#include "util/cnd_monotonic.h"
#include <formosa-hal/api.h>
#include <formosa-hal/hal.h>
#include "compiler.h"
#include "fsa_compute.h"
#include "fsa_entrypoints.h"
#include "vk_alloc.h"
#include "vk_buffer.h"
#include "vk_buffer_view.h"
#include "vk_command_buffer.h"
#include "vk_command_pool.h"
#include "vk_descriptor_set_layout.h"
#include "vk_device.h"
#include "vk_device_memory.h"
#include "vk_image.h"
#include "vk_instance.h"
#include "vk_physical_device.h"
#include "vk_pipeline_layout.h"
#include "vk_query_pool.h"
#include "vk_queue.h"
#include "vk_shader_module.h"
#include "vk_sync.h"
#include "vk_util.h"

/* Match the checked PoCL image limits; allocation capacity comes from HAL. */
#define FSA_MAX_IMAGE_2D     8192
#define FSA_MAX_IMAGE_3D     2048
#define FSA_MAX_IMAGE_LAYERS 2048

struct fsa_physical_device {
   struct vk_physical_device vk;
   FsaDeviceDescription description;
   const struct vk_sync_type *sync_types[2];
};

struct fsa_device {
   struct vk_device vk;
   struct vk_queue queue;
   struct fsa_physical_device *physical;
};

struct fsa_memory {
   struct vk_device_memory vk;
   void *address;
   void *host;
   bool mapped;
};

struct fsa_buffer {
   struct vk_buffer vk;
   struct fsa_memory *memory;
   uint64_t offset;
};

struct fsa_pipeline {
   struct vk_object_base base;
   struct fsa_compute_binary binary;
   void *address;
   uint64_t entry;
};

struct fsa_descriptor_set {
   struct vk_object_base base;
   struct list_head link;
   struct fsa_descriptor_layout *layout;
   uint32_t samplers[FSA_COMPUTE_MAX_BUFFERS];
   struct fsa_compute_buffer buffers[FSA_COMPUTE_MAX_BUFFERS];
   uint64_t buffer_available[FSA_COMPUTE_MAX_BUFFERS];
   dev_image_t images[FSA_COMPUTE_MAX_BUFFERS];
};

struct fsa_image {
   struct vk_image vk;
   struct fsa_memory *memory;
   uint64_t offset;
   uint64_t size;
   uint64_t levels[16];
};

struct fsa_sampler {
   struct vk_object_base base;
   uint32_t value;
};

struct fsa_query_pool {
   struct vk_query_pool vk;
   mtx_t mutex;
   struct u_cnd_monotonic changed;
   struct {
      uint64_t value;
      bool available;
   } queries[];
};

struct fsa_event {
   struct vk_object_base base;
   struct vk_sync *sync;
};

VkResult fsa_cond_wait(struct vk_device *device, struct u_cnd_monotonic *cond,
                       mtx_t *mutex, uint64_t deadline);

struct fsa_descriptor_layout {
   struct vk_descriptor_set_layout vk;
   uint32_t count;
   uint32_t immutable[FSA_COMPUTE_MAX_BUFFERS];
   struct {
      VkDescriptorType type;
      VkShaderStageFlags stages;
      uint32_t count;
      uint32_t offset;
   } bindings[FSA_COMPUTE_MAX_BINDINGS];
};

struct fsa_descriptor_pool {
   struct vk_object_base base;
   struct list_head sets;
   uint32_t maximum;
   uint32_t count;
};

VK_DEFINE_NONDISP_HANDLE_CASTS(fsa_memory, vk.base, VkDeviceMemory,
                               VK_OBJECT_TYPE_DEVICE_MEMORY)
VK_DEFINE_NONDISP_HANDLE_CASTS(fsa_image, vk.base, VkImage,
                               VK_OBJECT_TYPE_IMAGE)
VK_DEFINE_NONDISP_HANDLE_CASTS(fsa_sampler, base, VkSampler,
                               VK_OBJECT_TYPE_SAMPLER)
VK_DEFINE_NONDISP_HANDLE_CASTS(fsa_query_pool, vk.base, VkQueryPool,
                               VK_OBJECT_TYPE_QUERY_POOL)
VK_DEFINE_NONDISP_HANDLE_CASTS(fsa_event, base, VkEvent, VK_OBJECT_TYPE_EVENT)
VK_DEFINE_NONDISP_HANDLE_CASTS(fsa_buffer, vk.base, VkBuffer,
                               VK_OBJECT_TYPE_BUFFER)
VK_DEFINE_NONDISP_HANDLE_CASTS(fsa_pipeline, base, VkPipeline,
                               VK_OBJECT_TYPE_PIPELINE)
VK_DEFINE_NONDISP_HANDLE_CASTS(fsa_descriptor_set, base, VkDescriptorSet,
                               VK_OBJECT_TYPE_DESCRIPTOR_SET)
VK_DEFINE_NONDISP_HANDLE_CASTS(fsa_descriptor_layout, vk.base,
                               VkDescriptorSetLayout,
                               VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT)
VK_DEFINE_NONDISP_HANDLE_CASTS(fsa_descriptor_pool, base, VkDescriptorPool,
                               VK_OBJECT_TYPE_DESCRIPTOR_POOL)

extern const struct vk_sync_type fsa_sync_type;
extern const struct vk_command_buffer_ops fsa_command_buffer_ops;
VkResult fsa_queue_submit(struct vk_queue *queue,
                          struct vk_queue_submit *submit);
bool fsa_upload_binary(struct fsa_pipeline *pipeline);
bool fsa_image_descriptor(struct vk_image_view *view, dev_image_t *out);
bool fsa_texel_descriptor(struct vk_buffer_view *view, dev_image_t *out);
bool fsa_image_command(const struct vk_cmd_queue_entry *cmd);
bool fsa_query_command(VkDevice device, const struct vk_cmd_queue_entry *cmd);
uint64_t fsa_image_address(struct fsa_image *image, uint32_t mip,
                           uint32_t layer, VkOffset3D offset);

static inline bool
fsa_wait(FsaCommandSubmitStatus status, FsaCompletionToken token)
{
   FsaCompletionResult result;
   return status == kFsaCommandSubmitAccepted &&
          fsa_wait_completion(token, 0, &result) == kFsaCompletionWaitSuccess &&
          result == FSA_COMPLETION_RESULT_SUCCESS;
}

static inline uint64_t
fsa_buffer_address(const struct fsa_buffer *buffer)
{
   return (uintptr_t)buffer->memory->address + buffer->offset;
}

#endif
