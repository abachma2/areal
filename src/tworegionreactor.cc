#include "tworegionreactor.h"

using cyclus::Material;
using cyclus::toolkit::MatVec;
using cyclus::KeyError;
using cyclus::ValueError;
using cyclus::Request;

namespace areal {

TwoRegionReactor::TwoRegionReactor(cyclus::Context* ctx)
    : cyclus::Facility(ctx),
      cycle_time(0),
      refuel_time(0),
      cycle_step(0),
      power_cap(0),
      power_name("power"),
      discharged1(false),
      discharged2(false),
      keep_packaging(true) {}


#pragma cyclus def clone areal::TwoRegionReactor

#pragma cyclus def schema areal::TwoRegionReactor

#pragma cyclus def annotations areal::TwoRegionReactor

#pragma cyclus def infiletodb areal::TwoRegionReactor

#pragma cyclus def snapshot areal::TwoRegionReactor

#pragma cyclus def snapshotinv areal::TwoRegionReactor

#pragma cyclus def initinv areal::TwoRegionReactor

void TwoRegionReactor::InitFrom(TwoRegionReactor* m) {
  #pragma cyclus impl initfromcopy areal::TwoRegionReactor
  cyclus::toolkit::CommodityProducer::Copy(m);
}

void TwoRegionReactor::InitFrom(cyclus::QueryableBackend* b) {
  #pragma cyclus impl initfromdb areal::TwoRegionReactor

  namespace tk = cyclus::toolkit;
  tk::CommodityProducer::Add(tk::Commodity(power_name),
                             tk::CommodInfo(power_cap, power_cap));
}

void TwoRegionReactor::EnterNotify() {
  cyclus::Facility::EnterNotify();
  // Set keep packaging parameter in all ResBufs
  fresh1.keep_packaging(keep_packaging);
  core1.keep_packaging(keep_packaging);
  spent1.keep_packaging(keep_packaging);
  fresh2.keep_packaging(keep_packaging);
  core2.keep_packaging(keep_packaging);
  spent2.keep_packaging(keep_packaging);

  // Throw error if vectors do not have size 2
  if (fuel_incommods.size() != 2) {
    throw cyclus::ValueError("areal::TwoRegionReactor fuel_incommods "\
                             "does not have 2 entries");
  }
  if (fuel_outcommods.size() != 2) {
    throw cyclus::ValueError("areal::TwoRegionReactor fuel_outcommods "\
                             "does not have 2 entries");
  }
  if (fuel_inrecipes.size() != 2) {
    throw cyclus::ValueError("areal::TwoRegionReactor fuel_inrecipes "\
                             "does not have 2 entries");
  }
  if (fuel_outrecipes.size() != 2) {
    throw cyclus::ValueError("areal::TwoRegionReactor fuel_outrecipes "\
                             "does not have 2 entries");
  }
  if (assem_size.size() != 2) {
    throw cyclus::ValueError("areal::TwoRegionReactor assem_size "\
                             "does not have 2 entries");
  }
  if (n_assem_batch.size() != 2) {
    throw cyclus::ValueError("areal::TwoRegionReactor n_assem_batch "\
                             "does not have 2 entries");
  }
  if (n_assem_region.size() != 2) {
    throw cyclus::ValueError("areal::TwoRegionReactor n_assem_region "\
                             "does not have 2 entries");
  }
  if (n_assem_fresh.size() != 2) {
    throw cyclus::ValueError("areal::TwoRegionReactor n_assem_fresh "\
                             "does not have 2 entries");
  }
  if (n_assem_spent.size() != 2) {
    throw cyclus::ValueError("areal::TwoRegionReactor n_assem_spent "\
                             "does not have 2 entries");
  }
  InitializePosition();
}

bool TwoRegionReactor::CheckDecommissionCondition() {
  return core1.count() == 0 && spent1.count() == 0 &&
  core2.count() == 0 && spent2.count() == 0;
}

void TwoRegionReactor::Tick() {
  // The following code must go in the Tick so they fire on the time step
  // following the cycle_step update - allowing for the all reactor events to
  // occur and be recorded on the "beginning" of a time step.  Another reason
  // they
  // can't go at the beginning of the Tock is so that resource exchange has 
  // chance to occur after the discharge on this same time step.
  if (retired()) {
    Record("RETIRED", "");
    if (context()->time() == exit_time() + 1) { // only need to transmute once
      if (decom_transmute_all == true) {
        /// transmute all the fuel in each region
        for (int i=0; i<2; i++){
          Transmute(n_assem_region[i], i);
        }
      }
      else {
        /// transmute half the fuel in each region
        for (int i=0; i<2; i++){
          Transmute(ceil(static_cast<double>(n_assem_region[i]) / 2.0), i);
        }
      }
    }
    // discharging fuel from each core region. This needs to be in 
    // separate loops because if the regions have different numbers of 
    // assemblies then it might break before both regions are fully 
    // discharged. 
    while (core1.count() > 0){
      if (!Discharge(region1_ID)) {
        break;
      }
    }
    while (core2.count() > 0){
      if (!Discharge(region2_ID)) {
        break;
      }
    }
    // in case a cycle lands exactly on our last time step, we will need to
    // burn a batch from fresh inventory on this time step.  When retired,
    // this batch also needs to be discharged to spent fuel inventory.
    while (fresh1.count() > 0 && spent1.space() >= assem_size[region1_ID]) {
      spent1.Push(fresh1.Pop());
    }
    while (fresh2.count() > 0 && spent2.space() >= assem_size[region2_ID]) {
      spent2.Push(fresh2.Pop());
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

  if (cycle_step >= cycle_time && !discharged1 && !discharged2) {
    discharged1 = Discharge(region1_ID);
    discharged2 = Discharge(region2_ID);
  }
  if (cycle_step >= cycle_time) {
    Load(region1_ID);
    Load(region2_ID);
  }

}

std::set<cyclus::RequestPortfolio<Material>::Ptr> TwoRegionReactor::GetMatlRequests() {
  // DRE phase 1 -- placing requests
  using cyclus::RequestPortfolio;

  std::set<RequestPortfolio<Material>::Ptr> ports;
  Material::Ptr m;

  // second min expression reduces assembles to amount needed until
  // retirement if it is near.
  int n_assem_order1 = n_assem_region[region1_ID] - core1.count() + n_assem_fresh[region1_ID] - fresh1.count();
  int n_assem_order2 = n_assem_region[region2_ID] - core2.count() + n_assem_fresh[region2_ID] - fresh2.count(); 

  if (exit_time() != -1) {
    // the +1 accounts for the fact that the reactor is alive and gets to
    // operate during its exit_time time step.
    int t_left = exit_time() - context()->time() + 1;
    int t_left_cycle = cycle_time + refuel_time - cycle_step;
    double n_cycles_left = static_cast<double>(t_left - t_left_cycle) /
                         static_cast<double>(cycle_time + refuel_time);
    n_cycles_left = ceil(n_cycles_left);

    int n_need1 = std::max(0.0, n_cycles_left * n_assem_batch[region1_ID] - n_assem_fresh[region1_ID] + n_assem_region[region1_ID] - core1.count());

    int n_need2 = std::max(0.0, n_cycles_left * n_assem_batch[region2_ID] - n_assem_fresh[region2_ID] + n_assem_region[region2_ID] - core2.count());

    n_assem_order1 = std::min(n_assem_order1, n_need1);
    n_assem_order2 = std::min(n_assem_order2, n_need2); 
  }
  
  if (n_assem_order1 == 0 && n_assem_order2 == 0) {
    return ports;
  } else if (retired()) {
    return ports;
  }
  
  if (n_assem_order1 > 0){
    // building request portfolio for region 1 and recording demand
    for (int i = 0; i < n_assem_order1; i++) {
      RequestPortfolio<Material>::Ptr port(new RequestPortfolio<Material>());
      std::string commod = fuel_incommods[region1_ID];
      cyclus::Composition::Ptr recipe = context()->GetRecipe(fuel_inrecipes[region1_ID]);
      m = Material::CreateUntracked(assem_size[region1_ID], recipe);

      Request<Material>* r = port->AddRequest(m, this, commod, 1.0, true);
      cyclus::toolkit::RecordTimeSeries<double>("demand"+fuel_incommods[region1_ID], this,
                                            assem_size[region1_ID]) ;

      ports.insert(port);
    }
  }

  if (n_assem_order2 > 0){
    // building request portfolio for region 2 and recording demand
    for (int i = 0; i < n_assem_order2; i++) {
      RequestPortfolio<Material>::Ptr port(new RequestPortfolio<Material>());
      std::string commod = fuel_incommods[region2_ID];
      cyclus::Composition::Ptr recipe = context()->GetRecipe(fuel_inrecipes[region2_ID]);
      m = Material::CreateUntracked(assem_size[region2_ID], recipe);

      Request<Material>* r = port->AddRequest(m, this, commod, 1.0, true);
      cyclus::toolkit::RecordTimeSeries<double>("demand"+fuel_incommods[region2_ID], this,
                                            assem_size[region2_ID]) ;

      ports.insert(port);
    }
  }

  return ports;
}

void TwoRegionReactor::GetMatlTrades(
  // DRE phase 5.1 -- getting materials to trade away
    const std::vector<cyclus::Trade<Material> >& trades,
    std::vector<std::pair<cyclus::Trade<Material>, Material::Ptr> >&
        responses) {
  using cyclus::Trade;

  for (int i = 0; i < 2; i++){
    std::map<std::string, MatVec> mats = PopSpent(i);
    for (int j = 0; j < trades.size(); j++) {
      std::string commod = trades[j].request->commodity();
      if (commod != fuel_outcommods[i]) {
        continue;
      }
      Material::Ptr m = mats[commod].back();
      mats[commod].pop_back();
      responses.push_back(std::make_pair(trades[j], m));
      res_indexes.erase(m->obj_id());
    }
    PushSpent(mats, i);  // return leftovers back to spent buffer
  }
}

void TwoRegionReactor::AcceptMatlTrades(const std::vector<
    std::pair<cyclus::Trade<Material>, Material::Ptr> >& responses) {
  // DRE phase 5.2 -- getting materials from other facilities
  std::vector<std::pair<cyclus::Trade<Material>,
                        Material::Ptr> >::const_iterator trade;

  std::stringstream ss;
  int num_response1 = 0;
  int num_response2 = 0; 
  for (trade = responses.begin(); trade != responses.end(); ++trade){
    std::string commod = trade->first.request->commodity();
    if (commod == fuel_incommods[region1_ID]){
      ++num_response1;
    }
    if (commod == fuel_incommods[region2_ID]){
      ++num_response2;
    }
  }
  int nload1 = std::min(num_response1, n_assem_region[region1_ID] - core1.count());
  int nload2 = std::min(num_response2, n_assem_region[region2_ID] - core2.count());

  if (nload1 > 0) {
    ss << nload1 << " assemblies in Region 1";
    Record("LOAD", ss.str());
  }
  if (nload2 > 0 ) {
    ss << nload2 << "assemblies in Region 2";
    Record("LOAD", ss.str());
  }

  for (trade = responses.begin(); trade != responses.end(); ++trade) {
    std::string commod = trade->first.request->commodity();
    Material::Ptr m = trade->second;
    index_res(m, commod);
    if (commod == fuel_incommods[region1_ID]){
      if (core1.count() < n_assem_region[region1_ID]) {
        core1.Push(m);
      } else {
        fresh1.Push(m);
      }
    }
    if (commod == fuel_incommods[region2_ID]){
      if (core2.count() < n_assem_region[region2_ID]) {
        core2.Push(m);
      } else {
        fresh2.Push(m);
      }
    }
  }
}

std::set<cyclus::BidPortfolio<Material>::Ptr> TwoRegionReactor::GetMatlBids(
    cyclus::CommodMap<Material>::type& commod_requests) {
  // DRE phase 2 -- getting bids that might fulfil other facility requests
  using cyclus::BidPortfolio;
  std::set<BidPortfolio<Material>::Ptr> ports;

  bool gotmats = false;
  std::map<std::string, MatVec> all_mats;

  if (uniq_outcommods_.empty()) {
    for (int i = 0; i < fuel_outcommods.size(); i++) {
      uniq_outcommods_.insert(fuel_outcommods[i]);
    }
  }

  for (int i = 0; i < 2; i++) {
    std::string commod = fuel_outcommods[i];
    std::vector<Request<Material>*>& reqs = commod_requests[commod];
    all_mats = PeekSpent(i);
    if (reqs.size() == 0) {
      continue;
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

void TwoRegionReactor::Tock() {
  if (retired()) { 
    return;
  }
  
  // Check that irradiation and refueling periods are over, that 
  // the core is full and that fuel was successfully discharged in this refueling time.
  // If this is the case, then a new cycle will be initiated.
  if (ReadyToRefuel() && FullRegion(region1_ID) && FullRegion(region2_ID) && discharged1 == true && discharged2 == true) {
    discharged1 = false;
    discharged2 = false; 
    cycle_step = 0;
  }

  if (cycle_step == 0 && FullRegion(region1_ID) && FullRegion(region2_ID)) {
    Record("CYCLE_START", "");
  }

  // record power generation if we're in the middle of a cycle. 
  if (cycle_step >= 0 && cycle_step < cycle_time &&
      FullRegion(region1_ID) && FullRegion(region2_ID)) {
    cyclus::toolkit::RecordTimeSeries<cyclus::toolkit::POWER>(this, power_cap);
    cyclus::toolkit::RecordTimeSeries<double>("supplyPOWER", this, power_cap);
  } else {
    cyclus::toolkit::RecordTimeSeries<cyclus::toolkit::POWER>(this, 0);
    cyclus::toolkit::RecordTimeSeries<double>("supplyPOWER", this, 0);
  }

  // "if" prevents starting cycle after initial deployment until core is full
  // even though cycle_step is its initial zero.
  if ((cycle_step > 0) || (FullRegion(region1_ID) && FullRegion(region2_ID))){
      cycle_step++;
  }
}

void TwoRegionReactor::Transmute() { 
  for (int i=0; i < 2; i++){
    // transmute in each region of the core
    Transmute(n_assem_batch[i], i);
  }
}

void TwoRegionReactor::Transmute(int n_assem, int region_num) {
  MatVec old; 
  
  if (region_num == 0){
    old = core1.PopN(std::min(n_assem, core1.count()));
    core1.Push(old);
    if (core1.count() > old.size()) {
      // rotate untransmuted mats back to back of buffer
      core1.Push(core1.PopN(core1.count() - old.size()));
    }
  }

  if (region_num == 1){
    old = core2.PopN(std::min(n_assem, core2.count()));
    core2.Push(old);
    if (core2.count() > old.size()) {
      // rotate untransmuted mats back to back of buffer
      core2.Push(core2.PopN(core2.count() - old.size()));
    }}

  std::stringstream ss;
  ss << old.size() << " assemblies in region " << region_num;
  Record("TRANSMUTE", ss.str());

  for (int i = 0; i < old.size(); i++) {
    old[i]->Transmute(context()->GetRecipe(fuel_outrecipe(old[i])));
  }
}

std::map<std::string, MatVec> TwoRegionReactor::PeekSpent(int region_num) {
  // looking at the number and commodity name of the materials in each region
  std::map<std::string, MatVec> mapped;
  MatVec mats; 
  if (region_num == 0){
    mats = spent1.PopN(spent1.count());
    spent1.Push(mats);
  }
  if (region_num == 1){
    mats = spent2.PopN(spent2.count());
    spent2.Push(mats);
  }
  for (int i = 0; i < mats.size(); i++) {
    std::string commod = fuel_outcommod(mats[i]);
    mapped[commod].push_back(mats[i]);
  }
  
  return mapped;
}

bool TwoRegionReactor::Discharge(int region_num) {
  if (region_num == 0){
    int npop = std::min(n_assem_batch[region1_ID], core1.count());
    if (n_assem_spent[region1_ID] - spent1.count() < npop) {
      Record("DISCHARGE", "failed");
      return false;  // not enough room in spent buffer
    }

    std::stringstream ss;
    ss << npop << " assemblies from Region 1";
    Record("DISCHARGE", ss.str());
    spent1.Push(core1.PopN(npop));
  }

  if (region_num == 1){
    int npop = std::min(n_assem_batch[region2_ID], core2.count());
    if (n_assem_spent[region2_ID] - spent2.count() < npop) {
      Record("DISCHARGE", "failed");
      return false;  // not enough room in spent buffer
    }

    std::stringstream ss;
    ss << npop << " assemblies from Region 2";
    Record("DISCHARGE", ss.str());
    spent2.Push(core2.PopN(npop));
  }

  std::map<std::string, MatVec> spent_mats;
  spent_mats = PeekSpent(region_num);
  MatVec mats = spent_mats[fuel_outcommods[region_num]];
  double tot_spent = 0;
  for (int i = 0; i < mats.size(); i++){
    Material::Ptr m = mats[i];
    tot_spent += m->quantity();
  }
  cyclus::toolkit::RecordTimeSeries<double>("supply"+fuel_outcommods[region_num], this, tot_spent);

  return true;
}

void TwoRegionReactor::Load(int region_num) {
  if (region_num == 0){
    int n = std::min(n_assem_region[region1_ID] - core1.count(), fresh1.count());
    if (n == 0) {
      return;
    }

    std::stringstream ss;
    ss << n << " assemblies into Region 1";
    Record("LOAD", ss.str());
    core1.Push(fresh1.PopN(n));
  }
  if (region_num == 1){
    int n = std::min(n_assem_region[region2_ID] - core2.count(), fresh2.count());
    if (n == 0) {
      return;
    }

    std::stringstream ss;
    ss << n << " assemblies into Region 2";
    Record("LOAD", ss.str());
    core2.Push(fresh2.PopN(n));
  }
}

std::string TwoRegionReactor::fuel_incommod(Material::Ptr m) {
  // get the input commodity name for a material
  int i = res_indexes[m->obj_id()];
  if (i >= fuel_incommods.size()) {
    throw KeyError("areal::TwoRegionReactor - no incommod for material object");
  }
  return fuel_incommods[i];
}

std::string TwoRegionReactor::fuel_outcommod(Material::Ptr m) {
  // get the output commodity name for a material
  int i = res_indexes[m->obj_id()];
  if (i >= fuel_outcommods.size()) {
    throw KeyError("areal::TwoRegionReactor - no outcommod for material object");
  }
  return fuel_outcommods[i];
}

std::string TwoRegionReactor::fuel_inrecipe(Material::Ptr m) {
  // get the input recipe name for a material
  int i = res_indexes[m->obj_id()];
  if (i >= fuel_inrecipes.size()) {
    throw KeyError("areal::TwoRegionReactor - no inrecipe for material object");
  }
  return fuel_inrecipes[i];
}

std::string TwoRegionReactor::fuel_outrecipe(Material::Ptr m) {
  // get the output recipe name for a material
  int i = res_indexes[m->obj_id()];
  if (i >= fuel_outrecipes.size()) {
    throw KeyError("areal::TwoRegionReactor - no outrecipe for material object");
  }
  return fuel_outrecipes[i];
}

void TwoRegionReactor::index_res(cyclus::Resource::Ptr m, std::string incommod) {
  // get the index number of the fuel_incommods input that corresponds 
  // to a material
  for (int i = 0; i < fuel_incommods.size(); i++) {
    if (fuel_incommods[i] == incommod) {
      res_indexes[m->obj_id()] = i;
      return;
    }
  }
  throw ValueError(
      "areal::TwoRegionReactor - received unsupported incommod material");
}

std::map<std::string, MatVec> TwoRegionReactor::PopSpent(int region_num) {
  std::map<std::string, MatVec> mapped;
  MatVec mats; 
  if (region_num == 0){
    mats = spent1.PopN(spent1.count());
  }
  if (region_num == 1){
    mats = spent2.PopN(spent2.count());
  }
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

void TwoRegionReactor::PushSpent(std::map<std::string, MatVec> leftover, int region_num) {
  std::map<std::string, MatVec>::iterator it;
  for (it = leftover.begin(); it != leftover.end(); ++it) {
    // undo reverse in PopSpent to make sure oldest assemblies come out first
    std::reverse(it->second.begin(), it->second.end());
    if (region_num == 0){
      spent1.Push(it->second);
    }
    if (region_num == 1){
      spent2.Push(it->second);
    }
  }
}

bool TwoRegionReactor::ReadyToRefuel() {
  return cycle_step >= cycle_time + refuel_time;
}

bool TwoRegionReactor::FullRegion(int region_num) {
  if (region_num == 0){
    return core1.count() == n_assem_region[region1_ID];
  }
  if (region_num == 1){
    return core2.count() == n_assem_region[region2_ID];
  }
}

void TwoRegionReactor::Record(std::string name, std::string val) {
  context()
      ->NewDatum("TwoRegionReactorEvents")
      ->AddVal("AgentId", id())
      ->AddVal("Time", context()->time())
      ->AddVal("Event", name)
      ->AddVal("Value", val)
      ->Record();
}

extern "C" cyclus::Agent* ConstructTwoRegionReactor(cyclus::Context* ctx) {
  return new TwoRegionReactor(ctx);
}

}  // namespace areal
