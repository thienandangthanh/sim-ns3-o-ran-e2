/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Phase 7 (M6) offline structural proof of the PORTED ns-3 model KPM pipeline.
 *
 * Unlike the Phase-6 standalone builders (examples/kpm-indication-builder.h),
 * this exercises the REAL oran-interface MODEL classes — ns3::KpmIndicationMessage
 * and ns3::KpmIndicationHeader — the exact objects scenario-zero's
 * MmWaveEnbNetDevice::BuildAndSendReportMessage drives online. It builds a v3.00
 * indication from the 5 reduced-PM DU cell metric names (Stage D set) + dummy
 * values, then asserts both header and message produce a non-empty APER encode.
 *
 * A clean exit(0) with non-zero sizes proves the ported v3 flat-model encode
 * (MeasurementData + MeasurementInfoList) runs without ASN.1 constraint failure.
 * The live decode-by-kpimon-go proof is the deferred Phase-7 live run.
 */

#include <ns3/core-module.h>
#include <ns3/kpm-indication.h>

#include <cstdio>
#include <cstdlib>

using namespace ns3;

int
main (int argc, char *argv[])
{
  /* The 5 reduced-PM DU cell metric names the descriptor now advertises and the
   * mmwave helper emits (oran-interface/model/kpm-function-description.cc). */
  Ptr<MeasurementItemList> cellItems = Create<MeasurementItemList> ();
  cellItems->AddItem<long> ("TB.TotNbrDlInitial.Qpsk", 10);
  cellItems->AddItem<long> ("TB.TotNbrDlInitial.16Qam", 20);
  cellItems->AddItem<long> ("TB.TotNbrDlInitial.64Qam", 30);
  cellItems->AddItem<long> ("RRU.PrbUsedDl", 42);
  cellItems->AddItem<long> ("DRB.MeanActiveUeDl", 3);

  /* ---- Message (v3 IndicationMessage-Format1 via the model encode path) ---- */
  KpmIndicationMessage::KpmIndicationMessageValues msgValues;
  msgValues.m_cellObjectId = "NRCellDU";
  msgValues.m_cellMeasurementItems = cellItems;

  Ptr<KpmIndicationMessage> msg = Create<KpmIndicationMessage> (msgValues);

  /* ---- Header (v3 IndicationHeader-Format1, 8-byte colletStartTime) ---- */
  KpmIndicationHeader::KpmRicIndicationHeaderValues hdrValues;
  hdrValues.m_gnbId = "1";
  hdrValues.m_nrCellId = 1;
  hdrValues.m_plmId = "111";
  hdrValues.m_timestamp = 1591780000000; // nonzero -> non-degenerate 8-byte TS

  Ptr<KpmIndicationHeader> hdr =
      Create<KpmIndicationHeader> (KpmIndicationHeader::GlobalE2nodeType::gNB, hdrValues);

  std::printf ("[proof] v3 model encode: header=%zu B, message=%zu B\n",
               hdr->m_size, msg->m_size);

  if (hdr->m_size == 0 || hdr->m_buffer == nullptr)
    {
      std::fprintf (stderr, "[proof] FAIL: header did not encode (size 0)\n");
      return 1;
    }
  if (msg->m_size == 0 || msg->m_buffer == nullptr)
    {
      std::fprintf (stderr,
                    "[proof] FAIL: message did not encode (size 0) — likely an "
                    "ASN.1 constraint failure in the v3 flat-model build\n");
      return 1;
    }

  std::printf ("[proof] PASS: ported model emits a valid non-empty v3 KPM "
               "indication (5 named cell metrics)\n");
  return 0;
}
