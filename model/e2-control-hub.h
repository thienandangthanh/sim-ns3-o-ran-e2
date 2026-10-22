/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Inbound E2 control path of the ns3-e2-ipc stream (v1 minor 1): RC handover
 * controls that the out-of-process agents forward, applied once per E2 period
 * and recorded in a control ledger.
 */

#ifndef E2_CONTROL_HUB_H
#define E2_CONTROL_HUB_H

#include "ns3/callback.h"
#include "ns3/object.h"

#include <cstdint>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

namespace ns3 {

class E2IpcEmitter;

/** One `control` line as received from an agent. */
struct E2IpcControl
{
  uint16_t viaNodeId{0};    //!< node whose agent forwarded the control
  uint64_t ctlId{0};        //!< agent-monotonic id; the agent's pending key
  uint64_t imsi{0};         //!< UE (decimal IMSI)
  uint16_t targetCellId{0}; //!< target secondary cell
  int64_t afterSeq{-1};     //!< last sample seq the agent had forwarded
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
 * Emitters hand every inbound `control` line to the hub. When the last
 * registered emitter has emitted sample seq k, the hub drains every emitter
 * once more and applies the buffered controls synchronously, at the current
 * simulation time (actuation delay d = 0), in the C4 order (imsi, viaNodeId,
 * targetCellId, ctlId), through the handler registered by the LTE master
 * device. Each control gets one ledger line and one `controlAck` sent back
 * through the agent that forwarded it. Controls still buffered or arriving
 * when the emitters stop are answered `refused` with cause `late` and are
 * never applied. Without controls the hub writes nothing and schedules
 * nothing, so free-run output is unchanged.
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

private:
  void Apply (uint64_t seq);
  void Answer (const E2IpcControl &c, int64_t rxSeq, int64_t rxNs, bool applied,
               const std::string &cause, uint16_t sourceCellId);
  void WriteLedger (const std::string &line);
  void DrainAll ();

  std::string m_ledgerPath;
  std::string m_defaultTraceDir;
  std::FILE *m_ledger{nullptr};
  HandoverHandler m_handler;
  std::map<uint16_t, E2IpcEmitter *> m_emitters;
  std::map<uint16_t, int64_t> m_lastSeq; //!< last emitted seq per registered node
  int64_t m_appliedSeq{-1};              //!< last seq whose controls were applied
  std::vector<E2IpcControl> m_buffered;
  bool m_stopping{false};
  uint64_t m_badLines{0};
};

} // namespace ns3

#endif /* E2_CONTROL_HUB_H */
