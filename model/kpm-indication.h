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

#ifndef KPM_INDICATION_H
#define KPM_INDICATION_H

#include "ns3/object.h"
#include <ns3/l3-rrc-measurements.h>

#include <cstdint>
#include <set>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace ns3 {

  /**
  * One measurement value exactly as ns-3 computed it: a signed or unsigned
  * integer (C++ integral types) or a REAL (C++ floating-point types).
  */
  using MeasurementValue = std::variant<int64_t, uint64_t, double>;

  /**
  * Ordered list of named measurement values of one cell or one UE.
  *
  * Plain-value replacement of the former asn1c-backed list: items keep their
  * insertion order and the exact name passed to AddItem. L3 RRC reports
  * (HO.SrcCellQual / HO.TrgtCellQual) are kept as objects next to the values.
  */
  class MeasurementItemList : public SimpleRefCount<MeasurementItemList>
  {
  public:
    using Item = std::pair<std::string, MeasurementValue>;

    MeasurementItemList ();
    MeasurementItemList (std::string ueId);
     ~MeasurementItemList ();

    // NOTE defined here to avoid undefined references
    template<class T>
    void AddItem (std::string name, T value)
    {
      if constexpr (std::is_same_v<T, Ptr<L3RrcMeasurements>>)
        {
          m_l3Items.push_back (value);
        }
      else if constexpr (std::is_floating_point_v<T>)
        {
          m_items.emplace_back (std::move (name), MeasurementValue (static_cast<double> (value)));
        }
      else if constexpr (std::is_unsigned_v<T>)
        {
          m_items.emplace_back (std::move (name), MeasurementValue (static_cast<uint64_t> (value)));
        }
      else
        {
          static_assert (std::is_integral_v<T>, "measurement values are integers or reals");
          m_items.emplace_back (std::move (name), MeasurementValue (static_cast<int64_t> (value)));
        }
    }

    const std::vector<Item> &GetItems () const;
    const std::vector<Ptr<L3RrcMeasurements>> &GetL3Items () const;
    /** UE ID (zero-padded IMSI string) for a UE list, empty for a cell list */
    const std::string &GetId () const;

  private:
    std::string m_id; // ID, contains the UE IMSI if used to carry UE-specific measurement items
    std::vector<Item> m_items; //!< list of Measurement Information Items
    std::vector<Ptr<L3RrcMeasurements>> m_l3Items; //!< L3 RRC serving / neighbour reports
  };

  /**
  * Contains the values to be inserted in the O-DU EPC Measurement Container
  */
  class EpcDuPmContainer : public SimpleRefCount<EpcDuPmContainer>
  {
  public:
    long m_qci; //!< QCI value
    long m_dlPrbUsage; //!< Used number of PRBs in an average of DL for the monitored slice during E2 reporting period
    long m_ulPrbUsage; //!< Used number of PRBs in an average of UL for the monitored slice during E2 reporting period
    virtual ~EpcDuPmContainer () = default;
  };

  class ServedPlmnPerCell : public SimpleRefCount<ServedPlmnPerCell>
  {
  public:
    std::string m_plmId; //!< PLMN identity, octet string, 3 bytes
    uint16_t m_nrCellId;
    std::set<Ptr<EpcDuPmContainer>> m_perQciReportItems;
  };

  class CellResourceReport : public SimpleRefCount<CellResourceReport>
  {
  public:
    std::string m_plmId; //!< PLMN identity, octet string, 3 bytes
    uint16_t m_nrCellId;
    long dlAvailablePrbs;
    long ulAvailablePrbs;
    std::set<Ptr<ServedPlmnPerCell>> m_servedPlmnPerCellItems;
  };

  /**
  * The values of one E2 report section (CU-UP, CU-CP or DU) of one node.
  */
  class KpmIndicationMessage : public SimpleRefCount<KpmIndicationMessage>
  {
  public:

    /**
    * Holds the values of the report. Container values (PDCP bytes, active
    * UEs, PRB report) are flattened into the cell list under their C++ names.
    */
    struct KpmIndicationMessageValues
    {
      std::string m_cellObjectId; //!< Cell Object ID
      Ptr<MeasurementItemList> m_cellMeasurementItems; //!< list of cell-specific Measurement Information Items
      std::vector<Ptr<MeasurementItemList>> m_ueIndications; //!< per-UE lists, in insertion order
    };

    KpmIndicationMessage (KpmIndicationMessageValues values);
    ~KpmIndicationMessage ();

    const KpmIndicationMessageValues &GetValues () const;

  private:
    KpmIndicationMessageValues m_values;
  };
}

#endif /* KPM_INDICATION_H */
