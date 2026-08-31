#include "tablechrome.h"

#include "markdownhighlighter.h"

#include <QAbstractTextDocumentLayout>
#include <QPainter>
#include <QPen>
#include <QQuickTextDocument>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextLine>
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

void TableChrome::paintTables(QPainter *painter, QTextDocument *document,
                              const QColor &, const QColor &,
                              const QColor &rule) {
    if (!painter || !document)
        return;

    const QVector<TableBox> tables = collectTables(document);
    if (tables.isEmpty())
        return;

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, false);

    for (const TableBox &box : tables) {
        const QRectF bounds = box.bounds.normalized();
        if (bounds.width() < 2 || bounds.height() < 2)
            continue;

        QPen pen(rule, 1);
        pen.setCosmetic(true);
        painter->setPen(pen);

        for (qreal x : box.columns) {
            const int stroke = qRound(x);
            painter->drawLine(QPoint(stroke, qRound(bounds.top())),
                              QPoint(stroke, qRound(bounds.bottom())));
        }
        for (qreal y : box.rowEdges) {
            const int stroke = qRound(y);
            painter->drawLine(QPoint(qRound(bounds.left()), stroke),
                              QPoint(qRound(bounds.right()), stroke));
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

void TableChrome::paint(QPainter *painter) {
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
