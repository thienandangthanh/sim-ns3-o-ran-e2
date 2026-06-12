/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2022 Northeastern University
 * Copyright (c) 2022 Sapienza, University of Rome
 * Copyright (c) 2022 University of Padova
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Author: Andrea Lacava <thecave003@gmail.com>
 *		   Tommaso Zugno <tommasozugno@gmail.com>
 *		   Michele Polese <michele.polese@gmail.com>
 */

#ifndef ORAN_INTERFACE_H
#define ORAN_INTERFACE_H

#include "ns3/object.h"
#include <ns3/kpm-indication.h>
#include <ns3/kpm-function-description.h>
#include <ns3/function-description.h>
/* E2SM-RC (RAN Control): RC-2.0 asn.1 types are installed (Phase 3) and the real
 * RicControlMessage decoder is re-enabled (Phase 4, ric-control-message.h).
 * Only RicControlFunctionDescription remains a stub — its v1-era
 * RIC-ControlStyle-Item tree does not exist in RC-2.0; the RANFunctionDefinition
 * advertisement rewrite is deferred to Phase 5. */
#include "e2sim.hpp"
#include <functional>
#include <unordered_map>
#include <cstdint>

/* SubscriptionCallback in v3 e2sim.hpp is void(*)(E2AP_PDU_t*) — a raw function
 * pointer that cannot hold std::bind results.  E2TermCallback wraps both raw
 * pointers and std::bind functors so callers in lte/mmwave need no changes. */
using E2TermCallback = std::function<void(E2AP_PDU_t *)>;

/* SmCallback kept for source-compatibility with lte/mmwave callers that still
 * reference it. It is the same erasure type as E2TermCallback. */
using SmCallback = E2TermCallback;

namespace ns3 {

/* ==========================================================================
 * E2SM-RC compile shim — RicControlMessage now lives in ric-control-message.h
 * (real RC-2.0 decoder, Phase 4).  Only RicControlFunctionDescription remains a
 * stub: the RC-2.0 RANFunctionDefinition advertisement (real OID + ControlStyle
 * tree) is deferred to Phase 5, so callers still get a null-buffer descriptor.
 * ========================================================================== */

/** Stub for RicControlFunctionDescription — compile shim only. */
class RicControlFunctionDescription : public FunctionDescription
{
public:
  RicControlFunctionDescription ()
  {
    m_buffer = nullptr;
    m_size   = 0;
  }
  ~RicControlFunctionDescription () {}
};

/* ========================================================================== */

class E2Termination : public Object
{
public:
  E2Termination ();

  /**
   * \param ricAddress RIC IP address
   * \param ricPort RIC port
   * \param clientPort the local port to which the client will bind
   * \param gnbId the GNB ID
   * \param plmnId the PLMN ID
   */
  E2Termination (const std::string ricAddress, const uint16_t ricPort,
                 const uint16_t clientPort, const std::string gnbId,
                 const std::string plmnId);

  virtual ~E2Termination ();

  /** inherited from Object */
  static TypeId GetTypeId ();

  /**
   * Start the E2 termination.
   * Creates a separate thread to host the execution of e2sim.
   */
  void Start ();

  /**
   * Register a KPM Service Model callback.
   * Accepts std::bind results as well as raw function pointers.
   */
  void RegisterKpmCallbackToE2Sm (long ranFunctionId,
                                  Ptr<FunctionDescription> ranFunctionDescription,
                                  E2TermCallback sbCb);

  /**
   * Register an E2SM-RC (Sm) callback — compile shim for lte/mmwave callers.
   * The RC control path is excluded; this registers the same slot as KPM.
   */
  void RegisterSmCallbackToE2Sm (long ranFunctionId,
                                 Ptr<FunctionDescription> ranFunctionDescription,
                                 SmCallback smCb);

  /** Struct holding the values returned by ProcessRicSubscriptionRequest */
  struct RicSubscriptionRequest_rval_s
  {
    uint16_t requestorId;  //!< RIC Requestor ID
    uint16_t instanceId;   //!< RIC Instance ID
    uint16_t ranFuncionId; //!< RAN Function ID
    uint8_t  actionId;     //!< RIC Action ID
  };

  /**
   * Process RIC Subscription Request.
   */
  RicSubscriptionRequest_rval_s ProcessRicSubscriptionRequest (E2AP_PDU_t *sub_req_pdu);

  /**
   * Sends an E2 message to the RIC.
   */
  void SendE2Message (E2AP_PDU *pdu);

private:
  void DoStart ();

  void RegisterFunctionDescToE2Sm (long ranFunctionId,
                                   Ptr<FunctionDescription> ranFunctionDescription);

  /**
   * Register an E2TermCallback (std::function) for a given RAN function ID.
   * Stores the function in m_callbacks and registers a static trampoline with
   * e2sim so that the raw SubscriptionCallback slot is satisfied.
   */
  void StoreAndRegisterCallback (long ranFunctionId, E2TermCallback cb);

  /** Per-function std::function callbacks (supports std::bind / lambdas). */
  std::unordered_map<long, E2TermCallback> m_callbacks;

  E2Sim *       m_e2sim;       //!< pointer to an instance of the O-RAN E2 simulator
  std::string   m_ricAddress;  //!< IP address of the RIC
  uint16_t      m_ricPort;     //!< port of the RIC
  uint16_t      m_clientPort;  //!< local bind port
  std::string   m_gnbId;       //!< GNB id
  std::string   m_plmnId;      //!< PLMN Id
};

} // namespace ns3

#endif /* ORAN_INTERFACE_H */
