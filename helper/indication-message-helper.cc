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

#include <ns3/indication-message-helper.h>

#include <ns3/abort.h>

namespace ns3 {

IndicationMessageHelper::IndicationMessageHelper (IndicationMessageType type, bool isOffline,
                                                  bool reducedPmValues)
    : m_type (type), m_offline (isOffline), m_reducedPmValues (reducedPmValues)
{
  m_msgValues.m_cellMeasurementItems = Create<MeasurementItemList> ();
  if (!m_offline && type == IndicationMessageType::CuCp)
    {
      m_msgValues.m_cellObjectId = "NRCellCU";
    }
}

void
IndicationMessageHelper::FillBaseCuUpValues (std::string plmId)
{
  NS_ABORT_MSG_IF (m_type != IndicationMessageType::CuUp, "Wrong function for this object");
  // The PLMN ID is node identity, not a measurement: the IPC sample does not carry it.
  (void) plmId;
}

void
IndicationMessageHelper::FillBaseCuCpValues (uint16_t numActiveUes)
{
  NS_ABORT_MSG_IF (m_type != IndicationMessageType::CuCp, "Wrong function for this object");
  AddCellValue<uint16_t> ("numActiveUes", numActiveUes);
}

Ptr<MeasurementItemList>
IndicationMessageHelper::GetUeList (const std::string &ueImsiComplete)
{
  auto it = m_ueLists.find (ueImsiComplete);
  if (it != m_ueLists.end ())
    {
      return it->second;
    }
  Ptr<MeasurementItemList> ueVal = Create<MeasurementItemList> (ueImsiComplete);
  m_ueLists.emplace (ueImsiComplete, ueVal);
  m_msgValues.m_ueIndications.push_back (ueVal);
  return ueVal;
}

IndicationMessageHelper::~IndicationMessageHelper ()
{
}

Ptr<KpmIndicationMessage>
IndicationMessageHelper::CreateIndicationMessage ()
{
  return Create<KpmIndicationMessage> (m_msgValues);
}

} // namespace ns3
