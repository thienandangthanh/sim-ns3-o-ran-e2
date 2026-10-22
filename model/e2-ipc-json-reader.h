/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Minimal reader for the flat inbound ns3-e2-ipc lines (agent -> ns-3).
 */

#ifndef E2_IPC_JSON_READER_H
#define E2_IPC_JSON_READER_H

#include <cstdint>
#include <map>
#include <string>

namespace ns3 {

/**
 * Parses one JSON object and keeps its top-level members that are integers or
 * strings; nested objects, arrays, booleans, null and non-integer numbers are
 * validated and skipped (inbound control lines carry them only as diagnostics).
 */
class E2IpcJsonReader
{
public:
  /** Parse a whole line; false if it is not exactly one well-formed JSON object. */
  bool Parse (const std::string &text);

  bool HasInt (const std::string &key) const;
  int64_t GetInt (const std::string &key) const;
  bool HasString (const std::string &key) const;
  std::string GetString (const std::string &key) const;

private:
  bool ParseValue (bool topLevelMember, const std::string &key);
  bool ParseString (std::string &out);
  bool ParseNumber (bool keep, const std::string &key);
  bool ParseLiteral (const char *word);
  void SkipSpace ();

  const std::string *m_text{nullptr};
  size_t m_pos{0};
  int m_depth{0};
  std::map<std::string, int64_t> m_ints;
  std::map<std::string, std::string> m_strings;
};

} // namespace ns3

#endif /* E2_IPC_JSON_READER_H */
