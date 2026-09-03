#pragma once

#include <QHash>
#include <QTextBlock>

class QTextCursor;
class QTextDocument;

bool rangeNeedsTableTypography(QTextDocument *document, int position, int added);
bool blockMatchesTableTypography(const QTextBlock &block,
                                 const QHash<int, qreal> &tableRowHeights);
void applyBlockLineHeight(QTextCursor &cursor, const QTextBlock &block,
                          const QHash<int, qreal> &tableRowHeights);
