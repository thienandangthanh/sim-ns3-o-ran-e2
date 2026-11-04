/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Inbound E2 control path of the ns3-e2-ipc stream (v1 minor 1; UE ID form
 * minor 4): E2SM-RC Style 5 action 3 controls (DC PSCell change) that the
 * out-of-process agents forward, executed at the UE's source en-gNB, applied
 * once per E2 period and recorded in a control ledger; optionally paced in
 * lock-step (minor 2).
 */

#ifndef E2_CONTROL_HUB_H
#define E2_CONTROL_HUB_H

#include "ns3/callback.h"
#include "ns3/event-id.h"
#include "ns3/nstime.h"
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
class E2IpcJsonReader;

/** One `control` line as received from an agent (or injected by the pacer). */
struct E2IpcControl
{
  uint16_t viaNodeId{0};    //!< node whose agent forwarded the control
  uint64_t ctlId{0};        //!< agent-monotonic id; the agent's pending key
  uint16_t mEnbUeX2apId{0}; //!< ueId: the UE's MeNB UE X2AP ID (its RNTI at the master eNB)
  uint32_t mnCellId{0};     //!< ueId: the master eNB's macro eNB ID (its cell ID)
  uint64_t imsi{0};         //!< the UE that ueId names at viaNodeId, set by the hub; 0 if none
  uint16_t targetCellId{0}; //!< target secondary cell
  int64_t afterSeq{-1};     //!< last sample seq the agent had forwarded
  bool fromPacer{false};    //!< injected by the pacer (scripted lock-step, replay)
};

/**
 * Apply order of the controls decided together: (imsi, viaNodeId, targetCellId,
 * ctlId), on the IMSI the hub resolved at the control's node. Arrival order
 * plays no part: two controls for one UE through one node apply lower target
 * cell first, whatever their ctlIds.
 */
bool E2ControlApplyOrder (const E2IpcControl &a, const E2IpcControl &b);

/** What a node's control handler did with one control. */
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
 * emitter has emitted sample seq k, the buffered controls are decided at that
 * barrier, in E2ControlApplyOrder, and applied ActuationDelay (δ, sim time)
 * later through the control handler registered for the control's viaNodeId
 * (an en-gNB; the LTE eNB registers none): at δ = 0 synchronously at the
 * barrier (no event, so ties cannot reorder); when δ is a multiple of the E2
 * period T, at barrier k + δ/T, after that seq's samples and before that
 * barrier's own batch; otherwise in one event at t_k + δ. The precheck, the
 * handover, the ledger line and the `controlAck` of a control happen together
 * at apply time.
 *
 * UE resolution: a control names its UE by ueId {mEnbUeX2apId, mnCellId}. The
 * node's resolver maps it, reading state only, to the IMSI of the UE that the
 * node serves as its current secondary node, or to 0. Every control of a batch
 * is resolved before the batch is sorted, at the event that decides its
 * outcome: the barrier at δ = 0, the apply event at δ > 0 (the batch is
 * re-sorted there). A control with no resolved UE (or through a node with no
 * handler) is refused `unknownUe` with ledger imsi 0. A `late` line carries the
 * IMSI resolved when it was received: at its barrier, or, for a control
 * answered at the stop, at the stop (where the nodes have deregistered, so 0).
 *
 * A control still buffered or pending, or arriving, when the emitters stop is
 * answered `refused` with cause `late` once and never applied. Each inbound
 * control is recorded, as received, in e2-pacing-log.jsonl.
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
  /**
   * Maps (mEnbUeX2apId, mnCellId) to the IMSI of the UE that the node serves as
   * its current secondary node, or 0; reads state only.
   */
  typedef Callback<uint64_t, uint16_t, uint32_t> UeResolver;

  /** Applies one control to a resolved UE: (imsi, mnCellId, targetCellId). */
  typedef Callback<E2ControlResult, uint64_t, uint32_t, uint16_t> ControlHandler;

  /** Sort order of a batch (E2ControlApplyOrder unless a test swaps it). */
  typedef bool (*ApplyOrder) (const E2IpcControl &, const E2IpcControl &);

  /** Ledger cause of a control whose ueId resolves to no UE at its node. */
  static constexpr const char *kUnknownUe = "unknownUe";

  static TypeId GetTypeId ();

  /** The hub of this process, created on first use. */
  static Ptr<E2ControlHub> Get ();

  E2ControlHub ();
  ~E2ControlHub () override;

  /**
   * Registered by each en-gNB for its nodeId: controls forwarded through that
   * node are resolved and applied there. Null callbacks deregister the node.
   */
  void SetControlHandler (uint16_t nodeId, UeResolver resolver, ControlHandler handler);

  /** Replace the batch sort order; only a test that proves the order is asserted needs it. */
  void SetApplyOrder (ApplyOrder order);

  /** Called by every emitter in Start. traceDir is the emitter's trace directory. */
  void RegisterEmitter (uint16_t nodeId, E2IpcEmitter *emitter, const std::string &traceDir,
                        int64_t periodNs);

  /** An inbound line received from the agent of viaNodeId. */
  void OnInboundLine (uint16_t viaNodeId, const std::string &line);

  /** Emitter nodeId has emitted sample seq (after its inbound lines were delivered). */
  void OnSampleEmitted (uint16_t nodeId, uint64_t seq);

  /** Emitter nodeId is stopping: everything still buffered is answered late. */
  void OnEmitterStopping (uint16_t nodeId);

  /** The first emitter has stopped: inbound lines are no longer read. */
  bool IsStopping () const;

  /** "free" or "lockstep" (sent in every hello). */
  std::string GetPacing () const;
  double GetStartTimeoutS () const;
  double GetGateTimeoutS () const;

private:
  /** Controls decided at barrier rxSeq (sim time rxNs) whose apply is pending. */
  struct PendingBatch
  {
    std::vector<E2IpcControl> controls;
    int64_t rxSeq{-1};
    int64_t rxNs{0};
    EventId event; //!< set only for an event-scheduled batch (δ not a multiple of T)
  };

  /** The handlers of one node. */
  struct NodeHandler
  {
    UeResolver resolver;
    ControlHandler handler;
  };

  /** A control line's members, checked against the IPC control form; false if malformed. */
  static bool ParseControl (const E2IpcJsonReader &r, bool fromPacer, E2IpcControl &c);
  /** Record an inbound control, as received, in the pacing log. */
  void Receive (const E2IpcControl &c);
  void Resolve (E2IpcControl &c) const;
  void ResolveAndSort (std::vector<E2IpcControl> &batch) const;
  void Apply (uint64_t seq);
  void ApplyOne (const E2IpcControl &c, int64_t rxSeq, int64_t rxNs);
  void ApplyBatch (const PendingBatch &b);
  void ApplyDue (int64_t seq);
  void ApplyScheduled (uint64_t id);
  void Defer (std::vector<E2IpcControl> controls, int64_t rxSeq, int64_t rxNs);
  void Answer (const E2IpcControl &c, int64_t rxSeq, int64_t rxNs, int64_t applyNs, bool applied,
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
  std::map<uint16_t, NodeHandler> m_handlers; //!< control handler per viaNodeId
  ApplyOrder m_applyOrder{&E2ControlApplyOrder};
  std::map<uint16_t, E2IpcEmitter *> m_emitters;
  std::map<uint16_t, int64_t> m_lastSeq; //!< last emitted seq per registered node
  int64_t m_appliedSeq{-1};              //!< last seq whose controls were applied
  std::vector<E2IpcControl> m_buffered;
  Time m_actuationDelay{0};                     //!< δ (ActuationDelay)
  int64_t m_periodNs{0};                        //!< E2 period T of the registered emitters
  std::map<int64_t, std::vector<PendingBatch>> m_dueAtSeq; //!< δ = n·T: batches due at barrier seq
  std::map<uint64_t, PendingBatch> m_scheduled; //!< other δ: event-scheduled batches by id
  uint64_t m_nextBatchId{0};
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
