#include "fsa_lld_helper.h"
#include <string>
#include <vector>
#include <lld/Common/Driver.h>
#include <llvm/ADT/ArrayRef.h>
#include <llvm/Support/raw_ostream.h>

LLD_HAS_DRIVER(elf)

extern "C" void
fsa_lld_free_msg(char *msg)
{
   if (msg)
      free(msg);
}

bool
fsa_lld_link_elf(int argc, const char **argv, char **out_msg)
{
   std::vector<const char *> args;
   args.push_back("ld.lld");
   for (int i = 0; i < argc; ++i) {
      args.push_back(argv[i]);
   }

   std::string output_buffer;
   llvm::raw_string_ostream stream(output_buffer);

   bool result = lld::elf::link(args, stream, stream, false, false);

   if (out_msg) {
      *out_msg = strdup(stream.str().c_str());
   }

   return result;
}