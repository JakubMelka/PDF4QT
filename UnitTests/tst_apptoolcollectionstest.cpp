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

#include "apptoolsfixture.h"

#include "pdfappbuiltintoolcollections.h"
#include "pdfappcollectioniconchooser.h"
#include "pdfappcollectiontoollist.h"
#include "pdfappdocumentinfobar.h"
#include "pdfapphomedialogs.h"
#include "pdfapphomewidgets.h"
#include "pdfappmainwindow.h"
#include "pdfappnavigator.h"
#include "pdfapppagenotfound.h"
#include "pdfapptab.h"
#include "pdfapptoolcollectioneditor.h"
#include "pdfapptoolcollections.h"
#include "pdfapptoolcollectionspage.h"
#include "pdfapptoolstate.h"
#include "pdfappworkspace.h"
#include "pdfdatabase.h"

#include "pdfcomponentbuttons.h"
#include "pdfcomponentrows.h"
#include "pdfdocumenttoolcatalog.h"
#include "pdfdocumenttoolsuggestions.h"
#include "pdffluentwidgets.h"
#include "pdfstylepainter.h"
#include "pdfthemecolors.h"
#include "pdfthememanager.h"
#include "pdfwidgetstyle.h"

#include <QAccessible>
#include <QApplication>
#include <QElapsedTimer>
#include <QMimeData>
#include <QLabel>
#include <QPushButton>
#include <QRandomGenerator>
#include <QScrollArea>
#include <QScrollBar>
#include <QTextDocument>
#include <QTranslator>
#include <QtTest>

/// Translator of the tests (context, source text) -> translation
class CollectionsTestTranslator : public QTranslator
{
public:
    virtual QString translate(const char* context, const char* sourceText, const char* disambiguation, int n) const override
    {
        Q_UNUSED(disambiguation);
        Q_UNUSED(n);
        return entries.value(qMakePair(QByteArray(context ? context : ""), QByteArray(sourceText ? sourceText : "")));
    }

    virtual bool isEmpty() const override { return false; }

    QHash<QPair<QByteArray, QByteArray>, QString> entries;
};

/// Helpers of the tests of the collections page
class CollectionsTestHelper
{
public:
    CollectionsTestHelper() = delete;

    static pdf::PDFAppToolCollectionsPage* open(pdftest::AppToolsFixture& fixture, const QString& route)
    {
        fixture.openTools(route);
        return fixture.page<pdf::PDFAppToolCollectionsPage>();
    }

    static pdf::PDFAppToolCollectionEditor* editor(pdftest::AppToolsFixture& fixture)
    {
        pdf::PDFAppToolCollectionsPage* page = fixture.page<pdf::PDFAppToolCollectionsPage>();
        return page ? page->getEditor() : nullptr;
    }

    static void activate(pdftest::AppToolsFixture& fixture)
    {
        fixture.window->activateWindow();
        if (!QTest::qWaitForWindowActive(fixture.window.data()))
        {
            qWarning("The window is not active");
        }
    }

    /// Replaces the name as the user types it
    static void typeName(pdf::PDFAppToolCollectionEditor* editor, const QString& text)
    {
        pdf::PDFLineEdit* field = editor->getNameField();
        field->setFocus(Qt::OtherFocusReason);
        field->selectAll();
        QTest::keyClick(field, Qt::Key_Backspace);
        bool isAscii = true;
        for (QChar character : text)
        {
            isAscii = isAscii && character.unicode() >= 0x20 && character.unicode() < 0x7F;
        }
        if (isAscii)
        {
            QTest::keyClicks(field, text);
        }
        else
        {
            // QTest types only ASCII; insert edits the text as the user
            field->insert(text);
        }
    }

    static bool execute(pdftest::AppToolsFixture& fixture, const char* sql)
    {
        pdf::PDFDatabaseConnection* connection = fixture.context->getDatabase()->getConnection();
        return connection && !connection->execute(sql).isError();
    }

    /// Creates a custom collection by this connection
    static qint64 create(pdftest::AppToolsFixture& fixture, const QString& name, const QStringList& tools, const QString& icon = QString())
    {
        qint64 id = 0;
        const pdf::PDFDatabaseError error = fixture.collections().createCollection(name, icon, tools, &id);
        return error.isError() ? 0 : id;
    }

    /// Creates a custom collection by this connection with the time of its change
    /// (an old time: a change by another connection has always another time)
    static qint64 createAt(pdftest::AppToolsFixture& fixture, const QString& name, const QStringList& tools, qint64 time)
    {
        qint64 id = 0;
        pdf::PDFToolCollectionRepository repository = fixture.collections();
        repository.setCurrentTime(time);
        const pdf::PDFDatabaseError error = repository.createCollection(name, QString(), tools, &id);
        return error.isError() ? 0 : id;
    }

    static QImage render(QWidget* widget, qreal devicePixelRatio)
    {
        QImage image(QSize(qCeil(widget->width() * devicePixelRatio), qCeil(widget->height() * devicePixelRatio)), QImage::Format_ARGB32_Premultiplied);
        image.setDevicePixelRatio(devicePixelRatio);
        image.fill(pdf::PDFStylePainter::color(pdf::PDFThemeColors::BgCanvas));
        widget->render(&image, QPoint(), QRegion(), QWidget::RenderFlags(QWidget::DrawChildren));
        return image;
    }

    /// The widget at the first leak pixel of the image of the widget (diagnostics)
    static QString describeLeak(QWidget* widget, const QImage& image)
    {
        const QRgb leak = pdf::PDFThemeColors::getLeakColor().rgb() & 0xFFFFFF;
        const QImage converted = image.convertToFormat(QImage::Format_RGB32);
        for (int y = 0; y < converted.height(); ++y)
        {
            for (int x = 0; x < converted.width(); ++x)
            {
                if ((converted.pixel(x, y) & 0xFFFFFF) == leak)
                {
                    const QPoint point = QPoint(x, y) / image.devicePixelRatio();
                    QWidget* child = widget->childAt(point);
                    QString path;
                    for (QWidget* item = child; item && item != widget; item = item->parentWidget())
                    {
                        path += QStringLiteral("%1(%2) < ").arg(QString::fromLatin1(item->metaObject()->className()), item->objectName());
                    }
                    if (!qEnvironmentVariable("PDF4QT_COLLECTIONS_LEAK_DUMP").isEmpty())
                    {
                        image.save(qEnvironmentVariable("PDF4QT_COLLECTIONS_LEAK_DUMP"));
                    }
                    return QStringLiteral("leak at %1 %2: %3").arg(point.x()).arg(point.y()).arg(path);
                }
            }
        }
        return QString();
    }

    static int countLeakPixels(const QImage& image)
    {
        const QRgb leak = pdf::PDFThemeColors::getLeakColor().rgb() & 0xFFFFFF;
        int count = 0;
        const QImage converted = image.convertToFormat(QImage::Format_RGB32);
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

    static QStringList listRows(pdf::PDFAppToolCollectionsPage* page)
    {
        QStringList ids;
        QWidget* card = page->findChild<QWidget*>(QStringLiteral("collectionsList"));
        if (!card || !card->layout())
        {
            return ids;
        }
        for (int i = 0; i < card->layout()->count(); ++i)
        {
            if (pdf::PDFAppToolCollectionListRow* row = qobject_cast<pdf::PDFAppToolCollectionListRow*>(card->layout()->itemAt(i)->widget()))
            {
                ids << row->getRouteId();
            }
        }
        return ids;
    }

    /// Sends a drag of the data to the list and drops it at the point
    static bool drop(pdf::PDFAppCollectionToolList* list, QMimeData* mimeData, const QPoint& point, int* indicator = nullptr)
    {
        QDragEnterEvent enter(point, Qt::MoveAction, mimeData, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(list, &enter);
        if (indicator)
        {
            *indicator = list->getDropIndicatorIndex();
        }
        if (!enter.isAccepted())
        {
            QDragLeaveEvent leave;
            QApplication::sendEvent(list, &leave);
            return false;
        }

        QDropEvent dropEvent(QPointF(point), Qt::MoveAction, mimeData, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(list, &dropEvent);
        return dropEvent.isAccepted();
    }

    static bool hasText(QWidget* parent, const QString& text)
    {
        for (QLabel* label : parent->findChildren<QLabel*>())
        {
            if (label->text() == text)
            {
                return true;
            }
        }
        return false;
    }

    static QPushButton* button(QWidget* parent, const QString& name)
    {
        return parent ? parent->findChild<QPushButton*>(name) : nullptr;
    }

    static QStringList fixAScan()
    {
        return { QStringLiteral("pages.rotate"), QStringLiteral("pages.crop"), QStringLiteral("pages.interleave"), QStringLiteral("pages.remove") };
    }
};

/// The collections page of the Tools catalog (step 6, package B; Figma Manage
/// collections 283:1888)
class AppToolCollectionsTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void cleanup();

    void collectionsPageSmoke();
    void collectionsList();
    void editorGeometry();
    void createAndSave();
    void saveDisabledStates();
    void duplicateNames();
    void renameAndIcon();
    void reorderDragAndDrop();
    void foreignDropRefused();
    void reorderKeyboard();
    void removeTool();
    void addToolSearch();
    void suggestions();
    void cancelAndReset();
    void deleteCustom();
    void builtinNotDeletable();
    void draftInHistory();
    void otherInstanceWhileEditing();
    void limitsAndRouteIds();
    void performance();
    void hostileStoredRows();
    void deletionMatrix();
    void deleteFromOwnSignals();
    void themesAndScales();
    void rightToLeft();
    void minimalWindow();
    void keyboardAndAccessibility();
    void languageChange();
};

void AppToolCollectionsTest::initTestCase()
{
    QApplication::setStyle(new pdf::PDFWidgetStyle());
    QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedStates));
    pdf::PDFThemeManager::getInstance()->setScheme(pdf::PDFThemeColors::Scheme::Light);
}

void AppToolCollectionsTest::cleanup()
{
    pdf::PDFThemeManager::getInstance()->setScheme(pdf::PDFThemeColors::Scheme::Light);
    QApplication::setLayoutDirection(Qt::LeftToRight);
    pdf::PDFAppHomeFormat::setCurrentTimeOverride(0);
}

void AppToolCollectionsTest::collectionsPageSmoke()
{
    pdftest::AppToolsFixture fixture;

    // /tools/collections shows the first collection
    fixture.openTools(QStringLiteral("/tools/collections"));
    pdf::PDFAppToolCollectionsPage* page = fixture.page<pdf::PDFAppToolCollectionsPage>();
    QVERIFY(page);
    QCOMPARE(page->getCollectionId(), QStringLiteral("prepare-for-sharing"));
    QCOMPARE(fixture.currentRoute(), QStringLiteral("/tools/collections/prepare-for-sharing"));
    QCOMPARE(page->getToolIds().size(), 3);

    // A new collection with its first tool
    fixture.openTools(QStringLiteral("/tools/collections/new?tool=pages.merge"));
    page = fixture.page<pdf::PDFAppToolCollectionsPage>();
    QVERIFY(page && page->isNewCollection());
    QCOMPARE(page->getToolIds(), QStringList({ QStringLiteral("pages.merge") }));

    // A custom collection written by another instance
    pdf::PDFToolCollectionDraft draft;
    draft.name = QStringLiteral("Invoices");
    draft.toolIds = { QStringLiteral("optimize.compress") };
    qint64 id = 0;
    QVERIFY(fixture.writeCollectionFromOtherConnection(draft, &id));
    fixture.openTools(QStringLiteral("/tools/collections/custom-%1").arg(id));
    page = fixture.page<pdf::PDFAppToolCollectionsPage>();
    QVERIFY(page);
    QCOMPARE(page->getToolIds(), draft.toolIds);
}

void AppToolCollectionsTest::collectionsList()
{
    pdftest::AppToolsFixture fixture;
    pdf::PDFAppToolCollectionsPage* page = CollectionsTestHelper::open(fixture, QStringLiteral("/tools/collections/fix-a-scan"));
    QVERIFY(page);

    // The built-in group in the order of 283:2346, the custom group hidden without a custom collection
    QStringList builtins;
    for (const pdf::PDFAppBuiltinToolCollection& collection : pdf::PDFAppBuiltinToolCollections::getCollections())
    {
        builtins << collection.getId();
    }
    QCOMPARE(CollectionsTestHelper::listRows(page), builtins);
    QCOMPARE(page->getCountHint()->text(), QStringLiteral("7 built-in · 0 custom"));
    QVERIFY(page->findChild<QWidget*>(QStringLiteral("customGroup"))->isHidden());
    QCOMPARE(page->findChild<pdf::PDFLabel*>(QStringLiteral("builtinGroup"))->text(), QStringLiteral("Built-in"));
    QCOMPARE(page->findChild<pdf::PDFLabel*>(QStringLiteral("builtinGroup"))->font().capitalization(), QFont::AllUppercase);

    // The shown collection is selected (Pressed of the design), the meta texts are counts
    pdf::PDFAppToolCollectionListRow* fixRow = page->findListRow(QStringLiteral("fix-a-scan"));
    QVERIFY(fixRow && fixRow->isChecked());
    QVERIFY(!page->findListRow(QStringLiteral("prepare-for-sharing"))->isChecked());
    QCOMPARE(fixRow->getMeta(), QStringLiteral("4 tools"));
    QCOMPARE(fixRow->getIconName(), QStringLiteral("rotate-cw"));
    QCOMPARE(page->findListRow(QStringLiteral("prepare-for-sharing"))->getMeta(), QStringLiteral("3 tools"));

    // Custom collections of another instance join the list
    pdf::PDFToolCollectionDraft draft;
    draft.name = QStringLiteral("Monthly invoices");
    draft.iconName = QStringLiteral("folder");
    draft.toolIds = { QStringLiteral("pages.merge"), QStringLiteral("optimize.compress") };
    qint64 id = 0;
    QVERIFY(fixture.writeCollectionFromOtherConnection(draft, &id));
    QCOMPARE(page->getCountHint()->text(), QStringLiteral("7 built-in · 1 custom"));
    QVERIFY(!page->findChild<QWidget*>(QStringLiteral("customGroup"))->isHidden());
    pdf::PDFAppToolCollectionListRow* customRow = page->findListRow(QStringLiteral("custom-%1").arg(id));
    QVERIFY(customRow);
    QCOMPARE(customRow->getName(), QStringLiteral("Monthly invoices"));
    QCOMPARE(customRow->getMeta(), QStringLiteral("2 tools · edited today"));
    QCOMPARE(CollectionsTestHelper::listRows(page).last(), QStringLiteral("custom-%1").arg(id));

    // Meta texts by the time of the last change (O17: the language of the user interface)
    const qint64 now = QDateTime(QDate(2026, 10, 20), QTime(12, 0)).toMSecsSinceEpoch();
    pdf::PDFAppToolCollection collection;
    collection.toolIds = { QStringLiteral("pages.merge") };
    QCOMPARE(pdf::PDFAppToolCollectionsPage::getListMetaText(collection, now), QStringLiteral("1 tool"));
    collection.modified = QDateTime(QDate(2026, 10, 19), QTime(9, 0)).toMSecsSinceEpoch();
    QCOMPARE(pdf::PDFAppToolCollectionsPage::getListMetaText(collection, now), QStringLiteral("1 tool · edited yesterday"));
    collection.modified = QDateTime(QDate(2026, 9, 12), QTime(9, 0)).toMSecsSinceEpoch();
    QCOMPARE(pdf::PDFAppToolCollectionsPage::getListMetaText(collection, now), QStringLiteral("1 tool · edited Sep 12"));
    collection.modified = QDateTime(QDate(2025, 9, 12), QTime(9, 0)).toMSecsSinceEpoch();
    QCOMPARE(pdf::PDFAppToolCollectionsPage::getListMetaText(collection, now), QStringLiteral("1 tool · edited Sep 12, 2025"));
    collection.modified = std::numeric_limits<qint64>::max();
    QVERIFY(pdf::PDFAppToolCollectionsPage::getListMetaText(collection, now).startsWith(QStringLiteral("1 tool")));
    collection.toolIds.clear();
    collection.modified = 0;
    QCOMPARE(pdf::PDFAppToolCollectionsPage::getListMetaText(collection, now), QStringLiteral("0 tools"));

    // A click on a collection is a new entry of the history
    QTest::mouseClick(page->findListRow(QStringLiteral("sign-a-contract")), Qt::LeftButton);
    fixture.settle();
    QCOMPARE(fixture.currentRoute(), QStringLiteral("/tools/collections/sign-a-contract"));
    page = fixture.page<pdf::PDFAppToolCollectionsPage>();
    QVERIFY(page && page->findListRow(QStringLiteral("sign-a-contract"))->isChecked());
    QVERIFY(fixture.window->getNavigator()->goBack());
    fixture.settle();
    QCOMPARE(fixture.currentRoute(), QStringLiteral("/tools/collections/fix-a-scan"));

    // A click on the shown collection does nothing; New collection opens the empty editor
    page = fixture.page<pdf::PDFAppToolCollectionsPage>();
    QTest::mouseClick(page->findListRow(QStringLiteral("fix-a-scan")), Qt::LeftButton);
    fixture.settle();
    QCOMPARE(fixture.currentRoute(), QStringLiteral("/tools/collections/fix-a-scan"));
    QTest::mouseClick(page->getNewButton(), Qt::LeftButton);
    fixture.settle();
    QCOMPARE(fixture.currentRoute(), QStringLiteral("/tools/collections/new"));
    page = fixture.page<pdf::PDFAppToolCollectionsPage>();
    QVERIFY(page && page->isNewCollection());
    for (const QString& routeId : CollectionsTestHelper::listRows(page))
    {
        QVERIFY(!page->findListRow(routeId)->isChecked());
    }
}

void AppToolCollectionsTest::editorGeometry()
{
    pdftest::AppToolsFixture fixture;
    pdf::PDFAppToolCollectionsPage* page = CollectionsTestHelper::open(fixture, QStringLiteral("/tools/collections/fix-a-scan"));
    QVERIFY(page);
    pdf::PDFAppToolCollectionEditor* editor = page->getEditor();
    fixture.settle();

    // collections-grid 283:1939: the list 340, gap 24, the editor 802 (the page 1166;
    // the page scrolls at 960 px, the vertical scroll bar takes its width)
    QVERIFY(page->width() <= 1166 && page->width() >= 1150);
    QCOMPARE(page->getListColumn()->width(), 340);
    QCOMPARE(editor->width(), page->width() - 340 - 24);
    QCOMPARE(editor->mapTo(page, QPoint(0, 0)).x() - page->getListColumn()->mapTo(page, QPoint(0, 0)).x(), 340 + 24);

    // The header of the page (title-row 283:1932): the Tool icon 44 "layers" in Neutral
    // at x 242 of the window, the title and the subtitle at x 300 (gap 14), the icon
    // centered on them; the breadcrumbs stay at x 242
    pdf::PDFAppWorkspaceHeader* header = fixture.window->findChild<pdf::PDFAppWorkspaceHeader*>();
    QVERIFY(header);
    pdf::PDFToolBadge* headerIcon = header->getIconBadge();
    QVERIFY(headerIcon && headerIcon->isVisible());
    QCOMPARE(headerIcon->size(), QSize(44, 44));
    QCOMPARE(headerIcon->getIconName(), QStringLiteral("layers"));
    QCOMPARE(headerIcon->getHue(), pdf::PDFAccentHue::Neutral);
    QCOMPARE(headerIcon->mapTo(fixture.window, QPoint(0, 0)).x(), 242);
    QCOMPARE(header->getTitleLabel()->mapTo(fixture.window, QPoint(0, 0)).x(), 300);
    QCOMPARE(header->getSubtitleLabel()->mapTo(fixture.window, QPoint(0, 0)).x(), 300);
    QCOMPARE(header->getBreadcrumb()->mapTo(fixture.window, QPoint(0, 0)).x(), 242);
    const int textsTop = header->getTitleLabel()->mapTo(fixture.window, QPoint(0, 0)).y();
    const int textsBottom = header->getSubtitleLabel()->mapTo(fixture.window, QPoint(0, 0)).y() + header->getSubtitleLabel()->height();
    QVERIFY(qAbs(headerIcon->mapTo(fixture.window, QPoint(0, 0)).y() + 22 - (textsTop + textsBottom) / 2) <= 1);

    // The list: rows 52 high (padding 8 inside of the border), New collection 36 high, 12 below
    pdf::PDFAppToolCollectionListRow* row = page->findListRow(QStringLiteral("prepare-for-sharing"));
    QCOMPARE(row->height(), 52);
    QCOMPARE(row->width(), 340 - 2 * 9);
    QCOMPARE(page->getNewButton()->height(), 36);
    QCOMPARE(page->getNewButton()->width(), 340);

    // The header of the editor: Semi Bold 15, the tag Built-in, Reset to default
    QCOMPARE(editor->getTitleLabel()->text(), QStringLiteral("Fix a scan"));
    QCOMPARE(editor->getTitleLabel()->font().pixelSize(), 15);
    QCOMPARE(editor->getTitleLabel()->font().weight(), QFont::DemiBold);
    QVERIFY(editor->getBuiltinTag()->isVisible());
    QCOMPARE(editor->getBuiltinTag()->text(), QStringLiteral("Built-in"));
    QVERIFY(editor->getResetLink()->isVisible());
    QVERIFY(editor->getResetLink()->isEnabled());

    // The card: padding 20 inside of the border, 12 below the header
    QCOMPARE(editor->getCard()->layout()->contentsMargins(), QMargins(21, 21, 21, 21));
    QCOMPARE(editor->getCard()->width(), editor->width());

    // The name field 34 high, the icons 40 (selected) and 36, gap 6
    QCOMPARE(editor->getNameField()->height(), 34);
    QCOMPARE(editor->getNameField()->text(), QStringLiteral("Fix a scan"));
    pdf::PDFAppCollectionIconChooser* chooser = editor->getIconChooser();
    QCOMPARE(chooser->getIconName(), QStringLiteral("rotate-cw"));
    QCOMPARE(chooser->getChoice(QStringLiteral("rotate-cw"))->size(), QSize(40, 40));
    QCOMPARE(chooser->getChoice(QStringLiteral("camera"))->size(), QSize(36, 36));
    QCOMPARE(chooser->getChoice(QStringLiteral("camera"))->geometry().left() - chooser->getChoice(QStringLiteral("rotate-cw"))->geometry().right() - 1, 6);
    QVERIFY(chooser->mapTo(editor, QPoint(0, 0)).x() > editor->getNameField()->mapTo(editor, QPoint(0, 0)).x() + 260);

    // The tools: rows 48 high, the hint "4 tools · drag to reorder · shown in this order"
    pdf::PDFAppCollectionToolList* list = editor->getToolList();
    QCOMPARE(list->getTools(), CollectionsTestHelper::fixAScan());
    QCOMPARE(editor->getToolsHint()->text(), QStringLiteral("4 tools · drag to reorder · shown in this order"));
    QCOMPARE(list->findToolRow(QStringLiteral("pages.rotate"))->height(), 48);
    QVERIFY(!list->findToolRow(QStringLiteral("pages.rotate"))->isStarVisible());
    QCOMPARE(list->findRow(QStringLiteral("pages.crop"))->geometry().top() - list->findRow(QStringLiteral("pages.rotate"))->geometry().bottom() - 1, 2);
    QCOMPARE(list->findRow(QStringLiteral("pages.rotate"))->geometry().top(), 5);
    QCOMPARE(list->findHandle(QStringLiteral("pages.rotate"))->getIconName(), QStringLiteral("more-vertical"));
    QCOMPARE(list->findRemoveButton(QStringLiteral("pages.rotate"))->getIconName(), QStringLiteral("x"));
    QVERIFY(list->getEmptyLabel()->isHidden());

    // Add a tool: the search 36 high, the suggestions of 48
    QCOMPARE(editor->getSearchField()->placeholderText(), QStringLiteral("Search the catalog by name, action or keyword…"));
    QCOMPARE(editor->getOfferCaption()->text(), QStringLiteral("Suggested for this collection"));
    QVERIFY(!editor->getOfferedTools().isEmpty());
    QCOMPARE(editor->findOfferedToolRow(editor->getOfferedTools().front())->height(), 48);

    // The actions: Delete (disabled for a built-in collection) at the leading edge,
    // Cancel and Save at the trailing edge, 36 high
    QPushButton* deleteButton = editor->getDeleteButton();
    QVERIFY(!deleteButton->isEnabled());
    QCOMPARE(deleteButton->text(), QStringLiteral("Delete collection"));
    QCOMPARE(editor->getSaveButton()->text(), QStringLiteral("Save collection"));
    QCOMPARE(editor->getSaveButton()->height(), 36);
    QVERIFY(deleteButton->mapTo(editor, QPoint(0, 0)).x() < editor->getCancelButton()->mapTo(editor, QPoint(0, 0)).x());
    QVERIFY(editor->getCancelButton()->mapTo(editor, QPoint(0, 0)).x() < editor->getSaveButton()->mapTo(editor, QPoint(0, 0)).x());
    QCOMPARE(editor->getSaveButton()->mapTo(editor, QPoint(0, 0)).x() + editor->getSaveButton()->width(), editor->width() - 21);
    QCOMPARE(editor->getNoteLabel()->text(), QStringLiteral("Built-in collections can be edited; Reset to default restores the original tools. Delete is available for custom collections only."));

    // The vertical positions of 283:1888 (the texts in the line heights of the design, not
    // in the line spacing of the font): the list card 3 above the editor card (header 18
    // against 21), its first row 36 below its top (border 1, padding 8, group label 25,
    // gap 2); in the editor card (284:2218) the tools at 115, Add a tool at 367, the first
    // suggestion at 450, the actions at 618, the card 675 high
    QWidget* listCard = page->findChild<QWidget*>(QStringLiteral("collectionsList"));
    QWidget* editorCard = editor->getCard();
    QCOMPARE(page->findChild<QWidget*>(QStringLiteral("builtinGroup"))->height(), 25);
    QCOMPARE(page->findChild<QWidget*>(QStringLiteral("collectionsTitle"))->height(), 18);
    QCOMPARE(editorCard->mapTo(page, QPoint(0, 0)).y() - listCard->mapTo(page, QPoint(0, 0)).y(), 3);
    QCOMPARE(row->mapTo(listCard, QPoint(0, 0)).y(), 36);
    QCOMPARE(editor->findChild<QWidget*>(QStringLiteral("toolsInCollection"))->mapTo(editorCard, QPoint(0, 0)).y(), 115);
    QCOMPARE(editor->findChild<QWidget*>(QStringLiteral("addTool"))->mapTo(editorCard, QPoint(0, 0)).y(), 367);
    QCOMPARE(editor->findOfferedToolRow(editor->getOfferedTools().front())->mapTo(editorCard, QPoint(0, 0)).y(), 450);
    QCOMPARE(editor->getSaveButton()->mapTo(editorCard, QPoint(0, 0)).y(), 618);
    QCOMPARE(editorCard->height(), 675);
    QCOMPARE(editor->getNoteLabel()->mapTo(editor, QPoint(0, 0)).y() - editorCard->mapTo(editor, QPoint(0, 0)).y() - editorCard->height(), 12);

    fixture.screenshot(QStringLiteral("collections-fix-a-scan"));
    pdf::PDFThemeManager::getInstance()->setScheme(pdf::PDFThemeColors::Scheme::Dark);
    fixture.settle();
    fixture.screenshot(QStringLiteral("collections-fix-a-scan-dark"));

    // A page without an icon has its title at the edge of the header again
    fixture.openTools(QStringLiteral("/tools"));
    QVERIFY(header->getIconBadge()->isHidden());
    QCOMPARE(header->getTitleLabel()->mapTo(fixture.window, QPoint(0, 0)).x(), 242);
}

void AppToolCollectionsTest::createAndSave()
{
    pdftest::AppToolsFixture fixture;
    fixture.openTools(QStringLiteral("/tools"));
    pdf::PDFAppToolCollectionsPage* page = CollectionsTestHelper::open(fixture, QStringLiteral("/tools/collections/new?tool=pages.merge"));
    QVERIFY(page && page->isNewCollection());
    pdf::PDFAppToolCollectionEditor* editor = page->getEditor();
    QVERIFY(editor->isNewCollection());
    QCOMPARE(editor->getTitleLabel()->text(), QStringLiteral("New collection"));
    QVERIFY(editor->getBuiltinTag()->isHidden());
    QVERIFY(editor->getResetLink()->isHidden());
    QVERIFY(!editor->getDeleteButton()->isEnabled());
    QCOMPARE(editor->getIconChooser()->getIconName(), QStringLiteral("folder"));
    QCOMPARE(editor->getToolList()->getTools(), QStringList({ QStringLiteral("pages.merge") }));
    fixture.screenshot(QStringLiteral("collections-new"));

    // The name, the icon and a tool found by the search (Enter adds the best result)
    CollectionsTestHelper::typeName(editor, QStringLiteral("Invoices"));
    QCOMPARE(editor->getTitleLabel()->text(), QStringLiteral("Invoices"));
    QTest::mouseClick(editor->getIconChooser()->getChoice(QStringLiteral("star")), Qt::LeftButton);
    QCOMPARE(editor->getDraft().iconName, QStringLiteral("star"));
    editor->getSearchField()->setFocus();
    QTest::keyClicks(editor->getSearchField(), QStringLiteral("compress"));
    QCOMPARE(editor->getOfferedTools().front(), QStringLiteral("optimize.compress"));
    QTest::keyClick(editor->getSearchField(), Qt::Key_Return);
    QCOMPARE(editor->getDraft().toolIds, QStringList({ QStringLiteral("pages.merge"), QStringLiteral("optimize.compress") }));
    QVERIFY(!editor->getOfferedTools().contains(QStringLiteral("optimize.compress")));

    // Nothing is written before Save
    QCOMPARE(fixture.collections().getCustomCollectionCount(), 0);
    QVERIFY(editor->getSaveButton()->isEnabled());
    QTest::mouseClick(editor->getSaveButton(), Qt::LeftButton);
    fixture.settle();

    // One collection; the route of the new collection replaces /new
    const std::vector<pdf::PDFToolCollectionRow> rows = fixture.collections().getCollections();
    QCOMPARE(rows.size(), size_t(1));
    QCOMPARE(rows.front().name, QStringLiteral("Invoices"));
    QCOMPARE(rows.front().iconName, QStringLiteral("star"));
    QCOMPARE(rows.front().toolIds, QStringList({ QStringLiteral("pages.merge"), QStringLiteral("optimize.compress") }));
    const QString routeId = pdf::PDFAppToolCollections::getCustomRouteId(rows.front().id);
    QCOMPARE(fixture.currentRoute(), QStringLiteral("/tools/collections/") + routeId);
    page = fixture.page<pdf::PDFAppToolCollectionsPage>();
    QVERIFY(page && !page->isNewCollection());
    QVERIFY(page->findListRow(routeId)->isChecked());
    QVERIFY(!page->getEditor()->isModified());
    QVERIFY(page->getEditor()->getDeleteButton()->isEnabled());
    QVERIFY(fixture.window->getNavigator()->goBack());
    fixture.settle();
    QCOMPARE(fixture.currentRoute(), QStringLiteral("/tools"));

    // Save of a stored collection keeps the page; the name is normalized by the repository
    page = CollectionsTestHelper::open(fixture, QStringLiteral("/tools/collections/") + routeId);
    editor = page->getEditor();
    CollectionsTestHelper::typeName(editor, QStringLiteral("  Invoices 2026  "));
    QVERIFY(editor->isModified());
    QVERIFY(page->saveCollection());
    fixture.settle();
    QCOMPARE(fixture.page<pdf::PDFAppToolCollectionsPage>(), page);
    QCOMPARE(fixture.collections().getCollection(rows.front().id)->name, QStringLiteral("Invoices 2026"));
    QVERIFY(!editor->isModified());
    QVERIFY(!editor->getInfoBar());
    QCOMPARE(page->findListRow(routeId)->getName(), QStringLiteral("Invoices 2026"));
}

void AppToolCollectionsTest::saveDisabledStates()
{
    pdftest::AppToolsFixture fixture;
    pdf::PDFAppToolCollectionsPage* page = CollectionsTestHelper::open(fixture, QStringLiteral("/tools/collections/new"));
    pdf::PDFAppToolCollectionEditor* editor = page->getEditor();

    // An empty new collection: no name, no tool
    QVERIFY(!editor->getSaveButton()->isEnabled());
    QCOMPARE(editor->getSaveButton()->toolTip(), QStringLiteral("Enter a name of the collection."));
    QVERIFY(!editor->isNameInvalid());
    QCOMPARE(editor->getNameMessage(), QStringLiteral("Shown on the Tools page. Keep it short."));
    QVERIFY(!editor->getToolList()->getEmptyLabel()->isHidden());
    QVERIFY(editor->getOfferCaption()->isHidden());
    QVERIFY(!page->saveCollection());

    CollectionsTestHelper::typeName(editor, QStringLiteral("Scans"));
    QVERIFY(!editor->getSaveButton()->isEnabled());
    QCOMPARE(editor->getSaveButton()->toolTip(), QStringLiteral("Add at least one tool."));

    QVERIFY(editor->addTool(QStringLiteral("convert.scan")));
    QVERIFY(editor->getSaveButton()->isEnabled());
    QVERIFY(editor->getSaveButton()->toolTip().isEmpty());
    QVERIFY(!editor->addTool(QStringLiteral("convert.scan")));
    QVERIFY(!editor->addTool(QStringLiteral("unknown.tool")));

    // Only white space or invisible characters is no name (the error after an edit)
    CollectionsTestHelper::typeName(editor, QStringLiteral("   "));
    QVERIFY(!editor->getSaveButton()->isEnabled());
    QVERIFY(editor->isNameInvalid());
    QCOMPARE(editor->getNameMessage(), QStringLiteral("Enter a name of the collection."));
    CollectionsTestHelper::typeName(editor, QString(QChar(0x202E)) + QChar(0x200B));
    QVERIFY(!editor->getSaveButton()->isEnabled());

    // The field takes at most 64 characters
    CollectionsTestHelper::typeName(editor, QString(100, QLatin1Char('n')));
    QCOMPARE(editor->getDraft().name.size(), qsizetype(64));
    fixture.settle();
    QVERIFY(editor->getTitleLabel()->isElided() || editor->getTitleLabel()->width() >= editor->getTitleLabel()->getTextWidth());
    QCOMPARE(editor->getTitleLabel()->toolTip(), QString(64, QLatin1Char('n')));
    QVERIFY(editor->getTitleLabel()->width() < editor->width());
    QVERIFY(editor->getSaveButton()->isEnabled());
    fixture.screenshot(QStringLiteral("collections-new-filled"));
}

void AppToolCollectionsTest::duplicateNames()
{
    pdftest::AppToolsFixture fixture;
    QVERIFY(CollectionsTestHelper::create(fixture, QStringLiteral("Invoices"), { QStringLiteral("pages.merge") }) > 0);
    pdf::PDFAppToolCollectionsPage* page = CollectionsTestHelper::open(fixture, QStringLiteral("/tools/collections/new?tool=pages.split"));
    pdf::PDFAppToolCollectionEditor* editor = page->getEditor();

    // A custom name without case, a built-in name, then a free name
    CollectionsTestHelper::typeName(editor, QStringLiteral("INVOICES"));
    QVERIFY(editor->isNameInvalid());
    QCOMPARE(editor->getNameMessage(), QStringLiteral("A collection named “Invoices” already exists. Choose another name."));
    QVERIFY(!editor->getSaveButton()->isEnabled());
    QVERIFY(!page->saveCollection());

    CollectionsTestHelper::typeName(editor, QStringLiteral("fix a scan"));
    QVERIFY(editor->isNameInvalid());
    QVERIFY(editor->getNameMessage().contains(QStringLiteral("Fix a scan")));

    CollectionsTestHelper::typeName(editor, QStringLiteral("Invoices 2"));
    QVERIFY(!editor->isNameInvalid());
    QVERIFY(editor->getSaveButton()->isEnabled());

    // Another instance takes the name meanwhile: Save is refused after the refresh
    QVERIFY(fixture.writeFromOtherConnection([](pdf::PDFDatabase* database)
    {
        return pdf::PDFToolCollectionRepository(database).createCollection(QStringLiteral("invoices 2"), QString(), { QStringLiteral("pages.merge") });
    }, pdf::PDFToolCollectionRepository::getCollectionTopic()));
    QVERIFY(editor->isNameInvalid());
    QVERIFY(!page->saveCollection());
    QCOMPARE(fixture.collections().getCustomCollectionCount(), 2);

    // The own name of a stored collection is no duplicate
    const qint64 id = CollectionsTestHelper::create(fixture, QStringLiteral("Own"), { QStringLiteral("pages.merge") });
    page = CollectionsTestHelper::open(fixture, QStringLiteral("/tools/collections/custom-%1").arg(id));
    editor = page->getEditor();
    CollectionsTestHelper::typeName(editor, QStringLiteral("OWN"));
    QVERIFY(!editor->isNameInvalid());
    QVERIFY(page->saveCollection());
    fixture.settle();
    QCOMPARE(fixture.collections().getCollection(id)->name, QStringLiteral("OWN"));

    // A built-in collection keeps its own name
    page = CollectionsTestHelper::open(fixture, QStringLiteral("/tools/collections/fix-a-scan"));
    CollectionsTestHelper::typeName(page->getEditor(), QStringLiteral("FIX A SCAN"));
    QVERIFY(!page->getEditor()->isNameInvalid());
}

void AppToolCollectionsTest::renameAndIcon()
{
    pdftest::AppToolsFixture fixture;
    pdf::PDFAppToolCollectionsPage* page = CollectionsTestHelper::open(fixture, QStringLiteral("/tools/collections/fix-a-scan"));
    pdf::PDFAppToolCollectionEditor* editor = page->getEditor();

    CollectionsTestHelper::typeName(editor, QStringLiteral("My scans"));
    QTest::mouseClick(editor->getIconChooser()->getChoice(QStringLiteral("camera")), Qt::LeftButton);
    QVERIFY(editor->isModified());
    QVERIFY(editor->getResetLink()->isEnabled());
    QCOMPARE(editor->getTitleLabel()->text(), QStringLiteral("My scans"));

    // The list shows the stored name until Save
    QCOMPARE(page->findListRow(QStringLiteral("fix-a-scan"))->getName(), QStringLiteral("Fix a scan"));
    QVERIFY(page->saveCollection());
    fixture.settle();
    std::optional<pdf::PDFToolCollectionRow> override = fixture.collections().getBuiltinOverride(QStringLiteral("fix-a-scan"));
    QVERIFY(override);
    QCOMPARE(override->name, QStringLiteral("My scans"));
    QCOMPARE(override->iconName, QStringLiteral("camera"));
    QCOMPARE(page->findListRow(QStringLiteral("fix-a-scan"))->getName(), QStringLiteral("My scans"));
    QCOMPARE(page->findListRow(QStringLiteral("fix-a-scan"))->getIconName(), QStringLiteral("camera"));
    QVERIFY(page->findListRow(QStringLiteral("fix-a-scan"))->getMeta().contains(QStringLiteral("edited")));

    // The name and the icon of the definition again: the override is deleted by Save
    CollectionsTestHelper::typeName(editor, QStringLiteral("Fix a scan"));
    QTest::mouseClick(editor->getIconChooser()->getChoice(QStringLiteral("rotate-cw")), Qt::LeftButton);
    QVERIFY(page->saveCollection());
    fixture.settle();
    QVERIFY(!fixture.collections().getBuiltinOverride(QStringLiteral("fix-a-scan")));
    QCOMPARE(page->findListRow(QStringLiteral("fix-a-scan"))->getMeta(), QStringLiteral("4 tools"));
}

void AppToolCollectionsTest::reorderDragAndDrop()
{
    pdftest::AppToolsFixture fixture;
    pdf::PDFAppToolCollectionsPage* page = CollectionsTestHelper::open(fixture, QStringLiteral("/tools/collections/fix-a-scan"));
    pdf::PDFAppToolCollectionEditor* editor = page->getEditor();
    pdf::PDFAppCollectionToolList* list = editor->getToolList();
    fixture.settle();

    // Remove Pages dropped above the first row: the indicator before the first row
    QWidget* firstRow = list->findRow(QStringLiteral("pages.rotate"));
    std::unique_ptr<QMimeData> mimeData(list->createMimeData(QStringLiteral("pages.remove")));
    int indicator = -1;
    QDragEnterEvent enter(firstRow->geometry().topLeft() + QPoint(20, 4), Qt::MoveAction, mimeData.get(), Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(list, &enter);
    QVERIFY(enter.isAccepted());
    QCOMPARE(list->getDropIndicatorIndex(), 0);
    QVERIFY(!list->grab().isNull());

    // The indicator follows the pointer (below the last row = the end)
    QDragMoveEvent move(QPoint(20, list->height() - 3), Qt::MoveAction, mimeData.get(), Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(list, &move);
    QCOMPARE(list->getDropIndicatorIndex(), 4);
    QVERIFY(!list->grab().isNull());
    QDragLeaveEvent leave;
    QApplication::sendEvent(list, &leave);
    QCOMPARE(list->getDropIndicatorIndex(), -1);

    // Data of another list moved over the list: refused, no indicator
    pdf::PDFAppCollectionToolList otherList;
    otherList.setTools(CollectionsTestHelper::fixAScan());
    std::unique_ptr<QMimeData> foreignData(otherList.createMimeData(QStringLiteral("pages.remove")));
    QDragMoveEvent foreignMove(QPoint(20, 10), Qt::MoveAction, foreignData.get(), Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(list, &foreignMove);
    QVERIFY(!foreignMove.isAccepted());
    QCOMPARE(list->getDropIndicatorIndex(), -1);

    // The handle starts the drag after the drag distance (offscreen: QDrag::exec
    // returns at once); a press without a move and a release start nothing
    pdf::PDFIconButton* handle = list->findHandle(QStringLiteral("pages.crop"));
    QCOMPARE(handle->cursor().shape(), Qt::OpenHandCursor);
    QTest::mousePress(handle, Qt::LeftButton, Qt::NoModifier, QPoint(8, 8));
    QMouseEvent small(QEvent::MouseMove, QPointF(9, 9), handle->mapToGlobal(QPointF(9, 9)), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(handle, &small);
    QMouseEvent far(QEvent::MouseMove, QPointF(8, 60), handle->mapToGlobal(QPointF(8, 60)), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(handle, &far);
    QVERIFY(!list->isDragging());

    // Review RC6-2: the handle is not left pressed by the drag (its release may never come)
    QVERIFY(!handle->isDown());
    QTest::mouseRelease(handle, Qt::LeftButton, Qt::NoModifier, QPoint(8, 60));
    QApplication::sendEvent(handle, &far);
    QCOMPARE(list->getTools(), CollectionsTestHelper::fixAScan());

    QVERIFY(CollectionsTestHelper::drop(list, mimeData.get(), firstRow->geometry().topLeft() + QPoint(20, 4), &indicator));
    QCOMPARE(indicator, 0);
    QCOMPARE(list->getDropIndicatorIndex(), -1);
    QCOMPARE(editor->getDraft().toolIds, QStringList({ QStringLiteral("pages.remove"), QStringLiteral("pages.rotate"), QStringLiteral("pages.crop"), QStringLiteral("pages.interleave") }));
    QVERIFY(editor->isModified());
    QVERIFY(!fixture.collections().getBuiltinOverride(QStringLiteral("fix-a-scan")));

    // Rotate dropped on the lower half of Interleave: after it
    fixture.settle();
    std::unique_ptr<QMimeData> rotateData(list->createMimeData(QStringLiteral("pages.rotate")));
    const QRect interleave = list->findRow(QStringLiteral("pages.interleave"))->geometry();
    QVERIFY(CollectionsTestHelper::drop(list, rotateData.get(), QPoint(interleave.left() + 20, interleave.bottom() - 2)));
    QCOMPARE(editor->getDraft().toolIds, QStringList({ QStringLiteral("pages.remove"), QStringLiteral("pages.crop"), QStringLiteral("pages.interleave"), QStringLiteral("pages.rotate") }));

    // A drop at its own place changes nothing
    QSignalSpy spy(list, &pdf::PDFAppCollectionToolList::toolsChanged);
    const QRect crop = list->findRow(QStringLiteral("pages.crop"))->geometry();
    std::unique_ptr<QMimeData> cropData(list->createMimeData(QStringLiteral("pages.crop")));
    CollectionsTestHelper::drop(list, cropData.get(), QPoint(crop.left() + 20, crop.top() + 4));
    QCOMPARE(spy.count(), 0);

    // Saved in this order
    QVERIFY(page->saveCollection());
    fixture.settle();
    QCOMPARE(fixture.collections().getBuiltinOverride(QStringLiteral("fix-a-scan"))->toolIds, editor->getDraft().toolIds);
    fixture.screenshot(QStringLiteral("collections-reordered"));
}

void AppToolCollectionsTest::foreignDropRefused()
{
    pdftest::AppToolsFixture fixture;
    pdf::PDFAppToolCollectionsPage* page = CollectionsTestHelper::open(fixture, QStringLiteral("/tools/collections/fix-a-scan"));
    pdf::PDFAppCollectionToolList* list = page->getEditor()->getToolList();
    fixture.settle();
    const QPoint point = list->findRow(QStringLiteral("pages.rotate"))->geometry().topLeft() + QPoint(20, 4);
    QSignalSpy spy(list, &pdf::PDFAppCollectionToolList::toolsChanged);

    // Data of another list (another page or window)
    pdf::PDFAppCollectionToolList otherList;
    otherList.setTools(CollectionsTestHelper::fixAScan());
    std::unique_ptr<QMimeData> otherData(otherList.createMimeData(QStringLiteral("pages.remove")));
    QVERIFY(!CollectionsTestHelper::drop(list, otherData.get(), point));
    QVERIFY(list->getDraggedTool(otherData.get()).isEmpty());

    // Text of another application, malformed and oversized data, a tool of another collection
    const QString instance = list->getInstanceId();
    std::vector<QByteArray> payloads = {
        QByteArray(),
        (instance + QStringLiteral("\npages.remove\nextra")).toUtf8(),
        (instance + QStringLiteral("\npages.merge")).toUtf8(),
        (instance + QStringLiteral("\n") + QString(600, QLatin1Char('x'))).toUtf8(),
        QByteArray(10000, '\n'),
        QByteArray("\xff\xfe\x00\x01", 4),
        (instance + QStringLiteral("pages.remove")).toUtf8()
    };
    for (const QByteArray& payload : payloads)
    {
        QMimeData mimeData;
        mimeData.setData(pdf::PDFAppCollectionToolList::getMimeType(), payload);
        QVERIFY(!CollectionsTestHelper::drop(list, &mimeData, point));
    }
    QMimeData text;
    text.setText(instance + QStringLiteral("\npages.remove"));
    QVERIFY(!CollectionsTestHelper::drop(list, &text, point));
    QVERIFY(list->getDraggedTool(nullptr).isEmpty());

    QCOMPARE(spy.count(), 0);
    QCOMPARE(list->getTools(), CollectionsTestHelper::fixAScan());
    QCOMPARE(list->getDropIndicatorIndex(), -1);

    // The editor never takes tools of a list of another page
    QVERIFY(!page->getEditor()->isModified());
}

void AppToolCollectionsTest::reorderKeyboard()
{
    pdftest::AppToolsFixture fixture;
    pdf::PDFAppToolCollectionsPage* page = CollectionsTestHelper::open(fixture, QStringLiteral("/tools/collections/fix-a-scan"));
    CollectionsTestHelper::activate(fixture);
    pdf::PDFAppToolCollectionEditor* editor = page->getEditor();
    pdf::PDFAppCollectionToolList* list = editor->getToolList();

    pdf::PDFToolRow* crop = list->findToolRow(QStringLiteral("pages.crop"));
    crop->setFocus(Qt::TabFocusReason);
    QTest::keyClick(crop, Qt::Key_Up, Qt::AltModifier);
    QCOMPARE(list->getTools().front(), QStringLiteral("pages.crop"));
    QVERIFY(crop->hasFocus());
    QTest::keyClick(crop, Qt::Key_Up, Qt::AltModifier);
    QCOMPARE(list->getTools().front(), QStringLiteral("pages.crop"));
    QTest::keyClick(crop, Qt::Key_End, Qt::AltModifier);
    QCOMPARE(list->getTools().back(), QStringLiteral("pages.crop"));
    QTest::keyClick(crop, Qt::Key_Down, Qt::AltModifier);
    QCOMPARE(list->getTools().back(), QStringLiteral("pages.crop"));
    QTest::keyClick(crop, Qt::Key_Home, Qt::AltModifier);
    QCOMPARE(list->getTools(), QStringList({ QStringLiteral("pages.crop"), QStringLiteral("pages.rotate"), QStringLiteral("pages.interleave"), QStringLiteral("pages.remove") }));
    QTest::keyClick(crop, Qt::Key_Down, Qt::AltModifier);
    QCOMPARE(list->getTools(), QStringList({ QStringLiteral("pages.rotate"), QStringLiteral("pages.crop"), QStringLiteral("pages.interleave"), QStringLiteral("pages.remove") }));
    QVERIFY(crop->hasFocus());
    QCOMPARE(editor->getDraft().toolIds, list->getTools());

    // The order of Tab follows the order of the rows
    QCOMPARE(list->findToolRow(QStringLiteral("pages.rotate"))->nextInFocusChain(), list->findRemoveButton(QStringLiteral("pages.rotate")));
    QCOMPARE(list->findRemoveButton(QStringLiteral("pages.rotate"))->nextInFocusChain(), crop);

    // The remove button moves its row too, Delete removes it and the next row gets the focus
    pdf::PDFIconButton* remove = list->findRemoveButton(QStringLiteral("pages.interleave"));
    remove->setFocus(Qt::TabFocusReason);
    QTest::keyClick(remove, Qt::Key_Home, Qt::AltModifier);
    QCOMPARE(list->getTools().front(), QStringLiteral("pages.interleave"));
    crop->setFocus(Qt::TabFocusReason);
    QTest::keyClick(crop, Qt::Key_Delete);
    fixture.settle();
    QVERIFY(!list->getTools().contains(QStringLiteral("pages.crop")));
    QVERIFY(list->findToolRow(QStringLiteral("pages.remove"))->hasFocus());
    QCOMPARE(editor->getToolsHint()->text(), QStringLiteral("3 tools · drag to reorder · shown in this order"));

    // Other keys go on (Space on a row opens the tool - its route)
    QTest::keyClick(list->findToolRow(QStringLiteral("pages.remove")), Qt::Key_Down, Qt::ShiftModifier);
    QCOMPARE(list->getTools().size(), qsizetype(3));
}

void AppToolCollectionsTest::removeTool()
{
    pdftest::AppToolsFixture fixture;
    pdf::PDFAppToolCollectionsPage* page = CollectionsTestHelper::open(fixture, QStringLiteral("/tools/collections/fix-a-scan"));
    pdf::PDFAppToolCollectionEditor* editor = page->getEditor();
    pdf::PDFAppCollectionToolList* list = editor->getToolList();

    QTest::mouseClick(list->findRemoveButton(QStringLiteral("pages.crop")), Qt::LeftButton);
    fixture.settle();
    QCOMPARE(list->getTools(), QStringList({ QStringLiteral("pages.rotate"), QStringLiteral("pages.interleave"), QStringLiteral("pages.remove") }));
    QVERIFY(!list->findRow(QStringLiteral("pages.crop")));
    QVERIFY(editor->isModified());

    // The removed tool is offered again by the search
    editor->setSearchText(QStringLiteral("crop"));
    QVERIFY(editor->getOfferedTools().contains(QStringLiteral("pages.crop")));
    editor->setSearchText(QString());

    // Every tool removed: the empty list, Save disabled
    for (const QString& toolId : list->getTools())
    {
        QVERIFY(list->removeTool(toolId));
    }
    fixture.settle();
    QVERIFY(!list->getEmptyLabel()->isHidden());
    QCOMPARE(list->getEmptyLabel()->text(), QStringLiteral("No tools in this collection yet. Add a tool below."));
    QVERIFY(!editor->getSaveButton()->isEnabled());
    QCOMPARE(editor->getToolsHint()->text(), QStringLiteral("0 tools · drag to reorder · shown in this order"));
    QVERIFY(editor->getOfferCaption()->isHidden());
    QVERIFY(!list->removeTool(QStringLiteral("pages.rotate")));
    fixture.screenshot(QStringLiteral("collections-empty-list"));

    // Cancel gives the tools back, nothing was written
    QTest::mouseClick(editor->getCancelButton(), Qt::LeftButton);
    QCOMPARE(list->getTools(), CollectionsTestHelper::fixAScan());
    QVERIFY(!editor->isModified());
    QVERIFY(!fixture.collections().getBuiltinOverride(QStringLiteral("fix-a-scan")));
}

void AppToolCollectionsTest::addToolSearch()
{
    pdftest::AppToolsFixture fixture;
    pdf::PDFAppToolCollectionsPage* page = CollectionsTestHelper::open(fixture, QStringLiteral("/tools/collections/fix-a-scan"));
    pdf::PDFAppToolCollectionEditor* editor = page->getEditor();

    // The search of the catalog without the tools of the collection, at most 8
    editor->setSearchText(QStringLiteral("image"));
    const QStringList offered = editor->getOfferedTools();
    QVERIFY(!offered.isEmpty());
    QVERIFY(offered.size() <= pdf::PDFAppToolCollectionEditor::MAXIMUM_RESULTS);
    QCOMPARE(editor->getOfferCaption()->text(), QStringLiteral("Tools matching “image”"));
    for (const QString& toolId : offered)
    {
        QVERIFY(!editor->getDraft().toolIds.contains(toolId));
    }
    editor->setSearchText(QStringLiteral("page"));
    QVERIFY(editor->getOfferedTools().size() <= pdf::PDFAppToolCollectionEditor::MAXIMUM_RESULTS);
    QVERIFY(!editor->getOfferedTools().contains(QStringLiteral("pages.rotate")));

    // Q3: a tool found by its keyword says why ("join" finds Merge by its keyword)
    editor->setSearchText(QStringLiteral("join"));
    QVERIFY(editor->getOfferedTools().contains(QStringLiteral("pages.merge")));
    QVERIFY(editor->findOfferedToolRow(QStringLiteral("pages.merge"))->getDescription().startsWith(QStringLiteral("matches “")));
    QVERIFY(!editor->findOfferedToolRow(QStringLiteral("pages.merge"))->isStarVisible());

    // The add button and a click on the row add the tool
    QTest::mouseClick(editor->findAddButton(QStringLiteral("pages.merge")), Qt::LeftButton);
    fixture.settle();
    QCOMPARE(editor->getDraft().toolIds.back(), QStringLiteral("pages.merge"));
    QVERIFY(!editor->getOfferedTools().contains(QStringLiteral("pages.merge")));
    editor->setSearchText(QStringLiteral("compress"));
    QTest::mouseClick(editor->findOfferedToolRow(QStringLiteral("optimize.compress")), Qt::LeftButton);
    fixture.settle();
    QCOMPARE(editor->getDraft().toolIds.back(), QStringLiteral("optimize.compress"));
    QCOMPARE(editor->getToolList()->getTools(), editor->getDraft().toolIds);

    // Only tools of the collection match; nothing matches
    editor->setSearchText(QStringLiteral("rotate pages"));
    QVERIFY(editor->getOfferedTools().isEmpty());
    QCOMPARE(editor->getOfferCaption()->text(), QStringLiteral("Every tool matching “rotate pages” is in the collection already."));
    editor->setSearchText(QStringLiteral("zzqqxxww"));
    QVERIFY(editor->getOfferedTools().isEmpty());
    QCOMPARE(editor->getOfferCaption()->text(), QStringLiteral("No tools match “zzqqxxww”."));
    fixture.screenshot(QStringLiteral("collections-search-nothing"));

    // Hostile texts: controls, bidirectional characters, a long text (bounded caption)
    editor->setSearchText(QString(QChar(0x202E)) + QStringLiteral("merge") + QChar(0x0007));
    QVERIFY(!editor->getOfferCaption()->text().contains(QChar(0x202E)));
    editor->setSearchText(QString(10000, QLatin1Char('a')));
    QVERIFY(editor->getSearchText().size() <= 256);
    QVERIFY(editor->getOfferCaption()->text().size() < 100);
    for (int i = 0; i < 50; ++i)
    {
        QString text;
        for (int j = 0; j < 20; ++j)
        {
            text += QChar(ushort(QRandomGenerator::global()->bounded(1, 0xFFFF)));
        }
        editor->setSearchText(text);
    }

    // Down goes from the search to the first offered tool, Enter adds the best result
    CollectionsTestHelper::activate(fixture);
    editor->setSearchText(QStringLiteral("ocr"));
    editor->getSearchField()->setFocus();
    QTest::keyClick(editor->getSearchField(), Qt::Key_Down);
    QVERIFY(editor->findOfferedToolRow(editor->getOfferedTools().front())->hasFocus());
    editor->getSearchField()->setFocus();
    const QString first = editor->getOfferedTools().front();
    QTest::keyClick(editor->getSearchField(), Qt::Key_Enter);
    QCOMPARE(editor->getDraft().toolIds.back(), first);
    editor->setSearchText(QString());
    QTest::keyClick(editor->getSearchField(), Qt::Key_Return);
    QCOMPARE(editor->getDraft().toolIds.back(), first);
}

void AppToolCollectionsTest::suggestions()
{
    pdftest::AppToolsFixture fixture;
    pdf::PDFAppToolCollectionsPage* page = CollectionsTestHelper::open(fixture, QStringLiteral("/tools/collections/fix-a-scan"));
    pdf::PDFAppToolCollectionEditor* editor = page->getEditor();

    // The suggestions of PDFDocumentToolSuggestions over the tools of the collection
    auto expected = [&fixture](const QStringList& tools, bool isEnabled)
    {
        pdf::PDFDocumentToolSuggestionInput input;
        input.fromTools = tools;
        input.pairs = pdf::PDFAppToolState::readPairs(fixture.context->getDatabase(), tools);
        input.excludedTools = tools;
        input.isSuggestNextEnabled = isEnabled;
        input.isSameCategoryFilled = true;
        input.maximumCount = 3;
        return pdf::PDFDocumentToolSuggestions::suggest(input);
    };
    QCOMPARE(editor->getOfferCaption()->text(), QStringLiteral("Suggested for this collection"));
    QCOMPARE(editor->getOfferedTools(), expected(editor->getDraft().toolIds, true));
    QVERIFY(editor->getOfferedTools().size() <= 3);
    for (const QString& toolId : editor->getOfferedTools())
    {
        QVERIFY(!editor->getDraft().toolIds.contains(toolId));
    }

    // The counters of another instance change them (read again after the notification)
    QVERIFY(fixture.writeFromOtherConnection([](pdf::PDFDatabase* database)
    {
        pdf::PDFDatabaseConnection* connection = database->getConnection();
        return connection->execute("INSERT INTO tool_pair(from_tool, to_tool, count, last_used) VALUES "
                                   "('pages.rotate', 'review.compare', 9, 1000), ('pages.crop', 'review.compare', 9, 1000), "
                                   "('pages.remove', 'protect.redact', 7, 900)");
    }, pdf::PDFToolRepository::getPairTopic()));
    QCOMPARE(editor->getOfferedTools(), expected(editor->getDraft().toolIds, true));

    // A changed collection is suggested for again; an added suggestion leaves the list
    const QString suggested = editor->getOfferedTools().front();
    QVERIFY(editor->addTool(suggested));
    QVERIFY(!editor->getOfferedTools().contains(suggested));
    QCOMPARE(editor->getOfferedTools(), expected(editor->getDraft().toolIds, true));

    // A new collection without tools has no suggestions
    page = CollectionsTestHelper::open(fixture, QStringLiteral("/tools/collections/new"));
    QVERIFY(page->getEditor()->getOfferedTools().isEmpty());
    QVERIFY(page->getEditor()->getOfferCaption()->isHidden());
    page = CollectionsTestHelper::open(fixture, QStringLiteral("/tools/collections/new?tool=convert.scan"));
    QCOMPARE(page->getEditor()->getOfferedTools(), expected({ QStringLiteral("convert.scan") }, true));
}

void AppToolCollectionsTest::cancelAndReset()
{
    pdftest::AppToolsFixture fixture;

    // Cancel of a stored collection returns to its values
    pdf::PDFAppToolCollectionsPage* page = CollectionsTestHelper::open(fixture, QStringLiteral("/tools/collections/fix-a-scan"));
    pdf::PDFAppToolCollectionEditor* editor = page->getEditor();
    CollectionsTestHelper::typeName(editor, QStringLiteral("Other"));
    QTest::mouseClick(editor->getIconChooser()->getChoice(QStringLiteral("zap")), Qt::LeftButton);
    QVERIFY(editor->getToolList()->removeTool(QStringLiteral("pages.crop")));
    QTest::mouseClick(editor->getCancelButton(), Qt::LeftButton);
    QVERIFY(!editor->isModified());
    QCOMPARE(editor->getNameField()->text(), QStringLiteral("Fix a scan"));
    QCOMPARE(editor->getIconChooser()->getIconName(), QStringLiteral("rotate-cw"));
    QCOMPARE(editor->getToolList()->getTools(), CollectionsTestHelper::fixAScan());
    QCOMPARE(fixture.currentRoute(), QStringLiteral("/tools/collections/fix-a-scan"));

    // Reset to default of a changed built-in collection (written by another instance)
    pdf::PDFToolCollectionDraft draft;
    draft.builtinId = QStringLiteral("fix-a-scan");
    draft.name = QStringLiteral("Scan fixes");
    draft.iconName = QStringLiteral("star");
    draft.toolIds = { QStringLiteral("pages.rotate") };
    QVERIFY(fixture.writeCollectionFromOtherConnection(draft));
    QCOMPARE(editor->getNameField()->text(), QStringLiteral("Scan fixes"));
    QVERIFY(editor->getResetLink()->isEnabled());
    QTest::mouseClick(editor->getResetLink(), Qt::LeftButton);
    QCOMPARE(editor->getDraft().name, QStringLiteral("Fix a scan"));
    QCOMPARE(editor->getDraft().iconName, QStringLiteral("rotate-cw"));
    QCOMPARE(editor->getDraft().toolIds, CollectionsTestHelper::fixAScan());
    QVERIFY(editor->isModified());
    QVERIFY(fixture.collections().getBuiltinOverride(QStringLiteral("fix-a-scan")));
    QTest::mouseClick(editor->getSaveButton(), Qt::LeftButton);
    fixture.settle();
    QVERIFY(!fixture.collections().getBuiltinOverride(QStringLiteral("fix-a-scan")));
    QVERIFY(!editor->isModified());

    // Cancel of a new collection leaves the editor (the entry is replaced by the list)
    fixture.openTools(QStringLiteral("/tools"));
    page = CollectionsTestHelper::open(fixture, QStringLiteral("/tools/collections/new"));
    CollectionsTestHelper::typeName(page->getEditor(), QStringLiteral("Unsaved"));
    QTest::mouseClick(page->getEditor()->getCancelButton(), Qt::LeftButton);
    fixture.settle();
    QCOMPARE(fixture.currentRoute(), QStringLiteral("/tools/collections/prepare-for-sharing"));
    QCOMPARE(fixture.collections().getCustomCollectionCount(), 0);
    QVERIFY(fixture.window->getNavigator()->goBack());
    fixture.settle();
    QCOMPARE(fixture.currentRoute(), QStringLiteral("/tools"));
}

void AppToolCollectionsTest::deleteCustom()
{
    pdftest::AppToolsFixture fixture;
    const qint64 id = CollectionsTestHelper::create(fixture, QStringLiteral("Thesis export"), { QStringLiteral("convert.extract-text"), QStringLiteral("pages.merge") });
    QVERIFY(id > 0);
    pdf::PDFAppToolCollectionsPage* page = CollectionsTestHelper::open(fixture, QStringLiteral("/tools/collections/custom-%1").arg(id));
    pdf::PDFAppToolCollectionEditor* editor = page->getEditor();
    QVERIFY(editor->getDeleteButton()->isEnabled());
    QVERIFY(editor->getBuiltinTag()->isHidden());
    QVERIFY(editor->getResetLink()->isHidden());

    // The confirmation: Cancel (Primary) has the focus, Cancel and Escape keep the collection
    QTest::mouseClick(editor->getDeleteButton(), Qt::LeftButton);
    fixture.settle();
    pdf::PDFAppCardDialog* dialog = page->getDeleteDialog();
    QVERIFY(dialog && dialog->isVisible());
    QVERIFY(page->requestDelete());
    QCOMPARE(page->getDeleteDialog(), dialog);
    QVERIFY(CollectionsTestHelper::hasText(dialog, QStringLiteral("Delete the collection “Thesis export”?")));
    QPushButton* cancel = CollectionsTestHelper::button(dialog, QStringLiteral("cancelButton"));
    QVERIFY(cancel && cancel->hasFocus());
    fixture.screenshot(QStringLiteral("collections-delete-dialog"));
    QTest::mouseClick(cancel, Qt::LeftButton);
    fixture.settle();
    QVERIFY(!page->getDeleteDialog());
    QVERIFY(fixture.collections().getCollection(id));

    QVERIFY(page->requestDelete());
    fixture.settle();
    QTest::keyClick(page->getDeleteDialog(), Qt::Key_Escape);
    fixture.settle();
    QVERIFY(fixture.collections().getCollection(id));

    // Delete Collection deletes it and shows the list
    QVERIFY(page->requestDelete());
    fixture.settle();
    QTest::mouseClick(CollectionsTestHelper::button(page->getDeleteDialog(), QStringLiteral("deleteButton")), Qt::LeftButton);
    fixture.settle();
    QVERIFY(!fixture.collections().getCollection(id));
    QCOMPARE(fixture.currentRoute(), QStringLiteral("/tools/collections/prepare-for-sharing"));

    // Deleted meanwhile by another instance: the result is the same
    const qint64 other = CollectionsTestHelper::create(fixture, QStringLiteral("Other"), { QStringLiteral("pages.merge") });
    page = CollectionsTestHelper::open(fixture, QStringLiteral("/tools/collections/custom-%1").arg(other));
    QVERIFY(page->requestDelete());
    fixture.settle();
    QVERIFY(!fixture.collections().deleteCollection(other).isError());
    QTest::mouseClick(CollectionsTestHelper::button(page->getDeleteDialog(), QStringLiteral("deleteButton")), Qt::LeftButton);
    fixture.settle();
    QCOMPARE(fixture.currentRoute(), QStringLiteral("/tools/collections/prepare-for-sharing"));

    // Review RS6: another instance deletes the confirmed collection and creates a new
    // one, which gets the same id (no AUTOINCREMENT) - the new collection is not deleted
    const qint64 confirmed = CollectionsTestHelper::create(fixture, QStringLiteral("Confirmed"), { QStringLiteral("pages.merge") });
    page = CollectionsTestHelper::open(fixture, QStringLiteral("/tools/collections/custom-%1").arg(confirmed));
    QVERIFY(page->requestDelete());
    fixture.settle();
    QVERIFY(!fixture.collections().deleteCollection(confirmed).isError());
    const qint64 reused = CollectionsTestHelper::create(fixture, QStringLiteral("Created elsewhere"), { QStringLiteral("pages.rotate") });
    QCOMPARE(reused, confirmed);
    QVERIFY(page->getDeleteDialog());
    QTest::mouseClick(CollectionsTestHelper::button(page->getDeleteDialog(), QStringLiteral("deleteButton")), Qt::LeftButton);
    fixture.settle();
    QVERIFY(fixture.collections().getCollection(reused));
    QCOMPARE(fixture.collections().getCollection(reused)->name, QStringLiteral("Created elsewhere"));

    // The repository: only the confirmed row (name and time of the change) is deleted
    const std::optional<pdf::PDFToolCollectionRow> row = fixture.collections().getCollection(reused);
    QVERIFY(fixture.collections().deleteCollection(reused, QStringLiteral("Confirmed"), row->modified).isError());
    QVERIFY(fixture.collections().deleteCollection(reused, row->name, row->modified - 1).isError());
    QVERIFY(fixture.collections().getCollection(reused));
    QVERIFY(!fixture.collections().deleteCollection(reused, row->name, row->modified).isError());
    QVERIFY(!fixture.collections().getCollection(reused));
}

void AppToolCollectionsTest::builtinNotDeletable()
{
    pdftest::AppToolsFixture fixture;
    for (const pdf::PDFAppBuiltinToolCollection& collection : pdf::PDFAppBuiltinToolCollections::getCollections())
    {
        pdf::PDFAppToolCollectionsPage* page = CollectionsTestHelper::open(fixture, QStringLiteral("/tools/collections/") + collection.getId());
        QVERIFY(page);
        QVERIFY(!page->getEditor()->getDeleteButton()->isEnabled());
        QVERIFY(!page->requestDelete());
        QVERIFY(!page->getDeleteDialog());
        QVERIFY(page->getEditor()->getBuiltinTag()->isVisible());
        QCOMPARE(page->getEditor()->getDraft().toolIds, collection.getToolIds());
        QCOMPARE(page->getEditor()->getIconChooser()->getIconName(), collection.getIconName());
    }

    // Also a changed one (with its override row)
    pdf::PDFToolCollectionDraft draft;
    draft.builtinId = QStringLiteral("sign-a-contract");
    draft.name = QStringLiteral("Contracts");
    draft.toolIds = { QStringLiteral("protect.sign") };
    QVERIFY(fixture.writeCollectionFromOtherConnection(draft));
    pdf::PDFAppToolCollectionsPage* page = CollectionsTestHelper::open(fixture, QStringLiteral("/tools/collections/sign-a-contract"));
    QVERIFY(!page->getEditor()->getDeleteButton()->isEnabled());
    QVERIFY(!page->requestDelete());
}

void AppToolCollectionsTest::draftInHistory()
{
    pdftest::AppToolsFixture fixture;
    pdf::PDFAppToolCollectionsPage* page = CollectionsTestHelper::open(fixture, QStringLiteral("/tools/collections/fix-a-scan"));
    pdf::PDFAppToolCollectionEditor* editor = page->getEditor();
    CollectionsTestHelper::typeName(editor, QStringLiteral("Draft name"));
    QTest::mouseClick(editor->getIconChooser()->getChoice(QStringLiteral("printer")), Qt::LeftButton);
    QVERIFY(editor->getToolList()->removeTool(QStringLiteral("pages.crop")));
    editor->setSearchText(QStringLiteral("comp"));
    const pdf::PDFAppToolCollectionDraftState draft = editor->getDraft();

    // Q4: another collection, no question; Back gives the draft back
    QTest::mouseClick(page->findListRow(QStringLiteral("sign-a-contract")), Qt::LeftButton);
    fixture.settle();
    QCOMPARE(fixture.currentRoute(), QStringLiteral("/tools/collections/sign-a-contract"));
    QVERIFY(!fixture.collections().getBuiltinOverride(QStringLiteral("fix-a-scan")));
    QVERIFY(fixture.window->getNavigator()->goBack());
    fixture.settle();
    page = fixture.page<pdf::PDFAppToolCollectionsPage>();
    QVERIFY(page);
    editor = page->getEditor();
    QCOMPARE(editor->getDraft(), draft);
    QVERIFY(editor->isModified());
    QCOMPARE(editor->getNameField()->text(), QStringLiteral("Draft name"));
    QCOMPARE(editor->getIconChooser()->getIconName(), QStringLiteral("printer"));
    QCOMPARE(editor->getToolList()->getTools(), draft.toolIds);
    QCOMPARE(editor->getSearchText(), QStringLiteral("comp"));

    // Forward to the other collection and Back again; then Save writes the draft
    QVERIFY(fixture.window->getNavigator()->goForward());
    fixture.settle();
    QVERIFY(fixture.window->getNavigator()->goBack());
    fixture.settle();
    page = fixture.page<pdf::PDFAppToolCollectionsPage>();
    QCOMPARE(page->getEditor()->getDraft(), draft);
    QVERIFY(page->saveCollection());
    fixture.settle();
    QCOMPARE(fixture.collections().getBuiltinOverride(QStringLiteral("fix-a-scan"))->name, QStringLiteral("Draft name"));

    // The draft of a new collection is kept as well
    page = CollectionsTestHelper::open(fixture, QStringLiteral("/tools/collections/new?tool=pages.split"));
    CollectionsTestHelper::typeName(page->getEditor(), QStringLiteral("Half done"));
    fixture.openTools(QStringLiteral("/tools"));
    QVERIFY(fixture.window->getNavigator()->goBack());
    fixture.settle();
    page = fixture.page<pdf::PDFAppToolCollectionsPage>();
    QVERIFY(page && page->isNewCollection());
    QCOMPARE(page->getEditor()->getDraft().name, QStringLiteral("Half done"));
    QCOMPARE(page->getEditor()->getDraft().toolIds, QStringList({ QStringLiteral("pages.split") }));

    // Hostile view states: wrong types, unknown tools, a long name with controls, an icon not allowed
    QVariantMap state;
    QVariantMap hostile;
    hostile.insert(QStringLiteral("name"), QString(5000, QChar(0x0007)) + QStringLiteral("x"));
    hostile.insert(QStringLiteral("icon"), QStringLiteral("../../evil"));
    hostile.insert(QStringLiteral("tools"), QStringList({ QStringLiteral("pages.merge"), QStringLiteral("unknown"), QStringLiteral("pages.merge"), QString(100000, QLatin1Char('t')) }));
    state.insert(pdf::PDFAppToolCollectionsPage::getDraftKey(), hostile);
    state.insert(pdf::PDFAppToolCollectionsPage::getSearchKey(), QString(100000, QLatin1Char('s')));
    page->restoreViewState(state);
    QVERIFY(page->getEditor()->getDraft().name.size() <= 64);
    QVERIFY(!page->getEditor()->getDraft().name.contains(QChar(0x0007)));
    QCOMPARE(page->getEditor()->getDraft().iconName, QStringLiteral("folder"));
    QCOMPARE(page->getEditor()->getDraft().toolIds, QStringList({ QStringLiteral("pages.merge") }));
    QVERIFY(page->getEditor()->getSearchText().size() <= 256);

    QVariantMap wrongTypes;
    wrongTypes.insert(pdf::PDFAppToolCollectionsPage::getDraftKey(), QStringLiteral("not a map"));
    wrongTypes.insert(pdf::PDFAppToolCollectionsPage::getSearchKey(), 42);
    page->restoreViewState(wrongTypes);
    QVariantMap wrongFields;
    QVariantMap fields;
    fields.insert(QStringLiteral("name"), 5);
    fields.insert(QStringLiteral("icon"), QStringLiteral("star"));
    fields.insert(QStringLiteral("tools"), QStringLiteral("pages.merge"));
    wrongFields.insert(pdf::PDFAppToolCollectionsPage::getDraftKey(), fields);
    page->restoreViewState(wrongFields);
    QCOMPARE(page->getEditor()->getDraft().toolIds, QStringList({ QStringLiteral("pages.merge") }));
    QVERIFY(!pdf::PDFAppToolCollectionDraftState::fromVariantMap(fields));
    QVERIFY(!pdf::PDFAppToolCollectionDraftState::fromVariantMap(QVariantMap()));

    // An unchanged stored collection keeps no draft in its entry
    page = CollectionsTestHelper::open(fixture, QStringLiteral("/tools/collections/extract-content"));
    QVERIFY(!page->saveViewState().contains(pdf::PDFAppToolCollectionsPage::getDraftKey()));

    // O25: the draft keeps the identity of its collection (row id, time of the change)
    auto otherSave = [&fixture](qint64 rowId, const QString& name, const QStringList& tools)
    {
        pdf::PDFToolCollectionDraft stored;
        stored.id = rowId;
        stored.name = name;
        stored.toolIds = tools;
        return fixture.writeCollectionFromOtherConnection(stored);
    };
    auto otherDelete = [&fixture](qint64 rowId)
    {
        return fixture.writeFromOtherConnection([rowId](pdf::PDFDatabase* database) { return pdf::PDFToolCollectionRepository(database).deleteCollection(rowId); },
                                                pdf::PDFToolCollectionRepository::getCollectionTopic());
    };
    auto leaveAndBack = [&fixture](const std::function<void()>& meanwhile) -> pdf::PDFAppToolCollectionsPage*
    {
        fixture.openTools(QStringLiteral("/tools"));
        meanwhile();
        fixture.settle();
        if (!fixture.window->getNavigator()->goBack())
        {
            return nullptr;
        }
        fixture.settle();
        return fixture.page<pdf::PDFAppToolCollectionsPage>();
    };

    // The same collection: the draft without a bar; the identity is in the entry
    const qint64 sameId = CollectionsTestHelper::createAt(fixture, QStringLiteral("Same identity"), { QStringLiteral("pages.merge") }, 1000);
    page = CollectionsTestHelper::open(fixture, QStringLiteral("/tools/collections/custom-%1").arg(sameId));
    QVERIFY(page->getEditor()->addTool(QStringLiteral("pages.split")));
    const QVariantMap keptDraft = page->saveViewState().value(pdf::PDFAppToolCollectionsPage::getDraftKey()).toMap();
    const std::optional<pdf::PDFAppToolCollectionDraftIdentity> keptIdentity = pdf::PDFAppToolCollectionDraftIdentity::fromVariantMap(keptDraft);
    QVERIFY(keptIdentity);
    QCOMPARE(keptIdentity->rowId, sameId);
    QCOMPARE(keptIdentity->modified, qint64(1000));
    QVERIFY(keptIdentity->isNameFollowingBase);
    page = leaveAndBack([] { });
    QVERIFY(page);
    QCOMPARE(page->getEditor()->getDraft().toolIds, QStringList({ QStringLiteral("pages.merge"), QStringLiteral("pages.split") }));
    QVERIFY(!page->getEditor()->getInfoBar());

    // Changed by another instance meanwhile: the draft stays, Reload / Keep my changes
    CollectionsTestHelper::typeName(page->getEditor(), QStringLiteral("Mine"));
    page = leaveAndBack([&] { QVERIFY(otherSave(sameId, QStringLiteral("Changed elsewhere"), { QStringLiteral("optimize.compress") })); });
    QVERIFY(page);
    QCOMPARE(page->getEditor()->getDraft().name, QStringLiteral("Mine"));
    QCOMPARE(page->getEditor()->getBase().name, QStringLiteral("Changed elsewhere"));
    pdf::PDFAppInfoBar* identityBar = page->getEditor()->getInfoBar();
    QVERIFY(identityBar && identityBar->isVisible());
    QCOMPARE(identityBar->getRequest().title, QStringLiteral("This collection was changed in another window"));

    // The bar stays over a refresh of the page (a notification of another topic)
    QVERIFY(otherSave(0, QStringLiteral("Unrelated"), { QStringLiteral("pages.merge") }));
    QVERIFY(page->getEditor()->getInfoBar());
    QTest::mouseClick(page->getEditor()->getInfoBar()->getActionButtons()[1], Qt::LeftButton);
    fixture.settle();
    QVERIFY(!page->getEditor()->getInfoBar());
    QCOMPARE(page->getEditor()->getDraft().name, QStringLiteral("Mine"));
    QVERIFY(page->saveCollection());
    fixture.settle();
    QCOMPARE(fixture.collections().getCollection(sameId)->name, QStringLiteral("Mine"));

    // The id reused by a new collection of another instance (no AUTOINCREMENT): the
    // draft is not put silently onto the other collection
    const std::vector<pdf::PDFToolCollectionRow> before = fixture.collections().getCollections();
    const qint64 reusedId = CollectionsTestHelper::createAt(fixture, QStringLiteral("First owner"), { QStringLiteral("pages.merge") }, 2000);
    for (const pdf::PDFToolCollectionRow& row : before)
    {
        QVERIFY(row.id < reusedId);
    }
    page = CollectionsTestHelper::open(fixture, QStringLiteral("/tools/collections/custom-%1").arg(reusedId));
    QVERIFY(page->getEditor()->addTool(QStringLiteral("pages.rotate")));
    qint64 newOwnerId = 0;
    page = leaveAndBack([&]
    {
        QVERIFY(otherDelete(reusedId));
        pdf::PDFToolCollectionDraft newOwner;
        newOwner.name = QStringLiteral("Second owner");
        newOwner.toolIds = { QStringLiteral("optimize.compress") };
        QVERIFY(fixture.writeCollectionFromOtherConnection(newOwner, &newOwnerId));
    });
    QCOMPARE(newOwnerId, reusedId);
    QVERIFY(page);
    QCOMPARE(page->getEditor()->getBase().name, QStringLiteral("Second owner"));
    QVERIFY(page->getEditor()->getInfoBar());
    // The untouched name followed the shown collection; the tools of the draft stay until Reload
    QCOMPARE(page->getEditor()->getDraft().name, QStringLiteral("Second owner"));
    QVERIFY(page->getEditor()->getDraft().toolIds.contains(QStringLiteral("pages.rotate")));
    QTest::mouseClick(page->getEditor()->getInfoBar()->getActionButtons()[0], Qt::LeftButton);
    fixture.settle();
    QVERIFY(!page->getEditor()->getInfoBar());
    QVERIFY(!page->getEditor()->isModified());
    QCOMPARE(page->getEditor()->getDraft().toolIds, QStringList({ QStringLiteral("optimize.compress") }));

    // The collection deleted meanwhile: the draft is dropped (Page Not Found)
    const qint64 goneId = CollectionsTestHelper::create(fixture, QStringLiteral("Gone"), { QStringLiteral("pages.merge") });
    page = CollectionsTestHelper::open(fixture, QStringLiteral("/tools/collections/custom-%1").arg(goneId));
    CollectionsTestHelper::typeName(page->getEditor(), QStringLiteral("Gone draft"));
    page = leaveAndBack([&] { QVERIFY(otherDelete(goneId)); });
    QVERIFY(!page);
    QVERIFY(fixture.notFoundPage());

    // A draft of another collection than the shown one (a hostile entry): dropped
    const qint64 shownId = CollectionsTestHelper::create(fixture, QStringLiteral("Shown"), { QStringLiteral("pages.merge") });
    page = CollectionsTestHelper::open(fixture, QStringLiteral("/tools/collections/custom-%1").arg(shownId));
    QVariantMap foreignDraft = pdf::PDFAppToolCollectionDraftState{ QStringLiteral("Foreign"), QStringLiteral("star"), { QStringLiteral("pages.split") } }.toVariantMap();
    pdf::PDFAppToolCollectionDraftIdentity foreignIdentity;
    foreignIdentity.rowId = shownId + 1000;
    foreignIdentity.modified = 1;
    foreignIdentity.insertInto(foreignDraft);
    QVariantMap foreignState;
    foreignState.insert(pdf::PDFAppToolCollectionsPage::getDraftKey(), foreignDraft);
    page->restoreViewState(foreignState);
    QVERIFY(!page->getEditor()->isModified());
    QVERIFY(!page->getEditor()->getInfoBar());
    foreignIdentity.rowId = 0;
    foreignIdentity.builtinId = QStringLiteral("fix-a-scan");
    foreignIdentity.insertInto(foreignDraft);
    foreignState.insert(pdf::PDFAppToolCollectionsPage::getDraftKey(), foreignDraft);
    page->restoreViewState(foreignState);
    QVERIFY(!page->getEditor()->isModified());

    // Hostile and old identities (wrong types, out of range, an unknown built-in id,
    // missing - an older version): the draft is treated as a draft of a changed collection
    const QList<std::function<void(QVariantMap&)>> hostileIdentities = {
        [](QVariantMap&) { },
        [](QVariantMap& map) { map.insert(QStringLiteral("builtinId"), QString()); map.insert(QStringLiteral("rowId"), QStringLiteral("1")); map.insert(QStringLiteral("modified"), qint64(1)); map.insert(QStringLiteral("nameFollowsBase"), false); },
        [](QVariantMap& map) { map.insert(QStringLiteral("builtinId"), QString()); map.insert(QStringLiteral("rowId"), qint64(-1)); map.insert(QStringLiteral("modified"), qint64(1)); map.insert(QStringLiteral("nameFollowsBase"), false); },
        [](QVariantMap& map) { map.insert(QStringLiteral("builtinId"), QStringLiteral("../evil")); map.insert(QStringLiteral("rowId"), qint64(1)); map.insert(QStringLiteral("modified"), qint64(1)); map.insert(QStringLiteral("nameFollowsBase"), true); },
        [](QVariantMap& map) { map.insert(QStringLiteral("builtinId"), 5); map.insert(QStringLiteral("rowId"), qint64(1)); map.insert(QStringLiteral("modified"), qint64(-5)); map.insert(QStringLiteral("nameFollowsBase"), QStringLiteral("yes")); },
    };
    for (const std::function<void(QVariantMap&)>& makeIdentity : hostileIdentities)
    {
        page = CollectionsTestHelper::open(fixture, QStringLiteral("/tools/collections/custom-%1").arg(shownId));
        QVariantMap hostileDraft = pdf::PDFAppToolCollectionDraftState{ QStringLiteral("Hostile"), QStringLiteral("star"), { QStringLiteral("pages.split") } }.toVariantMap();
        makeIdentity(hostileDraft);
        QVERIFY(!pdf::PDFAppToolCollectionDraftIdentity::fromVariantMap(hostileDraft));
        QVariantMap hostileState;
        hostileState.insert(pdf::PDFAppToolCollectionsPage::getDraftKey(), hostileDraft);
        page->restoreViewState(hostileState);
        QCOMPARE(page->getEditor()->getDraft().name, QStringLiteral("Hostile"));
        QVERIFY(page->getEditor()->getInfoBar());
    }

    // Review RC6-2: a draft of a built-in collection kept in one language and restored
    // after a change of the language - the untouched name follows the new language,
    // Save stores no rename
    CollectionsTestTranslator translator;
    translator.entries.insert(qMakePair(QByteArray(pdf::PDFAppBuiltinToolCollections::TRANSLATION_CONTEXT), QByteArray("Archive documents")), QStringLiteral("Archivovat dokumenty"));
    QVERIFY(QCoreApplication::installTranslator(&translator));
    fixture.settle();
    page = CollectionsTestHelper::open(fixture, QStringLiteral("/tools/collections/archive-documents"));
    QCOMPARE(page->getEditor()->getDraft().name, QStringLiteral("Archivovat dokumenty"));
    QVERIFY(page->getEditor()->addTool(QStringLiteral("pages.split")));
    page = leaveAndBack([&] { QVERIFY(QCoreApplication::removeTranslator(&translator)); });
    QVERIFY(page);
    QCOMPARE(page->getEditor()->getDraft().name, QStringLiteral("Archive documents"));
    QVERIFY(page->getEditor()->getDraft().toolIds.contains(QStringLiteral("pages.split")));
    QVERIFY(!page->getEditor()->getInfoBar());
    QVERIFY(page->saveCollection());
    fixture.settle();
    const std::optional<pdf::PDFToolCollectionRow> archive = fixture.collections().getBuiltinOverride(QStringLiteral("archive-documents"));
    QVERIFY(archive);
    QVERIFY2(archive->name.isEmpty(), qPrintable(archive->name));

    // A name changed by the user stays over the change of the language
    QVERIFY(QCoreApplication::installTranslator(&translator));
    fixture.settle();
    page = CollectionsTestHelper::open(fixture, QStringLiteral("/tools/collections/archive-documents"));
    CollectionsTestHelper::typeName(page->getEditor(), QStringLiteral("Můj archiv"));
    page = leaveAndBack([&] { QVERIFY(QCoreApplication::removeTranslator(&translator)); });
    QVERIFY(page);
    QCOMPARE(page->getEditor()->getDraft().name, QStringLiteral("Můj archiv"));
}

void AppToolCollectionsTest::otherInstanceWhileEditing()
{
    pdftest::AppToolsFixture fixture;
    const qint64 id = CollectionsTestHelper::create(fixture, QStringLiteral("Invoices"), { QStringLiteral("pages.merge"), QStringLiteral("optimize.compress") });
    const QString route = QStringLiteral("/tools/collections/custom-%1").arg(id);
    pdf::PDFAppToolCollectionsPage* page = CollectionsTestHelper::open(fixture, route);
    pdf::PDFAppToolCollectionEditor* editor = page->getEditor();

    auto save = [&fixture, id](const QString& name, const QStringList& tools)
    {
        pdf::PDFToolCollectionDraft draft;
        draft.id = id;
        draft.name = name;
        draft.toolIds = tools;
        return fixture.writeCollectionFromOtherConnection(draft);
    };

    // Review RC6-2: a pin and a use of a tool by another instance do not read the page
    // again (the page shows neither); a change of a collection does
    fixture.settle();
    const int refreshes = page->getRefreshCount();
    QVERIFY(refreshes > 0);
    QVERIFY(fixture.pinFromOtherConnection(QStringLiteral("pages.merge")));
    QVERIFY(fixture.recordUsageFromOtherConnection(QStringLiteral("pages.split"), 5000));
    fixture.settle();
    QCOMPARE(page->getRefreshCount(), refreshes);

    // Without a change of the draft the editor follows (renamed elsewhere)
    QVERIFY(save(QStringLiteral("Invoices renamed"), { QStringLiteral("pages.merge"), QStringLiteral("optimize.compress") }));
    QVERIFY(page->getRefreshCount() > refreshes);
    QCOMPARE(editor->getNameField()->text(), QStringLiteral("Invoices renamed"));
    QCOMPARE(page->findListRow(QStringLiteral("custom-%1").arg(id))->getName(), QStringLiteral("Invoices renamed"));
    QVERIFY(!editor->getInfoBar());
    QVERIFY(!editor->isModified());

    // With a change the draft stays and the info bar says so; Keep my changes
    CollectionsTestHelper::typeName(editor, QStringLiteral("Mine"));
    QVERIFY(save(QStringLiteral("Invoices renamed"), { QStringLiteral("pages.merge") }));
    QCOMPARE(editor->getDraft().name, QStringLiteral("Mine"));
    QCOMPARE(editor->getDraft().toolIds.size(), qsizetype(2));
    pdf::PDFAppInfoBar* bar = editor->getInfoBar();
    QVERIFY(bar && bar->isVisible());
    QCOMPARE(bar->getRequest().title, QStringLiteral("This collection was changed in another window"));
    QCOMPARE(bar->getActionButtons().size(), size_t(2));
    QVERIFY(!bar->getCloseButton());
    fixture.screenshot(QStringLiteral("collections-changed-elsewhere"));
    QTest::mouseClick(bar->getActionButtons()[1], Qt::LeftButton);
    fixture.settle();
    QVERIFY(!editor->getInfoBar());
    QCOMPARE(editor->getDraft().name, QStringLiteral("Mine"));
    QVERIFY(editor->isModified());
    QCOMPARE(editor->getBase().toolIds, QStringList({ QStringLiteral("pages.merge") }));

    // Reload shows the stored collection
    QVERIFY(save(QStringLiteral("Third"), { QStringLiteral("pages.split") }));
    bar = editor->getInfoBar();
    QVERIFY(bar);
    QTest::mouseClick(bar->getActionButtons()[0], Qt::LeftButton);
    fixture.settle();
    QVERIFY(!editor->getInfoBar());
    QCOMPARE(editor->getDraft().name, QStringLiteral("Third"));
    QCOMPARE(editor->getToolList()->getTools(), QStringList({ QStringLiteral("pages.split") }));
    QVERIFY(!editor->isModified());

    // The other window saves the values of the draft: no bar
    CollectionsTestHelper::typeName(editor, QStringLiteral("Same"));
    QVERIFY(save(QStringLiteral("Same"), { QStringLiteral("pages.split") }));
    QVERIFY(!editor->getInfoBar());
    QVERIFY(!editor->isModified());

    // Review RC6-2: the other window changes the collection back to the base of the
    // draft - the reported change is not pending any more (Reload would load stale values)
    CollectionsTestHelper::typeName(editor, QStringLiteral("Draft"));
    QVERIFY(save(QStringLiteral("Elsewhere"), { QStringLiteral("pages.split") }));
    QVERIFY(editor->getInfoBar());
    QVERIFY(save(QStringLiteral("Same"), { QStringLiteral("pages.split") }));
    QVERIFY(!editor->getInfoBar());
    QCOMPARE(editor->getDraft().name, QStringLiteral("Draft"));
    QCOMPARE(editor->getBase().name, QStringLiteral("Same"));
    QVERIFY(editor->isModified());

    // Deleted elsewhere with a draft: Save creates it again (a new route)
    CollectionsTestHelper::typeName(editor, QStringLiteral("Survivor"));
    QVERIFY(fixture.writeFromOtherConnection([id](pdf::PDFDatabase* database) { return pdf::PDFToolCollectionRepository(database).deleteCollection(id); },
                                             pdf::PDFToolCollectionRepository::getCollectionTopic()));
    QVERIFY(editor->isNewCollection());
    bar = editor->getInfoBar();
    QVERIFY(bar);
    QCOMPARE(bar->getRequest().title, QStringLiteral("This collection was deleted in another window"));
    QVERIFY(!page->findListRow(QStringLiteral("custom-%1").arg(id)));
    QVERIFY(page->saveCollection());
    fixture.settle();
    const std::vector<pdf::PDFToolCollectionRow> rows = fixture.collections().getCollections();
    QCOMPARE(rows.size(), size_t(1));
    QCOMPARE(rows.front().name, QStringLiteral("Survivor"));
    QCOMPARE(fixture.currentRoute(), QStringLiteral("/tools/collections/custom-%1").arg(rows.front().id));

    // Deleted elsewhere with a draft: Discard leaves to the list
    const QString survivorRoute = fixture.currentRoute();
    page = fixture.page<pdf::PDFAppToolCollectionsPage>();
    CollectionsTestHelper::typeName(page->getEditor(), QStringLiteral("Discard me"));
    const qint64 survivorId = rows.front().id;
    QVERIFY(fixture.writeFromOtherConnection([survivorId](pdf::PDFDatabase* database) { return pdf::PDFToolCollectionRepository(database).deleteCollection(survivorId); },
                                             pdf::PDFToolCollectionRepository::getCollectionTopic()));
    bar = page->getEditor()->getInfoBar();
    QVERIFY(bar && bar->getCloseButton());
    QTest::mouseClick(bar->getActionButtons()[0], Qt::LeftButton);
    fixture.settle();
    QCOMPARE(fixture.currentRoute(), QStringLiteral("/tools/collections/prepare-for-sharing"));
    Q_UNUSED(survivorRoute);

    // Deleted before the page has read it: Save turns the draft into a new collection
    const qint64 raced = CollectionsTestHelper::create(fixture, QStringLiteral("Raced"), { QStringLiteral("pages.merge") });
    page = CollectionsTestHelper::open(fixture, QStringLiteral("/tools/collections/custom-%1").arg(raced));
    CollectionsTestHelper::typeName(page->getEditor(), QStringLiteral("Raced 2"));
    QVERIFY(!fixture.collections().deleteCollection(raced).isError());
    QVERIFY(!page->saveCollection());
    QVERIFY(page->getEditor()->isNewCollection());
    QVERIFY(!page->getEditor()->getSaveError().isEmpty());
    QVERIFY(page->getEditor()->getInfoBar());
    QVERIFY(page->saveCollection());
    fixture.settle();
    QCOMPARE(fixture.collections().getCollections().back().name, QStringLiteral("Raced 2"));

    // The same race without a change of the draft (RFW6): nothing to create again -
    // the error does not ask to save it again, the next refresh leaves the page
    const qint64 unchanged = CollectionsTestHelper::create(fixture, QStringLiteral("Unchanged"), { QStringLiteral("pages.merge") });
    page = CollectionsTestHelper::open(fixture, QStringLiteral("/tools/collections/custom-%1").arg(unchanged));
    QVERIFY(!page->getEditor()->isModified());
    QVERIFY(!fixture.collections().deleteCollection(unchanged).isError());
    QVERIFY(!page->saveCollection());
    QVERIFY(!page->getEditor()->isNewCollection());
    QVERIFY(!page->getEditor()->getSaveError().isEmpty());
    QVERIFY2(!page->getEditor()->getSaveError().contains(QStringLiteral("again")), qPrintable(page->getEditor()->getSaveError()));
    fixture.settle();
    QCOMPARE(fixture.currentRoute(), QStringLiteral("/tools/collections/prepare-for-sharing"));

    // Deleted elsewhere without a draft: the page leaves to the list at once
    const qint64 plain = CollectionsTestHelper::create(fixture, QStringLiteral("Plain"), { QStringLiteral("pages.merge") });
    page = CollectionsTestHelper::open(fixture, QStringLiteral("/tools/collections/custom-%1").arg(plain));
    QVERIFY(fixture.writeFromOtherConnection([plain](pdf::PDFDatabase* database) { return pdf::PDFToolCollectionRepository(database).deleteCollection(plain); },
                                             pdf::PDFToolCollectionRepository::getCollectionTopic()));
    fixture.settle();
    QCOMPARE(fixture.currentRoute(), QStringLiteral("/tools/collections/prepare-for-sharing"));

    // The bar of a deleted collection closed: the draft stays
    const qint64 closed = CollectionsTestHelper::create(fixture, QStringLiteral("Closed"), { QStringLiteral("pages.merge") });
    page = CollectionsTestHelper::open(fixture, QStringLiteral("/tools/collections/custom-%1").arg(closed));
    CollectionsTestHelper::typeName(page->getEditor(), QStringLiteral("Closed 2"));
    QVERIFY(fixture.writeFromOtherConnection([closed](pdf::PDFDatabase* database) { return pdf::PDFToolCollectionRepository(database).deleteCollection(closed); },
                                             pdf::PDFToolCollectionRepository::getCollectionTopic()));
    QTest::mouseClick(page->getEditor()->getInfoBar()->getCloseButton(), Qt::LeftButton);
    fixture.settle();
    QVERIFY(!page->getEditor()->getInfoBar());
    QCOMPARE(page->getEditor()->getDraft().name, QStringLiteral("Closed 2"));
    QVERIFY(page->getEditor()->getSaveButton()->isEnabled());
}

void AppToolCollectionsTest::limitsAndRouteIds()
{
    pdftest::AppToolsFixture fixture;

    // Route ids of custom collections and the tool of a new collection
    const qint64 id = CollectionsTestHelper::create(fixture, QStringLiteral("Existing"), { QStringLiteral("pages.merge") });
    const QStringList refused = {
        QStringLiteral("/tools/collections/custom-0"),
        QStringLiteral("/tools/collections/custom-0%1").arg(id),
        QStringLiteral("/tools/collections/custom-99999999999999999999"),
        QStringLiteral("/tools/collections/custom-999999"),
        QStringLiteral("/tools/collections/custom--1"),
        QStringLiteral("/tools/collections/custom-"),
        QStringLiteral("/tools/collections/unknown-builtin"),
        QStringLiteral("/tools/collections/new?tool=unknown.tool"),
        QStringLiteral("/tools/collections/new?tool=pages.merge%00"),
        QStringLiteral("/tools/collections/new?tool=") + QString(5000, QLatin1Char('a')),
        QStringLiteral("/tools/collections/fix-a-scan/extra"),
        QStringLiteral("/tools/collections/fix-a-scan#anchor")
    };
    for (const QString& route : refused)
    {
        fixture.openTools(route);
        QVERIFY2(!fixture.page<pdf::PDFAppToolCollectionsPage>(), qPrintable(route));
    }
    QVERIFY(CollectionsTestHelper::open(fixture, QStringLiteral("/tools/collections/custom-%1").arg(id)));

    // The direct API refuses invalid routes too
    pdf::PDFAppToolCollectionsPage direct(fixture.context.get());
    QVERIFY(!direct.setRoute(pdf::PDFAppRoute()));
    QVERIFY(!direct.setRoute(pdf::PDFAppRoute::parse(QStringLiteral("/tools"))));
    QVERIFY(direct.setRoute(pdf::PDFAppRoute::parse(QStringLiteral("/tools/collections"))));
    QCOMPARE(direct.getCollectionId(), QStringLiteral("prepare-for-sharing"));

    // 200 custom collections: a new one cannot be saved, the limit is said at once
    for (int i = 2; i < pdf::PDFToolCollectionRepository::MAXIMUM_CUSTOM_COLLECTIONS; ++i)
    {
        QVERIFY(CollectionsTestHelper::create(fixture, QStringLiteral("Collection %1").arg(i), { QStringLiteral("pages.merge") }) > 0);
    }
    pdf::PDFAppToolCollectionsPage* page = CollectionsTestHelper::open(fixture, QStringLiteral("/tools/collections/new?tool=pages.merge"));
    pdf::PDFAppToolCollectionEditor* editor = page->getEditor();
    CollectionsTestHelper::typeName(editor, QStringLiteral("One too many"));
    QVERIFY(editor->getSaveButton()->isEnabled());

    // The 200th collection written before the page has read it: the transaction refuses
    QVERIFY(CollectionsTestHelper::create(fixture, QStringLiteral("Collection 1"), { QStringLiteral("pages.merge") }) > 0);
    QVERIFY(!page->saveCollection());
    QVERIFY(editor->getSaveError().contains(QStringLiteral("200")));
    fixture.settle();
    QVERIFY(!editor->getSaveButton()->isEnabled());
    QVERIFY(editor->getSaveError().contains(QStringLiteral("200")));
    QVERIFY(!page->saveCollection());
    QCOMPARE(fixture.collections().getCustomCollectionCount(), pdf::PDFToolCollectionRepository::MAXIMUM_CUSTOM_COLLECTIONS);
    QCOMPARE(page->getCountHint()->text(), QStringLiteral("7 built-in · 200 custom"));
    fixture.screenshot(QStringLiteral("collections-limit"));

    // A stored collection can still be saved
    page = CollectionsTestHelper::open(fixture, QStringLiteral("/tools/collections/custom-%1").arg(id));
    CollectionsTestHelper::typeName(page->getEditor(), QStringLiteral("Existing 2"));
    QVERIFY(page->getEditor()->getSaveError().isEmpty());
    QVERIFY(page->saveCollection());
}

void AppToolCollectionsTest::performance()
{
    pdftest::AppToolsFixture fixture;

    // 200 custom collections of 46 tools each
    QStringList tools;
    for (const pdf::PDFDocumentToolCatalogEntry& tool : pdf::PDFDocumentToolCatalog::getTools())
    {
        tools << tool.getId();
    }
    QElapsedTimer timer;
    timer.start();
    for (int i = 0; i < pdf::PDFToolCollectionRepository::MAXIMUM_CUSTOM_COLLECTIONS; ++i)
    {
        QVERIFY(CollectionsTestHelper::create(fixture, QStringLiteral("Collection %1").arg(i), tools) > 0);
    }
    qInfo("200 collections x %d tools written in %lld ms", int(tools.size()), timer.elapsed());

    timer.restart();
    pdf::PDFAppToolCollectionsPage* page = CollectionsTestHelper::open(fixture, QStringLiteral("/tools/collections/custom-200"));
    QVERIFY(page);
    const qint64 openTime = timer.elapsed();
    QCOMPARE(page->getEditor()->getToolList()->getTools().size(), tools.size());
    QCOMPARE(CollectionsTestHelper::listRows(page).size(), qsizetype(207));

    // A change of another instance: one refresh (the rows are reused)
    QPointer<pdf::PDFAppToolCollectionListRow> row = page->findListRow(QStringLiteral("custom-1"));
    timer.restart();
    QVERIFY(fixture.pinFromOtherConnection(QStringLiteral("pages.merge")));
    const qint64 refreshTime = timer.elapsed();
    QVERIFY(row);
    QCOMPARE(page->findListRow(QStringLiteral("custom-1")), row.data());

    // Typing into the search of 46 tools
    timer.restart();
    for (const QString& text : { QStringLiteral("c"), QStringLiteral("co"), QStringLiteral("com"), QStringLiteral("comp") })
    {
        page->getEditor()->setSearchText(text);
    }
    const qint64 searchTime = timer.elapsed();
    qInfo("open %lld ms, refresh with the notification %lld ms, 4 searches %lld ms", openTime, refreshTime, searchTime);
    QVERIFY(openTime < 10000 * pdftest::AppDocumentFixture::getTimeoutFactor());
}

void AppToolCollectionsTest::hostileStoredRows()
{
    pdftest::AppToolsFixture fixture;
    QVERIFY(CollectionsTestHelper::execute(fixture,
        "INSERT INTO tool_collection(id, builtin_id, name, icon, modified) VALUES "
        "(1, 'unknown-builtin', 'Foreign', 'mail', 5), "
        "(2, NULL, zeroblob(4), 'evil', 'yesterday'), "
        "(3, NULL, '', NULL, -7), "
        "(4, NULL, replace(hex(zeroblob(32768)), '0', 'n'), 'star', 9), "
        "(5, NULL, X'41424344', 'zap', 1), "
        "(6, 42, 'Integer builtin', NULL, 1), "
        "(7, 'prepare-for-sharing', 'Prepare for sharing', 'not-an-icon', 3), "
        "(8, NULL, 'Unknown tools', NULL, 4), "
        "(9, NULL, 'Bidi ' || char(8238) || 'evil' || char(7), '../icon', 9223372036854775807)"));
    QVERIFY(CollectionsTestHelper::execute(fixture,
        "INSERT INTO tool_collection_item(collection_id, tool_id, position) VALUES "
        "(2, 'optimize.compress', 2), (2, 'pages.merge', 1), (2, 'unknown.tool', 0), (2, 42, 3), "
        "(3, 'review.compare', 5), (3, 'pages.split', 5), "
        "(4, 'pages.rotate', 'first'), (4, 'pages.crop', 0), "
        "(7, 'protect.password', 0), (8, 'nothing.known', 0), (9, 'pages.merge', 0)"));

    pdf::PDFAppToolCollectionsPage* page = CollectionsTestHelper::open(fixture, QStringLiteral("/tools/collections/prepare-for-sharing"));
    QVERIFY(page);
    QCOMPARE(page->getEditor()->getDraft().toolIds, QStringList({ QStringLiteral("protect.password") }));
    QCOMPARE(page->getEditor()->getIconChooser()->getIconName(), QStringLiteral("mail"));
    QVERIFY(!page->findListRow(QStringLiteral("custom-1")));
    QVERIFY(!page->findListRow(QStringLiteral("custom-6")));

    for (const QString& routeId : CollectionsTestHelper::listRows(page))
    {
        pdf::PDFAppToolCollectionListRow* row = page->findListRow(routeId);
        QVERIFY(!row->getName().isEmpty());
        QVERIFY(row->getName().size() <= 256);
        QVERIFY(!row->getName().contains(QChar(0x202E)));
        QVERIFY(pdf::PDFAppBuiltinToolCollections::isValidIconName(row->getIconName()));
    }
    QCOMPARE(page->findListRow(QStringLiteral("custom-2"))->getMeta(), QStringLiteral("2 tools"));
    QVERIFY(page->findListRow(QStringLiteral("custom-8"))->getMeta().startsWith(QStringLiteral("0 tools")));

    // Every custom collection opens; an empty one cannot be saved until it has a tool
    for (const QString& routeId : CollectionsTestHelper::listRows(page))
    {
        pdf::PDFAppToolCollectionsPage* opened = CollectionsTestHelper::open(fixture, QStringLiteral("/tools/collections/") + routeId);
        QVERIFY2(opened, qPrintable(routeId));
        QVERIFY(opened->getEditor()->getNameField()->text().size() <= 64);
        QVERIFY(!opened->getEditor()->getTitleLabel()->text().contains(QChar(0x202E)));
    }
    pdf::PDFAppToolCollectionsPage* empty = CollectionsTestHelper::open(fixture, QStringLiteral("/tools/collections/custom-8"));
    QVERIFY(!empty->getEditor()->getSaveButton()->isEnabled());
    QVERIFY(!empty->getEditor()->getToolList()->getEmptyLabel()->isHidden());
    QVERIFY(empty->getEditor()->addTool(QStringLiteral("pages.merge")));
    QVERIFY(empty->saveCollection());
    fixture.settle();
    QCOMPARE(fixture.collections().getCollection(8)->toolIds, QStringList({ QStringLiteral("pages.merge") }));

    // Review RS6: a name, which looks like HTML, is shown as it is - also by the tooltip
    // of the elided title (the tooltip of Qt shows a rich text and loads its pictures,
    // also from a share of another computer)
    const QString html = QStringLiteral("<img src=\"file://server/share/x.png\"><b>Bold</b> ") + QString(200, QLatin1Char('x'));
    qint64 htmlId = 0;
    QVERIFY(!fixture.collections().createCollection(html, QString(), { QStringLiteral("pages.merge") }, &htmlId).isError());
    pdf::PDFAppToolCollectionsPage* htmlPage = CollectionsTestHelper::open(fixture, QStringLiteral("/tools/collections/custom-%1").arg(htmlId));
    QVERIFY(htmlPage);
    pdf::PDFAppHeaderText* title = htmlPage->getEditor()->getTitleLabel();
    // (the draft keeps the first 64 characters of the name field)
    const QString shownTitle = title->text();
    QCOMPARE(shownTitle, html.left(pdf::PDFToolCollectionRepository::MAXIMUM_EDITED_NAME_LENGTH));
    QVERIFY(Qt::mightBeRichText(shownTitle));
    QCOMPARE(title->textFormat(), Qt::PlainText);
    QTextDocument shownToolTip;
    if (Qt::mightBeRichText(title->toolTip()))
    {
        shownToolTip.setHtml(title->toolTip());
    }
    else
    {
        shownToolTip.setPlainText(title->toolTip());
    }
    QCOMPARE(shownToolTip.toPlainText(), shownTitle);
}

void AppToolCollectionsTest::deletionMatrix()
{
    // The window deleted with the delete dialog open
    {
        pdftest::AppToolsFixture fixture;
        const qint64 id = CollectionsTestHelper::create(fixture, QStringLiteral("Doomed"), { QStringLiteral("pages.merge") });
        pdf::PDFAppToolCollectionsPage* page = CollectionsTestHelper::open(fixture, QStringLiteral("/tools/collections/custom-%1").arg(id));
        QVERIFY(page->requestDelete());
        fixture.settle();
        QPointer<pdf::PDFAppCardDialog> dialog = page->getDeleteDialog();
        delete fixture.window.data();
        fixture.settle();
        QVERIFY(!dialog);
        QVERIFY(fixture.collections().getCollection(id));
    }

    // The page left (a navigation) with the dialog open; the dialog finished after the page
    {
        pdftest::AppToolsFixture fixture;
        const qint64 id = CollectionsTestHelper::create(fixture, QStringLiteral("Doomed"), { QStringLiteral("pages.merge") });
        pdf::PDFAppToolCollectionsPage* page = CollectionsTestHelper::open(fixture, QStringLiteral("/tools/collections/custom-%1").arg(id));
        QVERIFY(page->requestDelete());
        QPointer<pdf::PDFAppCardDialog> dialog = page->getDeleteDialog();
        fixture.navigate(QStringLiteral("/tools"));
        QVERIFY(!dialog);
        QVERIFY(fixture.collections().getCollection(id));
    }

    // The tab / window deleted with the info bar shown, a pending refresh, offers
    for (int variant = 0; variant < 3; ++variant)
    {
        pdftest::AppToolsFixture fixture;
        const qint64 id = CollectionsTestHelper::create(fixture, QStringLiteral("Bar"), { QStringLiteral("pages.merge") });
        pdf::PDFAppToolCollectionsPage* page = CollectionsTestHelper::open(fixture, QStringLiteral("/tools/collections/custom-%1").arg(id));
        CollectionsTestHelper::typeName(page->getEditor(), QStringLiteral("Draft"));
        page->getEditor()->setSearchText(QStringLiteral("merge"));
        pdf::PDFToolCollectionDraft draft;
        draft.id = id;
        draft.name = QStringLiteral("Bar 2");
        draft.toolIds = { QStringLiteral("pages.split") };
        QVERIFY(fixture.writeCollectionFromOtherConnection(draft));
        QVERIFY(page->getEditor()->getInfoBar());

        if (variant == 0)
        {
            delete fixture.window.data();
        }
        else if (variant == 1)
        {
            // A write is notified, the page goes before its refresh runs
            QVERIFY(!fixture.collections().addTool(id, QString(), QStringLiteral("optimize.compress")).isError());
            fixture.navigate(QStringLiteral("/tools/collections/fix-a-scan"));
        }
        else
        {
            delete page;
        }
        fixture.settle();
        QTest::qWait(100);
        fixture.settle();
    }

    // The page deleted during a drag over its list
    {
        pdftest::AppToolsFixture fixture;
        pdf::PDFAppToolCollectionsPage* page = CollectionsTestHelper::open(fixture, QStringLiteral("/tools/collections/fix-a-scan"));
        pdf::PDFAppCollectionToolList* list = page->getEditor()->getToolList();
        std::unique_ptr<QMimeData> mimeData(list->createMimeData(QStringLiteral("pages.remove")));
        QDragEnterEvent enter(QPoint(20, 10), Qt::MoveAction, mimeData.get(), Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(list, &enter);
        QVERIFY(list->getDropIndicatorIndex() >= 0);
        delete page;
        fixture.settle();
    }

    // The window deleted with a new collection shown
    {
        pdftest::AppToolsFixture fixture;
        pdf::PDFAppToolCollectionsPage* page = CollectionsTestHelper::open(fixture, QStringLiteral("/tools/collections/new"));
        QVERIFY(page);
        delete fixture.window.data();
        fixture.settle();
    }
}

void AppToolCollectionsTest::deleteFromOwnSignals()
{
    // The window deleted synchronously from the signals of the editor and of the list
    // (called directly, not from an event handler of Qt)
    for (int signal = 0; signal < 4; ++signal)
    {
        pdftest::AppToolsFixture fixture;
        pdf::PDFAppToolCollectionsPage* page = CollectionsTestHelper::open(fixture, QStringLiteral("/tools/collections/fix-a-scan"));
        pdf::PDFAppToolCollectionEditor* editor = page->getEditor();
        pdf::PDFAppCollectionToolList* list = editor->getToolList();
        QPointer<pdf::PDFAppMainWindow> window = fixture.window;
        auto deleter = [window]() { delete window.data(); };
        switch (signal)
        {
            case 0:
                QObject::connect(editor, &pdf::PDFAppToolCollectionEditor::draftChanged, deleter);
                QVERIFY(!editor->addTool(QStringLiteral("pages.merge")));
                break;
            case 1:
                QObject::connect(list, &pdf::PDFAppCollectionToolList::toolsChanged, deleter);
                QVERIFY(!list->moveTool(QStringLiteral("pages.remove"), 0));
                break;
            case 2:
                QObject::connect(list, &pdf::PDFAppCollectionToolList::toolsChanged, deleter);
                QVERIFY(!list->removeTool(QStringLiteral("pages.crop")));
                break;
            case 3:
                QObject::connect(editor, &pdf::PDFAppToolCollectionEditor::draftChanged, deleter);
                editor->resetToDefault();
                break;
        }
        fixture.settle();
        QVERIFY(!window);
    }

    // The window deleted (later) from the navigation of every action of the page
    for (int action = 0; action < 6; ++action)
    {
        pdftest::AppToolsFixture fixture;
        const qint64 id = CollectionsTestHelper::create(fixture, QStringLiteral("Signals"), { QStringLiteral("pages.merge") });
        pdf::PDFAppToolCollectionsPage* page = CollectionsTestHelper::open(fixture, action == 3 ? QStringLiteral("/tools/collections/new?tool=pages.merge") : QStringLiteral("/tools/collections/custom-%1").arg(id));
        QPointer<pdf::PDFAppMainWindow> window = fixture.window;
        QObject::connect(page, &pdf::PDFAppPage::navigationRequested, window.data(), [window]() { window->deleteLater(); });
        pdf::PDFAppToolCollectionEditor* editor = page->getEditor();
        switch (action)
        {
            case 0:
                QTest::mouseClick(page->findListRow(QStringLiteral("fix-a-scan")), Qt::LeftButton);
                break;
            case 1:
                QTest::mouseClick(page->getNewButton(), Qt::LeftButton);
                break;
            case 2:
                QTest::mouseClick(editor->getToolList()->findToolRow(QStringLiteral("pages.merge")), Qt::LeftButton);
                break;
            case 3:
                CollectionsTestHelper::typeName(editor, QStringLiteral("Saved"));
                QTest::mouseClick(editor->getSaveButton(), Qt::LeftButton);
                break;
            case 4:
                QVERIFY(page->requestDelete());
                fixture.settle();
                QTest::mouseClick(CollectionsTestHelper::button(page->getDeleteDialog(), QStringLiteral("deleteButton")), Qt::LeftButton);
                break;
            case 5:
                QVERIFY(fixture.writeFromOtherConnection([id](pdf::PDFDatabase* database) { return pdf::PDFToolCollectionRepository(database).deleteCollection(id); },
                                                         pdf::PDFToolCollectionRepository::getCollectionTopic()));
                break;
        }
        fixture.settle();
        QVERIFY2(!window, qPrintable(QString::number(action)));
    }
}

void AppToolCollectionsTest::themesAndScales()
{
    pdftest::AppToolsFixture fixture;
    const qint64 id = CollectionsTestHelper::create(fixture, QStringLiteral("Monthly invoices"), { QStringLiteral("pages.merge"), QStringLiteral("review.output-preview") });
    for (pdf::PDFThemeColors::Scheme scheme : { pdf::PDFThemeColors::Scheme::Light, pdf::PDFThemeColors::Scheme::Dark })
    {
        pdf::PDFThemeManager::getInstance()->setScheme(scheme);
        for (const QString& route : { QStringLiteral("/tools/collections/fix-a-scan"), QStringLiteral("/tools/collections/new"), QStringLiteral("/tools/collections/custom-%1").arg(id) })
        {
            pdf::PDFAppToolCollectionsPage* page = CollectionsTestHelper::open(fixture, route);
            QVERIFY(page);
            if (!page->isNewCollection())
            {
                CollectionsTestHelper::typeName(page->getEditor(), QStringLiteral("Draft"));
                page->getEditor()->setSearchText(QStringLiteral("image"));
            }
            fixture.settle();
            for (qreal ratio : { 1.0, 1.5, 2.0 })
            {
                const QImage image = CollectionsTestHelper::render(page, ratio);
                QVERIFY2(CollectionsTestHelper::countLeakPixels(image) == 0, qPrintable(QStringLiteral("%1 at %2: %3").arg(route).arg(ratio).arg(CollectionsTestHelper::describeLeak(page, image))));

                // The header with the icon of the page (283:1932)
                pdf::PDFAppWorkspaceHeader* header = fixture.window->findChild<pdf::PDFAppWorkspaceHeader*>();
                QVERIFY(header && header->getIconBadge()->isVisible());
                QCOMPARE(CollectionsTestHelper::countLeakPixels(CollectionsTestHelper::render(header, ratio)), 0);
            }
        }

        // The info bar and the delete dialog
        pdf::PDFAppToolCollectionsPage* page = CollectionsTestHelper::open(fixture, QStringLiteral("/tools/collections/custom-%1").arg(id));
        CollectionsTestHelper::typeName(page->getEditor(), QStringLiteral("Mine"));
        pdf::PDFToolCollectionDraft draft;
        draft.id = id;
        draft.name = scheme == pdf::PDFThemeColors::Scheme::Light ? QStringLiteral("Light") : QStringLiteral("Dark");
        draft.toolIds = { QStringLiteral("pages.merge") };
        QVERIFY(fixture.writeCollectionFromOtherConnection(draft));
        QVERIFY(page->getEditor()->getInfoBar());
        QCOMPARE(CollectionsTestHelper::countLeakPixels(CollectionsTestHelper::render(page, 1.0)), 0);
        QCOMPARE(CollectionsTestHelper::countLeakPixels(CollectionsTestHelper::render(page->getEditor()->getInfoBar(), 2.0)), 0);
        QVERIFY(page->requestDelete());
        fixture.settle();
        QCOMPARE(CollectionsTestHelper::countLeakPixels(CollectionsTestHelper::render(page->getDeleteDialog(), 1.0)), 0);
        fixture.screenshot(scheme == pdf::PDFThemeColors::Scheme::Light ? QStringLiteral("collections-dialog-light") : QStringLiteral("collections-dialog-dark"));
        page->getDeleteDialog()->reject();
        fixture.settle();
        fixture.screenshot(scheme == pdf::PDFThemeColors::Scheme::Light ? QStringLiteral("collections-light") : QStringLiteral("collections-dark"));
        page->getEditor()->revert();
    }
}

void AppToolCollectionsTest::rightToLeft()
{
    pdftest::AppToolsFixture fixture;
    QApplication::setLayoutDirection(Qt::RightToLeft);
    pdf::PDFAppToolCollectionsPage* page = CollectionsTestHelper::open(fixture, QStringLiteral("/tools/collections/fix-a-scan"));
    QVERIFY(page);
    fixture.settle();

    // The list at the right, the editor at the left; the actions mirrored
    pdf::PDFAppToolCollectionEditor* editor = page->getEditor();
    QVERIFY(page->getListColumn()->mapTo(page, QPoint(0, 0)).x() > editor->mapTo(page, QPoint(0, 0)).x());
    QVERIFY(editor->getDeleteButton()->mapTo(editor, QPoint(0, 0)).x() > editor->getSaveButton()->mapTo(editor, QPoint(0, 0)).x());
    pdf::PDFAppCollectionToolList* list = editor->getToolList();
    QVERIFY(list->findHandle(QStringLiteral("pages.rotate"))->mapTo(list, QPoint(0, 0)).x() > list->findRemoveButton(QStringLiteral("pages.rotate"))->mapTo(list, QPoint(0, 0)).x());
    QCOMPARE(CollectionsTestHelper::countLeakPixels(CollectionsTestHelper::render(page, 1.0)), 0);

    // The icon of the header at the leading (right) side of the title, 14 px from it
    pdf::PDFAppWorkspaceHeader* header = fixture.window->findChild<pdf::PDFAppWorkspaceHeader*>();
    QVERIFY(header && header->getIconBadge()->isVisible());
    const int iconLeft = header->getIconBadge()->mapTo(header, QPoint(0, 0)).x();
    QCOMPARE(header->getTitleLabel()->mapTo(header, QPoint(0, 0)).x() + header->getTitleLabel()->width() + 14, iconLeft);
    QCOMPARE(CollectionsTestHelper::countLeakPixels(CollectionsTestHelper::render(header, 2.0)), 0);

    // The keys of the order are not mirrored (Up / Down)
    CollectionsTestHelper::activate(fixture);
    pdf::PDFToolRow* crop = list->findToolRow(QStringLiteral("pages.crop"));
    crop->setFocus(Qt::TabFocusReason);
    QTest::keyClick(crop, Qt::Key_Up, Qt::AltModifier);
    QCOMPARE(list->getTools().front(), QStringLiteral("pages.crop"));

    // The info bar at the leading (right) edge
    const qint64 id = CollectionsTestHelper::create(fixture, QStringLiteral("RTL"), { QStringLiteral("pages.merge") });
    page = CollectionsTestHelper::open(fixture, QStringLiteral("/tools/collections/custom-%1").arg(id));
    CollectionsTestHelper::typeName(page->getEditor(), QStringLiteral("Mine"));
    pdf::PDFToolCollectionDraft draft;
    draft.id = id;
    draft.name = QStringLiteral("Other");
    draft.toolIds = { QStringLiteral("pages.merge") };
    QVERIFY(fixture.writeCollectionFromOtherConnection(draft));
    pdf::PDFAppInfoBar* bar = page->getEditor()->getInfoBar();
    QVERIFY(bar);
    const QRect card = bar->getCardRect().translated(bar->pos());
    QCOMPARE(card.right(), page->getEditor()->width() - 1);
    fixture.screenshot(QStringLiteral("collections-rtl"));
}

void AppToolCollectionsTest::minimalWindow()
{
    pdftest::AppToolsFixture fixture(QSize(720, 480));
    pdf::PDFAppToolCollectionsPage* page = CollectionsTestHelper::open(fixture, QStringLiteral("/tools/collections/fix-a-scan"));
    QVERIFY(page);
    page->getEditor()->setSearchText(QStringLiteral("image"));
    fixture.settle();

    // The list above the editor, no horizontal scroll bar, the icons below the name
    QVERIFY(page->getListColumn()->mapTo(page, QPoint(0, 0)).y() < page->getEditor()->mapTo(page, QPoint(0, 0)).y());
    QScrollArea* area = nullptr;
    for (QWidget* widget = page; widget && !area; widget = widget->parentWidget())
    {
        area = qobject_cast<QScrollArea*>(widget);
    }
    QVERIFY(area);
    QVERIFY(!area->horizontalScrollBar()->isVisible() || area->horizontalScrollBar()->maximum() == 0);
    QVERIFY(page->width() <= area->viewport()->width());
    pdf::PDFAppToolCollectionEditor* editor = page->getEditor();
    QVERIFY(editor->getIconChooser()->mapTo(editor, QPoint(0, 0)).y() > editor->getNameField()->mapTo(editor, QPoint(0, 0)).y());
    QVERIFY(editor->getSaveButton()->mapTo(page, QPoint(0, 0)).x() + editor->getSaveButton()->width() <= page->width());
    fixture.screenshot(QStringLiteral("collections-720x480"));
}

void AppToolCollectionsTest::keyboardAndAccessibility()
{
    pdftest::AppToolsFixture fixture;
    pdf::PDFAppToolCollectionsPage* page = CollectionsTestHelper::open(fixture, QStringLiteral("/tools/collections/fix-a-scan"));
    CollectionsTestHelper::activate(fixture);
    pdf::PDFAppToolCollectionEditor* editor = page->getEditor();
    pdf::PDFAppCollectionIconChooser* chooser = editor->getIconChooser();

    // The icons are one stop of Tab; the arrows choose the icon
    editor->getNameField()->setFocus(Qt::TabFocusReason);
    QTest::keyClick(editor->getNameField(), Qt::Key_Tab);
    QVERIFY(chooser->getChoice(QStringLiteral("rotate-cw"))->hasFocus());
    QTest::keyClick(chooser->getChoice(QStringLiteral("rotate-cw")), Qt::Key_Right);
    QCOMPARE(chooser->getIconName(), QStringLiteral("camera"));
    QCOMPARE(editor->getDraft().iconName, QStringLiteral("camera"));
    QTest::keyClick(chooser->getChoice(QStringLiteral("camera")), Qt::Key_Tab);
    QVERIFY(!qobject_cast<pdf::PDFIconChoice*>(QApplication::focusWidget()));

    // Names for assistive technologies
    QAccessibleInterface* choice = QAccessible::queryAccessibleInterface(chooser->getChoice(QStringLiteral("camera")));
    QVERIFY(choice);
    QCOMPARE(choice->text(QAccessible::Name), QStringLiteral("Camera"));
    QVERIFY(choice->state().checked);
    QCOMPARE(chooser->getChoice(QStringLiteral("zap"))->toolTip(), QStringLiteral("Lightning"));
    pdf::PDFAppCollectionToolList* list = editor->getToolList();
    QCOMPARE(list->findRemoveButton(QStringLiteral("pages.rotate"))->accessibleName(), QStringLiteral("Remove Rotate Pages"));
    QCOMPARE(list->findHandle(QStringLiteral("pages.rotate"))->accessibleName(), QStringLiteral("Move Rotate Pages"));
    QCOMPARE(list->findHandle(QStringLiteral("pages.rotate"))->focusPolicy(), Qt::NoFocus);
    QCOMPARE(editor->getNameField()->accessibleName(), QStringLiteral("Name of the collection"));
    QCOMPARE(editor->getSearchField()->accessibleName(), QStringLiteral("Search the catalog"));
    const QString offered = editor->getOfferedTools().front();
    QVERIFY(editor->findAddButton(offered)->accessibleName().startsWith(QStringLiteral("Add ")));
    QAccessibleInterface* listRow = QAccessible::queryAccessibleInterface(page->findListRow(QStringLiteral("fix-a-scan")));
    QVERIFY(listRow && listRow->state().checked);

    // Space on an offered tool adds it; Enter on a tool row of the collection opens the tool
    pdf::PDFToolRow* offerRow = editor->findOfferedToolRow(offered);
    offerRow->setFocus(Qt::TabFocusReason);
    QTest::keyClick(offerRow, Qt::Key_Space);
    fixture.settle();
    QCOMPARE(editor->getDraft().toolIds.back(), offered);
    QVERIFY(editor->getOfferCaption()->isVisible());

    // The removed focus of an offer goes on to the next offer or to the search
    pdf::PDFToolRow* rotate = list->findToolRow(QStringLiteral("pages.rotate"));
    rotate->setFocus(Qt::TabFocusReason);
    QTest::keyClick(rotate, Qt::Key_Return);
    fixture.settle();
    QCOMPARE(fixture.currentRoute(), QStringLiteral("/tools/pages.rotate"));
    QVERIFY(fixture.window->getNavigator()->goBack());
    fixture.settle();
    QCOMPARE(fixture.page<pdf::PDFAppToolCollectionsPage>()->getEditor()->getDraft().toolIds.back(), offered);
}

void AppToolCollectionsTest::languageChange()
{
    pdftest::AppToolsFixture fixture;
    pdf::PDFAppToolCollectionsPage* page = CollectionsTestHelper::open(fixture, QStringLiteral("/tools/collections/fix-a-scan"));
    pdf::PDFAppToolCollectionEditor* editor = page->getEditor();
    editor->setSearchText(QStringLiteral("zzqqxxww"));

    CollectionsTestTranslator translator;
    translator.entries.insert(qMakePair(QByteArray("pdf::PDFAppToolCollectionsPage"), QByteArray("Your collections")), QStringLiteral("Vaše kolekce"));
    translator.entries.insert(qMakePair(QByteArray("pdf::PDFAppToolCollectionEditor"), QByteArray("Save collection")), QStringLiteral("Uložit kolekci"));
    translator.entries.insert(qMakePair(QByteArray("pdf::PDFAppToolCollectionEditor"), QByteArray("No tools match “%1”.")), QStringLiteral("Nic neodpovídá „%1“."));
    translator.entries.insert(qMakePair(QByteArray("pdf::PDFAppCollectionIconChooser"), QByteArray("Camera")), QStringLiteral("Fotoaparát"));
    translator.entries.insert(qMakePair(QByteArray("pdf::PDFAppCollectionToolList"), QByteArray("Remove %1")), QStringLiteral("Odebrat %1"));
    translator.entries.insert(qMakePair(QByteArray(pdf::PDFAppBuiltinToolCollections::TRANSLATION_CONTEXT), QByteArray("Fix a scan")), QStringLiteral("Opravit sken"));
    translator.entries.insert(qMakePair(QByteArray(pdf::PDFAppBuiltinToolCollections::TRANSLATION_CONTEXT), QByteArray("Sign a contract")), QStringLiteral("Podepsat smlouvu"));
    QVERIFY(QCoreApplication::installTranslator(&translator));
    fixture.settle();

    QCOMPARE(page->findChild<pdf::PDFLabel*>(QStringLiteral("collectionsTitle"))->text(), QStringLiteral("Vaše kolekce"));
    QCOMPARE(editor->getSaveButton()->text(), QStringLiteral("Uložit kolekci"));
    QCOMPARE(editor->getOfferCaption()->text(), QStringLiteral("Nic neodpovídá „zzqqxxww“."));
    QCOMPARE(editor->getIconChooser()->getChoice(QStringLiteral("camera"))->toolTip(), QStringLiteral("Fotoaparát"));
    QVERIFY(editor->getToolList()->findRemoveButton(QStringLiteral("pages.rotate"))->accessibleName().startsWith(QStringLiteral("Odebrat ")));
    QCOMPARE(page->findListRow(QStringLiteral("fix-a-scan"))->getName(), QStringLiteral("Opravit sken"));

    // An unchanged built-in collection follows its translated name, no bar
    QCOMPARE(editor->getNameField()->text(), QStringLiteral("Opravit sken"));
    QVERIFY(!editor->isModified());
    QVERIFY(!editor->getInfoBar());

    // A changed draft stays without the bar
    CollectionsTestHelper::typeName(editor, QStringLiteral("Moje"));
    QVERIFY(QCoreApplication::removeTranslator(&translator));
    fixture.settle();
    QCOMPARE(editor->getDraft().name, QStringLiteral("Moje"));
    QVERIFY(!editor->getInfoBar());
    QCOMPARE(editor->getSaveButton()->text(), QStringLiteral("Save collection"));
    QCOMPARE(page->findListRow(QStringLiteral("sign-a-contract"))->getName(), QStringLiteral("Sign a contract"));

    // Review RC6-2: a draft with changed tools and the untouched translated name
    // follows the name of the new language - Save stores no rename
    QVERIFY(QCoreApplication::installTranslator(&translator));
    fixture.settle();
    page = CollectionsTestHelper::open(fixture, QStringLiteral("/tools/collections/sign-a-contract"));
    editor = page->getEditor();
    QCOMPARE(editor->getDraft().name, QStringLiteral("Podepsat smlouvu"));
    QVERIFY(editor->addTool(QStringLiteral("pages.merge")));
    QVERIFY(QCoreApplication::removeTranslator(&translator));
    fixture.settle();
    QCOMPARE(editor->getDraft().name, QStringLiteral("Sign a contract"));
    QCOMPARE(editor->getNameField()->text(), QStringLiteral("Sign a contract"));
    QVERIFY(editor->isModified());
    QVERIFY(page->saveCollection());
    const std::optional<pdf::PDFToolCollectionRow> override = fixture.collections().getBuiltinOverride(QStringLiteral("sign-a-contract"));
    QVERIFY(override);
    QVERIFY2(override->name.isEmpty(), qPrintable(override->name));
    QVERIFY(override->toolIds.contains(QStringLiteral("pages.merge")));

    // Review RC6-2: the confirmation of Delete is translated again while it is open
    const qint64 id = CollectionsTestHelper::create(fixture, QStringLiteral("Old name"), { QStringLiteral("pages.merge") });
    page = CollectionsTestHelper::open(fixture, QStringLiteral("/tools/collections/custom-%1").arg(id));
    // Renamed by another instance under a draft: the question names the stored row, which is deleted
    CollectionsTestHelper::typeName(page->getEditor(), QStringLiteral("Draft"));
    pdf::PDFToolCollectionDraft renamed;
    renamed.id = id;
    renamed.name = QStringLiteral("Invoices");
    renamed.toolIds = { QStringLiteral("pages.merge") };
    QVERIFY(fixture.writeCollectionFromOtherConnection(renamed));
    QVERIFY(page->getEditor()->getInfoBar());
    QVERIFY(page->requestDelete());
    fixture.settle();
    QPointer<pdf::PDFAppDeleteCollectionDialog> dialog = page->getDeleteDialog();
    QVERIFY(dialog && dialog->isVisible());
    QVERIFY(CollectionsTestHelper::hasText(dialog, QStringLiteral("Delete the collection “Invoices”?")));
    QCOMPARE(dialog->getDeleteButton()->text(), QStringLiteral("Delete Collection"));
    translator.entries.insert(qMakePair(QByteArray("pdf::PDFAppToolCollectionsPage"), QByteArray("Delete the collection “%1”?")), QStringLiteral("Smazat kolekci „%1“?"));
    translator.entries.insert(qMakePair(QByteArray("pdf::PDFAppToolCollectionsPage"), QByteArray("Delete Collection")), QStringLiteral("Smazat kolekci"));
    translator.entries.insert(qMakePair(QByteArray("pdf::PDFAppToolCollectionsPage"), QByteArray("Cancel")), QStringLiteral("Zrušit"));
    QVERIFY(QCoreApplication::installTranslator(&translator));
    fixture.settle();
    QVERIFY(dialog && dialog->isVisible());
    QVERIFY(CollectionsTestHelper::hasText(dialog, QStringLiteral("Smazat kolekci „Invoices“?")));
    QVERIFY(!CollectionsTestHelper::hasText(dialog, QStringLiteral("Delete the collection “Invoices”?")));
    QCOMPARE(dialog->getDeleteButton()->text(), QStringLiteral("Smazat kolekci"));
    QCOMPARE(dialog->getCancelButton()->text(), QStringLiteral("Zrušit"));

    // Back in English; the dialog still deletes its collection
    QVERIFY(QCoreApplication::removeTranslator(&translator));
    fixture.settle();
    QVERIFY(CollectionsTestHelper::hasText(dialog, QStringLiteral("Delete the collection “Invoices”?")));
    QTest::mouseClick(dialog->getDeleteButton(), Qt::LeftButton);
    fixture.settle();
    QVERIFY(!fixture.collections().getCollection(id));
}

QTEST_MAIN(AppToolCollectionsTest)

#include "tst_apptoolcollectionstest.moc"
