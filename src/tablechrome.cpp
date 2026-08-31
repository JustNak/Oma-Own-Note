#include "tablechrome.h"

#include "markdownhighlighter.h"

#include <QAbstractTextDocumentLayout>
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

        qreal top = layout->blockBoundingRect(dataRows.first()).top();
        qreal bottom = layout->blockBoundingRect(dataRows.last()).bottom();
        const qreal left = box.columns.first();
        const qreal right = box.columns.last();
        box.bounds = QRectF(QPointF(left, top), QPointF(right, bottom));
        box.rowEdges.append(top);
        for (const QTextBlock &block : dataRows) {
            const QRectF row = layout->blockBoundingRect(block);
            if (block == header)
                box.header = QRectF(QPointF(left, row.top()),
                                    QPointF(right, row.bottom()));
            box.rowEdges.append(row.bottom());
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

static QPoint devicePoint(const QTransform &world, qreal x, qreal y)
{
    const QPointF mapped = world.map(QPointF(x, y));
    return QPoint(qRound(mapped.x()), qRound(mapped.y()));
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

        const QPoint topLeft = devicePoint(world, bounds.left(), bounds.top());
        const QPoint bottomRight = devicePoint(world, bounds.right(), bounds.bottom());
        const int left = topLeft.x();
        const int right = bottomRight.x();
        const int top = topLeft.y();
        const int bottom = bottomRight.y();
        const int height = qMax(1, bottom - top + 1);
        const int width = qMax(1, right - left + 1);

        for (qreal x : box.columns) {
            const int stroke = devicePoint(world, x, bounds.top()).x();
            painter->fillRect(stroke, top, 1, height, rule);
        }
        for (qreal y : box.rowEdges) {
            const int stroke = devicePoint(world, bounds.left(), y).y();
            painter->fillRect(left, stroke, width, 1, rule);
        }
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

void TableChrome::bindDocument(QTextDocument *document) {
    if (m_document) {
        disconnect(m_document, nullptr, this, nullptr);
        if (auto *layout = m_document->documentLayout())
            disconnect(layout, nullptr, this, nullptr);
    }

    m_document = document;
    if (!m_document)
        return;

    connect(m_document, &QTextDocument::contentsChanged, this, [this]() {
        update();
    });
    if (auto *layout = m_document->documentLayout()) {
        connect(layout, &QAbstractTextDocumentLayout::documentSizeChanged,
                this, [this](const QSizeF &) { update(); });
        connect(layout, &QAbstractTextDocumentLayout::update,
                this, [this](const QRectF &) { update(); });
    }
}
