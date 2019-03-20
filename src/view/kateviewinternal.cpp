/* This file is part of the KDE libraries
   Copyright (C) 2002 John Firebaugh <jfirebaugh@kde.org>
   Copyright (C) 2002 Joseph Wenninger <jowenn@kde.org>
   Copyright (C) 2002,2003 Christoph Cullmann <cullmann@kde.org>
   Copyright (C) 2002-2007 Hamish Rodda <rodda@kde.org>
   Copyright (C) 2003 Anakim Border <aborder@sources.sourceforge.net>
   Copyright (C) 2007 Mirko Stocker <me@misto.ch>
   Copyright (C) 2007 Matthew Woehlke <mw_triad@users.sourceforge.net>
   Copyright (C) 2008 Erlend Hamberg <ehamberg@gmail.com>
   Copyright (C) 2016 Sven Brauch <mail@svenbrauch.de>

   Based on:
     KWriteView : Copyright (C) 1999 Jochen Wilhelmy <digisnap@cs.tu-berlin.de>

   This library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Library General Public
   License version 2 as published by the Free Software Foundation.

   This library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Library General Public License for more details.

   You should have received a copy of the GNU Library General Public License
   along with this library; see the file COPYING.LIB.  If not, write to
   the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
   Boston, MA 02110-1301, USA.
*/

#include "kateviewinternal.h"

#include "kateview.h"
#include "kateviewhelpers.h"
#include "katehighlight.h"
#include "katebuffer.h"
#include "katerenderer.h"
#include "kateconfig.h"
#include "katelayoutcache.h"
#include "katecompletionwidget.h"
#include "spellcheck/spellingmenu.h"
#include "kateviewaccessible.h"
#include "katetextanimation.h"
#include "katemessagewidget.h"
#include "kateglobal.h"
#include "kateabstractinputmodefactory.h"
#include "kateabstractinputmode.h"
#include "katepartdebug.h"

#include <ktexteditor/movingrange.h>
#include <ktexteditor/documentcursor.h>
#include <ktexteditor/texthintinterface.h>
#include <KCursor>

#include <QMimeData>
#include <QAccessible>
#include <QClipboard>
#include <QKeyEvent>
#include <QPainter>
#include <QPixmap>
#include <QApplication>
#include <QLayout>
#include <QStyle>
#include <QToolTip>

static const bool debugPainting = false;

class ZoomEventFilter
{
public:
    ZoomEventFilter() = default;

    bool detectZoomingEvent(QWheelEvent *e, Qt::KeyboardModifiers modifier = Qt::ControlModifier)
    {
        Qt::KeyboardModifiers modState = e->modifiers();
        if (modState == modifier) {
            if (m_lastWheelEvent.isValid()) {
                const qint64 deltaT = m_lastWheelEvent.elapsed();
                // Pressing the specified modifier key within 200ms of the previous "unmodified"
                // wheelevent is not allowed to toggle on text zooming
                if (m_lastWheelEventUnmodified && deltaT < 200) {
                    m_ignoreZoom = true;
                }
                else if (deltaT > 1000) {
                    // the protection is kept active for 1s after the last wheel event
                    // TODO: this value should be tuned, preferrably by someone using
                    // Ctrl+Wheel zooming frequently.
                    m_ignoreZoom = false;
                }
            } else {
                // we can't say anything and have to assume there's nothing
                // accidental to the modifier being pressed.
                m_ignoreZoom = false;
            }
            m_lastWheelEventUnmodified = false;
            if (m_ignoreZoom) {
                // unset the modifier so the view scrollbars can handle the scroll
                // event and produce normal, not accelerated scrolling
                modState &= ~modifier;
                e->setModifiers(modState);
            }
        } else {
            // state is reset after any wheel event without the zoom modifier
            m_lastWheelEventUnmodified = true;
            m_ignoreZoom = false;
        }
        m_lastWheelEvent.start();

        // inform the caller whether this event is allowed to trigger text zooming.
        return !m_ignoreZoom && modState == modifier;
    }

protected:
    QElapsedTimer m_lastWheelEvent;
    bool m_ignoreZoom = false;
    bool m_lastWheelEventUnmodified = false;
};

KateViewInternal::KateViewInternal(KTextEditor::ViewPrivate *view)
    : QWidget(view)
    , editSessionNumber(0)
    , editIsRunning(false)
    , m_view(view)
    , m_cursors(this)
    , m_selections(this)
    , m_mouse()
    , m_possibleTripleClick(false)
    , m_completionItemExpanded(false)
    , m_bm(doc()->newMovingRange(KTextEditor::Range::invalid(), KTextEditor::MovingRange::DoNotExpand))
    , m_bmStart(doc()->newMovingRange(KTextEditor::Range::invalid(), KTextEditor::MovingRange::DoNotExpand))
    , m_bmEnd(doc()->newMovingRange(KTextEditor::Range::invalid(), KTextEditor::MovingRange::DoNotExpand))
    , m_bmLastFlashPos(doc()->newMovingCursor(KTextEditor::Cursor::invalid()))
    , m_dummy(nullptr)

    // stay on cursor will avoid that the view scroll around on press return at beginning
    , m_startPos(doc()->buffer(), KTextEditor::Cursor(0, 0), Kate::TextCursor::StayOnInsert)

    , m_visibleLineCount(0)
    , m_madeVisible(false)
    , m_shiftKeyPressed(false)
    , m_autoCenterLines(0)
    , m_minLinesVisible(0)
    , m_selChangedByUser(false)
    , m_selectAnchor(-1, -1)
    , m_layoutCache(new KateLayoutCache(renderer(), this))
    , m_cachedMaxStartPos(-1, -1)
    , m_dragScrollTimer(this)
    , m_scrollTimer(this)
    , m_cursorTimer(this)
    , m_textHintTimer(this)
    , m_textHintDelay(500)
    , m_textHintPos(-1, -1)
    , m_imPreeditRange(nullptr)
{
    QList<KateAbstractInputModeFactory *> factories = KTextEditor::EditorPrivate::self()->inputModeFactories();
    Q_FOREACH(KateAbstractInputModeFactory *factory, factories) {
        KateAbstractInputMode *m = factory->createInputMode(this);
        m_inputModes.insert(m->viewInputMode(), m);
    }
    m_currentInputMode = m_inputModes[KTextEditor::View::NormalInputMode]; // TODO: twisted, but needed

    setMinimumSize(0, 0);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setAttribute(Qt::WA_InputMethodEnabled);

    // bracket markers are only for this view and should not be printed
    m_bm->setView(m_view);
    m_bmStart->setView(m_view);
    m_bmEnd->setView(m_view);
    m_bm->setAttributeOnlyForViews(true);
    m_bmStart->setAttributeOnlyForViews(true);
    m_bmEnd->setAttributeOnlyForViews(true);

    // use z depth defined in moving ranges interface
    m_bm->setZDepth(-1000.0);
    m_bmStart->setZDepth(-1000.0);
    m_bmEnd->setZDepth(-1000.0);

    // update mark attributes
    updateBracketMarkAttributes();

    //
    // scrollbar for lines
    //
    m_lineScroll = new KateScrollBar(Qt::Vertical, this);
    m_lineScroll->show();
    m_lineScroll->setTracking(true);
    m_lineScroll->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);

    // Hijack the line scroller's controls, so we can scroll nicely for word-wrap
    connect(m_lineScroll, SIGNAL(actionTriggered(int)), SLOT(scrollAction(int)));
    connect(m_lineScroll, SIGNAL(sliderMoved(int)), SLOT(scrollLines(int)));
    connect(m_lineScroll, SIGNAL(sliderMMBMoved(int)), SLOT(scrollLines(int)));
    connect(m_lineScroll, SIGNAL(valueChanged(int)), SLOT(scrollLines(int)));

    //
    // scrollbar for columns
    //
    m_columnScroll = new QScrollBar(Qt::Horizontal, m_view);

    if (m_view->dynWordWrap()) {
        m_columnScroll->hide();
    } else {
        m_columnScroll->show();
    }

    m_columnScroll->setTracking(true);
    m_startX = 0;

    connect(m_columnScroll, SIGNAL(valueChanged(int)), SLOT(scrollColumns(int)));

    // bottom corner box
    m_dummy = new QWidget(m_view);
    m_dummy->setFixedSize(m_lineScroll->width(), m_columnScroll->sizeHint().height());
    m_dummy->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

    if (m_view->dynWordWrap()) {
        m_dummy->hide();
    } else {
        m_dummy->show();
    }

    cache()->setWrap(m_view->dynWordWrap());

    //
    // iconborder ;)
    //
    m_leftBorder = new KateIconBorder(this, m_view);
    m_leftBorder->show();

    // update view if folding ranges change
    connect(&m_view->textFolding(), SIGNAL(foldingRangesChanged()), SLOT(slotRegionVisibilityChanged()));

    m_displayCursor.setPosition(0, 0);

    setAcceptDrops(true);

    m_zoomEventFilter = new ZoomEventFilter();
    // event filter
    installEventFilter(this);

    // set initial cursor
    m_mouseCursor = Qt::IBeamCursor;
    setCursor(m_mouseCursor);

    // call mouseMoveEvent also if no mouse button is pressed
    setMouseTracking(true);

    m_dragInfo.state = diNone;

    // timers
    connect(&m_dragScrollTimer, SIGNAL(timeout()),
            this, SLOT(doDragScroll()));

    connect(&m_scrollTimer, SIGNAL(timeout()),
            this, SLOT(scrollTimeout()));

    connect(&m_cursorTimer, SIGNAL(timeout()),
            this, SLOT(cursorTimeout()));

    connect(&m_textHintTimer, SIGNAL(timeout()),
            this, SLOT(textHintTimeout()));

#ifndef QT_NO_ACCESSIBILITY
    QAccessible::installFactory(accessibleInterfaceFactory);
#endif
    connect(doc(), &KTextEditor::DocumentPrivate::textInserted, this, &KateViewInternal::documentTextInserted);
    connect(doc(), &KTextEditor::DocumentPrivate::textRemoved, this, &KateViewInternal::documentTextRemoved);

    // update is called in KTextEditor::ViewPrivate, after construction and layout is over
    // but before any other kateviewinternal call
}

KateViewInternal::~KateViewInternal()
{
    // delete text animation object here, otherwise it updates the view in its destructor
    delete m_textAnimation;

#ifndef QT_NO_ACCESSIBILITY
    QAccessible::removeFactory(accessibleInterfaceFactory);
#endif

    // kill preedit ranges
    delete m_imPreeditRange;
    qDeleteAll(m_imPreeditRangeChildren);

    qDeleteAll(m_inputModes);

    // delete bracket markers
    delete m_bm;
    delete m_bmStart;
    delete m_bmEnd;

    delete m_zoomEventFilter;
}

void KateViewInternal::prepareForDynWrapChange()
{
    // Which is the current view line?
    m_wrapChangeViewLine = cache()->displayViewLine(m_displayCursor, true);
}

void KateViewInternal::dynWrapChanged()
{
    m_dummy->setFixedSize(m_lineScroll->width(), m_columnScroll->sizeHint().height());
    if (m_view->dynWordWrap()) {
        m_columnScroll->hide();
        m_dummy->hide();

    } else {
        // column scrollbar + bottom corner box
        m_columnScroll->show();
        m_dummy->show();
    }

    cache()->setWrap(m_view->dynWordWrap());
    updateView();

    if (m_view->dynWordWrap()) {
        scrollColumns(0);
    }

    // Determine where the cursor should be to get the cursor on the same view line
    if (m_wrapChangeViewLine != -1) {
        KTextEditor::Cursor newStart = viewLineOffset(m_displayCursor, -m_wrapChangeViewLine);
        makeVisible(newStart, newStart.column(), true);

    } else {
        update();
    }
}

KTextEditor::Cursor KateViewInternal::endPos() const
{
    // Hrm, no lines laid out at all??
    if (!cache()->viewCacheLineCount()) {
        return KTextEditor::Cursor();
    }

    for (int i = qMin(linesDisplayed() - 1, cache()->viewCacheLineCount() - 1); i >= 0; i--) {
        const KateTextLayout &thisLine = cache()->viewLine(i);

        if (thisLine.line() == -1) {
            continue;
        }

        if (thisLine.virtualLine() >= m_view->textFolding().visibleLines()) {
            // Cache is too out of date
            return KTextEditor::Cursor(m_view->textFolding().visibleLines() - 1, doc()->lineLength(m_view->textFolding().visibleLineToLine(m_view->textFolding().visibleLines() - 1)));
        }

        return KTextEditor::Cursor(thisLine.virtualLine(), thisLine.wrap() ? thisLine.endCol() - 1 : thisLine.endCol());
    }

    // can happen, if view is still invisible
    return KTextEditor::Cursor();
}

int KateViewInternal::endLine() const
{
    return endPos().line();
}

KateTextLayout KateViewInternal::yToKateTextLayout(int y) const
{
    if (y < 0 || y > size().height()) {
        return KateTextLayout::invalid();
    }

    int range = y / renderer()->lineHeight();

    // lineRanges is always bigger than 0, after the initial updateView call
    if (range >= 0 && range < cache()->viewCacheLineCount()) {
        return cache()->viewLine(range);
    }

    return KateTextLayout::invalid();
}

int KateViewInternal::lineToY(int viewLine) const
{
    return (viewLine - startLine()) * renderer()->lineHeight();
}

void KateViewInternal::slotIncFontSizes(qreal step)
{
    renderer()->increaseFontSizes(step);
}

void KateViewInternal::slotDecFontSizes(qreal step)
{
    renderer()->decreaseFontSizes(step);
}

/**
 * Line is the real line number to scroll to.
 */
void KateViewInternal::scrollLines(int line)
{
    KTextEditor::Cursor newPos(line, 0);
    scrollPos(newPos);
}

// This can scroll less than one true line
void KateViewInternal::scrollViewLines(int offset)
{
    KTextEditor::Cursor c = viewLineOffset(startPos(), offset);
    scrollPos(c);

    bool blocked = m_lineScroll->blockSignals(true);
    m_lineScroll->setValue(startLine());
    m_lineScroll->blockSignals(blocked);
}

void KateViewInternal::scrollAction(int action)
{
    switch (action) {
    case QAbstractSlider::SliderSingleStepAdd:
        scrollNextLine();
        break;

    case QAbstractSlider::SliderSingleStepSub:
        scrollPrevLine();
        break;

    case QAbstractSlider::SliderPageStepAdd:
        scrollNextPage();
        break;

    case QAbstractSlider::SliderPageStepSub:
        scrollPrevPage();
        break;

    case QAbstractSlider::SliderToMinimum:
        top_home();
        break;

    case QAbstractSlider::SliderToMaximum:
        bottom_end();
        break;
    }
}

void KateViewInternal::scrollNextPage()
{
    scrollViewLines(qMax(linesDisplayed() - 1, 0));
}

void KateViewInternal::scrollPrevPage()
{
    scrollViewLines(-qMax(linesDisplayed() - 1, 0));
}

void KateViewInternal::scrollPrevLine()
{
    scrollViewLines(-1);
}

void KateViewInternal::scrollNextLine()
{
    scrollViewLines(1);
}

KTextEditor::Cursor KateViewInternal::maxStartPos(bool changed)
{
    cache()->setAcceptDirtyLayouts(true);

    if (m_cachedMaxStartPos.line() == -1 || changed) {
        KTextEditor::Cursor end(m_view->textFolding().visibleLines() - 1, doc()->lineLength(m_view->textFolding().visibleLineToLine(m_view->textFolding().visibleLines() - 1)));

        if (m_view->config()->scrollPastEnd()) {
            m_cachedMaxStartPos = viewLineOffset(end, -m_minLinesVisible);
        } else {
            m_cachedMaxStartPos = viewLineOffset(end, -(linesDisplayed() - 1));
        }
    }

    cache()->setAcceptDirtyLayouts(false);

    return m_cachedMaxStartPos;
}

// c is a virtual cursor
void KateViewInternal::scrollPos(KTextEditor::Cursor &c, bool force, bool calledExternally, bool emitSignals)
{
    if (!force && ((!m_view->dynWordWrap() && c.line() == startLine()) || c == startPos())) {
        return;
    }

    if (c.line() < 0) {
        c.setLine(0);
    }

    KTextEditor::Cursor limit = maxStartPos();
    if (c > limit) {
        c = limit;

        // Re-check we're not just scrolling to the same place
        if (!force && ((!m_view->dynWordWrap() && c.line() == startLine()) || c == startPos())) {
            return;
        }
    }

    int viewLinesScrolled = 0;

    // only calculate if this is really used and useful, could be wrong here, please recheck
    // for larger scrolls this makes 2-4 seconds difference on my xeon with dyn. word wrap on
    // try to get it really working ;)
    bool viewLinesScrolledUsable = !force
                                   && (c.line() >= startLine() - linesDisplayed() - 1)
                                   && (c.line() <= endLine() + linesDisplayed() + 1);

    if (viewLinesScrolledUsable) {
        viewLinesScrolled = cache()->displayViewLine(c);
    }

    m_startPos.setPosition(c);

    // set false here but reversed if we return to makeVisible
    m_madeVisible = false;

    if (viewLinesScrolledUsable) {
        int lines = linesDisplayed();
        if (m_view->textFolding().visibleLines() < lines) {
            KTextEditor::Cursor end(m_view->textFolding().visibleLines() - 1, doc()->lineLength(m_view->textFolding().visibleLineToLine(m_view->textFolding().visibleLines() - 1)));
            lines = qMin(linesDisplayed(), cache()->displayViewLine(end) + 1);
        }

        Q_ASSERT(lines >= 0);

        if (!calledExternally && qAbs(viewLinesScrolled) < lines &&
            // NOTE: on some machines we must update if the floating widget is visible
            //       otherwise strange painting bugs may occur during scrolling...
            !((m_view->m_messageWidgets[KTextEditor::Message::TopInView] &&
               m_view->m_messageWidgets[KTextEditor::Message::TopInView]->isVisible())
            ||(m_view->m_messageWidgets[KTextEditor::Message::CenterInView] &&
               m_view->m_messageWidgets[KTextEditor::Message::CenterInView]->isVisible())
            ||(m_view->m_messageWidgets[KTextEditor::Message::BottomInView] &&
               m_view->m_messageWidgets[KTextEditor::Message::BottomInView]->isVisible())
            )
        ) {
            updateView(false, viewLinesScrolled);

            int scrollHeight = -(viewLinesScrolled * (int)renderer()->lineHeight());

            // scroll excluding child widgets (floating notifications)
            scroll(0, scrollHeight, rect());
            m_leftBorder->scroll(0, scrollHeight);

            if (emitSignals) {
                emit m_view->verticalScrollPositionChanged(m_view, c);
                emit m_view->displayRangeChanged(m_view);
            }
            return;
        }
    }

    updateView();
    update();
    m_leftBorder->update();
    if (emitSignals) {
        emit m_view->verticalScrollPositionChanged(m_view, c);
        emit m_view->displayRangeChanged(m_view);
    }
}

void KateViewInternal::scrollColumns(int x)
{
    if (x < 0) {
        x = 0;
    }

    if (x > m_columnScroll->maximum()) {
        x = m_columnScroll->maximum();
    }

    if (x == m_startX) {
        return;
    }

    int dx = m_startX - x;
    m_startX = x;

    if (qAbs(dx) < width()) {
        // scroll excluding child widgets (floating notifications)
        scroll(dx, 0, rect());
    } else {
        update();
    }

    emit m_view->horizontalScrollPositionChanged(m_view);
    emit m_view->displayRangeChanged(m_view);

    bool blocked = m_columnScroll->blockSignals(true);
    m_columnScroll->setValue(m_startX);
    m_columnScroll->blockSignals(blocked);
}

// If changed is true, the lines that have been set dirty have been updated.
void KateViewInternal::updateView(bool changed, int viewLinesScrolled)
{
    doUpdateView(changed, viewLinesScrolled);

    if (changed) {
        updateDirty();
    }
}

void KateViewInternal::doUpdateView(bool changed, int viewLinesScrolled)
{
    if (!isVisible() && !viewLinesScrolled && !changed) {
        return;    //When this view is not visible, don't do anything
    }

    bool blocked = m_lineScroll->blockSignals(true);

    if (width() != cache()->viewWidth()) {
        cache()->setViewWidth(width());
        changed = true;
    }

    /* It was observed that height() could be negative here --
       when the main Kate view has 0 as size (during creation),
       and there frame arount KateViewInternal.  In which
       case we'd set the view cache to 0 (or less!) lines, and
       start allocating huge chunks of data, later. */
    int newSize = (qMax(0, height()) / renderer()->lineHeight()) + 1;
    cache()->updateViewCache(startPos(), newSize, viewLinesScrolled);
    m_visibleLineCount = newSize;

    KTextEditor::Cursor maxStart = maxStartPos(changed);
    int maxLineScrollRange = maxStart.line();
    if (m_view->dynWordWrap() && maxStart.column() != 0) {
        maxLineScrollRange++;
    }
    m_lineScroll->setRange(0, maxLineScrollRange);

    m_lineScroll->setValue(startPos().line());
    m_lineScroll->setSingleStep(1);
    m_lineScroll->setPageStep(qMax(0, height()) / renderer()->lineHeight());
    m_lineScroll->blockSignals(blocked);

    KateViewConfig::ScrollbarMode show_scrollbars = static_cast<KateViewConfig::ScrollbarMode>(view()->config()->showScrollbars());

    bool visible = ((show_scrollbars == KateViewConfig::AlwaysOn) ||
                    ((show_scrollbars == KateViewConfig::ShowWhenNeeded) && (maxLineScrollRange != 0)));
    bool visible_dummy = visible;

    m_lineScroll->setVisible(visible);

    if (!m_view->dynWordWrap()) {
        int max = maxLen(startLine()) - width();
        if (max < 0) {
            max = 0;
        }

        // if we lose the ability to scroll horizontally, move view to the far-left
        if (max == 0) {
            scrollColumns(0);
        }

        blocked = m_columnScroll->blockSignals(true);

        // disable scrollbar
        m_columnScroll->setDisabled(max == 0);

        visible = ((show_scrollbars == KateViewConfig::AlwaysOn) ||
                   ((show_scrollbars == KateViewConfig::ShowWhenNeeded) && (max != 0)));
        visible_dummy &= visible;
        m_columnScroll->setVisible(visible);

        m_columnScroll->setRange(0, max + (renderer()->spaceWidth() / 2)); // Add some space for the caret at EOL

        m_columnScroll->setValue(m_startX);

        // Approximate linescroll
        m_columnScroll->setSingleStep(renderer()->config()->fontMetrics().width(QLatin1Char('a')));
        m_columnScroll->setPageStep(width());

        m_columnScroll->blockSignals(blocked);
    } else {
        visible_dummy = false;
    }

    m_dummy->setVisible(visible_dummy);
}

/**
 * this function ensures a certain location is visible on the screen.
 * if endCol is -1, ignore making the columns visible.
 */
void KateViewInternal::makeVisible(const KTextEditor::Cursor &c, int endCol, bool force, bool center, bool calledExternally)
{
    //qCDebug(LOG_KTE) << "MakeVisible start " << startPos() << " end " << endPos() << " -> request: " << c;// , new start [" << scroll.line << "," << scroll.col << "] lines " << (linesDisplayed() - 1) << " height " << height();
    // if the line is in a folded region, unfold all the way up
    //if ( doc()->foldingTree()->findNodeForLine( c.line )->visible )
    //  qCDebug(LOG_KTE)<<"line ("<<c.line<<") should be visible";

    if (force) {
        KTextEditor::Cursor scroll = c;
        scrollPos(scroll, force, calledExternally);
    } else if (center && (c < startPos() || c > endPos())) {
        KTextEditor::Cursor scroll = viewLineOffset(c, -int(linesDisplayed()) / 2);
        scrollPos(scroll, false, calledExternally);
    } else if (c > viewLineOffset(startPos(), linesDisplayed() - m_minLinesVisible - 1)) {
        KTextEditor::Cursor scroll = viewLineOffset(c, -(linesDisplayed() - m_minLinesVisible - 1));
        scrollPos(scroll, false, calledExternally);
    } else if (c < viewLineOffset(startPos(), m_minLinesVisible)) {
        KTextEditor::Cursor scroll = viewLineOffset(c, -m_minLinesVisible);
        scrollPos(scroll, false, calledExternally);
    } else {
        // Check to see that we're not showing blank lines
        KTextEditor::Cursor max = maxStartPos();
        if (startPos() > max) {
            scrollPos(max, max.column(), calledExternally);
        }
    }

    if (!m_view->dynWordWrap() && (endCol != -1 || m_view->wrapCursor())) {
        KTextEditor::Cursor rc = toRealCursor(c);
        int sX = renderer()->cursorToX(cache()->textLayout(rc), rc, !m_view->wrapCursor());

        int sXborder = sX - 8;
        if (sXborder < 0) {
            sXborder = 0;
        }

        if (sX < m_startX) {
            scrollColumns(sXborder);
        } else if (sX > m_startX + width()) {
            scrollColumns(sX - width() + 8);
        }
    }

    m_madeVisible = !force;

#ifndef QT_NO_ACCESSIBILITY
    // FIXME -- is this needed?
//    QAccessible::updateAccessibility(this, KateCursorAccessible::ChildId, QAccessible::Focus);
#endif

}

void KateViewInternal::slotRegionVisibilityChanged()
{
    qCDebug(LOG_KTE);
    cache()->clear();

    m_cachedMaxStartPos.setLine(-1);
    KTextEditor::Cursor max = maxStartPos();
    if (startPos() > max) {
        scrollPos(max, false, false, false /* don't emit signals! */);
    }

    // if text was folded: make sure the cursor is on a visible line
    qint64 foldedRangeId = -1;
    if (!m_view->textFolding().isLineVisible(primaryCursor().line(), &foldedRangeId)) {
        KTextEditor::Range foldingRange = m_view->textFolding().foldingRange(foldedRangeId);
        Q_ASSERT(foldingRange.start().isValid());

        // set cursor to start of folding region
        cursors()->setPrimaryCursor(foldingRange.start(), true);
    } else {
        // force an update of the cursor, since otherwise the m_displayCursor
        // line may be below the total amount of visible lines.
        cursors()->setPrimaryCursor(primaryCursor(), true);
    }

    updateView();
    update();
    m_leftBorder->update();

    // emit signals here, scrollPos has this disabled, to ensure we do this after all stuff is updated!
    emit m_view->verticalScrollPositionChanged(m_view, max);
    emit m_view->displayRangeChanged(m_view);
}

void KateViewInternal::slotRegionBeginEndAddedRemoved(unsigned int)
{
    qCDebug(LOG_KTE);
    // FIXME: performance problem
    m_leftBorder->update();
}

void KateViewInternal::showEvent(QShowEvent *e)
{
    updateView();

    QWidget::showEvent(e);
}

int KateViewInternal::linesDisplayed() const
{
    int h = height();

    // catch zero heights, even if should not happen
    int fh = qMax(1, renderer()->lineHeight());

    // default to 1, there is always one line around....
    // too many places calc with linesDisplayed() - 1
    return qMax(1, (h - (h % fh)) / fh);
}

QPoint KateViewInternal::cursorToCoordinate(const KTextEditor::Cursor &cursor, bool realCursor, bool includeBorder) const
{
    if (cursor.line() >= doc()->lines()) {
        return QPoint(-1, -1);
    }

    int viewLine = cache()->displayViewLine(realCursor ? toVirtualCursor(cursor) : cursor, true);

    if (viewLine < 0 || viewLine >= cache()->viewCacheLineCount()) {
        return QPoint(-1, -1);
    }

    const int y = (int)viewLine * renderer()->lineHeight();

    KateTextLayout layout = cache()->viewLine(viewLine);

    if (cursor.column() > doc()->lineLength(cursor.line())) {
        return QPoint(-1, -1);
    }

    int x = 0;

    // only set x value if we have a valid layout (bug #171027)
    if (layout.isValid()) {
        x = (int)layout.lineLayout().cursorToX(cursor.column());
    }
//  else
//    qCDebug(LOG_KTE) << "Invalid Layout";

    if (includeBorder) {
        x += m_leftBorder->width();
    }

    x -= startX();

    return QPoint(x, y);
}

QPoint KateViewInternal::cursorCoordinates(bool includeBorder) const
{
    return cursorToCoordinate(m_displayCursor, false, includeBorder);
}

KTextEditor::Cursor KateViewInternal::findMatchingBracket()
{
    KTextEditor::Cursor c;

    if (!m_bm->toRange().isValid()) {
        return KTextEditor::Cursor::invalid();
    }

    Q_ASSERT(m_bmEnd->toRange().isValid());
    Q_ASSERT(m_bmStart->toRange().isValid());

    auto cursor = primaryCursor();
    if (m_bmStart->toRange().contains(cursor) || m_bmStart->end() == cursor) {
        c = m_bmEnd->end();
    } else if (m_bmEnd->toRange().contains(cursor) || m_bmEnd->end() == cursor) {
        c = m_bmStart->start();
    } else {
        // should never happen: a range exists, but the cursor position is
        // neither at the start nor at the end...
        return KTextEditor::Cursor::invalid();
    }

    return c;
}

void KateViewInternal::doReturn()
{
    doc()->newLine(m_view);
    m_leftBorder->updateForCursorLineChange();
    updateView();
}

void KateViewInternal::doSmartNewline()
{
    int ln = primaryCursor().line();
    Kate::TextLine line = doc()->kateTextLine(ln);
    int col = qMin(primaryCursor().column(), line->firstChar());
    if (col != -1) {
        while (line->length() > col &&
                !(line->at(col).isLetterOrNumber() || line->at(col) == QLatin1Char('_')) &&
                col < primaryCursor().column()) {
            ++col;
        }
    } else {
        col = line->length(); // stay indented
    }
    doc()->editStart();
    doc()->editWrapLine(ln, primaryCursor().column());
    doc()->insertText(KTextEditor::Cursor(ln + 1, 0), line->string(0, col));
    doc()->editEnd();

    updateView();
}

void KateViewInternal::doDelete()
{
    auto cursors = view()->allCursors();
    KTextEditor::Document::EditingTransaction t(doc());
    bool hadSelection = view()->selection();
    Q_FOREACH ( const auto& cursor, cursors ) {
        doc()->del(m_view, cursor);
            if (hadSelection) {
            // if we had a selection, only call del() once.
            break;
        }
    }
}

void KateViewInternal::doBackspace()
{
    auto cursors = view()->allCursors();
    KTextEditor::Document::EditingTransaction t(doc());
    bool hadSelection = view()->selection();
    Q_FOREACH ( const auto& cursor, cursors ) {
        doc()->backspace(m_view, cursor);
        if (hadSelection) {
            // if we had a selection, only call backspace() once.
            break;
        }
    }
}

void KateViewInternal::doTabulator()
{
    doc()->insertTab(m_view, primaryCursor());
}

void KateViewInternal::doTranspose()
{
    doc()->transpose(primaryCursor());
}

void KateViewInternal::doDeletePrevWord()
{
    doc()->editStart();
    wordPrev(true);
    KTextEditor::Range selection = m_view->selectionRange();
    m_view->removeSelectedText();
    doc()->editEnd();
    tagRange(selection, true);
    updateDirty();
}

void KateViewInternal::doDeleteNextWord()
{
    doc()->editStart();
    wordNext(true);
    KTextEditor::Range selection = m_view->selectionRange();
    m_view->removeSelectedText();
    doc()->editEnd();
    tagRange(selection, true);
    updateDirty();
}

void KateViewInternal::clearSelectionUnless(bool sel) {
    if ( ! sel ) {
        selections()->clearSelectionIfNotPersistent();
    }
}

void KateViewInternal::cursorPrevChar(bool sel)
{
    clearSelectionUnless(sel);
    cursors()->moveCursorsLeft(sel);
}

void KateViewInternal::cursorNextChar(bool sel)
{
    clearSelectionUnless(sel);
    cursors()->moveCursorsRight(sel);
}

void KateViewInternal::wordPrev(bool sel)
{
    clearSelectionUnless(sel);
    cursors()->moveCursorsWordPrevious(sel);
}

void KateViewInternal::wordNext(bool sel)
{
    clearSelectionUnless(sel);
    cursors()->moveCursorsWordNext(sel);
}

void KateViewInternal::home(bool sel)
{
    clearSelectionUnless(sel);
    cursors()->moveCursorsStartOfLine(sel);
}

void KateViewInternal::end(bool sel)
{
    clearSelectionUnless(sel);
    cursors()->moveCursorsEndOfLine(sel);
}

KateTextLayout KateViewInternal::currentLayout(const KTextEditor::Cursor& cursor) const
{
    return cache()->textLayout(cursor);
}

KateTextLayout KateViewInternal::previousLayout(const KTextEditor::Cursor& cursor) const
{
    int currentViewLine = cache()->viewLine(cursor);

    if (currentViewLine) {
        return cache()->textLayout(cursor.line(), currentViewLine - 1);
    } else {
        return cache()->textLayout(m_view->textFolding().visibleLineToLine(toVirtualCursor(cursor).line() - 1), -1);
    }
}

KateTextLayout KateViewInternal::nextLayout(const KTextEditor::Cursor& cursor) const
{
    int currentViewLine = cache()->viewLine(cursor) + 1;

    if (currentViewLine >= cache()->line(cursor.line())->viewLineCount()) {
        currentViewLine = 0;
        return cache()->textLayout(m_view->textFolding().visibleLineToLine(toVirtualCursor(cursor).line() + 1), currentViewLine);
    } else {
        return cache()->textLayout(cursor.line(), currentViewLine);
    }
}

/*
 * This returns the cursor which is offset by (offset) view lines.
 * This is the main function which is called by code not specifically dealing with word-wrap.
 * The opposite conversion (cursor to offset) can be done with cache()->displayViewLine().
 *
 * The cursors involved are virtual cursors (ie. equivalent to m_displayCursor)
 */

KTextEditor::Cursor KateViewInternal::viewLineOffset(const KTextEditor::Cursor &virtualCursor, int offset)
{
    if (!m_view->dynWordWrap()) {
        KTextEditor::Cursor ret(qMin((int)m_view->textFolding().visibleLines() - 1, virtualCursor.line() + offset), 0);

        if (ret.line() < 0) {
            ret.setLine(0);
        }

        return ret;
    }

    KTextEditor::Cursor realCursor = virtualCursor;
    realCursor.setLine(m_view->textFolding().visibleLineToLine(m_view->textFolding().lineToVisibleLine(virtualCursor.line())));

    int cursorViewLine = cache()->viewLine(realCursor);

    int currentOffset = 0;
    int virtualLine = 0;

    bool forwards = (offset > 0) ? true : false;

    if (forwards) {
        currentOffset = cache()->lastViewLine(realCursor.line()) - cursorViewLine;
        if (offset <= currentOffset) {
            // the answer is on the same line
            KateTextLayout thisLine = cache()->textLayout(realCursor.line(), cursorViewLine + offset);
            Q_ASSERT(thisLine.virtualLine() == (int) m_view->textFolding().lineToVisibleLine(virtualCursor.line()));
            return KTextEditor::Cursor(virtualCursor.line(), thisLine.startCol());
        }

        virtualLine = virtualCursor.line() + 1;

    } else {
        offset = -offset;
        currentOffset = cursorViewLine;
        if (offset <= currentOffset) {
            // the answer is on the same line
            KateTextLayout thisLine = cache()->textLayout(realCursor.line(), cursorViewLine - offset);
            Q_ASSERT(thisLine.virtualLine() == (int) m_view->textFolding().lineToVisibleLine(virtualCursor.line()));
            return KTextEditor::Cursor(virtualCursor.line(), thisLine.startCol());
        }

        virtualLine = virtualCursor.line() - 1;
    }

    currentOffset++;

    while (virtualLine >= 0 && virtualLine < (int)m_view->textFolding().visibleLines()) {
        int realLine = m_view->textFolding().visibleLineToLine(virtualLine);
        KateLineLayoutPtr thisLine = cache()->line(realLine, virtualLine);
        if (!thisLine) {
            break;
        }

        for (int i = 0; i < thisLine->viewLineCount(); ++i) {
            if (offset == currentOffset) {
                KateTextLayout thisViewLine = thisLine->viewLine(i);

                if (!forwards) {
                    // We actually want it the other way around
                    int requiredViewLine = cache()->lastViewLine(realLine) - thisViewLine.viewLine();
                    if (requiredViewLine != thisViewLine.viewLine()) {
                        thisViewLine = thisLine->viewLine(requiredViewLine);
                    }
                }

                KTextEditor::Cursor ret(virtualLine, thisViewLine.startCol());

                return ret;
            }

            currentOffset++;
        }

        if (forwards) {
            virtualLine++;
        } else {
            virtualLine--;
        }
    }

    // Looks like we were asked for something a bit exotic.
    // Return the max/min valid position.
    if (forwards) {
        return KTextEditor::Cursor(m_view->textFolding().visibleLines() - 1, doc()->lineLength(m_view->textFolding().visibleLineToLine(m_view->textFolding().visibleLines() - 1)));
    } else {
        return KTextEditor::Cursor(0, 0);
    }
}

int KateViewInternal::lineMaxCursorX(const KateTextLayout &range)
{
    if (!m_view->wrapCursor() && !range.wrap()) {
        return INT_MAX;
    }

    int maxX = range.endX();

    if (maxX && range.wrap()) {
        QChar lastCharInLine = doc()->kateTextLine(range.line())->at(range.endCol() - 1);
        maxX -= renderer()->config()->fontMetrics().width(lastCharInLine);
    }

    return maxX;
}

int KateViewInternal::lineMaxCol(const KateTextLayout &range)
{
    int maxCol = range.endCol();

    if (maxCol && range.wrap()) {
        maxCol--;
    }

    return maxCol;
}

void KateViewInternal::cursorUp(bool sel)
{
    if (!sel && m_view->completionWidget()->isCompletionActive()) {
        m_view->completionWidget()->cursorUp();
        return;
    }

    cursors()->moveCursorsUp(sel);
}

void KateViewInternal::cursorDown(bool sel)
{
    if (!sel && m_view->completionWidget()->isCompletionActive()) {
        m_view->completionWidget()->cursorDown();
        return;
    }

    cursors()->moveCursorsDown(sel);
}

void KateViewInternal::cursorToMatchingBracket(bool sel)
{
    KTextEditor::Cursor c = findMatchingBracket();

    if (c.isValid()) {
        updateSelection(c, sel);
        cursors()->setPrimaryCursor(c);
    }
}

void KateViewInternal::topOfView(bool sel)
{
    KTextEditor::Cursor c = viewLineOffset(startPos(), m_minLinesVisible);
    updateSelection(toRealCursor(c), sel);
    cursors()->setPrimaryCursor(toRealCursor(c));
}

void KateViewInternal::bottomOfView(bool sel)
{
    KTextEditor::Cursor c = viewLineOffset(endPos(), -m_minLinesVisible);
    updateSelection(toRealCursor(c), sel);
    cursors()->setPrimaryCursor(toRealCursor(c));
}

// lines is the offset to scroll by
void KateViewInternal::scrollLines(int lines, bool sel)
{
    KTextEditor::Cursor c = viewLineOffset(m_displayCursor, lines);

    // Fix the virtual cursor -> real cursor
    c.setLine(m_view->textFolding().visibleLineToLine(c.line()));

    // how far do we move?
    auto moveLines = c.line() - primaryCursor().line();
    cursors()->moveCursorsDown(sel, moveLines); // handles negative values
}

// This is a bit misleading... it's asking for the view to be scrolled, not the cursor
void KateViewInternal::scrollUp()
{
    KTextEditor::Cursor newPos = viewLineOffset(startPos(), -1);
    scrollPos(newPos);
}

void KateViewInternal::scrollDown()
{
    KTextEditor::Cursor newPos = viewLineOffset(startPos(), 1);
    scrollPos(newPos);
}

void KateViewInternal::setAutoCenterLines(int viewLines, bool updateView)
{
    m_autoCenterLines = viewLines;
    m_minLinesVisible = qMin(int((linesDisplayed() - 1) / 2), m_autoCenterLines);
    if (updateView) {
        KateViewInternal::updateView();
    }
}

void KateViewInternal::pageUp(bool sel, bool half)
{
    if (m_view->isCompletionActive()) {
        m_view->completionWidget()->pageUp();
        return;
    }

    bool atTop = startPos().atStartOfDocument();

    // Adjust for an auto-centering cursor
    int lineadj = m_minLinesVisible;

    int linesToScroll;
    if (! half) {
        linesToScroll = -qMax((linesDisplayed() - 1) - lineadj, 0);
    } else {
        linesToScroll = -qMax((linesDisplayed() / 2 - 1) - lineadj, 0);
    }
    qDebug() << "scroll by:" << linesToScroll;

    if (!doc()->pageUpDownMovesCursor() && !atTop) {
        KTextEditor::Cursor newStartPos = viewLineOffset(startPos(), linesToScroll - 1);
        scrollPos(newStartPos);

        cursors()->moveCursorsDown(sel, linesToScroll - 1);
    } else {
        scrollLines(linesToScroll, sel);
    }
}

void KateViewInternal::pageDown(bool sel, bool half)
{
    if (m_view->isCompletionActive()) {
        m_view->completionWidget()->pageDown();
        return;
    }

    bool atEnd = startPos() >= m_cachedMaxStartPos;

    // Adjust for an auto-centering cursor
    int lineadj = m_minLinesVisible;

    int linesToScroll;
    if (! half) {
        linesToScroll = qMax((linesDisplayed() - 1) - lineadj, 0);
    } else {
        linesToScroll = qMax((linesDisplayed() / 2 - 1) - lineadj, 0);
    }
    qDebug() << "scroll by:" << linesToScroll;

    if (!doc()->pageUpDownMovesCursor() && !atEnd) {
        KTextEditor::Cursor newStartPos = viewLineOffset(startPos(), linesToScroll + 1);
        scrollPos(newStartPos);

        cursors()->moveCursorsDown(sel, linesToScroll + 1);
    } else {
        scrollLines(linesToScroll, sel);
    }
}

int KateViewInternal::maxLen(int startLine)
{
    Q_ASSERT(!m_view->dynWordWrap());

    int displayLines = (m_view->height() / renderer()->lineHeight()) + 1;

    int maxLen = 0;

    for (int z = 0; z < displayLines; z++) {
        int virtualLine = startLine + z;

        if (virtualLine < 0 || virtualLine >= (int)m_view->textFolding().visibleLines()) {
            break;
        }

        maxLen = qMax(maxLen, cache()->line(m_view->textFolding().visibleLineToLine(virtualLine))->width());
    }

    return maxLen;
}

bool KateViewInternal::columnScrollingPossible()
{
    return !m_view->dynWordWrap() && m_columnScroll->isEnabled() && (m_columnScroll->maximum() > 0);
}

bool KateViewInternal::lineScrollingPossible()
{
    return m_lineScroll->minimum() != m_lineScroll->maximum();
}

void KateViewInternal::top_home(bool sel)
{
    if (m_view->isCompletionActive()) {
        m_view->completionWidget()->top();
        return;
    }

    cursors()->moveCursorsTopHome(sel);
}

void KateViewInternal::bottom_end(bool sel)
{
    if (m_view->isCompletionActive()) {
        m_view->completionWidget()->bottom();
        return;
    }

    cursors()->moveCursorsBottomEnd(sel);
}

void KateViewInternal::updateSelection(const KTextEditor::Cursor &_newCursor, bool keepSel)
{
    /**
    KTextEditor::Cursor newCursor = _newCursor;
    if (keepSel) {
        if (!m_view->selection() || (m_selectAnchor.line() == -1)
                //don't kill the selection if we have a persistent selection and
                //the cursor is inside or at the boundaries of the selected area
                || (m_view->config()->persistentSelection()
                    && !(m_view->selectionRange().contains(primaryCursor())
                         || m_view->selectionRange().boundaryAtCursor(primaryCursor())))) {
            m_selectAnchor = primaryCursor();
            setSelection(KTextEditor::Range(primaryCursor(), newCursor));
        } else {
            bool doSelect = true;
            switch (m_selectionMode) {
            case Word: {
                // Restore selStartCached if needed. It gets nuked by
                // viewSelectionChanged if we drag the selection into non-existence,
                // which can legitimately happen if a shift+DC selection is unable to
                // set a "proper" (i.e. non-empty) cached selection, e.g. because the
                // start was on something that isn't a word. Word select mode relies
                // on the cached selection being set properly, even if it is empty
                // (i.e. selStartCached == selEndCached).
                if (!m_selectionCached.isValid()) {
                    m_selectionCached.setStart(m_selectionCached.end());
                }

                int c;
                if (newCursor > m_selectionCached.start()) {
                    m_selectAnchor = m_selectionCached.start();

                    Kate::TextLine l = doc()->kateTextLine(newCursor.line());

                    c = newCursor.column();
                    if (c > 0 && doc()->highlight()->isInWord(l->at(c - 1))) {
                        for (; c < l->length(); c++)
                            if (!doc()->highlight()->isInWord(l->at(c))) {
                                break;
                            }
                    }

                    newCursor.setColumn(c);
                } else if (newCursor < m_selectionCached.start()) {
                    m_selectAnchor = m_selectionCached.end();

                    Kate::TextLine l = doc()->kateTextLine(newCursor.line());

                    c = newCursor.column();
                    if (c > 0 && c < doc()->lineLength(newCursor.line())
                            && doc()->highlight()->isInWord(l->at(c))
                            && doc()->highlight()->isInWord(l->at(c - 1))) {
                        for (c -= 2; c >= 0; c--)
                            if (!doc()->highlight()->isInWord(l->at(c))) {
                                break;
                            }
                        newCursor.setColumn(c + 1);
                    }
                } else {
                    doSelect = false;
                }

            }
            break;
            case Line:
                if (!m_selectionCached.isValid()) {
                    m_selectionCached = KTextEditor::Range(endLine(), 0, endLine(), 0);
                }
                if (newCursor.line() > m_selectionCached.start().line()) {
                    if (newCursor.line() + 1 >= doc()->lines()) {
                        newCursor.setColumn(doc()->line(newCursor.line()).length());
                    } else {
                        newCursor.setPosition(newCursor.line() + 1, 0);
                    }
                    // Grow to include the entire line
                    m_selectAnchor = m_selectionCached.start();
                    m_selectAnchor.setColumn(0);
                } else if (newCursor.line() < m_selectionCached.start().line()) {
                    newCursor.setColumn(0);
                    // Grow to include entire line
                    m_selectAnchor = m_selectionCached.end();
                    if (m_selectAnchor.column() > 0) {
                        if (m_selectAnchor.line() + 1 >= doc()->lines()) {
                            m_selectAnchor.setColumn(doc()->line(newCursor.line()).length());
                        } else {
                            m_selectAnchor.setPosition(m_selectAnchor.line() + 1, 0);
                        }
                    }
                } else { // same line, ignore
                    doSelect = false;
                }
                break;
            case Mouse: {
                if (!m_selectionCached.isValid()) {
                    break;
                }

                if (newCursor > m_selectionCached.end()) {
                    m_selectAnchor = m_selectionCached.start();
                } else if (newCursor < m_selectionCached.start()) {
                    m_selectAnchor = m_selectionCached.end();
                } else {
                    doSelect = false;
                }
            }
            break;
            default:;
            }

            if (doSelect) {
                setSelection(KTextEditor::Range(m_selectAnchor, newCursor));
            } else if (m_selectionCached.isValid()) { // we have a cached selection, so we restore that
                setSelection(m_selectionCached);
            }
        }

        m_selChangedByUser = true;
    } else if (!m_view->config()->persistentSelection()) {
        m_view->clearSelection();

        m_selectionCached = KTextEditor::Range::invalid();
        m_selectAnchor = KTextEditor::Cursor::invalid();
    }

    **/
#ifndef QT_NO_ACCESSIBILITY
//    FIXME KF5
//    QAccessibleTextSelectionEvent ev(this, /* selection start, selection end*/);
//    QAccessible::updateAccessibility(&ev);
#endif
}

void KateViewInternal::setSelection(const KTextEditor::Range &range)
{
    m_view->setSelection(range);
}

void KateViewInternal::moveCursorToSelectionEdge()
{
    if (!m_view->selection()) {
        return;
    }

    int tmp = m_minLinesVisible;
    m_minLinesVisible = 0;

    if (m_view->selectionRange().start() < m_selectAnchor) {
        cursors()->setPrimaryCursorWithoutSelection(m_view->selectionRange().start());
    } else {
        cursors()->setPrimaryCursorWithoutSelection(m_view->selectionRange().end());
    }

    m_minLinesVisible = tmp;
}

void KateViewInternal::updateCursorFlashTimer()
{
    if (m_cursorTimer.isActive()) {
        if (QApplication::cursorFlashTime() > 0) {
            m_cursorTimer.start(QApplication::cursorFlashTime() / 2);
        }
        renderer()->setDrawCaret(true);
    }
}

void KateViewInternal::notifyPrimaryCursorChanged(const KTextEditor::Cursor &newCursor, bool force, bool center, bool calledExternally)
{
    if (!force && (m_lastUpdatedPrimary == newCursor)) {
        m_displayCursor = toVirtualCursor(newCursor);
        if (!m_madeVisible && m_view == doc()->activeView()) {
            // unfold if required
            m_view->textFolding().ensureLineIsVisible(newCursor.line());

            makeVisible(m_displayCursor, m_displayCursor.column(), false, center, calledExternally);
        }

        return;
    }

    if (m_lastUpdatedPrimary.line() != newCursor.line()) {
        m_leftBorder->updateForCursorLineChange();
    }

    // unfold if required
    m_view->textFolding().ensureLineIsVisible(newCursor.line());

    m_displayCursor = toVirtualCursor(newCursor);
    Q_ASSERT(m_displayCursor.isValid());
    m_lastUpdatedPrimary = newCursor;

    if (m_view == doc()->activeView()) {
        makeVisible(m_displayCursor, m_displayCursor.column(), false, center, calledExternally);
    }

    updateBracketMarks();

    updateMicroFocus();

    updateCursorFlashTimer();

    cursorMoved();

    emit m_view->cursorPositionChanged(m_view, primaryCursor());
}

void KateViewInternal::updateBracketMarkAttributes()
{
    KTextEditor::Attribute::Ptr bracketFill = KTextEditor::Attribute::Ptr(new KTextEditor::Attribute());
    bracketFill->setBackground(m_view->m_renderer->config()->highlightedBracketColor());
    bracketFill->setBackgroundFillWhitespace(false);
    if (QFontInfo(renderer()->currentFont()).fixedPitch()) {
        // make font bold only for fixed fonts, otherwise text jumps around
        bracketFill->setFontBold();
    }

    m_bmStart->setAttribute(bracketFill);
    m_bmEnd->setAttribute(bracketFill);

    if (m_view->m_renderer->config()->showWholeBracketExpression()) {
        KTextEditor::Attribute::Ptr expressionFill = KTextEditor::Attribute::Ptr(new KTextEditor::Attribute());
        expressionFill->setBackground(m_view->m_renderer->config()->highlightedBracketColor());
        expressionFill->setBackgroundFillWhitespace(false);

        m_bm->setAttribute(expressionFill);
    } else {
        m_bm->setAttribute(KTextEditor::Attribute::Ptr(new KTextEditor::Attribute()));
    }
}

void KateViewInternal::updateBracketMarks()
{
    // add some limit to this, this is really endless on big files without limit
    const int maxLines = 5000;
    const KTextEditor::Range newRange = doc()->findMatchingBracket(primaryCursor(), maxLines);

    // new range valid, then set ranges to it
    if (newRange.isValid()) {
        if (m_bm->toRange() == newRange) {
            return;
        }

        // modify full range
        m_bm->setRange(newRange);

        // modify start and end ranges
        m_bmStart->setRange(KTextEditor::Range(m_bm->start(), KTextEditor::Cursor(m_bm->start().line(), m_bm->start().column() + 1)));
        m_bmEnd->setRange(KTextEditor::Range(m_bm->end(), KTextEditor::Cursor(m_bm->end().line(), m_bm->end().column() + 1)));

        // flash matching bracket
        if (!renderer()->config()->animateBracketMatching()) {
            return;
        }
        const KTextEditor::Cursor flashPos = (primaryCursor() == m_bmStart->start() || primaryCursor() == m_bmStart->end()) ? m_bmEnd->start() : m_bm->start();
        if (flashPos != m_bmLastFlashPos->toCursor()) {
            m_bmLastFlashPos->setPosition(flashPos);

            KTextEditor::Attribute::Ptr attribute = doc()->attributeAt(flashPos);
            attribute->setBackground(m_view->m_renderer->config()->highlightedBracketColor());
            attribute->setFontBold(m_bmStart->attribute()->fontBold());

            flashChar(flashPos, attribute);
        }
        return;
    }

    // new range was invalid
    m_bm->setRange(KTextEditor::Range::invalid());
    m_bmStart->setRange(KTextEditor::Range::invalid());
    m_bmEnd->setRange(KTextEditor::Range::invalid());
    m_bmLastFlashPos->setPosition(KTextEditor::Cursor::invalid());
}

bool KateViewInternal::tagLine(const KTextEditor::Cursor &virtualCursor)
{
    // FIXME may be a more efficient way for this
    if ((int)m_view->textFolding().visibleLineToLine(virtualCursor.line()) > doc()->lastLine()) {
        return false;
    }
    // End FIXME

    int viewLine = cache()->displayViewLine(virtualCursor, true);
    if (viewLine >= 0 && viewLine < cache()->viewCacheLineCount()) {
        cache()->viewLine(viewLine).setDirty();

        // tag one line more because of overlapping things like _, bug 335079
        if (viewLine+1 < cache()->viewCacheLineCount()) {
            cache()->viewLine(viewLine+1).setDirty();
        }

        m_leftBorder->update(0, lineToY(viewLine), m_leftBorder->width(), renderer()->lineHeight());
        return true;
    }
    return false;
}

bool KateViewInternal::tagLines(int start, int end, bool realLines)
{
    return tagLines(KTextEditor::Cursor(start, 0), KTextEditor::Cursor(end, -1), realLines);
}

bool KateViewInternal::tagLines(KTextEditor::Cursor start, KTextEditor::Cursor end, bool realCursors)
{
    if (realCursors) {
        cache()->relayoutLines(start.line(), end.line());

        //qCDebug(LOG_KTE)<<"realLines is true";
        start = toVirtualCursor(start);
        end = toVirtualCursor(end);

    } else {
        cache()->relayoutLines(toRealCursor(start).line(), toRealCursor(end).line());
    }

    if (end.line() < startLine()) {
        //qCDebug(LOG_KTE)<<"end<startLine";
        return false;
    }
    // Used to be > endLine(), but cache may not be valid when checking, so use a
    // less optimal but still adequate approximation (potential overestimation but minimal performance difference)
    if (start.line() > startLine() + cache()->viewCacheLineCount()) {
        //qCDebug(LOG_KTE)<<"start> endLine"<<start<<" "<<(endLine());
        return false;
    }

    cache()->updateViewCache(startPos());

    //qCDebug(LOG_KTE) << "tagLines( [" << start << "], [" << end << "] )";

    bool ret = false;

    for (int z = 0; z < cache()->viewCacheLineCount(); z++) {
        KateTextLayout &line = cache()->viewLine(z);
        if ((line.virtualLine() > start.line() || (line.virtualLine() == start.line() && line.endCol() >= start.column() && start.column() != -1)) &&
                (line.virtualLine() < end.line() || (line.virtualLine() == end.line() && (line.startCol() <= end.column() || end.column() == -1)))) {
            ret = true;
            break;
            //qCDebug(LOG_KTE) << "Tagged line " << line.line();
        }
    }

    if (!m_view->dynWordWrap()) {
        int y = lineToY(start.line());
        // FIXME is this enough for when multiple lines are deleted
        int h = (end.line() - start.line() + 2) * renderer()->lineHeight();
        if (end.line() >= m_view->textFolding().visibleLines() - 1) {
            h = height();
        }

        m_leftBorder->update(0, y, m_leftBorder->width(), h);
    } else {
        // FIXME Do we get enough good info in editRemoveText to optimize this more?
        //bool justTagged = false;
        for (int z = 0; z < cache()->viewCacheLineCount(); z++) {
            KateTextLayout &line = cache()->viewLine(z);
            if (!line.isValid() ||
                    ((line.virtualLine() > start.line() || (line.virtualLine() == start.line() && line.endCol() >= start.column() && start.column() != -1)) &&
                     (line.virtualLine() < end.line() || (line.virtualLine() == end.line() && (line.startCol() <= end.column() || end.column() == -1))))) {
                //justTagged = true;
                m_leftBorder->update(0, z * renderer()->lineHeight(), m_leftBorder->width(), m_leftBorder->height());
                break;
            }
            /*else if (justTagged)
            {
              justTagged = false;
              leftBorder->update (0, z * doc()->viewFont.fontHeight, leftBorder->width(), doc()->viewFont.fontHeight);
              break;
            }*/
        }
    }

    return ret;
}

bool KateViewInternal::tagRange(const KTextEditor::Range &range, bool realCursors)
{
    return tagLines(range.start(), range.end(), realCursors);
}

void KateViewInternal::tagAll()
{
    // clear the cache...
    cache()->clear();

    m_leftBorder->updateFont();
    m_leftBorder->update();
}

void KateViewInternal::paintCursor()
{
    Q_FOREACH ( const auto& secondary, view()->cursors()->cursors() ) {
        if (tagLine(secondary)) {
            updateDirty();
        }
    }
}

KTextEditor::Cursor KateViewInternal::pointToCursor(const QPoint& p) const
{
    KateTextLayout thisLine = yToKateTextLayout(p.y());
    KTextEditor::Cursor c;

    if (!thisLine.isValid()) { // probably user clicked below the last line -> use the last line
        thisLine = cache()->textLayout(doc()->lines() - 1, -1);
    }

    c = renderer()->xToCursor(thisLine, startX() + p.x(), !m_view->wrapCursor());

    if (c.line() < 0 || c.line() >= doc()->lines()) {
        return {};
    }

    return c;
}

// Point in content coordinates
void KateViewInternal::placeCursor(const QPoint &p, bool keepSelection, bool updateSelection, bool doSubstract)
{
    auto c = pointToCursor(p);
    m_cursorToSubstract = c;
    if ( ! c.isValid() ) {
        return;
    }

    int tmp = m_minLinesVisible;
    m_minLinesVisible = 0;
    if ( keepSelection ) {
        cursors()->setPrimaryCursorWithoutSelection(c);
    }
    else {
        cursors()->setPrimaryCursor(c, true, false, doSubstract);
    }
    m_minLinesVisible = tmp;

    if (updateSelection && keepSelection) {
        moveCursorToSelectionEdge();
    }
}

// Point in content coordinates
bool KateViewInternal::isTargetSelected(const QPoint &p)
{
    const KateTextLayout &thisLine = yToKateTextLayout(p.y());
    if (!thisLine.isValid()) {
        return false;
    }

    return m_view->cursorSelected(renderer()->xToCursor(thisLine, startX() + p.x(), !m_view->wrapCursor()));
}

//BEGIN EVENT HANDLING STUFF

bool KateViewInternal::eventFilter(QObject *obj, QEvent *e)
{

    switch (e->type()) {
    case QEvent::ChildAdded:
    case QEvent::ChildRemoved: {
        QChildEvent *c = static_cast<QChildEvent *>(e);
        if (c->added()) {
            c->child()->installEventFilter(this);
            /*foreach (QWidget* child, c->child()->findChildren<QWidget*>())
              child->installEventFilter(this);*/

        } else if (c->removed()) {
            c->child()->removeEventFilter(this);

            /*foreach (QWidget* child, c->child()->findChildren<QWidget*>())
              child->removeEventFilter(this);*/
        }
    } break;

    case QEvent::ShortcutOverride: {
        QKeyEvent *k = static_cast<QKeyEvent *>(e);

        if (k->key() == Qt::Key_Escape && k->modifiers() == Qt::NoModifier) {
            if (m_view->isCompletionActive()) {
                m_view->abortCompletion();
                k->accept();
                //qCDebug(LOG_KTE) << obj << "shortcut override" << k->key() << "aborting completion";
                return true;
            } else if (!m_view->bottomViewBar()->hiddenOrPermanent()) {
                m_view->bottomViewBar()->hideCurrentBarWidget();
                k->accept();
                //qCDebug(LOG_KTE) << obj << "shortcut override" << k->key() << "closing view bar";
                return true;
            } else if (!m_view->config()->persistentSelection() && m_view->selection()) {
                m_currentInputMode->clearSelection();
                k->accept();
                //qCDebug(LOG_KTE) << obj << "shortcut override" << k->key() << "clearing selection";
                return true;
            } else if (m_view->cursors()->hasSecondaryCursors()) {
                m_view->cursors()->clearSecondaryCursors();
                k->accept();
                return true;
            }
        }

        if (m_currentInputMode->stealKey(k)) {
            k->accept();
            return true;
        }

    } break;

    case QEvent::KeyPress: {
        QKeyEvent *k = static_cast<QKeyEvent *>(e);

        // Override all other single key shortcuts which do not use a modifier other than Shift
        if (obj == this && (!k->modifiers() || k->modifiers() == Qt::ShiftModifier)) {
            keyPressEvent(k);
            if (k->isAccepted()) {
                //qCDebug(LOG_KTE) << obj << "shortcut override" << k->key() << "using keystroke";
                return true;
            }
        }

        //qCDebug(LOG_KTE) << obj << "shortcut override" << k->key() << "ignoring";
    } break;

    case QEvent::DragMove: {
        QPoint currentPoint = ((QDragMoveEvent *) e)->pos();

        QRect doNotScrollRegion(s_scrollMargin, s_scrollMargin,
                                width() - s_scrollMargin * 2,
                                height() - s_scrollMargin * 2);

        if (!doNotScrollRegion.contains(currentPoint)) {
            startDragScroll();
            // Keep sending move events
            ((QDragMoveEvent *)e)->accept(QRect(0, 0, 0, 0));
        }

        dragMoveEvent((QDragMoveEvent *)e);
    } break;

    case QEvent::DragLeave:
        // happens only when pressing ESC while dragging
        stopDragScroll();
        break;

    default:
        break;
    }

    return QWidget::eventFilter(obj, e);
}

void KateViewInternal::keyPressEvent(QKeyEvent *e)
{
    if (e->key() == Qt::Key_Left && e->modifiers() == Qt::AltModifier) {
        m_view->emitNavigateLeft();
        e->setAccepted(true);
        return;
    }
    if (e->key() == Qt::Key_Right && e->modifiers() == Qt::AltModifier) {
        m_view->emitNavigateRight();
        e->setAccepted(true);
        return;
    }
    if (e->key() == Qt::Key_Up && e->modifiers() == Qt::AltModifier) {
        m_view->emitNavigateUp();
        e->setAccepted(true);
        return;
    }
    if (e->key() == Qt::Key_Down && e->modifiers() == Qt::AltModifier) {
        m_view->emitNavigateDown();
        e->setAccepted(true);
        return;
    }
    if (e->key() == Qt::Key_Return && e->modifiers() == Qt::AltModifier) {
        m_view->emitNavigateAccept();
        e->setAccepted(true);
        return;
    }
    if (e->key() == Qt::Key_Backspace && e->modifiers() == Qt::AltModifier) {
        m_view->emitNavigateBack();
        e->setAccepted(true);
        return;
    }

    if (e->key() == Qt::Key_Alt && m_view->completionWidget()->isCompletionActive()) {
        m_completionItemExpanded = m_view->completionWidget()->toggleExpanded(true);
        m_view->completionWidget()->resetHadNavigation();
        m_altDownTime.start();
    }

    // Note: AND'ing with <Shift> is a quick hack to fix Key_Enter
    const int key = e->key() | (e->modifiers() & Qt::ShiftModifier);

    if (m_currentInputMode->keyPress(e)) {
        return;
    }


    if (!doc()->isReadWrite()) {
        e->ignore();
        return;
    }

    if ((key == Qt::Key_Return) || (key == Qt::Key_Enter) || (key == Qt::SHIFT + Qt::Key_Return) || (key == Qt::SHIFT + Qt::Key_Enter)) {
        doReturn();
        e->accept();
        return;
    }

    if (key == Qt::Key_Backspace || key == Qt::SHIFT + Qt::Key_Backspace) {
        //m_view->backspace();
        e->accept();

        return;
    }

    if (key == Qt::Key_Tab || key == Qt::SHIFT + Qt::Key_Backtab || key == Qt::Key_Backtab) {
        if (m_view->completionWidget()->isCompletionActive()) {
            e->accept();
            m_view->completionWidget()->tab(key != Qt::Key_Tab);
            return;
        }

        if (key == Qt::Key_Tab) {
            uint tabHandling = doc()->config()->tabHandling();
            // convert tabSmart into tabInsertsTab or tabIndents:
            if (tabHandling == KateDocumentConfig::tabSmart) {
                // multiple lines selected
                if (m_view->selection() && !m_view->selectionRange().onSingleLine()) {
                    tabHandling = KateDocumentConfig::tabIndents;
                }

                // otherwise: take look at cursor position
                else {
                    // if the cursor is at or before the first non-space character
                    // or on an empty line,
                    // Tab indents, otherwise it inserts a tab character.
                    Kate::TextLine line = doc()->kateTextLine(primaryCursor().line());
                    int first = line->firstChar();
                    if (first < 0 || primaryCursor().column() <= first) {
                        tabHandling = KateDocumentConfig::tabIndents;
                    } else {
                        tabHandling = KateDocumentConfig::tabInsertsTab;
                    }
                }
            }

            if (tabHandling == KateDocumentConfig::tabInsertsTab) {
                doc()->typeChars(m_view, QStringLiteral("\t"));
            } else {
                Q_FOREACH ( const auto& cursor, m_view->allCursors() ) {
                    doc()->indent(m_view->selection() ? m_view->selectionRange() : KTextEditor::Range(cursor.line(), 0, cursor.line(), 0), 1);
                }
            }

            e->accept();

            return;
        } else if (doc()->config()->tabHandling() != KateDocumentConfig::tabInsertsTab) {
            // key == Qt::SHIFT+Qt::Key_Backtab || key == Qt::Key_Backtab
            Q_FOREACH ( const auto& cursor, m_view->allCursors() ) {
                doc()->indent(m_view->selection() ? m_view->selectionRange() : KTextEditor::Range(cursor.line(), 0, cursor.line(), 0), -1);
            }
            e->accept();

            return;
        }
    }

    if (!(e->modifiers() & Qt::ControlModifier) && !e->text().isEmpty() && doc()->typeChars(m_view, e->text())) {
        e->accept();

        return;
    }

    // allow composition of AltGr + (q|2|3) on windows
    static const int altGR = Qt::ControlModifier | Qt::AltModifier;
    if ((e->modifiers() & altGR) == altGR && !e->text().isEmpty() && doc()->typeChars(m_view, e->text())) {
        e->accept();

        return;
    }

    e->ignore();
}

void KateViewInternal::keyReleaseEvent(QKeyEvent *e)
{
    if (e->key() == Qt::Key_Alt && m_view->completionWidget()->isCompletionActive() && ((m_completionItemExpanded && (m_view->completionWidget()->hadNavigation() || m_altDownTime.elapsed() > 300)) || (!m_completionItemExpanded && !m_view->completionWidget()->hadNavigation()))) {

        m_view->completionWidget()->toggleExpanded(false, true);
    }

    if ((e->modifiers() & Qt::SHIFT) == Qt::SHIFT) {
        m_shiftKeyPressed = true;
    } else {
        if (m_shiftKeyPressed) {
            m_shiftKeyPressed = false;

            if (m_selChangedByUser) {
                if (m_view->selection()) {
                    QApplication::clipboard()->setText(m_view->selectionText(), QClipboard::Selection);
                }

                m_selChangedByUser = false;
            }
        }
    }

    e->ignore();
    return;
}

void KateViewInternal::contextMenuEvent(QContextMenuEvent *e)
{
    // try to show popup menu

    QPoint p = e->pos();

    if (e->reason() == QContextMenuEvent::Keyboard) {
        makeVisible(m_displayCursor, 0);
        p = cursorCoordinates(false);
        p.rx() -= startX();
    } else if (! m_view->selection() || m_view->config()->persistentSelection()) {
        placeCursor(e->pos());
    }

    // popup is a qguardedptr now
    if (m_view->contextMenu()) {
        m_view->spellingMenu()->setUseMouseForMisspelledRange((e->reason() == QContextMenuEvent::Mouse));
        m_view->contextMenu()->popup(mapToGlobal(p));
        e->accept();
    }
}

void KateViewInternal::mousePressEvent(QMouseEvent *e)
{
    qDebug() << "called";
    if ( e->button() == Qt::LeftButton ) {
        // request the software keyboard, if any
        if (qApp->autoSipEnabled()) {
            QStyle::RequestSoftwareInputPanel behavior = QStyle::RequestSoftwareInputPanel(style()->styleHint(QStyle::SH_RequestSoftwareInputPanel));
            if (hasFocus() || behavior == QStyle::RSIP_OnMouseClick) {
                QEvent event(QEvent::RequestSoftwareInputPanel);
                QApplication::sendEvent(this, &event);
            }
        }

        // handle cursor placement and selection
        auto newCursor = pointToCursor(e->pos());

        if (e->modifiers() & Qt::ShiftModifier) {
            auto flags = (KateMultiSelection::SelectionFlags) (KateMultiSelection::UsePrimaryCursor | KateMultiSelection::KeepSelectionRange);
            selections()->beginNewSelection(newCursor,
                                            KateMultiSelection::Character,
                                            flags);
            cursors()->setPrimaryCursorWithoutSelection(newCursor);
            Q_EMIT m_view->selectionChanged(m_view);
        }
        else {
            KateMultiSelection::SelectionMode selectionMode = KateMultiSelection::Character;
            KateMultiSelection::SelectionFlags flags = KateMultiSelection::UsePrimaryCursor;
            if ( m_possibleTripleClick ) {
                selectionMode = KateMultiSelection::Line;
            }
            if ( !m_possibleTripleClick && isTargetSelected(e->pos())) {
                m_dragInfo.state = diPending;
                m_dragInfo.start = e->pos();
            }
            else {
                if ( e->modifiers() == Qt::MetaModifier ) {
                    flags = KateMultiSelection::AddNewCursor;
                }
                else {
                    view()->cursors()->clearSecondaryCursors();
                }
                selections()->beginNewSelection(newCursor, selectionMode, flags);
                Q_EMIT m_view->selectionChanged(m_view);
            }
            m_possibleTripleClick = false;
        }
        updateCursorFlashTimer();
        e->accept();
    }
    else {
        e->ignore();
    }
}

void KateViewInternal::removeLastSelection()
{
    KateMultiCursor::CursorRepainter rep(cursors());
    selections()->removeLastSelection();
    Q_EMIT m_view->selectionChanged(m_view);
}

void KateViewInternal::mouseDoubleClickEvent(QMouseEvent *e)
{
    auto secondary = (e->modifiers() == Qt::MetaModifier);
    auto newCursor = pointToCursor(e->pos());

    switch (e->button()) {
    case Qt::LeftButton:
        selections()->beginNewSelection(newCursor, KateMultiSelection::Word,
                                        secondary ? KateMultiSelection::AddNewCursor : KateMultiSelection::UsePrimaryCursor);
        Q_EMIT m_view->selectionChanged(m_view);

#warning fixme: this weird "shift double click" feature
#warning fixme: select to matching bracket on dclick
#if 0
        if (e->modifiers() & Qt::ShiftModifier) {
            // Now select the word under the select anchor
            int cs, ce;
            Kate::TextLine l = doc()->kateTextLine(m_selectAnchor.line());

            ce = m_selectAnchor.column();
            if (ce > 0 && doc()->highlight()->isInWord(l->at(ce))) {
                for (; ce < l->length(); ce++)
                    if (!doc()->highlight()->isInWord(l->at(ce))) {
                        break;
                    }
            }

            cs = m_selectAnchor.column() - 1;
            if (cs < doc()->lineLength(m_selectAnchor.line())
                    && doc()->highlight()->isInWord(l->at(cs))) {
                for (cs--; cs >= 0; cs--)
                    if (!doc()->highlight()->isInWord(l->at(cs))) {
                        break;
                    }
            }

            // ...and keep it selected
            if (cs + 1 < ce) {
                m_selectionCached.setStart(KTextEditor::Cursor(m_selectAnchor.line(), cs + 1));
                m_selectionCached.setEnd(KTextEditor::Cursor(m_selectAnchor.line(), ce));
            } else {
                m_selectionCached.setStart(m_selectAnchor);
                m_selectionCached.setEnd(m_selectAnchor);
            }
            // Now word select to the mouse cursor
            placeCursor(e->pos(), true);
        } else {
            // first clear the selection, otherwise we run into bug #106402
            // ...and set the cursor position, for the same reason (otherwise there
            // are *other* idiosyncrasies we can't fix without reintroducing said
            // bug)
            // Parameters: don't redraw, and don't emit selectionChanged signal yet
            m_view->clearSelection(false, false);
            placeCursor(e->pos());
            m_view->selectWord(primaryCursor());
            cursorToMatchingBracket(true);

            if (m_view->selection()) {
                m_selectAnchor = m_view->selectionRange().start();
                m_selectionCached = m_view->selectionRange();
            } else {
                m_selectAnchor = primaryCursor();
                m_selectionCached = KTextEditor::Range(primaryCursor(), primaryCursor());
            }
        }
#endif

        if (m_view->selection()) {
#if !defined(Q_OS_OSX)
            QApplication::clipboard()->setText(m_view->selectionText(), QClipboard::Selection);
#endif
        }

        m_possibleTripleClick = true;
        QTimer::singleShot(QApplication::doubleClickInterval(), this, SLOT(tripleClickTimeout()));

        m_scrollX = 0;
        m_scrollY = 0;

        m_scrollTimer.start(50);

        e->accept();
        break;

    default:
        e->ignore();
        break;
    }
}

void KateViewInternal::tripleClickTimeout()
{
    m_possibleTripleClick = false;
}

void KateViewInternal::beginSelectLine(const QPoint &pos)
{
    placeCursor(pos);
    m_possibleTripleClick = true; // set so subsequent mousePressEvent will select line
}

void KateViewInternal::mouseReleaseEvent(QMouseEvent *e)
{
    switch (e->button()) {
    case Qt::LeftButton:
        if ( selections()->currentlySelecting() ) {
            selections()->finishNewSelection();
            Q_EMIT m_view->selectionChanged(m_view);
            updateCursorFlashTimer();
        }

        if (m_selChangedByUser) {
            if (m_view->selection()) {
                QApplication::clipboard()->setText(m_view->selectionText(), QClipboard::Selection);
            }
            m_selChangedByUser = false;
        }

        if (m_dragInfo.state == diPending) {
            // I don't know what is the best method to transmit the doSubstract value
            // Maybe I should do like m_cursorToSubstract
            bool doSubstract = (bool)(e->modifiers() & Qt::MetaModifier);
            placeCursor(e->pos(), e->modifiers() & Qt::ShiftModifier, false, doSubstract);
            Q_EMIT m_view->selectionChanged(m_view);
        } else if (m_dragInfo.state == diNone) {
            m_scrollTimer.stop();
        }

        m_dragInfo.state = diNone;
        e->accept();
        break;
    case Qt::MidButton:
        placeCursor(e->pos());

        if (doc()->isReadWrite()) {
            view()->m_clipboard.pasteFromClipboard(QClipboard::Selection);
        }

        e->accept();
        break;

    default:
        e->ignore();
        break;
    }
}

void KateViewInternal::leaveEvent(QEvent *)
{
    m_textHintTimer.stop();

    // fix bug 194452, scrolling keeps going if you scroll via mouse drag and press and other mouse
    // button outside the view area
    if (m_dragInfo.state == diNone) {
        m_scrollTimer.stop();
    }
}

KTextEditor::Cursor KateViewInternal::coordinatesToCursor(const QPoint &_coord, bool includeBorder) const
{
    QPoint coord(_coord);

    KTextEditor::Cursor ret = KTextEditor::Cursor::invalid();

    if (includeBorder) {
        coord.rx() -= m_leftBorder->width();
    }
    coord.rx() += startX();

    const KateTextLayout &thisLine = yToKateTextLayout(coord.y());
    if (thisLine.isValid()) {
        ret = renderer()->xToCursor(thisLine, coord.x(), !m_view->wrapCursor());
    }

    if (ret.column() > view()->document()->lineLength(ret.line())) {
        // The cursor is beyond the end of the line; in that case the renderer
        // gives the index of the character behind the last one.
        return KTextEditor::Cursor::invalid();
    }

    return ret;
}

void KateViewInternal::mouseMoveEvent(QMouseEvent *e)
{
    KTextEditor::Cursor newPosition = coordinatesToCursor(e->pos(), false);
    if (newPosition != m_mouse) {
        m_mouse = newPosition;
        mouseMoved();
    }

    if (e->buttons() & Qt::LeftButton) {
        if (m_dragInfo.state == diPending) {
            // we had a mouse down, but haven't confirmed a drag yet
            // if the mouse has moved sufficiently, we will confirm
            QPoint p(e->pos() - m_dragInfo.start);

            // we've left the drag square, we can start a real drag operation now
            if (p.manhattanLength() > QApplication::startDragDistance()) {
                doDrag();
            }

            return;
        } else if (m_dragInfo.state == diDragging) {
            // Don't do anything after a canceled drag until the user lets go of
            // the mouse button!
            return;
        }

        m_mouseX = e->x();
        m_mouseY = e->y();

        m_scrollX = 0;
        m_scrollY = 0;
        int d = renderer()->lineHeight();

        if (m_mouseX < 0) {
            m_scrollX = -d;
        }

        if (m_mouseX > width()) {
            m_scrollX = d;
        }

        if (m_mouseY < 0) {
            m_mouseY = 0;
            m_scrollY = -d;
        }

        if (m_mouseY > height()) {
            m_mouseY = height();
            m_scrollY = d;
        }

        auto c = pointToCursor(QPoint(m_mouseX, m_mouseY));
        selections()->updateNewSelection(c);
        updateCursorFlashTimer();

    } else {
        if (isTargetSelected(e->pos())) {
            // mouse is over selected text. indicate that the text is draggable by setting
            // the arrow cursor as other Qt text editing widgets do
            if (m_mouseCursor != Qt::ArrowCursor) {
                m_mouseCursor = Qt::ArrowCursor;
                setCursor(m_mouseCursor);
            }
        } else {
            // normal text cursor
            if (m_mouseCursor != Qt::IBeamCursor) {
                m_mouseCursor = Qt::IBeamCursor;
                setCursor(m_mouseCursor);
            }
        }
        //We need to check whether the mouse position is actually within the widget,
        //because other widgets like the icon border forward their events to this,
        //and we will create invalid text hint requests if we don't check
        if (textHintsEnabled() && geometry().contains(parentWidget()->mapFromGlobal(e->globalPos()))) {
            if (QToolTip::isVisible()) {
                QToolTip::hideText();
            }
            m_textHintTimer.start(m_textHintDelay);
            m_textHintPos = e->pos();
        }
    }
}

void KateViewInternal::updateDirty()
{
    const int h = renderer()->lineHeight();

    int currentRectStart = -1;
    int currentRectEnd = -1;

    QRegion updateRegion;

    {
        for (int i = 0; i < cache()->viewCacheLineCount(); ++i) {
            if (cache()->viewLine(i).isDirty()) {
                if (currentRectStart == -1) {
                    currentRectStart = h * i;
                    currentRectEnd = h;
                } else {
                    currentRectEnd += h;
                }

            } else if (currentRectStart != -1) {
                updateRegion += QRect(0, currentRectStart, width(), currentRectEnd);
                currentRectStart = -1;
                currentRectEnd = -1;
            }
        }
    }

    if (currentRectStart != -1) {
        updateRegion += QRect(0, currentRectStart, width(), currentRectEnd);
    }

    if (!updateRegion.isEmpty()) {
        if (debugPainting) {
            qCDebug(LOG_KTE) << "Update dirty region " << updateRegion;
        }
        update(updateRegion);
    }
}

void KateViewInternal::hideEvent(QHideEvent *e)
{
    Q_UNUSED(e);
    if (m_view->isCompletionActive()) {
        m_view->completionWidget()->abortCompletion();
    }
}

void KateViewInternal::paintEvent(QPaintEvent *e)
{
    if (debugPainting) {
        qCDebug(LOG_KTE) << "GOT PAINT EVENT: Region" << e->region();
    }

    const QRect &unionRect = e->rect();

    int xStart = startX() + unionRect.x();
    int xEnd = xStart + unionRect.width();
    uint h = renderer()->lineHeight();
    uint startz = (unionRect.y() / h);
    uint endz = startz + 1 + (unionRect.height() / h);
    uint lineRangesSize = cache()->viewCacheLineCount();

    QPainter paint(this);
    paint.setRenderHints(QPainter::Antialiasing);

    paint.save();

    renderer()->setCaretStyle(m_currentInputMode->caretStyle());
    renderer()->setShowTabs(doc()->config()->showTabs());
    renderer()->setShowTrailingSpaces(doc()->config()->showSpaces());
    renderer()->updateMarkerSize();

    int sy = startz * h;
    paint.translate(unionRect.x(), startz * h);

    for (uint z = startz; z <= endz; z++) {
        paint.save();

        if ((z >= lineRangesSize) || (cache()->viewLine(z).line() == -1)) {
            if (!(z >= lineRangesSize)) {
                cache()->viewLine(z).setDirty(false);
            }

            paint.fillRect(0, 0, unionRect.width(), h, renderer()->config()->backgroundColor());
        } else {
            //qCDebug(LOG_KTE)<<"KateViewInternal::paintEvent(QPaintEvent *e):cache()->viewLine"<<z;
            KateTextLayout &thisLine = cache()->viewLine(z);

            /* If viewLine() returns non-zero, then a document line was split
               in several visual lines, and we're trying to paint visual line
               that is not the first.  In that case, this line was already
               painted previously, since KateRenderer::paintTextLine paints
               all visual lines.
               Except if we're at the start of the region that needs to
               be painted -- when no previous calls to paintTextLine were made.
            */
            if (!thisLine.viewLine() || z == startz) {
                //qDebug() << "paint text: line: " << thisLine.line() << " viewLine " << thisLine.viewLine() << " x: " << unionRect.x() << " y: " << unionRect.y() << " width: " << xEnd-xStart << " height: " << h << endl;

                KTextEditor::Cursor pos = primaryCursor();

                // first: paint our line
                paint.translate(QPoint(0, h * - thisLine.viewLine()));
                paint.setClipRect(QRect(0, 0, unionRect.width(), h * thisLine.kateLineLayout()->viewLineCount()));
                renderer()->paintTextLine(paint, thisLine.kateLineLayout(), xStart, xEnd, &pos);
                paint.translate(0, h * thisLine.viewLine());

                // second: paint previous line elements, that span into our line like _, bug 335079
                if (z > 0) {
                    KateTextLayout &previousLine = cache()->viewLine(z-1);
                    paint.translate(QPoint(0, h * - (previousLine.viewLine() + 1)));
                    renderer()->paintTextLine(paint, previousLine.kateLineLayout(), xStart, xEnd, &pos);
                    paint.translate(0, h * (previousLine.viewLine() + 1));
                }

                /**
                 * line painted, reset and state + mark line as non-dirty
                 */
                thisLine.setDirty(false);
            }
        }

        paint.restore();
        paint.translate(0, h);
        sy += h;
    }

    paint.restore();
    if (m_textAnimation) {
        m_textAnimation->draw(paint);
    }
}

void KateViewInternal::resizeEvent(QResizeEvent *e)
{
    bool expandedHorizontally = width() > e->oldSize().width();
    bool expandedVertically = height() > e->oldSize().height();
    bool heightChanged = height() != e->oldSize().height();

    m_dummy->setFixedSize(m_lineScroll->width(), m_columnScroll->sizeHint().height());
    m_madeVisible = false;

    if (heightChanged) {
        setAutoCenterLines(m_autoCenterLines, false);
        m_cachedMaxStartPos.setPosition(-1, -1);
    }

    if (m_view->dynWordWrap()) {
        bool dirtied = false;

        for (int i = 0; i < cache()->viewCacheLineCount(); i++) {
            // find the first dirty line
            // the word wrap updateView algorithm is forced to check all lines after a dirty one
            KateTextLayout viewLine = cache()->viewLine(i);

            if (viewLine.wrap() || viewLine.isRightToLeft() || viewLine.width() > width()) {
                dirtied = true;
                viewLine.setDirty();
                break;
            }
        }

        if (dirtied || heightChanged) {
            updateView(true);
            m_leftBorder->update();
        }
    } else {
        updateView();

        if (expandedHorizontally && startX() > 0) {
            scrollColumns(startX() - (width() - e->oldSize().width()));
        }
    }

    if (width() < e->oldSize().width() && !m_view->wrapCursor()) {
        // May have to restrain cursor to new smaller width...
        if (primaryCursor().column() > doc()->lineLength(primaryCursor().line())) {
            KateTextLayout thisLine = m_layoutCache->viewLine(primaryCursor().line());

            KTextEditor::Cursor newCursor(primaryCursor().line(), thisLine.endCol() + ((width() - thisLine.xOffset() - (thisLine.width() - m_startX)) / renderer()->spaceWidth()) - 1);
            if (newCursor.column() < primaryCursor().column()) {
                cursors()->setPrimaryCursor(newCursor);
            }
        }
    }

    if (expandedVertically) {
        KTextEditor::Cursor max = maxStartPos();
        if (startPos() > max) {
            scrollPos(max);
            return; // already fired displayRangeChanged
        }
    }
    emit m_view->displayRangeChanged(m_view);
}

void KateViewInternal::scrollTimeout()
{
    if (m_scrollX || m_scrollY) {
        scrollLines(startPos().line() + (m_scrollY / (int) renderer()->lineHeight()));
        placeCursor(QPoint(m_mouseX, m_mouseY), true);
    }
}

void KateViewInternal::cursorTimeout()
{
    if (!debugPainting && m_currentInputMode->blinkCaret()) {
        renderer()->setDrawCaret(!renderer()->drawCaret());
        paintCursor();
    }
}

void KateViewInternal::textHintTimeout()
{
    m_textHintTimer.stop();

    KTextEditor::Cursor c = coordinatesToCursor(m_textHintPos, false);
    if (!c.isValid()) {
        return;
    }

    QStringList textHints;
    foreach(KTextEditor::TextHintProvider * const p, m_textHintProviders) {
        const QString hint = p->textHint(m_view, c);
        if (!hint.isEmpty()) {
            textHints.append(hint);
        }
    }

    if (!textHints.isEmpty()) {
        qCDebug(LOG_KTE) << "Hint text: " << textHints;
        QString hint;
        foreach(const QString & str, textHints) {
            hint += QStringLiteral("<p>%1</p>").arg(str);
        }
        QPoint pos(startX() + m_textHintPos.x(), m_textHintPos.y());
        QToolTip::showText(mapToGlobal(pos), hint);
    }
}

void KateViewInternal::focusInEvent(QFocusEvent *)
{
    if (QApplication::cursorFlashTime() > 0) {
        m_cursorTimer.start(QApplication::cursorFlashTime() / 2);
    }

    paintCursor();

    doc()->setActiveView(m_view);

    // this will handle focus stuff in kateview
    m_view->slotGotFocus();
}

void KateViewInternal::focusOutEvent(QFocusEvent *)
{
    //if (m_view->isCompletionActive())
    //m_view->abortCompletion();

    m_cursorTimer.stop();
    m_view->renderer()->setDrawCaret(true);
    paintCursor();

    m_textHintTimer.stop();

    m_view->slotLostFocus();
}

void KateViewInternal::doDrag()
{
    m_dragInfo.state = diDragging;
    m_dragInfo.dragObject = new QDrag(this);
    QMimeData *mimeData = new QMimeData();
    mimeData->setText(m_view->selectionText());
    m_dragInfo.dragObject->setMimeData(mimeData);
    m_dragInfo.dragObject->start(Qt::MoveAction);
}

void KateViewInternal::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->source() == this) {
        event->setDropAction(Qt::MoveAction);
    }
    event->setAccepted((event->mimeData()->hasText() && doc()->isReadWrite()) ||
                       event->mimeData()->hasUrls());
}

void KateViewInternal::fixDropEvent(QDropEvent *event)
{
    if (event->source() != this) {
        event->setDropAction(Qt::CopyAction);
    } else {
        Qt::DropAction action = Qt::MoveAction;
#ifdef Q_WS_MAC
        if (event->keyboardModifiers() & Qt::AltModifier) {
            action = Qt::CopyAction;
        }
#else
        if (event->keyboardModifiers() & Qt::ControlModifier) {
            action = Qt::CopyAction;
        }
#endif
        event->setDropAction(action);
    }
}

void KateViewInternal::dragMoveEvent(QDragMoveEvent *event)
{
    // track the cursor to the current drop location
    placeCursor(event->pos(), true, false);

    qDebug() << "update drag:" << m_view->cursors()->cursors() << m_view->selections()->selections();

    // important: accept action to switch between copy and move mode
    // without this, the text will always be copied.
    fixDropEvent(event);
}

void KateViewInternal::dropEvent(QDropEvent *event)
{
    /**
     * if we have urls, pass this event off to the hosting application
     */
    if (event->mimeData()->hasUrls()) {
        emit dropEventPass(event);
        return;
    }

    if (event->mimeData()->hasText() && doc()->isReadWrite()) {

        const QString text = event->mimeData()->text();

        // is the source our own document?
        bool priv = false;
        if (KateViewInternal *vi = qobject_cast<KateViewInternal *>(event->source())) {
            priv = doc()->ownedView(vi->m_view);
        }

        // dropped on a text selection area?
        qDebug() << "have selections:" << m_view->selections()->selections();
        bool selected = m_view->cursorSelected(primaryCursor());

        fixDropEvent(event);

        if (priv && selected && event->dropAction() != Qt::CopyAction) {
            // this is a drag that we started and dropped on our selection
            // ignore this case
            return;
        }

        // fix the cursor position before editStart(), so that it is correctly
        // stored for the undo action
        KTextEditor::Cursor targetCursor(primaryCursor()); // backup current cursor
        int selectionWidth = m_view->selectionRange().columnWidth(); // for block selection
        int selectionHeight = m_view->selectionRange().numberOfLines(); // for block selection

        if (event->dropAction() == Qt::CopyAction) {
            m_view->clearSelection();
        }

        // use one transaction
        doc()->editStart();

        // on move: remove selected text; on copy: duplicate text
        qDebug() << "insert text:" << text << text.length() << "at" << targetCursor;
        doc()->insertText(targetCursor, text, m_view->blockSelection());

        KTextEditor::DocumentCursor startCursor(doc(), targetCursor);
        if (event->dropAction() != Qt::CopyAction) {
            m_view->removeSelectedText();
            auto selectionStartsAhead = m_view->primarySelection().start() < targetCursor;
            if ( selectionStartsAhead ) {
                startCursor.move(-text.length());
            }
        }

        auto endCursor = startCursor;
        endCursor.move(text.length());
        qDebug() << "end and taget cursor:" << endCursor << targetCursor;
        setSelection({startCursor, endCursor});
        editSetCursor(endCursor);

        doc()->editEnd();

        event->acceptProposedAction();
        updateView();
    }

    // finally finish drag and drop mode
    m_dragInfo.state = diNone;
    // important, because the eventFilter`s DragLeave does not occur
    stopDragScroll();
}
//END EVENT HANDLING STUFF

void KateViewInternal::clear()
{
    m_startPos.setPosition(0, 0);
    m_displayCursor = KTextEditor::Cursor(0, 0);
    primaryCursor().setPosition(0, 0);
    cache()->clear();
    updateView(true);
    m_lineScroll->updatePixmap();
}

void KateViewInternal::wheelEvent(QWheelEvent *e)
{
    // check if this event should change the font size (Ctrl pressed, angle reported and not accidentally so)
    // Note: if detectZoomingEvent() doesn't unset the ControlModifier we'll get accelerated scrolling.
    if (m_zoomEventFilter->detectZoomingEvent(e)) {
        if (e->angleDelta().y() > 0) {
            slotIncFontSizes(qreal(e->angleDelta().y()) / QWheelEvent::DefaultDeltasPerStep);
        } else if (e->angleDelta().y() < 0) {
            slotDecFontSizes(qreal(-e->angleDelta().y()) / QWheelEvent::DefaultDeltasPerStep);
        }

        // accept always and be done for zooming
        e->accept();
        return;
    }

    // handle vertical scrolling via the scrollbar
    if (e->orientation() == Qt::Vertical) {
        // compute distance
        auto sign = m_lineScroll->invertedControls() ? -1 : 1;
        auto offset = sign * qreal(e->angleDelta().y()) / 120;
        if ( e->modifiers() & Qt::ShiftModifier ) {
            const auto pageStep = m_lineScroll->pageStep();
            offset = qBound(-pageStep, int(offset * pageStep), pageStep);
        } else {
            offset *= QApplication::wheelScrollLines();
        }

        // handle accumulation
        m_accumulatedScroll += offset - int(offset);
        auto extraAccumulated = int(m_accumulatedScroll);
        m_accumulatedScroll -= extraAccumulated;

        // do scroll
        scrollViewLines(int(offset) + extraAccumulated);
        e->accept();
    }

    // handle horizontal scrolling via the scrollbar
    if (e->orientation() == Qt::Horizontal) {
        // if we have dyn word wrap, we should ignore the scroll events
        if (m_view->dynWordWrap()) {
            e->accept();
            return;
        }

        QWheelEvent copy = *e;
        QApplication::sendEvent(m_columnScroll, &copy);
        if (copy.isAccepted()) {
            e->accept();
        }
    }
}

void KateViewInternal::startDragScroll()
{
    if (!m_dragScrollTimer.isActive()) {
        m_dragScrollTimer.start(s_scrollTime);
    }
}

void KateViewInternal::stopDragScroll()
{
    m_dragScrollTimer.stop();
    updateView();
}

void KateViewInternal::doDragScroll()
{
    QPoint p = this->mapFromGlobal(QCursor::pos());

    int dx = 0, dy = 0;
    if (p.y() < s_scrollMargin) {
        dy = p.y() - s_scrollMargin;
    } else if (p.y() > height() - s_scrollMargin) {
        dy = s_scrollMargin - (height() - p.y());
    }

    if (p.x() < s_scrollMargin) {
        dx = p.x() - s_scrollMargin;
    } else if (p.x() > width() - s_scrollMargin) {
        dx = s_scrollMargin - (width() - p.x());
    }

    dy /= 4;

    if (dy) {
        scrollLines(startPos().line() + dy);
    }

    if (columnScrollingPossible() && dx) {
        scrollColumns(qMin(m_startX + dx, m_columnScroll->maximum()));
    }

    if (!dy && !dx) {
        stopDragScroll();
    }
}

void KateViewInternal::registerTextHintProvider(KTextEditor::TextHintProvider *provider)
{
    if (! m_textHintProviders.contains(provider)) {
        m_textHintProviders.append(provider);
    }

    // we have a client, so start timeout
    m_textHintTimer.start(m_textHintDelay);
}

void KateViewInternal::unregisterTextHintProvider(KTextEditor::TextHintProvider *provider)
{
    const int index = m_textHintProviders.indexOf(provider);
    if (index >= 0) {
        m_textHintProviders.removeAt(index);
    }

    if (m_textHintProviders.isEmpty()) {
        m_textHintTimer.stop();
    }
}

void KateViewInternal::setTextHintDelay(int delay)
{
    if (delay <= 0) {
        m_textHintDelay = 200; // ms
    } else {
        m_textHintDelay = delay; // ms
    }
}

int KateViewInternal::textHintDelay() const
{
    return m_textHintDelay;
}

bool KateViewInternal::textHintsEnabled()
{
    return ! m_textHintProviders.isEmpty();
}

//BEGIN EDIT STUFF
void KateViewInternal::editStart()
{
    editSessionNumber++;

    if (editSessionNumber > 1) {
        return;
    }

    editIsRunning = true;
    editOldCursor = primaryCursor();
    editOldSelection = m_view->selectionRange();
}

void KateViewInternal::editEnd(int editTagLineStart, int editTagLineEnd, bool tagFrom)
{
    if (editSessionNumber == 0) {
        return;
    }

    editSessionNumber--;

    if (editSessionNumber > 0) {
        return;
    }

    // fix start position, might have moved from column 0
    // try to clever calculate the right start column for the tricky dyn word wrap case
    int col = 0;
    if (m_view->dynWordWrap()) {
        if (KateLineLayoutPtr layout = cache()->line(m_startPos.line())) {
            int index = layout->viewLineForColumn(m_startPos.column());
            if (index >= 0 && index < layout->viewLineCount()) {
                col = layout->viewLine(index).startCol();
            }
        }
    }
    m_startPos.setPosition(m_startPos.line(), col);

    if (tagFrom && (editTagLineStart <= int(m_view->textFolding().visibleLineToLine(startLine())))) {
        tagAll();
    } else {
        tagLines(editTagLineStart, tagFrom ? qMax(doc()->lastLine() + 1, editTagLineEnd) : editTagLineEnd, true);
    }

    if (editOldCursor == primaryCursor()) {
        updateBracketMarks();
    }

    updateView(true);

    if (editOldCursor != primaryCursor() || m_view == doc()->activeView()) {
        // Only scroll the view to the cursor if the insertion happens at the cursor.
        // This might not be the case for e.g. collaborative editing, when a remote user
        // inserts text at a position not at the caret.
        if (primaryCursor().line() >= editTagLineStart && primaryCursor().line() <= editTagLineEnd) {
            m_madeVisible = false;
            notifyPrimaryCursorChanged(primaryCursor(), true);
        }
    }

    /**
     * selection changed?
     * fixes bug 316226
     */
    if (editOldSelection != m_view->selectionRange()
            || (editOldSelection.isValid() && !editOldSelection.isEmpty() && !(editTagLineStart > editOldSelection.end().line() && editTagLineEnd < editOldSelection.start().line()))) {
        emit m_view->selectionChanged(m_view);
    }

    editIsRunning = false;
}

void KateViewInternal::editSetCursor(const KTextEditor::Cursor &_cursor)
{
    if (primaryCursor() != _cursor) {
        cursors()->setPrimaryCursor(_cursor, false);
    }
}
//END

KateLayoutCache *KateViewInternal::cache() const
{
    return m_layoutCache;
}

void KateViewInternal::notifyLinesUpdated(const QVector<KTextEditor::Cursor>& changed)
{
    Q_FOREACH ( const auto& cursor, changed ) {
        tagLine(toVirtualCursor(cursor));
    }
    updateCursorFlashTimer();
    updateDirty();
}

KTextEditor::Cursor KateViewInternal::toRealCursor(const KTextEditor::Cursor &virtualCursor) const
{
    return KTextEditor::Cursor(m_view->textFolding().visibleLineToLine(virtualCursor.line()), virtualCursor.column());
}

KTextEditor::Cursor KateViewInternal::toVirtualCursor(const KTextEditor::Cursor &realCursor) const
{
    /**
     * only convert valid lines, folding doesn't like invalid input!
     * don't validate whole cursor, column might be -1
     */
    if (realCursor.line() < 0) {
        return KTextEditor::Cursor::invalid();
    }

    return KTextEditor::Cursor(m_view->textFolding().lineToVisibleLine(realCursor.line()), realCursor.column());
}

KateRenderer *KateViewInternal::renderer() const
{
    return m_view->renderer();
}

void KateViewInternal::mouseMoved()
{
    m_view->notifyMousePositionChanged(m_mouse);
    m_view->updateRangesIn(KTextEditor::Attribute::ActivateMouseIn);
}

void KateViewInternal::cursorMoved()
{
    m_view->updateRangesIn(KTextEditor::Attribute::ActivateCaretIn);

#ifndef QT_NO_ACCESSIBILITY
    if (QAccessible::isActive()) {
        QAccessibleTextCursorEvent ev(this, static_cast<KateViewAccessible *>(QAccessible::queryAccessibleInterface(this))->positionFromCursor(this, primaryCursor()));
        QAccessible::updateAccessibility(&ev);
    }
#endif
}

bool KateViewInternal::rangeAffectsView(const KTextEditor::Range &range, bool realCursors) const
{
    int startLine = m_startPos.line();
    int endLine = startLine + (int)m_visibleLineCount;

    if (realCursors) {
        startLine = (int)m_view->textFolding().visibleLineToLine(startLine);
        endLine = (int)m_view->textFolding().visibleLineToLine(endLine);
    }

    return (range.end().line() >= startLine) || (range.start().line() <= endLine);
}

//BEGIN IM INPUT STUFF
QVariant KateViewInternal::inputMethodQuery(Qt::InputMethodQuery query) const
{
    switch (query) {
    case Qt::ImCursorRectangle: {
        // Cursor placement code is changed for Asian input method that
        // shows candidate window. This behavior is same as Qt/E 2.3.7
        // which supports Asian input methods. Asian input methods need
        // start point of IM selection text to place candidate window as
        // adjacent to the selection text.
        //
        // in Qt5, cursor rectangle is used as QRectF internally, and it
        // will be checked by QRectF::isValid(), which will mark rectangle
        // with width == 0 or height == 0 as invalid.
        auto lineHeight = renderer()->lineHeight();
        return QRect(cursorToCoordinate(primaryCursor(), true, false), QSize(1, lineHeight ? lineHeight : 1));
    }

    case Qt::ImFont:
        return renderer()->currentFont();

    case Qt::ImCursorPosition:
        return primaryCursor().column();

    case Qt::ImAnchorPosition:
        // If selectAnchor is at the same line, return the real anchor position
        // Otherwise return the same position of cursor
        if (m_view->selection() && m_selectAnchor.line() == primaryCursor().line()) {
            return m_selectAnchor.column();
        } else {
            return primaryCursor().column();
        }

    case Qt::ImSurroundingText:
        if (Kate::TextLine l = doc()->kateTextLine(primaryCursor().line())) {
            return l->string();
        } else {
            return QString();
        }

    case Qt::ImCurrentSelection:
        if (m_view->selection()) {
            return m_view->selectionText();
        } else {
            return QString();
        }
    default:
        /* values: ImMaximumTextLength */
        break;
    }

    return QWidget::inputMethodQuery(query);
}

void KateViewInternal::inputMethodEvent(QInputMethodEvent *e)
{
    if (doc()->readOnly()) {
        e->ignore();
        return;
    }

    //qCDebug(LOG_KTE) << "Event: cursor" << primaryCursor() << "commit" << e->commitString() << "preedit" << e->preeditString() << "replacement start" << e->replacementStart() << "length" << e->replacementLength();

    if (!m_imPreeditRange) {
        m_imPreeditRange = doc()->newMovingRange(KTextEditor::Range(primaryCursor(), primaryCursor()), KTextEditor::MovingRange::ExpandLeft | KTextEditor::MovingRange::ExpandRight);
    }

    if (!m_imPreeditRange->toRange().isEmpty()) {
        doc()->inputMethodStart();
        doc()->removeText(*m_imPreeditRange);
        doc()->inputMethodEnd();
    }

    if (!e->commitString().isEmpty() || e->replacementLength()) {
        m_view->removeSelectedText();

        KTextEditor::Range preeditRange = *m_imPreeditRange;

        KTextEditor::Cursor start(m_imPreeditRange->start().line(), m_imPreeditRange->start().column() + e->replacementStart());
        KTextEditor::Cursor removeEnd = start + KTextEditor::Cursor(0, e->replacementLength());

        doc()->editStart();
        if (start != removeEnd) {
            doc()->removeText(KTextEditor::Range(start, removeEnd));
        }
        if (!e->commitString().isEmpty()) {
            // if the input method event is text that should be inserted, call KTextEditor::DocumentPrivate::typeChars()
            // with the text. that method will handle the input and take care of overwrite mode, etc.
            doc()->typeChars(m_view, e->commitString());
        }
        doc()->editEnd();

        // Revert to the same range as above
        m_imPreeditRange->setRange(preeditRange);
    }

    if (!e->preeditString().isEmpty()) {
        doc()->inputMethodStart();
        doc()->insertText(m_imPreeditRange->start(), e->preeditString());
        doc()->inputMethodEnd();
        // The preedit range gets automatically repositioned
    }

    // Finished this input method context?
    if (m_imPreeditRange && e->preeditString().isEmpty()) {
        // delete the range and reset the pointer
        delete m_imPreeditRange;
        m_imPreeditRange = nullptr;
        qDeleteAll(m_imPreeditRangeChildren);
        m_imPreeditRangeChildren.clear();

        if (QApplication::cursorFlashTime() > 0) {
            renderer()->setDrawCaret(false);
        }
        renderer()->setCaretOverrideColor(QColor());

        e->accept();
        return;
    }

    KTextEditor::Cursor newCursor = primaryCursor();
    bool hideCursor = false;
    QColor caretColor;

    if (m_imPreeditRange) {
        qDeleteAll(m_imPreeditRangeChildren);
        m_imPreeditRangeChildren.clear();

        int decorationColumn = 0;
        foreach (const QInputMethodEvent::Attribute &a, e->attributes()) {
            if (a.type == QInputMethodEvent::Cursor) {
                newCursor = m_imPreeditRange->start() + KTextEditor::Cursor(0, a.start);
                hideCursor = !a.length;
                QColor c = qvariant_cast<QColor>(a.value);
                if (c.isValid()) {
                    caretColor = c;
                }

            } else if (a.type == QInputMethodEvent::TextFormat) {
                QTextCharFormat f = qvariant_cast<QTextFormat>(a.value).toCharFormat();
                if (f.isValid() && decorationColumn <= a.start) {
                    KTextEditor::Range fr(m_imPreeditRange->start().line(),  m_imPreeditRange->start().column() + a.start, m_imPreeditRange->start().line(), m_imPreeditRange->start().column() + a.start + a.length);
                    KTextEditor::MovingRange *formatRange = doc()->newMovingRange(fr);
                    KTextEditor::Attribute::Ptr attribute(new KTextEditor::Attribute());
                    attribute->merge(f);
                    formatRange->setAttribute(attribute);
                    decorationColumn = a.start + a.length;
                    m_imPreeditRangeChildren.push_back(formatRange);
                }
            }
        }
    }

    renderer()->setDrawCaret(hideCursor);
    renderer()->setCaretOverrideColor(caretColor);

    if (newCursor != primaryCursor()) {
        cursors()->setPrimaryCursor(newCursor);
    }

    e->accept();
}

//END IM INPUT STUFF

void KateViewInternal::flashChar(const KTextEditor::Cursor &pos, KTextEditor::Attribute::Ptr attribute)
{
    Q_ASSERT(pos.isValid());
    Q_ASSERT(attribute.constData());

    // if line is folded away, do nothing
    if (!m_view->textFolding().isLineVisible(pos.line())) {
        return;
    }

    KTextEditor::Range range(pos, KTextEditor::Cursor(pos.line(), pos.column() + 1));
    if (m_textAnimation) {
        m_textAnimation->deleteLater();
    }
    m_textAnimation = new KateTextAnimation(range, attribute, this);
}

void KateViewInternal::documentTextInserted(KTextEditor::Document *document, const KTextEditor::Range &range)
{
#ifndef QT_NO_ACCESSIBILITY
    if (QAccessible::isActive()) {
        QAccessibleTextInsertEvent ev(this,
            static_cast<KateViewAccessible *>(QAccessible::queryAccessibleInterface(this))->positionFromCursor(this, range.start()), document->text(range));
        QAccessible::updateAccessibility(&ev);
    }
#endif
}

void  KateViewInternal::documentTextRemoved(KTextEditor::Document * /*document*/, const KTextEditor::Range &range, const QString &oldText)
{
#ifndef QT_NO_ACCESSIBILITY
    if (QAccessible::isActive()) {
        QAccessibleTextRemoveEvent ev(this,
            static_cast<KateViewAccessible *>(QAccessible::queryAccessibleInterface(this))->positionFromCursor(this, range.start()), oldText);
        QAccessible::updateAccessibility(&ev);
    }
#endif
}
