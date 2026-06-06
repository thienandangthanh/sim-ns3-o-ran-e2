/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2022 Northeastern University
 * Copyright (c) 2022 Sapienza, University of Rome
 * Copyright (c) 2022 University of Padova
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Author: Andrea Lacava <thecave003@gmail.com>
 *         Tommaso Zugno <tommasozugno@gmail.com>
 *         Michele Polese <michele.polese@gmail.com>
 *
 * Phase 7 (M6): ported from E2SM-KPM v2 to v3.00 asn1c.
 *
 * Stage B — IndicationHeader: v2 GlobalE2node_ID / BitString cell-id logic
 *   removed; v3 Format1 uses sender strings + 8-byte colletStartTime only.
 *   Mirrors kpm_v3::BuildIndicationHeader (kpm-indication-builder.h).
 *
 * Stage C — IndicationMessage: v2 PM-container (FillPmContainer,
 *   FillOCuUp/CuCp/ODu) removed.  Replaced by a flat v3 Format1 builder
 *   that gathers all named MeasurementItems (cell first, then UE) into a
 *   single MeasurementRecord + parallel MeasurementInfoList.
 *   Mirrors kpm_v3::BuildDummyIndicationMessage generalised to N items.
 */

#include <ns3/kpm-indication.h>
#include <ns3/asn1c-types.h>
#include <ns3/log.h>

extern "C" {
/* v3.00 KPM ASN.1 headers */
#include "E2SM-KPM-IndicationHeader-Format1.h"
#include "E2SM-KPM-IndicationMessage-Format1.h"
#include "TimeStamp.h"
#include "MeasurementData.h"
#include "MeasurementDataItem.h"
#include "MeasurementRecord.h"
#include "MeasurementRecordItem.h"
#include "MeasurementInfoList.h"
#include "MeasurementInfoItem.h"
#include "MeasurementType.h"
#include "MeasurementTypeName.h"
#include "LabelInfoList.h"
#include "LabelInfoItem.h"
#include "MeasurementLabel.h"
#include "GranularityPeriod.h"
#include "S-NSSAI.h"
#include "SD.h"
#include "PLMNIdentity.h"
}

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("KpmIndication");

/* =========================================================================
 * KpmIndicationHeader — Stage B
 * =========================================================================
 *
 * v3.00 IndicationHeader-Format1 shape:
 *   colletStartTime   OCTET STRING (8 bytes) — mandatory
 *   fileFormatversion PrintableString        — OPTIONAL (omitted)
 *   senderName        PrintableString        — OPTIONAL (filled)
 *   senderType        PrintableString        — OPTIONAL (filled)
 *   vendorName        PrintableString        — OPTIONAL (filled)
 *
 * No GlobalE2node_ID, no collectionStartTime (those are v2 field names).
 * Mirrors kpm_v3::BuildIndicationHeader (examples/kpm-indication-builder.h).
 */

KpmIndicationHeader::KpmIndicationHeader (GlobalE2nodeType nodeType,
                                          KpmRicIndicationHeaderValues values)
{
  /* m_nodeType kept for ABI but unused in v3 header encoding */
  m_nodeType = nodeType;
  E2SM_KPM_IndicationHeader_t *descriptor = new E2SM_KPM_IndicationHeader_t ();
  memset (descriptor, 0, sizeof (E2SM_KPM_IndicationHeader_t));
  FillAndEncodeKpmRicIndicationHeader (descriptor, values);
  delete descriptor;
}

KpmIndicationHeader::~KpmIndicationHeader ()
{
  NS_LOG_FUNCTION (this);
  free (m_buffer);
  m_size = 0;
}

void
KpmIndicationHeader::Encode (E2SM_KPM_IndicationHeader_t *descriptor)
{
  asn_codec_ctx_t *opt_cod = 0; /* disable stack bounds checking */
  asn_encode_to_new_buffer_result_s encodedHeader = asn_encode_to_new_buffer (
      opt_cod, ATS_ALIGNED_BASIC_PER, &asn_DEF_E2SM_KPM_IndicationHeader, descriptor);

  if (encodedHeader.result.encoded < 0)
    {
      NS_FATAL_ERROR ("Error encoding RIC Indication Header, errno: "
                      << strerror (errno) << ", failed_type "
                      << encodedHeader.result.failed_type->name
                      << ", structure_ptr " << encodedHeader.result.structure_ptr);
    }

  m_buffer = encodedHeader.buffer;
  m_size = encodedHeader.result.encoded;
}

void
KpmIndicationHeader::FillAndEncodeKpmRicIndicationHeader (
    E2SM_KPM_IndicationHeader_t *descriptor,
    KpmRicIndicationHeaderValues values)
{
  /* Allocate the v3 Format1 struct (all optional pointer fields start NULL). */
  E2SM_KPM_IndicationHeader_Format1_t *fmt =
      (E2SM_KPM_IndicationHeader_Format1_t *) calloc (
          1, sizeof (E2SM_KPM_IndicationHeader_Format1_t));

  /* ---- senderName (PrintableString, OPTIONAL) ---- */
  /* Mirrors: fmt->senderName buf/size fill in BuildIndicationHeader(). */
  const char *sender = "ORANSim";
  fmt->senderName = (PrintableString_t *) calloc (1, sizeof (PrintableString_t));
  fmt->senderName->buf = (uint8_t *) calloc (1, strlen (sender));
  memcpy (fmt->senderName->buf, sender, strlen (sender));
  fmt->senderName->size = strlen (sender);

  /* ---- senderType (PrintableString, OPTIONAL) ---- */
  const char *stype = "simulator";
  fmt->senderType = (PrintableString_t *) calloc (1, sizeof (PrintableString_t));
  fmt->senderType->buf = (uint8_t *) calloc (1, strlen (stype));
  memcpy (fmt->senderType->buf, stype, strlen (stype));
  fmt->senderType->size = strlen (stype);

  /* ---- vendorName (PrintableString, OPTIONAL) ---- */
  const char *vendor = "ORAN-SC";
  fmt->vendorName = (PrintableString_t *) calloc (1, sizeof (PrintableString_t));
  fmt->vendorName->buf = (uint8_t *) calloc (1, strlen (vendor));
  memcpy (fmt->vendorName->buf, vendor, strlen (vendor));
  fmt->vendorName->size = strlen (vendor);

  /* ---- colletStartTime (TimeStamp_t = OCTET STRING, 8 bytes) ----
   *
   * v3.00 TimeStamp is an 8-byte OCTET STRING.  We encode the simulation
   * m_timestamp (uint64_t) big-endian into those 8 bytes — same width as
   * the OSC reference "20200613" (8 ASCII bytes).  The 8-byte width is what
   * makes the wire shape v3-compatible; the calendar value is not decoded
   * by the RIC for correctness checks.
   */
  NS_LOG_DEBUG ("Timestamp received: " << values.m_timestamp);
  uint64_t bigEndianTimestamp = htobe64 (values.m_timestamp);
  NS_LOG_DEBUG ("Timestamp big-endian: " << bigEndianTimestamp);

  fmt->colletStartTime.buf =
      (uint8_t *) calloc (1, (size_t) TIMESTAMP_LIMIT_SIZE);
  fmt->colletStartTime.size = (size_t) TIMESTAMP_LIMIT_SIZE;
  memcpy (fmt->colletStartTime.buf, &bigEndianTimestamp,
          (size_t) TIMESTAMP_LIMIT_SIZE);

  /* ---- Wire the Format1 into the outer wrapper (v3 indicationHeader_formats) ---- */
  descriptor->indicationHeader_formats.present =
      E2SM_KPM_IndicationHeader__indicationHeader_formats_PR_indicationHeader_Format1;
  descriptor->indicationHeader_formats.choice.indicationHeader_Format1 = fmt;

  NS_LOG_INFO (xer_fprint (stderr, &asn_DEF_E2SM_KPM_IndicationHeader_Format1, fmt));

  Encode (descriptor);

  /* Free the calloc'd Format1 sub-tree (senderName/Type/vendorName + their
   * buffers + colletStartTime.buf + fmt itself).  The ctor's `delete descriptor`
   * only releases the outer struct shell — it does NOT recurse into the asn1c
   * sub-tree — so the v2 code freed it explicitly with ASN_STRUCT_FREE; the v3
   * port must do the same or it leaks one Format1 sub-tree per indication
   * (unbounded on the periodic live report loop). Null the choice pointer so the
   * subsequent shell delete can't observe a dangling pointer. */
  ASN_STRUCT_FREE (asn_DEF_E2SM_KPM_IndicationHeader_Format1, fmt);
  descriptor->indicationHeader_formats.choice.indicationHeader_Format1 = nullptr;
}

/* =========================================================================
 * MeasurementItemList
 * =========================================================================*/

MeasurementItemList::MeasurementItemList ()
{
  m_id = nullptr;
}

MeasurementItemList::MeasurementItemList (std::string id)
{
  m_id = Create<OctetString> (id, id.length ());
}

MeasurementItemList::~MeasurementItemList ()
{
}

std::vector<Ptr<MeasurementItem>>
MeasurementItemList::GetItems ()
{
  return m_items;
}

OCTET_STRING_t
MeasurementItemList::GetId ()
{
  NS_ABORT_IF (m_id == nullptr);
  return m_id->GetValue ();
}

/* =========================================================================
 * KpmIndicationMessage — Stage C2
 * =========================================================================
 *
 * v3.00 IndicationMessage-Format1 shape:
 *   measData      MeasurementData     — mandatory (SEQUENCE OF MeasurementDataItem)
 *   measInfoList  MeasurementInfoList — OPTIONAL (SEQUENCE OF MeasurementInfoItem)
 *   granulPeriod  GranularityPeriod   — OPTIONAL
 *
 * Encoding strategy (N items total):
 *   - Collect ordered (name, value) pairs: cell items first (in AddItem order
 *     from m_cellMeasurementItems), then UE items appended per UE in set
 *     iteration order.  Order is deterministic within a single report
 *     (set iteration order is consistent across record and infoList).
 *   - Build ONE MeasurementDataItem with an N-element MeasurementRecord.
 *   - Build ONE MeasurementInfoList with N MeasurementInfoItems, each with
 *     measType.present = MeasurementType_PR_measName, and one LabelInfoItem
 *     carrying a static PLMN="747"/SST="1"/SD="100" label (same static label
 *     as the reference; per-item PLMN derivation is a Phase7-live TODO — see
 *     comment below).
 *   - record[i] ↔ infoList[i] (parallel order guaranteed by single loop).
 *
 * Mirrors kpm_v3::BuildDummyIndicationMessage, generalised to N items.
 */

KpmIndicationMessage::KpmIndicationMessage (KpmIndicationMessageValues values)
{
  E2SM_KPM_IndicationMessage_t *descriptor = new E2SM_KPM_IndicationMessage_t ();
  memset (descriptor, 0, sizeof (E2SM_KPM_IndicationMessage_t));
  FillAndEncodeKpmIndicationMessage (descriptor, values);
  delete descriptor;
}

KpmIndicationMessage::~KpmIndicationMessage ()
{
  free (m_buffer);
  m_size = 0;
}

void
KpmIndicationMessage::Encode (E2SM_KPM_IndicationMessage_t *descriptor)
{
  asn_codec_ctx_t *opt_cod = 0;
  asn_encode_to_new_buffer_result_s encodedMsg = asn_encode_to_new_buffer (
      opt_cod, ATS_ALIGNED_BASIC_PER, &asn_DEF_E2SM_KPM_IndicationMessage, descriptor);

  if (encodedMsg.result.encoded < 0)
    {
      /* NS_LOG_ERROR rather than NS_FATAL_ERROR so one bad report does not
       * kill the node.  m_buffer/m_size remain 0 — callers check m_size. */
      NS_LOG_ERROR ("Error encoding RIC Indication Message, errno: "
                    << strerror (errno) << ", failed_type "
                    << encodedMsg.result.failed_type->name
                    << ", structure_ptr " << encodedMsg.result.structure_ptr);
      return;
    }

  m_buffer = encodedMsg.buffer;
  m_size = encodedMsg.result.encoded;
}

/* Helper: build a single MeasurementInfoItem with measName from a C++ string
 * and a static PLMN+S-NSSAI label.
 *
 * Label choice rationale: v3 kpimon-go expects at least one LabelInfoItem per
 * MeasurementInfoItem.  We use the same static "747"/"1"/"100" values as the
 * Phase-6 reference (kpm_v3::BuildDummyIndicationMessage).  Deriving PLMN from
 * values.m_pmContainerValues->m_plmId requires knowing the container type and
 * adds complexity not needed for M6 offline correctness.
 * TODO(Phase7-live): derive PLMN from actual sim cell PLMN for live kpimon
 * parity; verify SD encoding (ASCII bytes vs BCD) against kpimon decoder.
 */
static MeasurementInfoItem_t *
BuildMeasInfoItem (const std::string &name)
{
  /* ---- Static label strings (mirrors BuildDummyIndicationMessage) ---- */
  static const char *PLMN_ID = "747";
  static const char *SST     = "1";
  static const char *SD_VAL  = "100";

  /* S-NSSAI */
  S_NSSAI_t *snssai = (S_NSSAI_t *) calloc (1, sizeof (S_NSSAI_t));
  snssai->sST.buf  = (uint8_t *) calloc (1, strlen (SST));
  snssai->sST.size = strlen (SST);
  memcpy (snssai->sST.buf, SST, strlen (SST));
  snssai->sD = (SD_t *) calloc (1, sizeof (SD_t));
  snssai->sD->buf  = (uint8_t *) calloc (1, strlen (SD_VAL));
  snssai->sD->size = strlen (SD_VAL);
  memcpy (snssai->sD->buf, SD_VAL, strlen (SD_VAL));

  /* PLMNIdentity */
  PLMNIdentity_t *plmn = (PLMNIdentity_t *) calloc (1, sizeof (PLMNIdentity_t));
  plmn->buf  = (uint8_t *) calloc (1, strlen (PLMN_ID));
  plmn->size = strlen (PLMN_ID);
  memcpy (plmn->buf, PLMN_ID, strlen (PLMN_ID));

  /* MeasurementLabel */
  MeasurementLabel_t *lbl = (MeasurementLabel_t *) calloc (1, sizeof (MeasurementLabel_t));
  lbl->plmnID  = plmn;
  lbl->sliceID = snssai;

  /* LabelInfoItem — shallow-copy the label value, then free the wrapper */
  LabelInfoItem_t *lblItem = (LabelInfoItem_t *) calloc (1, sizeof (LabelInfoItem_t));
  lblItem->measLabel = *lbl;
  free (lbl); /* only the wrapper struct; pointer members now owned by lblItem */

  /* LabelInfoList — shallow-copy into infoItem, free wrapper */
  LabelInfoList_t *lblList = (LabelInfoList_t *) calloc (1, sizeof (LabelInfoList_t));
  ASN_SEQUENCE_ADD (&lblList->list, lblItem);

  /* MeasurementInfoItem */
  MeasurementInfoItem_t *infoItem =
      (MeasurementInfoItem_t *) calloc (1, sizeof (MeasurementInfoItem_t));
  infoItem->labelInfoList = *lblList;
  free (lblList); /* shallow-copy done; free wrapper only */

  /* measType: measName (PrintableString = MeasurementTypeName_t).
   * MeasurementTypeName_t is typedef'd as PrintableString_t in v3 asn1c. */
  infoItem->measType.present = MeasurementType_PR_measName;
  infoItem->measType.choice.measName.buf =
      (uint8_t *) calloc (1, name.size ());
  memcpy (infoItem->measType.choice.measName.buf, name.c_str (), name.size ());
  infoItem->measType.choice.measName.size = name.size ();

  return infoItem;
}

void
KpmIndicationMessage::FillAndEncodeKpmIndicationMessage (
    E2SM_KPM_IndicationMessage_t *descriptor,
    KpmIndicationMessageValues values)
{
  /* Initialise outputs up-front: the soft constraint-fail path below returns
   * without encoding, and ~KpmIndicationMessage does free(m_buffer) — leaving
   * these indeterminate would be UB on that path. */
  m_buffer = nullptr;
  m_size = 0;

  /* ---- Collect ordered (name, MeasurementItem) pairs ---- */
  /* Cell items first, then UE items (deterministic per-UE via set iteration). */
  std::vector<Ptr<MeasurementItem>> orderedItems;

  if (values.m_cellMeasurementItems)
    {
      for (auto &item : values.m_cellMeasurementItems->GetItems ())
        {
          orderedItems.push_back (item);
        }
    }

  /* UE items: flatten all UE indications — each UE's items appended in
   * AddItem order.  Set iteration order is used across UEs (consistent
   * within one invocation; sufficient for parallel record/infoList build). */
  for (auto &ueList : values.m_ueIndications)
    {
      for (auto &item : ueList->GetItems ())
        {
          orderedItems.push_back (item);
        }
    }

  if (orderedItems.empty ())
    {
      NS_LOG_WARN ("KpmIndicationMessage: no measurement items to encode; "
                   "producing empty Format1 (measData has 0 records).");
    }

  /* ---- Build MeasurementRecord: N MeasurementRecordItems ---- */
  /* One MeasurementRecord per MeasurementDataItem (v3 style-1 shape). */
  MeasurementRecord_t *record =
      (MeasurementRecord_t *) calloc (1, sizeof (MeasurementRecord_t));

  /* ---- Build MeasurementInfoList: N MeasurementInfoItems (parallel) ---- */
  MeasurementInfoList_t *infoList =
      (MeasurementInfoList_t *) calloc (1, sizeof (MeasurementInfoList_t));

  for (auto &item : orderedItems)
    {
      /* --- MeasurementRecordItem ---------------------------------------- */
      MeasurementRecordItem_t *recItem =
          (MeasurementRecordItem_t *) calloc (1, sizeof (MeasurementRecordItem_t));

      switch (item->GetValueType ())
        {
          case MeasurementItem::ValueType::Int:
            /* integer field is unsigned long in v3 asn1c CHOICE. A negative
             * metric would wrap silently and still pass the constraint check —
             * warn loudly (all reduced-PM KPM metrics are >=0, but the API
             * accepts any signed long). */
            if (item->GetIntValue () < 0)
              {
                NS_LOG_WARN ("KpmIndicationMessage: metric '" << item->GetName ()
                             << "' has negative value " << item->GetIntValue ()
                             << " — wraps to unsigned in the v3 integer record.");
              }
            recItem->present        = MeasurementRecordItem_PR_integer;
            recItem->choice.integer = (unsigned long) item->GetIntValue ();
            break;

          case MeasurementItem::ValueType::Real:
            recItem->present     = MeasurementRecordItem_PR_real;
            recItem->choice.real = item->GetRealValue ();
            break;

          case MeasurementItem::ValueType::RRC:
            /* L3-RRC measurements have no v3 MeasurementRecordItem mapping.
             * Emitted as noValue per spec extension practice; logged so the
             * caller can detect RRC items are silently dropped from the PDU. */
            NS_LOG_WARN ("KpmIndicationMessage: item '" << item->GetName ()
                         << "' is an L3-RRC value — no v3 MeasurementRecordItem "
                            "mapping; emitted as noValue.");
            recItem->present = MeasurementRecordItem_PR_noValue;
            break;

          default:
            NS_LOG_WARN ("KpmIndicationMessage: unknown value type for item '"
                         << item->GetName () << "'; emitted as noValue.");
            recItem->present = MeasurementRecordItem_PR_noValue;
            break;
        }

      ASN_SEQUENCE_ADD (&record->list, recItem);

      /* --- MeasurementInfoItem (parallel to recItem above) -------------- */
      MeasurementInfoItem_t *infoItem = BuildMeasInfoItem (item->GetName ());
      ASN_SEQUENCE_ADD (&infoList->list, infoItem);
    }

  /* ---- MeasurementDataItem: shallow-copy the record, free record wrapper ---- */
  MeasurementDataItem_t *dataItem =
      (MeasurementDataItem_t *) calloc (1, sizeof (MeasurementDataItem_t));
  dataItem->measRecord = *record;
  dataItem->incompleteFlag = (long *) calloc (1, sizeof (long));
  *dataItem->incompleteFlag = 0;
  free (record); /* shallow-copy done; record->list.array now owned by dataItem */

  /* ---- MeasurementData: one MeasurementDataItem ---- */
  MeasurementData_t *measData =
      (MeasurementData_t *) calloc (1, sizeof (MeasurementData_t));
  ASN_SEQUENCE_ADD (&measData->list, dataItem);

  /* ---- Assemble IndicationMessage-Format1 ---- */
  E2SM_KPM_IndicationMessage_Format1_t *fmt =
      (E2SM_KPM_IndicationMessage_Format1_t *) calloc (
          1, sizeof (E2SM_KPM_IndicationMessage_Format1_t));

  /* granulPeriod = 1 ms (same as reference) */
  fmt->granulPeriod = (GranularityPeriod_t *) calloc (1, sizeof (GranularityPeriod_t));
  *fmt->granulPeriod = 1;

  /* measData: shallow-copy value into fmt, free wrapper */
  fmt->measData = *measData;
  free (measData);

  /* measInfoList: pointer field (OPTIONAL) */
  fmt->measInfoList = infoList;

  /* ---- Wire Format1 into the outer wrapper ---- */
  descriptor->indicationMessage_formats.present =
      E2SM_KPM_IndicationMessage__indicationMessage_formats_PR_indicationMessage_Format1;
  descriptor->indicationMessage_formats.choice.indicationMessage_Format1 = fmt;

  NS_LOG_INFO (xer_fprint (stderr, &asn_DEF_E2SM_KPM_IndicationMessage_Format1, fmt));

  /* ---- ASN.1 constraint check (I1: soft failure — log + skip, no exit) ---- */
  char errBuf[300] = {0};
  size_t errLen = sizeof (errBuf);
  if (asn_check_constraints (&asn_DEF_E2SM_KPM_IndicationMessage, descriptor,
                              errBuf, &errLen))
    {
      NS_LOG_WARN ("KpmIndicationMessage: constraint check failed: " << errBuf
                   << " — skipping encode for this report.");
      /* m_buffer/m_size stay 0 (set at top); free the Format1 sub-tree before
       * the caller deletes the (shell-only) descriptor, else this report leaks. */
      ASN_STRUCT_FREE (asn_DEF_E2SM_KPM_IndicationMessage_Format1, fmt);
      descriptor->indicationMessage_formats.choice.indicationMessage_Format1 = nullptr;
      return;
    }

  Encode (descriptor);

  /* Free the calloc'd Format1 sub-tree (measData records, measInfoList items,
   * granulPeriod, fmt) after Encode() has copied the wire buffer.  The ctor's
   * `delete descriptor` only frees the outer shell — not the asn1c sub-tree —
   * so omitting this leaks one full message tree per periodic report. Mirrors
   * the v2 original's ASN_STRUCT_FREE(...Format1, ...). */
  ASN_STRUCT_FREE (asn_DEF_E2SM_KPM_IndicationMessage_Format1, fmt);
  descriptor->indicationMessage_formats.choice.indicationMessage_Format1 = nullptr;
}

} // namespace ns3
