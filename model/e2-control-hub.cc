/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Inbound E2 control path of the ns3-e2-ipc stream (v1 minor 1).
 */

#include <ns3/e2-control-hub.h>

#include <ns3/abort.h>
#include <ns3/e2-ipc-emitter.h>
#include <ns3/e2-ipc-json-reader.h>
#include <ns3/e2-ipc-json-writer.h>
#include <ns3/log.h>
#include <ns3/simulator.h>
#include <ns3/string.h>

#include <algorithm>
#include <iostream>
#include <tuple>

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("E2ControlHub");

NS_OBJECT_ENSURE_REGISTERED (E2ControlHub);

namespace {
Ptr<E2ControlHub> g_hub;
} // namespace

TypeId
E2ControlHub::GetTypeId ()
{
  static TypeId tid =
      TypeId ("ns3::E2ControlHub")
          .SetParent<Object> ()
          .AddConstructor<E2ControlHub> ()
          .AddAttribute ("LedgerPath",
                         "Control ledger file; empty means e2-control-ledger.jsonl in the "
                         "first emitter's trace directory",
                         StringValue (""), MakeStringAccessor (&E2ControlHub::m_ledgerPath),
                         MakeStringChecker ());
  return tid;
}

Ptr<E2ControlHub>
E2ControlHub::Get ()
{
  if (!g_hub)
    {
      g_hub = CreateObject<E2ControlHub> ();
    }
  return g_hub;
}

E2ControlHub::E2ControlHub ()
{
}

E2ControlHub::~E2ControlHub ()
{
  if (m_ledger != nullptr)
    {
      std::fclose (m_ledger);
    }
}

void
E2ControlHub::SetHandoverHandler (HandoverHandler handler)
{
  m_handler = handler;
}

void
E2ControlHub::RegisterEmitter (uint16_t nodeId, E2IpcEmitter *emitter, const std::string &traceDir)
{
  if (m_emitters.empty ())
    {
      m_defaultTraceDir = traceDir;
    }
  m_emitters[nodeId] = emitter;
  m_lastSeq[nodeId] = -1;
}

void
E2ControlHub::OnInboundLine (uint16_t viaNodeId, const std::string &line)
{
  E2IpcJsonReader r;
  if (!r.Parse (line) || r.GetString ("type") != "control")
    {
      m_badLines++;
      NS_LOG_WARN ("E2 IPC node " << viaNodeId << ": ignored inbound line " << line);
      return;
    }
  if (!r.HasInt ("ctlId") || !r.HasInt ("imsi") || !r.HasInt ("targetCellId") ||
      r.GetInt ("nodeId") != viaNodeId || r.GetInt ("imsi") < 0 || r.GetInt ("ctlId") < 0 ||
      r.GetInt ("targetCellId") < 0 || r.GetInt ("targetCellId") > 65535)
    {
      m_badLines++;
      std::clog << "E2 control: malformed control from node " << viaNodeId << ": " << line
                << std::endl;
      return;
    }
  E2IpcControl c;
  c.viaNodeId = viaNodeId;
  c.ctlId = static_cast<uint64_t> (r.GetInt ("ctlId"));
  c.imsi = static_cast<uint64_t> (r.GetInt ("imsi"));
  c.targetCellId = static_cast<uint16_t> (r.GetInt ("targetCellId"));
  c.afterSeq = r.HasInt ("afterSeq") ? r.GetInt ("afterSeq") : -1;
  if (m_stopping)
    {
      Answer (c, m_appliedSeq, Simulator::Now ().GetNanoSeconds (), false, "late", 0);
      return;
    }
  m_buffered.push_back (c);
}

void
E2ControlHub::OnSampleEmitted (uint16_t nodeId, uint64_t seq)
{
  m_lastSeq[nodeId] = static_cast<int64_t> (seq);
  for (const auto &kv : m_lastSeq)
    {
      if (kv.second < static_cast<int64_t> (seq))
        {
          return; // some node has not emitted this period yet
        }
    }
  if (static_cast<int64_t> (seq) <= m_appliedSeq)
    {
      return;
    }
  DrainAll ();
  Apply (seq);
}

void
E2ControlHub::DrainAll ()
{
  for (const auto &kv : m_emitters)
    {
      kv.second->DeliverInbound ();
    }
}

void
E2ControlHub::Apply (uint64_t seq)
{
  m_appliedSeq = static_cast<int64_t> (seq);
  if (m_buffered.empty ())
    {
      return;
    }
  std::vector<E2IpcControl> batch;
  batch.swap (m_buffered);
  std::sort (batch.begin (), batch.end (), [] (const E2IpcControl &a, const E2IpcControl &b) {
    return std::tie (a.imsi, a.viaNodeId, a.targetCellId, a.ctlId) <
           std::tie (b.imsi, b.viaNodeId, b.targetCellId, b.ctlId);
  });
  const int64_t now = Simulator::Now ().GetNanoSeconds ();
  for (const E2IpcControl &c : batch)
    {
      NS_ABORT_MSG_IF (m_handler.IsNull (),
                       "E2 control received but no master node registered a handover handler");
      E2ControlResult res = m_handler (c.imsi, c.targetCellId);
      Answer (c, m_appliedSeq, now, res.applied, res.applied ? "" : res.cause, res.sourceCellId);
    }
}

void
E2ControlHub::Answer (const E2IpcControl &c, int64_t rxSeq, int64_t rxNs, bool applied,
                      const std::string &cause, uint16_t sourceCellId)
{
  E2IpcJsonWriter l;
  l.BeginObject ();
  l.Key ("rxSeq");
  l.Int (rxSeq);
  l.Key ("rxSimTimeNs");
  l.Int (rxNs);
  l.Key ("applySimTimeNs");
  l.Int (rxNs); // d = 0: a control applies at the period boundary it is received at
  l.Key ("viaNodeId");
  l.Int (c.viaNodeId);
  l.Key ("ctlId");
  l.UInt (c.ctlId);
  l.Key ("afterSeq");
  l.Int (c.afterSeq);
  l.Key ("imsi");
  l.UInt (c.imsi);
  l.Key ("sourceCell");
  l.Int (sourceCellId);
  l.Key ("targetCell");
  l.Int (c.targetCellId);
  l.Key ("outcome");
  l.String (applied ? "applied" : "refused");
  l.Key ("cause");
  l.String (cause);
  l.EndObject ();
  WriteLedger (l.Str () + "\n");

  E2IpcJsonWriter a;
  a.BeginObject ();
  a.Key ("type");
  a.String ("controlAck");
  a.Key ("v");
  a.Int (1);
  a.Key ("nodeId");
  a.Int (c.viaNodeId);
  a.Key ("ctlId");
  a.UInt (c.ctlId);
  a.Key ("outcome");
  a.String (applied ? "applied" : "refused");
  a.Key ("cause");
  a.String (cause);
  a.Key ("seq");
  a.Int (rxSeq);
  a.Key ("applySimTimeNs");
  a.Int (rxNs);
  a.Key ("sourceCellId");
  a.Int (sourceCellId);
  a.EndObject ();
  auto it = m_emitters.find (c.viaNodeId);
  if (it != m_emitters.end ())
    {
      it->second->SendLine (a.Str () + "\n");
    }
}

void
E2ControlHub::WriteLedger (const std::string &line)
{
  if (m_ledger == nullptr)
    {
      std::string path = m_ledgerPath;
      if (path.empty ())
        {
          path = (m_defaultTraceDir.empty () ? std::string () : m_defaultTraceDir + "/") +
                 "e2-control-ledger.jsonl";
        }
      m_ledger = std::fopen (path.c_str (), "w");
      NS_ABORT_MSG_IF (m_ledger == nullptr, "Can't open file " << path);
      const std::string header =
          "{\"type\":\"ledgerHeader\",\"v\":1,\"delta-model\":\"fixed 0\"}\n";
      std::fwrite (header.data (), 1, header.size (), m_ledger);
    }
  if (std::fwrite (line.data (), 1, line.size (), m_ledger) != line.size () ||
      std::fflush (m_ledger) != 0)
    {
      NS_FATAL_ERROR ("Can't write the control ledger");
    }
}

void
E2ControlHub::OnEmitterStopping (uint16_t nodeId)
{
  if (!m_stopping)
    {
      m_stopping = true;
      DrainAll (); // buffered controls get answered below; later arrivals in OnInboundLine
      std::vector<E2IpcControl> batch;
      batch.swap (m_buffered);
      const int64_t now = Simulator::Now ().GetNanoSeconds ();
      for (const E2IpcControl &c : batch)
        {
          Answer (c, m_appliedSeq, now, false, "late", 0);
        }
    }
  m_emitters.erase (nodeId);
}

} // namespace ns3
