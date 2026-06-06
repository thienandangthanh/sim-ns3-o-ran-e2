# CMakeLists-standalone-e2-setup.cmake — Standalone build for the Phase-3
# minimal E2 Setup test app (no ns-3 dependency).
#
# Usage (on oran-kvm-2):
#   mkdir -p /tmp/ns3-e2-phase3/build
#   cmake -S /home/uc/ns-O-RAN-installation/oran-interface/examples \
#         -B /tmp/ns3-e2-phase3/build \
#         -DCMAKE_BUILD_TYPE=RelWithDebInfo \
#         -f CMakeLists-standalone-e2-setup.cmake
#   make -C /tmp/ns3-e2-phase3/build -j$(nproc)
#
# The installed e2sim package (from Phase 2) provides:
#   headers  : /usr/local/include/e2sim/
#   shared   : /usr/local/lib/libe2sim_shared.so
#   static   : /usr/local/lib/libe2sim.a
#
# We link the shared lib so LD_LIBRARY_PATH=/usr/local/lib is needed at runtime
# (or add a rpath, which cmake handles via CMAKE_INSTALL_RPATH).

cmake_minimum_required(VERSION 3.10)
project(e2-setup-minimal CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# e2sim install prefix (installed in Phase 2 via dpkg)
set(E2SIM_INCLUDE_DIR "/usr/local/include/e2sim")
set(E2SIM_LIB_DIR     "/usr/local/lib")

include_directories(${E2SIM_INCLUDE_DIR})
link_directories(${E2SIM_LIB_DIR})

add_executable(e2-setup-minimal
    ${CMAKE_CURRENT_SOURCE_DIR}/e2-setup-minimal.cc
)

target_link_libraries(e2-setup-minimal
    e2sim_shared   # /usr/local/lib/libe2sim_shared.so
    sctp
)

# Embed rpath so the binary runs without LD_LIBRARY_PATH
set_target_properties(e2-setup-minimal PROPERTIES
    BUILD_RPATH "${E2SIM_LIB_DIR}"
    INSTALL_RPATH "${E2SIM_LIB_DIR}"
)
