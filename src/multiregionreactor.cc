#include "multiregionreactor.h"

using cyclus::Material;
using cyclus::toolkit::MatVec;
using cyclus::KeyError;
using cyclus::ValueError;
using cyclus::Request;

namespace areal {

MultiRegionReactor::MultiRegionReactor(cyclus::Context* ctx)
    : cyclus::Facility(ctx),
      n_assem_batch(0),
      n_assem_spent(0),
      n_assem_fresh(0),
      cycle_time(0),
      refuel_time(0),
      cycle_step(0),
      power_cap(0),
      power_name("power"),
      discharged(false),
      keep_packaging(true) {}


#pragma cyclus def clone areal::MultiRegionReactor

#pragma cyclus def schema areal::MultiRegionReactor

#pragma cyclus def annotations areal::MultiRegionReactor

#pragma cyclus def infiletodb areal::MultiRegionReactor

#pragma cyclus def snapshot areal::MultiRegionReactor

#pragma cyclus def snapshotinv areal::MultiRegionReactor

#pragma cyclus def initinv areal::MultiRegionReactor

void MultiRegionReactor::InitFrom(MultiRegionReactor* m) {
  #pragma cyclus impl initfromcopy areal::MultiRegionReactor
  cyclus::toolkit::CommodityProducer::Copy(m);
}

void MultiRegionReactor::InitFrom(cyclus::QueryableBackend* b) {
  #pragma cyclus impl initfromdb areal::MultiRegionReactor

  namespace tk = cyclus::toolkit;
  tk::CommodityProducer::Add(tk::Commodity(power_name),
                             tk::CommodInfo(power_cap, power_cap));
}

void MultiRegionReactor::EnterNotify() {
  cyclus::Facility::EnterNotify();

  // Set keep packaging parameter in all ResBufs
  fresh.keep_packaging(keep_packaging);
  core.keep_packaging(keep_packaging);
  spent.keep_packaging(keep_packaging);

  InitializePosition();
}

bool MultiRegionReactor::CheckDecommissionCondition() {
  return core.count() == 0 && spent.count() == 0;
}

void MultiRegionReactor::Tick() {
  // The following code must go in the Tick so they fire on the time step
  // following the cycle_step update - allowing for the all reactor events to
  // occur and be recorded on the "beginning" of a time step.  Another reason
  // they
  // can't go at the beginnin of the Tock is so that resource exchange has a
  // chance to occur after the discharge on this same time step.

  if (retired()) {
    Record("RETIRED", "");

    if (context()->time() == exit_time() + 1) { // only need to transmute once
      if (decom_transmute_all == true) {
        Transmute(ceil(static_cast<double>(n_assem_core)));
      }
      else {
        Transmute(ceil(static_cast<double>(n_assem_core) / 2.0));
      }
    }
    while (core.count() > 0) {
      if (!Discharge()) {
        break;
      }
    }
    // in case a cycle lands exactly on our last time step, we will need to
    // burn a batch from fresh inventory on this time step.  When retired,
    // this batch also needs to be discharged to spent fuel inventory.
    while (fresh.count() > 0 && spent.space() >= assem_size) {
      spent.Push(fresh.Pop());
    }
    if(CheckDecommissionCondition()) {
      context()->SchedDecom(this);    
    }
    return;
  }

  if (cycle_step == cycle_time) {
    Transmute();
    Record("CYCLE_END", "");
  }

  if (cycle_step >= cycle_time && !discharged) {
    discharged = Discharge();
  }
  if (cycle_step >= cycle_time) {
    Load();
  }

}

std::set<cyclus::RequestPortfolio<Material>::Ptr> MultiRegionReactor::GetMatlRequests() {
  using cyclus::RequestPortfolio;

  std::set<RequestPortfolio<Material>::Ptr> ports;
  Material::Ptr m;
  if (retired()){
    return ports;
  }

  // second min expression reduces assembles to amount needed until
  // retirement if it is near.
  for (int region = 0; region < n_regions; region++){ 
    // need to figure out a way to determine the number of each commodity type in core
    int n_assem_order = n_assem_region[region] - core.count() + n_assem_fresh - fresh.count();

    if (exit_time() != -1) {
      // the +1 accounts for the fact that the reactor is alive and gets to
      // operate during its exit_time time step.
      int t_left = exit_time() - context()->time() + 1;
      int t_left_cycle = cycle_time + refuel_time - cycle_step;
      double n_cycles_left = static_cast<double>(t_left - t_left_cycle) /
                          static_cast<double>(cycle_time + refuel_time);
      n_cycles_left = ceil(n_cycles_left);
      int n_need = std::max(0.0, n_cycles_left * n_assem_batch - n_assem_fresh + n_assem_core - core.count());
      n_assem_order = std::min(n_assem_order, n_need);
    }

    if (n_assem_order == 0) {
      // only want to return empty ports if all regions are full
      return ports;
    } 

    for (int i = 0; i < n_assem_order; i++) {
      RequestPortfolio<Material>::Ptr port(new RequestPortfolio<Material>());
    
      std::string commod = fuel_incommods[region];
    
      cyclus::Composition::Ptr recipe = context()->GetRecipe(fuel_inrecipes[region]);
      m = Material::CreateUntracked(assem_size, recipe);
          

      cyclus::toolkit::RecordTimeSeries<double>("demand"+fuel_incommods[region], this,
                                            assem_size) ;
      // assume all requests have a preference of 1 
      port->AddRequest(m, this, commod, 1.0, true);
      ports.insert(port);
    }
  }

  return ports;
}

void MultiRegionReactor::GetMatlTrades(
    const std::vector<cyclus::Trade<Material> >& trades,
    std::vector<std::pair<cyclus::Trade<Material>, Material::Ptr> >&
        responses) {
  using cyclus::Trade;

  std::map<std::string, MatVec> mats = PopSpent();
  for (int i = 0; i < trades.size(); i++) {
    std::string commod = trades[i].request->commodity();
    Material::Ptr m = mats[commod].back();
    mats[commod].pop_back();
    responses.push_back(std::make_pair(trades[i], m));
    res_indexes.erase(m->obj_id());
  }
  PushSpent(mats);  // return leftovers back to spent buffer
}

void MultiRegionReactor::AcceptMatlTrades(const std::vector<
    std::pair<cyclus::Trade<Material>, Material::Ptr> >& responses) {
  std::vector<std::pair<cyclus::Trade<Material>,
                        Material::Ptr> >::const_iterator trade;

  std::stringstream ss;
  int nload = std::min((int)responses.size(), n_assem_core - core.count());
  if (nload > 0) {
    ss << nload << " assemblies";
    Record("LOAD", ss.str());
  }

  for (trade = responses.begin(); trade != responses.end(); ++trade) {
    std::string commod = trade->first.request->commodity();
    Material::Ptr m = trade->second;
    index_res(m, commod);

    if (core.count() < n_assem_core) {
      core.Push(m);
    } else {
      fresh.Push(m);
    }
  }
}

std::set<cyclus::BidPortfolio<Material>::Ptr> MultiRegionReactor::GetMatlBids(
    cyclus::CommodMap<Material>::type& commod_requests) {
  using cyclus::BidPortfolio;
  std::set<BidPortfolio<Material>::Ptr> ports;

  bool gotmats = false;
  std::map<std::string, MatVec> all_mats;

  if (uniq_outcommods_.empty()) {
    for (int i = 0; i < fuel_outcommods.size(); i++) {
      uniq_outcommods_.insert(fuel_outcommods[i]);
    }
  }

  std::set<std::string>::iterator it;
  for (it = uniq_outcommods_.begin(); it != uniq_outcommods_.end(); ++it) {
    std::string commod = *it;
    std::vector<Request<Material>*>& reqs = commod_requests[commod];
    if (reqs.size() == 0) {
      continue;
    } else if (!gotmats) {
      all_mats = PeekSpent();
    }

    MatVec mats = all_mats[commod];
    if (mats.size() == 0) {
      continue;
    }

    BidPortfolio<Material>::Ptr port(new BidPortfolio<Material>());

    for (int j = 0; j < reqs.size(); j++) {
      Request<Material>* req = reqs[j];
      double tot_bid = 0;
      for (int k = 0; k < mats.size(); k++) {
        Material::Ptr m = mats[k];
        tot_bid += m->quantity();
        port->AddBid(req, m, this, true);
        if (tot_bid >= req->target()->quantity()) {
          break;
        }
      }
    }

    double tot_qty = 0;
    for (int j = 0; j < mats.size(); j++) {
      tot_qty += mats[j]->quantity();
    }

    cyclus::CapacityConstraint<Material> cc(tot_qty);
    port->AddConstraint(cc);
    ports.insert(port);
  }

  return ports;
}

void MultiRegionReactor::Tock() {
  if (retired()) {
    return;
  }
  
  // Check that irradiation and refueling periods are over, that 
  // the core is full and that fuel was successfully discharged in this refueling time.
  // If this is the case, then a new cycle will be initiated.
  if (cycle_step >= cycle_time + refuel_time && core.count() == n_assem_core && discharged == true) {
    discharged = false;
    cycle_step = 0;
  }

  if (cycle_step == 0 && core.count() == n_assem_core) {
    Record("CYCLE_START", "");
  }

  if (cycle_step >= 0 && cycle_step < cycle_time &&
      core.count() == n_assem_core) {
    cyclus::toolkit::RecordTimeSeries<cyclus::toolkit::POWER>(this, power_cap);
    cyclus::toolkit::RecordTimeSeries<double>("supplyPOWER", this, power_cap);
  } else {
    cyclus::toolkit::RecordTimeSeries<cyclus::toolkit::POWER>(this, 0);
    cyclus::toolkit::RecordTimeSeries<double>("supplyPOWER", this, 0);
  }

  // "if" prevents starting cycle after initial deployment until core is full
  // even though cycle_step is its initial zero.
  if (cycle_step > 0 || core.count() == n_assem_core) {
    cycle_step++;
  }
}

void MultiRegionReactor::Transmute() { Transmute(n_assem_batch); }

void MultiRegionReactor::Transmute(int n_assem) {
  MatVec old = core.PopN(std::min(n_assem, core.count()));
  core.Push(old);
  if (core.count() > old.size()) {
    // rotate untransmuted mats back to back of buffer
    core.Push(core.PopN(core.count() - old.size()));
  }

  std::stringstream ss;
  ss << old.size() << " assemblies";
  Record("TRANSMUTE", ss.str());

  for (int i = 0; i < old.size(); i++) {
    old[i]->Transmute(context()->GetRecipe(fuel_outrecipe(old[i])));
  }
}

std::map<std::string, MatVec> MultiRegionReactor::PeekSpent() {
  std::map<std::string, MatVec> mapped;
  MatVec mats = spent.PopN(spent.count());
  spent.Push(mats);
  for (int i = 0; i < mats.size(); i++) {
    std::string commod = fuel_outcommod(mats[i]);
    mapped[commod].push_back(mats[i]);
  }
  return mapped;
}

bool MultiRegionReactor::Discharge() {
  int npop = std::min(n_assem_batch, core.count());
  if (n_assem_spent - spent.count() < npop) {
    Record("DISCHARGE", "failed");
    return false;  // not enough room in spent buffer
  }

  std::stringstream ss;
  ss << npop << " assemblies";
  Record("DISCHARGE", ss.str());
  spent.Push(core.PopN(npop));

  std::map<std::string, MatVec> spent_mats;
  for (int i = 0; i < fuel_outcommods.size(); i++) {
    spent_mats = PeekSpent();
    MatVec mats = spent_mats[fuel_outcommods[i]];
    double tot_spent = 0;
    for (int j = 0; j<mats.size(); j++){
      Material::Ptr m = mats[j];
      tot_spent += m->quantity();
    }
    cyclus::toolkit::RecordTimeSeries<double>("supply"+fuel_outcommods[i], this, tot_spent);
  }

  return true;
}

void MultiRegionReactor::Load() {
  int n = std::min(n_assem_core - core.count(), fresh.count());
  if (n == 0) {
    return;
  }

  std::stringstream ss;
  ss << n << " assemblies";
  Record("LOAD", ss.str());
  core.Push(fresh.PopN(n));
}

std::string MultiRegionReactor::fuel_incommod(Material::Ptr m) {
  int i = res_indexes[m->obj_id()];
  if (i >= fuel_incommods.size()) {
    throw KeyError("areal::MultiRegionReactor - no incommod for material object");
  }
  return fuel_incommods[i];
}

std::string MultiRegionReactor::fuel_outcommod(Material::Ptr m) {
  int i = res_indexes[m->obj_id()];
  if (i >= fuel_outcommods.size()) {
    throw KeyError("areal::MultiRegionReactor - no outcommod for material object");
  }
  return fuel_outcommods[i];
}

std::string MultiRegionReactor::fuel_inrecipe(Material::Ptr m) {
  int i = res_indexes[m->obj_id()];
  if (i >= fuel_inrecipes.size()) {
    throw KeyError("areal::MultiRegionReactor - no inrecipe for material object");
  }
  return fuel_inrecipes[i];
}

std::string MultiRegionReactor::fuel_outrecipe(Material::Ptr m) {
  int i = res_indexes[m->obj_id()];
  if (i >= fuel_outrecipes.size()) {
    throw KeyError("areal::MultiRegionReactor - no outrecipe for material object");
  }
  return fuel_outrecipes[i];
}

double MultiRegionReactor::fuel_pref(Material::Ptr m) {
  int i = res_indexes[m->obj_id()];
  if (i >= fuel_prefs.size()) {
    return 0;
  }
  return fuel_prefs[i];
}

void MultiRegionReactor::index_res(cyclus::Resource::Ptr m, std::string incommod) {
  for (int i = 0; i < fuel_incommods.size(); i++) {
    if (fuel_incommods[i] == incommod) {
      res_indexes[m->obj_id()] = i;
      return;
    }
  }
  throw ValueError(
      "areal::MultiRegionReactor - received unsupported incommod material");
}

std::map<std::string, MatVec> MultiRegionReactor::PopSpent() {
  MatVec mats = spent.PopN(spent.count());
  std::map<std::string, MatVec> mapped;
  for (int i = 0; i < mats.size(); i++) {
    std::string commod = fuel_outcommod(mats[i]);
    mapped[commod].push_back(mats[i]);
  }

  // needed so we trade away oldest assemblies first
  std::map<std::string, MatVec>::iterator it;
  for (it = mapped.begin(); it != mapped.end(); ++it) {
    std::reverse(it->second.begin(), it->second.end());
  }

  return mapped;
}

void MultiRegionReactor::PushSpent(std::map<std::string, MatVec> leftover) {
  std::map<std::string, MatVec>::iterator it;
  for (it = leftover.begin(); it != leftover.end(); ++it) {
    // undo reverse in PopSpent to make sure oldest assemblies come out first
    std::reverse(it->second.begin(), it->second.end());
    spent.Push(it->second);
  }
}

void MultiRegionReactor::Record(std::string name, std::string val) {
  context()
      ->NewDatum("MultiRegionReactorEvents")
      ->AddVal("AgentId", id())
      ->AddVal("Time", context()->time())
      ->AddVal("Event", name)
      ->AddVal("Value", val)
      ->Record();
}

extern "C" cyclus::Agent* ConstructMultiRegionReactor(cyclus::Context* ctx) {
  return new MultiRegionReactor(ctx);
}

}  // namespace areal
