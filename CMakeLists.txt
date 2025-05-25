cmake_minimum_required(VERSION 3.10)
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

# Include directories
include_directories(${CMAKE_SOURCE_DIR}/aml)

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
    aml/aml.c
    src/csr_reference.c
)

# Define targets
add_executable(graph500_reference_bfs_sssp
    src/bfs_reference.c
    src/sssp_reference.c
    ${COMMON_SOURCES}
    ${GENERATOR_SOURCES}
)

add_executable(graph500_reference_bfs
    src/bfs_reference.c
    ${COMMON_SOURCES}
    ${GENERATOR_SOURCES}
)

add_executable(graph500_custom_bfs
    src/bfs_custom.c
    ${COMMON_SOURCES}
    ${GENERATOR_SOURCES}
)

add_executable(graph500_custom_bfs_sssp
    src/bfs_custom.c
    src/sssp_custom.c
    ${COMMON_SOURCES}
    ${GENERATOR_SOURCES}
)

# Add SSSP definition for the SSSP target
target_compile_definitions(graph500_reference_bfs_sssp PRIVATE SSSP)

# Link libraries for all targets
foreach(target
    graph500_reference_bfs_sssp
    graph500_reference_bfs
    graph500_custom_bfs
    graph500_custom_bfs_sssp
)
    target_link_libraries(${target}
        PRIVATE
        MPI::MPI_C
        Threads::Threads
        m
    )
endforeach()