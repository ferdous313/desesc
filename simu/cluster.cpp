// See LICENSE for details.

#include "cluster.hpp"

#include "config.hpp"
#include "estl.hpp"
#include "gmemory_system.hpp"
#include "gprocessor.hpp"
#include "port.hpp"
#include "resource.hpp"
#include "store_buffer.hpp"

// Begin: Fields used during constructions

Cluster::~Cluster() {
  // Nothing to do
}

Cluster::Cluster(const std::string &clusterName, uint32_t pos, uint32_t _cpuid)
    : window(_cpuid, cluster_id_counter, clusterName, pos)
    , MaxWinSize(Config::get_integer(clusterName, "win_size", 1, 32768))
    , windowSize(Config::get_integer(clusterName, "win_size"))
    , winNotUsed(fmt::format("P({})_{}{}_winNotUsed", _cpuid, clusterName, pos))
    , rdRegPool(fmt::format("P({})_{}{}_rdRegPool", _cpuid, clusterName, pos))
    , wrRegPool(fmt::format("P({})_{}{}_wrRegPool", _cpuid, clusterName, pos))
    , cpuid(_cpuid) {
  name       = fmt::format("{}{}", clusterName, pos);
  cluster_id = cluster_id_counter++;

  nready = 0;
}

std::shared_ptr<Resource> Cluster::buildUnit(const std::string &clusterName, uint32_t pos, std::shared_ptr<Gmemory_system> ms,
                                             std::shared_ptr<Cluster> cluster, Opcode op, GProcessor *gproc) {
  auto unitType = fmt::format("{}", op);

  if (!Config::has_entry(clusterName, unitType)) {
    return nullptr;
  }
  auto sUnitName = Config::get_string(clusterName, unitType);

  int id      = cpuid;
  int smt     = Config::get_integer("soc", "core", id, "smt", 1, 1024);
  int smt_ctx = id - (id % smt);

  TimeDelta_t  lat = Config::get_integer(sUnitName, "lat", 0, 1024);
  PortGeneric *gen;

  auto unitName = fmt::format("P({})_{}{}_{}", smt_ctx, clusterName, pos, sUnitName);
  auto it       = unitMap.find(unitName);
  if (it != unitMap.end()) {
    gen = it->second.gen;
  } else {
    UnitEntry e;
    e.num = Config::get_integer(sUnitName, "num", 0, 1024);
    e.occ = Config::get_integer(sUnitName, "occ", 0, 1024);

    e.gen = PortGeneric::create(unitName, e.num, e.occ);
    gen   = e.gen;

    unitMap[unitName] = e;
  }

  int unitID = 0;
  switch (op) {
    case Opcode::iBALU_LBRANCH:
    case Opcode::iBALU_LJUMP:
    case Opcode::iBALU_LCALL:
    case Opcode::iBALU_RBRANCH:
    case Opcode::iBALU_RJUMP:
    case Opcode::iBALU_RCALL:
    case Opcode::iBALU_RET: unitID = 1; break;
    case Opcode::iLALU_LD: unitID = 2; break;
    case Opcode::iSALU_LL:
    case Opcode::iSALU_SC:
    case Opcode::iSALU_ST:
    case Opcode::iSALU_ADDR: unitID = 3; break;
    default: unitID = 0; break;
  }

  auto resourceName = fmt::format("P({})_{}{}_{}_{}", smt_ctx, clusterName, pos, unitID, sUnitName);
  auto it2          = resourceMap.find(resourceName);

  std::shared_ptr<Resource> r;

  if (it2 != resourceMap.end()) {
    r = it2->second;
  } else {
    switch (op) {
      case Opcode::iOpInvalid:
      case Opcode::iRALU: r = std::make_shared<FURALU>(op, cluster, gen, lat, cpuid); break;
      case Opcode::iAALU:
      case Opcode::iCALU_FPMULT:
      case Opcode::iCALU_FPDIV:
      case Opcode::iCALU_FPALU:
      case Opcode::iCALU_MULT:
      case Opcode::iCALU_DIV: r = std::make_shared<FUGeneric>(op, cluster, gen, lat, cpuid); break;
      case Opcode::iBALU_LBRANCH:
      case Opcode::iBALU_LJUMP:
      case Opcode::iBALU_LCALL:
      case Opcode::iBALU_RBRANCH:
      case Opcode::iBALU_RJUMP:
      case Opcode::iBALU_RCALL:
      case Opcode::iBALU_RET: {
        auto max_branches = Config::get_integer("soc", "core", cpuid, "max_branches");
        if (max_branches == 0) {
          max_branches = INT_MAX;
        }
        bool drain_on_miss = Config::get_bool("soc", "core", cpuid, "drain_on_miss");

        r = std::make_shared<FUBranch>(op, cluster, gen, lat, cpuid, max_branches, drain_on_miss);
      } break;
      case Opcode::iLALU_LD: {
        TimeDelta_t st_fwd_delay = Config::get_integer("soc", "core", cpuid, "st_fwd_delay");
        int32_t     ldq_size     = Config::get_integer("soc", "core", cpuid, "ldq_size", 0, 256 * 1024);
        if (ldq_size == 0) {
          ldq_size = 256 * 1024;
        }

        r = std::make_shared<FULoad>(op,
                                     cluster,
                                     gen,
                                     gproc->getLSQ(),
                                     gproc->ref_SS(),
                                     gproc->ref_prefetcher(),
                                     gproc->ref_SCB(),
                                     st_fwd_delay,
                                     lat,
                                     ms,
                                     ldq_size,
                                     cpuid,
                                     "specld");
      } break;
      case Opcode::iSALU_LL:
      case Opcode::iSALU_SC:
      case Opcode::iSALU_ST:
      case Opcode::iSALU_ADDR: {
        int32_t stq_size = Config::get_integer("soc", "core", cpuid, "stq_size", 0, 256 * 1024);
        if (stq_size == 0) {
          stq_size = 256 * 1024;
        }

        r = std::make_shared<FUStore>(op,
                                      cluster,
                                      gen,
                                      gproc->getLSQ(),
                                      gproc->ref_SS(),
                                      gproc->ref_prefetcher(),
                                      gproc->ref_SCB(),
                                      lat,
                                      ms,
                                      stq_size,
                                      cpuid,
                                      fmt::format("{}", op));
      } break;
      default: Config::add_error(fmt::format("unknown unit type {}", op)); I(0);
    }
    I(r);
    resourceMap[resourceName] = r;
  }

  return r;
}

std::pair<std::shared_ptr<Cluster>, Opcode_array<std::shared_ptr<Resource>>> Cluster::create(const std::string &clusterName,
                                                                                             uint32_t           pos,
                                                                                             std::shared_ptr<Gmemory_system> ms,
                                                                                             uint32_t cpuid, GProcessor *gproc) {
  // Constraints
  Opcode_array<std::shared_ptr<Resource>> res;

  auto recycleAt = Config::get_string(clusterName, "recycle_at", {"executing", "executed", "retired"});

  int smt     = Config::get_integer("soc", "core", cpuid, "smt");
  int smt_ctx = cpuid - (cpuid % smt);

  auto cName = fmt::format("cluster({})_{}{}", smt_ctx, clusterName, pos);

  std::shared_ptr<Cluster> cluster;

  const auto it = clusterMap.find(cName);
  if (it != clusterMap.end()) {
    return it->second;
  }

  if (recycleAt == "retire") {
    cluster = std::make_shared<RetiredCluster>(clusterName, pos, cpuid);
  } else if (recycleAt == "executing") {
    cluster = std::make_shared<ExecutingCluster>(clusterName, pos, cpuid);
  } else {
    I(recycleAt == "executed");
    cluster = std::make_shared<ExecutedCluster>(clusterName, pos, cpuid);
  }

  cluster->nRegs     = Config::get_integer(clusterName, "num_regs", 2, 262144);
  cluster->regPool   = cluster->nRegs;
  cluster->lateAlloc = Config::get_bool(clusterName, "late_alloc");

  for (const auto t : Opcodes) {
    auto r = cluster->buildUnit(clusterName, pos, ms, cluster, t, gproc);
    res[t] = r;
  }

  clusterMap[cName] = std::pair(cluster, res);

  return std::pair(cluster, res);
}

void Cluster::select(Dinst *dinst) {
  I(nready >= 0);
  nready++;
  window.select(dinst);
}

StallCause Cluster::canIssue(Dinst *dinst) const {
  if (regPool <= 0) {
    return SmallREGStall;
  }

  if (windowSize <= 0) {
    return SmallWinStall;
  }

  StallCause sc = window.canIssue(dinst);
  if (sc != NoStall) {
    return sc;
  }

  return dinst->getClusterResource()->canIssue(dinst);
}

void Cluster::add_inst(Dinst *dinst) {
  rdRegPool.add(2, dinst->has_stats());  // 2 reads

  if (!lateAlloc && dinst->getInst()->hasDstRegister()) {
    wrRegPool.inc(dinst->has_stats());
    I(regPool > 0);
    regPool--;
  }
  // dinst->dump("add");

  newEntry();

  window.add_inst(dinst);
}

//************ Executing Cluster Class

void ExecutingCluster::executing(Dinst *dinst) {
  nready--;

  if (lateAlloc && dinst->getInst()->hasDstRegister()) {
    wrRegPool.inc(dinst->has_stats());
    I(regPool > 0);
    regPool--;
  }
  dinst->getGProc()->executing(dinst);

  delEntry();
}
void ExecutingCluster::flushed(Dinst *dinst) {
  nready--;

  if (lateAlloc && dinst->getInst()->hasDstRegister()) {
    wrRegPool.inc(dinst->has_stats());
    I(regPool > 0);
    regPool--;
  }
  dinst->getGProc()->flushed(dinst);

  // delEntry();
}

void ExecutingCluster::executed(Dinst *dinst) {
  window.executed(dinst);
  dinst->getGProc()->executed(dinst);
}

bool ExecutingCluster::retire(Dinst *dinst, bool reply) {
  bool done = dinst->getClusterResource()->retire(dinst, reply);

  if (!done) {
    return false;
  }

  bool hasDest = (dinst->getInst()->hasDstRegister());

  if (hasDest) {
    regPool++;
    I(regPool <= nRegs);
  }

  winNotUsed.sample(windowSize, dinst->has_stats());

  return true;
}

//************ Executed Cluster Class

void ExecutedCluster::executing(Dinst *dinst) {
  nready--;

  if (lateAlloc && dinst->getInst()->hasDstRegister()) {
    wrRegPool.inc(dinst->has_stats());
    I(regPool > 0);
    regPool--;
  }
  dinst->getGProc()->executing(dinst);
}

void ExecutedCluster::executed(Dinst *dinst) {
  window.executed(dinst);
  dinst->getGProc()->executed(dinst);
  if (!dinst->isTransient()) {
    I(!dinst->hasPending());
  }
  // if(
  dinst->mark_del_entry();
  delEntry();
}
void ExecutedCluster::flushed(Dinst *dinst) {
  window.executed_flushed(dinst);
  dinst->getGProc()->flushed(dinst);
  // I(!dinst->hasPending());
  // if(Executed cluster ) then delentry else return//TODO
  // delEntry();
}

bool ExecutedCluster::retire(Dinst *dinst, bool reply) {
  if (dinst->is_del_entry()) {
    // delEntry();
    dinst->unmark_del_entry();
  }
  // always return true from resource::retire()
  bool done = dinst->getClusterResource()->retire(dinst, reply);
  if (!done) {
    return false;
  }
  I(regPool <= nRegs);

  bool hasDest = (dinst->getInst()->hasDstRegister());
  if (hasDest) {
    regPool++;
    // if(!dinst->is_present_rrob()){
    // regPool++;
    //}
    I(regPool <= nRegs);
  }
  // dinst->dump("ret");

  // delEntry();
  winNotUsed.sample(windowSize, dinst->has_stats());

  return true;
}

//************ RetiredCluster Class

void RetiredCluster::executing(Dinst *dinst) {
  nready--;

  if (lateAlloc && dinst->getInst()->hasDstRegister()) {
    wrRegPool.inc(dinst->has_stats());
    regPool--;
  }
  dinst->getGProc()->executing(dinst);
}

void RetiredCluster::executed(Dinst *dinst) {
  window.executed(dinst);
  dinst->getGProc()->executed(dinst);
}

bool RetiredCluster::retire(Dinst *dinst, bool reply) {
  bool done = dinst->getClusterResource()->retire(dinst, reply);
  if (!done) {
    return false;
  }

  I(dinst->getGProc()->get_hid() == dinst->getFlowId());

  bool hasDest = (dinst->getInst()->hasDstRegister());

  if (hasDest) {
    regPool++;
    I(regPool <= nRegs);
  }

  winNotUsed.sample(windowSize, dinst->has_stats());

  delEntry();

  return true;
}
void RetiredCluster::flushed(Dinst *dinst) {
  bool done = dinst->getClusterResource()->flushed(dinst);
  if (!done) {
    // return false;
  }

  I(dinst->getGProc()->get_hid() == dinst->getFlowId());

  bool hasDest = (dinst->getInst()->hasDstRegister());

  if (hasDest) {
    regPool++;
    I(regPool <= nRegs);
  }

  winNotUsed.sample(windowSize, dinst->has_stats());
}
