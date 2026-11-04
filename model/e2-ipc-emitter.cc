/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Per-E2-node emitter of the ns3-e2-ipc v1 stream.
 */

#include <ns3/e2-ipc-emitter.h>

#include <ns3/abort.h>
#include <ns3/e2-control-hub.h>
#include <ns3/e2-ipc-tcp-client.h>
#include <ns3/log.h>
#include <ns3/simulator.h>

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <unistd.h>

// Commit SHAs of the two repositories, recorded at configure time (CMakeLists.txt)
#ifndef E2_IPC_NS3_SHA
#define E2_IPC_NS3_SHA ""
#endif
#ifndef E2_IPC_ORAN_INTERFACE_SHA
#define E2_IPC_ORAN_INTERFACE_SHA ""
#endif

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("E2IpcEmitter");

NS_OBJECT_ENSURE_REGISTERED (E2IpcEmitter);

namespace {

/** The environment variable wins over the compiled-in value; "unknown" if neither is set. */
std::string
EmitterSha (const char *envName, const char *compiled)
{
  const char *env = std::getenv (envName);
  if (env != nullptr && *env != '\0')
    {
      return env;
    }
  return *compiled != '\0' ? compiled : "unknown";
}

} // namespace

TypeId
E2IpcEmitter::GetTypeId ()
{
  static TypeId tid = TypeId ("ns3::E2IpcEmitter").SetParent<Object> ().AddConstructor<E2IpcEmitter> ();
  return tid;
}

E2IpcEmitter::E2IpcEmitter () : m_host ("127.0.0.1")
{
}

E2IpcEmitter::~E2IpcEmitter ()
{
  Stop ();
}

void
E2IpcEmitter::SetEndpoint (const std::string &host, uint16_t port)
{
  m_host = host;
  m_port = port;
}

void
E2IpcEmitter::SetTraceDir (const std::string &dir)
{
  m_traceDir = dir;
}

void
E2IpcEmitter::Start (const E2IpcNodeInfo &info)
{
  NS_ABORT_MSG_IF (m_started, "E2IpcEmitter started twice for node " << info.nodeId);
  m_info = info;
  Ptr<E2ControlHub> hub = E2ControlHub::Get ();
  m_info.pacing = hub->GetPacing ();
  m_info.startTimeoutS = hub->GetStartTimeoutS ();
  m_info.gateTimeoutS = hub->GetGateTimeoutS ();
  m_runId = std::to_string (info.simStartEpochMs) + "-" + std::to_string (getpid ());
  const std::string dirPrefix = m_traceDir.empty () ? std::string () : m_traceDir + "/";
  m_tracePath = dirPrefix + "e2-ipc-trace-" + std::to_string (info.nodeId) + ".jsonl";
  m_trace = std::fopen (m_tracePath.c_str (), "w");
  NS_ABORT_MSG_IF (m_trace == nullptr, "Can't open file " << m_tracePath);
  m_ueContextPath = dirPrefix + "e2-ue-context-" + std::to_string (info.nodeId) + ".jsonl";
  m_ueContextFile = std::fopen (m_ueContextPath.c_str (), "w");
  NS_ABORT_MSG_IF (m_ueContextFile == nullptr, "Can't open file " << m_ueContextPath);

  m_client = std::make_unique<E2IpcTcpClient> (m_host, m_port, MAX_PENDING_BYTES);
  // One short wait so a local agent's connect completes before the first sample
  m_client->WaitConnected (std::chrono::milliseconds (200));
  m_started = true;
  E2ControlHub::Get ()->RegisterEmitter (m_info.nodeId, this, m_traceDir, m_info.periodNs);
  SendHelloIfNewConnection ();
  std::clog << "E2IPC node " << m_info.nodeId << " (" << m_info.kind << ") -> " << m_host << ":"
            << m_port << (m_client->IsConnected () ? " connected" : " not connected (will retry)")
            << ", trace " << m_tracePath << std::endl;

  // Holding a Ptr keeps the emitter alive until the bye is sent
  Simulator::ScheduleDestroy (&E2IpcEmitter::Stop, Ptr<E2IpcEmitter> (this));
}

void
E2IpcEmitter::EmitSample (Ptr<KpmIndicationMessage> du, Ptr<KpmIndicationMessage> cuUp,
                          Ptr<KpmIndicationMessage> cuCp, std::vector<E2IpcUeContext> ueContexts)
{
  NS_ABORT_MSG_IF (!m_started || m_stopped, "E2IpcEmitter of node " << m_info.nodeId
                                                                    << " is not running");
  DeliverInbound ();

  // After DeliverInbound, so a new connection's hello goes first. The agent of a new
  // connection has no context yet, and one the pending buffer refused never reached it.
  std::sort (ueContexts.begin (), ueContexts.end ());
  if (!m_ueContextSent || m_ueContextRefused || ueContexts != m_lastUeContexts ||
      m_client->GetConnectCount () != m_ueContextConnects)
    {
      m_lastUeContexts = std::move (ueContexts);
      // Its seq is the one of the sample that follows
      SendUeContext (EncodeE2IpcUeContext (m_info.nodeId, m_counters.built, m_lastUeContexts) +
                     "\n");
    }

  const uint64_t seq = m_counters.built++;
  const uint64_t dropped = m_counters.droppedDisconnected + m_counters.droppedBackpressure;
  std::string line = EncodeE2IpcSample (m_info.nodeId, seq, Simulator::Now ().GetNanoSeconds (),
                                        dropped, du, cuUp, cuCp);
  line += '\n';
  WriteTrace (line);
  switch (m_client->Enqueue (line, true))
    {
    case E2IpcTcpClient::EnqueueResult::Disconnected:
      m_counters.droppedDisconnected++;
      break;
    case E2IpcTcpClient::EnqueueResult::Backpressure:
      m_counters.droppedBackpressure++;
      break;
    case E2IpcTcpClient::EnqueueResult::Queued:
      break;
    }
  CollectLostSamples ();
  m_counters.sent = m_client->GetSentSamples ();
  E2ControlHub::Get ()->OnSampleEmitted (m_info.nodeId, seq);
}

void
E2IpcEmitter::DeliverInbound ()
{
  m_client->Service ();
  CollectLostSamples ();
  SendHelloIfNewConnection ();
  std::vector<std::string> lines;
  m_client->TakeInboundLines (lines);
  for (const std::string &l : lines)
    {
      E2ControlHub::Get ()->OnInboundLine (m_info.nodeId, l);
    }
}

void
E2IpcEmitter::SendLine (const std::string &line)
{
  WriteTrace (line);
  m_client->Enqueue (line, false);
  CollectLostSamples ();
}

void
E2IpcEmitter::SendUeContext (const std::string &line)
{
  NS_ABORT_MSG_IF (!m_started || m_stopped, "E2IpcEmitter of node " << m_info.nodeId
                                                                    << " is not running");
  WriteUeContextFile (line);
  const E2IpcTcpClient::EnqueueResult res = m_client->Enqueue (line, false);
  // A line lost while disconnected is sent again after the next connect (connect count)
  m_ueContextSent = true;
  m_ueContextRefused = res == E2IpcTcpClient::EnqueueResult::Backpressure;
  m_ueContextConnects = m_client->GetConnectCount ();
  if (m_ueContextRefused)
    {
      NS_LOG_WARN ("Node " << m_info.nodeId << ": ueContext refused by backpressure, retried "
                                               "before the next sample");
    }
  CollectLostSamples ();
}

void
E2IpcEmitter::FlushOutbound (std::chrono::milliseconds timeout)
{
  m_client->Flush (timeout);
  CollectLostSamples ();
  m_counters.sent = m_client->GetSentSamples ();
}

int
E2IpcEmitter::GetFd () const
{
  return m_client ? m_client->GetFd () : -1;
}

bool
E2IpcEmitter::IsConnected () const
{
  return m_client && m_client->IsConnected ();
}

void
E2IpcEmitter::Stop ()
{
  if (!m_started || m_stopped)
    {
      return;
    }
  DeliverInbound ();
  E2ControlHub::Get ()->OnEmitterStopping (m_info.nodeId);
  m_stopped = true;
  m_client->Flush (std::chrono::milliseconds (2000));
  CollectLostSamples ();
  // Samples still unsent after the end-of-run flush never reach the agent
  const uint64_t stuck = m_client->GetPendingSamples ();
  m_counters.droppedDisconnected += stuck;
  m_counters.sent = m_client->GetSentSamples ();

  std::string bye = EncodeE2IpcBye (m_info.nodeId, m_counters) + "\n";
  WriteTrace (bye);
  if (stuck == 0)
    {
      m_client->Enqueue (bye, false);
      m_client->Flush (std::chrono::milliseconds (1000));
    }
  std::clog << "E2IPC node " << m_info.nodeId << " (" << m_info.kind
            << ") built=" << m_counters.built << " sent=" << m_counters.sent
            << " droppedDisconnected=" << m_counters.droppedDisconnected
            << " droppedBackpressure=" << m_counters.droppedBackpressure
            << " connects=" << m_client->GetConnectCount () << " trace=" << m_tracePath
            << std::endl;
  m_client.reset ();
  std::fclose (m_trace);
  m_trace = nullptr;
  std::fclose (m_ueContextFile);
  m_ueContextFile = nullptr;
}

const E2IpcCounters &
E2IpcEmitter::GetCounters () const
{
  return m_counters;
}

void
E2IpcEmitter::DoDispose ()
{
  Stop ();
  Object::DoDispose ();
}

void
E2IpcEmitter::WriteTrace (const std::string &line)
{
  if (std::fwrite (line.data (), 1, line.size (), m_trace) != line.size () ||
      std::fflush (m_trace) != 0)
    {
      NS_FATAL_ERROR ("Can't write file " << m_tracePath);
    }
}

void
E2IpcEmitter::WriteUeContextFile (const std::string &line)
{
  if (std::fwrite (line.data (), 1, line.size (), m_ueContextFile) != line.size () ||
      std::fflush (m_ueContextFile) != 0)
    {
      NS_FATAL_ERROR ("Can't write file " << m_ueContextPath);
    }
}

void
E2IpcEmitter::SendHelloIfNewConnection ()
{
  if (!m_client->TakeNewConnection ())
    {
      return;
    }
  std::string hello = EncodeE2IpcHello (m_info, m_runId,
                                        EmitterSha ("NS3_E2_IPC_NS3_SHA", E2_IPC_NS3_SHA),
                                        EmitterSha ("NS3_E2_IPC_ORAN_INTERFACE_SHA",
                                                    E2_IPC_ORAN_INTERFACE_SHA)) +
                      "\n";
  WriteTrace (hello);
  m_client->Enqueue (hello, false);
  CollectLostSamples ();
}

void
E2IpcEmitter::CollectLostSamples ()
{
  m_counters.droppedDisconnected += m_client->TakeLostSamples ();
}

} // namespace ns3
