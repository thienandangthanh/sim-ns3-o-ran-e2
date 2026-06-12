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

#include <ns3/ric-control-message.h>
#include <ns3/log.h>

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("RicControlMessage");

RicControlMessage::RicControlMessage (E2AP_PDU_t *pdu)
{
  DecodeRicControlMessage (pdu);
  NS_LOG_INFO ("End of RicControlMessage::RicControlMessage()");
}

RicControlMessage::~RicControlMessage ()
{
  if (m_e2SmRcControlHeader)
    {
      ASN_STRUCT_FREE (asn_DEF_E2SM_RC_ControlHeader, m_e2SmRcControlHeader);
      m_e2SmRcControlHeader = nullptr;
    }
}

void
RicControlMessage::DecodeRicControlMessage (E2AP_PDU_t *pdu)
{
  InitiatingMessage_t *mess = pdu->choice.initiatingMessage;
  auto *request = (RICcontrolRequest_t *) &mess->value.choice.RICcontrolRequest;
  NS_LOG_INFO (xer_fprint (stderr, &asn_DEF_RICcontrolRequest, request));

  size_t count = request->protocolIEs.list.count;
  if (count <= 0)
    {
      NS_LOG_ERROR ("[E2SM] received empty list");
      return;
    }

  for (size_t i = 0; i < count; i++)
    {
      RICcontrolRequest_IEs_t *ie = request->protocolIEs.list.array[i];
      switch (ie->value.present)
        {
        case RICcontrolRequest_IEs__value_PR_RICrequestID: {
          m_ricRequestId = ie->value.choice.RICrequestID;
          NS_LOG_DEBUG ("[E2SM] RICrequestID requestor="
                        << m_ricRequestId.ricRequestorID << " instance="
                        << m_ricRequestId.ricInstanceID);
          switch (m_ricRequestId.ricRequestorID)
            {
            case TS:
              m_requestType = TS;
              NS_LOG_DEBUG ("[E2SM] TS xApp control message");
              break;
            case QoS:
              m_requestType = QoS;
              NS_LOG_DEBUG ("[E2SM] QoS xApp control message");
              break;
            default:
              m_requestType = UNKNOWN;
              break;
            }
          break;
        }
        case RICcontrolRequest_IEs__value_PR_RANfunctionID: {
          m_ranFunctionId = ie->value.choice.RANfunctionID;
          NS_LOG_DEBUG ("[E2SM] RANfunctionID " << m_ranFunctionId);
          break;
        }
        case RICcontrolRequest_IEs__value_PR_RICcallProcessID: {
          m_ricCallProcessId = ie->value.choice.RICcallProcessID;
          m_hasCallProcessId = true;
          NS_LOG_DEBUG ("[E2SM] RICcallProcessID present");
          break;
        }
        case RICcontrolRequest_IEs__value_PR_RICcontrolHeader: {
          NS_LOG_DEBUG ("[E2SM] RICcontrolHeader");
          auto *hdr = (E2SM_RC_ControlHeader_t *) calloc (1, sizeof (E2SM_RC_ControlHeader_t));
          ASN_STRUCT_RESET (asn_DEF_E2SM_RC_ControlHeader, hdr);
          asn_dec_rval_t rval =
              asn_decode (nullptr, ATS_ALIGNED_BASIC_PER, &asn_DEF_E2SM_RC_ControlHeader,
                          (void **) &hdr, ie->value.choice.RICcontrolHeader.buf,
                          ie->value.choice.RICcontrolHeader.size);

          if (rval.code != RC_OK)
            {
              NS_LOG_WARN ("[E2SM] E2SM-RC ControlHeader decode failed (code "
                           << rval.code << ", consumed " << rval.consumed << ")");
              ASN_STRUCT_FREE (asn_DEF_E2SM_RC_ControlHeader, hdr);
              break;
            }

          NS_LOG_INFO (xer_fprint (stderr, &asn_DEF_E2SM_RC_ControlHeader, hdr));
          m_e2SmRcControlHeader = hdr;

          // RC-2.0 nests the format choice under ric_controlHeader_formats.
          if (hdr->ric_controlHeader_formats.present ==
              E2SM_RC_ControlHeader__ric_controlHeader_formats_PR_controlHeader_Format1)
            {
              E2SM_RC_ControlHeader_Format1_t *fmt1 =
                  hdr->ric_controlHeader_formats.choice.controlHeader_Format1;
              if (fmt1)
                {
                  NS_LOG_INFO ("[E2SM] RC ControlHeader Format1: ric_Style_Type="
                               << fmt1->ric_Style_Type << " ric_ControlAction_ID="
                               << fmt1->ric_ControlAction_ID);
                }
            }
          else
            {
              NS_LOG_DEBUG ("[E2SM] RC ControlHeader is not Format1 (present="
                            << hdr->ric_controlHeader_formats.present << ")");
            }
          break;
        }
        case RICcontrolRequest_IEs__value_PR_RICcontrolMessage: {
          NS_LOG_DEBUG ("[E2SM] RICcontrolMessage");
          auto *msg = (E2SM_RC_ControlMessage_t *) calloc (1, sizeof (E2SM_RC_ControlMessage_t));
          ASN_STRUCT_RESET (asn_DEF_E2SM_RC_ControlMessage, msg);
          asn_dec_rval_t rval =
              asn_decode (nullptr, ATS_ALIGNED_BASIC_PER, &asn_DEF_E2SM_RC_ControlMessage,
                          (void **) &msg, ie->value.choice.RICcontrolMessage.buf,
                          ie->value.choice.RICcontrolMessage.size);

          if (rval.code != RC_OK)
            {
              NS_LOG_WARN ("[E2SM] E2SM-RC ControlMessage decode failed (code "
                           << rval.code << ", consumed " << rval.consumed << ")");
            }
          else
            {
              NS_LOG_INFO (xer_fprint (stderr, &asn_DEF_E2SM_RC_ControlMessage, msg));
              if (msg->ric_controlMessage_formats.present ==
                  E2SM_RC_ControlMessage__ric_controlMessage_formats_PR_controlMessage_Format1)
                {
                  NS_LOG_DEBUG ("[E2SM] RC ControlMessage Format1 decoded "
                                "(RAN-parameter extraction deferred to Phase 5)");
                }
            }
          // The message body is only validated/logged for M-RC1; free it now.
          ASN_STRUCT_FREE (asn_DEF_E2SM_RC_ControlMessage, msg);
          break;
        }
        case RICcontrolRequest_IEs__value_PR_RICcontrolAckRequest: {
          m_ricControlAckRequest = ie->value.choice.RICcontrolAckRequest;
          NS_LOG_DEBUG ("[E2SM] RICcontrolAckRequest = " << m_ricControlAckRequest);
          break;
        }
        default: {
          NS_LOG_DEBUG ("[E2SM] Unhandled RIC Control IE (present="
                        << ie->value.present << ")");
          break;
        }
        }
    }

  NS_LOG_INFO ("End of DecodeRicControlMessage");
}

} // namespace ns3
