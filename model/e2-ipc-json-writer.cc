/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Minimal JSON writer for the ns3-e2-ipc NDJSON lines (no third-party library).
 */

#include <ns3/e2-ipc-json-writer.h>

#include <charconv>
#include <cmath>
#include <cstdio>

namespace ns3 {

void
E2IpcJsonWriter::BeforeValue ()
{
  if (m_afterKey)
    {
      m_afterKey = false;
      return;
    }
  if (!m_first.empty ())
    {
      if (!m_first.back ())
        {
          m_out += ',';
        }
      m_first.back () = false;
    }
}

void
E2IpcJsonWriter::BeginObject ()
{
  BeforeValue ();
  m_out += '{';
  m_first.push_back (true);
}

void
E2IpcJsonWriter::EndObject ()
{
  m_out += '}';
  m_first.pop_back ();
}

void
E2IpcJsonWriter::BeginArray ()
{
  BeforeValue ();
  m_out += '[';
  m_first.push_back (true);
}

void
E2IpcJsonWriter::EndArray ()
{
  m_out += ']';
  m_first.pop_back ();
}

void
E2IpcJsonWriter::Key (const std::string &key)
{
  BeforeValue ();
  WriteEscaped (key);
  m_out += ':';
  m_afterKey = true;
}

void
E2IpcJsonWriter::Int (int64_t value)
{
  BeforeValue ();
  char buf[24];
  auto res = std::to_chars (buf, buf + sizeof (buf), value);
  m_out.append (buf, res.ptr);
}

void
E2IpcJsonWriter::UInt (uint64_t value)
{
  BeforeValue ();
  char buf[24];
  auto res = std::to_chars (buf, buf + sizeof (buf), value);
  m_out.append (buf, res.ptr);
}

void
E2IpcJsonWriter::Double (double value)
{
  if (!std::isfinite (value))
    {
      Null ();
      return;
    }
  BeforeValue ();
  char buf[32];
  auto res = std::to_chars (buf, buf + sizeof (buf), value); // shortest round-trip form
  std::string text (buf, res.ptr);
  if (text.find_first_of (".eE") == std::string::npos)
    {
      text += ".0"; // keep the JSON number a real: "5" would read back as an integer
    }
  m_out += text;
}

void
E2IpcJsonWriter::Bool (bool value)
{
  BeforeValue ();
  m_out += value ? "true" : "false";
}

void
E2IpcJsonWriter::String (const std::string &value)
{
  BeforeValue ();
  WriteEscaped (value);
}

void
E2IpcJsonWriter::Null ()
{
  BeforeValue ();
  m_out += "null";
}

const std::string &
E2IpcJsonWriter::Str () const
{
  return m_out;
}

void
E2IpcJsonWriter::WriteEscaped (const std::string &text)
{
  m_out += '"';
  for (unsigned char c : text)
    {
      switch (c)
        {
        case '"':
          m_out += "\\\"";
          break;
        case '\\':
          m_out += "\\\\";
          break;
        case '\n':
          m_out += "\\n";
          break;
        case '\r':
          m_out += "\\r";
          break;
        case '\t':
          m_out += "\\t";
          break;
        default:
          if (c < 0x20)
            {
              char esc[8];
              std::snprintf (esc, sizeof (esc), "\\u%04x", c);
              m_out += esc;
            }
          else
            {
              m_out += static_cast<char> (c);
            }
        }
    }
  m_out += '"';
}

} // namespace ns3
