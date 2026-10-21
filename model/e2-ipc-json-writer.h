/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Minimal JSON writer for the ns3-e2-ipc NDJSON lines (no third-party library).
 */

#ifndef E2_IPC_JSON_WRITER_H
#define E2_IPC_JSON_WRITER_H

#include <cstdint>
#include <string>
#include <vector>

namespace ns3 {

/**
 * Streaming writer for one compact JSON value (one NDJSON line, no newline).
 *
 * Commas are inserted automatically. Doubles are written in the shortest form
 * that round-trips (std::to_chars), with ".0" appended when that form has no
 * fraction or exponent so the value stays a JSON number of real type;
 * non-finite doubles are written as null (ns3-e2-ipc v1 rule).
 */
class E2IpcJsonWriter
{
public:
  void BeginObject ();
  void EndObject ();
  void BeginArray ();
  void EndArray ();

  /** Write an object key; the next call writes its value. */
  void Key (const std::string &key);

  void Int (int64_t value);
  void UInt (uint64_t value);
  void Double (double value);
  void Bool (bool value);
  void String (const std::string &value);
  void Null ();

  /** The serialized text so far */
  const std::string &Str () const;

private:
  /** Emit a comma when a value or key follows another one at the same level */
  void BeforeValue ();
  void WriteEscaped (const std::string &text);

  std::string m_out;
  std::vector<bool> m_first; //!< per open container: no element written yet
  bool m_afterKey{false};    //!< a key was written, its value comes next
};

} // namespace ns3

#endif /* E2_IPC_JSON_WRITER_H */
