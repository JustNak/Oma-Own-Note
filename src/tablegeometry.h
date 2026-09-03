#pragma once

#include <QHash>
#include <QRectF>
#include <QString>
#include <QVector>
#include <QFont>

class QTextDocument;
class QTextLayout;

class TableGeometry {
public:
    struct Box {
        QRectF bounds;
        QRectF header;
        QVector<qreal> columns;
        QVector<qreal> rowEdges;
    };

    struct Cell {
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

    struct Table {
        Box box;
        QVector<Cell> cells;
        QVector<int> rowBlockPositions;
        // Block line heights per data row. Unlike rowEdges these exclude the
        // separator line folded into the header's edge.
        QVector<qreal> rowHeights;
    };

    static int layoutRelevantCursor(QTextDocument *document, int cursorPosition);
    static int structureRevision(QTextDocument *document);
    static QVector<Table> geometriesFor(QTextDocument *document, qreal wrapWidth,
                                        int cursorPosition);
    static QVector<Box> collectTables(QTextDocument *document, qreal wrapWidth = 0);
    static QHash<int, qreal> dataRowHeights(QTextDocument *document, qreal wrapWidth,
                                            int cursorPosition = -1);
    static qreal naturalWidthOf(QTextDocument *document);

    static void prepareCellLayout(QTextLayout *layout, const QString &text,
                                  const QFont &font, qreal width);
    static qreal cellLineHeight(const QTextLayout &layout, const QFont &font);
    static qreal cellTextOffset(const QTextLayout &layout, const QFont &font,
                                const QRectF &textRect);
    static int visibleEndForCursor(const QString &line, int contentEnd, int typingStart,
                                   int typingEnd, int blockPosition, int cursorPosition);
};
