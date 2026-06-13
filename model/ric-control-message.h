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

#ifndef RIC_CONTROL_MESSAGE_H
#define RIC_CONTROL_MESSAGE_H

#include "ns3/object.h"

extern "C" {
  #include "E2AP-PDU.h"
  #include "E2SM-RC-ControlHeader.h"
  #include "E2SM-RC-ControlHeader-Format1.h"
  #include "E2SM-RC-ControlMessage.h"
  #include "E2SM-RC-ControlMessage-Format1.h"
  #include "E2SM-RC-ControlMessage-Format1-Item.h"
  #include "RANParameter-ValueType.h"
  #include "RANParameter-ValueType-Choice-Structure.h"
  #include "RANParameter-ValueType-Choice-ElementFalse.h"
  #include "RANParameter-STRUCTURE.h"
  #include "RANParameter-STRUCTURE-Item.h"
  #include "RANParameter-Value.h"
  #include "UEID.h"
  #include "UEID-GNB.h"
  #include "RICcontrolRequest.h"
  #include "ProtocolIE-Field.h"
  #include "InitiatingMessage.h"
}

namespace ns3 {

/**
 * Decodes an incoming E2AP RIC Control Request that carries an E2SM-RC v2.0
 * payload. Extracts the outer E2AP identifiers needed to build the Control
 * Acknowledge (Phase 4 / M-RC1) and, for Phase 5 ("RC act: forced handover"),
 * the inner ControlHeader Format1 UE identity (`m_imsi`) and the ControlMessage
 * Format1 `ranP_List` target (secondary) cell (`m_targetCellId`).
 */
class RicControlMessage : public SimpleRefCount<RicControlMessage>
{
public:
  enum ControlMessageRequestIdType { TS = 1001, QoS = 1002, UNKNOWN = 0 };

  RicControlMessage (E2AP_PDU_t *pdu);
  ~RicControlMessage ();

  // Outer E2AP identifiers (echoed back in the Control Acknowledge).
  RICrequestID_t     m_ricRequestId {};
  RANfunctionID_t    m_ranFunctionId {0};
  RICcallProcessID_t m_ricCallProcessId {};
  bool               m_hasCallProcessId {false};

  // RICcontrolAckRequest value if present (-1 = absent). NO_ACK/ACK/NACK.
  long m_ricControlAckRequest {-1};

  // Best-effort classification from the RIC Requestor ID (kept for logging).
  ControlMessageRequestIdType m_requestType {UNKNOWN};

  // Decoded inner control header (Format1), owned by this object; null if the
  // header IE was absent or failed to decode.
  E2SM_RC_ControlHeader_t *m_e2SmRcControlHeader {nullptr};

  // UE IMSI from ControlHeader Format1 UEID.gNB_UEID.amf_UE_NGAP_ID; the xApp
  // sets amfUENGAPID = the ns-3 UE IMSI. 0 if absent/not a gNB UEID. (Phase 5)
  uint64_t m_imsi {0};

  // Target (secondary) cell ID from ControlMessage Format1 nested ranP_List
  // NRCGI (last byte). 0 if absent/malformed. (Phase 5 forced handover)
  uint16_t m_targetCellId {0};

private:
  void DecodeRicControlMessage (E2AP_PDU_t *pdu);

  // Navigate the nested ranP_List to the NRCGI OCTET STRING leaf and return its
  // last byte as the ns-3 target cell ID, or 0 if absent/malformed.
  static uint16_t ExtractTargetCellFromRanPList (E2SM_RC_ControlMessage_Format1_t *fmt1);
};

} // namespace ns3

#endif /* RIC_CONTROL_MESSAGE_H */
