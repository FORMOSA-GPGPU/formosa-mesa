/* SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 * SPDX-License-Identifier: MIT
 */

#include <elf.h>
#include "util/u_memory.h"
#include "vk_private.h"

VKAPI_ATTR VkResult VKAPI_CALL
fsa_AllocateMemory(VkDevice handle, const VkMemoryAllocateInfo *info,
                   const VkAllocationCallbacks *alloc, VkDeviceMemory *out)
{
   VK_FROM_HANDLE(vk_device, device, handle);
   if (info->memoryTypeIndex > 1 || !info->allocationSize)
      return VK_ERROR_OUT_OF_DEVICE_MEMORY;
   struct fsa_memory *memory =
      vk_device_memory_create(device, info, alloc, sizeof(*memory));
   if (!memory)
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   if (fsa_malloc(&memory->address, info->allocationSize)) {
      vk_device_memory_destroy(device, alloc, &memory->vk);
      return VK_ERROR_OUT_OF_DEVICE_MEMORY;
   }
   if (info->memoryTypeIndex == 1) {
      memory->host =
         align_calloc(info->allocationSize,
                      device->physical->properties.minMemoryMapAlignment);
      if (!memory->host) {
         fsa_free(memory->address);
         vk_device_memory_destroy(device, alloc, &memory->vk);
         return VK_ERROR_OUT_OF_HOST_MEMORY;
      }
   }
   *out = fsa_memory_to_handle(memory);
   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
fsa_FreeMemory(VkDevice handle, VkDeviceMemory mem,
               const VkAllocationCallbacks *alloc)
{
   VK_FROM_HANDLE(vk_device, device, handle);
   VK_FROM_HANDLE(fsa_memory, memory, mem);
   if (!memory)
      return;
   fsa_free(memory->address);
   align_free(memory->host);
   vk_device_memory_destroy(device, alloc, &memory->vk);
}

VKAPI_ATTR VkResult VKAPI_CALL
fsa_MapMemory2KHR(VkDevice handle, const VkMemoryMapInfoKHR *info, void **out)
{
   VK_FROM_HANDLE(fsa_memory, memory, info->memory);
   if (!memory->host || memory->mapped || info->offset >= memory->vk.size ||
       (info->size != VK_WHOLE_SIZE &&
        info->size > memory->vk.size - info->offset))
      return VK_ERROR_MEMORY_MAP_FAILED;
   memory->mapped = true;
   *out = (char *)memory->host + info->offset;
   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
fsa_UnmapMemory2KHR(VkDevice handle, const VkMemoryUnmapInfoKHR *info)
{
   VK_FROM_HANDLE(fsa_memory, memory, info->memory);
   memory->mapped = false;
   return VK_SUCCESS;
}

static VkResult
mapped_ranges(VkDevice handle, uint32_t count,
              const VkMappedMemoryRange *ranges, bool flush)
{
   VK_FROM_HANDLE(vk_device, device, handle);
   for (unsigned i = 0; i < count; i++) {
      VK_FROM_HANDLE(fsa_memory, memory, ranges[i].memory);
      if (!memory->mapped || ranges[i].offset > memory->vk.size)
         return VK_ERROR_MEMORY_MAP_FAILED;
      uint64_t size = ranges[i].size == VK_WHOLE_SIZE
                         ? memory->vk.size - ranges[i].offset
                         : ranges[i].size;
      if (size > memory->vk.size - ranges[i].offset)
         return VK_ERROR_MEMORY_MAP_FAILED;
      uint64_t address = (uintptr_t)memory->address + ranges[i].offset;
      void *host = (char *)memory->host + ranges[i].offset;
      if (flush ? fsa_copy_to_dev(address, host, size)
                : fsa_copy_from_dev(address, host, size))
         return vk_device_set_lost(device, "Mapped memory transfer failed");
   }
   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
fsa_FlushMappedMemoryRanges(VkDevice handle, uint32_t count,
                            const VkMappedMemoryRange *ranges)
{
   return mapped_ranges(handle, count, ranges, true);
}

VKAPI_ATTR VkResult VKAPI_CALL
fsa_InvalidateMappedMemoryRanges(VkDevice handle, uint32_t count,
                                 const VkMappedMemoryRange *ranges)
{
   return mapped_ranges(handle, count, ranges, false);
}

VKAPI_ATTR VkResult VKAPI_CALL
fsa_CreateBuffer(VkDevice handle, const VkBufferCreateInfo *info,
                 const VkAllocationCallbacks *alloc, VkBuffer *out)
{
   VK_FROM_HANDLE(vk_device, device, handle);
   if (info->flags || !info->size)
      return VK_ERROR_FEATURE_NOT_PRESENT;
   struct fsa_buffer *buffer =
      vk_buffer_create(device, info, alloc, sizeof(*buffer));
   if (!buffer)
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   *out = fsa_buffer_to_handle(buffer);
   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
fsa_DestroyBuffer(VkDevice handle, VkBuffer buf,
                  const VkAllocationCallbacks *alloc)
{
   VK_FROM_HANDLE(vk_device, device, handle);
   VK_FROM_HANDLE(fsa_buffer, buffer, buf);
   if (buffer)
      vk_buffer_destroy(device, alloc, &buffer->vk);
}

VKAPI_ATTR void VKAPI_CALL
fsa_GetBufferMemoryRequirements2(VkDevice device,
                                 const VkBufferMemoryRequirementsInfo2 *info,
                                 VkMemoryRequirements2 *out)
{
   VK_FROM_HANDLE(fsa_buffer, buffer, info->buffer);
   out->memoryRequirements = (VkMemoryRequirements){
      .size = align64(buffer->vk.size, 64),
      .alignment = 64,
      .memoryTypeBits = 3,
   };
}

VKAPI_ATTR VkResult VKAPI_CALL
fsa_BindBufferMemory2(VkDevice device, uint32_t count,
                      const VkBindBufferMemoryInfo *infos)
{
   for (unsigned i = 0; i < count; i++) {
      VK_FROM_HANDLE(fsa_buffer, buffer, infos[i].buffer);
      VK_FROM_HANDLE(fsa_memory, memory, infos[i].memory);
      if (infos[i].memoryOffset > memory->vk.size ||
          buffer->vk.size > memory->vk.size - infos[i].memoryOffset)
         return VK_ERROR_OUT_OF_DEVICE_MEMORY;
      buffer->memory = memory;
      buffer->offset = infos[i].memoryOffset;
   }
   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
fsa_CreateDescriptorSetLayout(VkDevice handle,
                              const VkDescriptorSetLayoutCreateInfo *info,
                              const VkAllocationCallbacks *alloc,
                              VkDescriptorSetLayout *out)
{
   VK_FROM_HANDLE(vk_device, device, handle);
   if (info->flags)
      return VK_ERROR_FEATURE_NOT_PRESENT;
   struct fsa_descriptor_layout *layout =
      (struct fsa_descriptor_layout *)vk_descriptor_set_layout_zalloc(
         device, sizeof(*layout));
   if (!layout)
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   uint32_t used = 0;
   for (unsigned i = 0; i < info->bindingCount; i++) {
      const VkDescriptorSetLayoutBinding *binding = &info->pBindings[i];
      if (binding->binding >= FSA_COMPUTE_MAX_BINDINGS ||
          binding->descriptorCount > FSA_COMPUTE_MAX_BUFFERS - layout->count ||
          (binding->descriptorType != VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER &&
           binding->descriptorType != VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER &&
           binding->descriptorType != VK_DESCRIPTOR_TYPE_SAMPLER &&
           binding->descriptorType !=
              VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER &&
           binding->descriptorType != VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE &&
           binding->descriptorType != VK_DESCRIPTOR_TYPE_STORAGE_IMAGE &&
           binding->descriptorType != VK_DESCRIPTOR_TYPE_STORAGE_BUFFER &&
           binding->descriptorType != VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER &&
           binding->descriptorType !=
              VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC &&
           binding->descriptorType !=
              VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC) ||
          (used & (1u << binding->binding))) {
         vk_descriptor_set_layout_unref(device, &layout->vk);
         return VK_ERROR_FEATURE_NOT_PRESENT;
      }
      used |= 1u << binding->binding;
      layout->bindings[binding->binding].type = binding->descriptorType;
      layout->bindings[binding->binding].stages = binding->stageFlags;
      layout->bindings[binding->binding].count = binding->descriptorCount;
      layout->count += binding->descriptorCount;
   }
   uint32_t cursor = 0;
   for (unsigned b = 0; b < FSA_COMPUTE_MAX_BINDINGS; b++) {
      layout->bindings[b].offset = cursor;
      cursor += layout->bindings[b].count;
   }
   for (unsigned i = 0; i < info->bindingCount; i++) {
      const VkDescriptorSetLayoutBinding *binding = &info->pBindings[i];
      if (binding->pImmutableSamplers &&
          (binding->descriptorType == VK_DESCRIPTOR_TYPE_SAMPLER ||
           binding->descriptorType ==
              VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER))
         for (unsigned j = 0; j < binding->descriptorCount; j++)
            layout->immutable[layout->bindings[binding->binding].offset + j] =
               fsa_sampler_from_handle(binding->pImmutableSamplers[j])->value;
   }
   _mesa_blake3_compute(
      &layout->count,
      sizeof(*layout) - offsetof(struct fsa_descriptor_layout, count),
      layout->vk.blake3);
   *out = fsa_descriptor_layout_to_handle(layout);
   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
fsa_CreatePipelineLayout(VkDevice handle,
                         const VkPipelineLayoutCreateInfo *info,
                         const VkAllocationCallbacks *alloc,
                         VkPipelineLayout *out)
{
   VK_FROM_HANDLE(vk_device, device, handle);
   if (info->setLayoutCount > FSA_COMPUTE_MAX_SETS || info->flags)
      return VK_ERROR_FEATURE_NOT_PRESENT;
   for (unsigned i = 0; i < info->pushConstantRangeCount; i++) {
      const VkPushConstantRange *range = &info->pPushConstantRanges[i];
      if (range->offset > FSA_COMPUTE_PUSH_SIZE ||
          range->size > FSA_COMPUTE_PUSH_SIZE - range->offset)
         return VK_ERROR_FEATURE_NOT_PRESENT;
   }
   struct vk_pipeline_layout *layout =
      vk_pipeline_layout_zalloc(device, sizeof(*layout), info);
   if (!layout)
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   *out = vk_pipeline_layout_to_handle(layout);
   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
fsa_CreateDescriptorPool(VkDevice handle,
                         const VkDescriptorPoolCreateInfo *info,
                         const VkAllocationCallbacks *alloc,
                         VkDescriptorPool *out)
{
   VK_FROM_HANDLE(vk_device, device, handle);
   struct fsa_descriptor_pool *pool = vk_object_zalloc(
      device, alloc, sizeof(*pool), VK_OBJECT_TYPE_DESCRIPTOR_POOL);
   if (!pool)
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   list_inithead(&pool->sets);
   pool->maximum = info->maxSets;
   *out = fsa_descriptor_pool_to_handle(pool);
   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
fsa_ResetDescriptorPool(VkDevice handle, VkDescriptorPool pool_handle,
                        VkDescriptorPoolResetFlags flags)
{
   VK_FROM_HANDLE(vk_device, device, handle);
   VK_FROM_HANDLE(fsa_descriptor_pool, pool, pool_handle);
   list_for_each_entry_safe (struct fsa_descriptor_set, set, &pool->sets,
                             link) {
      list_del(&set->link);
      vk_descriptor_set_layout_unref(device, &set->layout->vk);
      vk_object_free(device, NULL, set);
   }
   pool->count = 0;
   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
fsa_DestroyDescriptorPool(VkDevice handle, VkDescriptorPool pool_handle,
                          const VkAllocationCallbacks *alloc)
{
   VK_FROM_HANDLE(vk_device, device, handle);
   VK_FROM_HANDLE(fsa_descriptor_pool, pool, pool_handle);
   if (!pool)
      return;
   fsa_ResetDescriptorPool(handle, pool_handle, 0);
   vk_object_free(device, alloc, pool);
}

VKAPI_ATTR VkResult VKAPI_CALL
fsa_AllocateDescriptorSets(VkDevice handle,
                           const VkDescriptorSetAllocateInfo *info,
                           VkDescriptorSet *out)
{
   VK_FROM_HANDLE(vk_device, device, handle);
   VK_FROM_HANDLE(fsa_descriptor_pool, pool, info->descriptorPool);
   if (info->descriptorSetCount > pool->maximum - pool->count)
      return VK_ERROR_OUT_OF_POOL_MEMORY;
   for (unsigned i = 0; i < info->descriptorSetCount; i++) {
      struct fsa_descriptor_set *set = vk_object_zalloc(
         device, NULL, sizeof(*set), VK_OBJECT_TYPE_DESCRIPTOR_SET);
      if (!set) {
         for (unsigned j = 0; j < i; j++) {
            struct fsa_descriptor_set *old =
               fsa_descriptor_set_from_handle(out[j]);
            list_del(&old->link);
            vk_descriptor_set_layout_unref(device, &old->layout->vk);
            vk_object_free(device, NULL, old);
            out[j] = VK_NULL_HANDLE;
            pool->count--;
         }
         return VK_ERROR_OUT_OF_HOST_MEMORY;
      }
      set->layout = fsa_descriptor_layout_from_handle(info->pSetLayouts[i]);
      vk_descriptor_set_layout_ref(&set->layout->vk);
      memcpy(set->samplers, set->layout->immutable, sizeof(set->samplers));
      list_addtail(&set->link, &pool->sets);
      pool->count++;
      out[i] = fsa_descriptor_set_to_handle(set);
   }
   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
fsa_FreeDescriptorSets(VkDevice handle, VkDescriptorPool pool_handle,
                       uint32_t count, const VkDescriptorSet *sets)
{
   VK_FROM_HANDLE(vk_device, device, handle);
   VK_FROM_HANDLE(fsa_descriptor_pool, pool, pool_handle);
   for (unsigned i = 0; i < count; i++) {
      VK_FROM_HANDLE(fsa_descriptor_set, set, sets[i]);
      if (!set)
         continue;
      list_del(&set->link);
      vk_descriptor_set_layout_unref(device, &set->layout->vk);
      vk_object_free(device, NULL, set);
      pool->count--;
   }
   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
fsa_UpdateDescriptorSets(VkDevice handle, uint32_t write_count,
                         const VkWriteDescriptorSet *writes,
                         uint32_t copy_count, const VkCopyDescriptorSet *copies)
{
   VK_FROM_HANDLE(vk_device, device, handle);
   for (unsigned i = 0; i < write_count; i++) {
      const VkWriteDescriptorSet *w = &writes[i];
      VK_FROM_HANDLE(fsa_descriptor_set, set, w->dstSet);
      uint32_t offset =
         set->layout->bindings[w->dstBinding].offset + w->dstArrayElement;
      assert(offset + w->descriptorCount <= set->layout->count);
      for (unsigned j = 0; j < w->descriptorCount; j++) {
         unsigned index = offset + j;
         struct fsa_compute_buffer *buffer = &set->buffers[index];
         *buffer = (struct fsa_compute_buffer){0};
         set->images[index] = (dev_image_t){0};
         set->buffer_available[index] = 0;
         set->samplers[index] = set->layout->immutable[index];
         if (w->descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER ||
             w->descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER) {
            struct vk_buffer_view *view =
               vk_buffer_view_from_handle(w->pTexelBufferView[j]);
            if (!fsa_texel_descriptor(view, &set->images[index])) {
               vk_device_set_lost(device, "Invalid texel buffer descriptor");
               return;
            }
            struct fsa_buffer *buf =
               container_of(view->buffer, struct fsa_buffer, vk);
            *buffer = (struct fsa_compute_buffer){
               fsa_buffer_address(buf) + view->offset, view->range};
            set->buffer_available[index] = view->range;
         } else if (w->descriptorType == VK_DESCRIPTOR_TYPE_SAMPLER ||
                    w->descriptorType ==
                       VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER ||
                    w->descriptorType == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE ||
                    w->descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE) {
            if (w->descriptorType != VK_DESCRIPTOR_TYPE_SAMPLER) {
               struct vk_image_view *view =
                  vk_image_view_from_handle(w->pImageInfo[j].imageView);
               if (!fsa_image_descriptor(view, &set->images[index])) {
                  vk_device_set_lost(device, "Invalid image descriptor");
                  return;
               }
               struct fsa_image *image =
                  container_of(view->image, struct fsa_image, vk);
               *buffer = (struct fsa_compute_buffer){
                  (uintptr_t)image->memory->address + image->offset,
                  image->size};
               set->buffer_available[index] = image->size;
            }
            if ((w->descriptorType == VK_DESCRIPTOR_TYPE_SAMPLER ||
                 w->descriptorType ==
                    VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER) &&
                !set->layout->immutable[index])
               set->samplers[index] =
                  fsa_sampler_from_handle(w->pImageInfo[j].sampler)->value;
         } else {
            const VkDescriptorBufferInfo *info = &w->pBufferInfo[j];
            struct fsa_buffer *buf = fsa_buffer_from_handle(info->buffer);
            if (!buf->memory || info->offset > buf->vk.size) {
               vk_device_set_lost(device, "Invalid buffer descriptor");
               return;
            }
            set->buffer_available[index] = buf->vk.size - info->offset;
            *buffer = (struct fsa_compute_buffer){
               fsa_buffer_address(buf) + info->offset,
               info->range == VK_WHOLE_SIZE ? set->buffer_available[index]
                                            : info->range};
            if (buffer->size > set->buffer_available[index]) {
               vk_device_set_lost(device, "Buffer descriptor range overflow");
               return;
            }
         }
      }
   }
   for (unsigned i = 0; i < copy_count; i++) {
      const VkCopyDescriptorSet *c = &copies[i];
      VK_FROM_HANDLE(fsa_descriptor_set, src, c->srcSet);
      VK_FROM_HANDLE(fsa_descriptor_set, dst, c->dstSet);
      uint32_t from =
         src->layout->bindings[c->srcBinding].offset + c->srcArrayElement;
      uint32_t to =
         dst->layout->bindings[c->dstBinding].offset + c->dstArrayElement;
      assert(from + c->descriptorCount <= src->layout->count &&
             to + c->descriptorCount <= dst->layout->count);
      memmove(dst->buffers + to, src->buffers + from,
              c->descriptorCount * sizeof(*src->buffers));
      memmove(dst->images + to, src->images + from,
              c->descriptorCount * sizeof(*src->images));
      for (unsigned j = 0; j < c->descriptorCount; j++) {
         uint32_t immutable = dst->layout->immutable[to + j];
         dst->samplers[to + j] =
            immutable ? immutable : src->samplers[from + j];
      }
      memmove(dst->buffer_available + to, src->buffer_available + from,
              c->descriptorCount * sizeof(*src->buffer_available));
   }
}

bool
fsa_upload_binary(struct fsa_pipeline *pipeline)
{
   const uint8_t *data = pipeline->binary.data;
   size_t size = pipeline->binary.size;
   if (size < sizeof(Elf64_Ehdr))
      return false;
   const Elf64_Ehdr *eh = (const void *)data;
   if (memcmp(eh->e_ident, ELFMAG, SELFMAG) ||
       eh->e_ident[EI_CLASS] != ELFCLASS64 || eh->e_machine != EM_RISCV ||
       eh->e_phentsize != sizeof(Elf64_Phdr) || eh->e_phoff > size ||
       eh->e_phnum > (size - eh->e_phoff) / sizeof(Elf64_Phdr))
      return false;
   const Elf64_Phdr *ph = (const void *)(data + eh->e_phoff);
   uint64_t image_size = 0;
   for (unsigned i = 0; i < eh->e_phnum; i++) {
      if (ph[i].p_type != PT_LOAD)
         continue;
      if (ph[i].p_offset > size || ph[i].p_filesz > size - ph[i].p_offset ||
          ph[i].p_filesz > ph[i].p_memsz ||
          ph[i].p_vaddr > UINT64_MAX - ph[i].p_memsz)
         return false;
      image_size = MAX2(image_size, ph[i].p_vaddr + ph[i].p_memsz);
   }
   if (!image_size || image_size > 64 * 1024 * 1024 ||
       eh->e_entry >= image_size)
      return false;
   uint8_t *image = calloc(1, image_size);
   if (!image || fsa_malloc(&pipeline->address, image_size)) {
      free(image);
      return false;
   }
   for (unsigned i = 0; i < eh->e_phnum; i++) {
      if (ph[i].p_type == PT_LOAD)
         memcpy(image + ph[i].p_vaddr, data + ph[i].p_offset, ph[i].p_filesz);
   }
   bool ok = eh->e_shentsize == sizeof(Elf64_Shdr) && eh->e_shoff <= size &&
             eh->e_shnum <= (size - eh->e_shoff) / sizeof(Elf64_Shdr);
   const Elf64_Shdr *sh = ok ? (const void *)(data + eh->e_shoff) : NULL;
   for (unsigned i = 0; ok && i < eh->e_shnum; i++) {
      if (sh[i].sh_type != SHT_RELA)
         continue;
      if (sh[i].sh_offset > size || sh[i].sh_size > size - sh[i].sh_offset ||
          sh[i].sh_size % sizeof(Elf64_Rela)) {
         ok = false;
         break;
      }
      const Elf64_Rela *rel = (const void *)(data + sh[i].sh_offset);
      for (unsigned j = 0; j < sh[i].sh_size / sizeof(*rel); j++) {
         if (ELF64_R_TYPE(rel[j].r_info) == R_RISCV_NONE)
            continue;
         if (ELF64_R_TYPE(rel[j].r_info) != R_RISCV_RELATIVE ||
             image_size < 8 || rel[j].r_offset > image_size - 8) {
            ok = false;
            break;
         }
         uint64_t address = (uintptr_t)pipeline->address + rel[j].r_addend;
         memcpy(image + rel[j].r_offset, &address, sizeof(address));
      }
   }
   if (ok)
      ok =
         fsa_copy_to_dev((uintptr_t)pipeline->address, image, image_size) == 0;
   free(image);
   if (!ok) {
      fsa_free(pipeline->address);
      pipeline->address = NULL;
      return false;
   }
   pipeline->entry = (uintptr_t)pipeline->address + eh->e_entry;
   return true;
}

VKAPI_ATTR VkResult VKAPI_CALL
fsa_CreateComputePipelines(VkDevice handle, VkPipelineCache cache,
                           uint32_t count,
                           const VkComputePipelineCreateInfo *infos,
                           const VkAllocationCallbacks *alloc, VkPipeline *out)
{
   VK_FROM_HANDLE(vk_device, device, handle);
   for (unsigned i = 0; i < count; i++)
      out[i] = VK_NULL_HANDLE;
   VkResult result = VK_SUCCESS;
   for (unsigned i = 0; i < count; i++) {
      const VkPipelineShaderStageCreateInfo *stage = &infos[i].stage;
      if (stage->stage != VK_SHADER_STAGE_COMPUTE_BIT ||
          (infos[i].flags & ~(VK_PIPELINE_CREATE_DISABLE_OPTIMIZATION_BIT |
                              VK_PIPELINE_CREATE_ALLOW_DERIVATIVES_BIT |
                              VK_PIPELINE_CREATE_DERIVATIVE_BIT |
                              VK_PIPELINE_CREATE_DISPATCH_BASE_BIT))) {
         result = VK_ERROR_FEATURE_NOT_PRESENT;
         continue;
      }
      VK_FROM_HANDLE(vk_shader_module, module, stage->module);
      struct fsa_pipeline *pipeline = vk_object_zalloc(
         device, alloc, sizeof(*pipeline), VK_OBJECT_TYPE_PIPELINE);
      if (!pipeline)
         return VK_ERROR_OUT_OF_HOST_MEMORY;
      uint32_t specialization_count = 0;
      struct nir_spirv_specialization *specializations =
         vk_spec_info_to_nir_spirv(stage->pSpecializationInfo,
                                   &specialization_count);
      bool ok = fsa_compute_compile(
         (const uint32_t *)module->data, module->size / 4, stage->pName,
         specializations, specialization_count, &pipeline->binary);
      free(specializations);
      uint64_t threads = 1;
      for (unsigned d = 0; d < 3; d++)
         threads *= pipeline->binary.local_size[d];
      ok = ok && threads != 0 &&
           fsa_hal_check_occupancy(threads, pipeline->binary.shared_size,
                                   NULL) == 0;
      if (ok)
         ok = fsa_upload_binary(pipeline);
      if (!ok) {
         free(pipeline->binary.data);
         vk_object_free(device, alloc, pipeline);
         result = VK_ERROR_UNKNOWN;
         continue;
      }
      free(pipeline->binary.data);
      pipeline->binary.data = NULL;
      out[i] = fsa_pipeline_to_handle(pipeline);
   }
   return result;
}

VKAPI_ATTR void VKAPI_CALL
fsa_DestroyPipeline(VkDevice handle, VkPipeline pipe,
                    const VkAllocationCallbacks *alloc)
{
   VK_FROM_HANDLE(vk_device, device, handle);
   VK_FROM_HANDLE(fsa_pipeline, pipeline, pipe);
   if (!pipeline)
      return;
   fsa_free(pipeline->address);
   vk_object_free(device, alloc, pipeline);
}
