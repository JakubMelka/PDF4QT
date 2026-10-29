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

#include "appmcpfixture.h"
#include "fakemcphost.h"
#include "mcptestclient.h"

#include "pdfappaipage.h"
#include "pdfapplocale.h"
#include "pdfappmainwindow.h"
#include "pdfappmcpactivity.h"
#include "pdfappmcpconfirmationui.h"
#include "pdfappmcpconfirmdialog.h"
#include "pdfappmcpcontroller.h"
#include "pdfappmcphost.h"
#include "pdfappnavigator.h"
#include "pdfapppagelayout.h"
#include "pdfapproute.h"
#include "pdfappsettingcontrol.h"
#include "pdfappsettingrow.h"
#include "pdfdatabasenotifier.h"
#include "pdfmcpserver.h"
#include "pdfsettingdefinitions.h"
#include "pdfsettingsrepository.h"

#include "pdfcomponentassistants.h"
#include "pdfcomponentbadges.h"
#include "pdfstylepainter.h"
#include "pdfthemefonts.h"
#include "pdfthememanager.h"
#include "pdfwidgetstyle.h"

#include <QAccessible>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QDir>
#include <QJsonDocument>
#include <QPainter>
#include <QScrollArea>
#include <QScrollBar>
#include <QSpinBox>
#include <QTcpServer>
#include <QTranslator>
#include <QtMath>
#include <QtTest>

// Package E of step 7 (the AI page, the controller user interface, more
// instances; plan 9 E): the states of the page against the Figma map, the rows
// of the settings mcp.* (the segmented control of the permissions), the
// connected assistants and the recent activity of the database, the setup
// cards, other instances by the row mcp_server, the dialog of a new token, and
// the common cases of a page (deletion, themes, right-to-left, the minimal
// window, keyboard, language change).
class AppAiTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanup();
    void smoke();
    void segmentedBinder();
    void pageStates();
    void figmaGeometry();
    void pageStateOtherInstance();
    void pageStatePortInUseForeign();
    void pageStatePortInUsePdf4Qt();
    void settingsRowsApply();
    void remoteForcesToken();
    void defaultLocalWithoutToken();
    void reviewConnectionChangedWhileStarting();
    void newTokenDialog();
    void assistantsList();
    void assistantsFromServer();
    void reviewStaleClientRowsRemoved();
    void assistantsOtherInstance();
    void activityList();
    void activityShowReopensDialog();
    void activityHostileRows();
    void setupCards();
    void setupCardReservedHeight_data();
    void setupCardReservedHeight();
    void setupCardReservedHeightRecomputed();
    void setupNoteCentered_data();
    void setupNoteCentered();
    void startWithApp();
    void heartbeatStale();
    void showOtherWindow();
    void anchors();
    void deletionMatrix();
    void deleteFromOwnSignals();
    void themesAndScales_data();
    void themesAndScales();
    void rightToLeft();
    void minimalWindow();
    void keyboardAndAccessibility();
    void languageChange();
};

/// A translator of the test: every text of the page AI prefixed by "~"
class AppAiTestTranslator : public QTranslator
{
public:
    virtual QString translate(const char* context, const char* sourceText, const char* disambiguation, int n) const override
    {
        Q_UNUSED(disambiguation);
        Q_UNUSED(n);
        const QByteArray contextName(context ? context : "");
        if (contextName == "pdf::PDFAppAiPage" && sourceText)
        {
            return QStringLiteral("~") + QString::fromUtf8(sourceText);
        }
        return QString();
    }

    virtual bool isEmpty() const override { return false; }
};

/// Helpers of the tests of the page AI
class AppAiTestHelper
{
public:
    AppAiTestHelper() = delete;

    static pdf::PDFAppAiPage* open(pdftest::AppMcpFixture& fixture, const QString& route = QStringLiteral("/ai"))
    {
        fixture.window->getNavigator()->navigate(pdf::PDFAppRoute::parse(route));
        pdftest::AppDocumentFixture::settle();
        for (pdf::PDFAppAiPage* page : fixture.window->findChildren<pdf::PDFAppAiPage*>())
        {
            if (page->isVisible())
            {
                return page;
            }
        }
        return nullptr;
    }

    static pdf::PDFSettingsRepository settings(pdftest::AppMcpFixture& fixture)
    {
        return fixture.context->getSettings();
    }

    static pdf::PDFMcpRepository repository(pdftest::AppMcpFixture& fixture)
    {
        return pdf::PDFMcpRepository(fixture.context->getDatabase());
    }

    static QStringList badgeTexts(pdf::PDFAppAiPage* page)
    {
        QStringList texts;
        for (pdf::PDFStatusBadge* badge : page->getStatusBadges())
        {
            texts << badge->getText();
        }
        return texts;
    }

    static QImage render(QWidget* widget, qreal devicePixelRatio)
    {
        QImage image(QSize(qCeil(widget->width() * devicePixelRatio), qCeil(widget->height() * devicePixelRatio)), QImage::Format_ARGB32_Premultiplied);
        image.setDevicePixelRatio(devicePixelRatio);
        image.fill(pdf::PDFStylePainter::color(pdf::PDFThemeColors::BgCanvas));
        widget->render(&image, QPoint(), QRegion(), widget->isWindow() ? QWidget::RenderFlags(QWidget::DrawChildren | QWidget::DrawWindowBackground) : QWidget::RenderFlags(QWidget::DrawChildren));
        return image;
    }

    /// Class names of the widgets of the page at the first leak pixels (the message of a failure)
    static QString describeLeak(QWidget* page, const QImage& image)
    {
        const QRgb leak = pdf::PDFThemeColors::getLeakColor().rgb() & 0xFFFFFF;
        const QImage converted = image.convertToFormat(QImage::Format_RGB32);
        QStringList names;
        for (int y = 0; y < converted.height() && names.size() < 5; y += 4)
        {
            const QRgb* line = reinterpret_cast<const QRgb*>(converted.constScanLine(y));
            for (int x = 0; x < converted.width(); x += 4)
            {
                if ((line[x] & 0xFFFFFF) == leak)
                {
                    const QPoint point(qFloor(x / image.devicePixelRatio()), qFloor(y / image.devicePixelRatio()));
                    QWidget* child = page->childAt(point);
                    const QString name = child ? QStringLiteral("%1 %2 (%3,%4)").arg(QString::fromLatin1(child->metaObject()->className()), child->objectName()).arg(point.x()).arg(point.y()) : QStringLiteral("page");
                    if (!names.contains(name))
                    {
                        names << name;
                    }
                    break;
                }
            }
        }
        return names.join(QStringLiteral("; "));
    }

    static int countLeakPixels(const QImage& image)
    {
        const QRgb leak = pdf::PDFThemeColors::getLeakColor().rgb() & 0xFFFFFF;
        const QImage converted = image.convertToFormat(QImage::Format_RGB32);
        int count = 0;
        for (int y = 0; y < converted.height(); ++y)
        {
            const QRgb* line = reinterpret_cast<const QRgb*>(converted.constScanLine(y));
            for (int x = 0; x < converted.width(); ++x)
            {
                count += (line[x] & 0xFFFFFF) == leak ? 1 : 0;
            }
        }
        return count;
    }

    static void screenshot(QWidget* window, const QString& name)
    {
        const QString folder = qEnvironmentVariable("PDF4QT_APP_SCREENSHOTS");
        if (folder.isEmpty() || !window)
        {
            return;
        }
        QDir().mkpath(folder);
        pdftest::AppDocumentFixture::settle();

        // The window with the dialogs (windows of their own) drawn over it at their places
        for (qreal ratio : { 1.0, 2.0 })
        {
            QImage image = render(window, ratio);
            QPainter painter(&image);
            for (QWidget* widget : QApplication::topLevelWidgets())
            {
                if (widget != window && widget->isVisible() && (widget->windowType() == Qt::Popup || widget->windowType() == Qt::Dialog))
                {
                    widget->render(&painter, window->mapFromGlobal(widget->mapToGlobal(QPoint(0, 0))), QRegion(), QWidget::DrawChildren);
                }
            }
            painter.end();
            image.save(QDir(folder).absoluteFilePath(name + (ratio > 1.0 ? QStringLiteral("-2x.png") : QStringLiteral(".png"))));
        }

        // The whole page (the part below the window: Connect an assistant)
        for (pdf::PDFAppAiPage* page : window->findChildren<pdf::PDFAppAiPage*>())
        {
            if (page->isVisible())
            {
                render(page, 1.0).save(QDir(folder).absoluteFilePath(name + QStringLiteral("-page.png")));
            }
        }
    }

    /// Claims the row mcp_server for a fake instance of this process (another PDF4QT window)
    static bool claimOther(pdftest::AppMcpFixture& fixture, const QString& instance, qint64 pid, qint64 time = 0)
    {
        pdf::PDFMcpRepository repository = AppAiTestHelper::repository(fixture);
        repository.setCurrentTime(time);
        bool claimed = false;
        repository.claimServer(instance, pid, 8765, pdf::PDFMcpServerRowState::Running, QStringLiteral("http://127.0.0.1:8765/mcp"), &claimed);
        return claimed;
    }

    static pdf::PDFMcpClientRow client(const QString& key, const QString& name, const QString& address = QString())
    {
        pdf::PDFMcpClientRow row;
        row.key = key;
        row.name = name;
        row.address = address;
        row.connected = QDateTime(QDate(2026, 10, 27), QTime(14, 2)).toMSecsSinceEpoch();
        return row;
    }

    static pdf::PDFMcpActivityRow activity(const QString& action, pdf::PDFMcpActivityStatus status, const QJsonObject& detail = QJsonObject())
    {
        pdf::PDFMcpActivityRow row;
        row.time = QDateTime(QDate(2026, 10, 27), QTime(16, 44)).toMSecsSinceEpoch();
        row.client = QStringLiteral("Claude Code");
        row.action = action;
        row.document = QStringLiteral("Project_Report.pdf");
        row.detail = detail;
        row.status = status;
        return row;
    }

    static int timeout(int milliseconds)
    {
        // PDF4QT_TEST_TIMEOUT_FACTOR, 20 under valgrind (the probe of the fixture is
        // multiplied by the same factor)
        return milliseconds * pdftest::AppDocumentFixture::getTimeoutFactor();
    }
};

void AppAiTest::initTestCase()
{
    QApplication::setStyle(new pdf::PDFWidgetStyle());
    qRegisterMetaType<pdf::PDFAppRoute>();
}

void AppAiTest::cleanup()
{
    pdftest::AppDocumentFixture::settle();
}

void AppAiTest::smoke()
{
    pdftest::AppMcpFixture fixture;
    pdf::PDFAppAiPage* page = AppAiTestHelper::open(fixture);
    QVERIFY(page);
    QCOMPARE(page->getHeaderTitle(), QStringLiteral("AI"));
    QVERIFY(fixture.controller());
    QCOMPARE(fixture.controller()->getState(), pdf::PDFAppMcpServerState::Stopped);
    QCOMPARE(page->getShownState(), pdf::PDFAppAiPage::ShownState::Stopped);
    QCOMPARE(page->getStateText(), QStringLiteral("The MCP server is stopped."));
}

void AppAiTest::segmentedBinder()
{
    pdftest::AppMcpFixture fixture;
    pdf::PDFAppAiPage* page = AppAiTestHelper::open(fixture);
    QVERIFY(page);

    // permission-mode 955:143435: 240 x 32, three segments Allow / Ask / Deny
    pdf::PDFAppSettingRow* row = page->findSettingRow(QStringLiteral("edit-documents"));
    QVERIFY(row && row->getControl());
    QCOMPARE(row->getControl()->getKind(), pdf::PDFAppSettingControl::Kind::Segmented);
    pdf::PDFSegmentedControl* segmented = row->getControl()->getSegmentedControl();
    QVERIFY(segmented);
    QCOMPARE(segmented->size(), QSize(240, 32));
    QCOMPARE(segmented->getCount(), 3);
    QCOMPARE(segmented->getCurrentIndex(), 1);
    QCOMPARE(row->getControl()->getShownValue().toString(), pdf::PDFSettingValues::permissionAsk());
    QCOMPARE(row->getDescription(), QStringLiteral("Annotations, form fields, page operations and PDF4QT tools"));
    QCOMPARE(row->getControlWrapWidth(), 320);

    // A click writes the setting from the event loop, the host gets a new catalog
    const quint64 generation = fixture.host()->getCatalogGeneration();
    QList<QAbstractButton*> buttons = segmented->findChildren<QAbstractButton*>();
    QCOMPARE(buttons.size(), 3);
    QTest::mouseClick(buttons[2], Qt::LeftButton);
    QTRY_COMPARE(AppAiTestHelper::settings(fixture).getString(pdf::PDFSettingKeys::mcpPermissionEdit()), pdf::PDFSettingValues::permissionDeny());
    QTRY_VERIFY(fixture.host()->getCatalogGeneration() > generation);

    // Another connection: the control follows without writing back
    QVERIFY(!AppAiTestHelper::settings(fixture).setValue(pdf::PDFSettingKeys::mcpPermissionEdit(), pdf::PDFSettingValues::permissionAllow()).isError());
    QTRY_COMPARE(segmented->getCurrentIndex(), 0);

    // A hostile value is refused by the repository and the control keeps the stored one
    QVERIFY(AppAiTestHelper::settings(fixture).setValue(pdf::PDFSettingKeys::mcpPermissionEdit(), QStringLiteral("everything")).isError());
    pdftest::AppDocumentFixture::settle();
    QCOMPARE(segmented->getCurrentIndex(), 0);

    // The kind of a definition: only an option with the hint is segmented
    pdf::PDFSettingDefinition definition = pdf::PDFSettingDefinition::createOption(QStringLiteral("test.option"), QStringLiteral("a"),
                                                                                     std::vector<pdf::PDFSettingOption>{ { QStringLiteral("a"), QStringLiteral("A"), false } });
    QCOMPARE(pdf::PDFAppSettingControl::getKind(definition), pdf::PDFAppSettingControl::Kind::ComboBox);
    definition.setControl(pdf::PDFSettingDefinition::Control::Segmented);
    QCOMPARE(pdf::PDFAppSettingControl::getKind(definition), pdf::PDFAppSettingControl::Kind::Segmented);
}

void AppAiTest::pageStates()
{
    pdftest::AppMcpFixture fixture;
    pdf::PDFAppAiPage* page = AppAiTestHelper::open(fixture);
    QVERIFY(page);

    // Stopped 653:5814: plain card, Stopped, Start Server Primary, Copy Command Secondary
    QCOMPARE(page->getServerCard()->getHue(), std::nullopt);
    QCOMPARE(AppAiTestHelper::badgeTexts(page), QStringList({ QStringLiteral("Stopped") }));
    QVERIFY(page->getStatusBadges().front()->isDotVisible());
    QCOMPARE(page->getStartStopButton()->text(), QStringLiteral("Start Server"));
    QVERIFY(page->getStartStopButton()->property("primary").toBool());
    QVERIFY(!page->getCopySetupButton()->property("primary").toBool());
    QVERIFY(!page->getCallout()->isVisibleTo(page));
    QVERIFY(!page->getAddressField()->isEnabled());
    QVERIFY(page->getAssistantsEmptyState()->isVisibleTo(page));
    QCOMPARE(page->getAssistantsEmptyState()->getText(), QStringLiteral("Start the server to accept connections."));
    QVERIFY(!page->getDisconnectAllLink()->isVisibleTo(page));
    QVERIFY(!page->getDisconnectHint()->isVisibleTo(page));
    QCOMPARE(page->getNote()->getHue(), std::optional<pdf::PDFAccentHue>(pdf::PDFAccentHue::Green));
    AppAiTestHelper::screenshot(fixture.window, QStringLiteral("ai-stopped"));

    // Running 653:5231: blue card, Running, Stop Server Secondary, Copy Command Primary
    QVERIFY(fixture.startServer());
    QTRY_COMPARE(page->getShownState(), pdf::PDFAppAiPage::ShownState::Running);
    QCOMPARE(page->getServerCard()->getHue(), std::optional<pdf::PDFAccentHue>(pdf::PDFAccentHue::Blue));
    QCOMPARE(AppAiTestHelper::badgeTexts(page), QStringList({ QStringLiteral("Running") }));
    QCOMPARE(page->getStartStopButton()->text(), QStringLiteral("Stop Server"));
    QVERIFY(!page->getStartStopButton()->property("primary").toBool());
    QVERIFY(page->getCopySetupButton()->property("primary").toBool());
    QCOMPARE(page->getAddressField()->text(), QStringLiteral("http://127.0.0.1:%1/mcp").arg(fixture.port()));
    QVERIFY(page->getAddressField()->isEnabled());
    QCOMPARE(page->getAddressField()->font().family(), pdf::PDFThemeFonts::getMonospaceFamily());
    QVERIFY(page->getStateText().startsWith(QStringLiteral("The MCP server is running at http://127.0.0.1:")));
    QVERIFY(page->getDisconnectAllLink()->isVisibleTo(page));
    QCOMPARE(page->getDisconnectHint()->text(), QStringLiteral("Disconnecting ends the session. To block an assistant, create a new access token."));
    AppAiTestHelper::screenshot(fixture.window, QStringLiteral("ai-running"));

    // Copy Address
    page->getCopyAddressButton()->click();
    QCOMPARE(QGuiApplication::clipboard()->text(), page->getAddressField()->text());

    // Stop Server by its button
    page->getStartStopButton()->click();
    QTRY_COMPARE(page->getShownState(), pdf::PDFAppAiPage::ShownState::Stopped);
    QCOMPARE(page->getStartStopButton()->text(), QStringLiteral("Start Server"));

    // Remote 653:6384 (blue, Running + Remote access, the amber note and warning row)
    QVERIFY(!AppAiTestHelper::settings(fixture).setValue(pdf::PDFSettingKeys::mcpAllowRemote(), true).isError());
    page->getStartStopButton()->click();
    QTRY_COMPARE_WITH_TIMEOUT(page->getShownState(), pdf::PDFAppAiPage::ShownState::Remote, AppAiTestHelper::timeout(10000));
    QCOMPARE(page->getServerCard()->getHue(), std::optional<pdf::PDFAccentHue>(pdf::PDFAccentHue::Blue));
    QCOMPARE(AppAiTestHelper::badgeTexts(page), QStringList({ QStringLiteral("Running"), QStringLiteral("Remote access") }));
    QVERIFY(page->getRemoteWarning()->isVisibleTo(page));
    QCOMPARE(page->getNote()->getHue(), std::optional<pdf::PDFAccentHue>(pdf::PDFAccentHue::Amber));
    AppAiTestHelper::screenshot(fixture.window, QStringLiteral("ai-remote"));
    fixture.controller()->stop();
    QVERIFY(!AppAiTestHelper::settings(fixture).setValue(pdf::PDFSettingKeys::mcpAllowRemote(), false).isError());
    QTRY_VERIFY(!page->getRemoteWarning()->isVisibleTo(page));
}

void AppAiTest::figmaGeometry()
{
    // Review RF7: the server card, the remote warning row, the callouts, the setup
    // steps and the new token dialog against their frames (the frames show the
    // token required - not the default of the local mode, owner 2026-10-28)
    pdftest::AppMcpFixture fixture;
    QVERIFY(!AppAiTestHelper::settings(fixture).setValue(pdf::PDFSettingKeys::mcpRequireToken(), true).isError());
    pdf::PDFAppAiPage* page = AppAiTestHelper::open(fixture);
    QVERIFY(page);
    const pdf::PDFThemeManager* theme = pdf::PDFThemeManager::getInstance();
    QWidget* addressBox = page->findChild<QWidget*>(QStringLiteral("addressBox"));
    pdf::PDFIconBadge* logo = page->getServerCard()->findChild<pdf::PDFIconBadge*>(QStringLiteral("serverLogo"));
    QVERIFY(addressBox && logo);

    const auto pixel = [](QWidget* widget, QPoint point) {
        return AppAiTestHelper::render(widget, 1.0).pixelColor(point).rgb() & 0xFFFFFF;
    };
    const auto token = [theme](pdf::PDFThemeColors::Color color) { return theme->getColor(color).rgb() & 0xFFFFFF; };

    // Stopped 653:6030 / 653:6042: the neutral logo, the address bg/subtle in text/muted
    QCOMPARE(logo->getIconColor(), pdf::PDFAccentHueColors::getColor(pdf::PDFAccentHue::Neutral, pdf::PDFAccentHueColors::Role::Icon));
    QCOMPARE(pixel(addressBox, QPoint(addressBox->width() - 4, addressBox->height() / 2)), token(pdf::PDFThemeColors::BgSubtle));
    QCOMPARE(page->getAddressField()->palette().color(QPalette::Disabled, QPalette::Text).rgb() & 0xFFFFFF, token(pdf::PDFThemeColors::TextMuted));

    // Running 653:5446: the blue icon, the address bg/surface in text/primary, the
    // address takes the rest of the row (flex), the buttons end at the content edge
    QVERIFY(fixture.startServer());
    QTRY_COMPARE(page->getShownState(), pdf::PDFAppAiPage::ShownState::Running);
    pdftest::AppDocumentFixture::settle();
    QCOMPARE(logo->getIconColor(), pdf::PDFAccentHueColors::getColor(pdf::PDFAccentHue::Blue, pdf::PDFAccentHueColors::Role::Icon));
    QCOMPARE(pixel(addressBox, QPoint(addressBox->width() - 4, addressBox->height() / 2)), token(pdf::PDFThemeColors::BgSurface));
    QCOMPARE(page->getAddressField()->palette().color(QPalette::Active, QPalette::Text).rgb() & 0xFFFFFF, token(pdf::PDFThemeColors::TextPrimary));
    QWidget* card = page->getServerCard();
    const QRect addressRect(addressBox->mapTo(card, QPoint(0, 0)), addressBox->size());
    const QRect stopRect(page->getStartStopButton()->mapTo(card, QPoint(0, 0)), page->getStartStopButton()->size());
    const QRect copyRect(page->getCopyAddressButton()->mapTo(card, QPoint(0, 0)), page->getCopyAddressButton()->size());
    QCOMPARE(addressRect.top(), stopRect.top());
    QCOMPARE(addressRect.height(), 36);
    QCOMPARE(copyRect.left(), addressRect.right() + 1 + 10);
    QCOMPARE(stopRect.right() + 1, card->width() - 21);
    QCOMPARE(addressRect.left(), 21 + 56 + 20);

    // The header of Connect an assistant 653:5686 on one line (title 18) with its
    // hint at the trailing side, the tiles 12 below it
    QWidget* connectSection = page->findChild<QWidget*>(QStringLiteral("connectSection"));
    pdf::PDFAppSectionHeader* connectHeader = connectSection ? connectSection->findChild<pdf::PDFAppSectionHeader*>() : nullptr;
    QVERIFY(connectHeader);
    QCOMPARE(connectHeader->height(), 18);

    // Remote 653:6658: a flat amber row 54 high without a divider and a radius,
    // directly below Network access; the next row keeps its divider
    QVERIFY(!AppAiTestHelper::settings(fixture).setValue(pdf::PDFSettingKeys::mcpAllowRemote(), true).isError());
    fixture.controller()->stop();
    QTRY_COMPARE(page->getShownState(), pdf::PDFAppAiPage::ShownState::Stopped);
    page->getStartStopButton()->click();
    QTRY_COMPARE_WITH_TIMEOUT(page->getShownState(), pdf::PDFAppAiPage::ShownState::Remote, AppAiTestHelper::timeout(10000));
    pdftest::AppDocumentFixture::settle();
    pdf::PDFAppSettingRow* warningRow = qobject_cast<pdf::PDFAppSettingRow*>(page->getRemoteWarning());
    QVERIFY(warningRow && warningRow->isVisible());
    QVERIFY(!warningRow->isDividerVisible());
    QCOMPARE(warningRow->height(), 54);
    QWidget* warningBox = warningRow->findChild<QWidget*>(QStringLiteral("remoteWarningBox"));
    pdf::PDFIconBadge* warningIcon = warningBox ? warningBox->findChild<pdf::PDFIconBadge*>() : nullptr;
    QVERIFY(warningIcon);
    QCOMPARE(warningIcon->y() + warningIcon->height() / 2, warningBox->height() / 2);
    const QRgb amber = token(pdf::PDFAccentHueColors::getColor(pdf::PDFAccentHue::Amber, pdf::PDFAccentHueColors::Role::Background));
    QCOMPARE(pixel(warningRow, QPoint(0, 0)), amber);
    QCOMPARE(pixel(warningRow, QPoint(warningRow->width() - 1, warningRow->height() - 1)), amber);
    QCOMPARE(pixel(warningRow, QPoint(warningRow->width() / 2, 0)), amber);
    pdf::PDFAppSettingRow* requireRow = nullptr;
    for (pdf::PDFAppSettingRow* row : page->findChildren<pdf::PDFAppSettingRow*>())
    {
        if (row->isVisible() && row->y() == warningRow->y() + warningRow->height() && row->parentWidget() == warningRow->parentWidget())
        {
            requireRow = row;
        }
    }
    QVERIFY(requireRow);
    QVERIFY(requireRow->isDividerVisible());
    fixture.controller()->stop();
    QVERIFY(!AppAiTestHelper::settings(fixture).setValue(pdf::PDFSettingKeys::mcpAllowRemote(), false).isError());

    // The callouts 940:136797 / 955:148979: radius 8, the icon centered vertically
    QCOMPARE(page->getCallout()->getRadius(), 8.0);
    QWidget* calloutIcon = page->getCallout()->findChild<pdf::PDFIconBadge*>();
    QVERIFY(calloutIcon);
    const QLayoutItem* iconItem = nullptr;
    for (int i = 0; i < page->getCallout()->layout()->count(); ++i)
    {
        if (page->getCallout()->layout()->itemAt(i)->widget() == calloutIcon)
        {
            iconItem = page->getCallout()->layout()->itemAt(i);
        }
    }
    QVERIFY(iconItem);
    QCOMPARE(iconItem->alignment() & Qt::AlignVertical_Mask, Qt::AlignVCenter);

    // The steps of two columns (955:149417): each step keeps the height of its
    // text and its code box at the top, the shorter one is not stretched
    page->getTile(pdf::PDFAppMcpSetupClient::Other)->click();
    pdftest::AppDocumentFixture::settle();
    QList<pdf::PDFCodeBox*> boxes = page->getCodeBoxes();
    QCOMPARE(boxes.size(), 3);
    QWidget* step2 = page->findChild<QWidget*>(QStringLiteral("setupStep2"));
    QVERIFY(step2);
    pdf::PDFCodeBox* headerBox = boxes.at(1);
    QVERIFY(headerBox->height() < 60);
    QVERIFY2(headerBox->mapTo(step2, QPoint(0, 0)).y() < 60, qPrintable(QString::number(headerBox->mapTo(step2, QPoint(0, 0)).y())));

    // The new token dialog 941:7796: the badge at the top of the head
    QVERIFY(fixture.startServer());
    QTRY_COMPARE(page->getShownState(), pdf::PDFAppAiPage::ShownState::Running);
    QVERIFY(page->openNewTokenDialog());
    pdf::PDFAppAiNewTokenDialog* dialog = page->getNewTokenDialog();
    QTRY_VERIFY(dialog && dialog->isVisible());
    pdftest::AppDocumentFixture::settle();
    QWidget* badge = nullptr;
    QWidget* title = nullptr;
    for (QWidget* child : dialog->findChildren<QWidget*>())
    {
        if (child->isVisible() && child->size() == QSize(36, 36) && !qobject_cast<QAbstractButton*>(child))
        {
            badge = child;
        }
        pdf::PDFLabel* label = qobject_cast<pdf::PDFLabel*>(child);
        if (label && label->text() == QStringLiteral("Create a new access token?"))
        {
            title = label;
        }
    }
    QVERIFY(badge && title);
    QVERIFY2(qAbs(badge->mapTo(dialog, QPoint(0, 0)).y() - title->mapTo(dialog, QPoint(0, 0)).y()) <= 2,
             qPrintable(QStringLiteral("%1 %2").arg(badge->mapTo(dialog, QPoint(0, 0)).y()).arg(title->mapTo(dialog, QPoint(0, 0)).y())));
    dialog->getCancelButton()->click();
    QTRY_VERIFY(!page->getNewTokenDialog());
}

void AppAiTest::pageStateOtherInstance()
{
    pdftest::AppMcpFixture fixture;
    pdf::PDFAppAiPage* page = AppAiTestHelper::open(fixture);
    QVERIFY(page);

    // 940:135321: the row of a live owner of this process (another window)
    QVERIFY(AppAiTestHelper::claimOther(fixture, QStringLiteral("other"), QCoreApplication::applicationPid()));
    QTRY_COMPARE(page->getShownState(), pdf::PDFAppAiPage::ShownState::OtherInstance);
    QCOMPARE(AppAiTestHelper::badgeTexts(page), QStringList({ QStringLiteral("In another PDF4QT window") }));
    QCOMPARE(page->getServerCard()->getHue(), std::nullopt);
    QVERIFY(page->getCallout()->isVisibleTo(page));
    QCOMPARE(page->getCalloutButton()->text(), QStringLiteral("Show Other Window"));
    QVERIFY(page->getCalloutButton()->isVisibleTo(page));
    QVERIFY(!page->getStartStopButton()->isEnabled());
    QCOMPARE(page->getAddressField()->text(), QStringLiteral("http://127.0.0.1:8765/mcp"));
    QCOMPARE(page->getDisconnectHint()->text(), QStringLiteral("Disconnect and Clear act on the server in the other PDF4QT window. To block an assistant, create a new access token."));
    QVERIFY(page->getCopySetupButton()->property("primary").toBool());
    AppAiTestHelper::screenshot(fixture.window, QStringLiteral("ai-other-instance"));

    // Released: stopped again
    QVERIFY(!AppAiTestHelper::repository(fixture).releaseServer(QStringLiteral("other")).isError());
    QTRY_COMPARE(page->getShownState(), pdf::PDFAppAiPage::ShownState::Stopped);
    QVERIFY(page->getStartStopButton()->isEnabled());
}

void AppAiTest::pageStatePortInUseForeign()
{
    pdftest::AppMcpFixture fixture;
    pdf::PDFAppAiPage* page = AppAiTestHelper::open(fixture);
    QVERIFY(page);

    // A foreign listener, which never answers HTTP (955:148587)
    QTcpServer foreign;
    QVERIFY(foreign.listen(QHostAddress::LocalHost, 0));
    QVERIFY(!AppAiTestHelper::settings(fixture).setValue(pdf::PDFSettingKeys::mcpPort(), int(foreign.serverPort())).isError());
    pdftest::AppDocumentFixture::settle();
    fixture.controller()->start();
    QTRY_COMPARE_WITH_TIMEOUT(page->getShownState(), pdf::PDFAppAiPage::ShownState::PortInUse, AppAiTestHelper::timeout(10000));
    QCOMPARE(AppAiTestHelper::badgeTexts(page), QStringList({ QStringLiteral("Could not start") }));
    QCOMPARE(page->getStatusBadges().front()->getTextColor(), std::optional<pdf::PDFThemeColors::Color>(pdf::PDFThemeColors::TextDanger));
    QCOMPARE(page->getCallout()->getHue(), std::optional<pdf::PDFAccentHue>(pdf::PDFAccentHue::Red));
    QCOMPARE(page->getCalloutButton()->text(), QStringLiteral("Change Port"));
    QVERIFY(page->getStartStopButton()->property("primary").toBool());

    // The port row: the description in text/danger and the red spin box
    pdf::PDFAppSettingRow* portRow = page->findSettingRow(QStringLiteral("port"));
    QVERIFY(portRow);
    QCOMPARE(portRow->getDescription(), QStringLiteral("Port %1 is used by another program – choose a free port (1024–65535)").arg(foreign.serverPort()));
    QVERIFY(portRow->getControl()->getEditor()->property("error").toBool());
    AppAiTestHelper::screenshot(fixture.window, QStringLiteral("ai-port-in-use"));

    // Change Port focuses the field
    page->getCalloutButton()->click();
    pdftest::AppDocumentFixture::settle();
    QVERIFY(portRow->getControl()->getEditor()->hasFocus() || portRow->getControl()->getEditor()->isAncestorOf(QApplication::focusWidget()));
}

void AppAiTest::pageStatePortInUsePdf4Qt()
{
    pdftest::AppMcpFixture fixture;
    pdf::PDFAppAiPage* page = AppAiTestHelper::open(fixture);
    QVERIFY(page);

    // A PDF4QT server of another process (no row of this database): the probe finds
    // it, the page shows the other window without Show Other Window (a portable
    // instance cannot reach it)
    pdftest::FakeMcpHost fakeHost;
    pdf::PDFMcpServer other(&fakeHost);
    pdf::PDFMcpServerConfig config;
    config.port = 0;
    config.token = QByteArrayLiteral("token");
    QVERIFY(other.start(config));
    QTRY_COMPARE_WITH_TIMEOUT(other.getState(), pdf::PDFMcpServerState::Running, AppAiTestHelper::timeout(10000));

    QVERIFY(!AppAiTestHelper::settings(fixture).setValue(pdf::PDFSettingKeys::mcpPort(), int(other.getPort())).isError());
    pdftest::AppDocumentFixture::settle();
    fixture.controller()->start();
    QTRY_COMPARE_WITH_TIMEOUT(page->getShownState(), pdf::PDFAppAiPage::ShownState::OtherInstance, AppAiTestHelper::timeout(10000));
    QVERIFY(page->getCallout()->isVisibleTo(page));
    QVERIFY(!page->getCalloutButton()->isVisibleTo(page));
    other.stop();
}

void AppAiTest::settingsRowsApply()
{
    pdftest::AppMcpFixture fixture;
    pdf::PDFAppAiPage* page = AppAiTestHelper::open(fixture);
    QVERIFY(page);
    QVERIFY(fixture.startServer());
    QTRY_COMPARE(page->getShownState(), pdf::PDFAppAiPage::ShownState::Running);

    // The port: a change restarts the server on the new port
    const quint16 newPort = pdftest::AppMcpFixture::findFreePort();
    pdf::PDFAppSettingRow* portRow = page->findSettingRow(QStringLiteral("port"));
    QVERIFY(portRow);
    QSpinBox* spinBox = qobject_cast<QSpinBox*>(portRow->getControl()->getEditor());
    QVERIFY(spinBox);
    QVERIFY(!spinBox->isGroupSeparatorShown());
    spinBox->setValue(newPort);
    portRow->commitPendingValue();
    QTRY_COMPARE_WITH_TIMEOUT(fixture.controller()->getPort(), newPort, AppAiTestHelper::timeout(10000));
    QTRY_COMPARE_WITH_TIMEOUT(page->getShownState(), pdf::PDFAppAiPage::ShownState::Running, AppAiTestHelper::timeout(10000));
    QTRY_COMPARE(page->getAddressField()->text(), QStringLiteral("http://127.0.0.1:%1/mcp").arg(newPort));

    // A permission: a new catalog without a restart
    const quint64 generation = fixture.host()->getCatalogGeneration();
    pdf::PDFSegmentedControl* read = page->findSettingRow(QStringLiteral("read-documents"))->getControl()->getSegmentedControl();
    QTest::mouseClick(read->findChildren<QAbstractButton*>().at(1), Qt::LeftButton);
    QTRY_VERIFY(fixture.host()->getCatalogGeneration() > generation);
    QCOMPARE(fixture.controller()->getPort(), newPort);
    QVERIFY(fixture.controller()->isRunning());

    // Start with PDF4QT and Require access token are check boxes "Enable"
    QCheckBox* start = page->findSettingRow(QStringLiteral("start-with-pdf4qt"))->getControl()->getCheckBox();
    QVERIFY(start);
    QCOMPARE(start->text(), QStringLiteral("Enable"));
    start->click();
    QTRY_VERIFY(AppAiTestHelper::settings(fixture).getBool(pdf::PDFSettingKeys::mcpStartWithApp()));

    // Copy of the token
    page->getCopyTokenButton()->click();
    QCOMPARE(QGuiApplication::clipboard()->text(), fixture.controller()->getToken());
    QCOMPARE(page->getTokenField()->text(), fixture.controller()->getToken());
    QVERIFY(page->getTokenField()->isPasswordField());
    QCOMPARE(page->getTokenField()->width(), 232);
}

void AppAiTest::remoteForcesToken()
{
    pdftest::AppMcpFixture fixture;
    QVERIFY(!AppAiTestHelper::settings(fixture).setValue(pdf::PDFSettingKeys::mcpRequireToken(), false).isError());
    pdf::PDFAppAiPage* page = AppAiTestHelper::open(fixture);
    QVERIFY(page);

    QCheckBox* require = page->findSettingRow(QStringLiteral("require-access-token"))->getControl()->getCheckBox();
    QVERIFY(require && !require->isChecked() && require->isEnabled());

    // Allow remote connections: the token is generated, Require token checked and disabled (653:6662)
    page->getRemoteRadio()->click();
    QTRY_VERIFY(AppAiTestHelper::settings(fixture).getBool(pdf::PDFSettingKeys::mcpAllowRemote()));
    QVERIFY(!fixture.controller()->getToken().isEmpty());
    QTRY_VERIFY(require->isChecked() && !require->isEnabled());
    QCOMPARE(page->findSettingRow(QStringLiteral("require-access-token"))->getDescription(), QStringLiteral("Always required while remote connections are allowed"));
    QVERIFY(page->getRemoteWarning()->isVisibleTo(page));
    // The stored value of Require token is kept (the remote mode forces it)
    QVERIFY(!AppAiTestHelper::settings(fixture).getBool(pdf::PDFSettingKeys::mcpRequireToken()));
    QVERIFY(fixture.controller()->createConfig().requireToken);

    // The setup texts contain the token
    QVERIFY(page->getSetup().copyText.contains(fixture.controller()->getToken()));

    page->getLocalRadio()->click();
    QTRY_VERIFY(!AppAiTestHelper::settings(fixture).getBool(pdf::PDFSettingKeys::mcpAllowRemote()));
    QTRY_VERIFY(!require->isChecked() && require->isEnabled());
    QVERIFY(!page->getSetup().copyText.contains(QStringLiteral("Authorization")));
}

void AppAiTest::defaultLocalWithoutToken()
{
    // Owner 2026-10-28: the local mode requires no access token by default (the
    // server listens on the loopback only, checks Host and refuses every Origin
    // without the token). A token exists anyway, so Require access token turned
    // on later works at once; the remote mode still forces it
    pdftest::AppMcpFixture fixture;
    QVERIFY(!AppAiTestHelper::settings(fixture).getBool(pdf::PDFSettingKeys::mcpRequireToken()));
    QVERIFY(!fixture.controller()->createConfig().isTokenRequired());
    pdf::PDFAppAiPage* page = AppAiTestHelper::open(fixture);
    QVERIFY(page);

    // Review RTOK: Create a new access token? does not tell to set the assistants
    // up again with the new token while none is required (they connect without
    // it); the open box follows Require access token
    QVERIFY(page->openNewTokenDialog());
    QPointer<pdf::PDFAppAiNewTokenDialog> tokenDialog = page->getNewTokenDialog();
    QTRY_VERIFY(tokenDialog && tokenDialog->isVisible());
    QVERIFY(!tokenDialog->isTokenRequired());
    QCOMPARE(tokenDialog->accessibleDescription(), pdf::PDFAppAiNewTokenDialog::getBodyText(QStringList(), false));
    QVERIFY(tokenDialog->accessibleDescription().startsWith(QStringLiteral("Require access token is off")));
    QVERIFY(!tokenDialog->accessibleDescription().contains(QStringLiteral("Set up your assistants")));
    QVERIFY(pdf::PDFAppAiNewTokenDialog::getBodyText({ QStringLiteral("Claude Code") }, false).contains(QStringLiteral("(Claude Code) and can connect again at once")));
    QVERIFY(!pdf::PDFAppAiNewTokenDialog::getBodyText({ QStringLiteral("Claude Code") }, false).contains(QStringLiteral("setup command")));
    QVERIFY(!AppAiTestHelper::settings(fixture).setValue(pdf::PDFSettingKeys::mcpRequireToken(), true).isError());
    QTRY_VERIFY(tokenDialog->isTokenRequired());
    QCOMPARE(tokenDialog->accessibleDescription(), pdf::PDFAppAiNewTokenDialog::getBodyText(QStringList(), true));
    QVERIFY(!AppAiTestHelper::settings(fixture).setValue(pdf::PDFSettingKeys::mcpRequireToken(), false).isError());
    QTRY_VERIFY(!tokenDialog->isTokenRequired());
    tokenDialog->getCancelButton()->click();
    QTRY_VERIFY(!page->getNewTokenDialog());

    QVERIFY(fixture.startServer());
    QTRY_COMPARE(page->getShownState(), pdf::PDFAppAiPage::ShownState::Running);
    QVERIFY(!fixture.controller()->getServer()->getConfig().isTokenRequired());

    const QString token = fixture.controller()->getToken();
    QCOMPARE(token.size(), 43);
    QTRY_COMPARE(page->getTokenField()->text(), token);
    QVERIFY(page->getCopyTokenButton()->isEnabled());
    QCheckBox* require = page->findSettingRow(QStringLiteral("require-access-token"))->getControl()->getCheckBox();
    QVERIFY(require && !require->isChecked() && require->isEnabled());

    // The setup texts have no header and no token
    QTRY_VERIFY(!page->getSetup().copyText.isEmpty());
    QVERIFY(!page->getSetup().copyText.contains(QStringLiteral("Authorization")));
    QVERIFY(!page->getSetup().copyText.contains(token));
    QVERIFY(!page->getSetup().containsToken);

    // A request without Authorization passes, a client set up with a token (also
    // an old one) passes too, a browser page (Origin) is refused
    pdftest::McpTestClient client(fixture.port());
    QVERIFY(!client.discover().isEmpty());
    QCOMPARE(client.lastResponse().status, 200);
    client.setToken(token.toLatin1());
    QVERIFY(!client.discover().isEmpty());
    client.setToken(QByteArrayLiteral("an-old-token"));
    QVERIFY(!client.discover().isEmpty());
    client.setToken(QByteArray());
    client.setExtraHeaders({ qMakePair(QByteArray("Origin"), QByteArray("http://127.0.0.1:3000")) });
    QVERIFY(client.discover().isEmpty());
    QCOMPARE(client.lastResponse().status, 403);
    client.setExtraHeaders({});

    // Require access token turned on: the server restarts with the same token,
    // the setup texts get it, a request without it is refused
    require->click();
    QTRY_VERIFY(AppAiTestHelper::settings(fixture).getBool(pdf::PDFSettingKeys::mcpRequireToken()));
    QTRY_VERIFY_WITH_TIMEOUT(fixture.controller()->isRunning() && fixture.controller()->getServer()->getConfig().isTokenRequired(), AppAiTestHelper::timeout(10000));
    QCOMPARE(fixture.controller()->getToken(), token);
    QTRY_VERIFY(page->getSetup().copyText.contains(token));
    pdftest::McpTestClient strict(fixture.port());
    QVERIFY(strict.discover().isEmpty());
    QCOMPARE(strict.lastResponse().status, 401);
    strict.setToken(token.toLatin1());
    QVERIFY(!strict.discover().isEmpty());

    // Off again; the remote mode forces the token also with the option off
    require->click();
    QTRY_VERIFY(!AppAiTestHelper::settings(fixture).getBool(pdf::PDFSettingKeys::mcpRequireToken()));
    QTRY_VERIFY_WITH_TIMEOUT(fixture.controller()->isRunning() && !fixture.controller()->getServer()->getConfig().isTokenRequired(), AppAiTestHelper::timeout(10000));
    fixture.controller()->stop();
    QVERIFY(!AppAiTestHelper::settings(fixture).setValue(pdf::PDFSettingKeys::mcpAllowRemote(), true).isError());
    QVERIFY(fixture.controller()->createConfig().isTokenRequired());
    QTRY_VERIFY(page->getSetup().copyText.contains(token));
    QVERIFY(!AppAiTestHelper::settings(fixture).setValue(pdf::PDFSettingKeys::mcpAllowRemote(), false).isError());
}

void AppAiTest::reviewConnectionChangedWhileStarting()
{
    // Review RTOK: a change of the connection (Require access token - also of
    // another instance) while the server starts found no running server, so the
    // server ran without the token while the page showed it required. The server,
    // which comes up with an older configuration than the settings, restarts
    pdftest::AppMcpFixture fixture;
    QVERIFY(fixture.startServer());
    pdf::PDFAppMcpController* controller = fixture.controller();
    QVERIFY(!controller->getServer()->getConfig().isTokenRequired());

    controller->stop();
    QVERIFY(controller->start());
    QCOMPARE(controller->getServer()->getState(), pdf::PDFMcpServerState::Starting);
    QVERIFY(!AppAiTestHelper::settings(fixture).setValue(pdf::PDFSettingKeys::mcpRequireToken(), true).isError());
    QTRY_VERIFY_WITH_TIMEOUT(controller->isRunning() && controller->getServer()->getConfig().isTokenRequired(), AppAiTestHelper::timeout(10000));
    pdftest::McpTestClient client(fixture.port());
    QVERIFY(client.discover().isEmpty());
    QCOMPARE(client.lastResponse().status, 401);
    client.setToken(controller->getToken().toLatin1());
    QVERIFY(!client.discover().isEmpty());

    // Off again during a start: the server follows too, the restart settles
    controller->stop();
    QVERIFY(controller->start());
    QCOMPARE(controller->getServer()->getState(), pdf::PDFMcpServerState::Starting);
    QVERIFY(!AppAiTestHelper::settings(fixture).setValue(pdf::PDFSettingKeys::mcpRequireToken(), false).isError());
    QTRY_VERIFY_WITH_TIMEOUT(controller->isRunning() && !controller->getServer()->getConfig().isTokenRequired(), AppAiTestHelper::timeout(10000));
    QVERIFY(controller->isOwner());
    pdftest::McpTestClient open(fixture.port());
    QVERIFY(!open.discover().isEmpty());
}

void AppAiTest::newTokenDialog()
{
    // The setup texts with the token (Require access token on)
    pdftest::AppMcpFixture fixture;
    QVERIFY(!AppAiTestHelper::settings(fixture).setValue(pdf::PDFSettingKeys::mcpRequireToken(), true).isError());
    pdf::PDFAppAiPage* page = AppAiTestHelper::open(fixture);
    QVERIFY(page);
    QVERIFY(fixture.startServer());
    const QString oldToken = fixture.controller()->getToken();
    QVERIFY(!oldToken.isEmpty());

    // Without clients (955:150878)
    page->getNewTokenButton()->click();
    pdf::PDFAppAiNewTokenDialog* dialog = page->getNewTokenDialog();
    QVERIFY(dialog);
    QTRY_VERIFY(dialog->isVisible());
    QVERIFY(dialog->getClientNames().isEmpty());
    QVERIFY(pdf::PDFAppAiNewTokenDialog::getBodyText(QStringList()).contains(QStringLiteral("No assistant is connected now")));
    QVERIFY(dialog->getCancelButton()->property("primary").toBool());
    AppAiTestHelper::screenshot(fixture.window, QStringLiteral("ai-new-token"));
    dialog->getCancelButton()->click();
    QTRY_VERIFY(!page->getNewTokenDialog());
    QCOMPARE(fixture.controller()->getToken(), oldToken);

    // With clients (940:135813): the names in the text (the rows of the other window),
    // Create New Token writes a new token, which disconnects everybody [D2]
    fixture.controller()->stop();
    QVERIFY(AppAiTestHelper::claimOther(fixture, QStringLiteral("other"), QCoreApplication::applicationPid()));
    QVERIFY(!AppAiTestHelper::repository(fixture).upsertClient(AppAiTestHelper::client(QStringLiteral("a:00000000000000000000000000000001"), QStringLiteral("Claude Code"))).isError());
    QVERIFY(!AppAiTestHelper::repository(fixture).upsertClient(AppAiTestHelper::client(QStringLiteral("a:00000000000000000000000000000002"), QStringLiteral("Gemini CLI"))).isError());
    QTRY_COMPARE(page->getAssistantRows().size(), 2);
    QVERIFY(page->openNewTokenDialog());
    dialog = page->getNewTokenDialog();
    QTRY_VERIFY(dialog && dialog->isVisible());
    QCOMPARE(dialog->getClientNames().size(), 2);
    QVERIFY(dialog->getClientNames().contains(QStringLiteral("Claude Code")) && dialog->getClientNames().contains(QStringLiteral("Gemini CLI")));
    QVERIFY(pdf::PDFAppAiNewTokenDialog::getBodyText({ QStringLiteral("Claude Code"), QStringLiteral("Gemini CLI") }).contains(QStringLiteral("(Claude Code, Gemini CLI)")));
    AppAiTestHelper::screenshot(fixture.window, QStringLiteral("ai-new-token-clients"));
    dialog->getCreateButton()->click();
    QTRY_VERIFY(fixture.controller()->getToken() != oldToken);
    QCOMPARE(fixture.controller()->getToken().size(), 43);
    QTRY_COMPARE(page->getTokenField()->text(), fixture.controller()->getToken());
    QTRY_VERIFY(page->getSetup().copyText.contains(fixture.controller()->getToken()));

    // Escape cancels
    QVERIFY(page->openNewTokenDialog());
    dialog = page->getNewTokenDialog();
    QTRY_VERIFY(dialog && dialog->isVisible());
    const QString token = fixture.controller()->getToken();
    QTest::keyClick(dialog, Qt::Key_Escape);
    QTRY_VERIFY(!page->getNewTokenDialog());
    QCOMPARE(fixture.controller()->getToken(), token);
}

void AppAiTest::assistantsList()
{
    pdftest::AppMcpFixture fixture;
    pdf::PDFAppAiPage* page = AppAiTestHelper::open(fixture);
    QVERIFY(page);
    QVERIFY(fixture.startServer());
    QTRY_COMPARE(page->getShownState(), pdf::PDFAppAiPage::ShownState::Running);
    QCOMPARE(page->getAssistantsEmptyState()->getText(), QStringLiteral("Set up an assistant below to connect it."));
    QVERIFY(page->getAssistantsEmptyState()->isVisibleTo(page));
    fixture.controller()->stop();

    // The rows mcp_client of another window (written by its owner, not synchronized
    // here): local (48) and remote with the Tag (52)
    QVERIFY(AppAiTestHelper::claimOther(fixture, QStringLiteral("other"), QCoreApplication::applicationPid()));
    QVERIFY(!AppAiTestHelper::repository(fixture).upsertClient(AppAiTestHelper::client(QStringLiteral("a:00000000000000000000000000000001"), QStringLiteral("Claude Code"))).isError());
    QVERIFY(!AppAiTestHelper::repository(fixture).upsertClient(AppAiTestHelper::client(QStringLiteral("s:00000000000000000000000000000002"), QStringLiteral("Codex CLI"), QStringLiteral("192.168.1.57"))).isError());
    QTRY_COMPARE(page->getAssistantRows().size(), 2);
    QVERIFY(!page->getAssistantsEmptyState()->isVisibleTo(page));

    std::map<QString, pdf::PDFAssistantRow*> rows;
    for (pdf::PDFAssistantRow* row : page->getAssistantRows())
    {
        rows[row->getRow().name] = row;
    }
    QVERIFY(rows.count(QStringLiteral("Claude Code")) && rows.count(QStringLiteral("Codex CLI")));
    const QString time = pdf::PDFAppLocale::formatClock(QTime(14, 2));
    QCOMPARE(rows[QStringLiteral("Claude Code")]->getRow().meta, QStringLiteral("This computer · connected at %1").arg(time));
    QCOMPARE(rows[QStringLiteral("Claude Code")]->getRow().avatar.monogram, QStringLiteral("Cl"));
    QCOMPARE(rows[QStringLiteral("Claude Code")]->getRow().avatar.hue, pdf::PDFAccentHue::Amber);
    QVERIFY(rows[QStringLiteral("Claude Code")]->getRow().tag.isEmpty());
    QCOMPARE(rows[QStringLiteral("Codex CLI")]->getRow().meta, QStringLiteral("192.168.1.57 · connected at %1").arg(time));
    QCOMPARE(rows[QStringLiteral("Codex CLI")]->getRow().tag, QStringLiteral("Remote"));
    QCOMPARE(rows[QStringLiteral("Codex CLI")]->getRow().avatar.hue, pdf::PDFAccentHue::Green);
    QCOMPARE(rows[QStringLiteral("Codex CLI")]->getDisconnectToolTip(), QStringLiteral("Disconnect"));
    AppAiTestHelper::screenshot(fixture.window, QStringLiteral("ai-assistants"));

    // The same rows again: the widgets stay (the focus of a button stays)
    QPointer<pdf::PDFAssistantRow> kept = rows[QStringLiteral("Claude Code")];
    Q_EMIT fixture.controller()->assistantsChanged();
    QVERIFY(kept && page->getAssistantRows().contains(kept.data()));

    // A row removed by the owner goes
    QVERIFY(!AppAiTestHelper::repository(fixture).removeClient(QStringLiteral("s:00000000000000000000000000000002")).isError());
    QTRY_COMPARE(page->getAssistantRows().size(), 1);

    // The owner gone: the empty state of a stopped server
    QVERIFY(!AppAiTestHelper::repository(fixture).releaseServer(QStringLiteral("other")).isError());
    QTRY_VERIFY(page->getAssistantRows().isEmpty());
    QCOMPARE(page->getAssistantsEmptyState()->getText(), QStringLiteral("Start the server to accept connections."));
}

void AppAiTest::assistantsFromServer()
{
    pdftest::AppMcpFixture fixture;
    pdf::PDFAppAiPage* page = AppAiTestHelper::open(fixture);
    QVERIFY(page);
    QVERIFY(fixture.startServer());

    // A real assistant over HTTP: the owner writes its row, the page shows it [D2]
    std::unique_ptr<pdftest::McpTestClient> client = fixture.client(pdftest::McpTestEra::Modern);
    client->setClientInfo(QStringLiteral("gemini-cli-mcp-client"));
    const QJsonObject answer = client->listTools();
    if (answer.isEmpty())
    {
        QSKIP("The transport of package A does not answer");
    }
    QTRY_COMPARE_WITH_TIMEOUT(page->getAssistantRows().size(), 1, AppAiTestHelper::timeout(10000));
    QCOMPARE(page->getAssistantRows().front()->getRow().name, QStringLiteral("Gemini CLI"));
    QCOMPARE(page->getAssistantRows().front()->getRow().avatar.monogram, QStringLiteral("Ge"));
    QCOMPARE(AppAiTestHelper::repository(fixture).getClients().size(), size_t(1));

    // Disconnect: the assistant is gone until its next request
    QTest::mouseClick(page->getAssistantRows().front()->getDisconnectButton(), Qt::LeftButton);
    QTRY_VERIFY_WITH_TIMEOUT(page->getAssistantRows().isEmpty(), AppAiTestHelper::timeout(10000));
    QTRY_VERIFY(AppAiTestHelper::repository(fixture).getClients().empty());

    // The stop of the server deletes the rows
    client->listTools();
    QTRY_COMPARE_WITH_TIMEOUT(page->getAssistantRows().size(), 1, AppAiTestHelper::timeout(10000));
    fixture.controller()->stop();
    QTRY_VERIFY(AppAiTestHelper::repository(fixture).getClients().empty());
    QTRY_VERIFY(page->getAssistantRows().isEmpty());
}

void AppAiTest::reviewStaleClientRowsRemoved()
{
    // Review RFW7-M (coverage of PDFAppMcpController::syncClients): the rows
    // mcp_client of an owner, which ended without removing them (a crash), are
    // removed by the next owner - its server has no such assistants
    pdftest::AppMcpFixture fixture;
    pdf::PDFAppAiPage* page = AppAiTestHelper::open(fixture);
    QVERIFY(page);
    QVERIFY(!AppAiTestHelper::repository(fixture).upsertClient(AppAiTestHelper::client(QStringLiteral("a:00000000000000000000000000000002"), QStringLiteral("Claude Code"))).isError());
    QVERIFY(!AppAiTestHelper::repository(fixture).upsertClient(AppAiTestHelper::client(QStringLiteral("a:00000000000000000000000000000003"), QStringLiteral("Codex CLI"))).isError());
    QCOMPARE(AppAiTestHelper::repository(fixture).getClients().size(), size_t(2));

    QVERIFY(fixture.startServer());
    QTRY_VERIFY(fixture.controller()->isOwner());
    QTRY_VERIFY(AppAiTestHelper::repository(fixture).getClients().empty());
    QTRY_VERIFY(page->getAssistantRows().isEmpty());
}

void AppAiTest::assistantsOtherInstance()
{
    pdftest::AppMcpFixture fixture;
    pdf::PDFAppAiPage* page = AppAiTestHelper::open(fixture);
    QVERIFY(page);
    QVERIFY(AppAiTestHelper::claimOther(fixture, QStringLiteral("other"), QCoreApplication::applicationPid()));
    QVERIFY(!AppAiTestHelper::repository(fixture).upsertClient(AppAiTestHelper::client(QStringLiteral("a:00000000000000000000000000000001"), QStringLiteral("Claude Code"))).isError());
    QTRY_COMPARE(page->getShownState(), pdf::PDFAppAiPage::ShownState::OtherInstance);
    QTRY_COMPARE(page->getAssistantRows().size(), 1);

    // Disconnect and Disconnect All go to the owner by mcp_request
    QTest::mouseClick(page->getAssistantRows().front()->getDisconnectButton(), Qt::LeftButton);
    page->getDisconnectAllLink()->click();
    std::vector<pdf::PDFMcpRequestRow> requests = AppAiTestHelper::repository(fixture).takeRequests();
    QCOMPARE(requests.size(), size_t(2));
    QCOMPARE(requests[0].kind, pdf::PDFMcpRequestKind::Disconnect);
    QCOMPARE(requests[0].key, QStringLiteral("a:00000000000000000000000000000001"));
    QCOMPARE(requests[1].kind, pdf::PDFMcpRequestKind::DisconnectAll);
}

void AppAiTest::activityList()
{
    using Status = pdf::PDFMcpActivityStatus;
    pdftest::AppMcpFixture fixture;
    pdf::PDFAppAiPage* page = AppAiTestHelper::open(fixture);
    QVERIFY(page);
    QVERIFY(page->getActivityEmptyState()->isVisibleTo(page));
    QVERIFY(!page->getClearLink()->isVisibleTo(page));

    // Every status (955:150874) with its icon, hue and tone
    struct Case
    {
        Status status;
        QString icon;
        pdf::PDFAccentHue hue;
        QString statusText;
    };
    const Case cases[] =
    {
        { Status::Ok, "check", pdf::PDFAccentHue::Green, QString() },
        { Status::Allowed, "check", pdf::PDFAccentHue::Green, "Allowed by you" },
        { Status::DeniedUser, "user-x", pdf::PDFAccentHue::Neutral, "Denied by you" },
        { Status::DeniedTimeout, "timer-off", pdf::PDFAccentHue::Neutral, "Denied automatically · no answer in 2 min" },
        { Status::DeniedLocked, "lock", pdf::PDFAccentHue::Neutral, "Denied · the document is locked" },
        { Status::DeniedClosed, "ban", pdf::PDFAccentHue::Neutral, "Denied · the tab was closed" },
        { Status::DeniedBusy, "ban", pdf::PDFAccentHue::Neutral, "Denied · a tool is running on the document" },
        { Status::DeniedDialog, "ban", pdf::PDFAccentHue::Neutral, "Denied · a dialog was open in PDF4QT" },
        { Status::DeniedDisconnected, "unplug", pdf::PDFAccentHue::Neutral, "Denied · the assistant disconnected" },
        { Status::DeniedStopped, "power", pdf::PDFAccentHue::Neutral, "Denied · the server stopped" },
        { Status::Canceled, "x", pdf::PDFAccentHue::Neutral, "Canceled by the assistant" },
        { Status::Failed, "alert-circle", pdf::PDFAccentHue::Red, "Failed · The document changed in the meantime" },
    };

    for (const Case& testCase : cases)
    {
        QVERIFY(AppAiTestHelper::repository(fixture).clearActivity().isError() == false);
        QJsonObject detail;
        if (testCase.status == Status::Failed)
        {
            detail.insert(QStringLiteral("reason"), QStringLiteral("The document changed in the meantime"));
        }
        QVERIFY(!AppAiTestHelper::repository(fixture).addActivity({ AppAiTestHelper::activity(QStringLiteral("text.get_text"), testCase.status, detail) }).isError());
        QTRY_COMPARE(page->getActivityRows().size(), 1);
        QTRY_VERIFY(page->getActivityRows().front()->getRow().iconName == testCase.icon && page->getActivityRows().front()->getRow().status == testCase.statusText);
        const pdf::PDFComponentPainter::ActivityRow row = page->getActivityRows().front()->getRow();
        QCOMPARE(row.hue, testCase.hue);
        QCOMPARE(row.status, testCase.statusText);
        QCOMPARE(row.time, pdf::PDFAppLocale::formatClock(QTime(16, 44)));
        QCOMPARE(row.meta, QStringLiteral("Claude Code · Project_Report.pdf"));
        QVERIFY(!page->getActivityRows().front()->isClickable());
        QCOMPARE(row.statusTone, testCase.status == Status::Failed ? pdf::PDFComponentPainter::ActivityStatusTone::Danger : pdf::PDFComponentPainter::ActivityStatusTone::Muted);
    }

    // A waiting row of no request of this instance: not clickable, no Show
    QVERIFY(!AppAiTestHelper::repository(fixture).addActivity({ AppAiTestHelper::activity(QStringLiteral("files.open"), Status::Waiting) }).isError());
    QTRY_COMPARE(page->getActivityRows().size(), 2);
    QTRY_COMPARE(page->getActivityRows().front()->getRow().status, QStringLiteral("Waiting for your confirmation"));
    QVERIFY(page->getActivityRows().front()->getRow().linkText.isEmpty());
    QVERIFY(!page->getActivityRows().front()->isClickable());

    // The other window: "Waiting for confirmation in the other PDF4QT window" (955:147704;
    // a new owner ends the waiting rows of the previous one, so the row comes after the claim)
    QVERIFY(AppAiTestHelper::claimOther(fixture, QStringLiteral("other"), QCoreApplication::applicationPid()));
    QVERIFY(!AppAiTestHelper::repository(fixture).addActivity({ AppAiTestHelper::activity(QStringLiteral("files.open"), Status::Waiting) }).isError());
    QTRY_COMPARE(page->getActivityRows().front()->getRow().status, QStringLiteral("Waiting for confirmation in the other PDF4QT window"));
    QVERIFY(!page->getActivityRows().front()->isClickable());

    // 10 newest rows (owner Q6), Clear keeps the waiting rows
    std::vector<pdf::PDFMcpActivityRow> rows;
    for (int i = 0; i < 15; ++i)
    {
        rows.push_back(AppAiTestHelper::activity(QStringLiteral("render.page"), Status::Ok));
    }
    QVERIFY(!AppAiTestHelper::repository(fixture).addActivity(rows).isError());
    QTRY_COMPARE(page->getActivityRows().size(), qsizetype(pdf::PDFAppAiPage::ACTIVITY_ROW_COUNT));
    AppAiTestHelper::screenshot(fixture.window, QStringLiteral("ai-activity"));
    page->getClearLink()->click();
    QTRY_COMPARE(page->getActivityRows().size(), 1);
    QCOMPARE(page->getActivityRows().front()->getRow().iconName, QStringLiteral("hourglass"));
    QVERIFY(page->getClearLink()->isVisibleTo(page));
}

void AppAiTest::activityShowReopensDialog()
{
    pdftest::AppMcpFixture fixture;

    // The dialogs of package C instead of the test provider
    pdf::PDFAppMcpConfirmationUi* ui = pdf::PDFAppMcpConfirmationUi::find(fixture.host());
    if (!ui)
    {
        QSKIP("The confirmation interface of package C is not installed");
    }
    fixture.host()->setConfirmationProvider(ui);

    QVERIFY(fixture.open(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("simple.pdf"), 2)));
    QVERIFY(fixture.setPermission(pdf::PDFAppMcpPermissionKind::Read, pdf::PDFAppMcpPermission::Ask));
    std::shared_ptr<pdf::PDFMcpCall> call = fixture.startCall(QStringLiteral("document"), QJsonObject{ { QStringLiteral("action"), QStringLiteral("get_info") } });
    QTRY_VERIFY_WITH_TIMEOUT(!fixture.host()->getPendingConfirmations().empty(), AppAiTestHelper::timeout(10000));
    QVERIFY(fixture.waitActivity(pdf::PDFMcpActivityStatus::Waiting));

    pdf::PDFAppAiPage* page = AppAiTestHelper::open(fixture);
    QVERIFY(page);
    QTRY_VERIFY(!page->getActivityRows().isEmpty());
    pdf::PDFActivityRow* row = page->getActivityRows().front();
    QTRY_VERIFY(row->isClickable());
    QCOMPARE(row->getRow().linkText, QStringLiteral("Show"));
    QCOMPARE(row->getRow().status, QStringLiteral("Waiting for your confirmation · "));

    // Show: the tab of the document with its dialog
    QTest::mouseClick(row, Qt::LeftButton);
    QTRY_VERIFY(ui->getDialog() && ui->getDialog()->isVisible());
    ui->answer(fixture.host()->getPendingConfirmations().front()->id, pdf::PDFAppMcpDecision::DeniedUser);
    QVERIFY(pdftest::AppMcpFixture::waitResult(call).has_value());
    fixture.host()->setConfirmationProvider(fixture.confirmations());
}

void AppAiTest::activityHostileRows()
{
    pdftest::AppMcpFixture fixture;
    pdf::PDFAppAiPage* page = AppAiTestHelper::open(fixture);
    QVERIFY(page);

    // Hostile rows: unknown status, bidirectional and control characters, 10 kB texts
    pdf::PDFMcpActivityRow row = AppAiTestHelper::activity(QStringLiteral("annotations.add"), pdf::PDFMcpActivityStatus::Failed);
    row.client = QString(QChar(0x202E)) + QStringLiteral("Evil\nClient") + QString(10000, QChar('x'));
    row.document = QString(5000, QChar(0x05D0));
    row.detail = QJsonObject{ { QStringLiteral("reason"), QString(300, QChar('r')) }, { QStringLiteral("count"), QStringLiteral("NaN") } };
    QVERIFY(!AppAiTestHelper::repository(fixture).addActivity({ row }).isError());
    QTRY_COMPARE(page->getActivityRows().size(), 1);
    QVERIFY(!page->getActivityRows().front()->getRow().meta.contains(QChar(0x202E)));
    QVERIFY(!page->getActivityRows().front()->getRow().meta.contains(QChar('\n')));

    // Hostile client rows (names with controls)
    QVERIFY(AppAiTestHelper::claimOther(fixture, QStringLiteral("other"), QCoreApplication::applicationPid()));
    QVERIFY(!AppAiTestHelper::repository(fixture).upsertClient(AppAiTestHelper::client(QStringLiteral("a:00000000000000000000000000000001"), QString(QChar(0x202E)) + QStringLiteral("abc"))).isError());
    QTRY_COMPARE(page->getAssistantRows().size(), 1);
    QVERIFY(!page->getAssistantRows().front()->getRow().name.contains(QChar(0x202E)));
}

void AppAiTest::setupCards()
{
    // The variants with the token (Require access token on - not the default of
    // the local mode, owner 2026-10-28)
    pdftest::AppMcpFixture fixture;
    QVERIFY(!AppAiTestHelper::settings(fixture).setValue(pdf::PDFSettingKeys::mcpRequireToken(), true).isError());
    pdf::PDFAppAiPage* page = AppAiTestHelper::open(fixture);
    QVERIFY(page);
    QVERIFY(fixture.startServer());
    QTRY_COMPARE(page->getShownState(), pdf::PDFAppAiPage::ShownState::Running);
    const QString address = page->getAddressField()->text();
    const QString token = fixture.controller()->getToken();

    // Claude Code (653:5730): the chips, three steps, the token shortened in the box, whole in the copy
    QCOMPARE(page->getSetupClient(), pdf::PDFAppMcpSetupClient::ClaudeCode);
    QVERIFY(page->getTile(pdf::PDFAppMcpSetupClient::ClaudeCode)->isSelected());
    QCOMPARE(page->getVariantChips().size(), 2);
    QTRY_COMPARE(page->getCodeBoxes().size(), 3);
    pdf::PDFCodeBox* command = page->getCodeBoxes().front();
    QVERIFY(command->isMonospace());
    QVERIFY(command->getText().contains(pdf::PDFAppMcpClientSetup::getDisplayToken(token)));
    QVERIFY(!command->getText().contains(token));
    QVERIFY(command->getCopyText().contains(token));
    QVERIFY(command->getCopyText().contains(address));
    QVERIFY(!page->getCodeBoxes().back()->isMonospace());
    command->copy();
    QCOMPARE(QGuiApplication::clipboard()->text(), command->getCopyText());
    page->getCopySetupButton()->click();
    QCOMPARE(QGuiApplication::clipboard()->text(), page->getSetup().copyText);
    QCOMPARE(page->getCopySetupButton()->text(), page->getSetup().copyButtonText);

    // Claude Desktop by its chip (the tile stays Claude)
    page->getVariantChips().at(1)->click();
    QTRY_COMPARE(page->getSetupClient(), pdf::PDFAppMcpSetupClient::ClaudeDesktop);
    QTRY_VERIFY(page->getVariantChips().size() == 2 && page->getVariantChips().at(1)->isChecked());
    QVERIFY(page->getTile(pdf::PDFAppMcpSetupClient::ClaudeCode)->isSelected());

    // Every tile
    for (pdf::PDFAppMcpSetupClient client : pdf::PDFAppMcpClientSetup::getTileClients())
    {
        pdf::PDFAssistantTile* tile = page->getTile(client);
        QVERIFY(tile);
        QCOMPARE(tile->getTile().name, pdf::PDFAppMcpClientSetup::getTileName(client));
        tile->click();
        pdftest::AppDocumentFixture::settle();
        QVERIFY(tile->isSelected());
        const pdf::PDFAppMcpSetup setup = page->getSetup();
        QVERIFY(!setup.title.isEmpty());
        QVERIFY(!page->getCodeBoxes().isEmpty());
        QCOMPARE(page->getCopySetupButton()->text(), setup.copyButtonText);
        QVERIFY(setup.copyText.contains(token) || client == pdf::PDFAppMcpSetupClient::Other);
        if (client == pdf::PDFAppMcpSetupClient::Other)
        {
            // 955:149138: no chips, the JSON configuration
            QVERIFY(page->getVariantChips().isEmpty());
            QVERIFY(QJsonDocument::fromJson(setup.copyText.toUtf8()).isObject());
            AppAiTestHelper::screenshot(fixture.window, QStringLiteral("ai-setup-other-client"));
        }
    }

    // Copy Command is Secondary while stopped
    fixture.controller()->stop();
    QTRY_VERIFY(!page->getCopySetupButton()->property("primary").toBool());

    // Require access token off: no header part
    QVERIFY(!AppAiTestHelper::settings(fixture).setValue(pdf::PDFSettingKeys::mcpRequireToken(), false).isError());
    page->getTile(pdf::PDFAppMcpSetupClient::ClaudeCode)->click();
    QTRY_VERIFY(!page->getSetup().copyText.contains(QStringLiteral("Authorization")));
    QTRY_VERIFY(!page->getCodeBoxes().front()->getText().contains(QStringLiteral("Authorization")));
}

/// A translator of the test: the setup texts of the clients twice as long
class AppAiSetupTestTranslator : public QTranslator
{
public:
    virtual QString translate(const char* context, const char* sourceText, const char* disambiguation, int n) const override
    {
        Q_UNUSED(disambiguation);
        Q_UNUSED(n);
        const QByteArray contextName(context ? context : "");
        if (contextName == "pdf::PDFAppMcpClientSetup" && sourceText)
        {
            const QString text = QString::fromUtf8(sourceText);
            return text + QStringLiteral(" ") + text;
        }
        return QString();
    }

    virtual bool isEmpty() const override { return false; }
};

/// Geometry of the setup card of Connect an assistant (owner 2026-10-29)
class AppAiSetupGeometry
{
public:
    QRect card;             ///< The setup card in the page
    QRect section;          ///< Connect an assistant in the page
    int pageHeight = 0;     ///< Height of the page (everything below the card)
    int titleTop = 0;       ///< The title in the card (the content at the top)
    int actionsGap = 0;     ///< From the steps to the buttons (no stretched gap)
    int natural = 0;        ///< Height of the shown setup at the width of the card

    bool isSamePlace(const AppAiSetupGeometry& other) const
    {
        return card == other.card && section == other.section && pageHeight == other.pageHeight && titleTop == other.titleTop && actionsGap == other.actionsGap;
    }

    /// The same card and the same space below it (the server card above it may
    /// change with the state of the server)
    bool isSameCard(const AppAiSetupGeometry& other) const
    {
        return card.size() == other.card.size() && pageHeight - card.bottom() == other.pageHeight - other.card.bottom() && titleTop == other.titleTop && actionsGap == other.actionsGap;
    }

    QString toString() const
    {
        return QStringLiteral("card %1,%2 %3x%4 section %5 page %6 title %7 gap %8 natural %9")
                .arg(card.x()).arg(card.y()).arg(card.width()).arg(card.height()).arg(section.height()).arg(pageHeight).arg(titleTop).arg(actionsGap).arg(natural);
    }
};

class AppAiSetupTestHelper
{
public:
    AppAiSetupTestHelper() = delete;

    /// Padding 16 and the border 1 of the setup card
    static constexpr int CARD_INSET = 17;

    static AppAiSetupGeometry measure(pdf::PDFAppAiPage* page)
    {
        AppAiSetupGeometry geometry;
        QWidget* card = page->getSetupCard();
        QWidget* section = page->findChild<QWidget*>(QStringLiteral("connectSection"));
        QWidget* content = page->findChild<QWidget*>(QStringLiteral("setupContent"));
        QWidget* title = page->findChild<QWidget*>(QStringLiteral("setupTitle"));
        QWidget* step = page->findChild<QWidget*>(QStringLiteral("setupStep1"));
        QWidget* copy = page->getCopySetupButton();
        if (!card || !section || !content || !title || !step || !copy)
        {
            return geometry;
        }
        geometry.card = QRect(card->mapTo(page, QPoint(0, 0)), card->size());
        geometry.section = QRect(section->mapTo(page, QPoint(0, 0)), section->size());
        geometry.pageHeight = page->height();
        geometry.titleTop = title->mapTo(card, QPoint(0, 0)).y();
        QWidget* steps = step->parentWidget();
        geometry.actionsGap = copy->mapTo(content, QPoint(0, 0)).y() - (steps->mapTo(content, QPoint(0, 0)).y() + steps->height());
        geometry.natural = content->heightForWidth(content->width());
        return geometry;
    }

    /// Settles, until the page and the setup card keep their heights and no
    /// measured setup is built again (a resized window lays the columns of the
    /// page out in more passes, a card, which got shorter, too - the page may keep
    /// the height of the viewport meanwhile; the setups are measured again from
    /// the event loop after a new width)
    static void settleLayout(pdf::PDFAppAiPage* page)
    {
        QList<int> previous;
        for (int i = 0; i < 20; ++i)
        {
            const QList<int> current = { page->height(), page->getSetupCard() ? page->getSetupCard()->height() : 0, page->getSetupMeasureBuildCount() };
            if (current == previous)
            {
                break;
            }
            previous = current;
            pdftest::AppDocumentFixture::settle();
        }
    }

    static void clickClient(pdf::PDFAppAiPage* page, pdf::PDFAppMcpSetupClient client)
    {
        if (client == pdf::PDFAppMcpSetupClient::ClaudeDesktop)
        {
            page->getTile(pdf::PDFAppMcpSetupClient::ClaudeCode)->click();
            pdftest::AppDocumentFixture::settle();
            page->getVariantChips().at(1)->click();
        }
        else
        {
            page->getTile(client)->click();
        }
        pdftest::AppDocumentFixture::settle();
    }

    static QList<pdf::PDFAppMcpSetupClient> getClients()
    {
        return { pdf::PDFAppMcpSetupClient::ClaudeCode, pdf::PDFAppMcpSetupClient::ClaudeDesktop, pdf::PDFAppMcpSetupClient::Codex, pdf::PDFAppMcpSetupClient::Gemini,
                 pdf::PDFAppMcpSetupClient::Copilot, pdf::PDFAppMcpSetupClient::Cursor, pdf::PDFAppMcpSetupClient::Other };
    }

    /// Switches through every client: the card and everything below it keep
    /// their places, no measured setup is built again; returns the geometry and
    /// the largest natural height of the shown setups (\p maximumNatural)
    static AppAiSetupGeometry checkSwitching(pdf::PDFAppAiPage* page, int* maximumNatural, const QString& screenshotPrefix = QString())
    {
        clickClient(page, pdf::PDFAppMcpSetupClient::ClaudeCode);
        settleLayout(page);
        const AppAiSetupGeometry first = measure(page);
        const int buildCount = page->getSetupMeasureBuildCount();
        [&]() {
            QVERIFY2(first.card.height() > 0, qPrintable(first.toString()));
            QCOMPARE(first.titleTop, CARD_INSET);
            QCOMPARE(first.actionsGap, 16);
        }();
        for (pdf::PDFAppMcpSetupClient client : getClients())
        {
            clickClient(page, client);
            const AppAiSetupGeometry geometry = measure(page);
            *maximumNatural = qMax(*maximumNatural, geometry.natural);
            [&]() {
                QCOMPARE(page->getSetupClient(), client);
                QVERIFY2(geometry.isSamePlace(first), qPrintable(QStringLiteral("client %1: %2 / first %3").arg(int(client)).arg(geometry.toString(), first.toString())));
                QVERIFY2(geometry.natural + 2 * CARD_INSET <= geometry.card.height(), qPrintable(geometry.toString()));
                QCOMPARE(page->getSetupMeasureBuildCount(), buildCount);
            }();
            if (QTest::currentTestFailed())
            {
                return first;
            }
            if (!screenshotPrefix.isEmpty())
            {
                screenshot(page->getSetupCard(), QStringLiteral("%1-%2").arg(screenshotPrefix).arg(int(client)));
            }
        }
        return first;
    }

    /// PDF4QT_APP_SCREENSHOTS: the widget at 1x and 2x
    static void screenshot(QWidget* widget, const QString& name)
    {
        const QString folder = qEnvironmentVariable("PDF4QT_APP_SCREENSHOTS");
        if (folder.isEmpty() || !widget)
        {
            return;
        }
        QDir().mkpath(folder);
        AppAiTestHelper::render(widget, 1.0).save(QDir(folder).absoluteFilePath(name + QStringLiteral(".png")));
        AppAiTestHelper::render(widget, 2.0).save(QDir(folder).absoluteFilePath(name + QStringLiteral("-2x.png")));
    }

    /// Vertical center (logical pixels) of the drawn pixels inside of the rectangle
    /// of the image, which differ from the background (-1 = none)
    static qreal inkCenter(const QImage& image, const QRect& logicalRect, const QColor& background)
    {
        const qreal ratio = image.devicePixelRatio();
        const QRect rect(qFloor(logicalRect.x() * ratio), qFloor(logicalRect.y() * ratio), qCeil(logicalRect.width() * ratio), qCeil(logicalRect.height() * ratio));
        int top = -1;
        int bottom = -1;
        for (int y = qMax(0, rect.top()); y <= qMin(image.height() - 1, rect.bottom()); ++y)
        {
            for (int x = qMax(0, rect.left()); x <= qMin(image.width() - 1, rect.right()); ++x)
            {
                const QColor color = image.pixelColor(x, y);
                if (qAbs(color.red() - background.red()) + qAbs(color.green() - background.green()) + qAbs(color.blue() - background.blue()) > 60)
                {
                    top = top < 0 ? y : top;
                    bottom = y;
                    break;
                }
            }
        }
        return top < 0 ? -1.0 : (qreal(top) + qreal(bottom + 1)) / 2.0 / ratio;
    }
};

void AppAiTest::setupCardReservedHeight_data()
{
    QTest::addColumn<QSize>("size");
    QTest::newRow("1440") << QSize(1440, 960);
    QTest::newRow("720x480") << QSize(720, 480);
}

void AppAiTest::setupCardReservedHeight()
{
    // Owner 2026-10-29: the setup card keeps the height of the tallest setup at
    // its width, so nothing below it moves, when the tiles are switched; a shorter
    // setup stays at the top of the card. Only the shown state is reserved (every
    // client with the shown address and token - coordinator 2026-10-29): the
    // card without the token has no empty area of the setups with the token, a
    // change of Require access token measures the setups again once, the state
    // of the server changes nothing
    QFETCH(QSize, size);
    pdftest::AppMcpFixture fixture(size);
    pdf::PDFAppAiPage* page = AppAiTestHelper::open(fixture);
    QVERIFY(page);
    pdftest::AppDocumentFixture::settle();
    QVERIFY(!page->getSetup().containsToken);
    QCOMPARE(page->getMeasuredSetupCount(), int(AppAiSetupTestHelper::getClients().size()));

    const QString name = QStringLiteral("ai-setup-card-%1").arg(size.width());
    int reservedHeights[2] = { 0, 0 };
    for (bool isTokenRequired : { false, true })
    {
        if (isTokenRequired)
        {
            const int buildCount = page->getSetupMeasureBuildCount();
            QVERIFY(!AppAiTestHelper::settings(fixture).setValue(pdf::PDFSettingKeys::mcpRequireToken(), true).isError());
            QTRY_COMPARE(page->getSetupMeasureBuildCount(), buildCount + 1);
            QVERIFY(page->getSetup().containsToken);
            AppAiSetupTestHelper::settleLayout(page);
            QCOMPARE(page->getSetupMeasureBuildCount(), buildCount + 1);
        }

        AppAiSetupGeometry reference;
        for (bool isRunning : { false, true })
        {
            if (isRunning)
            {
                QVERIFY(fixture.startServer());
                QTRY_COMPARE(page->getShownState(), pdf::PDFAppAiPage::ShownState::Running);
            }
            else if (page->getShownState() != pdf::PDFAppAiPage::ShownState::Stopped)
            {
                fixture.controller()->stop();
                QTRY_COMPARE(page->getShownState(), pdf::PDFAppAiPage::ShownState::Stopped);
            }
            AppAiSetupTestHelper::settleLayout(page);
            QCOMPARE(page->getSetup().containsToken, isTokenRequired);

            int maximumNatural = 0;
            const QString prefix = isRunning ? QString() : QStringLiteral("%1%2").arg(name, isTokenRequired ? QStringLiteral("-token") : QString());
            const AppAiSetupGeometry geometry = AppAiSetupTestHelper::checkSwitching(page, &maximumNatural, prefix);
            QVERIFY(!QTest::currentTestFailed());

            // The reserved height is the tallest setup of the shown state, not more
            QVERIFY2(geometry.card.height() == maximumNatural + 2 * AppAiSetupTestHelper::CARD_INSET,
                     qPrintable(QStringLiteral("running %1 token %2: %3 tallest %4").arg(isRunning).arg(isTokenRequired).arg(geometry.toString()).arg(maximumNatural)));
            if (isRunning)
            {
                QVERIFY2(geometry.isSameCard(reference), qPrintable(QStringLiteral("token %1: %2 / %3").arg(isTokenRequired).arg(geometry.toString(), reference.toString())));
            }
            reference = geometry;
        }
        reservedHeights[isTokenRequired ? 1 : 0] = reference.card.height();

        QWidget* stack = page->findChild<QWidget*>(QStringLiteral("setupStack"));
        QVERIFY(stack);
        QCOMPARE(page->getReservedSetupHeight(stack->width()), stack->height());
    }

    // The token adds the header of the commands: its own, taller reservation
    QVERIFY2(reservedHeights[1] > reservedHeights[0], qPrintable(QStringLiteral("token on %1 off %2").arg(reservedHeights[1]).arg(reservedHeights[0])));

    // Back without the token: the shorter reservation again, measured once
    const int buildCount = page->getSetupMeasureBuildCount();
    QVERIFY(!AppAiTestHelper::settings(fixture).setValue(pdf::PDFSettingKeys::mcpRequireToken(), false).isError());
    QTRY_COMPARE(page->getSetupMeasureBuildCount(), buildCount + 1);
    QVERIFY(!page->getSetup().containsToken);
    AppAiSetupTestHelper::settleLayout(page);
    QCOMPARE(page->getSetupMeasureBuildCount(), buildCount + 1);
    QCOMPARE(AppAiSetupTestHelper::measure(page).card.height(), reservedHeights[0]);
    AppAiTestHelper::screenshot(fixture.window, name);
}

void AppAiTest::setupCardReservedHeightRecomputed()
{
    // The reserved height follows the width and the language of the texts; a tile
    // switch builds nothing again (with the token - the tallest setups are shown).
    // The window starts narrow and grows: a page laid out wide keeps the minimal
    // width of its columns (PDFAppColumns, a separate finding)
    pdftest::AppMcpFixture fixture(QSize(800, 960));
    QVERIFY(!AppAiTestHelper::settings(fixture).setValue(pdf::PDFSettingKeys::mcpRequireToken(), true).isError());
    pdf::PDFAppAiPage* page = AppAiTestHelper::open(fixture);
    QVERIFY(page);
    pdftest::AppDocumentFixture::settle();

    // A narrow window: the steps and the note wrap, the actions are stacked
    int narrowNatural = 0;
    const AppAiSetupGeometry narrow = AppAiSetupTestHelper::checkSwitching(page, &narrowNatural);
    QVERIFY(!QTest::currentTestFailed());
    QCOMPARE(narrow.card.height(), narrowNatural + 2 * AppAiSetupTestHelper::CARD_INSET);

    // A wide window: the setups measured again at the new width
    const int buildCount = page->getSetupMeasureBuildCount();
    fixture.window->resize(1440, 960);
    AppAiSetupTestHelper::settleLayout(page);
    QVERIFY(page->getSetupCard()->width() > narrow.card.width());
    QVERIFY(page->getSetupMeasureBuildCount() > buildCount);
    int wideNatural = 0;
    const AppAiSetupGeometry wide = AppAiSetupTestHelper::checkSwitching(page, &wideNatural);
    QVERIFY(!QTest::currentTestFailed());
    QCOMPARE(wide.card.height(), wideNatural + 2 * AppAiSetupTestHelper::CARD_INSET);
    QVERIFY2(narrow.card.height() > wide.card.height(), qPrintable(narrow.toString() + QStringLiteral(" / ") + wide.toString()));

    // Longer texts of another language
    AppAiSetupTestTranslator translator;
    QCoreApplication::installTranslator(&translator);
    AppAiSetupTestHelper::settleLayout(page);
    int translatedNatural = 0;
    const AppAiSetupGeometry translated = AppAiSetupTestHelper::checkSwitching(page, &translatedNatural);
    QCoreApplication::removeTranslator(&translator);
    QVERIFY(!QTest::currentTestFailed());
    QCOMPARE(translated.card.height(), translatedNatural + 2 * AppAiSetupTestHelper::CARD_INSET);
    QVERIFY2(translated.card.height() > wide.card.height(), qPrintable(translated.toString() + QStringLiteral(" / ") + wide.toString()));
    AppAiSetupTestHelper::settleLayout(page);
    QCOMPARE(AppAiSetupTestHelper::measure(page).card.height(), wide.card.height());

    // Require access token off: the tallest setup without the token (only the
    // shown state is reserved)
    const int buildCountWithToken = page->getSetupMeasureBuildCount();
    QVERIFY(!AppAiTestHelper::settings(fixture).setValue(pdf::PDFSettingKeys::mcpRequireToken(), false).isError());
    QTRY_COMPARE(page->getSetupMeasureBuildCount(), buildCountWithToken + 1);
    QVERIFY(!page->getSetup().containsToken);
    AppAiSetupTestHelper::settleLayout(page);
    int withoutTokenNatural = 0;
    const AppAiSetupGeometry withoutToken = AppAiSetupTestHelper::checkSwitching(page, &withoutTokenNatural);
    QVERIFY(!QTest::currentTestFailed());
    QCOMPARE(withoutToken.card.height(), withoutTokenNatural + 2 * AppAiSetupTestHelper::CARD_INSET);
    QVERIFY2(withoutToken.card.height() < wide.card.height(), qPrintable(withoutToken.toString() + QStringLiteral(" / ") + wide.toString()));
}

void AppAiTest::setupNoteCentered_data()
{
    QTest::addColumn<bool>("isDark");
    QTest::addColumn<qreal>("scale");
    QTest::addColumn<bool>("isRightToLeft");
    QTest::addColumn<QSize>("size");
    for (bool isDark : { false, true })
    {
        for (qreal scale : { 1.0, 2.0 })
        {
            for (bool isRightToLeft : { false, true })
            {
                QTest::newRow(qPrintable(QStringLiteral("%1@%2%3").arg(isDark ? "dark" : "light").arg(scale).arg(isRightToLeft ? "-rtl" : ""))) << isDark << scale << isRightToLeft << QSize(1440, 960);
            }
        }
    }
    QTest::newRow("light@1-720x480") << false << 1.0 << false << QSize(720, 480);
}

void AppAiTest::setupNoteCentered()
{
    // 653:5797 / 955:149437: the info icon and the text of the note centered on
    // each other and on the buttons beside them (the text sat at the top of the
    // row); the privacy note 653:5679 and the callouts 940:136797 the same
    QFETCH(bool, isDark);
    QFETCH(qreal, scale);
    QFETCH(bool, isRightToLeft);
    QFETCH(QSize, size);

    if (isRightToLeft)
    {
        QApplication::setLayoutDirection(Qt::RightToLeft);
    }
    pdftest::AppMcpFixture fixture(size);
    pdf::PDFThemeManager::getInstance()->setScheme(isDark ? pdf::PDFThemeColors::Scheme::Dark : pdf::PDFThemeColors::Scheme::Light);
    pdf::PDFAppAiPage* page = AppAiTestHelper::open(fixture);
    QVERIFY(page);
    pdftest::AppDocumentFixture::settle();

    QWidget* card = page->getSetupCard();
    pdf::PDFAppParagraph* text = page->findChild<pdf::PDFAppParagraph*>(QStringLiteral("setupNoteText"));
    QWidget* icon = page->findChild<QWidget*>(QStringLiteral("setupNoteIcon"));
    QVERIFY(card && text && icon && text->isVisible());
    const QRect textRect(text->mapTo(card, QPoint(0, 0)), text->size());
    const QRect iconRect(icon->mapTo(card, QPoint(0, 0)), icon->size());
    const QRect copyRect(page->getCopySetupButton()->mapTo(card, QPoint(0, 0)), page->getCopySetupButton()->size());

    // The geometry: the lines of the text (a paragraph draws them from its top)
    // and the icon share their center
    const int lines = text->getLines(text->width()).size();
    const qreal textCenter = textRect.top() + lines * text->getLineHeight() / 2.0;
    const qreal iconCenter = iconRect.top() + iconRect.height() / 2.0;
    QVERIFY2(qAbs(textCenter - iconCenter) <= 0.5, qPrintable(QStringLiteral("text %1 icon %2 lines %3").arg(textCenter).arg(iconCenter).arg(lines)));
    const bool isBesideButtons = textRect.top() < copyRect.bottom();
    if (isBesideButtons)
    {
        // One line beside the buttons (653:5797): centered on them
        QCOMPARE(lines, 1);
        QVERIFY2(qAbs(textCenter - (copyRect.top() + copyRect.height() / 2.0)) <= 0.5, qPrintable(QStringLiteral("text %1 button %2").arg(textCenter).arg(copyRect.center().y())));
    }
    QVERIFY2(isBesideButtons == (size.width() >= 1440), qPrintable(QStringLiteral("text %1 copy %2").arg(textRect.top()).arg(copyRect.bottom())));

    // The drawn pixels: the ink of the text and of the icon centered on each other
    const QImage image = AppAiTestHelper::render(card, scale);
    const QColor background = pdf::PDFStylePainter::color(pdf::PDFThemeColors::BgSurface);
    const qreal textInk = AppAiSetupTestHelper::inkCenter(image, textRect, background);
    const qreal iconInk = AppAiSetupTestHelper::inkCenter(image, iconRect, background);
    QVERIFY(textInk > 0.0 && iconInk > 0.0);
    QVERIFY2(qAbs(textInk - iconInk) <= 1.5, qPrintable(QStringLiteral("text ink %1 icon ink %2").arg(textInk).arg(iconInk)));
    QCOMPARE(AppAiTestHelper::countLeakPixels(image), 0);
    AppAiSetupTestHelper::screenshot(card, QStringLiteral("ai-setup-note-%1").arg(QString::fromLatin1(QTest::currentDataTag()).replace(QLatin1Char('@'), QLatin1Char('-'))));

    // The privacy note 653:5679: the circle centered on its texts
    pdf::PDFTintedCard* note = page->getNote();
    QWidget* noteIcon = note->findChild<pdf::PDFIconBadge*>();
    QVERIFY(noteIcon);
    QWidget* noteTexts = nullptr;
    for (QWidget* child : note->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly))
    {
        noteTexts = child != noteIcon ? child : noteTexts;
    }
    QVERIFY(noteTexts);
    QVERIFY2(qAbs((noteIcon->y() + noteIcon->height() / 2.0) - (noteTexts->y() + noteTexts->height() / 2.0)) <= 0.5,
             qPrintable(QStringLiteral("icon %1 texts %2").arg(noteIcon->geometry().center().y()).arg(noteTexts->geometry().center().y())));

    // The callout of another instance 940:136797: the texts keep their height in
    // the middle of the callout (the wide window - in the minimal window the
    // server card gets less than its height for its width, a separate finding)
    if (size.width() < 1440)
    {
        return;
    }
    QVERIFY(AppAiTestHelper::claimOther(fixture, QStringLiteral("other"), QCoreApplication::applicationPid()));
    QTRY_COMPARE(page->getShownState(), pdf::PDFAppAiPage::ShownState::OtherInstance);
    pdftest::AppDocumentFixture::settle();
    pdf::PDFTintedCard* callout = page->getCallout();
    QVERIFY(callout->isVisible());
    QWidget* calloutTitle = callout->findChild<QWidget*>(QStringLiteral("calloutTitle"));
    QVERIFY(calloutTitle);
    QWidget* calloutTexts = calloutTitle->parentWidget();
    QVERIFY2(calloutTexts->height() == calloutTexts->heightForWidth(calloutTexts->width()),
             qPrintable(QStringLiteral("texts %1x%2 hfw %3 callout %4x%5 hfw %6 card %7x%8 hfw %9 content %10 hfw %11")
                                .arg(calloutTexts->width()).arg(calloutTexts->height()).arg(calloutTexts->heightForWidth(calloutTexts->width()))
                                .arg(callout->width()).arg(callout->height()).arg(callout->heightForWidth(callout->width()))
                                .arg(page->getServerCard()->width()).arg(page->getServerCard()->height()).arg(page->getServerCard()->heightForWidth(page->getServerCard()->width()))
                                .arg(callout->parentWidget()->height()).arg(callout->parentWidget()->heightForWidth(callout->parentWidget()->width()))));
    QVERIFY2(qAbs((calloutTexts->y() + calloutTexts->height() / 2.0) - callout->height() / 2.0) <= 1.0,
             qPrintable(QStringLiteral("texts %1 callout %2").arg(calloutTexts->geometry().center().y()).arg(callout->height() / 2.0)));
}

void AppAiTest::startWithApp()
{
    pdftest::AppMcpFixture fixture;
    QVERIFY(!AppAiTestHelper::settings(fixture).setValue(pdf::PDFSettingKeys::mcpPort(), int(pdftest::AppMcpFixture::findFreePort())).isError());
    QVERIFY(!AppAiTestHelper::settings(fixture).setValue(pdf::PDFSettingKeys::mcpStartWithApp(), true).isError());

    // A new controller of the context (as a new start of the application)
    pdf::PDFAppMcpController controller(fixture.context.get());
    controller.initialize();
    QTRY_COMPARE_WITH_TIMEOUT(controller.getState(), pdf::PDFAppMcpServerState::Running, AppAiTestHelper::timeout(10000));
    controller.shutdown();

    QVERIFY(!AppAiTestHelper::settings(fixture).setValue(pdf::PDFSettingKeys::mcpStartWithApp(), false).isError());
    pdf::PDFAppMcpController stopped(fixture.context.get());
    stopped.initialize();
    pdftest::AppDocumentFixture::settle();
    QCOMPARE(stopped.getState(), pdf::PDFAppMcpServerState::Stopped);
    stopped.shutdown();
}

void AppAiTest::heartbeatStale()
{
    pdftest::AppMcpFixture fixture;
    fixture.controller()->setHeartbeatInterval(50);
    pdf::PDFAppAiPage* page = AppAiTestHelper::open(fixture);
    QVERIFY(page);

    // An owner, whose heartbeat is older than 10 s: stopped, Start enabled
    const qint64 old = QDateTime::currentMSecsSinceEpoch() - 20000;
    QVERIFY(AppAiTestHelper::claimOther(fixture, QStringLiteral("stale"), QCoreApplication::applicationPid(), old));
    pdftest::AppDocumentFixture::settle();
    QTRY_COMPARE(page->getShownState(), pdf::PDFAppAiPage::ShownState::Stopped);
    QVERIFY(page->getStartStopButton()->isEnabled());

    // An owner, whose process has ended (a killed PDF4QT): its row is free for a start
    const qint64 deadPid = 0x7FFFFFF0;
    QVERIFY(!AppAiTestHelper::repository(fixture).releaseServer(QStringLiteral("stale")).isError());
    QVERIFY(AppAiTestHelper::claimOther(fixture, QStringLiteral("killed"), deadPid));
    QVERIFY(!AppAiTestHelper::settings(fixture).setValue(pdf::PDFSettingKeys::mcpPort(), int(pdftest::AppMcpFixture::findFreePort())).isError());
    pdftest::AppDocumentFixture::settle();
    page->getStartStopButton()->click();
    QTRY_COMPARE_WITH_TIMEOUT(page->getShownState(), pdf::PDFAppAiPage::ShownState::Running, AppAiTestHelper::timeout(10000));
    QVERIFY(fixture.controller()->isOwner());
}

void AppAiTest::showOtherWindow()
{
    pdftest::AppMcpFixture fixture;
    pdf::PDFAppAiPage* page = AppAiTestHelper::open(fixture);
    QVERIFY(page);
    QVERIFY(AppAiTestHelper::claimOther(fixture, QStringLiteral("other"), QCoreApplication::applicationPid()));
    QTRY_COMPARE(page->getShownState(), pdf::PDFAppAiPage::ShownState::OtherInstance);

    // Show Other Window posts the request for the owner
    page->getCalloutButton()->click();
    std::vector<pdf::PDFMcpRequestRow> requests = AppAiTestHelper::repository(fixture).takeRequests();
    QCOMPARE(requests.size(), size_t(1));
    QCOMPARE(requests.front().kind, pdf::PDFMcpRequestKind::ShowWindow);
}

void AppAiTest::anchors()
{
    pdftest::AppMcpFixture fixture;
    pdf::PDFAppAiPage* page = AppAiTestHelper::open(fixture, QStringLiteral("/ai#edit-documents"));
    QVERIFY(page);
    QVERIFY(page->findSettingRow(QStringLiteral("edit-documents"))->isHighlighted());

    // An unknown anchor or a query is Page Not Found
    QVERIFY(!page->setRoute(pdf::PDFAppRoute::parse(QStringLiteral("/ai#nothing"))));
    QVERIFY(!page->setRoute(pdf::PDFAppRoute::parse(QStringLiteral("/ai?x=1"))));
}

void AppAiTest::deletionMatrix()
{
    // The page, the window and the context deleted with the new token dialog open
    for (int victim = 0; victim < 3; ++victim)
    {
        pdftest::AppMcpFixture fixture;
        QPointer<pdf::PDFAppAiPage> page = AppAiTestHelper::open(fixture);
        QVERIFY(page);
        QVERIFY(page->openNewTokenDialog());
        QPointer<pdf::PDFAppAiNewTokenDialog> dialog = page->getNewTokenDialog();
        QTRY_VERIFY(dialog && dialog->isVisible());
        switch (victim)
        {
            case 0:
                delete page.data();
                break;
            case 1:
                delete fixture.window.data();
                break;
            case 2:
                fixture.controller()->shutdown();
                break;
        }
        pdftest::AppDocumentFixture::settle();
        if (victim == 2 && dialog)
        {
            // The controller is shut down: Create New Token does not crash
            dialog->getCreateButton()->click();
            pdftest::AppDocumentFixture::settle();
        }
        QTRY_VERIFY(!dialog || victim == 2);
    }

    // The page deleted while the server starts and with rows of every kind
    {
        pdftest::AppMcpFixture fixture;
        QPointer<pdf::PDFAppAiPage> page = AppAiTestHelper::open(fixture);
        QVERIFY(!AppAiTestHelper::repository(fixture).upsertClient(AppAiTestHelper::client(QStringLiteral("a:00000000000000000000000000000001"), QStringLiteral("Claude Code"))).isError());
        QVERIFY(!AppAiTestHelper::repository(fixture).addActivity({ AppAiTestHelper::activity(QStringLiteral("files.open"), pdf::PDFMcpActivityStatus::Waiting) }).isError());
        fixture.controller()->start();
        delete page.data();
        pdftest::AppDocumentFixture::settle();
        QTRY_COMPARE_WITH_TIMEOUT(fixture.controller()->getState(), pdf::PDFAppMcpServerState::Running, AppAiTestHelper::timeout(10000));
    }
}

void AppAiTest::deleteFromOwnSignals()
{
    // The window deleted synchronously from the signals of the controller, which the page follows
    const char* signalNames[] = { "stateChanged", "assistantsChanged", "activityChanged" };
    for (const char* signalName : signalNames)
    {
        pdftest::AppMcpFixture fixture;
        QPointer<pdf::PDFAppAiPage> page = AppAiTestHelper::open(fixture);
        QVERIFY(page);
        QPointer<pdf::PDFAppMainWindow> window = fixture.window;
        const QByteArray name(signalName);
        QMetaObject::Connection connection;
        if (name == "stateChanged")
        {
            connection = QObject::connect(fixture.controller(), &pdf::PDFAppMcpController::stateChanged, fixture.controller(), [window]() { delete window.data(); });
        }
        else if (name == "assistantsChanged")
        {
            connection = QObject::connect(fixture.controller(), &pdf::PDFAppMcpController::assistantsChanged, fixture.controller(), [window]() { delete window.data(); });
        }
        else
        {
            connection = QObject::connect(fixture.controller(), &pdf::PDFAppMcpController::activityChanged, fixture.controller(), [window]() { delete window.data(); });
        }

        // The page is connected before the test: the page gets the signal first, then the window goes
        if (name == "stateChanged")
        {
            page->getStartStopButton()->click();
        }
        else if (name == "assistantsChanged")
        {
            QVERIFY(!AppAiTestHelper::repository(fixture).upsertClient(AppAiTestHelper::client(QStringLiteral("a:00000000000000000000000000000001"), QStringLiteral("Claude Code"))).isError());
        }
        else
        {
            QVERIFY(!AppAiTestHelper::repository(fixture).addActivity({ AppAiTestHelper::activity(QStringLiteral("files.open"), pdf::PDFMcpActivityStatus::Ok) }).isError());
        }
        QTRY_VERIFY_WITH_TIMEOUT(!window, AppAiTestHelper::timeout(10000));
        QObject::disconnect(connection);
        QVERIFY(!page);
        fixture.controller()->stop();
    }

    // The page deleted from the click of a tile, a chip, Clear and the Start button
    {
        pdftest::AppMcpFixture fixture;
        QPointer<pdf::PDFAppAiPage> page = AppAiTestHelper::open(fixture);
        QObject::connect(page->getTile(pdf::PDFAppMcpSetupClient::Codex), &QAbstractButton::clicked, page, [page]() { page->deleteLater(); });
        page->getTile(pdf::PDFAppMcpSetupClient::Codex)->click();
        QTRY_VERIFY(!page);
    }
    {
        pdftest::AppMcpFixture fixture;
        QPointer<pdf::PDFAppAiPage> page = AppAiTestHelper::open(fixture);
        QPointer<QPushButton> chip = page->getVariantChips().at(1);
        QObject::connect(chip, &QAbstractButton::toggled, fixture.window, [window = fixture.window]() { window->deleteLater(); });
        chip->click();
        QTRY_VERIFY(!fixture.window);
    }
}

void AppAiTest::themesAndScales_data()
{
    QTest::addColumn<bool>("isDark");
    QTest::addColumn<qreal>("scale");
    for (bool isDark : { false, true })
    {
        for (qreal scale : { 1.0, 1.5, 2.0 })
        {
            QTest::newRow(qPrintable(QStringLiteral("%1@%2").arg(isDark ? "dark" : "light").arg(scale))) << isDark << scale;
        }
    }
}

void AppAiTest::themesAndScales()
{
    QFETCH(bool, isDark);
    QFETCH(qreal, scale);

    pdftest::AppMcpFixture fixture;
    pdf::PDFThemeManager::getInstance()->setScheme(isDark ? pdf::PDFThemeColors::Scheme::Dark : pdf::PDFThemeColors::Scheme::Light);
    pdf::PDFAppAiPage* page = AppAiTestHelper::open(fixture);
    QVERIFY(page);
    QVERIFY(!AppAiTestHelper::repository(fixture).addActivity({ AppAiTestHelper::activity(QStringLiteral("files.open"), pdf::PDFMcpActivityStatus::Failed) }).isError());
    QVERIFY(fixture.startServer());
    QVERIFY(!AppAiTestHelper::repository(fixture).upsertClient(AppAiTestHelper::client(QStringLiteral("a:00000000000000000000000000000001"), QStringLiteral("Claude Code"))).isError());
    QTRY_COMPARE(page->getAssistantRows().size(), 1);
    pdftest::AppDocumentFixture::settle();

    const QImage image = AppAiTestHelper::render(page, scale);
    QVERIFY2(AppAiTestHelper::countLeakPixels(image) == 0, qPrintable(AppAiTestHelper::describeLeak(page, image)));

    // The server card is accent/blue/bg while running
    const QPoint inside = page->getServerCard()->mapTo(page, QPoint(8, 8));
    const QColor color = image.pixelColor(qFloor(inside.x() * scale), qFloor(inside.y() * scale));
    const QColor expected = pdf::PDFStylePainter::color(pdf::PDFAccentHueColors::getColor(pdf::PDFAccentHue::Blue, pdf::PDFAccentHueColors::Role::Background));
    QVERIFY2(qAbs(color.red() - expected.red()) <= 4 && qAbs(color.green() - expected.green()) <= 4 && qAbs(color.blue() - expected.blue()) <= 4, qPrintable(color.name()));

    if (scale == 1.0)
    {
        AppAiTestHelper::screenshot(fixture.window, QStringLiteral("ai-%1").arg(isDark ? "dark" : "light"));
    }
}

void AppAiTest::rightToLeft()
{
    pdftest::AppMcpFixture fixture;
    QApplication::setLayoutDirection(Qt::RightToLeft);
    pdf::PDFAppAiPage* page = AppAiTestHelper::open(fixture);
    QVERIFY(page);
    pdftest::AppDocumentFixture::settle();

    // The main column at the right edge, the side column at the left
    const QRect server = QRect(page->getServerCard()->mapTo(page, QPoint(0, 0)), page->getServerCard()->size());
    const QWidget* note = page->getNote();
    const QRect noteRect = QRect(note->mapTo(page, QPoint(0, 0)), note->size());
    QVERIFY(noteRect.right() < server.left());
    QCOMPARE(AppAiTestHelper::countLeakPixels(AppAiTestHelper::render(page, 1.0)), 0);
    AppAiTestHelper::screenshot(fixture.window, QStringLiteral("ai-rtl"));
}

void AppAiTest::minimalWindow()
{
    pdftest::AppMcpFixture fixture(QSize(720, 480));
    pdf::PDFAppAiPage* page = AppAiTestHelper::open(fixture);
    QVERIFY(page);
    QVERIFY(fixture.startServer());
    pdftest::AppDocumentFixture::settle();

    // No horizontal scroll bar: every part fits the width of the page
    QScrollArea* area = page->getScrollArea();
    QVERIFY(area);
    QVERIFY(area->horizontalScrollBar()->maximum() == 0);
    for (QWidget* widget : { static_cast<QWidget*>(page->getServerCard()), static_cast<QWidget*>(page->getNote()), static_cast<QWidget*>(page->getCopySetupButton()) })
    {
        const QRect rect(widget->mapTo(page, QPoint(0, 0)), widget->size());
        QVERIFY2(rect.right() <= page->width(), widget->metaObject()->className());
    }
    for (pdf::PDFCodeBox* box : page->getCodeBoxes())
    {
        QVERIFY(box->mapTo(page, QPoint(box->width(), 0)).x() <= page->width());
    }
    AppAiTestHelper::screenshot(fixture.window, QStringLiteral("ai-minimal"));
}

void AppAiTest::keyboardAndAccessibility()
{
    pdftest::AppMcpFixture fixture;
    pdf::PDFAppAiPage* page = AppAiTestHelper::open(fixture);
    QVERIFY(page);

    // Accessible names of the fields and buttons without a text
    QCOMPARE(page->getAddressField()->accessibleName(), QStringLiteral("Server address"));
    QCOMPARE(page->getTokenField()->accessibleName(), QStringLiteral("Access token"));
    QVERIFY(!page->getCopyTokenButton()->accessibleName().isEmpty());
    QVERIFY(!page->getNewTokenButton()->accessibleName().isEmpty());
    QCOMPARE(page->getServerCard()->accessibleDescription(), page->getStateText());

    // The tiles: one stop of Tab, the arrows choose
    pdf::PDFAssistantTile* claude = page->getTile(pdf::PDFAppMcpSetupClient::ClaudeCode);
    claude->setFocus(Qt::TabFocusReason);
    QTest::keyClick(claude, Qt::Key_Right);
    pdftest::AppDocumentFixture::settle();
    QTRY_COMPARE(page->getSetupClient(), pdf::PDFAppMcpSetupClient::Codex);

    // The new token button by the keyboard
    page->getNewTokenButton()->setFocus(Qt::TabFocusReason);
    QTest::keyClick(page->getNewTokenButton(), Qt::Key_Space);
    QTRY_VERIFY(page->getNewTokenDialog() && page->getNewTokenDialog()->isVisible());
    QTest::keyClick(page->getNewTokenDialog(), Qt::Key_Escape);
    QTRY_VERIFY(!page->getNewTokenDialog());
}

void AppAiTest::languageChange()
{
    pdftest::AppMcpFixture fixture;
    pdf::PDFAppAiPage* page = AppAiTestHelper::open(fixture);
    QVERIFY(page);
    page->setSetupClient(pdf::PDFAppMcpSetupClient::Gemini);
    QVERIFY(page->openNewTokenDialog());

    AppAiTestTranslator translator;
    QCoreApplication::installTranslator(&translator);
    pdftest::AppDocumentFixture::settle();
    QCOMPARE(page->getStartStopButton()->text(), QStringLiteral("~Start Server"));
    QCOMPARE(page->getSetupClient(), pdf::PDFAppMcpSetupClient::Gemini);
    QVERIFY(page->getTile(pdf::PDFAppMcpSetupClient::Gemini)->isSelected());
    QCoreApplication::removeTranslator(&translator);
    pdftest::AppDocumentFixture::settle();
    QCOMPARE(page->getStartStopButton()->text(), QStringLiteral("Start Server"));
}

int main(int argc, char* argv[])
{
    // Child processes of the tests of two instances (AppMcpFixture::secondInstance)
    const int childResult = pdftest::AppMcpFixture::runChild(argc, argv);
    if (childResult >= 0)
    {
        return childResult;
    }

    QApplication application(argc, argv);
    application.setAttribute(Qt::AA_Use96Dpi, true);
    AppAiTest test;
    QTEST_SET_MAIN_SOURCE_PATH
    return QTest::qExec(&test, argc, argv);
}

#include "tst_appaitest.moc"
