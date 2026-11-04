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
#include <set>
#include <string>

namespace ns3 {

/**
 * Parses one JSON object and keeps its top-level members that are integers or
 * strings, and the integer members of an object that is itself a top-level
 * member (such as a control's `ueId`). Everything else (deeper objects, arrays,
 * booleans, null, non-integer numbers) is validated and skipped; HasMember
 * still reports a top-level member of any type.
 */
class E2IpcJsonReader
{
public:
  /**
   * Parse a whole line; false if it is not exactly one well-formed JSON object,
   * or if any object in it, at any level, repeats a member name.
   */
  bool Parse (const std::string &text);

  bool HasInt (const std::string &key) const;
  int64_t GetInt (const std::string &key) const;
  bool HasString (const std::string &key) const;
  std::string GetString (const std::string &key) const;

  /** The top-level object has a member named key, whatever its type. */
  bool HasMember (const std::string &key) const;

  /** Integer member key of the object that is the top-level member parent. */
  bool HasInt (const std::string &parent, const std::string &key) const;
  int64_t GetInt (const std::string &parent, const std::string &key) const;

private:
  /**
   * level 1: a member of the top-level object; level 2: a member of an object
   * that is a level-1 member (named parent); 0: anything else (not kept).
   */
  bool ParseValue (int level, const std::string &parent, const std::string &key);
  bool ParseString (std::string &out);
  bool ParseNumber (int level, const std::string &parent, const std::string &key);
  bool ParseLiteral (const char *word);
  void SkipSpace ();

  const std::string *m_text{nullptr};
  size_t m_pos{0};
  int m_depth{0};
  std::map<std::string, int64_t> m_ints;
  std::map<std::string, std::string> m_strings;
  std::set<std::string> m_members;
  std::map<std::string, std::map<std::string, int64_t>> m_nestedInts;
};

} // namespace ns3

#endif /* E2_IPC_JSON_READER_H */
