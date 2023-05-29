// Contributed by Jose Renau
//                Basilio Fraguela
//                James Tuck
//                Milos Prvulovic
//                Luis Ceze
//
// The ESESC/BSD License
//
// Copyright (c) 2005-2013, Regents of the University of California and
// the ESESC Project.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
//   - Redistributions of source code must retain the above copyright notice,
//   this list of conditions and the following disclaimer.
//
//   - Redistributions in binary form must reproduce the above copyright
//   notice, this list of conditions and the following disclaimer in the
//   documentation and/or other materials provided with the distribution.
//
//   - Neither the name of the University of California, Santa Cruz nor the
//   names of its contributors may be used to endorse or promote products
//   derived from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.

#ifdef ENABLE_CUDA

#include "gpusmprocessor.hpp"

#include "clustermanager.hpp"
#include "config.hpp"
#include "fetchengine.hpp"
#include "gmemory_system.hpp"

GPUSMProcessor::GPUSMProcessor(Gmemory_system *gm, CPU_t i)
    : GProcessor(gm, i, 1), IFID(i, this, gm), pipeQ(i), lsq(i), clusterManager(gm, this) { /*{{{*/

  numSP             = Config::get_power2("soc", "core", i, "sp_per_sm");
  uint32_t maxwarps = Config::get_integer("soc", "core", i, "max_warps_sm");

  inst_perpe_percyc = new bool[numSP];
  for (uint32_t spcount = 0; spcount < numSP; spcount++) {
    inst_perpe_percyc[spcount] = false;
  }

  spaceInInstQueue = InstQueueSize;

  uint64_t ratsize = LREG_MAX * numSP * maxwarps;  // 16777216; //2^24
  // uint64_t ratsize =4294967296; //2^32
  RAT = new Dinst *[ratsize];
  bzero(RAT, sizeof(Dinst *) * ratsize);
} /*}}}*/

GPUSMProcessor::~GPUSMProcessor() { /*{{{*/
  delete RAT;
  // Nothing to do
} /*}}}*/

bool GPUSMProcessor::advance_clock_drain() {
  bool abort = decode_stage();
  if (abort || !busy) {
    return busy;
  }

  if (!pipeQ.instQueue.empty()) {
    for (uint32_t i = 0; i < numSP; i++) {
      inst_perpe_percyc[i] = false;
    }
    uint32_t n_insn = issue();
    spaceInInstQueue += n_insn;
  } else if (ROB.empty() && rROB.empty()) {
    busy = pipeQ.pipeLine.hasOutstandingItems();
  }

  retire();

  return busy;
}

bool GPUSMProcessor::advance_clock() {
  if (is_power_down()) {
    return false;
  }

  fetch();

  return advance_clock_drain();
}

StallCause GPUSMProcessor::add_inst(Dinst *dinst) {
  const Instruction *inst = dinst->getInst();

  if (((RAT[inst->getSrc1()] != 0) && (inst->getSrc1() != LREG_NoDependence) && (inst->getSrc1() != LREG_InvalidOutput))
      || ((RAT[inst->getSrc2()] != 0) && (inst->getSrc2() != LREG_NoDependence) && (inst->getSrc2() != LREG_InvalidOutput))
      || ((RAT[inst->getDst1()] != 0) && (inst->getDst1() != LREG_InvalidOutput))
      || ((RAT[inst->getDst2()] != 0) && (inst->getDst2() != LREG_InvalidOutput))) {
#if 0
    //Useful for debug
    if (hid == 1 ){
      MSG("\n---------- (@%lld) ---------------",(long long int)globalClock);
      string str ="";
      str.append("\nCONFLICT->");
      if (RAT[inst->getSrc1()] != 0){
        str.append("src1, ");
        MSG(" SRC1 = %d, RAT[entry] = %d",inst->getSrc1(), RAT[inst->getSrc1()] );
        RAT[inst->getSrc1()]->dump("\nSRC1 in use by:");
      }

      if (RAT[inst->getSrc2()] != 0){
        str.append("src2, ");
        RAT[inst->getSrc2()]->dump("\nSRC2 in use by:");
      }

      if ((RAT[inst->getDst1()] != 0) && (inst->getDst2() != LREG_InvalidOutput)){
        str.append("dst1, ");
        RAT[inst->getDst1()]->dump("\nDST1 in use by:");
      }

      if ((RAT[inst->getDst2()] != 0) && (inst->getDst2() != LREG_InvalidOutput)){
        str.append("dst2, ");
        RAT[inst->getDst2()]->dump("\nDST2 in use by:");
      }

      dinst->dump(str.c_str());

    }
#endif
    return SmallWinStall;
  }

  if ((ROB.size() + rROB.size()) >= (MaxROBSize - 1)) {
    return SmallROBStall;
  }

  // FIXME: if nInstPECounter is >0 for this cycle, do a DivertStall
#if 0
  if (inst_perpe_percyc[dinst->getPE()] == true){
    //MSG("d-%d",dinst->getPE());
    return DivergeStall;
  }
#endif
  Cluster *cluster = dinst->getCluster();
  if (!cluster) {
    Resource *res = clusterManager.getResource(dinst);
    cluster       = res->getCluster();
    dinst->setCluster(cluster, res);
  }

  StallCause sc = cluster->canIssue(dinst);
  if (sc != NoStall) {
    return sc;
  }

  // BEGIN INSERTION (note that cluster already inserted in the window)
  // dinst->dump("");

  inst_perpe_percyc[dinst->getPE()] = true;
  // MSG("Setting Dinst %lld PE-%d, GlobalClock = %lld ",dinst->getID(), dinst->getPE(),globalClock);

  nInst[inst->getOpcode()]->inc(dinst->has_stats());

  ROB.push(dinst);

#if 0

  dinst->setRAT1Entry(&RAT[inst->getDst1()]);
  dinst->setRAT2Entry(&RAT[inst->getDst2()]);
  RAT[inst->getDst1()] = dinst;
  RAT[inst->getDst2()] = dinst;

  I(dinst->getCluster());
  dinst->getCluster()->add_inst(dinst);
#else
  if (!dinst->isSrc2Ready()) {
    // It already has a src2 dep. It means that it is solved at
    // retirement (Memory consistency. coherence issues)
    if (RAT[inst->getSrc1()]) {
      RAT[inst->getSrc1()]->addSrc1(dinst);
    }
  } else {
    if (RAT[inst->getSrc1()]) {
      RAT[inst->getSrc1()]->addSrc1(dinst);
    }

    if (RAT[inst->getSrc2()]) {
      RAT[inst->getSrc2()]->addSrc2(dinst);
    }
  }

  dinst->setRAT1Entry(&RAT[inst->getDst1()]);
  dinst->setRAT2Entry(&RAT[inst->getDst2()]);

  dinst->getCluster()->add_inst(dinst);

  RAT[inst->getDst1()] = dinst;
  RAT[inst->getDst2()] = dinst;

  I(dinst->getCluster());
#endif

  return NoStall;
} /*}}}*/

void GPUSMProcessor::retire() { /*{{{*/

  // Pass all the ready instructions to the rrob
  bool stats = false;
  while (!ROB.empty()) {
    Dinst *dinst = ROB.top();
    stats        = dinst->has_stats();

    if (!dinst->isExecuted()) {
      break;
    }

    bool done = dinst->getClusterResource()->preretire(dinst, false);
    if (!done) {
      break;
    }

    rROB.push(dinst);
    ROB.pop();

    nCommitted.inc(dinst->has_stats());
  }

  robUsed.sample(ROB.size(), stats);
  rrobUsed.sample(rROB.size(), stats);

  for (uint16_t i = 0; i < RetireWidth && !rROB.empty(); i++) {
    Dinst *dinst = rROB.top();

    if (!dinst->isExecuted()) {
      break;
    }

    I(dinst->getCluster());

    bool done = dinst->getCluster()->retire(dinst, false);
    if (!done) {
      // dinst->getInst()->dump("not ret");
      return;
    }

    // nCommitted.inc();
    I(dinst->getFlowId() == hid);

    dinst->destroy(eint);
    rROB.pop();
  }

} /*}}}*/

void GPUSMProcessor::replay(Dinst *dinst) { /*{{{*/

  MSG("GPU_SM cores(which are essentially inorder) do not support replays. Set MemoryReplay = false at the confguration");

} /*}}}*/
#endif
