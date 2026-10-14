#pragma once

#include <QString>
#include <QVariantList>

struct MarkdownRenderOptions {
    bool dark = true;
    QString baseDirectory;
    QString foreground;
    QString accent;
    QString muted;
    QString codeBackground;
};

// CommonMark plus GitHub tables, strikethrough, autolinks, task lists, and
// footnotes. Raw HTML is passed through HtmlSanitizer before it reaches Qt.
class MarkdownRenderer {
public:
    static QVariantList render(const QString &markdown, const MarkdownRenderOptions &options);
};
