/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Inbound E2 control path of the ns3-e2-ipc stream (v1 minor 1, pacing minor 2).
 */

#include <ns3/e2-control-hub.h>

#include <ns3/abort.h>
#include <ns3/double.h>
#include <ns3/e2-ipc-emitter.h>
#include <ns3/e2-ipc-json-reader.h>
#include <ns3/e2-ipc-json-writer.h>
#include <ns3/log.h>
#include <ns3/simulator.h>
#include <ns3/string.h>
#include <ns3/uinteger.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <poll.h>
#include <tuple>

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("E2ControlHub");

NS_OBJECT_ENSURE_REGISTERED (E2ControlHub);

namespace {
Ptr<E2ControlHub> g_hub;

/** Exit code of a run ended by a pacing abort (freeze, then fail). */
constexpr int kPacingAbortExit = 5;
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
                         MakeStringChecker ())
          .AddAttribute ("Pacing", "free (default) or lockstep (paced by the harness pacer)",
                         StringValue ("free"), MakeStringAccessor (&E2ControlHub::m_pacing),
                         MakeStringChecker ())
          .AddAttribute ("PacerHost", "Host of the lock-step pacer", StringValue ("127.0.0.1"),
                         MakeStringAccessor (&E2ControlHub::m_pacerHost), MakeStringChecker ())
          .AddAttribute ("PacerPort", "TCP port of the lock-step pacer", UintegerValue (38499),
                         MakeUintegerAccessor (&E2ControlHub::m_pacerPort),
                         MakeUintegerChecker<uint16_t> ())
          .AddAttribute ("StartTimeout",
                         "T_start (s): the start barrier's wait for every agent's step -1",
                         DoubleValue (120), MakeDoubleAccessor (&E2ControlHub::m_startTimeoutS),
                         MakeDoubleChecker<double> (0))
          .AddAttribute ("GateTimeout",
                         "T_gate (s): every other barrier's wait for every agent's step k",
                         DoubleValue (10), MakeDoubleAccessor (&E2ControlHub::m_gateTimeoutS),
                         MakeDoubleChecker<double> (0));
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
  if (m_log != nullptr)
    {
      std::fclose (m_log);
    }
}

std::string
E2ControlHub::GetPacing () const
{
  return m_pacing;
}

double
E2ControlHub::GetStartTimeoutS () const
{
  return m_startTimeoutS;
}

double
E2ControlHub::GetGateTimeoutS () const
{
  return m_gateTimeoutS;
}

void
E2ControlHub::SetHandoverHandler (HandoverHandler handler)
{
  m_handler = handler;
}

void
E2ControlHub::RegisterEmitter (uint16_t nodeId, E2IpcEmitter *emitter, const std::string &traceDir)
{
  NS_ABORT_MSG_IF (m_pacing != "free" && m_pacing != "lockstep",
                   "ns3::E2ControlHub::Pacing must be free or lockstep, not " << m_pacing);
  if (m_emitters.empty ())
    {
      m_defaultTraceDir = traceDir;
    }
  m_emitters[nodeId] = emitter;
  m_lastSeq[nodeId] = -1;
  m_stepSeq[nodeId] = -2;
  if (m_pacing == "lockstep" && !m_startScheduled)
    {
      // Every emitter starts at t = 0 and emits its first sample at 0.5 ms or
      // later, so the start barrier at 1 us sees them all and precedes seq 0.
      m_startScheduled = true;
      Simulator::Schedule (MicroSeconds (1), &E2ControlHub::StartBarrier, this);
    }
}

void
E2ControlHub::OnInboundLine (uint16_t viaNodeId, const std::string &line)
{
  E2IpcJsonReader r;
  if (!r.Parse (line))
    {
      m_badLines++;
      NS_LOG_WARN ("E2 IPC node " << viaNodeId << ": ignored inbound line " << line);
      return;
    }
  const std::string type = r.GetString ("type");
  if (type == "step")
    {
      if (r.GetInt ("nodeId") == viaNodeId && r.HasInt ("seq"))
        {
          m_stepSeq[viaNodeId] = std::max (m_stepSeq[viaNodeId], r.GetInt ("seq"));
          WriteLog (line + "\n");
        }
      return;
    }
  if (type != "control")
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
  if (m_pacer.IsOpen ())
    {
      E2IpcJsonWriter w;
      w.BeginObject ();
      w.Key ("type");
      w.String ("controlRx");
      w.Key ("seq");
      w.Int (m_barrierSeq != -2 ? m_barrierSeq : m_appliedSeq);
      w.Key ("viaNodeId");
      w.Int (c.viaNodeId);
      w.Key ("ctlId");
      w.UInt (c.ctlId);
      w.Key ("buffered");
      w.UInt (m_buffered.size ());
      w.EndObject ();
      m_pacer.Send (w.Str () + "\n");
    }
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
  if (m_pacing == "lockstep")
    {
      m_appliedSeq = static_cast<int64_t> (seq); // claimed: one barrier per seq
      Simulator::ScheduleNow (&E2ControlHub::StepBarrier, this, seq);
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
  std::vector<E2IpcControl> batch;
  batch.swap (m_buffered);
  std::sort (batch.begin (), batch.end (), [] (const E2IpcControl &a, const E2IpcControl &b) {
    return std::tie (a.imsi, a.viaNodeId, a.targetCellId, a.ctlId) <
           std::tie (b.imsi, b.viaNodeId, b.targetCellId, b.ctlId);
  });
  const int64_t now = Simulator::Now ().GetNanoSeconds ();
  uint64_t late = 0;
  for (const E2IpcControl &c : batch)
    {
      if (m_pacing == "lockstep" && c.afterSeq < static_cast<int64_t> (seq))
        {
          // Lock-step applies a control at the barrier of its afterSeq; one that
          // reaches a later barrier missed its own and is refused, never applied
          Answer (c, m_appliedSeq, now, false, "late", 0);
          late++;
          continue;
        }
      NS_ABORT_MSG_IF (m_handler.IsNull (),
                       "E2 control received but no master node registered a handover handler");
      E2ControlResult res = m_handler (c.imsi, c.targetCellId);
      Answer (c, m_appliedSeq, now, res.applied, res.applied ? "" : res.cause, res.sourceCellId);
    }
  if (m_pacer.IsOpen ())
    {
      m_pacer.Send ("{\"type\":\"applied\",\"seq\":" + std::to_string (seq) +
                    ",\"controls\":" + std::to_string (batch.size ()) +
                    ",\"late\":" + std::to_string (late) + "}\n");
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
  if (c.fromPacer)
    {
      m_pacer.Send (a.Str () + "\n");
      return;
    }
  auto it = m_emitters.find (c.viaNodeId);
  if (it != m_emitters.end ())
    {
      it->second->SendLine (a.Str () + "\n");
    }
}

std::string
E2ControlHub::TraceDirFile (const std::string &name) const
{
  return (m_defaultTraceDir.empty () ? std::string () : m_defaultTraceDir + "/") + name;
}

void
E2ControlHub::WriteLedger (const std::string &line)
{
  if (m_ledger == nullptr)
    {
      std::string path = m_ledgerPath.empty () ? TraceDirFile ("e2-control-ledger.jsonl") : m_ledgerPath;
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
E2ControlHub::WriteLog (const std::string &line)
{
  if (m_log == nullptr)
    {
      std::string path = TraceDirFile ("e2-pacing-log.jsonl");
      m_log = std::fopen (path.c_str (), "w");
      NS_ABORT_MSG_IF (m_log == nullptr, "Can't open file " << path);
    }
  std::fwrite (line.data (), 1, line.size (), m_log);
  std::fflush (m_log);
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
      if (m_pacer.IsOpen ())
        {
          // Stop handshake: every line the pacer sends before endAck is handled (a
          // control is answered late); waits at most GateTimeout
          m_pacer.Send ("{\"type\":\"stopping\",\"seq\":" + std::to_string (m_appliedSeq) + "}\n");
          const auto deadline = std::chrono::steady_clock::now () +
                                std::chrono::milliseconds (static_cast<int64_t> (m_gateTimeoutS * 1000));
          while (!m_endAck && m_pacer.IsOpen () && std::chrono::steady_clock::now () < deadline)
            {
              pollfd pfd{m_pacer.GetFd (), POLLIN, 0};
              poll (&pfd, 1, 20);
              std::vector<std::string> lines;
              m_pacer.Read (lines);
              for (const std::string &l : lines)
                {
                  OnPacerLine (l);
                }
            }
          m_pacer.Send ("{\"type\":\"end\",\"seq\":" + std::to_string (m_appliedSeq) + "}\n");
        }
    }
  m_emitters.erase (nodeId);
}

// ---------------------------------------------------------------------------
// lock-step
// ---------------------------------------------------------------------------

void
E2ControlHub::StartBarrier ()
{
  if (!m_pacer.Connect (m_pacerHost, m_pacerPort,
                        std::chrono::milliseconds (static_cast<int64_t> (m_startTimeoutS * 1000))))
    {
      Abort (-1, "no pacer at " + m_pacerHost + ":" + std::to_string (m_pacerPort));
    }
  Barrier (-1, m_startTimeoutS);
}

void
E2ControlHub::StepBarrier (uint64_t seq)
{
  Barrier (static_cast<int64_t> (seq), m_gateTimeoutS);
  Apply (seq);
}

void
E2ControlHub::Barrier (int64_t seq, double timeoutS)
{
  using Clock = std::chrono::steady_clock;
  m_barrierSeq = seq;
  m_barrierEntry = Clock::now (); // the simulation is blocked from here; deadlines count from here
  for (const auto &kv : m_emitters)
    {
      kv.second->FlushOutbound (std::chrono::milliseconds (2000));
    }
  E2IpcJsonWriter w;
  w.BeginObject ();
  w.Key ("type");
  w.String ("ready");
  w.Key ("seq");
  w.Int (seq);
  w.Key ("simTimeNs");
  w.Int (Simulator::Now ().GetNanoSeconds ());
  w.Key ("buffered");
  w.UInt (m_buffered.size ());
  w.EndObject ();
  if (!m_pacer.Send (w.Str () + "\n"))
    {
      Abort (seq, "pacer link lost");
    }
  const auto t0 = m_barrierEntry;
  double stepsWaitS = -1;
  while (true)
    {
      std::vector<pollfd> fds;
      for (const auto &kv : m_emitters)
        {
          if (kv.second->GetFd () >= 0)
            {
              fds.push_back ({kv.second->GetFd (), POLLIN, 0});
            }
        }
      fds.push_back ({m_pacer.GetFd (), POLLIN, 0});
      poll (fds.data (), fds.size (), 20);
      DrainAll ();
      std::vector<std::string> lines;
      bool open = m_pacer.Read (lines);
      for (const std::string &l : lines)
        {
          OnPacerLine (l);
        }
      if (!m_pacerAbort.empty ())
        {
          Abort (seq, "pacer: " + m_pacerAbort);
        }
      if (!open)
        {
          Abort (seq, "pacer link lost");
        }
      const double elapsed = std::chrono::duration<double> (Clock::now () - t0).count ();
      std::string missing;
      for (const auto &kv : m_emitters)
        {
          // A node whose connection closed has hit EOF and counts as done, except at the
          // start barrier, where every agent must have connected and become ready
          bool done = m_stepSeq[kv.first] >= seq || (seq >= 0 && !kv.second->IsConnected ());
          if (!done)
            {
              missing += (missing.empty () ? "" : ",") + std::to_string (kv.first);
            }
        }
      if (missing.empty () && stepsWaitS < 0)
        {
          // Every agent has read sample seq: a control it forwards from now on
          // carries afterSeq >= seq, so the pacer may start a trigger
          stepsWaitS = elapsed;
          m_pacer.Send ("{\"type\":\"steps\",\"seq\":" + std::to_string (seq) + "}\n");
        }
      if (missing.empty () && m_goSeq >= seq)
        {
          break;
        }
      if (!missing.empty () && elapsed > timeoutS)
        {
          Abort (seq, "no step " + std::to_string (seq) + " from node(s) " + missing +
                          " within " + std::to_string (timeoutS) + " s");
        }
    }
  const double totalS = std::chrono::duration<double> (Clock::now () - t0).count ();
  WriteLog ("{\"type\":\"barrier\",\"seq\":" + std::to_string (seq) + ",\"stepsWaitS\":" +
            std::to_string (stepsWaitS) + ",\"totalWaitS\":" + std::to_string (totalS) +
            ",\"timeoutS\":" + std::to_string (timeoutS) + "}\n");
  m_barrierSeq = -2;
}

void
E2ControlHub::OnPacerLine (const std::string &line)
{
  E2IpcJsonReader r;
  if (!r.Parse (line))
    {
      m_pacerAbort = "unparsable pacer line " + line;
      return;
    }
  const std::string type = r.GetString ("type");
  if (type == "go" && r.HasInt ("seq"))
    {
      m_goSeq = std::max (m_goSeq, r.GetInt ("seq"));
    }
  else if (type == "endAck")
    {
      m_endAck = true;
    }
  else if (type == "abort")
    {
      m_pacerAbort = r.GetString ("reason").empty () ? "abort" : r.GetString ("reason");
    }
  else if (type == "control" && r.HasInt ("nodeId") && r.HasInt ("ctlId") && r.HasInt ("imsi") &&
           r.HasInt ("targetCellId"))
    {
      // Scripted lock-step (Fallback-1) and ledger replay: the pacer stands in for the agents
      E2IpcControl c;
      c.viaNodeId = static_cast<uint16_t> (r.GetInt ("nodeId"));
      c.ctlId = static_cast<uint64_t> (r.GetInt ("ctlId"));
      c.imsi = static_cast<uint64_t> (r.GetInt ("imsi"));
      c.targetCellId = static_cast<uint16_t> (r.GetInt ("targetCellId"));
      c.afterSeq = r.HasInt ("afterSeq") ? r.GetInt ("afterSeq") : -1;
      c.fromPacer = true;
      if (m_stopping)
        {
          Answer (c, m_appliedSeq, Simulator::Now ().GetNanoSeconds (), false, "late", 0);
        }
      else
        {
          m_buffered.push_back (c);
        }
    }
  else
    {
      m_pacerAbort = "unexpected pacer line " + line;
    }
}

void
E2ControlHub::Abort (int64_t seq, const std::string &reason)
{
  std::string path = TraceDirFile ("e2-pacing-abort.json");
  if (std::FILE *f = std::fopen (path.c_str (), "w"))
    {
      E2IpcJsonWriter w;
      w.BeginObject ();
      w.Key ("abortStep");
      w.Int (seq);
      w.Key ("simTimeNs");
      w.Int (Simulator::Now ().GetNanoSeconds ());
      w.Key ("reason");
      w.String (reason);
      w.Key ("blockedToAbortWallS");
      w.Double (m_barrierSeq == -2
                    ? -1.0
                    : std::chrono::duration<double> (std::chrono::steady_clock::now () -
                                                     m_barrierEntry)
                          .count ());
      w.EndObject ();
      std::string s = w.Str () + "\n";
      std::fwrite (s.data (), 1, s.size (), f);
      std::fclose (f);
    }
  if (m_pacer.IsOpen ())
    {
      m_pacer.Send ("{\"type\":\"abort\",\"seq\":" + std::to_string (seq) + "}\n");
    }
  if (m_ledger != nullptr)
    {
      std::fflush (m_ledger);
    }
  if (m_log != nullptr)
    {
      std::fflush (m_log);
    }
  std::cerr << "E2 pacing abort at step " << seq << ": " << reason << std::endl;
  std::_Exit (kPacingAbortExit);
}

} // namespace ns3
