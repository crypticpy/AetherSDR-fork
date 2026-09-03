#include "gui/DiversityFilterPanel.h"

#include "core/ThemeManager.h"

#include <QEvent>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QResizeEvent>

#include <algorithm>
#include <cmath>
#include <limits>

namespace AetherSDR {

namespace {

// How near a handle the pointer has to be, in pixels, to grab it. Six is about
// a fingertip's worth of slop at this scale and still narrow enough that the
// two handles of a 100 Hz CW filter do not overlap.
constexpr double kGrabPx = 6.0;

// The column one handle owns, for the partial repaint a drag does instead of a
// full one -- wider than the handle by its own pen, so nothing is left behind.
constexpr int kHandleRectPx = 16;

// Every edge the operator sets is a multiple of ten. The gate accepts any
// integer, but a passband edge is not a thing anybody wants to the Hz, and
// snapping is what makes a drag land on a round number instead of 2913.
constexpr int kSnapHz = 10;
constexpr int kArrowStepHz = 10;
// Shift is ten times the step, not five: 100 Hz is one gridline's worth of
// passband and the number an operator says out loud ("open it up a hundred").
constexpr int kArrowFastStepHz = 100;

// The narrowest passband a drag may produce. Below this the two handles are
// the same handle and the operator cannot get out of it by dragging.
constexpr int kMinSpanHz = 50;
constexpr int kMaxEdgeHz = 20000;

bool jsonNumber(const QJsonObject& obj, const char* key, double* out)
{
    const QJsonValue v = obj.value(QLatin1String(key));
    if (!v.isDouble())
        return false;
    *out = v.toDouble();
    return true;
}

int snapped(double hz)
{
    return int(std::lround(hz / double(kSnapHz))) * kSnapHz;
}

} // namespace

DiversityFilterPanel::DiversityFilterPanel(QWidget* parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("diversityWindowFilterPanel"));
    setAccessibleName(tr("Filter response"));
    setAccessibleDescription(
        tr("The slice filter's measured response, with the passband you asked "
           "for shaded over it. Drag either edge to move it, double-click "
           "anywhere on the curve to notch that frequency, drag a notch mark "
           "to move it, right-click one to take it away, and use the left and "
           "right arrow keys (hold Shift for ten times the step) to move the "
           "edge nearest the pointer. Up and down choose which edge that is."));
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setCursor(Qt::CrossCursor);
    m_cursorHz = std::numeric_limits<double>::quiet_NaN();
    m_contourHz = std::numeric_limits<double>::quiet_NaN();
    m_apfHz = std::numeric_limits<double>::quiet_NaN();
    m_specFloorDb = std::numeric_limits<double>::quiet_NaN();
    m_autoLowHz = std::numeric_limits<double>::quiet_NaN();
    m_autoHighHz = std::numeric_limits<double>::quiet_NaN();
    m_notchFromHz = std::numeric_limits<double>::quiet_NaN();
    m_notchGhostHz = std::numeric_limits<double>::quiet_NaN();
}

void DiversityFilterPanel::clear()
{
    if (!m_available && m_hz.isEmpty() && m_specDb.isEmpty())
        return;                       // already empty: an empty poll costs nothing
    m_available = false;
    m_hz.clear();
    m_db.clear();
    m_specHz.clear();
    m_specDb.clear();
    m_specAxisDb.clear();
    m_specFloorDb = std::numeric_limits<double>::quiet_NaN();
    m_notches.clear();
    m_anf.clear();
    m_minHz = 0.0;
    m_maxHz = 0.0;
    m_lowHz = 0;
    m_highHz = 0;
    m_contourHz = std::numeric_limits<double>::quiet_NaN();
    m_apfHz = std::numeric_limits<double>::quiet_NaN();
    m_autoLowHz = std::numeric_limits<double>::quiet_NaN();
    m_autoHighHz = std::numeric_limits<double>::quiet_NaN();
    m_drag = Edge::None;
    m_notchDrag = -1;
    m_layersDirty = true;
    m_specAreaDirty = true;
    update();
}

// What is on screen right now, as bytes. Two of them, because the picture has
// two clocks: the FILTER changes when somebody changes it, and the spectrum
// changes twice a second on its own. Comparing the bytes before and after a
// parse is how applyStatus() answers "did this body say anything new?" without
// keeping a second copy of every field -- the same trick ChainStage::shape()
// uses to decide whether a strip has to be rebuilt. Raw doubles, so a NaN
// matches a NaN: "the contour is off" is one state, not a change every poll.
namespace {
void feed(QByteArray& b, const QVector<double>& v)
{
    b.append(reinterpret_cast<const char*>(v.constData()),
             qsizetype(v.size() * sizeof(double)));
}
void feed(QByteArray& b, const QVector<QPointF>& v)
{
    b.append(reinterpret_cast<const char*>(v.constData()),
             qsizetype(v.size() * sizeof(QPointF)));
}
void feed(QByteArray& b, std::initializer_list<double> vs)
{
    for (double v : vs)
        b.append(reinterpret_cast<const char*>(&v), qsizetype(sizeof(double)));
}
} // namespace

QByteArray DiversityFilterPanel::filterFingerprint() const
{
    QByteArray b;
    feed(b, m_hz);
    feed(b, m_db);
    feed(b, m_notches);
    feed(b, m_anf);
    feed(b, {double(m_available), double(m_lowHz), double(m_highHz), m_contourHz,
             m_apfHz, m_autoLowHz, m_autoHighHz,
             // The spectrum APPEARING or going away is a filter-layer change
             // too: the floor tick and the "no audio yet" corner turn on and
             // off with it, and both are drawn in the cached layer.
             double(m_specDb.isEmpty())});
    return b;
}

QByteArray DiversityFilterPanel::spectrumFingerprint() const
{
    QByteArray b;
    feed(b, m_specHz);
    feed(b, m_specDb);
    feed(b, {m_specFloorDb});
    return b;
}

// One /filter answer. Rule 1 of the header is decided here: the body is parsed
// straight into the fields it describes, and the two fingerprints taken either
// side of that say whether anything on screen actually moved.
void DiversityFilterPanel::applyStatus(const QJsonObject& filter)
{
    if (!filter.value(QStringLiteral("available")).toBool()) {
        clear();
        return;
    }
    const QByteArray wasFilter = filterFingerprint();
    const QByteArray wasSpectrum = spectrumFingerprint();
    m_available = true;

    const QJsonObject response = filter.value(QStringLiteral("response")).toObject();
    const QJsonArray hz = response.value(QStringLiteral("hz")).toArray();
    const QJsonArray db = response.value(QStringLiteral("db")).toArray();
    // Both arrays or neither: a curve drawn from mismatched axes would be a
    // picture of arithmetic rather than of a filter. A body carrying no
    // response at all leaves the curve that is already there.
    if (!hz.isEmpty() && hz.size() == db.size()) {
        m_hz.resize(hz.size());
        m_db.resize(db.size());
        m_minHz = hz.at(0).toDouble();
        m_maxHz = m_minHz;
        for (int i = 0; i < hz.size(); ++i) {
            m_hz[i] = hz.at(i).toDouble();
            m_db[i] = db.at(i).toDouble();
            m_minHz = std::min(m_minHz, m_hz[i]);
            m_maxHz = std::max(m_maxHz, m_hz[i]);
        }
    }

    // The pre-filter spectrum. "spectrum": null before the gate has heard a
    // block, and then everything is dropped rather than left stale: an area
    // that stopped updating would go on claiming a channel is occupied.
    m_specHz.clear();
    m_specDb.clear();
    m_specFloorDb = std::numeric_limits<double>::quiet_NaN();
    const QJsonObject spectrum = filter.value(QStringLiteral("spectrum")).toObject();
    const QJsonArray specHz = spectrum.value(QStringLiteral("hz")).toArray();
    const QJsonArray specDb = spectrum.value(QStringLiteral("db")).toArray();
    double floorDb = 0.0;
    // The floor is not optional: without it there is no scale to pin the area
    // to, and guessing one would be an invented measurement.
    if (!specHz.isEmpty() && specHz.size() == specDb.size()
        && jsonNumber(spectrum, "floor_db", &floorDb)) {
        m_specHz.resize(specHz.size());
        m_specDb.resize(specDb.size());
        for (int i = 0; i < specHz.size(); ++i) {
            m_specHz[i] = specHz.at(i).toDouble();
            m_specDb[i] = specDb.at(i).toDouble();
        }
        m_specFloorDb = floorDb;
    }

    // A handle under the pointer keeps what the pointer is doing to it: a 2 Hz
    // poll landing mid-drag would snatch it back.
    const int heldLow = m_lowHz;
    const int heldHigh = m_highHz;
    double v = 0.0;
    if (jsonNumber(filter, "low_hz", &v))
        m_lowHz = int(std::lround(v));
    if (jsonNumber(filter, "high_hz", &v))
        m_highHz = int(std::lround(v));
    if (m_drag != Edge::None) {
        m_lowHz = heldLow;
        m_highHz = heldHigh;
    }

    m_notches.clear();
    const QJsonArray notches = filter.value(QStringLiteral("notches")).toArray();
    for (const QJsonValue& entry : notches) {
        const QJsonObject notch = entry.toObject();
        double at = 0.0;
        if (!jsonNumber(notch, "hz", &at))
            continue;
        double depth = 0.0;
        jsonNumber(notch, "depth_db", &depth);
        m_notches.append(QPointF(at, depth));
    }

    m_anf.clear();
    const QJsonObject anf = filter.value(QStringLiteral("anf")).toObject();
    if (anf.value(QStringLiteral("enabled")).toBool()) {
        const QJsonArray found = anf.value(QStringLiteral("found_hz")).toArray();
        const QJsonArray depths = anf.value(QStringLiteral("depth_db")).toArray();
        for (int i = 0; i < found.size(); ++i) {
            m_anf.append(QPointF(found.at(i).toDouble(),
                                 i < depths.size() ? depths.at(i).toDouble() : 0.0));
        }
    }

    const QJsonObject contour = filter.value(QStringLiteral("contour")).toObject();
    m_contourHz = std::numeric_limits<double>::quiet_NaN();
    if (contour.value(QStringLiteral("enabled")).toBool()
        && jsonNumber(contour, "hz", &v)) {
        m_contourHz = v;
    }
    const QJsonObject apf = filter.value(QStringLiteral("apf")).toObject();
    m_apfHz = std::numeric_limits<double>::quiet_NaN();
    if (apf.value(QStringLiteral("enabled")).toBool() && jsonNumber(apf, "hz", &v))
        m_apfHz = v;

    const QJsonObject autoWidth = filter.value(QStringLiteral("auto")).toObject();
    m_autoLowHz = std::numeric_limits<double>::quiet_NaN();
    m_autoHighHz = std::numeric_limits<double>::quiet_NaN();
    if (autoWidth.value(QStringLiteral("enabled")).toBool()) {
        if (jsonNumber(autoWidth, "low_hz", &v))
            m_autoLowHz = v;
        if (jsonNumber(autoWidth, "high_hz", &v))
            m_autoHighHz = v;
    }

    const bool specChanged = spectrumFingerprint() != wasSpectrum;
    const bool filterChanged = filterFingerprint() != wasFilter;
    if (!specChanged && !filterChanged)
        return;                       // a poll that is not news costs no paint
    if (specChanged) {
        // Floor pinning is arithmetic on the payload, not on the pixels: the
        // picture is drawn many more times than the numbers arrive.
        m_specAxisDb.resize(m_specDb.size());
        for (int i = 0; i < int(m_specDb.size()); ++i) {
            m_specAxisDb[i] = std::clamp(kFloorAxisDb + (m_specDb[i] - m_specFloorDb),
                                         kBottomDb, kTopDb);
        }
        m_specAreaDirty = true;
    }
    if (filterChanged)
        m_layersDirty = true;
    update();
}

// --------------------------------------------------------------------------
// Geometry
// --------------------------------------------------------------------------

QRectF DiversityFilterPanel::plotRect() const
{
    return QRectF(kLeftGutter, kTopMargin,
                  std::max(1, width() - kLeftGutter - kRightMargin),
                  std::max(1, height() - kTopMargin - kBottomGutter));
}

double DiversityFilterPanel::xForHz(double hz) const
{
    const QRectF r = plotRect();
    if (m_maxHz <= m_minHz)
        return r.left();
    const double t = (hz - m_minHz) / (m_maxHz - m_minHz);
    return r.left() + std::clamp(t, 0.0, 1.0) * r.width();
}

double DiversityFilterPanel::hzForX(double x) const
{
    const QRectF r = plotRect();
    if (m_maxHz <= m_minHz || r.width() <= 0.0)
        return 0.0;
    const double t = std::clamp((x - r.left()) / r.width(), 0.0, 1.0);
    return m_minHz + t * (m_maxHz - m_minHz);
}

double DiversityFilterPanel::yForDb(double db) const
{
    const QRectF r = plotRect();
    const double t = (kTopDb - std::clamp(db, kBottomDb, kTopDb)) / (kTopDb - kBottomDb);
    return r.top() + t * r.height();
}

double DiversityFilterPanel::spectrumAxisDbAt(int index) const
{
    if (index < 0 || index >= int(m_specAxisDb.size()))
        return std::numeric_limits<double>::quiet_NaN();
    return m_specAxisDb[index];
}

double DiversityFilterPanel::notchHzAt(int index) const
{
    if (index < 0 || index >= int(m_notches.size()))
        return std::numeric_limits<double>::quiet_NaN();
    return m_notches[index].x();
}

double DiversityFilterPanel::cursorDb() const
{
    if (std::isnan(m_cursorHz) || m_specAxisDb.isEmpty())
        return std::numeric_limits<double>::quiet_NaN();
    // The nearest bin, not an interpolation: a number between two bins is one
    // nobody measured.
    int best = 0;
    double bestGap = std::abs(m_specHz[0] - m_cursorHz);
    for (int i = 1; i < int(m_specHz.size()); ++i) {
        const double gap = std::abs(m_specHz[i] - m_cursorHz);
        if (gap < bestGap) {
            bestGap = gap;
            best = i;
        }
    }
    return m_specAxisDb[best];
}

DiversityFilterPanel::Edge DiversityFilterPanel::edgeAt(double x) const
{
    if (!m_available || m_maxHz <= m_minHz)
        return Edge::None;
    const double dLow = std::abs(x - xForHz(m_lowHz));
    const double dHigh = std::abs(x - xForHz(m_highHz));
    if (dLow <= kGrabPx && dLow <= dHigh)
        return Edge::Low;
    if (dHigh <= kGrabPx)
        return Edge::High;
    return Edge::None;
}

// The bottom ticks are eight pixels tall, so their hit box is the bottom of
// the plot; the tones and the AUTO edges run the whole height, so theirs is
// the column.
QString DiversityFilterPanel::markAt(double x, double y) const
{
    if (!m_available || m_maxHz <= m_minHz)
        return QString();
    const QRectF r = plotRect();
    const bool nearBottom = y >= r.bottom() - 12.0;
    for (double centre : {m_contourHz, m_apfHz}) {
        if (!std::isnan(centre) && nearBottom && std::abs(x - xForHz(centre)) <= kGrabPx)
            return centre == m_contourHz ? QStringLiteral("contour")
                                         : QStringLiteral("apf");
    }
    for (const QPointF& tone : m_anf) {
        if (std::abs(x - xForHz(tone.x())) <= kGrabPx)
            return QStringLiteral("anf");
    }
    for (double edge : {m_autoLowHz, m_autoHighHz}) {
        if (!std::isnan(edge) && std::abs(x - xForHz(edge)) <= kGrabPx)
            return QStringLiteral("auto");
    }
    return QString();
}

int DiversityFilterPanel::notchAt(double x) const
{
    if (!m_available || m_maxHz <= m_minHz || edgeAt(x) != Edge::None)
        return -1;
    int best = -1;
    double bestGap = kGrabPx;
    for (int i = 0; i < int(m_notches.size()); ++i) {
        const double gap = std::abs(x - xForHz(m_notches[i].x()));
        if (gap <= bestGap) {
            bestGap = gap;
            best = i;
        }
    }
    return best;
}

QRect DiversityFilterPanel::handleRect(double hz) const
{
    const QRectF r = plotRect();
    return QRect(int(xForHz(hz)) - kHandleRectPx / 2, int(r.top()) - 1, kHandleRectPx,
                 int(r.height()) + 2);
}

void DiversityFilterPanel::moveEdge(Edge edge, double hz, bool partial)
{
    const QRect before = handleRect(edge == Edge::Low ? m_lowHz : m_highHz);
    const int want = std::clamp(snapped(hz), 0, kMaxEdgeHz);
    if (edge == Edge::Low)
        m_lowHz = std::min(want, m_highHz - kMinSpanHz);
    else if (edge == Edge::High)
        m_highHz = std::max(want, m_lowHz + kMinSpanHz);
    const QRect after = handleRect(edge == Edge::Low ? m_lowHz : m_highHz);
    if (partial) {
        // Header rule 3. The shading stays where the GATE last put it until the
        // button comes up, which is also the more honest picture: it shows
        // where the edge was and where it is going at once.
        update(before.united(after));
        return;
    }
    m_layersDirty = true;
    update();
}

// --------------------------------------------------------------------------
// Input
// --------------------------------------------------------------------------

void DiversityFilterPanel::mousePressEvent(QMouseEvent* ev)
{
    const double x = ev->position().x();
    if (ev->button() == Qt::RightButton) {
        // The mark IS the control: one that needed a menu to remove would be a
        // mark you could only ever add.
        const int notch = notchAt(x);
        if (notch >= 0) {
            emit notchRemoveRequested(m_notches[notch].x());
            ev->accept();
            return;
        }
        QWidget::mousePressEvent(ev);
        return;
    }
    if (ev->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(ev);
        return;
    }
    m_pressMark.clear();
    const Edge edge = edgeAt(x);
    if (edge != Edge::None) {
        m_drag = edge;
        m_focusEdge = edge;
        m_pressLowHz = m_lowHz;
        m_pressHighHz = m_highHz;
        setFocus(Qt::MouseFocusReason);
        ev->accept();
        return;
    }
    const int notch = notchAt(x);
    if (notch >= 0) {
        m_notchDrag = notch;
        m_notchFromHz = m_notches[notch].x();
        m_notchGhostHz = m_notchFromHz;
        setFocus(Qt::MouseFocusReason);
        ev->accept();
        return;
    }
    // A mark that cannot be dragged. Nothing happens on the press: the release
    // decides, so a press that wanders off is not a jump to another tab.
    m_pressMark = markAt(x, ev->position().y());
    if (!m_pressMark.isEmpty()) {
        ev->accept();
        return;
    }
    QWidget::mousePressEvent(ev);
}

void DiversityFilterPanel::mouseMoveEvent(QMouseEvent* ev)
{
    const double x = ev->position().x();
    m_cursorHz = m_available ? hzForX(x) : std::numeric_limits<double>::quiet_NaN();
    emit cursorMoved(m_cursorHz, cursorDb());
    if (m_drag != Edge::None) {
        moveEdge(m_drag, hzForX(x), /*partial=*/true);
        ev->accept();
        return;
    }
    if (m_notchDrag >= 0) {
        const QRect before = handleRect(m_notchGhostHz);
        m_notchGhostHz = double(std::clamp(snapped(hzForX(x)), 0, kMaxEdgeHz));
        update(before.united(handleRect(m_notchGhostHz)));
        ev->accept();
        return;
    }
    // The cursor is the affordance: nothing says "draggable" except that the
    // pointer changes over the two handles and over a notch mark.
    const Edge overEdge = edgeAt(x);
    setCursor(overEdge != Edge::None ? Qt::SplitHCursor
              : notchAt(x) >= 0      ? Qt::SizeHorCursor
              : !markAt(x, ev->position().y()).isEmpty() ? Qt::PointingHandCursor
                                     : Qt::CrossCursor);
    // The arrow keys move whichever edge the pointer is nearest; Up/Down still
    // override that for anybody working without a mouse.
    const Edge nearer = std::abs(x - xForHz(m_lowHz)) <= std::abs(x - xForHz(m_highHz))
                            ? Edge::Low
                            : Edge::High;
    if (nearer != m_focusEdge) {
        m_focusEdge = nearer;
        if (hasFocus())
            update(handleRect(m_lowHz).united(handleRect(m_highHz)));
    }
}

void DiversityFilterPanel::mouseReleaseEvent(QMouseEvent* ev)
{
    if (m_drag != Edge::None) {
        m_drag = Edge::None;
        m_layersDirty = true;         // one rebuild per drag, not one per pixel
        update();
        // A press on a handle that never moved it is a CLICK on the passband,
        // and a click on a mark is the door to its stage -- not a write of the
        // two edges the gate already has.
        if (m_lowHz == m_pressLowHz && m_highHz == m_pressHighHz)
            emit markClicked(QStringLiteral("passband"));
        else
            emit edgesDragged(m_lowHz, m_highHz);
        ev->accept();
        return;
    }
    if (m_notchDrag >= 0) {
        const double from = m_notchFromHz;
        const double to = m_notchGhostHz;
        m_notchDrag = -1;
        m_notchFromHz = std::numeric_limits<double>::quiet_NaN();
        m_notchGhostHz = std::numeric_limits<double>::quiet_NaN();
        update();
        // A press that never moved is not a request to rebuild a filter; it
        // is a click on the notch mark.
        if (snapped(from) != snapped(to))
            emit notchMoveRequested(from, to);
        else
            emit markClicked(QStringLiteral("notch"));
        ev->accept();
        return;
    }
    if (!m_pressMark.isEmpty()) {
        const QString mark = m_pressMark;
        m_pressMark.clear();
        // Only if the button came up on the same mark it went down on.
        if (markAt(ev->position().x(), ev->position().y()) == mark)
            emit markClicked(mark);
        ev->accept();
        return;
    }
    QWidget::mouseReleaseEvent(ev);
}

void DiversityFilterPanel::mouseDoubleClickEvent(QMouseEvent* ev)
{
    const double x = ev->position().x();
    // Not on a handle, a notch, or any other mark: those are doors and drags,
    // and a double-click on one must not also drop a notch on it.
    if (ev->button() != Qt::LeftButton || !m_available || edgeAt(x) != Edge::None
        || notchAt(x) >= 0 || !markAt(x, ev->position().y()).isEmpty()) {
        QWidget::mouseDoubleClickEvent(ev);
        return;
    }
    emit notchRequested(double(snapped(hzForX(x))));
    ev->accept();
}

void DiversityFilterPanel::keyPressEvent(QKeyEvent* ev)
{
    if (!m_available) {
        QWidget::keyPressEvent(ev);
        return;
    }
    // Up/Down pick the edge, Left/Right move it. Two keys rather than a Tab
    // stop each, so the whole control is one stop in the window's focus chain
    // and an operator tabbing past it does not have to pass through two.
    if (ev->key() == Qt::Key_Up || ev->key() == Qt::Key_Down) {
        m_focusEdge = ev->key() == Qt::Key_Up ? Edge::High : Edge::Low;
        update(handleRect(m_lowHz).united(handleRect(m_highHz)));
        ev->accept();
        return;
    }
    if (ev->key() != Qt::Key_Left && ev->key() != Qt::Key_Right) {
        QWidget::keyPressEvent(ev);
        return;
    }
    const int step = (ev->modifiers() & Qt::ShiftModifier) ? kArrowFastStepHz
                                                           : kArrowStepHz;
    const int delta = ev->key() == Qt::Key_Left ? -step : step;
    const int from = m_focusEdge == Edge::High ? m_highHz : m_lowHz;
    moveEdge(m_focusEdge, double(from + delta));
    // The write waits for the key to come UP: holding an arrow down is one
    // adjustment, not forty writes into a threaded gate.
    m_keyMoved = true;
    ev->accept();
}

void DiversityFilterPanel::keyReleaseEvent(QKeyEvent* ev)
{
    if (!m_keyMoved || ev->isAutoRepeat()
        || (ev->key() != Qt::Key_Left && ev->key() != Qt::Key_Right)) {
        QWidget::keyReleaseEvent(ev);
        return;
    }
    m_keyMoved = false;
    emit edgesDragged(m_lowHz, m_highHz);
    ev->accept();
}

void DiversityFilterPanel::leaveEvent(QEvent* ev)
{
    m_cursorHz = std::numeric_limits<double>::quiet_NaN();
    emit cursorMoved(m_cursorHz, m_cursorHz);
    QWidget::leaveEvent(ev);
}

void DiversityFilterPanel::resizeEvent(QResizeEvent* ev)
{
    m_layersDirty = true;
    m_specAreaDirty = true;
    QWidget::resizeEvent(ev);
}

} // namespace AetherSDR
