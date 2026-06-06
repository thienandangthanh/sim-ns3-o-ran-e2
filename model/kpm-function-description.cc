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
 *		   Tommaso Zugno <tommasozugno@gmail.com>
 *		   Michele Polese <michele.polese@gmail.com>
 */

#include <ns3/kpm-function-description.h>
#include <ns3/asn1c-types.h>
#include <ns3/log.h>

extern "C" {
#include "RIC-EventTriggerStyle-Item.h"
#include "RIC-ReportStyle-Item.h"
#include "MeasurementInfo-Action-List.h"
#include "MeasurementInfo-Action-Item.h"
#include "MeasurementTypeID.h"
}

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("KpmFunctionDescription");

KpmFunctionDescription::KpmFunctionDescription ()
{
  E2SM_KPM_RANfunction_Description_t *descriptor = new E2SM_KPM_RANfunction_Description_t ();
  FillAndEncodeKpmFunctionDescription (descriptor);
  ASN_STRUCT_FREE_CONTENTS_ONLY (asn_DEF_E2SM_KPM_RANfunction_Description, descriptor);
  delete descriptor;
}

KpmFunctionDescription::~KpmFunctionDescription ()
{
  free (m_buffer);
  m_size = 0;
}

void
KpmFunctionDescription::Encode (E2SM_KPM_RANfunction_Description_t *descriptor)
{
  asn_codec_ctx_t *opt_cod = 0; // disable stack bounds checking
  // encode the structure into the e2smbuffer
  asn_encode_to_new_buffer_result_s encodedMsg = asn_encode_to_new_buffer (
      opt_cod, ATS_ALIGNED_BASIC_PER, &asn_DEF_E2SM_KPM_RANfunction_Description, descriptor);

  if (encodedMsg.result.encoded < 0)
    {
      NS_FATAL_ERROR ("Error during the encoding of the RIC Indication Header, errno: "
                      << strerror (errno) << ", failed_type " << encodedMsg.result.failed_type->name
                      << ", structure_ptr " << encodedMsg.result.structure_ptr);
    }

  m_buffer = encodedMsg.buffer;
  m_size = encodedMsg.result.encoded;
}

// E2SM-KPM v3.00 advertised measurements (golden parity with OSC encode_kpm.cpp).
static const char *const g_kpmPerfMeasurements[] = {
    "DRB.RlcSduTransmittedVolumeDL", "DRB.RlcSduTransmittedVolumeUL",
    "DRB.PerDataVolumeDLDist.Bin",   "DRB.PerDataVolumeULDist.Bin",
    "DRB.RlcPacketDropRateDLDist",   "DRB.PacketLossRateULDist",
    "L1M.DL-SS-RSRP.SSB",            "L1M.DL-SS-SINR.SSB",
    "L1M.UL-SRS-RSRP"};
static const int g_kpmNumberMeasurements = 9;

// Fill an OCTET_STRING/PrintableString from a NUL-terminated C string.
static void
SetKpmOctetString (OCTET_STRING_t *os, const char *s)
{
  os->size = (int) strlen (s);
  os->buf = (uint8_t *) calloc (1, os->size ? os->size : 1);
  memcpy (os->buf, s, os->size);
}

// Populate an (embedded) measInfo action list with the 9 KPM measurements.
// Each report style gets its own list so ASN_STRUCT_FREE stays double-free safe.
static void
BuildKpmMeasInfoActionList (MeasurementInfo_Action_List_t *list)
{
  for (int i = 0; i < g_kpmNumberMeasurements; i++)
    {
      MeasurementInfo_Action_Item_t *item =
          (MeasurementInfo_Action_Item_t *) calloc (1, sizeof (MeasurementInfo_Action_Item_t));
      SetKpmOctetString (&item->measName, g_kpmPerfMeasurements[i]);
      item->measID = (MeasurementTypeID_t *) calloc (1, sizeof (MeasurementTypeID_t));
      *item->measID = i + 1;
      ASN_SEQUENCE_ADD (&list->list, item);
    }
}

void
KpmFunctionDescription::FillAndEncodeKpmFunctionDescription (
    E2SM_KPM_RANfunction_Description_t *ranfunc_desc)
{
  // RANfunction-Name. ShortName via the OctetString wrapper (ns-3 idiom);
  // OID123 kept to match the L-release golden (Phase-4 OID decision).
  std::string shortNameBuffer = "ORAN-E2SM-KPM";
  Ptr<OctetString> shortName = Create<OctetString> (shortNameBuffer, shortNameBuffer.size ());
  ranfunc_desc->ranFunction_Name.ranFunction_ShortName = shortName->GetValue ();

  SetKpmOctetString (&ranfunc_desc->ranFunction_Name.ranFunction_Description, "KPM Monitor");
  SetKpmOctetString (&ranfunc_desc->ranFunction_Name.ranFunction_E2SM_OID, "OID123");
  ranfunc_desc->ranFunction_Name.ranFunction_Instance = (long *) calloc (1, sizeof (long));
  *ranfunc_desc->ranFunction_Name.ranFunction_Instance = 1;

  // Event-trigger style list: single "Periodic Report" (format 1).
  RIC_EventTriggerStyle_Item_t *trigger_style =
      (RIC_EventTriggerStyle_Item_t *) calloc (1, sizeof (RIC_EventTriggerStyle_Item_t));
  trigger_style->ric_EventTriggerStyle_Type = 1;
  SetKpmOctetString (&trigger_style->ric_EventTriggerStyle_Name, "Periodic Report");
  trigger_style->ric_EventTriggerFormat_Type = 1;

  ranfunc_desc->ric_EventTriggerStyle_List =
      (E2SM_KPM_RANfunction_Description::
           E2SM_KPM_RANfunction_Description__ric_EventTriggerStyle_List *)
          calloc (1, sizeof (E2SM_KPM_RANfunction_Description::
                                 E2SM_KPM_RANfunction_Description__ric_EventTriggerStyle_List));
  ASN_SEQUENCE_ADD (&ranfunc_desc->ric_EventTriggerStyle_List->list, trigger_style);

  // Report style list: 5 v3.00 styles, each with its own meas action list.
  // v3 field renames vs Bronze v2: ric_ReportIndication*Format_Type ->
  // ric_Indication*Format_Type; + ric_ActionFormat_Type; + measInfo_Action_List.
  struct ReportStyleSpec
  {
    long type;
    const char *name;
    long actionFormat;
    long indMsgFormat;
  };
  static const ReportStyleSpec reportStyles[] = {
      {1, "E2 Node Measurement", 1, 1},
      {2, "E2 Node Measurement for a single UE", 2, 1},
      {3, "Condition-based, UE-level E2 Node Measurement", 3, 2},
      {4, "Common Condition-based, UE-level Measurement", 4, 3},
      {5, "E2 Node Measurement for multiple UEs", 5, 3}};

  ranfunc_desc->ric_ReportStyle_List =
      (E2SM_KPM_RANfunction_Description::E2SM_KPM_RANfunction_Description__ric_ReportStyle_List *)
          calloc (1, sizeof (E2SM_KPM_RANfunction_Description::
                                 E2SM_KPM_RANfunction_Description__ric_ReportStyle_List));

  for (const ReportStyleSpec &spec : reportStyles)
    {
      RIC_ReportStyle_Item_t *report_style =
          (RIC_ReportStyle_Item_t *) calloc (1, sizeof (RIC_ReportStyle_Item_t));
      report_style->ric_ReportStyle_Type = spec.type;
      SetKpmOctetString (&report_style->ric_ReportStyle_Name, spec.name);
      report_style->ric_ActionFormat_Type = spec.actionFormat;
      report_style->ric_IndicationHeaderFormat_Type = 1;
      report_style->ric_IndicationMessageFormat_Type = spec.indMsgFormat;
      BuildKpmMeasInfoActionList (&report_style->measInfo_Action_List);
      ASN_SEQUENCE_ADD (&ranfunc_desc->ric_ReportStyle_List->list, report_style);
    }

  Encode (ranfunc_desc);

  NS_LOG_INFO (xer_fprint (stderr, &asn_DEF_E2SM_KPM_RANfunction_Description, ranfunc_desc));
}

} // namespace ns3
