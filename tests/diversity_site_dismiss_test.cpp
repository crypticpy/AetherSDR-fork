// DISMISS on the SITE page's NOISE PROFILE table, and the SITE page's two
// HELP buttons -- driven through a real AetherGateApplet and socket-free.
//
// Same harness as tests/diversity_site_actions_test.cpp, and for the same
// reason it is its own binary: that file (and diversity_site_test.cpp) are
// both at the 800-line budget AGENTS.md asks for. DISMISS's own state lives
// in DiversityNoiseProfilePanel's m_dismissed, persisted in AppSettings key
// DiversityDismissedNoiseKinds -- see R2.1 of the Phase 3a workflow plan:
// "every noise finding gets DISMISS... persisted per finding kind in
// AppSettings until the finding's dB changes by more than 3 dB".

#include "DiversityGateFixture.h"
#include "TestSettingsProfile.h"
#include "core/AppSettings.h"
#include "gui/AetherGateApplet.h"
#include "gui/AetherGateDiversityPanel.h"
#include "gui/DiversityNoiseProfilePanel.h"
#include "gui/DiversityWindow.h"

#include <QApplication>
#include <QLabel>
#include <QNetworkReply>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSet>
#include <QString>
#include <QTableWidget>
#include <QTest>
#include <QToolButton>
#include <QWidget>

#include <cstdio>

using AetherSDR::AetherGateApplet;
using AetherSDR::AppSettings;
using AetherSDR::DiversityNoiseProfilePanel;
using AetherSDR::DiversityWindow;

using namespace DiversityGateFixture;

namespace {

int g_failed = 0;

#define CHECK(cond)                                                                  \
    do {                                                                             \
        if (!(cond)) {                                                               \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);              \
            ++g_failed;                                                              \
        }                                                                            \
    } while (0)

void closedToStart()
{
    AppSettings::instance().setValue(QStringLiteral("DiversityWindowVisible"),
                                     QStringLiteral("False"));
    // A dismissal from one case must not leak into the next -- every case
    // starts from a known-empty dismissed set, the same way every case
    // starts from a known-closed window.
    AppSettings::instance().setValue(QStringLiteral("DiversityDismissedNoiseKinds"),
                                     QString());
}

void connectGate(AetherGateApplet& a, FakeGate& net,
                 const QByteArray& status = kDiversityStatusWithKinds,
                 const QByteArray& beacons = kDiversityBeaconsWithPattern)
{
    net.routes[QStringLiteral("/status")] = {QNetworkReply::NoError, kStatus};
    net.routes[QStringLiteral("/device")] = {QNetworkReply::NoError, kDevice};
    net.routes[QStringLiteral("/diversity")] = {QNetworkReply::NoError, status};
    net.routes[QStringLiteral("/diversity/set")] = {QNetworkReply::NoError, status};
    net.routes[QStringLiteral("/diversity/beacons")] = {QNetworkReply::NoError,
                                                        beacons};
    a.setRadioAddress(QStringLiteral("10.0.0.5"));
    settle();
    settle();
    settle();
}

QPushButton* openButton(AetherGateApplet& a)
{
    return a.findChild<QPushButton*>(QStringLiteral("gateDiversityOpenWindowButton"));
}

template <typename T>
T* child(DiversityWindow* w, const char* name)
{
    return w->findChild<T*>(QString::fromLatin1(name));
}

DiversityWindow* openOnSite(AetherGateApplet& a)
{
    openButton(a)->click();
    settle();
    DiversityWindow* w = a.diversityPanel()->window();
    if (!w)
        return nullptr;
    auto* page = w->findChild<QToolButton*>(QStringLiteral("diversityWindowPageSite"));
    if (page)
        page->click();
    settle();
    tick(a);
    return w;
}

QPushButton* kindAction(DiversityWindow* w, int row)
{
    return w->findChild<QPushButton*>(
        QStringLiteral("diversityWindowNoiseKindAction%1").arg(row));
}

QPushButton* dismissButton(DiversityWindow* w, const QString& kindUpper)
{
    return w->findChild<QPushButton*>(QStringLiteral("diversityWindowNoiseDismiss%1")
                                          .arg(kindUpper));
}

QPushButton* undoButton(DiversityWindow* w, const QString& kindUpper)
{
    return w->findChild<QPushButton*>(
        QStringLiteral("diversityWindowNoiseUndismiss%1").arg(kindUpper));
}

QLabel* dismissedLabel(DiversityWindow* w, int row)
{
    return w->findChild<QLabel*>(
        QStringLiteral("diversityWindowNoiseDismissedLabel%1").arg(row));
}

DiversityNoiseProfilePanel* noisePanel(DiversityWindow* w)
{
    return child<DiversityNoiseProfilePanel>(w, "diversityWindowNoiseProfilePanel");
}

// The impulse row's own "db" is 14.8 in kDiversityStatusWithKinds and unique
// in that payload -- see DiversityGateFixture.h. Replacing just that number
// is what lets a test move ONE finding's size without touching the five
// other rows the same payload carries.
QByteArray withImpulseDb(double db)
{
    QByteArray json = kDiversityStatusWithKinds;
    json.replace("\"db\": 14.8, \"window_s\": 4.0",
                QByteArray("\"db\": ") + QByteArray::number(db, 'f', 1)
                    + QByteArray(", \"window_s\": 4.0"));
    return json;
}

// The same payload with the impulse row removed from "kinds" altogether --
// the fixture's mains row is immediately followed by the impulse row, which
// is immediately followed by the first periodic row, so cutting from the
// start of one "kind" object to the start of the next removes exactly one
// array element (object plus its trailing comma) and nothing else.
QByteArray withImpulseRemoved()
{
    QByteArray json = kDiversityStatusWithKinds;
    const int start = json.indexOf("{\"kind\": \"impulse\"");
    const int end = json.indexOf("{\"kind\": \"periodic\"", start);
    if (start < 0 || end < 0)
        return json;
    json.remove(start, end - start);
    return json;
}

// (a) DISMISS on an eligible row hides the action and files it in
// AppSettings -- the whole point of the feature: a finding that has been
// dealt with stops asking about it every second.
// MUTATION: a dismiss that only updates the widget and never calls
// persistDismissed() would still pass a UI-only check; the AppSettings read
// below is what catches it.
void testDismissHidesTheActionAndPersists()
{
    closedToStart();
    FakeGate net;
    AetherGateApplet a(nullptr, &net);
    connectGate(a, net);
    DiversityWindow* w = openOnSite(a);
    CHECK(w != nullptr);
    if (!w)
        return;

    CHECK(kindAction(w, 1) != nullptr);
    QPushButton* dismiss = dismissButton(w, QStringLiteral("IMPULSE"));
    CHECK(dismiss != nullptr);
    if (!dismiss)
        return;
    dismiss->click();
    // QTableWidget::setCellWidget() deleteLater()s the widget it replaces
    // rather than deleting it on the spot -- give the event loop a turn so
    // the old action button is actually gone before checking for it.
    settle();

    CHECK(kindAction(w, 1) == nullptr);
    CHECK(dismissedLabel(w, 1) != nullptr);
    CHECK(dismissedLabel(w, 1)->text() == QStringLiteral("dismissed"));
    CHECK(undoButton(w, QStringLiteral("IMPULSE")) != nullptr);

    const QString stored = AppSettings::instance()
                                .value(QStringLiteral("DiversityDismissedNoiseKinds"))
                                .toString();
    CHECK(stored.contains(QStringLiteral("impulse|14.80")));

    DiversityNoiseProfilePanel* panel = noisePanel(w);
    CHECK(panel != nullptr);
    if (panel)
        CHECK(panel->dismissedKinds() == QSet<QString>{QStringLiteral("impulse")});
    closedToStart();
}

// (b) A dismissal survives a small drift and drops the moment the finding
// moves more than 3 dB from where it was dismissed.
// MUTATION: comparing with >= 0 dB (any change at all) would fail the first
// half of this case; never expiring would fail the second half.
void testDismissalExpiresWhenTheFindingMovesMoreThan3dB()
{
    closedToStart();
    FakeGate net;
    AetherGateApplet a(nullptr, &net);
    connectGate(a, net);
    DiversityWindow* w = openOnSite(a);
    CHECK(w != nullptr);
    if (!w)
        return;
    DiversityNoiseProfilePanel* panel = noisePanel(w);
    CHECK(panel != nullptr);
    if (!panel)
        return;

    dismissButton(w, QStringLiteral("IMPULSE"))->click();
    CHECK(panel->dismissedKinds().contains(QStringLiteral("impulse")));

    // 1.5 dB of drift: still dismissed.
    net.routes[QStringLiteral("/diversity")] = {QNetworkReply::NoError,
                                                withImpulseDb(16.3)};
    tick(a);
    CHECK(panel->dismissedKinds().contains(QStringLiteral("impulse")));
    CHECK(dismissedLabel(w, 1) != nullptr);

    // 3.7 dB of drift from the original 14.8: expired, the action is back.
    net.routes[QStringLiteral("/diversity")] = {QNetworkReply::NoError,
                                                withImpulseDb(18.5)};
    tick(a);
    CHECK(!panel->dismissedKinds().contains(QStringLiteral("impulse")));
    CHECK(kindAction(w, 1) != nullptr);
    const QString stored = AppSettings::instance()
                                .value(QStringLiteral("DiversityDismissedNoiseKinds"))
                                .toString();
    CHECK(!stored.contains(QStringLiteral("impulse|")));
    closedToStart();
}

// (c) A dismissal that outlives the finding it was about is not "handled"
// any more -- it is about nothing. Vanishing from the payload expires it
// exactly the way a 3 dB move does.
void testDismissalExpiresWhenTheKindDisappears()
{
    closedToStart();
    FakeGate net;
    AetherGateApplet a(nullptr, &net);
    connectGate(a, net);
    DiversityWindow* w = openOnSite(a);
    CHECK(w != nullptr);
    if (!w)
        return;
    DiversityNoiseProfilePanel* panel = noisePanel(w);
    CHECK(panel != nullptr);
    if (!panel)
        return;

    dismissButton(w, QStringLiteral("IMPULSE"))->click();
    CHECK(panel->dismissedKinds().contains(QStringLiteral("impulse")));

    net.routes[QStringLiteral("/diversity")] = {QNetworkReply::NoError,
                                                withImpulseRemoved()};
    tick(a);
    CHECK(!panel->dismissedKinds().contains(QStringLiteral("impulse")));

    // The row comes back on the next poll that has it, undismissed rather
    // than remembered as still-dismissed -- the disappearance already threw
    // the dismissal away.
    net.routes[QStringLiteral("/diversity")] = {QNetworkReply::NoError,
                                                kDiversityStatusWithKinds};
    tick(a);
    CHECK(kindAction(w, 1) != nullptr);
    closedToStart();
}

// (d) UNDO brings the action button back and drops the AppSettings entry --
// the operator's own reversal, not another kind of expiry.
void testUndoRestoresTheActionButton()
{
    closedToStart();
    FakeGate net;
    AetherGateApplet a(nullptr, &net);
    connectGate(a, net);
    DiversityWindow* w = openOnSite(a);
    CHECK(w != nullptr);
    if (!w)
        return;

    dismissButton(w, QStringLiteral("IMPULSE"))->click();
    settle(); // see the comment on the same pattern above
    CHECK(kindAction(w, 1) == nullptr);

    QPushButton* undo = undoButton(w, QStringLiteral("IMPULSE"));
    CHECK(undo != nullptr);
    if (!undo)
        return;
    undo->click();
    settle();

    CHECK(kindAction(w, 1) != nullptr);
    CHECK(kindAction(w, 1)->text() == QStringLiteral("BLANK"));
    CHECK(dismissedLabel(w, 1) == nullptr);

    DiversityNoiseProfilePanel* panel = noisePanel(w);
    CHECK(panel != nullptr && !panel->dismissedKinds().contains(QStringLiteral("impulse")));
    const QString stored = AppSettings::instance()
                                .value(QStringLiteral("DiversityDismissedNoiseKinds"))
                                .toString();
    CHECK(!stored.contains(QStringLiteral("impulse|")));
    closedToStart();
}

// (e) dismissedKindsChanged() always carries the CURRENT set, not just the
// one row that changed -- DiversitySessionModel::setDismissedKinds() (the
// caller this signal exists for) replaces its whole set on every emission.
void testDismissedKindsSignalCarriesTheCurrentSet()
{
    closedToStart();
    FakeGate net;
    AetherGateApplet a(nullptr, &net);
    connectGate(a, net);
    DiversityWindow* w = openOnSite(a);
    CHECK(w != nullptr);
    if (!w)
        return;
    DiversityNoiseProfilePanel* panel = noisePanel(w);
    CHECK(panel != nullptr);
    if (!panel)
        return;

    QSet<QString> lastSeen;
    int emissions = 0;
    QObject::connect(panel, &DiversityNoiseProfilePanel::dismissedKindsChanged,
                     [&](const QSet<QString>& kinds) {
                         lastSeen = kinds;
                         ++emissions;
                     });

    dismissButton(w, QStringLiteral("IMPULSE"))->click();
    CHECK(emissions == 1);
    CHECK(lastSeen == QSet<QString>{QStringLiteral("impulse")});

    dismissButton(w, QStringLiteral("TONE"))->click();
    CHECK(emissions == 2);
    CHECK(lastSeen
          == (QSet<QString>{QStringLiteral("impulse"), QStringLiteral("tone")}));

    undoButton(w, QStringLiteral("IMPULSE"))->click();
    CHECK(emissions == 3);
    CHECK(lastSeen == QSet<QString>{QStringLiteral("tone")});
    closedToStart();
}

// (f) A row whose action is already in force has nothing for DISMISS to do:
// it is already "handled" in the sense the button exists for.
// MUTATION: a gate check keyed on "has an action" alone (rather than "has an
// action AND is not active") would put a DISMISS on these rows too, since
// both have an action.
void testActiveRowsGetNoDismissButton()
{
    closedToStart();
    FakeGate net;
    AetherGateApplet a(nullptr, &net);
    connectGate(a, net, kDiversityStatusKindsActive);
    DiversityWindow* w = openOnSite(a);
    CHECK(w != nullptr);
    if (!w)
        return;

    CHECK(kindAction(w, 0) != nullptr && kindAction(w, 0)->isChecked());
    CHECK(kindAction(w, 1) != nullptr && kindAction(w, 1)->isChecked());
    CHECK(dismissButton(w, QStringLiteral("MAINS")) == nullptr);
    CHECK(dismissButton(w, QStringLiteral("IMPULSE")) == nullptr);
    closedToStart();
}

// Walks every QWidget under `root`, including itself, collecting any
// non-empty toolTip() longer than 90 characters.
void collectLongTooltips(QWidget* root, QStringList* out)
{
    if (!root)
        return;
    if (root->toolTip().size() > 90)
        *out << QStringLiteral("%1: %2 chars")
                    .arg(root->objectName(), QString::number(root->toolTip().size()));
    const auto children = root->findChildren<QWidget*>();
    for (QWidget* c : children) {
        if (c->toolTip().size() > 90) {
            *out << QStringLiteral("%1: %2 chars")
                        .arg(c->objectName(), QString::number(c->toolTip().size()));
        }
    }
}

// (g) Every widget tooltip on the SITE page is one line, AGENTS.md's 90-char
// rule. QTableWidgetItem tooltips (the kinds table's own Detail column, the
// header cells) are out of scope: QTableWidgetItem is not a QWidget and has
// no accessibleDescription to carry the long form, so it is not part of this
// migration -- see the final report.
// MUTATION: any long tooltip reintroduced anywhere on the page fails this.
void testEveryTooltipOnTheSitePageIsAtMostNinetyChars()
{
    closedToStart();
    FakeGate net;
    AetherGateApplet a(nullptr, &net);
    connectGate(a, net);
    DiversityWindow* w = openOnSite(a);
    CHECK(w != nullptr);
    if (!w)
        return;

    auto* page = w->findChild<QWidget*>(QStringLiteral("diversityWindowSitePage"));
    CHECK(page != nullptr);
    if (!page)
        return;

    QStringList offenders;
    collectLongTooltips(page, &offenders);
    CHECK(offenders.isEmpty());
    for (const QString& o : offenders)
        std::printf("  long tooltip: %s\n", qPrintable(o));
    closedToStart();
}

// (h) The two HELP buttons DiversityHelp::button() cannot tell apart by
// objectName on its own (it assigns the same one per Topic every call) --
// this is the SITE page renaming each instance to keep them distinct and
// under the right box.
// MUTATION: both buttons landing under the same frame (e.g. a copy-paste
// that reused noiseFrame for both) fails the "different parents" check.
void testHelpButtonsExistBesideBothTitles()
{
    closedToStart();
    FakeGate net;
    AetherGateApplet a(nullptr, &net);
    connectGate(a, net);
    DiversityWindow* w = openOnSite(a);
    CHECK(w != nullptr);
    if (!w)
        return;

    auto* noiseCaption =
        w->findChild<QLabel*>(QStringLiteral("diversityWindowNoiseProfileBoxCaption"));
    auto* beaconCaption =
        w->findChild<QLabel*>(QStringLiteral("diversityWindowBeaconBoxCaption"));
    auto* noiseHelp = w->findChild<QPushButton*>(QStringLiteral("diversityHelpButtonSiteNoise"));
    auto* beaconHelp =
        w->findChild<QPushButton*>(QStringLiteral("diversityHelpButtonSiteBeacons"));
    CHECK(noiseCaption != nullptr && beaconCaption != nullptr);
    CHECK(noiseHelp != nullptr && beaconHelp != nullptr);
    if (!noiseCaption || !beaconCaption || !noiseHelp || !beaconHelp)
        return;

    CHECK(noiseHelp->accessibleName() == QStringLiteral("Help for this page"));
    CHECK(noiseCaption->parentWidget() == noiseHelp->parentWidget());
    CHECK(beaconCaption->parentWidget() == beaconHelp->parentWidget());
    CHECK(noiseHelp->parentWidget() != beaconHelp->parentWidget());
    closedToStart();
}

// (i) A dismissed row, an undone one and a page full of six findings still
// fit the window's opening size with nothing scrolled -- the Do column's new
// container widgets are exactly the kind of change that would silently grow
// a cell past its row height. Also renders the page for a human to look at.
void testDismissedRowsStillFitAt1120x860()
{
    closedToStart();
    FakeGate net;
    AetherGateApplet a(nullptr, &net);
    connectGate(a, net);
    DiversityWindow* w = openOnSite(a);
    CHECK(w != nullptr);
    if (!w)
        return;

    dismissButton(w, QStringLiteral("IMPULSE"))->click();
    dismissButton(w, QStringLiteral("TONE"))->click();
    w->resize(1120, 860);
    settle();
    w->grab();   // offscreen platforms only finish layout on a real paint pass

    auto* scroll = child<QScrollArea>(w, "diversityWindowSiteScroll");
    CHECK(scroll != nullptr);
    if (!scroll)
        return;
    CHECK(scroll->widget()->minimumSizeHint().width() <= scroll->viewport()->width());
    CHECK(scroll->widget()->minimumSizeHint().height() <= scroll->viewport()->height());
    CHECK(!scroll->verticalScrollBar()->isVisible());
    CHECK(!scroll->horizontalScrollBar()->isVisible());

    QPixmap render = w->grab();
    render.save(QStringLiteral("/Users/crypticpy/Desktop/diversity-night/wpc-site.png"));

    w->close();
    settle();
    closedToStart();
}

} // namespace

int main(int argc, char** argv)
{
    TestSettingsProfile profile(QStringLiteral("diversity_site_dismiss_test"));
    QApplication app(argc, argv);

    testDismissHidesTheActionAndPersists();
    testDismissalExpiresWhenTheFindingMovesMoreThan3dB();
    testDismissalExpiresWhenTheKindDisappears();
    testUndoRestoresTheActionButton();
    testDismissedKindsSignalCarriesTheCurrentSet();
    testActiveRowsGetNoDismissButton();
    testEveryTooltipOnTheSitePageIsAtMostNinetyChars();
    testHelpButtonsExistBesideBothTitles();
    testDismissedRowsStillFitAt1120x860();

    std::printf("\n%d diversity site dismiss test(s) failed\n", g_failed);
    return g_failed == 0 ? 0 : 1;
}
