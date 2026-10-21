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

#ifndef L3_RRC_MEASUREMENTS_H
#define L3_RRC_MEASUREMENTS_H

#include "ns3/ptr.h"
#include "ns3/simple-ref-count.h"

#include <vector>

namespace ns3 {

/**
 * L3 RRC SINR report of one UE, as plain values.
 *
 * This replaces the asn1c-backed L3-RRC-Measurements wrapper. One object holds
 * either the serving-cell measurement (CreateL3RrcUeSpecificSinrServing) or the
 * list of neighbour-cell measurements (CreateL3RrcUeSpecificSinrNeigh plus
 * AddNeighbourCellMeasurement). Each entry keeps both the SINR in dB (the value
 * the file-logging CSV prints) and its 3GPP 0..127 mapping (the value the ASN.1
 * report carried), so the IPC sample can carry both.
 */
class L3RrcMeasurements : public SimpleRefCount<L3RrcMeasurements>
{
public:
  /** Maximum neighbour entries per UE (standard), as the ASN.1 list allowed. */
  static constexpr int MAX_MEAS_RESULTS_ITEMS = 8;

  /** SINR of one cell as seen by the UE. */
  struct CellSinr
  {
    long cellId;
    double sinrDb;   //!< 10*log10 of the linear L3 SINR
    double sinr3gpp; //!< ThreeGppMapSinr (sinrDb), integral or NaN
  };

  static Ptr<L3RrcMeasurements> CreateL3RrcUeSpecificSinrServing (long servingCellId,
                                                                  double sinr3gpp,
                                                                  double sinrDb);

  static Ptr<L3RrcMeasurements> CreateL3RrcUeSpecificSinrNeigh ();

  /** Append one neighbour; entries beyond MAX_MEAS_RESULTS_ITEMS are refused and logged. */
  void AddNeighbourCellMeasurement (long neighCellId, double sinr3gpp, double sinrDb);

  bool HasServing () const;
  const CellSinr &GetServing () const;
  const std::vector<CellSinr> &GetNeighbours () const;

  /**
   * Returns the input SINR on a 0-127 scale
   *
   * Refer to 3GPP TS 38.133 V17.2.0(2021-06), Table 10.1.16.1-1: SS-SINR and CSI-SINR measurement report mapping
   *
   * @param sinr SINR in dB
   * @return double
   */
  static double ThreeGppMapSinr (double sinr);

private:
  bool m_hasServing{false};
  CellSinr m_serving{0, 0.0, 0.0};
  std::vector<CellSinr> m_neighbours;
};

} // namespace ns3

#endif /* L3_RRC_MEASUREMENTS_H */
