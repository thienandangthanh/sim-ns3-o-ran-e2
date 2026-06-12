check_include_file_cxx(stdint.h HAVE_STDINT_H)
if(HAVE_STDINT_H)
    add_definitions(-DHAVE_STDINT_H)
endif()

set(examples_as_tests_sources)
if(${ENABLE_EXAMPLES})
    set(examples_as_tests_sources
        #test/oran-interface-examples-test-suite.cc
        )
endif()

#include_directories(/usr/local/include/e2sim)
#link_directories(/usr/local/lib)
#link_libraries(e2sim)

# libe2sim_shared.so carries the e2sim runtime (SCTP/encoding/messaging) but
# does NOT bundle the asn1c-generated type definitions (asn_DEF_*, xer_*, aper_*,
# asn_encode_to_new_buffer, etc.).  Those symbols live in a separate archive
# libasn1c_e2.a, compiled from e2sim/asn1c/*.c and installed to /usr/local/lib/.
#
# Link order: libasn1c_e2.a AFTER libe2sim_shared.so so the linker sees all
# undefined refs from the shared lib and resolves them from the archive.
set(E2SIM_SHARED_LIB "/usr/local/lib/libe2sim_shared.so")
set(ASN1C_E2_LIB     "/usr/local/lib/libasn1c_e2.a")
if(NOT EXISTS "${E2SIM_SHARED_LIB}")
    message(FATAL_ERROR "libe2sim_shared.so not found at ${E2SIM_SHARED_LIB}")
endif()
if(NOT EXISTS "${ASN1C_E2_LIB}")
    message(FATAL_ERROR "libasn1c_e2.a not found at ${ASN1C_E2_LIB} — run: cd e2sim/asn1c && gcc -O2 -fPIC -I. -c *.c && ar rcs /usr/local/lib/libasn1c_e2.a *.o")
endif()
set(e2sim_INCLUDE_DIRS "/usr/local/include/e2sim")
set(e2sim_LIBRARIES    "${E2SIM_SHARED_LIB}" "${ASN1C_E2_LIB}")

include_directories(${e2sim_INCLUDE_DIRS})
message(STATUS "e2sim include dirs: ${e2sim_INCLUDE_DIRS}")
message(STATUS "e2sim libraries:    ${e2sim_LIBRARIES}")

build_lib(
    LIBNAME oran-interface
    SOURCE_FILES model/oran-interface.cc
                 helper/oran-interface-helper.cc
                 model/asn1c-types.cc
                 model/function-description.cc
                 model/kpm-indication.cc
                 model/kpm-function-description.cc
                 # E2SM-RC (RAN Control): RC-2.0 asn.1 types now installed (Phase 3).
                 # ric-control-message.cc re-enabled for Phase 4 (RC receive + ACK).
                 # ric-control-function-description.cc still deferred — its v1-era
                 # RIC-ControlStyle-Item tree is absent in RC-2.0 (RANFunctionDefinition
                 # rewrite is Phase 5).
                 model/ric-control-message.cc
                 # model/ric-control-function-description.cc
                 helper/oran-interface-helper.cc
                 helper/indication-message-helper.cc
                 helper/lte-indication-message-helper.cc
                 helper/mmwave-indication-message-helper.cc
    HEADER_FILES model/oran-interface.h
                 helper/oran-interface-helper.h
                 model/asn1c-types.h
                 model/function-description.h
                 model/kpm-indication.h
                 model/kpm-function-description.h
                 # E2SM-RC: ric-control-message.h re-enabled (Phase 4);
                 # ric-control-function-description.h deferred (Phase 5).
                 model/ric-control-message.h
                 # model/ric-control-function-description.h
                 helper/indication-message-helper.h
                 helper/lte-indication-message-helper.h
                 helper/mmwave-indication-message-helper.h
    LIBRARIES_TO_LINK 
                    ${libcore}
                    ${e2sim_LIBRARIES}
)

