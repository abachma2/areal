# ------------------- Add all Concrete Agents ----------------------------

CONFIGURE_FILE(areal_version.h.in "${CMAKE_CURRENT_SOURCE_DIR}/areal_version.h" @ONLY)

SET(CYCLUS_CUSTOM_HEADERS "areal_version.h")

USE_CYCLUS("areal" "tworegionreactor")


INSTALL_CYCLUS_MODULE("areal" "" "NONE")

SET(TestSource ${areal_TEST_CC} PARENT_SCOPE)

# install header files
FILE(GLOB h_files "${CMAKE_CURRENT_SOURCE_DIR}/*.h")
