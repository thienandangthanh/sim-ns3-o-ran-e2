/*
 * e2-setup-minimal.cc — Phase 3 standalone E2 Setup test app.
 *
 * Registers a single KPM RAN function (id=2, OID="OID123", rev=2) with the
 * vendored OSC libe2sim (E2AP v3.01) and runs the E2 setup exchange against
 * the live L-release E2Term.
 *
 * Builds standalone against the installed libe2sim (no ns-3 dependency).
 * Usage: ./e2-setup-minimal <e2term-ip> <e2term-port>
 *   e.g: ./e2-setup-minimal 192.168.122.78 32222
 *
 * The run_loop() implementation (oran-e2sim/e2sim/src/base/e2sim.cpp) already:
 *   - connects SCTP to the supplied IP:port
 *   - reads ran_functions_registered and builds GlobalE2node-ID + E2setupRequest
 *   - hardcodes ranFunctionOId = "OID123", ranFunctionRev = 2
 * So we only need to register a dummy function descriptor to get a non-empty list.
 *
 * Adapter boundary:  all v3 struct/symbol renames (ProtocolIE-Container 85P21,
 *   ric_Style_Type, QosFlowIdentifier, S-NSSAI) are owned by the installed
 *   libe2sim.  oran-interface callers see only the E2Sim API.
 */

#include <cstring>
#include <cstdlib>
#include <cstdio>

/* Use installed headers — angle brackets, path-prefix included in CFLAGS. */
#include "e2sim.hpp"

extern "C" {
#include "OCTET_STRING.h"
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <e2term-ip> <e2term-port>\n", argv[0]);
        fprintf(stderr, "  e.g: %s 192.168.122.78 32222\n", argv[0]);
        return 1;
    }

    /* Build a dummy E2SM function descriptor buffer (single NUL byte is enough;
     * the E2term accepts the setup regardless of the descriptor content — it is
     * the OID string that governs acceptance, and OID123 is hardcoded in
     * libe2sim's run_loop).  A real KPM v3.00 descriptor is ported in Phase 4. */
    OCTET_STRING_t *func_desc = (OCTET_STRING_t*)calloc(1, sizeof(OCTET_STRING_t));
    const char dummy_desc[] = "ORAN-E2SM-KPM";   /* printable; assert-pcap checks ASCII */
    func_desc->buf  = (uint8_t*)calloc(1, sizeof(dummy_desc));
    func_desc->size = sizeof(dummy_desc) - 1;    /* exclude NUL */
    memcpy(func_desc->buf, dummy_desc, func_desc->size);

    E2Sim e2sim;
    /* RAN function id=2 mirrors the OSC kpm_sim (Phase-2 evidence).
     * Q3 (id 2 vs 0 for scp-kpimon) deferred to Phase 5. */
    e2sim.register_e2sm(2, func_desc);

    /* Delegate to libe2sim run_loop: connects SCTP, generates E2setupRequest
     * (GlobalE2node-ID, E2nodeComponentConfigAddition v3.01 IEs), sends it,
     * receives E2setupResponse, prints via XER to stderr, then exits or waits. */
    return e2sim.run_loop(argc, argv);
}
