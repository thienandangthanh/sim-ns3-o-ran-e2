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

#include <ns3/l3-rrc-measurements.h>

#include <ns3/log.h>

#include <cmath>

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("L3RrcMeasurements");

Ptr<L3RrcMeasurements>
L3RrcMeasurements::CreateL3RrcUeSpecificSinrServing (long servingCellId, double sinr3gpp,
                                                     double sinrDb)
{
  Ptr<L3RrcMeasurements> l3RrcMeasurement = Create<L3RrcMeasurements> ();
  l3RrcMeasurement->m_hasServing = true;
  l3RrcMeasurement->m_serving = CellSinr{servingCellId, sinrDb, sinr3gpp};
  return l3RrcMeasurement;
}

Ptr<L3RrcMeasurements>
L3RrcMeasurements::CreateL3RrcUeSpecificSinrNeigh ()
{
  return Create<L3RrcMeasurements> ();
}

void
L3RrcMeasurements::AddNeighbourCellMeasurement (long neighCellId, double sinr3gpp, double sinrDb)
{
  if (m_neighbours.size () == static_cast<size_t> (MAX_MEAS_RESULTS_ITEMS))
    {
      NS_LOG_ERROR ("Maximum number of items ("
                    << MAX_MEAS_RESULTS_ITEMS
                    << ")for the standard reached. This item will not be "
                       "inserted in the list");
      return;
    }
  m_neighbours.push_back (CellSinr{neighCellId, sinrDb, sinr3gpp});
}

bool
L3RrcMeasurements::HasServing () const
{
  return m_hasServing;
}

const L3RrcMeasurements::CellSinr &
L3RrcMeasurements::GetServing () const
{
  return m_serving;
}

const std::vector<L3RrcMeasurements::CellSinr> &
L3RrcMeasurements::GetNeighbours () const
{
  return m_neighbours;
}

double
L3RrcMeasurements::ThreeGppMapSinr (double sinr)
{
  double inputEnd = 40;
  double inputStart = -23;
  double outputStart = 0;
  double outputEnd = 127;
  double outputSinr;
  double slope = (outputEnd - outputStart) / (inputEnd - inputStart);

  if (sinr < inputStart)
    {
      outputSinr = outputStart;
    }
  else if (sinr > inputEnd)
    {
      outputSinr = outputEnd;
    }
  else
    {
      outputSinr = outputStart + std::round (slope * (sinr - inputStart));
    }

  NS_LOG_DEBUG ("input sinr" << sinr << " output sinr" << outputSinr);

  return outputSinr;
}

} // namespace ns3
