check_include_file_cxx(stdint.h HAVE_STDINT_H)
if(HAVE_STDINT_H)
    add_definitions(-DHAVE_STDINT_H)
endif()

# No asn1c / e2sim dependency: E2 runs in an out-of-process agent that ns-3
# feeds through the ns3-e2-ipc emitter.

# Commit SHAs reported in the ns3-e2-ipc hello (overridable at run time with
# NS3_E2_IPC_NS3_SHA / NS3_E2_IPC_ORAN_INTERFACE_SHA; "unknown" if unavailable).
find_package(Git QUIET)
function(oran_interface_git_sha dir out_var)
    set(sha "")
    if(GIT_FOUND)
        execute_process(
            COMMAND ${GIT_EXECUTABLE} -C ${dir} rev-parse HEAD
            OUTPUT_VARIABLE sha
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )
    endif()
    set(${out_var} "${sha}" PARENT_SCOPE)
endfunction()
oran_interface_git_sha(${PROJECT_SOURCE_DIR} E2_IPC_NS3_SHA)
oran_interface_git_sha(${CMAKE_CURRENT_SOURCE_DIR} E2_IPC_ORAN_INTERFACE_SHA)
message(STATUS "oran-interface: ns-3 ${E2_IPC_NS3_SHA}, oran-interface ${E2_IPC_ORAN_INTERFACE_SHA}")
add_definitions(
    -DE2_IPC_NS3_SHA="${E2_IPC_NS3_SHA}"
    -DE2_IPC_ORAN_INTERFACE_SHA="${E2_IPC_ORAN_INTERFACE_SHA}"
)

build_lib(
    LIBNAME oran-interface
    SOURCE_FILES model/e2-ipc-emitter.cc
                 model/e2-ipc-json-writer.cc
                 model/e2-ipc-message-encoder.cc
                 model/e2-ipc-tcp-client.cc
                 model/kpm-indication.cc
                 model/l3-rrc-measurements.cc
                 helper/oran-interface-helper.cc
                 helper/indication-message-helper.cc
                 helper/lte-indication-message-helper.cc
                 helper/mmwave-indication-message-helper.cc
    HEADER_FILES model/oran-interface.h
                 model/e2-ipc-emitter.h
                 model/e2-ipc-json-writer.h
                 model/e2-ipc-message-encoder.h
                 model/e2-ipc-tcp-client.h
                 model/kpm-indication.h
                 model/l3-rrc-measurements.h
                 helper/oran-interface-helper.h
                 helper/indication-message-helper.h
                 helper/lte-indication-message-helper.h
                 helper/mmwave-indication-message-helper.h
    LIBRARIES_TO_LINK
                    ${libcore}
)
