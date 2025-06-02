cmake_minimum_required(VERSION 3.13)
project(graph500)

cmake_policy(SET CMP0074 NEW)

# Find MPI package
find_package(MPI REQUIRED)
find_package(Threads REQUIRED)

# Set compiler flags
add_compile_definitions(
    restrict=__restrict__
    GRAPH_GENERATOR_MPI
    REUSE_CSR_FOR_VALIDATION
)

# Set optimization level
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O3")

# AML
add_subdirectory(aml)

# Define source files
set(GENERATOR_SOURCES
    generator/graph_generator.c
    generator/make_graph.c
    generator/splittable_mrg.c
    generator/utils.c
)

set(COMMON_SOURCES
    src/main.c
    src/utils.c
    src/validate.c
    src/csr_reference.c
)

# Define targets
add_executable(graph500_sssp
    src/bfs_reference.c
    src/sssp_reference.c
    ${COMMON_SOURCES}
    ${GENERATOR_SOURCES}
)

target_link_libraries(graph500_sssp
    PRIVATE
    aml
    MPI::MPI_C
    Threads::Threads
    m
)

# Add SSSP definition for the SSSP target
target_compile_definitions(graph500_sssp PRIVATE SSSP)

install(TARGETS graph500_sssp
    RUNTIME DESTINATION bin
)

add_executable(graph500_bfs
    src/bfs_reference.c
    ${COMMON_SOURCES}
    ${GENERATOR_SOURCES}
)

target_link_libraries(graph500_bfs
    PRIVATE
    aml
    MPI::MPI_C
    Threads::Threads
    m
)

install(TARGETS graph500_bfs
    RUNTIME DESTINATION bin
)