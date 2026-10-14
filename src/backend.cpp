#include "backend.h"

#include <QUrlQuery>

#include <QClipboard>
#include <QColor>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QDesktopServices>
#include <QGuiApplication>
#include <QImageReader>
#include <QMimeData>
#include <QProcess>
#include <QPrintDialog>
#include <QPrinter>
#include <QQuickTextDocument>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QSaveFile>
#include <QTextBlock>
#include <QTextBlockFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextStream>
#include <QUrl>
#include <QVariantMap>
#include <QWindow>

#include <algorithm>

#include "markdownhighlighter.h"

constexpr qreal typoraLineHeightPercent = 140;
const QString lastSaveDirectorySetting = QStringLiteral("file/lastSaveDirectory");
const QString zoomPercentSetting = QStringLiteral("view/zoomPercent");

QString Backend::normalizedLinkUrl(const QString &clipboardText) {
    QString candidate = clipboardText.trimmed();
    static const QRegularExpression lineBreakRe(QStringLiteral("[\\r\\n]"));
    const int lineBreak = candidate.indexOf(lineBreakRe);
    if (lineBreak >= 0)
        candidate = candidate.left(lineBreak).trimmed();

    if (candidate.isEmpty())
        return {};

    if (candidate.startsWith(QStringLiteral("www."), Qt::CaseInsensitive))
        candidate.prepend(QStringLiteral("https://"));

    static const QRegularExpression schemeRe(
        QStringLiteral("^[A-Za-z][A-Za-z0-9+.-]*:"));
    if (!schemeRe.match(candidate).hasMatch())
        return {};

    const QUrl url(candidate);
    if (!url.isValid() || url.scheme().isEmpty())
        return {};

    const QString scheme = url.scheme().toLower();
    const bool webUrl = scheme == QStringLiteral("http")
        || scheme == QStringLiteral("https")
        || scheme == QStringLiteral("ftp");
    if (webUrl && url.host().isEmpty())
        return {};

    if (!webUrl && scheme != QStringLiteral("mailto"))
        return {};

    return url.toString();
}

Backend::Backend(QObject *parent) : QObject(parent) {
    const QString stateDirectory = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(stateDirectory);
    // Claim an orphaned snapshot before taking an empty slot. This ensures a
    // crash in window 2 is still recovered even if window 1 exited normally.
    for (int pass = 0; pass < 2 && !m_recoveryLock; ++pass) {
        for (int slot = 0; slot < 100; ++slot) {
            const QString base = QDir(stateDirectory).filePath(
                QStringLiteral("recovery-%1").arg(slot));
            const bool snapshotExists = QFileInfo::exists(base + QStringLiteral(".json"));
            if ((pass == 0) != snapshotExists)
                continue;
            auto lock = std::make_unique<QLockFile>(base + QStringLiteral(".lock"));
            if (lock->tryLock()) {
                m_recoveryPath = base + QStringLiteral(".json");
                m_recoveryLock = std::move(lock);
                break;
            }
        }
    }
    m_wordCountTimer.setSingleShot(true);
    m_wordCountTimer.setInterval(120);
    connect(&m_wordCountTimer, &QTimer::timeout, this, &Backend::refreshWordCount);
    m_recoveryTimer.setSingleShot(true);
    m_recoveryTimer.setInterval(750);
    connect(&m_recoveryTimer, &QTimer::timeout, this, &Backend::writeRecovery);
    connect(&m_fileWatcher, &QFileSystemWatcher::fileChanged, this,
            [this](const QString &path) {
                if (path != m_fileUrl.toLocalFile())
                    return;

                const bool deleted = !QFileInfo::exists(path);
                if (!deleted && m_hasKnownFileContents) {
                    QFile file(path);
                    if (file.open(QIODevice::ReadOnly)
                            && file.readAll() == m_lastKnownFileContents) {
                        // Atomic saves can replace the watched inode. Re-arm the
                        // watcher, but do not report our own save as an outside edit.
                        watchCurrentFile();
                        return;
                    }
                }

                emit externalChangeDetected(deleted, m_modified);
            });

    loadOmarchyTheme();
    watchOmarchyTheme();
    connect(&m_themeWatcher, &QFileSystemWatcher::fileChanged, this, [this]() {
        loadOmarchyTheme();
        watchOmarchyTheme();
    });
    connect(&m_themeWatcher, &QFileSystemWatcher::directoryChanged, this, [this]() {
        loadOmarchyTheme();
        watchOmarchyTheme();
    });

    const int raw = QSettings().value(zoomPercentSetting,
                                      ViewZoom::kDefaultPercent).toInt();
    m_zoom = ViewZoom::fromPercent(raw);
    if (m_zoom.percent() != raw)
        persistZoom();
}

Backend::~Backend() = default;

void Backend::setParentWindow(QWindow *window) {
    m_parentWindow = window;
}

QString Backend::fileName() const {
    if (!m_fileUrl.isValid() || m_fileUrl.isEmpty())
        return QStringLiteral("Untitled.md");

    if (m_fileUrl.isLocalFile()) {
        const QFileInfo info(m_fileUrl.toLocalFile());
        if (!info.fileName().isEmpty())
            return info.fileName();
    }

    const QString name = m_fileUrl.fileName();
    return name.isEmpty() ? QStringLiteral("Untitled.md") : name;
}

void Backend::setDarkMode(bool darkMode) {
    if (m_darkMode == darkMode)
        return;

    m_darkMode = darkMode;
    loadOmarchyTheme();
    emit darkModeChanged();
}

void Backend::setTextScale(qreal textScale) {
    if (qFuzzyCompare(m_textScale, textScale))
        return;

    m_textScale = textScale;
    emit textScaleChanged();
}

void Backend::zoomIn() {
    setZoom(m_zoom.stepped(1));
}

void Backend::zoomOut() {
    setZoom(m_zoom.stepped(-1));
}

void Backend::resetZoom() {
    setZoom(ViewZoom{});
}

void Backend::zoomToPercent(int percent) {
    setZoom(ViewZoom::fromPercent(percent));
}

void Backend::setZoom(ViewZoom zoom) {
    if (zoom == m_zoom)
        return;
    m_zoom = zoom;
    persistZoom();
    emit zoomFactorChanged();
}

void Backend::persistZoom() const {
    QSettings().setValue(zoomPercentSetting, m_zoom.percent());
}

void Backend::attachDocument(QObject *textDocument) {
    auto *quickDocument = qobject_cast<QQuickTextDocument *>(textDocument);
    if (!quickDocument || !quickDocument->textDocument()) {
        setStatus(QStringLiteral("Could not attach the Markdown renderer."));
        return;
    }

    if (m_highlighter)
        delete m_highlighter.data();

    m_document = quickDocument->textDocument();
    m_lastDocumentText = m_document->toPlainText();
    const bool wasLoading = m_loading;
    m_loading = true;
    disconnect(m_document, &QTextDocument::contentsChange, this, nullptr);
    connect(m_document, &QTextDocument::contentsChange, this,
            [this](int position, int charsRemoved, int charsAdded) {
                if (m_formattingTypography || m_loading || m_restyling)
                    return;
                if (m_highlighter && m_highlighter->painting())
                    return;
                m_lastChangePos = position;
                m_lastChangeRemoved = charsRemoved;
                m_lastChangeAdded = charsAdded;
                ++m_changeGeneration;
            });
    m_highlighter = new MarkdownHighlighter(m_document);
    m_loading = wasLoading;
    m_highlighter->setDarkMode(m_darkMode);
    m_highlighter->setColors(m_themeBackground, m_themeForeground, m_themeAccent);

    applyDocumentTypography();
    restoreRecovery();
}

void Backend::openDialog() {
    emit openDialogRequested();
}

void Backend::open(const QUrl &url) {
    if (!url.isLocalFile()) {
        setStatus(QStringLiteral("Only local files can be opened."));
        return;
    }

    const QString targetName = QFileInfo(url.toLocalFile()).fileName();
    QFile file(url.toLocalFile());
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        setStatus(QStringLiteral("Could not open %1.").arg(targetName));
        return;
    }

    const QByteArray contents = file.readAll();
    setFileUrl(url);
    loadDocumentText(QString::fromUtf8(contents));
    clearRecovery();
    m_lastKnownFileContents = contents;
    m_hasKnownFileContents = true;
    watchCurrentFile();
    setModified(false);
    setStatus(QStringLiteral("Opened %1").arg(fileName()));
}

void Backend::save() {
    if (!m_fileUrl.isValid() || m_fileUrl.isEmpty()) {
        saveAsDialog();
        return;
    }

    saveTo(m_fileUrl);
}

void Backend::saveForClose() {
    if (!m_modified) {
        emit closeAfterSave();
        return;
    }

    m_closeAfterSave = true;
    save();
}

void Backend::saveAsDialog() {
    emit saveDialogRequested(suggestedSaveUrl());
}

void Backend::saveAs(const QUrl &url) {
    saveTo(url);
}

void Backend::fileDialogCanceled() {
    m_closeAfterSave = false;
}

void Backend::discardRecovery() {
    clearRecovery();
}

void Backend::reloadFromDisk() {
    if (m_fileUrl.isLocalFile())
        open(m_fileUrl);
}

void Backend::keepExternalVersion() {
    QFile file(m_fileUrl.toLocalFile());
    if (file.open(QIODevice::ReadOnly)) {
        m_lastKnownFileContents = file.readAll();
        m_hasKnownFileContents = true;
    } else {
        m_lastKnownFileContents.clear();
        m_hasKnownFileContents = false;
    }
    setModified(true);
    scheduleRecovery();
    watchCurrentFile();
    setStatus(QStringLiteral("Kept your version"));
}

void Backend::printDocument() {
    if (!m_document) {
        setStatus(QStringLiteral("There is no document to print."));
        return;
    }

    QPrinter printer(QPrinter::HighResolution);
    QPrintDialog dialog(&printer);
    dialog.setWindowTitle(QStringLiteral("Print %1").arg(fileName()));
    dialog.winId();
    if (dialog.windowHandle() && m_parentWindow)
        dialog.windowHandle()->setTransientParent(m_parentWindow);

    if (dialog.exec() == QDialog::Accepted) {
        QTextDocument rendered;
        rendered.setDefaultFont(m_document->defaultFont());
        rendered.setMarkdown(currentDocumentText());
        rendered.print(&printer);
    }
}

void Backend::newWindow() {
    const bool started = QProcess::startDetached(QCoreApplication::applicationFilePath(),
                                                 QStringList());
    if (!started)
        setStatus(QStringLiteral("Could not open a new window."));
}

QString Backend::clipboardUrl() const {
    const QClipboard *clipboard = QGuiApplication::clipboard();
    if (!clipboard)
        return {};

    const QMimeData *mimeData = clipboard->mimeData();
    if (!mimeData)
        return {};

    if (mimeData->hasUrls()) {
        const QList<QUrl> urls = mimeData->urls();
        for (const QUrl &url : urls) {
            const QString normalized = normalizedLinkUrl(url.toString());
            if (!normalized.isEmpty())
                return normalized;
        }
    }

    if (!mimeData->hasText())
        return {};

    return normalizedLinkUrl(mimeData->text());
}

QString Backend::clipboardText() const {
    const QClipboard *clipboard = QGuiApplication::clipboard();
    if (!clipboard)
        return {};

    const QMimeData *mimeData = clipboard->mimeData();
    return mimeData && mimeData->hasText() ? mimeData->text() : QString();
}

bool Backend::editorTextChanged() {
    if (m_loading || m_formattingTypography || m_restyling)
        return false;

    const QString text = currentDocumentText();
    if (text == m_lastDocumentText)
        return false;

    m_restyling = true;
    int position = 0;
    int removed = 0;
    int added = 0;
    if (m_changeGeneration != m_appliedGeneration) {
        position = m_lastChangePos;
        removed = m_lastChangeRemoved;
        added = m_lastChangeAdded;
        m_appliedGeneration = m_changeGeneration;
    } else {
        const QString &previous = m_lastDocumentText;
        int prefix = 0;
        const int prefixLimit = qMin(text.size(), previous.size());
        while (prefix < prefixLimit && text.at(prefix) == previous.at(prefix))
            ++prefix;
        int suffix = 0;
        while (suffix < text.size() - prefix && suffix < previous.size() - prefix
               && text.at(text.size() - 1 - suffix) == previous.at(previous.size() - 1 - suffix))
            ++suffix;
        position = prefix;
        removed = previous.size() - prefix - suffix;
        added = text.size() - prefix - suffix;
        m_lastChangePos = position;
        m_lastChangeRemoved = removed;
        m_lastChangeAdded = added;
    }
    m_lastDocumentText = text;

    int from = -1;
    int to = -1;
    if (m_document && m_highlighter) {
        m_highlighter->rehighlightChange(position, removed, added);
        if (m_highlighter->hasPaintedRange()) {
            from = m_highlighter->paintedFrom();
            to = m_highlighter->paintedTo();
        }
    }
    // Block metrics follow the text and join the edit that just changed it,
    // so one undo restores both the characters and their layout.
    reapplyTypographyToChange(from, to);
    if (m_document)
        m_formattedBlockCount = m_document->blockCount();

    scheduleWordCount();
    setModified(true);
    setStatus(QStringLiteral("Unsaved"));
    scheduleRecovery();
    rebuildDecorations(from, to);
    m_restyling = false;
    return true;
}

bool Backend::editingInside(const QTextBlock &block) const {
    if (m_editingPosition < 0 || !block.isValid())
        return false;
    int from = m_editingPosition;
    int to = m_editingPosition;
    if (m_selectionStart >= 0 && m_selectionEnd >= 0) {
        from = qMin(from, qMin(m_selectionStart, m_selectionEnd));
        to = qMax(to, qMax(m_selectionStart, m_selectionEnd));
    }
    const int start = block.position();
    const int end = start + qMax(0, block.length() - 1);
    return to >= start && from <= end;
}

QString Backend::themeMuted() const {
    return MarkdownHighlighter::mutedColor(m_darkMode);
}

QVariantList Backend::hiddenRangesAt(int position) const {
    QVariantList ranges;
    if (!m_document)
        return ranges;

    const QTextBlock block =
        m_document->findBlock(qBound(0, position, m_document->characterCount() - 1));
    if (!block.isValid())
        return ranges;

    const int lineStart = block.position();
    const bool legacy = m_editingPosition < 0;
    const bool onLine = editingInside(block);
    if (m_highlighter && m_highlighter->blockInCode(block))
        return ranges;
    QList<QPair<int, int>> spans;
    if (legacy || !onLine) {
        const QList<MarkdownHighlighter::InlineMarkup> markup =
            MarkdownHighlighter::inlineMarkup(block.text());
        for (const MarkdownHighlighter::InlineMarkup &item : markup) {
            for (const MarkdownHighlighter::Span &marker : item.markers) {
                spans.append({lineStart + marker.start,
                              lineStart + marker.start + marker.length});
            }
        }
        if (!onLine) {
            const QList<MarkdownHighlighter::ImageSpan> images =
                MarkdownHighlighter::imageSpans(block.text());
            for (const MarkdownHighlighter::ImageSpan &image : images) {
                spans.append({lineStart + image.start,
                              lineStart + image.start + image.length});
            }
            static const QRegularExpression bracketed(
                QStringLiteral("<((?:https?://|mailto:)[^\\s>]+)>"));
            QRegularExpressionMatchIterator matches = bracketed.globalMatch(block.text());
            while (matches.hasNext()) {
                const QRegularExpressionMatch match = matches.next();
                const int start = lineStart + int(match.capturedStart(0));
                const int end = start + int(match.capturedLength(0));
                if (end - start >= 2) {
                    spans.append({start, start + 1});
                    spans.append({end - 1, end});
                }
            }
        }
    }

    bool tableContainsCaret = false;
    if (!legacy && MarkdownHighlighter::isTableRow(block.text())) {
        QTextBlock row = block;
        while (row.previous().isValid() && MarkdownHighlighter::isTableRow(row.previous().text())
               && !(m_highlighter && m_highlighter->blockInCode(row.previous())))
            row = row.previous();
        for (; row.isValid() && MarkdownHighlighter::isTableRow(row.text())
               && !(m_highlighter && m_highlighter->blockInCode(row));
             row = row.next()) {
            if (editingInside(row)) {
                tableContainsCaret = true;
                break;
            }
        }
    }
    const bool hidePipes = legacy || !tableContainsCaret;
    if (hidePipes) {
        const MarkdownHighlighter::TableLine table =
            MarkdownHighlighter::parseTableLine(block.text());
        for (const MarkdownHighlighter::Span &hidden : table.hidden) {
            spans.append({lineStart + hidden.start,
                          lineStart + hidden.start + hidden.length});
        }
        if (table.kind == MarkdownHighlighter::TableLine::Kind::Separator
                && block.next().isValid()) {
            spans.append({lineStart + block.text().length(),
                          lineStart + block.text().length() + 1});
        }
    }
    std::sort(spans.begin(), spans.end());

    for (const auto &span : spans) {
        ranges.append(QVariantMap{{QStringLiteral("start"), span.first},
                                  {QStringLiteral("end"), span.second}});
    }
    return ranges;
}

void Backend::setSearchHighlight(const QString &query, int currentMatchStart) {
    if (m_highlighter)
        m_highlighter->setSearch(query, currentMatchStart);
}

bool Backend::openExternalUrl(const QUrl &url) {
    const QString scheme = url.scheme().toLower();
    if (scheme != QStringLiteral("http") && scheme != QStringLiteral("https")
            && scheme != QStringLiteral("mailto"))
        return false;
    QUrl opened = url;
    if (scheme == QStringLiteral("mailto")) {
        const QString cleaned = safeLinkTarget(url.toString(QUrl::FullyEncoded));
        if (cleaned.isEmpty())
            return false;
        opened = QUrl(cleaned);
    }
    // Offscreen test runs have no desktop handler; opening xdg-open there can hang.
    if (qEnvironmentVariable("QT_QPA_PLATFORM") == QLatin1String("offscreen"))
        return true;
    return QDesktopServices::openUrl(opened);
}

void Backend::setEditingPosition(int position) {
    setEditingRange(position, position, position);
}

void Backend::setEditingRange(int cursor, int selectionStart, int selectionEnd) {
    if (m_editingPosition == cursor && m_selectionStart == selectionStart
            && m_selectionEnd == selectionEnd)
        return;
    m_editingPosition = cursor;
    m_selectionStart = selectionStart;
    m_selectionEnd = selectionEnd;
    bool revealChanged = true;
    if (m_highlighter)
        revealChanged = m_highlighter->setEditingRange(cursor, selectionStart, selectionEnd);
    if (!revealChanged)
        return;
    if (m_highlighter && m_highlighter->hasPaintedRange())
        rebuildDecorations(m_highlighter->paintedFrom(), m_highlighter->paintedTo());
    else
        rebuildDecorations();
}

QString Backend::safeLinkTarget(const QString &destination) const {
    QString target = destination.trimmed();
    if (target.startsWith(QLatin1Char('<')) && target.endsWith(QLatin1Char('>'))
            && target.size() >= 2)
        target = target.mid(1, target.size() - 2).trimmed();
    if (target.startsWith(QLatin1String("www."), Qt::CaseInsensitive))
        target.prepend(QStringLiteral("https://"));
    const QUrl url(target);
    const QString scheme = url.scheme().toLower();
    if (scheme == QStringLiteral("http") || scheme == QStringLiteral("https"))
        return url.toString();
    if (target.startsWith(QLatin1String("mailto:"), Qt::CaseInsensitive)) {
        QString raw = target;
        const int hash = raw.indexOf(QLatin1Char('#'));
        if (hash >= 0)
            raw.truncate(hash);
        const int colon = raw.indexOf(QLatin1Char(':'));
        const QString rest = raw.mid(colon + 1);
        const int queryAt = rest.indexOf(QLatin1Char('?'));
        const QString address = queryAt >= 0 ? rest.left(queryAt) : rest;
        const QString queryText = queryAt >= 0 ? rest.mid(queryAt + 1) : QString();
        if (address.contains(QLatin1Char('?')) || address.contains(QLatin1Char('&'))
                || address.contains(QLatin1String("%3F"), Qt::CaseInsensitive))
            return {};
        QUrlQuery kept;
        const QUrlQuery parsed(queryText);
        for (const auto &item : parsed.queryItems(QUrl::FullyDecoded)) {
            const QString key = item.first.toLower();
            if (key == QLatin1String("subject") || key == QLatin1String("body"))
                kept.addQueryItem(key, item.second);
        }
        QString cleaned = QStringLiteral("mailto:") + address;
        const QString encoded = kept.toString(QUrl::FullyEncoded);
        if (!encoded.isEmpty())
            cleaned += QLatin1Char('?') + encoded;
        return cleaned;
    }
    return {};
}

QString Backend::linkAt(int position) const {
    if (!m_document)
        return {};
    const QTextBlock block =
        m_document->findBlock(qBound(0, position, m_document->characterCount() - 1));
    if (!block.isValid())
        return {};
    const QString text = block.text();
    const int local = position - block.position();
    const QList<MarkdownHighlighter::InlineMarkup> markup =
        MarkdownHighlighter::inlineMarkup(text);
    for (const MarkdownHighlighter::InlineMarkup &item : markup) {
        if (item.kind != MarkdownHighlighter::InlineKind::Link)
            continue;
        const int start = item.markers[0].start;
        const int end = item.markers[1].start + item.markers[1].length;
        if (local < start || local >= end)
            continue;
        const int destStart = item.content.start + item.content.length + 2;
        const int destLength = item.markers[1].length - 3;
        if (destLength <= 0)
            return {};
        return safeLinkTarget(text.mid(destStart, destLength));
    }

    static const QRegularExpression autolinkRe(
        QStringLiteral("<((?:https?://|mailto:)[^\\s>]+)>|(?<![<\\w/])((?:https?://|mailto:)[^\\s<]+)|(?<![\\w@])(www\\.[^\\s<]+)"));
    QRegularExpressionMatchIterator matches = autolinkRe.globalMatch(text);
    while (matches.hasNext()) {
        const QRegularExpressionMatch match = matches.next();
        if (local < match.capturedStart(0) || local >= match.capturedEnd(0))
            continue;
        const QString raw = !match.captured(1).isEmpty() ? match.captured(1)
            : !match.captured(2).isEmpty() ? match.captured(2)
                                           : match.captured(3);
        return safeLinkTarget(raw);
    }
    return {};
}

QVariantMap Backend::taskAt(int position) const {
    if (!m_document)
        return {};
    const QTextBlock block =
        m_document->findBlock(qBound(0, position, m_document->characterCount() - 1));
    if (!block.isValid() || (m_highlighter && m_highlighter->blockInCode(block)))
        return {};
    static const QRegularExpression taskRe(
        QStringLiteral("^\\s*(?:[-+*]|\\d+[.)])\\s+(\\[[ xX]\\])"));
    const QRegularExpressionMatch match = taskRe.match(block.text());
    if (!match.hasMatch())
        return {};
    const int start = block.position() + int(match.capturedStart(1));
    const int end = start + int(match.capturedLength(1));
    if (position < start || position >= end)
        return {};
    const QString box = match.captured(1);
    const bool checked = box.size() >= 2 && box.at(1) != QLatin1Char(' ');
    return {{QStringLiteral("start"), start},
            {QStringLiteral("end"), end},
            {QStringLiteral("checked"), checked},
            {QStringLiteral("replacement"),
             checked ? QStringLiteral("[ ]") : QStringLiteral("[x]")}};
}

bool Backend::toggleTask(int position) {
    if (!m_document)
        return false;
    const QVariantMap task = taskAt(position);
    if (task.isEmpty())
        return false;
    const int start = task.value(QStringLiteral("start")).toInt();
    const int end = task.value(QStringLiteral("end")).toInt();
    QTextCursor cursor(m_document);
    cursor.beginEditBlock();
    cursor.setPosition(start);
    cursor.setPosition(end, QTextCursor::KeepAnchor);
    cursor.insertText(task.value(QStringLiteral("replacement")).toString());
    cursor.endEditBlock();
    return true;
}

QString Backend::noteRoot() const {
    if (!m_fileUrl.isLocalFile())
        return {};
    const QFileInfo info(m_fileUrl.toLocalFile());
    const QString canonical = QFileInfo(info.absolutePath()).canonicalFilePath();
    return canonical.isEmpty() ? info.absolutePath() : canonical;
}

QVariantMap Backend::classifyImageDestination(const QString &destination) const {
    const auto rejected = [](const QString &reason) {
        return QVariantMap{{QStringLiteral("mode"), QStringLiteral("rejected")},
                           {QStringLiteral("reason"), reason},
                           {QStringLiteral("source"), QString()},
                           {QStringLiteral("remoteUrl"), QString()}};
    };
    const auto remote = [](const QString &url) {
        return QVariantMap{{QStringLiteral("mode"), QStringLiteral("remote")},
                           {QStringLiteral("reason"), QStringLiteral("remote")},
                           {QStringLiteral("source"), QString()},
                           {QStringLiteral("remoteUrl"), url}};
    };

    QString raw = destination.trimmed();
    if (raw.startsWith(QLatin1Char('<')) && raw.endsWith(QLatin1Char('>')) && raw.size() >= 2)
        raw = raw.mid(1, raw.size() - 2).trimmed();
    const QString cacheKey = noteRoot() + QLatin1Char('\n') + raw;
    const auto cached = m_imageClassCache.constFind(cacheKey);
    if (cached != m_imageClassCache.constEnd())
        return cached.value();
    const auto store = [this, &cacheKey](const QVariantMap &value) {
        const QString mode = value.value(QStringLiteral("mode")).toString();
        if (mode != QLatin1String("local") && mode != QLatin1String("missing"))
            m_imageClassCache.insert(cacheKey, value);
        return value;
    };
    if (raw.isEmpty())
        return store(rejected(QStringLiteral("empty")));
    // Keep the original remote spelling. Decoding first turned https://host/a%20b
    // into a different URL, and a scheme-relative //host resolved against qrc:/.
    if (raw.startsWith(QLatin1String("//")))
        return store(remote(QStringLiteral("https:") + raw));
    if (raw.startsWith(QLatin1String("http://"), Qt::CaseInsensitive)
            || raw.startsWith(QLatin1String("https://"), Qt::CaseInsensitive))
        return store(remote(raw));

    QString decoded = QUrl::fromPercentEncoding(raw.toUtf8()).trimmed();
    const QUrl parsed(decoded);
    const QString scheme = parsed.scheme().toLower();
    if (scheme == QLatin1String("http") || scheme == QLatin1String("https"))
        return store(remote(decoded));
    if (scheme == QLatin1String("javascript") || scheme == QLatin1String("data"))
        return store(rejected(scheme));
    if (scheme == QLatin1String("file") || decoded.startsWith(QLatin1String("file:"), Qt::CaseInsensitive)) {
        if (!parsed.host().isEmpty())
            return store(rejected(QStringLiteral("file-host")));
        QString path = parsed.toLocalFile();
        if (path.isEmpty())
            path = parsed.path();
        decoded = path;
    } else if (!scheme.isEmpty()) {
        return store(rejected(QStringLiteral("scheme")));
    }

    const QString root = noteRoot();
    if (root.isEmpty())
        return store(rejected(QStringLiteral("no-root")));

    const bool absolute = decoded.startsWith(QLatin1Char('/'))
        || QDir::isAbsolutePath(decoded);
    const QString absolutePath = QDir::cleanPath(absolute ? decoded
                                                          : QDir(root).filePath(decoded));
    const QString rootCanon = QFileInfo(root).canonicalFilePath().isEmpty()
        ? QDir::cleanPath(root)
        : QFileInfo(root).canonicalFilePath();

    QStringList pending;
    QFileInfo walk(absolutePath);
    while (!walk.exists()) {
        const QString name = walk.fileName();
        const QString parent = walk.absolutePath();
        if (name.isEmpty() || parent == walk.absoluteFilePath())
            return store(rejected(absolute ? QStringLiteral("absolute") : QStringLiteral("escape")));
        pending.prepend(name);
        walk = QFileInfo(parent);
    }
    QString combined = walk.canonicalFilePath();
    if (combined.isEmpty())
        return store(rejected(absolute ? QStringLiteral("absolute") : QStringLiteral("escape")));
    for (const QString &part : pending)
        combined = QDir::cleanPath(QDir(combined).filePath(part));
    if (combined != rootCanon && !combined.startsWith(rootCanon + QLatin1Char('/')))
        return store(rejected(absolute ? QStringLiteral("absolute") : QStringLiteral("escape")));

    if (QFileInfo::exists(combined)) {
        return store({{QStringLiteral("mode"), QStringLiteral("local")},
                      {QStringLiteral("reason"), QString()},
                      {QStringLiteral("source"), QUrl::fromLocalFile(combined).toString()},
                      {QStringLiteral("remoteUrl"), QString()},
                      {QStringLiteral("path"), combined}});
    }
    return store({{QStringLiteral("mode"), QStringLiteral("missing")},
                  {QStringLiteral("reason"), QStringLiteral("missing")},
                  {QStringLiteral("source"), QString()},
                  {QStringLiteral("remoteUrl"), QString()},
                  {QStringLiteral("path"), combined}});
}

QVariantMap Backend::imageAt(int position) const {
    if (!m_document)
        return {};
    const QTextBlock block =
        m_document->findBlock(qBound(0, position, m_document->characterCount() - 1));
    if (!block.isValid())
        return {};
    const int local = position - block.position();
    const QList<MarkdownHighlighter::ImageSpan> images =
        MarkdownHighlighter::imageSpans(block.text());
    for (const MarkdownHighlighter::ImageSpan &image : images) {
        if (local < image.start || local >= image.start + image.length)
            continue;
        QVariantMap classified = classifyImageDestination(image.destination);
        classified.insert(QStringLiteral("kind"), QStringLiteral("image"));
        classified.insert(QStringLiteral("start"), block.position() + image.start);
        classified.insert(QStringLiteral("end"), block.position() + image.start + image.length);
        classified.insert(QStringLiteral("alt"), image.alt);
        return classified;
    }
    return {};
}

QSize Backend::imageDisplaySize(const QVariantMap &classified) const {
    const QString mode = classified.value(QStringLiteral("mode")).toString();
    if (mode == QLatin1String("local")) {
        QImageReader reader(classified.value(QStringLiteral("path")).toString());
        reader.setAutoTransform(true);
        const QSize fitted = MarkdownHighlighter::fittedImageSize(reader.size());
        if (!fitted.isEmpty())
            return fitted;
        return QSize(MarkdownHighlighter::imageMaxWidth, MarkdownHighlighter::imageLineHeight);
    }
    if (mode == QLatin1String("remote"))
        return QSize(MarkdownHighlighter::imageMaxWidth, MarkdownHighlighter::imageLineHeight);
    return QSize(160, MarkdownHighlighter::imagePlaceholderHeight);
}

void Backend::expandDecorationRange(int &fromBlock, int &toBlock) const {
    if (!m_document || m_document->blockCount() <= 0)
        return;
    const int lastIndex = m_document->blockCount() - 1;
    fromBlock = qBound(0, fromBlock, lastIndex);
    toBlock = qBound(fromBlock, toBlock, lastIndex);
    const auto at = [this](int number) {
        return m_document->findBlockByNumber(number);
    };
    const auto inCode = [this](const QTextBlock &block) {
        return m_highlighter && m_highlighter->blockInCode(block);
    };
    const auto lineOf = [this](const QTextBlock &block) {
        return m_highlighter ? m_highlighter->codeLine(block) : CodeLine{};
    };
    const auto joined = [&](const QTextBlock &left, const QTextBlock &right) {
        if (!left.isValid() || !right.isValid())
            return false;
        if (inCode(left) && inCode(right) && !lineOf(right).opener)
            return true;
        if (!inCode(left) && !inCode(right)
                && MarkdownHighlighter::isTableRow(left.text())
                && MarkdownHighlighter::isTableRow(right.text()))
            return true;
        const bool leftQuote = left.text().trimmed().startsWith(QLatin1Char('>'));
        const bool rightQuote = right.text().trimmed().startsWith(QLatin1Char('>'));
        return !inCode(left) && !inCode(right) && leftQuote && rightQuote;
    };
    while (fromBlock > 0 && joined(at(fromBlock - 1), at(fromBlock)))
        --fromBlock;
    while (toBlock < lastIndex && joined(at(toBlock), at(toBlock + 1)))
        ++toBlock;
}

QVariantList Backend::decorationsForRange(int fromBlock, int toBlock) const {
    QVariantList next;
    if (!m_document || m_editingPosition < 0)
        return next;
    const int lastIndex = m_document->blockCount() - 1;
    fromBlock = qBound(0, fromBlock, lastIndex);
    toBlock = qBound(fromBlock, toBlock, lastIndex);
    const QTextBlock first = m_document->findBlockByNumber(fromBlock);
    const QTextBlock last = m_document->findBlockByNumber(toBlock);
    if (!first.isValid() || !last.isValid())
        return next;

    const auto inCode = [this](const QTextBlock &block) {
        return m_highlighter && m_highlighter->blockInCode(block);
    };
    const auto lineOf = [this](const QTextBlock &block) {
        return m_highlighter ? m_highlighter->codeLine(block) : CodeLine{};
    };

    for (QTextBlock block = first; block.isValid() && block.blockNumber() <= toBlock;
         block = block.next()) {
        if (inCode(block))
            continue;
        const QString text = block.text();
        static const QRegularExpression taskRe(
            QStringLiteral("^\\s*(?:[-+*]|\\d+[.)])\\s+(\\[[ xX]\\])"));
        const QRegularExpressionMatch task = taskRe.match(text);
        if (task.hasMatch() && !editingInside(block)) {
            const int start = block.position() + int(task.capturedStart(1));
            const QString box = task.captured(1);
            next.append(QVariantMap{
                {QStringLiteral("kind"), QStringLiteral("task")},
                {QStringLiteral("start"), start},
                {QStringLiteral("end"), start + box.size()},
                {QStringLiteral("checked"), box.size() >= 2 && box.at(1) != QLatin1Char(' ')}});
        }

        if (MarkdownHighlighter::isHorizontalRule(text) && !editingInside(block)) {
            next.append(QVariantMap{
                {QStringLiteral("kind"), QStringLiteral("rule")},
                {QStringLiteral("start"), block.position()},
                {QStringLiteral("end"), block.position() + text.size()}});
        }

        if (!editingInside(block)) {
            const QList<MarkdownHighlighter::ImageSpan> images =
                MarkdownHighlighter::imageSpans(text);
            int stack = 0;
            for (const MarkdownHighlighter::ImageSpan &image : images) {
                QVariantMap classified = classifyImageDestination(image.destination);
                const QSize display = imageDisplaySize(classified);
                classified.insert(QStringLiteral("kind"), QStringLiteral("image"));
                classified.insert(QStringLiteral("start"), block.position() + image.start);
                classified.insert(QStringLiteral("end"),
                                 block.position() + image.start + image.length);
                classified.insert(QStringLiteral("alt"), image.alt);
                classified.insert(QStringLiteral("displayWidth"), display.width());
                classified.insert(QStringLiteral("displayHeight"), display.height());
                classified.insert(QStringLiteral("stack"), stack);
                stack += display.height();
                next.append(classified);
            }
        }

        if (!text.trimmed().startsWith(QLatin1Char('>')))
            continue;
        if (block.previous().isValid()
                && block.previous().text().trimmed().startsWith(QLatin1Char('>'))
                && !inCode(block.previous()))
            continue;
        bool revealed = false;
        QList<QTextBlock> quoteRows;
        for (QTextBlock row = block; row.isValid()
             && row.text().trimmed().startsWith(QLatin1Char('>')) && !inCode(row);
             row = row.next()) {
            quoteRows.append(row);
            if (editingInside(row))
                revealed = true;
        }
        if (revealed)
            continue;
        static const QRegularExpression quoteMark(QStringLiteral(">"));
        for (const QTextBlock &row : quoteRows) {
            QVariantList bars;
            const int content = row.text().indexOf(QRegularExpression(QStringLiteral("[^>\\s]")));
            QRegularExpressionMatchIterator marks = quoteMark.globalMatch(row.text());
            while (marks.hasNext()) {
                const QRegularExpressionMatch mark = marks.next();
                if (content >= 0 && mark.capturedStart(0) >= content)
                    break;
                bars.append(row.position() + int(mark.capturedStart(0)));
            }
            next.append(QVariantMap{
                {QStringLiteral("kind"), QStringLiteral("quote")},
                {QStringLiteral("start"), row.position()},
                {QStringLiteral("end"), row.position() + row.text().size()},
                {QStringLiteral("bars"), bars}});
        }
    }

    for (QTextBlock block = first; block.isValid() && block.blockNumber() <= toBlock;) {
        if (!MarkdownHighlighter::isTableRow(block.text()) || inCode(block)) {
            block = block.next();
            continue;
        }
        QTextBlock start = block;
        QTextBlock end = block;
        bool separator = MarkdownHighlighter::isTableSeparator(block.text());
        bool raw = editingInside(block);
        while (end.next().isValid() && MarkdownHighlighter::isTableRow(end.next().text())
               && !inCode(end.next())) {
            end = end.next();
            separator = separator || MarkdownHighlighter::isTableSeparator(end.text());
            raw = raw || editingInside(end);
        }
        if (separator && !raw) {
            QTextBlock pipeRow = start;
            int pipeCount = 0;
            QVariantList rowStarts;
            int headerEnd = -1;
            for (QTextBlock row = start; row.isValid(); row = row.next()) {
                const MarkdownHighlighter::TableLine parsed =
                    MarkdownHighlighter::parseTableLine(row.text());
                if (parsed.hidden.size() > pipeCount) {
                    pipeCount = parsed.hidden.size();
                    pipeRow = row;
                }
                if (!MarkdownHighlighter::isTableSeparator(row.text()))
                    rowStarts.append(row.position());
                if (headerEnd < 0 && MarkdownHighlighter::isTableSeparator(row.text())
                        && row.previous().isValid()
                        && row.previous().position() >= start.position())
                    headerEnd = row.previous().position() + row.previous().text().size();
                if (row == end)
                    break;
            }
            QVariantList pipes;
            const MarkdownHighlighter::TableLine widest =
                MarkdownHighlighter::parseTableLine(pipeRow.text());
            for (const MarkdownHighlighter::Span &pipe : widest.hidden)
                pipes.append(pipeRow.position() + pipe.start);
            const QTextBlock after = end.next();
            const int below = after.isValid() ? after.position()
                                              : end.position() + end.length();
            next.append(QVariantMap{
                {QStringLiteral("kind"), QStringLiteral("table")},
                {QStringLiteral("start"), start.position()},
                {QStringLiteral("end"), end.position() + end.text().size()},
                {QStringLiteral("below"), below},
                {QStringLiteral("headerEnd"), headerEnd},
                {QStringLiteral("pipes"), pipes},
                {QStringLiteral("rows"), rowStarts}});
        }
        block = end.next();
    }

    for (QTextBlock block = first; block.isValid() && block.blockNumber() <= toBlock;) {
        const CodeLine line = lineOf(block);
        const bool indentedStart = line.inCode && line.indented
            && !(block.previous().isValid() && lineOf(block.previous()).inCode
                 && lineOf(block.previous()).indented);
        if (!line.opener && !indentedStart) {
            block = block.next();
            continue;
        }
        QTextBlock opening = block;
        QTextBlock end = block;
        bool editingFence = editingInside(block);
        for (QTextBlock row = block; row.isValid() && lineOf(row).inCode; row = row.next()) {
            if (row != block && lineOf(row).opener)
                break;
            end = row;
            if (editingInside(row))
                editingFence = true;
            if (lineOf(row).closer)
                break;
        }
        next.append(QVariantMap{
            {QStringLiteral("kind"), QStringLiteral("code")},
            {QStringLiteral("start"), opening.position()},
            {QStringLiteral("end"), end.position() + end.text().size()},
            {QStringLiteral("language"),
             m_highlighter ? m_highlighter->codeLanguage(opening) : QString()},
            {QStringLiteral("showLabel"), !editingFence}});
        block = end.next();
    }
    return next;
}

QVariantList Backend::imageDecorations() const {
    QVariantList images;
    if (!m_document)
        return images;
    const QVariantList all = decorationsForRange(0, m_document->blockCount() - 1);
    for (const QVariant &entry : all) {
        if (entry.toMap().value(QStringLiteral("kind")).toString() == QLatin1String("image"))
            images.append(entry);
    }
    return images;
}

void Backend::rebuildDecorations(int fromBlock, int toBlock) {
    if (!m_document || m_editingPosition < 0) {
        m_lastRestyleBlocks = 0;
        if (m_decorations.isEmpty())
            return;
        m_decorations.clear();
        emit decorationsChanged();
        return;
    }

    const bool full = fromBlock < 0 || toBlock < 0;
    int from = 0;
    int to = m_document->blockCount() - 1;
    if (!full) {
        from = fromBlock;
        to = toBlock;
        expandDecorationRange(from, to);
    }
    m_lastRestyleBlocks = to - from + 1;
    QVariantList generated = decorationsForRange(from, to);
    if (full) {
        if (generated == m_decorations)
            return;
        m_decorations = generated;
        emit decorationsChanged();
        return;
    }

    const QTextBlock first = m_document->findBlockByNumber(from);
    const QTextBlock last = m_document->findBlockByNumber(to);
    if (!first.isValid() || !last.isValid())
        return;
    const int spanStart = first.position();
    const int spanEnd = last.position() + last.length();
    bool refreshImages = false;
    for (const QVariant &entry : m_decorations) {
        if (entry.toMap().value(QStringLiteral("kind")).toString() == QLatin1String("image"))
            refreshImages = true;
    }
    const auto withoutImages = [](const QVariantList &list) {
        if (list.isEmpty())
            return list;
        QVariantList kept;
        kept.reserve(list.size());
        for (const QVariant &entry : list) {
            if (entry.toMap().value(QStringLiteral("kind")).toString() != QLatin1String("image"))
                kept.append(entry);
        }
        return kept;
    };
    if (refreshImages)
        generated = withoutImages(generated);
    QVariantList kept;
    kept.reserve(m_decorations.size());
    for (const QVariant &entry : m_decorations) {
        const QVariantMap map = entry.toMap();
        const int start = map.value(QStringLiteral("start")).toInt();
        const int end = map.value(QStringLiteral("end")).toInt();
        if (end <= spanStart || start >= spanEnd)
            kept.append(entry);
    }
    if (refreshImages)
        kept = withoutImages(kept);
    QVariantList merged;
    merged.reserve(kept.size() + generated.size());
    int keptIndex = 0;
    int generatedIndex = 0;
    while (keptIndex < kept.size() && generatedIndex < generated.size()) {
        const int keptStart = kept.at(keptIndex).toMap().value(QStringLiteral("start")).toInt();
        const int generatedStart =
            generated.at(generatedIndex).toMap().value(QStringLiteral("start")).toInt();
        if (keptStart <= generatedStart)
            merged.append(kept.at(keptIndex++));
        else
            merged.append(generated.at(generatedIndex++));
    }
    while (keptIndex < kept.size())
        merged.append(kept.at(keptIndex++));
    while (generatedIndex < generated.size())
        merged.append(generated.at(generatedIndex++));
    if (refreshImages) {
        const QVariantList images = imageDecorations();
        QVariantList withImages;
        withImages.reserve(merged.size() + images.size());
        int mergedIndex = 0;
        int imageIndex = 0;
        while (mergedIndex < merged.size() && imageIndex < images.size()) {
            const int mergedStart = merged.at(mergedIndex).toMap().value(QStringLiteral("start")).toInt();
            const int imageStart = images.at(imageIndex).toMap().value(QStringLiteral("start")).toInt();
            if (mergedStart <= imageStart)
                withImages.append(merged.at(mergedIndex++));
            else
                withImages.append(images.at(imageIndex++));
        }
        while (mergedIndex < merged.size())
            withImages.append(merged.at(mergedIndex++));
        while (imageIndex < images.size())
            withImages.append(images.at(imageIndex++));
        merged = withImages;
    }
    if (merged == m_decorations)
        return;
    m_decorations = merged;
    emit decorationsChanged();
}

QVariantMap Backend::windowGeometry() const {
    QSettings settings;
    return {{QStringLiteral("x"), settings.value(QStringLiteral("window/x"), -1)},
            {QStringLiteral("y"), settings.value(QStringLiteral("window/y"), -1)},
            {QStringLiteral("width"), settings.value(QStringLiteral("window/width"), 1280)},
            {QStringLiteral("height"), settings.value(QStringLiteral("window/height"), 820)},
            {QStringLiteral("maximized"), settings.value(QStringLiteral("window/maximized"), false)}};
}

void Backend::saveWindowGeometry(int x, int y, int width, int height, bool maximized) {
    QSettings settings;
    if (!maximized) {
        settings.setValue(QStringLiteral("window/x"), x);
        settings.setValue(QStringLiteral("window/y"), y);
        settings.setValue(QStringLiteral("window/width"), width);
        settings.setValue(QStringLiteral("window/height"), height);
    }
    settings.setValue(QStringLiteral("window/maximized"), maximized);
}

void Backend::loadDocumentText(const QString &text) {
    if (!m_document) {
        setStatus(QStringLiteral("Could not attach the Markdown renderer."));
        return;
    }

    m_loading = true;
    m_document->setPlainText(text);
    m_lastDocumentText = text;
    m_loading = false;

    applyDocumentTypography();
    m_wordCountTimer.stop();
    setWordCount(countWords(text));
}

void Backend::setFileUrl(const QUrl &url) {
    if (m_fileUrl == url)
        return;

    m_imageClassCache.clear();
    m_fileUrl = url;
    emit fileUrlChanged();
    watchCurrentFile();
}

void Backend::setModified(bool modified) {
    if (m_modified == modified)
        return;

    m_modified = modified;
    emit modifiedChanged();
}

void Backend::setStatus(const QString &status) {
    if (m_status == status)
        return;

    m_status = status;
    emit statusChanged();
}

void Backend::saveTo(const QUrl &url) {
    if (!url.isLocalFile()) {
        m_closeAfterSave = false;
        setStatus(QStringLiteral("Only local files can be saved."));
        return;
    }

    const QString targetName = QFileInfo(url.toLocalFile()).fileName();
    QSaveFile file(url.toLocalFile());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        m_closeAfterSave = false;
        setStatus(QStringLiteral("Could not save %1.").arg(targetName));
        return;
    }

    const QByteArray contents = currentDocumentText().toUtf8();
    file.write(contents);

    // QSaveFile commits by replacing the target. Stop watching the old inode
    // before that replacement so our own write is not classified as external.
    const QStringList watched = m_fileWatcher.files();
    if (!watched.isEmpty())
        m_fileWatcher.removePaths(watched);

    // commit() flushes, fsyncs, and atomically renames the temp file into place,
    // returning false (and leaving the original untouched) on any write error.
    if (!file.commit()) {
        watchCurrentFile();
        m_closeAfterSave = false;
        setStatus(QStringLiteral("Could not write %1.").arg(targetName));
        return;
    }

    const bool shouldClose = m_closeAfterSave;
    m_closeAfterSave = false;
    m_lastKnownFileContents = contents;
    m_hasKnownFileContents = true;
    setFileUrl(url);
    watchCurrentFile();
    QSettings().setValue(lastSaveDirectorySetting,
                         QFileInfo(url.toLocalFile()).absolutePath());
    setModified(false);
    setStatus(QStringLiteral("Saved %1").arg(fileName()));
    clearRecovery();
    emit saveSucceeded();

    if (shouldClose)
        emit closeAfterSave();
}

void Backend::scheduleRecovery() {
    m_recoveryTimer.start();
}

QString Backend::recoveryPath() const {
    return m_recoveryPath;
}

void Backend::writeRecovery() {
    if (!m_modified)
        return;
    const QString path = recoveryPath();
    if (path.isEmpty())
        return;
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return;
    const QJsonObject recovery{{QStringLiteral("fileUrl"), m_fileUrl.toString()},
                               {QStringLiteral("text"), currentDocumentText()}};
    file.write(QJsonDocument(recovery).toJson(QJsonDocument::Compact));
    file.commit();
}

void Backend::restoreRecovery() {
    QFile file(recoveryPath());
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QJsonDocument json = QJsonDocument::fromJson(file.readAll());
    if (!json.isObject() || !json.object().contains(QStringLiteral("text")))
        return;
    const QJsonObject recovery = json.object();
    loadDocumentText(recovery.value(QStringLiteral("text")).toString());
    const QUrl recoveredUrl(recovery.value(QStringLiteral("fileUrl")).toString());
    QFile diskFile(recoveredUrl.toLocalFile());
    if (recoveredUrl.isLocalFile() && diskFile.open(QIODevice::ReadOnly)) {
        m_lastKnownFileContents = diskFile.readAll();
        m_hasKnownFileContents = true;
    } else {
        m_lastKnownFileContents.clear();
        m_hasKnownFileContents = false;
    }
    setFileUrl(recoveredUrl);
    setModified(true);
    setStatus(QStringLiteral("Recovered unsaved changes"));
}

void Backend::clearRecovery() {
    m_recoveryTimer.stop();
    QFile::remove(recoveryPath());
}

void Backend::watchCurrentFile() {
    const QStringList watched = m_fileWatcher.files();
    if (!watched.isEmpty())
        m_fileWatcher.removePaths(watched);
    if (m_fileUrl.isLocalFile() && QFileInfo::exists(m_fileUrl.toLocalFile()))
        m_fileWatcher.addPath(m_fileUrl.toLocalFile());
}

void Backend::loadOmarchyTheme() {
    m_themeBackground = m_darkMode ? QStringLiteral("#101010") : QStringLiteral("#ffffff");
    m_themeForeground = m_darkMode ? QStringLiteral("#eeeeee") : QStringLiteral("#222324");
    m_themeAccent = m_darkMode ? QStringLiteral("#5584aa") : QStringLiteral("#2077b2");
    m_themeSelection = m_darkMode ? QStringLiteral("#186a9a") : QStringLiteral("#2077b2");

    const QString colorsPath = QDir::homePath()
        + QStringLiteral("/.local/state/omarchy/current/theme/colors.toml");
    QString themeMode;
    QFile file(colorsPath);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&file);
        while (!in.atEnd()) {
            const QString line = in.readLine().trimmed();
            if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
                continue;

            const int equals = line.indexOf(QLatin1Char('='));
            if (equals < 0)
                continue;

            const QString key = line.left(equals).trimmed();
            QString value = line.mid(equals + 1).trimmed();
            if (value.size() >= 2
                    && ((value.front() == QLatin1Char('"') && value.back() == QLatin1Char('"'))
                        || (value.front() == QLatin1Char('\'') && value.back() == QLatin1Char('\''))))
                value = value.mid(1, value.size() - 2);

            if (key == QStringLiteral("mode"))
                themeMode = value;
            else if (key == QStringLiteral("background"))
                m_themeBackground = value;
            else if (key == QStringLiteral("foreground"))
                m_themeForeground = value;
            else if (key == QStringLiteral("accent"))
                m_themeAccent = value;
            else if (key == QStringLiteral("selection"))
                m_themeSelection = value;
        }
    }

    bool themeModeKnown = false;
    bool themeIsDark = m_darkMode;
    if (themeMode == QStringLiteral("dark")) {
        themeIsDark = true;
        themeModeKnown = true;
    } else if (themeMode == QStringLiteral("light")) {
        themeIsDark = false;
        themeModeKnown = true;
    } else {
        const QColor background(m_themeBackground);
        if (background.isValid()) {
            const double luminance = 0.299 * background.redF()
                + 0.587 * background.greenF() + 0.114 * background.blueF();
            themeIsDark = luminance < 0.5;
            themeModeKnown = true;
        }
    }
    if (themeModeKnown && themeIsDark != m_darkMode) {
        m_darkMode = themeIsDark;
        emit darkModeChanged();
    }

    if (m_highlighter) {
        m_highlighter->setDarkMode(m_darkMode);
        m_highlighter->setColors(m_themeBackground, m_themeForeground, m_themeAccent);
    }

    emit themeColorsChanged();
}

void Backend::watchOmarchyTheme() {
    const QStringList watched = m_themeWatcher.files() + m_themeWatcher.directories();
    if (!watched.isEmpty())
        m_themeWatcher.removePaths(watched);

    const QString currentDir = QDir::homePath()
        + QStringLiteral("/.local/state/omarchy/current");
    const QString themeDir = currentDir + QStringLiteral("/theme");
    const QString colorsPath = themeDir + QStringLiteral("/colors.toml");

    if (QDir(currentDir).exists())
        m_themeWatcher.addPath(currentDir);
    if (QDir(themeDir).exists())
        m_themeWatcher.addPath(themeDir);
    if (QFile::exists(colorsPath))
        m_themeWatcher.addPath(colorsPath);
}

QUrl Backend::suggestedSaveUrl() const {
    if (m_fileUrl.isLocalFile())
        return m_fileUrl;

    const QString savedDirectory = QSettings().value(lastSaveDirectorySetting).toString();
    const QDir directory = savedDirectory.isEmpty() || !QDir(savedDirectory).exists()
        ? QDir::home()
        : QDir(savedDirectory);
    return QUrl::fromLocalFile(
        directory.filePath(suggestedFileName(currentDocumentText())));
}

QString Backend::currentDocumentText() const {
    return m_document ? m_document->toPlainText() : QString();
}

int Backend::countWords(const QString &text) {
    static const QRegularExpression wordRe(
        QStringLiteral("[\\p{L}\\p{N}]+(?:['-][\\p{L}\\p{N}]+)*"));
    int count = 0;
    QRegularExpressionMatchIterator it = wordRe.globalMatch(text);
    while (it.hasNext()) {
        it.next();
        ++count;
    }
    return count;
}

QString Backend::suggestedFileName(const QString &text) {
    QString name = text.section(QLatin1Char('\n'), 0, 0).trimmed();
    name.replace(QRegularExpression(QStringLiteral("[/\\x00-\\x1f\\x7f]")),
                 QStringLiteral("-"));
    name = name.left(120).trimmed();
    if (name.isEmpty() || name == QStringLiteral(".") || name == QStringLiteral(".."))
        name = QStringLiteral("Untitled");
    if (!name.endsWith(QStringLiteral(".md"), Qt::CaseInsensitive))
        name += QStringLiteral(".md");
    return name;
}

void Backend::setWordCount(int words) {
    if (m_wordCount == words)
        return;

    m_wordCount = words;
    emit wordCountChanged();
}

void Backend::refreshWordCount() {
    setWordCount(countWords(currentDocumentText()));
}

void Backend::scheduleWordCount() {
    m_wordCountTimer.start();
}

void Backend::applyBlockMetrics(QTextCursor &cursor, const QTextBlock &block) {
    QTextBlockFormat format = block.blockFormat();
    const QString text = block.text();
    const CodeLine code = m_highlighter ? m_highlighter->codeLine(block) : CodeLine{};
    // Fence and separator lines use a 1pt block font so a hidden character run
    // collapses. The code-block check comes first: a separator typed inside a
    // fence or an indented block keeps the code inset instead of the 1pt rule.
    QTextCharFormat blockChar;
    format.setLeftMargin(0);
    format.setRightMargin(0);
    format.setTopMargin(0);
    format.setBottomMargin(0);
    format.setNonBreakableLines(false);
    if (code.inCode) {
        if (code.opener || code.closer) {
            format.setLineHeight(100, QTextBlockFormat::ProportionalHeight);
            format.clearBackground();
            blockChar.setFontPointSize(1);
            if (code.opener)
                format.setTopMargin(MarkdownHighlighter::codeBlockInset);
        } else {
            format.setLineHeight(typoraLineHeightPercent, QTextBlockFormat::ProportionalHeight);
            format.setLeftMargin(MarkdownHighlighter::codeBlockInset);
            format.setRightMargin(MarkdownHighlighter::codeBlockInset);
            format.setBackground(m_darkMode ? QColor(QStringLiteral("#1c1a1a"))
                                            : QColor(QStringLiteral("#f8f8f8")));
        }
    } else if (MarkdownHighlighter::isTableSeparator(text)) {
        format.setLineHeight(100, QTextBlockFormat::ProportionalHeight);
        format.clearBackground();
        blockChar.setFontPointSize(1);
    } else {
        const QList<MarkdownHighlighter::ImageSpan> images =
            MarkdownHighlighter::imageSpans(text);
        if (!images.isEmpty()) {
            // The caret rect sits on the markdown, so the reserved gap has to
            // be below it and as tall as the pictures actually draw.
            int reserved = 0;
            for (const MarkdownHighlighter::ImageSpan &image : images)
                reserved += imageDisplaySize(classifyImageDestination(image.destination)).height();
            format.setLineHeight(100, QTextBlockFormat::ProportionalHeight);
            format.setBottomMargin(reserved);
            format.setNonBreakableLines(true);
            format.clearBackground();
        } else if (MarkdownHighlighter::isTableRow(text)) {
            format.setLineHeight(typoraLineHeightPercent, QTextBlockFormat::ProportionalHeight);
            format.setNonBreakableLines(true);
            format.clearBackground();
        } else {
            format.setLineHeight(typoraLineHeightPercent, QTextBlockFormat::ProportionalHeight);
            format.clearBackground();
        }
    }
    cursor.setPosition(block.position());
    cursor.setBlockCharFormat(blockChar);
    cursor.setBlockFormat(format);
}

void Backend::applyDocumentTypography() {
    if (!m_document)
        return;

    // A full pass is only used for freshly loaded or attached documents, so it
    // is safe to drop undo history here (re-enabling clears the stack anyway).
    const bool undoEnabled = m_document->isUndoRedoEnabled();
    m_document->setUndoRedoEnabled(false);

    m_formattingTypography = true;
    QTextCursor cursor(m_document);
    for (QTextBlock block = m_document->begin(); block.isValid(); block = block.next())
        applyBlockMetrics(cursor, block);
    m_formattingTypography = false;

    m_document->setUndoRedoEnabled(undoEnabled);

    m_formattedBlockCount = m_document->blockCount();
    rebuildDecorations();
}

void Backend::reapplyTypographyToChange(int fromBlock, int toBlock) {
    if (!m_document)
        return;

    int from = fromBlock;
    int to = toBlock;
    if (from < 0 || to < 0) {
        const int maxPos = qMax(0, m_document->characterCount() - 1);
        const int start = qBound(0, m_lastChangePos, maxPos);
        const int end = qBound(start, m_lastChangePos + qMax(0, m_lastChangeAdded), maxPos);
        const QTextBlock first = m_document->findBlock(start);
        const QTextBlock last = m_document->findBlock(end);
        from = first.isValid() ? first.blockNumber() : 0;
        to = last.isValid() ? last.blockNumber() : from;
        if (from > 0)
            --from;
        if (to + 1 < m_document->blockCount())
            ++to;
    }
    expandDecorationRange(from, to);

    m_formattingTypography = true;
    QTextCursor cursor(m_document);
    cursor.joinPreviousEditBlock();
    const QTextBlock last = m_document->findBlockByNumber(to);
    for (QTextBlock block = m_document->findBlockByNumber(from); block.isValid();
         block = block.next()) {
        applyBlockMetrics(cursor, block);
        if (block == last)
            break;
    }
    cursor.endEditBlock();
    m_formattingTypography = false;
}
