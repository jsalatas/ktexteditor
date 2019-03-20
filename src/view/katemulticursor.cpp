/* This file is part of the KDE and the Kate project
*
*   Copyright (C) 2016-2017 Sven Brauch <mail@svenbrauch.de>
*
*  This library is free software; you can redistribute it and/or
*  modify it under the terms of the GNU Library General Public
*  License as published by the Free Software Foundation; either
*  version 2 of the License, or (at your option) any later version.
*
*  This library is distributed in the hope that it will be useful,
*  but WITHOUT ANY WARRANTY; without even the implied warranty of
*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
*  Library General Public License for more details.
*
*  You should have received a copy of the GNU Library General Public License
*  along with this library; see the file COPYING.LIB.  If not, write to
*  the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
*  Boston, MA 02110-1301, USA.
*/


#include "katemulticursor.h"

#include "kateview.h"
#include "kateviewinternal.h"
#include "katehighlight.h"
#include "katedocument.h"
#include "kateconfig.h"
#include "katepartdebug.h"
#include "katelayoutcache.h"
#include "cursor.h"
#include "movinginterface.h"

#include <algorithm>

class CalculatingCursor
{
public:
    // These constructors constrain their arguments to valid positions
    // before only the third one did, but that leads to crashs
    // see bug 227449
    CalculatingCursor(KateViewInternal *vi)
        : m_vi(vi)
    {
        makeValid();
    }

    CalculatingCursor(KateViewInternal *vi, const KTextEditor::Cursor &c)
        : m_cursor(c)
        , m_vi(vi)
    {
        makeValid();
    }

    CalculatingCursor(KateViewInternal *vi, int line, int col)
        : m_cursor(line, col)
        , m_vi(vi)
    {
        makeValid();
    }

    virtual ~CalculatingCursor()
    {
    }

    int line() const
    {
        return m_cursor.line();
    }

    int column() const
    {
        return m_cursor.column();
    }

    operator KTextEditor::Cursor() const
    {
        return m_cursor;
    }

    virtual CalculatingCursor &operator+=(int n) = 0;

    virtual CalculatingCursor &operator-=(int n) = 0;

    CalculatingCursor &operator++()
    {
        return operator+=(1);
    }

    CalculatingCursor &operator--()
    {
        return operator-=(1);
    }

    void makeValid()
    {
        m_cursor.setLine(qBound(0, line(), int(doc()->lines() - 1)));
        if (view()->wrapCursor()) {
            m_cursor.setColumn(qBound(0, column(), doc()->lineLength(line())));
        } else {
            m_cursor.setColumn(qMax(0, column()));
        }
        Q_ASSERT(valid());
    }

    void toEdge(KateViewInternal::Bias bias)
    {
        if (bias == KateViewInternal::left) {
            m_cursor.setColumn(0);
        } else if (bias == KateViewInternal::right) {
            m_cursor.setColumn(doc()->lineLength(line()));
        }
    }

    bool atEdge() const
    {
        return atEdge(KateViewInternal::left) || atEdge(KateViewInternal::right);
    }

    bool atEdge(KateViewInternal::Bias bias) const
    {
        switch (bias) {
        case KateViewInternal::left:  return column() == 0;
        case KateViewInternal::none:  return atEdge();
        case KateViewInternal::right: return column() >= doc()->lineLength(line());
        default: Q_ASSERT(false); return false;
        }
    }

protected:
    bool valid() const
    {
        return line() >= 0 &&
               line() < doc()->lines() &&
               column() >= 0 &&
               (!view()->wrapCursor() || column() <= doc()->lineLength(line()));
    }
    KTextEditor::ViewPrivate *view()
    {
        return m_vi->m_view;
    }
    const KTextEditor::ViewPrivate *view() const
    {
        return m_vi->m_view;
    }
    KTextEditor::DocumentPrivate *doc()
    {
        return view()->doc();
    }
    const KTextEditor::DocumentPrivate *doc() const
    {
        return view()->doc();
    }
    KTextEditor::Cursor m_cursor;
    KateViewInternal *m_vi;
};

class BoundedCursor : public CalculatingCursor
{
public:
    BoundedCursor(KateViewInternal *vi)
        : CalculatingCursor(vi) {}
    BoundedCursor(KateViewInternal *vi, const KTextEditor::Cursor &c)
        : CalculatingCursor(vi, c) {}
    BoundedCursor(KateViewInternal *vi, int line, int col)
        : CalculatingCursor(vi, line, col) {}
    CalculatingCursor &operator+=(int n) Q_DECL_OVERRIDE
    {
        KateLineLayoutPtr thisLine = m_vi->cache()->line(line());
        if (!thisLine->isValid()) {
            qCWarning(LOG_KTE) << "Did not retrieve valid layout for line " << line();
            return *this;
        }

        const bool wrapCursor = view()->wrapCursor();
        int maxColumn = -1;
        if (n >= 0) {
            for (int i = 0; i < n; i++) {
                if (column() >= thisLine->length()) {
                    if (wrapCursor) {
                        break;

                    } else if (view()->dynWordWrap()) {
                        // Don't go past the edge of the screen in dynamic wrapping mode
                        if (maxColumn == -1) {
                            maxColumn = thisLine->length() + ((m_vi->width() - thisLine->widthOfLastLine()) / m_vi->renderer()->spaceWidth()) - 1;
                        }

                        if (column() >= maxColumn) {
                            m_cursor.setColumn(maxColumn);
                            break;
                        }

                        m_cursor.setColumn(column() + 1);

                    } else {
                        m_cursor.setColumn(column() + 1);
                    }

                } else {
                    m_cursor.setColumn(thisLine->layout()->nextCursorPosition(column()));
                }
            }
        } else {
            for (int i = 0; i > n; i--) {
                if (column() >= thisLine->length()) {
                    m_cursor.setColumn(column() - 1);
                } else if (column() == 0) {
                    break;
                } else {
                    m_cursor.setColumn(thisLine->layout()->previousCursorPosition(column()));
                }
            }
        }

        Q_ASSERT(valid());
        return *this;
    }
    CalculatingCursor &operator-=(int n) Q_DECL_OVERRIDE
    {
        return operator+=(-n);
    }
};

class WrappingCursor : public CalculatingCursor
{
public:
    WrappingCursor(KateViewInternal *vi)
        : CalculatingCursor(vi) {}
    WrappingCursor(KateViewInternal *vi, const KTextEditor::Cursor &c)
        : CalculatingCursor(vi, c) {}
    WrappingCursor(KateViewInternal *vi, int line, int col)
        : CalculatingCursor(vi, line, col) {}

    CalculatingCursor &operator+=(int n) Q_DECL_OVERRIDE
    {
        KateLineLayoutPtr thisLine = m_vi->cache()->line(line());
        if (!thisLine->isValid()) {
            qCWarning(LOG_KTE) << "Did not retrieve a valid layout for line " << line();
            return *this;
        }

        if (n >= 0) {
            for (int i = 0; i < n; i++) {
                if (column() >= thisLine->length()) {
                    // Have come to the end of a line
                    if (line() >= doc()->lines() - 1)
                        // Have come to the end of the document
                    {
                        break;
                    }

                    // Advance to the beginning of the next line
                    m_cursor.setColumn(0);
                    m_cursor.setLine(line() + 1);

                    // Retrieve the next text range
                    thisLine = m_vi->cache()->line(line());
                    if (!thisLine->isValid()) {
                        qCWarning(LOG_KTE) << "Did not retrieve a valid layout for line " << line();
                        return *this;
                    }

                    continue;
                }

                m_cursor.setColumn(thisLine->layout()->nextCursorPosition(column()));
            }

        } else {
            for (int i = 0; i > n; i--) {
                if (column() == 0) {
                    // Have come to the start of the document
                    if (line() == 0) {
                        break;
                    }

                    // Start going back to the end of the last line
                    m_cursor.setLine(line() - 1);

                    // Retrieve the next text range
                    thisLine = m_vi->cache()->line(line());
                    if (!thisLine->isValid()) {
                        qCWarning(LOG_KTE) << "Did not retrieve a valid layout for line " << line();
                        return *this;
                    }

                    // Finish going back to the end of the last line
                    m_cursor.setColumn(thisLine->length());

                    continue;
                }

                if (column() > thisLine->length()) {
                    m_cursor.setColumn(column() - 1);
                } else {
                    m_cursor.setColumn(thisLine->layout()->previousCursorPosition(column()));
                }
            }
        }

        Q_ASSERT(valid());
        return *this;
    }
    CalculatingCursor &operator-=(int n) Q_DECL_OVERRIDE
    {
        return operator+=(-n);
    }
};

KateMultiCursor::KateMultiCursor(KateViewInternal* view)
    : m_viewInternal(view)
{
    qDebug() << "creating new multicursor engine for view" << view;
    appendCursorInternal({0, 0});
    Q_ASSERT(view);
}

KTextEditor::ViewPrivate* KateMultiCursor::view() const
{
    return m_viewInternal->view();
}

KTextEditor::DocumentPrivate* KateMultiCursor::doc() const
{
    return view()->doc();
}

KateViewInternal* KateMultiCursor::viewInternal() const
{
    return m_viewInternal;
}

KateViewInternal* KateMultiSelection::viewInternal() const
{
    return m_viewInternal;
}

const KateMultiSelection* KateMultiCursor::selections() const
{
    return view()->selections();
}

KateMultiSelection* KateMultiCursor::selections()
{
    return view()->selections();
}

void KateMultiCursor::setPrimaryCursor(const KTextEditor::Cursor& cursor, bool repaint, bool select, bool doSubstract)
{
    qDebug() << "called" << cursor;
    Q_ASSERT(cursor.isValid());
    if (cursor == primaryCursor()) {
        return;
    }
    CursorRepainter rep(this, repaint);
    KateMultiSelection::SelectingCursorMovement sel(selections(), select, false, doSubstract);
    if (!doSubstract) {
        m_cursors.first()->setPosition(cursor);
    }
}

void KateMultiCursor::setPrimaryCursorWithoutSelection(const KTextEditor::Cursor& cursor, bool repaint)
{
    Q_ASSERT(cursor.isValid());
    if (cursor == primaryCursor()) {
        return;
    }
    CursorRepainter rep(this, repaint);
    m_cursors.first()->setPosition(cursor);
}


Cursors KateMultiCursor::cursors() const
{
    auto cursors = secondaryCursors();
    cursors.prepend(primaryCursor());
    std::sort(cursors.begin(), cursors.end(), [](const KTextEditor::Cursor c1, const KTextEditor::Cursor c2) {
        return c1 > c2;
    });
    return cursors;
}

const QVector<KTextEditor::MovingCursor::Ptr> KateMultiCursor::movingCursors() const
{
    QVector<KTextEditor::MovingCursor::Ptr> ret;
    Q_FOREACH (const auto& c, m_cursors) {
        ret.append(c);
    }
    return ret;
}

int KateMultiCursor::cursorsCount() const
{
    Q_ASSERT(!m_cursors.isEmpty());
    return m_cursors.size();
}

Cursor KateMultiCursor::primaryCursor() const
{
    return m_cursors.first()->toCursor();
}

Cursors KateMultiCursor::secondaryCursors() const
{
    QVector<KTextEditor::Cursor> cursors;
    cursors.reserve(m_cursors.size() - 1);
    foreach (const auto moving, m_cursors.mid(1)) {
        auto cursor = moving->toCursor();
        cursors.append(cursor);
    }
    return cursors;
}

bool KateMultiCursor::hasSecondaryCursors() const
{
    return m_cursors.size() > 1;
}

void KateMultiCursor::appendCursorInternal(const KTextEditor::Cursor& cursor)
{
    m_cursors.append(MovingCursor::Ptr(doc()->newMovingCursor(cursor, KTextEditor::MovingCursor::MoveOnInsert)));
    m_selections.append(MovingRange::Ptr(doc()->newMovingRange({cursor, cursor}, Kate::TextRange::ExpandLeft, Kate::TextRange::AllowEmpty)));
    m_selections.last()->setView(view());
    m_selections.last()->setZDepth(-100000.0);

    Q_ASSERT(m_cursors.size() == m_selections.size());
}

bool KateMultiCursor::toggleSecondaryCursorAt(const KTextEditor::Cursor& cursor, bool ensureExists)
{
    Q_ASSERT(cursor.isValid());
    qDebug() << "called" << cursor << m_cursors;

    if (selections()->positionSelected(cursor)) {
        // cannot place secondary cursors inside a selection
        qDebug() << "will not place cursor inside a selection";
        return false;
    }

    CursorRepainter rep(this);
    Q_FOREACH (const auto moving, m_cursors.mid(1)) {
        if (moving->toCursor() == cursor) {
            removeCursorInternal(moving);
            Q_ASSERT(!m_cursors.isEmpty());
            if (! ensureExists) {
                qDebug() << "removed secondary cursor" << cursor;
                return false;
            }
        }
    }
    appendCursorInternal(cursor);

    qDebug() << "new list of cursors:" << m_cursors;

    return true;
}

void KateMultiCursor::clearSecondaryCursors()
{
    qDebug() << "clearing secondary cursors";
    CursorRepainter rep(this);
    m_cursors.resize(1);
    m_selections.resize(1);
    qDebug() << "cursors:" << m_cursors;
}

QVector<MovingCursor::Ptr> KateMultiCursor::allCursors() const
{
    Q_ASSERT(m_cursors.size() >= 1);
    Q_ASSERT(m_selections.size() == m_cursors.size());
    return m_cursors;
}

void KateMultiCursor::moveCursorsLeft(bool sel, int32_t chars)
{
    qDebug() << "called" << sel << chars;
    CursorRepainter rep(this);
    KateMultiSelection::SelectingCursorMovement mov(selections(), sel);
    m_savedHorizontalPositions.clear();
    Q_FOREACH (const auto& cursor, allCursors()) {
        if (! view()->wrapCursor() && cursor->column() == 0) {
            continue;
        }
        cursor->setPosition(moveLeftRight(*cursor, -chars));
        if (secondaryFrozen()) {
            break;
        }
    }
}

void KateMultiCursor::moveCursorsRight(bool sel, int32_t chars)
{
    qDebug() << "called" << sel << chars;
    moveCursorsLeft(sel, -chars);
}

void KateMultiCursor::moveCursorsTopHome(bool sel)
{
    CursorRepainter rep(this);
    clearSecondaryCursors();
    KateMultiSelection::SelectingCursorMovement mov(selections(), sel);
    setPrimaryCursor({0, 0});
}

void KateMultiCursor::moveCursorsBottomEnd(bool sel)
{
    CursorRepainter rep(this);
    clearSecondaryCursors();
    KateMultiSelection::SelectingCursorMovement mov(selections(), sel);
    KTextEditor::Cursor c(doc()->lastLine(), doc()->lineLength(doc()->lastLine()));
    setPrimaryCursor(c);
}

void KateMultiCursor::moveCursorsUp(bool sel, int32_t chars)
{
    qDebug() << "called" << sel << chars;
    CursorRepainter rep(this);
    KateMultiSelection::SelectingCursorMovement mov(selections(), sel);
    for ( int i = 0; i < abs(chars); i++ ) {
        Q_FOREACH (const auto& cursor, allCursors()) {
            auto x = m_savedHorizontalPositions.value(cursor.data(), -1);
            // This always only moves by 1 line, thus the loop.
            cursor->setPosition(moveUpDown(*cursor, -chars, x));
            m_savedHorizontalPositions.insert(cursor.data(), x);
            qDebug() << "add cached x:" << *cursor.data() << x;
            if (secondaryFrozen()) {
                break;
            }
        }
    }
}

void KateMultiCursor::moveCursorsDown(bool sel, int32_t chars)
{
    qDebug() << "called" << sel << chars;
    moveCursorsUp(sel, -chars);
}

void KateMultiCursor::moveCursorsEndOfLine(bool sel)
{
    qDebug() << "called" << sel;
    m_savedHorizontalPositions.clear();

    CursorRepainter rep(this);
    KateMultiSelection::SelectingCursorMovement mov(selections(), sel);
    Q_FOREACH (const auto& cursor, allCursors()) {
        cursor->setPosition(moveEnd(*cursor));
        if (secondaryFrozen()) {
            return;
        }
    }
}

void KateMultiCursor::moveCursorsStartOfLine(bool sel)
{
    qDebug() << "called" << sel;
    m_savedHorizontalPositions.clear();

    CursorRepainter rep(this);
    KateMultiSelection::SelectingCursorMovement mov(selections(), sel);
    Q_FOREACH (const auto& cursor, allCursors()) {
        cursor->setPosition(moveHome(*cursor));
        if (secondaryFrozen()) {
            return;
        }
    }
}

void KateMultiCursor::moveCursorsWordNext(bool sel)
{
    qDebug() << "called" << sel;
    m_savedHorizontalPositions.clear();

    CursorRepainter rep(this);
    KateMultiSelection::SelectingCursorMovement mov(selections(), sel);
    Q_FOREACH (const auto& cursor, allCursors()) {
        cursor->setPosition(moveWord(*cursor, Right));
        if (secondaryFrozen()) {
            return;
        }
    }
}

void KateMultiCursor::moveCursorsWordPrevious(bool sel)
{
    qDebug() << "called" << sel;
    m_savedHorizontalPositions.clear();

    CursorRepainter rep(this);
    KateMultiSelection::SelectingCursorMovement mov(selections(), sel);
    Q_FOREACH (const auto& cursor, allCursors()) {
        cursor->setPosition(moveWord(*cursor, Left));
        if (secondaryFrozen()) {
            return;
        }
    }
}

Cursor KateMultiCursor::moveHome(const KTextEditor::Cursor& cursor) const
{
    auto currentLayout = viewInternal()->currentLayout(cursor);
    if (view()->dynWordWrap() && currentLayout.startCol()) {
        // Allow us to go to the real start if we're already at the start of the view line
        if (cursor.column() != currentLayout.startCol()) {
            return currentLayout.start();
        }
    }

    if (!doc()->config()->smartHome()) {
        BoundedCursor c(viewInternal(), cursor);
        c.toEdge(KateViewInternal::left);
        return static_cast<KTextEditor::Cursor>(c);
    }

    Kate::TextLine l = doc()->kateTextLine(cursor.line());

    if (!l) {
        return KTextEditor::Cursor::invalid();
    }

    KTextEditor::Cursor c = cursor;
    int lc = l->firstChar();

    if (lc < 0 || c.column() == lc) {
        c.setColumn(0);
    } else {
        c.setColumn(lc);
    }
    return c;
}

Cursor KateMultiCursor::moveEnd(const KTextEditor::Cursor& cursor) const
{
    auto layout = viewInternal()->currentLayout(cursor);
    if (view()->dynWordWrap() && layout.wrap()) {
        // Allow us to go to the real end if we're already at the end of the view line
        if (cursor.column() < layout.endCol() - 1) {
            KTextEditor::Cursor c(cursor.line(), layout.endCol() - 1);
            return c;
        }
    }

    if (!doc()->config()->smartHome()) {
        BoundedCursor c(viewInternal(), cursor);
        c.toEdge(KateViewInternal::right);
        return static_cast<KTextEditor::Cursor>(c);
    }

    Kate::TextLine l = doc()->kateTextLine(cursor.line());

    if (!l) {
        return KTextEditor::Cursor::invalid();
    }

    // "Smart End", as requested in bugs #78258 and #106970
    if (cursor.column() == doc()->lineLength(cursor.line())) {
        KTextEditor::Cursor c = cursor;
        c.setColumn(l->lastChar() + 1);
        return c;
    } else {
        BoundedCursor bounded(viewInternal(), cursor);
        bounded.toEdge(KateViewInternal::right);
        return static_cast<KTextEditor::Cursor>(bounded);
    }
    Q_UNREACHABLE();
}

Cursor KateMultiCursor::moveLeftRight(const Cursor& start, int32_t chars) const
{
    KTextEditor::Cursor c;
    if (view()->wrapCursor()) {
        c = WrappingCursor(viewInternal(), start) += chars;
    } else {
        c = BoundedCursor(viewInternal(), start) += chars;
    }

    return c;
}

Cursor KateMultiCursor::moveUpDown(const Cursor& start, int32_t direction, int32_t& x) const
{
    qDebug() << "called" << start << direction;
    /**
     * move cursor to start/end of line, if we are at first/last line!
     */
    auto visLine = viewInternal()->toVirtualCursor(start).line();
    auto cache = viewInternal()->cache();
    if (direction < 0) {
        if (visLine == 0 && (!view()->dynWordWrap() || cache->viewLine(start) == 0)) {
            return moveHome(start);
        }
    } else {
        if ((visLine >= view()->textFolding().visibleLines() - 1) && (!view()->dynWordWrap()
                || cache->viewLine(start) == cache->lastViewLine(start.line()))) {
            return moveEnd(start);
        }
    }

    // This is not the first/last line because that is already simplified out above
    KateTextLayout thisLine = viewInternal()->currentLayout(start);
    KateTextLayout pRange = direction > 0 ? viewInternal()->nextLayout(start) : viewInternal()->previousLayout(start);

    // Ensure we're in the right spot
    Q_ASSERT(start.line() == thisLine.line());
    Q_ASSERT(start.column() >= thisLine.startCol());
    Q_ASSERT(!thisLine.wrap() || start.column() < thisLine.endCol());

    auto prev_x = x == -1 ? viewInternal()->renderer()->cursorToX(thisLine, viewInternal()->toVirtualCursor(start).column()) : x;
    qDebug() << "use x:" << x << prev_x;
    auto res = viewInternal()->renderer()->xToCursor(pRange, prev_x, !view()->wrapCursor());
    x = prev_x;
    return res;
}

KTextEditor::Cursor KateMultiCursor::moveWord(const KTextEditor::Cursor& cursor, KateMultiCursor::Direction dir) const
{
    // We look up into which category the current position falls:
    // 1. a "word" character
    // 2. a "non-word" character (except space)
    // 3. the end of the line
    // and skip all following characters that fall into this class.
    // If the skipped characters are followed by space, we skip that too.
    // The code assumes that space is never part of the word character class.

    WrappingCursor c(viewInternal(), cursor);

    KateHighlighting *h = doc()->highlight();

    if (dir == Right) {
        if (c.atEdge(KateViewInternal::right)) {
            ++c;
        } else if (h->isInWord(doc()->line(c.line())[ c.column() ])) {
            while (!c.atEdge(KateViewInternal::right) && h->isInWord(doc()->line(c.line())[ c.column() ])) {
                ++c;
            }
        } else {
            while (!c.atEdge(KateViewInternal::right)
                   && !h->isInWord(doc()->line(c.line())[ c.column() ])
                   // we must not skip space, because if that space is followed
                   // by more non-word characters, we would skip them, too
                   && !doc()->line(c.line())[ c.column() ].isSpace()) {
                ++c;
            }
        }

        while (!c.atEdge(KateViewInternal::right) && doc()->line(c.line())[ c.column() ].isSpace()) {
            ++c;
        }
    } else if (dir == Left) {
        if ( !c.atEdge(KateViewInternal::left)) {
            while (!c.atEdge(KateViewInternal::left) && doc()->line(c.line())[ c.column() - 1 ].isSpace()) {
                --c;
            }
        }
        if (c.atEdge(KateViewInternal::left)) {
            --c;
        } else if (h->isInWord(doc()->line(c.line())[ c.column() - 1 ])) {
            while (!c.atEdge(KateViewInternal::left) && h->isInWord(doc()->line(c.line())[ c.column() - 1 ])) {
                --c;
            }
        } else {
            while (!c.atEdge(KateViewInternal::left)
                   && !h->isInWord(doc()->line(c.line())[ c.column() - 1 ])
                   // in order to stay symmetric to wordLeft()
                   // we must not skip space preceding a non-word sequence
                   && !doc()->line(c.line())[ c.column() - 1 ].isSpace()) {
                --c;
            }
        }
    }
    return c;
}

KTextEditor::Cursor KateMultiCursor::toWordBoundary(const KTextEditor::Cursor& cursor, KateMultiCursor::Direction dir) const
{
    // This is a simpler version of moveWord, which just moves the cursor to the next
    // word boundary right or left. It does not include spaces or things like that.

    WrappingCursor c(viewInternal(), cursor);
    KateHighlighting *h = doc()->highlight();

    if ( dir == Right ) {
        if (!c.atEdge(KateViewInternal::left) && !h->isInWord(doc()->line(c.line())[ c.column()-1 ])) {
            return c;
        }
        while (!c.atEdge(KateViewInternal::right) && h->isInWord(doc()->line(c.line())[ c.column() ])) {
            ++c;
        }
    }
    else {
        if (!c.atEdge(KateViewInternal::right) && !h->isInWord(doc()->line(c.line())[ c.column() ])) {
            return c;
        }
        while (!c.atEdge(KateViewInternal::left) && h->isInWord(doc()->line(c.line())[ c.column()-1 ])) {
            --c;
        }
    }
    return c;
}

bool KateMultiCursor::cursorAtWordBoundary(const KTextEditor::Cursor& c) const
{
    KateHighlighting *h = doc()->highlight();
    auto line = doc()->line(c.line());
    if (line.length() > c.column()) {
        auto character = line.at(c.column());
        return !h->isInWord(character);
    }
    return true;
}

const KateMultiCursor* KateMultiSelection::cursors() const
{
    return view()->cursors();
}

KTextEditor::ViewPrivate* KateMultiSelection::view() const
{
    return m_viewInternal->view();
}

KTextEditor::DocumentPrivate* KateMultiSelection::doc() const
{
    return view()->doc();
}

KateMultiSelection::KateMultiSelection(KateViewInternal* view)
    : m_viewInternal(view)
{

}

void KateMultiCursor::removeEncompassedSecondaryCursors(CursorSelectionFlags flags)
{
    // join adjacent or partially-overlapping ranges
    bool did_remove = false;
    do {
        did_remove = false;
        for (auto i = 0; i < m_selections.size(); i++) {
            auto sel = m_selections.at(i)->toRange();
            if (sel.isEmpty()) {
                continue;
            }
            for (auto j = i + 1; j < m_selections.size(); j++) {
                auto next = m_selections.at(j)->toRange();
                KTextEditor::Range intersect;
                if (!(intersect = sel.intersect(next)).isEmpty()) {
                    did_remove = true;
                    // update first to encompass both, then remove the second
                    qDebug() << "joining ranges:" << sel << next << i << j;
                    auto curPos = m_cursors.at(i)->toCursor();
                    auto newCurPos = m_cursors.at(j)->toCursor();

                    m_selections[i]->setRange({qMin(sel.start(), next.start()),
                                               qMax(sel.end(), next.end())});
                    if ( ! (flags & UseMostRecentCursor) ) {
                        // decide which cursor to keep: the one at the edge
                        if (m_selections.at(i)->toRange().boundaryAtCursor(newCurPos)) {
                            m_cursors.at(i)->setPosition(newCurPos);
                        }
                    } else {
                        auto resultingRange = m_selections.at(i)->toRange();
                        qDebug() << "cursor not at boundary, adjusting" << resultingRange << curPos << newCurPos;
                        auto newPos = KTextEditor::Cursor::invalid();
                        if (next.end() > sel.end()) {
                            // from the right
                            newPos = newCurPos == next.end() ? resultingRange.end() : resultingRange.start();
                        } else {
                            // from the left
                            newPos = newCurPos == next.start() ? resultingRange.start() : resultingRange.end();
                        }
                        m_cursors.at(i)->setPosition(newPos);
                    }
                    removeCursorInternal(m_cursors.at(j));
                    j--;
                }
            }
        }
    } while (did_remove);

    for (int i = 0; i < m_selections.size(); i++) {
        for ( int j = 0; j < m_cursors.size(); j++ ) {
            if ( i == j ) {
                continue;
            }
            auto sel = m_selections.at(i);
            auto c = m_cursors.at(j);
            if ( sel->start() < *c && sel->end() > *c ) {
                removeCursorInternal(m_cursors.at(j));
            }
        }
    }
}

void KateMultiCursor::removeDuplicateCursors()
{
    qDebug() << "called";
    // do not consider primary cursors in frozen mode
    auto start = secondaryFrozen() ? 1 : 0;
    for (int i = start; i < m_cursors.size(); i++) {
        for (int j = start; j < i; j++) {
            if (m_cursors.at(i)->toCursor() == m_cursors.at(j)->toCursor()) {
                qDebug() << "removing duplicate cursor" << *m_cursors.at(j);
                removeCursorInternal(m_cursors.at(j));
                j--;
                i--;
            }
        }
    }

    Q_ASSERT(!m_cursors.isEmpty());
    Q_ASSERT(m_cursors.size() == m_selections.size());
}

void KateMultiCursor::removeCursorInternal(const MovingCursor::Ptr& cursor)
{
    qDebug() << "removing cursor" << *cursor;

    Q_ASSERT(m_cursors.contains(cursor));
    auto index = m_cursors.indexOf(cursor);
    m_cursors.remove(index);
    m_selections.remove(index);
    Q_ASSERT(m_cursors.size() == m_selections.size());
    Q_ASSERT(m_cursors.size() >= 1);
}

void KateMultiCursor::removeCursorInternal(int index)
{
    if (m_cursors.size() <= 1) {
        return;
    }
    m_cursors.remove(index);
    m_selections.remove(index);
    Q_ASSERT(m_cursors.size() == m_selections.size());
    Q_ASSERT(m_cursors.size() >= 1);
}

void KateMultiCursor::removeLastCursorInternal()
{
    KTextEditor::Range range;
    if (m_cursors.size() > 1) {
        m_cursors.removeLast();
    }
    if (m_selections.size() > 1) {
        range = m_selections.last()->toRange();
        m_selections.removeLast();
    }
}

KTextEditor::Cursor KateMultiCursor::toVirtualCursor(const KTextEditor::Cursor& c) const
{
    return viewInternal()->toVirtualCursor(c);
}

void KateMultiSelection::clearSelectionIfNotPersistent(bool doSubstract)
{
    if (! view()->config()->persistentSelection()) {
        clearSelection(doSubstract);
    }
}

void KateMultiSelection::clearCursorsInternal()
{
    cursors()->m_cursors.clear();
    cursors()->m_selections.clear();
}

KTextEditor::MovingRange::Ptr KateMultiSelection::addSelectionInternal(const KTextEditor::Range& newSelection,
                                                                       const Cursor& newCursor)
{
    qDebug() << "called" << newSelection << newCursor;
    Q_ASSERT(newCursor.isValid());
    cursors()->appendCursorInternal(newCursor);
    auto sel = cursors()->m_selections.last();
    sel->setRange(newSelection);
    return sel;
}

void KateMultiSelection::setSelection(const KTextEditor::Range& selection, const KTextEditor::Cursor& cursor) {
    if ( selection.isEmpty() ) {
        clearSelectionInternal();
        return;
    }
    auto newCursor = cursor.isValid() ? cursor : selection.end();
    setSelection(QVector<KTextEditor::Range> {selection}, QVector<Cursor> {newCursor});
}

void KateMultiSelection::setSelection(const QVector<KTextEditor::Range>& selection, const QVector<Cursor>& newCursors)
{
    Q_ASSERT(selection.size() == newCursors.size());
    Q_ASSERT(!selection.isEmpty());

    KateMultiCursor::CursorRepainter rep(cursors());
    clearCursorsInternal();
    for (int i = 0; i < selection.size(); i++) {
        auto cursor = newCursors.at(i).isValid() ? newCursors.at(i) : selection.at(i).end();
        addSelectionInternal(selection.at(i), cursor);
    }

    qDebug() << "new selections:" << selections();
    notifySelectionChanged();
}

void KateMultiSelection::setSelectionBlock(const KTextEditor::Range& encompassedBlock, const KateMultiCursor::Direction cursorEdge)
{
    KateMultiCursor::CursorRepainter rep(cursors());
    clearCursorsInternal();
    for ( int line = encompassedBlock.start().line(); line <= encompassedBlock.end().line(); line++ ) {
        auto startColumn = encompassedBlock.start().column();
        auto endColumn = encompassedBlock.end().column();
        startColumn = qMin(startColumn, doc()->lineLength(line));
        endColumn = qMin(endColumn, doc()->lineLength(line));
        auto start = Cursor(line, startColumn);
        auto end = Cursor(line, endColumn);
        addSelectionInternal({start, end}, cursorEdge == KateMultiCursor::Left ? start : end);
    }
    notifySelectionChanged();
}

KateMultiCursor* KateMultiSelection::cursors()
{
    return m_viewInternal->cursors();
}

int KateMultiCursor::indexOfCursor(const KTextEditor::Cursor& cursor) const
{
    for (int i = 0; i < m_cursors.size(); i++) {
        if (m_cursors.at(i)->toCursor() == cursor) {
            return i;
        }
    }
    return -1;
}

void KateMultiSelection::doSelectWithCursorInternal(const KTextEditor::Range& select, int cursorIndex)
{
    auto adjacentRange = cursors()->m_selections.at(cursorIndex);
    auto adjacent = adjacentRange->toRange();
    KTextEditor::Range intersect;
    if (!adjacentRange->isEmpty() && !(intersect = adjacentRange->toRange().intersect(select)).isEmpty()) {
        // there is an ajdacent range, toggle or shrink it
        if (adjacent.contains(select)) {
            // case 1: only shrink the adjacent range
            if (adjacent.start() == select.start()) {
                adjacentRange->setRange({select.end(), adjacent.end()});
            } else {
                adjacentRange->setRange({adjacent.start(), select.start()});
            }
        } else {
            // case 2: toggle overlapped region
            if (adjacent.start() == select.start()) {
                adjacentRange->setRange({adjacent.end(), select.end()});
            } else {
                adjacentRange->setRange({select.start(), adjacent.start()});
            }
        }
    } else if (adjacentRange->isEmpty()) {
        // new selection
        adjacentRange->setRange(select);
    } else {
        // grow selection
        adjacentRange->setRange({qMin(adjacentRange->start().toCursor(), select.start()),
                                 qMax(adjacentRange->end().toCursor(), select.end())});
    }
}

KTextEditor::MovingRange::Ptr KateMultiSelection::selectionForCursor(const KTextEditor::Cursor& cursor) const
{
    auto index = cursors()->indexOfCursor(cursor);
    Q_ASSERT(index != -1);
    return cursors()->m_selections.at(index);
}

KTextEditor::Range KateMultiSelection::primarySelection() const
{
    return *cursors()->m_selections.first();
}

KTextEditor::Range KateMultiSelection::finalSelection() const
{
    return *cursors()->m_selections.last();
}

Selections KateMultiSelection::selections() const
{
    Q_ASSERT(cursors()->m_selections.size() == cursors()->cursorsCount());
    Selections ret;
    ret.reserve(cursors()->m_selections.size());
    Q_FOREACH (const auto& r, cursors()->m_selections) {
        ret.append(r->toRange());
    }
    return ret;
}

Selections KateMultiSelection::validSelections() const
{
    auto ret = selections();
    ret.erase(std::remove_if(ret.begin(), ret.end(), [](const KTextEditor::Range& s) {
        return !s.isValid();
    }), ret.end());
    return ret;
}

bool KateMultiSelection::hasMultipleSelections() const
{
    auto s = cursors()->m_selections;
    return std::count_if(s.begin(), s.end(), [](const KTextEditor::MovingRange::Ptr & r) {
        return !r->isEmpty();
    }) > 1;
}

bool KateMultiSelection::hasSelections() const
{
    auto s = cursors()->m_selections;
    return std::any_of(s.begin(), s.end(), [](const KTextEditor::MovingRange::Ptr & r) {
        return !r->isEmpty();
    });
}

bool KateMultiSelection::positionSelected(const KTextEditor::Cursor &cursor) const
{
    KTextEditor::Cursor ret = cursor;
    if ((!view()->blockSelection()) && (ret.column() < 0)) {
        ret.setColumn(0);
    }

    auto s = cursors()->m_selections;
    return std::any_of(s.begin(), s.end(), [&cursor](const KTextEditor::MovingRange::Ptr r) {
        return r->toRange().contains(cursor);
    });
}

bool KateMultiSelection::lineSelected(int line) const
{
    auto s = cursors()->m_selections;
    return !view()->blockSelection() && std::any_of(s.begin(), s.end(),
        [line](const KTextEditor::MovingRange::Ptr r) {
            return r->toRange().containsLine(line);
        }
    );
}

void KateMultiSelection::notifySelectionChanged()
{
    Q_EMIT view()->selectionChanged(view());
}

void KateMultiSelection::clearSelection(bool doSubstract)
{
    KateMultiCursor::CursorRepainter rep(cursors());
    clearSelectionInternal(doSubstract);
    notifySelectionChanged();
}

void KateMultiSelection::clearSelectionInternal(bool doSubstract)
{
    qDebug() << " *** clearing selections";
    Q_FOREACH (auto& s, cursors()->m_selections) {
        if (doSubstract) {
            if ((s->start() <= viewInternal()->m_cursorToSubstract) && (viewInternal()->m_cursorToSubstract) <= s->end()) {
                int index = cursors()->indexOfCursor(s->end());
                if (index > -1) {
                    cursors()->removeCursorInternal(index);
                }
                s->setRange(KTextEditor::Range::invalid());
            }
        } else {
            s->setRange(KTextEditor::Range::invalid());
        }
    }
}

void KateMultiSelection::removeLastSelection()
{
    cursors()->removeLastCursorInternal();
}

bool KateMultiSelection::lineEndSelected(const KTextEditor::Cursor &lineEndPos) const
{
    auto s = cursors()->m_selections;
    return !view()->blockSelection() && std::any_of(s.begin(), s.end(),
        [&lineEndPos](const KTextEditor::MovingRange::Ptr r) {
            return (lineEndPos.line() > r->start().line() ||
                     (lineEndPos.line() == r->start().line() &&
                     (r->start().column() < lineEndPos.column() || lineEndPos.column() == -1)))
                   && (lineEndPos.line() < r->end().line() ||
                     (lineEndPos.line() == r->end().line() &&
                     (lineEndPos.column() <= r->end().column() && lineEndPos.column() != -1)));
        }
    );
}

bool KateMultiSelection::lineHasSelection(int line) const
{
    auto s = cursors()->m_selections;
    return std::any_of(s.begin(), s.end(),
        [line](const KTextEditor::MovingRange::Ptr r) {
            return r->toRange().containsLine(line);
        });
}

bool KateMultiSelection::overlapsLine(int line) const
{
    auto s = cursors()->m_selections;
    return std::any_of(s.begin(), s.end(),
        [line](const KTextEditor::MovingRange::Ptr r) {
            return r->toRange().overlapsLine(line);
        });
}

void KateMultiSelection::selectEntityAt(const KTextEditor::Cursor& cursor, KTextEditor::MovingRange::Ptr update,
                                        KateMultiSelection::SelectionMode kind)
{
    if (kind == Character) {
        if ( !update->toRange().isValid() ) {
            update->setRange(cursor, cursor);
        }
    }
    if (kind == Word) {
        update->setRange(view()->document()->wordRangeAt(cursor));
    }
    if (kind == Line) {
        auto isLastLine = cursor.line() == view()->document()->lines() - 1;
        if ( !isLastLine ) {
            update->setRange({cursor.line(), 0, cursor.line() + 1, 0});
        }
        else {
            auto lastLineLength = view()->document()->lineLength(cursor.line());
            update->setRange({cursor.line(), 0, cursor.line(), lastLineLength});
        }
    }
}

void KateMultiSelection::beginNewSelection(const KTextEditor::Cursor& fromCursor,
                                           KateMultiSelection::SelectionMode mode,
                                           KateMultiSelection::SelectionFlags flags)
{
    qDebug() << "called" << fromCursor << mode << flags;
    KateMultiCursor::CursorRepainter rep(cursors());
    m_activeSelectionMode = mode;
    if (flags & AddNewCursor) {
        cursors()->appendCursorInternal(fromCursor);
    } else {
        cursors()->clearSecondaryCursors();
        SelectingCursorMovement sel(this, flags & KeepSelectionRange);
        cursors()->m_cursors.last()->setPosition(fromCursor);
    }

    m_activeSelectingCursor = cursors()->m_cursors.last();
    selectEntityAt(fromCursor, cursors()->m_selections.last(), m_activeSelectionMode);
    m_activeSelectingCursor->setPosition(cursors()->m_selections.last()->end());

    auto newSelection = cursors()->m_selections.last()->toRange();
    Q_FOREACH (const auto& c, cursors()->m_cursors) {
        auto cur = c->toCursor();
        if (newSelection.contains(cur) && !newSelection.boundaryAtCursor(cur)) {
            // The new selection contains a cursor which existed previously.
            // Remove that.
            cursors()->removeCursorInternal(c);
        }
    }
}

void KateMultiSelection::beginNewSelection(const KTextEditor::Cursor& fromCursor,
                                           const KTextEditor::Range& range,
                                           KateMultiSelection::SelectionFlags flags)
{
    KateMultiCursor::CursorRepainter rep(cursors());

    if (flags & AddNewCursor) {
        cursors()->appendCursorInternal(fromCursor);
    } else {
        cursors()->clearSecondaryCursors();
        SelectingCursorMovement sel(this, flags & KeepSelectionRange);
        cursors()->m_cursors.last()->setPosition(fromCursor);
    }

    m_activeSelectingCursor = cursors()->m_cursors.last();

    cursors()->m_selections.last()->setRange(range.start(), range.end());
    m_activeSelectingCursor->setPosition(cursors()->m_selections.last()->end());

    auto newSelection = cursors()->m_selections.last()->toRange();
    Q_FOREACH (const auto& c, cursors()->m_cursors) {
        auto cur = c->toCursor();
        if (newSelection.contains(cur) && !newSelection.boundaryAtCursor(cur)) {
            // The new selection contains a cursor which existed previously.
            // Remove that.
            cursors()->removeCursorInternal(c);
        }
    }
}

void KateMultiSelection::updateNewSelection(const KTextEditor::Cursor& cursor)
{
    qDebug() << "called" << cursor << m_activeSelectionMode;
    auto selection = cursors()->m_selections.last();

    Q_ASSERT(m_activeSelectionMode != None);
    Q_ASSERT(!m_activeSelectingCursor.isNull());
    Q_ASSERT(m_activeSelectingCursor->isValid());
    Q_ASSERT(selection->isEmpty() || selection->toRange().boundaryAtCursor(*m_activeSelectingCursor));

    auto oldPos = m_activeSelectingCursor->toCursor();
    auto anchor = (oldPos > selection->start() ? selection->start() : selection->end()).toCursor();
    auto dir = anchor < cursor ? KateMultiCursor::Right : KateMultiCursor::Left;

    KateMultiCursor::CursorRepainter rep(cursors());
    if (anchor == cursor) {
        // If the anchor (left/right end of selection) is equal to the new cursor,
        // do not change selection, but just move the cursor there.
        m_activeSelectingCursor->setPosition(cursor);
        return;
    }

    if (m_activeSelectionMode != Character) {
        // For word and line selection, we need "two-anchor" selection handling: the word
        // which was initially double-clicked always remains selected, even when moving
        // over it in the other direction. If such wrapping occurs, make sure the word
        // stays selected and the cursor is at the right edge to continue selection.

        // FIXME: broken for line selection when moving over the position more than once.
        if (dir == KateMultiCursor::Left && oldPos == selection->end().toCursor()) {
            selectEntityAt(selection->start(), cursors()->m_selections.last(), m_activeSelectionMode);
            m_activeSelectingCursor->setPosition(selection->start());
        }
        if (dir == KateMultiCursor::Right && oldPos == selection->start().toCursor()) {
            selectEntityAt(selection->end(), cursors()->m_selections.last(), m_activeSelectionMode);
            m_activeSelectingCursor->setPosition(selection->end());
        }
    }

    if (m_activeSelectionMode == Word) {
        // word select
        auto moved = cursors()->toWordBoundary(cursor, dir);
        SelectingCursorMovement sel(this, true, true);
        m_activeSelectingCursor->setPosition(moved);
    }
    else if (m_activeSelectionMode == Line) {
        // line select
        SelectingCursorMovement sel(this, true, true);
        m_activeSelectingCursor->setColumn(anchor < cursor ? doc()->lineLength(cursor.line()) : 0);
        m_activeSelectingCursor->setLine(cursor.line());
    }
    else if (m_activeSelectionMode == Character) {
        // normal char-wise select
        SelectingCursorMovement sel(this, true, true);
        m_activeSelectingCursor->setPosition(cursor);
    }
}

bool KateMultiSelection::currentlySelecting() const
{
    return m_activeSelectionMode != None;
}

KateMultiSelection::SelectionMode KateMultiSelection::activeSelectionMode() const
{
    return m_activeSelectionMode;
}

void KateMultiSelection::finishNewSelection()
{
    qDebug() << "called finishNewSelection";
    m_activeSelectionMode = None;
    m_activeSelectingCursor.clear();
    KateMultiCursor::CursorRepainter rep(cursors());
    cursors()->removeEncompassedSecondaryCursors(KateMultiCursor::UseMostRecentCursor);
}

KateMultiSelection::SelectingCursorMovement::SelectingCursorMovement(KateMultiSelection* selections,
                                                                     bool isSelecting,
                                                                     bool allowDuplicates,
                                                                     bool doSubstract)
    : m_selections(selections)
    , m_isSelecting(isSelecting)
    , m_allowDuplicates(allowDuplicates)
    , m_doSubstract(doSubstract)
{
    Q_ASSERT(selections);
    if (m_isSelecting) {
        m_oldPositions = currentPositions();
        // always unfreeze when selecting
        m_selections->cursors()->setSecondaryFrozen(false);
    } else {
        // if moving without selecting, clear the selection
        m_selections->clearSelectionIfNotPersistent(doSubstract);
    }
}

KateMultiSelection::SelectingCursorMovement::PositionMap KateMultiSelection::SelectingCursorMovement::currentPositions() const
{
    PositionMap ret;
    Q_FOREACH (const auto c, m_selections->cursors()->movingCursors()) {
        ret.insert(c, c->toCursor());
    }
    return ret;
}

KateMultiSelection::SelectingCursorMovement::~SelectingCursorMovement()
{
    if (! m_isSelecting) {
        m_selections->cursors()->removeDuplicateCursors();
        return;
    }

    auto newPositions = currentPositions();
    Q_ASSERT(newPositions.size() == m_oldPositions.size());
    if (newPositions.size() != m_oldPositions.size()) {
        qWarning() << "cursor count changed across movement, not modifying selection";
        return;
    }
    Q_FOREACH (const auto& cursor, m_oldPositions.keys()) {
        auto old = m_oldPositions.value(cursor);
        auto current = newPositions.value(cursor);
        qDebug() << "cursor moved:" << old << " -> " << current;
        auto range = KTextEditor::Range(qMin(old, current), qMax(old, current));
        m_selections->doSelectWithCursorInternal(range, m_selections->cursors()->m_cursors.indexOf(cursor));
    }
    if (! m_allowDuplicates) {
        m_selections->cursors()->removeEncompassedSecondaryCursors();
    }
    qDebug() << "** selections after cursor movement:" << m_selections->selections();
    m_selections->notifySelectionChanged();
}

KateMultiCursor::CursorRepainter::CursorRepainter(KateMultiCursor* cursors, bool repaint)
    : m_initialAffectedLines()
    , m_cursors(cursors)
    , m_repaint(repaint)
    , m_primary(cursors->primaryCursor())
{
    if (! m_repaint) {
        return;
    }
    Q_FOREACH (const auto& c, cursors->cursors()) {
        m_initialAffectedLines.append(cursors->toVirtualCursor(c));
    }

    Q_FOREACH (auto range, cursors->selections()->selections()) {
        if (! range.isValid()) {
            continue;
        }
        // adding superfluous items here is cheap; repainting
        // is done only once for each affected line in the end
        for (int32_t line = range.start().line(); line <= range.end().line(); line++) {
            m_initialAffectedLines.append({line, 0});
        }
    }
}

KateMultiCursor::CursorRepainter::~CursorRepainter()
{
    if (m_cursors->primaryCursor() != m_primary) {
        m_cursors->viewInternal()->notifyPrimaryCursorChanged(m_cursors->primaryCursor());
    }

    if (! m_repaint) {
        return;
    }

    QVector<KTextEditor::Cursor> resulting = m_initialAffectedLines;
    Q_FOREACH (const auto& cursor, m_cursors->cursors()) {
        Q_ASSERT(cursor.isValid());
        auto viewCursor = m_cursors->toVirtualCursor(cursor);
        if (!resulting.contains(viewCursor)) {
            resulting.append(viewCursor);
        }
    }
    Q_FOREACH (auto range, m_cursors->selections()->selections()) {
        if (! range.isValid()) {
            continue;
        }
        // TODO: only repaint changed selections
        for (int32_t line = range.start().line(); line <= range.end().line(); line++) {
            auto pos = Cursor{line, 0};
            if (pos.isValid() && ! resulting.contains(pos)) {
                resulting.append(pos);
            }
        }
    }
    qDebug() << "repaint:" << resulting;
    m_cursors->viewInternal()->notifyLinesUpdated(resulting);
}
