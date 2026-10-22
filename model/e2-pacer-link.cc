/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * NDJSON TCP link from ns-3 to the lock-step pacer.
 */

#include <ns3/e2-pacer-link.h>

#include <cerrno>
#include <cstring>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

namespace ns3 {

E2PacerLink::~E2PacerLink ()
{
  Close ();
}

void
E2PacerLink::Close ()
{
  if (m_fd >= 0)
    {
      close (m_fd);
      m_fd = -1;
    }
}

bool
E2PacerLink::Connect (const std::string &host, uint16_t port, std::chrono::milliseconds timeout)
{
  const auto deadline = std::chrono::steady_clock::now () + timeout;
  addrinfo hints{};
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  while (true)
    {
      addrinfo *res = nullptr;
      if (getaddrinfo (host.c_str (), std::to_string (port).c_str (), &hints, &res) == 0 && res)
        {
          int fd = socket (res->ai_family, res->ai_socktype | SOCK_CLOEXEC, res->ai_protocol);
          if (fd >= 0 && connect (fd, res->ai_addr, res->ai_addrlen) == 0)
            {
              int one = 1;
              setsockopt (fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof (one));
              freeaddrinfo (res);
              m_fd = fd;
              return true;
            }
          if (fd >= 0)
            {
              close (fd);
            }
        }
      if (res)
        {
          freeaddrinfo (res);
        }
      if (std::chrono::steady_clock::now () >= deadline)
        {
          return false;
        }
      std::this_thread::sleep_for (std::chrono::milliseconds (100));
    }
}

bool
E2PacerLink::Send (const std::string &line)
{
  size_t off = 0;
  while (m_fd >= 0 && off < line.size ())
    {
      ssize_t n = send (m_fd, line.data () + off, line.size () - off, MSG_NOSIGNAL);
      if (n > 0)
        {
          off += static_cast<size_t> (n);
        }
      else if (n < 0 && errno == EINTR)
        {
          continue;
        }
      else
        {
          Close ();
        }
    }
  return m_fd >= 0;
}

bool
E2PacerLink::Read (std::vector<std::string> &lines)
{
  char buf[4096];
  while (m_fd >= 0)
    {
      ssize_t n = recv (m_fd, buf, sizeof (buf), MSG_DONTWAIT);
      if (n > 0)
        {
          m_in.append (buf, static_cast<size_t> (n));
          continue;
        }
      if (n < 0 && errno == EINTR)
        {
          continue;
        }
      if (n == 0 || (errno != EAGAIN && errno != EWOULDBLOCK))
        {
          Close ();
        }
      break;
    }
  size_t start = 0;
  size_t nl;
  while ((nl = m_in.find ('\n', start)) != std::string::npos)
    {
      lines.emplace_back (m_in, start, nl - start);
      start = nl + 1;
    }
  m_in.erase (0, start);
  return m_fd >= 0;
}

int
E2PacerLink::GetFd () const
{
  return m_fd;
}

bool
E2PacerLink::IsOpen () const
{
  return m_fd >= 0;
}

} // namespace ns3
