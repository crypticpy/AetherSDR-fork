#include "gui/DiversityHelp.h"

#include "gui/HelpDialog.h"

#include <QPushButton>
#include <QWidget>

namespace AetherSDR {

namespace {

// Which of the six topics a Topic value is, spelled out once so button(),
// open(), resourcePath() and title() cannot disagree on the mapping.
struct TopicInfo {
    const char* objectSuffix; // "Session", "Slice", ...
    const char* label;        // "Session", "Slice", ... (title-case, one word)
    const char* resourceFile; // "diversity-session.md", ...
};

const TopicInfo& infoFor(DiversityHelp::Topic topic)
{
    static const TopicInfo kSession{"Session", "Session", "diversity-session.md"};
    static const TopicInfo kSlice{"Slice", "Slice", "diversity-slice.md"};
    static const TopicInfo kBand{"Band", "Band", "diversity-band.md"};
    static const TopicInfo kSite{"Site", "Site", "diversity-site.md"};
    static const TopicInfo kFilter{"Filter", "Filter", "diversity-filter.md"};
    static const TopicInfo kChain{"Chain", "Chain", "diversity-chain.md"};

    switch (topic) {
    case DiversityHelp::Topic::Session:
        return kSession;
    case DiversityHelp::Topic::Slice:
        return kSlice;
    case DiversityHelp::Topic::Band:
        return kBand;
    case DiversityHelp::Topic::Site:
        return kSite;
    case DiversityHelp::Topic::Filter:
        return kFilter;
    case DiversityHelp::Topic::Chain:
        return kChain;
    }
    return kSession;
}

} // namespace

QString DiversityHelp::resourcePath(Topic topic)
{
    return QStringLiteral(":/help/%1").arg(QString::fromLatin1(infoFor(topic).resourceFile));
}

QString DiversityHelp::title(Topic topic)
{
    return QStringLiteral("Diversity Help — %1").arg(QString::fromLatin1(infoFor(topic).label));
}

QPushButton* DiversityHelp::button(QWidget* parent, Topic topic)
{
    const TopicInfo& info = infoFor(topic);

    auto* btn = new QPushButton(QStringLiteral("i"), parent);
    btn->setObjectName(QStringLiteral("diversityHelpButton%1")
                           .arg(QString::fromLatin1(info.objectSuffix)));
    btn->setAccessibleName(QStringLiteral("Help for this page"));
    btn->setToolTip(QStringLiteral("Help for the %1 page").arg(QString::fromLatin1(info.label)));
    btn->setFlat(true);
    btn->setFixedSize(18, 18);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setFocusPolicy(Qt::TabFocus);

    QObject::connect(btn, &QPushButton::clicked, btn, [parent, topic]() {
        DiversityHelp::open(topic, parent);
    });

    return btn;
}

void DiversityHelp::open(Topic topic, QWidget* parent)
{
    auto* dlg = new HelpDialog(title(topic), resourcePath(topic), parent);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->show();
    dlg->raise();
    dlg->activateWindow();
}

} // namespace AetherSDR
