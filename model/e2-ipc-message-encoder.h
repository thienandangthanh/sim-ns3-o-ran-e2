/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Encoders for the ns3-e2-ipc v1 messages (hello, sample, ueContext, bye).
 * Normative contract: O-RAN-Testbed-Automation
 * RAN_Intelligent_Controllers/Near-Real-Time-RIC/ns3_e2_agent/schema/ns3-e2-ipc.v1.schema.json
 */

#ifndef E2_IPC_MESSAGE_ENCODER_H
#define E2_IPC_MESSAGE_ENCODER_H

#include <ns3/kpm-indication.h>

#include <cstdint>
#include <string>
#include <tuple>
#include <vector>

namespace ns3 {

/** Identity and configuration of one E2 node, sent in every hello. */
struct E2IpcNodeInfo
{
  uint16_t nodeId{0};          //!< ns-3 cellId
  std::string kind;            //!< "gnb" or "enb"
  int64_t periodNs{0};         //!< E2Periodicity
  int64_t simStartEpochMs{0};  //!< wall-clock epoch ms mapped to sim time 0
  bool reducedPmValues{false}; //!< informational, samples carry the full set
  uint32_t maxNeigh{0};        //!< maximum L3 neighbour entries per UE
  std::string pacing{"free"};  //!< "free" or "lockstep" (E2ControlHub Pacing)
  double startTimeoutS{0};     //!< T_start, sent when lockstep
  double gateTimeoutS{0};      //!< T_gate, sent when lockstep
};

/** Counters reported in bye (and logged at the end of the run). */
struct E2IpcCounters
{
  uint64_t built{0};               //!< samples built (= next seq)
  uint64_t sent{0};                //!< samples fully written to the socket
  uint64_t droppedDisconnected{0}; //!< samples lost while no agent connection existed
  uint64_t droppedBackpressure{0}; //!< samples refused because the pending buffer was full
};

/**
 * X2AP-type identity of one UE as ns-3 holds it at one E2 node (ueContext line).
 * Ordered by IMSI first, so a sorted list is in IMSI order.
 */
struct E2IpcUeContext
{
  uint64_t imsi{0};        //!< ns-3 IMSI, the key the sample lines use
  uint16_t lteRnti{0};     //!< RNTI of the UE at the master LTE eNB (its eNB UE X2AP ID)
  uint16_t mnCellId{0};    //!< cell ID of the master LTE eNB
  uint64_t mmeUeS1apId{0}; //!< MME UE S1AP ID (ns-3 assigns the IMSI)

  bool
  operator== (const E2IpcUeContext &o) const
  {
    return imsi == o.imsi && lteRnti == o.lteRnti && mnCellId == o.mnCellId &&
           mmeUeS1apId == o.mmeUeS1apId;
  }

  bool
  operator!= (const E2IpcUeContext &o) const
  {
    return !(*this == o);
  }

  bool
  operator< (const E2IpcUeContext &o) const
  {
    return std::tie (imsi, lteRnti, mnCellId, mmeUeS1apId) <
           std::tie (o.imsi, o.lteRnti, o.mnCellId, o.mmeUeS1apId);
  }
};

/** The hello line (no trailing newline). */
std::string EncodeE2IpcHello (const E2IpcNodeInfo &info, const std::string &runId,
                              const std::string &ns3Sha, const std::string &oranInterfaceSha);

/**
 * The sample line (no trailing newline): DU, CU-UP and CU-CP of one node and
 * period merged. A null section was not enabled on this node and is omitted.
 */
std::string EncodeE2IpcSample (uint16_t nodeId, uint64_t seq, int64_t simTimeNs,
                               uint64_t ipcDropped, Ptr<KpmIndicationMessage> du,
                               Ptr<KpmIndicationMessage> cuUp, Ptr<KpmIndicationMessage> cuCp);

/**
 * The ueContext line (no trailing newline): the UE contexts of one node, valid
 * from the sample with the same seq on. The UEs are written sorted by IMSI.
 */
std::string EncodeE2IpcUeContext (uint16_t nodeId, uint64_t seq,
                                  std::vector<E2IpcUeContext> ues);

/** The bye line (no trailing newline). */
std::string EncodeE2IpcBye (uint16_t nodeId, const E2IpcCounters &counters);

} // namespace ns3

#endif /* E2_IPC_MESSAGE_ENCODER_H */
