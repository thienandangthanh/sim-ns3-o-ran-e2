/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Non-blocking TCP client that carries the ns3-e2-ipc NDJSON lines of one E2
 * node to its out-of-process E2 agent.
 */

#ifndef E2_IPC_TCP_CLIENT_H
#define E2_IPC_TCP_CLIENT_H

#include <chrono>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace ns3 {

/**
 * Never blocks the simulation: connect, send and receive are non-blocking,
 * every send uses MSG_NOSIGNAL|MSG_DONTWAIT, and progress happens only when
 * the emitter calls Service() (once per sample). Unsent bytes wait in a
 * bounded pending buffer. On any socket error the connection is closed, the
 * lines still pending (including a partially sent one) are discarded and
 * counted, and a new connect is attempted after a backoff of 0.1 s doubling
 * to 5 s, measured on the monotonic (wall) clock, never on simulation time.
 * Inbound bytes are split into complete lines (at most 1 MiB each), which the
 * emitter takes with TakeInboundLines (ns3-e2-ipc v1 minor 1: control lines).
 */
class E2IpcTcpClient
{
public:
  E2IpcTcpClient (std::string host, uint16_t port, size_t maxPendingBytes);
  ~E2IpcTcpClient ();

  E2IpcTcpClient (const E2IpcTcpClient &) = delete;
  E2IpcTcpClient &operator= (const E2IpcTcpClient &) = delete;

  /** Advance connect / drain / flush without blocking. */
  void Service ();

  /** Wait up to timeout for a pending connect to complete (used once at start). */
  void WaitConnected (std::chrono::milliseconds timeout);

  /** Wait up to timeout for the pending buffer to drain (used once at the end). */
  void Flush (std::chrono::milliseconds timeout);

  bool IsConnected () const;

  /** The socket, or -1 while not connected (the pacing barrier polls it). */
  int GetFd () const;

  /** True once after each successful connect: the caller must queue its hello first. */
  bool TakeNewConnection ();

  enum class EnqueueResult { Queued, Disconnected, Backpressure };

  /**
   * Queue one complete line (newline included) and try to send it.
   * \param isSample whether the line is a sample (only samples are counted)
   */
  EnqueueResult Enqueue (const std::string &line, bool isSample);

  /** Samples queued earlier that a connection error discarded, since the last call. */
  uint64_t TakeLostSamples ();

  /** Samples whose last byte was accepted by send () */
  uint64_t GetSentSamples () const;

  /** Samples still waiting in the pending buffer */
  uint64_t GetPendingSamples () const;

  uint64_t GetConnectCount () const;

  /** Moves the complete inbound lines received so far (newline stripped) into lines. */
  void TakeInboundLines (std::vector<std::string> &lines);

private:
  using Clock = std::chrono::steady_clock; // CLOCK_MONOTONIC

  enum class State { Idle, Connecting, Connected };

  struct PendingLine
  {
    size_t remaining; //!< bytes of this line not yet sent
    bool isSample;
  };

  void StartConnect ();
  void CheckConnect ();
  void MarkConnected ();
  void Drain ();
  void SendPending ();
  void Fail (const char *what, int err);
  void WaitFd (short events, std::chrono::milliseconds timeout);

  std::string m_host;
  uint16_t m_port;
  size_t m_maxPending;

  int m_fd{-1};
  State m_state{State::Idle};
  bool m_newConnection{false};
  Clock::time_point m_nextAttempt{};     //!< earliest time of the next connect
  Clock::time_point m_connectDeadline{}; //!< give up an in-progress connect after this
  std::chrono::milliseconds m_backoff{100};

  std::string m_buf;   //!< pending bytes; m_buf[m_off..] still to send
  size_t m_off{0};
  std::deque<PendingLine> m_lines;
  uint64_t m_lostSamples{0};
  uint64_t m_sentSamples{0};
  uint64_t m_connects{0};

  std::string m_inBuf;                  //!< inbound bytes of the line being received
  std::vector<std::string> m_inLines;   //!< complete inbound lines not taken yet
};

} // namespace ns3

#endif /* E2_IPC_TCP_CLIENT_H */
