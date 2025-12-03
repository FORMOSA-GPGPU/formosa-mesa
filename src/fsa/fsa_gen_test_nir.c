#include "fsa_gen_test_nir.h"
#include <ctype.h>
#include <ncurses.h>
#include "compiler/nir/nir.h"
#include "compiler/nir/nir_builder.h"

nir_shader *
fsa_gen_add_store_without_input(const nir_shader_compiler_options *options)
{
   nir_shader *shader =
      nir_shader_create(NULL, MESA_SHADER_VERTEX, options, NULL);

   nir_function *func = nir_function_create(shader, "main");
   func->is_entrypoint = true;

   nir_function_impl *impl = nir_function_impl_create(func);
   nir_block *block = nir_start_block(impl);
   nir_builder b = nir_builder_create(impl);
   b.cursor = nir_after_block(block);

   nir_def *val_a = nir_imm_float(&b, 10.5f);
   nir_def *val_b = nir_imm_float(&b, 20.0f);
   nir_def *sum = nir_fadd(&b, val_a, val_b);

   nir_def *offset = nir_imm_int(&b, 0);
   nir_intrinsic_instr *store = nir_store_output(&b, sum, offset);
   /* nir_store_output needs to set config manually */
   nir_intrinsic_set_base(store, VARYING_SLOT_VAR0);
   nir_intrinsic_set_write_mask(store, 0x1);
   nir_intrinsic_set_component(store, 0);
   nir_intrinsic_set_src_type(store, nir_type_float32);

   nir_validate_shader(shader, "manual creation check");

   return shader;
}

nir_shader *
fsa_gen_load_add_store_scalar(const nir_shader_compiler_options *options)
{
   nir_shader *shader =
      nir_shader_create(NULL, MESA_SHADER_VERTEX, options, NULL);

   nir_function *func = nir_function_create(shader, "main");
   func->is_entrypoint = true;

   nir_function_impl *impl = nir_function_impl_create(func);
   nir_block *block = nir_start_block(impl);
   nir_builder b = nir_builder_create(impl);
   b.cursor = nir_after_block(block);

   nir_def *const_z = nir_imm_int(&b, 0);
   nir_def *test1 = nir_load_input(&b, 1, 32, const_z);
   nir_def *test2 = nir_load_input(&b, 1, 32, const_z);
   nir_def *sum = nir_fadd(&b, test1, test2);

   nir_def *offset = nir_imm_int(&b, 0);
   nir_intrinsic_instr *store = nir_store_output(&b, sum, offset);
   /* nir_store_output needs to set config manually */
   nir_intrinsic_set_base(store, VARYING_SLOT_VAR0);
   nir_intrinsic_set_write_mask(store, 0x1);
   nir_intrinsic_set_component(store, 0);
   nir_intrinsic_set_src_type(store, nir_type_float32);

   nir_validate_shader(shader, "manual creation check");

   return shader;
}

nir_shader *
fsa_gen_load_add_store_vec4(const nir_shader_compiler_options *options)
{
   nir_shader *shader =
      nir_shader_create(NULL, MESA_SHADER_VERTEX, options, NULL);

   nir_function *func = nir_function_create(shader, "main");
   func->is_entrypoint = true;

   nir_function_impl *impl = nir_function_impl_create(func);
   nir_block *block = nir_start_block(impl);
   nir_builder b = nir_builder_create(impl);
   b.cursor = nir_after_block(block);

   nir_def *const_z = nir_imm_int(&b, 0);
   nir_def *test1 = nir_load_input(&b, 4, 32, const_z);
   nir_def *test2 = nir_load_input(&b, 4, 32, const_z);
   nir_def *sum = nir_fadd(&b, test1, test2);

   nir_def *offset = nir_imm_int(&b, 0);
   nir_intrinsic_instr *store = nir_store_output(&b, sum, offset);
   nir_intrinsic_set_base(store, VARYING_SLOT_VAR0);
   nir_intrinsic_set_write_mask(store, 0b1111);
   nir_intrinsic_set_component(store, 0);
   nir_intrinsic_set_src_type(store, nir_type_float32);

   nir_validate_shader(shader, "manual creation check");

   return shader;
}

nir_shader *
fsa_gen_load_add_store_vec3(const nir_shader_compiler_options *options)
{
   nir_shader *shader =
      nir_shader_create(NULL, MESA_SHADER_VERTEX, options, NULL);

   nir_function *func = nir_function_create(shader, "main");
   func->is_entrypoint = true;

   nir_function_impl *impl = nir_function_impl_create(func);
   nir_block *block = nir_start_block(impl);
   nir_builder b = nir_builder_create(impl);
   b.cursor = nir_after_block(block);

   nir_def *const_z = nir_imm_int(&b, 0);
   nir_def *test1 = nir_load_input(&b, 3, 32, const_z);
   nir_def *test2 = nir_load_input(&b, 3, 32, const_z);
   nir_def *sum = nir_fadd(&b, test1, test2);

   nir_def *offset = nir_imm_int(&b, 0);
   nir_intrinsic_instr *store = nir_store_output(&b, sum, offset);
   nir_intrinsic_set_base(store, VARYING_SLOT_VAR0);
   nir_intrinsic_set_write_mask(store, 0b111);
   nir_intrinsic_set_component(store, 0);
   nir_intrinsic_set_src_type(store, nir_type_float32);

   nir_validate_shader(shader, "manual creation check");

   return shader;
}

struct fsa_nir_microbench_struct fsa_nir_benchlist[] = {
   {"load_add_store_scalar", "Scalar-load,fadd,store",
    fsa_gen_load_add_store_scalar},
   {"load_add_store_vec3", "Vec3-load,fadd,store", fsa_gen_load_add_store_vec3},
   {"load_add_store_vec4", "Vec4-load,fadd,store", fsa_gen_load_add_store_vec4},
   {NULL, NULL, NULL},
};

int
fsa_bench_count()
{
   int i = 0;
   while (fsa_nir_benchlist[i].name != NULL)
      i++;
   return i;
}

static int
is_fuzzy_match(const char *pattern, const char *str)
{
   while (*pattern) {
      if (!*str)
         return 0;
      if (tolower((unsigned char)*pattern) == tolower((unsigned char)*str)) {
         pattern++;
      }
      str++;
   }
   return 1;
}

int
fsa_select_bench_interactive()
{
   int total_count = fsa_bench_count();

   int *filtered_indices = malloc(sizeof(int) * total_count);
   int filtered_count = 0;

   char query[256] = {0};
   int highlight = 0;
   int choice = -1;
   int ch;

   initscr();
   cbreak();
   noecho();
   curs_set(0);
   keypad(stdscr, TRUE);

   while (choice == -1) {
      filtered_count = 0;
      for (int i = 0; i < total_count; i++) {
         if (is_fuzzy_match(query, fsa_nir_benchlist[i].name)) {
            filtered_indices[filtered_count++] = i;
         }
      }

      if (highlight >= filtered_count)
         highlight = (filtered_count > 0) ? filtered_count - 1 : 0;

      clear();

      attron(A_BOLD);
      mvprintw(0, 0, "Search: %s_", query);
      attroff(A_BOLD);
      mvprintw(1, 0, "------------------------------------------------");

      if (filtered_count == 0) {
         mvprintw(3, 2, "No matches found.");
      } else {
         for (int i = 0; i < filtered_count; ++i) {
            int real_idx = filtered_indices[i];

            int desc_start_col = 45;
            int max_width = COLS - desc_start_col - 1;

            if (i == highlight)
               attron(A_REVERSE);

            mvprintw(i + 2, 2, "[%3d] %s", real_idx,
                     fsa_nir_benchlist[real_idx].name);

            if (i == highlight)
               attroff(A_REVERSE);

            if (max_width > 5) {
               const char *desc = fsa_nir_benchlist[real_idx].desc;
               mvprintw(i + 2, desc_start_col, "| %.*s", max_width - 2, desc);
            }
         }
      }

      refresh();

      ch = getch();

      switch (ch) {
      case KEY_UP:
         if (highlight > 0)
            highlight--;
         break;
      case KEY_DOWN:
         if (highlight < filtered_count - 1)
            highlight++;
         break;
      case 10: // Enter
         if (filtered_count > 0) {
            choice = filtered_indices[highlight];
         }
         break;
      case KEY_BACKSPACE:
      case 127:
      case '\b': {
         size_t len = strlen(query);
         if (len > 0)
            query[len - 1] = '\0';
         highlight = 0;
      } break;
      default:
         if (isprint(ch)) {
            size_t len = strlen(query);
            if (len < sizeof(query) - 1) {
               query[len] = (char)ch;
               query[len + 1] = '\0';
               highlight = 0;
            }
         }
         break;
      }
   }

   free(filtered_indices);
   endwin();
   return choice;
}