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
  for (int r; r<n_regions; ++r){
    fresh_vector[r]->keep_packaging(keep_packaging);
    core_vector[r]->keep_packaging(keep_packaging);
    spent_vector[r]->keep_packaging(keep_packaging);
  }

  // Throw error if vectors do not have size n_regions
  
  std::map< std::vector<std::string>, std::string> input_check1 = {
   {fuel_incommods, "fuel_incommods"},
   {fuel_outcommods, "fuel_outcommods"},
   {fuel_inrecipes, "fuel_inrecipes"},
   {fuel_outrecipes, "fuel_outrecipes"},
   };

  for (auto const& pair : input_check1) {
    if (pair.first.size() != n_regions) {
        throw cyclus::ValueError("areal::TwoRegionReactor " + pair.second + 
                                " does not have " + std::to_string(n_regions) + 
                                " entries.");
    }
  }

  std::map<std::vector<int>, std::string> input_check2 = {
    {n_assem_batch, "n_assem_batch"},
    {n_assem_region, "n_assem_region"},
    {n_assem_fresh, "n_assem_fresh"},
    {n_assem_spent, "n_assem_spent"}
    };

  for (auto const& pair : input_check2) {
    if (pair.first.size() != n_regions) {
        throw cyclus::ValueError("areal::TwoRegionReactor " + pair.second + 
                                " does not have " + std::to_string(n_regions) + 
                                " entries.");
    }
  }

  std::map< std::vector<double>, std::string> input_check3 = {
    {assem_size, "assem_size"} };

  for (auto const& pair : input_check3) {
    if (pair.first.size() != n_regions) {
        throw cyclus::ValueError("areal::TwoRegionReactor " + pair.second + 
                                " does not have " + std::to_string(n_regions) + 
                                " entries.");
    }
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
      double transmute_fraction = 1.0;
      if (!decom_transmute_all){
        transmute_fraction = 0.5;
      }
        for (int r=0; r<n_regions; r++){
          Transmute(ceil(n_assem_region[r]*transmute_fraction), r);
        }
    }
    // discharging fuel from each core region. This needs to be in 
    // separate loops because if the regions have different numbers of 
    // assemblies then it might break before both regions are fully 
    // discharged. 
    for (int r; r<n_regions; ++r){
      while (core_vector[r]->count() > 0 && Discharge(r)){
        // continue to discharge fuel from region r
      }
    }
    // in case a cycle lands exactly on our last time step, we will need to
    // burn a batch from fresh inventory on this time step.  When retired,
    // this batch also needs to be discharged to spent fuel inventory.
    for (int r; r<n_regions; ++r){
      while (fresh_vector[r]->count() > 0 && spent_vector[r]->space() >= assem_size[r]) {
        spent_vector[r]->Push(fresh_vector[r]->Pop());
      }
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
    discharged1 = Discharge(regionA_ID);
    discharged2 = Discharge(regionB_ID);
  }
  if (cycle_step >= cycle_time) {
    for (int r; r<n_regions; ++r) {
      Load(r);
    }
  }

}

std::set<cyclus::RequestPortfolio<Material>::Ptr> TwoRegionReactor::GetMatlRequests() {
  // DRE phase 1 -- placing requests
  using cyclus::RequestPortfolio;

  std::set<RequestPortfolio<Material>::Ptr> ports;
  Material::Ptr m;

  // second min expression reduces assembles to amount needed until
  // retirement if it is near.
  std::vector<int> n_assem_order = {0, 0};
  for (int r; r<n_regions; ++r){
    n_assem_order[r] += n_assem_region[r] - core_vector[r]->count() + n_assem_fresh[r] - fresh_vector[r]->count();
  }

  if (exit_time() != -1) {
    // the +1 accounts for the fact that the reactor is alive and gets to
    // operate during its exit_time time step.
    int t_left = exit_time() - context()->time() + 1;
    int t_left_cycle = cycle_time + refuel_time - cycle_step;
    double n_cycles_left = static_cast<double>(t_left - t_left_cycle) /
                         static_cast<double>(cycle_time + refuel_time);
    n_cycles_left = ceil(n_cycles_left);

    for (int r; r<n_regions; ++r){
      int n_need = std::max(0.0, n_cycles_left * n_assem_batch[r] - n_assem_fresh[r] + n_assem_region[r] - core_vector[r]->count());      
      n_assem_order[r] = std::min(n_assem_order[r], n_need);
    }
  }
  
  if ( (n_assem_order[0] == 0 && n_assem_order[1] == 0) || retired()) {
     return ports;
  }
  for (int r; r<n_regions; ++r){ 
    if (n_assem_order[r] > 0){
      // building request portfolio for each region and recording demand
      for (int j = 0; j < n_assem_order[r]; j++) {
        RequestPortfolio<Material>::Ptr port(new RequestPortfolio<Material>());
        std::string commod = fuel_incommods[r];
        cyclus::Composition::Ptr recipe = context()->GetRecipe(fuel_inrecipes[r]);
        m = Material::CreateUntracked(assem_size[r], recipe);

        Request<Material>* req = port->AddRequest(m, this, commod, 1.0, true);
        cyclus::toolkit::RecordTimeSeries<double>("demand"+fuel_incommods[r], this,
                                              assem_size[r]) ;

        ports.insert(port);
      }
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

  for (int r=0; r< n_regions; r++){
    std::map<std::string, MatVec> mats = PopSpent(r);
    for (int j = 0; j < trades.size(); j++) {
      std::string commod = trades[j].request->commodity();
      if (commod != fuel_outcommods[r]) {
        continue;
      }
      Material::Ptr m = mats[commod].back();
      mats[commod].pop_back();
      responses.push_back(std::make_pair(trades[j], m));
      res_indexes.erase(m->obj_id());
    }
    PushSpent(mats, r);  // return leftovers back to spent buffer
  }
}

void TwoRegionReactor::AcceptMatlTrades(const std::vector<
    std::pair<cyclus::Trade<Material>, Material::Ptr> >& responses) {
  // DRE phase 5.2 -- getting materials from other facilities
  std::vector<std::pair<cyclus::Trade<Material>,
                        Material::Ptr> >::const_iterator trade;

  std::stringstream ss;
  std::vector<int> num_response = {0, 0};
  for (trade = responses.begin(); trade != responses.end(); ++trade){
    std::string commod = trade->first.request->commodity();
    for (int r; r<n_regions; ++r){
      if (commod == fuel_incommods[r]){
        ++num_response[r];
      }
    }
  }
  int nload;
  for (int r; r<n_regions; ++r){
    nload = (std::min(num_response[r], n_assem_region[r] - core_vector[r]->count()));
    if (nload > 0) {
      ss << nload << " assemblies in Region " + region_ID_map[r];
      Record("LOAD", ss.str());
    }
  }
  
  for (int r; r<n_regions; ++r){
    for (trade = responses.begin(); trade != responses.end(); ++trade) {
      std::string commod = trade->first.request->commodity();
      Material::Ptr m = trade->second;
      index_res(m, commod);
        if (commod == fuel_incommods[r]){
          if (core_vector[r]->count() < n_assem_region[r]) {
            core_vector[r]->Push(m);
          } else {
            fresh_vector[r]->Push(m);
          }
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

  for (int r; r<n_regions; r++) {
    std::string commod = fuel_outcommods[r];
    std::vector<Request<Material>*>& reqs = commod_requests[commod];
    all_mats = PeekSpent(r);
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
  if (ReadyToRefuel() && FullRegions() && discharged1 == true && discharged2 == true) {
    discharged1 = false;
    discharged2 = false; 
    cycle_step = 0;
  }

  if (cycle_step == 0 && FullRegions()) {
    Record("CYCLE_START", "");
  }

  // record power generation if we're in the middle of a cycle. 
  if (cycle_step >= 0 && cycle_step < cycle_time && FullRegions()) {
    cyclus::toolkit::RecordTimeSeries<cyclus::toolkit::POWER>(this, power_cap);
    cyclus::toolkit::RecordTimeSeries<double>("supplyPOWER", this, power_cap);
  } else {
    cyclus::toolkit::RecordTimeSeries<cyclus::toolkit::POWER>(this, 0);
    cyclus::toolkit::RecordTimeSeries<double>("supplyPOWER", this, 0);
  }

  // "if" prevents starting cycle after initial deployment until core is full
  // even though cycle_step is its initial zero.
  if ((cycle_step > 0) || (FullRegions())){
      cycle_step++;
  }
}

void TwoRegionReactor::Transmute() { 
  for (int r; r<n_regions; r++){
    // transmute in each region of the core
    Transmute(n_assem_batch[r], r);
  }
}

void TwoRegionReactor::Transmute(int n_assem, int region_num) {
  MatVec old; 
  old = core_vector[region_num]->PopN(std::min(n_assem, core_vector[region_num]->count()));
  core_vector[region_num]->Push(old);
  if (core_vector[region_num]->count() > old.size()) {
    // rotate untransmuted mats back to back of buffer
    core_vector[region_num]->Push(core_vector[region_num]->PopN(core_vector[region_num]->count() - old.size()));
  }
  

  std::stringstream ss;
  ss << old.size() << " assemblies in region " << region_num;
  Record("TRANSMUTE", ss.str());

  for (int r; r<old.size(); r++) {
    old[r]->Transmute(context()->GetRecipe(fuel_outrecipe(old[r])));
  }
}

std::map<std::string, MatVec> TwoRegionReactor::PeekSpent(int region_num) {
  // looking at the number and commodity name of the materials in each region
  std::map<std::string, MatVec> mapped;
  MatVec mats; 
    mats = spent_vector[region_num]->PopN(spent_vector[region_num]->count());
    spent_vector[region_num]->Push(mats);
  
  for (int i = 0; i < mats.size(); i++) {
    std::string commod = fuel_outcommod(mats[i]);
    mapped[commod].push_back(mats[i]);
  }
  
  return mapped;
}

bool TwoRegionReactor::Discharge(int region_num) {
  int npop = std::min(n_assem_batch[region_num], core_vector[region_num]->count());
  if (n_assem_spent[region_num] - spent_vector[region_num]->count() < npop) {
    Record("DISCHARGE", "failed");
    return false;  // not enough room in spent buffer
  }

    std::stringstream ss;
    ss << npop << " assemblies from Region " << region_ID_map[region_num];
    Record("DISCHARGE", ss.str());
    spent_vector[region_num]->Push(core_vector[region_num]->PopN(npop));


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
  int n = std::min(n_assem_region[region_num] - core_vector[region_num]->count(), fresh_vector[region_num]->count());
  if (n == 0) {
    return;
  }

  std::stringstream ss;
  ss << n << " assemblies into Region " + region_ID_map[region_num];
  Record("LOAD", ss.str());
  core_vector[region_num]->Push(fresh_vector[region_num]->PopN(n));
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
  mats = spent_vector[region_num]->PopN(spent_vector[region_num]->count());
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
    spent_vector[region_num]->Push(it->second);
  }
}

bool TwoRegionReactor::ReadyToRefuel() {
  return cycle_step >= cycle_time + refuel_time;
}

bool TwoRegionReactor::FullRegions() {
  bool full_region;
  for (int r; r<n_regions; ++r){
    full_region = core_vector[r]->count() == n_assem_region[r];
    if (full_region == false){
      break;
    }
  }
  return full_region;
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
