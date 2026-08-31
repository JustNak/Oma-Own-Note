#pragma once

#include <QColor>
#include <QObject>
#include <QPointer>
#include <QQuickPaintedItem>
#include <QRectF>
#include <QVector>

class QPainter;
class QTextDocument;

class TableChrome : public QQuickPaintedItem {
    Q_OBJECT
    Q_PROPERTY(QObject *textDocument READ textDocument WRITE setTextDocument
               NOTIFY textDocumentChanged)
    Q_PROPERTY(QColor paper READ paper WRITE setPaper NOTIFY paperChanged)
    Q_PROPERTY(QColor textColor READ textColor WRITE setTextColor NOTIFY textColorChanged)
    Q_PROPERTY(QColor ruleColor READ ruleColor WRITE setRuleColor NOTIFY ruleColorChanged)

public:
    struct TableBox {
        QRectF bounds;
        QRectF header;
        QVector<qreal> columns;
        QVector<qreal> rowEdges;
    };

    explicit TableChrome(QQuickItem *parent = nullptr);

    static void registerQmlType();
    static QColor mixToward(const QColor &from, const QColor &to, qreal amount);
    static QColor headerFill(const QColor &paper, const QColor &text);
    static QColor bodyFill(const QColor &paper, const QColor &text);
    static QVector<TableBox> collectTables(QTextDocument *document);
    static void paintTables(QPainter *painter, QTextDocument *document,
                            const QColor &paper, const QColor &text,
                            const QColor &rule);

    QObject *textDocument() const { return m_textDocumentObject; }
    void setTextDocument(QObject *textDocument);

    QColor paper() const { return m_paper; }
    void setPaper(const QColor &paper);

    QColor textColor() const { return m_textColor; }
    void setTextColor(const QColor &textColor);

    QColor ruleColor() const { return m_ruleColor; }
    void setRuleColor(const QColor &ruleColor);

    void paint(QPainter *painter) override;

signals:
    void textDocumentChanged();
    void paperChanged();
    void textColorChanged();
    void ruleColorChanged();

private:
    void bindDocument(QTextDocument *document);

    QPointer<QObject> m_textDocumentObject;
    QPointer<QTextDocument> m_document;
    QColor m_paper = QColor(QStringLiteral("#101010"));
    QColor m_textColor = QColor(QStringLiteral("#eeeeee"));
    QColor m_ruleColor = QColor(QStringLiteral("#4f525a"));
};
