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

#include <ns3/kpm-indication.h>

namespace ns3 {

KpmIndicationMessage::KpmIndicationMessage (KpmIndicationMessageValues values)
    : m_values (std::move (values))
{
}

KpmIndicationMessage::~KpmIndicationMessage ()
{
}

const KpmIndicationMessage::KpmIndicationMessageValues &
KpmIndicationMessage::GetValues () const
{
  return m_values;
}

MeasurementItemList::MeasurementItemList ()
{
}

MeasurementItemList::MeasurementItemList (std::string id) : m_id (std::move (id))
{
}

MeasurementItemList::~MeasurementItemList ()
{
}

const std::vector<MeasurementItemList::Item> &
MeasurementItemList::GetItems () const
{
  return m_items;
}

const std::vector<Ptr<L3RrcMeasurements>> &
MeasurementItemList::GetL3Items () const
{
  return m_l3Items;
}

const std::string &
MeasurementItemList::GetId () const
{
  return m_id;
}

} // namespace ns3
