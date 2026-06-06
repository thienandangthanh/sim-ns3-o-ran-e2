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
 *         Tommaso Zugno <tommasozugno@gmail.com>
 *         Michele Polese <michele.polese@gmail.com>
 *
 * Ported to E2SM-KPM v3.00 (Phase 7 / M6).  v2 container types (PF-Container,
 * RAN-Container, etc.) removed from ASN.1 path; the struct fields
 * (m_pmContainerValues, OCuUpContainerValues, etc.) are retained so that
 * callers (mmwave-enb-net-device.cc, indication-message-helper.cc) compile
 * unchanged — they no longer drive ASN.1 encoding.
 */

#ifndef KPM_INDICATION_H
#define KPM_INDICATION_H

#include "ns3/object.h"
#include <set>

extern "C" {
  /* v3.00 KPM headers only — v2 container headers removed */
  #include "E2SM-KPM-RANfunction-Description.h"
  #include "E2SM-KPM-IndicationHeader.h"
  #include "E2SM-KPM-IndicationMessage.h"
  #include "asn1c-types.h"
}

namespace ns3 {

  /**
   * KPM RIC Indication Header — v3.00 IndicationHeader-Format1.
   *
   * The GlobalE2nodeType enum and KpmRicIndicationHeaderValues struct are
   * preserved so callers (BuildRicIndicationHeader in mmwave-enb-net-device.cc)
   * compile without changes.  The node-type / cell-id fields are no longer used
   * inside FillAndEncodeKpmRicIndicationHeader because v3 IndicationHeader-
   * Format1 has no GlobalE2node_ID — only sender strings + 8-byte
   * colletStartTime.
   */
  class KpmIndicationHeader : public SimpleRefCount<KpmIndicationHeader>
  {
  public:
    /** Node type enum — kept for caller ABI; ignored in v3 header encoding. */
    enum GlobalE2nodeType { gNB = 0, eNB = 1, ng_eNB = 2, en_gNB = 3 };

    /** 8 bytes: v3.00 TimeStamp is an OCTET STRING of this exact width. */
    int TIMESTAMP_LIMIT_SIZE = 8;

    /**
     * Values for the v3 IndicationHeader-Format1.
     * m_gnbId / m_nrCellId / m_plmId are kept for caller ABI but are not
     * written into the v3 header (v3 has no GlobalE2node_ID field).
     * m_timestamp (uint64_t) is encoded as an 8-byte big-endian OCTET STRING
     * into colletStartTime.
     */
    struct KpmRicIndicationHeaderValues
    {
      // Retained for ABI (not used in v3 header encoding):
      std::string m_gnbId;      //!< gNB ID bit string (v2 only — ignored)
      uint16_t    m_nrCellId;   //!< NR cell ID (v2 only — ignored)
      std::string m_plmId;      //!< PLMN identity (v2 only — ignored)

      // v3 colletStartTime source — htobe64'd into 8-byte OCTET STRING:
      uint64_t m_timestamp;
    };

    KpmIndicationHeader (GlobalE2nodeType nodeType, KpmRicIndicationHeaderValues values);
    ~KpmIndicationHeader ();

    void* m_buffer;
    size_t m_size;

  private:
    /**
     * Build and encode v3 IndicationHeader-Format1.
     * Mirrors kpm_v3::BuildIndicationHeader (kpm-indication-builder.h).
     */
    void FillAndEncodeKpmRicIndicationHeader (E2SM_KPM_IndicationHeader_t* descriptor,
                                              KpmRicIndicationHeaderValues values);

    void Encode (E2SM_KPM_IndicationHeader_t* descriptor);

    GlobalE2nodeType m_nodeType; //!< kept for ABI; unused in v3
  };

  class MeasurementItemList : public SimpleRefCount<MeasurementItemList>
  {
  private:
    Ptr<OctetString> m_id; //!< UE IMSI if used for UE-specific items; NULL for cell
    std::vector<Ptr<MeasurementItem>> m_items;
  public:
    MeasurementItemList ();
    MeasurementItemList (std::string ueId);
    ~MeasurementItemList ();

    // NOTE: defined here to avoid undefined references
    template<class T>
    void AddItem (std::string name, T value)
    {
      Ptr<MeasurementItem> item = Create<MeasurementItem> (name, value);
      m_items.push_back (item);
    }

    std::vector<Ptr<MeasurementItem>> GetItems ();
    OCTET_STRING_t GetId ();
  };

  /**
   * Base class for PM Container value holders.
   * These classes no longer drive ASN.1 encoding (v2 containers removed);
   * they are retained so helper callers (FillCuUpValues, etc.) compile.
   */
  class PmContainerValues : public SimpleRefCount<PmContainerValues>
  {
  public:
    virtual ~PmContainerValues () = default;
  };

  /** O-CU-CP container values (retained for ABI; not encoded in v3). */
  class OCuCpContainerValues : public PmContainerValues
  {
  public:
    uint16_t m_numActiveUes;
  };

  /** O-CU-UP container values (retained for ABI; not encoded in v3). */
  class OCuUpContainerValues : public PmContainerValues
  {
  public:
    std::string m_plmId;
    long m_pDCPBytesUL;
    long m_pDCPBytesDL;
  };

  /** Per-QCI DU EPC container (retained for ABI; not encoded in v3). */
  class EpcDuPmContainer : public SimpleRefCount<EpcDuPmContainer>
  {
  public:
    long m_qci;
    long m_dlPrbUsage;
    long m_ulPrbUsage;
    virtual ~EpcDuPmContainer () = default;
  };

  /** Per-5QI DU 5GC container (retained for ABI; not encoded in v3). */
  class FiveGcDuPmContainer : public SimpleRefCount<FiveGcDuPmContainer>
  {
  public:
    long m_fiveQi;
    long m_dlPrbUsage;
    long m_ulPrbUsage;
    virtual ~FiveGcDuPmContainer () = default;
  };

  class ServedPlmnPerCell : public SimpleRefCount<ServedPlmnPerCell>
  {
  public:
    std::string m_plmId;
    uint16_t m_nrCellId;
    std::set<Ptr<EpcDuPmContainer>> m_perQciReportItems;
  };

  class CellResourceReport : public SimpleRefCount<CellResourceReport>
  {
  public:
    std::string m_plmId;
    uint16_t m_nrCellId;
    long dlAvailablePrbs;
    long ulAvailablePrbs;
    std::set<Ptr<ServedPlmnPerCell>> m_servedPlmnPerCellItems;
  };

  /** O-DU container values (retained for ABI; not encoded in v3). */
  class ODuContainerValues : public PmContainerValues
  {
  public:
    std::set<Ptr<CellResourceReport>> m_cellResourceReportItems;
  };

  /**
   * KPM RIC Indication Message — v3.00 IndicationMessage-Format1.
   *
   * v2 FillPmContainer/FillOCuUp/FillOCuCp/FillODu methods removed.
   * KpmIndicationMessageValues struct kept unchanged so all callers compile;
   * m_pmContainerValues is populated by helpers but no longer drives ASN.1.
   *
   * Encoding path: all named MeasurementItems from m_cellMeasurementItems
   * (then m_ueIndications) are emitted as a flat v3 Format1 with one
   * MeasurementDataItem carrying an N-element MeasurementRecord, parallel
   * to an N-element MeasurementInfoList.  Order: cell items first (in
   * AddItem order), then UE items flattened across all UE indications.
   */
  class KpmIndicationMessage : public SimpleRefCount<KpmIndicationMessage>
  {
  public:

    struct KpmIndicationMessageValues
    {
      std::string m_cellObjectId;                         //!< Cell Object ID (kept for ABI)
      Ptr<PmContainerValues> m_pmContainerValues;         //!< v2 container holder (ABI; unused in v3 ASN.1)
      Ptr<MeasurementItemList> m_cellMeasurementItems;    //!< cell-level named items → v3 record
      std::set<Ptr<MeasurementItemList>> m_ueIndications; //!< UE-level named items → appended to record
    };

    KpmIndicationMessage (KpmIndicationMessageValues values);
    ~KpmIndicationMessage ();

    void* m_buffer;
    size_t m_size;

  private:
    void FillAndEncodeKpmIndicationMessage (E2SM_KPM_IndicationMessage_t *descriptor,
                                            KpmIndicationMessageValues values);
    void Encode (E2SM_KPM_IndicationMessage_t *descriptor);
  };

} // namespace ns3

#endif /* KPM_INDICATION_H */
