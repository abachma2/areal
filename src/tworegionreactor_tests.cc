#include <gtest/gtest.h>

#include <sstream>

#include "cyclus.h"

using pyne::nucname::id;
using cyclus::Composition;
using cyclus::Material;
using cyclus::QueryResult;
using cyclus::Cond;
using cyclus::toolkit::MatQuery;

namespace areal {
namespace tworegionreactortests {

Composition::Ptr c_uox() {
  cyclus::CompMap m;
  m[id("u235")] = 0.04;
  m[id("u238")] = 0.96;
  return Composition::CreateFromMass(m);
};

Composition::Ptr c_mox() {
  cyclus::CompMap m;
  m[id("u235")] = .7;
  m[id("u238")] = 100;
  m[id("pu239")] = 3.3;
  return Composition::CreateFromMass(m);
};

Composition::Ptr c_spentuox() {
  cyclus::CompMap m;
  m[id("u235")] =  .8;
  m[id("u238")] =  100;
  m[id("pu239")] = 1;
  return Composition::CreateFromMass(m);
};

Composition::Ptr c_spentmox() {
  cyclus::CompMap m;
  m[id("u235")] =  .2;
  m[id("u238")] =  100;
  m[id("pu239")] = .9;
  return Composition::CreateFromMass(m);
};

Composition::Ptr c_water() {
  cyclus::CompMap m;
  m[id("O16")] =  1;
  m[id("H1")] =  2;
  return Composition::CreateFromAtom(m);
};

// Test that with a zero refuel_time and a zero capacity fresh fuel buffer
// (the default), fuel can be ordered and the cycle started with no time step
// delay.
TEST(TwoRegionReactorTests, JustInTimeOrdering) {
  std::string config =
     "  <fuel_inrecipes>  <val>lwr_fresh</val> <val>bwr_fresh</val> </fuel_inrecipes>  "
     "  <fuel_outrecipes> <val>lwr_spent</val> <val>bwr_spent</val> </fuel_outrecipes>  "
     "  <fuel_incommods>  <val>enriched_u</val> <val>natural_u</val> </fuel_incommods>  "
     "  <fuel_outcommods> <val>waste</val>   <val>bwr_waste</val>   </fuel_outcommods>  "
     ""
     "  <cycle_time>1</cycle_time>  "
     "  <refuel_time>0</refuel_time>  "
     "  <assem_size> <val>300</val> <val>300</val> </assem_size>  "
     "  <n_assem_region1>1</n_assem_region1>  "
     "  <n_assem_region2>1</n_assem_region2>"
     "  <n_assem_batch1>1</n_assem_batch1>  "
     "  <n_assem_batch2>1</n_assem_batch2>";

  int simdur = 50;
  cyclus::MockSim sim(cyclus::AgentSpec(":areal:TwoRegionReactor"), config, simdur);
  sim.AddSource("enriched_u").Finalize();
  sim.AddSource("natural_u").Finalize();
  sim.AddRecipe("lwr_fresh", c_uox());
  sim.AddRecipe("bwr_fresh", c_mox());
  sim.AddRecipe("bwr_spent", c_spentmox());
  sim.AddRecipe("lwr_spent", c_spentuox());
  int id = sim.Run();
  
  QueryResult qr = sim.db().Query("Transactions", NULL);
  int n_trans = qr.rows.size();
  EXPECT_EQ(simdur*2, n_trans) << "expected 100 transactions, got " << n_trans;
  
  std::vector<cyclus::Cond> cond1;
  cond1.push_back(cyclus::Cond("Commodity", "==", std::string("enriched_u")));
  qr = sim.db().Query("Transactions", &cond1);
  int n_trans1 = qr.rows.size();
  EXPECT_EQ(simdur, n_trans1) << "expected 50 transactions, got " << n_trans1;

  std::vector<cyclus::Cond> cond2;
  cond2.push_back(cyclus::Cond("Commodity", "==", std::string("natural_u")));
  qr = sim.db().Query("Transactions", &cond2);
  int n_trans2 = qr.rows.size();
  EXPECT_EQ(simdur, n_trans2) << "expected 50 transactions, got " << n_trans2;
}

// tests that the correct number of assemblies are popped from the core each
// cycle.
TEST(TwoRegionReactorTests, BatchSizes) {
  std::string config =
     "  <fuel_inrecipes>  <val>uox</val>  <val>mox</val>    </fuel_inrecipes>  "
     "  <fuel_outrecipes> <val>spentuox</val> <val>spentmox</val> </fuel_outrecipes>  "
     "  <fuel_incommods>  <val>uox</val>   <val>mox</val>   </fuel_incommods>  "
     "  <fuel_outcommods> <val>spentuox</val> <val>spentmox</val>  </fuel_outcommods>  "
     ""
     "  <cycle_time>1</cycle_time>  "
     "  <refuel_time>0</refuel_time>  "
     "  <assem_size> <val>1</val> <val>2</val> </assem_size>  "
     "  <n_assem_region1>7</n_assem_region1>  "
     "  <n_assem_region2>14</n_assem_region2>"
     "  <n_assem_batch1>3</n_assem_batch1>  "
     "  <n_assem_batch2>2</n_assem_batch2>  ";

  int simdur = 50;
  cyclus::MockSim sim(cyclus::AgentSpec(":areal:TwoRegionReactor"), config, simdur);
  sim.AddSource("uox").Finalize();
  sim.AddSource("mox").Finalize();
  sim.AddRecipe("uox", c_uox());
  sim.AddRecipe("mox", c_mox());
  sim.AddRecipe("spentuox", c_spentuox());
  sim.AddRecipe("spentmox", c_spentmox());
  int id = sim.Run();

  QueryResult qr = sim.db().Query("Transactions", NULL);
  // 7 for initial core, 3 per time step for each new batch for remainder in region 1
  // 14 for initial core, 2 per time step for each new batch for remainder in region 2
  EXPECT_EQ(7+3*(simdur-1)+14+2*(simdur-1), qr.rows.size());

  // test each commodity name
  std::vector<cyclus::Cond> cond1;
  cond1.push_back(cyclus::Cond("Commodity", "==", std::string("uox")));
  qr = sim.db().Query("Transactions", &cond1);
  int n_trans1 = qr.rows.size();
  EXPECT_EQ(7+3*(simdur-1), n_trans1) << "expected 154 transactions, got " << n_trans1;

  std::vector<cyclus::Cond> cond2;
  cond2.push_back(cyclus::Cond("Commodity", "==", std::string("mox")));
  qr = sim.db().Query("Transactions", &cond2);
  int n_trans2 = qr.rows.size();
  EXPECT_EQ(14+2*(simdur-1), n_trans2) << "expected 112 transactions, got " << n_trans2;
}

// tests that the refueling period between cycle end and start of the next
// cycle is honored.
TEST(TwoRegionReactorTests, RefuelTimes) {
  std::string config =
     "  <fuel_inrecipes>  <val>uox</val>  <val>mox</val>    </fuel_inrecipes>  "
     "  <fuel_outrecipes> <val>spentuox</val> <val>spentmox</val> </fuel_outrecipes>  "
     "  <fuel_incommods>  <val>uox</val>   <val>mox</val>   </fuel_incommods>  "
     "  <fuel_outcommods> <val>spentuox</val> <val>spentmox</val>  </fuel_outcommods>  "
     ""
     "  <cycle_time>4</cycle_time>  "
     "  <refuel_time>3</refuel_time>  "
     "  <assem_size> <val>1</val> <val>2</val> </assem_size>  "
     "  <n_assem_region1>1</n_assem_region1>  "
     "  <n_assem_region2>2</n_assem_region2>"
     "  <n_assem_batch1>1</n_assem_batch1>  "
     "  <n_assem_batch2>1</n_assem_batch2>  ";

  int simdur = 49;
  cyclus::MockSim sim(cyclus::AgentSpec(":areal:TwoRegionReactor"), config, simdur);
  sim.AddSource("uox").Finalize();
  sim.AddSource("mox").Finalize();
  sim.AddRecipe("uox", c_uox());
  sim.AddRecipe("mox", c_mox());
  sim.AddRecipe("spentuox", c_spentuox());
  sim.AddRecipe("spentmox", c_spentmox());
  int id = sim.Run();

  QueryResult qr = sim.db().Query("Transactions", NULL);
  int cyclet = 4;
  int refuelt = 3;
  int n_assem_want = simdur/(cyclet+refuelt); // refuels
  EXPECT_EQ(n_assem_want*2+3, qr.rows.size()) << "expected 16 transactions, got " << qr.rows.size();

  std::vector<cyclus::Cond> cond1;
  cond1.push_back(cyclus::Cond("Commodity", "==", std::string("uox")));
  qr = sim.db().Query("Transactions", &cond1);
  int n_trans1 = qr.rows.size();
  EXPECT_EQ(n_assem_want+1, n_trans1) << "expected 8 transactions, got " << n_trans1;

  std::vector<cyclus::Cond> cond2;
  cond2.push_back(cyclus::Cond("Commodity", "==", std::string("mox")));
  qr = sim.db().Query("Transactions", &cond2);
  int n_trans2 = qr.rows.size();
  EXPECT_EQ(n_assem_want+2, n_trans2) << "expected 8 transactions, got " << n_trans2;
}


// tests that a reactor decommissions on time without producing
// power at the end of its lifetime.
TEST(TwoRegionReactorTests, DecomTimes) {
  std::string config =
     "  <fuel_inrecipes>  <val>uox</val>  <val>mox</val>    </fuel_inrecipes>  "
     "  <fuel_outrecipes> <val>spentuox</val> <val>spentmox</val> </fuel_outrecipes>  "
     "  <fuel_incommods>  <val>uox</val>   <val>mox</val>   </fuel_incommods>  "
     "  <fuel_outcommods> <val>spentuox</val> <val>spentmox</val>  </fuel_outcommods>  "
     ""
     "  <cycle_time>2</cycle_time>  "
     "  <refuel_time>2</refuel_time>  "
     "  <assem_size> <val>1</val> <val>1</val> </assem_size>  "
     "  <n_assem_region1>3</n_assem_region1>  "
     "  <n_assem_region2>2</n_assem_region2>"
     "  <power_cap>1000</power_cap>  "
     "  <n_assem_batch1>1</n_assem_batch1>  "
     "  <n_assem_batch2>1</n_assem_batch2>  ";

  int simdur = 12;
  int lifetime = 7;
  cyclus::MockSim sim(cyclus::AgentSpec(":areal:TwoRegionReactor"), config, simdur, lifetime);
  sim.AddSource("uox").Finalize();
  sim.AddSource("mox").Finalize();
  sim.AddRecipe("uox", c_uox());
  sim.AddRecipe("mox", c_mox());
  sim.AddRecipe("spentuox", c_spentuox());
  sim.AddRecipe("spentmox", c_spentmox());
  int id = sim.Run();

  // operating for 2+2 months and shutdown for 2+1
  int on_time = 4;
  std::vector<Cond> conds;
  conds.push_back(Cond("Value", "==", 1000));
  QueryResult qr = sim.db().Query("TimeSeriesPower", &conds);
  EXPECT_EQ(on_time, qr.rows.size());

  int off_time = 3;
  conds.clear();
  conds.push_back(Cond("Value", "==", 0));
  qr = sim.db().Query("TimeSeriesPower", &conds);
  EXPECT_EQ(off_time, qr.rows.size());

  // make sure transactions are correct
  qr = sim.db().Query("Transactions", NULL);
  int n_trans = qr.rows.size();
  EXPECT_EQ(9, n_trans) << "expected 9 transactions, got " << n_trans;
  
  std::vector<cyclus::Cond> cond1;
  cond1.push_back(cyclus::Cond("Commodity", "==", std::string("uox")));
  qr = sim.db().Query("Transactions", &cond1);
  int n_trans1 = qr.rows.size();
  EXPECT_EQ(5, n_trans1) << "expected 5 transactions, got " << n_trans1;

  std::vector<cyclus::Cond> cond2;
  cond2.push_back(cyclus::Cond("Commodity", "==", std::string("mox")));
  qr = sim.db().Query("Transactions", &cond2);
  int n_trans2 = qr.rows.size();
  EXPECT_EQ(4, n_trans2) << "expected 4 transactions, got " << n_trans2;
}


// Tests if a reactor produces power at the time of its decommission
// given a refuel_time of zero.
TEST(TwoRegionReactorTests, DecomZeroRefuel) {
  std::string config =
     "  <fuel_inrecipes>  <val>uox</val>  <val>mox</val>    </fuel_inrecipes>  "
     "  <fuel_outrecipes> <val>spentuox</val> <val>spentmox</val> </fuel_outrecipes>  "
     "  <fuel_incommods>  <val>uox</val>   <val>mox</val>   </fuel_incommods>  "
     "  <fuel_outcommods> <val>spentuox</val> <val>spentmox</val>  </fuel_outcommods>  "
     ""
     "  <cycle_time>2</cycle_time>  "
     "  <refuel_time>0</refuel_time>  "
     "  <assem_size> <val>1</val> <val>2</val> </assem_size>  "
     "  <n_assem_region1>3</n_assem_region1>  "
     "  <n_assem_region2>2</n_assem_region2>"
     "  <n_assem_batch1>1</n_assem_batch1>  "
     "  <n_assem_batch2>1</n_assem_batch2>  "
     "  <power_cap>1000</power_cap>";

  int simdur = 8;
  int lifetime = 6;
  cyclus::MockSim sim(cyclus::AgentSpec(":areal:TwoRegionReactor"), config, simdur, lifetime);
  sim.AddSource("uox").Finalize();
  sim.AddSource("mox").Finalize();
  sim.AddRecipe("uox", c_uox());
  sim.AddRecipe("mox", c_mox());
  sim.AddRecipe("spentuox", c_spentuox());
  sim.AddRecipe("spentmox", c_spentmox());
  int id = sim.Run();

  // operating for 2+2 months and shutdown for 2+1
  int on_time = 6;
  std::vector<Cond> conds;
  conds.push_back(Cond("Value", "==", 1000));
  QueryResult qr = sim.db().Query("TimeSeriesPower", &conds);
  EXPECT_EQ(on_time, qr.rows.size());
}

// tests that new fuel is ordered immediately following cycle end - at the
// start of the refueling period - not before and not after. - thie is subtly
// different than RefuelTimes test and is not a duplicate of it.
TEST(TwoRegionReactorTests, OrderAtRefuelStart) {
  std::string config =
     "  <fuel_inrecipes>  <val>uox</val>  <val>mox</val>    </fuel_inrecipes>  "
     "  <fuel_outrecipes> <val>spentuox</val> <val>spentmox</val> </fuel_outrecipes>  "
     "  <fuel_incommods>  <val>uox</val>   <val>mox</val>   </fuel_incommods>  "
     "  <fuel_outcommods> <val>spentuox</val> <val>spentmox</val>  </fuel_outcommods>  "
     ""
     "  <cycle_time>4</cycle_time>  "
     "  <refuel_time>3</refuel_time>  "
     "  <assem_size> <val>1</val> <val>1</val> </assem_size>  "
     "  <n_assem_region1>1</n_assem_region1>  "
     "  <n_assem_region2>1</n_assem_region2>"
     "  <n_assem_batch1>1</n_assem_batch1>  "
     "  <n_assem_batch2>1</n_assem_batch2>";

  int simdur = 7;
  cyclus::MockSim sim(cyclus::AgentSpec(":areal:TwoRegionReactor"), config, simdur);
  sim.AddSource("uox").Finalize();
  sim.AddSource("mox").Finalize();
  sim.AddRecipe("uox", c_uox());
  sim.AddRecipe("mox", c_mox());
  sim.AddRecipe("spentuox", c_spentuox());
  sim.AddRecipe("spentmox", c_spentmox());
  int id = sim.Run();

  QueryResult qr = sim.db().Query("Transactions", NULL);
  int cyclet = 4;
  int refuelt = 3;
  int n_assem_want = simdur/(cyclet+refuelt)+1; // +1 for initial core
  EXPECT_EQ(n_assem_want*2, qr.rows.size()); // mult by 2 because 2 regions
}

// tests that the reactor handles requesting multiple types of fuel correctly
// - with proper inventory constraint honoring, etc.
TEST(TwoRegionReactorTests, MultiFuelMix) {
  std::string config =
     "  <fuel_inrecipes>  <val>uox</val>      <val>mox</val>      </fuel_inrecipes>  "
     "  <fuel_outrecipes> <val>spentuox</val> <val>spentmox</val> </fuel_outrecipes>  "
     "  <fuel_incommods>  <val>uox</val>      <val>mox</val>      </fuel_incommods>  "
     "  <fuel_outcommods> <val>waste</val>    <val>waste</val>    </fuel_outcommods>  "
     ""
     "  <cycle_time>1</cycle_time>  "
     "  <refuel_time>0</refuel_time>  "
     "  <assem_size> <val>1</val> <val>1</val> </assem_size>  "
     "  <n_assem_fresh1>3</n_assem_fresh1>  "
     "  <n_assem_region1>3</n_assem_region1>  "
     "  <n_assem_region2>3</n_assem_region2>"
     "  <n_assem_batch1>3</n_assem_batch1>  "
     "  <n_assem_batch2>3</n_assem_batch2>";

  // it is important that the sources have cumulative capacity greater than
  // the reactor can take on a single time step - to test that inventory
  // capacity constraints are being set properly.  It is also important that
  // each source is smaller capacity thatn the reactor orders on each time
  // step to make it easy to compute+check the number of transactions.
  int simdur = 50;
  cyclus::MockSim sim(cyclus::AgentSpec(":areal:TwoRegionReactor"), config, simdur);
  sim.AddSource("uox").capacity(2).Finalize();
  sim.AddSource("mox").capacity(2).Finalize();
  sim.AddRecipe("uox", c_uox());
  sim.AddRecipe("spentuox", c_spentuox());
  sim.AddRecipe("mox", c_spentuox());
  sim.AddRecipe("spentmox", c_spentuox());
  int id = sim.Run();

  QueryResult qr = sim.db().Query("Transactions", NULL);
  // +3 is for fresh fuel inventory
  EXPECT_EQ(3*simdur+3, qr.rows.size());
}

// tests that the reactor halts operation when it has no more room in its
// spent fuel inventory buffer.
TEST(TwoRegionReactorTests, FullSpentInventory) {
  std::string config =
     "  <fuel_inrecipes>  <val>uox</val>  <val>mox</val>    </fuel_inrecipes>  "
     "  <fuel_outrecipes> <val>spentuox</val> <val>spentmox</val> </fuel_outrecipes>  "
     "  <fuel_incommods>  <val>uox</val>   <val>mox</val>   </fuel_incommods>  "
     "  <fuel_outcommods> <val>spentuox</val> <val>spentmox</val>  </fuel_outcommods>  "
     ""
     "  <cycle_time>1</cycle_time>  "
     "  <refuel_time>0</refuel_time>  "
     "  <assem_size> <val>1</val> <val>1</val> </assem_size>  "
     "  <n_assem_region1>1</n_assem_region1>  "
     "  <n_assem_region2>1</n_assem_region2>"
     "  <n_assem_batch1>1</n_assem_batch1>  "
     "  <n_assem_batch2>1</n_assem_batch2>"
     "  <n_assem_spent1>3</n_assem_spent1>  ";

  int simdur = 10;
  cyclus::MockSim sim(cyclus::AgentSpec(":areal:TwoRegionReactor"), config, simdur);
  sim.AddSource("uox").Finalize();
  sim.AddSource("mox").Finalize();
  sim.AddRecipe("uox", c_uox());
  sim.AddRecipe("mox", c_mox());
  sim.AddRecipe("spentuox", c_spentuox());
  sim.AddRecipe("spentmox", c_spentmox());
  int id = sim.Run();

  QueryResult qr = sim.db().Query("Transactions", NULL);
  int n_assem_spent = 9; // 3 in spent1, 4 in spent2, and 1 in each region
  // the spent2 ResBuf can still take a batch-worth of assemblies once the 
  // spent1 buffer is full. 

  // +1 is for the assembly in the core + the three in spent
  EXPECT_EQ(n_assem_spent, qr.rows.size());
}

// tests that the reactor shuts down, ie., does not generate power, when the
// spent fuel inventory is full and the core cannot be unloaded.
TEST(TwoRegionReactorTests, FullSpentInventoryShutdown) {
  std::string config =
     "  <fuel_inrecipes>  <val>uox</val>  <val>mox</val>    </fuel_inrecipes>  "
     "  <fuel_outrecipes> <val>spentuox</val> <val>spentmox</val> </fuel_outrecipes>  "
     "  <fuel_incommods>  <val>uox</val>   <val>mox</val>   </fuel_incommods>  "
     "  <fuel_outcommods> <val>spentuox</val> <val>spentmox</val>  </fuel_outcommods>  "
     ""
     "  <cycle_time>1</cycle_time> "
     "  <refuel_time>0</refuel_time> "
     "  <assem_size> <val>1</val> <val>1</val> </assem_size> "
     "  <n_assem_region1>1</n_assem_region1> "
     "  <n_assem_region2>1</n_assem_region2> "
     "  <n_assem_batch1>1</n_assem_batch1> "
     "  <n_assem_batch2>1</n_assem_batch2> "
     " <n_assem_spent1>1</n_assem_spent1> "
     " <power_cap>100</power_cap> ";

  int simdur = 3;
  cyclus::MockSim sim(cyclus::AgentSpec(":areal:TwoRegionReactor"), config, simdur);
  sim.AddSource("uox").Finalize();
  sim.AddSource("mox").Finalize();
  sim.AddRecipe("uox", c_uox());
  sim.AddRecipe("mox", c_mox());
  sim.AddRecipe("spentuox", c_spentuox());
  sim.AddRecipe("spentmox", c_spentmox());
  int id = sim.Run();

  QueryResult qr = sim.db().Query("TimeSeriesPower", NULL);
  EXPECT_EQ(0, qr.GetVal<double>("Value", simdur - 1));

}

// tests that the reactor cycle is delayed as expected when it is unable to
// acquire fuel in time for the next cycle start.  This checks that after a
// cycle is delayed past an original scheduled start time, as soon as enough fuel is
// received, a new cycle pattern is established starting from the delayed
// start time.
TEST(TwoRegionReactorTests, FuelShortage) {
  std::string config =
     "  <fuel_inrecipes>  <val>uox</val>  <val>mox</val>    </fuel_inrecipes>  "
     "  <fuel_outrecipes> <val>spentuox</val> <val>spentmox</val> </fuel_outrecipes>  "
     "  <fuel_incommods>  <val>uox</val>   <val>mox</val>   </fuel_incommods>  "
     "  <fuel_outcommods> <val>waste</val> <val>spentmox</val>  </fuel_outcommods>  "
     ""
     "  <cycle_time>7</cycle_time>  "
     "  <refuel_time>0</refuel_time>  "
     "  <assem_size> <val>1</val> <val>1</val> </assem_size>  "
     "  <n_assem_region1>3</n_assem_region1>  "
     "  <n_assem_region2>2</n_assem_region2>"
     "  <n_assem_batch1>3</n_assem_batch1>  "
     "  <n_assem_batch2>2</n_assem_batch2>";

  int simdur = 50;
  cyclus::MockSim sim(cyclus::AgentSpec(":areal:TwoRegionReactor"), config, simdur);
  sim.AddSource("uox").lifetime(1).Finalize(); // provide initial full batch
  sim.AddSource("mox").lifetime(1).Finalize();
  sim.AddSource("uox").start(9).lifetime(1).capacity(2).Finalize(); // provide partial batch post cycle-end
  sim.AddSource("mox").start(9).lifetime(1).capacity(1).Finalize();
  sim.AddSource("uox").start(15).Finalize(); // provide remainder of batch much later
  sim.AddSource("mox").start(15).Finalize();
  sim.AddRecipe("uox", c_uox());
  sim.AddRecipe("mox", c_mox());
  sim.AddRecipe("spentuox", c_spentuox());
  sim.AddRecipe("spentmox", c_spentmox());
  int id = sim.Run();

  // check that we never got a full refueled batch during refuel period
  std::vector<Cond> conds;
  conds.push_back(Cond("Time", "<", 15));
  QueryResult qr = sim.db().Query("Transactions", &conds);
  EXPECT_EQ(8, qr.rows.size());

  // after being delayed past original scheduled start of new cycle, we got
  // final assembly for core.
  conds.clear();
  conds.push_back(Cond("Time", "==", 15));
  qr = sim.db().Query("Transactions", &conds);
  EXPECT_EQ(2, qr.rows.size());

  // all during the next (delayed) cycle we shouldn't be requesting any new fuel
  conds.clear();
  conds.push_back(Cond("Time", "<", 21));
  qr = sim.db().Query("Transactions", &conds);
  EXPECT_EQ(10, qr.rows.size());

  // as soon as this delayed cycle ends, we should be requesting/getting 3 new assemblies 
  // in region 1 and 2 new assemblies in region 2
  conds.clear();
  conds.push_back(Cond("Time", "==", 22));
  qr = sim.db().Query("Transactions", &conds);
  EXPECT_EQ(5, qr.rows.size());
}

// tests that discharged fuel is transmuted properly immediately at cycle end.
TEST(TwoRegionReactorTests, DischargedFuelTransmute) {
  std::string config =
     "  <fuel_inrecipes>  <val>uox</val>  <val>mox</val>    </fuel_inrecipes>  "
     "  <fuel_outrecipes> <val>spentuox</val> <val>spentmox</val> </fuel_outrecipes>  "
     "  <fuel_incommods>  <val>uox</val>   <val>mox</val>   </fuel_incommods>  "
     "  <fuel_outcommods> <val>waste</val> <val>spentmox</val>  </fuel_outcommods>  "
     ""
     "  <cycle_time>4</cycle_time>  "
     "  <refuel_time>3</refuel_time>  "
     "  <assem_size> <val>1</val> <val>1</val> </assem_size>  "
     "  <n_assem_region1>1</n_assem_region1>  "
     "  <n_assem_region2>2</n_assem_region2>"
     "  <n_assem_batch1>1</n_assem_batch1>  "
     "  <n_assem_batch2>1</n_assem_batch2>  ";

  int simdur = 7;
  cyclus::MockSim sim(cyclus::AgentSpec(":areal:TwoRegionReactor"), config, simdur);
  sim.AddSource("uox").Finalize();
  sim.AddSource("mox").Finalize();
  sim.AddSink("waste").Finalize();
  sim.AddSink("spentmox").Finalize();
  sim.AddRecipe("uox", c_uox());
  sim.AddRecipe("mox", c_mox());
  Composition::Ptr spentuox = c_spentuox();
  sim.AddRecipe("spentuox", spentuox);
  Composition::Ptr spentmox = c_spentmox();
  sim.AddRecipe("spentmox", spentmox);
  int id = sim.Run();

  std::vector<Cond> conds;
  conds.push_back(Cond("Commodity", "==", std::string("waste")));
  int resid = sim.db().Query("Transactions", &conds).GetVal<int>("ResourceId");
  Material::Ptr m = sim.GetMaterial(resid);
  MatQuery mq1(m);
  EXPECT_EQ(spentuox->id(), m->comp()->id());
  EXPECT_TRUE(mq1.mass(942390000) > 0) << "transmuted spent fuel doesn't have Pu239";

  conds.clear();
  conds.push_back(Cond("Commodity", "==", std::string("spentmox")));
  resid = sim.db().Query("Transactions", &conds).GetVal<int>("ResourceId");
  m = sim.GetMaterial(resid);
  MatQuery mq2(m);
  EXPECT_EQ(spentmox->id(), m->comp()->id());
  EXPECT_TRUE(mq2.mass(942390000) > 0) << "transmuted spent fuel doesn't have Pu239";
}

// tests that spent fuel is offerred on correct commods according to the
// incommod it was received on - esp when dealing with multiple fuel commods
// simultaneously.
TEST(TwoRegionReactorTests, SpentFuelProperCommodTracking) {
  std::string config =
     "  <fuel_inrecipes>  <val>uox</val>      <val>mox</val>      </fuel_inrecipes>  "
     "  <fuel_outrecipes> <val>spentuox</val> <val>spentmox</val> </fuel_outrecipes>  "
     "  <fuel_incommods>  <val>uox</val>      <val>mox</val>      </fuel_incommods>  "
     "  <fuel_outcommods> <val>waste1</val>   <val>waste2</val>   </fuel_outcommods>  "
     ""
     "  <cycle_time>1</cycle_time>  "
     "  <refuel_time>0</refuel_time>  "
     "  <assem_size> <val>1</val> <val>1</val> </assem_size>  "
     "  <n_assem_region1>3</n_assem_region1>  "
     "  <n_assem_region2>2</n_assem_region2>"
     "  <n_assem_batch1>3</n_assem_batch1>  "
     "  <n_assem_batch2>2</n_assem_batch2>  ";

  int simdur = 7;
  cyclus::MockSim sim(cyclus::AgentSpec(":areal:TwoRegionReactor"), config, simdur);
  sim.AddSource("uox").Finalize();
  sim.AddSource("mox").Finalize();
  sim.AddSink("waste1").Finalize();
  sim.AddSink("waste2").Finalize();
  sim.AddRecipe("uox", c_uox());
  sim.AddRecipe("spentuox", c_spentuox());
  sim.AddRecipe("mox", c_mox());
  sim.AddRecipe("spentmox", c_spentmox());
  int id = sim.Run();

  std::vector<Cond> conds;
  conds.push_back(Cond("SenderId", "==", id));
  conds.push_back(Cond("Commodity", "==", std::string("waste1")));
  QueryResult qr = sim.db().Query("Transactions", &conds);
  EXPECT_EQ(18, qr.rows.size());

  conds[1] = Cond("Commodity", "==", std::string("waste2"));
  qr = sim.db().Query("Transactions", &conds);
  EXPECT_EQ(12, qr.rows.size());
}

// The user can optionally omit fuel preferences.  In the case where
// preferences are adjusted, the ommitted preference vector must be populated
// with default values - if it wasn't then preferences won't be adjusted
// correctly and the reactor could segfault.  Check that this doesn't happen.


TEST(TwoRegionReactorTests, Retire) {
  std::string config =
     "  <fuel_inrecipes>  <val>lwr_fresh</val> <val>bwr_fresh</val> </fuel_inrecipes>  "
     "  <fuel_outrecipes> <val>lwr_spent</val> <val>bwr_spent</val> </fuel_outrecipes>  "
     "  <fuel_incommods>  <val>enriched_u</val> <val>enriched_pu</val> </fuel_incommods>  "
     "  <fuel_outcommods> <val>waste</val> <val>spent_pu</val>    </fuel_outcommods>  "
     ""
     "  <cycle_time>7</cycle_time>  "
     "  <refuel_time>0</refuel_time>  "
     "  <assem_size> <val>300</val> <val>150</val> </assem_size>  "
     "  <n_assem_fresh1>1</n_assem_fresh1>  "
     "  <n_assem_region1>3</n_assem_region1>  "
     "  <n_assem_region2>2</n_assem_region2>  "
     "  <n_assem_batch1>1</n_assem_batch1>  "
     "  <n_assem_batch2>1</n_assem_batch2>  "
     "  <power_cap>1</power_cap>  "
     "";

  int dur = 50;
  int life = 36;
  int cycle_time = 7;
  int refuel_time = 0;
  cyclus::MockSim sim(cyclus::AgentSpec(":areal:TwoRegionReactor"), config, dur, life);
  sim.AddSource("enriched_u").Finalize();
  sim.AddSource("enriched_pu").Finalize();
  sim.AddSink("waste").Finalize();
  sim.AddSink("spent_pu").Finalize();
  sim.AddRecipe("lwr_fresh", c_uox());
  sim.AddRecipe("bwr_fresh", c_mox());
  sim.AddRecipe("lwr_spent", c_spentuox());
  sim.AddRecipe("bwr_spent", c_spentmox());
  int id = sim.Run();

  int nregion1 = 3;
  int nregion2 = 2;
  int nbatch1 = 1;
  int nbatch2 = 1;

  // reactor should stop requesting new fresh fuel as it approaches retirement
  int nassem_recv =
      static_cast<int>(ceil(static_cast<double>(life) / 7.0)) * (nbatch1 + 
        nbatch2) + (nregion1 - nbatch1) + (nregion2 - nbatch2);
  int nassem_recv_r1 =
      static_cast<int>(ceil(static_cast<double>(life) / 7.0)) * nbatch1 + 
        (nregion1 - nbatch1);
  int nassem_recv_r2 =
      static_cast<int>(ceil(static_cast<double>(life) / 7.0)) * nbatch2 + 
        (nregion2 - nbatch2);

  std::vector<Cond> conds;
  conds.push_back(Cond("ReceiverId", "==", id));
  QueryResult qr = sim.db().Query("Transactions", &conds);
  EXPECT_EQ(nassem_recv, qr.rows.size())
      << "failed to stop ordering near retirement";

  conds.clear();
  conds.push_back(Cond("Commodity", "==", std::string("enriched_u")));
  qr = sim.db().Query("Transactions", &conds);
  EXPECT_EQ(nassem_recv_r1, qr.rows.size())
      << "failed to stop ordering region 1 near retirement";

  conds.clear();
  conds.push_back(Cond("Commodity", "==", std::string("enriched_pu")));
  qr = sim.db().Query("Transactions", &conds);
  EXPECT_EQ(nassem_recv_r2, qr.rows.size())
      << "failed to stop ordering region 2 near retirement";

  // TwoRegionReactor should discharge all fuel before/by retirement
  conds.clear();
  conds.push_back(Cond("SenderId", "==", id));
  qr = sim.db().Query("Transactions", &conds);
  EXPECT_EQ(nassem_recv, qr.rows.size())
      << "failed to discharge all material by retirement time";

  conds.clear();
  conds.push_back(Cond("Commodity", "==", std::string("waste")));
  qr = sim.db().Query("Transactions", &conds);
  EXPECT_EQ(nassem_recv_r1, qr.rows.size())
      << "failed to discharge R1 material by retirement time";

  conds.clear();
  conds.push_back(Cond("Commodity", "==", std::string("spent_pu")));
  qr = sim.db().Query("Transactions", &conds);
  EXPECT_EQ(nassem_recv_r2, qr.rows.size())
      << "failed to discharge R2 material by retirement time";

  // TwoRegionReactor should record power entry on the time step it retires if operating
  int time_online = life / (cycle_time + refuel_time) * cycle_time + std::min(life % (cycle_time + refuel_time), cycle_time);
  conds.clear();
  conds.push_back(Cond("AgentId", "==", id));
  conds.push_back(Cond("Value", ">", 0));
  qr = sim.db().Query("TimeSeriesPower", &conds);
  EXPECT_EQ(time_online, qr.rows.size())
      << "failed to generate power for the correct number of time steps";
}

TEST(TwoRegionReactorTests, PositionInitialize) {
  std::string config =
     "  <fuel_inrecipes>  <val>lwr_fresh</val> <val>bwr_fresh</val> </fuel_inrecipes>  "
     "  <fuel_outrecipes> <val>lwr_spent</val> <val>bwr_spent</val> </fuel_outrecipes>  "
     "  <fuel_incommods>  <val>enriched_u</val> <val>enriched_pu</val> </fuel_incommods>  "
     "  <fuel_outcommods> <val>waste</val> <val>waste2</val>    </fuel_outcommods>  "
     ""
     "  <cycle_time>1</cycle_time>  "
     "  <refuel_time>0</refuel_time>  "
     "  <assem_size> <val>300</val> <val>150</val> </assem_size>  "
     "  <n_assem_region1>1</n_assem_region1>  "
     "  <n_assem_region2>2</n_assem_region2>  "
     "  <n_assem_batch1>1</n_assem_batch1>  "
     "  <n_assem_batch2>1</n_assem_batch2>  ";

  int simdur = 50;
  cyclus::MockSim sim(cyclus::AgentSpec(":areal:TwoRegionReactor"), config, simdur);
  sim.AddSource("enriched_u").Finalize();
  sim.AddSource("enriched_pu").Finalize();
  sim.AddRecipe("lwr_fresh", c_uox());
  sim.AddRecipe("bwr_fresh", c_mox());
  sim.AddRecipe("lwr_spent", c_spentuox());
  sim.AddRecipe("bwr_spent", c_spentmox());
  int id = sim.Run();

  QueryResult qr = sim.db().Query("AgentPosition", NULL);
  EXPECT_EQ(qr.GetVal<double>("Latitude"), 0.0);
  EXPECT_EQ(qr.GetVal<double>("Longitude"), 0.0);
}

TEST(TwoRegionReactorTests, PositionInitialize2) {
  std::string config =
     "  <fuel_inrecipes>  <val>lwr_fresh</val> <val>bwr_fresh</val> </fuel_inrecipes>  "
     "  <fuel_outrecipes> <val>lwr_spent</val> <val>bwr_spent</val> </fuel_outrecipes>  "
     "  <fuel_incommods>  <val>enriched_u</val> <val>enriched_pu</val> </fuel_incommods>  "
     "  <fuel_outcommods> <val>waste</val> <val>waste2</val>    </fuel_outcommods>  "
     ""
     "  <cycle_time>1</cycle_time>  "
     "  <refuel_time>0</refuel_time>  "
     "  <assem_size> <val>300</val> <val>150</val> </assem_size>  "
     "  <n_assem_region1>1</n_assem_region1>  "
     "  <n_assem_region2>2</n_assem_region2>  "
     "  <n_assem_batch1>1</n_assem_batch1>  "
     "  <n_assem_batch2>1</n_assem_batch2>  "
     "  <longitude>30.0</longitude>  "
     "  <latitude>30.0</latitude>  ";

  int simdur = 50;
  cyclus::MockSim sim(cyclus::AgentSpec(":areal:TwoRegionReactor"), config, simdur);
  sim.AddSource("enriched_u").Finalize();
  sim.AddSource("enriched_pu").Finalize();
  sim.AddRecipe("lwr_fresh", c_uox());
  sim.AddRecipe("bwr_fresh", c_mox());
  sim.AddRecipe("lwr_spent", c_spentuox());
  sim.AddRecipe("bwr_spent", c_spentmox());
  int id = sim.Run();

  QueryResult qr = sim.db().Query("AgentPosition", NULL);
  EXPECT_EQ(qr.GetVal<double>("Latitude"), 30.0);
  EXPECT_EQ(qr.GetVal<double>("Longitude"), 30.0);
}

} // namespace TwoRegionReactortests
} // namespace areal

