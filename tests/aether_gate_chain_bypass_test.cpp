// HEAR RAW -- the CHAIN window's momentary bypass button. The operator's own
// words: "a little bypass button where we can temporarily hear the signal
// without going through the chain" so they can A/B how much the chain is
// doing. It rides the same /filter `bypass` boolean the SLICE FILTER row's
// own IN/BYPASS toggle already writes (AetherGateChainRows.cpp's
// chainFromFilter(), fed by the gate's chainstatus.py _slice_rows()) -- see
// AetherGateChainBypass.h for the button's full contract.
//
// Six things have to be true of it, and each test below earns exactly one:
// a press sends exactly bypass=on, a release sends exactly bypass=off, a
// window hidden mid-hold releases on its own, a gate that already reports
// bypass:true (from the row's own toggle, not from this button) disables
// the button rather than let a second press collide with the first, an
// older gate that has never mentioned "bypass" never shows the button at
// all, and the face reads differently while held so the operator can tell
// the hold is actually doing something.
//
// Nothing here opens a port -- the same socket-free FakeGate every other
// CHAIN window test uses.
#include "AetherGateChainFixture.h"

#include <QApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPushButton>
#include <QTest>

using namespace AetherGateChainFixture;

namespace {

const char* kHearRawName = "aetherGateChainHearRawButton";

// kChainFullFilter plus a top-level "bypass" key -- what a gate that has
// this feature actually sends. aether_gate/core/filter.py's status() always
// includes it, right alongside "notches" and "notches_on"; the shared
// fixture predates the feature and carries no such key at all, which is
// exactly the shape testOlderGateHidesButton() wants from it unmodified.
QByteArray filterWithBypass(bool bypassed)
{
    QJsonObject root = QJsonDocument::fromJson(kChainFullFilter).object();
    root.insert(QStringLiteral("bypass"), bypassed);
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

// So a write this test triggers gets a real reply instead of the FakeGate's
// default 404 -- the same thing every other write-sending chain test does
// for the route it is about to exercise (see aether_gate_chain_frontend_test
// .cpp's testTogglingGuardSendsGuardOnThenGuardOff()).
void okFilterSet(FakeGate& net)
{
    net.routes[QStringLiteral("/filter/set")] = {QNetworkReply::NoError,
                                                  QByteArrayLiteral("{}")};
}

QPushButton* hearRaw(AetherGateChainWindow* w)
{
    return w->findChild<QPushButton*>(QString::fromLatin1(kHearRawName));
}

// --------------------------------------------------------------------------

// MUTATION: a press that puts anything on the wire other than exactly
// "bypass=on" -- a stray parameter, the wrong value, or the SLICE FILTER
// row's own toggle query reused by mistake -- would ask the gate for the
// wrong thing the instant the operator presses down.
void testPressSendsExactlyBypassOn()
{
    FakeGate net;
    okFilterSet(net);
    AetherGateApplet applet(nullptr, &net);
    connectGate(applet, net, filterWithBypass(false));
    AetherGateChainWindow* w = openChain(applet);
    CHECK(w != nullptr);
    if (!w)
        return;

    QPushButton* button = hearRaw(w);
    CHECK(button != nullptr);
    if (!button)
        return;
    CHECK(button->isVisible());
    CHECK(button->isEnabled());

    QTest::mousePress(button, Qt::LeftButton);
    settle();
    CHECK(sentQueries(net, QStringLiteral("/filter/set"))
          == QStringList{QStringLiteral("bypass=on")});

    QTest::mouseRelease(button, Qt::LeftButton);
    settle();
}

// MUTATION: a release that sends anything other than exactly "bypass=off",
// or that fails to send at all, would leave the receiver silently bypassed
// the moment the operator lets go of the button.
void testReleaseSendsExactlyBypassOff()
{
    FakeGate net;
    okFilterSet(net);
    AetherGateApplet applet(nullptr, &net);
    connectGate(applet, net, filterWithBypass(false));
    AetherGateChainWindow* w = openChain(applet);
    CHECK(w != nullptr);
    if (!w)
        return;

    QPushButton* button = hearRaw(w);
    CHECK(button != nullptr);
    if (!button)
        return;

    QTest::mousePress(button, Qt::LeftButton);
    settle();
    QTest::mouseRelease(button, Qt::LeftButton);
    settle();

    const QStringList sent = sentQueries(net, QStringLiteral("/filter/set"));
    CHECK(sent.size() == 2);
    CHECK(sent.value(0) == QStringLiteral("bypass=on"));
    CHECK(sent.value(1) == QStringLiteral("bypass=off"));
}

// MUTATION: a window hidden while the button is still held -- closed, or
// swapped out from under a held mouse button -- that does not release on
// its own would leave the gate bypassed with nobody left looking at the
// CHAIN window to notice or undo it.
void testHideWhileHeldReleases()
{
    FakeGate net;
    okFilterSet(net);
    AetherGateApplet applet(nullptr, &net);
    connectGate(applet, net, filterWithBypass(false));
    AetherGateChainWindow* w = openChain(applet);
    CHECK(w != nullptr);
    if (!w)
        return;

    QPushButton* button = hearRaw(w);
    CHECK(button != nullptr);
    if (!button)
        return;

    QTest::mousePress(button, Qt::LeftButton);
    settle();
    CHECK(sentQueries(net, QStringLiteral("/filter/set"))
          == QStringList{QStringLiteral("bypass=on")});

    w->hide();
    settle();
    CHECK(sentQueries(net, QStringLiteral("/filter/set"))
          == (QStringList{QStringLiteral("bypass=on"), QStringLiteral("bypass=off")}));

    // The physical mouse button may still be down when the window vanished
    // out from under it; the eventual real release must not send a SECOND
    // bypass=off -- releaseIfHeld() already cleared the hold.
    QTest::mouseRelease(button, Qt::LeftButton);
    settle();
    CHECK(net.count(QStringLiteral("/filter/set")) == 2);
}

// MUTATION: a button that stays enabled, or that still reads "HEAR RAW",
// once the gate itself reports bypass:true -- from the SLICE FILTER row's
// own IN/BYPASS toggle, not from a hold of this button -- would invite a
// second, colliding bypass request onto a chain that is already out of
// circuit for a reason this button did not cause.
void testGateReportedBypassDisablesButtonWithInWording()
{
    FakeGate net;
    AetherGateApplet applet(nullptr, &net);
    connectGate(applet, net, filterWithBypass(false));
    AetherGateChainWindow* w = openChain(applet);
    CHECK(w != nullptr);
    if (!w)
        return;

    QPushButton* button = hearRaw(w);
    CHECK(button != nullptr);
    if (!button)
        return;
    CHECK(button->isEnabled());
    CHECK(button->text() == QStringLiteral("HEAR RAW"));

    net.routes[QStringLiteral("/filter")] = {QNetworkReply::NoError, filterWithBypass(true)};
    filterTick(applet);
    CHECK(!button->isEnabled());
    CHECK(button->text() == QStringLiteral("CHAIN IS BYPASSED"));
    CHECK(button->toolTip().contains(QStringLiteral("SLICE FILTER")));

    // Back to bypass:false re-enables it with its ordinary face.
    net.routes[QStringLiteral("/filter")] = {QNetworkReply::NoError, filterWithBypass(false)};
    filterTick(applet);
    CHECK(button->isEnabled());
    CHECK(button->text() == QStringLiteral("HEAR RAW"));
}

// MUTATION: a gate that has never once sent a "bypass" key at all -- every
// gate older than this feature -- must never show the button, not merely
// disable it: a visible-but-disabled control is still a new thing on an
// old gate's window, and "hidden" has to mean hidden.
void testOlderGateHidesButton()
{
    FakeGate net;
    AetherGateApplet applet(nullptr, &net);
    // kChainFullFilter carries no "bypass" key -- the fixture predates this
    // feature, which is exactly the older-gate shape this test wants.
    connectGate(applet, net, kChainFullFilter);
    AetherGateChainWindow* w = openChain(applet);
    CHECK(w != nullptr);
    if (!w)
        return;

    QPushButton* button = hearRaw(w);
    CHECK(button != nullptr);
    if (!button)
        return;
    CHECK(!button->isVisible());

    // A LATER body that does carry the key shows it -- the hiding is a
    // per-body read, not a one-time verdict passed on the gate.
    net.routes[QStringLiteral("/filter")] = {QNetworkReply::NoError, filterWithBypass(false)};
    filterTick(applet);
    CHECK(button->isVisible());
}

// MUTATION: a held button that keeps saying "HEAR RAW" -- or reads anything
// other than the release prompt -- would look like the press did nothing,
// and an operator staring at an unchanged label has no way to tell the
// chain really is out of circuit right now.
void testFaceTextWhileHeld()
{
    FakeGate net;
    okFilterSet(net);
    AetherGateApplet applet(nullptr, &net);
    connectGate(applet, net, filterWithBypass(false));
    AetherGateChainWindow* w = openChain(applet);
    CHECK(w != nullptr);
    if (!w)
        return;

    QPushButton* button = hearRaw(w);
    CHECK(button != nullptr);
    if (!button)
        return;
    CHECK(button->text() == QStringLiteral("HEAR RAW"));

    QTest::mousePress(button, Qt::LeftButton);
    settle();
    CHECK(button->text() == QStringLiteral("RAW · release to hear the chain"));
    CHECK(button->isEnabled());

    QTest::mouseRelease(button, Qt::LeftButton);
    settle();
    CHECK(button->text() == QStringLiteral("HEAR RAW"));
}

} // namespace

int main(int argc, char** argv)
{
    TestSettingsProfile profile(QStringLiteral("aether_gate_chain_bypass_test"));
    QApplication app(argc, argv);

    testPressSendsExactlyBypassOn();
    testReleaseSendsExactlyBypassOff();
    testHideWhileHeldReleases();
    testGateReportedBypassDisablesButtonWithInWording();
    testOlderGateHidesButton();
    testFaceTextWhileHeld();

    std::printf("\n%d aether gate chain bypass test(s) failed\n", g_failed);
    return g_failed == 0 ? 0 : 1;
}
