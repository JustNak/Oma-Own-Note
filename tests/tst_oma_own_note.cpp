#include <QAbstractTextDocumentLayout>
#include <QApplication>
#include <QtTest>

#include <cmath>

#include <QColor>
#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QQuickTextDocument>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTextDocument>
#include <QFont>
#include <QFontInfo>
#include <QFontMetricsF>
#include <QImage>
#include <QPainter>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTextBlock>
#include <QTextBlockFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextLayout>

#include "backend.h"
#include "markdownhighlighter.h"
#include "viewzoom.h"

class OmaOwnNoteTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        QVERIFY(m_settingsDirectory.isValid());
        QQuickStyle::setStyle(QStringLiteral("Material"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                           m_settingsDirectory.path());
        // The app registers these in main(). Without them the editor falls
        // back to whatever sans the runner has, and glyph widths are not the
        // ones the editor ships with.
        for (const char *face : {
                 "../fonts/iAWriterMonoS-Regular.ttf",
                 "../fonts/iAWriterMonoS-Italic.ttf",
                 "../fonts/iAWriterMonoS-Bold.ttf",
                 "../fonts/iAWriterMonoS-BoldItalic.ttf",
             }) {
            const QString path = QFINDTESTDATA(face);
            QVERIFY2(!path.isEmpty(), face);
            QVERIFY2(QFontDatabase::addApplicationFont(path) >= 0, qPrintable(path));
        }
    }

    void countsWords() {
        QCOMPARE(Backend::countWords(QStringLiteral("one two-three don't 42")), 4);
        QCOMPARE(Backend::countWords(QStringLiteral("你好 世界")), 2);
        QCOMPARE(Backend::countWords(QString()), 0);
    }

    void normalizesLinks() {
        QCOMPARE(Backend::normalizedLinkUrl(QStringLiteral("www.example.com/path")),
                 QStringLiteral("https://www.example.com/path"));
        QCOMPARE(Backend::normalizedLinkUrl(QStringLiteral("mailto:writer@example.com")),
                 QStringLiteral("mailto:writer@example.com"));
        QVERIFY(Backend::normalizedLinkUrl(QStringLiteral("example.com")).isEmpty());
        QVERIFY(Backend::normalizedLinkUrl(QStringLiteral("file:///tmp/private")).isEmpty());
    }

    void suggestsSafeNames() {
        QCOMPARE(Backend::suggestedFileName(QStringLiteral("My first draft\nBody")),
                 QStringLiteral("My first draft.md"));
        QCOMPARE(Backend::suggestedFileName(QStringLiteral("A/B")), QStringLiteral("A-B.md"));
        QCOMPARE(Backend::suggestedFileName(QString()), QStringLiteral("Untitled.md"));
        QCOMPARE(Backend::suggestedFileName(QStringLiteral("Already.md")),
                 QStringLiteral("Already.md"));
    }

    void findsInlineMarkdownRanges() {
        const auto markup = MarkdownHighlighter::inlineMarkup(
            QStringLiteral("**bold** and *italic* and [site](https://example.com)"));
        QCOMPARE(markup.size(), 3);
        QCOMPARE(markup.at(0).content.start, 2);
        QCOMPARE(markup.at(0).content.length, 4);
        QCOMPARE(markup.at(2).content.length, 4);
        QCOMPARE(markup.at(2).markers[0].length, 1);
    }

    void loadsCurrentOmarchyTheme() {
        QTemporaryDir homeDirectory;
        QVERIFY(homeDirectory.isValid());

        const QByteArray originalHome = qgetenv("HOME");
        struct HomeRestorer {
            QByteArray value;
            ~HomeRestorer() { qputenv("HOME", value); }
        } restoreHome{originalHome};
        QVERIFY(qputenv("HOME", homeDirectory.path().toUtf8()));

        const QString themeDirectory = homeDirectory.path()
            + QStringLiteral("/.local/state/omarchy/current/theme");
        QVERIFY(QDir().mkpath(themeDirectory));

        QFile colorsFile(themeDirectory + QStringLiteral("/colors.toml"));
        QVERIFY(colorsFile.open(QIODevice::WriteOnly | QIODevice::Text));
        const QByteArray palette(
            "mode = \"light\"\n"
            "accent = \"#112233\"\n"
            "selection = \"#445566\"\n"
            "background = \"#fefefe\"\n"
            "foreground = \"#101010\"\n");
        QCOMPARE(colorsFile.write(palette), qint64(palette.size()));
        colorsFile.close();

        Backend backend;
        QCOMPARE(backend.themeBackground(), QStringLiteral("#fefefe"));
        QCOMPARE(backend.themeForeground(), QStringLiteral("#101010"));
        QCOMPARE(backend.themeAccent(), QStringLiteral("#112233"));
        QCOMPARE(backend.themeSelection(), QStringLiteral("#445566"));
        QVERIFY(!backend.darkMode());
    }

    void ignoresFileWatcherEventsForSavedContents() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());

        const QString path = directory.filePath(QStringLiteral("first-save.md"));
        Backend backend;
        QSignalSpy externalChangeSpy(&backend, &Backend::externalChangeDetected);

        backend.saveAs(QUrl::fromLocalFile(path));
        QVERIFY(QFileInfo::exists(path));

        QFile sameContents(path);
        QVERIFY(sameContents.open(QIODevice::WriteOnly | QIODevice::Truncate));
        sameContents.close();
        QTest::qWait(100);
        QCOMPARE(externalChangeSpy.count(), 0);

        QFile changedContents(path);
        QVERIFY(changedContents.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QCOMPARE(changedContents.write("changed elsewhere"), qint64(17));
        changedContents.close();
        QTRY_COMPARE(externalChangeSpy.count(), 1);
    }

    void keepsCursorAndSelectionStableAcrossInsertions() {
        const QString mutationsPath = QFINDTESTDATA("../src/EditorMutations.js");
        QVERIFY(!mutationsPath.isEmpty());

        QQmlEngine engine;
        QQmlComponent component(&engine);
        const QByteArray harness = R"QML(
            import QtQuick
            import "EditorMutations.js" as EditorMutations

            TextEdit {
                property string insertionText
                property int insertionCursor
                property string wrappedText
                property int wrappedSelectionStart
                property int wrappedSelectionEnd

                Component.onCompleted: {
                    text = "alpha omega";
                    cursorPosition = 5;
                    EditorMutations.replaceRange(this, 5, 5, "one\r\ntwo");
                    insertionText = text;
                    insertionCursor = cursorPosition;

                    text = "alpha beta omega";
                    select(6, 10);
                    EditorMutations.replaceRange(this, selectionStart, selectionEnd,
                                                 "**beta**", 2, 6);
                    wrappedText = text;
                    wrappedSelectionStart = selectionStart;
                    wrappedSelectionEnd = selectionEnd;
                }
            }
        )QML";
        const QUrl harnessUrl = QUrl::fromLocalFile(
            QFileInfo(mutationsPath).absolutePath() + QStringLiteral("/MutationHarness.qml"));
        component.setData(harness, harnessUrl);
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> editor(component.create());
        QVERIFY2(editor, qPrintable(component.errorString()));

        QCOMPARE(editor->property("insertionText").toString(),
                 QStringLiteral("alphaone\ntwo omega"));
        QCOMPARE(editor->property("insertionCursor").toInt(), 12);
        QCOMPARE(editor->property("wrappedText").toString(),
                 QStringLiteral("alpha **beta** omega"));
        QCOMPARE(editor->property("wrappedSelectionStart").toInt(), 8);
        QCOMPARE(editor->property("wrappedSelectionEnd").toInt(), 12);
    }

    void insertsMarkdownBlocksAndTables() {
        const QString harnessPath = QFINDTESTDATA("InsertHarness.qml");
        QVERIFY(!harnessPath.isEmpty());

        QQmlEngine engine;
        QQmlComponent component(&engine, QUrl::fromLocalFile(harnessPath));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> editor(component.create());
        QVERIFY2(editor, qPrintable(component.errorString()));

        QCOMPARE(editor->property("headingText").toString(), QStringLiteral("# "));
        QCOMPARE(editor->property("headingCursor").toInt(), 2);
        QCOMPARE(editor->property("selectedHeading").toString(), QStringLiteral("# hello"));
        QCOMPARE(editor->property("midLineHeading").toString(), QStringLiteral("hello\n\n# "));
        QCOMPARE(editor->property("pipeTableSample").toString(),
                 QStringLiteral("|     |     |     |\n| --- | --- | --- |\n|     |     |     |"));
        QCOMPARE(editor->property("tableText").toString(),
                 QStringLiteral("|     |     |     |\n| --- | --- | --- |\n|     |     |     |"));
        QCOMPARE(editor->property("tableCursor").toInt(), 2);
        QCOMPARE(editor->property("convertedText").toString(),
                 QStringLiteral("| a   | b   |\n| --- | --- |\n| 1   | 2   |"));
        QCOMPARE(editor->property("fenceText").toString(), QStringLiteral("```\n\n```"));
        QCOMPARE(editor->property("fenceCursor").toInt(), 3);
        QCOMPARE(editor->property("imageText").toString(), QStringLiteral("![Cat](cat.png)"));
        QCOMPARE(editor->property("imageSelStart").toInt(), 2);
        QCOMPARE(editor->property("imageSelEnd").toInt(), 5);
        QCOMPARE(editor->property("dateText").toString(), QStringLiteral("2026-08-31"));
        QCOMPARE(editor->property("relativeImage").toString(), QStringLiteral("img/cat.png"));
        QVERIFY(editor->property("movedInTable").toBool());
        QCOMPARE(editor->property("nextCellStart").toInt(), 12);
        QCOMPARE(editor->property("dividerText").toString(), QStringLiteral("---\n"));
        QCOMPARE(editor->property("dividerCursor").toInt(), 4);
        QCOMPARE(editor->property("dividerMidText").toString(),
                 QStringLiteral("hello\n\n---\n\nworld"));
        QCOMPARE(editor->property("dividerMidCursor").toInt(), 11);
        QCOMPARE(editor->property("escapedImage").toString(),
                 QStringLiteral("![Cat \\[1\\]](<photo (2).png>)"));
        QVERIFY(editor->property("oneColumnMoved").toBool());
        QCOMPARE(editor->property("oneColumnCursor").toInt(), 22);
    }

    void insertPaletteOpensOnCtrlTab() {
        const QString mainQmlPath = QFINDTESTDATA("../src/Main.qml");
        QVERIFY(!mainQmlPath.isEmpty());

        Backend backend;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        QQmlComponent component(&engine, QUrl::fromLocalFile(mainQmlPath));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));

        QObject *editor = window->findChild<QObject *>(QStringLiteral("sourceEditor"));
        QVERIFY(editor);
        QVERIFY(QMetaObject::invokeMethod(editor, "forceActiveFocus"));

        auto *quickWindow = qobject_cast<QQuickWindow *>(window.data());
        QVERIFY(quickWindow);
        QTest::keyClick(quickWindow, Qt::Key_Tab, Qt::ControlModifier);
        QTRY_COMPARE(window->property("insertPaletteOpened").toBool(), true);

        QObject *palette = window->findChild<QObject *>(QStringLiteral("insertPalette"));
        QVERIFY(palette);
        QVERIFY(QMetaObject::invokeMethod(window.data(), "applyInsertKind",
                                          Q_ARG(QVariant, QVariant(QStringLiteral("heading1"))),
                                          Q_ARG(QVariant, QVariantMap())));
        QTRY_COMPARE(editor->property("text").toString(), QStringLiteral("# "));
        QVERIFY(QMetaObject::invokeMethod(window.data(), "closeInsertPalette"));

        QVERIFY(QMetaObject::invokeMethod(window.data(), "openInsertPalette"));
        QTRY_COMPARE(window->property("insertPaletteOpened").toBool(), true);
        QCOMPARE(palette->property("mode").toString(), QStringLiteral("root"));
        QCOMPARE(palette->property("choiceCount").toInt(), 5);
        QTest::keyClick(quickWindow, Qt::Key_Escape);
        QTRY_COMPARE(window->property("insertPaletteOpened").toBool(), false);
        QTRY_VERIFY(editor->property("activeFocus").toBool());
    }

    void insertPaletteDrillsIntoCategories() {
        const QString mainQmlPath = QFINDTESTDATA("../src/Main.qml");
        QVERIFY(!mainQmlPath.isEmpty());

        Backend backend;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        QQmlComponent component(&engine, QUrl::fromLocalFile(mainQmlPath));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));

        QVERIFY(QMetaObject::invokeMethod(window.data(), "openInsertPalette"));
        QTRY_COMPARE(window->property("insertPaletteOpened").toBool(), true);

        QObject *palette = window->findChild<QObject *>(QStringLiteral("insertPalette"));
        QObject *filter = window->findChild<QObject *>(QStringLiteral("insertPaletteFilter"));
        QVERIFY(palette);
        QVERIFY(filter);
        QCOMPARE(palette->property("mode").toString(), QStringLiteral("root"));
        QCOMPARE(palette->property("choiceCount").toInt(), 5);

        auto *quickWindow = qobject_cast<QQuickWindow *>(window.data());
        QVERIFY(quickWindow);
        QTest::keyClick(quickWindow, Qt::Key_Return);
        QTRY_COMPARE(palette->property("mode").toString(), QStringLiteral("category"));
        QCOMPARE(palette->property("activeSection").toString(), QStringLiteral("Structure"));
        QCOMPARE(palette->property("choiceCount").toInt(), 5);

        QTest::keyClick(quickWindow, Qt::Key_Escape);
        QTRY_COMPARE(palette->property("mode").toString(), QStringLiteral("root"));

        filter->setProperty("text", QStringLiteral("table"));
        QTRY_COMPARE(palette->property("mode").toString(), QStringLiteral("search"));
        QCOMPARE(palette->property("choiceCount").toInt(), 1);
        QCOMPARE(palette->property("previewText").toString(), QStringLiteral("3 × 3"));

        QTest::keyClick(quickWindow, Qt::Key_Return);
        QTRY_COMPARE(palette->property("mode").toString(), QStringLiteral("table"));
        QCOMPARE(palette->property("previewText").toString(), QStringLiteral("3 × 3"));

        QTest::keyClick(quickWindow, Qt::Key_Escape);
        QTRY_COMPARE(palette->property("mode").toString(), QStringLiteral("search"));

        QTest::keyClick(quickWindow, Qt::Key_Escape);
        QTRY_COMPARE(palette->property("mode").toString(), QStringLiteral("root"));
        QVERIFY(QMetaObject::invokeMethod(window.data(), "closeInsertPalette"));
    }

    void highlightsTablesTasksAndFences() {
        QTextDocument document;
        document.setPlainText(QStringLiteral(
            "| A | B |\n"
            "| --- | --- |\n"
            "| 1 | 2 |\n"
            "\n"
            "- [ ] task\n"
            "\n"
            "```\n"
            "code\n"
            "```\n"));
        MarkdownHighlighter highlighter(&document);
        highlighter.rehighlight();

        const auto formatAt = [&document](int position) {
            const QTextBlock block = document.findBlock(position);
            const int local = position - block.position();
            QTextCharFormat format;
            const auto ranges = block.layout()->formats();
            for (const QTextLayout::FormatRange &range : ranges) {
                if (local >= range.start && local < range.start + range.length)
                    format = range.format;
            }
            return format;
        };

        QVERIFY(formatAt(2).fontWeight() >= QFont::Bold);
        QVERIFY(formatAt(2).background().style() != Qt::NoBrush);
        QVERIFY(qFuzzyIsNull(formatAt(0).fontPointSize()) || formatAt(0).fontPointSize() > 8);
        QCOMPARE(formatAt(0).foreground().color(), QColor(QStringLiteral("#4f525a")));

        const int bodyDigit = document.toPlainText().indexOf(QLatin1Char('1'));
        QVERIFY(bodyDigit >= 0);
        QVERIFY(formatAt(bodyDigit).background().style() != Qt::NoBrush);

        const int separatorDash = document.toPlainText().indexOf(QStringLiteral("---"));
        QVERIFY(separatorDash >= 0);
        QCOMPARE(formatAt(separatorDash).background().color(),
                 QColor(QStringLiteral("#4f525a")));

        const int taskBox = document.toPlainText().indexOf(QStringLiteral("[ ]"));
        QVERIFY(taskBox >= 0);
        QCOMPARE(formatAt(taskBox).foreground().color(),
                 QColor(QStringLiteral("#4f525a")));

        const int codePos = document.toPlainText().indexOf(QStringLiteral("code"));
        QVERIFY(codePos >= 0);
        QVERIFY(formatAt(codePos).background().style() != Qt::NoBrush);
    }

    void tableGridKeepsColumnGutters() {
        QTextDocument document;
        QFont font(QStringLiteral("monospace"));
        font.setStyleHint(QFont::Monospace);
        font.setFixedPitch(true);
        font.setPixelSize(16);
        document.setDefaultFont(font);
        document.setPlainText(QStringLiteral(
            "| asd | asd | asd |\n"
            "| --- | --- | --- |\n"
            "|     |     |     |\n"));
        document.setTextWidth(2000);

        MarkdownHighlighter highlighter(&document);
        highlighter.setColors(QStringLiteral("#1a2744"), QStringLiteral("#eeeeee"),
                              QStringLiteral("#5584aa"));
        highlighter.rehighlight();

        QTextCursor cursor(&document);
        for (QTextBlock block = document.begin(); block.isValid(); block = block.next()) {
            QTextBlockFormat format = block.blockFormat();
            if (MarkdownHighlighter::isTableSeparator(block.text())) {
                format.setLineHeight(MarkdownHighlighter::tableSeparatorLineHeight,
                                     QTextBlockFormat::FixedHeight);
            } else {
                format.setLineHeight(140, QTextBlockFormat::ProportionalHeight);
            }
            cursor.setPosition(block.position());
            cursor.setBlockFormat(format);
        }

        QImage surface(1200, 200, QImage::Format_ARGB32_Premultiplied);
        surface.fill(QColor(QStringLiteral("#1a2744")));
        QPainter painter(&surface);
        document.drawContents(&painter);
        painter.end();

        const auto formatAt = [&document](int position) {
            const QTextBlock block = document.findBlock(position);
            const int local = position - block.position();
            QTextCharFormat format;
            const auto ranges = block.layout()->formats();
            for (const QTextLayout::FormatRange &range : ranges) {
                if (local >= range.start && local < range.start + range.length)
                    format = range.format;
            }
            return format;
        };

        const QTextBlock header = document.begin();
        QVERIFY(header.isValid());
        QVERIFY(header.layout());
        QVERIFY(header.layout()->lineCount() > 0);
        const QTextLine headerLine = header.layout()->lineAt(0);
        const qreal pipeWidth = headerLine.cursorToX(1) - headerLine.cursorToX(0);
        const qreal em = QFontMetricsF(font).horizontalAdvance(QLatin1Char('|'));
        QVERIFY2(pipeWidth > em * 0.6,
                 qPrintable(QStringLiteral("pipe width %1 vs em %2")
                                .arg(pipeWidth).arg(em)));
        QVERIFY(qFuzzyIsNull(formatAt(0).fontPointSize()) || formatAt(0).fontPointSize() > 8);

        const QColor headerFill = formatAt(2).background().color();
        QVERIFY(headerFill.isValid());
        QVERIFY(headerFill != QColor(QStringLiteral("#1c1c1a")));
        QCOMPARE(formatAt(0).background().style(), Qt::NoBrush);

        const QString headerText = header.text();
        int firstAsd = headerText.indexOf(QStringLiteral("asd"));
        int secondAsd = headerText.indexOf(QStringLiteral("asd"), firstAsd + 3);
        QVERIFY(firstAsd >= 0);
        QVERIFY(secondAsd > firstAsd);
        const int gutterPipe = headerText.indexOf(QLatin1Char('|'), firstAsd);
        QVERIFY(gutterPipe > firstAsd);
        QVERIFY(gutterPipe < secondAsd);
        QCOMPARE(formatAt(header.position() + gutterPipe).foreground().color(),
                 QColor(QStringLiteral("#4f525a")));

        const QTextBlock separator = header.next();
        QVERIFY(separator.isValid());
        QCOMPARE(int(separator.blockFormat().lineHeightType()),
                 int(QTextBlockFormat::FixedHeight));
        QCOMPARE(separator.blockFormat().lineHeight(),
                 MarkdownHighlighter::tableSeparatorLineHeight);

        const QTextBlock body = separator.next();
        QVERIFY(body.isValid());
        QVERIFY(body.layout());
        QVERIFY(body.layout()->lineCount() > 0);
        const qreal headerWidth = headerLine.naturalTextWidth();
        const qreal bodyWidth = body.layout()->lineAt(0).naturalTextWidth();
        QVERIFY2(qAbs(headerWidth - bodyWidth) < em * 3,
                 qPrintable(QStringLiteral("header %1 vs body %2")
                                .arg(headerWidth).arg(bodyWidth)));

        const auto parsed = MarkdownHighlighter::parseTableLine(headerText);
        QCOMPARE(parsed.hidden.size(), 4);
    }

    void tabMovesBetweenTableCells() {
        const QString mainQmlPath = QFINDTESTDATA("../src/Main.qml");
        QVERIFY(!mainQmlPath.isEmpty());

        Backend backend;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        QQmlComponent component(&engine, QUrl::fromLocalFile(mainQmlPath));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));

        QObject *editor = window->findChild<QObject *>(QStringLiteral("sourceEditor"));
        QVERIFY(editor);
        editor->setProperty("text", QStringLiteral("|  |  |\n| --- | --- |\n|  |  |"));
        editor->setProperty("cursorPosition", 2);
        QVERIFY(QMetaObject::invokeMethod(editor, "forceActiveFocus"));

        auto *quickWindow = qobject_cast<QQuickWindow *>(window.data());
        QVERIFY(quickWindow);
        QTest::keyClick(quickWindow, Qt::Key_Tab);
        QCOMPARE(editor->property("text").toString(),
                 QStringLiteral("|     |     |\n| --- | --- |\n|     |     |"));
        QCOMPARE(editor->property("cursorPosition").toInt(), 12);
    }

    void savesAndOpensFromFooterButtons() {
        const QString mainQmlPath = QFINDTESTDATA("../src/Main.qml");
        QVERIFY(!mainQmlPath.isEmpty());

        Backend backend;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        QQmlComponent component(&engine, QUrl::fromLocalFile(mainQmlPath));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));
        QVERIFY(window->property("title").toString().endsWith(QStringLiteral("Oma Own Note")));

        QVERIFY(window->findChild<QObject *>(QStringLiteral("sourceEditor")));
        QVERIFY(!window->findChild<QObject *>(QStringLiteral("renderedPreview")));
        QVERIFY(!window->findChild<QObject *>(QStringLiteral("modeToggle")));

        QObject *saveButton = window->findChild<QObject *>(QStringLiteral("saveButton"));
        QObject *openButton = window->findChild<QObject *>(QStringLiteral("openButton"));
        QVERIFY(saveButton);
        QVERIFY(openButton);

        QSignalSpy saveDialogSpy(&backend, &Backend::saveDialogRequested);
        QVERIFY(QMetaObject::invokeMethod(saveButton, "clicked"));
        QCOMPARE(saveDialogSpy.count(), 1);

        QSignalSpy openDialogSpy(&backend, &Backend::openDialogRequested);
        QVERIFY(QMetaObject::invokeMethod(openButton, "clicked"));
        QCOMPARE(openDialogSpy.count(), 1);
    }

    void zoomSnapsAndClamps() {
        QCOMPARE(ViewZoom::fromPercent(100).factor(), 1.0);
        QCOMPARE(ViewZoom::fromPercent(137).percent(), 140);
        QCOMPARE(ViewZoom::fromPercent(0).percent(), 50);
        QCOMPARE(ViewZoom::fromPercent(900).percent(), 300);
        QCOMPARE(ViewZoom::fromPercent(100).stepped(1).percent(), 110);
        QCOMPARE(ViewZoom::fromPercent(300).stepped(1).percent(), 300);
        QCOMPARE(ViewZoom::fromPercent(50).stepped(-1).percent(), 50);
        QCOMPARE(ViewZoom::fromPercent(150).stepped(-20).percent(), 50);
        QCOMPARE(ViewZoom{}.percent(), 100);
    }

    void zoomPersistsAcrossBackendInstances() {
        Backend first;
        first.zoomIn();
        QCOMPARE(first.zoomFactor(), 1.1);
        Backend second;
        QCOMPARE(second.zoomFactor(), 1.1);
        second.resetZoom();
        QCOMPARE(second.zoomFactor(), 1.0);
    }

    void zoomToPercentSnapsAndClamps() {
        Backend first;
        first.zoomToPercent(137);
        QCOMPARE(first.zoomFactor(), 1.4);
        first.zoomToPercent(0);
        QCOMPARE(first.zoomFactor(), 0.5);
        first.zoomToPercent(900);
        QCOMPARE(first.zoomFactor(), 3.0);
        first.zoomToPercent(100);
        QCOMPARE(first.zoomFactor(), 1.0);

        first.zoomToPercent(137);
        QCOMPARE(first.zoomFactor(), 1.4);
        Backend second;
        QCOMPARE(second.zoomFactor(), 1.4);
        second.resetZoom();
        QCOMPARE(second.zoomFactor(), 1.0);
    }

    void scalesTextWithDesktopTextSize() {
        const QString mainQmlPath = QFINDTESTDATA("../src/Main.qml");
        QVERIFY(!mainQmlPath.isEmpty());

        Backend backend;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        QQmlComponent component(&engine, QUrl::fromLocalFile(mainQmlPath));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));

        QObject *editor = window->findChild<QObject *>(QStringLiteral("sourceEditor"));
        QVERIFY(editor);
        QCOMPARE(editor->property("font").value<QFont>().pixelSize(), 20);

        // `omarchy display text size 16` sets the GNOME factor to 16/12.
        backend.setTextScale(16.0 / 12.0);
        QCOMPARE(window->property("editorFontPixelSize").toInt(), 27);
        QCOMPARE(editor->property("font").value<QFont>().pixelSize(), 27);

        backend.setTextScale(9.0 / 12.0);
        QCOMPARE(window->property("editorFontPixelSize").toInt(), 15);
        QCOMPARE(editor->property("font").value<QFont>().pixelSize(), 15);
    }

    void canvasZoomDoesNotChangeEditorPixelSize() {
        const QString mainQmlPath = QFINDTESTDATA("../src/Main.qml");
        QVERIFY(!mainQmlPath.isEmpty());

        Backend backend;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        QQmlComponent component(&engine, QUrl::fromLocalFile(mainQmlPath));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));

        QObject *editor = window->findChild<QObject *>(QStringLiteral("sourceEditor"));
        QObject *canvas = window->findChild<QObject *>(QStringLiteral("editorCanvas"));
        QVERIFY(editor);
        QVERIFY(canvas);
        QCOMPARE(editor->property("font").value<QFont>().pixelSize(), 20);
        QCOMPARE(canvas->property("scale").toReal(), 1.0);

        auto *quickWindow = qobject_cast<QQuickWindow *>(window.data());
        QVERIFY(quickWindow);
        QTest::keyClick(quickWindow, Qt::Key_Equal, Qt::ControlModifier);
        QTRY_COMPARE(backend.zoomFactor(), 1.1);
        QCOMPARE(editor->property("font").value<QFont>().pixelSize(), 20);
        QCOMPARE(canvas->property("scale").toReal(), 1.1);

        QTest::keyClick(quickWindow, Qt::Key_Minus, Qt::ControlModifier);
        QTRY_COMPARE(backend.zoomFactor(), 1.0);
        QCOMPARE(canvas->property("scale").toReal(), 1.0);

        QTest::keyClick(quickWindow, Qt::Key_Equal, Qt::ControlModifier);
        QTRY_COMPARE(backend.zoomFactor(), 1.1);
        QTest::keyClick(quickWindow, Qt::Key_0, Qt::ControlModifier);
        QTRY_COMPARE(backend.zoomFactor(), 1.0);
        QCOMPARE(canvas->property("scale").toReal(), 1.0);

        QVERIFY(QMetaObject::invokeMethod(window.data(), "zoomIn"));
        QCOMPARE(backend.zoomFactor(), 1.1);
        QCOMPARE(editor->property("font").value<QFont>().pixelSize(), 20);
        QCOMPARE(canvas->property("scale").toReal(), 1.1);

        backend.setTextScale(16.0 / 12.0);
        QCOMPARE(window->property("editorFontPixelSize").toInt(), 27);
        QCOMPARE(editor->property("font").value<QFont>().pixelSize(), 27);
        QCOMPARE(canvas->property("scale").toReal(), 1.1);

        QObject *wordCount = window->findChild<QObject *>(QStringLiteral("wordCountLabel"));
        QVERIFY(wordCount);
        QCOMPARE(wordCount->property("font").value<QFont>().pixelSize(),
                 qRound(11 * 16.0 / 12.0));

        backend.resetZoom();
        QCOMPARE(backend.zoomFactor(), 1.0);
    }

    void canvasZoomKeepsColumnOrigin() {
        const QString mainQmlPath = QFINDTESTDATA("../src/Main.qml");
        QVERIFY(!mainQmlPath.isEmpty());

        Backend backend;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        QQmlComponent component(&engine, QUrl::fromLocalFile(mainQmlPath));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));

        QObject *canvas = window->findChild<QObject *>(QStringLiteral("editorCanvas"));
        QObject *flick = window->findChild<QObject *>(QStringLiteral("editorFlick"));
        QVERIFY(canvas);
        QVERIFY(flick);

        const qreal originX = canvas->property("x").toReal();
        QVERIFY(originX > 0);

        for (int i = 0; i < 5; ++i)
            backend.zoomOut();
        QCOMPARE(backend.zoomFactor(), 0.5);
        QCOMPARE(canvas->property("x").toReal(), originX);

        backend.resetZoom();
        for (int i = 0; i < 20; ++i)
            backend.zoomIn();
        QCOMPARE(backend.zoomFactor(), 3.0);
        QCOMPARE(canvas->property("x").toReal(), originX);
        const qreal visualRight = originX
            + canvas->property("width").toReal() * canvas->property("scale").toReal();
        QVERIFY(flick->property("contentWidth").toReal() >= visualRight);

        backend.resetZoom();
    }

    void canvasZoomKeepsVisualColumn() {
        const QString mainQmlPath = QFINDTESTDATA("../src/Main.qml");
        QVERIFY(!mainQmlPath.isEmpty());

        Backend backend;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        QQmlComponent component(&engine, QUrl::fromLocalFile(mainQmlPath));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));

        QObject *editor = window->findChild<QObject *>(QStringLiteral("sourceEditor"));
        QObject *canvas = window->findChild<QObject *>(QStringLiteral("editorCanvas"));
        QObject *flick = window->findChild<QObject *>(QStringLiteral("editorFlick"));
        QVERIFY(editor);
        QVERIFY(canvas);
        QVERIFY(flick);

        const qreal columnWidth = editor->property("width").toReal();
        const qreal originX = canvas->property("x").toReal();
        QVERIFY(columnWidth > 0);
        QVERIFY(originX > 0);

        backend.zoomToPercent(70);
        QCOMPARE(backend.zoomFactor(), 0.7);
        QCOMPARE(canvas->property("x").toReal(), originX);
        QCOMPARE(editor->property("font").value<QFont>().pixelSize(), 20);
        QVERIFY(editor->property("width").toReal() > columnWidth);
        const qreal visualAt70 = canvas->property("width").toReal()
            * canvas->property("scale").toReal();
        QVERIFY2(qAbs(visualAt70 - columnWidth) < 2.0,
                 qPrintable(QStringLiteral("70% visual %1 vs column %2")
                            .arg(visualAt70)
                            .arg(columnWidth)));

        backend.zoomToPercent(300);
        QCOMPARE(backend.zoomFactor(), 3.0);
        QCOMPARE(canvas->property("x").toReal(), originX);
        QCOMPARE(editor->property("font").value<QFont>().pixelSize(), 20);
        QVERIFY(editor->property("width").toReal() < columnWidth);
        const qreal visualAt300 = canvas->property("width").toReal()
            * canvas->property("scale").toReal();
        QVERIFY2(qAbs(visualAt300 - columnWidth) < 2.0,
                 qPrintable(QStringLiteral("300% visual %1 vs column %2")
                            .arg(visualAt300)
                            .arg(columnWidth)));
        QVERIFY(flick->property("contentWidth").toReal()
                <= flick->property("width").toReal() + 1);

        backend.resetZoom();
        QCOMPARE(editor->property("width").toReal(), columnWidth);
    }

    void shortDocumentDoesNotPhantomScroll() {
        const QString mainQmlPath = QFINDTESTDATA("../src/Main.qml");
        QVERIFY(!mainQmlPath.isEmpty());

        Backend backend;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        QQmlComponent component(&engine, QUrl::fromLocalFile(mainQmlPath));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));

        QObject *flick = window->findChild<QObject *>(QStringLiteral("editorFlick"));
        QVERIFY(flick);
        QVERIFY(flick->property("contentHeight").toReal()
                <= flick->property("height").toReal() + 1);

        backend.resetZoom();
    }

    void zoomPercentLabelTracksCanvasZoom() {
        const QString mainQmlPath = QFINDTESTDATA("../src/Main.qml");
        QVERIFY(!mainQmlPath.isEmpty());

        Backend backend;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        QQmlComponent component(&engine, QUrl::fromLocalFile(mainQmlPath));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));

        QObject *zoomLabel = window->findChild<QObject *>(QStringLiteral("zoomPercentLabel"));
        QVERIFY(zoomLabel);
        QCOMPARE(zoomLabel->property("visible").toBool(), false);

        backend.zoomIn();
        QCOMPARE(zoomLabel->property("visible").toBool(), true);
        QCOMPARE(zoomLabel->property("text").toString(), QStringLiteral("110%"));

        backend.resetZoom();
        QCOMPARE(zoomLabel->property("visible").toBool(), false);

        backend.zoomOut();
        QCOMPARE(zoomLabel->property("visible").toBool(), true);
        QCOMPARE(zoomLabel->property("text").toString(), QStringLiteral("90%"));

        backend.resetZoom();
    }

    void zoomChordsKeepCaretInViewAfterWrapReflow() {
        const QString mainQmlPath = QFINDTESTDATA("../src/Main.qml");
        QVERIFY(!mainQmlPath.isEmpty());

        Backend backend;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        QQmlComponent component(&engine, QUrl::fromLocalFile(mainQmlPath));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));

        QObject *editor = window->findChild<QObject *>(QStringLiteral("sourceEditor"));
        QObject *flick = window->findChild<QObject *>(QStringLiteral("editorFlick"));
        QVERIFY(editor);
        QVERIFY(flick);

        QString paragraph;
        for (int i = 0; i < 400; ++i) {
            if (i)
                paragraph += QLatin1Char(' ');
            paragraph += QStringLiteral("word");
        }
        editor->setProperty("text", paragraph.repeated(8));
        QVERIFY(QMetaObject::invokeMethod(editor, "forceActiveFocus"));
        editor->setProperty("cursorPosition", editor->property("text").toString().size() / 2);

        QTRY_VERIFY(editor->property("cursorRectangle").toRectF().y() > 0);

        const qreal beforeY = editor->property("cursorRectangle").toRectF().y();
        for (int i = 0; i < 20; ++i)
            QVERIFY(QMetaObject::invokeMethod(window.data(), "zoomIn"));
        QCOMPARE(backend.zoomFactor(), 3.0);

        const QRectF caret = editor->property("cursorRectangle").toRectF();
        QVERIFY2(caret.y() > beforeY + 20,
                 qPrintable(QStringLiteral("wrap did not reflow caret y %1 -> %2")
                            .arg(beforeY)
                            .arg(caret.y())));

        const qreal zoom = backend.zoomFactor();
        const qreal canvasY = window->findChild<QObject *>(QStringLiteral("editorCanvas"))
                                  ->property("y").toReal();
        const qreal caretTop = canvasY + caret.y() * zoom;
        const qreal caretBottom = canvasY + (caret.y() + caret.height()) * zoom;
        const qreal contentY = flick->property("contentY").toReal();
        const qreal viewHeight = flick->property("height").toReal();
        QVERIFY2(caretBottom + 1 >= contentY && caretTop <= contentY + viewHeight + 1,
                 qPrintable(QStringLiteral(
                                "caret [%1, %2] outside view contentY=%3 height=%4")
                                .arg(caretTop)
                                .arg(caretBottom)
                                .arg(contentY)
                                .arg(viewHeight)));

        backend.resetZoom();
    }

    void remembersLastSaveDirectory() {
        QTemporaryDir saveDirectory;
        QVERIFY(saveDirectory.isValid());

        const QString savedPath = saveDirectory.filePath(QStringLiteral("first.md"));
        Backend savedDocument;
        savedDocument.saveAs(QUrl::fromLocalFile(savedPath));

        Backend nextDocument;
        QSignalSpy saveDialogSpy(&nextDocument, &Backend::saveDialogRequested);
        nextDocument.saveAsDialog();
        QCOMPARE(saveDialogSpy.count(), 1);

        const QUrl suggestedUrl = saveDialogSpy.takeFirst().constFirst().toUrl();
        QCOMPARE(QFileInfo(suggestedUrl.toLocalFile()).absolutePath(),
                 saveDirectory.path());
        QCOMPARE(QFileInfo(suggestedUrl.toLocalFile()).fileName(),
                 QStringLiteral("Untitled.md"));

        QSettings().setValue(QStringLiteral("file/lastSaveDirectory"),
                             saveDirectory.filePath(QStringLiteral("missing")));
        Backend fallbackDocument;
        QSignalSpy fallbackDialogSpy(&fallbackDocument, &Backend::saveDialogRequested);
        fallbackDocument.saveAsDialog();
        const QUrl fallbackUrl = fallbackDialogSpy.takeFirst().constFirst().toUrl();
        QCOMPARE(QFileInfo(fallbackUrl.toLocalFile()).absolutePath(), QDir::homePath());
    }

    void lightMutedMeetsContrast() {
        const QColor muted(MarkdownHighlighter::mutedColor(false));
        const auto linear = [](qreal channel) {
            return channel <= 0.03928 ? channel / 12.92
                                      : std::pow((channel + 0.055) / 1.055, 2.4);
        };
        const qreal luminance = 0.2126 * linear(muted.redF())
            + 0.7152 * linear(muted.greenF())
            + 0.0722 * linear(muted.blueF());
        const qreal contrast = 1.05 / (luminance + 0.05);
        QVERIFY2(contrast >= 4.5,
                 qPrintable(QStringLiteral("%1 contrast %2").arg(muted.name()).arg(contrast)));
    }

    void editorRoundTripLeavesSourceUntouched() {
        Backend backend;
        QQmlEngine engine;
        QObject *editor = nullptr;
        QScopedPointer<QObject> window(openEditor(backend, engine, &editor));
        QVERIFY(editor);

        const QString path = QFINDTESTDATA("../docs/markdown-test.md");
        QVERIFY(!path.isEmpty());
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QString original = QString::fromUtf8(file.readAll());
        backend.open(QUrl::fromLocalFile(path));
        QTRY_COMPARE(editor->property("text").toString(), original);

        const QList<int> stops{0, 40, 180, int(original.size() / 2), int(original.size())};
        for (int stop : stops) {
            editor->setProperty("cursorPosition", qBound(0, stop, original.size()));
            QCoreApplication::processEvents();
            QCOMPARE(editor->property("text").toString(), original);
        }
    }

    void rendersTableUntilTheCaretEntersIt() {
        Backend backend;
        QQmlEngine engine;
        QObject *editor = nullptr;
        QScopedPointer<QObject> window(openEditor(backend, engine, &editor));
        QVERIFY(editor);

        const QString text = QStringLiteral(
            "note\n"
            "\n"
            "| Left | Center | Right |\n"
            "| :--- | :----: | ----: |\n"
            "| alder | ash | elm |\n");
        editor->setProperty("text", text);
        editor->setProperty("cursorPosition", 0);
        QCoreApplication::processEvents();

        QTextDocument *document = editorDocument(editor);
        QVERIFY(document);
        const auto span = [document](const QString &line, const QString &word) {
            const int lineStart = document->toPlainText().indexOf(line);
            const QTextBlock block = document->findBlock(lineStart);
            const int local = line.indexOf(word);
            const QTextLine textLine = block.layout()->lineAt(0);
            return qMakePair(textLine.cursorToX(local), textLine.cursorToX(local + word.size()));
        };
        const auto headerLine = QStringLiteral("| Left | Center | Right |");
        const auto bodyLine = QStringLiteral("| alder | ash | elm |");
        const auto leftHeader = span(headerLine, QStringLiteral("Left"));
        const auto leftBody = span(bodyLine, QStringLiteral("alder"));
        QVERIFY2(qAbs(leftHeader.first - leftBody.first) < 2.0,
                 qPrintable(QStringLiteral("left %1 vs %2").arg(leftHeader.first).arg(leftBody.first)));

        const auto centerHeader = span(headerLine, QStringLiteral("Center"));
        const auto centerBody = span(bodyLine, QStringLiteral("ash"));
        const qreal headerMid = (centerHeader.first + centerHeader.second) / 2.0;
        const qreal bodyMid = (centerBody.first + centerBody.second) / 2.0;
        QVERIFY2(qAbs(headerMid - bodyMid) < 2.0,
                 qPrintable(QStringLiteral("center %1 vs %2").arg(headerMid).arg(bodyMid)));

        const auto rightHeader = span(headerLine, QStringLiteral("Right"));
        const auto rightBody = span(bodyLine, QStringLiteral("elm"));
        QVERIFY2(qAbs(rightHeader.second - rightBody.second) < 2.0,
                 qPrintable(QStringLiteral("right %1 vs %2")
                                .arg(rightHeader.second).arg(rightBody.second)));
        QTRY_VERIFY(findNamedItem(window.data(), QStringLiteral("tableFrame")));

        const int alder = text.indexOf(QStringLiteral("alder"));
        editor->setProperty("cursorPosition", alder);
        QCoreApplication::processEvents();
        QTRY_VERIFY(findNamedItem(window.data(), QStringLiteral("tableFrame")) == nullptr);

        const QTextBlock raw = document->findBlock(document->toPlainText().indexOf(
            QStringLiteral("| Left | Center | Right |")));
        QVERIFY(raw.layout());
        QVERIFY(raw.layout()->lineCount() > 0);
        const qreal pipe = raw.layout()->lineAt(0).cursorToX(1) - raw.layout()->lineAt(0).cursorToX(0);
        QTextCharFormat pipeFormat;
        const auto ranges = raw.layout()->formats();
        for (const QTextLayout::FormatRange &range : ranges) {
            if (range.start <= 0 && 0 < range.start + range.length)
                pipeFormat = range.format;
        }
        // A rendered pipe is a 1px hairline. A raw pipe keeps the glyph's own
        // advance. The width of 'a' is the wrong yardstick: DejaVu Sans draws
        // '|' at 6.73px and 'a' at 12.25px.
        QVERIFY(qFuzzyIsNull(pipeFormat.fontPointSize()) || pipeFormat.fontPointSize() > 8);
        if (pipeFormat.fontLetterSpacingType() == QFont::AbsoluteSpacing)
            QVERIFY2(qAbs(pipeFormat.fontLetterSpacing()) < 1.0,
                     qPrintable(QStringLiteral("pipe spacing %1").arg(pipeFormat.fontLetterSpacing())));
        QFont pipeFont = document->defaultFont();
        QCOMPARE(QFontInfo(pipeFont).family(), QStringLiteral("iA Writer Mono S"));
        if (pipeFormat.hasProperty(QTextFormat::FontPixelSize))
            pipeFont.setPixelSize(pipeFormat.intProperty(QTextFormat::FontPixelSize));
        else if (pipeFormat.fontPointSize() > 0)
            pipeFont.setPointSizeF(pipeFormat.fontPointSize());
        const qreal natural = QFontMetricsF(pipeFont).horizontalAdvance(QLatin1Char('|'));
        QVERIFY2(natural > 2.0, qPrintable(QStringLiteral("natural pipe %1").arg(natural)));
        QVERIFY2(pipe > natural * 0.6,
                 qPrintable(QStringLiteral("raw pipe %1 natural %2").arg(pipe).arg(natural)));
        QCOMPARE(editor->property("text").toString(), text);
    }

    void rawTablePipeKeepsItsOwnAdvance() {
        QTextDocument document;
        QFont font(QStringLiteral("DejaVu Sans"));
        font.setPixelSize(20);
        QCOMPARE(QFontInfo(font).family(), QStringLiteral("DejaVu Sans"));
        document.setDefaultFont(font);
        document.setTextWidth(2000);
        const QString text = QStringLiteral(
            "note\n"
            "\n"
            "| Left | Center | Right |\n"
            "| :--- | :----: | ----: |\n"
            "| alder | ash | elm |\n");
        document.setPlainText(text);

        MarkdownHighlighter highlighter(&document);
        highlighter.setColors(QStringLiteral("#ffffff"), QStringLiteral("#222324"),
                              QStringLiteral("#2077b2"));
        const auto pipeAdvance = [&document, &text]() {
            QImage surface(8, 8, QImage::Format_ARGB32_Premultiplied);
            QPainter painter(&surface);
            document.drawContents(&painter);
            painter.end();
            const QTextBlock header = document.findBlock(text.indexOf(QStringLiteral("| Left")));
            if (!header.isValid() || !header.layout() || header.layout()->lineCount() < 1)
                return qreal(-1);
            const QTextLine line = header.layout()->lineAt(0);
            return line.cursorToX(1) - line.cursorToX(0);
        };
        const qreal natural = QFontMetricsF(font).horizontalAdvance(QLatin1Char('|'));
        QVERIFY2(natural > 2.0, qPrintable(QStringLiteral("natural %1").arg(natural)));

        highlighter.setEditingPosition(0);
        const qreal rendered = pipeAdvance();
        QVERIFY2(rendered >= 0 && rendered < natural * 0.6,
                 qPrintable(QStringLiteral("rendered pipe %1 natural %2").arg(rendered).arg(natural)));

        highlighter.setEditingPosition(text.indexOf(QStringLiteral("alder")));
        const qreal raw = pipeAdvance();
        QVERIFY2(raw > natural * 0.6,
                 qPrintable(QStringLiteral("raw pipe %1 natural %2").arg(raw).arg(natural)));
    }

    void clickingATaskTogglesOnlyTheBoxAndUndoes() {
        Backend backend;
        QQmlEngine engine;
        QObject *editor = nullptr;
        QScopedPointer<QObject> window(openEditor(backend, engine, &editor));
        QVERIFY(editor);
        auto *quickWindow = qobject_cast<QQuickWindow *>(window.data());
        QVERIFY(quickWindow);
        quickWindow->show();
        QVERIFY(QTest::qWaitForWindowExposed(quickWindow));

        const QString original = QStringLiteral("keep\n- [ ] alpha\n| stay | put |\n");
        editor->setProperty("text", original);
        editor->setProperty("cursorPosition", 0);
        QCoreApplication::processEvents();
        QQuickItem *box = nullptr;
        QTRY_VERIFY((box = findNamedItem(window.data(), QStringLiteral("taskCheckbox"))));
        auto *item = qobject_cast<QQuickItem *>(box);
        QVERIFY(item);
        QVERIFY(QMetaObject::invokeMethod(box, "forceActiveFocus"));
        const QPoint center = item->mapToScene(QPointF(item->width() / 2.0, item->height() / 2.0)).toPoint();
        QTest::mouseClick(quickWindow, Qt::LeftButton, Qt::NoModifier, center);
        QTRY_COMPARE(editor->property("text").toString(),
                     QStringLiteral("keep\n- [x] alpha\n| stay | put |\n"));

        QVERIFY(QMetaObject::invokeMethod(editor, "undo"));
        QTRY_COMPARE(editor->property("text").toString(), original);
    }

    void ctrlClickOpensOnlyWebAndMailLinks() {
        Backend backend;
        QQmlEngine engine;
        QObject *editor = nullptr;
        QScopedPointer<QObject> window(openEditor(backend, engine, &editor));
        QVERIFY(editor);
        auto *quickWindow = qobject_cast<QQuickWindow *>(window.data());
        QVERIFY(quickWindow);
        quickWindow->show();
        QVERIFY(QTest::qWaitForWindowExposed(quickWindow));

        const QString text = QStringLiteral(
            "See [safe](https://example.com/a) and [bad](javascript:alert(1)) "
            "and [mail](mailto:writer@example.com) and [disk](file:///etc/passwd)\n");
        editor->setProperty("text", text);
        editor->setProperty("cursorPosition", 0);
        QCoreApplication::processEvents();

        const auto at = [&text](const QString &label) {
            return text.indexOf(label);
        };
        QCOMPARE(backend.linkAt(at(QStringLiteral("safe"))),
                 QStringLiteral("https://example.com/a"));
        QCOMPARE(backend.linkAt(at(QStringLiteral("mail"))),
                 QStringLiteral("mailto:writer@example.com"));
        QVERIFY(backend.linkAt(at(QStringLiteral("bad"))).isEmpty());
        QVERIFY(backend.linkAt(at(QStringLiteral("disk"))).isEmpty());

        QSignalSpy activated(window.data(), SIGNAL(linkActivated(QString)));
        const auto click = [&](const QString &label) {
            const int position = at(label);
            QRectF rect;
            const bool invoked = QMetaObject::invokeMethod(editor, "positionToRectangle",
                                                          Q_RETURN_ARG(QRectF, rect),
                                                          Q_ARG(int, position));
            QVERIFY(invoked);
            auto *editorItem = qobject_cast<QQuickItem *>(editor);
            const QPoint scene = editorItem->mapToScene(rect.center()).toPoint();
            QTest::mouseClick(quickWindow, Qt::LeftButton, Qt::ControlModifier, scene);
        };
        click(QStringLiteral("safe"));
        QTRY_COMPARE(activated.count(), 1);
        QCOMPARE(activated.takeFirst().constFirst().toString(),
                 QStringLiteral("https://example.com/a"));
        click(QStringLiteral("bad"));
        click(QStringLiteral("disk"));
        QCOMPARE(activated.count(), 0);
        click(QStringLiteral("mail"));
        QTRY_COMPARE(activated.count(), 1);
        QCOMPARE(editor->property("text").toString(), text);
    }

    void editorShowsCodeQuotesRulesAndHtmlSource() {
        Backend backend;
        QQmlEngine engine;
        QObject *editor = nullptr;
        QScopedPointer<QObject> window(openEditor(backend, engine, &editor));
        QVERIFY(editor);

        const QString text = QStringLiteral(
            "intro\n"
            "\n"
            "> quoted line\n"
            "\n"
            "---\n"
            "\n"
            "```java\n"
            "public class A {}\n"
            "```\n"
            "\n"
            "```go\n"
            "package main\n"
            "```\n"
            "\n"
            "```rust\n"
            "fn main() {}\n"
            "```\n"
            "\n"
            "```mermaid\n"
            "%% note\n"
            "```\n"
            "\n"
            "<em>Emphasized</em>\n");
        editor->setProperty("text", text);
        editor->setProperty("cursorPosition", 0);
        QCoreApplication::processEvents();

        QTextDocument *document = editorDocument(editor);
        QVERIFY(document);
        const auto formatAt = [document](int position) {
            const QTextBlock block = document->findBlock(position);
            const int local = position - block.position();
            QTextCharFormat format;
            const auto ranges = block.layout()->formats();
            for (const QTextLayout::FormatRange &range : ranges) {
                if (local >= range.start && local < range.start + range.length)
                    format = range.format;
            }
            return format;
        };
        const QString plain = document->toPlainText();
        const int quoteMarker = plain.indexOf(QLatin1Char('>'));
        QVERIFY(quoteMarker >= 0);
        QCOMPARE(formatAt(quoteMarker).fontPointSize(), 1.0);
        editor->setProperty("cursorPosition", quoteMarker);
        QCoreApplication::processEvents();
        QVERIFY(qFuzzyIsNull(formatAt(quoteMarker).fontPointSize())
                || formatAt(quoteMarker).fontPointSize() > 8);

        editor->setProperty("cursorPosition", 0);
        QCoreApplication::processEvents();
        QTRY_VERIFY(findNamedItem(window.data(), QStringLiteral("horizontalRule")));
        const int rule = plain.indexOf(QStringLiteral("---"));
        editor->setProperty("cursorPosition", rule);
        QCoreApplication::processEvents();
        QTRY_VERIFY(findNamedItem(window.data(), QStringLiteral("horizontalRule")) == nullptr);

        const QColor accent(backend.themeAccent());
        const int keyword = plain.indexOf(QStringLiteral("public"));
        QCOMPARE(formatAt(keyword).foreground().color(), accent);
        QVERIFY(formatAt(keyword).background().style() != Qt::NoBrush);
        QVERIFY(document->findBlock(keyword).blockFormat().background().style() != Qt::NoBrush);

        const int goKeyword = plain.indexOf(QStringLiteral("package"));
        QCOMPARE(formatAt(goKeyword).foreground().color(), accent);
        const int rustKeyword = plain.indexOf(QStringLiteral("fn"));
        QCOMPARE(formatAt(rustKeyword).foreground().color(), accent);
        const int comment = plain.indexOf(QStringLiteral("%%"));
        QCOMPARE(formatAt(comment).foreground().color(),
                 QColor(MarkdownHighlighter::mutedColor(backend.darkMode())));

        const int html = plain.indexOf(QStringLiteral("<em>"));
        QVERIFY(html >= 0);
        QCOMPARE(document->characterAt(html), QLatin1Char('<'));
        QCOMPARE(int(formatAt(html).objectType()), int(QTextFormat::NoObject));
        QVERIFY(qFuzzyIsNull(formatAt(html).fontPointSize()) || formatAt(html).fontPointSize() > 8);
        QVERIFY(plain.contains(QStringLiteral("<em>Emphasized</em>")));
        QCOMPARE(editor->property("text").toString(), text);
    }

    void imageLoadsStayInsideTheNote() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString notePath = directory.filePath(QStringLiteral("note.md"));
        QFile sampleSource(QFINDTESTDATA("../docs/sample.png"));
        QVERIFY(sampleSource.open(QIODevice::ReadOnly));
        const QByteArray sampleBytes = sampleSource.readAll();
        QFile sample(directory.filePath(QStringLiteral("sample.png")));
        QVERIFY(sample.open(QIODevice::WriteOnly));
        QCOMPARE(sample.write(sampleBytes), qint64(sampleBytes.size()));
        sample.close();
        const QString text = QStringLiteral(
            "title\n"
            "\n"
            "![local](sample.png)\n"
            "![remote](https://example.com/a.png)\n"
            "![proto](//cdn.example/a.png)\n"
            "![spaced](https://cdn.example/my%20file.png)\n"
            "![escape](../secret.png)\n"
            "![abs](/etc/passwd)\n"
            "![fileout](file:///etc/passwd)\n"
            "![filehost](file://evil.example/share.png)\n"
            "![encoded](..%2Fsecret.png)\n");
        QFile note(notePath);
        QVERIFY(note.open(QIODevice::WriteOnly | QIODevice::Text));
        note.write(text.toUtf8());
        note.close();

        Backend backend;
        QQmlEngine engine;
        QObject *editor = nullptr;
        QScopedPointer<QObject> window(openEditor(backend, engine, &editor));
        QVERIFY(editor);
        backend.open(QUrl::fromLocalFile(notePath));
        editor->setProperty("cursorPosition", 0);
        QCoreApplication::processEvents();

        const auto image = [&backend, &text](const QString &alt) {
            const int position = text.indexOf(QStringLiteral("![") + alt);
            return backend.imageAt(position);
        };
        const QVariantMap local = image(QStringLiteral("local"));
        QCOMPARE(local.value(QStringLiteral("mode")).toString(), QStringLiteral("local"));
        const QString localPath = QUrl(local.value(QStringLiteral("source")).toString()).toLocalFile();
        QVERIFY(localPath.startsWith(QFileInfo(directory.path()).canonicalFilePath() + QLatin1Char('/')));
        QVERIFY(localPath.endsWith(QStringLiteral("/sample.png")));

        QCOMPARE(image(QStringLiteral("remote")).value(QStringLiteral("mode")).toString(),
                 QStringLiteral("remote"));
        QVERIFY(image(QStringLiteral("remote")).value(QStringLiteral("source")).toString().isEmpty());
        QCOMPARE(image(QStringLiteral("proto")).value(QStringLiteral("mode")).toString(),
                 QStringLiteral("remote"));
        QVERIFY(image(QStringLiteral("proto")).value(QStringLiteral("source")).toString().isEmpty());
        QCOMPARE(image(QStringLiteral("proto")).value(QStringLiteral("remoteUrl")).toString(),
                 QStringLiteral("https://cdn.example/a.png"));
        QCOMPARE(image(QStringLiteral("spaced")).value(QStringLiteral("remoteUrl")).toString(),
                 QStringLiteral("https://cdn.example/my%20file.png"));
        QVERIFY(!image(QStringLiteral("spaced")).value(QStringLiteral("remoteUrl")).toString().contains(
            QLatin1Char(' ')));
        QCOMPARE(image(QStringLiteral("escape")).value(QStringLiteral("mode")).toString(),
                 QStringLiteral("rejected"));
        QCOMPARE(image(QStringLiteral("abs")).value(QStringLiteral("mode")).toString(),
                 QStringLiteral("rejected"));
        QCOMPARE(image(QStringLiteral("fileout")).value(QStringLiteral("mode")).toString(),
                 QStringLiteral("rejected"));
        QCOMPARE(image(QStringLiteral("filehost")).value(QStringLiteral("mode")).toString(),
                 QStringLiteral("rejected"));
        QCOMPARE(image(QStringLiteral("filehost")).value(QStringLiteral("reason")).toString(),
                 QStringLiteral("file-host"));
        QCOMPARE(image(QStringLiteral("encoded")).value(QStringLiteral("mode")).toString(),
                 QStringLiteral("rejected"));

        const QList<QQuickItem *> images = findNamedItems(window.data(), QStringLiteral("renderedImage"));
        QVERIFY(!images.isEmpty());
        bool sawLocal = false;
        for (QObject *imageObject : images) {
            const QString source = imageObject->property("source").toString();
            QVERIFY(!source.startsWith(QStringLiteral("http://")));
            QVERIFY(!source.startsWith(QStringLiteral("https://")));
            QVERIFY(!source.startsWith(QStringLiteral("//")));
            if (source.startsWith(QStringLiteral("file:")))
                sawLocal = true;
        }
        QVERIFY(sawLocal);

        QQuickItem *remoteButton = nullptr;
        QTRY_VERIFY((remoteButton = findNamedItem(window.data(), QStringLiteral("remoteImageButton"))));
        QVERIFY(remoteButton->property("activeFocusOnTab").toBool());
        QObject *editorChrome = window->findChild<QObject *>(QStringLiteral("editorChrome"));
        QVERIFY(editorChrome);
        QVERIFY(QMetaObject::invokeMethod(editorChrome, "consentRemote",
                                          Q_ARG(QVariant, QStringLiteral("https://example.com/a.png"))));
        QCoreApplication::processEvents();
        bool optedIn = false;
        for (QQuickItem *imageObject : findNamedItems(window.data(), QStringLiteral("renderedImage"))) {
            if (imageObject->property("source").toString() == QStringLiteral("https://example.com/a.png"))
                optedIn = true;
        }
        QVERIFY(optedIn);
        for (QQuickItem *imageObject : findNamedItems(window.data(), QStringLiteral("renderedImage"))) {
            const QString source = imageObject->property("source").toString();
            QVERIFY(source != QStringLiteral("https://cdn.example/a.png"));
            QVERIFY(!source.contains(QStringLiteral("%20")) || source.startsWith(QStringLiteral("file:")));
        }

        backend.setTextScale(2);
        QCoreApplication::processEvents();
        QQuickItem *actionLabel = nullptr;
        for (QQuickItem *candidate : findNamedItems(window.data(), QStringLiteral("imageActionLabel"))) {
            if (candidate->property("text").toString() == QStringLiteral("Load image"))
                actionLabel = candidate;
        }
        QVERIFY(actionLabel);
        const QFont editorFont = editor->property("font").value<QFont>();
        const QFont labelFont = actionLabel->property("font").value<QFont>();
        const int expected = qMax(11, qRound(editorFont.pixelSize() * 0.65));
        QCOMPARE(labelFont.pixelSize(), expected);
        QVERIFY(labelFont.pixelSize() > 13);
        QCOMPARE(editor->property("text").toString(), text);
    }

    void renderedPipesDisappearAndShortRowsShareTheFrame() {
        const auto escaped = MarkdownHighlighter::parseTableLine(
            QStringLiteral("| a \\| b | `c|d` |"));
        QCOMPARE(escaped.cells.size(), 2);
        QVERIFY(escaped.cells.at(0).length > 0);
        QCOMPARE(escaped.hidden.size(), 3);
        const QString escapedCell = QStringLiteral("| a \\| b | `c|d` |").mid(
            escaped.cells.at(0).start, escaped.cells.at(0).length);
        QVERIFY(escapedCell.contains(QStringLiteral("\\|")));
        const QString codeCell = QStringLiteral("| a \\| b | `c|d` |").mid(
            escaped.cells.at(1).start, escaped.cells.at(1).length);
        QVERIFY(codeCell.contains(QStringLiteral("c|d")));

        Backend backend;
        QQmlEngine engine;
        QObject *editor = nullptr;
        QScopedPointer<QObject> window(openEditor(backend, engine, &editor));
        QVERIFY(editor);

        const QString wide(100, QChar(0x6c49));
        const QString text = QStringLiteral("note\n"
                                            "\n"
                                            "| alpha | beta | gamma |\n"
                                            "| :--- | :----: | ----: |\n"
                                            "| only |\n"
                                            "| ")
            + wide + QStringLiteral(" | short |\n");
        editor->setProperty("text", text);
        editor->setProperty("cursorPosition", 0);
        QCoreApplication::processEvents();

        QTextDocument *document = editorDocument(editor);
        QVERIFY(document);
        const auto formatAt = [document](int position) {
            const QTextBlock block = document->findBlock(position);
            const int local = position - block.position();
            QTextCharFormat format;
            const auto ranges = block.layout() ? block.layout()->formats() : QList<QTextLayout::FormatRange>{};
            for (const QTextLayout::FormatRange &range : ranges) {
                if (local >= range.start && local < range.start + range.length)
                    format = range.format;
            }
            return format;
        };
        const int headerPipe = text.indexOf(QLatin1Char('|'));
        const QTextBlock header = document->findBlock(headerPipe);
        QVERIFY(header.layout());
        QVERIFY(header.layout()->lineCount() > 0);
        const qreal pipeAdvance = header.layout()->lineAt(0).cursorToX(1)
            - header.layout()->lineAt(0).cursorToX(0);
        QVERIFY2(pipeAdvance <= 2.0,
                 qPrintable(QStringLiteral("rendered pipe %1").arg(pipeAdvance)));
        QCOMPARE(formatAt(headerPipe).foreground().color().alpha(), 0);
        QCOMPARE(formatAt(headerPipe).fontPointSize(), 1.0);

        int rowCount = 0;
        for (const QVariant &entry : backend.decorations()) {
            const QVariantMap map = entry.toMap();
            if (map.value(QStringLiteral("kind")).toString() == QStringLiteral("table"))
                rowCount = map.value(QStringLiteral("rows")).toList().size();
        }
        QCOMPARE(rowCount, 3);
        QCOMPARE(findNamedItems(window.data(), QStringLiteral("tableRowRule")).size(), 2);

        const auto edgeX = [document](const QString &line) {
            const int start = document->toPlainText().indexOf(line);
            const QTextBlock block = document->findBlock(start);
            const QTextLine textLine = block.layout()->lineAt(0);
            return textLine.cursorToX(line.size());
        };
        const QString headerLine = QStringLiteral("| alpha | beta | gamma |");
        const QString shortLine = QStringLiteral("| only |");
        const qreal headerEdge = edgeX(headerLine);
        const qreal shortEdge = edgeX(shortLine);
        QVERIFY2(qAbs(headerEdge - shortEdge) < 4.0,
                 qPrintable(QStringLiteral("header edge %1 short edge %2")
                                .arg(headerEdge).arg(shortEdge)));
        QQuickItem *frame = nullptr;
        QTRY_VERIFY((frame = findNamedItem(window.data(), QStringLiteral("tableFrame"))));
        const QTextBlock headerBlock = document->findBlock(document->toPlainText().indexOf(headerLine));
        const QTextLine headerTextLine = headerBlock.layout()->lineAt(0);
        const qreal pipesWidth = headerTextLine.cursorToX(headerLine.size()) - headerTextLine.cursorToX(0);
        QVERIFY2(qAbs(frame->width() - pipesWidth) < 4.0,
                 qPrintable(QStringLiteral("frame %1 pipes %2").arg(frame->width()).arg(pipesWidth)));

        const int wideRow = text.indexOf(wide);
        const QTextBlock wideBlock = document->findBlock(wideRow);
        QVERIFY(wideBlock.blockFormat().nonBreakableLines());
        QVERIFY(wideBlock.layout());
        QCOMPARE(wideBlock.layout()->lineCount(), 1);
        QVERIFY2(document->textWidth() > 0
                     && wideBlock.layout()->lineAt(0).naturalTextWidth() > document->textWidth(),
                 qPrintable(QStringLiteral("wide %1 text width %2")
                                .arg(wideBlock.layout()->lineAt(0).naturalTextWidth())
                                .arg(document->textWidth())));
        QFont cellFont = document->defaultFont();
        cellFont.setWeight(QFont::Normal);
        QTextLayout measure(wide, cellFont);
        measure.beginLayout();
        QTextLine measured = measure.createLine();
        if (measured.isValid())
            measured.setLineWidth(1.0e6);
        measure.endLayout();
        const int local = wideRow - wideBlock.position();
        const QTextLine laid = wideBlock.layout()->lineAt(0);
        const qreal laidWidth = laid.cursorToX(local + wide.size()) - laid.cursorToX(local);
        QVERIFY2(laidWidth + 1.0 >= measured.naturalTextWidth(),
                 qPrintable(QStringLiteral("laid %1 natural %2")
                                .arg(laidWidth).arg(measured.naturalTextWidth())));
        QCOMPARE(editor->property("text").toString(), text);
    }

    void largeImageReservesTheLineBelowIt() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QImage photo(1400, 900, QImage::Format_ARGB32);
        photo.fill(QColor(QStringLiteral("#336699")));
        {
            QPainter painter(&photo);
            painter.setPen(Qt::white);
            painter.drawLine(0, 0, 1399, 899);
            painter.drawText(QPoint(48, 120), QStringLiteral("large photo"));
        }
        QVERIFY(photo.save(directory.filePath(QStringLiteral("wide.png"))));
        const QString text = QStringLiteral("before\n![photo](wide.png)\nafter\n");
        QFile note(directory.filePath(QStringLiteral("note.md")));
        QVERIFY(note.open(QIODevice::WriteOnly | QIODevice::Text));
        note.write(text.toUtf8());
        note.close();

        Backend backend;
        QQmlEngine engine;
        QObject *editor = nullptr;
        QScopedPointer<QObject> window(openEditor(backend, engine, &editor));
        QVERIFY(editor);
        backend.open(QUrl::fromLocalFile(note.fileName()));
        editor->setProperty("cursorPosition", 0);
        QCoreApplication::processEvents();

        QTextDocument *document = editorDocument(editor);
        QVERIFY(document);
        const QTextBlock imageBlock = document->findBlock(text.indexOf(QStringLiteral("![")));
        const QTextBlock next = imageBlock.next();
        QVERIFY(imageBlock.isValid());
        QVERIFY(next.isValid());
        const qreal imageTop = document->documentLayout()->blockBoundingRect(imageBlock).top();
        const qreal nextTop = document->documentLayout()->blockBoundingRect(next).top();
        QVERIFY2(nextTop - imageTop >= MarkdownHighlighter::imageLineHeight - 0.5,
                 qPrintable(QStringLiteral("gap %1").arg(nextTop - imageTop)));
        QRectF imageCaret;
        QRectF nextCaret;
        QVERIFY(QMetaObject::invokeMethod(editor, "positionToRectangle",
                                          Q_RETURN_ARG(QRectF, imageCaret),
                                          Q_ARG(int, imageBlock.position())));
        QVERIFY(QMetaObject::invokeMethod(editor, "positionToRectangle",
                                          Q_RETURN_ARG(QRectF, nextCaret),
                                          Q_ARG(int, next.position())));
        QVERIFY2(nextCaret.top() >= imageCaret.top() + MarkdownHighlighter::imageLineHeight - 1.0,
                 qPrintable(QStringLiteral("visual gap %1").arg(nextCaret.top() - imageCaret.top())));

        QQuickItem *decoration = nullptr;
        QTRY_VERIFY((decoration = findNamedItem(window.data(), QStringLiteral("imageDecoration"))));
        QCOMPARE(decoration->height(), MarkdownHighlighter::imageLineHeight);
        QQuickItem *rendered = nullptr;
        QTRY_VERIFY((rendered = findNamedItem(window.data(), QStringLiteral("renderedImage"))));
        QVERIFY2(rendered->height() > 96.0 && rendered->height() <= MarkdownHighlighter::imageLineHeight,
                 qPrintable(QStringLiteral("image height %1").arg(rendered->height())));
        QCOMPARE(editor->property("text").toString(), text);
    }

    void fencedMarkdownStaysLiteral() {
        Backend backend;
        QQmlEngine engine;
        QObject *editor = nullptr;
        QScopedPointer<QObject> window(openEditor(backend, engine, &editor));
        QVERIFY(editor);

        const QString text = QStringLiteral(
            "- [ ] live\n"
            "\n"
            "```\n"
            "- [ ] hidden\n"
            "| a | b |\n"
            "| --- | --- |\n"
            "| c | d |\n"
            "---\n"
            "```\n"
            "\n"
            "- [x] after\n");
        editor->setProperty("text", text);
        editor->setProperty("cursorPosition", text.indexOf(QStringLiteral("\n\n")) + 1);
        QCoreApplication::processEvents();

        const int hiddenBox = text.indexOf(QStringLiteral("- [ ] hidden")) + 2;
        QVERIFY(backend.taskAt(hiddenBox).isEmpty());
        QVERIFY(!backend.toggleTask(hiddenBox));
        QCOMPARE(editor->property("text").toString(), text);
        QVERIFY(findNamedItem(window.data(), QStringLiteral("tableFrame")) == nullptr);
        QVERIFY(findNamedItem(window.data(), QStringLiteral("horizontalRule")) == nullptr);

        int taskBoxes = 0;
        for (const QVariant &entry : backend.decorations()) {
            const QVariantMap map = entry.toMap();
            const QString kind = map.value(QStringLiteral("kind")).toString();
            QVERIFY(kind != QStringLiteral("table"));
            QVERIFY(kind != QStringLiteral("rule"));
            if (kind == QStringLiteral("task")) {
                ++taskBoxes;
                QVERIFY(map.value(QStringLiteral("start")).toInt() != hiddenBox);
            }
        }
        QCOMPARE(taskBoxes, 2);

        const int liveBox = text.indexOf(QStringLiteral("[ ]"));
        QVERIFY(backend.toggleTask(liveBox));
        QCoreApplication::processEvents();
        QVERIFY(editor->property("text").toString().contains(QStringLiteral("- [x] live")));
        QVERIFY(editor->property("text").toString().contains(QStringLiteral("- [ ] hidden")));
        QVERIFY(QMetaObject::invokeMethod(editor, "undo"));
        QCoreApplication::processEvents();
        QCOMPARE(editor->property("text").toString(), text);
    }

    void caretAndSelectionRevealOnlyTheActiveMarkup() {
        Backend backend;
        QQmlEngine engine;
        QObject *editor = nullptr;
        QScopedPointer<QObject> window(openEditor(backend, engine, &editor));
        QVERIFY(editor);

        const QString text = QStringLiteral(
            "# Title\n"
            "\n"
            "see <https://example.com/auto> now\n"
            "\n"
            "- [ ] task\n"
            "\n"
            "> one\n"
            "> > two\n"
            "\n"
            "```cpp\n"
            "int x;\n"
            "```\n");
        editor->setProperty("text", text);
        editor->setProperty("cursorPosition", 0);
        QCoreApplication::processEvents();

        QTextDocument *document = editorDocument(editor);
        QVERIFY(document);
        const auto formatAt = [document](int position) {
            const QTextBlock block = document->findBlock(position);
            const int local = position - block.position();
            QTextCharFormat format;
            const auto ranges = block.layout() ? block.layout()->formats() : QList<QTextLayout::FormatRange>{};
            for (const QTextLayout::FormatRange &range : ranges) {
                if (local >= range.start && local < range.start + range.length)
                    format = range.format;
            }
            return format;
        };

        const int bracket = text.indexOf(QLatin1Char('<'));
        editor->setProperty("cursorPosition", 0);
        QCoreApplication::processEvents();
        QCOMPARE(formatAt(bracket).fontPointSize(), 1.0);
        editor->setProperty("cursorPosition", bracket);
        QCoreApplication::processEvents();
        QVERIFY(qFuzzyIsNull(formatAt(bracket).fontPointSize())
                || formatAt(bracket).fontPointSize() > 8);

        const int taskBox = text.indexOf(QStringLiteral("[ ]"));
        editor->setProperty("cursorPosition", taskBox);
        QCoreApplication::processEvents();
        QTRY_VERIFY(findNamedItem(window.data(), QStringLiteral("taskCheckbox")) == nullptr);
        editor->setProperty("cursorPosition", 0);
        QCoreApplication::processEvents();
        QTRY_VERIFY(findNamedItem(window.data(), QStringLiteral("taskCheckbox")));

        editor->setProperty("cursorPosition", 0);
        QCoreApplication::processEvents();
        QCOMPARE(findNamedItems(window.data(), QStringLiteral("quoteBar")).size(), 3);
        const int quoteMark = text.indexOf(QStringLiteral("> one"));
        QCOMPARE(formatAt(quoteMark).fontPointSize(), 1.0);
        const int nested = text.indexOf(QStringLiteral("> > two"));
        editor->setProperty("cursorPosition", nested);
        QCoreApplication::processEvents();
        QTRY_VERIFY(findNamedItems(window.data(), QStringLiteral("quoteBar")).isEmpty());
        QVERIFY(qFuzzyIsNull(formatAt(quoteMark).fontPointSize())
                || formatAt(quoteMark).fontPointSize() > 8);

        const int codeBody = text.indexOf(QStringLiteral("int x;"));
        editor->setProperty("cursorPosition", 0);
        QCoreApplication::processEvents();
        QCOMPARE(document->findBlock(codeBody).blockFormat().leftMargin(),
                 MarkdownHighlighter::codeBlockInset);
        QQuickItem *language = nullptr;
        QTRY_VERIFY((language = findNamedItem(window.data(), QStringLiteral("codeLanguage"))));
        QCOMPARE(language->property("text").toString(), QStringLiteral("cpp"));
        editor->setProperty("cursorPosition", codeBody);
        QCoreApplication::processEvents();
        QTRY_VERIFY(findNamedItem(window.data(), QStringLiteral("codeLanguage")) == nullptr);
        QCOMPARE(document->findBlock(codeBody).blockFormat().leftMargin(),
                 MarkdownHighlighter::codeBlockInset);

        const int body = text.indexOf(QStringLiteral("see"));
        QVERIFY(QMetaObject::invokeMethod(editor, "select", Q_ARG(int, 0), Q_ARG(int, body + 2)));
        QCoreApplication::processEvents();
        QVERIFY(qFuzzyIsNull(formatAt(0).fontPointSize()) || formatAt(0).fontPointSize() > 8);
        editor->setProperty("cursorPosition", body);
        QCoreApplication::processEvents();
        QCOMPARE(formatAt(0).fontPointSize(), 1.0);
        QCOMPARE(editor->property("text").toString(), text);
    }

    void movingTheCaretDoesNotPushUndo() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("note.md"));
        QFile note(path);
        QVERIFY(note.open(QIODevice::WriteOnly | QIODevice::Text));
        note.write("hello\nworld\n");
        note.close();

        Backend backend;
        QQmlEngine engine;
        QObject *editor = nullptr;
        QScopedPointer<QObject> window(openEditor(backend, engine, &editor));
        QVERIFY(editor);
        backend.open(QUrl::fromLocalFile(path));
        QCoreApplication::processEvents();
        QTextDocument *document = editorDocument(editor);
        QVERIFY(document);
        QVERIFY(!document->isUndoAvailable());
        editor->setProperty("cursorPosition", 4);
        QCoreApplication::processEvents();
        QVERIFY(!document->isUndoAvailable());
        QVERIFY(QMetaObject::invokeMethod(editor, "select", Q_ARG(int, 0), Q_ARG(int, 5)));
        QCoreApplication::processEvents();
        QVERIFY(!document->isUndoAvailable());
        QCOMPARE(editor->property("text").toString(), QStringLiteral("hello\nworld\n"));
    }

    void mailtoDropsEveryQueryExceptSubjectAndBody() {
        Backend backend;
        QQmlEngine engine;
        QObject *editor = nullptr;
        QScopedPointer<QObject> window(openEditor(backend, engine, &editor));
        QVERIFY(editor);

        const QString text = QStringLiteral(
            "[x](mailto:a@evil?attach=/home/u/.ssh/id_rsa)\n"
            "[y](mailto:a@evil?Subject=Hi&CC=spy@evil&Bcc=other@evil&Body=Yo&attach=/tmp/x)\n");
        editor->setProperty("text", text);
        editor->setProperty("cursorPosition", 0);
        QCoreApplication::processEvents();

        const int attachLink = text.indexOf(QStringLiteral("[x]"));
        QCOMPARE(backend.linkAt(attachLink), QStringLiteral("mailto:a@evil"));
        const QString kept = backend.linkAt(text.indexOf(QStringLiteral("[y]")));
        QVERIFY(kept.startsWith(QStringLiteral("mailto:a@evil")));
        QVERIFY(!kept.contains(QStringLiteral("attach"), Qt::CaseInsensitive));
        QVERIFY(!kept.contains(QStringLiteral("ssh")));
        QVERIFY(!kept.contains(QStringLiteral("cc="), Qt::CaseInsensitive));
        QVERIFY(!kept.contains(QStringLiteral("bcc"), Qt::CaseInsensitive));
        QVERIFY(kept.contains(QStringLiteral("subject=Hi"), Qt::CaseInsensitive));
        QVERIFY(kept.contains(QStringLiteral("body=Yo"), Qt::CaseInsensitive));

        QSignalSpy activated(window.data(), SIGNAL(linkActivated(QString)));
        QVariant opened;
        QVERIFY(QMetaObject::invokeMethod(window.data(), "openLinkAt",
                                          Q_RETURN_ARG(QVariant, opened),
                                          Q_ARG(QVariant, attachLink)));
        QVERIFY(opened.toBool());
        QCOMPARE(activated.count(), 1);
        const QString sent = activated.takeFirst().constFirst().toString();
        QCOMPARE(sent, QStringLiteral("mailto:a@evil"));
        QVERIFY(backend.openExternalUrl(QUrl(QStringLiteral(
            "mailto:a@evil?attach=/home/u/.ssh/id_rsa&subject=Hi"))));
        QCOMPARE(editor->property("text").toString(), text);
    }

    void statusAndUnsavedPromptStayPlainText() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        // A closed <b></b> pair is rejected by this environment's path filter.
        // An opening tag is enough for Qt AutoText to treat the name as HTML.
        const QString path = directory.filePath(QStringLiteral("<b>x.md"));
        QFile note(path);
        QVERIFY(note.open(QIODevice::WriteOnly | QIODevice::Text));
        note.write("# hello\n");
        note.close();

        Backend backend;
        QQmlEngine engine;
        QObject *editor = nullptr;
        QScopedPointer<QObject> window(openEditor(backend, engine, &editor));
        QVERIFY(editor);
        backend.open(QUrl::fromLocalFile(path));
        QCoreApplication::processEvents();

        QObject *status = window->findChild<QObject *>(QStringLiteral("statusLabel"));
        QVERIFY(status);
        QCOMPARE(status->property("textFormat").toInt(), int(Qt::PlainText));
        QVERIFY(status->property("text").toString().contains(QStringLiteral("<b>x.md")));
        QVERIFY(status->property("text").toString().contains(QStringLiteral("<b>")));

        QObject *prompt = window->findChild<QObject *>(QStringLiteral("unsavedPrompt"));
        QVERIFY(prompt);
        QCOMPARE(prompt->property("textFormat").toInt(), int(Qt::PlainText));
        QObject *dialog = window->findChild<QObject *>(QStringLiteral("unsavedChangesDialog"));
        QVERIFY(dialog);
        dialog->setProperty("fileName", QStringLiteral("<b>x</b>.md"));
        QVERIFY(prompt->property("text").toString().contains(QStringLiteral("<b>x</b>.md")));
        QCOMPARE(editor->property("text").toString(), QStringLiteral("# hello\n"));
    }

private:
    QTemporaryDir m_settingsDirectory;

    QObject *openEditor(Backend &backend, QQmlEngine &engine, QObject **editor) {
        const QString mainQmlPath = QFINDTESTDATA("../src/Main.qml");
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        auto *component = new QQmlComponent(&engine, QUrl::fromLocalFile(mainQmlPath), &engine);
        QObject::connect(&engine, &QQmlEngine::warnings, &engine,
                         [](const QList<QQmlError> &warnings) {
            for (const QQmlError &warning : warnings)
                qWarning().noquote() << warning.toString();
        });
        QObject *window = component->create();
        if (!component->isReady() || !window) {
            qWarning() << component->errorString();
            return nullptr;
        }
        *editor = window->findChild<QObject *>(QStringLiteral("sourceEditor"));
        return window;
    }

    QTextDocument *editorDocument(QObject *editor) const {
        auto *quickDocument = editor->property("textDocument").value<QQuickTextDocument *>();
        return quickDocument ? quickDocument->textDocument() : nullptr;
    }

    QQuickItem *findNamedItem(QQuickItem *root, const QString &name) const {
        if (!root)
            return nullptr;
        if (root->objectName() == name)
            return root;
        const auto children = root->childItems();
        for (QQuickItem *child : children) {
            if (QQuickItem *found = findNamedItem(child, name))
                return found;
        }
        return nullptr;
    }

    QQuickItem *findNamedItem(QObject *window, const QString &name) const {
        auto *quickWindow = qobject_cast<QQuickWindow *>(window);
        return quickWindow ? findNamedItem(quickWindow->contentItem(), name) : nullptr;
    }

    QList<QQuickItem *> findNamedItems(QQuickItem *root, const QString &name) const {
        QList<QQuickItem *> found;
        if (!root)
            return found;
        if (root->objectName() == name)
            found.append(root);
        const auto children = root->childItems();
        for (QQuickItem *child : children)
            found += findNamedItems(child, name);
        return found;
    }

    QList<QQuickItem *> findNamedItems(QObject *window, const QString &name) const {
        auto *quickWindow = qobject_cast<QQuickWindow *>(window);
        return quickWindow ? findNamedItems(quickWindow->contentItem(), name) : QList<QQuickItem *>{};
    }
};

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    int status = 0;
    {
        OmaOwnNoteTest test;
        status |= QTest::qExec(&test, argc, argv);
    }
    return status;
}

#include "tst_oma_own_note.moc"
