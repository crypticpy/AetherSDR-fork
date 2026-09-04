// B25 AUTO CLEAN -- the app half: the "held by AUTO" note on a stage card,
// the AUTO CLEAN card's own inspector (state+why, then its recent moves),
// and the one write the toggle is allowed to make.
//
// The governor's shape and rules are docs/DIVERSITY.md's own "AUTO CLEAN:
// the chain decides" section, verbatim in gui/AetherGateChainAuto.h. This
// file tests gui/AetherGateChainAuto.cpp's parsing and formatting plus the
// two hooks AetherGateChainWindow.cpp added to call it -- it does NOT retest
// anything AetherGateChainStrip/Stage already cover (tile selection, no
// word wrap, the settling window), the way aether_gate_chain_frontend_test
// .cpp and aether_gate_chain_squeeze_test.cpp do not either.
//
// The auto_clean chain row and the "squeeze" row it needs to show a held
// squeeze note are built locally, the same way squeeze's own
// withSqueezeChainRow() is -- B25 landed after AetherGateChainFixture.h was
// written, and the task that adds it may not change a helper an earlier
// test already depends on.
#include "AetherGateChainFixture.h"

#include <QApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPushButton>
#include <QScrollBar>
#include <QTest>

#include <cstdio>

using namespace AetherGateChainFixture;

namespace {

constexpr int kTabChain = 0;

void bringUp(AetherGateChainWindow* w)
{
    w->resize(1120, 820);
    w->setCurrentTab(kTabChain);
    settle();
    w->grab();
    settle();
}

// One /device body with a "frontend" key, applied the way
// aether_gate_chain_frontend_test.cpp's own feedDevice() is -- only the
// render test below needs it, to put the FRONT END card's synthetic GUARD
// row on screen so a held "guard" tool has a row to hold.
void feedDevice(AetherGateChainWindow* w, const QJsonObject& fe)
{
    w->applyDevice(QJsonDocument::fromJson(deviceWithFrontend(fe)).object());
    settle();
}

// The auto_clean row, at the head of chain[] the way chainstatus.py puts it
// -- kind "toggle", action /diversity/set?auto=<the OTHER state>, because the
// action is always the write that would flip it.
QJsonObject autoCleanRow(bool on)
{
    QJsonObject row;
    row.insert(QStringLiteral("id"), QStringLiteral("auto_clean"));
    row.insert(QStringLiteral("name"), QStringLiteral("AUTO CLEAN"));
    row.insert(QStringLiteral("kind"), QStringLiteral("toggle"));
    row.insert(QStringLiteral("enabled"), on);
    row.insert(QStringLiteral("detail"), on ? QStringLiteral("holding 1")
                                            : QStringLiteral("off"));
    QJsonObject action;
    action.insert(QStringLiteral("label"), on ? QStringLiteral("OFF") : QStringLiteral("ON"));
    action.insert(QStringLiteral("route"), QStringLiteral("/diversity/set"));
    action.insert(QStringLiteral("query"),
                 on ? QStringLiteral("auto=off") : QStringLiteral("auto=on"));
    row.insert(QStringLiteral("action"), action);
    return row;
}

// One more chain[] row, shaped the way chainstatus.py's own _squeeze_row()
// shapes it -- the same row aether_gate_chain_squeeze_test.cpp's
// withSqueezeChainRow() builds, copied rather than shared because that
// helper is local to that file for the same reason this one is local here.
QJsonObject squeezeRow()
{
    QJsonObject row;
    row.insert(QStringLiteral("id"), QStringLiteral("squeeze"));
    row.insert(QStringLiteral("name"), QStringLiteral("SQUEEZE"));
    row.insert(QStringLiteral("kind"), QStringLiteral("value"));
    row.insert(QStringLiteral("enabled"), true);
    row.insert(QStringLiteral("detail"), QStringLiteral("null · 1450 Hz"));
    QJsonObject action;
    action.insert(QStringLiteral("label"), QStringLiteral("RELEASE"));
    action.insert(QStringLiteral("route"), QStringLiteral("/diversity/set"));
    action.insert(QStringLiteral("query"), QStringLiteral("squeeze=off"));
    row.insert(QStringLiteral("action"), action);
    return row;
}

// kChainFullFilter with auto_clean prepended and squeeze appended -- so a
// held squeeze note has a card to land on and the toggle test has its row.
QByteArray withAutoCleanChain(bool autoOn, const QJsonObject& governor = QJsonObject())
{
    QJsonObject root = QJsonDocument::fromJson(kChainFullFilter).object();
    QJsonArray chain = root.value(QStringLiteral("chain")).toArray();
    chain.insert(0, autoCleanRow(autoOn));
    chain.append(squeezeRow());
    root.insert(QStringLiteral("chain"), chain);
    if (!governor.isEmpty())
        root.insert(QStringLiteral("governor"), governor);
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

// One row of governor.holding[]/pending -- {tool, kind, why, since, delta_db}.
// `hasDelta` false omits delta_db (JSON null), the "measuring, no score yet"
// case chainAutoNoteForStage() must not print a "0.0 dB" for.
QJsonObject held(const QString& tool, const QString& kind, const QString& why,
                 double since, bool hasDelta, double deltaDb = 0.0)
{
    QJsonObject h;
    h.insert(QStringLiteral("tool"), tool);
    h.insert(QStringLiteral("kind"), kind);
    h.insert(QStringLiteral("why"), why);
    h.insert(QStringLiteral("since"), since);
    h.insert(QStringLiteral("delta_db"), hasDelta ? QJsonValue(deltaDb) : QJsonValue());
    return h;
}

// One row of governor.events[] -- held's shape plus `t` and `result`. `wall`
// is the real epoch-seconds companion core/governor.py's events now carry
// alongside `t`'s monotonic one; omitted (hasWall false) reproduces an older
// gate that never sends it.
QJsonObject event(double t, const QString& tool, const QString& kind, const QString& why,
                  const QString& result, bool hasDelta, double deltaDb = 0.0,
                  bool hasWall = false, double wall = 0.0)
{
    QJsonObject e = held(tool, kind, why, 0.0, hasDelta, deltaDb);
    e.insert(QStringLiteral("t"), t);
    e.insert(QStringLiteral("result"), result);
    if (hasWall)
        e.insert(QStringLiteral("wall"), wall);
    return e;
}

// `until_wall` is core/governor.py's real epoch-seconds companion to
// `until`'s monotonic one; omitted (hasUntilWall false) reproduces an older
// gate that never sends it.
QJsonObject backoff(const QString& kind, const QString& tool, double until,
                    bool hasUntilWall = false, double untilWall = 0.0)
{
    QJsonObject b;
    b.insert(QStringLiteral("kind"), kind);
    b.insert(QStringLiteral("tool"), tool);
    b.insert(QStringLiteral("until"), until);
    if (hasUntilWall)
        b.insert(QStringLiteral("until_wall"), untilWall);
    return b;
}

// One row of governor.ruled_out[].
QJsonObject ruledOut(const QString& tool, const QString& why)
{
    QJsonObject r;
    r.insert(QStringLiteral("tool"), tool);
    r.insert(QStringLiteral("why"), why);
    return r;
}

// The whole governor block, /diversity/governor's own shape (docs/DIVERSITY
// .md "AUTO CLEAN: the chain decides").
QJsonObject governor(bool autoOn, const QString& state, const QString& why,
                     const QJsonArray& holding = {}, const QJsonValue& pending = QJsonValue(),
                     const QJsonArray& events = {}, const QJsonArray& backoffs = {},
                     const QJsonArray& ruledOutRows = {},
                     const QString& objectiveSource = QString(),
                     const QString& error = QString())
{
    QJsonObject g;
    g.insert(QStringLiteral("available"), true);
    g.insert(QStringLiteral("auto"), autoOn);
    g.insert(QStringLiteral("state"), state);
    g.insert(QStringLiteral("why"), why);
    g.insert(QStringLiteral("settle_s"), 5.0);
    g.insert(QStringLiteral("margin_db"), 1.0);
    g.insert(QStringLiteral("spread_db"), 2.0);
    g.insert(QStringLiteral("objective_source"), objectiveSource);
    g.insert(QStringLiteral("holding"), holding);
    g.insert(QStringLiteral("pending"), pending);
    g.insert(QStringLiteral("events"), events);
    g.insert(QStringLiteral("ruled_out"), ruledOutRows);
    g.insert(QStringLiteral("backoff"), backoffs);
    g.insert(QStringLiteral("error"), error);
    return g;
}

// 12:41:07 UTC-relative epoch seconds so the event-line tests' expected
// "HH:mm:ss" text is deterministic regardless of the machine's zone: built
// from QDateTime::currentDateTime()'s own date at a fixed local time, the
// same trick a local-time round-trip needs.
double localEpoch(int h, int m, int s)
{
    QDateTime dt = QDateTime::currentDateTime();
    dt.setTime(QTime(h, m, s));
    return dt.toMSecsSinceEpoch() / 1000.0;
}

// --------------------------------------------------------------------------
// The "held by AUTO" note
// --------------------------------------------------------------------------

void testNoteForHeldSqueeze()
{
    FakeGate net;
    AetherGateApplet applet(nullptr, &net);
    const QJsonArray holding = {held(QStringLiteral("squeeze"), QStringLiteral("carrier"),
                                     QStringLiteral("strongest first"), 100.0, true, 1.8)};
    connectGate(applet, net,
               withAutoCleanChain(true, governor(true, QStringLiteral("settling"),
                                                 QStringLiteral("holding squeeze"), holding)));
    AetherGateChainWindow* w = openChain(applet);
    CHECK(w != nullptr);
    if (!w)
        return;
    bringUp(w);

    auto* note = w->findChild<QLabel*>(QStringLiteral("gateChainAutoNote_squeeze"));
    CHECK(note != nullptr);
    if (!note)
        return;
    CHECK(note->isVisible());
    // toolTip()/accessibleDescription(), not text(): chainAutoApplyNotes()
    // runs the note through chainFitToWidth() the same way the card's own
    // WHY line is, so text() may be word-truncated to fit the card's 178 px
    // -- the tooltip and accessible description are set from the untouched
    // string and are the exact-match surface.
    CHECK(note->toolTip() == QStringLiteral("AUTO · carrier · strongest first, +1.8 dB"));
    CHECK(note->accessibleDescription() == note->toolTip());
    CHECK(note->text().startsWith(QStringLiteral("AUTO · carrier")));
    CHECK(!note->wordWrap());
}

// A HELD tool (not pending) with no score yet -- freshly applied, before the
// next tick has measured it. Verified by hand during development: dropping
// the "if (h.hasDelta)" guard in chainAutoNoteForStage() (always append the
// delta) makes this the one test in the file that catches it -- every other
// held case here carries a delta, so an unguarded append would otherwise
// pass by accident with a spurious ", +0.0 dB" nobody's assertion checks
// for. Mutated, reran (this test's toolTip CHECK below failed as expected),
// restored, and confirmed `git diff` on AetherGateChainAuto.cpp was clean.
void testNoteForHeldToolWithNoScoreYetOmitsDelta()
{
    FakeGate net;
    AetherGateApplet applet(nullptr, &net);
    const QJsonArray holding = {held(QStringLiteral("mode"), QStringLiteral("floor"),
                                     QStringLiteral("coherence steady"), 100.0, false)};
    connectGate(applet, net,
               withAutoCleanChain(true, governor(true, QStringLiteral("applying"),
                                                 QStringLiteral("nulling floor"), holding)));
    AetherGateChainWindow* w = openChain(applet);
    CHECK(w != nullptr);
    if (!w)
        return;
    bringUp(w);

    auto* note = w->findChild<QLabel*>(QStringLiteral("gateChainAutoNote_combiner"));
    CHECK(note != nullptr);
    if (!note)
        return;
    CHECK(note->isVisible());
    CHECK(note->toolTip() == QStringLiteral("AUTO · floor · coherence steady"));
}

void testNoteForPendingNbHasNoScoreYet()
{
    FakeGate net;
    AetherGateApplet applet(nullptr, &net);
    const QJsonValue pending =
        held(QStringLiteral("nb"), QStringLiteral("impulse"), QStringLiteral("1.4/s"),
            100.0, false);
    connectGate(applet, net,
               withAutoCleanChain(true, governor(true, QStringLiteral("measuring"),
                                                 QStringLiteral("trying blanker"), {},
                                                 pending)));
    AetherGateChainWindow* w = openChain(applet);
    CHECK(w != nullptr);
    if (!w)
        return;
    bringUp(w);

    auto* note = w->findChild<QLabel*>(QStringLiteral("gateChainAutoNote_nb"));
    CHECK(note != nullptr);
    if (!note)
        return;
    CHECK(note->isVisible());
    CHECK(note->toolTip() == QStringLiteral("AUTO · trying · 1.4/s"));
    CHECK(note->text().startsWith(QStringLiteral("AUTO · trying")));
}

void testNoteRemovedWhenHoldingEmpties()
{
    FakeGate net;
    AetherGateApplet applet(nullptr, &net);
    const QJsonArray holding = {held(QStringLiteral("squeeze"), QStringLiteral("mains"),
                                     QStringLiteral("comb found"), 100.0, true, 2.1)};
    connectGate(applet, net,
               withAutoCleanChain(true, governor(true, QStringLiteral("settling"),
                                                 QStringLiteral("holding squeeze"), holding)));
    AetherGateChainWindow* w = openChain(applet);
    CHECK(w != nullptr);
    if (!w)
        return;
    bringUp(w);

    auto* note = w->findChild<QLabel*>(QStringLiteral("gateChainAutoNote_squeeze"));
    CHECK(note != nullptr);
    if (note)
        CHECK(note->isVisible());

    // The gate releases it: the next /filter carries an empty holding[].
    net.routes[QStringLiteral("/filter")] = {
        QNetworkReply::NoError,
        withAutoCleanChain(true, governor(true, QStringLiteral("idle"),
                                          QStringLiteral("nothing to hold")))};
    filterTick(applet);

    note = w->findChild<QLabel*>(QStringLiteral("gateChainAutoNote_squeeze"));
    CHECK(note != nullptr);
    if (note)
        CHECK(!note->isVisible());
    // MUTATION: in chainAutoApplyNotes(), drop the
    // `note->setVisible(!text.isEmpty())` call (leave a once-shown note
    // visible forever) and this pair fails on the second CHECK above -- kept
    // as a comment because it is a one-line removal, not a build worth
    // scripting for CI.
}

// --------------------------------------------------------------------------
// Name hygiene
// --------------------------------------------------------------------------

void testNoteIsADirectChildOfItsOwnTileOnly()
{
    FakeGate net;
    AetherGateApplet applet(nullptr, &net);
    const QJsonArray holding = {held(QStringLiteral("squeeze"), QStringLiteral("carrier"),
                                     QStringLiteral("strongest first"), 100.0, true, 1.8)};
    connectGate(applet, net,
               withAutoCleanChain(true, governor(true, QStringLiteral("settling"),
                                                 QStringLiteral("holding squeeze"), holding)));
    AetherGateChainWindow* w = openChain(applet);
    CHECK(w != nullptr);
    if (!w)
        return;
    bringUp(w);

    AetherGateChainTile* tile = strip(w)->tile(QStringLiteral("squeeze"));
    CHECK(tile != nullptr);
    if (!tile)
        return;
    auto direct = tile->findChildren<QLabel*>(QStringLiteral("gateChainAutoNote_squeeze"),
                                              Qt::FindDirectChildrenOnly);
    CHECK(direct.size() == 1);
    // Not found two levels down as some other tile's grandchild.
    AetherGateChainTile* nb = strip(w)->tile(QStringLiteral("nb"));
    CHECK(nb != nullptr);
    if (nb)
        CHECK(nb->findChildren<QLabel*>(QStringLiteral("gateChainAutoNote_squeeze"),
                                        Qt::FindDirectChildrenOnly)
                  .isEmpty());
}

// --------------------------------------------------------------------------
// The AUTO CLEAN card's own inspector
// --------------------------------------------------------------------------

void testInspectorEventLinesNewestFirstWithResultAndDelta()
{
    FakeGate net;
    AetherGateApplet applet(nullptr, &net);
    const QJsonArray events = {
        event(localEpoch(12, 40, 1), QStringLiteral("nb"), QStringLiteral("impulse"),
             QStringLiteral("1.4/s"), QStringLiteral("released"), false),
        event(localEpoch(12, 41, 7), QStringLiteral("squeeze"), QStringLiteral("carrier"),
             QStringLiteral("strongest first"), QStringLiteral("kept"), true, 1.8),
        event(localEpoch(12, 42, 30), QStringLiteral("guard"), QStringLiteral("neighbour"),
             QStringLiteral("headroom low"), QStringLiteral("undone"), true, -0.9),
    };
    connectGate(applet, net,
               withAutoCleanChain(true, governor(true, QStringLiteral("idle"),
                                                 QStringLiteral("nothing to hold"), {},
                                                 QJsonValue(), events)));
    AetherGateChainWindow* w = openChain(applet);
    CHECK(w != nullptr);
    if (!w)
        return;
    bringUp(w);

    // selectStage(), not a click on the toggle: AetherGateChainTile's own
    // mousePressEvent is what a real click on the CARD reaches, and the
    // switch is a child QPushButton that consumes the press before it gets
    // there -- clicking the switch actually writes, which is its own test
    // below and must not be entangled with this one.
    strip(w)->selectStage(QStringLiteral("auto_clean"));
    settle();

    auto* state = w->findChild<QLabel*>(QStringLiteral("gateChainAutoState"));
    CHECK(state != nullptr);
    if (state)
        CHECK(state->text() == QStringLiteral("idle · nothing to hold"));

    auto* events_ = w->findChild<QLabel*>(QStringLiteral("gateChainAutoEvents"));
    CHECK(events_ != nullptr);
    if (!events_)
        return;
    CHECK(events_->isVisible());
    CHECK(!events_->wordWrap());
    const QStringList lines = events_->text().split(QLatin1Char('\n'));
    CHECK(lines.size() == 3);
    if (lines.size() != 3)
        return;
    // Newest first: the 12:42:30 undone row, then 12:41:07 kept, then
    // 12:40:01 released.
    CHECK(lines.at(0) == QStringLiteral("12:42:30 · guard · neighbour · undone -0.9 dB · "
                                        "headroom low"));
    CHECK(lines.at(1) == QStringLiteral("12:41:07 · squeeze · carrier · kept +1.8 dB · "
                                        "strongest first"));
    CHECK(lines.at(2) == QStringLiteral("12:40:01 · nb · impulse · released · 1.4/s"));
}

void testInspectorErrorEventAndBackoffLine()
{
    FakeGate net;
    AetherGateApplet applet(nullptr, &net);
    const QJsonArray events = {
        event(localEpoch(9, 1, 0), QStringLiteral("mode"), QStringLiteral("floor"),
             QStringLiteral("bad value: mode='x'"), QStringLiteral("error"), false),
    };
    const QJsonArray backoffs = {
        backoff(QStringLiteral("mains"), QStringLiteral("squeeze"), localEpoch(12, 46, 0)),
    };
    connectGate(applet, net,
               withAutoCleanChain(true, governor(true, QStringLiteral("backoff"),
                                                 QStringLiteral("mains/squeeze backing off"),
                                                 {}, QJsonValue(), events, backoffs)));
    AetherGateChainWindow* w = openChain(applet);
    CHECK(w != nullptr);
    if (!w)
        return;
    bringUp(w);

    strip(w)->selectStage(QStringLiteral("auto_clean"));
    settle();

    auto* events_ = w->findChild<QLabel*>(QStringLiteral("gateChainAutoEvents"));
    CHECK(events_ != nullptr);
    if (!events_)
        return;
    const QStringList lines = events_->text().split(QLatin1Char('\n'));
    CHECK(lines.size() == 2);
    if (lines.size() != 2)
        return;
    CHECK(lines.at(0) == QStringLiteral("09:01:00 · mode · floor · error: bad value: "
                                        "mode='x'"));
    CHECK(lines.at(1) == QStringLiteral("backing off: mains/squeeze until 12:46"));
}

// H2: events[].wall, governor.ruled_out/objective_source/error, and
// backoff[].until_wall -- all previously parsed-and-ignored or never parsed
// at all. Each test below turns exactly one of them on and checks the one
// line it is supposed to change; the "absent" half of every one of these is
// already covered above (testInspectorEventLinesNewestFirstWithResultAndDelta
// and testInspectorErrorEventAndBackoffLine both omit wall/until_wall/
// ruled_out/objective_source and still pass unchanged).
// --------------------------------------------------------------------------

// A `t` chosen to format as a wildly wrong time of day (it is the gate's
// monotonic clock, not epoch seconds) so the assertion only passes if the
// line is actually reading `wall`, not silently still reading `t`.
void testEventLinePrefersWallOverT()
{
    FakeGate net;
    AetherGateApplet applet(nullptr, &net);
    const QJsonArray events = {
        event(12.5, QStringLiteral("nb"), QStringLiteral("impulse"), QStringLiteral("1.4/s"),
             QStringLiteral("released"), false, 0.0, true, localEpoch(9, 15, 3)),
    };
    connectGate(applet, net,
               withAutoCleanChain(true, governor(true, QStringLiteral("idle"),
                                                 QStringLiteral("nothing to hold"), {},
                                                 QJsonValue(), events)));
    AetherGateChainWindow* w = openChain(applet);
    CHECK(w != nullptr);
    if (!w)
        return;
    bringUp(w);
    strip(w)->selectStage(QStringLiteral("auto_clean"));
    settle();

    auto* events_ = w->findChild<QLabel*>(QStringLiteral("gateChainAutoEvents"));
    CHECK(events_ != nullptr);
    if (!events_)
        return;
    CHECK(events_->text() == QStringLiteral("09:15:03 · nb · impulse · released · 1.4/s"));
}

void testBackoffLinePrefersUntilWallAndAddsSeconds()
{
    FakeGate net;
    AetherGateApplet applet(nullptr, &net);
    const QJsonArray backoffs = {
        backoff(QStringLiteral("mains"), QStringLiteral("squeeze"), 999.0, true,
               localEpoch(12, 46, 30)),
    };
    connectGate(applet, net,
               withAutoCleanChain(true, governor(true, QStringLiteral("backoff"),
                                                 QStringLiteral("mains/squeeze backing off"),
                                                 {}, QJsonValue(), {}, backoffs)));
    AetherGateChainWindow* w = openChain(applet);
    CHECK(w != nullptr);
    if (!w)
        return;
    bringUp(w);
    strip(w)->selectStage(QStringLiteral("auto_clean"));
    settle();

    auto* events_ = w->findChild<QLabel*>(QStringLiteral("gateChainAutoEvents"));
    CHECK(events_ != nullptr);
    if (events_)
        CHECK(events_->text() == QStringLiteral("backing off: mains/squeeze until 12:46:30"));
}

void testStateLineAppendsHeldBackFromRuledOut()
{
    FakeGate net;
    AetherGateApplet applet(nullptr, &net);
    const QJsonArray ruled = {
        ruledOut(QStringLiteral("guard"), QStringLiteral("the guard is already on")),
        ruledOut(QStringLiteral("dig"), QStringLiteral("talker 3.2 dB: waiting for steady")),
    };
    // `why` here is deliberately NOT the ruled_out join (the governor is
    // "applying" something else this tick), so the held-back clause has
    // something to say that `why` alone does not.
    connectGate(applet, net,
               withAutoCleanChain(true, governor(true, QStringLiteral("applying"),
                                                 QStringLiteral("nulling floor"), {},
                                                 QJsonValue(), {}, {}, ruled)));
    AetherGateChainWindow* w = openChain(applet);
    CHECK(w != nullptr);
    if (!w)
        return;
    bringUp(w);
    strip(w)->selectStage(QStringLiteral("auto_clean"));
    settle();

    auto* state = w->findChild<QLabel*>(QStringLiteral("gateChainAutoState"));
    CHECK(state != nullptr);
    if (state)
        CHECK(state->text()
              == QStringLiteral("applying · nulling floor · held back: the guard is "
                                "already on · talker 3.2 dB: waiting for steady"));
}

// When the governor is idle with nothing held, `why` IS already ruled_out's
// own join (core/governor.py's _why_idle()) -- the held-back clause must not
// repeat it a second time on the same line.
void testStateLineOmitsHeldBackWhenSameAsWhy()
{
    FakeGate net;
    AetherGateApplet applet(nullptr, &net);
    const QJsonArray ruled = {
        ruledOut(QStringLiteral("guard"), QStringLiteral("2 dB of ADC headroom, no clipping")),
    };
    connectGate(applet, net,
               withAutoCleanChain(true, governor(true, QStringLiteral("idle"),
                                                 QStringLiteral("2 dB of ADC headroom, no "
                                                               "clipping"),
                                                 {}, QJsonValue(), {}, {}, ruled)));
    AetherGateChainWindow* w = openChain(applet);
    CHECK(w != nullptr);
    if (!w)
        return;
    bringUp(w);
    strip(w)->selectStage(QStringLiteral("auto_clean"));
    settle();

    auto* state = w->findChild<QLabel*>(QStringLiteral("gateChainAutoState"));
    CHECK(state != nullptr);
    if (state)
        CHECK(state->text()
              == QStringLiteral("idle · 2 dB of ADC headroom, no clipping"));
}

void testStateLineAppendsObjectiveSource()
{
    FakeGate net;
    AetherGateApplet applet(nullptr, &net);
    connectGate(applet, net,
               withAutoCleanChain(true, governor(true, QStringLiteral("idle"),
                                                 QStringLiteral("nothing to hold"), {},
                                                 QJsonValue(), {}, {}, {},
                                                 QStringLiteral("none"))));
    AetherGateChainWindow* w = openChain(applet);
    CHECK(w != nullptr);
    if (!w)
        return;
    bringUp(w);
    strip(w)->selectStage(QStringLiteral("auto_clean"));
    settle();

    auto* state = w->findChild<QLabel*>(QStringLiteral("gateChainAutoState"));
    CHECK(state != nullptr);
    if (state)
        CHECK(state->text() == QStringLiteral("idle · nothing to hold · objective: none"));
}

void testStateLinePrefixesNonEmptyErrorAsWarning()
{
    FakeGate net;
    AetherGateApplet applet(nullptr, &net);
    connectGate(applet, net,
               withAutoCleanChain(true, governor(true, QStringLiteral("idle"),
                                                 QStringLiteral("nothing to hold"), {},
                                                 QJsonValue(), {}, {}, {}, QString(),
                                                 QStringLiteral("RuntimeError: adapter has "
                                                               "no device controls"))));
    AetherGateChainWindow* w = openChain(applet);
    CHECK(w != nullptr);
    if (!w)
        return;
    bringUp(w);
    strip(w)->selectStage(QStringLiteral("auto_clean"));
    settle();

    auto* state = w->findChild<QLabel*>(QStringLiteral("gateChainAutoState"));
    CHECK(state != nullptr);
    if (state)
        CHECK(state->text()
              == QStringLiteral("AUTO CLEAN ERROR: RuntimeError: adapter has no device "
                                "controls · idle · nothing to hold"));
}

// --------------------------------------------------------------------------
// The write path -- the one thing this task must not get wrong
// --------------------------------------------------------------------------

void testToggleWritesExactlyAutoOffWhileOnAndAutoOnWhileOff()
{
    FakeGate net;
    AetherGateApplet applet(nullptr, &net);
    connectGate(applet, net, withAutoCleanChain(true));
    AetherGateChainWindow* w = openChain(applet);
    CHECK(w != nullptr);
    if (!w)
        return;
    bringUp(w);

    auto* toggle = w->findChild<QPushButton*>(QStringLiteral("gateChainToggle_auto_clean"));
    CHECK(toggle != nullptr);
    if (!toggle)
        return;

    int writes = net.log.size();
    toggle->click();
    settle();
    CHECK(net.log.size() == writes + 1);
    CHECK(net.log.last() == QStringLiteral("/diversity/set?auto=off"));

    // The gate confirms off; the same button now asks for on.
    net.routes[QStringLiteral("/filter")] = {QNetworkReply::NoError,
                                             withAutoCleanChain(false)};
    filterTick(applet);

    writes = net.log.size();
    toggle->click();
    settle();
    CHECK(net.log.size() == writes + 1);
    CHECK(net.log.last() == QStringLiteral("/diversity/set?auto=on"));
    // MUTATION: swap the on/off strings in autoCleanRow()'s action.query
    // above (this fixture, not production code) and both CHECKs above fail
    // -- proof the assertion is reading the gate's own action.query rather
    // than a hard-coded guess. Verified by hand during development: flipped
    // the two literals, reran, watched both FAIL lines print, restored, and
    // confirmed `git diff` on this file was clean.
}

// --------------------------------------------------------------------------
// Names, and nothing scrolls
// --------------------------------------------------------------------------

void testEveryB25WidgetHasANameAndNothingScrollsAtInitialSize()
{
    FakeGate net;
    AetherGateApplet applet(nullptr, &net);
    // The longest why/kind pair this file writes, and three events plus a
    // backoff, so the no-scroll claim is checked against a full inspector
    // rather than an empty one that would pass by accident.
    const QJsonArray holding = {
        held(QStringLiteral("squeeze"), QStringLiteral("mains"),
            QStringLiteral("mains comb found and coherence at least 0.4"), 100.0, true, 3.4)};
    const QJsonArray events = {
        event(localEpoch(12, 40, 1), QStringLiteral("nb"), QStringLiteral("impulse"),
             QStringLiteral("1.4/s"), QStringLiteral("released"), false),
        event(localEpoch(12, 41, 7), QStringLiteral("squeeze"), QStringLiteral("carrier"),
             QStringLiteral("strongest first"), QStringLiteral("kept"), true, 1.8),
        event(localEpoch(12, 42, 30), QStringLiteral("guard"), QStringLiteral("neighbour"),
             QStringLiteral("headroom low"), QStringLiteral("undone"), true, -0.9),
    };
    const QJsonArray backoffs = {
        backoff(QStringLiteral("mains"), QStringLiteral("squeeze"), localEpoch(12, 46, 0))};
    connectGate(applet, net,
               withAutoCleanChain(true, governor(true, QStringLiteral("settling"),
                                                 QStringLiteral("holding squeeze"), holding,
                                                 QJsonValue(), events, backoffs)));
    AetherGateChainWindow* w = openChain(applet);
    CHECK(w != nullptr);
    if (!w)
        return;
    bringUp(w);

    auto* note = w->findChild<QLabel*>(QStringLiteral("gateChainAutoNote_squeeze"));
    CHECK(note != nullptr);
    if (note) {
        CHECK(!note->objectName().isEmpty());
        CHECK(!note->wordWrap());
    }

    CHECK(w->findChild<QPushButton*>(QStringLiteral("gateChainToggle_auto_clean")) != nullptr);
    strip(w)->selectStage(QStringLiteral("auto_clean"));
    settle();

    auto* state = w->findChild<QLabel*>(QStringLiteral("gateChainAutoState"));
    auto* events_ = w->findChild<QLabel*>(QStringLiteral("gateChainAutoEvents"));
    CHECK(state != nullptr);
    CHECK(events_ != nullptr);
    if (state)
        CHECK(!state->wordWrap());
    if (events_)
        CHECK(!events_->wordWrap());

    auto* scroll = w->findChild<QScrollArea*>(QStringLiteral("gateChainScroll"));
    CHECK(scroll != nullptr);
    if (scroll) {
        for (QScrollBar* sb : scroll->findChildren<QScrollBar*>()) {
            if (sb->orientation() == Qt::Horizontal)
                CHECK(!sb->isVisible());
        }
    }
}

// With CHAIN_AUTO_RENDER_PNG set (to anything), the AUTO CLEAN card holding
// two tools with a third event queued is rendered to a fixed path, so B25
// can be looked at.
void testRenderAutoWhenAsked()
{
    if (qgetenv("CHAIN_AUTO_RENDER_PNG").isEmpty())
        return;
    FakeGate net;
    AetherGateApplet applet(nullptr, &net);
    const QJsonArray holding = {
        held(QStringLiteral("squeeze"), QStringLiteral("mains"),
            QStringLiteral("mains comb found"), 100.0, true, 3.4),
        held(QStringLiteral("guard"), QStringLiteral("neighbour"),
            QStringLiteral("headroom low"), 90.0, true, 1.1),
    };
    const QJsonArray events = {
        event(localEpoch(12, 40, 1), QStringLiteral("nb"), QStringLiteral("impulse"),
             QStringLiteral("1.4/s"), QStringLiteral("released"), false),
        event(localEpoch(12, 41, 7), QStringLiteral("squeeze"), QStringLiteral("carrier"),
             QStringLiteral("strongest first"), QStringLiteral("kept"), true, 1.8),
        event(localEpoch(12, 42, 30), QStringLiteral("guard"), QStringLiteral("neighbour"),
             QStringLiteral("headroom low"), QStringLiteral("undone"), true, -0.9),
    };
    connectGate(applet, net,
               withAutoCleanChain(true, governor(true, QStringLiteral("settling"),
                                                 QStringLiteral("holding 2"), holding,
                                                 QJsonValue(), events)));
    AetherGateChainWindow* w = openChain(applet);
    CHECK(w != nullptr);
    if (!w)
        return;
    // The FRONT END card's synthetic GUARD row so the held "guard" tool has
    // a row to hold, matching the squeeze row withAutoCleanChain() already
    // carries -- otherwise only one of the two held tools would have a card
    // to put its note on, and the PNG would show one note for two holds.
    feedDevice(w, frontend(true, QStringLiteral("4"), QStringLiteral("6"), true, 1.1, 0,
                          QStringLiteral("holding")));
    bringUp(w);
    strip(w)->selectStage(QStringLiteral("auto_clean"));
    settle();
    CHECK(w->grab().save(QStringLiteral("/tmp/chain-b25-auto.png")));
}

} // namespace

int main(int argc, char** argv)
{
    TestSettingsProfile profile(QStringLiteral("aether_gate_chain_auto_test"));
    QApplication app(argc, argv);

    testNoteForHeldSqueeze();
    testNoteForHeldToolWithNoScoreYetOmitsDelta();
    testNoteForPendingNbHasNoScoreYet();
    testNoteRemovedWhenHoldingEmpties();
    testNoteIsADirectChildOfItsOwnTileOnly();
    testInspectorEventLinesNewestFirstWithResultAndDelta();
    testInspectorErrorEventAndBackoffLine();
    testEventLinePrefersWallOverT();
    testBackoffLinePrefersUntilWallAndAddsSeconds();
    testStateLineAppendsHeldBackFromRuledOut();
    testStateLineOmitsHeldBackWhenSameAsWhy();
    testStateLineAppendsObjectiveSource();
    testStateLinePrefixesNonEmptyErrorAsWarning();
    testToggleWritesExactlyAutoOffWhileOnAndAutoOnWhileOff();
    testEveryB25WidgetHasANameAndNothingScrollsAtInitialSize();
    testRenderAutoWhenAsked();

    std::printf("\n%d aether gate chain auto test(s) failed\n", g_failed);
    return g_failed == 0 ? 0 : 1;
}
