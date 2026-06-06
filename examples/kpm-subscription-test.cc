/*
 * kpm-subscription-test.cc — Phase 5 standalone correctness proof for the
 * RICsubscriptionRequest -> RICsubscriptionResponse(success) path (M4 logic),
 * with no network and no live RIC.
 *
 * It exercises the exact accept/reject + response-encode code that the live node
 * (e2-setup-minimal.cc) runs when the submgr forwards a kpimon-go subscription:
 *   1. synthesize a realistic RICsubscriptionRequest via libe2sim's encoder
 *      (RICrequestID requestor=22 / instance=6, one REPORT action id=5),
 *   2. run kpm_v3::BuildSubscriptionResponse() — assert the REPORT action is
 *      admitted (1 accepted, 0 rejected),
 *   3. APER-encode the response and round-trip decode it (must consume all bytes
 *      => valid E2AP v3.01 RICsubscriptionResponse),
 *   4. assert the decoded PDU is successfulOutcome / id-RICsubscription and echoes
 *      RICrequestID 22/6 with the admitted action id 5 in RICactions-Admitted,
 *   5. XER-dump for human inspection.
 *
 * Exit 0 = M4 response logic is correct (independent of the flaky live capture).
 *
 * Build: same recipe as kpm-func-desc-v3-test (installed libe2sim.a + asn1_objects).
 */

#include <cstdio>
#include <cstdlib>
#include <vector>

#include "kpm-subscription-handler.h"

extern "C" {
#include "E2AP-PDU.h"
#include "SuccessfulOutcome.h"
#include "ProcedureCode.h"
#include "ProtocolIE-Field.h"
#include "asn_application.h"
#include "aper_decoder.h"
}

/* libe2sim's request synthesizer; declared in encode_e2apv1.hpp (not installed),
 * so forward-declare it here — the symbol lives in libe2sim.a. */
namespace encoding {
void generate_e2apv1_subscription_request (E2AP_PDU *e2ap_pdu);
}

int
main (int argc, char *argv[])
{
  /* 1. Synthesize a realistic subscription request (1 REPORT action, id=5). */
  E2AP_PDU_t *req = (E2AP_PDU_t *) calloc (1, sizeof (E2AP_PDU_t));
  encoding::generate_e2apv1_subscription_request (req);
  fprintf (stderr, "[ok]   synthesized RICsubscriptionRequest\n");

  /* 2. Run the real accept/reject + response-build logic (no send). */
  E2Sim e2sim; /* used only for its (socket-free) response encoder */
  std::vector<long> accepted, rejected;
  E2AP_PDU_t *resp = kpm_v3::BuildSubscriptionResponse (e2sim, req, accepted, rejected);
  if (!resp)
    {
      fprintf (stderr, "[FAIL] BuildSubscriptionResponse returned nullptr\n");
      return 1;
    }
  fprintf (stderr, "[ok]   admitted=%zu rejected=%zu\n", accepted.size (), rejected.size ());
  if (accepted.size () != 1 || rejected.size () != 0 || accepted[0] != 5)
    {
      fprintf (stderr, "[FAIL] expected 1 admitted action id=5, 0 rejected\n");
      return 2;
    }

  /* 3. APER-encode the response, then round-trip decode.  asn_encode_to_buffer
   *    with ATS_ALIGNED_BASIC_PER returns the byte length in .encoded (same as
   *    the rest of the stack, e.g. kpm_callbacks.cpp / kpm-func-desc-v3). */
  uint8_t buf[8192] = {0};
  asn_codec_ctx_t *opt_cod = 0;
  asn_enc_rval_t er = asn_encode_to_buffer (opt_cod, ATS_ALIGNED_BASIC_PER, &asn_DEF_E2AP_PDU,
                                            resp, buf, sizeof (buf));
  if (er.encoded <= 0 || (size_t) er.encoded > sizeof (buf))
    {
      fprintf (stderr, "[FAIL] APER encode of response returned %zd\n", er.encoded);
      return 3;
    }
  long len = er.encoded;
  fprintf (stderr, "[ok]   encoded RICsubscriptionResponse: %ld bytes\n", len);

  E2AP_PDU_t *decoded = 0;
  asn_dec_rval_t rv =
      aper_decode_complete (0, &asn_DEF_E2AP_PDU, (void **) &decoded, buf, len);
  if (rv.code != RC_OK || (long) rv.consumed != len)
    {
      fprintf (stderr, "[FAIL] round-trip decode rc=%d consumed=%zu/%ld\n", rv.code,
               rv.consumed, len);
      return 4;
    }
  fprintf (stderr, "[ok]   round-trip decode consumed all %ld bytes\n", len);

  /* 4. Assert successfulOutcome / id-RICsubscription + echoed RICrequestID 22/6
   *    + admitted action id 5. */
  if (decoded->present != E2AP_PDU_PR_successfulOutcome || !decoded->choice.successfulOutcome
      || decoded->choice.successfulOutcome->procedureCode != ProcedureCode_id_RICsubscription)
    {
      fprintf (stderr, "[FAIL] response is not successfulOutcome/id-RICsubscription\n");
      return 5;
    }

  RICsubscriptionResponse_t resp_msg =
      decoded->choice.successfulOutcome->value.choice.RICsubscriptionResponse;
  RICsubscriptionResponse_IEs_t **r_ies =
      (RICsubscriptionResponse_IEs_t **) resp_msg.protocolIEs.list.array;

  bool reqIdOk = false, admittedOk = false;
  for (int i = 0; i < resp_msg.protocolIEs.list.count; i++)
    {
      RICsubscriptionResponse_IEs_t *ie = r_ies[i];
      if (ie->value.present == RICsubscriptionResponse_IEs__value_PR_RICrequestID)
        {
          RICrequestID_t id = ie->value.choice.RICrequestID;
          reqIdOk = (id.ricRequestorID == 22 && id.ricInstanceID == 6);
        }
      else if (ie->value.present
               == RICsubscriptionResponse_IEs__value_PR_RICaction_Admitted_List)
        {
          RICaction_Admitted_List_t adm = ie->value.choice.RICaction_Admitted_List;
          for (int j = 0; j < adm.list.count; j++)
            {
              RICaction_Admitted_ItemIEs_t *ai = (RICaction_Admitted_ItemIEs_t *) adm.list.array[j];
              if (ai->value.choice.RICaction_Admitted_Item.ricActionID == 5)
                admittedOk = true;
            }
        }
    }
  if (!reqIdOk)
    {
      fprintf (stderr, "[FAIL] response did not echo RICrequestID 22/6\n");
      return 6;
    }
  if (!admittedOk)
    {
      fprintf (stderr, "[FAIL] admitted action id 5 not in RICactions-Admitted\n");
      return 7;
    }
  fprintf (stderr, "[ok]   response echoes RICrequestID 22/6, admits action id 5\n");

  /* 5. XER dump. */
  fprintf (stderr, "----- XER -----\n");
  xer_fprint (stderr, &asn_DEF_E2AP_PDU, decoded);

  ASN_STRUCT_FREE (asn_DEF_E2AP_PDU, decoded);
  fprintf (stderr, "[PASS] RICsubscriptionResponse(success) logic is correct (M4 precondition)\n");
  return 0;
}
