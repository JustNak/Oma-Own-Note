#pragma once

#include <QChar>
#include <QString>
#include <QTextBlock>
#include <QTextBlockUserData>
#include <QVector>

// One open list item. markerColumn is where the marker sits; contentColumn is
// the column later lines must reach to stay inside the item. Indented code is
// four columns past contentColumn, not past column 0.
struct CodeContainer {
    int markerColumn = 0;
    int contentColumn = 0;
    QChar style;

    bool operator==(const CodeContainer &other) const
    {
        return markerColumn == other.markerColumn && contentColumn == other.contentColumn
            && style == other.style;
    }
};

// One CommonMark code-block classification shared by the highlighter, the
// overlays, and the block metrics. Callers read the result; they do not
// reimplement fence or indent rules.
//
// carryKind, carryLength, carryParagraph, language, and containers are what
// the next line inherits. nextState hashes them so a language or container
// change rehighlights the following blocks. The hash is not decoded.
struct CodeLine {
    bool inCode = false;
    bool opener = false;
    bool closer = false;
    bool indented = false;
    QString language;
    int nextState = 0;
    QVector<CodeContainer> containers;
    int carryKind = 0;
    int carryLength = 0;
    bool carryParagraph = false;
};

class CodeBlockData : public QTextBlockUserData {
public:
    CodeLine line;
};

namespace CodeBlocks {

CodeLine classify(const QString &text, int previousState, const QTextBlock &block);
int structural(int state);
int withComment(int state, bool comment);
bool commentContinued(int state);

}
