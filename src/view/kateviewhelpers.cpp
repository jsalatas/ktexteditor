/* This file is part of the KDE libraries
   Copyright (C) 2008, 2009 Matthew Woehlke <mw_triad@users.sourceforge.net>
   Copyright (C) 2007 Mirko Stocker <me@misto.ch>
   Copyright (C) 2002 John Firebaugh <jfirebaugh@kde.org>
   Copyright (C) 2001 Anders Lund <anders@alweb.dk>
   Copyright (C) 2001 Christoph Cullmann <cullmann@kde.org>
   Copyright (C) 2011 Svyatoslav Kuzmich <svatoslav1@gmail.com>
   Copyright (C) 2012 Kåre Särs <kare.sars@iki.fi> (Minimap)
   Copyright 2017-2018 Friedrich W. H. Kossebau <kossebau@kde.org>

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

#include "kateviewhelpers.h"

#include "katecmd.h"
#include <ktexteditor/attribute.h>
#include <ktexteditor/annotationinterface.h>
#include <ktexteditor/movingrange.h>
#include "kateconfig.h"
#include "katedocument.h"
#include <katebuffer.h>
#include "katerenderer.h"
#include "kateannotationitemdelegate.h"
#include "kateview.h"
#include "kateviewinternal.h"
#include "katelayoutcache.h"
#include "katetextlayout.h"
#include "kateglobal.h"
#include "katepartdebug.h"
#include "katecommandrangeexpressionparser.h"
#include "kateabstractinputmode.h"
#include "katetextpreview.h"

#include <KCharsets>
#include <KColorUtils>
#include <KLocalizedString>
#include <KConfigGroup>
#include <khelpclient.h>

#include <QRegExp>
#include <QTextCodec>
#include <QTimer>
#include <QVariant>
#include <QtAlgorithms>
#include <QCursor>
#include <QKeyEvent>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPen>
#include <QAction>
#include <QActionGroup>
#include <QBoxLayout>
#include <QMenu>
#include <QStyle>
#include <QStyleOption>
#include <QToolButton>
#include <QToolTip>
#include <QWhatsThis>

#include <math.h>

//BEGIN KateMessageLayout
KateMessageLayout::KateMessageLayout(QWidget *parent)
    : QLayout(parent)
{
}

KateMessageLayout::~KateMessageLayout()
{
    while (QLayoutItem *item = takeAt(0))
        delete item;
}

void KateMessageLayout::addItem(QLayoutItem *item)
{
    Q_ASSERT(false);
    add(item, KTextEditor::Message::CenterInView);
}

void KateMessageLayout::addWidget(QWidget *widget, KTextEditor::Message::MessagePosition pos)
{
    add(new QWidgetItem(widget), pos);
}

int KateMessageLayout::count() const
{
    return m_items.size();
}

QLayoutItem *KateMessageLayout::itemAt(int index) const
{
    if (index < 0 || index >= m_items.size())
        return nullptr;

    return m_items[index]->item;
}

void KateMessageLayout::setGeometry(const QRect &rect)
{
    QLayout::setGeometry(rect);
    const int s = spacing();
    const QRect adjustedRect = rect.adjusted(s, s, -s, -s);

    for (auto wrapper : m_items) {
        QLayoutItem *item = wrapper->item;
        auto position = wrapper->position;

        if (position == KTextEditor::Message::TopInView) {
            const QRect r(adjustedRect.width() - item->sizeHint().width(), s, item->sizeHint().width(), item->sizeHint().height());
            item->setGeometry(r);
        } else if (position == KTextEditor::Message::BottomInView) {
            const QRect r(adjustedRect.width() - item->sizeHint().width(), adjustedRect.height() - item->sizeHint().height(), item->sizeHint().width(), item->sizeHint().height());
            item->setGeometry(r);
        } else if (position == KTextEditor::Message::CenterInView) {
            QRect r(0, 0, item->sizeHint().width(), item->sizeHint().height());
            r.moveCenter(adjustedRect.center());
            item->setGeometry(r);
        } else {
            Q_ASSERT_X(false, "setGeometry", "Only TopInView, CenterInView, and BottomInView are supported.");
        }
    }
}

QSize KateMessageLayout::sizeHint() const
{
    return QSize();
}

QLayoutItem *KateMessageLayout::takeAt(int index)
{
    if (index >= 0 && index < m_items.size()) {
        ItemWrapper *layoutStruct = m_items.takeAt(index);
        return layoutStruct->item;
    }
    return nullptr;
}

void KateMessageLayout::add(QLayoutItem *item, KTextEditor::Message::MessagePosition pos)
{
    m_items.push_back(new ItemWrapper(item, pos));
}
//END KateMessageLayout

//BEGIN KateScrollBar
static const int s_lineWidth = 100;
static const int s_pixelMargin = 8;
static const int s_linePixelIncLimit = 6;

const unsigned char KateScrollBar::characterOpacity[256] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // <- 15
    0, 0, 0, 0, 0, 0, 0, 0, 255, 0, 255, 0, 0, 0, 0, 0,  // <- 31
    0, 125, 41, 221, 138, 195, 218, 21, 142, 142, 137, 137, 97, 87, 87, 140,  // <- 47
    223, 164, 183, 190, 191, 193, 214, 158, 227, 216, 103, 113, 146, 140, 146, 149,  // <- 63
    248, 204, 240, 174, 217, 197, 178, 205, 209, 176, 168, 211, 160, 246, 238, 218,  // <- 79
    195, 229, 227, 196, 167, 212, 188, 238, 197, 169, 189, 158, 21, 151, 115, 90,  // <- 95
    15, 192, 209, 153, 208, 187, 162, 221, 183, 149, 161, 191, 146, 203, 167, 182,  // <- 111
    208, 203, 139, 166, 158, 167, 157, 189, 164, 179, 156, 167, 145, 166, 109, 0,  // <- 127
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // <- 143
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // <- 159
    0, 125, 184, 187, 146, 201, 127, 203, 89, 194, 156, 141, 117, 87, 202, 88,  // <- 175
    115, 165, 118, 121, 85, 190, 236, 87, 88, 111, 151, 140, 194, 191, 203, 148,  // <- 191
    215, 215, 222, 224, 223, 234, 230, 192, 208, 208, 216, 217, 187, 187, 194, 195,  // <- 207
    228, 255, 228, 228, 235, 239, 237, 150, 255, 222, 222, 229, 232, 180, 197, 225,  // <- 223
    208, 208, 216, 217, 212, 230, 218, 170, 202, 202, 211, 204, 156, 156, 165, 159,  // <- 239
    214, 194, 197, 197, 206, 206, 201, 132, 214, 183, 183, 192, 187, 195, 227, 198
};

KateScrollBar::KateScrollBar(Qt::Orientation orientation, KateViewInternal *parent)
    : QScrollBar(orientation, parent->m_view)
    , m_middleMouseDown(false)
    , m_leftMouseDown(false)
    , m_view(parent->m_view)
    , m_doc(parent->doc())
    , m_viewInternal(parent)
    , m_textPreview(nullptr)
    , m_showMarks(false)
    , m_showMiniMap(false)
    , m_miniMapAll(true)
    , m_miniMapWidth(40)
    , m_grooveHeight(height())
    , m_linesModified(0)
{
    connect(this, SIGNAL(valueChanged(int)), this, SLOT(sliderMaybeMoved(int)));
    connect(m_doc, SIGNAL(marksChanged(KTextEditor::Document*)), this, SLOT(marksChanged()));

    m_updateTimer.setInterval(300);
    m_updateTimer.setSingleShot(true);
    QTimer::singleShot(10, this, SLOT(updatePixmap()));

    // track mouse for text preview widget
    setMouseTracking(orientation == Qt::Vertical);

    // setup text preview timer
    m_delayTextPreviewTimer.setSingleShot(true);
    m_delayTextPreviewTimer.setInterval(250);
    connect(&m_delayTextPreviewTimer, SIGNAL(timeout()), this, SLOT(showTextPreview()));
}

KateScrollBar::~KateScrollBar()
{
    delete m_textPreview;
}

void KateScrollBar::setShowMiniMap(bool b)
{
    if (b && !m_showMiniMap) {
        connect(m_view, SIGNAL(selectionChanged(KTextEditor::View*)), &m_updateTimer, SLOT(start()), Qt::UniqueConnection);
        connect(m_doc, SIGNAL(textChanged(KTextEditor::Document*)), &m_updateTimer, SLOT(start()), Qt::UniqueConnection);
        connect(m_view, SIGNAL(delayedUpdateOfView()), &m_updateTimer, SLOT(start()), Qt::UniqueConnection);
        connect(&m_updateTimer, SIGNAL(timeout()), this, SLOT(updatePixmap()), Qt::UniqueConnection);
        connect(&(m_view->textFolding()), SIGNAL(foldingRangesChanged()), &m_updateTimer, SLOT(start()), Qt::UniqueConnection);
    } else if (!b) {
        disconnect(&m_updateTimer);
    }

    m_showMiniMap = b;

    updateGeometry();
    update();
}

QSize KateScrollBar::sizeHint() const
{
    if (m_showMiniMap) {
        return QSize(m_miniMapWidth, QScrollBar::sizeHint().height());
    }
    return QScrollBar::sizeHint();
}

int KateScrollBar::minimapYToStdY(int y)
{
    // Check if the minimap fills the whole scrollbar
    if (m_stdGroveRect.height() == m_mapGroveRect.height()) {
        return y;
    }

    // check if y is on the step up/down
    if ((y < m_stdGroveRect.top()) || (y > m_stdGroveRect.bottom())) {
        return y;
    }

    if (y < m_mapGroveRect.top()) {
        return m_stdGroveRect.top() + 1;
    }

    if (y > m_mapGroveRect.bottom()) {
        return m_stdGroveRect.bottom() - 1;
    }

    // check for div/0
    if (m_mapGroveRect.height() == 0) {
        return y;
    }

    int newY = (y - m_mapGroveRect.top()) * m_stdGroveRect.height() / m_mapGroveRect.height();
    newY += m_stdGroveRect.top();
    return newY;
}

void KateScrollBar::mousePressEvent(QMouseEvent *e)
{
    // delete text preview
    hideTextPreview();

    if (e->button() == Qt::MidButton) {
        m_middleMouseDown = true;
    } else if (e->button() == Qt::LeftButton) {
        m_leftMouseDown = true;
    }

    if (m_showMiniMap) {
        if (m_leftMouseDown) {
            // if we show the minimap left-click jumps directly to the selected position
            int newVal = (e->pos().y()-m_mapGroveRect.top()) / (double)m_mapGroveRect.height() * (double)(maximum()+pageStep()) - pageStep()/2;
            newVal = qBound(0, newVal, maximum());
            setSliderPosition(newVal);
        }
        QMouseEvent eMod(QEvent::MouseButtonPress,
                         QPoint(6, minimapYToStdY(e->pos().y())),
                         e->button(), e->buttons(), e->modifiers());
        QScrollBar::mousePressEvent(&eMod);
    } else {
        QScrollBar::mousePressEvent(e);
    }

    m_toolTipPos = e->globalPos() - QPoint(e->pos().x(), 0);
    const int fromLine = m_viewInternal->toRealCursor(m_viewInternal->startPos()).line() + 1;
    const int lastLine = m_viewInternal->toRealCursor(m_viewInternal->endPos()).line() + 1;
    QToolTip::showText(m_toolTipPos, i18nc("from line - to line", "<center>%1<br/>&#x2014;<br/>%2</center>", fromLine, lastLine), this);

    redrawMarks();
}

void KateScrollBar::mouseReleaseEvent(QMouseEvent *e)
{
    if (e->button() == Qt::MidButton) {
        m_middleMouseDown = false;
    } else if (e->button() == Qt::LeftButton) {
        m_leftMouseDown = false;
    }

    redrawMarks();

    if (m_leftMouseDown || m_middleMouseDown) {
        QToolTip::hideText();
    }

    if (m_showMiniMap) {
        QMouseEvent eMod(QEvent::MouseButtonRelease,
                         QPoint(e->pos().x(), minimapYToStdY(e->pos().y())),
                         e->button(), e->buttons(), e->modifiers());
        QScrollBar::mouseReleaseEvent(&eMod);
    } else {
        QScrollBar::mouseReleaseEvent(e);
    }
}

void KateScrollBar::mouseMoveEvent(QMouseEvent *e)
{
    if (m_showMiniMap) {
        QMouseEvent eMod(QEvent::MouseMove,
                         QPoint(e->pos().x(), minimapYToStdY(e->pos().y())),
                         e->button(), e->buttons(), e->modifiers());
        QScrollBar::mouseMoveEvent(&eMod);
    } else {
        QScrollBar::mouseMoveEvent(e);
    }

    if (e->buttons() & (Qt::LeftButton | Qt::MidButton)) {
        redrawMarks();

        // current line tool tip
        m_toolTipPos = e->globalPos() - QPoint(e->pos().x(), 0);
        const int fromLine = m_viewInternal->toRealCursor(m_viewInternal->startPos()).line() + 1;
        const int lastLine = m_viewInternal->toRealCursor(m_viewInternal->endPos()).line() + 1;
        QToolTip::showText(m_toolTipPos, i18nc("from line - to line", "<center>%1<br/>&#x2014;<br/>%2</center>", fromLine, lastLine), this);
    }

    showTextPreviewDelayed();
}

void KateScrollBar::leaveEvent(QEvent *event)
{
    hideTextPreview();

    QAbstractSlider::leaveEvent(event);
}

bool KateScrollBar::eventFilter(QObject *object, QEvent *event)
{
    Q_UNUSED(object)

    if (m_textPreview && event->type() == QEvent::WindowDeactivate) {
        // We need hide the scrollbar TextPreview widget
        hideTextPreview();
    }

    return false;
}

void KateScrollBar::paintEvent(QPaintEvent *e)
{
    if (m_doc->marks().size() != m_lines.size()) {
        recomputeMarksPositions();
    }
    if (m_showMiniMap) {
        miniMapPaintEvent(e);
    } else {
        normalPaintEvent(e);
    }
}

void KateScrollBar::showTextPreviewDelayed()
{
    if (!m_textPreview) {
        if (!m_delayTextPreviewTimer.isActive()) {
            m_delayTextPreviewTimer.start();
        }
    } else {
        showTextPreview();
    }
}

void KateScrollBar::showTextPreview()
{
    if (orientation() != Qt::Vertical || isSliderDown() || (minimum() == maximum()) || !m_view->config()->scrollBarPreview()) {
        return;
    }

    // only show when main window is active (#392396)
    if (window() && !window()->isActiveWindow()) {
        return;
    }

    QRect grooveRect;
    if (m_showMiniMap) {
        // If mini-map is shown, the height of the map might not be the whole height
        grooveRect = m_mapGroveRect;
    } else {
        QStyleOptionSlider opt;
        opt.init(this);
        opt.subControls = QStyle::SC_None;
        opt.activeSubControls = QStyle::SC_None;
        opt.orientation = orientation();
        opt.minimum = minimum();
        opt.maximum = maximum();
        opt.sliderPosition = sliderPosition();
        opt.sliderValue = value();
        opt.singleStep = singleStep();
        opt.pageStep = pageStep();

        grooveRect = style()->subControlRect(QStyle::CC_ScrollBar, &opt, QStyle::SC_ScrollBarGroove, this);
    }

    if (m_view->config()->scrollPastEnd()) {
        // Adjust the grove size to accommodate the added pageStep at the bottom
        int adjust = pageStep()*grooveRect.height() / (maximum() + pageStep() - minimum());
        grooveRect.adjust(0,0,0, -adjust);
    }

    const QPoint cursorPos = mapFromGlobal(QCursor::pos());
    if (grooveRect.contains(cursorPos)) {

        if (!m_textPreview) {
            m_textPreview = new KateTextPreview(m_view, this);
            m_textPreview->setAttribute(Qt::WA_ShowWithoutActivating);
            m_textPreview->setFrameStyle(QFrame::StyledPanel);

            // event filter to catch application WindowDeactivate event, to hide the preview window
            qApp->installEventFilter(this);
        }

        const qreal posInPercent = static_cast<double>(cursorPos.y() - grooveRect.top()) / grooveRect.height();
        const qreal startLine = posInPercent * m_view->textFolding().visibleLines();

        m_textPreview->resize(m_view->width() / 2, m_view->height() / 5);
        const int xGlobal = mapToGlobal(QPoint(0, 0)).x();
        const int yGlobal = qMin(mapToGlobal(QPoint(0, height())).y() - m_textPreview->height(),
                                 qMax(mapToGlobal(QPoint(0, 0)).y(), mapToGlobal(cursorPos).y() - m_textPreview->height() / 2));
        m_textPreview->move(xGlobal - m_textPreview->width(), yGlobal);
        m_textPreview->setLine(startLine);
        m_textPreview->setCenterView(true);
        m_textPreview->setScaleFactor(0.8);
        m_textPreview->raise();
        m_textPreview->show();
    } else {
        hideTextPreview();
    }
}

void KateScrollBar::hideTextPreview()
{
    if (m_delayTextPreviewTimer.isActive()) {
        m_delayTextPreviewTimer.stop();
    }

    qApp->removeEventFilter(this);
    delete m_textPreview;
}

// This function is optimized for bing called in sequence.
const QColor KateScrollBar::charColor(const QVector<Kate::TextLineData::Attribute> &attributes, int &attributeIndex,
                                      const QVector<QTextLayout::FormatRange> &decorations,
                                      const QColor &defaultColor, int x, QChar ch)
{
    QColor color = defaultColor;

    bool styleFound = false;

    // Query the decorations, that is, things like search highlighting, or the
    // KDevelop DUChain highlighting, for a color to use
    foreach (const QTextLayout::FormatRange &range, decorations) {
        if (range.start <= x && range.start + range.length > x) {
            // If there's a different background color set (search markers, ...)
            // use that, otherwise use the foreground color.
            if (range.format.hasProperty(QTextFormat::BackgroundBrush)) {
                color = range.format.background().color();
            } else {
                color = range.format.foreground().color();
            }
            styleFound = true;
            break;
        }
    }

    // If there's no decoration set for the current character (this will mostly be the case for
    // plain Kate), query the styles, that is, the default kate syntax highlighting.
    if (!styleFound) {
        // go to the block containing x
        while ((attributeIndex < attributes.size()) &&
                ((attributes[attributeIndex].offset + attributes[attributeIndex].length) < x)) {
            ++attributeIndex;
        }
        if ((attributeIndex < attributes.size()) && (x < attributes[attributeIndex].offset + attributes[attributeIndex].length)) {
            color = m_view->renderer()->attribute(attributes[attributeIndex].attributeValue)->foreground().color();
        }
    }

    // Query how much "blackness" the character has.
    // This causes for example a dot or a dash to appear less intense
    // than an A or similar.
    // This gives the pixels created a bit of structure, which makes it look more
    // like real text.
    color.setAlpha((ch.unicode() < 256) ? characterOpacity[ch.unicode()] : 222);

    return color;
}

void KateScrollBar::updatePixmap()
{
    //QTime time;
    //time.start();

    if (!m_showMiniMap) {
        // make sure no time is wasted if the option is disabled
        return;
    }

    // For performance reason, only every n-th line will be drawn if the widget is
    // sufficiently small compared to the amount of lines in the document.
    int docLineCount = m_view->textFolding().visibleLines();
    int pixmapLineCount = docLineCount;
    if (m_view->config()->scrollPastEnd()) {
        pixmapLineCount += pageStep();
    }
    int pixmapLinesUnscaled = pixmapLineCount;
    if (m_grooveHeight < 5) {
        m_grooveHeight = 5;
    }
    int lineDivisor = pixmapLinesUnscaled / m_grooveHeight;
    if (lineDivisor < 1) {
        lineDivisor = 1;
    }
    int charIncrement = 1;
    int lineIncrement = 1;
    if ((m_grooveHeight > 10) && (pixmapLineCount >= m_grooveHeight * 2)) {
        charIncrement = pixmapLineCount / m_grooveHeight;
        while (charIncrement > s_linePixelIncLimit) {
            lineIncrement++;
            pixmapLineCount = pixmapLinesUnscaled / lineIncrement;
            charIncrement = pixmapLineCount / m_grooveHeight;
        }
        pixmapLineCount /= charIncrement;
    }

    int pixmapLineWidth = s_pixelMargin + s_lineWidth / charIncrement;

    //qCDebug(LOG_KTE) << "l" << lineIncrement << "c" << charIncrement << "d" << lineDivisor;
    //qCDebug(LOG_KTE) << "pixmap" << pixmapLineCount << pixmapLineWidth << "docLines" << m_view->textFolding().visibleLines() << "height" << m_grooveHeight;

    const QColor backgroundColor = m_view->defaultStyleAttribute(KTextEditor::dsNormal)->background().color();
    const QColor defaultTextColor = m_view->defaultStyleAttribute(KTextEditor::dsNormal)->foreground().color();
    const QColor selectionBgColor = m_view->renderer()->config()->selectionColor();
    QColor modifiedLineColor = m_view->renderer()->config()->modifiedLineColor();
    QColor savedLineColor = m_view->renderer()->config()->savedLineColor();
    // move the modified line color away from the background color
    modifiedLineColor.setHsv(modifiedLineColor.hue(), 255, 255 - backgroundColor.value() / 3);
    savedLineColor.setHsv(savedLineColor.hue(), 100, 255 - backgroundColor.value() / 3);

    // increase dimensions by ratio
    m_pixmap = QPixmap(pixmapLineWidth * m_view->devicePixelRatioF(), pixmapLineCount * m_view->devicePixelRatioF());
    m_pixmap.fill(QColor("transparent"));

    // The text currently selected in the document, to be drawn later.
    const KTextEditor::Range &selection = m_view->selectionRange();

    QPainter painter;
    if (painter.begin(&m_pixmap)) {
        // init pen once, afterwards, only change it if color changes to avoid a lot of allocation for setPen
        painter.setPen(selectionBgColor);

        // Do not force updates of the highlighting if the document is very large
        bool simpleMode = m_doc->lines() > 7500;

        int pixelY = 0;
        int drawnLines = 0;

        // Iterate over all visible lines, drawing them.
        for (int virtualLine = 0; virtualLine < docLineCount; virtualLine += lineIncrement) {

            int realLineNumber = m_view->textFolding().visibleLineToLine(virtualLine);
            QString lineText = m_doc->line(realLineNumber);

            if (!simpleMode) {
                m_doc->buffer().ensureHighlighted(realLineNumber);
            }
            const Kate::TextLine &kateline = m_doc->plainKateTextLine(realLineNumber);

            const QVector<Kate::TextLineData::Attribute> &attributes = kateline->attributesList();
            QVector<QTextLayout::FormatRange> decorations = m_view->renderer()->decorationsForLine(kateline, realLineNumber);
            int attributeIndex = 0;

            // Draw selection if it is on an empty line
            if (selection.contains(KTextEditor::Cursor(realLineNumber, 0)) && lineText.size() == 0) {
                if (selectionBgColor != painter.pen().color()) {
                    painter.setPen(selectionBgColor);
                }
                painter.drawLine(s_pixelMargin, pixelY, s_pixelMargin + s_lineWidth - 1, pixelY);
            }

            // Iterate over the line to draw the background
            int selStartX = -1;
            int selEndX = -1;
            int pixelX = s_pixelMargin; // use this to control the offset of the text from the left
            for (int x = 0; (x < lineText.size() && x < s_lineWidth); x += charIncrement) {
                if (pixelX >= s_lineWidth + s_pixelMargin) {
                    break;
                }
                // Query the selection and draw it behind the character
                if (selection.contains(KTextEditor::Cursor(realLineNumber, x))) {
                    if (selStartX == -1) selStartX = pixelX;
                    selEndX = pixelX;
                    if (lineText.size() - 1 == x) {
                        selEndX = s_lineWidth + s_pixelMargin-1;
                    }
                }

                if (lineText[x] == QLatin1Char('\t')) {
                    pixelX += qMax(4 / charIncrement, 1); // FIXME: tab width...
                } else {
                    pixelX++;
                }
            }

            if (selStartX != -1) {
                if (selectionBgColor != painter.pen().color()) {
                    painter.setPen(selectionBgColor);
                }
                painter.drawLine(selStartX, pixelY, selEndX, pixelY);
            }

            // Iterate over all the characters in the current line
            pixelX = s_pixelMargin;
            for (int x = 0; (x < lineText.size() && x < s_lineWidth); x += charIncrement) {
                if (pixelX >= s_lineWidth + s_pixelMargin) {
                    break;
                }

                // draw the pixels
                if (lineText[x] == QLatin1Char(' ')) {
                    pixelX++;
                } else if (lineText[x] == QLatin1Char('\t')) {
                    pixelX += qMax(4 / charIncrement, 1); // FIXME: tab width...
                } else {
                    const QColor newPenColor(charColor(attributes, attributeIndex, decorations, defaultTextColor, x, lineText[x]));
                    if (newPenColor != painter.pen().color()) {
                        painter.setPen(newPenColor);
                    }

                    // Actually draw the pixel with the color queried from the renderer.
                    painter.drawPoint(pixelX, pixelY);

                    pixelX++;
                }

            }
            drawnLines++;
            if (((drawnLines) % charIncrement) == 0) {
                pixelY++;
            }

        }
        //qCDebug(LOG_KTE) << drawnLines;
        // Draw line modification marker map.
        // Disable this if the document is really huge,
        // since it requires querying every line.
        if (m_doc->lines() < 50000) {
            for (int lineno = 0; lineno < docLineCount; lineno++) {
                int realLineNo = m_view->textFolding().visibleLineToLine(lineno);
                const Kate::TextLine &line = m_doc->plainKateTextLine(realLineNo);
                const QColor & col = line->markedAsModified() ? modifiedLineColor : savedLineColor;
                if (line->markedAsModified() || line->markedAsSavedOnDisk()) {
                    painter.fillRect(2, lineno / lineDivisor, 3, 1, col);
                }
            }
        }

        // end painting
        painter.end();
    }

    // set right ratio
    m_pixmap.setDevicePixelRatio(m_view->devicePixelRatioF());

    //qCDebug(LOG_KTE) << time.elapsed();
    // Redraw the scrollbar widget with the updated pixmap.
    update();
}

void KateScrollBar::miniMapPaintEvent(QPaintEvent *e)
{
    QScrollBar::paintEvent(e);

    QPainter painter(this);

    QStyleOptionSlider opt;
    opt.init(this);
    opt.subControls = QStyle::SC_None;
    opt.activeSubControls = QStyle::SC_None;
    opt.orientation = orientation();
    opt.minimum = minimum();
    opt.maximum = maximum();
    opt.sliderPosition = sliderPosition();
    opt.sliderValue = value();
    opt.singleStep = singleStep();
    opt.pageStep = pageStep();

    QRect grooveRect = style()->subControlRect(QStyle::CC_ScrollBar, &opt, QStyle::SC_ScrollBarGroove, this);
    m_stdGroveRect = grooveRect;
    if (style()->subControlRect(QStyle::CC_ScrollBar, &opt, QStyle::SC_ScrollBarSubLine, this).height() == 0) {
        int alignMargin = style()->pixelMetric(QStyle::PM_FocusFrameVMargin, &opt, this);
        grooveRect.moveTop(alignMargin);
        grooveRect.setHeight(grooveRect.height() - alignMargin);
    }
    if (style()->subControlRect(QStyle::CC_ScrollBar, &opt, QStyle::SC_ScrollBarAddLine, this).height() == 0) {
        int alignMargin = style()->pixelMetric(QStyle::PM_FocusFrameVMargin, &opt, this);
        grooveRect.setHeight(grooveRect.height() - alignMargin);
    }
    m_grooveHeight = grooveRect.height();

    const int docXMargin = 1;
    QRect sliderRect = style()->subControlRect(QStyle::CC_ScrollBar, &opt, QStyle::SC_ScrollBarSlider, this);
    sliderRect.adjust(docXMargin, 0, 0, 0);

    //style()->drawControl(QStyle::CE_ScrollBarAddLine, &opt, &painter, this);
    //style()->drawControl(QStyle::CE_ScrollBarSubLine, &opt, &painter, this);

    // calculate the document size and position
    const int docHeight = qMin(grooveRect.height(), int(m_pixmap.height() / m_pixmap.devicePixelRatio() * 2)) - 2 * docXMargin;
    const int yoffset = 1; // top-aligned in stead of center-aligned (grooveRect.height() - docHeight) / 2;
    const QRect docRect(QPoint(grooveRect.left() + docXMargin, yoffset + grooveRect.top()), QSize(grooveRect.width() - docXMargin, docHeight));
    m_mapGroveRect = docRect;

    // calculate the visible area
    int max = qMax(maximum() + 1, 1);
    int visibleStart = value() * docHeight / (max + pageStep()) + docRect.top() + 0.5;
    int visibleEnd = (value() + pageStep()) * docHeight / (max + pageStep()) + docRect.top();
    QRect visibleRect = docRect;
    visibleRect.moveTop(visibleStart);
    visibleRect.setHeight(visibleEnd - visibleStart);

    // calculate colors
    const QColor backgroundColor = m_view->defaultStyleAttribute(KTextEditor::dsNormal)->background().color();
    const QColor foregroundColor = m_view->defaultStyleAttribute(KTextEditor::dsNormal)->foreground().color();
    const QColor highlightColor  = palette().link().color();

    const int backgroundLightness = backgroundColor.lightness();
    const int foregroundLightness = foregroundColor.lightness();
    const int lighnessDiff = (foregroundLightness - backgroundLightness);

    // get a color suited for the color theme
    QColor darkShieldColor = palette().color(QPalette::Mid);
    int hue, sat, light;
    darkShieldColor.getHsl(&hue, &sat, &light);
    // apply suitable lightness
    darkShieldColor.setHsl(hue, sat, backgroundLightness + lighnessDiff * 0.35);
    // gradient for nicer results
    QLinearGradient gradient(0, 0, width(), 0);
    gradient.setColorAt(0, darkShieldColor);
    gradient.setColorAt(0.3, darkShieldColor.lighter(115));
    gradient.setColorAt(1, darkShieldColor);

    QColor lightShieldColor;
    lightShieldColor.setHsl(hue, sat, backgroundLightness + lighnessDiff * 0.15);

    QColor outlineColor;
    outlineColor.setHsl(hue, sat, backgroundLightness + lighnessDiff * 0.5);

    // draw the grove background in case the document is small
    painter.setPen(Qt::NoPen);
    painter.setBrush(backgroundColor);
    painter.drawRect(grooveRect);

    // adjust the rectangles
    if ((docHeight + 2 * docXMargin >= grooveRect.height()) && (sliderRect.height() > visibleRect.height() + 2)) {
        visibleRect.adjust(2, 0, -3, 0);
    } else {
        visibleRect.adjust(1, 0, -1, 2);
        sliderRect.setTop(visibleRect.top() - 1);
        sliderRect.setBottom(visibleRect.bottom() + 1);
    }

    // Smooth transform only when squeezing
    if (grooveRect.height() < m_pixmap.height() / m_pixmap.devicePixelRatio()) {
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
    }

    // draw the modified lines margin
    QRect pixmapMarginRect(QPoint(0, 0), QSize(s_pixelMargin, m_pixmap.height() / m_pixmap.devicePixelRatio()));
    QRect docPixmapMarginRect(QPoint(0, docRect.top()), QSize(s_pixelMargin, docRect.height()));
    painter.drawPixmap(docPixmapMarginRect, m_pixmap, pixmapMarginRect);

    // calculate the stretch and draw the stretched lines (scrollbar marks)
    QRect pixmapRect(QPoint(s_pixelMargin, 0), QSize(m_pixmap.width() / m_pixmap.devicePixelRatio() - s_pixelMargin, m_pixmap.height() / m_pixmap.devicePixelRatio()));
    QRect docPixmapRect(QPoint(s_pixelMargin, docRect.top()), QSize(docRect.width() - s_pixelMargin, docRect.height()));
    painter.drawPixmap(docPixmapRect, m_pixmap, pixmapRect);

    // delimit the end of the document
    const int y = docPixmapRect.height() + grooveRect.y();
    if (y+2 < grooveRect.y() + grooveRect.height()) {
        QColor fg(foregroundColor);
        fg.setAlpha(30);
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(fg, 1));
        painter.drawLine(grooveRect.x()+1,y+2,width()-1,y+2);
    }

    // fade the invisible sections
    const QRect top(
        grooveRect.x(),
        grooveRect.y(),
        grooveRect.width(),
        visibleRect.y()-grooveRect.y() //Pen width
    );
    const QRect bottom(
        grooveRect.x(),
        grooveRect.y()+visibleRect.y()+visibleRect.height()-grooveRect.y(), //Pen width
        grooveRect.width(),
        grooveRect.height() - (visibleRect.y()-grooveRect.y())-visibleRect.height()
    );

    QColor faded(backgroundColor);
    faded.setAlpha(110);
    painter.fillRect(top, faded);
    painter.fillRect(bottom, faded);

    // add a thin line to limit the scrollbar
    QColor c(foregroundColor);
    c.setAlpha(10);
    painter.setPen(QPen(c,1));
    painter.drawLine(0, 0, 0, height());

    if (m_showMarks) {
        QHashIterator<int, QColor> it = m_lines;
        QPen penBg;
        penBg.setWidth(4);
        lightShieldColor.setAlpha(180);
        penBg.setColor(lightShieldColor);
        painter.setPen(penBg);
        while (it.hasNext()) {
            it.next();
            int y = (it.key() - grooveRect.top()) * docHeight / grooveRect.height() + docRect.top();;
            painter.drawLine(6, y, width() - 6, y);
        }

        it = m_lines;
        QPen pen;
        pen.setWidth(2);
        while (it.hasNext()) {
            it.next();
            pen.setColor(it.value());
            painter.setPen(pen);
            int y = (it.key() - grooveRect.top()) * docHeight / grooveRect.height() + docRect.top();
            painter.drawLine(6, y, width() - 6, y);
        }
    }

    // slider outline
    QColor sliderColor(highlightColor);
    sliderColor.setAlpha(50);
    painter.fillRect(sliderRect, sliderColor);
    painter.setPen(QPen(highlightColor, 0));
    // rounded rect looks ugly for some reason, so we draw 4 lines.
    painter.drawLine(sliderRect.left(), sliderRect.top() + 1, sliderRect.left(), sliderRect.bottom() - 1);
    painter.drawLine(sliderRect.right(), sliderRect.top() + 1, sliderRect.right(), sliderRect.bottom() - 1);
    painter.drawLine(sliderRect.left() + 1, sliderRect.top(), sliderRect.right() - 1, sliderRect.top());
    painter.drawLine(sliderRect.left() + 1, sliderRect.bottom(), sliderRect.right() - 1, sliderRect.bottom());
}

void KateScrollBar::normalPaintEvent(QPaintEvent *e)
{
    QScrollBar::paintEvent(e);

    if (!m_showMarks) {
        return;
    }

    QPainter painter(this);

    QStyleOptionSlider opt;
    opt.init(this);
    opt.subControls = QStyle::SC_None;
    opt.activeSubControls = QStyle::SC_None;
    opt.orientation = orientation();
    opt.minimum = minimum();
    opt.maximum = maximum();
    opt.sliderPosition = sliderPosition();
    opt.sliderValue = value();
    opt.singleStep = singleStep();
    opt.pageStep = pageStep();

    QRect rect = style()->subControlRect(QStyle::CC_ScrollBar, &opt, QStyle::SC_ScrollBarSlider, this);
    int sideMargin = width() - rect.width();
    if (sideMargin < 4) {
        sideMargin = 4;
    }
    sideMargin /= 2;

    QHashIterator<int, QColor> it = m_lines;
    while (it.hasNext()) {
        it.next();
        painter.setPen(it.value());
        if (it.key() < rect.top() || it.key() > rect.bottom()) {
            painter.drawLine(0, it.key(), width(), it.key());
        } else {
            painter.drawLine(0, it.key(), sideMargin, it.key());
            painter.drawLine(width() - sideMargin, it.key(), width(), it.key());
        }
    }
}

void KateScrollBar::resizeEvent(QResizeEvent *e)
{
    QScrollBar::resizeEvent(e);
    m_updateTimer.start();
    m_lines.clear();
    update();
}

void KateScrollBar::sliderChange(SliderChange change)
{
    // call parents implementation
    QScrollBar::sliderChange(change);

    if (change == QAbstractSlider::SliderValueChange) {
        redrawMarks();
    } else if (change == QAbstractSlider::SliderRangeChange) {
        marksChanged();
    }

    if (m_leftMouseDown || m_middleMouseDown) {
        const int fromLine = m_viewInternal->toRealCursor(m_viewInternal->startPos()).line() + 1;
        const int lastLine = m_viewInternal->toRealCursor(m_viewInternal->endPos()).line() + 1;
        QToolTip::showText(m_toolTipPos, i18nc("from line - to line", "<center>%1<br/>&#x2014;<br/>%2</center>", fromLine, lastLine), this);
    }
}

void KateScrollBar::marksChanged()
{
    m_lines.clear();
    update();
}

void KateScrollBar::redrawMarks()
{
    if (!m_showMarks) {
        return;
    }
    update();
}

void KateScrollBar::recomputeMarksPositions()
{
    // get the style options to compute the scrollbar pixels
    QStyleOptionSlider opt;
    initStyleOption(&opt);
    QRect grooveRect = style()->subControlRect(QStyle::CC_ScrollBar, &opt, QStyle::SC_ScrollBarGroove, this);

    // cache top margin and groove height
    const int top = grooveRect.top();
    const int h = grooveRect.height() - 1;

    // make sure we have a sane height
    if (h <= 0) {
        return;
    }

    // get total visible (=without folded) lines in the document
    int visibleLines = m_view->textFolding().visibleLines() - 1;
    if (m_view->config()->scrollPastEnd()) {
        visibleLines += m_viewInternal->linesDisplayed() - 1;
        visibleLines -= m_view->config()->autoCenterLines();
    }

    // now repopulate the scrollbar lines list
    m_lines.clear();
    const QHash<int, KTextEditor::Mark *> &marks = m_doc->marks();
    for (QHash<int, KTextEditor::Mark *>::const_iterator i = marks.constBegin(); i != marks.constEnd(); ++i) {
        KTextEditor::Mark *mark = i.value();
        const int line = m_view->textFolding().lineToVisibleLine(mark->line);
        const double ratio = static_cast<double>(line) / visibleLines;
        m_lines.insert(top + (int)(h * ratio),
                       KateRendererConfig::global()->lineMarkerColor((KTextEditor::MarkInterface::MarkTypes)mark->type));
    }
}

void KateScrollBar::sliderMaybeMoved(int value)
{
    if (m_middleMouseDown) {
        // we only need to emit this signal once, as for the following slider
        // movements the signal sliderMoved() is already emitted.
        // Thus, set m_middleMouseDown to false right away.
        m_middleMouseDown = false;
        emit sliderMMBMoved(value);
    }
}
//END

//BEGIN KateCmdLineEditFlagCompletion
/**
 * This class provide completion of flags. It shows a short description of
 * each flag, and flags are appended.
 */
class KateCmdLineEditFlagCompletion : public KCompletion
{
public:
    KateCmdLineEditFlagCompletion()
    {
        ;
    }

    QString makeCompletion(const QString & /*s*/) override
    {
        return QString();
    }

};
//END KateCmdLineEditFlagCompletion

//BEGIN KateCmdLineEdit
KateCommandLineBar::KateCommandLineBar(KTextEditor::ViewPrivate *view, QWidget *parent)
    : KateViewBarWidget(true, parent)
{
    QHBoxLayout *topLayout = new QHBoxLayout();
    centralWidget()->setLayout(topLayout);
    topLayout->setContentsMargins(0, 0, 0, 0);
    m_lineEdit = new KateCmdLineEdit(this, view);
    connect(m_lineEdit, SIGNAL(hideRequested()), SIGNAL(hideMe()));
    topLayout->addWidget(m_lineEdit);

    QToolButton *helpButton = new QToolButton(this);
    helpButton->setAutoRaise(true);
    helpButton->setIcon(QIcon::fromTheme(QStringLiteral("help-contextual")));
    topLayout->addWidget(helpButton);
    connect(helpButton, SIGNAL(clicked()), this, SLOT(showHelpPage()));

    setFocusProxy(m_lineEdit);
}

void KateCommandLineBar::showHelpPage()
{
    KHelpClient::invokeHelp(QStringLiteral("advanced-editing-tools-commandline"), QStringLiteral("kate"));
}

KateCommandLineBar::~KateCommandLineBar()
{
}

// inserts the given string in the command line edit and (if selected = true) selects it so the user
// can type over it if they want to
void KateCommandLineBar::setText(const QString &text, bool selected)
{
    m_lineEdit->setText(text);
    if (selected) {
        m_lineEdit->selectAll();
    }
}

void KateCommandLineBar::execute(const QString &text)
{
    m_lineEdit->slotReturnPressed(text);
}

KateCmdLineEdit::KateCmdLineEdit(KateCommandLineBar *bar, KTextEditor::ViewPrivate *view)
    : KLineEdit()
    , m_view(view)
    , m_bar(bar)
    , m_msgMode(false)
    , m_histpos(0)
    , m_cmdend(0)
    , m_command(nullptr)
{
    connect(this, SIGNAL(returnPressed(QString)),
            this, SLOT(slotReturnPressed(QString)));

    setCompletionObject(KateCmd::self()->commandCompletionObject());
    setAutoDeleteCompletionObject(false);

    m_hideTimer = new QTimer(this);
    m_hideTimer->setSingleShot(true);
    connect(m_hideTimer, SIGNAL(timeout()), this, SLOT(hideLineEdit()));

    // make sure the timer is stopped when the user switches views. if not, focus will be given to the
    // wrong view when KateViewBar::hideCurrentBarWidget() is called after 4 seconds. (the timer is
    // used for showing things like "Success" for four seconds after the user has used the kate
    // command line)
    connect(m_view, SIGNAL(focusOut(KTextEditor::View*)), m_hideTimer, SLOT(stop()));
}

void KateCmdLineEdit::hideEvent(QHideEvent *e)
{
    Q_UNUSED(e);
}

QString KateCmdLineEdit::helptext(const QPoint &) const
{
    QString beg = QStringLiteral("<qt background=\"white\"><div><table width=\"100%\"><tr><td bgcolor=\"brown\"><font color=\"white\"><b>Help: <big>");
    QString mid = QStringLiteral("</big></b></font></td></tr><tr><td>");
    QString end = QStringLiteral("</td></tr></table></div><qt>");

    QString t = text();
    QRegExp re(QLatin1String("\\s*help\\s+(.*)"));
    if (re.indexIn(t) > -1) {
        QString s;
        // get help for command
        QString name = re.cap(1);
        if (name == QLatin1String("list")) {
            return beg + i18n("Available Commands") + mid
                   + KateCmd::self()->commandList().join(QLatin1Char(' '))
                   + i18n("<p>For help on individual commands, do <code>'help &lt;command&gt;'</code></p>")
                   + end;
        } else if (! name.isEmpty()) {
            KTextEditor::Command *cmd = KateCmd::self()->queryCommand(name);
            if (cmd) {
                if (cmd->help(m_view, name, s)) {
                    return beg + name + mid + s + end;
                } else {
                    return beg + name + mid + i18n("No help for '%1'",  name) + end;
                }
            } else {
                return beg + mid + i18n("No such command <b>%1</b>", name) + end;
            }
        }
    }

    return beg + mid + i18n(
               "<p>This is the Katepart <b>command line</b>.<br />"
               "Syntax: <code><b>command [ arguments ]</b></code><br />"
               "For a list of available commands, enter <code><b>help list</b></code><br />"
               "For help for individual commands, enter <code><b>help &lt;command&gt;</b></code></p>")
           + end;
}

bool KateCmdLineEdit::event(QEvent *e)
{
    if (e->type() == QEvent::QueryWhatsThis) {
        setWhatsThis(helptext(QPoint()));
        e->accept();
        return true;
    }
    return KLineEdit::event(e);
}

/**
 * Parse the text as a command.
 *
 * The following is a simple PEG grammar for the syntax of the command.
 * (A PEG grammar is like a BNF grammar, except that "/" stands for
 * ordered choice: only the first matching rule is used. In other words,
 * the parsing is short-circuited in the manner of the "or" operator in
 * programming languages, and so the grammar is unambiguous.)
 *
 * Text <- Range? Command
 *       / Position
 * Range <- Position ("," Position)?
 *        / "%"
 * Position <- Base Offset?
 * Base <- Line
 *       / LastLine
 *       / ThisLine
 *       / Mark
 * Offset <- [+-] Base
 * Line <- [0-9]+
 * LastLine <- "$"
 * ThisLine <- "."
 * Mark <- "'" [a-z]
 */

void KateCmdLineEdit::slotReturnPressed(const QString &text)
{
    if (text.isEmpty()) {
        return;
    }
    // silently ignore leading space characters
    uint n = 0;
    const uint textlen = text.length();
    while ((n < textlen) && (text[n].isSpace())) {
        n++;
    }

    if (n >= textlen) {
        return;
    }

    QString cmd = text.mid(n);

    // Parse any leading range expression, and strip it (and maybe do some other transforms on the command).
    QString leadingRangeExpression;
    KTextEditor::Range range = CommandRangeExpressionParser::parseRangeExpression(cmd, m_view, leadingRangeExpression, cmd);

    // Built in help: if the command starts with "help", [try to] show some help
    if (cmd.startsWith(QLatin1String("help"))) {
        QWhatsThis::showText(mapToGlobal(QPoint(0, 0)), helptext(QPoint()));
        clear();
        KateCmd::self()->appendHistory(cmd);
        m_histpos = KateCmd::self()->historyLength();
        m_oldText.clear();
        return;
    }

    if (cmd.length() > 0) {
        KTextEditor::Command *p = KateCmd::self()->queryCommand(cmd);

        m_oldText = leadingRangeExpression + cmd;
        m_msgMode = true;

        // the following commands changes the focus themselves, so bar should be hidden before execution.
        if (QRegExp(QLatin1String("buffer|b|new|vnew|bp|bprev|bn|bnext|bf|bfirst|bl|blast|edit|e")).exactMatch(cmd.split(QLatin1Char(' ')).at(0))) {
            emit hideRequested();
        }

        if (!p) {
            setText(i18n("No such command: \"%1\"",  cmd));
        } else if (range.isValid() && !p->supportsRange(cmd)) {
            // Raise message, when the command does not support ranges.
            setText(i18n("Error: No range allowed for command \"%1\".",  cmd));
        } else {
            QString msg;
            if (p->exec(m_view, cmd, msg, range)) {

                // append command along with range (will be empty if none given) to history
                KateCmd::self()->appendHistory(leadingRangeExpression + cmd);
                m_histpos = KateCmd::self()->historyLength();
                m_oldText.clear();

                if (msg.length() > 0) {
                    setText(i18n("Success: ") + msg);
                } else if (isVisible()) {
                    // always hide on success without message
                    emit hideRequested();
                }
            } else {
                if (msg.length() > 0) {
                    if (msg.contains(QLatin1Char('\n'))) {
                        // multiline error, use widget with more space
                        QWhatsThis::showText(mapToGlobal(QPoint(0, 0)), msg);
                    } else {
                        setText(msg);
                    }
                } else {
                    setText(i18n("Command \"%1\" failed.",  cmd));
                }
            }
        }
    }

    // clean up
    if (completionObject() != KateCmd::self()->commandCompletionObject()) {
        KCompletion *c = completionObject();
        setCompletionObject(KateCmd::self()->commandCompletionObject());
        delete c;
    }
    m_command = nullptr;
    m_cmdend = 0;

    // the following commands change the focus themselves
    if (!QRegExp(QLatin1String("buffer|b|new|vnew|bp|bprev|bn|bnext|bf|bfirst|bl|blast|edit|e")).exactMatch(cmd.split(QLatin1Char(' ')).at(0))) {
        m_view->setFocus();
    }

    if (isVisible()) {
        m_hideTimer->start(4000);
    }
}

void KateCmdLineEdit::hideLineEdit()  // unless i have focus ;)
{
    if (! hasFocus()) {
        emit hideRequested();
    }
}

void KateCmdLineEdit::focusInEvent(QFocusEvent *ev)
{
    if (m_msgMode) {
        m_msgMode = false;
        setText(m_oldText);
        selectAll();
    }

    KLineEdit::focusInEvent(ev);
}

void KateCmdLineEdit::keyPressEvent(QKeyEvent *ev)
{
    if (ev->key() == Qt::Key_Escape ||
            (ev->key() == Qt::Key_BracketLeft && ev->modifiers() == Qt::ControlModifier)) {
        m_view->setFocus();
        hideLineEdit();
        clear();
    } else if (ev->key() == Qt::Key_Up) {
        fromHistory(true);
    } else if (ev->key() == Qt::Key_Down) {
        fromHistory(false);
    }

    uint cursorpos = cursorPosition();
    KLineEdit::keyPressEvent(ev);

    // during typing, let us see if we have a valid command
    if (! m_cmdend || cursorpos <= m_cmdend) {
        QChar c;
        if (! ev->text().isEmpty()) {
            c = ev->text().at(0);
        }

        if (! m_cmdend && ! c.isNull()) { // we have no command, so lets see if we got one
            if (! c.isLetterOrNumber() && c != QLatin1Char('-') && c != QLatin1Char('_')) {
                m_command = KateCmd::self()->queryCommand(text().trimmed());
                if (m_command) {
                    //qCDebug(LOG_KTE)<<"keypress in commandline: We have a command! "<<m_command<<". text is '"<<text()<<"'";
                    // if the typed character is ":",
                    // we try if the command has flag completions
                    m_cmdend = cursorpos;
                    //qCDebug(LOG_KTE)<<"keypress in commandline: Set m_cmdend to "<<m_cmdend;
                } else {
                    m_cmdend = 0;
                }
            }
        } else { // since cursor is inside the command name, we reconsider it
            //qCDebug(LOG_KTE) << "keypress in commandline: \\W -- text is " << text();
            m_command = KateCmd::self()->queryCommand(text().trimmed());
            if (m_command) {
                //qCDebug(LOG_KTE)<<"keypress in commandline: We have a command! "<<m_command;
                QString t = text();
                m_cmdend = 0;
                bool b = false;
                for (; (int)m_cmdend < t.length(); m_cmdend++) {
                    if (t[m_cmdend].isLetter()) {
                        b = true;
                    }
                    if (b && (! t[m_cmdend].isLetterOrNumber() && t[m_cmdend] != QLatin1Char('-') && t[m_cmdend] != QLatin1Char('_'))) {
                        break;
                    }
                }

                if (c == QLatin1Char(':') && cursorpos == m_cmdend) {
                    // check if this command wants to complete flags
                    //qCDebug(LOG_KTE)<<"keypress in commandline: Checking if flag completion is desired!";
                }
            } else {
                // clean up if needed
                if (completionObject() != KateCmd::self()->commandCompletionObject()) {
                    KCompletion *c = completionObject();
                    setCompletionObject(KateCmd::self()->commandCompletionObject());
                    delete c;
                }

                m_cmdend = 0;
            }
        }

        // if we got a command, check if it wants to do something.
        if (m_command) {
            KCompletion *cmpl = m_command->completionObject(m_view, text().left(m_cmdend).trimmed());
            if (cmpl) {
                // We need to prepend the current command name + flag string
                // when completion is done
                //qCDebug(LOG_KTE)<<"keypress in commandline: Setting completion object!";

                setCompletionObject(cmpl);
            }
        }
    } else if (m_command && !ev->text().isEmpty()) {
        // check if we should call the commands processText()
        if (m_command->wantsToProcessText(text().left(m_cmdend).trimmed())) {
            m_command->processText(m_view, text());
        }
    }
}

void KateCmdLineEdit::fromHistory(bool up)
{
    if (! KateCmd::self()->historyLength()) {
        return;
    }

    QString s;

    if (up) {
        if (m_histpos > 0) {
            m_histpos--;
            s = KateCmd::self()->fromHistory(m_histpos);
        }
    } else {
        if (m_histpos < (KateCmd::self()->historyLength() - 1)) {
            m_histpos++;
            s = KateCmd::self()->fromHistory(m_histpos);
        } else {
            m_histpos = KateCmd::self()->historyLength();
            setText(m_oldText);
        }
    }
    if (! s.isEmpty()) {
        // Select the argument part of the command, so that it is easy to overwrite
        setText(s);
        static QRegExp reCmd = QRegExp(QLatin1String(".*[\\w\\-]+(?:[^a-zA-Z0-9_-]|:\\w+)(.*)"));
        if (reCmd.indexIn(text()) == 0) {
            setSelection(text().length() - reCmd.cap(1).length(), reCmd.cap(1).length());
        }
    }
}
//END KateCmdLineEdit

//BEGIN KateIconBorder
using namespace KTextEditor;

KateIconBorder::KateIconBorder(KateViewInternal *internalView, QWidget *parent)
    : QWidget(parent)
    , m_view(internalView->m_view)
    , m_doc(internalView->doc())
    , m_viewInternal(internalView)
    , m_iconBorderOn(false)
    , m_lineNumbersOn(false)
    , m_relLineNumbersOn(false)
    , m_updateRelLineNumbers(false)
    , m_foldingMarkersOn(false)
    , m_dynWrapIndicatorsOn(false)
    , m_annotationBorderOn(false)
    , m_dynWrapIndicators(0)
    , m_lastClickedLine(-1)
    , m_cachedLNWidth(0)
    , m_maxCharWidth(0.0)
    , iconPaneWidth(16)
    , m_annotationBorderWidth(6)
    , m_annotationItemDelegate(new KateAnnotationItemDelegate(m_viewInternal, this))
    , m_foldingPreview(nullptr)
    , m_foldingRange(nullptr)
    , m_nextHighlightBlock(-2)
    , m_currentBlockLine(-1)
{
    setAcceptDrops(true);
    setAttribute(Qt::WA_StaticContents);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Minimum);
    setMouseTracking(true);
    m_doc->setMarkDescription(MarkInterface::markType01, i18n("Bookmark"));
    m_doc->setMarkPixmap(MarkInterface::markType01, QIcon::fromTheme(QStringLiteral("bookmarks")).pixmap(32, 32));

    connect(m_annotationItemDelegate, &AbstractAnnotationItemDelegate::sizeHintChanged,
            this, &KateIconBorder::updateAnnotationBorderWidth);

    updateFont();

    m_delayFoldingHlTimer.setSingleShot(true);
    m_delayFoldingHlTimer.setInterval(150);
    connect(&m_delayFoldingHlTimer, SIGNAL(timeout()), this, SLOT(showBlock()));

    // user interaction (scrolling) hides e.g. preview
    connect(m_view, SIGNAL(displayRangeChanged(KTextEditor::ViewPrivate*)), this, SLOT(displayRangeChanged()));
}

KateIconBorder::~KateIconBorder()
{
    delete m_foldingPreview;
    delete m_foldingRange;
}

void KateIconBorder::setIconBorderOn(bool enable)
{
    if (enable == m_iconBorderOn) {
        return;
    }

    m_iconBorderOn = enable;

    updateGeometry();

    QTimer::singleShot(0, this, SLOT(update()));
}

void KateIconBorder::setAnnotationBorderOn(bool enable)
{
    if (enable == m_annotationBorderOn) {
        return;
    }

    m_annotationBorderOn = enable;

    // make sure the tooltip is hidden
    if (!m_annotationBorderOn && !m_hoveredAnnotationGroupIdentifier.isEmpty()) {
        m_hoveredAnnotationGroupIdentifier.clear();
        hideAnnotationTooltip();
    }

    emit m_view->annotationBorderVisibilityChanged(m_view, enable);

    updateGeometry();

    QTimer::singleShot(0, this, SLOT(update()));
}

void KateIconBorder::removeAnnotationHovering()
{
    // remove hovering if it's still there
    if (m_annotationBorderOn && !m_hoveredAnnotationGroupIdentifier.isEmpty()) {
        m_hoveredAnnotationGroupIdentifier.clear();
        QTimer::singleShot(0, this, SLOT(update()));
    }
}

void KateIconBorder::setLineNumbersOn(bool enable)
{
    if (enable == m_lineNumbersOn) {
        return;
    }

    m_lineNumbersOn = enable;
    m_dynWrapIndicatorsOn = (m_dynWrapIndicators == 1) ? enable : m_dynWrapIndicators;

    updateGeometry();

    QTimer::singleShot(0, this, SLOT(update()));
}

void KateIconBorder::setRelLineNumbersOn(bool enable)
{
    if (enable == m_relLineNumbersOn) {
        return;
    }

    m_relLineNumbersOn = enable;
    /*
     * We don't have to touch the m_dynWrapIndicatorsOn because
     * we already got it right from the m_lineNumbersOn
     */
    updateGeometry();

    QTimer::singleShot( 0, this, SLOT(update()) );
}

void KateIconBorder::updateForCursorLineChange()
{
    if (m_relLineNumbersOn) {
        m_updateRelLineNumbers = true;
    }

    // always do normal update, e.g. for different current line color!
    update();
}

void KateIconBorder::setDynWrapIndicators(int state)
{
    if (state == m_dynWrapIndicators) {
        return;
    }

    m_dynWrapIndicators = state;
    m_dynWrapIndicatorsOn = (state == 1) ? m_lineNumbersOn : state;

    updateGeometry();

    QTimer::singleShot(0, this, SLOT(update()));
}

void KateIconBorder::setFoldingMarkersOn(bool enable)
{
    if (enable == m_foldingMarkersOn) {
        return;
    }

    m_foldingMarkersOn = enable;

    updateGeometry();

    QTimer::singleShot(0, this, SLOT(update()));
}

QSize KateIconBorder::sizeHint() const
{
    int w = 0;

    if (m_iconBorderOn) {
        w += iconPaneWidth + 2;
    }

    if (m_annotationBorderOn) {
        w += m_annotationBorderWidth + 2;
    }

    if (m_lineNumbersOn || (m_view->dynWordWrap() && m_dynWrapIndicatorsOn)) {
        w += lineNumberWidth() + 2;
    }

    if (m_foldingMarkersOn) {
        w += iconPaneWidth;
    }

    // space for the line modification system border
    if (m_view->config()->lineModification()) {
        w += 3;
    }

    // two pixel space
    w += 2;

    return QSize(w, 0);
}

// This function (re)calculates the maximum width of any of the digit characters (0 -> 9)
// for graceful handling of variable-width fonts as the linenumber font.
void KateIconBorder::updateFont()
{
    const QFontMetricsF &fm = m_view->renderer()->config()->fontMetrics();
    m_maxCharWidth = 0.0;
    // Loop to determine the widest numeric character in the current font.
    // 48 is ascii '0'
    for (int i = 48; i < 58; i++) {
        const qreal charWidth = ceil(fm.width(QChar(i)));
        m_maxCharWidth = qMax(m_maxCharWidth, charWidth);
    }

    // the icon pane scales with the font...
    iconPaneWidth = fm.height();

    calcAnnotationBorderWidth();

    updateGeometry();

    QTimer::singleShot(0, this, SLOT(update()));
}

int KateIconBorder::lineNumberWidth() const
{
    // width = (number of digits + 1) * char width
    const int digits = (int) ceil(log10((double)(m_view->doc()->lines() + 1)));
    int width = m_lineNumbersOn ? (int)ceil((digits + 1) * m_maxCharWidth) : 0;

    if (m_view->dynWordWrap() && m_dynWrapIndicatorsOn) {
        // HACK: 16 == style().scrollBarExtent().width()
        width = qMax(16 + 4, width);

        if (m_cachedLNWidth != width || m_oldBackgroundColor != m_view->renderer()->config()->iconBarColor()) {
            int w = 16;// HACK: 16 == style().scrollBarExtent().width() style().scrollBarExtent().width();
            int h = m_view->renderer()->lineHeight();

            QSize newSize(w * devicePixelRatio(), h * devicePixelRatio());
            if ((m_arrow.size() != newSize || m_oldBackgroundColor != m_view->renderer()->config()->iconBarColor()) && !newSize.isEmpty()) {
                m_arrow = QPixmap(newSize);
                m_arrow.setDevicePixelRatio(devicePixelRatioF());

                QPainter p(&m_arrow);
                p.fillRect(0, 0, w, h, m_view->renderer()->config()->iconBarColor());

                h = m_view->renderer()->config()->fontMetrics().ascent();

                p.setPen(m_view->renderer()->config()->lineNumberColor());

                QPainterPath path;
                path.moveTo(w / 2, h / 2);
                path.lineTo(w / 2, 0);
                path.lineTo(w / 4, h / 4);
                path.lineTo(0, 0);
                path.lineTo(0, h / 2);
                path.lineTo(w / 2, h - 1);
                path.lineTo(w * 3 / 4, h - 1);
                path.lineTo(w - 1, h * 3 / 4);
                path.lineTo(w * 3 / 4, h / 2);
                path.lineTo(0, h / 2);
                p.drawPath(path);
            }
        }
    }

    return width;
}

void KateIconBorder::dragMoveEvent(QDragMoveEvent *event)
{
    // FIXME Just calling m_view->m_viewInternal->dragMoveEvent(e) don't work
    // as intended, we need to set the cursor at column 1
    // Is there a way to change the pos of the event?
    QPoint pos(0, event->pos().y());
    // Code copy of KateViewInternal::dragMoveEvent
    m_view->m_viewInternal->placeCursor(pos, true, false);
    m_view->m_viewInternal->fixDropEvent(event);
}

void KateIconBorder::dropEvent(QDropEvent *event)
{
    m_view->m_viewInternal->dropEvent(event);
}

void KateIconBorder::paintEvent(QPaintEvent *e)
{
    paintBorder(e->rect().x(), e->rect().y(), e->rect().width(), e->rect().height());
}

static void paintTriangle(QPainter &painter, QColor c, int xOffset, int yOffset, int width, int height, bool open)
{
    painter.setRenderHint(QPainter::Antialiasing);

    qreal size = qMin(width, height);

    if (KColorUtils::luma(c) > 0.25) {
        c = KColorUtils::darken(c);
    } else {
        c = KColorUtils::shade(c, 0.1);
    }

    QPen pen;
    pen.setJoinStyle(Qt::RoundJoin);
    pen.setColor(c);
    pen.setWidthF(1.5);
    painter.setPen(pen);

    painter.setBrush(c);

    // let some border, if possible
    size *= 0.6;

    qreal halfSize = size / 2;
    qreal halfSizeP = halfSize * 0.6;
    QPointF middle(xOffset + (qreal)width / 2, yOffset + (qreal)height / 2);

    if (open) {
        QPointF points[3] = { middle + QPointF(-halfSize, -halfSizeP), middle + QPointF(halfSize, -halfSizeP), middle + QPointF(0, halfSizeP) };
        painter.drawConvexPolygon(points, 3);
    } else {
        QPointF points[3] = { middle + QPointF(-halfSizeP, -halfSize), middle + QPointF(-halfSizeP, halfSize), middle + QPointF(halfSizeP, 0) };
        painter.drawConvexPolygon(points, 3);
    }

    painter.setRenderHint(QPainter::Antialiasing, false);
}

/**
 * Helper class for an identifier which can be an empty or non-empty string or invalid.
 * Avoids complicated explicit statements in code dealing with the identifier
 * received as QVariant from a model.
 */
class KateAnnotationGroupIdentifier
{
public:
    KateAnnotationGroupIdentifier(const QVariant &identifier)
        : m_isValid(identifier.isValid() && identifier.canConvert<QString>())
        , m_id(m_isValid ? identifier.toString() : QString())
    {
    }
    KateAnnotationGroupIdentifier() = default;
    KateAnnotationGroupIdentifier(const KateAnnotationGroupIdentifier &rhs) = default;

    KateAnnotationGroupIdentifier& operator=(const KateAnnotationGroupIdentifier &rhs)
    {
        m_isValid = rhs.m_isValid;
        m_id = rhs.m_id;
        return *this;
    }
    KateAnnotationGroupIdentifier& operator=(const QVariant &identifier)
    {
        m_isValid = (identifier.isValid() && identifier.canConvert<QString>());
        if (m_isValid) {
            m_id = identifier.toString();
        } else {
            m_id.clear();
        }
        return *this;
    }

    bool operator==(const KateAnnotationGroupIdentifier &rhs) const
    {
        return (m_isValid == rhs.m_isValid) && (!m_isValid || (m_id == rhs.m_id));
    }
    bool operator!=(const KateAnnotationGroupIdentifier &rhs) const
    {
        return (m_isValid != rhs.m_isValid) || (m_isValid && (m_id != rhs.m_id));
    }

    void clear()
    {
        m_isValid = false;
        m_id.clear();
    }
    bool isValid() const { return m_isValid; }
    const QString& id() const { return m_id; }

private:
    bool m_isValid = false;
    QString m_id;
};

/**
 * Helper class for iterative calculation of data regarding the position
 * of a line with regard to annotation item grouping.
 */
class KateAnnotationGroupPositionState
{
public:
    /**
     * @param startz first rendered displayed line
     * @param isUsed flag whether the KateAnnotationGroupPositionState object will
     *               be used or is just created due to being on the stack
     */
    KateAnnotationGroupPositionState(KateViewInternal *viewInternal,
                                     const KTextEditor::AnnotationModel *model,
                                     const QString &hoveredAnnotationGroupIdentifier,
                                     uint startz,
                                     bool isUsed);
    /**
     * @param styleOption option to fill with data for the given line
     * @param z rendered displayed line
     * @param realLine real line which is rendered here (passed to avoid another look-up)
     */
    void nextLine(KTextEditor::StyleOptionAnnotationItem &styleOption, uint z, int realLine);

private:
    KateViewInternal *m_viewInternal;
    const KTextEditor::AnnotationModel * const m_model;
    const QString m_hoveredAnnotationGroupIdentifier;

    int m_visibleWrappedLineInAnnotationGroup = -1;
    KateAnnotationGroupIdentifier m_lastAnnotationGroupIdentifier;
    KateAnnotationGroupIdentifier m_nextAnnotationGroupIdentifier;
    bool m_isSameAnnotationGroupsSinceLast = false;
};

KateAnnotationGroupPositionState::KateAnnotationGroupPositionState(KateViewInternal *viewInternal,
                                                                   const KTextEditor::AnnotationModel *model,
                                                                   const QString &hoveredAnnotationGroupIdentifier,
                                                                   uint startz,
                                                                   bool isUsed)
    : m_viewInternal(viewInternal)
    , m_model(model)
    , m_hoveredAnnotationGroupIdentifier(hoveredAnnotationGroupIdentifier)
{
    if (!isUsed) {
        return;
    }

    if (!m_model || (static_cast<int>(startz) >= m_viewInternal->cache()->viewCacheLineCount())) {
        return;
    }

    const auto realLineAtStart = m_viewInternal->cache()->viewLine(startz).line();
    m_nextAnnotationGroupIdentifier = m_model->data(realLineAtStart,
                                                    (Qt::ItemDataRole)KTextEditor::AnnotationModel::GroupIdentifierRole);
    if (m_nextAnnotationGroupIdentifier.isValid()) {
        // estimate state of annotation group before first rendered line
        if (startz == 0) {
            if (realLineAtStart > 0) {
                // TODO: here we would want to scan until the next line that would be displayed,
                // to see if there are any group changes until then
                // for now simply taking neighbour line into account, not a grave bug on the first displayed line
                m_lastAnnotationGroupIdentifier = m_model->data(realLineAtStart - 1,
                                                                (Qt::ItemDataRole)        KTextEditor::AnnotationModel::GroupIdentifierRole);
                m_isSameAnnotationGroupsSinceLast = (m_lastAnnotationGroupIdentifier == m_nextAnnotationGroupIdentifier);
            }
        } else {
            const auto realLineBeforeStart = m_viewInternal->cache()->viewLine(startz-1).line();
            m_lastAnnotationGroupIdentifier = m_model->data(realLineBeforeStart, (Qt::ItemDataRole)KTextEditor::AnnotationModel::GroupIdentifierRole);
            if (m_lastAnnotationGroupIdentifier.isValid()) {
                if (m_lastAnnotationGroupIdentifier.id() == m_nextAnnotationGroupIdentifier.id()) {
                    m_isSameAnnotationGroupsSinceLast = true;
                    // estimate m_visibleWrappedLineInAnnotationGroup from lines before startz
                    for (uint z = startz; z > 0; --z) {
                        const auto realLine = m_viewInternal->cache()->viewLine(z-1).line();
                        const KateAnnotationGroupIdentifier identifier = m_model->data(realLine, (Qt::ItemDataRole)KTextEditor::AnnotationModel::GroupIdentifierRole);
                        if (identifier != m_lastAnnotationGroupIdentifier) {
                            break;
                        }
                        ++m_visibleWrappedLineInAnnotationGroup;
                    }
                }
            }
        }
    }
}

void KateAnnotationGroupPositionState::nextLine(KTextEditor::StyleOptionAnnotationItem &styleOption,
                                                uint z, int realLine)
{
    styleOption.wrappedLine = m_viewInternal->cache()->viewLine(z).viewLine();
    styleOption.wrappedLineCount = m_viewInternal->cache()->viewLineCount(realLine);

    // Estimate position in group
    const KateAnnotationGroupIdentifier annotationGroupIdentifier = m_nextAnnotationGroupIdentifier;
    bool isSameAnnotationGroupsSinceThis = false;
    // Calculate next line's group identifier
    // shortcut: assuming wrapped lines are always displayed together, test is simple
    if (styleOption.wrappedLine+1 < styleOption.wrappedLineCount) {
        m_nextAnnotationGroupIdentifier = annotationGroupIdentifier;
        isSameAnnotationGroupsSinceThis = true;
    } else {
        if (static_cast<int>(z+1) < m_viewInternal->cache()->viewCacheLineCount()) {
            const int realLineAfter = m_viewInternal->cache()->viewLine(z+1).line();
            // search for any realLine with a different group id, also the non-displayed
            int rl = realLine + 1;
            for (; rl <= realLineAfter; ++rl) {
                m_nextAnnotationGroupIdentifier = m_model->data(rl, (Qt::ItemDataRole)        KTextEditor::AnnotationModel::GroupIdentifierRole);
                if (!m_nextAnnotationGroupIdentifier.isValid() ||
                    (m_nextAnnotationGroupIdentifier.id() != annotationGroupIdentifier.id())) {
                    break;
                }
            }
            isSameAnnotationGroupsSinceThis = (rl > realLineAfter);
            if (rl < realLineAfter) {
                m_nextAnnotationGroupIdentifier = m_model->data(realLineAfter, (Qt::ItemDataRole)        KTextEditor::AnnotationModel::GroupIdentifierRole);
            }
        } else {
            // TODO: check next line after display end
            m_nextAnnotationGroupIdentifier.clear();
            isSameAnnotationGroupsSinceThis = false;
        }
    }

    if (annotationGroupIdentifier.isValid()) {
        if (m_hoveredAnnotationGroupIdentifier == annotationGroupIdentifier.id()) {
            styleOption.state |= QStyle::State_MouseOver;
        } else {
            styleOption.state &= ~QStyle::State_MouseOver;
        }

        if (m_isSameAnnotationGroupsSinceLast) {
            ++m_visibleWrappedLineInAnnotationGroup;
        } else {
            m_visibleWrappedLineInAnnotationGroup = 0;
        }

        styleOption.annotationItemGroupingPosition = StyleOptionAnnotationItem::InGroup;
        if (!m_isSameAnnotationGroupsSinceLast) {
            styleOption.annotationItemGroupingPosition |= StyleOptionAnnotationItem::GroupBegin;
        }
        if (!isSameAnnotationGroupsSinceThis) {
            styleOption.annotationItemGroupingPosition |= StyleOptionAnnotationItem::GroupEnd;
        }
    } else {
        m_visibleWrappedLineInAnnotationGroup = 0;
    }
    styleOption.visibleWrappedLineInGroup = m_visibleWrappedLineInAnnotationGroup;

    m_lastAnnotationGroupIdentifier = m_nextAnnotationGroupIdentifier;
    m_isSameAnnotationGroupsSinceLast = isSameAnnotationGroupsSinceThis;
}


void KateIconBorder::paintBorder(int /*x*/, int y, int /*width*/, int height)
{
    uint h = m_view->renderer()->lineHeight();
    uint startz = (y / h);
    uint endz = startz + 1 + (height / h);
    uint lineRangesSize = m_viewInternal->cache()->viewCacheLineCount();
    uint currentLine = m_view->cursorPosition().line();

    // center the folding boxes
    int m_px = (h - 11) / 2;
    if (m_px < 0) {
        m_px = 0;
    }

    int lnWidth(0);
    if (m_lineNumbersOn || (m_view->dynWordWrap() && m_dynWrapIndicatorsOn)) { // avoid calculating unless needed ;-)
        lnWidth = lineNumberWidth();
        if (lnWidth != m_cachedLNWidth || m_oldBackgroundColor != m_view->renderer()->config()->iconBarColor()) {
            // we went from n0 ->n9 lines or vice versa
            // this causes an extra updateGeometry() first time the line numbers
            // are displayed, but sizeHint() is supposed to be const so we can't set
            // the cached value there.
            m_cachedLNWidth = lnWidth;
            m_oldBackgroundColor = m_view->renderer()->config()->iconBarColor();
            updateGeometry();
            update();
            return;
        }
    }

    int w(this->width());                       // sane value/calc only once

    QPainter p(this);
    p.setRenderHints(QPainter::TextAntialiasing);
    p.setFont(m_view->renderer()->config()->font());    // for line numbers

    KTextEditor::AnnotationModel *model = m_view->annotationModel() ?
                                          m_view->annotationModel() : m_doc->annotationModel();
    KateAnnotationGroupPositionState annotationGroupPositionState(m_viewInternal, model,
                                                                  m_hoveredAnnotationGroupIdentifier,
                                                                  startz, m_annotationBorderOn);

    for (uint z = startz; z <= endz; z++) {
        int y = h * z;
        int realLine = -1;

        if (z < lineRangesSize) {
            realLine = m_viewInternal->cache()->viewLine(z).line();
        }

        int lnX = 0;

        p.fillRect(0, y, w - 5, h, m_view->renderer()->config()->iconBarColor());
        p.fillRect(w - 5, y, 5, h, m_view->renderer()->config()->backgroundColor());

        // icon pane
        if (m_iconBorderOn) {
            p.setPen(m_view->renderer()->config()->separatorColor());
            p.setBrush(m_view->renderer()->config()->separatorColor());
            p.drawLine(lnX + iconPaneWidth + 1, y, lnX + iconPaneWidth + 1, y + h);

            if ((realLine > -1) && (m_viewInternal->cache()->viewLine(z).startCol() == 0)) {
                uint mrk(m_doc->mark(realLine));      // call only once

                if (mrk) {
                    for (uint bit = 0; bit < 32; bit++) {
                        MarkInterface::MarkTypes markType = (MarkInterface::MarkTypes)(1 << bit);
                        if (mrk & markType) {
                            QPixmap px_mark(m_doc->markPixmap(markType));
                            px_mark.setDevicePixelRatio(devicePixelRatioF());

                            if (!px_mark.isNull() && h > 0 && iconPaneWidth > 0) {
                                // scale up to a usable size
                                px_mark = px_mark.scaled(iconPaneWidth * devicePixelRatio(), h * devicePixelRatio(), Qt::KeepAspectRatio);

                                // center the mark pixmap
                                int x_px = (iconPaneWidth - px_mark.width()/ devicePixelRatio()) / 2;
                                if (x_px < 0) {
                                    x_px = 0;
                                }

                                int y_px = (h - px_mark.height() / devicePixelRatio()) / 2;
                                if (y_px < 0) {
                                    y_px = 0;
                                }

                                p.drawPixmap(lnX + x_px, y + y_px, px_mark);
                            }
                        }
                    }
                }
            }

            lnX += iconPaneWidth + 2;
        }

        // annotation information
        if (m_annotationBorderOn) {
            // Draw a border line between annotations and the line numbers
            p.setPen(m_view->renderer()->config()->lineNumberColor());
            p.setBrush(m_view->renderer()->config()->lineNumberColor());

            const qreal borderX = lnX + m_annotationBorderWidth + 0.5;
            p.drawLine(QPointF(borderX, y+0.5), QPointF(borderX, y + h - 0.5));

            if ((realLine > -1) && model) {
                KTextEditor::StyleOptionAnnotationItem styleOption;
                initStyleOption(&styleOption);
                styleOption.rect.setRect(lnX, y, m_annotationBorderWidth, h);
                annotationGroupPositionState.nextLine(styleOption, z, realLine);

                m_annotationItemDelegate->paint(&p, styleOption, model, realLine);
            }

            // adjust current X position
            lnX += m_annotationBorderWidth + /* separator line width */1;
        }

        // line number
        if (m_lineNumbersOn || (m_view->dynWordWrap() && m_dynWrapIndicatorsOn)) {
            if (realLine > -1) {
                int distanceToCurrent = abs(realLine - static_cast<int>(currentLine));
                QColor color;

                if (distanceToCurrent == 0) {
                    color = m_view->renderer()->config()->currentLineNumberColor();
                } else {
                    color = m_view->renderer()->config()->lineNumberColor();
                }
                p.setPen(color);
                p.setBrush(color);

                if (m_viewInternal->cache()->viewLine(z).startCol() == 0) {
                    if (m_relLineNumbersOn) {
                        if (distanceToCurrent == 0) {
                            p.drawText(lnX + m_maxCharWidth / 2, y, lnWidth - m_maxCharWidth, h,
                                       Qt::TextDontClip|Qt::AlignLeft|Qt::AlignVCenter, QString::number(realLine + 1));
                        } else {
                            p.drawText(lnX + m_maxCharWidth / 2, y, lnWidth - m_maxCharWidth, h,
                                       Qt::TextDontClip|Qt::AlignRight|Qt::AlignVCenter, QString::number(distanceToCurrent));
                        }
                        if (m_updateRelLineNumbers) {
                            m_updateRelLineNumbers = false;
                            update();
                        }
                    } else if (m_lineNumbersOn) {
                        p.drawText(lnX + m_maxCharWidth / 2, y, lnWidth - m_maxCharWidth, h,
                                   Qt::TextDontClip | Qt::AlignRight | Qt::AlignVCenter, QString::number(realLine + 1));
                    }
                } else if (m_view->dynWordWrap() && m_dynWrapIndicatorsOn) {
                    p.drawPixmap(lnX + lnWidth - (m_arrow.width() / m_arrow.devicePixelRatio()) - 2, y, m_arrow);
                }
            }

            lnX += lnWidth + 2;
        }

        // folding markers
        if (m_foldingMarkersOn) {
            // first icon border background
            p.fillRect(lnX, y, iconPaneWidth, h, m_view->renderer()->config()->iconBarColor());

            // possible additional folding highlighting
            if ((realLine >= 0) && m_foldingRange && m_foldingRange->overlapsLine(realLine)) {
                p.save();

                // use linear gradient as brush
                QLinearGradient g(lnX, y, lnX + iconPaneWidth, y);
                const QColor foldingColor(m_view->renderer()->config()->foldingColor());
                g.setColorAt(0, foldingColor);
                g.setColorAt(0.3, foldingColor.lighter(110));
                g.setColorAt(1, foldingColor);
                p.setBrush(g);
                p.setPen(foldingColor);

                p.setClipRect(lnX, y, iconPaneWidth, h);
                p.setRenderHint(QPainter::Antialiasing);

                const qreal r = 4.0;
                if (m_foldingRange->start().line() == realLine &&
                        m_viewInternal->cache()->viewLine(z).viewLine() == 0) {
                    p.drawRect(lnX, y, iconPaneWidth, h + r);
                } else if (m_foldingRange->end().line() == realLine &&
                           m_viewInternal->cache()->viewLine(z).viewLine() == m_viewInternal->cache()->viewLineCount(realLine) - 1) {
                    p.drawRect(lnX, y - r, iconPaneWidth, h + r);
                } else {
                    p.drawRect(lnX, y - r, iconPaneWidth, h + 2 * r);
                }
                p.restore();
            }

            if ((realLine >= 0) && (m_viewInternal->cache()->viewLine(z).startCol() == 0)) {
                QVector<QPair<qint64, Kate::TextFolding::FoldingRangeFlags> > startingRanges = m_view->textFolding().foldingRangesStartingOnLine(realLine);
                bool anyFolded = false;
                for (int i = 0; i < startingRanges.size(); ++i)
                    if (startingRanges[i].second & Kate::TextFolding::Folded) {
                        anyFolded = true;
                    }

                Kate::TextLine tl = m_doc->kateTextLine(realLine);

                if (!startingRanges.isEmpty() || tl->markedAsFoldingStart()) {
                    if (anyFolded) {
                        paintTriangle(p, m_view->renderer()->config()->foldingColor(), lnX, y, iconPaneWidth, h, false);
                    } else {
                        paintTriangle(p, m_view->renderer()->config()->foldingColor(), lnX, y, iconPaneWidth, h, true);
                    }
                }
            }

            lnX += iconPaneWidth;
        }

        // modified line system
        if (m_view->config()->lineModification() &&
                realLine > -1 && !m_doc->url().isEmpty()) {
            // one pixel space
            ++lnX;

            Kate::TextLine tl = m_doc->plainKateTextLine(realLine);
            if (tl->markedAsModified()) {
                p.fillRect(lnX, y, 3, h, m_view->renderer()->config()->modifiedLineColor());
            }
            if (tl->markedAsSavedOnDisk()) {
                p.fillRect(lnX, y, 3, h, m_view->renderer()->config()->savedLineColor());
            }
        }
    }
}

KateIconBorder::BorderArea KateIconBorder::positionToArea(const QPoint &p) const
{
    // see KateIconBorder::sizeHint() for pixel spacings
    int x = 0;
    if (m_iconBorderOn) {
        x += iconPaneWidth;
        if (p.x() <= x) {
            return IconBorder;
        }
        x += 2;
    }
    if (this->m_annotationBorderOn) {
        x += m_annotationBorderWidth;
        if (p.x() <= x) {
            return AnnotationBorder;
        }
        x += 2;
    }
    if (m_lineNumbersOn || m_dynWrapIndicators) {
        x += lineNumberWidth();
        if (p.x() <= x) {
            return LineNumbers;
        }
        x += 2;
    }
    if (m_foldingMarkersOn) {
        x += iconPaneWidth;
        if (p.x() <= x) {
            return FoldingMarkers;
        }
    }
    if (m_view->config()->lineModification()) {
        x += 3 + 2;
        if (p.x() <= x) {
            return ModificationBorder;
        }
    }
    return None;
}

void KateIconBorder::mousePressEvent(QMouseEvent *e)
{
    const KateTextLayout &t = m_viewInternal->yToKateTextLayout(e->y());
    if (t.isValid()) {
        m_lastClickedLine = t.line();
        const auto area = positionToArea(e->pos());
        // IconBorder and AnnotationBorder have their own behavior; don't forward to view
        if (area != IconBorder && area != AnnotationBorder) {
            const auto pos = QPoint(0, e->y());
            if (area == LineNumbers && e->button() == Qt::LeftButton && !(e->modifiers() & Qt::ShiftModifier)) {
                // setup view so the following mousePressEvent will select the line
                m_viewInternal->beginSelectLine(pos);
            }
            QMouseEvent forward(QEvent::MouseButtonPress,
                                pos, e->button(), e->buttons(), e->modifiers());
            m_viewInternal->mousePressEvent(&forward);
        }
        return e->accept();
    }

    QWidget::mousePressEvent(e);
}

void KateIconBorder::showDelayedBlock(int line)
{
    // save the line over which the mouse hovers
    // either we start the timer for delay, or we show the block immediately
    // if the moving range already exists
    m_nextHighlightBlock = line;
    if (!m_foldingRange) {
        if (!m_delayFoldingHlTimer.isActive()) {
            m_delayFoldingHlTimer.start();
        }
    } else {
        showBlock();
    }
}

void KateIconBorder::showBlock()
{
    if (m_nextHighlightBlock == m_currentBlockLine) {
        return;
    }
    m_currentBlockLine = m_nextHighlightBlock;
    if (m_currentBlockLine >= m_doc->buffer().lines()) {
        return;
    }

    /**
     * compute to which folding range we belong
     * FIXME: optimize + perhaps have some better threshold or use timers!
     */
    KTextEditor::Range newRange = KTextEditor::Range::invalid();
    for (int line = m_currentBlockLine; line >= qMax(0, m_currentBlockLine - 1024); --line) {
        /**
         * try if we have folding range from that line, should be fast per call
         */
        KTextEditor::Range foldingRange = m_doc->buffer().computeFoldingRangeForStartLine(line);
        if (!foldingRange.isValid()) {
            continue;
        }

        /**
         * does the range reach us?
         */
        if (foldingRange.overlapsLine(m_currentBlockLine)) {
            newRange = foldingRange;
            break;
        }
    }

    if (newRange.isValid() && m_foldingRange && *m_foldingRange == newRange) {
        // new range equals the old one, nothing to do.
        return;
    } else { // the ranges differ, delete the old, if it exists
        delete m_foldingRange;
        m_foldingRange = nullptr;
    }

    if (newRange.isValid()) {
        //qCDebug(LOG_KTE) << "new folding hl-range:" << newRange;
        m_foldingRange = m_doc->newMovingRange(newRange, KTextEditor::MovingRange::ExpandRight);
        KTextEditor::Attribute::Ptr attr(new KTextEditor::Attribute());

        /**
         * create highlighting color
         * we avoid alpha as overpainting leads to ugly lines (https://bugreports.qt.io/browse/QTBUG-66036)
         */
        attr->setBackground(QBrush(m_view->renderer()->config()->foldingColor()));

        m_foldingRange->setView(m_view);
        // use z depth defined in moving ranges interface
        m_foldingRange->setZDepth(-100.0);
        m_foldingRange->setAttribute(attr);
    }

    // show text preview, if a folded region starts here
    bool foldUnderMouse = false;
    if (m_foldingRange && m_view->config()->foldingPreview()) {
        const QPoint globalPos = QCursor::pos();
        const QPoint pos = mapFromGlobal(globalPos);
        const KateTextLayout &t = m_view->m_viewInternal->yToKateTextLayout(pos.y());
        if (t.isValid() && positionToArea(pos) == FoldingMarkers) {

            const int realLine = t.line();
            foldUnderMouse = !m_view->textFolding().isLineVisible(realLine + 1);

            // only show when main window is active (#392396)
            const bool isWindowActive = !window() || window()->isActiveWindow();
            if (foldUnderMouse && isWindowActive) {
                if (!m_foldingPreview) {
                    m_foldingPreview = new KateTextPreview(m_view, this);
                    m_foldingPreview->setAttribute(Qt::WA_ShowWithoutActivating);
                    m_foldingPreview->setFrameStyle(QFrame::StyledPanel);

                    // event filter to catch application WindowDeactivate event, to hide the preview window
//                     qApp->installEventFilter(this);
                }

                // TODO: use KateViewInternal::maxLen() somehow to compute proper width for amount of lines to display

                // try using the end line of the range for proper popup height
                const int lineCount = qMin(m_foldingRange->numberOfLines() + 1,
                                           (height() - pos.y()) / m_view->renderer()->lineHeight());

                m_foldingPreview->resize(m_view->width() / 2, lineCount * m_view->renderer()->lineHeight() + 2 * m_foldingPreview->frameWidth());
                const int xGlobal = mapToGlobal(QPoint(width(), 0)).x();
                const int yGlobal = m_view->mapToGlobal(m_view->cursorToCoordinate(KTextEditor::Cursor(realLine, 0))).y();
                m_foldingPreview->move(QPoint(xGlobal, yGlobal) - m_foldingPreview->contentsRect().topLeft());
                m_foldingPreview->setLine(realLine);
                m_foldingPreview->setCenterView(false);
                m_foldingPreview->setShowFoldedLines(true);
                m_foldingPreview->raise();
                m_foldingPreview->show();
            }
        }
    }

    if (!foldUnderMouse) {
        delete m_foldingPreview;
    }

    /**
     * repaint
     */
    repaint();
}

void KateIconBorder::hideBlock()
{
    if (m_delayFoldingHlTimer.isActive()) {
        m_delayFoldingHlTimer.stop();
    }

    m_nextHighlightBlock = -2;
    m_currentBlockLine = -1;
    delete m_foldingRange;
    m_foldingRange = nullptr;

    delete m_foldingPreview;
}

void KateIconBorder::leaveEvent(QEvent *event)
{
    hideBlock();
    removeAnnotationHovering();

    QWidget::leaveEvent(event);
}

void KateIconBorder::mouseMoveEvent(QMouseEvent *e)
{
    const KateTextLayout &t = m_viewInternal->yToKateTextLayout(e->y());
    if (t.isValid()) {
        if (positionToArea(e->pos()) == FoldingMarkers) {
            showDelayedBlock(t.line());
        } else {
            hideBlock();
        }
        if (positionToArea(e->pos()) == AnnotationBorder) {
            KTextEditor::AnnotationModel *model = m_view->annotationModel() ?
                                                  m_view->annotationModel() : m_doc->annotationModel();
            if (model) {
                m_hoveredAnnotationGroupIdentifier = model->data( t.line(),
                                                                  (Qt::ItemDataRole) KTextEditor::AnnotationModel::GroupIdentifierRole ).toString();
                const QPoint viewRelativePos = m_view->mapFromGlobal(e->globalPos());
                QHelpEvent helpEvent(QEvent::ToolTip, viewRelativePos, e->globalPos());
                KTextEditor::StyleOptionAnnotationItem styleOption;
                initStyleOption(&styleOption);
                styleOption.rect = annotationLineRectInView(t.line());
                setStyleOptionLineData(&styleOption, e->y(), t.line(), model, m_hoveredAnnotationGroupIdentifier);
                m_annotationItemDelegate->helpEvent(&helpEvent, m_view, styleOption, model, t.line());

                QTimer::singleShot(0, this, SLOT(update()));
            }
        } else {
            if (positionToArea(e->pos()) == IconBorder) {
                m_doc->requestMarkTooltip(t.line(), e->globalPos());
            }

            m_hoveredAnnotationGroupIdentifier.clear();
            QTimer::singleShot(0, this, SLOT(update()));
        }
        if (positionToArea(e->pos()) != IconBorder) {
            QPoint p = m_viewInternal->mapFromGlobal(e->globalPos());
            QMouseEvent forward(QEvent::MouseMove, p, e->button(), e->buttons(), e->modifiers());
            m_viewInternal->mouseMoveEvent(&forward);
        }
    } else {
        // remove hovering if it's still there
        removeAnnotationHovering();
    }

    QWidget::mouseMoveEvent(e);
}

void KateIconBorder::mouseReleaseEvent(QMouseEvent *e)
{
    const int cursorOnLine = m_viewInternal->yToKateTextLayout(e->y()).line();
    if (cursorOnLine == m_lastClickedLine && cursorOnLine >= 0 && cursorOnLine <= m_doc->lastLine()) {
        BorderArea area = positionToArea(e->pos());
        if (area == IconBorder) {
            if (e->button() == Qt::LeftButton) {
                if (!m_doc->handleMarkClick(cursorOnLine)) {
                    KateViewConfig *config = m_view->config();
                    const uint editBits = m_doc->editableMarks();
                    // is the default or the only editable mark
                    const uint singleMark = qPopulationCount(editBits) > 1 ?
                        editBits & config->defaultMarkType() : editBits;
                    if (singleMark) {
                        if (m_doc->mark(cursorOnLine) & singleMark) {
                            m_doc->removeMark(cursorOnLine, singleMark);
                        } else {
                            m_doc->addMark(cursorOnLine, singleMark);
                        }
                    } else if (config->allowMarkMenu()) {
                        showMarkMenu(cursorOnLine, QCursor::pos());
                    }
                }
            } else if (e->button() == Qt::RightButton) {
                showMarkMenu(cursorOnLine, QCursor::pos());
            }
        }

        if (area == FoldingMarkers) {
            // ask the folding info for this line, if any folds are around!
            QVector<QPair<qint64, Kate::TextFolding::FoldingRangeFlags> > startingRanges = m_view->textFolding().foldingRangesStartingOnLine(cursorOnLine);
            bool anyFolded = false;
            for (int i = 0; i < startingRanges.size(); ++i)
                if (startingRanges[i].second & Kate::TextFolding::Folded) {
                    anyFolded = true;
                }

            // fold or unfold all ranges, remember if any action happened!
            bool actionDone = false;
            for (int i = 0; i < startingRanges.size(); ++i) {
                actionDone = (anyFolded ? m_view->textFolding().unfoldRange(startingRanges[i].first) : m_view->textFolding().foldRange(startingRanges[i].first)) || actionDone;
            }

            // if no action done, try to fold it, create non-persistent folded range, if possible!
            if (!actionDone) {
                // either use the fold for this line or the range that is highlighted ATM if any!
                KTextEditor::Range foldingRange = m_view->doc()->buffer().computeFoldingRangeForStartLine(cursorOnLine);
                if (!foldingRange.isValid() && m_foldingRange) {
                    foldingRange = m_foldingRange->toRange();
                }
                m_view->textFolding().newFoldingRange(foldingRange, Kate::TextFolding::Folded);
            }

            delete m_foldingPreview;
        }

        if (area == AnnotationBorder) {
            const bool singleClick = style()->styleHint(QStyle::SH_ItemView_ActivateItemOnSingleClick, nullptr, this);
            if (e->button() == Qt::LeftButton && singleClick) {
                emit m_view->annotationActivated(m_view, cursorOnLine);
            } else if (e->button() == Qt::RightButton) {
                showAnnotationMenu(cursorOnLine, e->globalPos());
            }
        }
    }

    QMouseEvent forward(QEvent::MouseButtonRelease,
                        QPoint(0, e->y()), e->button(), e->buttons(), e->modifiers());
    m_viewInternal->mouseReleaseEvent(&forward);
}

void KateIconBorder::mouseDoubleClickEvent(QMouseEvent *e)
{
    int cursorOnLine = m_viewInternal->yToKateTextLayout(e->y()).line();

    if (cursorOnLine == m_lastClickedLine &&
            cursorOnLine <= m_doc->lastLine()) {
        BorderArea area = positionToArea(e->pos());
        const bool singleClick = style()->styleHint(QStyle::SH_ItemView_ActivateItemOnSingleClick, nullptr, this);
        if (area == AnnotationBorder && !singleClick) {
            emit m_view->annotationActivated(m_view, cursorOnLine);
        }
    }
    QMouseEvent forward(QEvent::MouseButtonDblClick,
                        QPoint(0, e->y()), e->button(), e->buttons(), e->modifiers());
    m_viewInternal->mouseDoubleClickEvent(&forward);
}

void KateIconBorder::wheelEvent(QWheelEvent *e)
{
    QCoreApplication::sendEvent(m_viewInternal, e);
}

void KateIconBorder::showMarkMenu(uint line, const QPoint &pos)
{
    if (m_doc->handleMarkContextMenu(line, pos)) {
        return;
    }

    if (!m_view->config()->allowMarkMenu()) {
        return;
    }

    QMenu markMenu;
    QMenu selectDefaultMark;
    auto selectDefaultMarkActionGroup = new QActionGroup(&selectDefaultMark);

    QVector<int> vec(33);
    int i = 1;

    for (uint bit = 0; bit < 32; bit++) {
        MarkInterface::MarkTypes markType = (MarkInterface::MarkTypes)(1 << bit);
        if (!(m_doc->editableMarks() & markType)) {
            continue;
        }

        QAction *mA;
        QAction *dMA;
        const QPixmap icon = m_doc->markPixmap(markType);
        if (!m_doc->markDescription(markType).isEmpty()) {
            mA = markMenu.addAction(icon, m_doc->markDescription(markType));
            dMA = selectDefaultMark.addAction(icon, m_doc->markDescription(markType));
        } else {
            mA = markMenu.addAction(icon, i18n("Mark Type %1",  bit + 1));
            dMA = selectDefaultMark.addAction(icon, i18n("Mark Type %1",  bit + 1));
        }
        selectDefaultMarkActionGroup->addAction(dMA);
        mA->setData(i);
        mA->setCheckable(true);
        dMA->setData(i + 100);
        dMA->setCheckable(true);
        if (m_doc->mark(line) & markType) {
            mA->setChecked(true);
        }

        if (markType & KateViewConfig::global()->defaultMarkType()) {
            dMA->setChecked(true);
        }

        vec[i++] = markType;
    }

    if (markMenu.actions().count() == 0) {
        return;
    }

    if (markMenu.actions().count() > 1) {
        markMenu.addAction(i18n("Set Default Mark Type"))->setMenu(&selectDefaultMark);
    }

    QAction *rA = markMenu.exec(pos);
    if (!rA) {
        return;
    }
    int result = rA->data().toInt();
    if (result > 100) {
        KateViewConfig::global()->setDefaultMarkType(vec[result - 100]);
    } else {
        MarkInterface::MarkTypes markType = (MarkInterface::MarkTypes) vec[result];
        if (m_doc->mark(line) & markType) {
            m_doc->removeMark(line, markType);
        } else {
            m_doc->addMark(line, markType);
        }
    }
}

KTextEditor::AbstractAnnotationItemDelegate* KateIconBorder::annotationItemDelegate() const
{
    return m_annotationItemDelegate;
}

void KateIconBorder::setAnnotationItemDelegate(KTextEditor::AbstractAnnotationItemDelegate *delegate)
{
    if (delegate == m_annotationItemDelegate) {
        return;
    }

    // reset to default, but already on that?
    if (!delegate && m_isDefaultAnnotationItemDelegate) {
        // nothing to do
        return;
    }

    // make sure the tooltip is hidden
    if (m_annotationBorderOn && !m_hoveredAnnotationGroupIdentifier.isEmpty()) {
        m_hoveredAnnotationGroupIdentifier.clear();
        hideAnnotationTooltip();
    }

    disconnect(m_annotationItemDelegate, &AbstractAnnotationItemDelegate::sizeHintChanged,
               this, &KateIconBorder::updateAnnotationBorderWidth);
    if (!m_isDefaultAnnotationItemDelegate) {
        disconnect(m_annotationItemDelegate, &QObject::destroyed,
                   this, &KateIconBorder::handleDestroyedAnnotationItemDelegate);
    }

    if (!delegate) {
        // reset to a default delegate
        m_annotationItemDelegate = new KateAnnotationItemDelegate(m_viewInternal, this);
        m_isDefaultAnnotationItemDelegate = true;
    } else {
        // drop any default delegate
        if (m_isDefaultAnnotationItemDelegate) {
            delete m_annotationItemDelegate;
            m_isDefaultAnnotationItemDelegate = false;
        }

        m_annotationItemDelegate = delegate;
        // catch delegate being destroyed
        connect(delegate, &QObject::destroyed,
                this, &KateIconBorder::handleDestroyedAnnotationItemDelegate);
    }

    connect(m_annotationItemDelegate, &AbstractAnnotationItemDelegate::sizeHintChanged,
            this, &KateIconBorder::updateAnnotationBorderWidth);

    if (m_annotationBorderOn) {
        updateGeometry();

        QTimer::singleShot(0, this, SLOT(update()));
    }
}

void KateIconBorder::handleDestroyedAnnotationItemDelegate()
{
    setAnnotationItemDelegate(nullptr);
}

void KateIconBorder::initStyleOption(KTextEditor::StyleOptionAnnotationItem* styleOption) const
{
    styleOption->initFrom(this);
    styleOption->view = m_view;
    styleOption->decorationSize = QSize(iconPaneWidth, iconPaneWidth);
    styleOption->contentFontMetrics = m_view->renderer()->config()->fontMetrics();
}

void KateIconBorder::setStyleOptionLineData(KTextEditor::StyleOptionAnnotationItem* styleOption,
                                            int y,
                                            int realLine,
                                            const KTextEditor::AnnotationModel *model,
                                            const QString &annotationGroupIdentifier) const
{
    // calculate rendered displayed line
    const uint h = m_view->renderer()->lineHeight();
    const uint z = (y / h);

    KateAnnotationGroupPositionState annotationGroupPositionState(m_viewInternal, model,
                                                                  annotationGroupIdentifier,
                                                                  z, true);
    annotationGroupPositionState.nextLine(*styleOption, z, realLine);
}


QRect KateIconBorder::annotationLineRectInView(int line) const
{
    int x = 0;
    if (m_iconBorderOn) {
        x += iconPaneWidth + 2;
    }
    const int y = m_view->m_viewInternal->lineToY(line);

    return QRect(x, y, m_annotationBorderWidth, m_view->renderer()->lineHeight());
}

void KateIconBorder::updateAnnotationLine(int line)
{
    // TODO: why has the default value been 8, where is that magic number from?
    int width = 8;
    KTextEditor::AnnotationModel *model = m_view->annotationModel() ?
                                          m_view->annotationModel() : m_doc->annotationModel();

    if (model) {
        KTextEditor::StyleOptionAnnotationItem styleOption;
        initStyleOption(&styleOption);
        width = m_annotationItemDelegate->sizeHint(styleOption, model, line).width();
    }

    if (width > m_annotationBorderWidth) {
        m_annotationBorderWidth = width;
        updateGeometry();

        QTimer::singleShot(0, this, SLOT(update()));
    }
}

void KateIconBorder::showAnnotationMenu(int line, const QPoint &pos)
{
    QMenu menu;
    QAction a(i18n("Disable Annotation Bar"), &menu);
    a.setIcon(QIcon::fromTheme(QStringLiteral("dialog-close")));
    menu.addAction(&a);
    emit m_view->annotationContextMenuAboutToShow(m_view, &menu, line);
    if (menu.exec(pos) == &a) {
        m_view->setAnnotationBorderVisible(false);
    }
}

void KateIconBorder::hideAnnotationTooltip()
{
    m_annotationItemDelegate->hideTooltip(m_view);
}

void KateIconBorder::updateAnnotationBorderWidth()
{
    calcAnnotationBorderWidth();

    updateGeometry();

    QTimer::singleShot(0, this, SLOT(update()));
}

void KateIconBorder::calcAnnotationBorderWidth()
{
    // TODO: another magic number, not matching the one in updateAnnotationLine()
    m_annotationBorderWidth = 6;
    KTextEditor::AnnotationModel *model = m_view->annotationModel() ?
                                          m_view->annotationModel() : m_doc->annotationModel();

    if (model) {
        KTextEditor::StyleOptionAnnotationItem styleOption;
        initStyleOption(&styleOption);

        const int lineCount = m_view->doc()->lines();
        if (lineCount > 0) {
            const int checkedLineCount = m_hasUniformAnnotationItemSizes ? 1 : lineCount;
            for (int i = 0; i < checkedLineCount; ++i) {
                const int curwidth = m_annotationItemDelegate->sizeHint(styleOption, model, i).width();
                if (curwidth > m_annotationBorderWidth) {
                    m_annotationBorderWidth = curwidth;
                }
            }
        }
    }
}

void KateIconBorder::annotationModelChanged(KTextEditor::AnnotationModel *oldmodel, KTextEditor::AnnotationModel *newmodel)
{
    if (oldmodel) {
        oldmodel->disconnect(this);
    }
    if (newmodel) {
        connect(newmodel, SIGNAL(reset()), this, SLOT(updateAnnotationBorderWidth()));
        connect(newmodel, SIGNAL(lineChanged(int)), this, SLOT(updateAnnotationLine(int)));
    }
    updateAnnotationBorderWidth();
}

void KateIconBorder::displayRangeChanged()
{
    hideBlock();
    removeAnnotationHovering();
}

//END KateIconBorder

//BEGIN KateViewEncodingAction
// According to http://www.iana.org/assignments/ianacharset-mib
// the default/unknown mib value is 2.
#define MIB_DEFAULT 2

bool lessThanAction(KSelectAction *a, KSelectAction *b)
{
    return a->text() < b->text();
}

void KateViewEncodingAction::Private::init()
{
    QList<KSelectAction *> actions;

    q->setToolBarMode(MenuMode);

    int i;
    foreach (const QStringList &encodingsForScript, KCharsets::charsets()->encodingsByScript()) {
        KSelectAction *tmp = new KSelectAction(encodingsForScript.at(0), q);

        for (i = 1; i < encodingsForScript.size(); ++i) {
            tmp->addAction(encodingsForScript.at(i));
        }
        q->connect(tmp, SIGNAL(triggered(QAction*)), q, SLOT(_k_subActionTriggered(QAction*)));
        //tmp->setCheckable(true);
        actions << tmp;
    }
    std::sort(actions.begin(), actions.end(), lessThanAction);
    foreach (KSelectAction *action, actions) {
        q->addAction(action);
    }
}

void KateViewEncodingAction::Private::_k_subActionTriggered(QAction *action)
{
    if (currentSubAction == action) {
        return;
    }
    currentSubAction = action;
    bool ok = false;
    int mib = q->mibForName(action->text(), &ok);
    if (ok) {
        emit q->KSelectAction::triggered(action->text());
        emit q->triggered(q->codecForMib(mib));
    }
}

KateViewEncodingAction::KateViewEncodingAction(KTextEditor::DocumentPrivate *_doc, KTextEditor::ViewPrivate *_view, const QString &text, QObject *parent, bool saveAsMode)
    : KSelectAction(text, parent), doc(_doc), view(_view), d(new Private(this))
    , m_saveAsMode(saveAsMode)
{
    d->init();

    connect(menu(), SIGNAL(aboutToShow()), this, SLOT(slotAboutToShow()));
    connect(this, SIGNAL(triggered(QString)), this, SLOT(setEncoding(QString)));
}

KateViewEncodingAction::~KateViewEncodingAction()
{
    delete d;
}

void KateViewEncodingAction::slotAboutToShow()
{
    setCurrentCodec(doc->config()->encoding());
}

void KateViewEncodingAction::setEncoding(const QString &e)
{
    /**
     * in save as mode => trigger saveAs
     */
    if (m_saveAsMode) {
        doc->documentSaveAsWithEncoding(e);
        return;
    }

    /**
     * else switch encoding
     */
    doc->userSetEncodingForNextReload();
    doc->setEncoding(e);
    view->reloadFile();
}

int KateViewEncodingAction::mibForName(const QString &codecName, bool *ok) const
{
    // FIXME logic is good but code is ugly

    bool success = false;
    int mib = MIB_DEFAULT;
    KCharsets *charsets = KCharsets::charsets();

    QTextCodec *codec = charsets->codecForName(codecName, success);
    if (!success) {
        // Maybe we got a description name instead
        codec = charsets->codecForName(charsets->encodingForName(codecName), success);
    }

    if (codec) {
        mib = codec->mibEnum();
    }

    if (ok) {
        *ok = success;
    }

    if (success) {
        return mib;
    }

    qCWarning(LOG_KTE) << "Invalid codec name: "  << codecName;
    return MIB_DEFAULT;
}

QTextCodec *KateViewEncodingAction::codecForMib(int mib) const
{
    if (mib == MIB_DEFAULT) {
        // FIXME offer to change the default codec
        return QTextCodec::codecForLocale();
    } else {
        return QTextCodec::codecForMib(mib);
    }
}

QTextCodec *KateViewEncodingAction::currentCodec() const
{
    return codecForMib(currentCodecMib());
}

bool KateViewEncodingAction::setCurrentCodec(QTextCodec *codec)
{
    disconnect(this, SIGNAL(triggered(QString)), this, SLOT(setEncoding(QString)));

    int i, j;
    for (i = 0; i < actions().size(); ++i) {
        if (actions().at(i)->menu()) {
            for (j = 0; j < actions().at(i)->menu()->actions().size(); ++j) {
                if (!j && !actions().at(i)->menu()->actions().at(j)->data().isNull()) {
                    continue;
                }
                if (actions().at(i)->menu()->actions().at(j)->isSeparator()) {
                    continue;
                }

                if (codec == KCharsets::charsets()->codecForName(actions().at(i)->menu()->actions().at(j)->text())) {
                    d->currentSubAction = actions().at(i)->menu()->actions().at(j);
                    d->currentSubAction->setChecked(true);
                } else {
                    actions().at(i)->menu()->actions().at(j)->setChecked(false);
                }
            }
        }
    }

    connect(this, SIGNAL(triggered(QString)), this, SLOT(setEncoding(QString)));
    return true;
}

QString KateViewEncodingAction::currentCodecName() const
{
    return d->currentSubAction->text();
}

bool KateViewEncodingAction::setCurrentCodec(const QString &codecName)
{
    return setCurrentCodec(KCharsets::charsets()->codecForName(codecName));
}

int KateViewEncodingAction::currentCodecMib() const
{
    return mibForName(currentCodecName());
}

bool KateViewEncodingAction::setCurrentCodec(int mib)
{
    return setCurrentCodec(codecForMib(mib));
}
//END KateViewEncodingAction

//BEGIN KateViewBar related classes

KateViewBarWidget::KateViewBarWidget(bool addCloseButton, QWidget *parent)
    : QWidget(parent)
    , m_viewBar(nullptr)
{
    QHBoxLayout *layout = new QHBoxLayout(this);

    // NOTE: Here be cosmetics.
    layout->setContentsMargins(0, 0, 0, 0);

    // hide button
    if (addCloseButton) {
        QToolButton *hideButton = new QToolButton(this);
        hideButton->setAutoRaise(true);
        hideButton->setIcon(QIcon::fromTheme(QStringLiteral("dialog-close")));
        connect(hideButton, SIGNAL(clicked()), SIGNAL(hideMe()));
        layout->addWidget(hideButton);
        layout->setAlignment(hideButton, Qt::AlignLeft | Qt::AlignTop);
    }

    // widget to be used as parent for the real content
    m_centralWidget = new QWidget(this);
    layout->addWidget(m_centralWidget);

    setLayout(layout);
    setFocusProxy(m_centralWidget);
}

KateViewBar::KateViewBar(bool external, QWidget *parent, KTextEditor::ViewPrivate *view)
    : QWidget(parent), m_external(external), m_view(view), m_permanentBarWidget(nullptr)

{
    m_layout = new QVBoxLayout(this);
    m_stack = new QStackedWidget(this);
    m_layout->addWidget(m_stack);
    m_layout->setContentsMargins(0, 0, 0, 0);

    m_stack->hide();
    hide();
}

void KateViewBar::addBarWidget(KateViewBarWidget *newBarWidget)
{
    // just ignore additional adds for already existing widgets
    if (hasBarWidget(newBarWidget)) {
        return;
    }

    // add new widget, invisible...
    newBarWidget->hide();
    m_stack->addWidget(newBarWidget);
    newBarWidget->setAssociatedViewBar(this);
    connect(newBarWidget, SIGNAL(hideMe()), SLOT(hideCurrentBarWidget()));
}

void KateViewBar::removeBarWidget(KateViewBarWidget *barWidget)
{
    // remove only if there
    if (!hasBarWidget(barWidget)) {
        return;
    }

    m_stack->removeWidget(barWidget);
    barWidget->setAssociatedViewBar(nullptr);
    barWidget->hide();
    disconnect(barWidget, nullptr, this, nullptr);
}

void KateViewBar::addPermanentBarWidget(KateViewBarWidget *barWidget)
{
    Q_ASSERT(barWidget);
    Q_ASSERT(!m_permanentBarWidget);

    m_stack->addWidget(barWidget);
    m_stack->setCurrentWidget(barWidget);
    m_stack->show();
    m_permanentBarWidget = barWidget;
    m_permanentBarWidget->show();

    setViewBarVisible(true);
}

void KateViewBar::removePermanentBarWidget(KateViewBarWidget *barWidget)
{
    Q_ASSERT(m_permanentBarWidget == barWidget);
    Q_UNUSED(barWidget);

    const bool hideBar = m_stack->currentWidget() == m_permanentBarWidget;

    m_permanentBarWidget->hide();
    m_stack->removeWidget(m_permanentBarWidget);
    m_permanentBarWidget = nullptr;

    if (hideBar) {
        m_stack->hide();
        setViewBarVisible(false);
    }
}

bool KateViewBar::hasPermanentWidget(KateViewBarWidget *barWidget) const
{
    return (m_permanentBarWidget == barWidget);
}

void KateViewBar::showBarWidget(KateViewBarWidget *barWidget)
{
    Q_ASSERT(barWidget != nullptr);

    if (barWidget != qobject_cast<KateViewBarWidget *>(m_stack->currentWidget())) {
        hideCurrentBarWidget();
    }

    // raise correct widget
    m_stack->setCurrentWidget(barWidget);
    barWidget->show();
    barWidget->setFocus(Qt::ShortcutFocusReason);
    m_stack->show();
    setViewBarVisible(true);
}

bool KateViewBar::hasBarWidget(KateViewBarWidget *barWidget) const
{
    return m_stack->indexOf(barWidget) != -1;
}

void KateViewBar::hideCurrentBarWidget()
{
    KateViewBarWidget *current = qobject_cast<KateViewBarWidget *>(m_stack->currentWidget());
    if (current) {
        current->closed();
    }

    // if we have any permanent widget, make it visible again
    if (m_permanentBarWidget) {
        m_stack->setCurrentWidget (m_permanentBarWidget);
    } else {
        // else: hide the bar
        m_stack->hide();
        setViewBarVisible(false);
    }

    m_view->setFocus();
}

void KateViewBar::setViewBarVisible(bool visible)
{
    if (m_external) {
        if (visible) {
            m_view->mainWindow()->showViewBar(m_view);
        } else {
            m_view->mainWindow()->hideViewBar(m_view);
        }
    } else {
        setVisible(visible);
    }
}

bool KateViewBar::hiddenOrPermanent() const
{
    KateViewBarWidget *current = qobject_cast<KateViewBarWidget *>(m_stack->currentWidget());
    if (!isVisible() || (m_permanentBarWidget && m_permanentBarWidget == current)) {
        return true;
    }
    return false;
}

void KateViewBar::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        hideCurrentBarWidget();
        return;
    }
    QWidget::keyPressEvent(event);

}

void KateViewBar::hideEvent(QHideEvent *event)
{
    Q_UNUSED(event);
//   if (!event->spontaneous())
//     m_view->setFocus();
}

//END KateViewBar related classes

KatePasteMenu::KatePasteMenu(const QString &text, KTextEditor::ViewPrivate *view)
    : KActionMenu(text, view)
    , m_view(view)
{
    connect(menu(), SIGNAL(aboutToShow()), this, SLOT(slotAboutToShow()));
}

void KatePasteMenu::slotAboutToShow()
{
    menu()->clear();

    /**
     * insert complete paste history
     */
    int i = 0;
    Q_FOREACH (const auto &texts, KTextEditor::EditorPrivate::self()->clipboardHistory()) {
        /**
         * get text for the menu ;)
         */
        QString text;
        Q_FOREACH (const auto& t, texts) {
            if ( !text.isEmpty() ) {
                text.append(QLatin1String(" "));
            }
            text.append(t);
        }
        if ( texts.size() > 1 ) {
            text.prepend(QLatin1String("[") + i18nc("%1 entries", "always plural", texts.size()) + QLatin1String("] "));
        }
        QString leftPart = (text.size() > 48) ? (text.left(48) + QLatin1String("...")) : text;
        QAction *a = menu()->addAction(leftPart.replace(QLatin1String("\n"), QLatin1String(" ")), this, SLOT(paste()));
        a->setData(i++);
    }
}

void KatePasteMenu::paste()
{
    if (!sender()) {
        return;
    }

    QAction *action = qobject_cast<QAction *>(sender());
    if (!action) {
        return;
    }

    // get index
    int i = action->data().toInt();
    if (i >= KTextEditor::EditorPrivate::self()->clipboardHistory().size()) {
        return;
    }

    // paste
    m_view->pasteInternal(KTextEditor::EditorPrivate::self()->clipboardHistory().at(i));
}
