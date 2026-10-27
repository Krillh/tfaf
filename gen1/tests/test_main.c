#include "test_support.h"

#include <stdio.h>

int main(void) {
  static const test_case cases[] = {
      {"workspace and empty minimum image", test_workspace_and_empty_image},
      {"unaligned multi-block table and public read API",
       test_table_iteration_lookup_and_reading},
      {"odd block size and printable-name boundaries",
       test_odd_block_size_and_name_edges},
      {"empty files and permitted data gaps", test_empty_files_and_gaps},
      {"header and device geometry rejection", test_header_rejections},
      {"entry bounds, names, and uniqueness", test_entry_and_name_rejections},
      {"allocated ranges and boundary rules", test_allocation_ranges},
      {"handle ownership and mount lifecycle",
       test_interface_handles_and_lifecycle},
      {"read failures and partial progress", test_io_errors_and_partial_progress},
      {"bounded validation work", test_validation_work_limit},
      {"HAL geometry and block reader", test_hal_open_and_reader},
      {"HAL root disk selection", test_hal_root_selection},
  };
  unsigned passed = 0;
  for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
    if (!cases[i].run()) {
      fprintf(stderr, "FAIL %s\n", cases[i].name);
      return 1;
    }
    ++passed;
    printf("PASS %s\n", cases[i].name);
  }
  printf("%u TFAF1 test groups passed\n", passed);
  return 0;
}
