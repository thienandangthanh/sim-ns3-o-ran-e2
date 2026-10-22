/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Inbound E2 control path of the ns3-e2-ipc stream (v1 minor 1): RC handover
 * controls that the out-of-process agents forward, applied once per E2 period
 * and recorded in a control ledger; optionally paced in lock-step (minor 2).
 */

#ifndef E2_CONTROL_HUB_H
#define E2_CONTROL_HUB_H

#include "ns3/callback.h"
#include "ns3/object.h"

#include <ns3/e2-pacer-link.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

namespace ns3 {

class E2IpcEmitter;

/** One `control` line as received from an agent (or injected by the pacer). */
struct E2IpcControl
{
  uint16_t viaNodeId{0};    //!< node whose agent forwarded the control
  uint64_t ctlId{0};        //!< agent-monotonic id; the agent's pending key
  uint64_t imsi{0};         //!< UE (decimal IMSI)
  uint16_t targetCellId{0}; //!< target secondary cell
  int64_t afterSeq{-1};     //!< last sample seq the agent had forwarded
  bool fromPacer{false};    //!< injected by the pacer (scripted lock-step, replay)
};

/** What the handover handler of the master node did with one control. */
struct E2ControlResult
{
  bool applied{false};
  std::string cause;         //!< refusal cause (ledger name); empty when applied
  uint16_t sourceCellId{0};  //!< UE's secondary cell before the control
};

/**
 * Process-wide hub between the per-node emitters and the master node.
 *
 * Emitters hand every inbound line to the hub. When the last registered
 * emitter has emitted sample seq k, the buffered controls are applied
 * synchronously at the current simulation time (actuation delay d = 0), in
 * the order (imsi, viaNodeId, targetCellId, ctlId), through the handler
 * registered by the LTE master device. Each control gets one ledger line and
 * one `controlAck`. Controls still buffered or arriving when the emitters stop
 * are answered `refused` with cause `late` and never applied.
 *
 * Pacing "free" (default): the controls are applied right after the last
 * emitter's sample k; without controls the hub writes nothing and schedules
 * nothing, so free-run output is unchanged.
 *
 * Pacing "lockstep": ns-3 connects to the pacer. A start barrier (seq -1)
 * waits for every agent's `step` -1 (READY with a subscription) and the
 * pacer's `go` -1 before the first sample; after sample k a barrier scheduled
 * with ScheduleNow flushes every emitter, reports `ready` k to the pacer and
 * blocks the simulation until every node has sent `step` k (or its connection
 * is closed) and the pacer has sent `go` k, buffering every control that
 * arrives. Once every node's step k is in, the pacer gets `steps` k: from then
 * on an agent has read sample k, so a control it forwards carries afterSeq >= k.
 * Then the controls are applied as above, except that a control with
 * afterSeq < k missed its own barrier and is answered `refused`/`late`
 * without being applied. Deadlines (freeze, then fail): the start barrier
 * allows StartTimeout for the agents' steps, every other barrier GateTimeout,
 * counted from the barrier's entry, only while the simulation is blocked;
 * the pacer owns T_ctl and W and may send `abort`. Any abort, or a lost pacer
 * link, ends the run with exit code 5 and e2-pacing-abort.json naming the step.
 */
class E2ControlHub : public Object
{
public:
  /** Applies one control; returns the outcome (called on the simulation thread). */
  typedef Callback<E2ControlResult, uint64_t, uint16_t> HandoverHandler;

  static TypeId GetTypeId ();

  /** The hub of this process, created on first use. */
  static Ptr<E2ControlHub> Get ();

  E2ControlHub ();
  ~E2ControlHub () override;

  /** Registered by the LTE master device, which owns the UE contexts. */
  void SetHandoverHandler (HandoverHandler handler);

  /** Called by every emitter in Start. traceDir is the emitter's trace directory. */
  void RegisterEmitter (uint16_t nodeId, E2IpcEmitter *emitter, const std::string &traceDir);

  /** An inbound line received from the agent of viaNodeId. */
  void OnInboundLine (uint16_t viaNodeId, const std::string &line);

  /** Emitter nodeId has emitted sample seq (after its inbound lines were delivered). */
  void OnSampleEmitted (uint16_t nodeId, uint64_t seq);

  /** Emitter nodeId is stopping: everything still buffered is answered late. */
  void OnEmitterStopping (uint16_t nodeId);

  /** "free" or "lockstep" (sent in every hello). */
  std::string GetPacing () const;
  double GetStartTimeoutS () const;
  double GetGateTimeoutS () const;

private:
  void Apply (uint64_t seq);
  void Answer (const E2IpcControl &c, int64_t rxSeq, int64_t rxNs, bool applied,
               const std::string &cause, uint16_t sourceCellId);
  void WriteLedger (const std::string &line);
  void WriteLog (const std::string &line);
  void DrainAll ();
  std::string TraceDirFile (const std::string &name) const;

  // lock-step
  void StartBarrier ();
  void StepBarrier (uint64_t seq);
  void Barrier (int64_t seq, double timeoutS);
  void OnPacerLine (const std::string &line);
  [[noreturn]] void Abort (int64_t seq, const std::string &reason);

  std::string m_ledgerPath;
  std::string m_pacing;
  std::string m_pacerHost;
  uint16_t m_pacerPort{0};
  double m_startTimeoutS{120};
  double m_gateTimeoutS{10};
  std::string m_defaultTraceDir;
  std::FILE *m_ledger{nullptr};
  std::FILE *m_log{nullptr};
  HandoverHandler m_handler;
  std::map<uint16_t, E2IpcEmitter *> m_emitters;
  std::map<uint16_t, int64_t> m_lastSeq; //!< last emitted seq per registered node
  int64_t m_appliedSeq{-1};              //!< last seq whose controls were applied
  std::vector<E2IpcControl> m_buffered;
  bool m_stopping{false};
  uint64_t m_badLines{0};

  E2PacerLink m_pacer;
  bool m_startScheduled{false};
  int64_t m_barrierSeq{-2};              //!< seq of the barrier in progress, -2 if none
  std::chrono::steady_clock::time_point m_barrierEntry; //!< wall time the barrier blocked
  std::map<uint16_t, int64_t> m_stepSeq; //!< last `step` seq per node
  int64_t m_goSeq{-2};                   //!< last `go` seq from the pacer
  std::string m_pacerAbort;              //!< reason of a pacer `abort`
  bool m_endAck{false};                  //!< the pacer acknowledged the stop handshake
};

} // namespace ns3

#endif /* E2_CONTROL_HUB_H */
