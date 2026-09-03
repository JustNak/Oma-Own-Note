#include "tablechrome.h"

#include "markdownhighlighter.h"

#include <QAbstractTextDocumentLayout>
#include <QList>
#include <QPainter>
#include <QQuickTextDocument>
#include <QSize>
#include <QSizeF>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextLayout>
#include <QTextCharFormat>
#include <QTextLine>
#include <QTransform>
#include <QtQml>

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

void TableChrome::paintCellText(QPainter *painter, const TableGeometry::Table &geom,
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

    for (const TableGeometry::Cell &cell : geom.cells) {
        if (cell.text.isEmpty() && selFrom == selTo)
            continue;

        const QFont &cellFont = cell.header ? headerFont : font;
        QTextLayout cellLayout;
        TableGeometry::prepareCellLayout(&cellLayout, cell.text, cellFont, cell.textRect.width());
        const qreal yOff = TableGeometry::cellTextOffset(cellLayout, cellFont, cell.textRect);
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
    paintGeometries(painter, TableGeometry::geometriesFor(document, wrapWidth, cursorPosition),
                    document, text, rule, selectionStart, selectionEnd,
                    selectionColor);
}

void TableChrome::paintGeometries(QPainter *painter, const QVector<TableGeometry::Table> &geoms,
                                  QTextDocument *document, const QColor &text,
                                  const QColor &rule, int selectionStart,
                                  int selectionEnd, const QColor &selectionColor) {
    if (!painter || geoms.isEmpty())
        return;

    const QTransform world = painter->combinedTransform();
    const QFont font = document ? document->defaultFont() : QFont();
    QString searchQuery;
    int currentMatchStart = -1;
    QColor searchColor;
    QColor currentSearchColor;
    if (document) {
        if (auto *highlighter = document->findChild<MarkdownHighlighter *>()) {
            searchQuery = highlighter->searchQuery();
            currentMatchStart = highlighter->currentMatchStart();
            searchColor = highlighter->searchBackground();
            currentSearchColor = highlighter->currentSearchBackground();
        }
    }

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, false);

    for (const TableGeometry::Table &geom : geoms) {
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

    for (const TableGeometry::Table &geom : geoms) {
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
    refreshTables();
    emit wrapWidthChanged();
    update();
}

void TableChrome::setCursorPosition(int cursorPosition) {
    if (m_cursorPosition == cursorPosition)
        return;
    m_cursorPosition = cursorPosition;
    const int relevant = TableGeometry::layoutRelevantCursor(m_document, m_cursorPosition);
    emit cursorPositionChanged();
    if (relevant == m_layoutCursor)
        return;
    m_layoutCursor = relevant;
    refreshTables();
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
    const bool scaleHairlines = mapped > 0.98 && mapped < 1.02
            && !qFuzzyCompare(m_viewScale, qreal(1));
    if (scaleHairlines) {
        painter->save();
        painter->scale(m_viewScale, m_viewScale);
    }
    paintGeometries(painter, m_tables, m_document, m_textColor, m_ruleColor,
                    m_selectionStart, m_selectionEnd, m_selectionColor);
    if (scaleHairlines)
        painter->restore();
}

void TableChrome::refreshNaturalWidth() {
    const qreal nextWidth = TableGeometry::naturalWidthOf(m_document);
    if (qAbs(m_naturalWidth - nextWidth) < qreal(0.5))
        return;
    m_naturalWidth = nextWidth;
    emit naturalWidthChanged();
}

void TableChrome::markLayoutDirty() {
    m_layoutDirty = true;
}

void TableChrome::refreshTables() {
    m_tables = TableGeometry::geometriesFor(m_document, m_wrapWidth, m_cursorPosition);
    m_layoutDirty = false;
    const int sr = TableGeometry::structureRevision(m_document);
    if (sr == m_lastStructureRevision)
        return;
    m_lastStructureRevision = sr;
    ++m_layoutRevision;
    emit layoutChanged();
}

void TableChrome::ensureLayout() const {
    if (!m_layoutDirty)
        return;
    m_tables = TableGeometry::geometriesFor(m_document, m_wrapWidth, m_cursorPosition);
    m_layoutDirty = false;
}

const TableGeometry::Cell *TableChrome::cellAtPosition(int position) const {
    ensureLayout();
    const TableGeometry::Cell *best = nullptr;
    int bestDistance = std::numeric_limits<int>::max();
    for (const TableGeometry::Table &table : m_tables) {
        for (const TableGeometry::Cell &cell : table.cells) {
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
    for (const TableGeometry::Table &table : m_tables) {
        for (const TableGeometry::Cell &cell : table.cells) {
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
    const TableGeometry::Table *hitTable = nullptr;
    int hitRow = -1;
    for (const TableGeometry::Table &table : m_tables) {
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

    const TableGeometry::Cell *cell = nullptr;
    for (const TableGeometry::Cell &candidate : hitTable->cells) {
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
    TableGeometry::prepareCellLayout(&layout, cell->text, font, cell->textRect.width());
    const qreal yOff = TableGeometry::cellTextOffset(layout, font, cell->textRect);
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
    const TableGeometry::Cell *cell = cellAtPosition(position);
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
            const QString line = block.text();
            const int visEnd = TableGeometry::visibleEndForCursor(
                line, cell->contentEnd, cell->typingStart,
                cell->typingEnd, cell->blockPosition, position);
            text = line.mid(cell->contentStart, visEnd - cell->contentStart);
            offset = qBound(0, position - start, text.size());
        }
    }

    QTextLayout layout;
    TableGeometry::prepareCellLayout(&layout, text, font, cell->textRect.width());
    const qreal yOff = TableGeometry::cellTextOffset(layout, font, cell->textRect);

    qreal caretX = cell->textRect.left();
    qreal caretY = cell->textRect.top() + yOff;
    qreal caretH = TableGeometry::cellLineHeight(layout, font);
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
    if (!m_document)
        return false;
    const QTextBlock block = m_document->findBlock(position);
    if (!block.isValid()
            || position < block.position()
            || position >= block.position() + block.length()
            || block.userState() == 1)
        return false;
    return MarkdownHighlighter::isTableRow(block.text())
            && !MarkdownHighlighter::isTableSeparator(block.text());
}

int TableChrome::movePositionVertically(int position, int direction) const {
    const TableGeometry::Cell *cellPtr = cellAtPosition(position);
    if (!cellPtr)
        return -1;
    const TableGeometry::Cell cell = *cellPtr;

    QFont font = m_document ? m_document->defaultFont() : QFont();
    if (cell.header)
        font.setBold(true);
    QTextLayout layout;
    TableGeometry::prepareCellLayout(&layout, cell.text, font, cell.textRect.width());
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
    for (const TableGeometry::Table &table : m_tables) {
        bool owns = false;
        for (const TableGeometry::Cell &candidate : table.cells) {
            if (candidate.blockPosition == cell.blockPosition
                    && candidate.column == cell.column) {
                owns = true;
                break;
            }
        }
        if (!owns)
            continue;
        for (const TableGeometry::Cell &candidate : table.cells) {
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
    m_lastDocumentWidth = -1;
    m_lastDocumentHeight = -1;
    m_layoutCursor = std::numeric_limits<int>::min();
    m_lastStructureRevision = 0;
    if (!m_document) {
        if (!qFuzzyIsNull(m_naturalWidth)) {
            m_naturalWidth = 0;
            emit naturalWidthChanged();
        }
        m_tables.clear();
        return;
    }

    connect(m_document, &QTextDocument::contentsChange, this,
            [this](int, int removed, int added) {
        // Format-only restretch and highlighter updates do not change cell
        // source. Row Y shifts arrive through documentSizeChanged.
        if (removed == 0 && added == 0)
            return;
        refreshTables();
        refreshNaturalWidth();
        update();
    });
    if (auto *layout = m_document->documentLayout()) {
        connect(layout, &QAbstractTextDocumentLayout::documentSizeChanged,
                this, [this](const QSizeF &size) {
            if (qAbs(size.width() - m_lastDocumentWidth) < 0.5
                    && qAbs(size.height() - m_lastDocumentHeight) < 0.5)
                return;
            m_lastDocumentWidth = size.width();
            m_lastDocumentHeight = size.height();
            refreshTables();
            refreshNaturalWidth();
            update();
        });
        connect(layout, &QAbstractTextDocumentLayout::update,
                this, [this](const QRectF &) { update(); });
        const QSizeF size = layout->documentSize();
        m_lastDocumentWidth = size.width();
        m_lastDocumentHeight = size.height();
    }
    refreshTables();
    refreshNaturalWidth();
}
