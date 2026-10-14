#include "codeblocks.h"

#include <QRegularExpression>

namespace {

constexpr int kindMask = 0x3;
constexpr int lengthShift = 4;
constexpr int lengthMask = 0xFFF;
constexpr int paragraphBit = 1 << 16;
constexpr int commentBit = 1 << 20;

enum Kind { None = 0, Backtick = 1, Tilde = 2, Indented = 3 };

struct Measured {
    int columns = 0;
    int index = 0;
    bool blank = false;
};

Measured measure(const QString &text) {
    Measured measured;
    while (measured.index < text.size()) {
        const QChar character = text.at(measured.index);
        if (character == QLatin1Char(' ')) {
            ++measured.columns;
            ++measured.index;
            continue;
        }
        if (character == QLatin1Char('\t')) {
            measured.columns += 4 - (measured.columns % 4);
            ++measured.index;
            continue;
        }
        break;
    }
    measured.blank = measured.index >= text.size();
    return measured;
}

int encode(int kind, int length, bool paragraph) {
    return (kind & kindMask) | (qBound(0, length, lengthMask) << lengthShift)
        | (paragraph ? paragraphBit : 0);
}

int kindOf(int state) {
    return state & kindMask;
}

int lengthOf(int state) {
    return (state >> lengthShift) & lengthMask;
}

bool paragraphOf(int state) {
    return (state & paragraphBit) != 0;
}

QChar fenceCharacter(int kind) {
    if (kind == Backtick)
        return QLatin1Char('`');
    if (kind == Tilde)
        return QLatin1Char('~');
    return {};
}

bool onlySpaces(const QString &text) {
    for (const QChar character : text) {
        if (character != QLatin1Char(' ') && character != QLatin1Char('\t'))
            return false;
    }
    return true;
}

bool openingFence(const QString &text, const Measured &measured, QChar *character, int *length,
                  QString *language) {
    if (measured.columns > 3 || measured.blank)
        return false;
    const QChar marker = text.at(measured.index);
    if (marker != QLatin1Char('`') && marker != QLatin1Char('~'))
        return false;
    int run = 0;
    while (measured.index + run < text.size() && text.at(measured.index + run) == marker)
        ++run;
    if (run < 3)
        return false;
    const QString rest = text.mid(measured.index + run);
    // A backtick fence whose info string contains a backtick is not a fence.
    if (marker == QLatin1Char('`') && rest.contains(QLatin1Char('`')))
        return false;
    QString info = rest;
    while (info.startsWith(QLatin1Char(' ')) || info.startsWith(QLatin1Char('\t')))
        info.remove(0, 1);
    while (info.endsWith(QLatin1Char(' ')) || info.endsWith(QLatin1Char('\t')))
        info.chop(1);
    int wordEnd = info.size();
    for (int index = 0; index < info.size(); ++index) {
        const QChar wordCharacter = info.at(index);
        if (wordCharacter == QLatin1Char(' ') || wordCharacter == QLatin1Char('\t')) {
            wordEnd = index;
            break;
        }
    }
    const QString word = info.left(wordEnd);
    static const QRegularExpression languageRe(QStringLiteral("^[A-Za-z0-9_+#.\\-]{1,40}$"));
    *character = marker;
    *length = run;
    *language = languageRe.match(word).hasMatch() ? word : QString();
    return true;
}

bool closingFence(const QString &text, const Measured &measured, QChar expected, int minimum) {
    if (measured.columns > 3 || measured.blank || expected.isNull())
        return false;
    if (text.at(measured.index) != expected)
        return false;
    int run = 0;
    while (measured.index + run < text.size() && text.at(measured.index + run) == expected)
        ++run;
    if (run < qMax(3, minimum))
        return false;
    return onlySpaces(text.mid(measured.index + run));
}

bool indentedContent(const Measured &measured) {
    return !measured.blank && measured.columns >= 4;
}

bool atxHeading(const QString &text) {
    const Measured measured = measure(text);
    if (measured.columns > 3 || measured.blank)
        return false;
    int hashes = 0;
    int index = measured.index;
    while (index < text.size() && text.at(index) == QLatin1Char('#') && hashes < 7) {
        ++hashes;
        ++index;
    }
    if (hashes < 1 || hashes > 6)
        return false;
    return index >= text.size() || text.at(index) == QLatin1Char(' ')
        || text.at(index) == QLatin1Char('\t');
}

bool thematicBreak(const QString &text) {
    const Measured measured = measure(text);
    if (measured.columns > 3 || measured.blank)
        return false;
    const QChar marker = text.at(measured.index);
    if (marker != QLatin1Char('-') && marker != QLatin1Char('*') && marker != QLatin1Char('_'))
        return false;
    int count = 0;
    for (int index = measured.index; index < text.size(); ++index) {
        const QChar character = text.at(index);
        if (character == marker)
            ++count;
        else if (character != QLatin1Char(' ') && character != QLatin1Char('\t'))
            return false;
    }
    return count >= 3;
}

bool followingIndentedChunk(const QTextBlock &block) {
    QTextBlock next = block.next();
    while (next.isValid() && measure(next.text()).blank)
        next = next.next();
    if (!next.isValid())
        return false;
    const Measured measured = measure(next.text());
    if (!indentedContent(measured))
        return false;
    QChar character;
    int length = 0;
    QString language;
    return !openingFence(next.text(), measured, &character, &length, &language);
}

CodeLine finished(bool inCode, bool opener, bool closer, bool indented, const QString &language,
                  int nextState) {
    CodeLine line;
    line.inCode = inCode;
    line.opener = opener;
    line.closer = closer;
    line.indented = indented;
    line.language = language;
    line.nextState = nextState;
    return line;
}

} // namespace

int CodeBlocks::structural(int state) {
    if (state < 0)
        return 0;
    return state & ~commentBit;
}

int CodeBlocks::withComment(int state, bool comment) {
    const int base = structural(state);
    return comment ? (base | commentBit) : base;
}

bool CodeBlocks::commentContinued(int state) {
    return state >= 0 && (state & commentBit) != 0;
}

CodeLine CodeBlocks::classify(const QString &text, int previousState, const QTextBlock &block) {
    previousState = structural(previousState);
    const int kind = kindOf(previousState);
    const int fenceLength = lengthOf(previousState);
    const bool inParagraph = paragraphOf(previousState);
    const Measured measured = measure(text);

    if (kind == Backtick || kind == Tilde) {
        if (closingFence(text, measured, fenceCharacter(kind), fenceLength))
            return finished(true, false, true, false, {}, encode(None, 0, false));
        return finished(true, false, false, false, {}, encode(kind, fenceLength, false));
    }

    QChar character;
    int length = 0;
    QString language;
    if (openingFence(text, measured, &character, &length, &language)) {
        const int opened = character == QLatin1Char('`') ? Backtick : Tilde;
        return finished(true, true, false, false, language, encode(opened, length, false));
    }

    if (measured.blank) {
        const bool keep = kind == Indented && block.isValid() && followingIndentedChunk(block);
        if (keep)
            return finished(true, false, false, true, {}, encode(Indented, 0, false));
        return finished(false, false, false, false, {}, encode(None, 0, false));
    }

    if ((kind == Indented || !inParagraph) && indentedContent(measured))
        return finished(true, false, false, true, {}, encode(Indented, 0, false));

    const bool endsBlock = atxHeading(text) || thematicBreak(text);
    return finished(false, false, false, false, {}, encode(None, 0, !endsBlock));
}
