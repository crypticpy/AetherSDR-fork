#pragma once

// DiversityHelp -- the small "i" button every Diversity window page carries,
// and what it opens. One button factory rather than six copies of the same
// six lines, and one place that maps a page to its help resource so the
// mapping cannot drift between pages.
//
// The dialog itself is the app's existing HelpDialog (see HelpDialog.h): a
// PersistentDialog that renders a bundled markdown resource. This class only
// picks which resource and builds the button that opens it.

#include <QString>

class QWidget;
class QPushButton;

namespace AetherSDR {

class DiversityHelp {
public:
    // One topic per Diversity window page, plus CHAIN (its own window, not
    // one of the five DiversitySessionModel::Page tabs).
    enum class Topic {
        Start,
        Slice,
        Band,
        Site,
        Filter,
        Chain
    };

    // An 18x18 flat QPushButton reading "i", wired to open(topic) on click.
    // objectName is "diversityHelpButton<Topic>" (e.g.
    // "diversityHelpButtonSlice"), accessibleName is "Help for this page",
    // and the tooltip names the page in one line -- screen-reader and
    // check_a11y.py both read the same two strings.
    static QPushButton* button(QWidget* parent, Topic topic);

    // Opens the topic's help resource in the app's existing HelpDialog.
    // parent is the dialog's Qt parent (typically the page or window the
    // button lives on); the dialog is WA_DeleteOnClose, so callers do not
    // own it past this call.
    static void open(Topic topic, QWidget* parent);

    // ":/help/diversity-slice.md" and so on. Every Topic resolves to a
    // real, non-empty resource path -- what diversity_help_test asserts.
    static QString resourcePath(Topic topic);

    // The window title HelpDialog shows for the topic, e.g. "Diversity Help
    // -- Slice".
    static QString title(Topic topic);
};

} // namespace AetherSDR
