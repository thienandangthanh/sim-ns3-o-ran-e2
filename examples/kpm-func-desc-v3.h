/*
 * kpm-func-desc-v3.h — standalone E2SM-KPM v3.00 RAN-function-description builder.
 *
 * Mirrors OSC e2sim encode_kpm.cpp:encode_kpm_function_description (the source
 * that produced the Phase-2 byte-identical golden) so the ns-3 side advertises a
 * wire-valid E2SM-KPM v3.00 descriptor.  No ns-3 dependency: builds only against
 * the installed libe2sim headers (/usr/local/include/e2sim).
 *
 * Phase 4 (area C).  Used by:
 *   - kpm-func-desc-v3-test.cc   (encode/decode round-trip correctness proof)
 *   - e2-setup-minimal.cc        (advertise the real descriptor for live M3)
 *
 * v3 deltas absorbed vs Bronze v2 (RIC-ReportStyle-Item):
 *   ric_ReportIndicationHeaderFormat_Type  -> ric_IndicationHeaderFormat_Type
 *   ric_ReportIndicationMessageFormat_Type -> ric_IndicationMessageFormat_Type
 *   + ric_ActionFormat_Type   (new, required)
 *   + measInfo_Action_List    (new, required; embedded SEQUENCE OF, by value)
 *
 * Free-safety note: unlike OSC (which shares one measInfo list across all 5
 * styles -> double-free under ASN_STRUCT_FREE), each report style gets its own
 * freshly-built measInfo list.  APER output is byte-identical (memory sharing
 * does not affect encoded bytes) while ASN_STRUCT_FREE stays safe.
 */

#ifndef KPM_FUNC_DESC_V3_H
#define KPM_FUNC_DESC_V3_H

#include <cstring>
#include <cstdlib>

extern "C" {
#include "E2SM-KPM-RANfunction-Description.h"
#include "RIC-EventTriggerStyle-Item.h"
#include "RIC-ReportStyle-Item.h"
#include "MeasurementInfo-Action-List.h"
#include "MeasurementInfo-Action-Item.h"
#include "MeasurementTypeID.h"
#include "asn_application.h"
}

namespace kpm_v3 {

/* 9 performance measurements advertised by the KPM v3.00 service model.
 * Identical set + order to OSC encode_kpm.cpp (golden parity). */
static const char *const kPerfMeasurements[] = {
    "DRB.RlcSduTransmittedVolumeDL", "DRB.RlcSduTransmittedVolumeUL",
    "DRB.PerDataVolumeDLDist.Bin",   "DRB.PerDataVolumeULDist.Bin",
    "DRB.RlcPacketDropRateDLDist",   "DRB.PacketLossRateULDist",
    "L1M.DL-SS-RSRP.SSB",            "L1M.DL-SS-SINR.SSB",
    "L1M.UL-SRS-RSRP"};
static const int kNumberMeasurements = 9;

/* One report style row: type, name, the three format types, message format. */
struct ReportStyleSpec
{
  long        type;
  const char *name;
  long        actionFormat;
  long        indMsgFormat;
};

/* 5 report styles, identical to OSC golden (indHeaderFormat is always 1). */
static const ReportStyleSpec kReportStyles[] = {
    {1, "E2 Node Measurement", 1, 1},
    {2, "E2 Node Measurement for a single UE", 2, 1},
    {3, "Condition-based, UE-level E2 Node Measurement", 3, 2},
    {4, "Common Condition-based, UE-level Measurement", 4, 3},
    {5, "E2 Node Measurement for multiple UEs", 5, 3}};

/* Set an OCTET_STRING/PrintableString from a NUL-terminated C string. */
static inline void
SetOctetString (OCTET_STRING_t *os, const char *s)
{
  os->size = (int) strlen (s);
  os->buf = (uint8_t *) calloc (1, os->size ? os->size : 1);
  memcpy (os->buf, s, os->size);
}

/* Populate an (embedded) measInfo action list with the 9 KPM measurements. */
static inline void
BuildMeasInfoActionList (MeasurementInfo_Action_List_t *list)
{
  for (int i = 0; i < kNumberMeasurements; i++)
    {
      MeasurementInfo_Action_Item_t *item =
          (MeasurementInfo_Action_Item_t *) calloc (1, sizeof (MeasurementInfo_Action_Item_t));
      SetOctetString (&item->measName, kPerfMeasurements[i]);
      item->measID = (MeasurementTypeID_t *) calloc (1, sizeof (MeasurementTypeID_t));
      *item->measID = i + 1;
      ASN_SEQUENCE_ADD (&list->list, item);
    }
}

/*
 * Fill a heap E2SM-KPM v3.00 RAN-function-description.
 * Caller owns the struct; release with
 *   ASN_STRUCT_FREE_CONTENTS_ONLY(asn_DEF_E2SM_KPM_RANfunction_Description, d).
 */
static inline void
FillKpmFunctionDescription (E2SM_KPM_RANfunction_Description_t *d)
{
  ASN_STRUCT_RESET (asn_DEF_E2SM_KPM_RANfunction_Description, d);

  /* RANfunction-Name (golden parity: OID123 — see Phase-4 OID decision). */
  SetOctetString (&d->ranFunction_Name.ranFunction_ShortName, "ORAN-E2SM-KPM");
  SetOctetString (&d->ranFunction_Name.ranFunction_Description, "KPM Monitor");
  SetOctetString (&d->ranFunction_Name.ranFunction_E2SM_OID, "OID123");
  d->ranFunction_Name.ranFunction_Instance = (long *) calloc (1, sizeof (long));
  *d->ranFunction_Name.ranFunction_Instance = 1;

  /* Event-trigger style list: single "Periodic Report" (format 1). */
  RIC_EventTriggerStyle_Item_t *trigger =
      (RIC_EventTriggerStyle_Item_t *) calloc (1, sizeof (RIC_EventTriggerStyle_Item_t));
  trigger->ric_EventTriggerStyle_Type = 1;
  SetOctetString (&trigger->ric_EventTriggerStyle_Name, "Periodic Report");
  trigger->ric_EventTriggerFormat_Type = 1;

  d->ric_EventTriggerStyle_List =
      (E2SM_KPM_RANfunction_Description::E2SM_KPM_RANfunction_Description__ric_EventTriggerStyle_List *)
          calloc (1, sizeof (E2SM_KPM_RANfunction_Description::
                                 E2SM_KPM_RANfunction_Description__ric_EventTriggerStyle_List));
  ASN_SEQUENCE_ADD (&d->ric_EventTriggerStyle_List->list, trigger);

  /* Report style list: 5 styles, each with its own meas action list. */
  d->ric_ReportStyle_List =
      (E2SM_KPM_RANfunction_Description::E2SM_KPM_RANfunction_Description__ric_ReportStyle_List *)
          calloc (1, sizeof (E2SM_KPM_RANfunction_Description::
                                 E2SM_KPM_RANfunction_Description__ric_ReportStyle_List));

  for (const ReportStyleSpec &spec : kReportStyles)
    {
      RIC_ReportStyle_Item_t *style =
          (RIC_ReportStyle_Item_t *) calloc (1, sizeof (RIC_ReportStyle_Item_t));
      style->ric_ReportStyle_Type = spec.type;
      SetOctetString (&style->ric_ReportStyle_Name, spec.name);
      style->ric_ActionFormat_Type = spec.actionFormat;
      style->ric_IndicationHeaderFormat_Type = 1;
      style->ric_IndicationMessageFormat_Type = spec.indMsgFormat;
      BuildMeasInfoActionList (&style->measInfo_Action_List);
      ASN_SEQUENCE_ADD (&d->ric_ReportStyle_List->list, style);
    }
}

/*
 * Encode a filled descriptor to a freshly malloc'd APER buffer.
 * Returns encoded length (>0) and sets *out_buf (caller frees), or -1 on error.
 */
static inline long
EncodeKpmFunctionDescription (E2SM_KPM_RANfunction_Description_t *d, uint8_t **out_buf)
{
  asn_codec_ctx_t *opt_cod = 0; /* disable stack bounds checking */
  asn_encode_to_new_buffer_result_t res = asn_encode_to_new_buffer (
      opt_cod, ATS_ALIGNED_BASIC_PER, &asn_DEF_E2SM_KPM_RANfunction_Description, d);
  if (res.result.encoded < 0)
    {
      *out_buf = 0;
      return -1;
    }
  *out_buf = (uint8_t *) res.buffer;
  return res.result.encoded;
}

} // namespace kpm_v3

#endif /* KPM_FUNC_DESC_V3_H */
