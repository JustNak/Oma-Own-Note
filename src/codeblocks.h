#pragma once

#include <QString>
#include <QTextBlock>
#include <QTextBlockUserData>

// One CommonMark code-block classification shared by the highlighter, the
// overlays, and the block metrics. Callers read the result; they do not
// reimplement fence or indent rules.
struct CodeLine {
    bool inCode = false;
    bool opener = false;
    bool closer = false;
    bool indented = false;
    QString language;
    int nextState = 0;
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
