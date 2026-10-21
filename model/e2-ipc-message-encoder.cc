/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Encoders for the ns3-e2-ipc v1 messages (hello, sample, bye).
 */

#include <ns3/e2-ipc-message-encoder.h>

#include <ns3/abort.h>
#include <ns3/e2-ipc-json-writer.h>

#include <charconv>
#include <cmath>
#include <map>

namespace ns3 {

namespace {

/** Per-UE lists of one sample, one per section. */
struct UeSections
{
  Ptr<MeasurementItemList> du;
  Ptr<MeasurementItemList> cuUp;
  Ptr<MeasurementItemList> cuCp;
};

void
WriteValue (E2IpcJsonWriter &w, const MeasurementValue &value)
{
  if (const int64_t *i = std::get_if<int64_t> (&value))
    {
      w.Int (*i);
    }
  else if (const uint64_t *u = std::get_if<uint64_t> (&value))
    {
      w.UInt (*u);
    }
  else
    {
      w.Double (std::get<double> (value));
    }
}

/** A 3GPP-mapped SINR: integral by construction, null when not finite. */
void
WriteSinr3gpp (E2IpcJsonWriter &w, double value)
{
  if (std::isfinite (value))
    {
      w.Int (static_cast<int64_t> (value));
    }
  else
    {
      w.Null ();
    }
}

/** L3 RRC SINR object of one UE (schema $defs/l3). */
void
WriteL3 (E2IpcJsonWriter &w, const std::vector<Ptr<L3RrcMeasurements>> &l3Items)
{
  Ptr<L3RrcMeasurements> serving;
  Ptr<L3RrcMeasurements> neigh;
  for (const auto &item : l3Items)
    {
      (item->HasServing () ? serving : neigh) = item;
    }
  w.Key ("l3");
  w.BeginObject ();
  if (serving)
    {
      w.Key ("servCellId");
      w.Int (serving->GetServing ().cellId);
      w.Key ("servSinrDb");
      w.Double (serving->GetServing ().sinrDb);
      w.Key ("servSinr3gpp");
      WriteSinr3gpp (w, serving->GetServing ().sinr3gpp);
    }
  if (neigh)
    {
      w.Key ("neigh");
      w.BeginArray ();
      for (const auto &cell : neigh->GetNeighbours ())
        {
          w.BeginObject ();
          w.Key ("cellId");
          w.Int (cell.cellId);
          w.Key ("sinrDb");
          w.Double (cell.sinrDb);
          w.Key ("sinr3gpp");
          WriteSinr3gpp (w, cell.sinr3gpp);
          w.EndObject ();
        }
      w.EndArray ();
    }
  w.EndObject ();
}

/** One section object: the items in insertion order, plus l3 for a UE CU-CP list. */
void
WriteSection (E2IpcJsonWriter &w, const char *name, const Ptr<MeasurementItemList> &list)
{
  if (!list)
    {
      return;
    }
  w.Key (name);
  w.BeginObject ();
  for (const auto &item : list->GetItems ())
    {
      w.Key (item.first);
      WriteValue (w, item.second);
    }
  if (!list->GetL3Items ().empty ())
    {
      WriteL3 (w, list->GetL3Items ());
    }
  w.EndObject ();
}

/** Group the per-UE lists of every section by IMSI (std::map keeps IMSI order). */
void
CollectUes (std::map<uint64_t, UeSections> &ues, const Ptr<KpmIndicationMessage> &msg,
            Ptr<MeasurementItemList> UeSections::*section)
{
  if (!msg)
    {
      return;
    }
  for (const auto &ueList : msg->GetValues ().m_ueIndications)
    {
      // The UE ID is ns-3's zero-padded decimal IMSI string
      const std::string &id = ueList->GetId ();
      uint64_t imsi = 0;
      auto res = std::from_chars (id.data (), id.data () + id.size (), imsi);
      NS_ABORT_MSG_IF (res.ec != std::errc () || res.ptr != id.data () + id.size (),
                       "UE id is not a decimal IMSI: " << id);
      ues[imsi].*section = ueList;
    }
}

Ptr<MeasurementItemList>
CellList (const Ptr<KpmIndicationMessage> &msg)
{
  return msg ? msg->GetValues ().m_cellMeasurementItems : nullptr;
}

} // namespace

std::string
EncodeE2IpcHello (const E2IpcNodeInfo &info, const std::string &runId, const std::string &ns3Sha,
                  const std::string &oranInterfaceSha)
{
  E2IpcJsonWriter w;
  w.BeginObject ();
  w.Key ("type");
  w.String ("hello");
  w.Key ("schema");
  w.String ("ns3-e2-ipc");
  w.Key ("v");
  w.Int (1);
  w.Key ("minor");
  w.Int (0);
  w.Key ("nodeId");
  w.Int (info.nodeId);
  w.Key ("kind");
  w.String (info.kind);
  w.Key ("periodNs");
  w.Int (info.periodNs);
  w.Key ("simStartEpochMs");
  w.Int (info.simStartEpochMs);
  w.Key ("reducedPmValues");
  w.Bool (info.reducedPmValues);
  w.Key ("maxNeigh");
  w.Int (info.maxNeigh);
  w.Key ("runId");
  w.String (runId);
  w.Key ("emitter");
  w.BeginObject ();
  w.Key ("ns3Sha");
  w.String (ns3Sha);
  w.Key ("oranInterfaceSha");
  w.String (oranInterfaceSha);
  w.EndObject ();
  w.EndObject ();
  return w.Str ();
}

std::string
EncodeE2IpcSample (uint16_t nodeId, uint64_t seq, int64_t simTimeNs, uint64_t ipcDropped,
                   Ptr<KpmIndicationMessage> du, Ptr<KpmIndicationMessage> cuUp,
                   Ptr<KpmIndicationMessage> cuCp)
{
  E2IpcJsonWriter w;
  w.BeginObject ();
  w.Key ("type");
  w.String ("sample");
  w.Key ("v");
  w.Int (1);
  w.Key ("nodeId");
  w.Int (nodeId);
  w.Key ("seq");
  w.UInt (seq);
  w.Key ("simTimeNs");
  w.Int (simTimeNs);
  w.Key ("ipcDropped");
  w.UInt (ipcDropped);

  w.Key ("cell");
  w.BeginObject ();
  WriteSection (w, "du", CellList (du));
  WriteSection (w, "cuUp", CellList (cuUp));
  WriteSection (w, "cuCp", CellList (cuCp));
  w.EndObject ();

  std::map<uint64_t, UeSections> ues;
  CollectUes (ues, du, &UeSections::du);
  CollectUes (ues, cuUp, &UeSections::cuUp);
  CollectUes (ues, cuCp, &UeSections::cuCp);
  w.Key ("ues");
  w.BeginArray ();
  for (const auto &ue : ues)
    {
      w.BeginObject ();
      w.Key ("imsi");
      w.UInt (ue.first);
      WriteSection (w, "du", ue.second.du);
      WriteSection (w, "cuUp", ue.second.cuUp);
      WriteSection (w, "cuCp", ue.second.cuCp);
      w.EndObject ();
    }
  w.EndArray ();
  w.EndObject ();
  return w.Str ();
}

std::string
EncodeE2IpcBye (uint16_t nodeId, const E2IpcCounters &counters)
{
  E2IpcJsonWriter w;
  w.BeginObject ();
  w.Key ("type");
  w.String ("bye");
  w.Key ("v");
  w.Int (1);
  w.Key ("nodeId");
  w.Int (nodeId);
  w.Key ("lastSeq");
  w.Int (static_cast<int64_t> (counters.built) - 1);
  w.Key ("built");
  w.UInt (counters.built);
  w.Key ("sent");
  w.UInt (counters.sent);
  w.Key ("droppedDisconnected");
  w.UInt (counters.droppedDisconnected);
  w.Key ("droppedBackpressure");
  w.UInt (counters.droppedBackpressure);
  w.EndObject ();
  return w.Str ();
}

} // namespace ns3
