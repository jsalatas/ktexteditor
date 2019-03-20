/* This file is part of the KDE and the Kate project
*
*   Copyright (C) 2016 Sven Brauch <mail@svenbrauch.de>
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

#ifndef KATEMULTICURSOR_H
#define KATEMULTICURSOR_H

#include "ktexteditor/movingrange.h"

namespace KTextEditor
{
class DocumentPrivate;
class ViewPrivate;
}

class KateMultiSelection;
class KateViewInternal;

using KTextEditor::MovingCursor;
using KTextEditor::MovingRange;
using KTextEditor::Cursor;

using Cursors = QVector<Cursor>;
using Selections = QVector<KTextEditor::Range>;

class KTEXTEDITOR_EXPORT KateMultiCursor
{
public:
    friend class KateMultiSelection;

    enum Direction {
        Left = -1,
        None = 0,
        Right = +1
    };

    KateMultiCursor(KateViewInternal* view);

    Cursors cursors() const;
    const QVector<KTextEditor::MovingCursor::Ptr> movingCursors() const;
    Cursor primaryCursor() const;
    Cursors secondaryCursors() const;
    bool hasSecondaryCursors() const;
    int cursorsCount() const;

    void setPrimaryCursor(const Cursor& cursor, bool repaint = true, bool select = false, bool doSubstract = false);
    /// do not touch selection
    void setPrimaryCursorWithoutSelection(const Cursor& cursor, bool repaint = true);

    bool toggleSecondaryCursorAt(const Cursor& cursor, bool ensureExists = false);
    void clearSecondaryCursors();

    void moveCursorsLeft(bool select = false, int32_t chars = 1);
    void moveCursorsRight(bool select = false, int32_t chars = 1);
    void moveCursorsUp(bool select = false, int32_t chars = 1);
    void moveCursorsDown(bool select = false, int32_t chars = 1);
    void moveCursorsEndOfLine(bool select = false);
    void moveCursorsStartOfLine(bool select = false);
    void moveCursorsWordPrevious(bool select = false);
    void moveCursorsWordNext(bool select = false);
    void moveCursorsTopHome(bool select = false);
    void moveCursorsBottomEnd(bool select = false);

    void removeDuplicateCursors();

    bool secondaryFrozen() const
    {
        return m_secondaryFrozen;
    }

    void toggleSecondaryFrozen()
    {
        return setSecondaryFrozen(!m_secondaryFrozen);
    }
    /**
     * @brief Freeze secondary cursors.
     *
     * This means they will not move when the user navigates the
     * primary cursor. Typing or removing chars automatically unfreezes
     * cursors.
     *
     * @param frozen true to freeze, false to unfreeze.
     */
    void setSecondaryFrozen(bool frozen)
    {
        m_secondaryFrozen = frozen;
    };

    const KateMultiSelection* selections() const;
    KateMultiSelection* selections();

protected:
    /// Cursor transformations. Functions to calculate where
    /// a given cursor moves under a certain operation.
    Cursor moveLeftRight(const Cursor& c, int32_t chars) const;
    Cursor moveUpDown(const Cursor& c, int32_t direction, int32_t& xpos) const;
    Cursor moveWord(const Cursor& c, Direction dir) const;
    Cursor toWordBoundary(const Cursor& c, Direction dir) const;
    bool cursorAtWordBoundary(const Cursor& c) const;
    Cursor moveHome(const Cursor& c) const;
    Cursor moveEnd(const Cursor& c) const;

public:
    KTextEditor::ViewPrivate* view() const;
    KateViewInternal* viewInternal() const;
    KTextEditor::DocumentPrivate* doc() const;

private:
    int indexOfCursor(const KTextEditor::Cursor& cursor) const;

private:
    KateViewInternal* m_viewInternal = nullptr;
    QVector<MovingCursor::Ptr> m_cursors;
    // It is guaranteed that this always contains exactly one selection
    // for each cursor, and in the same order.
    QVector<KTextEditor::MovingRange::Ptr> m_selections;
    QMap<MovingCursor*, int32_t> m_savedHorizontalPositions;

    bool m_secondaryFrozen = false;

public:
    void removeLastCursorInternal();

private:
    QVector<MovingCursor::Ptr> allCursors() const;
    void appendCursorInternal(const Cursor& cursor);
    void removeCursorInternal(const MovingCursor::Ptr& cursor);
    void removeCursorInternal(int index);
    enum CursorSelectionFlags {
        NoFlags = 0x0,
        UseMostRecentCursor = 0x1
    };
    void removeEncompassedSecondaryCursors(CursorSelectionFlags flags = NoFlags);

private:
    KTextEditor::Cursor toVirtualCursor(const KTextEditor::Cursor& c) const;

public:
    class CursorRepainter
    {
    public:
        CursorRepainter(KateMultiCursor* cursors, bool repaint = true);
        ~CursorRepainter();

    private:
        QVector<KTextEditor::Cursor> m_initialAffectedLines;
        KateMultiCursor* m_cursors;
        const bool m_repaint;
        Cursor m_primary;
    };
    friend class CursorRepainter;
};

class KTEXTEDITOR_EXPORT KateMultiSelection
{
public:
    KateMultiSelection(KateViewInternal* view);

    KTextEditor::Range primarySelection() const;
    KTextEditor::Range finalSelection() const;
    bool hasMultipleSelections() const;
    bool hasSelections() const;
    Selections selections() const;
    Selections validSelections() const;
    KTextEditor::MovingRange::Ptr selectionForCursor(const KTextEditor::Cursor& cursor) const;

    void setSelection(const KTextEditor::Range& selection, const Cursor& cursor = Cursor::invalid());
    void setSelection(const QVector<KTextEditor::Range>& selection, const QVector<Cursor>& cursors);
    void setSelectionBlock(const KTextEditor::Range& encompassedBlock, const KateMultiCursor::Direction cursorEdge);
    void clearSelection(bool doSubstract = false);
    void clearSelectionIfNotPersistent(bool doSubstract = false);
    void removeLastSelection();

    // Mouse selection
    enum SelectionMode {
        None,
        Character,
        Word,
        Line
    };
    enum SelectionFlag {
        UsePrimaryCursor = 0x1,
        AddNewCursor = 0x2,
        KeepSelectionRange = 0x4
    };
    Q_DECLARE_FLAGS(SelectionFlags, SelectionFlag)
    void beginNewSelection(const Cursor& fromCursor, SelectionMode mode = Character, SelectionFlags flags = UsePrimaryCursor);
    void beginNewSelection(const Cursor& fromCursor, const KTextEditor::Range& range, SelectionFlags flags = UsePrimaryCursor);
    void updateNewSelection(const Cursor& cursor);
    void finishNewSelection();
    bool currentlySelecting() const;
    SelectionMode activeSelectionMode() const;

public:
    bool positionSelected(const Cursor& cursor) const;
    bool lineSelected(int line) const;
    bool lineEndSelected(const Cursor& lineEnd) const;
    bool lineHasSelection(int line) const;
    bool overlapsLine(int line) const;

private:
    const KateMultiCursor* cursors() const;
    KateMultiCursor* cursors();
    KTextEditor::ViewPrivate* view() const;
    KateViewInternal* viewInternal() const;
    KTextEditor::DocumentPrivate* doc() const;
    void notifySelectionChanged();

    KTextEditor::MovingRange::Ptr addSelectionInternal(const KTextEditor::Range& range, const Cursor& cursor);
    void doSelectWithCursorInternal(const KTextEditor::Range& range, int cursorIndex);
    void selectEntityAt(const Cursor& cursor, KTextEditor::MovingRange::Ptr update, SelectionMode kind);

    /**
     * @brief Clear the selection, i.e. set all selection ranges to empty.
     */
    void clearSelectionInternal(bool doSubstract = false);

    /**
     * @brief Removes *all* cursors and selections, including the primary cursor.
     * Make sure to add at least one new cursor after calling this.
     */
    void clearCursorsInternal();

private:
    KateViewInternal* m_viewInternal = nullptr;

private:
    // members for mouse selection
    SelectionMode m_activeSelectionMode = None;
    KTextEditor::MovingCursor::Ptr m_activeSelectingCursor;

public:
    class SelectingCursorMovement
    {
    public:
        SelectingCursorMovement(KateMultiSelection* selections, bool isSelecting = true, bool allowDuplicates = false, bool doSubtract = false);
        ~SelectingCursorMovement();

    private:
        using PositionMap = QMap<KTextEditor::MovingCursor::Ptr, KTextEditor::Cursor>;
        KateMultiSelection* m_selections;
        bool m_isSelecting;
        PositionMap m_oldPositions;
        PositionMap currentPositions() const;
        bool m_allowDuplicates;
        bool m_doSubstract;
    };
    friend class SelectingCursorMovement;
    friend class CursorRepainter;
};

Q_DECLARE_OPERATORS_FOR_FLAGS(KateMultiSelection::SelectionFlags)

#endif // KATEMULTICURSOR_H
