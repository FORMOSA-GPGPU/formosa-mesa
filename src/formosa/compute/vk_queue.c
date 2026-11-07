/* SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 * SPDX-License-Identifier: MIT
 */

#include <stdlib.h>
#include "util/cnd_monotonic.h"
#include "util/os_time.h"
#include "util/timespec.h"
#include "util/u_printf.h"
#include "vk_private.h"

struct fsa_sync {
   struct vk_sync vk;
   mtx_t mutex;
   struct u_cnd_monotonic changed;
   uint64_t value;
};

VkResult
fsa_cond_wait(struct vk_device *device, struct u_cnd_monotonic *cond,
              mtx_t *mutex, uint64_t deadline)
{
   if (vk_device_is_lost_no_report(device))
      return VK_ERROR_DEVICE_LOST;
   uint64_t now = os_time_get_nano();
   if (now >= deadline)
      return VK_TIMEOUT;
   /* Notice device loss without keeping a registry of live syncs/queries. */
   struct timespec timeout;
   timespec_from_nsec(&timeout, MIN2(deadline, now + 100000000ull));
   int ret = u_cnd_monotonic_timedwait(cond, mutex, &timeout);
   if (vk_device_is_lost_no_report(device))
      return VK_ERROR_DEVICE_LOST;
   return ret == thrd_error ? VK_ERROR_UNKNOWN : VK_SUCCESS;
}

static VkResult
sync_init(struct vk_device *device, struct vk_sync *vk, uint64_t initial)
{
   struct fsa_sync *sync = container_of(vk, struct fsa_sync, vk);
   if (mtx_init(&sync->mutex, mtx_plain) != thrd_success)
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   if (u_cnd_monotonic_init(&sync->changed) != thrd_success) {
      mtx_destroy(&sync->mutex);
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   }
   sync->value = initial;
   return VK_SUCCESS;
}

static void
sync_finish(struct vk_device *device, struct vk_sync *vk)
{
   struct fsa_sync *sync = container_of(vk, struct fsa_sync, vk);
   u_cnd_monotonic_destroy(&sync->changed);
   mtx_destroy(&sync->mutex);
}

static VkResult
sync_signal(struct vk_device *device, struct vk_sync *vk, uint64_t value)
{
   struct fsa_sync *sync = container_of(vk, struct fsa_sync, vk);
   mtx_lock(&sync->mutex);
   sync->value =
      (vk->flags & VK_SYNC_IS_TIMELINE) ? MAX2(sync->value, value) : 1;
   u_cnd_monotonic_broadcast(&sync->changed);
   mtx_unlock(&sync->mutex);
   return VK_SUCCESS;
}

static VkResult
sync_reset(struct vk_device *device, struct vk_sync *vk)
{
   struct fsa_sync *sync = container_of(vk, struct fsa_sync, vk);
   mtx_lock(&sync->mutex);
   sync->value = 0;
   mtx_unlock(&sync->mutex);
   return VK_SUCCESS;
}

static VkResult
sync_wait(struct vk_device *device, struct vk_sync *vk, uint64_t value,
          enum vk_sync_wait_flags flags, uint64_t deadline)
{
   struct fsa_sync *sync = container_of(vk, struct fsa_sync, vk);
   VkResult result = VK_SUCCESS;
   mtx_lock(&sync->mutex);
   /* ponytail: pending waits use completion; separate pending payloads if
    * binary semaphore submission latency becomes significant. */
   uint64_t target = (vk->flags & VK_SYNC_IS_TIMELINE) ? value : 1;
   while (sync->value < target) {
      result = fsa_cond_wait(device, &sync->changed, &sync->mutex, deadline);
      if (result != VK_SUCCESS)
         break;
   }
   mtx_unlock(&sync->mutex);
   return result;
}

static VkResult
sync_get_value(struct vk_device *device, struct vk_sync *vk, uint64_t *value)
{
   struct fsa_sync *sync = container_of(vk, struct fsa_sync, vk);
   mtx_lock(&sync->mutex);
   *value = sync->value;
   mtx_unlock(&sync->mutex);
   return VK_SUCCESS;
}

static VkResult
sync_move(struct vk_device *device, struct vk_sync *destination,
          struct vk_sync *source)
{
   struct fsa_sync *dst = container_of(destination, struct fsa_sync, vk);
   struct fsa_sync *src = container_of(source, struct fsa_sync, vk);
   struct fsa_sync *first = (uintptr_t)dst < (uintptr_t)src ? dst : src;
   struct fsa_sync *second = first == dst ? src : dst;
   mtx_lock(&first->mutex);
   mtx_lock(&second->mutex);
   dst->value = src->value;
   src->value = 0;
   u_cnd_monotonic_broadcast(&dst->changed);
   mtx_unlock(&second->mutex);
   mtx_unlock(&first->mutex);
   return VK_SUCCESS;
}

const struct vk_sync_type fsa_sync_type = {
   .size = sizeof(struct fsa_sync),
   .features = VK_SYNC_FEATURE_BINARY | VK_SYNC_FEATURE_TIMELINE |
               VK_SYNC_FEATURE_WAIT_BEFORE_SIGNAL |
               VK_SYNC_FEATURE_WAIT_PENDING | VK_SYNC_FEATURE_GPU_WAIT |
               VK_SYNC_FEATURE_CPU_WAIT | VK_SYNC_FEATURE_CPU_RESET |
               VK_SYNC_FEATURE_CPU_SIGNAL,
   .init = sync_init,
   .finish = sync_finish,
   .signal = sync_signal,
   .reset = sync_reset,
   .move = sync_move,
   .get_value = sync_get_value,
   .wait = sync_wait,
};

static void
command_destroy(struct vk_command_buffer *buffer)
{
   vk_command_buffer_finish(buffer);
   vk_free(&buffer->pool->alloc, buffer);
}

static VkResult
command_create(struct vk_command_pool *pool, VkCommandBufferLevel level,
               struct vk_command_buffer **out)
{
   struct vk_command_buffer *buffer = vk_zalloc(
      &pool->alloc, sizeof(*buffer), 8, VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!buffer)
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   VkResult result =
      vk_command_buffer_init(pool, buffer, &fsa_command_buffer_ops, level);
   if (result != VK_SUCCESS) {
      vk_free(&pool->alloc, buffer);
      return result;
   }
   *out = buffer;
   return VK_SUCCESS;
}

static void
command_reset(struct vk_command_buffer *buffer, VkCommandBufferResetFlags flags)
{
   vk_command_buffer_reset(buffer);
}

const struct vk_command_buffer_ops fsa_command_buffer_ops = {
   .create = command_create,
   .destroy = command_destroy,
   .reset = command_reset,
};

VKAPI_ATTR VkResult VKAPI_CALL
fsa_BeginCommandBuffer(VkCommandBuffer handle,
                       const VkCommandBufferBeginInfo *info)
{
   VK_FROM_HANDLE(vk_command_buffer, buffer, handle);
   vk_command_buffer_begin(buffer, info);
   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
fsa_EndCommandBuffer(VkCommandBuffer handle)
{
   VK_FROM_HANDLE(vk_command_buffer, buffer, handle);
   return vk_command_buffer_end(buffer);
}

static bool
cache_buffers(const struct fsa_compute_args *args, bool flush)
{
   for (unsigned i = 0; i < FSA_COMPUTE_MAX_BUFFERS; i++) {
      if (!args->buffers[i].size)
         continue;
      FsaMemoryRange range = {args->buffers[i].address, args->buffers[i].size};
      FsaCompletionToken token = 0;
      FsaCommandSubmitStatus status =
         flush ? fsa_cmd_cache_flush(&range, &token)
               : fsa_cmd_cache_invalidate(&range, &token);
      if (!fsa_wait(status, token))
         return false;
   }
   return true;
}

static bool
dispatch(struct fsa_pipeline *pipeline, struct fsa_compute_args *args,
         uint32_t x, uint32_t y, uint32_t z)
{
   if (!x || !y || !z)
      return true;
   if (!pipeline || x > 65535 || y > 65535 || z > 65535 ||
       (uint64_t)x * y * z > INT32_MAX)
      return false;
   void *arg_address = NULL, *status_address = NULL;
   if (fsa_malloc(&arg_address, sizeof(*args)) ||
       fsa_malloc(&status_address, sizeof(FsaKernelStatus))) {
      if (arg_address)
         fsa_free(arg_address);
      return false;
   }
   void *scratch_address = NULL;
   uint64_t scratch_size = 0, invocations = (uint64_t)x * y * z;
   args->scratch_address = 0;
   args->scratch_stride = pipeline->binary.scratch_size;
   if (args->scratch_stride) {
      bool fits = true;
      for (unsigned i = 0; i < 3; i++)
         fits =
            fits && !__builtin_mul_overflow(
                       invocations, (uint64_t)pipeline->binary.local_size[i],
                       &invocations);
      fits =
         fits && !__builtin_mul_overflow(
                    invocations, (uint64_t)args->scratch_stride, &scratch_size);
      if (!fits || scratch_size > SIZE_MAX ||
          fsa_malloc(&scratch_address, scratch_size)) {
         fsa_free(status_address);
         fsa_free(arg_address);
         return false;
      }
      args->scratch_address = (uintptr_t)scratch_address;
   }
   void *printf_address = NULL;
   uint32_t printf_header[2] = {8, 0};
   args->printf_address = 0;
   if (pipeline->binary.uses_printf) {
      if (fsa_malloc(&printf_address, FSA_COMPUTE_PRINTF_SIZE) ||
          fsa_copy_to_dev((uintptr_t)printf_address, printf_header,
                          sizeof(printf_header))) {
         if (printf_address)
            fsa_free(printf_address);
         if (scratch_address)
            fsa_free(scratch_address);
         fsa_free(status_address);
         fsa_free(arg_address);
         return false;
      }
      args->printf_address = (uintptr_t)printf_address;
   }
   FsaKernelStatus kernel_status = {0};
   bool ok =
      fsa_copy_to_dev((uintptr_t)arg_address, args, sizeof(*args)) == 0 &&
      fsa_copy_to_dev((uintptr_t)status_address, &kernel_status,
                      sizeof(kernel_status)) == 0 &&
      cache_buffers(args, false);
   if (ok) {
      FsaKernelLaunchInfo launch = {
         .struct_size = sizeof(launch),
         .dimensions = 3,
         .num_groups = {x, y, z},
         .local_mem_size = pipeline->binary.shared_size,
         /* LSAR is enabled by default. */
         .enable_stack_remap = 1,
         .kernel_entry = pipeline->entry,
         .kernarg_address = (uintptr_t)arg_address,
         .kernel_status = (uintptr_t)status_address,
      };
      memcpy(launch.local_size, pipeline->binary.local_size,
             sizeof(launch.local_size));
      FsaCompletionToken token = 0;
      FsaCommandSubmitStatus status = fsa_cmd_start_kernel(&launch, &token);
      ok = fsa_wait(status, token);
      if (ok)
         ok = cache_buffers(args, true);
   }
   if (ok && scratch_address) {
      FsaMemoryRange range = {(uintptr_t)scratch_address, scratch_size};
      FsaCompletionToken token = 0;
      FsaCommandSubmitStatus status = fsa_cmd_cache_flush(&range, &token);
      ok = fsa_wait(status, token);
   }
   if (ok && printf_address) {
      ok = fsa_copy_from_dev((uintptr_t)printf_address, printf_header,
                             sizeof(printf_header)) == 0;
      if (ok && printf_header[0] >= 8 &&
          printf_header[0] <= FSA_COMPUTE_PRINTF_SIZE) {
         size_t size = printf_header[0] - 8;
         if (size) {
            char *data = malloc(size);
            ok = data && fsa_copy_from_dev((uintptr_t)printf_address + 8, data,
                                           size) == 0;
            if (ok)
               u_printf(stdout, data, size, NULL, 0);
            free(data);
         }
      } else if (ok) {
         fprintf(stderr, "Formosa: shader printf buffer overflow\n");
      }
   }
   if (fsa_hal_is_available()) {
      if (printf_address)
         fsa_free(printf_address);
      if (scratch_address)
         fsa_free(scratch_address);
      fsa_free(status_address);
      fsa_free(arg_address);
   }
   return ok;
}

static bool
execute_commands(struct vk_command_buffer *buffer)
{
   struct fsa_pipeline *pipeline = NULL;
   struct fsa_descriptor_set *sets[FSA_COMPUTE_MAX_SETS] = {0};
   uint32_t dynamic[FSA_COMPUTE_MAX_SETS][FSA_COMPUTE_MAX_BUFFERS] = {0};
   struct fsa_compute_args args = {0};
   list_for_each_entry (struct vk_cmd_queue_entry, cmd, &buffer->cmd_queue.cmds,
                        cmd_link) {
      switch (cmd->type) {
      case VK_CMD_BIND_PIPELINE:
         if (cmd->u.bind_pipeline.pipeline_bind_point !=
             VK_PIPELINE_BIND_POINT_COMPUTE)
            return false;
         pipeline = fsa_pipeline_from_handle(cmd->u.bind_pipeline.pipeline);
         break;
      case VK_CMD_BIND_DESCRIPTOR_SETS2: {
         const VkBindDescriptorSetsInfoKHR *info =
            cmd->u.bind_descriptor_sets2.bind_descriptor_sets_info;
         if (info->firstSet + info->descriptorSetCount > FSA_COMPUTE_MAX_SETS)
            return false;
         unsigned index = 0;
         for (unsigned i = 0; i < info->descriptorSetCount; i++) {
            unsigned slot = info->firstSet + i;
            sets[slot] =
               fsa_descriptor_set_from_handle(info->pDescriptorSets[i]);
            memset(dynamic[slot], 0, sizeof(dynamic[slot]));
            for (unsigned b = 0; b < FSA_COMPUTE_MAX_BINDINGS; b++) {
               const struct fsa_descriptor_layout *layout = sets[slot]->layout;
               if (layout->bindings[b].type !=
                      VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC &&
                   layout->bindings[b].type !=
                      VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC)
                  continue;
               for (unsigned j = 0; j < layout->bindings[b].count; j++) {
                  if (index >= info->dynamicOffsetCount)
                     return false;
                  dynamic[slot][layout->bindings[b].offset + j] =
                     info->pDynamicOffsets[index++];
               }
            }
         }
         if (index != info->dynamicOffsetCount)
            return false;
         break;
      }
      case VK_CMD_PUSH_CONSTANTS2: {
         const VkPushConstantsInfoKHR *info =
            cmd->u.push_constants2.push_constants_info;
         if (info->offset > sizeof(args.push) ||
             info->size > sizeof(args.push) - info->offset)
            return false;
         memcpy(args.push + info->offset, info->pValues, info->size);
         break;
      }
      case VK_CMD_DISPATCH:
      case VK_CMD_DISPATCH_BASE:
      case VK_CMD_DISPATCH_INDIRECT: {
         memset(args.buffers, 0, sizeof(args.buffers));
         memset(args.bindings, 0, sizeof(args.bindings));
         memset(args.images, 0, sizeof(args.images));
         memset(args.samplers, 0, sizeof(args.samplers));
         unsigned cursor = 0;
         for (unsigned s = 0; s < FSA_COMPUTE_MAX_SETS; s++) {
            const struct fsa_descriptor_set *set = sets[s];
            if (!set)
               continue;
            if (set->layout->count > FSA_COMPUTE_MAX_BUFFERS - cursor)
               return false;
            for (unsigned b = 0; b < FSA_COMPUTE_MAX_BINDINGS; b++)
               args.bindings[s][b] = cursor + set->layout->bindings[b].offset;
            for (unsigned i = 0; i < set->layout->count; i++) {
               uint64_t offset = dynamic[s][i];
               if (offset > set->buffer_available[i] ||
                   set->buffers[i].size > set->buffer_available[i] - offset)
                  return false;
               args.buffers[cursor + i] = set->buffers[i];
               args.buffers[cursor + i].address += offset;
               args.images[cursor + i] = set->images[i];
               args.samplers[cursor + i] = set->samplers[i];
            }
            cursor += set->layout->count;
         }
         uint32_t groups[3];
         memset(args.base_group, 0, sizeof(args.base_group));
         if (cmd->type == VK_CMD_DISPATCH) {
            groups[0] = cmd->u.dispatch.group_count_x;
            groups[1] = cmd->u.dispatch.group_count_y;
            groups[2] = cmd->u.dispatch.group_count_z;
         } else if (cmd->type == VK_CMD_DISPATCH_BASE) {
            groups[0] = cmd->u.dispatch_base.group_count_x;
            groups[1] = cmd->u.dispatch_base.group_count_y;
            groups[2] = cmd->u.dispatch_base.group_count_z;
            args.base_group[0] = cmd->u.dispatch_base.base_group_x;
            args.base_group[1] = cmd->u.dispatch_base.base_group_y;
            args.base_group[2] = cmd->u.dispatch_base.base_group_z;
         } else {
            struct fsa_buffer *indirect =
               fsa_buffer_from_handle(cmd->u.dispatch_indirect.buffer);
            uint64_t offset = cmd->u.dispatch_indirect.offset;
            if (!indirect->memory || offset > indirect->vk.size ||
                sizeof(groups) > indirect->vk.size - offset ||
                fsa_copy_from_dev(fsa_buffer_address(indirect) + offset, groups,
                                  sizeof(groups)))
               return false;
         }
         if (!dispatch(pipeline, &args, groups[0], groups[1], groups[2]))
            return false;
         break;
      }
      case VK_CMD_EXECUTE_COMMANDS:
         for (unsigned i = 0; i < cmd->u.execute_commands.command_buffer_count;
              i++) {
            struct vk_command_buffer *secondary = vk_command_buffer_from_handle(
               cmd->u.execute_commands.command_buffers[i]);
            if (!execute_commands(secondary))
               return false;
         }
         break;
      case VK_CMD_FILL_BUFFER:
      case VK_CMD_UPDATE_BUFFER: {
         struct fsa_buffer *dst = fsa_buffer_from_handle(
            cmd->type == VK_CMD_FILL_BUFFER ? cmd->u.fill_buffer.dst_buffer
                                            : cmd->u.update_buffer.dst_buffer);
         uint64_t offset = cmd->type == VK_CMD_FILL_BUFFER
                              ? cmd->u.fill_buffer.dst_offset
                              : cmd->u.update_buffer.dst_offset;
         uint64_t size = cmd->type == VK_CMD_FILL_BUFFER
                            ? cmd->u.fill_buffer.size
                            : cmd->u.update_buffer.data_size;
         if (!dst->memory || offset > dst->vk.size)
            return false;
         if (size == VK_WHOLE_SIZE)
            size = (dst->vk.size - offset) & ~3ull;
         if (size > dst->vk.size - offset)
            return false;
         if (cmd->type == VK_CMD_UPDATE_BUFFER) {
            if (fsa_copy_to_dev(fsa_buffer_address(dst) + offset,
                                cmd->u.update_buffer.data, size))
               return false;
         } else {
            uint32_t data[1024];
            for (unsigned i = 0; i < ARRAY_SIZE(data); i++)
               data[i] = cmd->u.fill_buffer.data;
            while (size) {
               uint64_t chunk = MIN2(size, sizeof(data));
               if (fsa_copy_to_dev(fsa_buffer_address(dst) + offset, data,
                                   chunk))
                  return false;
               offset += chunk;
               size -= chunk;
            }
         }
         break;
      }
      case VK_CMD_RESET_QUERY_POOL:
      case VK_CMD_WRITE_TIMESTAMP2:
      case VK_CMD_COPY_QUERY_POOL_RESULTS:
      case VK_CMD_SET_EVENT2:
      case VK_CMD_RESET_EVENT2:
      case VK_CMD_WAIT_EVENTS2:
         if (!fsa_query_command(vk_device_to_handle(buffer->base.device), cmd))
            return false;
         break;
      case VK_CMD_COPY_IMAGE2:
      case VK_CMD_COPY_BUFFER_TO_IMAGE2:
      case VK_CMD_COPY_IMAGE_TO_BUFFER2:
      case VK_CMD_CLEAR_COLOR_IMAGE:
         if (!fsa_image_command(cmd))
            return false;
         break;
      case VK_CMD_COPY_BUFFER2: {
         const VkCopyBufferInfo2 *info = cmd->u.copy_buffer2.copy_buffer_info;
         struct fsa_buffer *src = fsa_buffer_from_handle(info->srcBuffer);
         struct fsa_buffer *dst = fsa_buffer_from_handle(info->dstBuffer);
         for (unsigned i = 0; i < info->regionCount; i++) {
            const VkBufferCopy2 *r = &info->pRegions[i];
            if (r->srcOffset > src->vk.size ||
                r->size > src->vk.size - r->srcOffset ||
                r->dstOffset > dst->vk.size ||
                r->size > dst->vk.size - r->dstOffset)
               return false;
            FsaMemoryCopyInfo copy = {
               .struct_size = sizeof(copy),
               .source = {kFsaMemoryDomainDevice,
                          {fsa_buffer_address(src) + r->srcOffset, r->size}},
               .destination = {kFsaMemoryDomainDevice,
                               {fsa_buffer_address(dst) + r->dstOffset,
                                r->size}},
            };
            FsaCompletionToken token = 0;
            FsaCommandSubmitStatus status = fsa_cmd_memory_copy(&copy, &token);
            if (!fsa_wait(status, token))
               return false;
         }
         break;
      }
      case VK_CMD_PIPELINE_BARRIER2: {
         /* Every operation completes and flushes its writes before the next. */
         FsaCompletionToken token = 0;
         FsaCommandSubmitStatus status = fsa_cmd_barrier(&token);
         if (!fsa_wait(status, token))
            return false;
         break;
      }
      default:
         return false;
      }
   }
   return true;
}

VkResult
fsa_queue_submit(struct vk_queue *queue, struct vk_queue_submit *submit)
{
   if (submit->buffer_bind_count || submit->image_bind_count ||
       submit->image_opaque_bind_count)
      return VK_ERROR_FEATURE_NOT_PRESENT;
   VkResult result =
      vk_sync_wait_many(queue->base.device, submit->wait_count, submit->waits,
                        VK_SYNC_WAIT_COMPLETE, UINT64_MAX);
   if (result != VK_SUCCESS)
      return result;
   for (unsigned i = 0; i < submit->command_buffer_count; i++) {
      if (!execute_commands(submit->command_buffers[i]))
         return vk_queue_set_lost(queue, "Formosa command execution failed");
   }
   for (unsigned i = 0; i < submit->signal_count; i++) {
      result = vk_sync_signal(queue->base.device, submit->signals[i].sync,
                              submit->signals[i].signal_value);
      if (result != VK_SUCCESS)
         return result;
   }
   return VK_SUCCESS;
}
