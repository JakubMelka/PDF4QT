// MIT License
//
// Copyright (c) 2018-2026 Jakub Melka and Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

// Tests of the content of the new application (step 2): the What's New files
// (parser, catalog, the bundled files against RELEASES.txt), the update check
// against a loopback HTTP server, the What's New pages, the Markdown parser and
// view and the user manual /help.

#include "pdfapppagelayout.h"
#include "pdfappcontext.h"
#include "pdfapphelpcatalog.h"
#include "pdfapphelppage.h"
#include "pdfappmainwindow.h"
#include "pdfappmarkdown.h"
#include "pdfappmarkdownview.h"
#include "pdfappnavigator.h"
#include "pdfapppagenotfound.h"
#include "pdfapproute.h"
#include "pdfapproutetable.h"
#include "pdfapptab.h"
#include "pdfappwhatsnewpage.h"
#include "pdfappworkspace.h"
#include "pdfdatabase.h"
#include "pdfdatabasenotifier.h"
#include "pdfdatabasestatement.h"
#include "pdfsettingdefinitions.h"
#include "pdftoolrepository.h"
#include "pdfupdatechecker.h"
#include "pdfwhatsnew.h"

#include "pdfdocumenttoolcatalog.h"
#include "pdfstylepainter.h"
#include "pdfthemeiconengine.h"
#include "pdfthememanager.h"
#include "pdfwidgetstyle.h"

#include <QApplication>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QImageReader>
#include <QLabel>
#include <QProxyStyle>
#include <QFile>
#include <QPainter>
#include <QRandomGenerator>
#include <QScrollBar>
#include <QSignalSpy>
#include <QStandardItemModel>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>

#include <algorithm>
#include <map>
#include <memory>
#include <set>

/// HTTP server of the test on the loopback interface (the latest release of GitHub)
class TestReleaseServer
{
public:
    struct Reply
    {
        int status = 200;
        QByteArray body;
        QList<QPair<QByteArray, QByteArray>> headers;
        bool isAnswered = true;
        int delay = 0;
    };

    bool start()
    {
        QObject::connect(&m_server, &QTcpServer::newConnection, &m_server, [this]() { onConnection(); });
        return m_server.listen(QHostAddress::LocalHost);
    }

    QUrl getUrl(const QString& path = QStringLiteral("/repos/JakubMelka/PDF4QT/releases/latest")) const
    {
        return QUrl(QStringLiteral("http://127.0.0.1:%1%2").arg(m_server.serverPort()).arg(path));
    }

    static QByteArray createRelease(const QString& tag)
    {
        return QStringLiteral("{\"tag_name\": \"%1\", \"html_url\": \"https://github.com/JakubMelka/PDF4QT/releases/tag/%1\", \"draft\": false}").arg(tag).toUtf8();
    }

    Reply reply;
    QList<QByteArray> requests;

    /// Connections closed (by the client or after the answer): a test waits for
    /// the close of an aborted request instead of a fixed time
    int closedConnections = 0;

private:
    void onConnection()
    {
        while (QTcpSocket* socket = m_server.nextPendingConnection())
        {
            QObject::connect(socket, &QTcpSocket::disconnected, &m_server, [this]() { ++closedConnections; });
            auto buffer = std::make_shared<QByteArray>();
            QObject::connect(socket, &QTcpSocket::readyRead, socket, [this, socket, buffer]()
            {
                buffer->append(socket->readAll());
                const qsizetype headerEnd = buffer->indexOf("\r\n\r\n");
                if (headerEnd == -1)
                {
                    return;
                }

                const QByteArray request = buffer->left(headerEnd);
                buffer->clear();
                requests.append(request);
                if (!reply.isAnswered)
                {
                    return;
                }

                QPointer<QTcpSocket> guard(socket);
                const Reply answer = reply;
                auto send = [guard, answer]()
                {
                    if (!guard)
                    {
                        return;
                    }
                    QByteArray response = "HTTP/1.1 " + QByteArray::number(answer.status) + " Status\r\n";
                    response += "Content-Type: application/json\r\n";
                    response += "Content-Length: " + QByteArray::number(answer.body.size()) + "\r\n";
                    for (const auto& header : answer.headers)
                    {
                        response += header.first + ": " + header.second + "\r\n";
                    }
                    response += "Connection: close\r\n\r\n";
                    response += answer.body;
                    guard->write(response);
                    guard->disconnectFromHost();
                };

                if (answer.delay > 0)
                {
                    QTimer::singleShot(answer.delay, socket, send);
                }
                else
                {
                    send();
                }
            });
            QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        }
    }

    QTcpServer m_server;
};

/// Style, which activates the items of views by a single click (some platforms do)
class SingleClickStyle : public QProxyStyle
{
public:
    virtual int styleHint(StyleHint hint, const QStyleOption* option, const QWidget* widget, QStyleHintReturn* returnData) const override
    {
        if (hint == QStyle::SH_ItemView_ActivateItemOnSingleClick)
        {
            return 1;
        }
        return QProxyStyle::styleHint(hint, option, widget, returnData);
    }
};

/// Context with a portable memory database (or a database file), a window with
/// the pages and the update checker pointed to the server of the test
class ContentFixture
{
public:
    explicit ContentFixture(bool createWindow = true, const QString& databaseFile = QString())
    {
        QStringList arguments = { QStringLiteral("PDF4QT") };
        if (databaseFile.isEmpty())
        {
            arguments << QStringLiteral("--portable");
        }
        context = std::make_unique<pdf::PDFAppContext>(pdf::PDFAppCommandLine::parse(arguments, QDir::currentPath()));

        pdf::PDFDatabaseOptions options;
        options.memoryName = QStringLiteral("/pdf4qt-content-%1-%2").arg(QCoreApplication::applicationPid()).arg(++s_counter);
        options.filePath = databaseFile;
        context->open(options);
        if (context->getNotifier())
        {
            context->getNotifier()->setPollInterval(20);
        }

        // Never GitHub: a port of the loopback, which refuses the connection
        checker()->setUrl(QUrl(QStringLiteral("http://127.0.0.1:1/latest")));
        checker()->setTimeout(2000);

        if (createWindow)
        {
            window = new pdf::PDFAppMainWindow(context.get());
            window->getNavigator()->setExternalUrlsEnabled(false);
            window->resize(1440, 977);
            window->show();
            (void)QTest::qWaitForWindowExposed(window.data());
            QCoreApplication::processEvents();
        }
    }

    ~ContentFixture()
    {
        delete window.data();
        context.reset();
        pdf::PDFThemeManager::getInstance()->setScheme(pdf::PDFThemeColors::Scheme::Light);
    }

    pdf::PDFUpdateChecker* checker() const { return pdf::PDFUpdateChecker::getInstance(context.get()); }
    pdf::PDFAppTab* currentTab() const { return window ? window->getCurrentTab() : nullptr; }
    pdf::PDFAppPage* currentPage() const { return currentTab() ? currentTab()->getPage() : nullptr; }

    template<typename T>
    T* page() const { return qobject_cast<T*>(currentPage()); }

    void navigate(const QString& text)
    {
        window->getNavigator()->navigate(pdf::PDFAppRoute::parse(text), pdf::PDFAppNavigationMode::Push);
        QCoreApplication::processEvents();
        QCoreApplication::processEvents();
    }

    /// Writes the stored values of the update check, so that no check is due
    void setUpdateValues(bool autoCheck, qint64 lastCheck, qint64 lastSuccess, const QString& latest, const QString& error)
    {
        pdf::PDFSettingsRepository settings = context->getSettings();
        QVERIFY(!settings.setValue(pdf::PDFSettingKeys::updateAutoCheck(), autoCheck).isError());
        QVERIFY(!settings.setValue(pdf::PDFSettingKeys::updateLastCheck(), lastCheck).isError());
        QVERIFY(!settings.setValue(pdf::PDFSettingKeys::updateLastSuccess(), lastSuccess).isError());
        QVERIFY(!settings.setValue(pdf::PDFSettingKeys::updateLatestVersion(), latest).isError());
        QVERIFY(!settings.setValue(pdf::PDFSettingKeys::updateLastError(), error).isError());
    }

    QScrollArea* scrollArea() const
    {
        pdf::PDFAppWorkspaceTab* tab = qobject_cast<pdf::PDFAppWorkspaceTab*>(currentTab());
        return tab ? tab->getPageScrollArea() : nullptr;
    }

    std::unique_ptr<pdf::PDFAppContext> context;
    QPointer<pdf::PDFAppMainWindow> window;

    static int s_counter;
};

int ContentFixture::s_counter = 0;

class AppContentTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    // Markdown
    void markdownBlocks();
    void markdownInline_data();
    void markdownInline();
    void markdownLinks_data();
    void markdownLinks();
    void markdownAnchors();
    void markdownStructure_data();
    void markdownStructure();
    void markdownDocumentApi();
    void markdownHostile_data();
    void markdownHostile();
    void markdownView();
    void markdownGeometry();
    void markdownViewKeyboard();
    void markdownReviewFixes();
    void markdownViewDeletedBySlot();
    void markdownViewNoLeak_data();
    void markdownViewNoLeak();
    void markdownViewStates();
    void markdownViewPictures();

    // What's New files
    void whatsNewParser();
    void whatsNewParserErrors_data();
    void whatsNewParserErrors();
    void whatsNewParserRandom();
    void whatsNewCatalog();
    void whatsNewTranslation();
    void whatsNewBundledFiles();
    void whatsNewParserEdges();
    void whatsNewCatalogLimits();

    // Update check
    void updateStatus_data();
    void updateStatus();
    void updateFormatTime();
    void updateReplies_data();
    void updateReplies();
    void updateOncePerDay();
    void updateTwoInstances();
    void updateCheckerDeleted();
    void updateSlowServer();
    void updateReplyErrors_data();
    void updateReplyErrors();
    void updateCheckerWithoutDatabase();
    void updateDatabaseLocked();

    // Pages
    void whatsNewPage();
    void whatsNewPreviousVersion();
    void whatsNewVersionHistory();
    void whatsNewSections();
    void whatsNewToolPins();
    void whatsNewUpdateCard_data();
    void whatsNewUpdateCard();
    void whatsNewCheckButton();
    void whatsNewDeletedDuringCheck();
    void releaseNotesPage();
    void contentPagesNoLeak_data();
    void contentPagesNoLeak();
    void contentPagesMinimalWindow();
    void whatsNewPageStandalone();
    void whatsNewPageCatalog();
    void releaseNotesPageStates();

    // Manual
    void helpCatalog();
    void helpCatalogErrors_data();
    void helpCatalogErrors();
    void helpLinks();
    void helpTranslation();
    void helpPage();
    void helpPageAnchor();
    void helpPageNavigation();
    void helpCatalogLimits();
    void helpPageCatalog();
    void helpPageContentsClicks();

    void screenshots();

private:
    static QImage render(QWidget* widget, qreal scale);
    static int countLeakPixels(const QImage& image);
    static QString spansToString(const pdf::PDFAppMarkdownSpans& spans);
    static QString readSourceFile(const QString& relativePath);
    static QString validWhatsNewFile();
    static QString dumpBlocks(const std::vector<pdf::PDFAppMarkdownBlock>& blocks);
    static void writeFile(const QString& path, const QByteArray& data);
    static QString findPictureResource(int minimumHeight);
    static void writeTestCatalog(const QString& folder);

    struct ReleaseSection
    {
        QString version;
        QString date;
        bool isRemoved = false;
        std::vector<std::pair<int, QString>> issues;
    };
    static std::vector<ReleaseSection> parseReleases(const QString& text);
};

void AppContentTest::initTestCase()
{
    QApplication::setStyle(new pdf::PDFWidgetStyle());
    qRegisterMetaType<pdf::PDFAppRoute>();
    qRegisterMetaType<pdf::PDFAppNavigationMode>();
    qRegisterMetaType<pdf::PDFAppMarkdownLinkKind>();
}

QImage AppContentTest::render(QWidget* widget, qreal scale)
{
    QImage image(widget->size() * scale, QImage::Format_ARGB32_Premultiplied);
    image.setDevicePixelRatio(scale);
    image.fill(pdf::PDFStylePainter::color(pdf::PDFThemeColors::BgCanvas));
    QPainter painter(&image);
    widget->render(&painter, QPoint(), QRegion(), QWidget::DrawChildren);
    painter.end();
    return image;
}

int AppContentTest::countLeakPixels(const QImage& image)
{
    const QRgb leak = pdf::PDFThemeColors::getLeakColor().rgb() & 0xFFFFFF;
    int count = 0;
    for (int y = 0; y < image.height(); ++y)
    {
        const QRgb* line = reinterpret_cast<const QRgb*>(image.constScanLine(y));
        for (int x = 0; x < image.width(); ++x)
        {
            if ((line[x] & 0xFFFFFF) == leak)
            {
                ++count;
            }
        }
    }
    return count;
}

QString AppContentTest::spansToString(const pdf::PDFAppMarkdownSpans& spans)
{
    QStringList parts;
    for (const pdf::PDFAppMarkdownSpan& span : spans)
    {
        QString flags;
        flags += span.bold ? QStringLiteral("b") : QString();
        flags += span.italic ? QStringLiteral("i") : QString();
        flags += span.code ? QStringLiteral("c") : QString();
        if (span.isLink())
        {
            flags += QStringLiteral("l=") + span.link;
        }
        parts << (flags.isEmpty() ? span.text : QStringLiteral("[%1|%2]").arg(flags, span.text));
    }
    return parts.join(QString());
}

QString AppContentTest::readSourceFile(const QString& relativePath)
{
    QFile file(QDir(QStringLiteral(PDF4QT_SOURCE_DIRECTORY)).filePath(relativePath));
    if (!file.open(QFile::ReadOnly))
    {
        return QString();
    }
    return QString::fromUtf8(file.readAll());
}

QString AppContentTest::validWhatsNewFile()
{
    return QStringLiteral(
        "---\n"
        "version: 1.6.0.0\n"
        "date: 2026-11-20\n"
        "summary: One unified app.\n"
        "---\n"
        "\n"
        "## Overview\n"
        "\n"
        "This release merges the applications\n"
        "into one.\n"
        "\n"
        "## Highlights\n"
        "\n"
        "### One app for everything\n"
        "icon: layout \u00B7 hue: blue\n"
        "Editor and Viewer are one application.\n"
        "\n"
        "### Faster loading\n"
        "icon: zap \u00B7 hue: amber\n"
        "Large documents open faster.\n"
        "\n"
        "## New tools\n"
        "\n"
        "- optimize.compress\n"
        "- pages.merge\n"
        "\n"
        "## Fixed issues\n"
        "\n"
        "- #430 \u00B7 2026-10-12 \u00B7 Text layout optimization\n"
        "  Large documents lay out text faster.\n"
        "- #417 \u00B7 Visible signatures\n"
        "\n"
        "## Release notes\n"
        "\n"
        "## Free Markdown\n"
        "\n"
        "Anything **here**.\n");
}

QString AppContentTest::dumpBlocks(const std::vector<pdf::PDFAppMarkdownBlock>& blocks)
{
    // A compact text of the structure: P(spans) H<level>#<anchor>(spans) UL[items]
    // OL<start>[items] {item blocks} C(code, lines by |) Q<callout>(title){blocks}
    // T<L/C/R per column>[cells by |, rows by /] I(alt,source) R
    using Type = pdf::PDFAppMarkdownBlock::Type;
    QStringList parts;
    for (const pdf::PDFAppMarkdownBlock& block : blocks)
    {
        switch (block.type)
        {
            case Type::Paragraph:
                parts << QStringLiteral("P(") + spansToString(block.spans) + QStringLiteral(")");
                break;
            case Type::Heading:
                parts << QStringLiteral("H%1#").arg(block.level) + block.anchor + QStringLiteral("(") + spansToString(block.spans) + QStringLiteral(")");
                break;
            case Type::List:
                parts << (block.ordered ? QStringLiteral("OL%1").arg(block.startNumber) : QStringLiteral("UL")) + QStringLiteral("[") + dumpBlocks(block.children) + QStringLiteral("]");
                break;
            case Type::ListItem:
                parts << QStringLiteral("{") + dumpBlocks(block.children) + QStringLiteral("}");
                break;
            case Type::CodeBlock:
                parts << QStringLiteral("C(") + QString(block.text).replace(QChar(u'\n'), QChar(u'|')) + QStringLiteral(")");
                break;
            case Type::Quote:
                parts << QStringLiteral("Q%1(").arg(int(block.callout)) + spansToString(block.spans) + QStringLiteral("){") + dumpBlocks(block.children) + QStringLiteral("}");
                break;
            case Type::Table:
            {
                QString alignments;
                for (const int alignment : block.alignments)
                {
                    alignments += alignment == Qt::AlignLeft ? QChar(u'L') : (alignment == Qt::AlignRight ? QChar(u'R') : QChar(u'C'));
                }
                QStringList rows;
                for (const std::vector<pdf::PDFAppMarkdownSpans>& row : block.rows)
                {
                    QStringList cells;
                    for (const pdf::PDFAppMarkdownSpans& cell : row)
                    {
                        cells << spansToString(cell);
                    }
                    rows << cells.join(QChar(u'|'));
                }
                parts << QStringLiteral("T") + alignments + QStringLiteral("[") + rows.join(QChar(u'/')) + QStringLiteral("]");
                break;
            }
            case Type::Image:
                parts << QStringLiteral("I(") + block.text + QStringLiteral(",") + block.source + QStringLiteral(")");
                break;
            case Type::Rule:
                parts << QStringLiteral("R");
                break;
        }
    }
    return parts.join(QChar(u' '));
}

void AppContentTest::writeFile(const QString& path, const QByteArray& data)
{
    QFile file(path);
    QVERIFY2(file.open(QFile::WriteOnly), qPrintable(path));
    QCOMPARE(file.write(data), data.size());
}

QString AppContentTest::findPictureResource(int minimumHeight)
{
    // A raster picture of the resources of Qt (the Markdown view reads only resources)
    QDirIterator iterator(QStringLiteral(":/"), QStringList() << QStringLiteral("*.png"), QDir::Files, QDirIterator::Subdirectories);
    while (iterator.hasNext())
    {
        const QString path = iterator.next();
        const QSize size = QImageReader(path).size();
        if (size.isValid() && size.height() >= minimumHeight && size.width() <= 4096 && size.height() <= 4096 && !path.contains(QStringLiteral("..")))
        {
            return path;
        }
    }
    return QString();
}

// ------------------------------------------------------------------- Markdown

void AppContentTest::markdownBlocks()
{
    const QString text = QStringLiteral(
        "# Title {#custom-anchor}\n"
        "\n"
        "First paragraph\n"
        "continues here.\n"
        "\n"
        "## What you need\n"
        "\n"
        "- one\n"
        "- two\n"
        "  - nested\n"
        "- three\n"
        "\n"
        "3. third\n"
        "4. fourth\n"
        "\n"
        "```cpp\n"
        "int x = 1;\n"
        "  <b>not html</b>\n"
        "```\n"
        "\n"
        "> plain quote\n"
        "\n"
        "> [!WARNING] Careful\n"
        "> Body of the warning.\n"
        "\n"
        "| Kind | What | Note |\n"
        "| --- | :---: | ---: |\n"
        "| A | b | c |\n"
        "| D | e \\| f |\n"
        "\n"
        "![Logo](images/logo.png)\n"
        "\n"
        "---\n"
        "\n"
        "<div>raw html</div>\n");

    const pdf::PDFAppMarkdownDocument document = pdf::PDFAppMarkdownParser::parse(text);
    using Type = pdf::PDFAppMarkdownBlock::Type;
    const std::vector<Type> expected = { Type::Heading, Type::Paragraph, Type::Heading, Type::List, Type::List, Type::CodeBlock,
                                         Type::Quote, Type::Quote, Type::Table, Type::Image, Type::Rule, Type::Paragraph };
    QCOMPARE(document.blocks.size(), expected.size());
    for (size_t i = 0; i < expected.size(); ++i)
    {
        QCOMPARE(int(document.blocks[i].type), int(expected[i]));
    }

    QCOMPARE(document.blocks[0].level, 1);
    QCOMPARE(document.blocks[0].anchor, QStringLiteral("custom-anchor"));
    QCOMPARE(pdf::PDFAppMarkdownParser::toPlainText(document.blocks[0].spans), QStringLiteral("Title"));
    QCOMPARE(pdf::PDFAppMarkdownParser::toPlainText(document.blocks[1].spans), QStringLiteral("First paragraph continues here."));
    QCOMPARE(document.blocks[2].anchor, QStringLiteral("what-you-need"));

    const pdf::PDFAppMarkdownBlock& list = document.blocks[3];
    QVERIFY(!list.ordered);
    QCOMPARE(list.children.size(), size_t(3));
    QCOMPARE(list.children[1].children.size(), size_t(2));
    QCOMPARE(int(list.children[1].children[1].type), int(Type::List));
    QCOMPARE(pdf::PDFAppMarkdownParser::toPlainText(list.children[1].children[1].children[0].children[0].spans), QStringLiteral("nested"));

    QVERIFY(document.blocks[4].ordered);
    QCOMPARE(document.blocks[4].startNumber, 3);
    QCOMPARE(document.blocks[5].text, QStringLiteral("int x = 1;\n  <b>not html</b>"));
    QCOMPARE(int(document.blocks[6].callout), int(pdf::PDFAppMarkdownCallout::None));
    QCOMPARE(int(document.blocks[7].callout), int(pdf::PDFAppMarkdownCallout::Warning));
    QCOMPARE(pdf::PDFAppMarkdownParser::toPlainText(document.blocks[7].spans), QStringLiteral("Careful"));

    const pdf::PDFAppMarkdownBlock& table = document.blocks[8];
    QCOMPARE(table.rows.size(), size_t(3));
    QCOMPARE(table.alignments, (std::vector<int>{ Qt::AlignLeft, Qt::AlignHCenter, Qt::AlignRight }));
    QCOMPARE(pdf::PDFAppMarkdownParser::toPlainText(table.rows[2][1]), QStringLiteral("e | f"));
    QCOMPARE(pdf::PDFAppMarkdownParser::toPlainText(table.rows[2][2]), QString());

    QCOMPARE(document.blocks[9].source, QStringLiteral("images/logo.png"));
    QCOMPARE(document.blocks[9].text, QStringLiteral("Logo"));

    // Raw HTML stays a text
    QCOMPARE(pdf::PDFAppMarkdownParser::toPlainText(document.blocks[11].spans), QStringLiteral("<div>raw html</div>"));

    QVERIFY(document.hasAnchor(QStringLiteral("what-you-need")));
    QVERIFY(!document.hasAnchor(QStringLiteral("title")));
    QVERIFY(document.toPlainText().contains(QStringLiteral("Body of the warning.")));
}

void AppContentTest::markdownInline_data()
{
    QTest::addColumn<QString>("input");
    QTest::addColumn<QString>("expected");

    QTest::newRow("plain") << "plain text" << "plain text";
    QTest::newRow("bold") << "a **b** c" << "a [b|b] c";
    QTest::newRow("italic star") << "a *b* c" << "a [i|b] c";
    QTest::newRow("italic underscore") << "a _b_ c" << "a [i|b] c";
    QTest::newRow("bold italic") << "***x***" << "[bi|x]";
    QTest::newRow("snake case") << "snake_case_name" << "snake_case_name";
    QTest::newRow("unmatched") << "a * b ** c" << "a * b ** c";
    QTest::newRow("unclosed") << "*open" << "*open";
    QTest::newRow("code") << "use `x * y` here" << "use [c|x * y] here";
    QTest::newRow("code double") << "``a ` b``" << "[c|a ` b]";
    QTest::newRow("code spaces") << "` a `" << "[c|a]";
    QTest::newRow("code unclosed") << "``a`" << "``a`";
    QTest::newRow("escape") << "\\*not\\* \\[x\\]" << "*not* [x]";
    QTest::newRow("link route") << "[Settings](/settings/general)" << "[l=/settings/general|Settings]";
    QTest::newRow("link https") << "see [site](https://example.com/a)" << "see [l=https://example.com/a|site]";
    QTest::newRow("link anchor") << "[up](#top)" << "[l=#top|up]";
    QTest::newRow("link bold") << "[**b** t](/home)" << "[bl=/home|b][l=/home| t]";
    QTest::newRow("link javascript") << "[x](javascript:alert(1))" << "x)";
    QTest::newRow("link file") << "[x](file:///c:/a.pdf)" << "x";
    QTest::newRow("link http") << "[x](http://example.com)" << "x";
    QTest::newRow("link relative") << "[x](other.md)" << "x";
    QTest::newRow("link protocol relative") << "[x](//evil.com)" << "x";
    QTest::newRow("link space") << "[x](/a b)" << "[x](/a b)";
    QTest::newRow("brackets") << "[idea] choose" << "[idea] choose";
    QTest::newRow("image inline") << "an ![alt](x.png) image" << "an alt image";
    QTest::newRow("html") << "<script>alert(1)</script>" << "<script>alert(1)</script>";
    QTest::newRow("nested emphasis") << "**a *b* c**" << "[b|a ][bi|b][b| c]";
    QTest::newRow("crossing") << "*a **b* c**" << "[i|a **b] c**";
    QTest::newRow("escape letter") << "\\a\\*" << "\\a*";
    const QString backticks = QStringLiteral("a ") + QString(33, QLatin1Char('`')) + QStringLiteral("b") + QString(33, QLatin1Char('`'));
    QTest::newRow("too many backticks") << backticks << backticks;
    QTest::newRow("bracket at end") << "x [a]" << "x [a]";
    QTest::newRow("parenthesis in code") << "[a]`(x)`" << "[a][c|(x)]";
    const QString longLink = QStringLiteral("[a](/") + QString(3000, QLatin1Char('b')) + QStringLiteral(")");
    QTest::newRow("link too long") << longLink << longLink;
    QTest::newRow("escaped picture") << "\\![a](/home)" << "![l=/home|a]";
    QTest::newRow("long emphasis run") << "****a****" << "****a****";
    QTest::newRow("trailing backslash") << "a\\" << "a\\";
}

void AppContentTest::markdownInline()
{
    QFETCH(QString, input);
    QFETCH(QString, expected);
    QCOMPARE(spansToString(pdf::PDFAppMarkdownParser::parseInline(input)), expected);
}

void AppContentTest::markdownLinks_data()
{
    QTest::addColumn<QString>("target");
    QTest::addColumn<int>("kind");

    using Kind = pdf::PDFAppMarkdownLinkKind;
    QTest::newRow("route") << "/help/portable-mode#data" << int(Kind::Route);
    QTest::newRow("https") << "https://github.com/JakubMelka/PDF4QT" << int(Kind::External);
    QTest::newRow("anchor") << "#what-you-need" << int(Kind::Anchor);
    QTest::newRow("anchor invalid") << "#What You" << int(Kind::None);
    QTest::newRow("anchor empty") << "#" << int(Kind::None);
    QTest::newRow("http") << "http://example.com" << int(Kind::None);
    QTest::newRow("javascript") << "javascript:alert(1)" << int(Kind::None);
    QTest::newRow("file") << "file:///etc/passwd" << int(Kind::None);
    QTest::newRow("mailto") << "mailto:a@b.c" << int(Kind::None);
    QTest::newRow("protocol relative") << "//evil.com/x" << int(Kind::None);
    QTest::newRow("https user") << "https://user:password@example.com/" << int(Kind::None);
    QTest::newRow("https no host") << "https:///path" << int(Kind::None);
    QTest::newRow("control") << QStringLiteral("/home\u0001") << int(Kind::None);
    QTest::newRow("empty") << "" << int(Kind::None);
    QTest::newRow("too long") << QStringLiteral("/") + QString(3000, QLatin1Char('a')) << int(Kind::None);
    QTest::newRow("anchor uppercase") << "#Abc" << int(Kind::None);
    QTest::newRow("https invalid") << "https://a%zz/" << int(Kind::None);
    QTest::newRow("delete character") << QStringLiteral("/a\u007F") << int(Kind::None);
}

void AppContentTest::markdownLinks()
{
    QFETCH(QString, target);
    QFETCH(int, kind);
    QCOMPARE(int(pdf::PDFAppMarkdownParser::classifyLink(target)), kind);
}

void AppContentTest::markdownAnchors()
{
    QCOMPARE(pdf::PDFAppMarkdownParser::createAnchor(QStringLiteral("What you need")), QStringLiteral("what-you-need"));
    QCOMPARE(pdf::PDFAppMarkdownParser::createAnchor(QStringLiteral("  Step-by-step: 1, 2 & 3!  ")), QStringLiteral("step-by-step-1-2-3"));
    QCOMPARE(pdf::PDFAppMarkdownParser::createAnchor(QStringLiteral("Příliš žluťoučký")), QStringLiteral("pli-luouk"));
    QVERIFY(pdf::PDFAppMarkdownParser::createAnchor(QString(500, QLatin1Char('a'))).size() <= 64);

    const pdf::PDFAppMarkdownDocument document = pdf::PDFAppMarkdownParser::parse(QStringLiteral("## Usage\n\n## Usage\n\n## Usage {#usage-1}\n\n## !!!\n"));
    const std::vector<pdf::PDFAppMarkdownHeading> headings = document.getHeadings();
    QCOMPARE(headings.size(), size_t(4));
    QCOMPARE(headings[0].anchor, QStringLiteral("usage"));
    QCOMPARE(headings[1].anchor, QStringLiteral("usage-1"));
    QVERIFY(headings[2].anchor != headings[1].anchor);
    QCOMPARE(headings[3].anchor, QStringLiteral("section"));
}

void AppContentTest::markdownStructure_data()
{
    QTest::addColumn<QString>("text");
    QTest::addColumn<QString>("expected");

    // Normalization of the text
    QTest::newRow("byte order mark") << QStringLiteral("\uFEFFText") << "P(Text)";
    QTest::newRow("carriage returns") << "a\rb\r\n\rc" << "P(a b) P(c)";
    QTest::newRow("control characters") << QStringLiteral("a\u007Fb\u0085c\u2028d\u2029e\uFEFFf\u0001g") << "P(abcdefg)";

    // Code fences
    QTest::newRow("trailing carriage return") << "a\r" << "P(a)";
    QTest::newRow("tilde fence") << "~~~\ncode\n~~~" << "C(code)";
    QTest::newRow("backtick in info") << "```a`b\ntext" << "P(```a`b text)";
    QTest::newRow("fence closings") << "```\none\n~~~\n``\n``` x\n```" << "C(one|~~~|``|``` x)";
    QTest::newRow("shorter closing") << "````\na\n```\n````" << "C(a|```)";

    // Headings
    QTest::newRow("only hash") << "#" << "H1#section()";
    QTest::newRow("seven hashes") << "####### x" << "P(####### x)";
    QTest::newRow("closing hashes") << "## Title ##" << "H2#title(Title)";
    QTest::newRow("hash in text") << "## C#" << "H2#c(C#)";
    QTest::newRow("only closing hashes") << "### ###" << "H3#section()";
    QTest::newRow("brace without anchor") << "# Set {x}" << "H1#set-x(Set {x})";
    QTest::newRow("long explicit anchor") << QStringLiteral("# T {#") + QString(100, QLatin1Char('a')) + QStringLiteral("}") << QStringLiteral("H1#") + QString(64, QLatin1Char('a')) + QStringLiteral("(T)");
    QTest::newRow("anchor characters") << "# T {#Ab-9_x!}" << "H1#ab-9_x(T)";
    QTest::newRow("anchor symbols") << "# T {#a~^b}" << "H1#ab(T)";
    QTest::newRow("anchor collision") << "## A-1\n## A\n## A" << "H2#a-1(A-1) H2#a(A) H2#a-2(A)";

    // A paragraph is ended by another block
    QTest::newRow("paragraph heading") << "para\n# H" << "P(para) H1#h(H)";
    QTest::newRow("paragraph fence") << "para\n```\nx\n```" << "P(para) C(x)";
    QTest::newRow("paragraph rule") << "para\n***" << "P(para) R";
    QTest::newRow("paragraph table") << "para\n| a | b |\n| - | - |\n| 1 | 2 |" << "P(para) TLL[a|b/1|2]";

    // Quotes
    QTest::newRow("quote without space") << ">quote" << "Q0(){P(quote)}";
    QTest::newRow("lazy quote") << "> a\nb" << "Q0(){P(a b)}";
    QTest::newRow("quote heading") << "> a\n# H" << "Q0(){P(a)} H1#h(H)";
    QTest::newRow("quote blank text") << "> a\n>\nb" << "Q0(){P(a)} P(b)";

    // Lists
    QTest::newRow("ten digits") << "1234567890. x" << "P(1234567890. x)";
    QTest::newRow("parenthesis marker") << "1) one\n2) two" << "OL1[{P(one)} {P(two)}]";
    QTest::newRow("empty item") << "-\n- b" << "UL[{} {P(b)}]";
    QTest::newRow("marker with spaces") << "-   " << "UL[{}]";
    QTest::newRow("wide gap") << "-      x" << "UL[{P(x)}]";
    QTest::newRow("lazy item") << "- a\nb" << "UL[{P(a b)}]";
    QTest::newRow("item heading") << "- a\n# H" << "UL[{P(a)}] H1#h(H)";
    QTest::newRow("item continuation") << "- a\n\n  b" << "UL[{P(a) P(b)}]";
    QTest::newRow("bullet change") << "- a\n* b" << "UL[{P(a)}] UL[{P(b)}]";
    QTest::newRow("list rule") << "- a\n* * *" << "UL[{P(a)}] R";

    // Tables
    QTest::newRow("escaped last pipe") << "| a | b \\|\n| --- | --- |" << "TLL[a|b |]";
    QTest::newRow("empty delimiter cell") << "| a | b |\n| --- | |" << "P(| a | b | | --- | |)";
    QTest::newRow("colon delimiter") << "| a |\n| : |" << "P(| a | | : |)";
    QTest::newRow("explicit alignments") << "| a | b |\n| :-- | --: |" << "TLR[a|b]";
    QTest::newRow("table at end") << "| a |\n| - |" << "TL[a]";
    QTest::newRow("table text") << "| a |\n| - |\ntext" << "TL[a] P(text)";
    QTest::newRow("table fence") << "| a |\n| - |\n```|\nx\n```" << "TL[a] C(x)";
    QTest::newRow("table heading") << "| a |\n| - |\n# H |" << "TL[a] H1#h(H |)";
    QTest::newRow("table quote") << "| a |\n| - |\n> q |" << "TL[a] Q0(){P(q |)}";

    // Pictures
    QTest::newRow("picture and text") << "![a](b.png) text" << "P(a text)";
    QTest::newRow("picture no target") << "![x)" << "P(![x))";
    QTest::newRow("picture brackets") << "![a[b]](x.png)" << "P(a[b])";
    QTest::newRow("picture empty target") << "![a]()" << "P(a)";
    QTest::newRow("picture space") << "![a](x y.png)" << "P(![a](x y.png))";
    QTest::newRow("picture parenthesis") << "![a](x(y).png)" << "P(a.png))";
    QTest::newRow("picture empty alt") << "![](x.png)" << "I(,x.png)";
    QTest::newRow("picture bracket in alt") << "![a]b](x.png)" << "P(![a]b](x.png))";
    QTest::newRow("picture closing parenthesis") << "![a](x).png)" << "P(a.png))";
    const QString longPicture = QStringLiteral("![a](") + QString(5000, QLatin1Char('p')) + QStringLiteral(")");
    QTest::newRow("picture too long") << longPicture << QStringLiteral("P(") + longPicture + QStringLiteral(")");
}

void AppContentTest::markdownStructure()
{
    QFETCH(QString, text);
    QFETCH(QString, expected);
    QCOMPARE(dumpBlocks(pdf::PDFAppMarkdownParser::parse(text).blocks), expected);
}

void AppContentTest::markdownDocumentApi()
{
    // Anchors: the limit of the length is applied also to a pending dash
    QCOMPARE(pdf::PDFAppMarkdownParser::createAnchor(QStringLiteral("a_b")), QStringLiteral("a-b"));
    QCOMPARE(pdf::PDFAppMarkdownParser::createAnchor(QString(63, QLatin1Char('a')) + QStringLiteral(" b")), QString(63, QLatin1Char('a')));
    QCOMPARE(pdf::PDFAppMarkdownParser::createAnchor(QStringLiteral("x -")), QStringLiteral("x"));

    // Links: one link of more spans is listed once, a link repeated later again
    const pdf::PDFAppMarkdownDocument links = pdf::PDFAppMarkdownParser::parse(QStringLiteral("[**b** t](/home) [c](/home)\n\n| [x](/open) | y |\n| - | - |\n| [z](#top) | w |"));
    QCOMPARE(links.getLinks(), (QStringList{ QStringLiteral("/home"), QStringLiteral("/home"), QStringLiteral("/open"), QStringLiteral("#top") }));

    // The plain text of every kind of block
    const pdf::PDFAppMarkdownDocument document = pdf::PDFAppMarkdownParser::parse(QStringLiteral(
        "# H\n\npara\n\n- item\n\n```\ncode\n```\n\n> [!TIP] Title\n> body\n\n> plain\n\n| a | b |\n| - | - |\n| 1 | 2 |\n\n![alt](x.png)\n\n---\n"));
    QCOMPARE(document.toPlainText(), QStringLiteral("H\npara\nitem\ncode\nTitle\nbody\nplain\na\tb\n1\t2\nalt"));
    QVERIFY(!document.truncated);
}

void AppContentTest::markdownHostile_data()
{
    QTest::addColumn<QString>("text");

    QTest::newRow("deep quotes") << QString(100000, QLatin1Char('>')) + QStringLiteral(" x");
    QTest::newRow("deep lists") << [] { QString text; for (int i = 0; i < 2000; ++i) { text += QString(i * 2, QLatin1Char(' ')) + QStringLiteral("- item\n"); } return text; }();
    QTest::newRow("list markers") << QString(200000, QLatin1Char('-')).replace(QStringLiteral("--"), QStringLiteral("- "));
    QTest::newRow("brackets") << QString(300000, QLatin1Char('['));
    QTest::newRow("link starts") << QStringLiteral("[a](").repeated(200000);
    QTest::newRow("closing") << QStringLiteral("](").repeated(200000);
    QTest::newRow("stars") << QStringLiteral("*a").repeated(300000);
    QTest::newRow("underscores") << QStringLiteral("_ a").repeated(300000);
    QTest::newRow("backticks") << QStringLiteral("` ``").repeated(200000);
    QTest::newRow("long backticks") << QString(1000000, QLatin1Char('`'));
    QTest::newRow("table") << QStringLiteral("|a").repeated(100000) + QStringLiteral("\n|---|\n") + QStringLiteral("|b").repeated(100000);
    QTest::newRow("fence") << QStringLiteral("```\n") + QStringLiteral("code\n").repeated(100000);
    QTest::newRow("html") << QStringLiteral("<script>alert('x')</script><img src=x onerror=1>").repeated(10000);
    QTest::newRow("big") << QStringLiteral("Word **bold** _it_ `code` [l](/home) text.\n").repeated(30000);
    QTest::newRow("headings") << QStringLiteral("# H\n").repeated(100000);
    QTest::newRow("invalid utf8") << [] { QByteArray data(500000, '\0'); QRandomGenerator generator(42); for (char& c : data) { c = char(generator.bounded(256)); } return QString::fromUtf8(data); }();
    QTest::newRow("control") << [] { QString text; for (int i = 0; i < 70000; ++i) { text += QChar(char16_t(i % 0x30)); text += QChar(char16_t(0xD800 + (i % 64))); } return text; }();
    QTest::newRow("too long") << QString(3 * 1024 * 1024, QLatin1Char('x'));
}

void AppContentTest::markdownHostile()
{
    QFETCH(QString, text);

    QElapsedTimer timer;
    timer.start();
    const pdf::PDFAppMarkdownDocument document = pdf::PDFAppMarkdownParser::parse(text);
    QVERIFY2(timer.elapsed() < 20000, qPrintable(QString::number(timer.elapsed())));
    QVERIFY(document.toPlainText().size() <= pdf::PDFAppMarkdownParser::MAXIMUM_TEXT_LENGTH);
    if (text.size() > pdf::PDFAppMarkdownParser::MAXIMUM_TEXT_LENGTH)
    {
        QVERIFY(document.truncated);
    }

    // The view lays the document out and paints it
    pdf::PDFAppMarkdownView view;
    view.setDocument(document);
    view.resize(600, 400);
    const int height = view.heightForWidth(600);
    QVERIFY(height > 0);
    QImage image(600, 400, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::white);
    QPainter painter(&image);
    view.render(&painter);
    painter.end();
}

void AppContentTest::markdownView()
{
    pdf::PDFAppMarkdownView view;
    view.setMarkdown(QStringLiteral(
        "Intro with [a route](/settings/general) and [an address](https://example.com/x).\n"
        "\n"
        "## Section\n"
        "\n"
        "- [first](#section) item\n"
        "- second\n"
        "\n"
        "| A | B |\n"
        "| --- | --- |\n"
        "| [cell](/home) | x |\n"
        "\n"
        "Refused [script](javascript:x) link.\n"));
    view.resize(700, view.heightForWidth(700));
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    QCOMPARE(view.getLinkCount(), 4);
    QCOMPARE(view.getLinkTarget(0), QStringLiteral("/settings/general"));
    QCOMPARE(int(view.getLinkKind(1)), int(pdf::PDFAppMarkdownLinkKind::External));
    QCOMPARE(view.getLinkTarget(2), QStringLiteral("#section"));
    QCOMPARE(view.getLinkTarget(3), QStringLiteral("/home"));
    QCOMPARE(view.getLinkTarget(4), QString());
    QCOMPARE(view.getFocusedLink(), -1);
    QCOMPARE(view.focusPolicy(), Qt::TabFocus);

    // The anchor of the heading lies below the paragraph
    const int anchor = view.getAnchorPosition(QStringLiteral("section"));
    QVERIFY(anchor > pdf::PDFAppMarkdownView::PADDING_TOP);
    QCOMPARE(view.getAnchorPosition(QStringLiteral("unknown")), -1);

    // A click on a link activates it, a click elsewhere does nothing
    QSignalSpy spy(&view, &pdf::PDFAppMarkdownView::linkActivated);
    for (int link = 0; link < view.getLinkCount(); ++link)
    {
        const QList<QRect> rects = view.getLinkRects(link);
        QVERIFY(!rects.isEmpty());
        QVERIFY(view.rect().contains(rects.first()));
        QTest::mouseMove(&view, rects.first().center());
        QCOMPARE(view.getHoveredLink(), link);
        QCOMPARE(view.cursor().shape(), Qt::PointingHandCursor);
        QTest::mouseClick(&view, Qt::LeftButton, Qt::NoModifier, rects.first().center());
        QCOMPARE(spy.size(), link + 1);
        QCOMPARE(spy.last().at(0).toString(), view.getLinkTarget(link));
    }
    QTest::mouseClick(&view, Qt::LeftButton, Qt::NoModifier, QPoint(2, 2));
    QCOMPARE(spy.size(), 4);

    // Press on a link and release elsewhere: nothing
    const QPoint linkCenter = view.getLinkRects(0).first().center();
    QTest::mousePress(&view, Qt::LeftButton, Qt::NoModifier, linkCenter);
    QTest::mouseRelease(&view, Qt::LeftButton, Qt::NoModifier, QPoint(2, 2));
    QCOMPARE(spy.size(), 4);

    // The plain text has the refused link as a text
    QVERIFY(view.getPlainText().contains(QStringLiteral("Refused script link.")));

    // Right-to-left: the paragraph is aligned to the right
    const QRect leftToRight = view.getLinkRects(0).first();
    view.setLayoutDirection(Qt::RightToLeft);
    QCoreApplication::processEvents();
    const QRect rightToLeft = view.getLinkRects(0).first();
    QVERIFY(rightToLeft.left() != leftToRight.left());
    QVERIFY(view.rect().contains(rightToLeft));

    // A document without links takes no focus
    view.setMarkdown(QStringLiteral("No links"));
    QCOMPARE(view.focusPolicy(), Qt::NoFocus);
    QCOMPARE(view.getLinkCount(), 0);
}

void AppContentTest::markdownGeometry()
{
    // markdown 955:152873: the border of 1 px and the paddings 20 / 24 inside of it,
    // ol / ul: markers in a column of 18 px at the indentation 4, gap 10, the number
    // aligned to the text, the bullet centered; in right-to-left all of it mirrored,
    // the texts aligned to the right (review R2-D2)
    for (const Qt::LayoutDirection direction : { Qt::LeftToRight, Qt::RightToLeft })
    {
        pdf::PDFAppMarkdownView view;
        view.setLayoutDirection(direction);
        view.setMarkdown(QStringLiteral("[Intro](/a)\n\n1. [first](/b)\n\n- [second](/c)\n"));
        const int width = 600;
        view.resize(width, view.heightForWidth(width));
        const QImage image = render(&view, 1.0);
        const bool isRightToLeft = direction == Qt::RightToLeft;

        const QRect intro = view.getLinkRects(0).first();
        const QRect first = view.getLinkRects(1).first();
        const QRect second = view.getLinkRects(2).first();
        const int introStart = isRightToLeft ? width - (intro.right() + 1) : intro.left();
        const int firstStart = isRightToLeft ? width - (first.right() + 1) : first.left();
        const int secondStart = isRightToLeft ? width - (second.right() + 1) : second.left();
        QCOMPARE(introStart, 25);
        QCOMPARE(firstStart, 25 + 4 + 18 + 10);
        QCOMPARE(secondStart, 25 + 4 + 18 + 10);
        // The first line box lies at 21..41 (the text 15.7 px high centered in it)
        QCOMPARE(intro.top(), 23);

        // Ink of a marker: pixels different from bg/surface in the column of the markers
        const QColor surface = pdf::PDFStylePainter::color(pdf::PDFThemeColors::BgSurface);
        auto inkOfMarker = [&](const QRect& line) -> std::pair<int, int>
        {
            int left = width;
            int right = -1;
            for (int y = line.top(); y <= line.bottom(); ++y)
            {
                for (int x = 25; x < 25 + 4 + 18 + 5; ++x)
                {
                    const int imageX = isRightToLeft ? width - 1 - x : x;
                    const QColor pixel = image.pixelColor(imageX, y);
                    if (qAbs(pixel.red() - surface.red()) + qAbs(pixel.green() - surface.green()) + qAbs(pixel.blue() - surface.blue()) > 120)
                    {
                        left = qMin(left, x);
                        right = qMax(right, x);
                    }
                }
            }
            return { left, right };
        };

        // The number ends at the column (25 + 4 + 18), mirrored in right-to-left
        const auto [numberStart, numberEnd] = inkOfMarker(first);
        QVERIFY2(numberEnd >= 44 && numberEnd <= 47, qPrintable(QStringLiteral("%1 %2").arg(numberStart).arg(numberEnd)));
        QVERIFY2(numberStart >= 35, qPrintable(QStringLiteral("%1 %2").arg(numberStart).arg(numberEnd)));

        // The bullet is centered in the column (25 + 4 + 9)
        const auto [bulletStart, bulletEnd] = inkOfMarker(second);
        QVERIFY2(bulletEnd >= bulletStart && qAbs((bulletStart + bulletEnd) / 2.0 - 38.0) <= 1.5, qPrintable(QStringLiteral("%1 %2").arg(bulletStart).arg(bulletEnd)));
    }
}

void AppContentTest::markdownViewKeyboard()
{
    QWidget window;
    QVBoxLayout* layout = new QVBoxLayout(&window);
    QPushButton* before = new QPushButton(QStringLiteral("Before"), &window);
    pdf::PDFAppMarkdownView* view = new pdf::PDFAppMarkdownView(&window);
    QPushButton* after = new QPushButton(QStringLiteral("After"), &window);
    layout->addWidget(before);
    layout->addWidget(view);
    layout->addWidget(after);
    view->setMarkdown(QStringLiteral("[one](/home) [two](/open) [three](/recent)"));
    window.resize(600, 300);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    window.activateWindow();
    (void)QTest::qWaitForWindowActive(&window);

    before->setFocus(Qt::TabFocusReason);
    QSignalSpy spy(view, &pdf::PDFAppMarkdownView::linkActivated);

    // Tab enters the view at its first link, each link is one stop
    QTest::keyClick(before, Qt::Key_Tab);
    QVERIFY(view->hasFocus());
    QCOMPARE(view->getFocusedLink(), 0);
    QTest::keyClick(view, Qt::Key_Tab);
    QCOMPARE(view->getFocusedLink(), 1);
    QTest::keyClick(view, Qt::Key_Return);
    QCOMPARE(spy.size(), 1);
    QCOMPARE(spy.last().at(0).toString(), QStringLiteral("/open"));
    QTest::keyClick(view, Qt::Key_Space);
    QCOMPARE(spy.size(), 2);
    QTest::keyClick(view, Qt::Key_Tab);
    QCOMPARE(view->getFocusedLink(), 2);
    QTest::keyClick(view, Qt::Key_Tab);
    QVERIFY(after->hasFocus());
    QCOMPARE(view->getFocusedLink(), -1);

    // Backtab enters at the last link
    QTest::keyClick(after, Qt::Key_Backtab);
    QVERIFY(view->hasFocus());
    QCOMPARE(view->getFocusedLink(), 2);
    QTest::keyClick(view, Qt::Key_Backtab);
    QCOMPARE(view->getFocusedLink(), 1);
    QTest::keyClick(view, Qt::Key_Backtab);
    QTest::keyClick(view, Qt::Key_Backtab);
    QVERIFY(before->hasFocus());
}

void AppContentTest::markdownReviewFixes()
{
    // A code block longer than one chunk of the layout (4096 characters) is as high
    // as all its lines: the blocks after it do not overlap it
    {
        const auto heightOfCode = [](int length)
        {
            pdf::PDFAppMarkdownView view;
            view.setMarkdown(QStringLiteral("```\n") + QString(length, QLatin1Char('x')) + QStringLiteral("\n```\n\nAfter"));
            return view.heightForWidth(400);
        };
        const int height4000 = heightOfCode(4000);
        const int height5000 = heightOfCode(5000);
        const int height8000 = heightOfCode(8000);
        QVERIFY2(height5000 > height4000 + (height4000 / 8), qPrintable(QStringLiteral("%1 %2").arg(height4000).arg(height5000)));
        QVERIFY2(height8000 > height4000 + (height4000 / 2), qPrintable(QStringLiteral("%1 %2").arg(height4000).arg(height8000)));
    }

    // Duplicate anchors: the same names as before, in linear time
    {
        const pdf::PDFAppMarkdownDocument document = pdf::PDFAppMarkdownParser::parse(QStringLiteral("# x\n# x\n# x-1\n# x\n"));
        const std::vector<pdf::PDFAppMarkdownHeading> headings = document.getHeadings();
        QCOMPARE(headings.size(), size_t(4));
        QCOMPARE(headings[0].anchor, QStringLiteral("x"));
        QCOMPARE(headings[1].anchor, QStringLiteral("x-1"));
        QCOMPARE(headings[2].anchor, QStringLiteral("x-1-1"));
        QCOMPARE(headings[3].anchor, QStringLiteral("x-2"));

        QString text;
        for (int i = 1; i <= 40000; ++i)
        {
            text += QStringLiteral("# x-%1\n").arg(i);
        }
        text += QStringLiteral("# x\n").repeated(40000);
        QElapsedTimer timer;
        timer.start();
        const pdf::PDFAppMarkdownDocument hostile = pdf::PDFAppMarkdownParser::parse(text);
        QVERIFY2(timer.elapsed() < 20000, qPrintable(QString::number(timer.elapsed())));
        const std::vector<pdf::PDFAppMarkdownHeading> hostileHeadings = hostile.getHeadings();
        QCOMPARE(hostileHeadings.size(), size_t(80000));
        QCOMPARE(hostileHeadings[40000].anchor, QStringLiteral("x"));
        QCOMPARE(hostileHeadings[40001].anchor, QStringLiteral("x-40001"));
        QCOMPARE(hostileHeadings.back().anchor, QStringLiteral("x-79999"));
    }

    // A rebuild of the Help page (language change) does not keep the old model of the contents
    {
        ContentFixture fixture;
        fixture.navigate(QStringLiteral("/help/getting-started"));
        pdf::PDFAppHelpPage* page = fixture.page<pdf::PDFAppHelpPage>();
        QVERIFY(page);
        QCOMPARE(page->findChildren<QStandardItemModel*>().size(), 1);
        for (int i = 0; i < 3; ++i)
        {
            QEvent languageChange(QEvent::LanguageChange);
            QCoreApplication::sendEvent(page, &languageChange);
            QCoreApplication::processEvents();
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        }
        QCOMPARE(page->findChildren<QStandardItemModel*>().size(), 1);
        QVERIFY(page->getContentsView()->model());
        QCOMPARE(page->getContentsView()->model()->rowCount() > 0, true);
    }

    QWidget window;
    QVBoxLayout* layout = new QVBoxLayout(&window);
    pdf::PDFAppMarkdownView* view = new pdf::PDFAppMarkdownView(&window);
    QPushButton* after = new QPushButton(QStringLiteral("After"), &window);
    layout->addWidget(view);
    layout->addWidget(after);
    view->setMarkdown(QStringLiteral("[one](/home) [two](/open) [three](/recent)"));
    window.resize(600, 300);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    window.activateWindow();
    (void)QTest::qWaitForWindowActive(&window);
    QSignalSpy spy(view, &pdf::PDFAppMarkdownView::linkActivated);

    // Enter of the keypad activates the focused link
    after->setFocus(Qt::TabFocusReason);
    QTest::keyClick(after, Qt::Key_Backtab);
    QVERIFY(view->hasFocus());
    QCOMPARE(view->getFocusedLink(), 2);
    QTest::keyClick(view, Qt::Key_Backtab);
    QCOMPARE(view->getFocusedLink(), 1);
    QTest::keyClick(view, Qt::Key_Enter, Qt::KeypadModifier);
    QCOMPARE(spy.size(), 1);
    QCOMPARE(spy.last().at(0).toString(), QStringLiteral("/open"));

    // A switch to another window keeps the focused link, a Tab away forgets it
    {
        QFocusEvent focusOut(QEvent::FocusOut, Qt::ActiveWindowFocusReason);
        QCoreApplication::sendEvent(view, &focusOut);
        QFocusEvent focusIn(QEvent::FocusIn, Qt::ActiveWindowFocusReason);
        QCoreApplication::sendEvent(view, &focusIn);
    }
    QCOMPARE(view->getFocusedLink(), 1);
    QTest::keyClick(view, Qt::Key_Return);
    QCOMPARE(spy.size(), 2);
    {
        QFocusEvent focusOut(QEvent::FocusOut, Qt::TabFocusReason);
        QCoreApplication::sendEvent(view, &focusOut);
    }
    QCOMPARE(view->getFocusedLink(), -1);

    // The pointer over a link, then a resize: no link is hovered and the cursor is reset
    const QList<QRect> rects = view->getLinkRects(0);
    QVERIFY(!rects.isEmpty());
    QTest::mouseMove(view, rects.first().center());
    QTRY_COMPARE(view->getHoveredLink(), 0);
    QVERIFY(view->testAttribute(Qt::WA_SetCursor));
    QCOMPARE(view->cursor().shape(), Qt::PointingHandCursor);
    window.resize(500, 300);
    QTRY_COMPARE(view->width() < 600, true);
    QCOMPARE(view->getHoveredLink(), -1);
    QVERIFY(!view->testAttribute(Qt::WA_SetCursor));
}

void AppContentTest::markdownViewDeletedBySlot()
{
    // The slot of the signal deletes the view (a navigation deletes the page)
    QPointer<pdf::PDFAppMarkdownView> view = new pdf::PDFAppMarkdownView();
    view->setMarkdown(QStringLiteral("[link](/home)"));
    view->resize(400, view->heightForWidth(400));
    view->show();
    QVERIFY(QTest::qWaitForWindowExposed(view.data()));
    QObject::connect(view.data(), &pdf::PDFAppMarkdownView::linkActivated, view.data(), [&view]() { delete view.data(); });
    QTest::mouseClick(view.data(), Qt::LeftButton, Qt::NoModifier, view->getLinkRects(0).first().center());
    QVERIFY(view.isNull());

    view = new pdf::PDFAppMarkdownView();
    view->setMarkdown(QStringLiteral("[link](/home)"));
    view->resize(400, view->heightForWidth(400));
    view->show();
    QVERIFY(QTest::qWaitForWindowExposed(view.data()));
    view->activateWindow();
    (void)QTest::qWaitForWindowActive(view.data());
    view->setFocus(Qt::OtherFocusReason);
    QFocusEvent focusEvent(QEvent::FocusIn, Qt::TabFocusReason);
    QApplication::sendEvent(view.data(), &focusEvent);
    QCOMPARE(view->getFocusedLink(), 0);
    QObject::connect(view.data(), &pdf::PDFAppMarkdownView::linkActivated, view.data(), [&view]() { delete view.data(); });
    QTest::keyClick(view.data(), Qt::Key_Return);
    QVERIFY(view.isNull());
}

void AppContentTest::markdownViewNoLeak_data()
{
    QTest::addColumn<bool>("isDark");
    QTest::addColumn<bool>("isRightToLeft");
    QTest::addColumn<qreal>("scale");

    for (const bool isDark : { false, true })
    {
        for (const bool isRightToLeft : { false, true })
        {
            for (const qreal scale : { 1.0, 1.5, 2.0 })
            {
                QTest::addRow("%s %s %.1f", isDark ? "dark" : "light", isRightToLeft ? "rtl" : "ltr", scale) << isDark << isRightToLeft << scale;
            }
        }
    }
}

void AppContentTest::markdownViewNoLeak()
{
    QFETCH(bool, isDark);
    QFETCH(bool, isRightToLeft);
    QFETCH(qreal, scale);

    pdf::PDFThemeManager::getInstance()->setScheme(isDark ? pdf::PDFThemeColors::Scheme::Dark : pdf::PDFThemeColors::Scheme::Light);
    {
        pdf::PDFAppMarkdownView view;
        view.setLayoutDirection(isRightToLeft ? Qt::RightToLeft : Qt::LeftToRight);
        view.setMarkdown(QStringLiteral(
            "# Heading\n\nText with **bold**, *italic*, `code`, [a link](/home) and [an address](https://example.com).\n\n"
            "## Section\n\n### Sub\n\n#### Small\n\n- a\n  1. b\n  2. c\n\n```\ncode block\n```\n\n> quote\n\n"
            "> [!NOTE] Note\n> text\n\n> [!TIP] Tip\n> text\n\n> [!IMPORTANT] Important\n> text\n\n> [!WARNING] Warning\n> text\n\n> [!CAUTION] Caution\n> text\n\n"
            "| A | B |\n| --- | --- |\n| [x](/home) | y |\n\n![missing](none.png)\n\n---\n"));
        view.resize(640, view.heightForWidth(640));
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));

        // The link is hovered (underline) and focused (focus ring)
        QTest::mouseMove(&view, view.getLinkRects(0).first().center());
        const QImage image = render(&view, scale);
        QCOMPARE(countLeakPixels(image), 0);

        // The text of the paragraph uses the tokens of the theme
        const QColor secondary = pdf::PDFStylePainter::color(pdf::PDFThemeColors::TextSecondary);
        bool hasTextColor = false;
        const QImage converted = image.convertToFormat(QImage::Format_ARGB32);
        for (int y = 0; y < converted.height() && !hasTextColor; ++y)
        {
            for (int x = 0; x < converted.width() && !hasTextColor; ++x)
            {
                hasTextColor = QColor(converted.pixel(x, y)) == secondary;
            }
        }
        QVERIFY(hasTextColor);
    }
    pdf::PDFThemeManager::getInstance()->setScheme(pdf::PDFThemeColors::Scheme::Light);
}

void AppContentTest::markdownViewStates()
{
    // A document of the public API built by hand: an empty span, an item outside of
    // a list, an unknown kind of block, a table without columns, a short row
    pdf::PDFAppMarkdownDocument document;
    {
        pdf::PDFAppMarkdownBlock empty;
        empty.spans.push_back(pdf::PDFAppMarkdownSpan());
        document.blocks.push_back(empty);

        pdf::PDFAppMarkdownBlock item;
        item.type = pdf::PDFAppMarkdownBlock::Type::ListItem;
        item.children = pdf::PDFAppMarkdownParser::parse(QStringLiteral("item text")).blocks;
        document.blocks.push_back(item);

        pdf::PDFAppMarkdownBlock unknown;
        unknown.type = pdf::PDFAppMarkdownBlock::Type(99);
        document.blocks.push_back(unknown);

        pdf::PDFAppMarkdownBlock noColumns;
        noColumns.type = pdf::PDFAppMarkdownBlock::Type::Table;
        document.blocks.push_back(noColumns);

        pdf::PDFAppMarkdownBlock noRows;
        noRows.type = pdf::PDFAppMarkdownBlock::Type::Table;
        noRows.alignments = { Qt::AlignLeft };
        document.blocks.push_back(noRows);

        pdf::PDFAppMarkdownBlock shortRow;
        shortRow.type = pdf::PDFAppMarkdownBlock::Type::Table;
        shortRow.alignments = { Qt::AlignLeft, Qt::AlignRight };
        shortRow.rows.push_back({ pdf::PDFAppMarkdownParser::parseInline(QStringLiteral("a")), pdf::PDFAppMarkdownParser::parseInline(QStringLiteral("b")) });
        shortRow.rows.push_back({ pdf::PDFAppMarkdownParser::parseInline(QStringLiteral("c")) });
        document.blocks.push_back(shortRow);
    }

    pdf::PDFAppMarkdownView handBuilt;
    handBuilt.setDocument(document);
    pdf::PDFAppMarkdownView emptyView;
    emptyView.setDocument(pdf::PDFAppMarkdownDocument());
    QVERIFY(handBuilt.heightForWidth(400) > emptyView.heightForWidth(400));
    QFocusEvent emptyFocus(QEvent::FocusIn, Qt::TabFocusReason);
    QApplication::sendEvent(&emptyView, &emptyFocus);
    QCOMPARE(emptyView.getFocusedLink(), -1);
    QCOMPARE(handBuilt.getLinkCount(), 0);
    handBuilt.resize(400, handBuilt.heightForWidth(400));
    QVERIFY(!render(&handBuilt, 1.0).isNull());

    // The card: its paddings are added to the height, the same value changes nothing
    pdf::PDFAppMarkdownView card;
    card.setMarkdown(QStringLiteral("x"));
    QVERIFY(card.isCardVisible());
    const int cardHeight = card.heightForWidth(600);
    card.setCardVisible(false);
    card.setCardVisible(false);
    QVERIFY(!card.isCardVisible());
    QCOMPARE(cardHeight - card.heightForWidth(600), pdf::PDFAppMarkdownView::PADDING_TOP + pdf::PDFAppMarkdownView::PADDING_BOTTOM);
    card.resize(600, card.heightForWidth(600));
    QVERIFY(!render(&card, 1.0).isNull());
    card.setCardVisible(true);
    QCOMPARE(card.heightForWidth(600), cardHeight);

    // Formats: a bold heading, bold text of a quote, callouts without a title, with
    // only a title, nested headings, aligned columns, one link of more spans, many spans
    pdf::PDFAppMarkdownView view;
    view.setMarkdown(QStringLiteral(
        "# **Bold heading** [x](/a)\n\n"
        "> **quote bold** [q](/b)\n\n"
        "> [!NOTE]\n> No title\n\n"
        "> [!TIP] Only title\n\n"
        "> [!WARNING] W\n> ## Nested heading\n\n"
        "- ## Item heading\n\n"
        "| L | C | R |\n| :-- | :-: | --: |\n| [**b** t](/c) | m | r |\n\n"
        "![](missing.png)\n\n") + QStringLiteral("**a** b ").repeated(200));
    QCOMPARE(view.getLinkCount(), 3);
    QCOMPARE(view.getLinkTarget(2), QStringLiteral("/c"));
    QCOMPARE(view.getLinkTarget(-1), QString());
    QCOMPARE(int(view.getLinkKind(-1)), int(pdf::PDFAppMarkdownLinkKind::None));
    QCOMPARE(int(view.getLinkKind(3)), int(pdf::PDFAppMarkdownLinkKind::None));
    // One link of two spans (two formats): one link with an area for each span
    QCOMPARE(view.getLinkRects(2).size(), 2);
    view.resize(600, view.heightForWidth(600));
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    // Hover: a move inside of the same link keeps it, a move outside and the leave reset it
    QSignalSpy spy(&view, &pdf::PDFAppMarkdownView::linkActivated);
    const QPoint linkCenter = view.getLinkRects(0).first().center();
    QTest::mouseMove(&view, linkCenter);
    QCOMPARE(view.getHoveredLink(), 0);
    QTest::mouseMove(&view, linkCenter + QPoint(1, 0));
    QCOMPARE(view.getHoveredLink(), 0);
    QCOMPARE(view.cursor().shape(), Qt::PointingHandCursor);
    QTest::mouseMove(&view, QPoint(2, 2));
    QCOMPARE(view.getHoveredLink(), -1);
    QCOMPARE(view.cursor().shape(), Qt::ArrowCursor);
    QTest::mouseMove(&view, linkCenter);
    QCOMPARE(view.getHoveredLink(), 0);
    QEvent leave(QEvent::Leave);
    QApplication::sendEvent(&view, &leave);
    QCOMPARE(view.getHoveredLink(), -1);

    // The right button does not activate a link; the pressed link is painted
    QTest::mouseClick(&view, Qt::RightButton, Qt::NoModifier, linkCenter);
    QCOMPARE(spy.size(), 0);
    QTest::mousePress(&view, Qt::LeftButton, Qt::NoModifier, linkCenter);
    QVERIFY(!render(&view, 1.0).isNull());
    QTest::mouseRelease(&view, Qt::LeftButton, Qt::NoModifier, linkCenter);
    QCOMPARE(spy.size(), 1);

    // Keyboard: a focus by another reason than Tab focuses no link, Enter does nothing
    view.activateWindow();
    (void)QTest::qWaitForWindowActive(&view);
    view.setFocus(Qt::OtherFocusReason);
    QFocusEvent otherFocus(QEvent::FocusIn, Qt::OtherFocusReason);
    QApplication::sendEvent(&view, &otherFocus);
    QCOMPARE(view.getFocusedLink(), -1);
    QTest::keyClick(&view, Qt::Key_Return);
    QCOMPARE(spy.size(), 1);
    QTest::keyClick(&view, Qt::Key_Tab);
    QCOMPARE(view.getFocusedLink(), -1);

    // A focus by Tab: the keypad Enter and Space activate, a modifier does not
    QFocusEvent tabFocus(QEvent::FocusIn, Qt::TabFocusReason);
    QApplication::sendEvent(&view, &tabFocus);
    QCOMPARE(view.getFocusedLink(), 0);
    QTest::keyClick(&view, Qt::Key_Enter);
    QCOMPARE(spy.size(), 2);
    QTest::keyClick(&view, Qt::Key_Return, Qt::ShiftModifier);
    QCOMPARE(spy.size(), 2);
    QTest::keyClick(&view, Qt::Key_Escape);
    QCOMPARE(spy.size(), 2);

    // A focus, which comes again, keeps the focused link; a switch to another window
    // keeps it, a focus out by Tab clears it
    QFocusEvent backtabFocus(QEvent::FocusIn, Qt::BacktabFocusReason);
    QApplication::sendEvent(&view, &backtabFocus);
    QCOMPARE(view.getFocusedLink(), 0);
    QFocusEvent windowFocusOut(QEvent::FocusOut, Qt::ActiveWindowFocusReason);
    QApplication::sendEvent(&view, &windowFocusOut);
    QCOMPARE(view.getFocusedLink(), 0);
    QFocusEvent popupFocusOut(QEvent::FocusOut, Qt::PopupFocusReason);
    QApplication::sendEvent(&view, &popupFocusOut);
    QCOMPARE(view.getFocusedLink(), 0);
    QFocusEvent tabFocusOut(QEvent::FocusOut, Qt::TabFocusReason);
    QApplication::sendEvent(&view, &tabFocusOut);
    QCOMPARE(view.getFocusedLink(), -1);
    QApplication::sendEvent(&view, &tabFocus);
    QCOMPARE(view.getFocusedLink(), 0);
    QVERIFY(!render(&view, 1.0).isNull());

    // A change of the font and of the language lays the text out again
    QFont font = view.font();
    font.setPixelSize(30);
    view.setFont(font);
    QEvent languageChange(QEvent::LanguageChange);
    QApplication::sendEvent(&view, &languageChange);
    QCOMPARE(view.getLinkCount(), 3);
    QVERIFY(!view.getLinkRects(0).isEmpty());

    // A view in a scroll area, but not in its widget: the focused link scrolls nothing
    QScrollArea area;
    pdf::PDFAppMarkdownView* nested = new pdf::PDFAppMarkdownView(&area);
    nested->setMarkdown(QStringLiteral("[x](/a)"));
    nested->resize(300, nested->heightForWidth(300));
    QApplication::sendEvent(nested, &tabFocus);
    QCOMPARE(nested->getFocusedLink(), 0);
    area.setWidget(new QWidget());
    nested->setFocusPolicy(Qt::TabFocus);
    QFocusEvent focusOut(QEvent::FocusOut, Qt::TabFocusReason);
    QApplication::sendEvent(nested, &focusOut);
    QApplication::sendEvent(nested, &tabFocus);
    QCOMPARE(nested->getFocusedLink(), 0);
    QCOMPARE(area.verticalScrollBar()->value(), 0);

    // Thin shapes: a view one pixel wide
    pdf::PDFAppMarkdownView thin;
    thin.setMarkdown(QStringLiteral("```\ncode\n```\n\n---\n\n> quote\n"));
    thin.resize(1, thin.heightForWidth(1));
    QVERIFY(!render(&thin, 1.0).isNull());
}

void AppContentTest::markdownViewPictures()
{
    // Pictures only of the resources: a picture of the resources of Qt
    const QString picture = findPictureResource(32);
    if (picture.isEmpty())
    {
        QSKIP("No raster picture in the resources");
    }
    const QSize size = QImageReader(picture).size();
    const QString folder = QFileInfo(picture).path() + QStringLiteral("/");
    const QString name = QFileInfo(picture).fileName();

    auto createView = [](const QString& markdown, const QString& base, Qt::LayoutDirection direction)
    {
        auto view = std::make_unique<pdf::PDFAppMarkdownView>();
        view->setLayoutDirection(direction);
        view->setCardVisible(false);
        view->setResourceBase(base);
        view->setMarkdown(markdown);
        return view;
    };

    // Refused or missing sources are the alternative text (also the source, when it has none)
    const int textHeight = createView(QStringLiteral("![alt](missing.png)"), folder, Qt::LeftToRight)->heightForWidth(600);
    for (const QString& source : { QStringLiteral("a:b.png"), QStringLiteral("/absolute.png"), QStringLiteral("\\x.png"), QStringLiteral("sub\\x.png"),
                                   QStringLiteral(":/../x.png"), QStringLiteral(":/no/such/picture.png"), QStringLiteral("../") + name,
                                   QStringLiteral("../") + QDir(QFileInfo(picture).path()).dirName() + QStringLiteral("/") + name, QStringLiteral("..\\") + name })
    {
        QCOMPARE(createView(QStringLiteral("![alt](%1)").arg(source), folder, Qt::LeftToRight)->heightForWidth(600), textHeight);
    }
    QCOMPARE(createView(QStringLiteral("![](missing.png)"), folder, Qt::LeftToRight)->heightForWidth(600), textHeight);
    QCOMPARE(createView(QStringLiteral("![alt](%1)").arg(name), QString(), Qt::LeftToRight)->heightForWidth(600), textHeight);

    // The picture of the resources: absolute and relative to the base, at most the width
    const qreal scale = qMin(1.0, 600.0 / qMax(size.width(), 1));
    const int pictureHeight = int(std::ceil(size.height() * scale));
    QVERIFY(pictureHeight != textHeight);
    QCOMPARE(createView(QStringLiteral("![alt](%1)").arg(picture), QString(), Qt::LeftToRight)->heightForWidth(600), pictureHeight);
    QCOMPARE(createView(QStringLiteral("![alt](%1)").arg(name), folder, Qt::LeftToRight)->heightForWidth(600), pictureHeight);
    const int narrowWidth = qMax(size.width() / 2, 41);
    QCOMPARE(createView(QStringLiteral("![alt](%1)").arg(name), folder, Qt::LeftToRight)->heightForWidth(narrowWidth),
             int(std::ceil(size.height() * qMin(1.0, qreal(narrowWidth) / size.width()))));

    // A painted region without the picture
    {
        std::unique_ptr<pdf::PDFAppMarkdownView> view = createView(QStringLiteral("![alt](%1)\n\nText below").arg(name), folder, Qt::LeftToRight);
        view->resize(600, view->heightForWidth(600));
        QImage image(view->size(), QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::white);
        QPainter painter(&image);
        view->render(&painter, QPoint(), QRegion(0, pictureHeight + 2, 600, view->height() - pictureHeight - 2));
        painter.end();
        QCOMPARE(image.pixel(size.width() / 2, size.height() / 2), QColor(Qt::white).rgb());
    }

    // The picture is painted at the leading side (mirrored in right-to-left)
    for (const Qt::LayoutDirection direction : { Qt::LeftToRight, Qt::RightToLeft })
    {
        std::unique_ptr<pdf::PDFAppMarkdownView> view = createView(QStringLiteral("![alt](%1)").arg(name), folder, direction);
        const int width = qMax(4 * size.width(), 200);
        view->resize(width, view->heightForWidth(width));
        const QImage image = render(view.get(), 1.0).convertToFormat(QImage::Format_ARGB32);
        const QRgb background = image.pixel(width / 2, 0);
        int leftInk = 0;
        int rightInk = 0;
        for (int y = 0; y < image.height(); ++y)
        {
            for (int x = 0; x < image.width(); ++x)
            {
                if (image.pixel(x, y) != background)
                {
                    (x < width / 2 ? leftInk : rightInk) += 1;
                }
            }
        }
        QVERIFY2(direction == Qt::LeftToRight ? (leftInk > 0 && rightInk == 0) : (rightInk > 0 && leftInk == 0), qPrintable(QStringLiteral("%1 %2").arg(leftInk).arg(rightInk)));
    }
}

// ----------------------------------------------------------------- What's New

void AppContentTest::whatsNewParser()
{
    pdf::PDFWhatsNewParser::Options options;
    options.expectedVersion = QVersionNumber({ 1, 6, 0, 0 });
    options.isToolKnown = [](const QString& id) { return pdf::PDFDocumentToolCatalog::findTool(id) != nullptr; };
    options.isIconKnown = [](const QString& name) { return name == QStringLiteral("layout") || name == QStringLiteral("zap"); };

    // CRLF and a byte order mark are accepted
    const QString text = QChar(0xFEFF) + validWhatsNewFile().replace(QStringLiteral("\n"), QStringLiteral("\r\n"));
    const pdf::PDFWhatsNewParser::Result result = pdf::PDFWhatsNewParser::parse(text, options);
    for (const pdf::PDFWhatsNewError& error : result.errors)
    {
        qWarning("%s", qPrintable(error.toString()));
    }
    QVERIFY(result.isValid());

    const pdf::PDFWhatsNewRelease& release = result.release;
    QCOMPARE(release.version, QVersionNumber({ 1, 6, 0, 0 }));
    QCOMPARE(release.getDisplayVersion(), QStringLiteral("1.6.0"));
    QCOMPARE(release.date, QDate(2026, 11, 20));
    QVERIFY(!release.isUnreleased);
    QCOMPARE(release.summary, QStringLiteral("One unified app."));
    QCOMPARE(release.overview, QStringLiteral("This release merges the applications into one."));
    QCOMPARE(release.highlights.size(), size_t(2));
    QCOMPARE(release.highlights[1].title, QStringLiteral("Faster loading"));
    QCOMPARE(release.highlights[1].iconName, QStringLiteral("zap"));
    QCOMPARE(int(release.highlights[1].hue), int(pdf::PDFAccentHue::Amber));
    QCOMPARE(release.highlights[1].description, QStringLiteral("Large documents open faster."));
    QCOMPARE(release.newTools, (QStringList{ QStringLiteral("optimize.compress"), QStringLiteral("pages.merge") }));
    QCOMPARE(release.fixedIssues.size(), size_t(2));
    QCOMPARE(release.fixedIssues[0].number, 430);
    QCOMPARE(release.fixedIssues[0].date, QDate(2026, 10, 12));
    QCOMPARE(release.fixedIssues[0].title, QStringLiteral("Text layout optimization"));
    QCOMPARE(release.fixedIssues[0].description, QStringLiteral("Large documents lay out text faster."));
    QVERIFY(!release.fixedIssues[1].date.isValid());
    QCOMPARE(release.releaseNotes, QStringLiteral("## Free Markdown\n\nAnything **here**."));

    // Optional sections: only the header and the overview
    const QString minimal = QStringLiteral("---\nversion: 1.2\ndate: unreleased\nsummary: S\n---\n## Overview\nText.\n");
    const pdf::PDFWhatsNewParser::Result minimalResult = pdf::PDFWhatsNewParser::parse(minimal, pdf::PDFWhatsNewParser::Options());
    QVERIFY(minimalResult.isValid());
    QVERIFY(minimalResult.release.isUnreleased);
    QVERIFY(!minimalResult.release.date.isValid());
    QVERIFY(minimalResult.release.highlights.empty());
    QCOMPARE(minimalResult.release.getDisplayVersion(), QStringLiteral("1.2.0"));

    // Versions and hues
    QCOMPARE(pdf::PDFWhatsNewParser::parseVersion(QStringLiteral("1.6.0.0")), QVersionNumber({ 1, 6, 0, 0 }));
    QVERIFY(pdf::PDFWhatsNewParser::parseVersion(QStringLiteral("1.6.0.0.1")).isNull());
    QVERIFY(pdf::PDFWhatsNewParser::parseVersion(QStringLiteral("1..6")).isNull());
    QVERIFY(pdf::PDFWhatsNewParser::parseVersion(QStringLiteral("v1.6")).isNull());
    QVERIFY(pdf::PDFWhatsNewParser::parseVersion(QStringLiteral("123456.1")).isNull());
    QVERIFY(pdf::PDFWhatsNewParser::parseVersion(QStringLiteral("-1.2")).isNull());
    for (const QString& hue : { QStringLiteral("blue"), QStringLiteral("violet"), QStringLiteral("amber"), QStringLiteral("red"), QStringLiteral("teal"), QStringLiteral("green"), QStringLiteral("neutral") })
    {
        QVERIFY(pdf::PDFWhatsNewParser::parseHue(hue).has_value());
        QCOMPARE(pdf::PDFWhatsNewParser::getHueName(*pdf::PDFWhatsNewParser::parseHue(hue)), hue);
    }
    QVERIFY(!pdf::PDFWhatsNewParser::parseHue(QStringLiteral("Blue")).has_value());
}

void AppContentTest::whatsNewParserErrors_data()
{
    QTest::addColumn<QString>("text");
    QTest::addColumn<int>("code");
    QTest::addColumn<int>("line");

    using Code = pdf::PDFWhatsNewError::Code;
    const QString valid = validWhatsNewFile();
    auto replace = [&valid](const QString& before, const QString& after)
    {
        QString text = valid;
        return text.replace(before, after);
    };

    QTest::newRow("empty") << QString() << int(Code::Empty) << 1;
    QTest::newRow("blank") << QStringLiteral("  \n\n ") << int(Code::Empty) << 1;
    QTest::newRow("too large") << QString(pdf::PDFWhatsNewParser::MAXIMUM_FILE_SIZE + 1, QLatin1Char('a')) << int(Code::TooLarge) << 0;
    QTest::newRow("no header") << QStringLiteral("## Overview\ntext\n") << int(Code::MissingHeader) << 1;
    QTest::newRow("header not closed") << QStringLiteral("---\nversion: 1.0\n") << int(Code::MissingHeader) << 3;
    QTest::newRow("unknown key") << replace(QStringLiteral("summary:"), QStringLiteral("author: x\nsummary:")) << int(Code::UnknownKey) << 4;
    QTest::newRow("duplicate key") << replace(QStringLiteral("summary:"), QStringLiteral("date: 2026-01-01\nsummary:")) << int(Code::DuplicateKey) << 4;
    QTest::newRow("missing key") << replace(QStringLiteral("summary: One unified app.\n"), QString()) << int(Code::MissingKey) << 4;
    QTest::newRow("header syntax") << replace(QStringLiteral("summary:"), QStringLiteral("no colon\nsummary:")) << int(Code::InvalidSyntax) << 4;
    QTest::newRow("bad version") << replace(QStringLiteral("version: 1.6.0.0"), QStringLiteral("version: 1.6.x")) << int(Code::InvalidVersion) << 2;
    QTest::newRow("version mismatch") << replace(QStringLiteral("version: 1.6.0.0"), QStringLiteral("version: 1.5.0")) << int(Code::VersionMismatch) << 2;
    QTest::newRow("bad date") << replace(QStringLiteral("date: 2026-11-20"), QStringLiteral("date: 20.11.2026")) << int(Code::InvalidDate) << 3;
    QTest::newRow("impossible date") << replace(QStringLiteral("date: 2026-11-20"), QStringLiteral("date: 2026-02-30")) << int(Code::InvalidDate) << 3;
    QTest::newRow("empty summary") << replace(QStringLiteral("summary: One unified app."), QStringLiteral("summary:")) << int(Code::InvalidSummary) << 4;
    QTest::newRow("text outside") << replace(QStringLiteral("---\n\n## Overview"), QStringLiteral("---\nstray\n## Overview")) << int(Code::TextOutsideSection) << 6;
    QTest::newRow("unknown section") << replace(QStringLiteral("## New tools"), QStringLiteral("## New stuff")) << int(Code::UnknownSection) << 22;
    QTest::newRow("section order") << replace(QStringLiteral("## Highlights"), QStringLiteral("## Fixed issues\n\n## Highlights")) << int(Code::SectionOrder) << 14;
    QTest::newRow("missing overview") << replace(QStringLiteral("## Overview\n\nThis release merges the applications\ninto one.\n"), QString()) << int(Code::MissingOverview) << 6;
    QTest::newRow("two paragraphs") << replace(QStringLiteral("into one.\n"), QStringLiteral("into one.\n\nSecond.\n")) << int(Code::InvalidSyntax) << 12;
    QTest::newRow("unknown icon") << replace(QStringLiteral("icon: zap"), QStringLiteral("icon: rocket")) << int(Code::UnknownIcon) << 19;
    QTest::newRow("unknown hue") << replace(QStringLiteral("hue: amber"), QStringLiteral("hue: pink")) << int(Code::UnknownHue) << 19;
    QTest::newRow("unknown highlight key") << replace(QStringLiteral("hue: amber"), QStringLiteral("hue: amber \u00B7 color: red")) << int(Code::UnknownKey) << 19;
    QTest::newRow("missing icon line") << replace(QStringLiteral("icon: zap \u00B7 hue: amber\n"), QString()) << int(Code::InvalidSyntax) << 18;
    QTest::newRow("unknown tool") << replace(QStringLiteral("- pages.merge"), QStringLiteral("- pages.teleport")) << int(Code::UnknownTool) << 25;
    QTest::newRow("tool syntax") << replace(QStringLiteral("- pages.merge"), QStringLiteral("* pages.merge")) << int(Code::InvalidSyntax) << 25;
    QTest::newRow("duplicate tool") << replace(QStringLiteral("- pages.merge"), QStringLiteral("- optimize.compress")) << int(Code::DuplicateTool) << 25;
    QTest::newRow("duplicate issue") << replace(QStringLiteral("- #417"), QStringLiteral("- #430")) << int(Code::DuplicateIssue) << 31;
    QTest::newRow("issue number") << replace(QStringLiteral("- #417"), QStringLiteral("- #abc")) << int(Code::InvalidIssue) << 31;
    QTest::newRow("issue zero") << replace(QStringLiteral("- #417"), QStringLiteral("- #0")) << int(Code::InvalidIssue) << 31;
    QTest::newRow("issue huge") << replace(QStringLiteral("- #417"), QStringLiteral("- #99999999")) << int(Code::InvalidIssue) << 31;
    QTest::newRow("issue date") << replace(QStringLiteral("2026-10-12"), QStringLiteral("2026-13-12")) << int(Code::InvalidDate) << 29;
    QTest::newRow("issue syntax") << replace(QStringLiteral("- #417 \u00B7 Visible signatures"), QStringLiteral("- 417 Visible signatures")) << int(Code::InvalidSyntax) << 31;
    QTest::newRow("issue parts") << replace(QStringLiteral("- #417 \u00B7 Visible signatures"), QStringLiteral("- #417")) << int(Code::InvalidSyntax) << 31;
    QTest::newRow("issue plus") << replace(QStringLiteral("- #417"), QStringLiteral("- #+417")) << int(Code::InvalidIssue) << 31;
    QTest::newRow("highlight no title") << replace(QStringLiteral("### Faster loading"), QStringLiteral("### ")) << int(Code::InvalidSyntax) << 18;
}

void AppContentTest::whatsNewParserErrors()
{
    QFETCH(QString, text);
    QFETCH(int, code);
    QFETCH(int, line);

    pdf::PDFWhatsNewParser::Options options;
    options.expectedVersion = QVersionNumber({ 1, 6, 0, 0 });
    options.isToolKnown = [](const QString& id) { return pdf::PDFDocumentToolCatalog::findTool(id) != nullptr; };
    options.isIconKnown = [](const QString& name) { return name == QStringLiteral("layout") || name == QStringLiteral("zap"); };

    const pdf::PDFWhatsNewParser::Result result = pdf::PDFWhatsNewParser::parse(text, options);
    QVERIFY(!result.isValid());

    bool isFound = false;
    QStringList errors;
    for (const pdf::PDFWhatsNewError& error : result.errors)
    {
        errors << QStringLiteral("%1 %2").arg(int(error.code)).arg(error.toString());
        isFound = isFound || (int(error.code) == code && error.line == line);
    }
    QVERIFY2(isFound, qPrintable(errors.join(QStringLiteral("; "))));
}

void AppContentTest::whatsNewParserRandom()
{
    // Random texts and random mutations of a valid file never crash
    QRandomGenerator generator(1234);
    const QString valid = validWhatsNewFile();
    const QString alphabet = QStringLiteral("-#:\u00B7 \n\tabc019[]*_`>|");
    for (int i = 0; i < 3000; ++i)
    {
        QString text = valid;
        const int mutations = 1 + int(generator.bounded(8));
        for (int k = 0; k < mutations; ++k)
        {
            const int position = int(generator.bounded(qMax(1, int(text.size()))));
            switch (generator.bounded(3))
            {
                case 0:
                    text.remove(position, 1 + int(generator.bounded(20)));
                    break;
                case 1:
                    text.insert(position, alphabet.at(int(generator.bounded(int(alphabet.size())))));
                    break;
                default:
                    text.insert(position, text.mid(int(generator.bounded(qMax(1, int(text.size())))), int(generator.bounded(40))));
                    break;
            }
        }
        (void)pdf::PDFWhatsNewParser::parse(text, pdf::PDFWhatsNewCatalog::createDefaultOptions());
    }

    for (int i = 0; i < 500; ++i)
    {
        QByteArray data(int(generator.bounded(2000)), '\0');
        for (char& c : data)
        {
            c = char(generator.bounded(256));
        }
        (void)pdf::PDFWhatsNewParser::parse(QString::fromUtf8(data), pdf::PDFWhatsNewCatalog::createDefaultOptions());
        (void)pdf::PDFWhatsNewParser::parse(QStringLiteral("---\n") + QString::fromUtf8(data), pdf::PDFWhatsNewCatalog::createDefaultOptions());
    }
}

void AppContentTest::whatsNewCatalog()
{
    const pdf::PDFWhatsNewCatalog catalog = pdf::PDFWhatsNewCatalog::load();
    QVERIFY2(catalog.getErrors().isEmpty(), qPrintable(catalog.getErrors().join(QStringLiteral("\n"))));
    QVERIFY(catalog.getReleases().size() >= 10);

    // Sorted by the version, the newest first; the running version is bundled
    for (size_t i = 1; i < catalog.getReleases().size(); ++i)
    {
        QVERIFY(QVersionNumber::compare(catalog.getReleases()[i - 1].version, catalog.getReleases()[i].version) > 0);
    }
    QVERIFY(catalog.findRelease(pdf::PDFWhatsNewCatalog::getRunningVersion()));
    QCOMPARE(catalog.findRelease(QVersionNumber(1, 5, 2))->version, QVersionNumber({ 1, 5, 2, 0 }));
    QCOMPARE(catalog.findRelease(QVersionNumber(1, 5, 3))->version, QVersionNumber({ 1, 5, 3, 1 }));
    QVERIFY(!catalog.findRelease(QVersionNumber({ 1, 5, 3, 0 })));
    QVERIFY(!catalog.findRelease(QVersionNumber()));
    QVERIFY(!catalog.findRelease(QVersionNumber(9, 9, 9)));
    QCOMPARE(pdf::PDFAppWhatsNewPage::findRunningRelease(catalog, QVersionNumber({ 1, 5, 2, 7 }))->version, QVersionNumber({ 1, 5, 2, 0 }));
    QCOMPARE(pdf::PDFAppWhatsNewPage::findRunningRelease(catalog, QVersionNumber(0, 1))->version, catalog.getReleases().front().version);

    // File names
    QString language;
    QCOMPARE(pdf::PDFWhatsNewCatalog::parseFileName(QStringLiteral("1.6.0.0.md"), &language), QVersionNumber({ 1, 6, 0, 0 }));
    QVERIFY(language.isEmpty());
    QCOMPARE(pdf::PDFWhatsNewCatalog::parseFileName(QStringLiteral("1.6.0.0.pt_BR.md"), &language), QVersionNumber({ 1, 6, 0, 0 }));
    QCOMPARE(language, QStringLiteral("pt_BR"));
    QVERIFY(pdf::PDFWhatsNewCatalog::parseFileName(QStringLiteral("1.6.0.0.PT.md"), &language).isNull());
    QVERIFY(pdf::PDFWhatsNewCatalog::parseFileName(QStringLiteral("notes.md"), &language).isNull());
    QVERIFY(pdf::PDFWhatsNewCatalog::parseFileName(QStringLiteral("1.6.txt"), &language).isNull());
    QCOMPARE(pdf::PDFWhatsNewCatalog::getLanguageFallbacks(QStringLiteral("pt_BR")), (QStringList{ QStringLiteral("pt_BR"), QStringLiteral("pt") }));
    QCOMPARE(pdf::PDFWhatsNewCatalog::getLanguageFallbacks(QStringLiteral("cs")), (QStringList{ QStringLiteral("cs") }));
    QVERIFY(pdf::PDFWhatsNewCatalog::getLanguageFallbacks(QStringLiteral("en")).isEmpty());
    QVERIFY(pdf::PDFWhatsNewCatalog::getLanguageFallbacks(QStringLiteral("../x")).isEmpty());

    // A folder with damaged, foreign and duplicate files: they are skipped with errors
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    auto write = [&directory](const QString& name, const QString& text)
    {
        QFile file(QDir(directory.path()).filePath(name));
        QVERIFY(file.open(QFile::WriteOnly));
        file.write(text.toUtf8());
    };
    write(QStringLiteral("2.0.md"), QStringLiteral("---\nversion: 2.0\ndate: 2027-01-01\nsummary: S\n---\n## Overview\nA.\n"));
    write(QStringLiteral("2.0.0.md"), QStringLiteral("---\nversion: 2.0.0\ndate: 2027-01-01\nsummary: S\n---\n## Overview\nB.\n"));
    write(QStringLiteral("1.9.md"), QStringLiteral("---\nversion: 1.8\ndate: 2027-01-01\nsummary: S\n---\n## Overview\nC.\n"));
    write(QStringLiteral("1.7.md"), QStringLiteral("garbage"));
    write(QStringLiteral("readme.md"), QStringLiteral("x"));
    write(QStringLiteral("1.5.md"), QStringLiteral("---\nversion: 1.5\ndate: 2025-01-01\nsummary: S\n---\n## Overview\nD.\n"));
    const pdf::PDFWhatsNewCatalog folderCatalog = pdf::PDFWhatsNewCatalog::load(directory.path(), pdf::PDFWhatsNewCatalog::createDefaultOptions());
    QCOMPARE(folderCatalog.getReleases().size(), size_t(2));
    QCOMPARE(folderCatalog.getReleases()[0].overview, QStringLiteral("B."));
    QCOMPARE(folderCatalog.getErrors().size(), 4);
}

void AppContentTest::whatsNewTranslation()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    auto write = [&directory](const QString& name, const QString& text)
    {
        QFile file(QDir(directory.path()).filePath(name));
        QVERIFY(file.open(QFile::WriteOnly));
        file.write(text.toUtf8());
    };
    write(QStringLiteral("2.0.md"), QStringLiteral("---\nversion: 2.0\ndate: 2027-01-01\nsummary: English\n---\n## Overview\nEnglish.\n"));
    write(QStringLiteral("2.0.pt.md"), QStringLiteral("---\nversion: 2.0\ndate: 2027-01-01\nsummary: Portugu\u00EAs\n---\n## Overview\nPortugu\u00EAs.\n"));
    write(QStringLiteral("2.0.cs.md"), QStringLiteral("broken"));

    const pdf::PDFWhatsNewCatalog catalog = pdf::PDFWhatsNewCatalog::load(directory.path(), pdf::PDFWhatsNewCatalog::createDefaultOptions());
    QCOMPARE(catalog.getReleases().size(), size_t(1));
    QCOMPARE(catalog.getLocalizedRelease(QVersionNumber(2), QStringLiteral("pt_BR")).summary, QStringLiteral("Portugu\u00EAs"));
    QCOMPARE(catalog.getLocalizedRelease(QVersionNumber(2), QStringLiteral("pt_BR")).language, QStringLiteral("pt"));
    QCOMPARE(catalog.getLocalizedRelease(QVersionNumber(2), QStringLiteral("cs")).summary, QStringLiteral("English"));
    QCOMPARE(catalog.getLocalizedRelease(QVersionNumber(2), QString()).summary, QStringLiteral("English"));
    QVERIFY(!catalog.getLocalizedRelease(QVersionNumber(3), QStringLiteral("pt")).isValid());

    // Every bundled translation has the structure of its English file
    const pdf::PDFWhatsNewCatalog bundled = pdf::PDFWhatsNewCatalog::load();
    const QDir resources(pdf::PDFWhatsNewCatalog::getResourceDirectory());
    for (const QString& fileName : resources.entryList(QStringList() << QStringLiteral("*.md"), QDir::Files))
    {
        QString language;
        const QVersionNumber version = pdf::PDFWhatsNewCatalog::parseFileName(fileName, &language);
        if (language.isEmpty())
        {
            continue;
        }

        const pdf::PDFWhatsNewRelease* english = bundled.findRelease(version);
        QVERIFY2(english, qPrintable(fileName));
        const pdf::PDFWhatsNewRelease translated = bundled.getLocalizedRelease(version, language);
        QCOMPARE(translated.language, language);
        QCOMPARE(translated.highlights.size(), english->highlights.size());
        QCOMPARE(translated.newTools, english->newTools);
        QCOMPARE(translated.fixedIssues.size(), english->fixedIssues.size());
        for (size_t i = 0; i < english->fixedIssues.size(); ++i)
        {
            QCOMPARE(translated.fixedIssues[i].number, english->fixedIssues[i].number);
        }
    }
}

std::vector<AppContentTest::ReleaseSection> AppContentTest::parseReleases(const QString& text)
{
    std::vector<ReleaseSection> sections;
    static const QRegularExpression versionPattern(QStringLiteral("^V:\\s*([0-9.]+)[ ,A-Za-z]*\\s+(\\d+)\\.(\\d+)\\.(\\d+)(.*)$"));
    static const QRegularExpression issuePattern(QStringLiteral("^-\\s*Issue #(\\d+):\\s*(.*)$"));
    for (const QString& line : text.split(QChar(u'\n')))
    {
        const QString trimmed = line.trimmed();
        if (trimmed.startsWith(QStringLiteral("CURRENT:")))
        {
            sections.push_back(ReleaseSection());
            continue;
        }

        const QRegularExpressionMatch versionMatch = versionPattern.match(trimmed);
        if (versionMatch.hasMatch())
        {
            ReleaseSection section;
            section.version = versionMatch.captured(1);
            section.date = QStringLiteral("%1-%2-%3").arg(versionMatch.captured(4).toInt(), 4, 10, QLatin1Char('0'))
                                                     .arg(versionMatch.captured(3).toInt(), 2, 10, QLatin1Char('0'))
                                                     .arg(versionMatch.captured(2).toInt(), 2, 10, QLatin1Char('0'));
            section.isRemoved = versionMatch.captured(5).contains(QStringLiteral("removed"));
            sections.push_back(section);
            continue;
        }

        const QRegularExpressionMatch issueMatch = issuePattern.match(trimmed);
        if (issueMatch.hasMatch() && !sections.empty())
        {
            sections.back().issues.emplace_back(issueMatch.captured(1).toInt(), issueMatch.captured(2).trimmed());
        }
    }
    return sections;
}

void AppContentTest::whatsNewBundledFiles()
{
    // Rules of the bundled files (agent C, step 2):
    // - every released version of RELEASES.txt has a file <version>.md with its date
    //   and exactly its issues (numbers and titles);
    // - a release marked "removed" is merged into the next newer release (its issues
    //   are listed in the file of that release);
    // - "date: unreleased" (the placeholder of a release, whose date is not known)
    //   is allowed only for a version without a "V:" line; its issues are the
    //   section CURRENT;
    // - every file of the folder is in whatsnew.qrc.
    const QString releasesText = readSourceFile(QStringLiteral("RELEASES.txt"));
    QVERIFY(!releasesText.isEmpty());
    const std::vector<ReleaseSection> sections = parseReleases(releasesText);
    QVERIFY(sections.size() > 10);

    const pdf::PDFWhatsNewCatalog catalog = pdf::PDFWhatsNewCatalog::load();
    QVERIFY(catalog.getErrors().isEmpty());

    std::set<QString> releasedVersions;
    const ReleaseSection* removed = nullptr;
    for (auto it = sections.rbegin(); it != sections.rend(); ++it)
    {
        const ReleaseSection& section = *it;
        if (section.version.isEmpty())
        {
            continue;
        }
        releasedVersions.insert(section.version);
        if (section.isRemoved)
        {
            removed = &section;
            continue;
        }

        const pdf::PDFWhatsNewRelease* release = catalog.findRelease(pdf::PDFWhatsNewParser::parseVersion(section.version));
        QVERIFY2(release, qPrintable(section.version));
        QCOMPARE(release->version.toString(), section.version);
        QCOMPARE(release->date.toString(Qt::ISODate), section.date);
        QVERIFY(!release->isUnreleased);

        std::vector<std::pair<int, QString>> expected;
        if (removed)
        {
            expected = removed->issues;
        }
        expected.insert(expected.end(), section.issues.begin(), section.issues.end());
        removed = nullptr;

        QCOMPARE(release->fixedIssues.size(), expected.size());
        for (size_t i = 0; i < expected.size(); ++i)
        {
            QCOMPARE(release->fixedIssues[i].number, expected[i].first);
            QCOMPARE(release->fixedIssues[i].title, expected[i].second);
        }
    }

    for (const pdf::PDFWhatsNewRelease& release : catalog.getReleases())
    {
        if (release.isUnreleased)
        {
            QVERIFY2(releasedVersions.count(release.version.toString()) == 0, qPrintable(release.version.toString()));
            const ReleaseSection& current = sections.front();
            QVERIFY(current.version.isEmpty());
            QCOMPARE(release.fixedIssues.size(), current.issues.size());
        }
        else
        {
            QVERIFY2(releasedVersions.count(release.version.toString()) > 0, qPrintable(release.version.toString()));
        }
    }

    // The resources are the files of the source folder, and all of them are in the qrc
    const QString qrc = readSourceFile(QStringLiteral("Pdf4QtLibApp/resources/whatsnew.qrc"));
    const QDir sourceFolder(QDir(QStringLiteral(PDF4QT_SOURCE_DIRECTORY)).filePath(QStringLiteral("Pdf4QtLibApp/resources/whatsnew")));
    const QStringList sourceFiles = sourceFolder.entryList(QStringList() << QStringLiteral("*.md"), QDir::Files);
    QVERIFY(!sourceFiles.isEmpty());
    for (const QString& fileName : sourceFiles)
    {
        QVERIFY2(qrc.contains(QStringLiteral("whatsnew/") + fileName), qPrintable(fileName));
        QVERIFY(QFile::exists(pdf::PDFWhatsNewCatalog::getResourceDirectory() + QStringLiteral("/") + fileName));
    }

    // The release notes of every file are valid Markdown with links only to GitHub
    for (const pdf::PDFWhatsNewRelease& release : catalog.getReleases())
    {
        const pdf::PDFAppMarkdownDocument document = pdf::PDFAppMarkdownParser::parse(release.releaseNotes);
        for (const QString& link : document.getLinks())
        {
            QVERIFY2(link.startsWith(QStringLiteral("https://github.com/JakubMelka/PDF4QT/")) || link.startsWith(QStringLiteral("/")), qPrintable(link));
        }
    }
}

void AppContentTest::whatsNewParserEdges()
{
    // A hue outside of the enumeration has the name of the neutral hue
    QCOMPARE(pdf::PDFWhatsNewParser::getHueName(pdf::PDFAccentHue(99)), QStringLiteral("neutral"));

    // Options without the checks of the tools and of the icons accept any identifier
    const QString text = validWhatsNewFile().replace(QStringLiteral("icon: zap"), QStringLiteral("icon: rocket")).replace(QStringLiteral("- pages.merge"), QStringLiteral("- pages.teleport"));
    const pdf::PDFWhatsNewParser::Result result = pdf::PDFWhatsNewParser::parse(text, pdf::PDFWhatsNewParser::Options());
    QVERIFY(result.isValid());
    QCOMPARE(result.release.newTools.last(), QStringLiteral("pages.teleport"));
    QCOMPARE(result.release.highlights.at(1).iconName, QStringLiteral("rocket"));

    // A file, which ends by its highlights (no blank line after the last one)
    const pdf::PDFWhatsNewParser::Result highlightsLast = pdf::PDFWhatsNewParser::parse(QStringLiteral(
        "---\nversion: 1.0\ndate: 2020-01-01\nsummary: S\n---\n## Overview\nText.\n\n## Highlights\n\n### Last\nicon: zap · hue: teal\nThe description"), pdf::PDFWhatsNewParser::Options());
    QVERIFY(highlightsLast.isValid());
    QCOMPARE(highlightsLast.release.highlights.size(), size_t(1));
    QCOMPARE(highlightsLast.release.highlights.front().description, QStringLiteral("The description"));
    QCOMPARE(int(highlightsLast.release.highlights.front().hue), int(pdf::PDFAccentHue::Teal));

    // File names without the language output, English variants, a broken version
    QCOMPARE(pdf::PDFWhatsNewCatalog::parseFileName(QStringLiteral("1.6.md"), nullptr), QVersionNumber(1, 6));
    QCOMPARE(pdf::PDFWhatsNewCatalog::parseFileName(QStringLiteral("1.6.pt.md"), nullptr), QVersionNumber(1, 6));
    QString language = QStringLiteral("stale");
    QVERIFY(pdf::PDFWhatsNewCatalog::parseFileName(QStringLiteral("a.b.pt.md"), &language).isNull());
    QVERIFY(language.isEmpty());
    QVERIFY(pdf::PDFWhatsNewCatalog::getLanguageFallbacks(QStringLiteral("en_US")).isEmpty());
}

void AppContentTest::whatsNewCatalogLimits()
{
    // A file larger than the limit is not read, a translation larger than the limit
    // is not used (the English text)
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QDir folder(directory.path());
    writeFile(folder.filePath(QStringLiteral("2.0.md")), QByteArrayLiteral("---\nversion: 2.0\ndate: 2027-01-01\nsummary: English\n---\n## Overview\nEnglish.\n"));
    const QByteArray huge = QByteArray(pdf::PDFWhatsNewParser::MAXIMUM_FILE_SIZE * 4 + 1, 'x');
    writeFile(folder.filePath(QStringLiteral("2.1.md")), huge);
    writeFile(folder.filePath(QStringLiteral("2.0.pt.md")), QByteArrayLiteral("---\nversion: 2.0\ndate: 2027-01-01\nsummary: Portugues\n---\n## Overview\nPortugues.\n") + huge);

    const pdf::PDFWhatsNewCatalog catalog = pdf::PDFWhatsNewCatalog::load(directory.path(), pdf::PDFWhatsNewCatalog::createDefaultOptions());
    QCOMPARE(catalog.getReleases().size(), size_t(1));
    QCOMPARE(catalog.getErrors(), QStringList{ QStringLiteral("2.1.md: cannot be read") });
    QCOMPARE(catalog.getLocalizedRelease(QVersionNumber(2), QStringLiteral("pt")).summary, QStringLiteral("English"));
}

// --------------------------------------------------------------- Update check

void AppContentTest::updateStatus_data()
{
    QTest::addColumn<bool>("autoCheck");
    QTest::addColumn<bool>("checkingHere");
    QTest::addColumn<qint64>("lastCheck");
    QTest::addColumn<qint64>("lastSuccess");
    QTest::addColumn<QString>("latest");
    QTest::addColumn<QString>("error");
    QTest::addColumn<int>("state");

    using State = pdf::PDFUpdateStatus::State;
    const qint64 now = 1000000000000;
    const qint64 hour = 3600000;
    QTest::newRow("never") << true << false << qint64(0) << qint64(0) << QString() << QString() << int(State::NotChecked);
    QTest::newRow("checking here") << true << true << now - hour << now - hour << QStringLiteral("1.6.0.0") << QString() << int(State::Checking);
    QTest::newRow("pending elsewhere") << true << false << now - 1000 << now - hour << QStringLiteral("1.6.0.0") << QString() << int(State::Checking);
    QTest::newRow("stale reservation") << true << false << now - hour << now - 2 * hour << QStringLiteral("1.6.0.0") << QString() << int(State::UpToDate);
    QTest::newRow("up to date") << true << false << now - hour << now - hour << QStringLiteral("1.6.0.0") << QString() << int(State::UpToDate);
    QTest::newRow("older on github") << true << false << now - hour << now - hour << QStringLiteral("1.5.0") << QString() << int(State::UpToDate);
    QTest::newRow("update") << true << false << now - hour << now - hour << QStringLiteral("1.7.0") << QString() << int(State::UpdateAvailable);
    QTest::newRow("update auto off") << false << false << now - hour << now - hour << QStringLiteral("2.0") << QString() << int(State::UpdateAvailable);
    QTest::newRow("failed") << true << false << now - hour << now - 2 * hour << QStringLiteral("1.6.0.0") << QStringLiteral("Error") << int(State::Failed);
    QTest::newRow("failed never") << true << false << now - 1000 << qint64(0) << QString() << QStringLiteral("Error") << int(State::Failed);
    QTest::newRow("auto off") << false << false << now - hour << now - hour << QStringLiteral("1.6.0.0") << QString() << int(State::AutoCheckOff);
    QTest::newRow("auto off never") << false << false << qint64(0) << qint64(0) << QString() << QString() << int(State::AutoCheckOff);
    QTest::newRow("invalid latest") << true << false << now - hour << now - hour << QStringLiteral("garbage") << QString() << int(State::UpToDate);
    QTest::newRow("future check") << true << false << now + hour << qint64(0) << QString() << QString() << int(State::NotChecked);
}

void AppContentTest::updateStatus()
{
    QFETCH(bool, autoCheck);
    QFETCH(bool, checkingHere);
    QFETCH(qint64, lastCheck);
    QFETCH(qint64, lastSuccess);
    QFETCH(QString, latest);
    QFETCH(QString, error);
    QFETCH(int, state);

    const pdf::PDFUpdateStatus status = pdf::PDFUpdateStatus::create(autoCheck, checkingHere, lastCheck, lastSuccess, latest, error, QVersionNumber({ 1, 6, 0, 0 }), 1000000000000);
    QCOMPARE(int(status.state), state);
    if (status.state == pdf::PDFUpdateStatus::State::UpdateAvailable)
    {
        QCOMPARE(status.getDownloadUrl(), QUrl(QStringLiteral("https://github.com/JakubMelka/PDF4QT/releases/tag/v%1").arg(latest)));
    }
}

void AppContentTest::updateFormatTime()
{
    const QDateTime now(QDate(2026, 9, 30), QTime(15, 0));
    const qint64 nowTime = now.toMSecsSinceEpoch();
    QCOMPARE(pdf::PDFUpdateStatus::formatTime(QDateTime(QDate(2026, 9, 30), QTime(9, 12)).toMSecsSinceEpoch(), nowTime), QStringLiteral("today at 09:12"));
    QCOMPARE(pdf::PDFUpdateStatus::formatTime(QDateTime(QDate(2026, 9, 29), QTime(8, 40)).toMSecsSinceEpoch(), nowTime), QStringLiteral("yesterday at 08:40"));
    QCOMPARE(pdf::PDFUpdateStatus::formatTime(QDateTime(QDate(2026, 9, 14), QTime(10, 0)).toMSecsSinceEpoch(), nowTime), QStringLiteral("Sep 14, 2026 at 10:00"));
    QCOMPARE(pdf::PDFUpdateStatus::formatDate(QDate(2026, 6, 4)), QStringLiteral("Jun 4, 2026"));
    QCOMPARE(pdf::PDFUpdateChecker::getReleaseUrl(QVersionNumber()), QUrl(QStringLiteral("https://github.com/JakubMelka/PDF4QT/releases")));
    QCOMPARE(pdf::PDFUpdateChecker::getDefaultUrl().scheme(), QStringLiteral("https"));

    QString error;
    QCOMPARE(pdf::PDFUpdateChecker::parseReply(TestReleaseServer::createRelease(QStringLiteral("v1.6.1.0")), &error), QVersionNumber({ 1, 6, 1, 0 }));
    QVERIFY(error.isEmpty());
    QCOMPARE(pdf::PDFUpdateChecker::parseReply(TestReleaseServer::createRelease(QStringLiteral("V2.0")), &error), QVersionNumber(2, 0));
    QVERIFY(pdf::PDFUpdateChecker::parseReply(QByteArrayLiteral("[]"), &error).isNull());
    QVERIFY(!error.isEmpty());
    QVERIFY(pdf::PDFUpdateChecker::parseReply(QByteArrayLiteral("{\"tag_name\": 7}"), &error).isNull());
    QVERIFY(pdf::PDFUpdateChecker::parseReply(QByteArray(pdf::PDFUpdateChecker::MAXIMUM_REPLY_SIZE + 1, ' '), &error).isNull());
    QCOMPARE(pdf::PDFUpdateChecker::parseReply(TestReleaseServer::createRelease(QStringLiteral("v1.0")), nullptr), QVersionNumber(1, 0));
    QVERIFY(pdf::PDFUpdateChecker::parseReply(QByteArrayLiteral("[]"), nullptr).isNull());
}

void AppContentTest::updateReplies_data()
{
    QTest::addColumn<int>("status");
    QTest::addColumn<QByteArray>("body");
    QTest::addColumn<QByteArray>("location");
    QTest::addColumn<bool>("isAnswered");
    QTest::addColumn<QString>("latest");
    QTest::addColumn<bool>("isError");

    QTest::newRow("newer") << 200 << TestReleaseServer::createRelease(QStringLiteral("v1.7.0.0")) << QByteArray() << true << QStringLiteral("1.7.0.0") << false;
    QTest::newRow("same") << 200 << TestReleaseServer::createRelease(QStringLiteral("v1.6.0.0")) << QByteArray() << true << QStringLiteral("1.6.0.0") << false;
    QTest::newRow("not found") << 404 << QByteArray("{}") << QByteArray() << true << QString() << true;
    QTest::newRow("rate limit") << 403 << QByteArray("{\"message\": \"API rate limit exceeded\"}") << QByteArray() << true << QString() << true;
    QTest::newRow("server error") << 500 << QByteArray() << QByteArray() << true << QString() << true;
    QTest::newRow("invalid json") << 200 << QByteArray("{\"tag_name\": ") << QByteArray() << true << QString() << true;
    QTest::newRow("wrong type") << 200 << QByteArray("{\"tag_name\": [1, 2]}") << QByteArray() << true << QString() << true;
    QTest::newRow("no tag") << 200 << QByteArray("{\"name\": \"x\"}") << QByteArray() << true << QString() << true;
    QTest::newRow("bad version") << 200 << TestReleaseServer::createRelease(QStringLiteral("nightly-2026")) << QByteArray() << true << QString() << true;
    QTest::newRow("empty") << 200 << QByteArray() << QByteArray() << true << QString() << true;
    QTest::newRow("binary") << 200 << QByteArray("\x00\xFF\xFE garbage", 12) << QByteArray() << true << QString() << true;
    QTest::newRow("huge") << 200 << QByteArray(3 * 1024 * 1024, 'x') << QByteArray() << true << QString() << true;
    QTest::newRow("redirect to http") << 302 << QByteArray() << QByteArray("http://example.invalid/latest") << true << QString() << true;
    QTest::newRow("redirect same origin") << 302 << QByteArray() << QByteArray("/repos/other/latest") << true << QString() << true;
    QTest::newRow("timeout") << 200 << QByteArray() << QByteArray() << false << QString() << true;
}

void AppContentTest::updateReplies()
{
    QFETCH(int, status);
    QFETCH(QByteArray, body);
    QFETCH(QByteArray, location);
    QFETCH(bool, isAnswered);
    QFETCH(QString, latest);
    QFETCH(bool, isError);

    TestReleaseServer server;
    QVERIFY(server.start());
    server.reply.status = status;
    server.reply.body = body;
    server.reply.isAnswered = isAnswered;
    server.reply.headers.append({ QByteArrayLiteral("Set-Cookie"), QByteArrayLiteral("tracking=1; Path=/") });
    if (!location.isEmpty())
    {
        server.reply.headers.append({ QByteArrayLiteral("Location"), location });
    }

    ContentFixture fixture(false);
    pdf::PDFUpdateChecker* checker = fixture.checker();
    checker->setUrl(server.getUrl());
    checker->setTimeout(500);
    checker->setRunningVersion(QVersionNumber({ 1, 6, 0, 0 }));

    QVERIFY(checker->checkNow());
    QVERIFY(checker->isChecking());
    QCOMPARE(int(checker->getStatus().state), int(pdf::PDFUpdateStatus::State::Checking));
    QVERIFY(!checker->checkNow());
    QTRY_VERIFY_WITH_TIMEOUT(!checker->isChecking(), 10000);

    const pdf::PDFUpdateStatus updateStatus = checker->getStatus();
    QCOMPARE(updateStatus.error.isEmpty(), !isError);
    if (!isError)
    {
        QCOMPARE(updateStatus.latestVersion.toString(), latest);
        QVERIFY(updateStatus.lastSuccess > 0);
    }
    else
    {
        QCOMPARE(int(updateStatus.state), int(pdf::PDFUpdateStatus::State::Failed));
        QVERIFY(updateStatus.error.size() <= 1024);
    }

    // The request: the user agent of PDF4QT, no cookies (also not in a second request)
    QVERIFY(!server.requests.isEmpty());
    const QByteArray request = server.requests.first().toLower();
    QVERIFY(request.contains("user-agent: pdf4qt/1.6.0.0"));
    QVERIFY(!request.contains("cookie:"));
    QVERIFY(!request.contains("authorization:"));

    if (status == 200 && isAnswered)
    {
        QVERIFY(checker->checkNow());
        QTRY_VERIFY_WITH_TIMEOUT(!checker->isChecking(), 10000);
        QVERIFY(!server.requests.last().toLower().contains("cookie:"));
    }

    // A same origin redirect is followed (the server answers it again by a redirect,
    // until the limit of the redirects)
    if (location.startsWith('/'))
    {
        QVERIFY(server.requests.size() > 1);
    }
}

void AppContentTest::updateOncePerDay()
{
    TestReleaseServer server;
    QVERIFY(server.start());
    server.reply.body = TestReleaseServer::createRelease(QStringLiteral("v1.6.0.0"));

    ContentFixture fixture(false);
    pdf::PDFUpdateChecker* checker = fixture.checker();
    checker->setUrl(server.getUrl());
    qint64 now = QDateTime(QDate(2026, 10, 1), QTime(12, 0)).toMSecsSinceEpoch();
    int clockReads = 0;
    checker->setClock([&now, &clockReads]() { ++clockReads; return now; });

    QCOMPARE(int(checker->getStatus().state), int(pdf::PDFUpdateStatus::State::NotChecked));
    QVERIFY(checker->checkIfDue());
    QTRY_VERIFY(!checker->isChecking());
    QCOMPARE(server.requests.size(), 1);
    QCOMPARE(int(checker->getStatus().state), int(pdf::PDFUpdateStatus::State::UpToDate));

    // Not again on the same day, nor by the timer of the automatic check
    now += 23 * 3600 * 1000;
    QVERIFY(!checker->checkIfDue());
    const int readsBeforeTimer = clockReads;
    checker->startAutomaticChecks(0);
    QTRY_VERIFY(clockReads > readsBeforeTimer);
    QVERIFY(!checker->isChecking());
    QCOMPARE(server.requests.size(), 1);

    // After a day
    now += 3600 * 1000;
    QVERIFY(checker->checkIfDue());
    QTRY_VERIFY(!checker->isChecking());
    QCOMPARE(server.requests.size(), 2);

    // The automatic check off: only the button checks
    QVERIFY(!fixture.context->getSettings().setValue(pdf::PDFSettingKeys::updateAutoCheck(), false).isError());
    now += 48 * 3600 * 1000;
    QVERIFY(!checker->checkIfDue());
    QCOMPARE(int(checker->getStatus().state), int(pdf::PDFUpdateStatus::State::AutoCheckOff));
    QVERIFY(checker->checkNow());
    QTRY_VERIFY(!checker->isChecking());
    QCOMPARE(server.requests.size(), 3);

    // The clock was set back: the last check in the future does not block the checks
    QVERIFY(!fixture.context->getSettings().setValue(pdf::PDFSettingKeys::updateAutoCheck(), true).isError());
    now -= 30 * 24 * 3600 * qint64(1000);
    QVERIFY(checker->checkIfDue());
    QTRY_VERIFY(!checker->isChecking());
    QCOMPARE(server.requests.size(), 4);

    // The automatic checks of the timer
    now += 2 * 24 * 3600 * qint64(1000);
    checker->startAutomaticChecks(0);
    QTRY_COMPARE(server.requests.size(), 5);
    QTRY_VERIFY(!checker->isChecking());
}

void AppContentTest::updateTwoInstances()
{
    // Two instances (two contexts with their own connections to one database file):
    // only one of them asks GitHub, the other one reads the result
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString databaseFile = QDir(directory.path()).filePath(QStringLiteral("pdf4qt.db"));

    TestReleaseServer server;
    QVERIFY(server.start());
    server.reply.body = TestReleaseServer::createRelease(QStringLiteral("v9.0.0.0"));
    server.reply.delay = 200;

    {
        ContentFixture first(false, databaseFile);
        ContentFixture second(false, databaseFile);
        QVERIFY(first.context->getStorageState().isPersistent());
        QVERIFY(second.context->getStorageState().isPersistent());
        first.checker()->setUrl(server.getUrl());
        second.checker()->setUrl(server.getUrl());

        QVERIFY(first.checker()->checkIfDue());
        QVERIFY(!second.checker()->checkIfDue());
        QCOMPARE(int(second.checker()->getStatus().state), int(pdf::PDFUpdateStatus::State::Checking));

        QTRY_VERIFY(!first.checker()->isChecking());
        QCOMPARE(server.requests.size(), 1);
        QCOMPARE(int(first.checker()->getStatus().state), int(pdf::PDFUpdateStatus::State::UpdateAvailable));
        QCOMPARE(int(second.checker()->getStatus().state), int(pdf::PDFUpdateStatus::State::UpdateAvailable));

        // The other instance is notified by its notifier
        QSignalSpy spy(second.context->getNotifier(), &pdf::PDFDatabaseNotifier::changed);
        QVERIFY(first.checker()->checkNow());
        QTRY_VERIFY(!first.checker()->isChecking());
        QTRY_VERIFY(!spy.isEmpty());
    }
}

void AppContentTest::updateSlowServer()
{
    // A server, which sends one byte now and then: the transfer timeout of the request
    // is restarted by every byte, the whole request still ends after the time limit
    QTcpServer server;
    QList<QPointer<QTcpSocket>> sockets;
    QObject::connect(&server, &QTcpServer::newConnection, &server, [&server, &sockets]()
    {
        while (QTcpSocket* socket = server.nextPendingConnection())
        {
            sockets.append(socket);
            QObject::connect(socket, &QTcpSocket::readyRead, socket, [socket]()
            {
                if (!socket->readAll().contains("\r\n\r\n") || socket->property("answered").toBool())
                {
                    return;
                }
                socket->setProperty("answered", true);
                socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: 100000\r\n\r\n{");
                QTimer* trickle = new QTimer(socket);
                QObject::connect(trickle, &QTimer::timeout, socket, [socket]() { socket->write(" "); });
                trickle->start(50);
            });
        }
    });
    QVERIFY(server.listen(QHostAddress::LocalHost));

    ContentFixture fixture(false);
    pdf::PDFUpdateChecker* checker = fixture.checker();
    checker->setUrl(QUrl(QStringLiteral("http://127.0.0.1:%1/latest").arg(server.serverPort())));
    checker->setTimeout(1000);

    QElapsedTimer timer;
    timer.start();
    QVERIFY(checker->checkNow());
    QTRY_VERIFY_WITH_TIMEOUT(!checker->isChecking(), 15000);
    QVERIFY2(timer.elapsed() < 10000, qPrintable(QString::number(timer.elapsed())));
    QVERIFY(!sockets.isEmpty());

    const pdf::PDFUpdateStatus status = checker->getStatus();
    QCOMPARE(int(status.state), int(pdf::PDFUpdateStatus::State::Failed));
    QVERIFY(!status.error.isEmpty());

    for (const QPointer<QTcpSocket>& socket : sockets)
    {
        delete socket.data();
    }
}

void AppContentTest::updateCheckerDeleted()
{
    TestReleaseServer server;
    QVERIFY(server.start());
    server.reply.body = TestReleaseServer::createRelease(QStringLiteral("v1.6.0.0"));
    server.reply.delay = 300;

    // The checker is deleted while its reply runs
    {
        ContentFixture fixture(false);
        QPointer<pdf::PDFUpdateChecker> checker = fixture.checker();
        checker->setUrl(server.getUrl());
        QVERIFY(checker->checkNow());
        QTRY_COMPARE(server.requests.size(), 1);
        const int closed = server.closedConnections;
        delete checker.data();
        QTRY_VERIFY(server.closedConnections > closed);
        QCoreApplication::processEvents();
    }

    // The context (with its checker and database) is deleted while the reply runs
    {
        ContentFixture fixture(false);
        fixture.checker()->setUrl(server.getUrl());
        QVERIFY(fixture.checker()->checkNow());
        QTRY_COMPARE(server.requests.size(), 2);
        const int closed = server.closedConnections;
        fixture.context.reset();
        QTRY_VERIFY(server.closedConnections > closed);
        QCoreApplication::processEvents();
    }

    // A slot of the status deletes the checker
    {
        ContentFixture fixture(false);
        QPointer<pdf::PDFUpdateChecker> checker = fixture.checker();
        checker->setUrl(server.getUrl());
        QObject::connect(checker.data(), &pdf::PDFUpdateChecker::statusChanged, checker.data(), [&checker]()
        {
            if (!checker->isChecking())
            {
                checker->deleteLater();
            }
        });
        QVERIFY(checker->checkNow());
        QTRY_VERIFY_WITH_TIMEOUT(checker.isNull(), 5000);
    }
}

void AppContentTest::updateReplyErrors_data()
{
    // Status -1 = no answer; headers "Name: value" by lines, PORT = the port of the server
    QTest::addColumn<int>("status");
    QTest::addColumn<QByteArray>("body");
    QTest::addColumn<QByteArray>("headers");
    QTest::addColumn<QString>("expected");
    QTest::addColumn<QString>("forbidden");

    const QByteArray bomb = qCompress(QByteArray(2 * 1024 * 1024, ' '), 9).mid(4);
    QTest::newRow("no content") << 204 << QByteArray() << QByteArray() << "error 204" << "";
    QTest::newRow("not found") << 404 << QByteArray("{}") << QByteArray() << "error 404" << "";
    QTest::newRow("no answer") << -1 << QByteArray() << QByteArray() << "did not answer" << "";
    QTest::newRow("too large") << 200 << QByteArray(2 * 1024 * 1024, ' ') << QByteArray() << "too large" << "";
    QTest::newRow("compressed bomb") << 200 << bomb << QByteArray("Content-Encoding: deflate") << "too large" << "";
    QTest::newRow("long tag") << 200 << TestReleaseServer::createRelease(QStringLiteral("v") + QString(40, QLatin1Char('1'))) << QByteArray() << "no valid version" << "";
    QTest::newRow("redirect other port") << 302 << QByteArray() << QByteArray("Location: http://127.0.0.1:1/latest") << "not secure" << "";
    QTest::newRow("redirect other host") << 302 << QByteArray() << QByteArray("Location: http://localhost:PORT/latest") << "not secure" << "";
    QTest::newRow("redirect https") << 302 << QByteArray() << QByteArray("Location: https://127.0.0.1:PORT/latest") << "" << "not secure";
}

void AppContentTest::updateReplyErrors()
{
    QFETCH(int, status);
    QFETCH(QByteArray, body);
    QFETCH(QByteArray, headers);
    QFETCH(QString, expected);
    QFETCH(QString, forbidden);

    TestReleaseServer server;
    QVERIFY(server.start());
    server.reply.status = status;
    server.reply.body = body;
    server.reply.isAnswered = status > 0;
    const QByteArray port = QByteArray::number(server.getUrl().port());
    for (const QByteArray& line : headers.split('\n'))
    {
        const qsizetype colon = line.indexOf(':');
        if (colon > 0)
        {
            server.reply.headers.append({ line.left(colon), line.mid(colon + 1).trimmed().replace("PORT", port) });
        }
    }

    ContentFixture fixture(false);
    pdf::PDFUpdateChecker* checker = fixture.checker();
    checker->setUrl(server.getUrl());
    checker->setTimeout(1000);

    QVERIFY(checker->checkNow());
    QTRY_VERIFY_WITH_TIMEOUT(!checker->isChecking(), 10000);
    const pdf::PDFUpdateStatus updateStatus = checker->getStatus();
    QCOMPARE(int(updateStatus.state), int(pdf::PDFUpdateStatus::State::Failed));
    QVERIFY2(!updateStatus.error.isEmpty() && updateStatus.error.contains(expected), qPrintable(updateStatus.error));
    QVERIFY2(forbidden.isEmpty() || !updateStatus.error.contains(forbidden), qPrintable(updateStatus.error));
    QVERIFY(updateStatus.latestVersion.isNull());
}

void AppContentTest::updateCheckerWithoutDatabase()
{
    QCOMPARE(pdf::PDFUpdateChecker::getInstance(nullptr), nullptr);

    // A checker without a context, and the checker of a context without a database:
    // the default status, no request
    for (const bool hasContext : { false, true })
    {
        pdf::PDFAppContext context(pdf::PDFAppCommandLine::parse({ QStringLiteral("PDF4QT"), QStringLiteral("--portable") }, QDir::currentPath()));
        QVERIFY(!context.getDatabase());
        pdf::PDFUpdateChecker* checker = hasContext ? pdf::PDFUpdateChecker::getInstance(&context) : new pdf::PDFUpdateChecker(nullptr, &context);
        checker->setUrl(QUrl(QStringLiteral("http://127.0.0.1:1/latest")));
        checker->setRunningVersion(QVersionNumber({ 1, 6, 0, 0 }));

        const pdf::PDFUpdateStatus status = checker->getStatus();
        QCOMPARE(int(status.state), int(pdf::PDFUpdateStatus::State::NotChecked));
        QCOMPARE(status.runningVersion, QVersionNumber({ 1, 6, 0, 0 }));
        QVERIFY(status.isAutoCheck);
        QVERIFY(!checker->checkNow());
        QVERIFY(!checker->checkIfDue());
        QVERIFY(!checker->isChecking());
        QCOMPARE(checker->getRequestCount(), 0);
    }
}

void AppContentTest::updateDatabaseLocked()
{
    // Another instance holds the write lock of the database file
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString databaseFile = QDir(directory.path()).filePath(QStringLiteral("pdf4qt.db"));

    TestReleaseServer server;
    QVERIFY(server.start());
    server.reply.body = TestReleaseServer::createRelease(QStringLiteral("v1.7.0.0"));
    server.reply.delay = 200;

    ContentFixture holder(false, databaseFile);
    ContentFixture fixture(false, databaseFile);
    QVERIFY(holder.context->getDatabase() && fixture.context->getDatabase());
    fixture.context->getDatabase()->getConnection()->setBusyTimeout(50);
    pdf::PDFUpdateChecker* checker = fixture.checker();
    checker->setUrl(server.getUrl());
    checker->setRunningVersion(QVersionNumber({ 1, 6, 0, 0 }));

    // The check cannot be reserved: no request
    {
        pdf::PDFDatabaseTransaction lock(holder.context->getDatabase()->getConnection(), pdf::PDFDatabaseTransaction::Mode::Write);
        QVERIFY(lock.isActive());
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("^The check for updates cannot be reserved")));
        QVERIFY(!checker->checkNow());
        QVERIFY(!checker->isChecking());
        lock.rollback();
    }
    QVERIFY(server.requests.isEmpty());

    // A due check is not started, while a check runs
    QVERIFY(checker->checkNow());
    QVERIFY(!checker->checkIfDue());
    QTRY_COMPARE(server.requests.size(), 1);

    // The lock is taken while the reply runs: the result cannot be stored, the status
    // is told anyway
    {
        pdf::PDFDatabaseTransaction lock(holder.context->getDatabase()->getConnection(), pdf::PDFDatabaseTransaction::Mode::Write);
        QVERIFY(lock.isActive());
        QSignalSpy spy(checker, &pdf::PDFUpdateChecker::statusChanged);
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("^The result of the check for updates cannot be stored")));
        QTRY_VERIFY_WITH_TIMEOUT(!checker->isChecking(), 10000);
        QCOMPARE(spy.size(), 1);
        lock.rollback();
    }
    QVERIFY(checker->getStatus().latestVersion.isNull());
    QCOMPARE(checker->getRequestCount(), 1);
}

// --------------------------------------------------------------------- Pages

void AppContentTest::whatsNewPage()
{
    ContentFixture fixture;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    fixture.setUpdateValues(true, now - 3600000, now - 3600000, QStringLiteral("1.6.0.0"), QString());
    fixture.checker()->setRunningVersion(QVersionNumber({ 1, 6, 0, 0 }));

    QSignalSpy navigated(fixture.window->getNavigator(), &pdf::PDFAppNavigator::navigated);
    fixture.navigate(QStringLiteral("/whats-new"));
    pdf::PDFAppWhatsNewPage* page = fixture.page<pdf::PDFAppWhatsNewPage>();
    QVERIFY(page);
    QVERIFY(!navigated.isEmpty());
    QCOMPARE(navigated.last().at(2).toBool(), false);
    QCOMPARE(page->getRelease().version, QVersionNumber({ 1, 6, 0, 0 }));
    QVERIFY(page->isRunningRelease());
    QCOMPARE(page->getHeaderTitle(), QStringLiteral("What's New"));
    QCOMPARE(fixture.window->getHomeTab()->getNavItem(), pdf::PDFAppNavItem::WhatsNew);

    // The release card of the running version: "Up to date", Read full release notes
    QCOMPARE(page->getReleaseBadge()->getText(), QStringLiteral("Up to date"));
    QVERIFY(page->getReleaseBadge()->isVisible());
    QVERIFY(page->getReleaseNotesButton()->isVisible());
    QVERIFY(!page->getExploreToolsButton()->isVisible());
    QPushButton* notesButton = page->getReleaseNotesButton();
    notesButton->click();
    QCoreApplication::processEvents();
    QVERIFY(fixture.page<pdf::PDFAppReleaseNotesPage>());
    QCOMPARE(fixture.currentTab()->getHistory().getCurrentEntry()->route.toString(), QStringLiteral("/whats-new/1.6.0/notes"));
    QVERIFY(fixture.window->getNavigator()->goBack());
    QCoreApplication::processEvents();
    page = fixture.page<pdf::PDFAppWhatsNewPage>();
    QVERIFY(page);

    // /whats-new/1.6.0 is the same release; an unknown version is Page Not Found
    fixture.navigate(QStringLiteral("/whats-new/1.6.0"));
    QVERIFY(fixture.page<pdf::PDFAppWhatsNewPage>());
    QVERIFY(fixture.page<pdf::PDFAppWhatsNewPage>()->isRunningRelease());
    fixture.navigate(QStringLiteral("/whats-new/9.9.9"));
    QVERIFY(qobject_cast<pdf::PDFPageNotFoundPage*>(fixture.currentPage()));
    fixture.navigate(QStringLiteral("/whats-new/9.9.9/notes"));
    QVERIFY(qobject_cast<pdf::PDFPageNotFoundPage*>(fixture.currentPage()));

    // Issues and links open GitHub
    fixture.navigate(QStringLiteral("/whats-new"));
    page = fixture.page<pdf::PDFAppWhatsNewPage>();
    QSignalSpy external(fixture.window->getNavigator(), &pdf::PDFAppNavigator::externalUrlOpened);
    QVERIFY(!page->getIssueRows().isEmpty());
    const pdf::PDFWhatsNewIssue& issue = page->getRelease().fixedIssues.front();
    page->getIssueRows().front()->click();
    QCOMPARE(external.last().at(0).toUrl(), pdf::PDFAppWhatsNewPage::getIssueUrl(issue.number));
    page->getViewAllIssuesLink()->click();
    QCOMPARE(external.last().at(0).toUrl(), QUrl(QStringLiteral("https://github.com/JakubMelka/PDF4QT/releases/tag/v1.6.0.0")));
    QCOMPARE(page->getLinkRows().size(), 5);
    QStringList links;
    for (pdf::PDFLinkRow* row : page->getLinkRows())
    {
        row->click();
        links << external.last().at(0).toUrl().toString();
    }
    QCOMPARE(links.size(), 5);
    for (const QString& link : links)
    {
        QVERIFY2(link.startsWith(QStringLiteral("https://github.com/JakubMelka/PDF4QT")), qPrintable(link));
    }

    // The issue list scrolls inside of its card, at most 310 px high
    QVERIFY(page->getIssueScrollArea()->height() <= pdf::PDFAppWhatsNewPage::MAXIMUM_ISSUE_LIST_HEIGHT);
    QVERIFY(page->getIssueScrollArea()->verticalScrollBar()->maximum() > 0);
}

void AppContentTest::whatsNewPreviousVersion()
{
    ContentFixture fixture;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    fixture.setUpdateValues(true, now - 3600000, now - 3600000, QStringLiteral("1.6.0.0"), QString());
    fixture.checker()->setRunningVersion(QVersionNumber({ 1, 6, 0, 0 }));

    fixture.navigate(QStringLiteral("/whats-new/1.5.2"));
    pdf::PDFAppWhatsNewPage* page = fixture.page<pdf::PDFAppWhatsNewPage>();
    QVERIFY(page);
    QCOMPARE(page->getRelease().version, QVersionNumber({ 1, 5, 2, 0 }));
    QVERIFY(!page->isRunningRelease());
    QCOMPARE(page->getReleaseBadge()->getText(), QStringLiteral("Previous release"));
    QCOMPARE(page->getHeaderSubtitle(), QStringLiteral("What changed in PDF4QT 1.5.2, an earlier release."));
    QCOMPARE(fixture.window->getHomeTab()->getHeader()->getSubtitleLabel()->text(), page->getHeaderSubtitle());

    // The release card: a plain card, the meta text of an older version
    pdf::PDFTintedCard* card = qobject_cast<pdf::PDFTintedCard*>(page->getReleaseCard());
    QVERIFY(card);
    QVERIFY(!card->getHue().has_value());
    pdf::PDFAppParagraph* meta = page->getReleaseCard()->findChild<pdf::PDFAppParagraph*>(QStringLiteral("releaseMeta"));
    QCOMPARE(meta->text(), QStringLiteral("Released Sep 20, 2025 \u00B7 You are running 1.6.0"));

    // The update status is the same as on the running version
    QCOMPARE(int(page->getUpdateStatusCard()->getStatus().state), int(pdf::PDFUpdateStatus::State::UpToDate));
}

void AppContentTest::whatsNewVersionHistory()
{
    ContentFixture fixture;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    fixture.setUpdateValues(true, now - 3600000, now - 3600000, QStringLiteral("1.6.0.0"), QString());
    fixture.checker()->setRunningVersion(QVersionNumber({ 1, 6, 0, 0 }));

    fixture.navigate(QStringLiteral("/whats-new"));
    pdf::PDFAppWhatsNewPage* page = fixture.page<pdf::PDFAppWhatsNewPage>();
    pdf::PDFAppVersionHistory* history = page->getVersionHistory();
    QVERIFY(history);
    QCOMPARE(history->getRows().size(), pdf::PDFAppVersionHistory::COLLAPSED_VERSIONS);
    QCOMPARE(history->getViewAllLink()->text(), QStringLiteral("View all"));
    QVERIFY(!history->getRows().back()->isLineVisible());

    // A bundled unreleased version (newer than the running one) may precede the running version
    pdf::PDFVersionRow* currentRow = nullptr;
    for (pdf::PDFVersionRow* row : history->getRows())
    {
        if (history->getRowVersion(row) == QVersionNumber({ 1, 6, 0, 0 }))
        {
            currentRow = row;
        }
        else
        {
            QVERIFY(!row->isCurrent());
            QVERIFY(!row->isSelected());
        }
    }
    QVERIFY(currentRow);
    QVERIFY(currentRow->isCurrent());
    QVERIFY(currentRow->isSelected());
    QCOMPARE(currentRow->getVersion(), QStringLiteral("v1.6.0"));
    QCOMPARE(currentRow->getDate(), QStringLiteral("Jun 14, 2026"));

    // View all / Show less
    history->getViewAllLink()->click();
    QCOMPARE(history->getRows().size(), int(page->getCatalog().getReleases().size()));
    QCOMPARE(history->getViewAllLink()->text(), QStringLiteral("Show less"));
    QVERIFY(page->saveViewState().value(pdf::PDFAppWhatsNewPage::getAllVersionsKey()).toBool());

    // A version row switches the version; Back restores the expanded history and the scroll
    pdf::PDFVersionRow* older = history->getRows().at(5);
    const QVersionNumber olderVersion = history->getRowVersion(older);
    fixture.scrollArea()->verticalScrollBar()->setValue(120);
    const int scroll = fixture.scrollArea()->verticalScrollBar()->value();
    QTest::mouseClick(older, Qt::LeftButton);
    QCoreApplication::processEvents();
    page = fixture.page<pdf::PDFAppWhatsNewPage>();
    QVERIFY(page);
    QCOMPARE(page->getRelease().version, olderVersion);
    QVERIFY(page->getVersionHistory()->isAllShown() == false);

    // The shown older version is in the collapsed history, selected
    bool isSelectedShown = false;
    for (pdf::PDFVersionRow* row : page->getVersionHistory()->getRows())
    {
        isSelectedShown = isSelectedShown || (row->isSelected() && page->getVersionHistory()->getRowVersion(row) == olderVersion);
    }
    QVERIFY(isSelectedShown);

    QVERIFY(fixture.window->getNavigator()->goBack());
    QCoreApplication::processEvents();
    page = fixture.page<pdf::PDFAppWhatsNewPage>();
    QVERIFY(page->getVersionHistory()->isAllShown());
    QTRY_COMPARE(fixture.scrollArea()->verticalScrollBar()->value(), scroll);

    // The keyboard: Enter on a focused version row
    pdf::PDFVersionRow* row = page->getVersionHistory()->getRows().at(1);
    const QVersionNumber secondVersion = page->getCatalog().getReleases().at(1).version;
    row->setFocus(Qt::TabFocusReason);
    QTest::keyClick(row, Qt::Key_Return);
    QCoreApplication::processEvents();
    QCOMPARE(fixture.page<pdf::PDFAppWhatsNewPage>()->getRelease().version, secondVersion);
}

void AppContentTest::whatsNewSections()
{
    // A catalog of the test: a release with every section and one without the optional ones
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    auto write = [&directory](const QString& name, const QString& text)
    {
        QFile file(QDir(directory.path()).filePath(name));
        QVERIFY(file.open(QFile::WriteOnly));
        file.write(text.toUtf8());
    };
    write(QStringLiteral("1.6.0.0.md"), validWhatsNewFile());
    write(QStringLiteral("1.5.0.md"), QStringLiteral("---\nversion: 1.5.0\ndate: 2025-03-09\nsummary: Old.\n---\n## Overview\nOnly the overview.\n"));
    const pdf::PDFWhatsNewCatalog catalog = pdf::PDFWhatsNewCatalog::load(directory.path(), pdf::PDFWhatsNewCatalog::createDefaultOptions());
    QCOMPARE(catalog.getReleases().size(), size_t(2));

    ContentFixture fixture;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    fixture.setUpdateValues(true, now - 3600000, now - 3600000, QStringLiteral("1.6.0.0"), QString());
    fixture.checker()->setRunningVersion(QVersionNumber({ 1, 6, 0, 0 }));
    fixture.navigate(QStringLiteral("/whats-new"));
    pdf::PDFAppWhatsNewPage* page = fixture.page<pdf::PDFAppWhatsNewPage>();
    page->setCatalog(catalog);

    QVERIFY(page->getHighlightsSection()->isVisible());
    QCOMPARE(page->getHighlightCards().size(), 2);
    QCOMPARE(page->getHighlightCards().at(0)->getTitle(), QStringLiteral("One app for everything"));
    QCOMPARE(page->getHighlightCards().at(1)->getHue(), pdf::PDFAccentHue::Amber);
    QVERIFY(page->getNewToolsSection()->isVisible());
    QCOMPARE(page->getToolRows().size(), 2);
    QCOMPARE(page->getToolRows().at(0)->getName(), pdf::PDFDocumentToolCatalog::findTool(QStringLiteral("optimize.compress"))->getTitle());
    QVERIFY(page->getExploreToolsButton()->isVisible());
    QCOMPARE(page->getIssueRows().size(), 2);
    QCOMPARE(page->getIssueRows().at(0)->getNumber(), QStringLiteral("#430"));
    QCOMPARE(page->getIssueRows().at(0)->getDate(), QStringLiteral("Oct 12, 2026"));
    QCOMPARE(page->getIssueRows().at(1)->getDate(), QString());
    QVERIFY(page->getIssueScrollArea()->verticalScrollBar()->maximum() == 0);

    // Explore new tools scrolls to the section; a tool row opens the tool
    page->getExploreToolsButton()->click();
    QCoreApplication::processEvents();
    QVERIFY(fixture.scrollArea()->verticalScrollBar()->value() > 0);
    page->getToolRows().at(1)->click();
    QCoreApplication::processEvents();
    QCOMPARE(fixture.currentTab()->getHistory().getCurrentEntry()->route.toString(), QStringLiteral("/tools/pages.merge"));
    QVERIFY(fixture.window->getNavigator()->goBack());
    QCoreApplication::processEvents();

    // The release without the optional sections hides them
    fixture.navigate(QStringLiteral("/whats-new/1.5.0"));
    page = fixture.page<pdf::PDFAppWhatsNewPage>();
    page->setCatalog(catalog);
    QVERIFY(!page->getHighlightsSection()->isVisible());
    QVERIFY(!page->getNewToolsSection()->isVisible());
    QVERIFY(!page->getFixedIssuesSection()->isVisible());
    QVERIFY(!page->getExploreToolsButton()->isVisible());
    QVERIFY(page->getVersionHistory()->isVisible());
    QVERIFY(!page->getVersionHistory()->getViewAllLink()->isVisible());
}

void AppContentTest::whatsNewToolPins()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QFile file(QDir(directory.path()).filePath(QStringLiteral("1.6.0.0.md")));
    QVERIFY(file.open(QFile::WriteOnly));
    file.write(validWhatsNewFile().toUtf8());
    file.close();
    const pdf::PDFWhatsNewCatalog catalog = pdf::PDFWhatsNewCatalog::load(directory.path(), pdf::PDFWhatsNewCatalog::createDefaultOptions());

    ContentFixture fixture;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    fixture.setUpdateValues(true, now - 3600000, now - 3600000, QStringLiteral("1.6.0.0"), QString());
    fixture.checker()->setRunningVersion(QVersionNumber({ 1, 6, 0, 0 }));
    fixture.navigate(QStringLiteral("/whats-new"));
    pdf::PDFAppWhatsNewPage* page = fixture.page<pdf::PDFAppWhatsNewPage>();
    page->setCatalog(catalog);

    pdf::PDFToolRow* row = page->getToolRows().at(0);
    QVERIFY(!row->isPinned());

    // The star pins the tool in the database
    row->getStarButton()->click();
    pdf::PDFToolRepository repository(fixture.context->getDatabase());
    QVERIFY(repository.isPinned(QStringLiteral("optimize.compress")));
    QVERIFY(row->isPinned());

    // A change of another connection (thread) moves the star by the notifier
    std::thread thread([&fixture]()
    {
        pdf::PDFToolRepository threadRepository(fixture.context->getDatabase());
        (void)threadRepository.setPinned(QStringLiteral("optimize.compress"), false);
        (void)threadRepository.setPinned(QStringLiteral("pages.merge"), true);
    });
    thread.join();
    QTRY_VERIFY(!page->getToolRows().at(0)->isPinned());
    QTRY_VERIFY(page->getToolRows().at(1)->isPinned());
    QVERIFY(!repository.isPinned(QStringLiteral("optimize.compress")));
}

void AppContentTest::whatsNewUpdateCard_data()
{
    QTest::addColumn<bool>("autoCheck");
    QTest::addColumn<qint64>("lastCheckAge");
    QTest::addColumn<qint64>("lastSuccessAge");
    QTest::addColumn<QString>("latest");
    QTest::addColumn<QString>("error");
    QTest::addColumn<QString>("title");
    QTest::addColumn<QString>("icon");
    QTest::addColumn<QString>("badge");
    QTest::addColumn<bool>("isCheckEnabled");
    QTest::addColumn<QString>("checkText");
    QTest::addColumn<bool>("isDownloadVisible");
    QTest::addColumn<bool>("isSettingsLinkVisible");
    QTest::addColumn<bool>("isTimeVisible");

    const qint64 hour = 3600000;
    const qint64 never = -1;
    QTest::newRow("up to date") << true << hour << hour << "1.6.0.0" << "" << "You're up to date" << "check" << "Up to date" << true << "Check for updates" << false << false << true;
    QTest::newRow("checking elsewhere") << true << qint64(5000) << hour << "1.6.0.0" << "" << "Checking for updates\u2026" << "hourglass" << "" << false << "Check for updates" << false << false << true;
    QTest::newRow("checking never") << true << qint64(5000) << never << "" << "" << "Checking for updates\u2026" << "hourglass" << "" << false << "Check for updates" << false << false << false;
    QTest::newRow("update available") << true << hour << hour << "1.7.2.0" << "" << "PDF4QT 1.7.2 is available" << "download" << "Update available" << true << "Check for updates" << true << false << true;
    QTest::newRow("could not check") << true << hour << 2 * hour << "1.6.0.0" << "GitHub could not be reached. Check your connection." << "Could not check for updates" << "alert-triangle" << "" << true << "Try Again" << false << false << true;
    QTest::newRow("could not check never") << true << hour << never << "" << "Bad" << "Could not check for updates" << "alert-triangle" << "" << true << "Try Again" << false << false << true;
    QTest::newRow("auto off") << false << hour << hour << "1.6.0.0" << "" << "Automatic check is off" << "timer-off" << "" << true << "Check for updates" << false << true << false;
    QTest::newRow("auto off never") << false << never << never << "" << "" << "Automatic check is off" << "timer-off" << "" << true << "Check for updates" << false << true << false;
    QTest::newRow("not checked") << true << hour << never << "" << "" << "Not checked yet" << "rotate-ccw" << "" << true << "Check for updates" << false << false << false;
}

void AppContentTest::whatsNewUpdateCard()
{
    QFETCH(bool, autoCheck);
    QFETCH(qint64, lastCheckAge);
    QFETCH(qint64, lastSuccessAge);
    QFETCH(QString, latest);
    QFETCH(QString, error);
    QFETCH(QString, title);
    QFETCH(QString, icon);
    QFETCH(QString, badge);
    QFETCH(bool, isCheckEnabled);
    QFETCH(QString, checkText);
    QFETCH(bool, isDownloadVisible);
    QFETCH(bool, isSettingsLinkVisible);
    QFETCH(bool, isTimeVisible);

    ContentFixture fixture;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    fixture.checker()->setRunningVersion(QVersionNumber({ 1, 6, 0, 0 }));
    fixture.checker()->setClock([now]() { return now; });
    fixture.setUpdateValues(autoCheck, lastCheckAge >= 0 ? now - lastCheckAge : 0, lastSuccessAge >= 0 ? now - lastSuccessAge : 0, latest, error);

    fixture.navigate(QStringLiteral("/whats-new"));
    pdf::PDFAppWhatsNewPage* page = fixture.page<pdf::PDFAppWhatsNewPage>();
    QVERIFY(page);
    QCoreApplication::processEvents();
    QVERIFY(!fixture.checker()->isChecking());

    pdf::PDFAppUpdateStatusCard* card = page->getUpdateStatusCard();
    QCOMPARE(card->getTitle(), title);
    QCOMPARE(card->getIconCircle()->getIconName(), icon);
    QVERIFY(pdf::PDFThemeIcons::hasIcon(icon));
    QCOMPARE(card->getCheckButton()->isEnabled(), isCheckEnabled);
    QCOMPARE(card->getCheckButton()->text(), checkText);
    QCOMPARE(card->getDownloadButton()->isVisible(), isDownloadVisible);
    QCOMPARE(card->getSettingsLink()->isVisible(), isSettingsLinkVisible);
    QCOMPARE(!card->getTimeText().isEmpty(), isTimeVisible);
    QVERIFY(!card->getDetail().isEmpty());
    if (!error.isEmpty())
    {
        QCOMPARE(card->getDetail(), error);
    }
    QCOMPARE(page->getReleaseBadge()->isVisible() ? page->getReleaseBadge()->getText() : QString(), badge);

    // The buttons keep their place: the check button is always shown, full width
    QVERIFY(card->getCheckButton()->isVisible());
    QCOMPARE(card->getCheckButton()->width(), card->width() - 34);

    if (QByteArray(QTest::currentDataTag()) == "up to date")
    {
        // card 104:2144 (review R2-D2): the icon circle 40 at 17, the title and the detail
        // (34 px) centered by it, last-checked 13 px at 69, the button at 94, 147 high
        QCOMPARE(QRect(card->getIconCircle()->mapTo(card, QPoint(0, 0)), card->getIconCircle()->size()), QRect(17, 17, 40, 40));
        QLabel* titleLabel = nullptr;
        for (QLabel* label : card->findChildren<QLabel*>())
        {
            titleLabel = label->text() == title ? label : titleLabel;
        }
        QVERIFY(titleLabel);
        QVERIFY2(qAbs(titleLabel->mapTo(card, QPoint(0, 0)).y() - 20) <= 1, qPrintable(QString::number(titleLabel->mapTo(card, QPoint(0, 0)).y())));
        QCOMPARE(card->getCheckButton()->y(), 94);
        QCOMPARE(card->height(), 147);
    }

    QSignalSpy external(fixture.window->getNavigator(), &pdf::PDFAppNavigator::externalUrlOpened);
    if (isDownloadVisible)
    {
        card->getDownloadButton()->click();
        QCOMPARE(external.last().at(0).toUrl(), QUrl(QStringLiteral("https://github.com/JakubMelka/PDF4QT/releases/tag/v%1").arg(latest)));
    }
    if (isSettingsLinkVisible)
    {
        card->getSettingsLink()->click();
        QCoreApplication::processEvents();
        QCOMPARE(fixture.currentTab()->getHistory().getCurrentEntry()->route.toString(), QStringLiteral("/settings/general#check-for-updates"));
    }

    // A change of the stored values by another instance updates the card
    fixture.navigate(QStringLiteral("/whats-new"));
    page = fixture.page<pdf::PDFAppWhatsNewPage>();
    QVERIFY(!fixture.context->getSettings().setValue(pdf::PDFSettingKeys::updateLatestVersion(), QStringLiteral("3.0")).isError());
    QVERIFY(!fixture.context->getSettings().resetValue(pdf::PDFSettingKeys::updateLastError()).isError());
    QVERIFY(!fixture.context->getSettings().setValue(pdf::PDFSettingKeys::updateLastCheck(), now - 3600000).isError());
    QTRY_COMPARE(page->getUpdateStatusCard()->getTitle(), QStringLiteral("PDF4QT 3.0.0 is available"));
}

void AppContentTest::whatsNewCheckButton()
{
    TestReleaseServer server;
    QVERIFY(server.start());
    server.reply.body = TestReleaseServer::createRelease(QStringLiteral("v1.8.0.0"));
    server.reply.delay = 150;

    ContentFixture fixture;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    fixture.setUpdateValues(false, now - 3600000, now - 3600000, QStringLiteral("1.6.0.0"), QString());
    fixture.checker()->setRunningVersion(QVersionNumber({ 1, 6, 0, 0 }));
    fixture.checker()->setUrl(server.getUrl());

    fixture.navigate(QStringLiteral("/whats-new"));
    pdf::PDFAppWhatsNewPage* page = fixture.page<pdf::PDFAppWhatsNewPage>();
    QCOMPARE(page->getUpdateStatusCard()->getTitle(), QStringLiteral("Automatic check is off"));
    QVERIFY(server.requests.isEmpty());

    // Check for updates: Checking (the button disabled), then Update available
    page->getUpdateStatusCard()->getCheckButton()->click();
    QCOMPARE(page->getUpdateStatusCard()->getTitle(), QStringLiteral("Checking for updates\u2026"));
    QVERIFY(!page->getUpdateStatusCard()->getCheckButton()->isEnabled());
    QTRY_COMPARE(page->getUpdateStatusCard()->getTitle(), QStringLiteral("PDF4QT 1.8.0 is available"));
    QCOMPARE(page->getReleaseBadge()->getText(), QStringLiteral("Update available"));

    // A failure: Try Again
    server.reply.status = 500;
    page->getUpdateStatusCard()->getCheckButton()->click();
    QTRY_COMPARE(page->getUpdateStatusCard()->getTitle(), QStringLiteral("Could not check for updates"));
    QCOMPARE(page->getUpdateStatusCard()->getCheckButton()->text(), QStringLiteral("Try Again"));
    server.reply.status = 200;
    server.reply.body = TestReleaseServer::createRelease(QStringLiteral("v1.6.0.0"));
    QVERIFY(!fixture.context->getSettings().setValue(pdf::PDFSettingKeys::updateAutoCheck(), true).isError());
    page->getUpdateStatusCard()->getCheckButton()->click();
    QTRY_COMPARE(page->getUpdateStatusCard()->getTitle(), QStringLiteral("You're up to date"));

    // The page started a due check, when it was shown
    QVERIFY(!fixture.context->getSettings().setValue(pdf::PDFSettingKeys::updateLastCheck(), now - 48 * 3600000).isError());
    const qsizetype requests = server.requests.size();
    fixture.navigate(QStringLiteral("/whats-new/1.5.2"));
    QTRY_COMPARE(server.requests.size(), requests + 1);
    QTRY_VERIFY(!fixture.checker()->isChecking());
}

void AppContentTest::whatsNewDeletedDuringCheck()
{
    TestReleaseServer server;
    QVERIFY(server.start());
    server.reply.body = TestReleaseServer::createRelease(QStringLiteral("v1.6.0.0"));
    server.reply.delay = 300;

    // The page is left (deleted) while the check runs
    {
        ContentFixture fixture;
        fixture.checker()->setUrl(server.getUrl());
        fixture.navigate(QStringLiteral("/whats-new"));
        QTRY_VERIFY(fixture.checker()->isChecking());
        fixture.navigate(QStringLiteral("/about"));
        QTRY_VERIFY_WITH_TIMEOUT(!fixture.checker()->isChecking(), 5000);
    }

    // The window is deleted while the check runs, then the context
    {
        ContentFixture fixture;
        fixture.checker()->setUrl(server.getUrl());
        fixture.navigate(QStringLiteral("/whats-new"));
        QTRY_VERIFY(fixture.checker()->isChecking());
        delete fixture.window.data();
        QCoreApplication::processEvents();
        QVERIFY(fixture.checker()->isChecking());
        const int closed = server.closedConnections;
        fixture.context.reset();
        QTRY_VERIFY(server.closedConnections > closed);
        QCoreApplication::processEvents();
    }

    // A slot of the status deletes the page (a navigation)
    {
        ContentFixture fixture;
        fixture.checker()->setUrl(server.getUrl());
        fixture.navigate(QStringLiteral("/whats-new"));
        QPointer<pdf::PDFAppPage> page = fixture.currentPage();
        QObject::connect(fixture.checker(), &pdf::PDFUpdateChecker::statusChanged, fixture.window.data(), [&fixture]()
        {
            if (!fixture.checker()->isChecking())
            {
                fixture.navigate(QStringLiteral("/about"));
            }
        });
        QTRY_VERIFY_WITH_TIMEOUT(page.isNull(), 5000);
    }
}

void AppContentTest::releaseNotesPage()
{
    ContentFixture fixture;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    fixture.setUpdateValues(true, now - 3600000, now - 3600000, QStringLiteral("1.6.0.0"), QString());
    fixture.checker()->setRunningVersion(QVersionNumber({ 1, 6, 0, 0 }));

    fixture.navigate(QStringLiteral("/whats-new/1.6.0/notes"));
    pdf::PDFAppReleaseNotesPage* page = fixture.page<pdf::PDFAppReleaseNotesPage>();
    QVERIFY(page);
    QCOMPARE(page->getHeaderTitle(), QStringLiteral("PDF4QT 1.6.0 release notes"));
    QCOMPARE(page->getHeaderSubtitle(), QStringLiteral("Released Jun 14, 2026 \u00B7 This is the version you are running"));
    QCOMPARE(fixture.window->getHomeTab()->getNavItem(), pdf::PDFAppNavItem::WhatsNew);
    QVERIFY(page->getMarkdownView()->getPlainText().contains(QStringLiteral("Adding hyperlink to _internal_ object in PDF")));
    QVERIFY(page->getMarkdownView()->getLinkCount() > 10);

    // The links of the notes: issues on GitHub
    QSignalSpy external(fixture.window->getNavigator(), &pdf::PDFAppNavigator::externalUrlOpened);
    const QRect linkRect = page->getMarkdownView()->getLinkRects(0).first();
    QTest::mouseClick(page->getMarkdownView(), Qt::LeftButton, Qt::NoModifier, linkRect.center());
    QCOMPARE(external.size(), 1);
    QVERIFY(external.last().at(0).toUrl().toString().startsWith(QStringLiteral("https://github.com/JakubMelka/PDF4QT/issues/")));

    // Links of the side: Release on GitHub, Full changelog, Back to What's New
    QCOMPARE(page->getLinkRows().size(), 3);
    page->getLinkRows().at(0)->click();
    QCOMPARE(external.last().at(0).toUrl(), QUrl(QStringLiteral("https://github.com/JakubMelka/PDF4QT/releases/tag/v1.6.0.0")));
    page->getLinkRows().at(1)->click();
    QCOMPARE(external.last().at(0).toUrl(), pdf::PDFAppWhatsNewPage::getChangelogUrl());

    // A version of the history opens its notes
    pdf::PDFVersionRow* row = page->getVersionHistory()->getRows().at(2);
    const QVersionNumber version = page->getVersionHistory()->getRowVersion(row);
    row->click();
    QCoreApplication::processEvents();
    page = fixture.page<pdf::PDFAppReleaseNotesPage>();
    QVERIFY(page);
    QCOMPARE(page->getRelease().version, version);
    QVERIFY(page->getHeaderSubtitle().contains(QStringLiteral("You are running 1.6.0")));

    // The breadcrumbs of the header: What's New, the version, Release notes
    pdf::PDFAppBreadcrumb* breadcrumb = fixture.window->getHomeTab()->getHeader()->getBreadcrumb();
    QVERIFY(breadcrumb->isVisible());
    QCOMPARE(breadcrumb->getLinks().size(), 2);
    QCOMPARE(page->getHeaderBreadcrumbs().size(), 3);
    breadcrumb->getLinks().at(1)->click();
    QCoreApplication::processEvents();
    QCOMPARE(fixture.page<pdf::PDFAppWhatsNewPage>()->getRelease().version, version);
    QVERIFY(!fixture.window->getHomeTab()->getHeader()->getBreadcrumb()->isVisible());
    QVERIFY(fixture.window->getNavigator()->goBack());
    QCoreApplication::processEvents();
    page = fixture.page<pdf::PDFAppReleaseNotesPage>();
    page->getLinkRows().at(2)->click();
    QCoreApplication::processEvents();
    QVERIFY(fixture.page<pdf::PDFAppWhatsNewPage>());
}

void AppContentTest::contentPagesNoLeak_data()
{
    QTest::addColumn<QString>("route");
    QTest::addColumn<bool>("isDark");
    QTest::addColumn<bool>("isRightToLeft");
    QTest::addColumn<qreal>("scale");

    for (const QString route : { QStringLiteral("/whats-new"), QStringLiteral("/whats-new/1.5.3"), QStringLiteral("/whats-new/1.6.0/notes"), QStringLiteral("/help/ai-assistant") })
    {
        for (const bool isDark : { false, true })
        {
            QTest::addRow("%s %s", qPrintable(route), isDark ? "dark" : "light") << route << isDark << false << 1.0;
        }
        QTest::addRow("%s rtl 2x", qPrintable(route)) << route << false << true << 2.0;
        QTest::addRow("%s dark 1.5x", qPrintable(route)) << route << true << false << 1.5;
    }
}

void AppContentTest::contentPagesNoLeak()
{
    QFETCH(QString, route);
    QFETCH(bool, isDark);
    QFETCH(bool, isRightToLeft);
    QFETCH(qreal, scale);

    ContentFixture fixture;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    fixture.setUpdateValues(true, now - 3600000, now - 3600000, QStringLiteral("1.7.0.0"), QString());
    fixture.checker()->setRunningVersion(QVersionNumber({ 1, 6, 0, 0 }));
    pdf::PDFThemeManager::getInstance()->setScheme(isDark ? pdf::PDFThemeColors::Scheme::Dark : pdf::PDFThemeColors::Scheme::Light);
    fixture.window->setLayoutDirection(isRightToLeft ? Qt::RightToLeft : Qt::LeftToRight);
    fixture.window->getHomeTab()->getBanner()->hide();
    fixture.navigate(route);
    QVERIFY(!qobject_cast<pdf::PDFPageNotFoundPage*>(fixture.currentPage()));
    QCoreApplication::processEvents();

    // The whole page (also the part below the window) has no leak color
    QScrollArea* area = fixture.scrollArea();
    QWidget* content = area->widget();
    QVERIFY(content->height() > 400);
    QCOMPARE(countLeakPixels(render(content, scale)), 0);
    QCOMPARE(countLeakPixels(render(fixture.window.data(), scale)), 0);

    // Nothing is wider than the page
    QCOMPARE(area->horizontalScrollBar()->maximum(), 0);
}

void AppContentTest::contentPagesMinimalWindow()
{
    ContentFixture fixture;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    fixture.setUpdateValues(true, now - 3600000, now - 3600000, QStringLiteral("1.6.0.0"), QString());
    fixture.window->resize(720, 480);
    QCoreApplication::processEvents();

    for (const QString route : { QStringLiteral("/whats-new"), QStringLiteral("/whats-new/1.6.0/notes"), QStringLiteral("/help/getting-started") })
    {
        fixture.navigate(route);
        QCoreApplication::processEvents();
        QScrollArea* area = fixture.scrollArea();
        QCOMPARE(area->horizontalScrollBar()->maximum(), 0);

        // The side column is below the main column
        QWidget* content = area->widget();
        QVERIFY(content->width() < pdf::PDFAppColumns::CONTENT_MINIMUM_MAIN_WIDTH + pdf::PDFAppColumns::GAP + pdf::PDFAppColumns::SIDE_WIDTH);
        pdf::PDFAppColumns* columns = content->findChild<pdf::PDFAppColumns*>(QStringLiteral("columns"));
        QVERIFY(columns && columns->isStacked());
        for (QWidget* child : content->findChildren<QWidget*>())
        {
            if (child->isVisible() && child->parentWidget() && child->parentWidget()->isVisible())
            {
                const QRect rect(child->mapTo(content, QPoint(0, 0)), child->size());
                QVERIFY2(rect.right() <= content->width() + 1, qPrintable(child->objectName() + QStringLiteral(" ") + QString::fromLatin1(child->metaObject()->className())));
            }
        }
    }
}

void AppContentTest::writeTestCatalog(const QString& folder)
{
    // 1.6.0.0: one tool of every category and an unknown tool (the catalog is loaded
    // without the checks), 1.7.0 not released yet, 1.5.0 without release notes
    QStringList tools;
    for (const pdf::PDFDocumentToolCategoryInfo& info : pdf::PDFDocumentToolCatalog::getCategories())
    {
        const std::optional<pdf::PDFDocumentToolCategory> category = pdf::PDFDocumentToolCatalog::findCategory(info.getId());
        QVERIFY(category.has_value());
        tools << QStringLiteral("- ") + pdf::PDFDocumentToolCatalog::getTools(*category).front()->getId();
    }
    tools << QStringLiteral("- unknown.tool");

    const QString release = validWhatsNewFile().replace(QStringLiteral("- optimize.compress\n- pages.merge\n"), tools.join(QChar(u'\n')) + QStringLiteral("\n"));
    const QDir directory(folder);
    writeFile(directory.filePath(QStringLiteral("1.6.0.0.md")), release.toUtf8());
    writeFile(directory.filePath(QStringLiteral("1.7.0.md")), QByteArrayLiteral("---\nversion: 1.7.0\ndate: unreleased\nsummary: Next.\n---\n## Overview\nNot yet.\n"));
    writeFile(directory.filePath(QStringLiteral("1.5.0.md")), QByteArrayLiteral("---\nversion: 1.5.0\ndate: 2025-03-09\nsummary: Old.\n---\n## Overview\nOnly the overview.\n"));
}

void AppContentTest::whatsNewPageStandalone()
{
    // Pages without a route: nothing is built, nothing breaks
    ContentFixture fixture(false);
    fixture.checker()->setRunningVersion(QVersionNumber({ 1, 6, 0, 0 }));

    pdf::PDFAppWhatsNewPage page(fixture.context.get());
    QVERIFY(!page.getRelease().isValid());
    QCOMPARE(page.getHeaderSubtitle(), QStringLiteral("What changed in the latest PDF4QT release: new tools, improvements and fixed issues."));
    QVERIFY(!page.saveViewState().value(pdf::PDFAppWhatsNewPage::getAllVersionsKey()).toBool());
    page.restoreViewState({ { pdf::PDFAppWhatsNewPage::getAllVersionsKey(), true } });
    QVERIFY(!page.getVersionHistory());
    QEvent languageChange(QEvent::LanguageChange);
    QApplication::sendEvent(&page, &languageChange);
    QVERIFY(!page.getUpdateStatusCard());
    QVERIFY(page.getHighlightCards().isEmpty());
    QVERIFY(page.getToolRows().isEmpty());
    QVERIFY(page.getIssueRows().isEmpty());
    QVERIFY(page.getLinkRows().isEmpty());
    QVERIFY(page.openLink(QStringLiteral("#fixed-issues")));
    page.resize(800, 600);
    page.show();
    QVERIFY(QTest::qWaitForWindowExposed(&page));
    QVERIFY(!page.getUpdateStatusCard());

    // An empty catalog has no release to show
    page.setCatalog(pdf::PDFWhatsNewCatalog());
    QVERIFY(!pdf::PDFAppWhatsNewPage::findRunningRelease(pdf::PDFWhatsNewCatalog(), QVersionNumber(1, 6)));
    QVERIFY(!page.setRoute(pdf::PDFAppRoute::parse(QStringLiteral("/whats-new"))));
    QVERIFY(!page.getUpdateStatusCard());

    pdf::PDFAppReleaseNotesPage notes(fixture.context.get());
    QVERIFY(notes.getHeaderBreadcrumbs().isEmpty());
    QVERIFY(!notes.saveViewState().value(pdf::PDFAppWhatsNewPage::getAllVersionsKey()).toBool());
    notes.restoreViewState({ { pdf::PDFAppWhatsNewPage::getAllVersionsKey(), true } });
    notes.setCatalog(pdf::PDFWhatsNewCatalog::load());
    QApplication::sendEvent(&notes, &languageChange);
    QVERIFY(!notes.getMarkdownView());
    QVERIFY(notes.getLinkRows().isEmpty());
    QVERIFY(notes.openLink(QStringLiteral("#anchor")));

    // A page without a context and a page of a context without a database show the
    // release without the status and the pins
    pdf::PDFAppContext unopened(pdf::PDFAppCommandLine::parse({ QStringLiteral("PDF4QT"), QStringLiteral("--portable") }, QDir::currentPath()));
    for (pdf::PDFAppContext* context : { static_cast<pdf::PDFAppContext*>(nullptr), &unopened })
    {
        pdf::PDFAppWhatsNewPage pageWithoutDatabase(context);
        QVERIFY(pageWithoutDatabase.setRoute(pdf::PDFAppRoute::parse(QStringLiteral("/whats-new/1.5.2"))));
        pageWithoutDatabase.resize(1000, 800);
        pageWithoutDatabase.show();
        QVERIFY(QTest::qWaitForWindowExposed(&pageWithoutDatabase));
        QCOMPARE(pageWithoutDatabase.getRelease().version, QVersionNumber({ 1, 5, 2, 0 }));
        QVERIFY(pageWithoutDatabase.getUpdateStatusCard());
        QCOMPARE(pageWithoutDatabase.getUpdateStatusCard()->getTitle(), QStringLiteral("Not checked yet"));
    }

    // The version history: an unknown row, a change of the language keeps the texts
    pdf::PDFAppVersionHistory history;
    history.setReleases(pdf::PDFWhatsNewCatalog::load().getReleases(), QVersionNumber(1, 6), QVersionNumber(1, 6));
    QVERIFY(history.getRowVersion(nullptr).isNull());
    history.setAllShown(true);
    QApplication::sendEvent(&history, &languageChange);
    QCOMPARE(history.getViewAllLink()->text(), QStringLiteral("Show less"));
    history.setAllShown(false);
    QApplication::sendEvent(&history, &languageChange);
    QCOMPARE(history.getViewAllLink()->text(), QStringLiteral("View all"));

    // The update card: a change of the language keeps the texts
    pdf::PDFAppUpdateStatusCard card;
    card.setStatus(pdf::PDFUpdateStatus::create(true, false, 0, 0, QString(), QString(), QVersionNumber(1, 6), 0), 0);
    QCOMPARE(card.getTitle(), QStringLiteral("Not checked yet"));
    QApplication::sendEvent(&card, &languageChange);
    QCOMPARE(card.getTitle(), QStringLiteral("Not checked yet"));
    QCOMPARE(card.getDetail(), QStringLiteral("PDF4QT 1.6.0 has not looked for updates yet"));
}

void AppContentTest::whatsNewPageCatalog()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    writeTestCatalog(directory.path());
    const pdf::PDFWhatsNewCatalog catalog = pdf::PDFWhatsNewCatalog::load(directory.path(), pdf::PDFWhatsNewParser::Options());
    QVERIFY2(catalog.getErrors().isEmpty(), qPrintable(catalog.getErrors().join(QStringLiteral("; "))));
    QCOMPARE(catalog.getReleases().size(), size_t(3));

    ContentFixture fixture;
    fixture.window->resize(1440, 600);
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    fixture.setUpdateValues(true, now - 3600000, now - 3600000, QStringLiteral("1.6.0.0"), QString());
    fixture.checker()->setRunningVersion(QVersionNumber({ 1, 6, 0, 0 }));
    fixture.navigate(QStringLiteral("/whats-new"));
    pdf::PDFAppWhatsNewPage* page = fixture.page<pdf::PDFAppWhatsNewPage>();
    QVERIFY(page);
    page->setCatalog(catalog);
    QCoreApplication::processEvents();

    // The known tools have rows with the hues of their categories, the unknown one none
    QCOMPARE(page->getToolRows().size(), int(pdf::PDFDocumentToolCatalog::getCategories().size()));
    std::set<int> hues;
    for (pdf::PDFToolRow* row : page->getToolRows())
    {
        hues.insert(int(row->getHue()));
    }
    QCOMPARE(hues.size(), pdf::PDFDocumentToolCatalog::getCategories().size());

    // The unreleased version in the history
    bool isUnreleasedFound = false;
    for (pdf::PDFVersionRow* row : page->getVersionHistory()->getRows())
    {
        if (page->getVersionHistory()->getRowVersion(row) == QVersionNumber(1, 7, 0))
        {
            isUnreleasedFound = true;
            QCOMPARE(row->getDate(), QStringLiteral("Not released yet"));
        }
    }
    QVERIFY(isUnreleasedFound);

    // Anchors of the sections scroll the page, an unknown anchor does nothing
    QScrollBar* scrollBar = fixture.scrollArea()->verticalScrollBar();
    QVERIFY(scrollBar->maximum() > 0);
    scrollBar->setValue(0);
    QVERIFY(page->openLink(QStringLiteral("#fixed-issues")));
    QTRY_VERIFY(scrollBar->value() > 0);
    const int fixedIssuesScroll = scrollBar->value();
    scrollBar->setValue(0);
    QVERIFY(page->openLink(QStringLiteral("#highlights")));
    QTRY_VERIFY(scrollBar->value() > 0);
    QVERIFY(scrollBar->value() < fixedIssuesScroll);
    scrollBar->setValue(0);
    QVERIFY(page->openLink(QStringLiteral("#unknown")));
    QCoreApplication::processEvents();
    QCOMPARE(scrollBar->value(), 0);

    // An unknown change of the database reads the status again; a change of another
    // setting does not change it
    QVERIFY(!fixture.context->getSettings().setValue(pdf::PDFSettingKeys::updateLatestVersion(), QStringLiteral("2.0")).isError());
    Q_EMIT fixture.context->getNotifier()->everythingChanged();
    QCOMPARE(page->getUpdateStatusCard()->getTitle(), QStringLiteral("PDF4QT 2.0.0 is available"));
    QSignalSpy changes(fixture.context->getNotifier(), &pdf::PDFDatabaseNotifier::changed);
    QVERIFY(!fixture.context->getSettings().setValue(pdf::PDFSettingKeys::recentMaxFiles(), 20).isError());
    QTRY_VERIFY(std::any_of(changes.cbegin(), changes.cend(), [](const QList<QVariant>& change) { return change.at(1).toString() == pdf::PDFSettingKeys::recentMaxFiles(); }));
    QCOMPARE(page->getUpdateStatusCard()->getTitle(), QStringLiteral("PDF4QT 2.0.0 is available"));

    // A change of the language builds the page again and keeps the expanded history
    page->getVersionHistory()->setAllShown(true);
    QPointer<pdf::PDFAppUpdateStatusCard> oldCard = page->getUpdateStatusCard();
    QEvent languageChange(QEvent::LanguageChange);
    QApplication::sendEvent(page, &languageChange);
    QVERIFY(oldCard.isNull());
    QVERIFY(page->getVersionHistory()->isAllShown());
    QCOMPARE(page->getToolRows().size(), int(pdf::PDFDocumentToolCatalog::getCategories().size()));

    // The version not released yet (a route of the page itself)
    QVERIFY(page->setRoute(pdf::PDFAppRoute::parse(QStringLiteral("/whats-new/1.7.0"))));
    pdf::PDFAppParagraph* meta = page->getReleaseCard()->findChild<pdf::PDFAppParagraph*>(QStringLiteral("releaseMeta"));
    QVERIFY(meta);
    QCOMPARE(meta->text(), QStringLiteral("Not released yet · You are running 1.6.0"));

    // The release without new tools: its anchor scrolls nowhere
    QVERIFY(page->setRoute(pdf::PDFAppRoute::parse(QStringLiteral("/whats-new/1.5.0"))));
    QCoreApplication::processEvents();
    scrollBar->setValue(0);
    QVERIFY(page->openLink(QStringLiteral("#new-tools")));
    QCoreApplication::processEvents();
    QCOMPARE(scrollBar->value(), 0);
}

void AppContentTest::releaseNotesPageStates()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    writeTestCatalog(directory.path());
    const pdf::PDFWhatsNewCatalog catalog = pdf::PDFWhatsNewCatalog::load(directory.path(), pdf::PDFWhatsNewParser::Options());

    ContentFixture fixture;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    fixture.setUpdateValues(true, now - 3600000, now - 3600000, QStringLiteral("1.6.0.0"), QString());
    fixture.checker()->setRunningVersion(QVersionNumber({ 1, 6, 0, 0 }));
    fixture.navigate(QStringLiteral("/whats-new/1.6.0/notes"));
    pdf::PDFAppReleaseNotesPage* page = fixture.page<pdf::PDFAppReleaseNotesPage>();
    QVERIFY(page);

    // A new catalog shows the notes of the same version
    page->setCatalog(catalog);
    QCOMPARE(page->getMarkdownView()->getPlainText(), QStringLiteral("Free Markdown\nAnything here."));
    QVERIFY(page->getMarkdownView()->getAnchorPosition(QStringLiteral("free-markdown")) >= 0);
    QVERIFY(page->setRoute(pdf::PDFAppRoute::parse(QStringLiteral("/whats-new/1.6.0/notes#free-markdown"))));
    QCoreApplication::processEvents();
    QVERIFY(page->openLink(QStringLiteral("#unknown-anchor")));

    // A release without notes shows its overview; a version not released yet
    QVERIFY(page->setRoute(pdf::PDFAppRoute::parse(QStringLiteral("/whats-new/1.5.0/notes"))));
    QCOMPARE(page->getMarkdownView()->getPlainText(), QStringLiteral("Only the overview."));
    QCOMPARE(page->getHeaderSubtitle(), QStringLiteral("Released Mar 9, 2025 · You are running 1.6.0"));
    QVERIFY(page->setRoute(pdf::PDFAppRoute::parse(QStringLiteral("/whats-new/1.7.0/notes"))));
    QCOMPARE(page->getHeaderSubtitle(), QStringLiteral("Not released yet · You are running 1.6.0"));

    // The expanded history is saved, kept by a new build (language) and restored
    page->getVersionHistory()->setAllShown(true);
    QVERIFY(page->saveViewState().value(pdf::PDFAppWhatsNewPage::getAllVersionsKey()).toBool());
    QPointer<pdf::PDFAppMarkdownView> oldView = page->getMarkdownView();
    QEvent languageChange(QEvent::LanguageChange);
    QApplication::sendEvent(page, &languageChange);
    QVERIFY(oldView.isNull());
    QVERIFY(page->getVersionHistory()->isAllShown());
    page->restoreViewState({ { pdf::PDFAppWhatsNewPage::getAllVersionsKey(), false } });
    QVERIFY(!page->getVersionHistory()->isAllShown());
}

// -------------------------------------------------------------------- Manual

void AppContentTest::helpCatalog()
{
    const pdf::PDFAppHelpCatalog catalog = pdf::PDFAppHelpCatalog::load();
    QVERIFY2(catalog.getErrors().isEmpty(), qPrintable(catalog.getErrors().join(QStringLiteral("\n"))));
    QVERIFY(catalog.getTopics().size() >= 10);

    // Topics are sorted by the categories and their order; every category has a topic
    int previousCategory = -1;
    int previousOrder = std::numeric_limits<int>::min();
    std::set<QString> ids;
    for (const pdf::PDFAppHelpTopic& topic : catalog.getTopics())
    {
        QVERIFY(ids.insert(topic.id).second);
        QVERIFY(!topic.title.isEmpty());
        QVERIFY(!topic.summary.isEmpty());
        QVERIFY(!topic.headings.empty());
        QVERIFY(pdf::PDFAppHelpCatalog::findCategory(topic.categoryId));
        int category = 0;
        for (const pdf::PDFAppHelpCategory& item : pdf::PDFAppHelpCatalog::getCategories())
        {
            if (item.id == topic.categoryId)
            {
                break;
            }
            ++category;
        }
        QVERIFY(category >= previousCategory);
        if (category == previousCategory)
        {
            QVERIFY(topic.order > previousOrder);
        }
        previousCategory = category;
        previousOrder = topic.order;

        // American English: no British spellings
        const QString text = (topic.title + topic.summary + topic.markdown).toLower();
        for (const QString& british : { QStringLiteral("colour"), QStringLiteral("behaviour"), QStringLiteral("cancelled"), QStringLiteral("centre"), QStringLiteral("licence"), QStringLiteral("grey") })
        {
            QVERIFY2(!text.contains(british), qPrintable(topic.id + QStringLiteral(": ") + british));
        }
    }
    for (const pdf::PDFAppHelpCategory& category : pdf::PDFAppHelpCatalog::getCategories())
    {
        QVERIFY2(!catalog.getCategoryTopics(category.id).empty(), category.title);
        QVERIFY(!category.getTitle().isEmpty());
    }

    // The topics, which other pages link (Home, AI, What's New)
    for (const QString& id : { QStringLiteral("getting-started"), QStringLiteral("ai-assistant"), QStringLiteral("whats-new-and-updates"), QStringLiteral("portable-mode") })
    {
        QVERIFY2(catalog.findTopic(id), qPrintable(id));
    }
    QVERIFY(!catalog.findTopic(QStringLiteral("unknown")));
    QCOMPARE(catalog.getNextTopic(QStringLiteral("getting-started"))->id, QStringLiteral("window-and-tabs"));
    QVERIFY(!catalog.getNextTopic(QStringLiteral("unknown")));
    const std::vector<const pdf::PDFAppHelpTopic*> lastCategory = catalog.getCategoryTopics(pdf::PDFAppHelpCatalog::getCategories().back().id);
    QVERIFY(!catalog.getNextTopic(lastCategory.back()->id));
    QCOMPARE(pdf::PDFAppHelpCatalog::getRoute(QStringLiteral("settings"), QStringLiteral("find-a-setting")), QStringLiteral("/help/settings#find-a-setting"));

    // Search: titles before headings before keywords
    const std::vector<pdf::PDFAppHelpMatch> portable = catalog.search(QStringLiteral("portable"));
    QVERIFY(!portable.empty());
    QCOMPARE(portable.front().topic->id, QStringLiteral("portable-mode"));
    QCOMPARE(portable.front().score, 100);
    const std::vector<pdf::PDFAppHelpMatch> heading = catalog.search(QStringLiteral("restore the last session"));
    QVERIFY(!heading.empty());
    QCOMPARE(heading.front().anchor, QStringLiteral("restore-the-last-session"));
    QCOMPARE(heading.front().score, 80);
    QVERIFY(!catalog.search(QStringLiteral("usb")).empty());
    QVERIFY(catalog.search(QString()).empty());
    QVERIFY(catalog.search(QStringLiteral("xyzzy-nothing")).empty());
    (void)catalog.search(QString(100000, QLatin1Char('a')));
}

void AppContentTest::helpCatalogErrors_data()
{
    QTest::addColumn<QString>("id");
    QTest::addColumn<QString>("text");
    QTest::addColumn<QString>("error");

    const QString valid = QStringLiteral("---\ntitle: T\ncategory: settings\norder: 1\nsummary: S\n---\nBody.\n");
    QTest::newRow("valid") << "topic" << valid << "";
    QTest::newRow("no header") << "topic" << "Body." << "header";
    QTest::newRow("not closed") << "topic" << "---\ntitle: T\n" << "not closed";
    QTest::newRow("unknown key") << "topic" << QString(valid).replace(QStringLiteral("order: 1"), QStringLiteral("author: x")) << "unknown key";
    QTest::newRow("duplicate key") << "topic" << QString(valid).replace(QStringLiteral("order: 1"), QStringLiteral("title: U")) << "twice";
    QTest::newRow("unknown category") << "topic" << QString(valid).replace(QStringLiteral("category: settings"), QStringLiteral("category: tools")) << "unknown category";
    QTest::newRow("bad order") << "topic" << QString(valid).replace(QStringLiteral("order: 1"), QStringLiteral("order: x")) << "order";
    QTest::newRow("related not route") << "topic" << QString(valid).replace(QStringLiteral("order: 1"), QStringLiteral("related: https://evil.com")) << "not a route";
    QTest::newRow("no title") << "topic" << QString(valid).replace(QStringLiteral("title: T"), QStringLiteral("title:")) << "no title";
    QTest::newRow("no body") << "topic" << QString(valid).replace(QStringLiteral("Body.\n"), QString()) << "no text";
    QTest::newRow("bad id") << "../x" << valid << "invalid id";
    QTest::newRow("too large") << "topic" << valid + QString(pdf::PDFAppHelpCatalog::MAXIMUM_FILE_SIZE, QLatin1Char('a')) << "too large";
}

void AppContentTest::helpCatalogErrors()
{
    QFETCH(QString, id);
    QFETCH(QString, text);
    QFETCH(QString, error);

    pdf::PDFAppHelpTopic topic;
    QStringList errors;
    const bool isValid = pdf::PDFAppHelpCatalog::parseTopic(id, text, &topic, &errors);
    QCOMPARE(isValid, error.isEmpty());
    if (!error.isEmpty())
    {
        QVERIFY2(errors.join(QStringLiteral("; ")).contains(error), qPrintable(errors.join(QStringLiteral("; "))));
    }
}

void AppContentTest::helpLinks()
{
    // Every link of every topic leads to a known route, topic and anchor (or to https)
    const pdf::PDFAppHelpCatalog catalog = pdf::PDFAppHelpCatalog::load();
    const pdf::PDFAppRouteTable table = pdf::PDFAppRouteTable::createDefault();
    const pdf::PDFSettingRegistry* registry = nullptr;
    ContentFixture fixture(false);
    registry = fixture.context->getSettingRegistry();

    int routeLinks = 0;
    for (const pdf::PDFAppHelpTopic& topic : catalog.getTopics())
    {
        const pdf::PDFAppMarkdownDocument document = pdf::PDFAppMarkdownParser::parse(topic.markdown);
        QStringList links = document.getLinks();
        links << topic.related;

        // The text has no link, which was refused (a typo would be a plain text)
        QVERIFY2(!topic.markdown.contains(QStringLiteral("](http:")), qPrintable(topic.id));

        for (const QString& link : links)
        {
            const QString context = topic.id + QStringLiteral(": ") + link;
            switch (pdf::PDFAppMarkdownParser::classifyLink(link))
            {
                case pdf::PDFAppMarkdownLinkKind::Anchor:
                    QVERIFY2(document.hasAnchor(link.mid(1)), qPrintable(context));
                    break;

                case pdf::PDFAppMarkdownLinkKind::External:
                    QVERIFY2(link.startsWith(QStringLiteral("https://")), qPrintable(context));
                    break;

                case pdf::PDFAppMarkdownLinkKind::Route:
                {
                    ++routeLinks;
                    const pdf::PDFAppRoute route = pdf::PDFAppRoute::parse(link);
                    QVERIFY2(route.isValid(), qPrintable(context));
                    const pdf::PDFAppRouteMatch match = table.match(route);
                    QVERIFY2(match.result == pdf::PDFAppRouteMatch::Result::NotImplemented || match.result == pdf::PDFAppRouteMatch::Result::Found ||
                             match.result == pdf::PDFAppRouteMatch::Result::Redirect, qPrintable(context));
                    if (route.getSegment(0) == QStringLiteral("help"))
                    {
                        const pdf::PDFAppHelpTopic* target = catalog.findTopic(route.getSegment(1));
                        QVERIFY2(target, qPrintable(context));
                        if (!route.getFragment().isEmpty())
                        {
                            QVERIFY2(pdf::PDFAppMarkdownParser::parse(target->markdown).hasAnchor(route.getFragment()), qPrintable(context));
                        }
                    }
                    if (route.getSegment(0) == QStringLiteral("settings") && !route.getFragment().isEmpty())
                    {
                        // The anchor of a setting row exists
                        bool isKnown = false;
                        for (const pdf::PDFSettingDefinition* definition : registry->getSectionDefinitions(route.getSegment(1)))
                        {
                            isKnown = isKnown || definition->getAnchor() == route.getFragment();
                        }
                        QVERIFY2(isKnown, qPrintable(context));
                    }
                    break;
                }

                case pdf::PDFAppMarkdownLinkKind::None:
                    QFAIL(qPrintable(QStringLiteral("Refused link ") + context));
                    break;
            }
        }
    }
    QVERIFY(routeLinks > 20);

    // The shortcuts of the topic Keyboard shortcuts are the defaults of the command registry
    const pdf::PDFAppHelpTopic* shortcuts = catalog.findTopic(QStringLiteral("keyboard-shortcuts"));
    QVERIFY(shortcuts);
    for (const pdf::PDFAppCommand* command : fixture.context->getCommandRegistry()->getCommands())
    {
        if (!command->defaultShortcut.isEmpty())
        {
            const QString text = command->defaultShortcut.toString(QKeySequence::PortableText);
            QVERIFY2(shortcuts->markdown.contains(QStringLiteral("| ") + text + QStringLiteral(" |")), qPrintable(text));
        }
    }
}

void AppContentTest::helpTranslation()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    auto write = [&directory](const QString& name, const QString& text)
    {
        QFile file(QDir(directory.path()).filePath(name));
        QVERIFY(file.open(QFile::WriteOnly));
        file.write(text.toUtf8());
    };
    write(QStringLiteral("alpha.md"), QStringLiteral("---\ntitle: Alpha\ncategory: settings\norder: 2\nsummary: S\n---\n## English\n"));
    write(QStringLiteral("alpha.pt.md"), QStringLiteral("---\ntitle: Alfa\ncategory: getting-started\norder: 9\nsummary: S\n---\n## Portugu\u00EAs\n"));
    write(QStringLiteral("alpha.cs.md"), QStringLiteral("broken"));
    write(QStringLiteral("beta.md"), QStringLiteral("---\ntitle: Beta\ncategory: settings\norder: 1\nsummary: S\n---\nText.\n"));
    write(QStringLiteral("broken.md"), QStringLiteral("---\ntitle: X\n"));

    const pdf::PDFAppHelpCatalog catalog = pdf::PDFAppHelpCatalog::load(directory.path());
    QCOMPARE(catalog.getTopics().size(), size_t(2));
    QCOMPARE(catalog.getTopics().front().id, QStringLiteral("beta"));
    QVERIFY(!catalog.getErrors().isEmpty());
    for (const QString& error : catalog.getErrors())
    {
        QVERIFY2(error.startsWith(QStringLiteral("broken.md:")), qPrintable(error));
    }
    QCOMPARE(catalog.getLocalizedTopic(QStringLiteral("alpha"), QStringLiteral("pt_BR")).title, QStringLiteral("Alfa"));
    QCOMPARE(catalog.getLocalizedTopic(QStringLiteral("alpha"), QStringLiteral("pt_BR")).categoryId, QStringLiteral("settings"));
    QCOMPARE(catalog.getLocalizedTopic(QStringLiteral("alpha"), QStringLiteral("cs")).title, QStringLiteral("Alpha"));
    QVERIFY(catalog.getLocalizedTopic(QStringLiteral("gamma"), QStringLiteral("cs")).id.isEmpty());

    // Every bundled translation of the manual is a valid topic with the same headings
    const pdf::PDFAppHelpCatalog bundled = pdf::PDFAppHelpCatalog::load();
    const QDir resources(pdf::PDFAppHelpCatalog::getResourceDirectory());
    for (const QString& fileName : resources.entryList(QStringList() << QStringLiteral("*.*.md"), QDir::Files))
    {
        const QStringList parts = fileName.split(QChar(u'.'));
        QCOMPARE(parts.size(), 3);
        const pdf::PDFAppHelpTopic translated = bundled.getLocalizedTopic(parts[0], parts[1]);
        QCOMPARE(translated.language, parts[1]);
        QCOMPARE(translated.headings.size(), bundled.findTopic(parts[0])->headings.size());
    }

    // The resources are the files of the source folder, all of them in help.qrc
    const QString qrc = readSourceFile(QStringLiteral("Pdf4QtLibApp/resources/help.qrc"));
    const QDir sourceFolder(QDir(QStringLiteral(PDF4QT_SOURCE_DIRECTORY)).filePath(QStringLiteral("Pdf4QtLibApp/resources/help")));
    for (const QString& fileName : sourceFolder.entryList(QStringList() << QStringLiteral("*.md"), QDir::Files))
    {
        QVERIFY2(qrc.contains(QStringLiteral("help/") + fileName), qPrintable(fileName));
    }
    QCOMPARE(sourceFolder.entryList(QStringList() << QStringLiteral("*.md"), QDir::Files).size(), resources.entryList(QStringList() << QStringLiteral("*.md"), QDir::Files).size());
}

void AppContentTest::helpPage()
{
    ContentFixture fixture;
    fixture.navigate(QStringLiteral("/help/getting-started"));
    pdf::PDFAppHelpPage* page = fixture.page<pdf::PDFAppHelpPage>();
    QVERIFY(page);
    QCOMPARE(page->getTopic().id, QStringLiteral("getting-started"));
    QCOMPARE(page->getHeaderTitle(), QStringLiteral("Getting started"));
    QCOMPARE(page->getHeaderSubtitle(), QStringLiteral("User manual \u00B7 Getting started \u00B7 for PDF4QT 1.6.0"));
    // The manual is in the Help tab (owner decision O13): its header has the title
    QCOMPARE(fixture.currentTab()->getKind(), pdf::PDFAppTabKind::Help);
    QCOMPARE(fixture.currentTab(), fixture.window->findHelpTab());
    QCOMPARE(static_cast<pdf::PDFAppWorkspaceTab*>(fixture.currentTab())->getHeader()->getTitleLabel()->text(), QStringLiteral("Getting started"));
    QVERIFY(fixture.currentTab()->getSearchField());
    QVERIFY(page->getMarkdownView()->getLinkCount() > 3);

    // The contents: the categories, the category of the topic expanded, the topic current
    pdf::PDFAppHelpContentsView* contents = page->getContentsView();
    QAbstractItemModel* model = contents->model();
    QCOMPARE(model->rowCount(), int(pdf::PDFAppHelpCatalog::getCategories().size()));
    QVERIFY(contents->isExpanded(model->index(0, 0)));
    QVERIFY(!contents->isExpanded(model->index(1, 0)));
    QCOMPARE(contents->currentIndex().data(pdf::PDFAppHelpPage::TopicRole).toString(), QStringLiteral("getting-started"));
    QCOMPARE(contents->height(), contents->getContentHeight());
    QCOMPARE(contents->verticalScrollBar()->maximum(), 0);

    // help-contents 955:154306: the rows (28 px) lie 9 px inside of the card (border 1 +
    // padding 8) and they are 322 px wide (review R2-D2)
    QWidget* contentsCard = contents->parentWidget();
    const QRect firstRow = contents->visualRect(model->index(0, 0));
    QCOMPARE(firstRow.height(), 28);
    QCOMPARE(contents->viewport()->mapTo(contentsCard, QPoint(0, firstRow.top())), QPoint(9, 9));
    QCOMPARE(contents->viewport()->width(), contentsCard->width() - 18);

    // A click on a category expands it (the tree grows)
    const int height = contents->getContentHeight();
    QTest::mouseClick(contents->viewport(), Qt::LeftButton, Qt::NoModifier, contents->visualRect(model->index(1, 0)).center());
    QVERIFY(contents->isExpanded(model->index(1, 0)));
    QVERIFY(contents->getContentHeight() > height);

    // The breadcrumbs of the header: Help, the category, the topic
    pdf::PDFAppWorkspaceTab* helpTab = static_cast<pdf::PDFAppWorkspaceTab*>(fixture.currentTab());
    QCOMPARE(helpTab->getHeader()->getBreadcrumb()->getLinks().size(), 2);
    QVERIFY(helpTab->getHeader()->getBreadcrumb()->isVisible());

    // The Help tab (pdf4qt-help 955:152873): no sidebar, no footer, the header and
    // the page at most 1166 px wide and centered, the topic 802 px, the side 340 px
    QVERIFY(!helpTab->getSidebar());
    QVERIFY(!helpTab->getFooter());
    pdf::PDFAppWorkspaceHeader* header = helpTab->getHeader();
    const QMargins headerMargins = header->contentsMargins();
    QCOMPARE(header->width() - headerMargins.left() - headerMargins.right(), 1166);
    QCOMPARE(page->width(), 1166);
    QCOMPARE(page->getMarkdownView()->width(), 802);
    QCOMPARE(page->getContentsView()->parentWidget()->width(), 340);
    QCOMPARE(page->getMarkdownView()->mapTo(helpTab, QPoint(0, 0)).x(), header->mapTo(helpTab, QPoint(headerMargins.left(), 0)).x());
    QCOMPARE(header->getSearchField()->mapTo(helpTab, QPoint(header->getSearchField()->width(), 0)).x(), page->mapTo(helpTab, QPoint(1166, 0)).x());

    // Related: the routes of the topic and the next topic of the category
    const QList<pdf::PDFLinkRow*> related = page->getRelatedRows();
    QVERIFY(related.size() >= 2);
    QCOMPARE(pdf::PDFAppHelpPage::getRelatedTarget(related.back()), QStringLiteral("/help/window-and-tabs"));
    QCOMPARE(related.back()->getSubtitle(), QStringLiteral("Next topic of Getting started"));

    // Unknown topic and an invalid one: Page Not Found
    fixture.navigate(QStringLiteral("/help/unknown-topic"));
    QVERIFY(qobject_cast<pdf::PDFPageNotFoundPage*>(fixture.currentPage()));
    fixture.navigate(QStringLiteral("/help/Bad_Topic!"));
    QVERIFY(qobject_cast<pdf::PDFPageNotFoundPage*>(fixture.currentPage()));

    // Every topic opens
    const pdf::PDFAppHelpCatalog allTopics = pdf::PDFAppHelpCatalog::load();
    for (const pdf::PDFAppHelpTopic& topic : allTopics.getTopics())
    {
        fixture.navigate(pdf::PDFAppHelpCatalog::getRoute(topic.id));
        QVERIFY2(fixture.page<pdf::PDFAppHelpPage>(), qPrintable(topic.id));
        QCOMPARE(fixture.page<pdf::PDFAppHelpPage>()->getTitle(), topic.title);
    }
}

void AppContentTest::helpPageAnchor()
{
    ContentFixture fixture;
    fixture.window->resize(1440, 600);
    QCoreApplication::processEvents();

    // /help/{topic}#anchor scrolls the heading to the top of the page
    fixture.navigate(QStringLiteral("/help/recent-files#clear-the-history"));
    pdf::PDFAppHelpPage* page = fixture.page<pdf::PDFAppHelpPage>();
    QVERIFY(page);
    QScrollArea* area = fixture.scrollArea();
    const int anchor = page->getMarkdownView()->getAnchorPosition(QStringLiteral("clear-the-history"));
    QVERIFY(anchor > 0);
    QTRY_VERIFY(area->verticalScrollBar()->value() > 0);
    const int expected = page->getMarkdownView()->mapTo(area->widget(), QPoint(0, anchor)).y();
    QCOMPARE(area->verticalScrollBar()->value(), qMin(expected, area->verticalScrollBar()->maximum()));

    // An unknown anchor: the topic from its top
    fixture.navigate(QStringLiteral("/help/recent-files#unknown"));
    QVERIFY(fixture.page<pdf::PDFAppHelpPage>());
    QTest::qWait(50);
    QCOMPARE(area->verticalScrollBar()->value(), 0);

    // An anchor link inside of the topic scrolls
    fixture.navigate(QStringLiteral("/help/search"));
    page = fixture.page<pdf::PDFAppHelpPage>();
    QVERIFY(page);
    QVERIFY(page->openLink(QStringLiteral("#use-the-search")));
    QVERIFY(area->verticalScrollBar()->value() > 0 || area->verticalScrollBar()->maximum() == 0);

    // Back restores the scroll of the previous page
    area->verticalScrollBar()->setValue(37);
    fixture.navigate(QStringLiteral("/help/settings"));
    QVERIFY(fixture.window->getNavigator()->goBack());
    QCoreApplication::processEvents();
    QTRY_COMPARE(area->verticalScrollBar()->value(), qMin(37, area->verticalScrollBar()->maximum()));
}

void AppContentTest::helpPageNavigation()
{
    ContentFixture fixture;
    fixture.navigate(QStringLiteral("/help/getting-started"));
    pdf::PDFAppHelpPage* page = fixture.page<pdf::PDFAppHelpPage>();

    // A route link of the topic (the Markdown view) navigates
    pdf::PDFAppMarkdownView* view = page->getMarkdownView();
    int settingsLink = -1;
    for (int i = 0; i < view->getLinkCount(); ++i)
    {
        if (view->getLinkTarget(i) == QStringLiteral("/help/open-documents"))
        {
            settingsLink = i;
            break;
        }
    }
    QVERIFY(settingsLink >= 0);
    QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier, view->getLinkRects(settingsLink).first().center());
    QCoreApplication::processEvents();
    QCOMPARE(fixture.page<pdf::PDFAppHelpPage>()->getTopic().id, QStringLiteral("open-documents"));

    // A topic of the contents navigates
    page = fixture.page<pdf::PDFAppHelpPage>();
    pdf::PDFAppHelpContentsView* contents = page->getContentsView();
    const QModelIndex category = contents->model()->index(0, 0);
    contents->expand(category);
    QCoreApplication::processEvents();
    const QModelIndex topic = contents->model()->index(1, 0, category);
    const QString topicId = topic.data(pdf::PDFAppHelpPage::TopicRole).toString();
    QTest::mouseClick(contents->viewport(), Qt::LeftButton, Qt::NoModifier, contents->visualRect(topic).center());
    QCoreApplication::processEvents();
    QCOMPARE(fixture.page<pdf::PDFAppHelpPage>()->getTopic().id, topicId);

    // The keyboard: Enter on a topic of the contents
    page = fixture.page<pdf::PDFAppHelpPage>();
    contents = page->getContentsView();
    const QModelIndex next = contents->model()->index(2, 0, contents->model()->index(0, 0));
    contents->setFocus(Qt::TabFocusReason);
    contents->setCurrentIndex(next);
    QTest::keyClick(contents, Qt::Key_Return);
    QCoreApplication::processEvents();
    QCOMPARE(fixture.page<pdf::PDFAppHelpPage>()->getTopic().id, next.data(pdf::PDFAppHelpPage::TopicRole).toString());

    // Related and the breadcrumb navigate; a https link opens the system
    page = fixture.page<pdf::PDFAppHelpPage>();
    page->getRelatedRows().front()->click();
    QCoreApplication::processEvents();
    QVERIFY(fixture.window->getNavigator()->goBack());
    QCoreApplication::processEvents();
    page = fixture.page<pdf::PDFAppHelpPage>();
    static_cast<pdf::PDFAppWorkspaceTab*>(fixture.currentTab())->getHeader()->getBreadcrumb()->getLinks().front()->click();
    QCoreApplication::processEvents();
    QCOMPARE(fixture.page<pdf::PDFAppHelpPage>()->getTopic().id, QStringLiteral("getting-started"));

    QSignalSpy external(fixture.window->getNavigator(), &pdf::PDFAppNavigator::externalUrlOpened);
    QVERIFY(fixture.page<pdf::PDFAppHelpPage>()->openLink(QStringLiteral("https://example.com/x")));
    QCOMPARE(external.size(), 1);
    QVERIFY(fixture.page<pdf::PDFAppHelpPage>()->openLink(QStringLiteral("javascript:alert(1)")));
    QVERIFY(fixture.page<pdf::PDFAppHelpPage>()->openLink(QStringLiteral("file:///c:/x")));
    QCOMPARE(external.size(), 1);

    // A link of the view deletes its page by the navigation (no crash)
    QPointer<pdf::PDFAppHelpPage> oldPage = fixture.page<pdf::PDFAppHelpPage>();
    // (openLink tells, whether the page still lives after the navigation)
    const bool isAlive = oldPage->openLink(QStringLiteral("/help/settings"));
    QCOMPARE(isAlive, !oldPage.isNull());
    QCoreApplication::processEvents();
    QTRY_VERIFY(oldPage.isNull());

    // A link to another route goes to the Home tab, the Help tab keeps its topic
    pdf::PDFAppTab* helpTab = fixture.window->findHelpTab();
    QVERIFY(fixture.page<pdf::PDFAppHelpPage>()->openLink(QStringLiteral("/whats-new")));
    QCoreApplication::processEvents();
    QCOMPARE(fixture.currentTab(), static_cast<pdf::PDFAppTab*>(fixture.window->getHomeTab()));
    QVERIFY(qobject_cast<pdf::PDFAppHelpPage*>(helpTab->getPage()));
}

void AppContentTest::helpCatalogLimits()
{
    // A file larger than the limit; topics of one category with the same order are
    // sorted by their identifiers; equal search scores by their texts
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QDir folder(directory.path());
    writeFile(folder.filePath(QStringLiteral("huge.md")), QByteArray(pdf::PDFAppHelpCatalog::MAXIMUM_FILE_SIZE + 1, 'x'));
    writeFile(folder.filePath(QStringLiteral("first.md")), QByteArrayLiteral("---\ntitle: First\ncategory: settings\norder: 1\nsummary: S\n---\nBody.\n"));
    writeFile(folder.filePath(QStringLiteral("beta.md")), QByteArrayLiteral("---\ntitle: Alpha two\ncategory: settings\norder: 2\nsummary: S\n---\nBody.\n"));
    writeFile(folder.filePath(QStringLiteral("alpha.md")), QByteArrayLiteral("---\ntitle: Alpha one\ncategory: settings\norder: 2\nsummary: S\n---\nBody.\n"));

    const pdf::PDFAppHelpCatalog catalog = pdf::PDFAppHelpCatalog::load(directory.path());
    QStringList ids;
    for (const pdf::PDFAppHelpTopic& topic : catalog.getTopics())
    {
        ids << topic.id;
    }
    QCOMPARE(ids, (QStringList{ QStringLiteral("first"), QStringLiteral("alpha"), QStringLiteral("beta") }));
    QVERIFY(!catalog.getErrors().isEmpty());
    for (const QString& error : catalog.getErrors())
    {
        QVERIFY2(error.startsWith(QStringLiteral("huge.md:")), qPrintable(error));
    }

    const std::vector<pdf::PDFAppHelpMatch> matches = catalog.search(QStringLiteral("alpha"));
    QCOMPARE(matches.size(), size_t(2));
    QCOMPARE(matches[0].score, matches[1].score);
    QCOMPARE(matches[0].topic->id, QStringLiteral("alpha"));
    QCOMPARE(matches[1].topic->id, QStringLiteral("beta"));

    // A byte order mark, a header line without a colon, no output arguments
    const QString valid = QStringLiteral("---\ntitle: T\ncategory: settings\norder: 1\nsummary: S\n---\nBody.\n");
    pdf::PDFAppHelpTopic topic;
    QVERIFY(pdf::PDFAppHelpCatalog::parseTopic(QStringLiteral("x"), QChar(0xFEFF) + valid, &topic, nullptr));
    QCOMPARE(topic.title, QStringLiteral("T"));
    QVERIFY(pdf::PDFAppHelpCatalog::parseTopic(QStringLiteral("x"), valid, nullptr, nullptr));
    QVERIFY(!pdf::PDFAppHelpCatalog::parseTopic(QStringLiteral("x"), QStringLiteral("garbage"), nullptr, nullptr));
    QStringList errors;
    QVERIFY(!pdf::PDFAppHelpCatalog::parseTopic(QStringLiteral("x"), QString(valid).replace(QStringLiteral("order: 1"), QStringLiteral("no colon")), nullptr, &errors));
    QVERIFY2(errors.join(QStringLiteral("; ")).contains(QStringLiteral("unknown key")), qPrintable(errors.join(QStringLiteral("; "))));
}

void AppContentTest::helpPageCatalog()
{
    // A catalog of the test: related routes of every kind, two categories only
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QDir folder(directory.path());
    writeFile(folder.filePath(QStringLiteral("getting-started.md")), QByteArrayLiteral(
        "---\ntitle: Alpha one\ncategory: settings\norder: 1\nsummary: S\nrelated: /tools, /help/x/y, /help/../x, /help/missing, /help/gamma\n---\n## Usage\n\nText.\n"));
    writeFile(folder.filePath(QStringLiteral("zeta.md")), QByteArrayLiteral("---\ntitle: Alpha two\ncategory: settings\norder: 1\nsummary: S\n---\nText.\n"));
    writeFile(folder.filePath(QStringLiteral("gamma.md")), QByteArrayLiteral("---\ntitle: Gamma\ncategory: getting-started\norder: 5\nsummary: S\n---\nText.\n"));
    const pdf::PDFAppHelpCatalog catalog = pdf::PDFAppHelpCatalog::load(directory.path());
    QVERIFY2(catalog.getErrors().isEmpty(), qPrintable(catalog.getErrors().join(QStringLiteral("; "))));

    ContentFixture fixture;
    fixture.navigate(QStringLiteral("/help/getting-started"));
    pdf::PDFAppHelpPage* page = fixture.page<pdf::PDFAppHelpPage>();
    QVERIFY(page);
    page->setCatalog(catalog);
    QCOMPARE(page->getTopic().title, QStringLiteral("Alpha one"));
    QCOMPARE(page->getContentsView()->model()->rowCount(), 2);

    // Related: valid routes and existing topics, the next topic of the category last
    QStringList targets;
    QStringList subtitles;
    for (pdf::PDFLinkRow* row : page->getRelatedRows())
    {
        targets << pdf::PDFAppHelpPage::getRelatedTarget(row);
        subtitles << row->getSubtitle();
    }
    QCOMPARE(targets, (QStringList{ QStringLiteral("/tools"), QStringLiteral("/help/x/y"), QStringLiteral("/help/gamma"), QStringLiteral("/help/zeta") }));
    QCOMPARE(subtitles, (QStringList{ QStringLiteral("Open the page"), QStringLiteral("Open the page"), QStringLiteral("Topic of the user manual"), QStringLiteral("Next topic of Settings") }));
    QCOMPARE(page->getRelatedRows().at(0)->getIconName(), QStringLiteral("tools"));
    QCOMPARE(page->getRelatedRows().at(1)->getTitle(), QStringLiteral("/help/x/y"));
    QCOMPARE(pdf::PDFAppHelpPage::getRelatedTarget(nullptr), QString());

    // Breadcrumbs: Help = the first topic of the catalog, the category = its first topic
    const QList<pdf::PDFAppBreadcrumbItem> breadcrumbs = page->getHeaderBreadcrumbs();
    QCOMPARE(breadcrumbs.size(), 3);
    QCOMPARE(breadcrumbs.at(0).route.toString(), QStringLiteral("/help/gamma"));
    QCOMPARE(breadcrumbs.at(1).route.toString(), QStringLiteral("/help/getting-started"));

    // A change of the language builds the page again
    QPointer<pdf::PDFAppMarkdownView> oldView = page->getMarkdownView();
    QEvent languageChange(QEvent::LanguageChange);
    QApplication::sendEvent(page, &languageChange);
    QVERIFY(oldView.isNull());
    QVERIFY(page->getMarkdownView());
    QVERIFY(page->openLink(QStringLiteral("#usage")));

    // A page without a route
    pdf::PDFAppHelpPage standalone(fixture.context.get());
    QVERIFY(standalone.getHeaderBreadcrumbs().isEmpty());
    QVERIFY(standalone.openLink(QStringLiteral("#usage")));
    standalone.setCatalog(catalog);
    QApplication::sendEvent(&standalone, &languageChange);
    QVERIFY(!standalone.getMarkdownView());
    standalone.setCatalog(pdf::PDFAppHelpCatalog());
    QVERIFY(standalone.getHeaderBreadcrumbs().isEmpty());
    QVERIFY(!standalone.setRoute(pdf::PDFAppRoute::parse(QStringLiteral("/help/getting-started"))));
}

void AppContentTest::helpPageContentsClicks()
{
    SingleClickStyle singleClickStyle;
    ContentFixture fixture;
    fixture.navigate(QStringLiteral("/help/getting-started"));
    pdf::PDFAppHelpPage* page = fixture.page<pdf::PDFAppHelpPage>();
    QVERIFY(page);
    pdf::PDFAppHelpContentsView* contents = page->getContentsView();
    QAbstractItemModel* model = contents->model();
    QSignalSpy navigated(fixture.window->getNavigator(), &pdf::PDFAppNavigator::navigated);
    const QModelIndex currentTopic = model->index(0, 0, model->index(0, 0));
    QCOMPARE(currentTopic.data(pdf::PDFAppHelpPage::TopicRole).toString(), QStringLiteral("getting-started"));

    // A click into the indentation of a category, beside its chevron, does nothing
    const QModelIndex category = model->index(1, 0);
    const QRect categoryRect = contents->visualRect(category);
    QVERIFY(categoryRect.left() > 4);
    const bool isExpanded = contents->isExpanded(category);
    QTest::mouseClick(contents->viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(1, categoryRect.center().y()));
    QCOMPARE(contents->isExpanded(category), isExpanded);

    // A click on the current topic does not navigate
    QTest::mouseClick(contents->viewport(), Qt::LeftButton, Qt::NoModifier, contents->visualRect(currentTopic).center());
    QCoreApplication::processEvents();
    QVERIFY(navigated.isEmpty());

    // Enter on two topics before the old page is gone: only the first one navigates
    const QModelIndex next = model->index(1, 0, model->index(0, 0));
    const QModelIndex other = model->index(2, 0, model->index(0, 0));
    const QString nextId = next.data(pdf::PDFAppHelpPage::TopicRole).toString();
    QVERIFY(other.isValid());
    contents->setFocus(Qt::TabFocusReason);
    contents->setCurrentIndex(next);
    QPointer<pdf::PDFAppHelpContentsView> guard(contents);
    QTest::keyClick(contents, Qt::Key_Return);
    QVERIFY(guard);
    contents->setCurrentIndex(other);
    QTest::keyClick(contents, Qt::Key_Return);
    QCoreApplication::processEvents();
    QCOMPARE(navigated.size(), 1);
    QCOMPARE(fixture.page<pdf::PDFAppHelpPage>()->getTopic().id, nextId);

    // A style, which activates an item by a single click: one navigation by activated
    page = fixture.page<pdf::PDFAppHelpPage>();
    contents = page->getContentsView();
    contents->setStyle(&singleClickStyle);
    const QModelIndex first = contents->model()->index(0, 0, contents->model()->index(0, 0));
    const QString firstId = first.data(pdf::PDFAppHelpPage::TopicRole).toString();
    QVERIFY(firstId != nextId);
    QTest::mouseClick(contents->viewport(), Qt::LeftButton, Qt::NoModifier, contents->visualRect(first).center());
    QCoreApplication::processEvents();
    QCOMPARE(navigated.size(), 2);
    QCOMPARE(fixture.page<pdf::PDFAppHelpPage>()->getTopic().id, firstId);
}

void AppContentTest::screenshots()
{
    // PDF4QT_APP_SCREENSHOTS=<folder>: pictures of the pages in both themes at 1x and 2x
    const QString folder = qEnvironmentVariable("PDF4QT_APP_SCREENSHOTS");
    ContentFixture fixture;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    fixture.setUpdateValues(true, now - 3600000, now - 3600000, QStringLiteral("1.6.0.0"), QString());
    fixture.checker()->setRunningVersion(QVersionNumber({ 1, 6, 0, 0 }));
    fixture.window->getHomeTab()->getBanner()->hide();

    struct Shot
    {
        const char* name;
        const char* route;
    };
    const Shot shots[] = { { "whatsnew", "/whats-new" }, { "whatsnew-previous", "/whats-new/1.5.2" }, { "whatsnew-notes", "/whats-new/1.6.0/notes" },
                           { "help", "/help/ai-assistant" }, { "help-tables", "/help/keyboard-shortcuts" } };
    for (const Shot& shot : shots)
    {
        fixture.navigate(QString::fromLatin1(shot.route));
        for (const pdf::PDFThemeColors::Scheme scheme : { pdf::PDFThemeColors::Scheme::Light, pdf::PDFThemeColors::Scheme::Dark })
        {
            pdf::PDFThemeManager::getInstance()->setScheme(scheme);
            QCoreApplication::processEvents();
            for (const qreal scale : { 1.0, 2.0 })
            {
                const QImage image = render(fixture.window.data(), scale);
                QVERIFY(!image.isNull());
                if (!folder.isEmpty())
                {
                    QDir().mkpath(folder);
                    const QString name = QStringLiteral("%1-%2-%3x.png").arg(QString::fromLatin1(shot.name), scheme == pdf::PDFThemeColors::Scheme::Light ? QStringLiteral("light") : QStringLiteral("dark")).arg(int(scale));
                    QVERIFY(image.save(QDir(folder).filePath(name)));

                    // The whole page (it is higher than the window)
                    const QImage page = render(fixture.scrollArea()->widget(), scale);
                    QVERIFY(page.save(QDir(folder).filePath(QStringLiteral("page-") + name)));
                }
            }
        }
    }

    // The states of the card Update status (989:14257) and all versions (988:13824)
    struct CardShot
    {
        const char* name;
        bool autoCheck;
        qint64 lastCheckAge;
        qint64 lastSuccessAge;
        const char* latest;
        const char* error;
    };
    const CardShot cardShots[] = { { "update-uptodate", true, 3600000, 3600000, "1.6.0.0", "" },
                                   { "update-checking", true, 5000, 3600000, "1.6.0.0", "" },
                                   { "update-available", true, 3600000, 3600000, "1.7.2.0", "" },
                                   { "update-error", true, 3600000, 7200000, "1.6.0.0", "GitHub could not be reached. Check your connection." },
                                   { "update-off", false, 3600000, 3600000, "1.6.0.0", "" } };
    fixture.checker()->setClock([now]() { return now; });
    for (const CardShot& shot : cardShots)
    {
        fixture.setUpdateValues(shot.autoCheck, now - shot.lastCheckAge, now - shot.lastSuccessAge, QString::fromLatin1(shot.latest), QString::fromLatin1(shot.error));
        fixture.navigate(QStringLiteral("/whats-new"));
        pdf::PDFAppWhatsNewPage* page = fixture.page<pdf::PDFAppWhatsNewPage>();
        QVERIFY(page);
        if (QByteArray(shot.name) == "update-available")
        {
            page->getVersionHistory()->getViewAllLink()->click();
        }
        QCoreApplication::processEvents();
        for (const pdf::PDFThemeColors::Scheme scheme : { pdf::PDFThemeColors::Scheme::Light, pdf::PDFThemeColors::Scheme::Dark })
        {
            pdf::PDFThemeManager::getInstance()->setScheme(scheme);
            QCoreApplication::processEvents();
            for (const qreal scale : { 1.0, 2.0 })
            {
                const QImage image = render(page->getUpdateStatusCard(), scale);
                QVERIFY(!image.isNull());
                if (!folder.isEmpty())
                {
                    const QString name = QStringLiteral("%1-%2-%3x.png").arg(QString::fromLatin1(shot.name), scheme == pdf::PDFThemeColors::Scheme::Light ? QStringLiteral("light") : QStringLiteral("dark")).arg(int(scale));
                    QVERIFY(image.save(QDir(folder).filePath(name)));
                    if (QByteArray(shot.name) == "update-available")
                    {
                        QVERIFY(render(fixture.scrollArea()->widget(), scale).save(QDir(folder).filePath(QStringLiteral("page-whatsnew-all-") + name)));
                    }
                }
            }
        }
    }
    pdf::PDFThemeManager::getInstance()->setScheme(pdf::PDFThemeColors::Scheme::Light);
}

QTEST_MAIN(AppContentTest)
#include "tst_appcontenttest.moc"
