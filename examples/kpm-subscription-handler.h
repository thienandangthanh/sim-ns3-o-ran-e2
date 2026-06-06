/*
 * kpm-subscription-handler.h — Phase 5 (subscription handling) standalone,
 * no-ns-3 RICsubscriptionRequest handler for the E2SM-KPM v3.00 RAN function.
 *
 * Mirrors OSC kpm_callbacks.cpp:callback_kpm_subscription_request field-for-field
 * (the reference that drives the live L-release submgr), MINUS the indication
 * report loop (deferred to Phase 6).  The decode/accept logic is identical so the
 * RICsubscriptionResponse is wire-compatible with what kpimon-go expects.
 *
 * Design split (vs the OSC reference, which inlines everything in the callback):
 *   - BuildSubscriptionResponse() is PURE — it parses the request and builds the
 *     RICsubscriptionResponse(success) PDU but does NOT touch the SCTP socket.
 *     This lets the Phase-5 offline correctness test (kpm-subscription-test.cc)
 *     exercise the exact accept/reject + response-encode path with no network.
 *   - The live SubscriptionCallback wrapper (in e2-setup-minimal.cc) calls this,
 *     then encode_and_send_sctp_data() on the returned PDU.
 *
 * Accept policy (matches OSC + the plan): admit the FIRST action whose type is
 * REPORT; reject every other action.  ActionDefinition Formats 1-5 are tolerated
 * implicitly — we key only off ricActionType, never decoding the action body, so
 * any format is accepted without a decode failure (Phase-05 requirement).
 *
 * Response quirk (inherited from libe2sim's encoder): RANfunctionID in the
 * response is hardcoded to 147; submgr correlates the response by RICrequestID,
 * not RANfunctionID, so this is accepted in practice (proven by the OSC sim).
 */

#ifndef KPM_SUBSCRIPTION_HANDLER_H
#define KPM_SUBSCRIPTION_HANDLER_H

#include <cstdio>
#include <cstdlib>
#include <vector>

#include "e2sim.hpp"

extern "C" {
#include "E2AP-PDU.h"
#include "InitiatingMessage.h"
#include "RICsubscriptionRequest.h"
#include "RICsubscriptionResponse.h"
#include "RICactionType.h"
#include "ProtocolIE-Field.h"
#include "ProtocolIE-SingleContainer.h"
}

namespace kpm_v3 {

/*
 * Parse a decoded RICsubscriptionRequest, fill `accepted`/`rejected` with the
 * admitted/refused RIC action IDs (first REPORT admitted, rest rejected), and
 * build a RICsubscriptionResponse(success) echoing the request's RICrequestID.
 *
 * Returns a heap E2AP_PDU the caller owns; nullptr if the PDU is malformed or
 * carries no RICrequestID.  Does NOT send.  The returned PDU is built by
 * libe2sim's encoder, which uses shallow struct copies internally — the live
 * caller mirrors the OSC reference and does NOT ASN_STRUCT_FREE it (a tiny,
 * per-subscription leak is preferred over a double-free on the admitted list).
 */
inline E2AP_PDU_t *
BuildSubscriptionResponse (E2Sim &e2sim, E2AP_PDU_t *sub_req_pdu,
                           std::vector<long> &accepted, std::vector<long> &rejected)
{
  if (!sub_req_pdu || sub_req_pdu->present != E2AP_PDU_PR_initiatingMessage
      || !sub_req_pdu->choice.initiatingMessage)
    {
      fprintf (stderr, "[sub] malformed PDU (expected an initiatingMessage)\n");
      return nullptr;
    }

  RICsubscriptionRequest_t orig_req =
      sub_req_pdu->choice.initiatingMessage->value.choice.RICsubscriptionRequest;

  long reqRequestorId = 0;
  long reqInstanceId = 0;
  bool haveReqId = false;

  int count = orig_req.protocolIEs.list.count;
  RICsubscriptionRequest_IEs_t **ies =
      (RICsubscriptionRequest_IEs_t **) orig_req.protocolIEs.list.array;

  for (int i = 0; i < count; i++)
    {
      RICsubscriptionRequest_IEs_t *next_ie = ies[i];
      switch (next_ie->value.present)
        {
        case RICsubscriptionRequest_IEs__value_PR_RICrequestID:
          {
            RICrequestID_t reqId = next_ie->value.choice.RICrequestID;
            reqRequestorId = reqId.ricRequestorID;
            reqInstanceId = reqId.ricInstanceID;
            haveReqId = true;
            break;
          }
        case RICsubscriptionRequest_IEs__value_PR_RICsubscriptionDetails:
          {
            /* Trigger definition is ignored (KPM Periodic Report only).  Walk
             * the action list: first REPORT action admitted, rest rejected. */
            RICsubscriptionDetails_t det = next_ie->value.choice.RICsubscriptionDetails;
            RICactions_ToBeSetup_List_t actionList = det.ricAction_ToBeSetup_List;
            int actionCount = actionList.list.count;
            RICaction_ToBeSetup_ItemIEs_t **item_array =
                (RICaction_ToBeSetup_ItemIEs_t **) actionList.list.array;

            bool foundReport = false;
            for (int j = 0; j < actionCount; j++)
              {
                RICaction_ToBeSetup_ItemIEs_t *it = item_array[j];
                /* Guard the union tag before reading the item body (defensive;
                 * a conformant decode always sets this variant). */
                if (!it
                    || it->value.present
                           != RICaction_ToBeSetup_ItemIEs__value_PR_RICaction_ToBeSetup_Item)
                  continue;
                RICactionID_t aid = it->value.choice.RICaction_ToBeSetup_Item.ricActionID;
                RICactionType_t at = it->value.choice.RICaction_ToBeSetup_Item.ricActionType;
                if (!foundReport && at == RICactionType_report)
                  {
                    accepted.push_back (aid);
                    foundReport = true;
                  }
                else
                  {
                    rejected.push_back (aid);
                  }
              }
            break;
          }
        default:
          break;
        }
    }

  if (!haveReqId)
    {
      fprintf (stderr, "[sub] no RICrequestID IE in request — cannot respond\n");
      return nullptr;
    }
  if (accepted.empty ())
    {
      fprintf (stderr, "[sub] no REPORT action to admit (rejected=%zu)\n", rejected.size ());
    }

  E2AP_PDU_t *resp = (E2AP_PDU_t *) calloc (1, sizeof (E2AP_PDU_t));
  long *acc = accepted.empty () ? nullptr : &accepted[0];
  long *rej = rejected.empty () ? nullptr : &rejected[0];
  e2sim.generate_e2apv1_subscription_response_success (
      resp, acc, rej, (int) accepted.size (), (int) rejected.size (), reqRequestorId,
      reqInstanceId);
  return resp;
}

} // namespace kpm_v3

#endif /* KPM_SUBSCRIPTION_HANDLER_H */
