#pragma once

#include <QColor>
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QQuickPaintedItem>
#include <QRectF>
#include <QString>
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
    Q_PROPERTY(QColor selectionColor READ selectionColor WRITE setSelectionColor
               NOTIFY selectionColorChanged)
    Q_PROPERTY(qreal viewScale READ viewScale WRITE setViewScale NOTIFY viewScaleChanged)
    Q_PROPERTY(qreal wrapWidth READ wrapWidth WRITE setWrapWidth NOTIFY wrapWidthChanged)
    Q_PROPERTY(qreal naturalWidth READ naturalWidth NOTIFY naturalWidthChanged)
    Q_PROPERTY(int cursorPosition READ cursorPosition WRITE setCursorPosition
               NOTIFY cursorPositionChanged)
    Q_PROPERTY(int selectionStart READ selectionStart WRITE setSelectionStart
               NOTIFY selectionStartChanged)
    Q_PROPERTY(int selectionEnd READ selectionEnd WRITE setSelectionEnd
               NOTIFY selectionEndChanged)
    Q_PROPERTY(int layoutRevision READ layoutRevision NOTIFY layoutChanged)

public:
    struct TableBox {
        QRectF bounds;
        QRectF header;
        QVector<qreal> columns;
        QVector<qreal> rowEdges;
    };

    explicit TableChrome(QQuickItem *parent = nullptr);

    static void registerQmlType();
    static QVector<TableBox> collectTables(QTextDocument *document);
    static QVector<TableBox> collectTables(QTextDocument *document, qreal wrapWidth);
    static void paintTables(QPainter *painter, QTextDocument *document,
                            const QColor &paper, const QColor &text,
                            const QColor &rule);
    static void paintTables(QPainter *painter, QTextDocument *document,
                            const QColor &paper, const QColor &text,
                            const QColor &rule, qreal wrapWidth,
                            int selectionStart = 0, int selectionEnd = 0,
                            const QColor &selectionColor = QColor(),
                            int cursorPosition = -1);
    static qreal naturalWidthOf(QTextDocument *document);
    static QHash<int, qreal> dataRowHeights(QTextDocument *document, qreal wrapWidth,
                                            int cursorPosition = -1);

    QObject *textDocument() const { return m_textDocumentObject; }
    void setTextDocument(QObject *textDocument);

    QColor paper() const { return m_paper; }
    void setPaper(const QColor &paper);

    QColor textColor() const { return m_textColor; }
    void setTextColor(const QColor &textColor);

    QColor ruleColor() const { return m_ruleColor; }
    void setRuleColor(const QColor &ruleColor);

    QColor selectionColor() const { return m_selectionColor; }
    void setSelectionColor(const QColor &selectionColor);

    qreal viewScale() const { return m_viewScale; }
    void setViewScale(qreal viewScale);

    qreal wrapWidth() const { return m_wrapWidth; }
    void setWrapWidth(qreal wrapWidth);

    qreal naturalWidth() const { return m_naturalWidth; }

    int cursorPosition() const { return m_cursorPosition; }
    void setCursorPosition(int cursorPosition);

    int selectionStart() const { return m_selectionStart; }
    void setSelectionStart(int selectionStart);

    int selectionEnd() const { return m_selectionEnd; }
    void setSelectionEnd(int selectionEnd);

    int layoutRevision() const { return m_layoutRevision; }

    Q_INVOKABLE int hitTest(qreal x, qreal y) const;
    Q_INVOKABLE QRectF caretRect(int position) const;
    Q_INVOKABLE bool positionInTable(int position) const;
    Q_INVOKABLE int movePositionVertically(int position, int direction) const;

    void paint(QPainter *painter) override;

signals:
    void textDocumentChanged();
    void paperChanged();
    void textColorChanged();
    void ruleColorChanged();
    void selectionColorChanged();
    void viewScaleChanged();
    void wrapWidthChanged();
    void naturalWidthChanged();
    void cursorPositionChanged();
    void selectionStartChanged();
    void selectionEndChanged();
    void layoutChanged();

protected:
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;

private:
    struct CellGeom {
        int row = 0;
        int column = 0;
        int blockPosition = 0;
        int contentStart = 0;
        int contentEnd = 0;
        int typingStart = 0;
        int typingEnd = 0;
        QRectF rect;
        QRectF textRect;
        QString text;
        bool header = false;
    };

    struct TableGeom {
        TableBox box;
        QVector<CellGeom> cells;
        QVector<int> rowBlockPositions;
        // Block line heights per data row. Unlike rowEdges these exclude the
        // separator line folded into the header's edge.
        QVector<qreal> rowHeights;
    };

    void bindDocument(QTextDocument *document);
    void syncTextureSize();
    void refreshNaturalWidth();
    void markLayoutDirty();
    void ensureLayout() const;
    const CellGeom *cellAtPosition(int position) const;
    static QVector<TableGeom> buildGeometries(QTextDocument *document, qreal wrapWidth,
                                              int cursorPosition = -1);
    static void paintCellText(QPainter *painter, const TableGeom &geom,
                              const QFont &font, const QColor &textColor,
                              const QColor &selectionColor,
                              int selectionStart, int selectionEnd,
                              const QString &searchQuery = QString(),
                              int currentMatchStart = -1,
                              const QColor &searchColor = QColor(),
                              const QColor &currentSearchColor = QColor());

    QPointer<QObject> m_textDocumentObject;
    QPointer<QTextDocument> m_document;
    QColor m_paper = QColor(QStringLiteral("#101010"));
    QColor m_textColor = QColor(QStringLiteral("#eeeeee"));
    QColor m_ruleColor = QColor(QStringLiteral("#eeeeee"));
    QColor m_selectionColor = QColor(QStringLiteral("#445566"));
    qreal m_viewScale = 1;
    qreal m_wrapWidth = 0;
    qreal m_naturalWidth = 0;
    int m_cursorPosition = 0;
    int m_selectionStart = 0;
    int m_selectionEnd = 0;
    int m_layoutRevision = 0;
    mutable bool m_layoutDirty = true;
    mutable QVector<TableGeom> m_tables;
};
