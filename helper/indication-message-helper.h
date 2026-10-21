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

#ifndef INDICATION_MESSAGE_HELPER_H
#define INDICATION_MESSAGE_HELPER_H

#include <ns3/kpm-indication.h>

#include <map>

namespace ns3 {

/**
 * Collects the values of one E2 report section (CU-UP, CU-CP or DU).
 *
 * The section carries the full metric set whatever reducedPmValues says:
 * reducedPmValues only filtered what the former ASN.1 report contained, and
 * filtering now happens downstream of the IPC emitter (it is still passed on
 * as information). Values that only the file-logging CSV printed are added
 * with AddCellValue / AddUeValue under their C++ field names.
 */
class IndicationMessageHelper : public Object
{
public:
  enum class IndicationMessageType { CuCp = 0, CuUp = 1, Du = 2 };
  IndicationMessageHelper (IndicationMessageType type, bool isOffline, bool reducedPmValues);

  ~IndicationMessageHelper ();

  Ptr<KpmIndicationMessage> CreateIndicationMessage ();

  bool const &
  IsOffline () const
  {
    return m_offline;
  }

  /** Add a cell-level value that has no ASN.1 item (C++ field name as key). */
  template <class T>
  void
  AddCellValue (std::string name, T value)
  {
    m_msgValues.m_cellMeasurementItems->AddItem<T> (std::move (name), value);
  }

  /** Add a UE-level value that has no ASN.1 item (C++ field name as key). */
  template <class T>
  void
  AddUeValue (const std::string &ueImsiComplete, std::string name, T value)
  {
    GetUeList (ueImsiComplete)->AddItem<T> (std::move (name), value);
  }

protected:
  void FillBaseCuUpValues (std::string plmId);

  void FillBaseCuCpValues (uint16_t numActiveUes);

  /** The value list of one UE, created on first use (one list per UE and section). */
  Ptr<MeasurementItemList> GetUeList (const std::string &ueImsiComplete);

  IndicationMessageType m_type;
  bool m_offline;
  bool m_reducedPmValues; //!< informational only, see the class description
  KpmIndicationMessage::KpmIndicationMessageValues m_msgValues;
  std::map<std::string, Ptr<MeasurementItemList>> m_ueLists;
};

} // namespace ns3

#endif /* INDICATION_MESSAGE_HELPER_H */