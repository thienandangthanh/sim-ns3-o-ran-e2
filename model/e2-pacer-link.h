/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * NDJSON TCP link from ns-3 to the lock-step pacer (a harness process).
 */

#ifndef E2_PACER_LINK_H
#define E2_PACER_LINK_H

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace ns3 {

/**
 * Used only while ns-3 is blocked at a pacing barrier, so it may block: Send
 * writes the whole line, Connect retries until its deadline. Reads never
 * block; a closed or failed link stays closed (the barrier then aborts).
 */
class E2PacerLink
{
public:
  ~E2PacerLink ();

  /** Connect, retrying every 100 ms until timeout; false if never connected. */
  bool Connect (const std::string &host, uint16_t port, std::chrono::milliseconds timeout);

  /** Write one complete line (newline included); false and closed on error. */
  bool Send (const std::string &line);

  /** Move the complete lines received so far into lines; false once the peer has closed. */
  bool Read (std::vector<std::string> &lines);

  int GetFd () const;
  bool IsOpen () const;

private:
  void Close ();

  int m_fd{-1};
  std::string m_in;
};

} // namespace ns3

#endif /* E2_PACER_LINK_H */
