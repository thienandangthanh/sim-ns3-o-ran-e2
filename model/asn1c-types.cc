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

#include <ns3/asn1c-types.h>
#include <ns3/log.h>

NS_LOG_COMPONENT_DEFINE ("Asn1Types");

namespace ns3 {

OctetString::OctetString (std::string value, size_t size)
{
  NS_LOG_FUNCTION (this);
  CreateBaseOctetString (size);
  memcpy (m_octetString->buf, value.c_str (), size);
}

void OctetString::CreateBaseOctetString (size_t size)
{
  NS_LOG_FUNCTION (this);
  m_octetString = (OCTET_STRING_t *) calloc (1, sizeof (OCTET_STRING_t));
  m_octetString->buf = (uint8_t *) calloc (1, size);
  m_octetString->size = size;
}

OctetString::OctetString (void *value, size_t size)
{
  NS_LOG_FUNCTION (this);
  CreateBaseOctetString (size);
  memcpy (m_octetString->buf, value, size);
}

OctetString::~OctetString ()
{
  NS_LOG_FUNCTION (this);
  free (m_octetString);
}

OCTET_STRING_t *
OctetString::GetPointer ()
{
  return m_octetString;
}

OCTET_STRING_t
OctetString::GetValue ()
{
  return *m_octetString;
}

std::string OctetString::DecodeContent(){
  int size = this->GetValue ().size;
  char out[size + 1];
  std::memcpy (out, this->GetValue ().buf, size);
  out[size] = '\0';

  return std::string (out);
}

BitString::BitString (std::string value, size_t size)
{
  NS_LOG_FUNCTION (this);
  m_bitString = (BIT_STRING_t *) calloc (1, sizeof (BIT_STRING_t));
  m_bitString->buf = (uint8_t *) calloc (1, size);
  m_bitString->size = size;
  memcpy (m_bitString->buf, value.c_str(), size);
}

BitString::BitString (std::string value, size_t size, size_t bits_unused)
    : BitString::BitString (value, size)
{
  NS_LOG_FUNCTION (this);
  m_bitString->bits_unused = bits_unused;
}

BitString::~BitString ()
{
  NS_LOG_FUNCTION (this);
  free (m_bitString);
}

BIT_STRING_t *
BitString::GetPointer ()
{
  return m_bitString;
}

BIT_STRING_t
BitString::GetValue ()
{
  return *m_bitString;
}

NrCellId::NrCellId (uint16_t value)
{
  NS_LOG_FUNCTION (this);
  uint16_t shifted = value * 16;
  std::string str_shift = std::to_string (shifted);
  m_bitString = Create<BitString> (str_shift, 5, 4);
}

NrCellId::~NrCellId ()
{
}

BIT_STRING_t
NrCellId::GetValue ()
{
  return m_bitString->GetValue ();
}

BIT_STRING_t*
NrCellId::GetPointer ()
{
  return m_bitString->GetPointer ();
}

/* =========================================================================
 * Snssai — v3 S_NSSAI_t (was SNSSAI_t in v2)
 * ========================================================================= */

Snssai::Snssai (std::string sst)
{
  m_sNssai = (S_NSSAI_t *) calloc (1, sizeof (S_NSSAI_t));
  m_sst = (OCTET_STRING_t *) calloc (1, sizeof (OCTET_STRING_t));
  m_sst->buf = (uint8_t *) calloc (1, sst.size ());
  m_sst->size = sst.size ();
  memcpy (m_sst->buf, sst.c_str (), sst.size ());
  m_sNssai->sST = *m_sst;
  m_sd = nullptr;
}

Snssai::Snssai (std::string sst, std::string sd) : Snssai (sst)
{
  m_sd = (OCTET_STRING_t *) calloc (1, sizeof (OCTET_STRING_t));
  m_sd->buf = (uint8_t *) calloc (1, sd.size ());
  m_sd->size = sd.size ();
  memcpy (m_sd->buf, sd.c_str (), sd.size ());
  m_sNssai->sD = m_sd;
}

Snssai::~Snssai ()
{
  if (m_sNssai != nullptr)
    ASN_STRUCT_FREE (asn_DEF_S_NSSAI, m_sNssai);
}

S_NSSAI_t *
Snssai::GetPointer ()
{
  return m_sNssai;
}

S_NSSAI_t
Snssai::GetValue ()
{
  return *m_sNssai;
}

/* =========================================================================
 * Stub implementations for v2 E2SM-NI RRC measurement wrappers.
 *
 * The v2 asn1c types (MeasQuantityResults_t, MeasResultNR_t, RRCEvent_t, etc.)
 * are absent from the v3 asn1c install.  These stubs preserve the public API
 * so callers in mmwave-enb-net-device.cc compile unchanged.  The internal
 * v2 ASN.1 storage is replaced with plain C++ members; no encoding is
 * performed for the RRC path (kpm-indication.cc emits PR_noValue).
 * ========================================================================= */

MeasQuantityResultsWrap::MeasQuantityResultsWrap ()
{
}

MeasQuantityResultsWrap::~MeasQuantityResultsWrap ()
{
}

void
MeasQuantityResultsWrap::AddRsrp (long rsrp)
{
  m_rsrp = rsrp;
}

void
MeasQuantityResultsWrap::AddRsrq (long rsrq)
{
  m_rsrq = rsrq;
}

void
MeasQuantityResultsWrap::AddSinr (long sinr)
{
  m_sinr = sinr;
}

ResultsPerCsiRsIndex::ResultsPerCsiRsIndex (long csiRsIndex,
                                            MeasQuantityResultsWrap * /*csiRsResults*/)
    : ResultsPerCsiRsIndex (csiRsIndex)
{
}

ResultsPerCsiRsIndex::ResultsPerCsiRsIndex (long csiRsIndex) : m_index (csiRsIndex)
{
}

ResultsPerSSBIndex::ResultsPerSSBIndex (long ssbIndex,
                                        MeasQuantityResultsWrap * /*ssbResults*/)
    : ResultsPerSSBIndex (ssbIndex)
{
}

ResultsPerSSBIndex::ResultsPerSSBIndex (long ssbIndex) : m_index (ssbIndex)
{
}

MeasResultNr::MeasResultNr (long physCellId) : MeasResultNr ()
{
  m_physCellId = physCellId;
}

MeasResultNr::MeasResultNr ()
{
}

MeasResultNr::~MeasResultNr ()
{
}

void
MeasResultNr::AddCellResults (MeasResultNr::ResultCell /*cell*/,
                               MeasQuantityResultsWrap * /*results*/)
{
  /* stub — v2 ASN.1 types gone; no encoding performed for RRC path */
}

void
MeasResultNr::AddPerSsbIndexResults (ResultsPerSSBIndex * /*resultsSsbIndex*/)
{
}

void
MeasResultNr::AddPerCsiRsIndexResults (ResultsPerCsiRsIndex * /*resultsCsiRsIndex*/)
{
}

void
MeasResultNr::AddPhyCellId (long physCellId)
{
  m_physCellId = physCellId;
}

MeasResultEutra::MeasResultEutra (long eutraPhysCellId, long rsrp, long rsrq, long sinr)
    : MeasResultEutra (eutraPhysCellId)
{
  AddRsrp (rsrp);
  AddRsrq (rsrq);
  AddSinr (sinr);
}

MeasResultEutra::MeasResultEutra (long eutraPhysCellId) : m_physCellId (eutraPhysCellId)
{
}

void
MeasResultEutra::AddRsrp (long /*rsrp*/)
{
}

void
MeasResultEutra::AddRsrq (long /*rsrq*/)
{
}

void
MeasResultEutra::AddSinr (long /*sinr*/)
{
}

MeasResultPCellWrap::MeasResultPCellWrap (long eutraPhysCellId, long rsrpResult,
                                          long rsrqResult)
    : MeasResultPCellWrap (eutraPhysCellId)
{
  AddRsrpResult (rsrpResult);
  AddRsrqResult (rsrqResult);
}

MeasResultPCellWrap::MeasResultPCellWrap (long eutraPhysCellId) : m_physCellId (eutraPhysCellId)
{
}

void
MeasResultPCellWrap::AddRsrpResult (long /*rsrpResult*/)
{
}

void
MeasResultPCellWrap::AddRsrqResult (long /*rsrqResult*/)
{
}

MeasResultServMo::MeasResultServMo (long servCellId, MeasResultNr * /*measResultServingCell*/,
                                    MeasResultNr * /*measResultBestNeighCell*/)
    : MeasResultServMo (servCellId, nullptr)
{
}

MeasResultServMo::MeasResultServMo (long servCellId, MeasResultNr * /*measResultServingCell*/)
    : m_servCellId (servCellId)
{
}

ServingCellMeasurementsWrap::ServingCellMeasurementsWrap ()
{
}

void
ServingCellMeasurementsWrap::AddMeasResultPCell (MeasResultPCellWrap * /*measResultPCell*/)
{
}

void
ServingCellMeasurementsWrap::AddMeasResultServMo (MeasResultServMo * /*measResultServMO*/)
{
}

/* =========================================================================
 * L3RrcMeasurements — public API unchanged; internals are pure C++ stubs.
 * ========================================================================= */

L3RrcMeasurements::L3RrcMeasurements () : m_measItemsCounter (0)
{
}

L3RrcMeasurements::~L3RrcMeasurements ()
{
}

void
L3RrcMeasurements::AddMeasResultEUTRANeighCells (MeasResultEutra * /*measResultItemEUTRA*/)
{
  if (m_measItemsCounter < MAX_MEAS_RESULTS_ITEMS)
    m_measItemsCounter++;
}

void
L3RrcMeasurements::AddMeasResultNRNeighCells (MeasResultNr * /*measResultItemNR*/)
{
  if (m_measItemsCounter < MAX_MEAS_RESULTS_ITEMS)
    m_measItemsCounter++;
}

void
L3RrcMeasurements::AddServingCellMeasurement (
    ServingCellMeasurementsWrap * /*servingCellMeasurements*/)
{
}

void
L3RrcMeasurements::AddNeighbourCellMeasurement (long /*neighCellId*/, long /*sinr*/)
{
  if (m_measItemsCounter < MAX_MEAS_RESULTS_ITEMS)
    m_measItemsCounter++;
}

Ptr<L3RrcMeasurements>
L3RrcMeasurements::CreateL3RrcUeSpecificSinrServing (long /*servingCellId*/,
                                                     long /*physCellId*/, long /*sinr*/)
{
  return Create<L3RrcMeasurements> ();
}

Ptr<L3RrcMeasurements>
L3RrcMeasurements::CreateL3RrcUeSpecificSinrNeigh ()
{
  return Create<L3RrcMeasurements> ();
}

double
L3RrcMeasurements::ThreeGppMapSinr (double sinr)
{
  const double inputStart  = -23.0;
  const double inputEnd    =  40.0;
  const double outputStart =   0.0;
  const double outputEnd   = 127.0;
  const double slope = (outputEnd - outputStart) / (inputEnd - inputStart);

  double outputSinr;
  if (sinr < inputStart)
    outputSinr = outputStart;
  else if (sinr > inputEnd)
    outputSinr = outputEnd;
  else
    outputSinr = outputStart + std::round (slope * (sinr - inputStart));

  NS_LOG_DEBUG ("input sinr " << sinr << " output sinr " << outputSinr);
  return outputSinr;
}

/* =========================================================================
 * MeasurementItem — Phase 7 (M6) v3 refactor
 * =========================================================================
 *
 * v2 backing store (PM_Info_Item_t / MeasurementValue_t / MeasurementType_t)
 * removed; replaced with plain C++ member storage.  Three public ctors kept
 * so all AddItem<T>() call sites compile unchanged.
 */

MeasurementItem::MeasurementItem (std::string name, long value)
    : m_name (name), m_valueType (Int), m_valueInt (value)
{
  NS_LOG_FUNCTION (this << name << "long" << value);
}

MeasurementItem::MeasurementItem (std::string name, double value)
    : m_name (name), m_valueType (Real), m_valueReal (value)
{
  NS_LOG_FUNCTION (this << name << "double" << value);
}

MeasurementItem::MeasurementItem (std::string name, Ptr<L3RrcMeasurements> value)
    : m_name (name), m_valueType (RRC), m_rrcValue (value)
{
  NS_LOG_FUNCTION (this << name << "L3 RRC" << value);
}

MeasurementItem::~MeasurementItem ()
{
  NS_LOG_FUNCTION (this);
}

std::string
MeasurementItem::GetName () const
{
  return m_name;
}

MeasurementItem::ValueType
MeasurementItem::GetValueType () const
{
  return m_valueType;
}

long
MeasurementItem::GetIntValue () const
{
  return m_valueInt;
}

double
MeasurementItem::GetRealValue () const
{
  return m_valueReal;
}

Ptr<L3RrcMeasurements>
MeasurementItem::GetRrcValue () const
{
  return m_rrcValue;
}

/* RANParameterItem implementation removed: ric-control-message.cc (the sole
 * user) is excluded from build; RANParameter-* headers absent in v3 asn1c. */

}; // namespace ns3
