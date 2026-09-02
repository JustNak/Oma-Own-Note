#include "tablechrome.h"

#include "markdownhighlighter.h"

#include <QAbstractTextDocumentLayout>
#include <QFontMetricsF>
#include <QHash>
#include <QList>
#include <QPainter>
#include <QQuickTextDocument>
#include <QSize>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextLayout>
#include <QTextCharFormat>
#include <QTextLine>
#include <QTextOption>
#include <QTransform>
#include <QtQml>

#include <algorithm>
#include <limits>

TableChrome::TableChrome(QQuickItem *parent)
    : QQuickPaintedItem(parent) {
    setOpaquePainting(false);
    setFillColor(Qt::transparent);
    setAntialiasing(false);
}

void TableChrome::registerQmlType() {
    static const int typeId = qmlRegisterType<TableChrome>("OmaOwnNote", 1, 0,
                                                           "TableChrome");
    Q_UNUSED(typeId);
}

static qreal effectiveWrapWidth(QTextDocument *document, qreal wrapWidth) {
    if (wrapWidth > 1)
        return wrapWidth;
    if (!document)
        return 10000;
    const qreal textWidth = document->textWidth();
    if (textWidth > 1 && textWidth < 1e6)
        return textWidth;
    return 10000;
}

static void typingSpan(const QString &line, const MarkdownHighlighter::Span &cell,
                       int *typingStart, int *typingEnd) {
    int left = cell.start;
    const int right = cell.start + cell.length;
    if (left < right && (line.at(left) == QLatin1Char(' ')
                         || line.at(left) == QLatin1Char('\t')))
        ++left;
    *typingStart = left;
    *typingEnd = right;
}

static void contentSpan(const QString &line, const MarkdownHighlighter::Span &cell,
                        int *contentStart, int *contentEnd, QString *text) {
    int start = cell.start;
    int end = cell.start + cell.length;
    while (start < end && (line.at(start) == QLatin1Char(' ')
                           || line.at(start) == QLatin1Char('\t')))
        ++start;
    while (end > start && (line.at(end - 1) == QLatin1Char(' ')
                           || line.at(end - 1) == QLatin1Char('\t')))
        --end;
    if (start >= end) {
        int left = cell.start;
        if (left < cell.start + cell.length
                && (line.at(left) == QLatin1Char(' ')
                    || line.at(left) == QLatin1Char('\t')))
            ++left;
        *contentStart = left;
        *contentEnd = left;
        *text = QString();
        return;
    }
    *contentStart = start;
    *contentEnd = end;
    *text = line.mid(start, end - start);
}

// Pretty-print padding stays hidden, but spaces at or before the caret are
// real cell text so a typed trailing space stays visible and the overlay
// caret can sit in it.
static QString visibleCellText(const QString &line, int contentStart, int contentEnd,
                               int typingStart, int typingEnd,
                               int blockPosition, int cursorPosition) {
    int visibleEnd = contentEnd;
    if (cursorPosition >= 0) {
        const int absStart = blockPosition + typingStart;
        const int absEnd = blockPosition + typingEnd;
        if (cursorPosition >= absStart && cursorPosition <= absEnd) {
            const int cursorInLine = cursorPosition - blockPosition;
            visibleEnd = qBound(typingStart, qMax(contentEnd, cursorInLine), typingEnd);
        }
    }
    if (visibleEnd <= contentStart)
        return {};
    return line.mid(contentStart, visibleEnd - contentStart);
}

struct CellSpan {
    int contentStart = 0;
    int contentEnd = 0;
    int typingStart = 0;
    int typingEnd = 0;
    QString text;
};

static CellSpan cellSpanFor(const QString &line, const MarkdownHighlighter::Span &cell,
                            int blockPosition, int cursorPosition) {
    CellSpan span;
    typingSpan(line, cell, &span.typingStart, &span.typingEnd);
    contentSpan(line, cell, &span.contentStart, &span.contentEnd, &span.text);
    span.text = visibleCellText(line, span.contentStart, span.contentEnd,
                                span.typingStart, span.typingEnd,
                                blockPosition, cursorPosition);
    return span;
}

static qreal layoutCellHeight(const QString &text, const QFont &font, qreal width) {
    if (text.isEmpty() || width < 1)
        return 0;

    QTextLayout layout(text, font);
    QTextOption option;
    option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    option.setAlignment(Qt::AlignLeft | Qt::AlignTop);
    layout.setTextOption(option);
    layout.beginLayout();
    qreal y = 0;
    while (true) {
        QTextLine line = layout.createLine();
        if (!line.isValid())
            break;
        line.setLineWidth(width);
        line.setPosition(QPointF(0, y));
        y += line.height();
    }
    layout.endLayout();
    return y;
}

static void prepareCellLayout(QTextLayout *layout, const QString &text,
                              const QFont &font, qreal width) {
    layout->setFont(font);
    layout->setText(text);
    QTextOption option;
    option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    option.setAlignment(Qt::AlignLeft | Qt::AlignTop);
    layout->setTextOption(option);
    layout->beginLayout();
    qreal y = 0;
    if (text.isEmpty()) {
        layout->endLayout();
        return;
    }
    while (true) {
        QTextLine line = layout->createLine();
        if (!line.isValid())
            break;
        line.setLineWidth(qMax(qreal(1), width));
        line.setPosition(QPointF(0, y));
        y += line.height();
    }
    layout->endLayout();
}

// An empty cell lays out no lines, so its bounding rect is zero-height. Treat
// it as a single line of the cell font so the caret and text origin are
// centred like a one-line cell instead of collapsing to the row's midpoint.
static qreal cellLineHeight(const QTextLayout &layout, const QFont &font) {
    if (layout.lineCount() > 0)
        return layout.boundingRect().height();
    return QFontMetricsF(font).height();
}

static qreal cellTextOffset(const QTextLayout &layout, const QFont &font,
                            const QRectF &textRect) {
    return qMax(qreal(0), (textRect.height() - cellLineHeight(layout, font)) * 0.5);
}

static QVector<qreal> shrinkInners(QVector<qreal> inners, qreal minInner, qreal extra) {
    while (extra > 0.5) {
        int widest = -1;
        qreal widestWidth = minInner;
        for (int i = 0; i < inners.size(); ++i) {
            if (inners.at(i) > widestWidth + 0.5) {
                widestWidth = inners.at(i);
                widest = i;
            }
        }
        if (widest < 0)
            break;
        const qreal room = inners.at(widest) - minInner;
        const qreal take = qMin(extra, room);
        inners[widest] -= take;
        extra -= take;
    }
    return inners;
}

QVector<TableChrome::TableGeom> TableChrome::buildGeometries(QTextDocument *document,
                                                             qreal wrapWidth,
                                                             int cursorPosition) {
    QVector<TableGeom> tables;
    if (!document)
        return tables;

    QAbstractTextDocumentLayout *layout = document->documentLayout();
    if (!layout)
        return tables;

    const QFont font = document->defaultFont();
    QFont headerFont = font;
    headerFont.setBold(true);
    const QFontMetricsF metrics(font);
    const qreal minRow = MarkdownHighlighter::tableDataRowLineHeight(font);
    const qreal pipeAdvance = qMax(qreal(1), metrics.horizontalAdvance(QLatin1Char('|')));
    const qreal spaceAdvance = qMax(qreal(1), metrics.horizontalAdvance(QLatin1Char(' ')));
    const qreal minInner = qMax(spaceAdvance * 3, metrics.averageCharWidth() * 3);
    const qreal cap = effectiveWrapWidth(document, wrapWidth);
    const qreal margin = document->documentMargin();

    QVector<QTextBlock> run;
    const auto flush = [&]() {
        if (run.isEmpty())
            return;

        QVector<QTextBlock> dataRows;
        QTextBlock header;
        for (const QTextBlock &block : run) {
            if (MarkdownHighlighter::isTableSeparator(block.text()))
                continue;
            if (!header.isValid() && block.next().isValid()
                    && MarkdownHighlighter::isTableSeparator(block.next().text()))
                header = block;
            dataRows.append(block);
        }
        if (dataRows.isEmpty()) {
            run.clear();
            return;
        }

        int columns = 0;
        QVector<QVector<MarkdownHighlighter::Span>> rowCells;
        QVector<QString> rowTexts;
        rowCells.reserve(dataRows.size());
        for (const QTextBlock &block : dataRows) {
            const MarkdownHighlighter::TableLine parsed =
                MarkdownHighlighter::parseTableLine(block.text());
            rowCells.append(parsed.cells);
            rowTexts.append(block.text());
            columns = qMax(columns, parsed.cells.size());
        }
        if (columns < 1) {
            run.clear();
            return;
        }

        QVector<qreal> inners(columns, minInner);
        for (int r = 0; r < dataRows.size(); ++r) {
            const QFont &rowFont = (header.isValid()
                                    && dataRows.at(r).position() == header.position())
                ? headerFont : font;
            const QFontMetricsF rowMetrics(rowFont);
            const QVector<MarkdownHighlighter::Span> &cells = rowCells.at(r);
            for (int c = 0; c < columns; ++c) {
                QString text;
                if (c < cells.size())
                    text = cellSpanFor(rowTexts.at(r), cells.at(c),
                                       dataRows.at(r).position(), cursorPosition).text;
                inners[c] = qMax(inners.at(c), rowMetrics.horizontalAdvance(text));
            }
        }

        const qreal gutter = 2 * spaceAdvance + pipeAdvance;
        qreal total = pipeAdvance;
        for (qreal inner : inners)
            total += inner + gutter;
        const qreal available = qMax(pipeAdvance + minInner * columns + gutter * columns,
                                     cap - margin);
        if (total > available)
            inners = shrinkInners(inners, minInner, total - available);

        QVector<qreal> columnXs;
        qreal x = margin + pipeAdvance * 0.5;
        columnXs.append(x);
        for (int c = 0; c < columns; ++c) {
            x += inners.at(c) + gutter;
            columnXs.append(x);
        }

        const QRectF firstRow = layout->blockBoundingRect(dataRows.first());
        qreal y = firstRow.top();
        QVector<qreal> rowEdges;
        rowEdges.append(y);
        QVector<qreal> rowHeights;
        rowHeights.reserve(dataRows.size());
        for (int r = 0; r < dataRows.size(); ++r) {
            const QFont &rowFont = (header.isValid()
                                    && dataRows.at(r).position() == header.position())
                ? headerFont : font;
            qreal rowHeight = minRow;
            const QVector<MarkdownHighlighter::Span> &cells = rowCells.at(r);
            for (int c = 0; c < columns; ++c) {
                QString text;
                if (c < cells.size())
                    text = cellSpanFor(rowTexts.at(r), cells.at(c),
                                       dataRows.at(r).position(), cursorPosition).text;
                const qreal textWidth = qMax(qreal(1), inners.at(c));
                rowHeight = qMax(rowHeight, layoutCellHeight(text, rowFont, textWidth));
            }
            rowHeights.append(rowHeight);
            y += rowHeight;
            // The collapsed separator block still occupies its fixed line
            // height between the header and the first body block. Fold it
            // into the header row's edge so body hairlines land on the body
            // blocks instead of drifting above them.
            if (header.isValid() && dataRows.at(r).position() == header.position())
                y += MarkdownHighlighter::tableSeparatorLineHeight;
            rowEdges.append(y);
        }

        TableGeom geom;
        geom.rowHeights = rowHeights;
        geom.box.columns = columnXs;
        geom.box.rowEdges = rowEdges;
        geom.box.bounds = QRectF(QPointF(columnXs.first(), rowEdges.first()),
                                 QPointF(columnXs.last(), rowEdges.last()));
        for (int r = 0; r < dataRows.size(); ++r) {
            geom.rowBlockPositions.append(dataRows.at(r).position());
            if (header.isValid() && dataRows.at(r).position() == header.position()) {
                geom.box.header = QRectF(QPointF(columnXs.first(), rowEdges.at(r)),
                                         QPointF(columnXs.last(), rowEdges.at(r + 1)));
            }
            const bool isHeader = header.isValid()
                && dataRows.at(r).position() == header.position();
            const QVector<MarkdownHighlighter::Span> &cells = rowCells.at(r);
            for (int c = 0; c < columns; ++c) {
                CellGeom cell;
                cell.row = r;
                cell.column = c;
                cell.blockPosition = dataRows.at(r).position();
                cell.header = isHeader;
                cell.rect = QRectF(QPointF(columnXs.at(c), rowEdges.at(r)),
                                   QPointF(columnXs.at(c + 1), rowEdges.at(r + 1)));
                const qreal textLeft = columnXs.at(c) + pipeAdvance * 0.5 + spaceAdvance;
                const qreal textRight = columnXs.at(c + 1) - pipeAdvance * 0.5 - spaceAdvance;
                cell.textRect = QRectF(QPointF(textLeft, rowEdges.at(r)),
                                       QPointF(qMax(textLeft + 1, textRight),
                                               rowEdges.at(r + 1)));
                if (c < cells.size()) {
                    const CellSpan span = cellSpanFor(rowTexts.at(r), cells.at(c),
                                                      cell.blockPosition, cursorPosition);
                    cell.contentStart = span.contentStart;
                    cell.contentEnd = span.contentEnd;
                    cell.typingStart = span.typingStart;
                    cell.typingEnd = span.typingEnd;
                    cell.text = span.text;
                } else {
                    cell.contentStart = dataRows.at(r).text().size();
                    cell.contentEnd = cell.contentStart;
                    cell.typingStart = cell.contentStart;
                    cell.typingEnd = cell.contentStart;
                }
                geom.cells.append(cell);
            }
        }
        tables.append(geom);
        run.clear();
    };

    bool inFence = false;
    for (QTextBlock block = document->begin(); block.isValid(); block = block.next()) {
        if (MarkdownHighlighter::isFenceLine(block.text())) {
            flush();
            inFence = !inFence;
            continue;
        }
        if (inFence || !MarkdownHighlighter::isTableRow(block.text()))
            flush();
        else
            run.append(block);
    }
    flush();
    return tables;
}

QVector<TableChrome::TableBox> TableChrome::collectTables(QTextDocument *document) {
    return collectTables(document, 0);
}

QVector<TableChrome::TableBox> TableChrome::collectTables(QTextDocument *document,
                                                          qreal wrapWidth) {
    QVector<TableBox> tables;
    const QVector<TableGeom> geoms = buildGeometries(document, wrapWidth);
    tables.reserve(geoms.size());
    for (const TableGeom &geom : geoms)
        tables.append(geom.box);
    return tables;
}

QHash<int, qreal> TableChrome::dataRowHeights(QTextDocument *document, qreal wrapWidth,
                                              int cursorPosition) {
    QHash<int, qreal> heights;
    const QVector<TableGeom> geoms = buildGeometries(document, wrapWidth, cursorPosition);
    for (const TableGeom &geom : geoms) {
        for (int i = 0; i < geom.rowBlockPositions.size(); ++i)
            heights.insert(geom.rowBlockPositions.at(i), geom.rowHeights.at(i));
    }
    return heights;
}

qreal TableChrome::naturalWidthOf(QTextDocument *document) {
    if (!document)
        return 0;

    const QFontMetricsF metrics(document->defaultFont());
    const qreal left = document->documentMargin();
    qreal maxAdvance = 0;
    bool inFence = false;
    for (QTextBlock block = document->begin(); block.isValid(); block = block.next()) {
        if (MarkdownHighlighter::isFenceLine(block.text())) {
            inFence = !inFence;
            continue;
        }
        if (inFence || !MarkdownHighlighter::isTableRow(block.text()))
            continue;
        maxAdvance = qMax(maxAdvance, metrics.horizontalAdvance(block.text()));
    }
    if (maxAdvance <= 0)
        return 0;
    return left + maxAdvance + 2;
}

static QPoint devicePoint(const QTransform &world, qreal x, qreal y) {
    const QPointF mapped = world.map(QPointF(x, y));
    return QPoint(qRound(mapped.x()), qRound(mapped.y()));
}

static QVector<int> snapRowStrokes(const QTransform &world, qreal x,
                                   const QVector<qreal> &edges) {
    QVector<int> strokes;
    if (edges.isEmpty())
        return strokes;

    const int first = devicePoint(world, x, edges.first()).y();
    strokes.append(first);
    const int lastIndex = edges.size() - 1;
    if (lastIndex == 0)
        return strokes;

    const qreal itemSpan = edges.last() - edges.first();
    const int last = devicePoint(world, x, edges.last()).y();
    if (qAbs(itemSpan) < qreal(0.5)) {
        for (int i = 1; i <= lastIndex; ++i)
            strokes.append(strokes.last() + 1);
        return strokes;
    }

    for (int i = 1; i <= lastIndex; ++i) {
        const qreal t = (edges.at(i) - edges.first()) / itemSpan;
        int y = first + qRound(t * (last - first));
        if (y <= strokes.last())
            y = strokes.last() + 1;
        strokes.append(y);
    }
    return strokes;
}

void TableChrome::paintCellText(QPainter *painter, const TableGeom &geom,
                                const QFont &font, const QColor &textColor,
                                const QColor &selectionColor, int selectionStart,
                                int selectionEnd, const QString &searchQuery,
                                int currentMatchStart, const QColor &searchColor,
                                const QColor &currentSearchColor) {
    QFont headerFont = font;
    headerFont.setBold(true);
    const int selFrom = qMin(selectionStart, selectionEnd);
    const int selTo = qMax(selectionStart, selectionEnd);
    const bool searching = !searchQuery.isEmpty() && searchColor.isValid();

    for (const CellGeom &cell : geom.cells) {
        if (cell.text.isEmpty() && selFrom == selTo)
            continue;

        const QFont &cellFont = cell.header ? headerFont : font;
        QTextLayout cellLayout;
        prepareCellLayout(&cellLayout, cell.text, cellFont, cell.textRect.width());
        const qreal yOff = cellTextOffset(cellLayout, cellFont, cell.textRect);
        const QPointF origin(cell.textRect.left(), cell.textRect.top() + yOff);

        QList<QTextLayout::FormatRange> ranges;
        if (selTo > selFrom) {
            const int absStart = cell.blockPosition + cell.contentStart;
            const int absEnd = cell.blockPosition + cell.contentStart + cell.text.size();
            const int overlapStart = qMax(selFrom, absStart);
            const int overlapEnd = qMin(selTo, absEnd);
            if (overlapEnd > overlapStart) {
                QTextLayout::FormatRange range;
                range.start = overlapStart - absStart;
                range.length = overlapEnd - overlapStart;
                range.format.setBackground(selectionColor);
                range.format.setForeground(textColor);
                ranges.append(range);
            }
        }
        if (searching) {
            int from = 0;
            while ((from = cell.text.indexOf(searchQuery, from, Qt::CaseInsensitive)) >= 0) {
                QTextLayout::FormatRange range;
                range.start = from;
                range.length = searchQuery.size();
                const int absStart = cell.blockPosition + cell.contentStart + from;
                const QColor fill = (currentMatchStart >= 0 && absStart == currentMatchStart
                                     && currentSearchColor.isValid())
                    ? currentSearchColor : searchColor;
                range.format.setBackground(fill);
                range.format.setForeground(textColor);
                ranges.append(range);
                from += qMax(1, searchQuery.size());
            }
        }
        painter->setPen(textColor);
        if (!ranges.isEmpty())
            cellLayout.draw(painter, origin, ranges);
        else
            cellLayout.draw(painter, origin);
    }
}

void TableChrome::paintTables(QPainter *painter, QTextDocument *document,
                              const QColor &paper, const QColor &text,
                              const QColor &rule) {
    paintTables(painter, document, paper, text, rule, 0);
}

void TableChrome::paintTables(QPainter *painter, QTextDocument *document,
                              const QColor &, const QColor &text,
                              const QColor &rule, qreal wrapWidth,
                              int selectionStart, int selectionEnd,
                              const QColor &selectionColor,
                              int cursorPosition) {
    if (!painter || !document)
        return;

    const QVector<TableGeom> geoms = buildGeometries(document, wrapWidth, cursorPosition);
    if (geoms.isEmpty())
        return;

    const QTransform world = painter->combinedTransform();
    const QFont font = document->defaultFont();
    QString searchQuery;
    int currentMatchStart = -1;
    QColor searchColor;
    QColor currentSearchColor;
    if (auto *highlighter = document->findChild<MarkdownHighlighter *>()) {
        searchQuery = highlighter->searchQuery();
        currentMatchStart = highlighter->currentMatchStart();
        searchColor = highlighter->searchBackground();
        currentSearchColor = highlighter->currentSearchBackground();
    }

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, false);

    for (const TableGeom &geom : geoms) {
        painter->save();
        painter->setTransform(world);
        paintCellText(painter, geom, font, text, selectionColor,
                      selectionStart, selectionEnd, searchQuery, currentMatchStart,
                      searchColor, currentSearchColor);
        painter->restore();
    }

    painter->save();
    painter->resetTransform();
    painter->setRenderHint(QPainter::Antialiasing, false);

    for (const TableGeom &geom : geoms) {
        const QRectF bounds = geom.box.bounds.normalized();
        if (bounds.width() < 2 || bounds.height() < 2)
            continue;

        const QVector<int> rowStrokes = snapRowStrokes(world, bounds.left(), geom.box.rowEdges);
        if (rowStrokes.isEmpty())
            continue;

        const QPoint topLeft = devicePoint(world, bounds.left(), bounds.top());
        const QPoint bottomRight = devicePoint(world, bounds.right(), bounds.bottom());
        const int left = topLeft.x();
        const int right = bottomRight.x();
        const int top = rowStrokes.first();
        const int bottom = rowStrokes.last();
        const int height = qMax(1, bottom - top + 1);
        const int width = qMax(1, right - left + 1);

        for (qreal x : geom.box.columns) {
            const int stroke = devicePoint(world, x, bounds.top()).x();
            painter->fillRect(stroke, top, 1, height, rule);
        }
        for (int stroke : rowStrokes)
            painter->fillRect(left, stroke, width, 1, rule);
    }

    painter->restore();
    painter->restore();
}

void TableChrome::setTextDocument(QObject *textDocument) {
    if (m_textDocumentObject == textDocument)
        return;

    m_textDocumentObject = textDocument;
    QTextDocument *document = nullptr;
    if (auto *quick = qobject_cast<QQuickTextDocument *>(textDocument))
        document = quick->textDocument();
    else
        document = qobject_cast<QTextDocument *>(textDocument);
    bindDocument(document);
    emit textDocumentChanged();
    update();
}

void TableChrome::setPaper(const QColor &paper) {
    if (m_paper == paper)
        return;
    m_paper = paper;
    emit paperChanged();
    update();
}

void TableChrome::setTextColor(const QColor &textColor) {
    if (m_textColor == textColor)
        return;
    m_textColor = textColor;
    emit textColorChanged();
    update();
}

void TableChrome::setRuleColor(const QColor &ruleColor) {
    if (m_ruleColor == ruleColor)
        return;
    m_ruleColor = ruleColor;
    emit ruleColorChanged();
    update();
}

void TableChrome::setSelectionColor(const QColor &selectionColor) {
    if (m_selectionColor == selectionColor)
        return;
    m_selectionColor = selectionColor;
    emit selectionColorChanged();
    update();
}

void TableChrome::setViewScale(qreal viewScale) {
    const qreal scale = qMax(viewScale, qreal(0.01));
    if (qFuzzyCompare(m_viewScale, scale))
        return;
    m_viewScale = scale;
    emit viewScaleChanged();
    syncTextureSize();
    update();
}

void TableChrome::setWrapWidth(qreal wrapWidth) {
    const qreal next = qMax(wrapWidth, qreal(0));
    if (qAbs(m_wrapWidth - next) < 0.5)
        return;
    m_wrapWidth = next;
    markLayoutDirty();
    emit wrapWidthChanged();
    update();
}

void TableChrome::setCursorPosition(int cursorPosition) {
    if (m_cursorPosition == cursorPosition)
        return;
    m_cursorPosition = cursorPosition;
    markLayoutDirty();
    emit cursorPositionChanged();
    update();
}

void TableChrome::setSelectionStart(int selectionStart) {
    if (m_selectionStart == selectionStart)
        return;
    m_selectionStart = selectionStart;
    emit selectionStartChanged();
    update();
}

void TableChrome::setSelectionEnd(int selectionEnd) {
    if (m_selectionEnd == selectionEnd)
        return;
    m_selectionEnd = selectionEnd;
    emit selectionEndChanged();
    update();
}

void TableChrome::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) {
    QQuickPaintedItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.size() != oldGeometry.size()) {
        syncTextureSize();
        markLayoutDirty();
    }
}

void TableChrome::syncTextureSize() {
    const QSize size(qMax(1, qRound(width() * m_viewScale)),
                     qMax(1, qRound(height() * m_viewScale)));
    if (textureSize() != size)
        setTextureSize(size);
}

void TableChrome::paint(QPainter *painter) {
    if (!painter)
        return;
    ensureLayout();
    const qreal mapped = qAbs(painter->transform().m11());
    const qreal wrap = m_wrapWidth;
    if (mapped > 0.98 && mapped < 1.02 && !qFuzzyCompare(m_viewScale, qreal(1))) {
        painter->save();
        painter->scale(m_viewScale, m_viewScale);
        paintTables(painter, m_document, m_paper, m_textColor, m_ruleColor, wrap,
                    m_selectionStart, m_selectionEnd, m_selectionColor,
                    m_cursorPosition);
        painter->restore();
        return;
    }
    paintTables(painter, m_document, m_paper, m_textColor, m_ruleColor, wrap,
                m_selectionStart, m_selectionEnd, m_selectionColor,
                m_cursorPosition);
}

void TableChrome::refreshNaturalWidth() {
    const qreal nextWidth = naturalWidthOf(m_document);
    if (qAbs(m_naturalWidth - nextWidth) < qreal(0.5))
        return;
    m_naturalWidth = nextWidth;
    emit naturalWidthChanged();
}

void TableChrome::markLayoutDirty() {
    m_layoutDirty = true;
    ++m_layoutRevision;
    emit layoutChanged();
}

void TableChrome::ensureLayout() const {
    if (!m_layoutDirty)
        return;
    m_tables = buildGeometries(m_document, m_wrapWidth, m_cursorPosition);
    m_layoutDirty = false;
}

const TableChrome::CellGeom *TableChrome::cellAtPosition(int position) const {
    ensureLayout();
    const CellGeom *best = nullptr;
    int bestDistance = std::numeric_limits<int>::max();
    for (const TableGeom &table : m_tables) {
        for (const CellGeom &cell : table.cells) {
            const int start = cell.blockPosition + cell.typingStart;
            const int end = cell.blockPosition + cell.typingEnd;
            if (position >= start && position <= end)
                return &cell;
        }
    }
    if (!m_document)
        return nullptr;

    const QTextBlock block = m_document->findBlock(position);
    if (!block.isValid()
            || position < block.position()
            || position >= block.position() + block.length())
        return nullptr;

    const int blockPosition = block.position();
    for (const TableGeom &table : m_tables) {
        for (const CellGeom &cell : table.cells) {
            if (cell.blockPosition != blockPosition)
                continue;
            const int start = cell.blockPosition + cell.typingStart;
            const int end = cell.blockPosition + cell.typingEnd;
            const int distance = position < start ? start - position : position - end;
            if (distance < bestDistance) {
                bestDistance = distance;
                best = &cell;
            }
        }
    }
    return best;
}

int TableChrome::hitTest(qreal x, qreal y) const {
    ensureLayout();
    const TableGeom *hitTable = nullptr;
    int hitRow = -1;
    for (const TableGeom &table : m_tables) {
        const QRectF bounds = table.box.bounds;
        if (y < bounds.top() || y > bounds.bottom())
            continue;
        hitTable = &table;
        for (int i = 0; i + 1 < table.box.rowEdges.size(); ++i) {
            if (y <= table.box.rowEdges.at(i + 1) || i + 2 == table.box.rowEdges.size()) {
                hitRow = i;
                break;
            }
        }
        break;
    }
    if (!hitTable || hitRow < 0)
        return -1;

    int hitColumn = 0;
    if (x <= hitTable->box.columns.first()) {
        hitColumn = 0;
    } else if (x >= hitTable->box.columns.last()) {
        hitColumn = qMax(0, hitTable->box.columns.size() - 2);
    } else {
        for (int c = 0; c + 1 < hitTable->box.columns.size(); ++c) {
            if (x <= hitTable->box.columns.at(c + 1)) {
                hitColumn = c;
                break;
            }
        }
    }

    const CellGeom *cell = nullptr;
    for (const CellGeom &candidate : hitTable->cells) {
        if (candidate.row == hitRow && candidate.column == hitColumn) {
            cell = &candidate;
            break;
        }
    }
    if (!cell)
        return -1;

    QFont font = m_document ? m_document->defaultFont() : QFont();
    if (cell->header)
        font.setBold(true);
    QTextLayout layout;
    prepareCellLayout(&layout, cell->text, font, cell->textRect.width());
    const qreal yOff = cellTextOffset(layout, font, cell->textRect);
    const qreal localX = x - cell->textRect.left();
    const qreal localY = y - cell->textRect.top() - yOff;

    int cursor = cell->text.size();
    if (layout.lineCount() > 0) {
        int lineIndex = 0;
        for (int i = 0; i < layout.lineCount(); ++i) {
            const QTextLine line = layout.lineAt(i);
            if (localY <= line.y() + line.height() || i + 1 == layout.lineCount()) {
                lineIndex = i;
                break;
            }
        }
        cursor = layout.lineAt(lineIndex).xToCursor(localX);
    } else {
        cursor = 0;
    }
    cursor = qBound(0, cursor, qMax(0, cell->typingEnd - cell->contentStart));
    if (cursor > cell->text.size())
        cursor = cell->text.size();
    return cell->blockPosition + cell->contentStart + cursor;
}

QRectF TableChrome::caretRect(int position) const {
    const CellGeom *cell = cellAtPosition(position);
    if (!cell)
        return {};

    const int start = cell->blockPosition + cell->contentStart;
    const int typingEnd = cell->blockPosition + cell->typingEnd;
    if (position < start - 1 || position > typingEnd)
        return {};

    QFont font = m_document ? m_document->defaultFont() : QFont();
    if (cell->header)
        font.setBold(true);

    QString text = cell->text;
    int offset = qBound(0, position - start, text.size());
    if (position > start + text.size() && position <= typingEnd && m_document) {
        const QTextBlock block = m_document->findBlock(cell->blockPosition);
        if (block.isValid()) {
            const int visEnd = qBound(cell->typingStart,
                                      qMax(cell->contentEnd, position - cell->blockPosition),
                                      cell->typingEnd);
            text = block.text().mid(cell->contentStart, visEnd - cell->contentStart);
            offset = qBound(0, position - start, text.size());
        }
    }

    QTextLayout layout;
    prepareCellLayout(&layout, text, font, cell->textRect.width());
    const qreal yOff = cellTextOffset(layout, font, cell->textRect);

    qreal caretX = cell->textRect.left();
    qreal caretY = cell->textRect.top() + yOff;
    qreal caretH = cellLineHeight(layout, font);
    if (layout.lineCount() > 0) {
        const QTextLine line = layout.lineForTextPosition(offset);
        if (line.isValid()) {
            caretX = cell->textRect.left() + line.cursorToX(offset);
            caretY = cell->textRect.top() + yOff + line.y();
            caretH = line.height();
        }
    }
    return QRectF(caretX, caretY, 1, caretH);
}

bool TableChrome::positionInTable(int position) const {
    ensureLayout();
    for (const TableGeom &table : m_tables) {
        for (int blockPosition : table.rowBlockPositions) {
            if (!m_document)
                continue;
            const QTextBlock block = m_document->findBlock(blockPosition);
            if (!block.isValid())
                continue;
            if (position >= block.position()
                    && position < block.position() + block.length())
                return true;
        }
    }
    return false;
}

int TableChrome::movePositionVertically(int position, int direction) const {
    const CellGeom *cellPtr = cellAtPosition(position);
    if (!cellPtr)
        return -1;
    const CellGeom cell = *cellPtr;

    QFont font = m_document ? m_document->defaultFont() : QFont();
    if (cell.header)
        font.setBold(true);
    QTextLayout layout;
    prepareCellLayout(&layout, cell.text, font, cell.textRect.width());
    const int start = cell.blockPosition + cell.contentStart;
    const int offset = qBound(0, position - start, cell.text.size());
    if (layout.lineCount() > 0) {
        const QTextLine line = layout.lineForTextPosition(offset);
        if (line.isValid()) {
            const int nextIndex = line.lineNumber() + (direction > 0 ? 1 : -1);
            if (nextIndex >= 0 && nextIndex < layout.lineCount()) {
                const QTextLine next = layout.lineAt(nextIndex);
                const int cursor = next.xToCursor(line.cursorToX(offset));
                return start + qBound(0, cursor, cell.text.size());
            }
        }
    }

    const int nextRow = cell.row + (direction > 0 ? 1 : -1);
    for (const TableGeom &table : m_tables) {
        bool owns = false;
        for (const CellGeom &candidate : table.cells) {
            if (candidate.blockPosition == cell.blockPosition
                    && candidate.column == cell.column) {
                owns = true;
                break;
            }
        }
        if (!owns)
            continue;
        for (const CellGeom &candidate : table.cells) {
            if (candidate.row == nextRow && candidate.column == cell.column)
                return candidate.blockPosition + candidate.contentStart;
        }
        break;
    }
    return -1;
}

void TableChrome::bindDocument(QTextDocument *document) {
    if (m_document) {
        disconnect(m_document, nullptr, this, nullptr);
        if (auto *layout = m_document->documentLayout())
            disconnect(layout, nullptr, this, nullptr);
    }

    m_document = document;
    markLayoutDirty();
    if (!m_document) {
        if (!qFuzzyIsNull(m_naturalWidth)) {
            m_naturalWidth = 0;
            emit naturalWidthChanged();
        }
        m_tables.clear();
        return;
    }

    connect(m_document, &QTextDocument::contentsChanged, this, [this]() {
        markLayoutDirty();
        refreshNaturalWidth();
        update();
    });
    if (auto *layout = m_document->documentLayout()) {
        connect(layout, &QAbstractTextDocumentLayout::documentSizeChanged,
                this, [this](const QSizeF &) {
            markLayoutDirty();
            refreshNaturalWidth();
            update();
        });
        connect(layout, &QAbstractTextDocumentLayout::update,
                this, [this](const QRectF &) { update(); });
    }
    refreshNaturalWidth();
}
