/*
 * kpm-indication-builder.h — Phase 6 (KPM indication messages) standalone,
 * no-ns-3 E2SM-KPM v3.00 RICindication builder for the registered KPM RAN
 * function.
 *
 * Mirrors OSC encode_kpm.cpp field-for-field for the two pieces that travel in a
 * RICindication: the E2SM-KPM IndicationHeader (Format1) and IndicationMessage
 * (Format1).  We port the *minimal self-contained* shapes the live kpimon-go
 * accepts:
 *   - Header  : sender name/type/vendor strings + 8-byte colletStartTime
 *               (v3.00 TimeStamp is an OCTET STRING; OSC fills 8 ASCII bytes —
 *               we keep the same 8-byte width so the wire shape matches the
 *               Phase-1 golden / OSC reference).
 *   - Message : IndicationMessage-Format1 with one MeasurementData record
 *               (integer value) and one MeasurementInfoItem labelled with PLMN +
 *               S-NSSAI (the v3 cell-measurement "style 1" shape from
 *               encode_kpm.cpp:cell_meas_kpm_report_indication_message_style_1).
 *               This is the smallest PDU that validates against
 *               asn_DEF_E2SM_KPM_IndicationMessage and decodes in kpimon-go.
 *
 * Design split (same rationale as kpm-subscription-handler.h):
 *   - BuildIndicationHeader() / BuildDummyIndicationMessage() are PURE — they fill
 *     a caller-owned ASN.1 struct and do not touch the socket, so the Phase-6
 *     offline test (kpm-indication-test.cc) can APER-encode + round-trip decode
 *     them deterministically with no live RIC.
 *   - SendIndication() is the live path: it builds both pieces, APER-encodes each
 *     to a fresh buffer, wraps them in a proc-5 RICindication via libe2sim's
 *     generate_e2apv1_indication_request_parameterized(), and sends over SCTP.
 *
 * M5 scope: dummy (static) values.  Real ns-3 metric mapping (M6) is Phase 7.
 */

#ifndef KPM_INDICATION_BUILDER_H
#define KPM_INDICATION_BUILDER_H

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "e2sim.hpp"

extern "C" {
#include "E2AP-PDU.h"
#include "E2SM-KPM-IndicationHeader.h"
#include "E2SM-KPM-IndicationHeader-Format1.h"
#include "E2SM-KPM-IndicationMessage.h"
#include "E2SM-KPM-IndicationMessage-Format1.h"
#include "TimeStamp.h"
#include "MeasurementData.h"
#include "MeasurementDataItem.h"
#include "MeasurementRecord.h"
#include "MeasurementRecordItem.h"
#include "MeasurementInfoList.h"
#include "MeasurementInfoItem.h"
#include "LabelInfoList.h"
#include "LabelInfoItem.h"
#include "MeasurementLabel.h"
#include "MeasurementType.h"
#include "GranularityPeriod.h"
#include "S-NSSAI.h"
#include "SD.h"
#include "PLMNIdentity.h"
}

namespace kpm_v3 {

/* Default v3.00 collection start time: 8 ASCII bytes, matching OSC's reference
 * sim ("20200613").  The TimeStamp IE is an OCTET STRING, so the 8-byte width —
 * not the calendar value — is what makes the v3 header shape (was 4 bytes in
 * v2) wire-compatible with the decoder. */
static const char KPM_TIMESTAMP_8B[] = "20200613";

/*
 * Fill a caller-allocated E2SM_KPM_IndicationHeader_t with an IndicationHeader-
 * Format1 (sender strings + 8-byte colletStartTime).  Mirrors
 * encode_kpm.cpp:kpm_report_indication_header_initialized (the fields the OSC
 * builder actually populates; the unused node-id args of that signature are
 * dropped here per YAGNI — the live submgr/kpimon-go only need the timestamp +
 * sender identity).
 */
inline void
BuildIndicationHeader (E2SM_KPM_IndicationHeader_t *ihead)
{
  E2SM_KPM_IndicationHeader_Format1_t *fmt =
      (E2SM_KPM_IndicationHeader_Format1_t *) calloc (
          1, sizeof (E2SM_KPM_IndicationHeader_Format1_t));

  /* OCTET/PrintableString are length-prefixed (size member), so the buffers are
   * sized to strlen() with no NUL terminator — intentional, matches OSC. */
  const char *sender = "ORANSim";
  fmt->senderName = (PrintableString_t *) calloc (1, sizeof (PrintableString_t));
  fmt->senderName->buf = (uint8_t *) calloc (1, strlen (sender));
  memcpy (fmt->senderName->buf, sender, strlen (sender));
  fmt->senderName->size = strlen (sender);

  const char *stype = "simulator";
  fmt->senderType = (PrintableString_t *) calloc (1, sizeof (PrintableString_t));
  fmt->senderType->buf = (uint8_t *) calloc (1, strlen (stype));
  memcpy (fmt->senderType->buf, stype, strlen (stype));
  fmt->senderType->size = strlen (stype);

  const char *vendor = "ORAN-SC";
  fmt->vendorName = (PrintableString_t *) calloc (1, sizeof (PrintableString_t));
  fmt->vendorName->buf = (uint8_t *) calloc (1, strlen (vendor));
  memcpy (fmt->vendorName->buf, vendor, strlen (vendor));
  fmt->vendorName->size = strlen (vendor);

  size_t ts_len = strlen (KPM_TIMESTAMP_8B); /* 8 bytes */
  fmt->colletStartTime.buf = (uint8_t *) calloc (1, ts_len);
  fmt->colletStartTime.size = ts_len;
  memcpy (fmt->colletStartTime.buf, KPM_TIMESTAMP_8B, ts_len);

  ihead->indicationHeader_formats.present =
      E2SM_KPM_IndicationHeader__indicationHeader_formats_PR_indicationHeader_Format1;
  ihead->indicationHeader_formats.choice.indicationHeader_Format1 = fmt;
}

/*
 * Fill a caller-allocated E2SM_KPM_IndicationMessage_t with the minimal valid v3
 * IndicationMessage-Format1: one MeasurementData record (integer value) + one
 * MeasurementInfoItem labelled with PLMN + S-NSSAI (SST/SD).  Mirrors
 * encode_kpm.cpp:cell_meas_kpm_report_indication_message_style_1_initialized but
 * keeps only the self-contained dummy fields (no PRB/5QI args — those map to
 * real ns-3 metrics in M6/Phase 7).  Aborts (exit 1) if the assembled message
 * fails ASN.1 constraint validation, exactly like the OSC reference.
 */
inline void
BuildDummyIndicationMessage (E2SM_KPM_IndicationMessage_t *imsg)
{
  /* --- MeasurementData: one record item carrying the integer value 1 --- */
  MeasurementRecordItem_t *recItem =
      (MeasurementRecordItem_t *) calloc (1, sizeof (MeasurementRecordItem_t));
  recItem->present = MeasurementRecordItem_PR_integer;
  recItem->choice.integer = 1;

  MeasurementRecord_t *record =
      (MeasurementRecord_t *) calloc (1, sizeof (MeasurementRecord_t));
  ASN_SEQUENCE_ADD (&record->list, recItem);

  MeasurementDataItem_t *dataItem =
      (MeasurementDataItem_t *) calloc (1, sizeof (MeasurementDataItem_t));
  dataItem->measRecord = *record;
  dataItem->incompleteFlag = (long *) calloc (1, sizeof (long));
  *dataItem->incompleteFlag = 0;
  free (record); /* shallow-copied into dataItem; free the wrapper only */

  MeasurementData_t *measData =
      (MeasurementData_t *) calloc (1, sizeof (MeasurementData_t));
  ASN_SEQUENCE_ADD (&measData->list, dataItem);

  /* --- MeasurementInfoList: one item, measID=1, labelled PLMN + S-NSSAI --- */
  const char *plmnid = "747";
  const char *sst = "1";
  const char *sd = "100";

  S_NSSAI_t *snssai = (S_NSSAI_t *) calloc (1, sizeof (S_NSSAI_t));
  snssai->sST.buf = (uint8_t *) calloc (1, strlen (sst));
  snssai->sST.size = strlen (sst);
  memcpy (snssai->sST.buf, sst, strlen (sst));
  snssai->sD = (SD_t *) calloc (1, sizeof (SD_t));
  snssai->sD->buf = (uint8_t *) calloc (1, strlen (sd));
  snssai->sD->size = strlen (sd);
  memcpy (snssai->sD->buf, sd, strlen (sd));

  PLMNIdentity_t *plmn = (PLMNIdentity_t *) calloc (1, sizeof (PLMNIdentity_t));
  plmn->buf = (uint8_t *) calloc (1, strlen (plmnid));
  plmn->size = strlen (plmnid);
  memcpy (plmn->buf, plmnid, strlen (plmnid));

  MeasurementLabel_t *label =
      (MeasurementLabel_t *) calloc (1, sizeof (MeasurementLabel_t));
  label->plmnID = plmn;
  label->sliceID = snssai;

  LabelInfoItem_t *labelItem =
      (LabelInfoItem_t *) calloc (1, sizeof (LabelInfoItem_t));
  labelItem->measLabel = *label;
  free (label);

  LabelInfoList_t *labelList =
      (LabelInfoList_t *) calloc (1, sizeof (LabelInfoList_t));
  ASN_SEQUENCE_ADD (&labelList->list, labelItem);

  MeasurementInfoItem_t *infoItem =
      (MeasurementInfoItem_t *) calloc (1, sizeof (MeasurementInfoItem_t));
  infoItem->labelInfoList = *labelList;
  free (labelList);
  infoItem->measType.present = MeasurementType_PR_measID;
  infoItem->measType.choice.measID = 1;

  MeasurementInfoList_t *infoList =
      (MeasurementInfoList_t *) calloc (1, sizeof (MeasurementInfoList_t));
  ASN_SEQUENCE_ADD (&infoList->list, infoItem);

  /* --- assemble Format1 --- */
  E2SM_KPM_IndicationMessage_Format1_t *fmt =
      (E2SM_KPM_IndicationMessage_Format1_t *) calloc (
          1, sizeof (E2SM_KPM_IndicationMessage_Format1_t));
  fmt->granulPeriod = (GranularityPeriod_t *) calloc (1, sizeof (GranularityPeriod_t));
  *fmt->granulPeriod = 1;
  fmt->measData = *measData;
  free (measData);
  fmt->measInfoList = infoList;

  imsg->indicationMessage_formats.present =
      E2SM_KPM_IndicationMessage__indicationMessage_formats_PR_indicationMessage_Format1;
  imsg->indicationMessage_formats.choice.indicationMessage_Format1 = fmt;

  char err_buf[300] = {0};
  size_t err_len = sizeof (err_buf);
  if (asn_check_constraints (&asn_DEF_E2SM_KPM_IndicationMessage, imsg, err_buf, &err_len))
    {
      fprintf (stderr, "[ind] IndicationMessage constraint check failed: %s\n", err_buf);
      exit (1);
    }
}

/*
 * Encode a filled ASN.1 struct to a freshly malloc'd APER buffer.
 * Returns encoded length (>0) and sets *out_buf (caller frees), or -1 on error.
 * Mirrors EncodeKpmFunctionDescription (kpm-func-desc-v3.h).
 */
inline long
EncodeToBuffer (asn_TYPE_descriptor_t *td, void *sptr, uint8_t **out_buf)
{
  asn_codec_ctx_t *opt_cod = 0;
  asn_encode_to_new_buffer_result_t res =
      asn_encode_to_new_buffer (opt_cod, ATS_ALIGNED_BASIC_PER, td, sptr);
  if (res.result.encoded < 0)
    {
      *out_buf = 0;
      return -1;
    }
  *out_buf = (uint8_t *) res.buffer;
  return res.result.encoded;
}

/*
 * Live path: build a dummy E2SM-KPM v3 indication (header + message), wrap it in
 * a proc-5 RICindication for the given subscription, and send over SCTP.  The
 * RANfunctionID is the function the subscription was registered under (=2 for
 * kpimon-go); seqNum increments per report (caller-supplied).
 *
 * Returns 0 on success, -1 on an encode failure (nothing sent).  The E2AP_PDU is
 * built by libe2sim's encoder using shallow copies, so it is intentionally NOT
 * freed — same no-free rationale as kpm-subscription-handler.h.
 */
inline int
SendIndication (E2Sim &e2sim, long requestorId, long instanceId, long ranFuncId,
                long actionId, long seqNum)
{
  E2SM_KPM_IndicationHeader_t *ihead =
      (E2SM_KPM_IndicationHeader_t *) calloc (1, sizeof (E2SM_KPM_IndicationHeader_t));
  E2SM_KPM_IndicationMessage_t *imsg =
      (E2SM_KPM_IndicationMessage_t *) calloc (1, sizeof (E2SM_KPM_IndicationMessage_t));
  BuildIndicationHeader (ihead);
  BuildDummyIndicationMessage (imsg);

  uint8_t *hdr_buf = 0;
  uint8_t *msg_buf = 0;
  long hdr_len = EncodeToBuffer (&asn_DEF_E2SM_KPM_IndicationHeader, ihead, &hdr_buf);
  long msg_len = EncodeToBuffer (&asn_DEF_E2SM_KPM_IndicationMessage, imsg, &msg_buf);

  int rc = 0;
  if (hdr_len <= 0 || msg_len <= 0)
    {
      fprintf (stderr, "[ind] APER encode failed (hdr=%ld msg=%ld); skipping send\n",
               hdr_len, msg_len);
      rc = -1;
    }
  else
    {
      E2AP_PDU_t *pdu = (E2AP_PDU_t *) calloc (1, sizeof (E2AP_PDU_t));
      e2sim.generate_e2apv1_indication_request_parameterized (
          pdu, requestorId, instanceId, ranFuncId, actionId, seqNum,
          hdr_buf, (int) hdr_len, msg_buf, (int) msg_len);
      e2sim.encode_and_send_sctp_data (pdu);
      fprintf (stderr,
               "[ind] sent RICindication sn=%ld (req=%ld/%ld func=%ld action=%ld, "
               "hdr=%ld B msg=%ld B)\n",
               seqNum, requestorId, instanceId, ranFuncId, actionId, hdr_len, msg_len);
    }

  if (hdr_buf) free (hdr_buf);
  if (msg_buf) free (msg_buf);
  ASN_STRUCT_FREE (asn_DEF_E2SM_KPM_IndicationHeader, ihead);
  ASN_STRUCT_FREE (asn_DEF_E2SM_KPM_IndicationMessage, imsg);
  return rc;
}

} // namespace kpm_v3

#endif /* KPM_INDICATION_BUILDER_H */
