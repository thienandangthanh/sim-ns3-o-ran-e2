/*
 * kpm-indication-test.cc — Phase 6 standalone correctness proof for the
 * E2SM-KPM v3.00 RICindication build path (M5 logic), with no network and no
 * live RIC.
 *
 * It exercises the exact builder + encode path the live node (e2-setup-minimal.cc)
 * runs after admitting a subscription:
 *   1. build the E2SM-KPM v3 IndicationHeader (Format1, 8-byte colletStartTime)
 *      and IndicationMessage (Format1, one MeasurementData record + PLMN/S-NSSAI
 *      label) via kpm-indication-builder.h,
 *   2. APER-encode each, then round-trip decode (must consume all bytes => valid
 *      v3.00 IEs) and assert the Format1 present tags + the 8-byte timestamp,
 *   3. wrap header+message into a proc-5 RICindication via libe2sim's
 *      generate_e2apv1_indication_request_parameterized() and round-trip decode
 *      the E2AP-PDU, asserting initiatingMessage / id-RICindication,
 *   4. XER-dump the indication for human inspection.
 *
 * Exit 0 = M5 indication build logic is correct (independent of the live capture).
 *
 * Build: same recipe as kpm-subscription-test (installed libe2sim.a + asn1_objects).
 */

#include <cstdio>
#include <cstdlib>

#include "kpm-indication-builder.h"

extern "C" {
#include "E2AP-PDU.h"
#include "InitiatingMessage.h"
#include "ProcedureCode.h"
#include "asn_application.h"
#include "aper_decoder.h"
}

/* Round-trip an APER-encoded buffer back through the decoder; returns 0 on a
 * clean full-consume decode, sets *out to the decoded struct (caller frees). */
static int
roundtrip (asn_TYPE_descriptor_t *td, uint8_t *buf, long len, void **out, const char *what)
{
  asn_dec_rval_t rv = aper_decode_complete (0, td, out, buf, len);
  if (rv.code != RC_OK || (long) rv.consumed != len)
    {
      fprintf (stderr, "[FAIL] %s round-trip decode rc=%d consumed=%zu/%ld\n", what,
               rv.code, rv.consumed, len);
      return -1;
    }
  fprintf (stderr, "[ok]   %s round-trip decode consumed all %ld bytes\n", what, len);
  return 0;
}

int
main (int argc, char *argv[])
{
  /* 1. Build the v3 IndicationHeader + IndicationMessage. */
  E2SM_KPM_IndicationHeader_t *ihead =
      (E2SM_KPM_IndicationHeader_t *) calloc (1, sizeof (E2SM_KPM_IndicationHeader_t));
  E2SM_KPM_IndicationMessage_t *imsg =
      (E2SM_KPM_IndicationMessage_t *) calloc (1, sizeof (E2SM_KPM_IndicationMessage_t));
  kpm_v3::BuildIndicationHeader (ihead);
  kpm_v3::BuildDummyIndicationMessage (imsg); /* exits 1 on constraint failure */
  fprintf (stderr, "[ok]   built v3 IndicationHeader + IndicationMessage\n");

  /* 2. APER-encode each. */
  uint8_t *hdr_buf = 0, *msg_buf = 0;
  long hdr_len = kpm_v3::EncodeToBuffer (&asn_DEF_E2SM_KPM_IndicationHeader, ihead, &hdr_buf);
  long msg_len = kpm_v3::EncodeToBuffer (&asn_DEF_E2SM_KPM_IndicationMessage, imsg, &msg_buf);
  if (hdr_len <= 0 || msg_len <= 0)
    {
      fprintf (stderr, "[FAIL] APER encode (hdr=%ld msg=%ld)\n", hdr_len, msg_len);
      return 1;
    }
  fprintf (stderr, "[ok]   encoded header=%ld B, message=%ld B\n", hdr_len, msg_len);

  /* 3. Round-trip decode each + assert v3 Format1 shape. */
  E2SM_KPM_IndicationHeader_t *hdec = 0;
  E2SM_KPM_IndicationMessage_t *mdec = 0;
  if (roundtrip (&asn_DEF_E2SM_KPM_IndicationHeader, hdr_buf, hdr_len, (void **) &hdec, "header"))
    return 2;
  if (roundtrip (&asn_DEF_E2SM_KPM_IndicationMessage, msg_buf, msg_len, (void **) &mdec, "message"))
    return 3;

  if (hdec->indicationHeader_formats.present
      != E2SM_KPM_IndicationHeader__indicationHeader_formats_PR_indicationHeader_Format1)
    {
      fprintf (stderr, "[FAIL] decoded header is not IndicationHeader-Format1\n");
      return 4;
    }
  E2SM_KPM_IndicationHeader_Format1_t *hf =
      hdec->indicationHeader_formats.choice.indicationHeader_Format1;
  if (hf->colletStartTime.size != 8)
    {
      fprintf (stderr, "[FAIL] colletStartTime is %zu bytes, expected 8 (v3 TimeStamp)\n",
               hf->colletStartTime.size);
      return 5;
    }
  fprintf (stderr, "[ok]   header is Format1 with an 8-byte colletStartTime (v3)\n");

  if (mdec->indicationMessage_formats.present
      != E2SM_KPM_IndicationMessage__indicationMessage_formats_PR_indicationMessage_Format1)
    {
      fprintf (stderr, "[FAIL] decoded message is not IndicationMessage-Format1\n");
      return 6;
    }
  E2SM_KPM_IndicationMessage_Format1_t *mf =
      mdec->indicationMessage_formats.choice.indicationMessage_Format1;
  if (mf->measData.list.count < 1 || !mf->measInfoList || mf->measInfoList->list.count < 1)
    {
      fprintf (stderr, "[FAIL] message Format1 missing measData/measInfoList\n");
      return 7;
    }
  fprintf (stderr, "[ok]   message is Format1 with %d measData record(s), %d measInfo item(s)\n",
           mf->measData.list.count, mf->measInfoList->list.count);

  /* 4. Wrap into a proc-5 RICindication + round-trip the E2AP-PDU. */
  E2Sim e2sim; /* socket-free; used only for the indication encoder */
  E2AP_PDU_t *pdu = (E2AP_PDU_t *) calloc (1, sizeof (E2AP_PDU_t));
  e2sim.generate_e2apv1_indication_request_parameterized (
      pdu, /*requestor*/ 22, /*instance*/ 6, /*ranFunc*/ 2, /*action*/ 5, /*seqNum*/ 1,
      hdr_buf, (int) hdr_len, msg_buf, (int) msg_len);

  uint8_t pdu_buf[8192] = {0};
  asn_codec_ctx_t *opt_cod = 0;
  asn_enc_rval_t er = asn_encode_to_buffer (opt_cod, ATS_ALIGNED_BASIC_PER, &asn_DEF_E2AP_PDU,
                                            pdu, pdu_buf, sizeof (pdu_buf));
  if (er.encoded <= 0 || (size_t) er.encoded > sizeof (pdu_buf))
    {
      fprintf (stderr, "[FAIL] APER encode of RICindication returned %zd\n", er.encoded);
      return 8;
    }
  fprintf (stderr, "[ok]   encoded RICindication E2AP-PDU: %zd bytes\n", er.encoded);

  E2AP_PDU_t *pdec = 0;
  if (roundtrip (&asn_DEF_E2AP_PDU, pdu_buf, er.encoded, (void **) &pdec, "RICindication"))
    return 9;
  if (pdec->present != E2AP_PDU_PR_initiatingMessage || !pdec->choice.initiatingMessage
      || pdec->choice.initiatingMessage->procedureCode != ProcedureCode_id_RICindication)
    {
      fprintf (stderr, "[FAIL] PDU is not initiatingMessage/id-RICindication\n");
      return 10;
    }
  fprintf (stderr, "[ok]   PDU is initiatingMessage / id-RICindication (proc 5)\n");

  /* 5. XER dump. */
  fprintf (stderr, "----- XER -----\n");
  xer_fprint (stderr, &asn_DEF_E2AP_PDU, pdec);

  if (hdr_buf) free (hdr_buf);
  if (msg_buf) free (msg_buf);
  ASN_STRUCT_FREE (asn_DEF_E2SM_KPM_IndicationHeader, ihead);
  ASN_STRUCT_FREE (asn_DEF_E2SM_KPM_IndicationMessage, imsg);
  ASN_STRUCT_FREE (asn_DEF_E2SM_KPM_IndicationHeader, hdec);
  ASN_STRUCT_FREE (asn_DEF_E2SM_KPM_IndicationMessage, mdec);
  ASN_STRUCT_FREE (asn_DEF_E2AP_PDU, pdec);
  fprintf (stderr, "[PASS] E2SM-KPM v3.00 RICindication build logic is correct (M5 precondition)\n");
  return 0;
}
