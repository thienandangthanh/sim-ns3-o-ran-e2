/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * E2ControlHub: the apply order of a batch, the UE resolution at the
 * control's node and the accepted control line form.
 *
 * The hub runs on its own (no emitter, free pacing) in a temporary working
 * directory, where it writes e2-control-ledger.jsonl and e2-pacing-log.jsonl.
 * Two fake en-gNBs stand in for ns-3: each resolves (mEnbUeX2apId, mnCellId) to
 * the UE whose current secondary node it is and admits a control the way the
 * master eNB does (unknown target, then in handover, then already on target).
 */

#include <ns3/e2-control-hub.h>
#include <ns3/e2-ipc-json-reader.h>
#include <ns3/nstime.h>
#include <ns3/simulator.h>
#include <ns3/test.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <vector>

using namespace ns3;

namespace {

/** One UE of the fake RAN. */
struct FakeUe
{
  uint16_t x2apId;
  uint32_t mnCellId;
  uint16_t servingCell; //!< current secondary node (cell)
  bool inHandover;
};

/** UE state shared by the fake en-gNBs, keyed by IMSI. */
struct FakeRan
{
  std::map<uint64_t, FakeUe> ues;
  std::set<uint16_t> cells{2, 3};
};

/** A fake en-gNB registered with the hub for its cell. */
class FakeEnGnb
{
public:
  FakeEnGnb (FakeRan &ran, uint16_t cell) : m_ran (ran), m_cell (cell)
  {
  }

  uint64_t
  Resolve (uint16_t x2apId, uint32_t mnCellId) const
  {
    for (const auto &kv : m_ran.ues)
      {
        if (kv.second.x2apId == x2apId && kv.second.mnCellId == mnCellId &&
            kv.second.servingCell == m_cell)
          {
            return kv.first;
          }
      }
    return 0;
  }

  E2ControlResult
  Apply (uint64_t imsi, uint32_t mnCellId, uint16_t targetCellId)
  {
    m_calls++;
    E2ControlResult res;
    FakeUe &ue = m_ran.ues.at (imsi);
    res.sourceCellId = ue.servingCell;
    if (mnCellId != ue.mnCellId)
      {
        res.cause = "unknownUe";
      }
    else if (m_ran.cells.count (targetCellId) == 0)
      {
        res.cause = "unknownTarget";
      }
    else if (ue.inHandover)
      {
        res.cause = "inHandover";
      }
    else if (ue.servingCell == targetCellId)
      {
        res.cause = "alreadyOnTarget";
      }
    else
      {
        ue.inHandover = true;
        res.applied = true;
      }
    return res;
  }

  /** Controls this node was asked to execute. */
  int
  GetCalls () const
  {
    return m_calls;
  }

  void
  Register (Ptr<E2ControlHub> hub)
  {
    hub->SetControlHandler (m_cell, MakeCallback (&FakeEnGnb::Resolve, this),
                            MakeCallback (&FakeEnGnb::Apply, this));
  }

private:
  FakeRan &m_ran;
  uint16_t m_cell;
  int m_calls{0};
};

/** Runs a test body in a fresh temporary directory; restores the working directory. */
class TempWorkDir
{
public:
  TempWorkDir () : m_old (std::filesystem::current_path ())
  {
    std::string tmpl = (std::filesystem::temp_directory_path () / "e2-control-order-XXXXXX").string ();
    char *dir = mkdtemp (tmpl.data ());
    NS_ABORT_MSG_IF (dir == nullptr, "Can't create a temporary directory");
    m_dir = dir;
    std::filesystem::current_path (m_dir);
  }

  ~TempWorkDir ()
  {
    std::filesystem::current_path (m_old);
    std::error_code ec;
    std::filesystem::remove_all (m_dir, ec);
  }

private:
  std::filesystem::path m_old;
  std::filesystem::path m_dir;
};

std::vector<std::string>
ReadLines (const std::string &path)
{
  std::vector<std::string> lines;
  std::ifstream in (path);
  for (std::string l; std::getline (in, l);)
    {
      lines.push_back (l);
    }
  return lines;
}

/** One ledger line, as the checks read it. */
struct LedgerLine
{
  int64_t viaNodeId;
  int64_t ctlId;
  int64_t imsi;
  int64_t targetCell;
  int64_t sourceCell;
  std::string outcome;
  std::string cause;

  bool
  operator== (const LedgerLine &o) const
  {
    return std::tie (viaNodeId, ctlId, imsi, targetCell, sourceCell, outcome, cause) ==
           std::tie (o.viaNodeId, o.ctlId, o.imsi, o.targetCell, o.sourceCell, o.outcome, o.cause);
  }
};

/** The control lines of the ledger (its header skipped). */
std::vector<LedgerLine>
ReadLedger ()
{
  std::vector<LedgerLine> out;
  for (const std::string &l : ReadLines ("e2-control-ledger.jsonl"))
    {
      E2IpcJsonReader r;
      NS_ABORT_MSG_UNLESS (r.Parse (l), "unparsable ledger line " << l);
      if (r.GetString ("type") == "ledgerHeader")
        {
          continue;
        }
      out.push_back ({r.GetInt ("viaNodeId"), r.GetInt ("ctlId"), r.GetInt ("imsi"),
                      r.GetInt ("targetCell"), r.GetInt ("sourceCell"), r.GetString ("outcome"),
                      r.GetString ("cause")});
    }
  return out;
}

/** The agent's IPC control line, in the form ns-3 accepts. */
std::string
ControlLine (uint16_t nodeId, uint64_t ctlId, uint16_t x2apId, uint32_t mnCellId,
             uint16_t targetCellId, int64_t afterSeq)
{
  return "{\"type\":\"control\",\"v\":1,\"nodeId\":" + std::to_string (nodeId) +
         ",\"ctlId\":" + std::to_string (ctlId) +
         ",\"ric\":{\"requestorId\":1,\"instanceId\":1,\"ranFuncId\":3},\"style\":5,\"action\":3,"
         "\"ueId\":{\"mEnbUeX2apId\":" +
         std::to_string (x2apId) + ",\"mnCellId\":" + std::to_string (mnCellId) +
         "},\"targetCellId\":" + std::to_string (targetCellId) +
         ",\"afterSeq\":" + std::to_string (afterSeq) + ",\"diag\":{\"rxUnixMs\":1}}";
}

/** text with its one occurrence of from replaced by to (a fixture edit). */
std::string
Replaced (std::string text, const std::string &from, const std::string &to)
{
  size_t at = text.find (from);
  NS_ABORT_MSG_IF (at == std::string::npos || text.find (from, at + 1) != std::string::npos,
                   "fixture: " << from);
  return text.replace (at, from.size (), to);
}

/** The pacing-log record of an inbound control. */
std::string
PacingRecord (uint16_t via, uint64_t ctlId, uint16_t x2apId, uint32_t mnCellId, uint16_t target,
              int64_t afterSeq)
{
  return "{\"type\":\"control\",\"v\":1,\"viaNodeId\":" + std::to_string (via) +
         ",\"ctlId\":" + std::to_string (ctlId) + ",\"ueId\":{\"mEnbUeX2apId\":" +
         std::to_string (x2apId) + ",\"mnCellId\":" + std::to_string (mnCellId) +
         "},\"targetCellId\":" + std::to_string (target) +
         ",\"afterSeq\":" + std::to_string (afterSeq) + "}";
}

std::vector<std::string>
PacingControlRecords ()
{
  std::vector<std::string> out;
  for (const std::string &l : ReadLines ("e2-pacing-log.jsonl"))
    {
      if (l.rfind ("{\"type\":\"control\"", 0) == 0)
        {
          out.push_back (l);
        }
    }
  return out;
}

/** The scenario's UE: IMSI 2, MeNB UE X2AP ID 4 on MN cell 1, on secondary cell 3. */
FakeRan
ScenarioRan ()
{
  FakeRan ran;
  ran.ues[2] = {4, 1, 3, false};
  ran.ues[6] = {6, 1, 2, false};
  return ran;
}

/** Arrival order of one agent's controls: the agent assigns ctlIds as it forwards them. */
bool
ArrivalOrder (const E2IpcControl &a, const E2IpcControl &b)
{
  return std::tie (a.viaNodeId, a.ctlId) < std::tie (b.viaNodeId, b.ctlId);
}

/**
 * Same-step pair for one UE through its source en-gNB, arriving lower ctlId first:
 * ctlId 1 to target 3, ctlId 2 to target 2. The check: the higher-ctlId, target-2
 * control is applied first, then the lower-ctlId, target-3 one is refused
 * inHandover, both via 3 and both naming IMSI 2.
 */
bool
SameUePairAppliedInSortOrder (const std::vector<LedgerLine> &ledger)
{
  const std::vector<LedgerLine> want = {{3, 2, 2, 2, 3, "applied", ""},
                                        {3, 1, 2, 3, 3, "refused", "inHandover"}};
  return ledger == want;
}

// ---------------------------------------------------------------------------

/** The full (imsi, viaNodeId, targetCellId, ctlId) order of E2ControlApplyOrder. */
class E2ControlApplyOrderFullTestCase : public TestCase
{
public:
  E2ControlApplyOrderFullTestCase ()
      : TestCase ("E2ControlApplyOrder sorts by (imsi, viaNodeId, targetCellId, ctlId)")
  {
  }

private:
  void
  DoRun () override
  {
    // Each pair of neighbours in want differs in exactly one key, from the last key
    // to the first, and each key overrides every later one
    std::vector<std::tuple<uint64_t, uint16_t, uint16_t, uint64_t>> want = {
        {0, 2, 2, 9}, {0, 3, 1, 1}, {1, 1, 4, 5}, {1, 2, 1, 7}, {1, 2, 3, 2},
        {1, 2, 3, 8}, {2, 1, 1, 1}, {2, 3, 2, 2}, {2, 3, 3, 1}, {6, 2, 3, 1}};
    std::vector<E2IpcControl> batch;
    for (const auto &k : want)
      {
        E2IpcControl c;
        std::tie (c.imsi, c.viaNodeId, c.targetCellId, c.ctlId) = k;
        batch.push_back (c);
      }
    std::reverse (batch.begin (), batch.end ());
    std::rotate (batch.begin (), batch.begin () + 3, batch.end ());
    std::sort (batch.begin (), batch.end (), &E2ControlApplyOrder);
    for (size_t i = 0; i < want.size (); i++)
      {
        const E2IpcControl &c = batch[i];
        NS_TEST_ASSERT_MSG_EQ ((std::make_tuple (c.imsi, c.viaNodeId, c.targetCellId, c.ctlId) ==
                                want[i]),
                               true, "position " << i << " of the sorted batch");
      }
    E2IpcControl a;
    E2IpcControl b;
    a.imsi = b.imsi = 2;
    a.viaNodeId = b.viaNodeId = 3;
    a.targetCellId = 3;
    a.ctlId = 1;
    b.targetCellId = 2;
    b.ctlId = 2;
    NS_TEST_ASSERT_MSG_EQ (E2ControlApplyOrder (b, a), true, "lower target before lower ctlId");
    NS_TEST_ASSERT_MSG_EQ (E2ControlApplyOrder (a, b), false, "strict weak order");
    NS_TEST_ASSERT_MSG_EQ (E2ControlApplyOrder (a, a), false, "irreflexive");
  }
};

/**
 * The same-UE pair through the source en-gNB, end to end through the hub, with
 * the hub's order or with the order swapped for arrival order. The same check
 * must hold with the hub's order and fail with arrival order.
 */
class E2ControlSameUePairTestCase : public TestCase
{
public:
  E2ControlSameUePairTestCase (E2ControlHub::ApplyOrder order, bool hubOrder)
      : TestCase (std::string ("same-UE pair via the source en-gNB, ") +
                  (hubOrder ? "hub order: the check holds"
                            : "order swapped for arrival order: the check fails")),
        m_order (order),
        m_hubOrder (hubOrder)
  {
  }

private:
  void
  DoRun () override
  {
    TempWorkDir dir;
    FakeRan ran = ScenarioRan ();
    FakeEnGnb gnb2 (ran, 2);
    FakeEnGnb gnb3 (ran, 3);
    Ptr<E2ControlHub> hub = CreateObject<E2ControlHub> ();
    gnb2.Register (hub);
    gnb3.Register (hub);
    if (!m_hubOrder)
      {
        hub->SetApplyOrder (m_order);
      }
    // Arrival: ctlId 1 to target 3, then ctlId 2 to target 2, both through node 3
    hub->OnInboundLine (3, ControlLine (3, 1, 4, 1, 3, 30));
    hub->OnInboundLine (3, ControlLine (3, 2, 4, 1, 2, 30));
    hub->OnSampleEmitted (3, 30);
    const std::vector<LedgerLine> ledger = ReadLedger ();
    NS_TEST_ASSERT_MSG_EQ (ledger.size (), 2u, "one ledger line per control");
    NS_TEST_ASSERT_MSG_EQ (SameUePairAppliedInSortOrder (ledger), m_hubOrder,
                           "applied target 2 (ctlId 2), then refused inHandover target 3 (ctlId 1)");
    const std::vector<int64_t> arrival = {1, 2};
    std::vector<int64_t> applied;
    for (const LedgerLine &l : ledger)
      {
        applied.push_back (l.ctlId);
      }
    NS_TEST_ASSERT_MSG_EQ ((applied != arrival), m_hubOrder,
                           "the hub's order differs from arrival order");
    if (!m_hubOrder)
      {
        // Arrival order: target 3 first, refused alreadyOnTarget, then target 2 applied
        const std::vector<LedgerLine> want = {{3, 1, 2, 3, 3, "refused", "alreadyOnTarget"},
                                              {3, 2, 2, 2, 3, "applied", ""}};
        NS_TEST_ASSERT_MSG_EQ ((ledger == want), true, "arrival-order outcomes");
      }
    Simulator::Destroy ();
  }

  E2ControlHub::ApplyOrder m_order;
  bool m_hubOrder;
};

/**
 * UE resolution at the control's node: through a non-source en-gNB and through a
 * node with no handler (the eNB) the UE is unknown (imsi 0); through its source
 * it is applied. The ledger is in apply order on the resolved IMSI, and every
 * inbound control has its pacing-log record.
 */
class E2ControlResolutionTestCase : public TestCase
{
public:
  E2ControlResolutionTestCase ()
      : TestCase ("controls resolve their UE at their node; unresolved -> unknownUe, imsi 0")
  {
  }

private:
  void
  DoRun () override
  {
    TempWorkDir dir;
    FakeRan ran = ScenarioRan ();
    FakeEnGnb gnb2 (ran, 2);
    FakeEnGnb gnb3 (ran, 3);
    Ptr<E2ControlHub> hub = CreateObject<E2ControlHub> ();
    gnb2.Register (hub);
    gnb3.Register (hub);
    hub->OnInboundLine (2, ControlLine (2, 1, 4, 1, 2, 20)); // IMSI 2 through node 2: not its SN
    hub->OnInboundLine (3, ControlLine (3, 1, 4, 1, 2, 20)); // IMSI 2 through its source
    hub->OnInboundLine (1, ControlLine (1, 1, 6, 1, 3, 20)); // the eNB registers no handler
    hub->OnInboundLine (2, ControlLine (2, 2, 4095, 1, 3, 20)); // an X2AP ID no UE holds
    hub->OnInboundLine (2, ControlLine (2, 3, 6, 1, 3, 20)); // IMSI 6 through its source
    hub->OnSampleEmitted (2, 20);
    const std::vector<LedgerLine> want = {
        {1, 1, 0, 3, 0, "refused", "unknownUe"}, {2, 1, 0, 2, 0, "refused", "unknownUe"},
        {2, 2, 0, 3, 0, "refused", "unknownUe"}, {3, 1, 2, 2, 3, "applied", ""},
        {2, 3, 6, 3, 2, "applied", ""}};
    NS_TEST_ASSERT_MSG_EQ ((ReadLedger () == want), true, "ledger lines and order");
    const std::vector<std::string> records = {
        PacingRecord (2, 1, 4, 1, 2, 20), PacingRecord (3, 1, 4, 1, 2, 20),
        PacingRecord (1, 1, 6, 1, 3, 20), PacingRecord (2, 2, 4095, 1, 3, 20),
        PacingRecord (2, 3, 6, 1, 3, 20)};
    NS_TEST_ASSERT_MSG_EQ ((PacingControlRecords () == records), true,
                           "one pacing-log record per inbound control, in arrival order");
    Simulator::Destroy ();
  }
};

/**
 * The control line form: the UE is named by ueId only. A line with an imsi, with
 * no ueId or an incomplete or out-of-range one, or with another style, action or
 * version, is malformed: no ledger line, no pacing record.
 */
class E2ControlLineFormTestCase : public TestCase
{
public:
  E2ControlLineFormTestCase ()
      : TestCase ("only the ueId control form is accepted")
  {
  }

private:
  void
  DoRun () override
  {
    E2IpcJsonReader r;
    NS_TEST_ASSERT_MSG_EQ (r.Parse (ControlLine (3, 1, 4, 1, 2, 7)), true, "binding form parses");
    NS_TEST_ASSERT_MSG_EQ (r.HasInt ("ueId", "mEnbUeX2apId"), true, "nested X2AP ID");
    NS_TEST_ASSERT_MSG_EQ (r.GetInt ("ueId", "mEnbUeX2apId"), 4, "nested X2AP ID value");
    NS_TEST_ASSERT_MSG_EQ (r.GetInt ("ueId", "mnCellId"), 1, "nested MN cell value");
    NS_TEST_ASSERT_MSG_EQ (r.HasInt ("ric", "ranFuncId"), true, "other nested members are kept");
    NS_TEST_ASSERT_MSG_EQ (r.HasInt ("diag", "rxUnixMs"), true, "diag kept, unused");
    NS_TEST_ASSERT_MSG_EQ (r.HasMember ("ueId"), true, "object member present");
    NS_TEST_ASSERT_MSG_EQ (r.HasInt ("ueId"), false, "an object is not an integer");
    NS_TEST_ASSERT_MSG_EQ (r.HasMember ("imsi"), false, "no imsi");
    NS_TEST_ASSERT_MSG_EQ (r.Parse ("{\"a\":{\"b\":{\"c\":1}},\"d\":[{\"e\":2}]}"), true, "deep");
    NS_TEST_ASSERT_MSG_EQ (r.HasInt ("a", "b") || r.HasInt ("b", "c") || r.HasInt ("d", "e"),
                           false, "nothing deeper than one nested object is kept");

    TempWorkDir dir;
    FakeRan ran = ScenarioRan ();
    FakeEnGnb gnb3 (ran, 3);
    Ptr<E2ControlHub> hub = CreateObject<E2ControlHub> ();
    gnb3.Register (hub);
    const std::string ok = ControlLine (3, 9, 4, 1, 2, 5);
    auto with = [&ok] (const std::string &from, const std::string &to) {
      std::string s = ok;
      size_t at = s.find (from);
      NS_ABORT_MSG_IF (at == std::string::npos, "fixture: " << from);
      return s.replace (at, from.size (), to);
    };
    const std::vector<std::string> malformed = {
        with ("\"targetCellId\"", "\"imsi\":2,\"targetCellId\""),        // imsi beside ueId
        with ("\"ueId\":{\"mEnbUeX2apId\":4,\"mnCellId\":1},", "\"imsi\":2,"), // the old form
        with ("\"ueId\":{\"mEnbUeX2apId\":4,\"mnCellId\":1},", ""),        // no ueId
        with ("\"ueId\":{\"mEnbUeX2apId\":4,\"mnCellId\":1}", "\"ueId\":4"), // not an object
        with ("\"mEnbUeX2apId\":4,", ""),                                  // no X2AP ID
        with (",\"mnCellId\":1", ""),                                      // no MN cell
        with ("\"mEnbUeX2apId\":4", "\"mEnbUeX2apId\":4096"),              // X2AP ID > 4095
        with ("\"mEnbUeX2apId\":4", "\"mEnbUeX2apId\":-1"),
        with ("\"mnCellId\":1", "\"mnCellId\":1048576"),                   // > 20 bits
        with ("\"style\":5", "\"style\":3"),                               // Style 3
        with ("\"action\":3", "\"action\":1"),                             // action 1
        with ("\"style\":5,", ""),                                         // no style
        with ("\"v\":1,", ""),                                             // no version
        with ("\"nodeId\":3", "\"nodeId\":2"),                             // another node's line
        with ("\"targetCellId\":2", "\"targetCellId\":65536")};
    for (const std::string &l : malformed)
      {
        hub->OnInboundLine (3, l);
      }
    hub->OnInboundLine (3, ok);
    hub->OnSampleEmitted (3, 5);
    const std::vector<LedgerLine> want = {{3, 9, 2, 2, 3, "applied", ""}};
    NS_TEST_ASSERT_MSG_EQ ((ReadLedger () == want), true, "only the well-formed line is handled");
    NS_TEST_ASSERT_MSG_EQ ((PacingControlRecords () ==
                            std::vector<std::string>{PacingRecord (3, 9, 4, 1, 2, 5)}),
                           true, "only the well-formed line is recorded");
    Simulator::Destroy ();
  }
};

/**
 * δ > 0: a deferred batch resolves its UEs again in its apply event; a late line
 * keeps the IMSI resolved when it was received: at its barrier for a pending
 * control the stop cancels, at the stop for a buffered one.
 */
class E2ControlDeferredResolutionTestCase : public TestCase
{
public:
  E2ControlDeferredResolutionTestCase ()
      : TestCase ("δ > 0 resolves in the apply event; late lines keep their rx resolution")
  {
  }

private:
  void
  DoRun () override
  {
    TempWorkDir dir;
    FakeRan ran = ScenarioRan ();
    FakeEnGnb gnb2 (ran, 2);
    FakeEnGnb gnb3 (ran, 3);
    Ptr<E2ControlHub> hub = CreateObject<E2ControlHub> ();
    hub->SetAttribute ("ActuationDelay", TimeValue (MilliSeconds (150)));
    gnb2.Register (hub);
    gnb3.Register (hub);

    // Decided at barrier 10 with IMSI 2 on 3; before the apply event IMSI 2 moves to 2
    hub->OnInboundLine (3, ControlLine (3, 1, 4, 1, 2, 10));
    hub->OnSampleEmitted (3, 10);
    ran.ues[2].servingCell = 2;
    Simulator::Run ();
    std::vector<LedgerLine> want = {{3, 1, 0, 2, 0, "refused", "unknownUe"}};
    NS_TEST_ASSERT_MSG_EQ ((ReadLedger () == want), true, "resolved again in the apply event");

    // Decided at barrier 11 (IMSI 6 on 2), pending when the emitters stop; node 2
    // deregisters first, as a disposed en-gNB does
    hub->OnInboundLine (2, ControlLine (2, 1, 6, 1, 3, 11));
    hub->OnSampleEmitted (3, 11);
    // Buffered (no barrier yet) when the emitters stop: resolved at the stop
    hub->OnInboundLine (3, ControlLine (3, 2, 4, 1, 3, 12)); // IMSI 2 is on 2 now: unknown at 3
    hub->OnInboundLine (2, ControlLine (2, 2, 4, 1, 3, 12)); // through 2: deregistered at the stop
    hub->SetControlHandler (2, E2ControlHub::UeResolver (), E2ControlHub::ControlHandler ());
    hub->OnEmitterStopping (3);
    want.push_back ({2, 1, 6, 3, 0, "refused", "late"});
    want.push_back ({3, 2, 0, 3, 0, "refused", "late"});
    want.push_back ({2, 2, 0, 3, 0, "refused", "late"});
    NS_TEST_ASSERT_MSG_EQ ((ReadLedger () == want), true, "late lines and their IMSIs");
    NS_TEST_ASSERT_MSG_EQ (hub->IsStopping (), true, "stopping");
    // Arriving while stopping: answered late at once, resolved at reception (node 3 still
    // registered, IMSI 6 is not its UE; IMSI 2 back on 3 is)
    ran.ues[2].servingCell = 3;
    hub->OnInboundLine (3, ControlLine (3, 3, 4, 1, 2, 12));
    want.push_back ({3, 3, 2, 2, 0, "refused", "late"});
    NS_TEST_ASSERT_MSG_EQ ((ReadLedger () == want), true, "a control arriving at the stop");
    NS_TEST_ASSERT_MSG_EQ (PacingControlRecords ().size (), 5u, "every inbound control recorded");
    Simulator::Destroy ();
  }
};

/**
 * A control line that repeats a member name in one object is malformed: the
 * reader rejects it before keeping any value, so its parts are never merged
 * into a control. No execution, no ledger line, no pacing record.
 */
class E2ControlRepeatedMemberTestCase : public TestCase
{
public:
  E2ControlRepeatedMemberTestCase (const std::string &what, const std::string &line)
      : TestCase ("a control line with a repeated member is malformed: " + what),
        m_line (line)
  {
  }

private:
  void
  DoRun () override
  {
    // EXPECT, not ASSERT: every check is reported, so a reader that let the line
    // through shows that it was executed and recorded
    E2IpcJsonReader r;
    NS_TEST_EXPECT_MSG_EQ (r.Parse (m_line), false, "the reader rejects " << m_line);
    TempWorkDir dir;
    FakeRan ran = ScenarioRan ();
    FakeEnGnb gnb3 (ran, 3);
    Ptr<E2ControlHub> hub = CreateObject<E2ControlHub> ();
    gnb3.Register (hub);
    hub->OnInboundLine (3, m_line);
    hub->OnSampleEmitted (3, 5);
    NS_TEST_EXPECT_MSG_EQ (gnb3.GetCalls (), 0, "not executed");
    NS_TEST_EXPECT_MSG_EQ (ReadLedger ().size (), 0u, "no ledger line");
    NS_TEST_EXPECT_MSG_EQ (PacingControlRecords ().size (), 0u, "no pacing record");
    Simulator::Destroy ();
  }

  std::string m_line;
};

/** The binding line of the repeated-member cases: IMSI 2 through its source 3, to cell 2. */
std::string
RepeatedMemberBase ()
{
  return ControlLine (3, 9, 4, 1, 2, 5);
}

class E2ControlOrderTestSuite : public TestSuite
{
public:
  E2ControlOrderTestSuite () : TestSuite ("e2-control-order", UNIT)
  {
    // The end-to-end pair first: ns-3 stops a suite at its first failing case
    AddTestCase (new E2ControlSameUePairTestCase (&E2ControlApplyOrder, true), TestCase::QUICK);
    AddTestCase (new E2ControlSameUePairTestCase (&ArrivalOrder, false), TestCase::QUICK);
    AddTestCase (new E2ControlApplyOrderFullTestCase (), TestCase::QUICK);
    AddTestCase (new E2ControlResolutionTestCase (), TestCase::QUICK);
    AddTestCase (new E2ControlLineFormTestCase (), TestCase::QUICK);
    AddTestCase (new E2ControlDeferredResolutionTestCase (), TestCase::QUICK);
    const std::string base = RepeatedMemberBase ();
    const std::string ueId = "\"ueId\":{\"mEnbUeX2apId\":4,\"mnCellId\":1}";
    AddTestCase (new E2ControlRepeatedMemberTestCase (
                     "ueId split over two objects",
                     Replaced (base, ueId,
                               "\"ueId\":{\"mEnbUeX2apId\":4},\"ueId\":{\"mnCellId\":1}")),
                 TestCase::QUICK);
    AddTestCase (new E2ControlRepeatedMemberTestCase (
                     "style repeated as a string",
                     Replaced (base, "\"style\":5", "\"style\":5,\"style\":\"3\"")),
                 TestCase::QUICK);
    AddTestCase (new E2ControlRepeatedMemberTestCase (
                     "action repeated as a string",
                     Replaced (base, "\"action\":3", "\"action\":3,\"action\":\"1\"")),
                 TestCase::QUICK);
    AddTestCase (new E2ControlRepeatedMemberTestCase (
                     "ctlId repeated at the top level",
                     Replaced (base, "\"targetCellId\"", "\"ctlId\":10,\"targetCellId\"")),
                 TestCase::QUICK);
    AddTestCase (new E2ControlRepeatedMemberTestCase (
                     "X2AP ID repeated inside ueId",
                     Replaced (base, ueId,
                               "\"ueId\":{\"mEnbUeX2apId\":6,\"mEnbUeX2apId\":4,\"mnCellId\":1}")),
                 TestCase::QUICK);
  }
};

E2ControlOrderTestSuite g_e2ControlOrderTestSuite;

} // namespace
