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

#ifndef ASN1C_TYPES_H
#define ASN1C_TYPES_H

#include "ns3/object.h"
#include <ns3/math.h>

extern "C" {
  #include "OCTET_STRING.h"
  #include "BIT_STRING.h"
  /* PM-Info-Item.h removed (v2 only) — MeasurementItem now uses plain C++ storage */
  #include "S-NSSAI.h"   /* v3: was SNSSAI.h */
  /* RRCEvent.h, L3-RRC-Measurements.h, ServingCellMeasurements.h, MeasResultNR.h,
   * MeasResultEUTRA.h, MeasResultPCell.h, MeasResultListEUTRA.h,
   * MeasResultListNR.h, MeasResultServMO.h, MeasResultServMOList.h,
   * MeasQuantityResults.h, ResultsPerSSB-Index.h, ResultsPerCSI-RS-Index.h:
   * all absent from v3 asn1c (were ns-O-RAN v2 E2SM-NI extensions).
   * Classes below that previously wrapped these types are now pure C++ stubs
   * that preserve the public API so mmwave-enb-net-device.cc compiles unchanged.
   * The L3-RRC path is not on the KPM critical path; MeasurementItem::RRC items
   * are emitted as PR_noValue in FillAndEncodeKpmIndicationMessage.
   *
   * E2SM-RC / RANParameter headers also absent from v3; RANParameterItem and
   * ric-control-message.cc are excluded from build. */
}

#include <cstdint>
#include <string>
#include <vector>

namespace ns3 {

/**
* Wrapper for class for OCTET STRING
*/
class OctetString : public SimpleRefCount<OctetString>
{
public:
  OctetString (std::string value, size_t size);
  OctetString (void *value, size_t size);
  ~OctetString ();
  OCTET_STRING_t *GetPointer ();
  OCTET_STRING_t GetValue ();
  std::string DecodeContent ();

private:
  void CreateBaseOctetString (size_t size);
  OCTET_STRING_t *m_octetString;
};

/**
* Wrapper for class for BIT STRING
*/
class BitString : public SimpleRefCount<BitString>
{
public:
  BitString (std::string value, size_t size);
  BitString (std::string value, size_t size, size_t bits_unused);
  ~BitString ();
  BIT_STRING_t *GetPointer ();
  BIT_STRING_t GetValue ();
  // TODO maybe a to string or a decode method should be created

private:
  BIT_STRING_t *m_bitString;
};

class NrCellId : public SimpleRefCount<NrCellId>
{
public:
  NrCellId (uint16_t value);
  virtual ~NrCellId ();
  BIT_STRING_t *GetPointer ();
  BIT_STRING_t GetValue ();

private:
  Ptr<BitString> m_bitString;
};

/**
* Wrapper for class for S-NSSAI (v3: S_NSSAI_t)
*/
class Snssai : public SimpleRefCount<Snssai>
{
public:
  Snssai (std::string sst);
  Snssai (std::string sst, std::string sd);
  ~Snssai ();
  S_NSSAI_t *GetPointer ();
  S_NSSAI_t GetValue ();

private:
  OCTET_STRING_t *m_sst;
  OCTET_STRING_t *m_sd;
  S_NSSAI_t *m_sNssai;
};

/* ==========================================================================
 * Stub classes for v2 E2SM-NI RRC measurement wrappers.
 *
 * The underlying v2 asn1c types (MeasQuantityResults_t, MeasResultNR_t, etc.)
 * are absent from the v3 asn1c install.  These stubs preserve the public API
 * so all callers in mmwave-enb-net-device.cc and helpers compile unchanged.
 * Internally they hold only plain C++ data; no ASN.1 encoding is performed
 * for the RRC path (kpm-indication.cc emits PR_noValue for RRC items).
 * ========================================================================== */

/** Stub for MeasQuantityResults — holds RSRP/RSRQ/SINR as plain longs. */
class MeasQuantityResultsWrap : public SimpleRefCount<MeasQuantityResultsWrap>
{
public:
  MeasQuantityResultsWrap ();
  ~MeasQuantityResultsWrap ();
  void AddRsrp (long rsrp);
  void AddRsrq (long rsrq);
  void AddSinr (long sinr);

private:
  long m_rsrp  {0};
  long m_rsrq  {0};
  long m_sinr  {0};
};

/** Stub for ResultsPerCSI-RS-Index. */
class ResultsPerCsiRsIndex : public SimpleRefCount<ResultsPerCsiRsIndex>
{
public:
  ResultsPerCsiRsIndex (long csiRsIndex, MeasQuantityResultsWrap *csiRsResults);
  ResultsPerCsiRsIndex (long csiRsIndex);

private:
  long m_index {0};
};

/** Stub for ResultsPerSSB-Index. */
class ResultsPerSSBIndex : public SimpleRefCount<ResultsPerSSBIndex>
{
public:
  ResultsPerSSBIndex (long ssbIndex, MeasQuantityResultsWrap *ssbResults);
  ResultsPerSSBIndex (long ssbIndex);

private:
  long m_index {0};
};

/** Stub for MeasResultNR. */
class MeasResultNr : public SimpleRefCount<MeasResultNr>
{
public:
  enum ResultCell { SSB = 0, CSI_RS = 1 };
  MeasResultNr (long physCellId);
  MeasResultNr ();
  ~MeasResultNr ();
  void AddCellResults (ResultCell cell, MeasQuantityResultsWrap *results);
  void AddPerSsbIndexResults (ResultsPerSSBIndex *resultsSsbIndex);
  void AddPerCsiRsIndexResults (ResultsPerCsiRsIndex *resultsCsiRsIndex);
  void AddPhyCellId (long physCellId);

private:
  long m_physCellId {0};
};

/** Stub for MeasResultEUTRA. */
class MeasResultEutra : public SimpleRefCount<MeasResultEutra>
{
public:
  MeasResultEutra (long eutraPhysCellId, long rsrp, long rsrq, long sinr);
  MeasResultEutra (long eutraPhysCellId);
  void AddRsrp (long rsrp);
  void AddRsrq (long rsrq);
  void AddSinr (long sinr);

private:
  long m_physCellId {0};
};

/** Stub for MeasResultPCell. */
class MeasResultPCellWrap : public SimpleRefCount<MeasResultPCellWrap>
{
public:
  MeasResultPCellWrap (long eutraPhysCellId, long rsrpResult, long rsrqResult);
  MeasResultPCellWrap (long eutraPhysCellId);
  void AddRsrpResult (long rsrpResult);
  void AddRsrqResult (long rsrqResult);

private:
  long m_physCellId {0};
};

/** Stub for MeasResultServMO. */
class MeasResultServMo : public SimpleRefCount<MeasResultServMo>
{
public:
  MeasResultServMo (long servCellId, MeasResultNr *measResultServingCell,
                    MeasResultNr *measResultBestNeighCell);
  MeasResultServMo (long servCellId, MeasResultNr *measResultServingCell);

private:
  long m_servCellId {0};
};

/** Stub for ServingCellMeasurements. */
class ServingCellMeasurementsWrap : public SimpleRefCount<ServingCellMeasurementsWrap>
{
public:
  ServingCellMeasurementsWrap ();
  void AddMeasResultPCell (MeasResultPCellWrap *measResultPCell);
  void AddMeasResultServMo (MeasResultServMo *measResultServMO);
};

/**
 * Wrapper for L3 RRC Measurements — public API preserved for
 * mmwave-enb-net-device.cc; internal v2 ASN.1 storage replaced with plain C++.
 */
class L3RrcMeasurements : public SimpleRefCount<L3RrcMeasurements>
{
public:
  int MAX_MEAS_RESULTS_ITEMS = 8; // Maximum 8 per UE (standard)

  L3RrcMeasurements ();
  ~L3RrcMeasurements ();

  void AddMeasResultEUTRANeighCells (MeasResultEutra *measResultItemEUTRA);
  void AddMeasResultNRNeighCells (MeasResultNr *measResultItemNR);
  void AddServingCellMeasurement (ServingCellMeasurementsWrap *servingCellMeasurements);
  void AddNeighbourCellMeasurement (long neighCellId, long sinr);

  static Ptr<L3RrcMeasurements> CreateL3RrcUeSpecificSinrServing (long servingCellId,
                                                                  long physCellId, long sinr);
  static Ptr<L3RrcMeasurements> CreateL3RrcUeSpecificSinrNeigh ();

  /**
   * Returns the input SINR on a 0-127 scale.
   * Refer to 3GPP TS 38.133 V17.2.0(2021-06), Table 10.1.16.1-1.
   */
  static double ThreeGppMapSinr (double sinr);

private:
  int m_measItemsCounter {0};
};

/**
 * Plain C++ holder for a named KPM measurement item.
 *
 * Phase 7 (M6) — ported from v2 PM_Info_Item_t / MeasurementValue_t (both
 * removed from v3 asn1c) to a plain struct with typed accessors.  The three
 * public constructors are preserved so all callers compile unchanged.
 *
 * ValueType::RRC: the L3-RRC measurement pointer is kept in m_rrcValue so
 * callers that read it (e.g. CuCp UE path) can still access it; however there
 * is no v3 MeasurementRecordItem mapping — FillAndEncodeKpmIndicationMessage
 * emits PR_noValue and logs a warning for RRC items.
 */
class MeasurementItem : public SimpleRefCount<MeasurementItem>
{
public:
  enum ValueType { Int = 0, Real = 1, RRC = 2 };

  MeasurementItem (std::string name, long value);
  MeasurementItem (std::string name, double value);
  MeasurementItem (std::string name, Ptr<L3RrcMeasurements> value);
  ~MeasurementItem ();

  /** Return the measurement name (used as MeasurementTypeName in v3). */
  std::string GetName () const;

  /** Return the stored value type. */
  ValueType GetValueType () const;

  /** Return integer value; valid only when GetValueType() == Int. */
  long GetIntValue () const;

  /** Return real value; valid only when GetValueType() == Real. */
  double GetRealValue () const;

  /** Return RRC measurement pointer; valid only when GetValueType() == RRC. */
  Ptr<L3RrcMeasurements> GetRrcValue () const;

private:
  std::string            m_name;
  ValueType              m_valueType;
  long                   m_valueInt   {0};
  double                 m_valueReal  {0.0};
  Ptr<L3RrcMeasurements> m_rrcValue;
};

/* RANParameterItem removed: RANParameter-* headers absent from v3 asn1c install.
 * ric-control-message.cc (the only user) is excluded from the build. */

} // namespace ns3
#endif /* ASN1C_TYPES_H */
