#pragma once

// DiversityFilterPanel -- the gate's slice filter drawn as the thing it
// actually is: a response curve with the passband you asked for laid over the
// response you got.
//
// Every other view of a receiver filter in this application is a pair of
// numbers in two spin boxes. That is enough to SET a filter and useless for
// understanding one: it cannot show you that a 1023-tap sharp filter has a
// 49 Hz skirt while the soft one has 300, that the notch you placed is 40 Hz
// off the heterodyne, that the auto-width tracker has quietly narrowed you to
// 250 Hz above your own setting, or that the APF is sitting outside the
// passband doing nothing. All of those are one glance at a curve.
//
// So the curve is the primary control here, not an illustration of one. The
// two passband edges are draggable handles on it, a double-click drops a notch
// where you clicked, and the arrow keys move the focused edge for anybody who
// cannot or would rather not drag. The spin boxes in the WIDTH column beside
// it are still there for exact numbers -- this is the instrument, they are the
// keypad.
//
// It holds NO transport and NO authority: applyStatus() takes one /filter
// status object and the widget draws exactly what is in it. The gate is the
// source of truth, so a drag does not move the drawn passband permanently --
// it moves the handles, emits edgesDragged(), and the next status object is
// what the widget is actually showing a moment later. The one exception is the
// drag itself: while dragging(), the page does not feed it a status, because
// a 2 Hz poll landing mid-drag would snatch the handle back.
//
// Frequencies are AUDIO Hz throughout, always positive, exactly as the gate
// reports low_hz/high_hz. On LSB the spectrum is inverted at RF and the gate
// still reports 100..2900; drawing that mirrored would be a truer picture of
// the RF and a worse picture of what the operator is about to type into a
// spin box. The sideband is stated in the caption above the widget instead.
//
// THE SPECTRUM UNDER THE CURVE. The response is what the filter WOULD do to
// anything arriving; the "spectrum" object is what is actually arriving -- the
// one-second pre-filter spectrum the AUTO width and the ANF read. Drawn as a
// filled area behind the curve on the same Hz axis, it turns every other mark
// on this widget into a claim you can check: whether the AUTO edges landed on
// the energy, whether the notch is on the carrier, whether the passband is
// wide open over nothing.
//
// The gate reports it in dB BELOW ITS OWN PEAK, so its maximum is always 0.0.
// Painting that straight onto the 0..-60 axis would put a full-scale slab
// under the curve on a dead channel, which is the most confident possible
// picture of nothing. The floor is pinned instead: the gate's own median
// (floor_db) is drawn at kFloorAxisDb and everything else keeps its distance
// from it, so a quiet channel is a thin band at the floor tick and a station
// 30 dB over it rises 30 dB up the axis. The axis is then the filter's, and
// the area's HEIGHT is signal-over-noise rather than a level.
//
// WHAT IT COSTS TO DRAW. The operator's word for the first build was "a little
// laggy", and the reason was that a 2 Hz poll repainted everything: axes,
// gridlines, labels, the response curve, every notch mark, on every body,
// whether or not one pixel of it had changed. Three rules fix that and they
// are the reason for the counters below.
//
//   1. A poll that says the same thing costs NO paint. applyStatus() compares
//      what it parsed against what is already drawn and returns without an
//      update() when they match.
//   2. Everything that changes only when the FILTER changes -- grid, labels,
//      floor tick, AUTO marks, passband shading, notch and ANF marks, contour
//      and APF ticks, the response curve -- is cached in ONE transparent pixmap
//      at the device pixel ratio, rebuilt only when the response, the edges,
//      the notches or the widget's size change. The background wash and the
//      spectrum area are painted UNDER it every frame: the spectrum is the one
//      thing that genuinely moves twice a second, and painting it over the
//      curve would put the band on top of the answer, which is backwards.
//   3. A drag repaints the handle's own column, not the widget: mouseMove
//      calls update(QRect) over the old and new handle positions, and no layer
//      is rebuilt until the button comes up.
//
// DIRECT MANIPULATION. Everything the picture shows about the passband can be
// done ON the picture: drag either edge, double-click to notch what is under
// the pointer, drag an existing notch mark to move it, right-click one to take
// it away, and the arrow keys move whichever edge the pointer was last nearer
// to. A notch move is emitted as (from, to) rather than written here -- the
// gate's /filter/notch takes add= and clear= and has no move, so the page that
// owns the transport turns one gesture into the two writes, in order.
//
// And every mark is a DOOR. A click that does not turn into a drag -- on a
// handle, a notch, an ANF tone, the contour or APF tick, an AUTO edge -- is
// emitted as markClicked() with the id of the chain stage that owns the mark,
// so the CHAIN window can turn to that stage's card. The ids are the gate's own
// chain ids (passband, notch, anf, contour, apf, auto); this widget knows
// nothing else about the chain.

#include <QByteArray>
#include <QPixmap>
#include <QPolygonF>
#include <QRect>
#include <QString>
#include <QVector>
#include <QWidget>

class QJsonObject;
class QPainter;

namespace AetherSDR {

class DiversityFilterPanel : public QWidget {
    Q_OBJECT
public:
    explicit DiversityFilterPanel(QWidget* parent = nullptr);

    // One /filter status object. Every field is independently optional: a
    // missing "response" leaves the curve empty rather than flat, a missing
    // "notches" draws none rather than clearing the ones the gate still has.
    // An "available": false object empties the widget -- there is no filter to
    // draw in FM, and a stale curve would be a claim about a filter that is
    // not running.
    void applyStatus(const QJsonObject& filter);

    // Gate gone. Same as an unavailable payload, said by the window instead of
    // by the gate.
    void clear();

    // Where a frequency is drawn, and what frequency a column is. Public so a
    // test can put the pointer ON a handle rather than guess at the gutters.
    double xForHz(double hz) const;
    double hzForX(double x) const;

    // True from the press on a handle OR on a notch mark OR on the roof
    // handle to the release. The page checks it before feeding a poll: see
    // the header comment.
    bool dragging() const
    {
        return m_drag != Edge::None || m_notchDrag >= 0 || m_roofDrag;
    }

    // What the widget currently draws for the two edges. The page reads these
    // back on a release to work out WHICH edge moved, so a drag of the low
    // handle writes low= alone rather than re-asserting a high= the auto-width
    // tracker may own.
    int lowHz() const { return m_lowHz; }
    int highHz() const { return m_highHz; }

    // What the spectrum area currently draws. A painted widget has no child to
    // read back from and these are rendered values, so they are exposed the
    // way DiversitySnrMeter::shownDb() is -- there is no other way to check
    // that a floor moving in the payload moves the picture.
    bool   hasSpectrum() const { return !m_specDb.isEmpty(); }
    int    spectrumPointCount() const { return int(m_specDb.size()); }
    // The gate's own median, on the gate's dB-below-peak scale. NaN when there
    // is no spectrum -- "the floor is zero" is a different claim.
    double spectrumFloorDb() const { return m_specFloorDb; }
    // The dB `index` is actually PLOTTED at on this widget's 0..-60 axis:
    // floor-pinned and clipped. This, not the payload, is the picture.
    double spectrumAxisDbAt(int index) const;
    // Where AUTO has put the edges, or NaN each when AUTO is off. Drawn as
    // their own thin dashed marks so the operator can see what the tracker
    // chose against the energy it chose it from.
    double autoLowHz() const { return m_autoLowHz; }
    double autoHighHz() const { return m_autoHighHz; }

    // How many notch marks are drawn, and where. Read back by the page for its
    // one-line readout and by the tests, for the same reason the spectrum
    // getters exist: a painted widget has no child to ask.
    int    notchCount() const { return int(m_notches.size()); }
    double notchHzAt(int index) const;

    // Hz under the pointer, and the dB the spectrum is PLOTTED at there -- the
    // number on this widget's own 0..-60 gutter, not the gate's dB-below-peak,
    // so the corner readout and the axis beside it agree. Both NaN when the
    // pointer is not over the widget or there is no spectrum.
    double cursorHz() const { return m_cursorHz; }
    double cursorDb() const;

    // What the picture has actually cost. paintCount() counts paintEvent()s,
    // staticRebuildCount() counts rebuilds of the two cached layers. They are
    // how the three rules in the header comment are TESTED rather than
    // asserted: three identical polls must add one paint and no rebuild, a
    // spectrum-only body must paint without a rebuild, and a drag must not
    // rebuild until the button comes up.
    int paintCount() const { return m_paintCount; }
    int staticRebuildCount() const { return m_staticRebuilds; }

    // SQUEEZE (B24): the gate's own held/armed/off target, out of applyStatus()'s
    // "squeeze" block. Off is "since" absent; armed is "since" present and not
    // held (a target is configured but not yet accepted, or was refused);
    // held is the tool actually in force. See DiversityFilterPanelSqueeze.cpp
    // for the Hz-axis conversion this is drawn under.
    bool    squeezeOff() const { return !m_squeezeActive; }
    bool    squeezeArmed() const { return m_squeezeActive && !m_squeezeHeld; }
    bool    squeezeHeld() const { return m_squeezeHeld; }
    QString squeezeTarget() const { return m_squeezeTarget; }   // "signal" | "comb" | ""
    QString squeezeTool() const { return m_squeezeTool; }       // "null" | "notch" | ""
    QString squeezeReason() const { return m_squeezeReason; }
    QString squeezeWhy() const { return m_squeezeWhy; }
    // Signed, in the SLICE's own frame -- the gate's own squeeze.hz, not
    // abs-ified the way low_hz/high_hz are. NaN when there is no signal target.
    double  squeezeHz() const { return m_squeezeHz; }
    double  squeezeDepthDb() const { return m_squeezeDepthDb; }
    double  squeezeCombSpacingHz() const { return m_squeezeCombSpacingHz; }
    int     squeezeCombTeethSeen() const { return m_squeezeCombTeethSeen; }
    int     squeezeCombTeethInBandCount() const { return int(m_squeezeTeethHz.size()); }

    // Where the SQUEEZE mark is actually DRAWN, on this panel's own always-
    // positive axis -- read back by tests the way notchHzAt() is. NaN / no
    // entries when there is nothing of that kind held.
    double  squeezeBracketLowHz() const;
    double  squeezeBracketHighHz() const;
    int     squeezeToothCount() const { return int(m_squeezeTeethHz.size()); }
    double  squeezeToothHzAt(int index) const;

    // ROOFING · DIGITAL PEAK OFFSET (A1): the digital roof drawn as a band
    // on this same axis -- see DiversityFilterPanelRoof.cpp. `roofAvailable`
    // is `roofing.digital_hz > 0` (there is a roof to draw at all);
    // `roofDraggable` is `roofing.offset_max_hz > 0` (there is a handle to
    // drag) -- the two are asked separately because a roof too narrow for
    // its passband holds `offset_max_hz` at 0 and still has a band to show.
    bool    roofAvailable() const { return m_roofAvailable; }
    bool    roofDraggable() const { return m_roofMaxHz > 0.0; }
    bool    roofChecked() const { return m_roofChecked; }
    double  roofOffsetHz() const { return m_roofOffsetHz; }
    double  roofMaxHz() const { return m_roofMaxHz; }
    double  roofDigitalHz() const { return m_roofDigitalHz; }

signals:
    // A handle has been let go, and the two edges are now `lowHz`/`highHz`,
    // snapped to 10 Hz. Emitted on RELEASE, not per pixel: a set per mouse-move
    // would be dozens of gate writes for one adjustment.
    void edgesDragged(int lowHz, int highHz);

    // A double-click on the curve at `hz`. The page turns it into
    // /filter/notch?add=<hz>, which is the fastest way to kill a carrier you
    // can see: point at it, double-click.
    void notchRequested(double hz);

    // A notch mark was dragged from `fromHz` and let go at `toHz`. TWO writes
    // on the page's side, in order, because the gate has no move: the notch at
    // `fromHz` is cleared and one is added at `toHz`. Emitted only when the
    // mark actually moved -- a click that lands back where it started is not a
    // request to rebuild a filter.
    void notchMoveRequested(double fromHz, double toHz);

    // A right-click on a notch mark. /filter/notch?clear=<hz> on the page's
    // side: the gate's own parameter for taking one notch away and leaving the
    // others where they are.
    void notchRemoveRequested(double hz);

    // The pointer moved over the picture, or left it (both NaN). `db` is the
    // dB the spectrum is PLOTTED at under the pointer -- see cursorDb().
    void cursorMoved(double hz, double db);

    // A click on a mark that did not become a drag: the id of the chain stage
    // the mark belongs to. See the header comment.
    void markClicked(QString stageId);

    // Shift+click on the curve, away from the SQUEEZE bracket/teeth: place
    // (or move) the target there. `hz` is SIGNED, in the slice's own frame --
    // what the gate's squeeze=<hz> takes -- not this panel's own positive
    // axis. See DiversityFilterPanelSqueeze.cpp.
    void squeezeRequested(double hz);
    // Right-click, or Shift+click, ON the bracket or a tooth: squeeze=off.
    void squeezeReleaseRequested();

    // ROOFING · DIGITAL PEAK OFFSET (A1): the handle was let go at a new,
    // already-clamped offset. Emitted on RELEASE, the same rule an edge
    // drag keeps -- a write per pixel would be dozens of gate writes for one
    // adjustment.
    void roofOffsetDragged(int offsetHz);

protected:
    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent* ev) override;
    void mousePressEvent(QMouseEvent* ev) override;
    void mouseMoveEvent(QMouseEvent* ev) override;
    void mouseReleaseEvent(QMouseEvent* ev) override;
    void mouseDoubleClickEvent(QMouseEvent* ev) override;
    void keyPressEvent(QKeyEvent* ev) override;
    void keyReleaseEvent(QKeyEvent* ev) override;
    void leaveEvent(QEvent* ev) override;

private:
    enum class Edge { None, Low, High };

    // The axis and the gutters. Class constants rather than file ones because
    // this widget is two translation units -- the state and the input in
    // DiversityFilterPanel.cpp, the picture in DiversityFilterPanelPaint.cpp --
    // and both halves have to agree about where 0 dB is.
    //
    // 0 dB at the top is the convention every filter plot in every radio manual
    // uses; -60 dB at the bottom is where a 1023-tap sharp filter's stopband
    // already is, so a deeper floor would be sixty pixels of nothing.
    static constexpr double kTopDb = 0.0;
    static constexpr double kBottomDb = -60.0;
    // Where the pre-filter spectrum's own median is pinned on that axis:
    // fifteen dB of headroom above it before the curve's 0 dB line and fifteen
    // below it before the axis runs out, so a floor that has crept up is still
    // visibly a floor and a 40 dB station is clipped at the top rather than off
    // it. See the header comment for why the gate's own scale is not used.
    static constexpr double kFloorAxisDb = -45.0;
    // The left gutter carries the dB scale, the bottom one the Hz scale.
    static constexpr int kLeftGutter = 32;
    static constexpr int kBottomGutter = 16;
    static constexpr int kTopMargin = 6;
    static constexpr int kRightMargin = 8;

    // Plot rectangle in widget coordinates -- the widget minus the dB gutter
    // on the left and the Hz axis along the bottom.
    QRectF plotRect() const;
    double yForDb(double db) const;
    // What is on screen, as bytes: one fingerprint for the filter (everything
    // in the cached layer) and one for the spectrum. applyStatus() takes each
    // either side of its parse and repaints only what actually moved.
    QByteArray filterFingerprint() const;
    QByteArray spectrumFingerprint() const;
    // The one part of the picture that is about the SIGNAL rather than about
    // the filter, and the only thing painted live: everything else is in the
    // two cached layers this is drawn between.
    void   paintSpectrum(QPainter& p, const QRectF& r);
    // Rebuilds the cached layer at the current device pixel ratio. The only
    // thing that increments staticRebuildCount().
    void   rebuildLayer();
    void   paintLayer(QPainter& p, const QRectF& r) const;
    // The column a handle occupies, for the partial repaint a drag does.
    QRect  handleRect(double hz) const;
    // Which handle is within grab distance of `x`, or None.
    Edge   edgeAt(double x) const;
    // Which notch mark is within grab distance of `x`, or -1. Handles win: at
    // the one pixel where an edge and a notch overlap, the edge is the thing
    // an operator is far more often reaching for.
    int    notchAt(double x) const;
    // Which of the marks that cannot be dragged is under (`x`, `y`): an ANF
    // tone, the contour or APF tick along the bottom, an AUTO edge. The chain
    // stage id, or "" when none is. Handles and notches are not in it -- they
    // are the two hit tests above, and they are asked first.
    QString markAt(double x, double y) const;
    // Moves the focused/dragged edge to `hz`, snapped to 10 Hz and kept the
    // right side of its neighbour. `partial` repaints the two handle columns
    // instead of the widget -- the drag path.
    void   moveEdge(Edge edge, double hz, bool partial = false);

    // SQUEEZE (B24), defined in DiversityFilterPanelSqueeze.cpp: parses the
    // "squeeze" block and the "mode" field (for the sign flip below), draws
    // the bracket/teeth into the cached layer, and answers the two hit tests
    // the gestures need.
    void    resetSqueeze();
    void    parseSqueeze(const QJsonObject& filter);
    void    paintSqueeze(QPainter& p, const QRectF& r) const;
    // Whether (x, y) is on the bracket or a tooth -- asked by markAt() (the
    // door) and by mousePressEvent (Shift+click / right-click's release).
    bool    squeezeHit(double x) const;
    // The signed, slice-relative Hz a click at `x` asks for -- the inverse
    // of how this panel draws squeeze.hz.
    double  squeezeHzForClick(double x) const;

    // ROOFING · DIGITAL PEAK OFFSET (A1), defined in
    // DiversityFilterPanelRoof.cpp: parses `roofing` off /filter, draws the
    // band into the cached layer and the handle live (it drags), and answers
    // the one hit test the gesture needs.
    void    resetRoof();
    void    parseRoof(const QJsonObject& filter);
    void    paintRoofBand(QPainter& p, const QRectF& r) const;
    void    paintRoofHandle(QPainter& p, const QRectF& r) const;
    bool    roofHandleHit(double x) const;
    // `hz` clamped to +/- offset_max_hz -- what a drag past either end of
    // the allowed range settles at instead of past it.
    double  roofClampedHz(double hz) const;

    QVector<double> m_hz;
    QVector<double> m_db;
    double m_minHz{0.0};
    double m_maxHz{0.0};

    // The pre-filter spectrum on the same Hz grid, its own median, and the two
    // edges AUTO has chosen. Empty / NaN when the gate has sent "spectrum":
    // null, which is what it says before it has heard a block.
    QVector<double> m_specHz;
    QVector<double> m_specDb;
    double m_specFloorDb{0.0};
    double m_autoLowHz{0.0};
    double m_autoHighHz{0.0};

    bool m_available{false};
    int  m_lowHz{0};
    int  m_highHz{0};

    // hz / depth_db of every manual notch, and hz / depth_db of every tone the
    // automatic notcher has found. Drawn differently on purpose: one is a
    // filter the operator placed, the other is one that appeared.
    QVector<QPointF> m_notches;
    QVector<QPointF> m_anf;
    // Centre frequencies of the contour and audio-peaking filters, when they
    // are switched on. NaN when they are not -- an APF that is off has a
    // frequency and no business being drawn.
    double m_contourHz{0.0};
    double m_apfHz{0.0};

    // SQUEEZE (B24). m_squeezeHz/m_squeezeWidthHz/m_squeezeTeethHz are all in
    // the SLICE's own signed frame -- the gate does not abs-ify squeeze the
    // way it does low_hz/high_hz/found_hz, so this panel does it at paint and
    // hit-test time (DiversityFilterPanelSqueeze.cpp). m_squeezeLsb is which
    // way a FUTURE click's sign should go, kept even while off so a Shift+
    // click on an empty curve still asks for the right sign.
    bool    m_squeezeActive{false};    // "since" present: held or armed
    bool    m_squeezeHeld{false};
    bool    m_squeezeLsb{false};
    QString m_squeezeTarget;           // "signal" | "comb" | ""
    QString m_squeezeTool;             // "null" | "notch" | ""
    QString m_squeezeReason;
    QString m_squeezeWhy;
    double  m_squeezeHz{0.0};
    double  m_squeezeWidthHz{0.0};
    double  m_squeezeDepthDb{0.0};
    double  m_squeezeCombSpacingHz{0.0};
    int     m_squeezeCombTeethSeen{0};
    QVector<double> m_squeezeTeethHz;

    // ROOFING · DIGITAL PEAK OFFSET (A1). m_roofOffsetHz/m_roofDigitalHz/
    // m_roofMaxHz come straight off /filter's own "roofing" object -- not a
    // chain[] row -- so the band is drawn whether or not the gate's chain[]
    // carries roof_digital's checks[] at all. m_roofDrag/m_roofDragHz are
    // the ghost position while the handle is down, the same shape m_notch
    // Drag/m_notchGhostHz use for a notch.
    bool    m_roofAvailable{false};   // roofing.digital_hz > 0
    bool    m_roofChecked{false};     // roofing.offset_enabled
    double  m_roofDigitalHz{0.0};
    double  m_roofOffsetHz{0.0};      // roofing.offset_applied_hz
    double  m_roofMaxHz{0.0};         // roofing.offset_max_hz
    bool    m_roofDrag{false};
    double  m_roofDragHz{0.0};

    Edge   m_drag{Edge::None};
    Edge   m_focusEdge{Edge::Low};
    // An arrow key has moved an edge and the write is waiting for the key to
    // come up. Holding an arrow down is ONE adjustment, not forty writes.
    bool   m_keyMoved{false};
    // The notch being dragged (an index into m_notches) and where it started,
    // or -1 / NaN when none is. The mark stays drawn where the gate put it and
    // a ghost follows the pointer, so one gesture shows both ends of the move
    // and no cached layer has to be rebuilt to animate it.
    int    m_notchDrag{-1};
    double m_notchFromHz{0.0};
    double m_notchGhostHz{0.0};
    // The mark a left press landed on, held until the release says whether
    // the press was a click or the start of a drag. Where the handle was at
    // the press, for the same question.
    QString m_pressMark;
    int     m_pressLowHz{0};
    int     m_pressHighHz{0};
    // Hz under the pointer for the corner readout, or NaN when the pointer is
    // not over the widget.
    double m_cursorHz{0.0};

    // The picture, cached: everything except the background wash, the spectrum
    // area and the two handles, on a transparent pixmap at the device pixel
    // ratio. The spectrum's own polygon is cached separately because it changes
    // on its own schedule (every poll) and the layer does not.
    QPixmap  m_layer;
    QPolygonF m_specArea;
    QVector<double> m_specAxisDb;
    bool m_layersDirty{true};
    bool m_specAreaDirty{true};
    int  m_paintCount{0};
    int  m_staticRebuilds{0};
};

} // namespace AetherSDR
