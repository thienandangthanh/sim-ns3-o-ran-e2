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
/* E2SM-RC (RAN Control) deferred per KPM-only L-release migration; the OSC
 * e2sim install (/usr/local/include/e2sim) ships no E2SM-RC asn1c types.
 * ric-control-message.cc/.h and ric-control-function-description.cc/.h are
 * excluded from build.  Minimal stubs below keep lte-enb-net-device.cc
 * (src/lte) and mmwave-enb-net-device.cc (src/mmwave) compiling. */
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
 * Minimal stub types for E2SM-RC — E2SM-RC ASN.1 headers are absent from the
 * v3 asn1c install.  These stubs expose only the members accessed by
 * lte-enb-net-device.cc so that translation unit compiles; the RC control path
 * is dead code in the KPM-only scenario (ControlMessageReceivedCallback is
 * never invoked via the live E2 path).
 * ========================================================================== */

/** Stub for E2SM_RC_ControlHeader_Format1_t::ueId (OCTET_STRING shape). */
struct RcControlHeaderUeId_t
{
  uint8_t *buf  {nullptr};
  std::size_t size {0};
};

/** Stub for E2SM_RC_ControlHeader_Format1_t. */
struct RcControlHeaderFormat1Stub
{
  RcControlHeaderUeId_t ueId;
};

/** Stub for RicControlMessage — compile shim, no real decoding. */
class RicControlMessage : public SimpleRefCount<RicControlMessage>
{
public:
  enum ControlMessageRequestIdType { TS = 1001, QoS = 1002 };

  explicit RicControlMessage (E2AP_PDU_t * /*pdu*/)
      : m_requestType (TS), m_e2SmRcControlHeaderFormat1 (new RcControlHeaderFormat1Stub ())
  {
  }

  ~RicControlMessage ()
  {
    delete m_e2SmRcControlHeaderFormat1;
  }

  ControlMessageRequestIdType m_requestType;
  RcControlHeaderFormat1Stub *m_e2SmRcControlHeaderFormat1;

  std::string GetSecondaryCellIdHO () const
  {
    return "";
  }
};

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
