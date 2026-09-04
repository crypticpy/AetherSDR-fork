// diversity_help_audit_test -- the H1 90-char tooltip rule and the three
// DiversityHelp buttons Phase 3a's WP-D added, plus the sidebar's own
// DiversitySessionModel-fed next line and QUICK START button.
//
// Deliberately does NOT construct DiversityWindow: it is being restructured
// by other WP-D siblings in this same rework, concurrently with this test's
// own authoring. Everything here is either a widget this file's owner can
// build standalone (AetherGateChainWindow, AetherGateDiversityPanel, and the
// panels under both) or, for the two DiversityWindow::-member-function files
// that cannot be (DiversityWindowPanels.cpp, DiversityWindowEvents.cpp), a
// static regex scan of the source text itself -- the same shape
// tools/check_a11y.py already runs at review time, narrowed here to exactly
// the 90-char rule.

#include "TestSettingsProfile.h"
#include "DiversityGateFixture.h"
#include "DiversitySessionFixture.h"
#include "gui/AetherGateApplet.h"
#include "gui/AetherGateChainWindow.h"
#include "gui/AetherGateDiversityPanel.h"
#include "gui/DiversityBeaconPanel.h"
#include "gui/DiversityFilterControls.h"
#include "gui/DiversityFinderPanel.h"
#include "gui/DiversityNoiseProfilePanel.h"
#include "gui/DiversityScope.h"
#include "gui/DiversitySpatialLegend.h"
#include "gui/DiversitySpatialWaterfall.h"
#include "gui/DiversityTalkerControls.h"
#include "gui/DiversityTimeline.h"

#include <QApplication>
#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QPair>
#include <QPushButton>
#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QUrlQuery>
#include <QVector>
#include <QWidget>

#include <cstdio>

using AetherSDR::AetherGateApplet;
using AetherSDR::AetherGateChainWindow;
using AetherSDR::AetherGateDiversityPanel;
using AetherSDR::DiversityBeaconPanel;
using AetherSDR::DiversityFilterControls;
using AetherSDR::DiversityFinderPanel;
using AetherSDR::DiversityNoiseProfilePanel;
using AetherSDR::DiversityScope;
using AetherSDR::DiversitySpatialLegend;
using AetherSDR::DiversitySpatialWaterfall;
using AetherSDR::DiversityTalkerControls;
using AetherSDR::DiversityTimeline;

namespace {

int g_failed = 0;

void report(const char* name, bool ok)
{
    std::printf("%s %s\n", ok ? "[ OK ]" : "[FAIL]", name);
    if (!ok)
        ++g_failed;
}

// --- runtime tooltip walk ---------------------------------------------------

// Every QWidget under root (root included) whose own toolTip() runs past the
// H1 rule, plus every QTableWidget header/cell item under root -- items are
// QTableWidgetItem, not QWidget, so findChildren<QWidget*>() alone would miss
// DiversityFinderPanel's table entirely.
QStringList overLongTooltips(QWidget* root)
{
    QStringList out;
    QList<QWidget*> all = root->findChildren<QWidget*>(QString(), Qt::FindChildrenRecursively);
    all.prepend(root);
    for (QWidget* w : all) {
        const QString tip = w->toolTip();
        if (tip.length() > 90) {
            out << QStringLiteral("%1[%2]: %3 chars")
                       .arg(QString::fromLatin1(w->metaObject()->className()),
                            w->objectName())
                       .arg(tip.length());
        }
    }
    const QList<QTableWidget*> tables =
        root->findChildren<QTableWidget*>(QString(), Qt::FindChildrenRecursively);
    for (QTableWidget* t : tables) {
        for (int c = 0; c < t->columnCount(); ++c) {
            if (QTableWidgetItem* header = t->horizontalHeaderItem(c)) {
                if (header->toolTip().length() > 90)
                    out << QStringLiteral("%1 header col %2: %3 chars")
                               .arg(t->objectName()).arg(c).arg(header->toolTip().length());
            }
        }
        for (int r = 0; r < t->rowCount(); ++r) {
            for (int c = 0; c < t->columnCount(); ++c) {
                if (QTableWidgetItem* item = t->item(r, c)) {
                    if (item->toolTip().length() > 90)
                        out << QStringLiteral("%1 cell %2,%3: %4 chars")
                                   .arg(t->objectName()).arg(r).arg(c)
                                   .arg(item->toolTip().length());
                }
            }
        }
    }
    return out;
}

void printViolations(const QStringList& over)
{
    for (const QString& v : over)
        std::printf("       over-90 tooltip: %s\n", qUtf8Printable(v));
}

// --- static source scan, for the two files this audit cannot construct -----

// setToolTip(tr("...")) calls whose whole argument is nothing but a
// string-literal concatenation -- a dynamic argument (.arg(), a QString
// variable) is out of a source-text scan's reach and is reviewed by hand
// instead, same as tools/audit_colours.py's own VAR carve-out.
QStringList overLongLiteralTooltipsInSource(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return {QStringLiteral("could not open %1").arg(path)};
    const QString text = QString::fromUtf8(file.readAll());

    static const QRegularExpression callStart(QStringLiteral("setToolTip\\("));
    static const QRegularExpression strLit(QStringLiteral("\"((?:[^\"\\\\]|\\\\.)*)\""));
    static const QRegularExpression pureTr(
        QStringLiteral("^\\s*tr\\(\\s*(?:\"(?:[^\"\\\\]|\\\\.)*\"\\s*)+\\)\\s*$"));

    QStringList out;
    int pos = 0;
    while (true) {
        const QRegularExpressionMatch m = callStart.match(text, pos);
        if (!m.hasMatch())
            break;
        int depth = 1;
        int i = int(m.capturedEnd());
        while (i < text.size() && depth > 0) {
            if (text.at(i) == QLatin1Char('('))
                ++depth;
            else if (text.at(i) == QLatin1Char(')'))
                --depth;
            ++i;
        }
        const QString arg = text.mid(int(m.capturedEnd()), i - 1 - int(m.capturedEnd()));
        pos = i;

        if (!pureTr.match(arg).hasMatch())
            continue;

        int total = 0;
        QRegularExpressionMatchIterator it = strLit.globalMatch(arg);
        while (it.hasNext())
            total += it.next().captured(1).length();
        if (total > 90) {
            const int line = int(text.left(int(m.capturedStart())).count(QLatin1Char('\n'))) + 1;
            out << QStringLiteral("%1:%2 (%3 chars)").arg(path).arg(line).arg(total);
        }
    }
    return out;
}

// --- CHAIN-window ratchet: the H1 90-char rule PLUS the mechanics blocklist,
// read straight off setToolTip()'s own argument. The CHAIN window's generic
// control factory (AetherGateChainStage.cpp) never passes a literal to
// setToolTip() directly -- it builds a local `tip` a few lines above and
// passes that -- so this scan traces a bare-identifier argument back to its
// own initializer before reading the tr()/translate() message(s) in it,
// rather than requiring the whole call to be one literal the way
// overLongLiteralTooltipsInSource() above does for the two DiversityWindow
// files it reads. Anything the trace cannot resolve (a member, a helper
// function's return value, a gate-reported string) is out of a static
// scan's reach and is reviewed by hand, the same carve-out that function
// already documents.

// The depth-matched substring between `afterOpen` (the index right after an
// opening paren already consumed by the caller) and its own closing paren.
QString chainMatchedParens(const QString& text, int afterOpen)
{
    int depth = 1;
    int i = afterOpen;
    while (i < text.size() && depth > 0) {
        if (text.at(i) == QLatin1Char('('))
            ++depth;
        else if (text.at(i) == QLatin1Char(')'))
            --depth;
        ++i;
    }
    return text.mid(afterOpen, i - 1 - afterOpen);
}

// The last top-level (paren-depth 0, outside any string literal) comma-
// separated argument in `argText`, with its own adjacent string literals
// joined: `tr("a" "b")` is one message; `translate("Ctx", "a" "b")` is the
// same shape once its context argument is skipped. A `.arg()` placeholder
// is never part of `argText` at all -- chainMatchedParens above already
// stopped at the tr()/translate() call's own closing paren -- so a
// placeholder is counted exactly as it reads on the page, `%1` and all.
QString chainLastTopLevelArgLiteral(const QString& argText)
{
    QVector<int> splits;
    int depth = 0;
    bool inStr = false;
    for (int i = 0; i < argText.size(); ++i) {
        const QChar c = argText.at(i);
        if (inStr) {
            if (c == QLatin1Char('\\'))
                ++i;
            else if (c == QLatin1Char('"'))
                inStr = false;
            continue;
        }
        if (c == QLatin1Char('"')) {
            inStr = true;
        } else if (c == QLatin1Char('(') || c == QLatin1Char('[')) {
            ++depth;
        } else if (c == QLatin1Char(')') || c == QLatin1Char(']')) {
            --depth;
        } else if (c == QLatin1Char(',') && depth == 0) {
            splits << i;
        }
    }
    const int start = splits.isEmpty() ? 0 : splits.last() + 1;
    static const QRegularExpression strLit(QStringLiteral("\"((?:[^\"\\\\]|\\\\.)*)\""));
    QString joined;
    QRegularExpressionMatchIterator it = strLit.globalMatch(argText.mid(start));
    while (it.hasNext())
        joined += it.next().captured(1);
    return joined;
}

// Every tr()/QCoreApplication::translate() call's own message inside
// `snippet`, one entry per call -- the generic factory's
// "actionable ? tr(A) : tr(B)" shape has to check A and B separately, not
// run together as if they were one string.
QStringList chainMessagesInSnippet(const QString& snippet)
{
    static const QRegularExpression callStart(
        QStringLiteral("\\b(?:tr|QCoreApplication::translate)\\("));
    QStringList out;
    int pos = 0;
    while (true) {
        const QRegularExpressionMatch m = callStart.match(snippet, pos);
        if (!m.hasMatch())
            break;
        const QString arg = chainMatchedParens(snippet, int(m.capturedEnd()));
        pos = int(m.capturedEnd()) + arg.length() + 1;
        const QString msg = chainLastTopLevelArgLiteral(arg);
        if (!msg.isEmpty())
            out << msg;
    }
    return out;
}

// One entry per setToolTip() call in `text`: (line, message). A literal (or
// `.arg()`-placeholder) argument is read straight off the call; a bare local
// identifier is traced back to the nearest earlier `QString NAME = ...;` (or
// `const QString NAME = ...;`) declaration and read from there instead.
QVector<QPair<int, QString>> chainToolTipMessages(const QString& text)
{
    static const QRegularExpression callStart(QStringLiteral("setToolTip\\("));
    static const QRegularExpression identOnly(QStringLiteral("^\\s*(\\w+)\\s*$"));
    QVector<QPair<int, QString>> out;
    int pos = 0;
    while (true) {
        const QRegularExpressionMatch m = callStart.match(text, pos);
        if (!m.hasMatch())
            break;
        const int callStartIdx = m.capturedStart();
        const QString arg = chainMatchedParens(text, int(m.capturedEnd()));
        pos = int(m.capturedEnd()) + arg.length() + 1;
        const int line = int(text.left(callStartIdx).count(QLatin1Char('\n'))) + 1;

        const QRegularExpressionMatch ident = identOnly.match(arg);
        if (!ident.hasMatch()) {
            for (const QString& msg : chainMessagesInSnippet(arg))
                out << qMakePair(line, msg);
            continue;
        }

        const QString name = ident.captured(1);
        const QRegularExpression decl(
            QStringLiteral("(?:const\\s+)?QString\\s+") + QRegularExpression::escape(name)
            + QStringLiteral("\\s*=\\s*"));
        int declEnd = -1;
        int searchFrom = 0;
        while (true) {
            const QRegularExpressionMatch d = decl.match(text, searchFrom);
            if (!d.hasMatch() || d.capturedStart() >= callStartIdx)
                break;
            declEnd = int(d.capturedEnd());
            searchFrom = declEnd;
        }
        if (declEnd < 0)
            continue; // assigned some other way -- out of a static scan's reach

        int depth = 0;
        bool inStr = false;
        int i = declEnd;
        for (; i < text.size(); ++i) {
            const QChar c = text.at(i);
            if (inStr) {
                if (c == QLatin1Char('\\')) { ++i; continue; }
                if (c == QLatin1Char('"')) inStr = false;
                continue;
            }
            if (c == QLatin1Char('"')) inStr = true;
            else if (c == QLatin1Char('(')) ++depth;
            else if (c == QLatin1Char(')')) --depth;
            else if (c == QLatin1Char(';') && depth == 0) break;
        }
        const QString init = text.mid(declEnd, i - declEnd);
        for (const QString& msg : chainMessagesInSnippet(init))
            out << qMakePair(line, msg);
    }
    return out;
}

// MUTATION GUARD: a `setToolTip()` literal in the CHAIN window's own files
// creeping past 90 chars, or the write-confirmation mechanics ("stays where
// you leave it", "does not move until the receiver says", ...) creeping back
// onto a hover instead of staying in accessibleDescription where design 3d
// put it. Every `src/gui/AetherGateChain*.cpp` plus AetherGateChainStagePrivate.h,
// found by glob rather than a fixed list, so a new file in this window is
// covered without this test needing an update.
void everyChainWindowSetToolTipObeysTheHoverRule()
{
    static const QStringList kMechanicsBlocklist = {
        QStringLiteral("stays where you"),
        QStringLiteral("does not move until"),
        QStringLiteral("the receiver says"),
        QStringLiteral("until the gate"),
    };

    const QDir dir(QStringLiteral(AETHER_SOURCE_DIR "/src/gui"));
    QStringList names = dir.entryList(QStringList{QStringLiteral("AetherGateChain*.cpp")},
                                      QDir::Files, QDir::Name);
    names << QStringLiteral("AetherGateChainStagePrivate.h");

    QStringList violations;
    for (const QString& name : names) {
        const QString path = dir.filePath(name);
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            violations << QStringLiteral("could not open %1").arg(path);
            continue;
        }
        const QString text = QString::fromUtf8(file.readAll());
        const QVector<QPair<int, QString>> messages = chainToolTipMessages(text);
        for (const auto& entry : messages) {
            const int line = entry.first;
            const QString& msg = entry.second;
            if (msg.length() > 90) {
                violations << QStringLiteral("%1:%2: %3 chars: \"%4\"")
                                  .arg(name).arg(line).arg(msg.length()).arg(msg);
                continue;
            }
            for (const QString& banned : kMechanicsBlocklist) {
                if (msg.contains(banned, Qt::CaseInsensitive)) {
                    violations << QStringLiteral("%1:%2: mechanics (\"%3\") in tooltip: "
                                                 "\"%4\"")
                                      .arg(name).arg(line).arg(banned, msg);
                    break;
                }
            }
        }
    }
    printViolations(violations);
    report("every CHAIN-window setToolTip() literal is <=90 chars and free of "
           "write-confirmation mechanics",
           violations.isEmpty());
}

// A minimal /diversity/finder answer: one voice candidate, so
// everyPanelTooltipIsAtMostNinetyChars also exercises the kind-column item
// tooltip DiversityFinderPanel sets per row, not just its static headers.
QJsonObject makeFinderCandidate()
{
    QJsonObject c;
    c["hz"] = 14200000.0;
    c["kind"] = QStringLiteral("voice");
    c["kind_conf"] = 0.82;
    c["width_hz"] = 2500.0;
    c["mode"] = QStringLiteral("USB");
    return c;
}

QJsonObject makeFinder()
{
    QJsonObject o;
    o["available"] = true;
    QJsonArray candidates;
    candidates.append(makeFinderCandidate());
    o["candidates"] = candidates;
    QJsonArray span;
    span.append(14000000.0);
    span.append(14350000.0);
    o["span_hz"] = span;
    return o;
}

// MUTATION GUARD: any setToolTip() literal (or the dynamic-but-literal-heavy
// forms above) creeping back past 90 chars in the CHAIN window's own widget
// tree -- AetherGateChainWindow, and the Strip/Visual/Presets it builds.
void everyVisibleTooltipInTheChainWindowIsAtMostNinetyChars()
{
    AetherGateChainWindow win;
    const QStringList over = overLongTooltips(&win);
    printViolations(over);
    report("every CHAIN window tooltip is <=90 chars", over.isEmpty());
}

// MUTATION GUARD: same rule, over AetherGateDiversityPanel's own widget tree.
void everyVisibleTooltipInTheSidebarIsAtMostNinetyChars()
{
    AetherGateDiversityPanel panel;
    const QStringList over = overLongTooltips(&panel);
    printViolations(over);
    report("every sidebar tooltip is <=90 chars", over.isEmpty());
}

// MUTATION GUARD: same rule over every other panel this WP owns -- the
// standalone-constructible ones by runtime walk, DiversityWindowPanels.cpp
// and DiversityWindowEvents.cpp (DiversityWindow::-member functions, not
// standalone-constructible while DiversityWindow is mid-restructure) by
// static source scan.
void everyPanelTooltipIsAtMostNinetyChars()
{
    DiversityFinderPanel finder;
    finder.applyFinder(makeFinder());
    QStringList over = overLongTooltips(&finder);

    DiversityScope scope;
    over += overLongTooltips(&scope);

    DiversityTimeline timeline;
    over += overLongTooltips(&timeline);

    DiversitySpatialLegend legend;
    over += overLongTooltips(&legend);

    DiversitySpatialWaterfall waterfall;
    over += overLongTooltips(&waterfall);

    DiversityFilterControls filterControls;
    over += overLongTooltips(&filterControls);

    DiversityTalkerControls talkerControls;
    over += overLongTooltips(&talkerControls);

    DiversityBeaconPanel beaconPanel;
    over += overLongTooltips(&beaconPanel);

    DiversityNoiseProfilePanel noisePanel;
    over += overLongTooltips(&noisePanel);

    printViolations(over);
    report("every standalone panel's tooltip is <=90 chars", over.isEmpty());

    QStringList sourceOver = overLongLiteralTooltipsInSource(
        QStringLiteral(AETHER_SOURCE_DIR "/src/gui/DiversityWindowPanels.cpp"));
    sourceOver += overLongLiteralTooltipsInSource(
        QStringLiteral(AETHER_SOURCE_DIR "/src/gui/DiversityWindowEvents.cpp"));
    sourceOver += overLongLiteralTooltipsInSource(
        QStringLiteral(AETHER_SOURCE_DIR "/src/gui/DiversityWindow.cpp"));
    sourceOver += overLongLiteralTooltipsInSource(
        QStringLiteral(AETHER_SOURCE_DIR "/src/gui/DiversityWindowChain.cpp"));
    sourceOver += overLongLiteralTooltipsInSource(
        QStringLiteral(AETHER_SOURCE_DIR "/src/gui/DiversityWindowBand.cpp"));
    printViolations(sourceOver);
    report("DiversityWindow/Chain/Band/Panels/Events literal tooltips are <=90 chars",
           sourceOver.isEmpty());
}

// MUTATION GUARD: the applet's own static tooltips (span/bins/OPEN CHAIN/
// status) or a dynamically-built device control (antenna, or a setting the
// gate reports, e.g. LNA state) creeping past 90 chars -- the only place
// those per-device widgets exist to be walked at all.
void everyGateAppletTooltipIsAtMostNinetyChars()
{
    using namespace DiversityGateFixture;

    FakeGate net;
    net.routes[QStringLiteral("/status")] = {QNetworkReply::NoError, kStatus};
    net.routes[QStringLiteral("/device")] = {QNetworkReply::NoError, kDevice};
    net.routes[QStringLiteral("/diversity")] = {QNetworkReply::NoError,
                                                QByteArrayLiteral("{\"available\": false}")};

    AetherGateApplet applet(nullptr, &net);
    applet.setRadioAddress(QStringLiteral("10.0.0.5"));
    settle();
    settle();

    // Scoped to the resolution box, the device box (antenna + every setting
    // the gate reported), and the OPEN CHAIN/status widgets built or given a
    // tooltip in this WP: AetherGateDeviceStrip (the DIVERSITY/A/B toggles at
    // the top of the applet) is a different file this WP does not own, and
    // walking the whole applet would fail this guard on a pre-existing
    // violation this test cannot fix.
    QStringList over;
    if (QWidget* resBox = applet.findChild<QWidget*>(QStringLiteral("gateResolutionBox")))
        over += overLongTooltips(resBox);
    if (QWidget* deviceBox = applet.findChild<QWidget*>(QStringLiteral("gateDeviceBox")))
        over += overLongTooltips(deviceBox);
    if (QWidget* openChain =
            applet.findChild<QWidget*>(QStringLiteral("gateOpenChainWindowButton")))
        over += overLongTooltips(openChain);
    if (QWidget* status = applet.findChild<QWidget*>(QStringLiteral("gateStatusLabel")))
        over += overLongTooltips(status);

    printViolations(over);
    report("the applet's own resolution/device/OPEN CHAIN/status tooltips are "
           "<=90 chars",
           over.isEmpty());
}

// MUTATION GUARD: dropping the long text off accessibleDescription entirely,
// or mirroring the short tooltip onto it instead of keeping the original
// long sentence -- both fail the "contains the long-only phrase" and the
// "longer than the tooltip" checks below.
void longTextSurvivesAsAccessibleDescription()
{
    AetherGateDiversityPanel panel;
    auto* btn = panel.findChild<QPushButton*>(QStringLiteral("gateDiversityOpenWindowButton"));
    report("the door button exists", btn != nullptr);
    if (!btn)
        return;

    report("its tooltip is <=90 chars", btn->toolTip().length() <= 90);
    report("its accessibleDescription still names the meters and noise map",
           btn->accessibleDescription().contains(QStringLiteral("per-antenna meters"))
               && btn->accessibleDescription().contains(QStringLiteral("noise map")));
    report("the description is the long one, not a copy of the short tooltip",
           btn->accessibleDescription().length() > btn->toolTip().length());
}

// MUTATION GUARD: a help button missing from any one of the three
// load-bearing boxes -- SLICE TALKERS (source scan; see the file header
// comment), BAND FINDER and the CHAIN window's VISUAL tab (runtime).
void helpButtonsSitBesideTalkersFinderAndVisual()
{
    QFile eventsSource(
        QStringLiteral(AETHER_SOURCE_DIR "/src/gui/DiversityWindowEvents.cpp"));
    report("DiversityWindowEvents.cpp can be inspected",
           eventsSource.open(QIODevice::ReadOnly | QIODevice::Text));
    const QByteArray eventsText = eventsSource.readAll();
    report("TALKERS gets a Slice help button",
           eventsText.contains("addHelpBesideCaption(frame, DiversityHelp::Topic::Slice)"));

    DiversityFinderPanel finder;
    report("FINDER gets a Band help button",
           finder.findChild<QPushButton*>(QStringLiteral("diversityHelpButtonBand")) != nullptr);

    AetherGateChainWindow win;
    report("VISUAL gets a Chain help button",
           win.findChild<QPushButton*>(QStringLiteral("diversityHelpButtonChain")) != nullptr);
}

// MUTATION GUARD: the sidebar computing its own answer instead of reading
// m_sessionModel -- feeding two states that the model resolves to two
// DIFFERENT next steps must produce two different lines; a hardcoded or
// stuck line fails this no matter which state it froze on.
void sidebarNextLineFollowsTheModel()
{
    using namespace DiversitySessionFixture;

    AetherGateDiversityPanel panel;
    auto* line = panel.findChild<QLabel*>(QStringLiteral("aetherGateDiversityNextLine"));
    report("the next line exists", line != nullptr);
    if (!line)
        return;

    // State 1: not aligned -- RECEIVER is the first undone step.
    panel.applyDiversity(makeDiversity(/*available=*/true, QStringLiteral("track"),
                                       QStringLiteral("combined"), /*aligned=*/false),
                         true);
    const QString first = line->text();
    report("state 1 text is <=26 chars", first.length() <= 26);
    report("state 1 reads \"next: realign\"", first == QStringLiteral("next: realign"));

    // State 2: aligned, no noise findings, but the tuned band has nothing
    // measured yet -- BAND is the first undone step.
    panel.setActiveSliceHz(kHz20m);
    panel.applyDiversity(makeDiversity(/*available=*/true, QStringLiteral("track"),
                                       QStringLiteral("combined"), /*aligned=*/true),
                         true);
    panel.applyBeacons(makeBeacons(true));   // no propagation rows at all
    const QString second = line->text();
    report("state 2 text is <=26 chars", second.length() <= 26);
    report("state 2 reads \"next: beacons\"", second == QStringLiteral("next: beacons"));

    report("the line actually changed between the two states", first != second);
}

// MUTATION GUARD: QUICK START sending the wrong count, the wrong order, or
// not sending at all.
void sidebarQuickStartSendsThreeQueriesInOrder()
{
    AetherGateDiversityPanel panel;
    auto* btn = panel.findChild<QPushButton*>(
        QStringLiteral("aetherGateDiversityQuickStartButton"));
    report("the QUICK START button exists", btn != nullptr);
    if (!btn)
        return;

    QVector<QUrlQuery> sent;
    QObject::connect(&panel, &AetherGateDiversityPanel::requestSet, &panel,
                     [&sent](QUrlQuery q) { sent.push_back(q); });
    btn->click();

    report("QUICK START sends exactly three writes", sent.size() == 3);
    if (sent.size() != 3)
        return;
    report("write 1 is mode=track",
           sent.at(0).queryItemValue(QStringLiteral("mode")) == QStringLiteral("track"));
    report("write 2 is source=combined",
           sent.at(1).queryItemValue(QStringLiteral("source"))
               == QStringLiteral("combined"));
    report("write 3 is auto=on",
           sent.at(2).queryItemValue(QStringLiteral("auto")) == QStringLiteral("on"));
}

// MUTATION GUARD: the next line/QUICK START staying visible with no gate, or
// never appearing once the gate does.
void sidebarNextLineHiddenWithoutDiversity()
{
    using namespace DiversitySessionFixture;

    AetherGateDiversityPanel panel;
    auto* line = panel.findChild<QLabel*>(QStringLiteral("aetherGateDiversityNextLine"));
    auto* btn = panel.findChild<QPushButton*>(
        QStringLiteral("aetherGateDiversityQuickStartButton"));
    report("the next line exists", line != nullptr);
    report("the QUICK START button exists", btn != nullptr);
    if (!line || !btn)
        return;

    report("both start hidden with no gate", line->isHidden() && btn->isHidden());

    panel.applyDiversity(makeDiversity(true), true);
    report("both show once diversity is available",
           !line->isHidden() && !btn->isHidden());

    panel.applyDiversity(QJsonObject(), false);
    report("both hide again once the gate is gone",
           line->isHidden() && btn->isHidden());
}

} // namespace

int main(int argc, char** argv)
{
    TestSettingsProfile settingsProfile(QStringLiteral("aether-diversity-help-audit-test"));
    QApplication app(argc, argv);

    everyVisibleTooltipInTheChainWindowIsAtMostNinetyChars();
    everyChainWindowSetToolTipObeysTheHoverRule();
    everyVisibleTooltipInTheSidebarIsAtMostNinetyChars();
    everyPanelTooltipIsAtMostNinetyChars();
    everyGateAppletTooltipIsAtMostNinetyChars();
    longTextSurvivesAsAccessibleDescription();
    helpButtonsSitBesideTalkersFinderAndVisual();
    sidebarNextLineFollowsTheModel();
    sidebarQuickStartSendsThreeQueriesInOrder();
    sidebarNextLineHiddenWithoutDiversity();

    std::printf("\n%d check(s) failed\n", g_failed);
    return g_failed == 0 ? 0 : 1;
}
