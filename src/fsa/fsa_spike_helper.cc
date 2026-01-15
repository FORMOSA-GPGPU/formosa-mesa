#include "fsa_spike_helper.h"
#include "riscv/sim.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

struct SpikeMachine {
   sim_t *sim;
   std::vector<std::pair<reg_t, abstract_mem_t *>> mems;
   std::vector<std::string> args;
};

extern "C" SpikeMachine *
spike_create(const char *elf_name, uint64_t max_memory)
{
   SpikeMachine *machine = new SpikeMachine;
   machine->args.push_back(elf_name);

   mem_cfg_t mem_cfg(0x0, max_memory);
   mem_t *main_mem = new mem_t(max_memory);
   machine->mems.push_back(std::make_pair(0x1000, main_mem));

   const char *isa = "RV64IMFD_zicsr";
   const char *priv = "M";

   cfg_t cfg;
   cfg.isa = isa;
   cfg.priv = priv;
   cfg.mem_layout.push_back(mem_cfg);
   cfg.hartids.push_back(0);
   cfg.start_pc = std::nullopt;

   std::vector<device_factory_sargs_t> plugin_factories;
   std::vector<std::string> args;
   args.push_back(elf_name);

   debug_module_config_t dm_config;
   const char *log_path = nullptr;
   bool dtb_enabled = false;
   const char *dtb_file = nullptr;
   bool socket_enabled = false;
   FILE *cmd_file = nullptr;
   std::optional<unsigned long long> instruction_limit = std::nullopt;

   machine->sim = new sim_t(&cfg, false, machine->mems, plugin_factories, args,
                            dm_config, log_path, dtb_enabled, dtb_file,
                            socket_enabled, cmd_file, instruction_limit);

	/* Turn on to enter interactive mode */
   //machine->sim->set_debug(true);

   return machine;
}

extern "C" void
spike_destroy(SpikeMachine *machine)
{
   if (!machine)
      return;
   for (auto &m : machine->mems) {
      delete m.second;
   }
   delete machine;
}

extern "C" int
spike_write_memory(SpikeMachine *machine, uint64_t dst, const void *src,
                   size_t len)
{
   if (!machine)
      return -1;
   simif_t *simif = static_cast<simif_t *>(machine->sim);
   simif->mmio_store(dst, len, (const uint8_t *)src);
   return 0;
}

extern "C" int
spike_read_memory(SpikeMachine *machine, void *dst, uint64_t src, size_t len)
{
   if (!machine)
      return -1;
   simif_t *simif = static_cast<simif_t *>(machine->sim);
   simif->mmio_load(src, len, (uint8_t *)dst);
   return 0;
}

extern "C" int
spike_run(SpikeMachine *machine)
{
   if (!machine)
      return -1;
   machine->sim->run();
   return 0;
}