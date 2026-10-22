/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Non-blocking TCP client that carries the ns3-e2-ipc NDJSON lines of one E2
 * node to its out-of-process E2 agent.
 */

#include <ns3/e2-ipc-tcp-client.h>

#include <ns3/log.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("E2IpcTcpClient");

namespace {
constexpr std::chrono::milliseconds kMinBackoff{100};
constexpr std::chrono::milliseconds kMaxBackoff{5000};
constexpr std::chrono::milliseconds kConnectTimeout{5000};
constexpr size_t kCompactThreshold = 1 << 20;
constexpr size_t kMaxInboundLine = 1 << 20;
} // namespace

E2IpcTcpClient::E2IpcTcpClient (std::string host, uint16_t port, size_t maxPendingBytes)
    : m_host (std::move (host)), m_port (port), m_maxPending (maxPendingBytes)
{
  m_nextAttempt = Clock::now ();
}

E2IpcTcpClient::~E2IpcTcpClient ()
{
  if (m_fd >= 0)
    {
      close (m_fd);
    }
}

void
E2IpcTcpClient::Service ()
{
  if (m_state == State::Idle && Clock::now () >= m_nextAttempt)
    {
      StartConnect ();
    }
  if (m_state == State::Connecting)
    {
      CheckConnect ();
    }
  if (m_state == State::Connected)
    {
      Drain ();
    }
  if (m_state == State::Connected)
    {
      SendPending ();
    }
}

void
E2IpcTcpClient::StartConnect ()
{
  addrinfo hints{};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_NUMERICSERV;
  addrinfo *res = nullptr;
  int gai = getaddrinfo (m_host.c_str (), std::to_string (m_port).c_str (), &hints, &res);
  if (gai != 0 || res == nullptr)
    {
      NS_LOG_WARN ("E2 IPC: cannot resolve " << m_host << ": " << gai_strerror (gai));
      Fail ("resolve", 0);
      return;
    }
  m_fd = socket (res->ai_family, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
  if (m_fd < 0)
    {
      freeaddrinfo (res);
      Fail ("socket", errno);
      return;
    }
  int one = 1;
  setsockopt (m_fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof (one));
  int rc = connect (m_fd, res->ai_addr, res->ai_addrlen);
  int err = errno;
  freeaddrinfo (res);
  if (rc == 0)
    {
      MarkConnected ();
    }
  else if (err == EINPROGRESS)
    {
      m_state = State::Connecting;
      m_connectDeadline = Clock::now () + kConnectTimeout;
    }
  else
    {
      Fail ("connect", err);
    }
}

void
E2IpcTcpClient::MarkConnected ()
{
  m_state = State::Connected;
  m_newConnection = true;
  m_backoff = kMinBackoff;
  m_connects++;
}

void
E2IpcTcpClient::CheckConnect ()
{
  pollfd pfd{m_fd, POLLOUT, 0};
  if (poll (&pfd, 1, 0) > 0)
    {
      int err = 0;
      socklen_t len = sizeof (err);
      getsockopt (m_fd, SOL_SOCKET, SO_ERROR, &err, &len);
      if (err != 0)
        {
          Fail ("connect", err);
          return;
        }
      MarkConnected ();
    }
  else if (Clock::now () > m_connectDeadline)
    {
      Fail ("connect timeout", ETIMEDOUT);
    }
}

void
E2IpcTcpClient::Drain ()
{
  char buf[4096];
  while (true)
    {
      ssize_t n = recv (m_fd, buf, sizeof (buf), MSG_DONTWAIT);
      if (n > 0)
        {
          m_inBuf.append (buf, static_cast<size_t> (n));
          size_t start = 0;
          size_t nl;
          while ((nl = m_inBuf.find ('\n', start)) != std::string::npos)
            {
              m_inLines.emplace_back (m_inBuf, start, nl - start);
              start = nl + 1;
            }
          m_inBuf.erase (0, start);
          if (m_inBuf.size () > kMaxInboundLine)
            {
              Fail ("inbound line longer than 1 MiB", 0);
              return;
            }
          continue;
        }
      if (n == 0)
        {
          Fail ("peer closed", 0);
          return;
        }
      if (errno == EINTR)
        {
          continue;
        }
      if (errno != EAGAIN && errno != EWOULDBLOCK)
        {
          Fail ("recv", errno);
        }
      return;
    }
}

void
E2IpcTcpClient::SendPending ()
{
  while (m_off < m_buf.size ())
    {
      ssize_t n =
          send (m_fd, m_buf.data () + m_off, m_buf.size () - m_off, MSG_NOSIGNAL | MSG_DONTWAIT);
      if (n < 0)
        {
          if (errno == EINTR)
            {
              continue;
            }
          if (errno != EAGAIN && errno != EWOULDBLOCK)
            {
              Fail ("send", errno);
            }
          break;
        }
      m_off += static_cast<size_t> (n);
      // account the bytes to the queued lines, oldest first
      size_t left = static_cast<size_t> (n);
      while (left > 0 && !m_lines.empty ())
        {
          size_t take = std::min (left, m_lines.front ().remaining);
          m_lines.front ().remaining -= take;
          left -= take;
          if (m_lines.front ().remaining == 0)
            {
              m_sentSamples += m_lines.front ().isSample ? 1 : 0;
              m_lines.pop_front ();
            }
        }
    }
  if (m_off == m_buf.size ())
    {
      m_buf.clear ();
      m_off = 0;
    }
  else if (m_off > kCompactThreshold)
    {
      m_buf.erase (0, m_off);
      m_off = 0;
    }
}

void
E2IpcTcpClient::Fail (const char *what, int err)
{
  if (m_fd >= 0)
    {
      close (m_fd);
      m_fd = -1;
    }
  for (const auto &line : m_lines)
    {
      m_lostSamples += line.isSample ? 1 : 0; // a partially sent line is lost too
    }
  m_lines.clear ();
  m_buf.clear ();
  m_off = 0;
  m_inBuf.clear (); // a partial inbound line dies with its connection
  m_state = State::Idle;
  m_newConnection = false;
  m_nextAttempt = Clock::now () + m_backoff;
  NS_LOG_WARN ("E2 IPC " << m_host << ":" << m_port << " " << what << " failed ("
                         << (err ? std::strerror (err) : "no error code") << "), retry in "
                         << m_backoff.count () << " ms");
  m_backoff = std::min (m_backoff * 2, kMaxBackoff);
}

void
E2IpcTcpClient::WaitFd (short events, std::chrono::milliseconds timeout)
{
  if (m_fd < 0 || timeout.count () <= 0)
    {
      return;
    }
  pollfd pfd{m_fd, events, 0};
  poll (&pfd, 1, static_cast<int> (timeout.count ()));
}

void
E2IpcTcpClient::WaitConnected (std::chrono::milliseconds timeout)
{
  Service ();
  if (m_state == State::Connecting)
    {
      WaitFd (POLLOUT, timeout);
      Service ();
    }
}

void
E2IpcTcpClient::Flush (std::chrono::milliseconds timeout)
{
  const auto deadline = Clock::now () + timeout;
  while (true)
    {
      Service ();
      if (m_state != State::Connected || m_off == m_buf.size ())
        {
          return;
        }
      auto left = std::chrono::duration_cast<std::chrono::milliseconds> (deadline - Clock::now ());
      if (left.count () <= 0)
        {
          return;
        }
      WaitFd (POLLOUT, std::min (left, std::chrono::milliseconds (100)));
    }
}

bool
E2IpcTcpClient::IsConnected () const
{
  return m_state == State::Connected;
}

bool
E2IpcTcpClient::TakeNewConnection ()
{
  bool fresh = m_newConnection;
  m_newConnection = false;
  return fresh;
}

E2IpcTcpClient::EnqueueResult
E2IpcTcpClient::Enqueue (const std::string &line, bool isSample)
{
  if (m_state != State::Connected)
    {
      return EnqueueResult::Disconnected;
    }
  if (m_buf.size () - m_off + line.size () > m_maxPending)
    {
      return EnqueueResult::Backpressure;
    }
  m_buf += line;
  m_lines.push_back (PendingLine{line.size (), isSample});
  SendPending ();
  return EnqueueResult::Queued;
}

uint64_t
E2IpcTcpClient::TakeLostSamples ()
{
  uint64_t lost = m_lostSamples;
  m_lostSamples = 0;
  return lost;
}

uint64_t
E2IpcTcpClient::GetSentSamples () const
{
  return m_sentSamples;
}

uint64_t
E2IpcTcpClient::GetPendingSamples () const
{
  return std::count_if (m_lines.begin (), m_lines.end (),
                        [] (const PendingLine &l) { return l.isSample; });
}

uint64_t
E2IpcTcpClient::GetConnectCount () const
{
  return m_connects;
}

void
E2IpcTcpClient::TakeInboundLines (std::vector<std::string> &lines)
{
  for (auto &l : m_inLines)
    {
      lines.push_back (std::move (l));
    }
  m_inLines.clear ();
}

} // namespace ns3
