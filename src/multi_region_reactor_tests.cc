#include <gtest/gtest.h>

#include "multi_region_reactor.h"

#include "agent_tests.h"
#include "context.h"
#include "facility_tests.h"
#include "pyhooks.h"

using areal::MultiRegionReactor;

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
class MultiRegionReactorTest : public ::testing::Test {
 protected:
  cyclus::TestContext tc;
  MultiRegionReactor* facility;

  virtual void SetUp() {
    cyclus::PyStart();
    facility = new MultiRegionReactor(tc.get());
  }

  virtual void TearDown() {
    delete facility;
    cyclus::PyStop();
  }
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
TEST_F(MultiRegionReactorTest, InitialState) {
  // Test things about the initial state of the facility here
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
TEST_F(MultiRegionReactorTest, Print) {
  EXPECT_NO_THROW(std::string s = facility->str());
  // Test MultiRegionReactor specific aspects of the print method here
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
TEST_F(MultiRegionReactorTest, Tick) {
  ASSERT_NO_THROW(facility->Tick());
  // Test MultiRegionReactor specific behaviors of the Tick function here
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
TEST_F(MultiRegionReactorTest, Tock) {
  EXPECT_NO_THROW(facility->Tock());
  // Test MultiRegionReactor specific behaviors of the Tock function here
  int result = 4;
  EXPECT_EQ(5, result);
}



// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
// Do Not Touch! Below section required for connection with Cyclus
cyclus::Agent* MultiRegionReactorConstructor(cyclus::Context* ctx) {
  return new MultiRegionReactor(ctx);
}
// Required to get functionality in cyclus agent unit tests library
#ifndef CYCLUS_AGENT_TESTS_CONNECTED
int ConnectAgentTests();
static int cyclus_agent_tests_connected = ConnectAgentTests();
#define CYCLUS_AGENT_TESTS_CONNECTED cyclus_agent_tests_connected
#endif  // CYCLUS_AGENT_TESTS_CONNECTED
INSTANTIATE_TEST_SUITE_P(MultiRegionReactor, FacilityTests,
                        ::testing::Values(&MultiRegionReactorConstructor));
INSTANTIATE_TEST_SUITE_P(MultiRegionReactor, AgentTests,
                        ::testing::Values(&MultiRegionReactorConstructor));
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
