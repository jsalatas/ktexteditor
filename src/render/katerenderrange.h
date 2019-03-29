/*  This file is part of the KDE libraries and the Kate part.
 *
 *  Copyright (C) 2003-2005 Hamish Rodda <rodda@kde.org>
 *  Copyright (C) 2008 David Nolden <david.nolden.kdevelop@art-master.de>
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

#ifndef KATERENDERRANGE_H
#define KATERENDERRANGE_H

#include <ktexteditor/range.h>
#include <ktexteditor/attribute.h>

#include <QList>
#include <QPair>

class KateRenderRange
{
public:
    virtual ~KateRenderRange() {}
    virtual KTextEditor::Cursor nextBoundary() const = 0;
    virtual bool advanceTo(const KTextEditor::Cursor &pos) = 0;
    virtual KTextEditor::Attribute::Ptr currentAttribute() const = 0;
    virtual bool isReady() const;
};

typedef QPair<KTextEditor::Range *, KTextEditor::Attribute::Ptr> pairRA;

class NormalRenderRange : public KateRenderRange
{
public:
    NormalRenderRange();
    ~NormalRenderRange() override;

    void addRange(KTextEditor::Range *range, KTextEditor::Attribute::Ptr attribute);

    KTextEditor::Cursor nextBoundary() const override;
    bool advanceTo(const KTextEditor::Cursor &pos) override;
    KTextEditor::Attribute::Ptr currentAttribute() const override;

private:
    QVector<pairRA> m_ranges;
    KTextEditor::Cursor m_nextBoundary;
    KTextEditor::Attribute::Ptr m_currentAttribute;
    int m_currentRange = 0;
};

class RenderRangeList : public QVector<KateRenderRange *>
{
public:
    ~RenderRangeList();
    KTextEditor::Cursor nextBoundary() const;
    void advanceTo(const KTextEditor::Cursor &pos);
    bool hasAttribute() const;
    KTextEditor::Attribute::Ptr generateAttribute() const;

private:
    KTextEditor::Cursor m_currentPos;
};

#endif
