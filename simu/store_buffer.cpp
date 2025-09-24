// See LICENSE for details.

#include "store_buffer.hpp"

#include "absl/container/flat_hash_map.h"
#include "absl/strings/str_split.h"
#include "config.hpp"
#include "memrequest.hpp"

using ownership_doneCB = CallbackMember1<Store_buffer, Addr_t, &Store_buffer::ownership_done>;

Store_buffer::Store_buffer(Hartid_t hid, std::shared_ptr<Gmemory_system> ms) {
  std::vector<std::string> v      = absl::StrSplit(Config::get_string("soc", "core", hid, "il1"), ' ');
  auto                     l1_sec = v[0];
  line_size                       = Config::get_power2(l1_sec, "line_size");

  bool enableDcache = Config::get_bool("soc", "core", hid, "caches");
  if (enableDcache) {
    dl1 = ms->getDL1();
  } else {
    dl1 = nullptr;
  }

  //scb_clean_lines     = 0;
  //scb_lines_num       = 0;
  line_size_addr_bits = log2i(line_size);
  line_size_mask      = line_size - 1;
  /*scb_size=32*/
  scb_size            = Config::get_integer("soc", "core", hid, "scb_size", 1, 2048);
  scb_clean_lines     = scb_size;
  //scb_lines_num       = 0;
}

/*bool original Store_buffer::can_accept_st(Addr_t st_addr) const {
  if ((static_cast<int>(scb_lines_map.size()) - scb_clean_lines) < scb_size) {
    printf("Store_buffer::TRUE return::  addr %ld \n", st_addr);
    return true;
  }

  auto it = scb_lines_map.find(calc_line(st_addr));
  if(it != scb_lines_map.end()){
      printf("Store_buffer:map return::  addr already in scb  %ld and line_addr %ld\n", st_addr,calc_line(st_addr));
  } else {
      printf("Store_buffer:map return:: RETURN FALSE  !addr already in scb  %ld and line_addr %ld\n", st_addr,calc_line(st_addr));
  }
  //printf("Store_buffer:map return::  addr %ld and line_addri %ld\n", st_addr,calc_line(st_addr));
  return it != scb_lines_map.end();
}*/
bool Store_buffer::can_accept_st(Addr_t st_addr) const {
  
  /* scb_clean_lines can be wrtiteback to L1cache and new entry can be accepted*/
  /* scb_clean_lines can be wrtiteback to L1cache; so  new space can be created by deleting clean lines; 34-5<32*/
  if ((static_cast<int>(scb_lines_map.size()) - scb_clean_lines) < scb_size) {
    printf("Store_buffer::TRUE return::  addr %ld \n", st_addr);
    return true;
  }

  auto it = scb_lines_map.find(calc_line(st_addr));
  if(it != scb_lines_map.end()){
      printf("Store_buffer:map return::  addr already in scb  %ld and line_addr %ld\n", st_addr,calc_line(st_addr));
      return true;
  } else {
      printf("Store_buffer:map return:: RETURN FALSE  !addr already in scb  %ld and line_addr %ld\n", st_addr,calc_line(st_addr));
  }
  //printf("Store_buffer:map return::  addr %ld and line_addri %ld\n", st_addr,calc_line(st_addr));
  return it != scb_lines_map.end();
}

bool Store_buffer::can_accept(Addr_t addr) const {
/* Icache load miss for spec dinst*/
  
  /* scb_clean_lines can be wrtiteback to L1cache; so  new space can be created by deleting clean lines; 34-5<32*/
  if ((static_cast<int>(scb_lines_map.size()) - scb_clean_lines) < scb_size) {
    printf("Store_buffer::Can accept new entry in scb return::  addr %ld \n", addr);
    return true;
  } else {
   return false;
  } 

  auto it = scb_lines_map.find(calc_line(addr));
  if(it != scb_lines_map.end()){
      printf("Store_buffer:map return::  addr already in scb  %ld and line_addr %ld\n", addr,calc_line(addr));
  } else {
      printf("Store_buffer:map return:: RETURN FALSE  !addr already in scb  %ld and line_addr %ld\n", addr,calc_line(addr));
  }
  //return it != scb_lines_map.end();
}



void Store_buffer::remove_clean() {
  I(scb_clean_lines);

  printf("Store_buffer::remove_clean():: Entering in scb\n");
  size_t num = 0;

  absl::erase_if(scb_lines_map, [&num](std::pair<const Addr_t, Store_buffer_line> p) {
    if (p.second.is_clean()) {
      printf("Store_buffer::remove_clean():: Removing  st_addr_line %ld from scb\n", p.first);
      ++num;
      return true;
    }
    return false;
  });

 printf("Store_buffer::remove_clean:: Before scbcleanlines+num is  %d and num clean removed  is %ld \n", scb_clean_lines, num);
 scb_clean_lines =  scb_clean_lines+num;
 printf("Store_buffer::remove_clean::After scbcleanlines+num is  %d and   num clean removed  is %ld \n", scb_clean_lines, num);
 printf("Store_buffer::remove_clean():: Leaving from scb\n");
}


void Store_buffer::remove(Dinst *dinst) {
 /*spec_load removed from scb*/

  printf("Store_buffer::Remove:: Entering for specLoad to scb inst  %ld\n", dinst->getID());
  //I(scb_lines_num);
  Addr_t addr = dinst->getAddr();
  Addr_t addr_line = calc_line(addr);
   
  printf("Store_buffer::remove::spec load addr %ld and addr_line %ld\n", addr, addr_line);

//Removes the element from the hashmap named 'scb_map' with key erase(key)
//The erase() method typically returns the number of elements removed (0 or 1 when erasing by key) 
//or an iterator i to the element following the erased one (when erasing by iterator). 
  auto it           = scb_lines_map.find(addr_line);
  if (!(it == scb_lines_map.end()) && !it->second.is_waiting_wb()) {

  printf("Store_buffer::remove::Found spec load addr %ld and addr_line %ld\n", addr, addr_line);
  scb_lines_map.erase(addr_line);
  printf("Store_buffer::remove::Removing spec load addr %ld and addr_line %ld\n", addr, addr_line);
  //scb_clean_num++;
  printf("Store_buffer::remove::scbcleanlines++ is  %d  ownership done for add %ld\n", scb_clean_lines, dinst->getID());
  ++scb_clean_lines;
  printf("Store_buffer::remove::scbcleanlines is  %d  ownership done for add %ld\n", scb_clean_lines, dinst->getID());
  }
}
bool Store_buffer::is_clean_disp(Dinst *dinst) {
 /*spec_load removed from scb*/

  printf("Store_buffer::Remove:: Entering for specLoad to scb inst  %ld\n", dinst->getID());
  //I(scb_lines_num);
  Addr_t addr = dinst->getAddr();
  Addr_t addr_line = calc_line(addr);
  auto it           = scb_lines_map.find(addr_line);
  if (!(it == scb_lines_map.end()) && it->second.is_clean()) {
    return true;
  }else {
    return false;
  }
   
  printf("Store_buffer::remove::spec load addr %ld and addr_line %ld\n", addr, addr_line);

}
void Store_buffer::insert(Dinst *dinst) {
/* Icache/Dcache load miss for spec dinst*/
 /*spec_load inserted into scb*/

  printf("Store_buffer::Insert ::insert Load into scb instead of Dcache::dinst  %ld\n", dinst->getID());
  auto addr = dinst->getAddr();
    
  auto addr_line = calc_line(addr);
  auto it           = scb_lines_map.find(addr_line);
  
  if (it == scb_lines_map.end()) {
  /*scb does not has the addr : new entry in map 'scb_map'*/
    printf("Store_buffer::add_st::In scb No entry found  LOAD addr %ld and addr_line %ld\n", addr, addr_line);
    if ((static_cast<int>(scb_lines_map.size()) + scb_clean_lines) >= scb_size) {
      remove_clean();
    }
  
    Store_buffer_line line;

    line.init(line_size, addr_line);
    line.add_st(calc_offset(addr));
    I(line.state == Store_buffer_line::State::Uncoherent);

    scb_lines_map.insert({addr_line, line});
  } else {
    //scb already have it beforhand: duplicate entry
    it->second.add_st(calc_offset(addr));
  }

}

void Store_buffer::add_st(Dinst *dinst) {

  auto st_addr = dinst->getAddr();
  //I(can_accept_st(st_addr));
  printf("Store_buffer::add_st::Entering store add_st in scb for dinst  %ld\n", dinst->getID());
  printf("Store_buffer::add_st::scbcleanlines is  %d\n", scb_clean_lines);

  auto st_addr_line = calc_line(st_addr);
  printf("Store_buffer::add_st::add_st in scb for st_addr %ld and st_addr_line  %ld\n", st_addr,st_addr_line);
  auto it           = scb_lines_map.find(st_addr_line);
  //scb does not has the addr : new entry in map 'scb_map'
  if (it == scb_lines_map.end()) {
    printf("Store_buffer::add_st::In scb No entry found for store st_addr %ld and st_addr_line %ld\n", st_addr, st_addr_line);
    if ((static_cast<int>(scb_lines_map.size()) + scb_clean_lines) >= scb_size) {
      remove_clean();
    }

    Store_buffer_line line;

    line.init(line_size, st_addr_line);
    line.add_st(calc_offset(st_addr));
    I(line.state == Store_buffer_line::State::Uncoherent);

    printf("Store_buffer::add_st::Inserting new entry for store st_addr  %ld\n", st_addr_line);
    scb_lines_map.insert({st_addr_line, line});
    line.set_waiting_wb();

    CallbackBase *cb = ownership_doneCB::create(this, st_addr);
    if (dl1 && !dinst->isTransient()) {
      printf("Store_buffer::add_st::SCB new entry for the store addr +Sending the store to cache for st_addr %ld and st_addr_line  %ld\n", 
          st_addr,st_addr_line);
      MemRequest::sendReqWrite(dl1, dinst->has_stats(), st_addr, dinst->getPC(), cb);
    } else {
      cb->schedule(1);
    }

    // fmt::print("scb::add_st {} no pending st for addr 0x{}\n", dinst->getID(), st_addr);

    return;
  }
  //scb already have the address beforehand in map 'scb_map': duplicate entry
  printf("Store_buffer::add_st::SCB already have this addr for store st_addr %ld and st_addr_line %ld\n",st_addr, calc_line(st_addr));
  it->second.add_st(calc_offset(st_addr));
  if (it->second.is_waiting_wb()) {
     printf("Store_buffer::add_st::WaitingPending for writeback to SCB from cache + already have this addr for store st_addr %ld and st_addr_line %ld\n",
         st_addr, calc_line(st_addr));
    // fmt::print("scb::add_st {} with pending WB for addr 0x{}\n", dinst->getID(), st_addr);
    return;  // DONE
  }
  // FIX
  auto it_found = scb_lines_map.find(st_addr_line);
  if (it_found != scb_lines_map.end()) {
    // FIXEND

    it->second.set_waiting_wb();
  }
  printf("Store_buffer::add_st:: Before scbcleanlines-- is  %d after sending ownership done for add %ld\n", scb_clean_lines, dinst->getID());
  --scb_clean_lines;
  printf("Store_buffer::add_st::After scbcleanlines is  %d after sending ownership done for add %ld\n", scb_clean_lines, dinst->getID());
  if (dl1) {
    auto *cb = ownership_doneCB::create(this, st_addr);
    printf("Store_buffer::add_st::SCB already have this addr+Sending the store to cache for store st_addr %ld and st_addr_line %ld\n", 
        st_addr, calc_line(st_addr));
    MemRequest::sendReqWrite(dl1, dinst->has_stats(), st_addr, dinst->getPC(), cb);
  } else {
    ownership_doneCB::schedule(1, this, st_addr);
  }

  // fmt::print("scb::add_st {} clean WB for addr 0x{}\n", dinst->getID(), st_addr);
}

void Store_buffer::ownership_done(Addr_t st_addr) {
  auto st_addr_line = calc_line(st_addr);

  printf("Store_buffer::ownership_done Entering in scb for st_addr  %ld and st_addr_line %ld\n", st_addr, st_addr_line);
  auto it = scb_lines_map.find(st_addr_line);
  I(it != scb_lines_map.end());
  I(it->second.is_waiting_wb());

  printf("Store_buffer::ownership_done::Before scbcleanlines++ is  %d  ownership done for add %ld\n", scb_clean_lines, st_addr);
  ++scb_clean_lines;
  printf("Store_buffer::ownership_done::After scbcleanlines is  %d  ownership done for add %ld\n", scb_clean_lines,st_addr);
  it->second.set_clean();
  printf("Store_buffer::ownership_done Leaving from scb for st_addr  %ld\n", st_addr);
}

bool Store_buffer::is_ld_forward(Addr_t addr) const {
  const auto it = scb_lines_map.find(calc_line(addr));
  if (it == scb_lines_map.end()) {
    return false;
  }

  return it->second.is_ld_forward(calc_offset(addr));
}

bool Store_buffer::find(Dinst *dinst) {
    auto st_addr = dinst->getAddr();
    auto st_addr_line = calc_line(st_addr);
    auto it           = scb_lines_map.find(st_addr_line);
    //end is the iterator position: found if(it!=end position)
    if (it == scb_lines_map.end()) {
    return false;
    }
  return true;
}
