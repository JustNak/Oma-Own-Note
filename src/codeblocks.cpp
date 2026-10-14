#include "codeblocks.h"

#include <QRegularExpression>

namespace {

constexpr int commentBit = 1 << 20;

enum Kind { None = 0, Backtick = 1, Tilde = 2, Indented = 3 };

struct Measured {
    int columns = 0;
    int index = 0;
    bool blank = false;
};

struct Marker {
    bool matched = false;
    bool bullet = false;
    int number = 0;
    int contentColumn = 0;
    bool empty = false;
    QChar style;
};

struct Carry {
    int kind = None;
    int length = 0;
    bool paragraph = false;
    QString language;
    QVector<CodeContainer> containers;
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

int baselineOf(const QVector<CodeContainer> &containers) {
    return containers.isEmpty() ? 0 : containers.last().contentColumn;
}

int landingBaseline(const Measured &measured, const QVector<CodeContainer> &containers) {
    int baseline = 0;
    for (const CodeContainer &container : containers) {
        if (container.contentColumn > measured.columns)
            break;
        baseline = container.contentColumn;
    }
    return baseline;
}

int packState(int kind, int length, bool paragraph, const QString &language,
              const QVector<CodeContainer> &containers) {
    quint32 hash = 2166136261u;
    const auto mix = [&hash](quint32 value) {
        hash ^= value;
        hash *= 16777619u;
    };
    mix(quint32(kind));
    mix(quint32(length));
    mix(paragraph ? 1u : 0u);
    mix(quint32(language.size()));
    for (const QChar character : language)
        mix(character.unicode());
    mix(quint32(containers.size()));
    for (const CodeContainer &container : containers) {
        mix(quint32(container.markerColumn));
        mix(quint32(container.contentColumn));
        mix(container.style.unicode());
    }
    hash &= 0x7FFFFFFFu;
    hash &= ~quint32(commentBit);
    if (hash == 0u)
        hash = 1u;
    return int(hash);
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

bool blockIndent(const Measured &measured, int baseline) {
    const int relative = measured.columns - baseline;
    return !measured.blank && relative >= 0 && relative <= 3;
}

bool openingFence(const QString &text, const Measured &measured, int baseline, QChar *character,
                  int *length, QString *language) {
    if (!blockIndent(measured, baseline))
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

bool closingFence(const QString &text, const Measured &measured, int baseline, QChar expected,
                  int minimum) {
    if (!blockIndent(measured, baseline) || expected.isNull())
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

bool atxHeading(const QString &text, int baseline) {
    const Measured measured = measure(text);
    if (!blockIndent(measured, baseline))
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

bool thematicBreak(const QString &text, int baseline) {
    const Measured measured = measure(text);
    if (!blockIndent(measured, baseline))
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

Marker listMarker(const QString &text, const Measured &measured, int baseline) {
    Marker marker;
    if (!blockIndent(measured, baseline))
        return marker;
    int index = measured.index;
    const QChar first = text.at(index);
    int width = 0;
    if (first == QLatin1Char('-') || first == QLatin1Char('+') || first == QLatin1Char('*')) {
        marker.bullet = true;
        marker.style = first;
        width = 1;
        ++index;
    } else if (first >= QLatin1Char('0') && first <= QLatin1Char('9')) {
        int value = 0;
        int digits = 0;
        while (index < text.size() && digits < 9 && text.at(index) >= QLatin1Char('0')
               && text.at(index) <= QLatin1Char('9')) {
            value = value * 10 + (text.at(index).unicode() - u'0');
            ++index;
            ++digits;
        }
        if (digits < 1 || index >= text.size())
            return Marker();
        if (text.at(index) >= QLatin1Char('0') && text.at(index) <= QLatin1Char('9'))
            return Marker();
        const QChar delimiter = text.at(index);
        if (delimiter != QLatin1Char('.') && delimiter != QLatin1Char(')'))
            return Marker();
        ++index;
        width = digits + 1;
        marker.bullet = false;
        marker.number = value;
        marker.style = delimiter;
    } else {
        return marker;
    }

    if (index >= text.size()) {
        marker.matched = true;
        marker.empty = true;
        marker.contentColumn = measured.columns + width + 1;
        return marker;
    }
    if (text.at(index) != QLatin1Char(' ') && text.at(index) != QLatin1Char('\t'))
        return marker;

    int spaces = 0;
    int column = measured.columns + width;
    while (index < text.size()) {
        const QChar character = text.at(index);
        if (character == QLatin1Char(' ')) {
            ++spaces;
            ++column;
            ++index;
            continue;
        }
        if (character == QLatin1Char('\t')) {
            const int tab = 4 - (column % 4);
            spaces += tab;
            column += tab;
            ++index;
            continue;
        }
        break;
    }
    if (spaces < 1)
        return marker;
    const int padding = spaces > 4 ? 1 : spaces;
    marker.matched = true;
    marker.empty = index >= text.size() && spaces <= 4;
    marker.contentColumn = measured.columns + width + padding;
    return marker;
}

bool interruptsAt(const QString &text, const Measured &measured, int baseline) {
    if (atxHeading(text, baseline) || thematicBreak(text, baseline))
        return true;
    QChar character;
    int length = 0;
    QString language;
    if (openingFence(text, measured, baseline, &character, &length, &language))
        return true;
    const Marker marker = listMarker(text, measured, baseline);
    if (!marker.matched || thematicBreak(text, baseline))
        return false;
    if (marker.bullet)
        return true;
    return marker.number == 1;
}

int matchingSibling(const QString &text, const Measured &measured,
                    const QVector<CodeContainer> &containers) {
    if (measured.blank)
        return -1;
    for (int index = containers.size() - 1; index >= 0; --index) {
        const CodeContainer &container = containers.at(index);
        if (container.markerColumn != measured.columns)
            continue;
        if (thematicBreak(text, container.markerColumn))
            continue;
        const Marker marker = listMarker(text, measured, container.markerColumn);
        if (marker.matched && marker.style == container.style)
            return index;
    }
    return -1;
}

bool followingIndentedChunk(const QTextBlock &block, int baseline) {
    QTextBlock next = block.next();
    while (next.isValid() && measure(next.text()).blank)
        next = next.next();
    if (!next.isValid())
        return false;
    const Measured measured = measure(next.text());
    return !measured.blank && measured.columns >= baseline + 4;
}

Carry readCarry(int previousState, const QTextBlock &block) {
    Carry carry;
    if (previousState < 0 || !block.isValid())
        return carry;
    const QTextBlock previous = block.previous();
    if (!previous.isValid())
        return carry;
    const auto *data = dynamic_cast<const CodeBlockData *>(previous.userData());
    if (!data)
        return carry;
    carry.kind = data->line.carryKind;
    carry.length = data->line.carryLength;
    carry.paragraph = data->line.carryParagraph;
    carry.containers = data->line.containers;
    if (carry.kind == Backtick || carry.kind == Tilde)
        carry.language = data->line.language;
    return carry;
}

CodeLine finished(bool inCode, bool opener, bool closer, bool indented, const QString &language,
                  const QVector<CodeContainer> &containers, int carryKind, int carryLength,
                  bool carryParagraph) {
    CodeLine line;
    line.inCode = inCode;
    line.opener = opener;
    line.closer = closer;
    line.indented = indented;
    line.language = language;
    line.containers = containers;
    line.carryKind = carryKind;
    line.carryLength = carryLength;
    line.carryParagraph = carryParagraph;
    line.nextState = packState(carryKind, carryLength, carryParagraph, language, containers);
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
    const Carry carry = readCarry(previousState, block);
    const Measured measured = measure(text);

    if (carry.kind == Backtick || carry.kind == Tilde) {
        const int baseline = baselineOf(carry.containers);
        if (closingFence(text, measured, baseline, fenceCharacter(carry.kind), carry.length))
            return finished(true, false, true, false, {}, carry.containers, None, 0, false);
        return finished(true, false, false, false, carry.language, carry.containers, carry.kind,
                        carry.length, false);
    }

    if (measured.blank) {
        const int baseline = baselineOf(carry.containers);
        const bool keep = carry.kind == Indented && block.isValid()
            && followingIndentedChunk(block, baseline);
        if (keep) {
            return finished(true, false, false, true, {}, carry.containers, Indented, 0, false);
        }
        return finished(false, false, false, false, {}, carry.containers, None, 0, false);
    }

    QVector<CodeContainer> containers = carry.containers;
    bool replacedSibling = false;
    const int sibling = matchingSibling(text, measured, containers);
    if (sibling >= 0) {
        while (containers.size() > sibling)
            containers.removeLast();
        replacedSibling = true;
    }
    while (!containers.isEmpty() && measured.columns < containers.last().contentColumn) {
        const int landing = landingBaseline(measured, containers);
        if (!replacedSibling && carry.paragraph && !interruptsAt(text, measured, landing))
            break;
        containers.removeLast();
    }

    const int baseline = baselineOf(containers);
    // Indented code cannot interrupt a paragraph, and it starts four columns
    // past the open list item. A nested task indented only to the item's
    // content stays a task.
    if (!carry.paragraph && measured.columns >= baseline + 4) {
        return finished(true, false, false, true, {}, containers, Indented, 0, false);
    }

    QChar character;
    int length = 0;
    QString language;
    if (openingFence(text, measured, baseline, &character, &length, &language)) {
        const int opened = character == QLatin1Char('`') ? Backtick : Tilde;
        return finished(true, true, false, false, language, containers, opened, length, false);
    }

    if (atxHeading(text, baseline) || thematicBreak(text, baseline))
        return finished(false, false, false, false, {}, containers, None, 0, false);

    const Marker marker = listMarker(text, measured, baseline);
    if (marker.matched && !thematicBreak(text, baseline)) {
        const bool starts = replacedSibling || !carry.paragraph || marker.bullet || marker.number == 1;
        if (starts) {
            CodeContainer item;
            item.markerColumn = measured.columns;
            item.contentColumn = marker.contentColumn;
            item.style = marker.style;
            containers.append(item);
            return finished(false, false, false, false, {}, containers, None, 0, !marker.empty);
        }
    }

    return finished(false, false, false, false, {}, containers, None, 0, true);
}
