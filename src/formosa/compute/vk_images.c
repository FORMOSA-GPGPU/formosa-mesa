/* SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 * SPDX-License-Identifier: MIT
 */

#include "util/u_pack_color.h"
#include "_kernel_constants.h"
#include "vk_format.h"
#include "vk_private.h"

static bool
image_format(VkFormat format, dev_image_t *out)
{
   unsigned channels = vk_format_get_nr_components(format);
   if (channels != 1 && channels != 2 && channels != 4)
      return false;
   out->_num_channels = channels;
   out->_order = channels == 1 ? CLK_R : channels == 2 ? CLK_RG : CLK_RGBA;
   switch (format) {
#define FORMATS(bits, suffix, cltype)                                          \
   case VK_FORMAT_R##bits##_##suffix:                                          \
   case VK_FORMAT_R##bits##G##bits##_##suffix:                                 \
   case VK_FORMAT_R##bits##G##bits##B##bits##A##bits##_##suffix:               \
      out->_elem_size = bits / 8;                                              \
      out->_data_type = cltype;                                                \
      break
      FORMATS(8, UNORM, CLK_UNORM_INT8);
      FORMATS(8, SNORM, CLK_SNORM_INT8);
      FORMATS(8, UINT, CLK_UNSIGNED_INT8);
      FORMATS(8, SINT, CLK_SIGNED_INT8);
      FORMATS(16, UNORM, CLK_UNORM_INT16);
      FORMATS(16, SNORM, CLK_SNORM_INT16);
      FORMATS(16, UINT, CLK_UNSIGNED_INT16);
      FORMATS(16, SINT, CLK_SIGNED_INT16);
      FORMATS(16, SFLOAT, CLK_HALF_FLOAT);
      FORMATS(32, UINT, CLK_UNSIGNED_INT32);
      FORMATS(32, SINT, CLK_SIGNED_INT32);
      FORMATS(32, SFLOAT, CLK_FLOAT);
#undef FORMATS
   case VK_FORMAT_B8G8R8A8_UNORM:
      out->_order = CLK_BGRA;
      out->_elem_size = 1;
      out->_data_type = CLK_UNORM_INT8;
      break;
   default:
      return false;
   }
   return true;
}

VKAPI_ATTR void VKAPI_CALL
fsa_GetPhysicalDeviceFormatProperties2(VkPhysicalDevice device, VkFormat format,
                                       VkFormatProperties2 *out)
{
   dev_image_t image = {0};
   VkFormatFeatureFlags flags = 0;
   if (image_format(format, &image)) {
      flags = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
              VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT |
              VK_FORMAT_FEATURE_TRANSFER_SRC_BIT |
              VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
      if (image._data_type != CLK_SIGNED_INT8 &&
          image._data_type != CLK_SIGNED_INT16 &&
          image._data_type != CLK_SIGNED_INT32 &&
          image._data_type != CLK_UNSIGNED_INT8 &&
          image._data_type != CLK_UNSIGNED_INT16 &&
          image._data_type != CLK_UNSIGNED_INT32)
         flags |= VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
   }
   VkFormatFeatureFlags buffer_flags =
      flags ? VK_FORMAT_FEATURE_UNIFORM_TEXEL_BUFFER_BIT |
                 VK_FORMAT_FEATURE_STORAGE_TEXEL_BUFFER_BIT
            : 0;
   if (format == VK_FORMAT_R32_UINT || format == VK_FORMAT_R32_SINT) {
      flags |= VK_FORMAT_FEATURE_STORAGE_IMAGE_ATOMIC_BIT;
      buffer_flags |= VK_FORMAT_FEATURE_STORAGE_TEXEL_BUFFER_ATOMIC_BIT;
   }
   out->formatProperties = (VkFormatProperties){flags, flags, buffer_flags};
   vk_foreach_struct (ext, out->pNext) {
      if (ext->sType == VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_3) {
         VkFormatProperties3 *p = (void *)ext;
         p->linearTilingFeatures = flags;
         p->optimalTilingFeatures = flags;
         p->bufferFeatures = buffer_flags;
         if (flags & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT) {
            p->linearTilingFeatures |=
               VK_FORMAT_FEATURE_2_STORAGE_READ_WITHOUT_FORMAT_BIT |
               VK_FORMAT_FEATURE_2_STORAGE_WRITE_WITHOUT_FORMAT_BIT;
            p->optimalTilingFeatures = p->linearTilingFeatures;
         }
      }
   }
}

VKAPI_ATTR VkResult VKAPI_CALL
fsa_GetPhysicalDeviceImageFormatProperties2(
   VkPhysicalDevice device, const VkPhysicalDeviceImageFormatInfo2 *info,
   VkImageFormatProperties2 *out)
{
   VK_FROM_HANDLE(vk_physical_device, vk, device);
   struct fsa_physical_device *physical =
      container_of(vk, struct fsa_physical_device, vk);
   dev_image_t image = {0};
   if (!image_format(info->format, &image) || info->flags ||
       (info->tiling != VK_IMAGE_TILING_LINEAR &&
        info->tiling != VK_IMAGE_TILING_OPTIMAL) ||
       (info->usage &
        ~(VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT |
          VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT)))
      return VK_ERROR_FORMAT_NOT_SUPPORTED;
   out->imageFormatProperties = (VkImageFormatProperties){
      .maxExtent = info->type == VK_IMAGE_TYPE_1D
                      ? (VkExtent3D){FSA_MAX_IMAGE_2D, 1, 1}
                   : info->type == VK_IMAGE_TYPE_2D
                      ? (VkExtent3D){FSA_MAX_IMAGE_2D, FSA_MAX_IMAGE_2D, 1}
                      : (VkExtent3D){FSA_MAX_IMAGE_3D, FSA_MAX_IMAGE_3D,
                                     FSA_MAX_IMAGE_3D},
      .maxMipLevels = 1,
      .maxArrayLayers =
         info->type == VK_IMAGE_TYPE_3D ? 1 : FSA_MAX_IMAGE_LAYERS,
      .sampleCounts = VK_SAMPLE_COUNT_1_BIT,
      .maxResourceSize = physical->description.max_allocation_size,
   };
   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
fsa_CreateImage(VkDevice handle, const VkImageCreateInfo *info,
                const VkAllocationCallbacks *alloc, VkImage *out)
{
   VK_FROM_HANDLE(vk_device, device, handle);
   VkPhysicalDeviceImageFormatInfo2 query = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2,
      .format = info->format,
      .type = info->imageType,
      .tiling = info->tiling,
      .usage = info->usage,
      .flags = info->flags,
   };
   VkImageFormatProperties2 properties = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2};
   if (fsa_GetPhysicalDeviceImageFormatProperties2(
          vk_physical_device_to_handle(device->physical), &query,
          &properties) != VK_SUCCESS ||
       info->samples != VK_SAMPLE_COUNT_1_BIT || info->mipLevels != 1 ||
       info->arrayLayers > properties.imageFormatProperties.maxArrayLayers ||
       info->extent.width > properties.imageFormatProperties.maxExtent.width ||
       info->extent.height >
          properties.imageFormatProperties.maxExtent.height ||
       info->extent.depth > properties.imageFormatProperties.maxExtent.depth)
      return VK_ERROR_FORMAT_NOT_SUPPORTED;
   struct fsa_image *image =
      vk_image_create(device, info, alloc, sizeof(*image));
   if (!image)
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   image->size = (uint64_t)info->extent.width * info->extent.height *
                 info->extent.depth * info->arrayLayers *
                 vk_format_get_blocksize(info->format);
   if (image->size > properties.imageFormatProperties.maxResourceSize) {
      vk_image_destroy(device, alloc, &image->vk);
      return VK_ERROR_OUT_OF_DEVICE_MEMORY;
   }
   *out = fsa_image_to_handle(image);
   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
fsa_DestroyImage(VkDevice handle, VkImage image_handle,
                 const VkAllocationCallbacks *alloc)
{
   VK_FROM_HANDLE(vk_device, device, handle);
   VK_FROM_HANDLE(fsa_image, image, image_handle);
   if (image)
      vk_image_destroy(device, alloc, &image->vk);
}

VKAPI_ATTR void VKAPI_CALL
fsa_GetImageMemoryRequirements2(VkDevice device,
                                const VkImageMemoryRequirementsInfo2 *info,
                                VkMemoryRequirements2 *out)
{
   VK_FROM_HANDLE(fsa_image, image, info->image);
   out->memoryRequirements =
      (VkMemoryRequirements){align64(image->size, 64), 64, 3};
}

VKAPI_ATTR VkResult VKAPI_CALL
fsa_BindImageMemory2(VkDevice device, uint32_t count,
                     const VkBindImageMemoryInfo *infos)
{
   for (unsigned i = 0; i < count; i++) {
      VK_FROM_HANDLE(fsa_image, image, infos[i].image);
      VK_FROM_HANDLE(fsa_memory, memory, infos[i].memory);
      if (infos[i].memoryOffset > memory->vk.size ||
          image->size > memory->vk.size - infos[i].memoryOffset)
         return VK_ERROR_OUT_OF_DEVICE_MEMORY;
      image->memory = memory;
      image->offset = infos[i].memoryOffset;
   }
   return VK_SUCCESS;
}

uint64_t
fsa_image_address(struct fsa_image *image, uint32_t mip, uint32_t layer,
                  VkOffset3D offset)
{
   VkExtent3D extent = vk_image_mip_level_extent(&image->vk, mip);
   uint64_t pixel = ((uint64_t)layer * extent.depth + offset.z) *
                       extent.height * extent.width +
                    (uint64_t)offset.y * extent.width + offset.x;
   return (uintptr_t)image->memory->address + image->offset +
          image->levels[mip] +
          pixel * vk_format_get_blocksize(image->vk.format);
}

VKAPI_ATTR void VKAPI_CALL
fsa_GetImageSubresourceLayout(VkDevice device, VkImage handle,
                              const VkImageSubresource *subresource,
                              VkSubresourceLayout *out)
{
   VK_FROM_HANDLE(fsa_image, image, handle);
   uint64_t row =
      image->vk.extent.width * vk_format_get_blocksize(image->vk.format);
   uint64_t slice = row * image->vk.extent.height;
   *out = (VkSubresourceLayout){
      .offset = subresource->arrayLayer * slice * image->vk.extent.depth,
      .size = slice * image->vk.extent.depth,
      .rowPitch = row,
      .arrayPitch = slice * image->vk.extent.depth,
      .depthPitch = slice};
}

VKAPI_ATTR VkResult VKAPI_CALL
fsa_CreateImageView(VkDevice handle, const VkImageViewCreateInfo *info,
                    const VkAllocationCallbacks *alloc, VkImageView *out)
{
   VK_FROM_HANDLE(vk_device, device, handle);
   dev_image_t check = {0};
   if (!image_format(info->format, &check) || info->flags ||
       info->viewType == VK_IMAGE_VIEW_TYPE_CUBE ||
       info->viewType == VK_IMAGE_VIEW_TYPE_CUBE_ARRAY)
      return VK_ERROR_FORMAT_NOT_SUPPORTED;
   const VkComponentSwizzle swizzle[] = {info->components.r, info->components.g,
                                         info->components.b,
                                         info->components.a};
   for (unsigned i = 0; i < 4; i++)
      if (swizzle[i] != VK_COMPONENT_SWIZZLE_IDENTITY &&
          swizzle[i] != VK_COMPONENT_SWIZZLE_R + i)
         return VK_ERROR_FEATURE_NOT_PRESENT;
   struct vk_image_view *view =
      vk_image_view_create(device, false, info, alloc, sizeof(*view));
   if (!view)
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   *out = vk_image_view_to_handle(view);
   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
fsa_DestroyImageView(VkDevice handle, VkImageView view_handle,
                     const VkAllocationCallbacks *alloc)
{
   VK_FROM_HANDLE(vk_device, device, handle);
   VK_FROM_HANDLE(vk_image_view, view, view_handle);
   if (view)
      vk_image_view_destroy(device, alloc, view);
}

bool
fsa_image_descriptor(struct vk_image_view *view, dev_image_t *out)
{
   struct fsa_image *image = container_of(view->image, struct fsa_image, vk);
   if (!image->memory || !image_format(view->format, out))
      return false;
   out->_data = (void *)(uintptr_t)fsa_image_address(
      image, view->base_mip_level, view->base_array_layer, (VkOffset3D){0});
   out->_width = view->extent.width;
   /* PoCL identifies dimensionality with zero unused dimensions. */
   out->_height = (view->view_type == VK_IMAGE_VIEW_TYPE_1D ||
                   view->view_type == VK_IMAGE_VIEW_TYPE_1D_ARRAY)
                     ? 0
                     : view->extent.height;
   out->_depth =
      view->view_type == VK_IMAGE_VIEW_TYPE_3D ? view->extent.depth : 0;
   out->_image_array_size = (view->view_type == VK_IMAGE_VIEW_TYPE_1D_ARRAY ||
                             view->view_type == VK_IMAGE_VIEW_TYPE_2D_ARRAY)
                               ? view->layer_count
                               : 0;
   out->_row_pitch = out->_width * out->_num_channels * out->_elem_size;
   out->_slice_pitch = out->_row_pitch * MAX2(out->_height, 1);
   out->_num_mip_levels = 1;
   out->_num_samples = 1;
   return true;
}

VKAPI_ATTR VkResult VKAPI_CALL
fsa_CreateSampler(VkDevice handle, const VkSamplerCreateInfo *info,
                  const VkAllocationCallbacks *alloc, VkSampler *out)
{
   VK_FROM_HANDLE(vk_device, device, handle);
   if (info->flags || info->anisotropyEnable || info->compareEnable ||
       info->minLod > 0 || info->magFilter != info->minFilter ||
       info->addressModeU != info->addressModeV ||
       info->addressModeU != info->addressModeW ||
       (info->borderColor != VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK &&
        info->borderColor != VK_BORDER_COLOR_INT_TRANSPARENT_BLACK))
      return VK_ERROR_FEATURE_NOT_PRESENT;
   uint32_t mode;
   switch (info->addressModeU) {
   case VK_SAMPLER_ADDRESS_MODE_REPEAT:
      mode = CLK_ADDRESS_REPEAT;
      break;
   case VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT:
      mode = CLK_ADDRESS_MIRRORED_REPEAT;
      break;
   case VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE:
      mode = CLK_ADDRESS_CLAMP_TO_EDGE;
      break;
   case VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER:
      mode = CLK_ADDRESS_CLAMP;
      break;
   default:
      return VK_ERROR_FEATURE_NOT_PRESENT;
   }
   struct fsa_sampler *sampler =
      vk_object_zalloc(device, alloc, sizeof(*sampler), VK_OBJECT_TYPE_SAMPLER);
   if (!sampler)
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   sampler->value =
      mode | (info->unnormalizedCoordinates ? 0 : CLK_NORMALIZED_COORDS_TRUE) |
      (info->minFilter == VK_FILTER_LINEAR ? CLK_FILTER_LINEAR
                                           : CLK_FILTER_NEAREST);
   *out = fsa_sampler_to_handle(sampler);
   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
fsa_DestroySampler(VkDevice handle, VkSampler sampler_handle,
                   const VkAllocationCallbacks *alloc)
{
   VK_FROM_HANDLE(vk_device, device, handle);
   VK_FROM_HANDLE(fsa_sampler, sampler, sampler_handle);
   if (sampler)
      vk_object_free(device, alloc, sampler);
}

VKAPI_ATTR VkResult VKAPI_CALL
fsa_CreateBufferView(VkDevice handle, const VkBufferViewCreateInfo *info,
                     const VkAllocationCallbacks *alloc, VkBufferView *out)
{
   VK_FROM_HANDLE(vk_device, device, handle);
   dev_image_t image = {0};
   if (info->flags || !image_format(info->format, &image))
      return VK_ERROR_FORMAT_NOT_SUPPORTED;
   struct vk_buffer_view *view =
      vk_buffer_view_create(device, info, alloc, sizeof(*view));
   if (!view)
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   *out = vk_buffer_view_to_handle(view);
   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
fsa_DestroyBufferView(VkDevice handle, VkBufferView view_handle,
                      const VkAllocationCallbacks *alloc)
{
   VK_FROM_HANDLE(vk_device, device, handle);
   VK_FROM_HANDLE(vk_buffer_view, view, view_handle);
   if (view)
      vk_buffer_view_destroy(device, alloc, view);
}

bool
fsa_texel_descriptor(struct vk_buffer_view *view, dev_image_t *out)
{
   struct fsa_buffer *buffer =
      container_of(view->buffer, struct fsa_buffer, vk);
   if (!buffer->memory || !image_format(view->format, out))
      return false;
   out->_data = (void *)(uintptr_t)(fsa_buffer_address(buffer) + view->offset);
   out->_width = view->range / (out->_num_channels * out->_elem_size);
   out->_row_pitch = out->_width * out->_num_channels * out->_elem_size;
   out->_slice_pitch = out->_row_pitch;
   return true;
}

static bool
copy_bytes(uint64_t source, uint64_t destination, uint64_t size)
{
   FsaMemoryCopyInfo copy = {
      .struct_size = sizeof(copy),
      .source = {kFsaMemoryDomainDevice, {source, size}},
      .destination = {kFsaMemoryDomainDevice, {destination, size}}};
   FsaCompletionToken token = 0;
   FsaCommandSubmitStatus status = fsa_cmd_memory_copy(&copy, &token);
   return fsa_wait(status, token);
}

static bool
valid_region(struct fsa_image *image, const VkImageSubresourceLayers *sub,
             VkOffset3D offset, VkExtent3D extent)
{
   if (!image->memory || sub->mipLevel >= image->vk.mip_levels ||
       offset.x < 0 || offset.y < 0 || offset.z < 0 ||
       sub->baseArrayLayer > image->vk.array_layers ||
       sub->layerCount > image->vk.array_layers - sub->baseArrayLayer)
      return false;
   VkExtent3D size = vk_image_mip_level_extent(&image->vk, sub->mipLevel);
   return (uint64_t)offset.x + extent.width <= size.width &&
          (uint64_t)offset.y + extent.height <= size.height &&
          (uint64_t)offset.z + extent.depth <= size.depth;
}

static bool
full_mip(struct fsa_image *image, uint32_t mip, VkOffset3D offset,
         VkExtent3D extent)
{
   VkExtent3D size = vk_image_mip_level_extent(&image->vk, mip);
   return offset.x == 0 && offset.y == 0 && offset.z == 0 &&
          extent.width == size.width && extent.height == size.height &&
          extent.depth == size.depth;
}

bool
fsa_image_command(const struct vk_cmd_queue_entry *cmd)
{
   if (cmd->type == VK_CMD_COPY_BUFFER_TO_IMAGE2 ||
       cmd->type == VK_CMD_COPY_IMAGE_TO_BUFFER2) {
      bool write = cmd->type == VK_CMD_COPY_BUFFER_TO_IMAGE2;
      const VkCopyBufferToImageInfo2 *to =
         write ? cmd->u.copy_buffer_to_image2.copy_buffer_to_image_info : NULL;
      const VkCopyImageToBufferInfo2 *from =
         write ? NULL : cmd->u.copy_image_to_buffer2.copy_image_to_buffer_info;
      struct fsa_buffer *buffer =
         fsa_buffer_from_handle(write ? to->srcBuffer : from->dstBuffer);
      struct fsa_image *image =
         fsa_image_from_handle(write ? to->dstImage : from->srcImage);
      unsigned count = write ? to->regionCount : from->regionCount;
      const VkBufferImageCopy2 *regions = write ? to->pRegions : from->pRegions;
      for (unsigned i = 0; i < count; i++) {
         const VkBufferImageCopy2 *r = &regions[i];
         if (!buffer->memory || !valid_region(image, &r->imageSubresource,
                                              r->imageOffset, r->imageExtent))
            return false;
         struct vk_image_buffer_layout layout =
            vk_image_buffer_copy_layout(&image->vk, r);
         uint64_t range = vk_image_buffer_range(
            &image->vk, &layout, &r->imageExtent, &r->imageSubresource);
         if (r->bufferOffset > buffer->vk.size ||
             range > buffer->vk.size - r->bufferOffset)
            return false;
         if (!r->bufferRowLength && !r->bufferImageHeight &&
             full_mip(image, r->imageSubresource.mipLevel, r->imageOffset,
                      r->imageExtent)) {
            uint64_t image_address = fsa_image_address(
               image, r->imageSubresource.mipLevel,
               r->imageSubresource.baseArrayLayer, r->imageOffset);
            uint64_t buffer_address =
               fsa_buffer_address(buffer) + r->bufferOffset;
            if (!copy_bytes(write ? buffer_address : image_address,
                            write ? image_address : buffer_address, range))
               return false;
            continue;
         }
         for (unsigned layer = 0; layer < r->imageSubresource.layerCount;
              layer++)
            for (unsigned z = 0; z < r->imageExtent.depth; z++)
               for (unsigned y = 0; y < r->imageExtent.height; y++) {
                  VkOffset3D off = r->imageOffset;
                  off.y += y;
                  off.z += z;
                  uint64_t image_address = fsa_image_address(
                     image, r->imageSubresource.mipLevel,
                     r->imageSubresource.baseArrayLayer + layer, off);
                  uint64_t buffer_address = fsa_buffer_address(buffer) +
                                            r->bufferOffset +
                                            (layer * r->imageExtent.depth + z) *
                                               layout.image_stride_B +
                                            (uint64_t)y * layout.row_stride_B;
                  uint64_t bytes =
                     (uint64_t)r->imageExtent.width * layout.element_size_B;
                  if (!copy_bytes(write ? buffer_address : image_address,
                                  write ? image_address : buffer_address,
                                  bytes))
                     return false;
               }
      }
      return true;
   }
   if (cmd->type == VK_CMD_COPY_IMAGE2) {
      const VkCopyImageInfo2 *info = cmd->u.copy_image2.copy_image_info;
      struct fsa_image *src = fsa_image_from_handle(info->srcImage),
                       *dst = fsa_image_from_handle(info->dstImage);
      if (vk_format_get_blocksize(src->vk.format) !=
          vk_format_get_blocksize(dst->vk.format))
         return false;
      for (unsigned i = 0; i < info->regionCount; i++) {
         const VkImageCopy2 *r = &info->pRegions[i];
         bool src_3d = src->vk.image_type == VK_IMAGE_TYPE_3D;
         bool dst_3d = dst->vk.image_type == VK_IMAGE_TYPE_3D;
         VkExtent3D src_extent = r->extent, dst_extent = r->extent;
         if (!src_3d)
            src_extent.depth = 1;
         if (!dst_3d)
            dst_extent.depth = 1;
         if (!valid_region(src, &r->srcSubresource, r->srcOffset, src_extent) ||
             !valid_region(dst, &r->dstSubresource, r->dstOffset, dst_extent))
            return false;
         unsigned slices = MAX2(r->extent.depth, r->srcSubresource.layerCount);
         if (full_mip(src, r->srcSubresource.mipLevel, r->srcOffset,
                      src_extent) &&
             full_mip(dst, r->dstSubresource.mipLevel, r->dstOffset,
                      dst_extent)) {
            uint64_t bytes = (uint64_t)r->extent.width * r->extent.height *
                             slices * vk_format_get_blocksize(src->vk.format);
            if (!copy_bytes(fsa_image_address(src, r->srcSubresource.mipLevel,
                                              r->srcSubresource.baseArrayLayer,
                                              r->srcOffset),
                            fsa_image_address(dst, r->dstSubresource.mipLevel,
                                              r->dstSubresource.baseArrayLayer,
                                              r->dstOffset),
                            bytes))
               return false;
            continue;
         }
         for (unsigned slice = 0; slice < slices; slice++)
            for (unsigned y = 0; y < r->extent.height; y++) {
               VkOffset3D a = r->srcOffset, b = r->dstOffset;
               a.y += y;
               b.y += y;
               a.z += src_3d ? slice : 0;
               b.z += dst_3d ? slice : 0;
               if (!copy_bytes(
                      fsa_image_address(src, r->srcSubresource.mipLevel,
                                        r->srcSubresource.baseArrayLayer +
                                           (src_3d ? 0 : slice),
                                        a),
                      fsa_image_address(dst, r->dstSubresource.mipLevel,
                                        r->dstSubresource.baseArrayLayer +
                                           (dst_3d ? 0 : slice),
                                        b),
                      (uint64_t)r->extent.width *
                         vk_format_get_blocksize(src->vk.format)))
                  return false;
            }
      }
      return true;
   }
   if (cmd->type == VK_CMD_CLEAR_COLOR_IMAGE) {
      struct fsa_image *image =
         fsa_image_from_handle(cmd->u.clear_color_image.image);
      if (!image->memory)
         return false;
      union util_color color;
      enum pipe_format format = vk_format_to_pipe_format(image->vk.format);
      util_format_pack_rgba(format, &color, cmd->u.clear_color_image.color, 1);
      unsigned pixel = vk_format_get_blocksize(image->vk.format);
      uint8_t row[4096];
      for (unsigned i = 0; i < sizeof(row); i += pixel)
         memcpy(row + i, &color, pixel);
      for (unsigned i = 0; i < cmd->u.clear_color_image.range_count; i++) {
         const VkImageSubresourceRange *range =
            &cmd->u.clear_color_image.ranges[i];
         unsigned layers = vk_image_subresource_layer_count(&image->vk, range);
         uint64_t size = (uint64_t)image->vk.extent.width *
                         image->vk.extent.height * image->vk.extent.depth *
                         pixel * layers;
         uint64_t address =
            fsa_image_address(image, 0, range->baseArrayLayer, (VkOffset3D){0});
         while (size) {
            uint64_t bytes = MIN2(size, sizeof(row));
            if (fsa_copy_to_dev(address, row, bytes))
               return false;
            address += bytes;
            size -= bytes;
         }
      }
      return true;
   }
   return false;
}
