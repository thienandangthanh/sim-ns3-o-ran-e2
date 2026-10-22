/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Minimal reader for the flat inbound ns3-e2-ipc lines (agent -> ns-3).
 */

#include <ns3/e2-ipc-json-reader.h>

#include <charconv>
#include <cstring>

namespace ns3 {

namespace {
constexpr int kMaxDepth = 16;
} // namespace

bool
E2IpcJsonReader::Parse (const std::string &text)
{
  m_text = &text;
  m_pos = 0;
  m_depth = 0;
  m_ints.clear ();
  m_strings.clear ();
  SkipSpace ();
  if (m_pos >= text.size () || text[m_pos] != '{')
    {
      return false;
    }
  if (!ParseValue (false, ""))
    {
      return false;
    }
  SkipSpace ();
  return m_pos == text.size ();
}

bool
E2IpcJsonReader::HasInt (const std::string &key) const
{
  return m_ints.count (key) != 0;
}

int64_t
E2IpcJsonReader::GetInt (const std::string &key) const
{
  auto it = m_ints.find (key);
  return it == m_ints.end () ? 0 : it->second;
}

bool
E2IpcJsonReader::HasString (const std::string &key) const
{
  return m_strings.count (key) != 0;
}

std::string
E2IpcJsonReader::GetString (const std::string &key) const
{
  auto it = m_strings.find (key);
  return it == m_strings.end () ? std::string () : it->second;
}

void
E2IpcJsonReader::SkipSpace ()
{
  const std::string &t = *m_text;
  while (m_pos < t.size () &&
         (t[m_pos] == ' ' || t[m_pos] == '\t' || t[m_pos] == '\r' || t[m_pos] == '\n'))
    {
      m_pos++;
    }
}

bool
E2IpcJsonReader::ParseLiteral (const char *word)
{
  size_t n = std::strlen (word);
  if (m_text->compare (m_pos, n, word) != 0)
    {
      return false;
    }
  m_pos += n;
  return true;
}

bool
E2IpcJsonReader::ParseString (std::string &out)
{
  const std::string &t = *m_text;
  if (m_pos >= t.size () || t[m_pos] != '"')
    {
      return false;
    }
  m_pos++;
  out.clear ();
  while (m_pos < t.size ())
    {
      char c = t[m_pos++];
      if (c == '"')
        {
          return true;
        }
      if (static_cast<unsigned char> (c) < 0x20)
        {
          return false;
        }
      if (c != '\\')
        {
          out += c;
          continue;
        }
      if (m_pos >= t.size ())
        {
          return false;
        }
      char e = t[m_pos++];
      switch (e)
        {
        case '"':
        case '\\':
        case '/':
          out += e;
          break;
        case 'b':
          out += '\b';
          break;
        case 'f':
          out += '\f';
          break;
        case 'n':
          out += '\n';
          break;
        case 'r':
          out += '\r';
          break;
        case 't':
          out += '\t';
          break;
        case 'u':
          // Control lines carry ASCII only; keep the escape verbatim
          if (m_pos + 4 > t.size ())
            {
              return false;
            }
          out += "\\u" + t.substr (m_pos, 4);
          m_pos += 4;
          break;
        default:
          return false;
        }
    }
  return false;
}

bool
E2IpcJsonReader::ParseNumber (bool keep, const std::string &key)
{
  const std::string &t = *m_text;
  size_t start = m_pos;
  bool integer = true;
  if (m_pos < t.size () && t[m_pos] == '-')
    {
      m_pos++;
    }
  size_t digits = m_pos;
  while (m_pos < t.size () && t[m_pos] >= '0' && t[m_pos] <= '9')
    {
      m_pos++;
    }
  if (m_pos == digits)
    {
      return false;
    }
  while (m_pos < t.size () && (std::strchr (".eE+-", t[m_pos]) != nullptr ||
                               (t[m_pos] >= '0' && t[m_pos] <= '9')))
    {
      integer = false;
      m_pos++;
    }
  if (keep && integer)
    {
      int64_t v = 0;
      auto r = std::from_chars (t.data () + start, t.data () + m_pos, v);
      if (r.ec != std::errc () || r.ptr != t.data () + m_pos)
        {
          return false;
        }
      m_ints[key] = v;
    }
  return true;
}

bool
E2IpcJsonReader::ParseValue (bool topLevelMember, const std::string &key)
{
  const std::string &t = *m_text;
  SkipSpace ();
  if (m_pos >= t.size ())
    {
      return false;
    }
  char c = t[m_pos];
  if (c == '{' || c == '[')
    {
      if (++m_depth > kMaxDepth)
        {
          return false;
        }
      bool object = c == '{';
      char close = object ? '}' : ']';
      bool outer = m_depth == 1;
      m_pos++;
      SkipSpace ();
      if (m_pos < t.size () && t[m_pos] == close)
        {
          m_pos++;
          m_depth--;
          return true;
        }
      while (true)
        {
          std::string member;
          if (object)
            {
              SkipSpace ();
              if (!ParseString (member))
                {
                  return false;
                }
              SkipSpace ();
              if (m_pos >= t.size () || t[m_pos] != ':')
                {
                  return false;
                }
              m_pos++;
            }
          if (!ParseValue (object && outer, member))
            {
              return false;
            }
          SkipSpace ();
          if (m_pos < t.size () && t[m_pos] == ',')
            {
              m_pos++;
              continue;
            }
          if (m_pos < t.size () && t[m_pos] == close)
            {
              m_pos++;
              m_depth--;
              return true;
            }
          return false;
        }
    }
  if (c == '"')
    {
      std::string v;
      if (!ParseString (v))
        {
          return false;
        }
      if (topLevelMember)
        {
          m_strings[key] = v;
        }
      return true;
    }
  if (c == 't')
    {
      return ParseLiteral ("true");
    }
  if (c == 'f')
    {
      return ParseLiteral ("false");
    }
  if (c == 'n')
    {
      return ParseLiteral ("null");
    }
  return ParseNumber (topLevelMember, key);
}

} // namespace ns3
