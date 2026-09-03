#include "tabletypography.h"

#include "markdownhighlighter.h"

#include <QFont>
#include <QTextBlockFormat>
#include <QTextCursor>
#include <QTextDocument>

namespace {
constexpr qreal typoraLineHeightPercent = 140;

QTextBlockFormat desiredBlockTypography(const QTextBlock &block,
                                        const QHash<int, qreal> &tableRowHeights) {
    QTextBlockFormat format = block.blockFormat();
    format.setTopMargin(0);
    format.setBottomMargin(0);
    const QTextDocument *document = block.document();
    const QFont font = document ? document->defaultFont() : QFont();
    if (MarkdownHighlighter::isTableSeparator(block.text())) {
        format.setLineHeight(MarkdownHighlighter::tableSeparatorLineHeight,
                             QTextBlockFormat::FixedHeight);
        format.setNonBreakableLines(true);
    } else if (MarkdownHighlighter::isTableRow(block.text())) {
        const qreal height = tableRowHeights.value(
            block.position(), MarkdownHighlighter::tableDataRowLineHeight(font));
        format.setLineHeight(height, QTextBlockFormat::MinimumHeight);
        format.setNonBreakableLines(true);
    } else {
        format.setLineHeight(typoraLineHeightPercent, QTextBlockFormat::ProportionalHeight);
        format.setNonBreakableLines(false);
    }
    return format;
}
}

bool rangeNeedsTableTypography(QTextDocument *document, int position, int added)
{
    if (!document)
        return false;

    const int maxPos = qMax(0, document->characterCount() - 1);
    const int start = qBound(0, position, maxPos);
    const int end = qBound(start, position + added, maxPos);
    QTextBlock block = document->findBlock(start);
    if (block.isValid() && block.previous().isValid())
        block = block.previous();
    const QTextBlock last = document->findBlock(end);
    const QTextBlock stop = last.isValid() && last.next().isValid() ? last.next().next()
                                                                   : QTextBlock();
    for (; block.isValid() && block != stop; block = block.next()) {
        if (MarkdownHighlighter::isTableRow(block.text())
                || block.blockFormat().nonBreakableLines())
            return true;
    }
    return false;
}

bool blockMatchesTableTypography(const QTextBlock &block,
                                 const QHash<int, qreal> &tableRowHeights) {
    const QTextBlockFormat desired = desiredBlockTypography(block, tableRowHeights);
    const QTextBlockFormat format = block.blockFormat();
    return format.lineHeight() == desired.lineHeight()
            && format.lineHeightType() == desired.lineHeightType()
            && format.nonBreakableLines() == desired.nonBreakableLines();
}

void applyBlockLineHeight(QTextCursor &cursor, const QTextBlock &block,
                          const QHash<int, qreal> &tableRowHeights) {
    const QTextBlockFormat desired = desiredBlockTypography(block, tableRowHeights);
    const QTextBlockFormat format = block.blockFormat();
    if (format.lineHeight() == desired.lineHeight()
            && format.lineHeightType() == desired.lineHeightType()
            && format.nonBreakableLines() == desired.nonBreakableLines())
        return;
    cursor.setPosition(block.position());
    cursor.setBlockFormat(desired);
}
