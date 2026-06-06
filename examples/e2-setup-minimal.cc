/*
 * e2-setup-minimal.cc — Phase 3/4 standalone E2 Setup test app.
 *
 * Registers a single KPM RAN function (id=2, OID="OID123", rev=2) with the
 * vendored OSC libe2sim (E2AP v3.01) and runs the E2 setup exchange against
 * the live L-release E2Term.
 *
 * Phase 4 (area C): the registered ranFunctionDefinition is now a *real*
 * E2SM-KPM v3.00 RAN-function-description (built by kpm-func-desc-v3.h: 1
 * Periodic-Report trigger style + 5 report styles + 9 measurements), replacing
 * the Phase-3 printable placeholder.  This makes the advertised function genuine
 * so a Phase-5 subscriber can parse the styles; M3 acceptance is unchanged.
 *
 * Phase 5 (subscription handling): the node now also registers a subscription
 * callback (kpm-subscription-handler.h).  When the live submgr forwards a
 * kpimon-go RICsubscriptionRequest for our KPM function, the callback admits the
 * first REPORT action and replies RICsubscriptionResponse(success) (M4).  The
 * callback is registered under func id 2 (kpimon-go subscribes func 2) AND func
 * id 0 (scp-kpimon fallback) so either subscriber is handled.  Indication
 * emission for the admitted action is deferred to Phase 6.
 *
 * Builds standalone against the installed libe2sim (no ns-3 dependency).
 * Usage: ./e2-setup-minimal <e2term-ip> <e2term-port>
 *   e.g: ./e2-setup-minimal 192.168.122.78 32222
 *
 * The run_loop() implementation (oran-e2sim/e2sim/src/base/e2sim.cpp) already:
 *   - connects SCTP to the supplied IP:port
 *   - reads ran_functions_registered and builds GlobalE2node-ID + E2setupRequest
 *   - hardcodes ranFunctionOId = "OID123", ranFunctionRev = 2
 * and emits the registered OCTET_STRING as the ranFunctionDefinition IE.
 *
 * Adapter boundary:  all v3 struct/symbol renames (ProtocolIE-Container 85P21,
 *   ric_Style_Type, QosFlowIdentifier, S-NSSAI) are owned by the installed
 *   libe2sim.  oran-interface callers see only the E2Sim API.
 */

#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <unistd.h>

/* Use installed headers — angle brackets, path-prefix included in CFLAGS. */
#include "e2sim.hpp"
#include "kpm-func-desc-v3.h"
#include "kpm-subscription-handler.h"
#include "kpm-indication-builder.h"

extern "C" {
#include "OCTET_STRING.h"
}

#include <vector>

/* The SubscriptionCallback ABI is a plain C function pointer (void(*)(E2AP_PDU*))
 * with no user-data slot, so the E2Sim instance is reached via a file-scope
 * pointer — same pattern as the OSC kpm_sim reference. */
static E2Sim *g_e2sim = nullptr;

/* RANfunctionID carried in the RICindication.  The callback ABI (void(*)(E2AP_PDU*))
 * has no func-id slot, so — like the OSC reference's global gFuncId — we use the
 * id the KPM function is registered under (=2, kpimon-go).  libe2sim's response
 * encoder hardcodes 147 in the *subscription* response, but the *indication*
 * encoder takes the func id parameterized, so kpimon-go (filters RanFunctionId==2)
 * receives the indication under the id it expects. */
static const long KPM_RAN_FUNC_ID = 2;

/* Live RICsubscriptionRequest handler (M4 + M5).  Builds the success response off
 * the thread that reads SCTP, sends it, then (Phase 6) emits a short burst of
 * E2SM-KPM v3 RICindications for the admitted action.  See the two handler
 * headers for the accept policy and the deliberate no-free of the built PDUs. */
static void
HandleKpmSubscription (E2AP_PDU_t *req)
{
    std::vector<long> accepted, rejected;
    long requestorId = 0, instanceId = 0;
    E2AP_PDU_t *resp = kpm_v3::BuildSubscriptionResponse (*g_e2sim, req, accepted, rejected,
                                                          &requestorId, &instanceId);
    if (!resp) {
        fprintf (stderr, "[e2-setup-minimal] subscription parse failed; no response sent\n");
        return;
    }
    fprintf (stderr,
             "[e2-setup-minimal] RICsubscriptionRequest handled: %zu action(s) admitted, "
             "%zu rejected -> sending RICsubscriptionResponse(success)\n",
             accepted.size (), rejected.size ());
    g_e2sim->encode_and_send_sctp_data (resp);
    /* resp is intentionally not freed (see kpm-subscription-handler.h). */

    if (accepted.empty ()) {
        fprintf (stderr, "[e2-setup-minimal] no admitted action -> no indications emitted\n");
        return;
    }

    /* Phase 6 (M5): emit a burst of dummy E2SM-KPM v3 indications for the first
     * admitted action.  Count/interval overridable for the harness; defaults give
     * kpimon-go enough reports to decode and enough wire frames to capture. */
    long action = accepted[0];
    const char *cnt_env = getenv ("KPM_IND_COUNT");
    const char *int_env = getenv ("KPM_IND_INTERVAL_MS");
    long count = cnt_env ? strtol (cnt_env, nullptr, 10) : 10;
    long interval_ms = int_env ? strtol (int_env, nullptr, 10) : 1000;
    if (count <= 0) count = 10;
    if (interval_ms < 0) interval_ms = 1000;

    fprintf (stderr,
             "[e2-setup-minimal] starting indication report loop: %ld indication(s) "
             "@ %ld ms for action %ld (func %ld)\n",
             count, interval_ms, action, KPM_RAN_FUNC_ID);
    for (long sn = 1; sn <= count; sn++) {
        kpm_v3::SendIndication (*g_e2sim, requestorId, instanceId, KPM_RAN_FUNC_ID, action, sn);
        if (sn < count && interval_ms > 0)
            usleep ((useconds_t) interval_ms * 1000);
    }
    fprintf (stderr, "[e2-setup-minimal] indication report loop done (%ld sent)\n", count);
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <e2term-ip> <e2term-port>\n", argv[0]);
        fprintf(stderr, "  e.g: %s 192.168.122.78 32222\n", argv[0]);
        return 1;
    }

    /* Build + APER-encode the real E2SM-KPM v3.00 RAN-function-description and
     * register it as the ranFunctionDefinition.  E2Term accepts the setup
     * regardless of descriptor content (OID123, hardcoded in run_loop, governs
     * acceptance), but the genuine descriptor is required for Phase-5 subscribe. */
    E2SM_KPM_RANfunction_Description_t *kpm_desc =
        (E2SM_KPM_RANfunction_Description_t*)calloc(1, sizeof(E2SM_KPM_RANfunction_Description_t));
    kpm_v3::FillKpmFunctionDescription(kpm_desc);

    uint8_t *desc_buf = nullptr;
    long desc_len = kpm_v3::EncodeKpmFunctionDescription(kpm_desc, &desc_buf);
    if (desc_len <= 0) {
        fprintf(stderr, "Error: failed to encode E2SM-KPM v3.00 descriptor (%ld)\n", desc_len);
        return 1;
    }
    fprintf(stderr, "[e2-setup-minimal] E2SM-KPM v3.00 descriptor: %ld bytes\n", desc_len);

    OCTET_STRING_t *func_desc = (OCTET_STRING_t*)calloc(1, sizeof(OCTET_STRING_t));
    func_desc->buf  = desc_buf;          /* APER bytes own buffer (freed at exit) */
    func_desc->size = desc_len;

    E2Sim e2sim;
    g_e2sim = &e2sim;
    /* RAN function id=2 mirrors the OSC kpm_sim (Phase-2 evidence) and matches
     * kpimon-go, which hardcodes funcId=2 (control/control.go) — Q3 resolved. */
    e2sim.register_e2sm(2, func_desc);

    /* Phase 5: handle RICsubscriptionRequest for our KPM function (M4).
     * Register under func 2 (kpimon-go) and func 0 (scp-kpimon fallback); the
     * dispatcher keys the callback off the func id carried in the request. */
    e2sim.register_subscription_callback(2, HandleKpmSubscription);
    e2sim.register_subscription_callback(0, HandleKpmSubscription);

    /* Delegate to libe2sim run_loop: connects SCTP, generates E2setupRequest
     * (GlobalE2node-ID, E2nodeComponentConfigAddition v3.01 IEs), sends it,
     * receives E2setupResponse, prints via XER to stderr, then exits or waits. */
    return e2sim.run_loop(argc, argv);
}
