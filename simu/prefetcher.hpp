// See LICENSE for details.

#pragma once

#include "addresspredictor.hpp"
#include "cachecore.hpp"
#include "callback.hpp"
#include "port.hpp"
#include "stats.hpp"

#pragma once
#include <memory>  

class MemObj;

class Prefetcher {
private:
  MemObj* DL1;  // L1 cache
  std::shared_ptr<Store_buffer> scb;  //NEW -- shared ownership matches gprocessor pattern

  Stats_avg  avgPrefetchNum;
  Stats_avg  avgPrefetchConf;
  Stats_hist histPrefetchDelta;

  std::unique_ptr<AddressPredictor> apred;

  int32_t degree;
  int32_t distance;

  int32_t  curPrefetch;
  uint32_t lineSizeBits;

  Addr_t pref_sign;

  bool         pending_prefetch;
  Addr_t       pending_preq_pc;
  uint16_t     pending_preq_conf;
  bool         pending_statsFlag;
  FetchEngine* pending_chain_fetch;
  bool         inducing_spec_load;  //NEW -- was the load that induce this prefetch a spec load?
  
  uint16_t conf = 0;
  Addr_t   pending_preq_addr;
  Addr_t   inducing_spec_load_addr;  //NEW -- snapshot of the spec load's own address, stable across
                                         // the whole prefetch chain. 

  void nextPrefetch();

  StaticCallbackMember0<Prefetcher, &Prefetcher::nextPrefetch> nextPrefetchCB;

public:
  Prefetcher(MemObj* l1, int cpud_id, std::shared_ptr<Store_buffer> scb);
  ~Prefetcher() {}

  void exe(Dinst* dinst);
  void ret(Dinst* dinst);
};
