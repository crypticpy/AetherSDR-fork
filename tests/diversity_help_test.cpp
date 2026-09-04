// DiversityHelp unit test -- needs QApplication + Widgets (button() builds a
// real QPushButton), same reason tests/help_dialog_test.cpp does. Confirms
// every Topic resolves to a real, non-empty bundled resource (catching a
// typo in resources.qrc's alias or a missing help/diversity-*.md file) and
// that the button factory sets the three things an assistive-tech user or
// check_a11y.py actually reads: objectName, accessibleName, tooltip.

#include "TestSettingsProfile.h"
#include "gui/DiversityHelp.h"
#include "gui/HelpDialog.h"

#include <QApplication>
#include <QFile>
#include <QPushButton>
#include <QString>
#include <QWidget>

#include <cstdio>

using namespace AetherSDR;

namespace {

int g_failed = 0;

void report(const char* name, bool ok)
{
    std::printf("%s %s\n", ok ? "[ OK ]" : "[FAIL]", name);
    if (!ok)
        ++g_failed;
}

const DiversityHelp::Topic kAllTopics[] = {
    DiversityHelp::Topic::Start, DiversityHelp::Topic::Slice, DiversityHelp::Topic::Band,
    DiversityHelp::Topic::Site,    DiversityHelp::Topic::Filter, DiversityHelp::Topic::Chain,
};

// MUTATION GUARD: a Topic whose resourcePath() is empty, misspelled, or
// missing from resources.qrc -- QFile::open() catches all three.
void everyTopicResolvesToANonEmptyResource()
{
    for (DiversityHelp::Topic topic : kAllTopics) {
        const QString path = DiversityHelp::resourcePath(topic);
        report("resourcePath is non-empty", !path.isEmpty());

        QFile file(path);
        const bool opened = file.open(QIODevice::ReadOnly | QIODevice::Text);
        report("resource opens", opened);
        if (opened) {
            const QByteArray body = file.readAll();
            report("resource is non-empty", !body.isEmpty());
        }
    }
}

// MUTATION GUARD: button() forgetting the objectName suffix, the shared
// accessibleName, or leaving the tooltip blank.
void buttonCarriesObjectNameAccessibleNameAndTooltip()
{
    auto* btn = DiversityHelp::button(nullptr, DiversityHelp::Topic::Slice);
    report("objectName names the topic",
           btn->objectName() == QStringLiteral("diversityHelpButtonSlice"));
    report("accessibleName is the shared one",
           btn->accessibleName() == QStringLiteral("Help for this page"));
    report("tooltip is one line and non-empty",
           !btn->toolTip().isEmpty() && !btn->toolTip().contains(QLatin1Char('\n')));
    report("button is 18x18", btn->width() == 18 && btn->height() == 18);
    report("button reads \"i\"", btn->text() == QStringLiteral("i"));
    delete btn;
}

// Every topic's button gets its own objectName -- MUTATION GUARD: all six
// buttons sharing one hard-coded suffix.
void everyTopicGetsItsOwnButtonObjectName()
{
    QStringList seen;
    for (DiversityHelp::Topic topic : kAllTopics) {
        auto* btn = DiversityHelp::button(nullptr, topic);
        report("objectName not already seen", !seen.contains(btn->objectName()));
        seen << btn->objectName();
        delete btn;
    }
}

// open() shows the app's existing HelpDialog, not a bespoke one --
// MUTATION GUARD: open() building its own QDialog instead of reusing
// HelpDialog, or leaving it without WA_DeleteOnClose.
void openShowsAHelpDialog()
{
    auto* parent = new QWidget();
    DiversityHelp::open(DiversityHelp::Topic::Band, parent);

    HelpDialog* dlg = parent->findChild<HelpDialog*>();
    report("open() creates a HelpDialog", dlg != nullptr);
    if (dlg) {
        report("open() shows it", dlg->isVisible());
        report("open() sets WA_DeleteOnClose", dlg->testAttribute(Qt::WA_DeleteOnClose));
        dlg->close();
    }
    delete parent;
}

} // namespace

int main(int argc, char** argv)
{
    TestSettingsProfile settingsProfile(QStringLiteral("aether-diversity-help-test"));
    QApplication app(argc, argv);

    everyTopicResolvesToANonEmptyResource();
    buttonCarriesObjectNameAccessibleNameAndTooltip();
    everyTopicGetsItsOwnButtonObjectName();
    openShowsAHelpDialog();

    std::printf("\n%d check(s) failed\n", g_failed);
    return g_failed == 0 ? 0 : 1;
}
