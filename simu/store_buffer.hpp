// See LICENSE for details.

#pragma once

#include <cstdint>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "callback.hpp"
#include "dinst.hpp"
#include "gmemory_system.hpp"
#include "opcode.hpp"
#include "resource.hpp"

// SCB SPEC buffer : directly L3->SCB //#define ENABLE_SCB_SPEC
#define ENABLE_SCB_SPEC
class MemObj;  // To break circular dependencies
class FUStore;
class Store_buffer_line {
public:
  // NOTE: Invalid not used because when invalid it is removed from the map
  enum class State { Uncoherent, Modified, Invalid, Clean };  // UMIC

  State             state;
  bool              transient;
  std::vector<bool> word_present;  // FIXME: dinst does byte info

  Addr_t line_addr;
  
  bool   prefetch_line;       // true = this is a spec prefetch due to spec load, not a store
  Addr_t inducing_spec_ld_addr;  // calc_line(load_addr) of the spec load that triggered this prefetch
  
  
  Store_buffer_line() { state = State::Invalid; }

  void init(size_t line_size, Addr_t addr) {
    I(state == State::Invalid);
    word_present.assign(line_size >> 2, false);
    state     = State::Uncoherent;
    line_addr = addr;
    transient = false;
  }

  // NEW: separate init path for a speculative prefetch line (no word_present needed -- never stored to)
  void init_prefetch(Addr_t addr, Addr_t inducing_spec_ld_line) {
    I(state == State::Invalid);
    state              = State::Uncoherent;
    line_addr          = addr;
    transient          = true;
    prefetch_line      = true;
    inducing_spec_ld_addr = inducing_spec_ld_line;
  }


  void set_waiting_wb() { state = State::Uncoherent; }
  
  void convert_to_store(size_t line_size) {  //NEW                                      
    if (!prefetch_line) {
      return;             
    }                                                                                                                                                                 
    word_present.assign(line_size >> 2, false);                                                                                                                       
    prefetch_line = false;                                                
  } 

  void add_st(Addr_t addr_off) {
    I((addr_off >> 2) < word_present.size());  // pass only the line offset
    word_present[addr_off >> 2] = true;
  }

  bool is_ld_forward(Addr_t addr_off) const { return word_present[addr_off >> 2]; }

  void set_clean() { state = State::Clean; }
  bool is_clean() const { return state == State::Clean; }
  void set_transient() { transient = true; }
  bool is_transient() const { return transient; }
  bool is_prefetch_line() const { return prefetch_line; }
  bool is_waiting_wb() const { return state == State::Uncoherent; }
};

class Store_buffer {
protected:
  MemObj* dl1;

  // FA structure, so a map is fine
  absl::flat_hash_map<Addr_t, Store_buffer_line> scb_lines_map;

  /*scb_size=32*/
  // int    scb_size;
  int scb_clean_lines;
  // int    scb_lines_num;
  size_t line_size;
  size_t line_size_addr_bits;
  size_t line_size_mask;

  Addr_t calc_line(Addr_t addr) const { return addr >> line_size_addr_bits; }
  Addr_t calc_offset(Addr_t addr) const { return addr & line_size_mask; }

  // void remove_clean();

public:
  int  scb_size;
  void ownership_done(Addr_t addr);

  Store_buffer(Hartid_t hid, std::shared_ptr<Gmemory_system> ms);
  ~Store_buffer() {}

  bool can_accept_st(Addr_t st_addr) const;
  void add_st(Dinst* dinst);
  void remove_spec_load(Dinst* dinst);
  bool find(Dinst* dinst);
  bool is_clean_disp(Dinst* dinst);
  void remove_clean();
  int  get_clean_num() const;
  void set_clean_scb(Dinst* dinst);
  void flush_transient();

  bool is_ld_forward(Addr_t ld_addr) const;
 
  // NEW: prefetcher hook -- record a speculative prefetch line, tagged with the load that induced it
  //void try_prefetch(Addr_t paddr, bool doStats, int degree, Addr_t pref_sign, Addr_t pc, Addr_t inducing_spec_load_addr);
  void try_prefetch(Addr_t paddr, bool doStats, Addr_t pc, Addr_t inducing_spec_load_addr);

  // NEW: callback fired when the prefetch's data arrives from memory
  void prefetch_done(Addr_t paddr);

  // NEW: called from FULoad::preretire() once the inducing spec load becomes safe
  void promote_prefetch_scb_to_cache(Addr_t inducing_load_addr, MemObj* l1, bool doStats, Addr_t pc);

};
