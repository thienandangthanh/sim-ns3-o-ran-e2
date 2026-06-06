# All v2 example binaries excluded from build:
#   - v2-only examples use headers/types absent in v3 asn1c install
#   - v3 kpm examples have a pre-existing linker issue (libe2sim symbols not
#     propagated to example binaries via liboran-interface SHARED linkage)
#   - The reference .cc files are kept in-tree for documentation purposes only
# Re-enable individual entries after fixing the libe2sim link propagation.
set(examples
  kpm-v3-model-proof   # Phase-7 offline structural proof of the ported model path
)
foreach(
  example
  ${examples}
)
  build_lib_example(
    NAME ${example}
    SOURCE_FILES ${example}.cc
    # Link the e2sim runtime + asn1c archive explicitly so asn_DEF_* / encode
    # symbols resolve in the example binary (liboran-interface SHARED does not
    # re-export them). ${ASN1C_E2_LIB}/${E2SIM_SHARED_LIB} set in ../CMakeLists.txt.
    LIBRARIES_TO_LINK ${liboran-interface} ${E2SIM_SHARED_LIB} ${ASN1C_E2_LIB}
  )
endforeach()