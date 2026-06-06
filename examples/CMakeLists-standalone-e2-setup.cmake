# CMakeLists-standalone-e2-setup.cmake — Standalone build (no ns-3) for the
# Phase 3/4/5 minimal E2 node test apps + offline correctness tests.
#
# Usage (on oran-kvm-2):
#   rm -rf /tmp/ns3-e2-build && mkdir /tmp/ns3-e2-build && cd /tmp/ns3-e2-build
#   cp /home/uc/ns-O-RAN-installation/oran-interface/examples/CMakeLists-standalone-e2-setup.cmake CMakeLists.txt
#   cmake . -DCMAKE_BUILD_TYPE=RelWithDebInfo
#   make -j$(nproc)
#
# The installed e2sim package (from Phase 2) provides:
#   headers : /usr/local/include/e2sim/
#   static  : /usr/local/lib/libe2sim.a      (linked here)
#   shared  : /usr/local/lib/libe2sim_shared.so
# The generated ASN.1 C is compiled into an OBJECT lib (asn1_objects) from the
# checked-out e2sim tree so the offline tests resolve asn_DEF_* + aper_* symbols.

cmake_minimum_required(VERSION 3.10)
project(ns3-e2-standalone CXX C)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(REPO_DIR  "/home/uc/ns-O-RAN-installation")
set(ASN1C_DIR "${REPO_DIR}/oran-e2sim/e2sim/asn1c")
set(EX_DIR    "${REPO_DIR}/oran-interface/examples")
set(E2SIM_INCLUDE_DIR "/usr/local/include/e2sim")
set(E2SIM_LIB_DIR     "/usr/local/lib")

file(GLOB ASN1C_SOURCES "${ASN1C_DIR}/*.c")
add_library(asn1_objects OBJECT ${ASN1C_SOURCES})
target_include_directories(asn1_objects PUBLIC ${ASN1C_DIR})

# Common link recipe for every target below (asn1_objects listed twice to satisfy
# the static-lib ordering; pthread + sctp required by libe2sim).
macro(add_e2_target name src)
    add_executable(${name} ${EX_DIR}/${src})
    target_include_directories(${name} PRIVATE ${E2SIM_INCLUDE_DIR} ${ASN1C_DIR} ${EX_DIR})
    target_link_libraries(${name} asn1_objects ${E2SIM_LIB_DIR}/libe2sim.a asn1_objects pthread sctp)
endmacro()

# --- live E2 node app: E2 Setup (M2/M3) + subscription handling (M4) ---
add_e2_target(e2-setup-minimal      e2-setup-minimal.cc)

# --- offline correctness proofs (no network) ---
add_e2_target(kpm-func-desc-v3-test kpm-func-desc-v3-test.cc)  # Phase 4 (M3 precondition)
add_e2_target(kpm-subscription-test kpm-subscription-test.cc)  # Phase 5 (M4 precondition)
