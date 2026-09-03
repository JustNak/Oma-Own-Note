#include "tablegeometry.h"

#include "markdownhighlighter.h"

#include <QAbstractTextDocumentLayout>
#include <QFontMetricsF>
#include <QHash>
#include <QObject>
#include <QSizeF>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextLayout>
#include <QTextLine>
#include <QTextOption>

#include <limits>

namespace {

qreal effectiveWrapWidth(QTextDocument *document, qreal wrapWidth) {
    if (wrapWidth > 1)
        return wrapWidth;
    if (!document)
        return 10000;
    const qreal textWidth = document->textWidth();
    if (textWidth > 1 && textWidth < 1e6)
        return textWidth;
    return 10000;
}

void typingSpan(const QString &line, const MarkdownHighlighter::Span &cell,
                int *typingStart, int *typingEnd) {
    int left = cell.start;
    const int right = cell.start + cell.length;
    if (left < right && (line.at(left) == QLatin1Char(' ')
                         || line.at(left) == QLatin1Char('\t')))
        ++left;
    *typingStart = left;
    *typingEnd = right;
}

void contentSpan(const QString &line, const MarkdownHighlighter::Span &cell,
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

QString visibleCellText(const QString &line, int contentStart, int contentEnd,
                        int typingStart, int typingEnd,
                        int blockPosition, int cursorPosition) {
    const int visibleEnd = TableGeometry::visibleEndForCursor(
        line, contentEnd, typingStart, typingEnd, blockPosition, cursorPosition);
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

CellSpan cellSpanFor(const QString &line, const MarkdownHighlighter::Span &cell,
                     int blockPosition, int cursorPosition) {
    CellSpan span;
    typingSpan(line, cell, &span.typingStart, &span.typingEnd);
    contentSpan(line, cell, &span.contentStart, &span.contentEnd, &span.text);
    span.text = visibleCellText(line, span.contentStart, span.contentEnd,
                                span.typingStart, span.typingEnd,
                                blockPosition, cursorPosition);
    return span;
}

QVector<qreal> shrinkInners(QVector<qreal> inners, qreal minInner, qreal extra) {
    // Overflow shrinks every currently-widest column together down to the next
    // tier (or minInner). Dumping the whole extra onto one column crushed it to
    // a few characters while a slightly shorter sibling kept the row.
    while (extra > 0.5) {
        qreal widestWidth = minInner;
        for (qreal inner : inners)
            widestWidth = qMax(widestWidth, inner);
        if (widestWidth <= minInner + 0.5)
            break;

        QVector<int> widest;
        qreal nextWidth = minInner;
        for (int i = 0; i < inners.size(); ++i) {
            if (inners.at(i) > widestWidth - 0.5) {
                widest.append(i);
                continue;
            }
            nextWidth = qMax(nextWidth, inners.at(i));
        }
        if (widest.isEmpty())
            break;

        const qreal roomEach = widestWidth - nextWidth;
        if (roomEach <= 0.5)
            break;
        const qreal takeEach = qMin(extra / widest.size(), roomEach);
        for (int index : widest)
            inners[index] -= takeEach;
        extra -= takeEach * widest.size();
    }
    return inners;
}

class GeomStore : public QObject {
public:
    explicit GeomStore(QTextDocument *parent)
        : QObject(parent)
    {
    }

    int revision = -1;
    qreal wrapWidth = -1;
    int layoutCursor = std::numeric_limits<int>::min();
    qreal layoutWidth = -1;
    qreal layoutHeight = -1;
    QVector<TableGeom> tables;
};

GeomStore *storeFor(QTextDocument *document) {
    if (!document)
        return nullptr;
    const QString name = QStringLiteral("oma.TableGeomStore");
    if (QObject *existing = document->findChild<QObject *>(name, Qt::FindDirectChildrenOnly))
        return static_cast<GeomStore *>(existing);
    auto *store = new GeomStore(document);
    store->setObjectName(name);
    return store;
}

QVector<TableGeom> buildGeometries(QTextDocument *document, qreal wrapWidth,
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

        QVector<QVector<CellSpan>> rowSpans;
        rowSpans.resize(dataRows.size());
        QVector<qreal> inners(columns, minInner);
        for (int r = 0; r < dataRows.size(); ++r) {
            const QFont &rowFont = (header.isValid()
                                    && dataRows.at(r).position() == header.position())
                ? headerFont : font;
            const QFontMetricsF rowMetrics(rowFont);
            const QVector<MarkdownHighlighter::Span> &cells = rowCells.at(r);
            rowSpans[r].resize(cells.size());
            for (int c = 0; c < columns; ++c) {
                QString text;
                if (c < cells.size()) {
                    rowSpans[r][c] = cellSpanFor(rowTexts.at(r), cells.at(c),
                                                 dataRows.at(r).position(),
                                                 cursorPosition);
                    text = rowSpans[r].at(c).text;
                }
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
            const QVector<CellSpan> &spans = rowSpans.at(r);
            for (int c = 0; c < columns; ++c) {
                const QString text = c < spans.size() ? spans.at(c).text : QString();
                const qreal textWidth = qMax(qreal(1), inners.at(c));
                QTextLayout cellLayout;
                TableGeometry::prepareCellLayout(&cellLayout, text, rowFont, textWidth);
                if (!text.isEmpty())
                    rowHeight = qMax(rowHeight, cellLayout.boundingRect().height());
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
                TableCellGeom cell;
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
                    const CellSpan &span = rowSpans.at(r).at(c);
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

} // namespace

int TableGeometry::visibleEndForCursor(const QString &line, int contentEnd, int typingStart,
                                       int typingEnd, int blockPosition, int cursorPosition) {
    // Pretty-print padding stays hidden, but spaces at or before the caret are
    // real cell text so a typed trailing space stays visible and the overlay
    // caret can sit in it. A caret on a closing `|` must not unhide unused
    // padding. A last cell with no closing pipe uses typingEnd at the end of
    // the line; a typed space there is real content.
    if (cursorPosition < 0)
        return contentEnd;
    const int absStart = blockPosition + typingStart;
    const int absEnd = blockPosition + typingEnd;
    if (cursorPosition < absStart || cursorPosition > absEnd)
        return contentEnd;
    const bool onClosingPipe = cursorPosition == absEnd
            && typingEnd < line.size()
            && line.at(typingEnd) == QLatin1Char('|');
    if (onClosingPipe)
        return contentEnd;
    const int cursorInLine = cursorPosition - blockPosition;
    return qBound(typingStart, qMax(contentEnd, cursorInLine), typingEnd);
}

void TableGeometry::prepareCellLayout(QTextLayout *layout, const QString &text,
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

qreal TableGeometry::cellLineHeight(const QTextLayout &layout, const QFont &font) {
    // An empty cell lays out no lines, so its bounding rect is zero-height.
    // Treat it as a single line of the cell font so the caret and text origin
    // are centred like a one-line cell instead of collapsing to the row's midpoint.
    if (layout.lineCount() > 0)
        return layout.boundingRect().height();
    return QFontMetricsF(font).height();
}

qreal TableGeometry::cellTextOffset(const QTextLayout &layout, const QFont &font,
                                    const QRectF &textRect) {
    return qMax(qreal(0), (textRect.height() - cellLineHeight(layout, font)) * 0.5);
}

int TableGeometry::layoutRelevantCursor(QTextDocument *document, int cursorPosition) {
    if (!document || cursorPosition < 0)
        return -1;
    const QTextBlock block = document->findBlock(cursorPosition);
    if (!block.isValid()
            || !MarkdownHighlighter::isTableRow(block.text())
            || MarkdownHighlighter::isTableSeparator(block.text())
            || block.userState() == 1)
        return -1;
    const QString line = block.text();
    const MarkdownHighlighter::TableLine parsed = MarkdownHighlighter::parseTableLine(line);
    const int blockPosition = block.position();
    for (const MarkdownHighlighter::Span &cell : parsed.cells) {
        int typingStart = 0;
        int typingEnd = 0;
        int contentStart = 0;
        int contentEnd = 0;
        QString text;
        typingSpan(line, cell, &typingStart, &typingEnd);
        contentSpan(line, cell, &contentStart, &contentEnd, &text);
        const int visibleEnd = visibleEndForCursor(line, contentEnd, typingStart, typingEnd,
                                                   blockPosition, cursorPosition);
        if (visibleEnd > contentEnd)
            return cursorPosition;
    }
    return -1;
}

QVector<TableGeom> TableGeometry::geometriesFor(QTextDocument *document, qreal wrapWidth,
                                                int cursorPosition) {
    const int layoutCursor = layoutRelevantCursor(document, cursorPosition);
    GeomStore *store = storeFor(document);
    const int revision = document ? document->revision() : -1;
    const qreal cap = effectiveWrapWidth(document, wrapWidth);
    qreal layoutWidth = 0;
    qreal layoutHeight = 0;
    if (document) {
        if (QAbstractTextDocumentLayout *layout = document->documentLayout()) {
            const QSizeF size = layout->documentSize();
            layoutWidth = size.width();
            layoutHeight = size.height();
        }
    }

    if (store
            && store->revision == revision
            && store->layoutCursor == layoutCursor
            && qAbs(store->wrapWidth - cap) < 0.5
            && qAbs(store->layoutWidth - layoutWidth) < 0.5
            && qAbs(store->layoutHeight - layoutHeight) < 0.5)
        return store->tables;

    QVector<TableGeom> tables = buildGeometries(document, wrapWidth, layoutCursor);
    if (!store)
        return tables;
    store->revision = revision;
    store->wrapWidth = cap;
    store->layoutCursor = layoutCursor;
    store->layoutWidth = layoutWidth;
    store->layoutHeight = layoutHeight;
    store->tables = tables;
    return tables;
}

QVector<TableBox> TableGeometry::collectTables(QTextDocument *document, qreal wrapWidth) {
    QVector<TableBox> tables;
    const QVector<TableGeom> geoms = geometriesFor(document, wrapWidth, -1);
    tables.reserve(geoms.size());
    for (const TableGeom &geom : geoms)
        tables.append(geom.box);
    return tables;
}

QHash<int, qreal> TableGeometry::dataRowHeights(QTextDocument *document, qreal wrapWidth,
                                                int cursorPosition) {
    QHash<int, qreal> heights;
    const QVector<TableGeom> geoms = geometriesFor(document, wrapWidth, cursorPosition);
    for (const TableGeom &geom : geoms) {
        for (int i = 0; i < geom.rowBlockPositions.size(); ++i)
            heights.insert(geom.rowBlockPositions.at(i), geom.rowHeights.at(i));
    }
    return heights;
}

qreal TableGeometry::naturalWidthOf(QTextDocument *document) {
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
