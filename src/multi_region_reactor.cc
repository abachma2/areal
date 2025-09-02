#include "multi_region_reactor.h"

namespace areal {

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
MultiRegionReactor::MultiRegionReactor(cyclus::Context* ctx) : cyclus::Facility(ctx) {}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
std::string MultiRegionReactor::str() {
  return Facility::str();
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void MultiRegionReactor::Tick() {}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void MultiRegionReactor::Tock() {}

// WARNING! Do not change the following this function!!! This enables your
// archetype to be dynamically loaded and any alterations will cause your
// archetype to fail.
extern "C" cyclus::Agent* ConstructMultiRegionReactor(cyclus::Context* ctx) {
  return new MultiRegionReactor(ctx);
}

}  // namespace areal
