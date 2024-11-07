// See LICENSE for details.

#include "port.hpp"

PortGeneric::PortGeneric(const std::string &name) : avgTime(name) { nUsers = 0; }

PortGeneric::~PortGeneric() {
  if (nUsers) {
    fmt::print("ERROR: Not enough destroys for FUGenUnit\n");
  }
}

PortGeneric *PortGeneric::create(const std::string &unitName, NumUnits_t nUnits) {
  // Search for the configuration with the best performance. In theory
  // everything can be solved with PortNPipe, but it is the slowest.
  // Sorry, but I'm a performance freak!

  auto name = fmt::format("{}_occ", unitName);

  PortGeneric *gen;

  if (nUnits == 0) {
    gen = new PortUnlimited(name);
  } else {
    if (nUnits == 1) {
      gen = new PortFullyPipe(name);
    } else {
      gen = new PortFullyNPipe(name, nUnits);
    }
  }

  return gen;
}

void PortGeneric::destroy() { delete this; }

PortUnlimited::PortUnlimited(const std::string &name) : PortGeneric(name) {}

Time_t PortUnlimited::nextSlot(bool en) {
  avgTime.sample(0, en);  // Just to keep usage statistics
  return globalClock;
}

bool PortUnlimited::is_busy_for(TimeDelta_t clk) const {
  (void)clk;
  return false;
}

PortFullyPipe::PortFullyPipe(const std::string &name) : PortGeneric(name) { lTime = globalClock; }

Time_t PortFullyPipe::nextSlot(bool en) {
  if (lTime < globalClock) {
    lTime = globalClock;
  }

  avgTime.sample(lTime - globalClock, en);
  return lTime++;
}

bool PortFullyPipe::is_busy_for(TimeDelta_t clk) const { return lTime - clk >= globalClock; }

PortFullyNPipe::PortFullyNPipe(const std::string &name, NumUnits_t nFU) : PortGeneric(name), nUnitsMinusOne(nFU - 1) {
  I(nFU > 0);  // For unlimited resources use the FUUnlimited

  lTime = globalClock;

  freeUnits = nFU;
}

Time_t PortFullyNPipe::nextSlot(bool en) {
  if (lTime < globalClock) {
    lTime     = globalClock;
    freeUnits = nUnitsMinusOne;
  } else if (freeUnits > 0) {
    freeUnits--;
  } else {
    lTime++;
    freeUnits = nUnitsMinusOne;
  }

  avgTime.sample(lTime - globalClock, en);
  return lTime;
}

bool PortFullyNPipe::is_busy_for(TimeDelta_t clk) const { return lTime - clk >= globalClock; }
