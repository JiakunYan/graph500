add_library(aml SHARED)
target_include_directories(aml PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

set(GRAPH500_AML_BACKEND "mpi" CACHE STRING "Backend for AML")

if(GRAPH500_AML_BACKEND STREQUAL "mpi")
    target_sources(aml PRIVATE aml_mpi.c)
    target_link_libraries(aml PRIVATE MPI::MPI_C)
endif()

if(GRAPH500_AML_BACKEND STREQUAL "lci")
  # Try to find LCI externally
  find_package(
    LCI
    CONFIG
    PATH_SUFFIXES
    lib/cmake
    lib64/cmake)
  if(NOT LCI_FOUND)
    message(STATUS "Existing LCI installation not found. Try FetchContent.")
    include(FetchContent)
    FetchContent_Declare(
      lci
      GIT_REPOSITORY https://github.com/uiuc-hpc/lci.git
      GIT_TAG master)
    FetchContent_MakeAvailable(lci)
  endif()

  set_target_properties(aml PROPERTIES CXX_STANDARD 17)
  target_sources(aml PRIVATE aml_lci.cpp)
  target_link_libraries(aml PRIVATE LCI::lci MPI::MPI_C)

endif()
message(STATUS "GRAPH500_AML_BACKEND: ${GRAPH500_AML_BACKEND}")

install(TARGETS aml
    LIBRARY DESTINATION lib
)