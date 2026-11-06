/* SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <stdlib.h>
#include "compiler.h"

int
main(int argc, char **argv)
{
   if (argc != 3) {
      fprintf(stderr, "usage: %s shader.spv shader.elf\n", argv[0]);
      return 2;
   }
   FILE *input = fopen(argv[1], "rb");
   if (!input || fseek(input, 0, SEEK_END))
      return 1;
   long size = ftell(input);
   if (size < 20 || size % 4 || fseek(input, 0, SEEK_SET)) {
      fclose(input);
      return 1;
   }
   uint32_t *words = malloc(size);
   bool read = words && fread(words, 1, size, input) == (size_t)size;
   fclose(input);
   struct fsa_compute_binary binary;
   bool ok =
      read && fsa_compute_compile(words, size / 4, "main", NULL, 0, &binary);
   free(words);
   if (!ok)
      return 1;
   FILE *output = fopen(argv[2], "wb");
   ok = output && fwrite(binary.data, 1, binary.size, output) == binary.size;
   if (output)
      ok = fclose(output) == 0 && ok;
   if (ok)
      printf("local_size=%u,%u,%u shared_size=%u elf_bytes=%zu\n",
             binary.local_size[0], binary.local_size[1], binary.local_size[2],
             binary.shared_size, binary.size);
   free(binary.data);
   return ok ? 0 : 1;
}
