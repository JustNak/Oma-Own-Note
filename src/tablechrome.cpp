#include "tablechrome.h"

#include "markdownhighlighter.h"

#include <QAbstractTextDocumentLayout>
#include <QFontMetricsF>
#include <QPainter>
#include <QQuickTextDocument>
#include <QSize>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextLine>
#include <QTransform>
#include <QtQml>

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

static QVector<qreal> pipeCenters(const QTextBlock &block) {
    QVector<qreal> xs;
    if (!block.isValid() || !block.document() || !block.layout()
            || block.layout()->lineCount() <= 0)
        return xs;

    QAbstractTextDocumentLayout *layout = block.document()->documentLayout();
    if (!layout)
        return xs;

    const QRectF box = layout->blockBoundingRect(block);
    const QTextLine line = block.layout()->lineAt(0);
    const QString text = block.text();
    for (int i = 0; i < text.length(); ++i) {
        if (text.at(i) != QLatin1Char('|'))
            continue;
        const qreal left = line.cursorToX(i);
        const qreal right = line.cursorToX(i + 1);
        xs.append(box.x() + line.x() + (left + right) * 0.5);
    }
    return xs;
}

QVector<TableChrome::TableBox> TableChrome::collectTables(QTextDocument *document) {
    QVector<TableBox> tables;
    if (!document)
        return tables;

    QAbstractTextDocumentLayout *layout = document->documentLayout();
    if (!layout)
        return tables;

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

        TableBox box;
        const QTextBlock columnSource = header.isValid() ? header : dataRows.first();
        box.columns = pipeCenters(columnSource);
        if (box.columns.size() < 2) {
            run.clear();
            return;
        }

        const QRectF firstRow = layout->blockBoundingRect(dataRows.first());
        const QRectF lastRow = layout->blockBoundingRect(dataRows.last());
        const qreal top = firstRow.top();
        const qreal left = box.columns.first();
        const qreal right = box.columns.last();

        QTextBlock after = run.last().next();
        qreal tableBottom = lastRow.bottom();
        if (after.isValid())
            tableBottom = layout->blockBoundingRect(after).top();
        const qreal minBottom = top
            + MarkdownHighlighter::tableDataRowLineHeight(document->defaultFont())
                * dataRows.size();
        if (tableBottom < minBottom)
            tableBottom = minBottom;

        const qreal step = (tableBottom - top) / dataRows.size();
        box.bounds = QRectF(QPointF(left, top), QPointF(right, tableBottom));
        box.rowEdges.append(top);
        for (int i = 0; i < dataRows.size(); ++i) {
            const qreal edge = top + step * (i + 1);
            box.rowEdges.append(edge);
            if (header.isValid() && dataRows.at(i).position() == header.position())
                box.header = QRectF(QPointF(left, top + step * i),
                                    QPointF(right, edge));
        }
        tables.append(box);
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

static QPoint devicePoint(const QTransform &world, qreal x, qreal y)
{
    const QPointF mapped = world.map(QPointF(x, y));
    return QPoint(qRound(mapped.x()), qRound(mapped.y()));
}

static QVector<int> snapRowStrokes(const QTransform &world, qreal x,
                                   const QVector<qreal> &edges)
{
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

void TableChrome::paintTables(QPainter *painter, QTextDocument *document,
                              const QColor &, const QColor &,
                              const QColor &rule) {
    if (!painter || !document)
        return;

    const QVector<TableBox> tables = collectTables(document);
    if (tables.isEmpty())
        return;

    // Cosmetic 1px pens miss at fractional canvas zoom. Map through the
    // current transform, then fill whole device pixels with it turned off.
    const QTransform world = painter->combinedTransform();

    painter->save();
    painter->resetTransform();
    painter->setRenderHint(QPainter::Antialiasing, false);

    for (const TableBox &box : tables) {
        const QRectF bounds = box.bounds.normalized();
        if (bounds.width() < 2 || bounds.height() < 2)
            continue;

        const QVector<int> rowStrokes = snapRowStrokes(world, bounds.left(), box.rowEdges);
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

        for (qreal x : box.columns) {
            const int stroke = devicePoint(world, x, bounds.top()).x();
            painter->fillRect(stroke, top, 1, height, rule);
        }
        for (int stroke : rowStrokes)
            painter->fillRect(left, stroke, width, 1, rule);
    }

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

void TableChrome::setViewScale(qreal viewScale) {
    const qreal scale = qMax(viewScale, qreal(0.01));
    if (qFuzzyCompare(m_viewScale, scale))
        return;
    m_viewScale = scale;
    emit viewScaleChanged();
    syncTextureSize();
    update();
}

void TableChrome::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) {
    QQuickPaintedItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.size() != oldGeometry.size())
        syncTextureSize();
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
    // QQuickPaintedItem already scales the painter to textureSize. Apply
    // viewScale only when that transform is still identity.
    const qreal mapped = qAbs(painter->transform().m11());
    if (mapped > 0.98 && mapped < 1.02 && !qFuzzyCompare(m_viewScale, qreal(1))) {
        painter->save();
        painter->scale(m_viewScale, m_viewScale);
        paintTables(painter, m_document, m_paper, m_textColor, m_ruleColor);
        painter->restore();
        return;
    }
    paintTables(painter, m_document, m_paper, m_textColor, m_ruleColor);
}

void TableChrome::refreshNaturalWidth() {
    const qreal nextWidth = naturalWidthOf(m_document);
    if (qAbs(m_naturalWidth - nextWidth) < qreal(0.5))
        return;
    m_naturalWidth = nextWidth;
    emit naturalWidthChanged();
}

void TableChrome::bindDocument(QTextDocument *document) {
    if (m_document) {
        disconnect(m_document, nullptr, this, nullptr);
        if (auto *layout = m_document->documentLayout())
            disconnect(layout, nullptr, this, nullptr);
    }

    m_document = document;
    if (!m_document) {
        if (!qFuzzyIsNull(m_naturalWidth)) {
            m_naturalWidth = 0;
            emit naturalWidthChanged();
        }
        return;
    }

    connect(m_document, &QTextDocument::contentsChanged, this, [this]() {
        refreshNaturalWidth();
        update();
    });
    if (auto *layout = m_document->documentLayout()) {
        connect(layout, &QAbstractTextDocumentLayout::documentSizeChanged,
                this, [this](const QSizeF &) {
            refreshNaturalWidth();
            update();
        });
        connect(layout, &QAbstractTextDocumentLayout::update,
                this, [this](const QRectF &) { update(); });
    }
    refreshNaturalWidth();
}
