/* SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 * SPDX-License-Identifier: MIT
 */

#include <pthread.h>
#include <stdlib.h>
#include "util/u_printf.h"
#include "vk_cmd_enqueue_entrypoints.h"
#include "vk_private.h"
#include "vk_sync.h"

static pthread_mutex_t session_lock = PTHREAD_MUTEX_INITIALIZER;
static unsigned session_refs;
static bool session_initialized;
static FsaDeviceDescription session_description;
/* Like the PoCL device, retain the transport for the Host process lifetime.
 * Applications may destroy discovery instances before creating their device. */
static void
finish_session(void)
{
   pthread_mutex_lock(&session_lock);
   if (session_initialized) {
      fsa_hal_cleanup();
      session_initialized = false;
   }
   pthread_mutex_unlock(&session_lock);
}

static const struct vk_instance_extension_table instance_extensions = {
   .KHR_get_physical_device_properties2 = true,
};

static void
destroy_physical(struct vk_physical_device *vk)
{
   struct fsa_physical_device *physical =
      container_of(vk, struct fsa_physical_device, vk);
   pthread_mutex_lock(&session_lock);
   assert(session_refs > 0);
   --session_refs;
   u_printf_singleton_decref();
   pthread_mutex_unlock(&session_lock);
   vk_physical_device_finish(vk);
   vk_free(&vk->instance->alloc, physical);
}

static VkResult
enumerate_physical(struct vk_instance *instance)
{
   struct fsa_physical_device *physical =
      vk_zalloc(&instance->alloc, sizeof(*physical), 8,
                VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
   if (!physical)
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   pthread_mutex_lock(&session_lock);
   if (!session_initialized) {
      int probe = fsa_probe();
      if (probe != 0) {
         pthread_mutex_unlock(&session_lock);
         vk_free(&instance->alloc, physical);
         return VK_SUCCESS;
      }
      int initialized = fsa_hal_init(&session_description);
      if (initialized != 0) {
         pthread_mutex_unlock(&session_lock);
         vk_free(&instance->alloc, physical);
         return VK_ERROR_INITIALIZATION_FAILED;
      }
      session_initialized = true;
      if (atexit(finish_session) != 0) {
         fsa_hal_cleanup();
         session_initialized = false;
         pthread_mutex_unlock(&session_lock);
         vk_free(&instance->alloc, physical);
         return VK_ERROR_OUT_OF_HOST_MEMORY;
      }
   }
   uint64_t width = session_description.threads_per_warp;
   if (!width || width > 64 || (width & (width - 1))) {
      pthread_mutex_unlock(&session_lock);
      vk_free(&instance->alloc, physical);
      return VK_ERROR_INCOMPATIBLE_DRIVER;
   }
   session_refs++;
   physical->description = session_description;
   pthread_mutex_unlock(&session_lock);
   struct vk_physical_device_dispatch_table dispatch;
   vk_physical_device_dispatch_table_from_entrypoints(
      &dispatch, &fsa_physical_device_entrypoints, true);
   VkResult result = vk_physical_device_init(&physical->vk, instance, NULL,
                                             NULL, NULL, &dispatch);
   if (result != VK_SUCCESS) {
      pthread_mutex_lock(&session_lock);
      --session_refs;
      pthread_mutex_unlock(&session_lock);
      vk_free(&instance->alloc, physical);
      return result;
   }
   u_printf_singleton_init_or_ref();
   physical->vk.supported_extensions.KHR_shader_non_semantic_info = true;
   physical->vk.supported_features.shaderUniformBufferArrayDynamicIndexing =
      true;
   physical->vk.supported_features.shaderStorageBufferArrayDynamicIndexing =
      true;
   physical->vk.supported_features.timelineSemaphore = true;
   physical->vk.supported_features.hostQueryReset = true;
   physical->vk.supported_extensions.EXT_host_query_reset = true;
   physical->vk.supported_extensions.KHR_timeline_semaphore = true;
   physical->vk.supported_features.shaderFloat64 = true;
   physical->vk.supported_features.shaderInt64 = true;
   physical->vk.supported_features.shaderInt16 = true;
   physical->vk.supported_features.shaderInt8 = true;
   physical->vk.supported_features.storageBuffer8BitAccess = true;
   physical->vk.supported_features.uniformAndStorageBuffer8BitAccess = true;
   physical->vk.supported_features.storagePushConstant8 = true;
   physical->vk.supported_features.storageBuffer16BitAccess = true;
   physical->vk.supported_features.uniformAndStorageBuffer16BitAccess = true;
   physical->vk.supported_features.storagePushConstant16 = true;
   physical->vk.supported_extensions.KHR_8bit_storage = true;
   physical->vk.supported_extensions.KHR_16bit_storage = true;
   physical->vk.supported_extensions.KHR_shader_float16_int8 = true;
   physical->vk.supported_features.shaderStorageImageReadWithoutFormat = true;
   physical->vk.supported_features.shaderStorageImageWriteWithoutFormat = true;
   physical->vk.supported_features.shaderBufferInt64Atomics = true;
   physical->vk.supported_features.shaderSharedInt64Atomics = true;
   physical->vk.supported_features.shaderSubgroupClock = true;
   physical->vk.supported_features.shaderDeviceClock = true;
   physical->vk.supported_extensions.KHR_shader_atomic_int64 = true;
   physical->vk.supported_extensions.KHR_shader_clock = true;
   physical->sync_types[0] = &fsa_sync_type;
   physical->vk.supported_sync_types = physical->sync_types;
   struct vk_properties *p = &physical->vk.properties;
   p->apiVersion = VK_API_VERSION_1_1;
   p->driverVersion = 1;
   static const uint8_t uuid[VK_UUID_SIZE] = "FormosaCompute01";
   memcpy(p->pipelineCacheUUID, uuid, sizeof(uuid));
   memcpy(p->deviceUUID, uuid, sizeof(uuid));
   memcpy(p->driverUUID, uuid, sizeof(uuid));
   p->deviceType = VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU;
   snprintf(p->deviceName, sizeof(p->deviceName),
            "Formosa compute (experimental)");
   p->subgroupSize = physical->description.threads_per_warp;
   p->subgroupSupportedStages = VK_SHADER_STAGE_COMPUTE_BIT;
   p->subgroupSupportedOperations = VK_SUBGROUP_FEATURE_BASIC_BIT;
   p->timestampPeriod = 1.0f;
   p->timestampComputeAndGraphics = true;
   p->maxTexelBufferElements = physical->description.max_allocation_size / 16;
   p->minTexelBufferOffsetAlignment = 4;
   p->maxImageDimension1D = FSA_MAX_IMAGE_2D;
   p->maxImageDimension2D = FSA_MAX_IMAGE_2D;
   p->maxImageDimension3D = FSA_MAX_IMAGE_3D;
   p->maxImageArrayLayers = FSA_MAX_IMAGE_LAYERS;
   p->maxSamplerAllocationCount = 4000;
   p->maxPerStageDescriptorSamplers = FSA_COMPUTE_MAX_BUFFERS;
   p->maxPerStageDescriptorSampledImages = FSA_COMPUTE_MAX_BUFFERS;
   p->maxPerStageDescriptorStorageImages = FSA_COMPUTE_MAX_BUFFERS;
   p->maxDescriptorSetSamplers = FSA_COMPUTE_MAX_BUFFERS;
   p->maxDescriptorSetSampledImages = FSA_COMPUTE_MAX_BUFFERS;
   p->maxDescriptorSetStorageImages = FSA_COMPUTE_MAX_BUFFERS;
   p->maxMemoryAllocationCount = 4096;
   p->maxMemoryAllocationSize = physical->description.max_allocation_size;
   p->maxPerSetDescriptors = FSA_COMPUTE_MAX_BUFFERS;
   p->maxTimelineSemaphoreValueDifference = UINT64_MAX;
   p->maxStorageBufferRange =
      MIN2(physical->description.max_allocation_size, UINT32_MAX);
   p->maxUniformBufferRange = 65536;
   p->maxBoundDescriptorSets = FSA_COMPUTE_MAX_SETS;
   p->maxPerStageDescriptorStorageBuffers = FSA_COMPUTE_MAX_BUFFERS;
   p->maxDescriptorSetStorageBuffers = FSA_COMPUTE_MAX_BUFFERS;
   p->maxPerStageDescriptorUniformBuffers = FSA_COMPUTE_MAX_BUFFERS;
   p->maxDescriptorSetUniformBuffers = FSA_COMPUTE_MAX_BUFFERS;
   p->maxDescriptorSetUniformBuffersDynamic = FSA_COMPUTE_MAX_BUFFERS;
   p->maxDescriptorSetStorageBuffersDynamic = FSA_COMPUTE_MAX_BUFFERS;
   p->maxPerStageResources = FSA_COMPUTE_MAX_BUFFERS;
   p->maxPushConstantsSize = FSA_COMPUTE_PUSH_SIZE;
   p->maxComputeSharedMemorySize =
      physical->description.local_mem_size_per_core;
   p->maxComputeWorkGroupInvocations =
      physical->description.max_threads_per_work_group;
   for (unsigned i = 0; i < 3; i++) {
      p->maxComputeWorkGroupSize[i] =
         physical->description.max_threads_per_work_group;
      p->maxComputeWorkGroupCount[i] = 65535;
   }
   p->minMemoryMapAlignment = 64;
   p->minStorageBufferOffsetAlignment = 4;
   p->minUniformBufferOffsetAlignment = 4;
   p->nonCoherentAtomSize = 1;
   p->bufferImageGranularity = 1;
   list_addtail(&physical->vk.link, &instance->physical_devices.list);
   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
fsa_EnumerateInstanceVersion(uint32_t *version)
{
   *version = VK_API_VERSION_1_1;
   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
fsa_EnumerateInstanceExtensionProperties(const char *layer, uint32_t *count,
                                         VkExtensionProperties *properties)
{
   if (layer)
      return VK_ERROR_LAYER_NOT_PRESENT;
   return vk_enumerate_instance_extension_properties(&instance_extensions,
                                                     count, properties);
}

VKAPI_ATTR VkResult VKAPI_CALL
fsa_CreateInstance(const VkInstanceCreateInfo *info,
                   const VkAllocationCallbacks *alloc, VkInstance *out)
{
   if (!alloc)
      alloc = vk_default_allocator();
   struct vk_instance *instance = vk_zalloc(
      alloc, sizeof(*instance), 8, VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
   if (!instance)
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   struct vk_instance_dispatch_table dispatch;
   vk_instance_dispatch_table_from_entrypoints(&dispatch,
                                               &fsa_instance_entrypoints, true);
   VkResult result =
      vk_instance_init(instance, &instance_extensions, &dispatch, info, alloc);
   if (result != VK_SUCCESS) {
      vk_free(alloc, instance);
      return result;
   }
   instance->physical_devices.enumerate = enumerate_physical;
   instance->physical_devices.destroy = destroy_physical;
   *out = vk_instance_to_handle(instance);
   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
fsa_DestroyInstance(VkInstance handle, const VkAllocationCallbacks *alloc)
{
   VK_FROM_HANDLE(vk_instance, instance, handle);
   if (!instance)
      return;
   VkAllocationCallbacks saved = instance->alloc;
   vk_instance_finish(instance);
   vk_free(&saved, instance);
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
fsa_GetInstanceProcAddr(VkInstance handle, const char *name)
{
   VK_FROM_HANDLE(vk_instance, instance, handle);
   return vk_instance_get_proc_addr(instance, &fsa_instance_entrypoints, name);
}

PUBLIC VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
vk_icdGetInstanceProcAddr(VkInstance instance, const char *name);
PUBLIC VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
vk_icdGetInstanceProcAddr(VkInstance instance, const char *name)
{
   return fsa_GetInstanceProcAddr(instance, name);
}

VKAPI_ATTR void VKAPI_CALL
fsa_GetPhysicalDeviceQueueFamilyProperties2(VkPhysicalDevice handle,
                                            uint32_t *count,
                                            VkQueueFamilyProperties2 *properties)
{
   if (!properties) {
      *count = 1;
      return;
   }
   if (*count > 0) {
      properties[0].queueFamilyProperties = (VkQueueFamilyProperties){
         .queueFlags = VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT,
         .queueCount = 1,
         .timestampValidBits = 64,
         .minImageTransferGranularity = {1, 1, 1},
      };
      *count = 1;
   }
}

VKAPI_ATTR void VKAPI_CALL
fsa_GetPhysicalDeviceMemoryProperties2(VkPhysicalDevice handle,
                                       VkPhysicalDeviceMemoryProperties2 *out)
{
   VK_FROM_HANDLE(vk_physical_device, vk, handle);
   struct fsa_physical_device *physical =
      container_of(vk, struct fsa_physical_device, vk);
   out->memoryProperties = (VkPhysicalDeviceMemoryProperties){
      .memoryHeapCount = 1,
      .memoryHeaps = {{physical->description.global_mem_size,
                       VK_MEMORY_HEAP_DEVICE_LOCAL_BIT}},
      .memoryTypeCount = 2,
      .memoryTypes =
         {
            {VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0},
            {VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT |
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
             0},
         },
   };
}

VKAPI_ATTR VkResult VKAPI_CALL
fsa_CreateDevice(VkPhysicalDevice handle, const VkDeviceCreateInfo *info,
                 const VkAllocationCallbacks *alloc, VkDevice *out)
{
   VK_FROM_HANDLE(vk_physical_device, vk, handle);
   struct fsa_physical_device *physical =
      container_of(vk, struct fsa_physical_device, vk);
   if (info->queueCreateInfoCount != 1 ||
       info->pQueueCreateInfos[0].queueFamilyIndex != 0 ||
       info->pQueueCreateInfos[0].queueCount != 1)
      return VK_ERROR_INITIALIZATION_FAILED;
   struct fsa_device *device =
      vk_zalloc2(&vk->instance->alloc, alloc, sizeof(*device), 8,
                 VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);
   if (!device)
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   struct vk_device_dispatch_table dispatch;
   vk_device_dispatch_table_from_entrypoints(&dispatch, &fsa_device_entrypoints,
                                             true);
   struct vk_device_dispatch_table enqueue;
   vk_device_dispatch_table_from_entrypoints(
      &enqueue, &vk_cmd_enqueue_device_entrypoints, true);
   dispatch.CmdBindPipeline = enqueue.CmdBindPipeline;
   dispatch.CmdBindDescriptorSets2KHR = enqueue.CmdBindDescriptorSets2KHR;
   dispatch.CmdPushConstants2KHR = enqueue.CmdPushConstants2KHR;
   dispatch.CmdDispatch = enqueue.CmdDispatch;
   dispatch.CmdDispatchBase = enqueue.CmdDispatchBase;
   dispatch.CmdDispatchIndirect = enqueue.CmdDispatchIndirect;
   dispatch.CmdFillBuffer = enqueue.CmdFillBuffer;
   dispatch.CmdUpdateBuffer = enqueue.CmdUpdateBuffer;
   dispatch.CmdExecuteCommands = enqueue.CmdExecuteCommands;
   dispatch.CmdCopyBuffer2 = enqueue.CmdCopyBuffer2;
   dispatch.CmdCopyImage2 = enqueue.CmdCopyImage2;
   dispatch.CmdCopyBufferToImage2 = enqueue.CmdCopyBufferToImage2;
   dispatch.CmdCopyImageToBuffer2 = enqueue.CmdCopyImageToBuffer2;
   dispatch.CmdClearColorImage = enqueue.CmdClearColorImage;
   dispatch.CmdResetQueryPool = enqueue.CmdResetQueryPool;
   dispatch.CmdWriteTimestamp2 = enqueue.CmdWriteTimestamp2;
   dispatch.CmdCopyQueryPoolResults = enqueue.CmdCopyQueryPoolResults;
   dispatch.CmdSetEvent2 = enqueue.CmdSetEvent2;
   dispatch.CmdResetEvent2 = enqueue.CmdResetEvent2;
   dispatch.CmdWaitEvents2 = enqueue.CmdWaitEvents2;
   dispatch.CmdPipelineBarrier2 = enqueue.CmdPipelineBarrier2;
   VkResult result = vk_device_init(&device->vk, vk, &dispatch, info, alloc);
   if (result != VK_SUCCESS)
      goto fail;
   device->physical = physical;
   device->vk.command_buffer_ops = &fsa_command_buffer_ops;
   vk_device_enable_threaded_submit(&device->vk);
   result =
      vk_queue_init(&device->queue, &device->vk, info->pQueueCreateInfos, 0);
   if (result != VK_SUCCESS) {
      vk_device_finish(&device->vk);
      goto fail;
   }
   device->queue.driver_submit = fsa_queue_submit;
   result = vk_queue_enable_submit_thread(&device->queue);
   if (result != VK_SUCCESS) {
      vk_queue_finish(&device->queue);
      vk_device_finish(&device->vk);
      goto fail;
   }
   *out = vk_device_to_handle(&device->vk);
   return VK_SUCCESS;
fail:
   vk_free2(&vk->instance->alloc, alloc, device);
   return result;
}

VKAPI_ATTR void VKAPI_CALL
fsa_DestroyDevice(VkDevice handle, const VkAllocationCallbacks *alloc)
{
   VK_FROM_HANDLE(vk_device, vk, handle);
   if (!vk)
      return;
   struct fsa_device *device = container_of(vk, struct fsa_device, vk);
   VkAllocationCallbacks saved = vk->alloc;
   vk_queue_finish(&device->queue);
   vk_device_finish(vk);
   vk_free(&saved, device);
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
fsa_GetDeviceProcAddr(VkDevice handle, const char *name)
{
   VK_FROM_HANDLE(vk_device, device, handle);
   return vk_device_get_proc_addr(device, name);
}
