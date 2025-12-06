#include "fsa_ubench_menu.h"
#include <ctype.h>
#include "fsa_gen_test_nir.h"

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

   int scroll_top = 0;
   const int SCROLL_OFF = 3;

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

      int header_height = 2;
      int max_rows = LINES - header_height;

      if (max_rows < 1)
         max_rows = 1;

      if (highlight < scroll_top + SCROLL_OFF) {
         scroll_top = highlight - SCROLL_OFF;
      }

      if (highlight > (scroll_top + max_rows - 1) - SCROLL_OFF) {
         scroll_top = highlight - (max_rows - 1) + SCROLL_OFF;
      }

      if (filtered_count > 0 && filtered_count < max_rows) {
         scroll_top = 0;
      }

      if (scroll_top < 0) {
         scroll_top = 0;
      }

      clear();

      attron(A_BOLD);
      mvprintw(0, 0, "Search: %s_", query);
      attroff(A_BOLD);
      mvprintw(1, 0, "------------------------------------------------");

      if (filtered_count == 0) {
         mvprintw(3, 2, "No matches found.");
      } else {
         int end_index = scroll_top + max_rows;
         if (end_index > filtered_count)
            end_index = filtered_count;

         for (int i = scroll_top; i < end_index; ++i) {
            int real_idx = filtered_indices[i];

            int screen_y = (i - scroll_top) + header_height;

            int desc_start_col = 45;
            int max_width = COLS - desc_start_col - 1;

            if (i == highlight)
               attron(A_REVERSE);

            mvprintw(screen_y, 2, "[%3d] %s", real_idx,
                     fsa_nir_benchlist[real_idx].name);

            if (i == highlight)
               attroff(A_REVERSE);

            if (max_width > 5) {
               const char *desc = fsa_nir_benchlist[real_idx].desc;
               if (desc) {
                  mvprintw(screen_y, desc_start_col, "| %.*s", max_width - 2,
                           desc);
               }
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
         scroll_top = 0;
      } break;
      default:
         if (isprint(ch)) {
            size_t len = strlen(query);
            if (len < sizeof(query) - 1) {
               query[len] = (char)ch;
               query[len + 1] = '\0';
               highlight = 0;
               scroll_top = 0;
            }
         }
         break;
      }
   }

   free(filtered_indices);
   endwin();
   return choice;
}