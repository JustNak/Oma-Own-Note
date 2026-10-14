#pragma once

#include <QString>

// Allowlist sanitizer for markdown HTML. Rebuilds tags from a parse instead of
// stripping with a regular expression, so event handlers and unsafe URLs do
// not survive odd quoting or entity encoding.
class HtmlSanitizer {
public:
    static QString escape(const QString &text);
    static QString sanitize(const QString &html, const QString &imageBaseDirectory = QString());

    // normalized receives the URL that would be written into an attribute.
    static bool isSafeLinkUrl(const QString &url, QString *normalized = nullptr);
    static bool isSafeImageUrl(const QString &url, QString *normalized = nullptr);
};
