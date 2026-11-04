/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Per-E2-node emitter of the ns3-e2-ipc v1 stream (replaces the in-process
 * e2sim E2Termination): ns-3 -> out-of-process E2 agent, NDJSON over TCP.
 */

#ifndef E2_IPC_EMITTER_H
#define E2_IPC_EMITTER_H

#include "ns3/object.h"
#include <ns3/e2-ipc-message-encoder.h>
#include <ns3/kpm-indication.h>

#include <chrono>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace ns3 {

class E2IpcTcpClient;

/**
 * One emitter per E2 node (mmWave gNB or LTE eNB, nodeId = cellId).
 *
 * After Start it sends hello on every (re)connect, one sample per E2 period
 * (EmitSample, DU + CU-UP + CU-CP merged) and bye at Simulator::Destroy.
 * Every line is first written and flushed to the per-node trace file
 * e2-ipc-trace-<nodeId>.jsonl, dropped samples included, so the trace is the
 * ground truth of what ns-3 produced. Sending never blocks simulation time:
 * while the agent is unreachable or slow, samples are dropped and counted.
 *
 * Inbound `control` lines (v1 minor 1) go to the E2ControlHub, which answers
 * each with a `controlAck` sent (and traced) through this emitter.
 *
 * Before a sample, a `ueContext` line carries the node's UE contexts whenever
 * they differ from the last line sent, at the first sample, after every new
 * connection and after a line the pending buffer refused. ueContext lines are
 * written to e2-ue-context-<nodeId>.jsonl, never to the IPC trace, so the
 * trace and its samples do not depend on them.
 */
class E2IpcEmitter : public Object
{
public:
  /** Bound of unsent bytes per node; a sample that does not fit is dropped whole. */
  static constexpr size_t MAX_PENDING_BYTES = 4 * 1024 * 1024;

  static TypeId GetTypeId ();

  E2IpcEmitter ();
  ~E2IpcEmitter () override;

  /** Agent endpoint (E2AgentHost, E2AgentPortBase + cellId). */
  void SetEndpoint (const std::string &host, uint16_t port);

  /** Directory of the trace file; empty means the current directory. */
  void SetTraceDir (const std::string &dir);

  /** Open the trace and connect to the agent. Called once, at simulation start. */
  void Start (const E2IpcNodeInfo &info);

  /**
   * Build, trace and send one sample at the current simulation time, preceded
   * by a ueContext line when ueContexts (the node's UE contexts read right
   * before this sample, in any order) need one.
   */
  void EmitSample (Ptr<KpmIndicationMessage> du, Ptr<KpmIndicationMessage> cuUp,
                   Ptr<KpmIndicationMessage> cuCp, std::vector<E2IpcUeContext> ueContexts);

  /**
   * Flush, send bye, log the counters and close. Idempotent. Inbound lines are
   * read first only while the hub is not stopping yet.
   */
  void Stop ();

  const E2IpcCounters &GetCounters () const;

  /** Advance the connection and hand every complete inbound line to the E2ControlHub. */
  void DeliverInbound ();

  /** Trace and send one complete line (newline included) that is not a sample. */
  void SendLine (const std::string &line);

  /**
   * Write one complete ueContext line (newline included) to the UE context
   * file and send it; never written to the IPC trace.
   */
  void SendUeContext (const std::string &line);

  /** Wait up to timeout until every queued line is written (pacing barrier). */
  void FlushOutbound (std::chrono::milliseconds timeout);

  /** The agent socket, or -1 while not connected. */
  int GetFd () const;

  bool IsConnected () const;

protected:
  void DoDispose () override;

private:
  void WriteTrace (const std::string &line);
  void WriteUeContextFile (const std::string &line);
  void SendHelloIfNewConnection ();
  void CollectLostSamples ();

  std::string m_host;
  uint16_t m_port{0};
  std::string m_traceDir;
  std::string m_tracePath;
  E2IpcNodeInfo m_info;
  std::string m_runId;
  std::unique_ptr<E2IpcTcpClient> m_client;
  std::FILE *m_trace{nullptr};
  std::string m_ueContextPath;
  std::FILE *m_ueContextFile{nullptr};
  std::vector<E2IpcUeContext> m_lastUeContexts; //!< sorted, as last sent
  bool m_ueContextSent{false};                  //!< a ueContext line was sent
  bool m_ueContextRefused{false};               //!< the last one hit backpressure
  uint64_t m_ueContextConnects{0};              //!< connect count when it was sent
  E2IpcCounters m_counters;
  bool m_started{false};
  bool m_stopped{false};
};

} // namespace ns3

#endif /* E2_IPC_EMITTER_H */
