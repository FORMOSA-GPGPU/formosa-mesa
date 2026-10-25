/* SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 * SPDX-License-Identifier: MIT
 */

#include "vk_private.h"

VKAPI_ATTR VkResult VKAPI_CALL
fsa_CreateQueryPool(VkDevice handle, const VkQueryPoolCreateInfo *info,
                    const VkAllocationCallbacks *alloc, VkQueryPool *out)
{
   VK_FROM_HANDLE(vk_device, device, handle);
   if (info->queryType != VK_QUERY_TYPE_TIMESTAMP)
      return VK_ERROR_FEATURE_NOT_PRESENT;
   size_t size;
   if (__builtin_mul_overflow((size_t)info->queryCount,
                              sizeof(((struct fsa_query_pool *)0)->queries[0]),
                              &size) ||
       __builtin_add_overflow(size, sizeof(struct fsa_query_pool), &size))
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   struct fsa_query_pool *pool =
      vk_query_pool_create(device, info, alloc, size);
   if (!pool)
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   if (mtx_init(&pool->mutex, mtx_plain) != thrd_success) {
      vk_query_pool_destroy(device, alloc, &pool->vk);
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   }
   if (u_cnd_monotonic_init(&pool->changed) != thrd_success) {
      mtx_destroy(&pool->mutex);
      vk_query_pool_destroy(device, alloc, &pool->vk);
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   }
   *out = fsa_query_pool_to_handle(pool);
   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
fsa_DestroyQueryPool(VkDevice handle, VkQueryPool pool_handle,
                     const VkAllocationCallbacks *alloc)
{
   VK_FROM_HANDLE(vk_device, device, handle);
   VK_FROM_HANDLE(fsa_query_pool, pool, pool_handle);
   if (!pool)
      return;
   u_cnd_monotonic_destroy(&pool->changed);
   mtx_destroy(&pool->mutex);
   vk_query_pool_destroy(device, alloc, &pool->vk);
}

VKAPI_ATTR void VKAPI_CALL
fsa_ResetQueryPool(VkDevice device, VkQueryPool handle, uint32_t first,
                   uint32_t count)
{
   VK_FROM_HANDLE(fsa_query_pool, pool, handle);
   mtx_lock(&pool->mutex);
   memset(pool->queries + first, 0, count * sizeof(pool->queries[0]));
   mtx_unlock(&pool->mutex);
}

VKAPI_ATTR VkResult VKAPI_CALL
fsa_GetQueryPoolResults(VkDevice device, VkQueryPool handle, uint32_t first,
                        uint32_t count, size_t size, void *data,
                        VkDeviceSize stride, VkQueryResultFlags flags)
{
   VK_FROM_HANDLE(vk_device, vk, device);
   VK_FROM_HANDLE(fsa_query_pool, pool, handle);
   unsigned bytes = flags & VK_QUERY_RESULT_64_BIT ? 8 : 4;
   unsigned element =
      bytes * ((flags & VK_QUERY_RESULT_WITH_AVAILABILITY_BIT) ? 2 : 1);
   if (first > pool->vk.query_count || count > pool->vk.query_count - first ||
       (count && ((uint64_t)(count - 1) * stride > size ||
                  element > size - (uint64_t)(count - 1) * stride)))
      return VK_ERROR_UNKNOWN;
   VkResult result = VK_SUCCESS;
   mtx_lock(&pool->mutex);
   for (unsigned i = 0; i < count; i++) {
      unsigned index = first + i;
      while (!pool->queries[index].available &&
             (flags & VK_QUERY_RESULT_WAIT_BIT)) {
         VkResult wait =
            fsa_cond_wait(vk, &pool->changed, &pool->mutex, UINT64_MAX);
         if (wait != VK_SUCCESS) {
            mtx_unlock(&pool->mutex);
            return wait;
         }
      }
      uint8_t *out = (uint8_t *)data + i * stride;
      if (pool->queries[index].available ||
          (flags & VK_QUERY_RESULT_PARTIAL_BIT))
         memcpy(out, &pool->queries[index].value, bytes);
      else
         result = VK_NOT_READY;
      if (flags & VK_QUERY_RESULT_WITH_AVAILABILITY_BIT) {
         uint64_t available = pool->queries[index].available;
         memcpy(out + bytes, &available, bytes);
      }
   }
   mtx_unlock(&pool->mutex);
   return result;
}

VKAPI_ATTR VkResult VKAPI_CALL
fsa_CreateEvent(VkDevice handle, const VkEventCreateInfo *info,
                const VkAllocationCallbacks *alloc, VkEvent *out)
{
   VK_FROM_HANDLE(vk_device, device, handle);
   struct fsa_event *event =
      vk_object_zalloc(device, alloc, sizeof(*event), VK_OBJECT_TYPE_EVENT);
   if (!event)
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   VkResult result = vk_sync_create(device, &fsa_sync_type, 0, 0, &event->sync);
   if (result != VK_SUCCESS) {
      vk_object_free(device, alloc, event);
      return result;
   }
   *out = fsa_event_to_handle(event);
   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
fsa_DestroyEvent(VkDevice handle, VkEvent event_handle,
                 const VkAllocationCallbacks *alloc)
{
   VK_FROM_HANDLE(vk_device, device, handle);
   VK_FROM_HANDLE(fsa_event, event, event_handle);
   if (!event)
      return;
   vk_sync_destroy(device, event->sync);
   vk_object_free(device, alloc, event);
}

VKAPI_ATTR VkResult VKAPI_CALL
fsa_SetEvent(VkDevice handle, VkEvent event_handle)
{
   VK_FROM_HANDLE(vk_device, device, handle);
   VK_FROM_HANDLE(fsa_event, event, event_handle);
   return vk_sync_signal(device, event->sync, 0);
}

VKAPI_ATTR VkResult VKAPI_CALL
fsa_ResetEvent(VkDevice handle, VkEvent event_handle)
{
   VK_FROM_HANDLE(vk_device, device, handle);
   VK_FROM_HANDLE(fsa_event, event, event_handle);
   return vk_sync_reset(device, event->sync);
}

VKAPI_ATTR VkResult VKAPI_CALL
fsa_GetEventStatus(VkDevice handle, VkEvent event_handle)
{
   VK_FROM_HANDLE(vk_device, device, handle);
   VK_FROM_HANDLE(fsa_event, event, event_handle);
   VkResult result =
      vk_sync_wait(device, event->sync, 0, VK_SYNC_WAIT_COMPLETE, 0);
   return result == VK_SUCCESS   ? VK_EVENT_SET
          : result == VK_TIMEOUT ? VK_EVENT_RESET
                                 : result;
}

bool
fsa_query_command(VkDevice device, const struct vk_cmd_queue_entry *cmd)
{
   switch (cmd->type) {
   case VK_CMD_RESET_QUERY_POOL:
      fsa_ResetQueryPool(device, cmd->u.reset_query_pool.query_pool,
                         cmd->u.reset_query_pool.first_query,
                         cmd->u.reset_query_pool.query_count);
      return true;
   case VK_CMD_WRITE_TIMESTAMP2: {
      struct fsa_query_pool *pool =
         fsa_query_pool_from_handle(cmd->u.write_timestamp2.query_pool);
      uint64_t now;
      if (fsa_hal_time_ns(&now))
         return false;
      mtx_lock(&pool->mutex);
      pool->queries[cmd->u.write_timestamp2.query].value = now;
      pool->queries[cmd->u.write_timestamp2.query].available = true;
      u_cnd_monotonic_broadcast(&pool->changed);
      mtx_unlock(&pool->mutex);
      return true;
   }
   case VK_CMD_COPY_QUERY_POOL_RESULTS: {
      const struct vk_cmd_copy_query_pool_results *info =
         &cmd->u.copy_query_pool_results;
      struct fsa_buffer *dst = fsa_buffer_from_handle(info->dst_buffer);
      unsigned bytes = info->flags & VK_QUERY_RESULT_64_BIT ? 8 : 4;
      unsigned element =
         bytes *
         ((info->flags & VK_QUERY_RESULT_WITH_AVAILABILITY_BIT) ? 2 : 1);
      for (unsigned i = 0; i < info->query_count; i++) {
         uint64_t values[2] = {0};
         VkResult result = fsa_GetQueryPoolResults(
            device, info->query_pool, info->first_query + i, 1, sizeof(values),
            values, sizeof(values), info->flags);
         if (result != VK_SUCCESS && result != VK_NOT_READY)
            return false;
         uint64_t offset = info->dst_offset + i * info->stride;
         if (!dst->memory || offset > dst->vk.size ||
             element > dst->vk.size - offset)
            return false;
         /* Unavailable query results leave the result bytes untouched. */
         if (result == VK_SUCCESS ||
             (info->flags & VK_QUERY_RESULT_PARTIAL_BIT)) {
            if (fsa_copy_to_dev(fsa_buffer_address(dst) + offset, values,
                                element))
               return false;
         } else if (info->flags & VK_QUERY_RESULT_WITH_AVAILABILITY_BIT) {
            if (fsa_copy_to_dev(fsa_buffer_address(dst) + offset + bytes,
                                (uint8_t *)values + bytes, bytes))
               return false;
         }
      }
      return true;
   }
   case VK_CMD_SET_EVENT2:
      return fsa_SetEvent(device, cmd->u.set_event2.event) == VK_SUCCESS;
   case VK_CMD_RESET_EVENT2:
      return fsa_ResetEvent(device, cmd->u.reset_event2.event) == VK_SUCCESS;
   case VK_CMD_WAIT_EVENTS2:
      for (unsigned i = 0; i < cmd->u.wait_events2.event_count; i++) {
         struct fsa_event *event =
            fsa_event_from_handle(cmd->u.wait_events2.events[i]);
         if (vk_sync_wait(vk_device_from_handle(device), event->sync, 0,
                          VK_SYNC_WAIT_COMPLETE, UINT64_MAX) != VK_SUCCESS)
            return false;
      }
      return true;
   default:
      return false;
   }
}
