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

#include "appdocumentfixture.h"

#include "pdfappannotationcontroller.h"
#include "pdfappannotationoperations.h"
#include "pdfappcommand.h"
#include "pdfappdocument.h"
#include "pdfappdocumentcommands.h"
#include "pdfappdocumentpanels_notes.h"
#include "pdfappdocumentpanels_outline.h"
#include "pdfappdocumentsidebar.h"
#include "pdfappdocumentstatusbar.h"
#include "pdfappdocumenttab.h"
#include "pdfappdocumentview.h"
#include "pdfappnavigator.h"
#include "pdfapproute.h"
#include "pdfappoutlineeditor.h"
#include "pdfappundostack.h"

#include "pdfcatalog.h"
#include "pdfdocumentbuilder.h"
#include "pdfdocumentreader.h"
#include "pdfdocumentwriter.h"
#include "pdfencoding.h"
#include "pdffluentwidgets.h"
#include "pdfthemecolors.h"
#include "pdfthememanager.h"
#include "pdfwidgetstyle.h"
#include "pdfwidgetannotation.h"

#include <QAction>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QLineEdit>
#include <QMenu>
#include <QMimeData>
#include <QSignalSpy>
#include <QStandardItemModel>
#include <QToolButton>
#include <QtTest>

#include <functional>
#include <set>

/// Helpers of the tests of package C
class NotesOutlineTestHelper
{
public:
    NotesOutlineTestHelper() = delete;

    using Path = pdf::PDFAppAnnotationOperations::OutlinePath;

    static pdf::PDFObject name(const char* value) { return pdf::PDFObject::createName(QByteArray(value)); }
    static pdf::PDFObject text(const QString& value) { return pdf::PDFObjectFactory::createTextString(value); }
    static pdf::PDFObject reference(pdf::PDFObjectReference value) { return pdf::PDFObject::createReference(value); }
    static pdf::PDFObject integer(pdf::PDFInteger value) { return pdf::PDFObject::createInteger(value); }

    static pdf::PDFObject array(std::initializer_list<pdf::PDFObject> items)
    {
        pdf::PDFArrayBuilder builder;
        for (const pdf::PDFObject& item : items)
        {
            builder.appendItem(item);
        }
        return pdf::PDFObject::createArray(std::move(builder));
    }

    static pdf::PDFObject dictionary(std::initializer_list<std::pair<const char*, pdf::PDFObject>> entries)
    {
        pdf::PDFDictionaryBuilder builder;
        for (const auto& entry : entries)
        {
            builder.setEntry(pdf::PDFInplaceOrMemoryString(entry.first), entry.second);
        }
        return pdf::PDFObject::createDictionary(std::move(builder));
    }

    static pdf::PDFObject fit(pdf::PDFObjectReference page)
    {
        return array({ reference(page), name("Fit") });
    }

    static QString write(pdf::PDFDocumentBuilder& builder, const QString& folder, const QString& fileName)
    {
        pdf::PDFDocument document = builder.build();
        const QString path = QDir(folder).absoluteFilePath(fileName);
        return pdftest::AppDocumentSamples::write(document, path) ? path : QString();
    }

    static pdf::PDFDocumentPointer read(const QString& path)
    {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
        {
            return pdf::PDFDocumentPointer();
        }
        pdf::PDFDocumentReader reader(nullptr, [](bool* ok) { *ok = false; return QString(); }, true, false);
        pdf::PDFDocument document = reader.readFromBuffer(file.readAll());
        if (reader.getReadingResult() != pdf::PDFDocumentReader::Result::OK)
        {
            return pdf::PDFDocumentPointer();
        }
        return pdf::PDFDocumentPointer(new pdf::PDFDocument(std::move(document)));
    }

    static pdf::PDFDocumentPointer apply(const pdf::PDFAppAnnotationResult& result)
    {
        return result ? pdf::PDFDocumentPointer(*result) : pdf::PDFDocumentPointer();
    }

    /// Titles of the outline in the pre-order, children prefixed by ">" per level
    static QStringList titles(const pdf::PDFDocument* document)
    {
        QStringList result;
        const QSharedPointer<pdf::PDFOutlineItem> root = document ? document->getCatalog()->getOutlineRootPtr() : QSharedPointer<pdf::PDFOutlineItem>();
        if (!root)
        {
            return result;
        }
        std::vector<std::pair<const pdf::PDFOutlineItem*, int>> stack;
        for (size_t i = root->getChildCount(); i > 0; --i)
        {
            stack.emplace_back(root->getChild(i - 1), 0);
        }
        while (!stack.empty())
        {
            const auto [item, level] = stack.back();
            stack.pop_back();
            result << QString(level, QChar(u'>')) + item->getTitle();
            for (size_t i = item->getChildCount(); i > 0; --i)
            {
                stack.emplace_back(item->getChild(i - 1), level + 1);
            }
        }
        return result;
    }

    /// Titles of the items of the panel (the source model) in the same form
    static QStringList titles(const QAbstractItemModel* model, const QModelIndex& parent = QModelIndex(), int level = 0)
    {
        QStringList result;
        for (int row = 0; model && row < model->rowCount(parent); ++row)
        {
            const QModelIndex index = model->index(row, 0, parent);
            result << QString(level, QChar(u'>')) + index.data().toString();
            result << titles(model, index, level + 1);
        }
        return result;
    }

    /// The links of the outline objects are consistent: /Parent, /Prev, /Next,
    /// /First, /Last and /Count of every item and of the outline dictionary
    static QString checkStructure(const pdf::PDFDocument* document)
    {
        const pdf::PDFObjectStorage& storage = document->getStorage();
        const pdf::PDFDictionary* trailer = storage.getDictionaryFromObject(storage.getTrailerDictionary());
        const pdf::PDFDictionary* catalog = trailer ? storage.getDictionaryFromObject(trailer->get("Root")) : nullptr;
        const pdf::PDFObject& outlines = catalog ? catalog->get("Outlines") : pdf::PDFObject();
        if (!outlines.isReference())
        {
            return QStringLiteral("no outline reference");
        }

        std::function<QString(pdf::PDFObjectReference, pdf::PDFInteger*, bool)> check;
        check = [&](pdf::PDFObjectReference parent, pdf::PDFInteger* visible, bool isRoot) -> QString
        {
            const pdf::PDFDictionary* parentDictionary = storage.getDictionaryFromObject(storage.getObjectByReference(parent));
            if (!parentDictionary)
            {
                return QStringLiteral("parent is not a dictionary");
            }
            const pdf::PDFObject& first = parentDictionary->get("First");
            const pdf::PDFObject& last = parentDictionary->get("Last");
            *visible = 0;
            if (first.isNull() != last.isNull())
            {
                return QStringLiteral("First / Last");
            }
            pdf::PDFObjectReference previous;
            pdf::PDFObjectReference current = first.isReference() ? first.getReference() : pdf::PDFObjectReference();
            int guard = 0;
            while (current.isValid())
            {
                if (++guard > 1000000)
                {
                    return QStringLiteral("cycle");
                }
                const pdf::PDFDictionary* item = storage.getDictionaryFromObject(storage.getObjectByReference(current));
                if (!item)
                {
                    return QStringLiteral("item is not a dictionary");
                }
                if (!item->get("Parent").isReference() || item->get("Parent").getReference() != parent)
                {
                    return QStringLiteral("Parent");
                }
                const pdf::PDFObject& prev = item->get("Prev");
                if ((previous.isValid() && (!prev.isReference() || prev.getReference() != previous)) || (!previous.isValid() && !prev.isNull()))
                {
                    return QStringLiteral("Prev");
                }
                pdf::PDFInteger childVisible = 0;
                const QString error = check(current, &childVisible, false);
                if (!error.isEmpty())
                {
                    return error;
                }
                const pdf::PDFObject& count = item->get("Count");
                const bool isOpen = !count.isInt() || count.getInteger() >= 0;
                if (childVisible > 0 && (!count.isInt() || qAbs(count.getInteger()) != childVisible))
                {
                    return QStringLiteral("Count");
                }
                *visible += 1 + (isOpen ? childVisible : 0);
                previous = current;
                const pdf::PDFObject& next = item->get("Next");
                current = next.isReference() ? next.getReference() : pdf::PDFObjectReference();
            }
            if (previous.isValid() && (!last.isReference() || last.getReference() != previous))
            {
                return QStringLiteral("Last");
            }
            if (isRoot && *visible > 0 && (!parentDictionary->get("Count").isInt() || parentDictionary->get("Count").getInteger() != *visible))
            {
                return QStringLiteral("root Count");
            }
            return QString();
        };

        pdf::PDFInteger visible = 0;
        return check(outlines.getReference(), &visible, true);
    }

    static pdf::PDFAppDocumentSidebar* sidebar(pdf::PDFAppDocumentTab* tab)
    {
        return tab && tab->getView() ? tab->getView()->getSidebar() : nullptr;
    }

    template<typename T>
    static T* panel(pdf::PDFAppDocumentTab* tab, const QString& id)
    {
        pdf::PDFAppDocumentSidebar* bar = sidebar(tab);
        if (!bar || !bar->showPanel(id))
        {
            return nullptr;
        }
        return qobject_cast<T*>(bar->findPanel(id));
    }

    static pdf::PDFAppOutlinePanel* outline(pdf::PDFAppDocumentTab* tab)
    {
        return panel<pdf::PDFAppOutlinePanel>(tab, pdf::PDFAppDocumentPanelIds::outline());
    }

    static pdf::PDFAppNotesPanel* notes(pdf::PDFAppDocumentTab* tab)
    {
        return panel<pdf::PDFAppNotesPanel>(tab, pdf::PDFAppDocumentPanelIds::notes());
    }

    /// Inline editor of the outline tree (nullptr if none)
    static QLineEdit* inlineEditor(pdf::PDFAppOutlinePanel* panel)
    {
        for (QLineEdit* edit : panel->getTreeView()->viewport()->findChildren<QLineEdit*>())
        {
            if (edit->isVisible())
            {
                return edit;
            }
        }
        return nullptr;
    }

    /// Row of the note with the text (-1 if none)
    static int findNote(const pdf::PDFAppNotesModel* model, const QString& noteText)
    {
        for (int row = 0; row < model->rowCount(); ++row)
        {
            const pdf::PDFAppNote* note = model->getNote(row);
            if (note && note->text == noteText)
            {
                return row;
            }
        }
        return -1;
    }

    static pdf::PDFObjectReference noteReference(const pdf::PDFAppNotesModel* model, const QString& noteText)
    {
        const pdf::PDFAppNote* note = model->getNote(findNote(model, noteText));
        return note ? note->reference : pdf::PDFObjectReference();
    }

    static QAction* findAction(QMenu* menu, const QString& objectName)
    {
        for (QAction* action : menu ? menu->actions() : QList<QAction*>())
        {
            if (action->objectName() == objectName)
            {
                return action;
            }
        }
        return nullptr;
    }

    static QStringList actionNames(QMenu* menu)
    {
        QStringList names;
        for (QAction* action : menu ? menu->actions() : QList<QAction*>())
        {
            if (!action->isSeparator())
            {
                names << action->objectName();
            }
        }
        return names;
    }

    /// Counts the pixels of the leak color in the image
    static int countLeakPixels(const QImage& image)
    {
        const QRgb leak = pdf::PDFThemeColors::getLeakColor().rgb();
        int count = 0;
        const QImage converted = image.convertToFormat(QImage::Format_RGB32);
        for (int y = 0; y < converted.height(); ++y)
        {
            const QRgb* line = reinterpret_cast<const QRgb*>(converted.constScanLine(y));
            for (int x = 0; x < converted.width(); ++x)
            {
                if ((line[x] & 0xFFFFFF) == (leak & 0xFFFFFF))
                {
                    ++count;
                }
            }
        }
        return count;
    }

    /// Outline with an item of a URI action, an unknown key, a closed item and a
    /// structure element - entries, which an edit must keep
    static QString richOutline(const QString& folder)
    {
        pdf::PDFDocumentBuilder builder;
        std::vector<pdf::PDFObjectReference> pages;
        for (int i = 0; i < 4; ++i)
        {
            pages.push_back(builder.appendPage(QRectF(0, 0, 300, 400)));
        }
        const pdf::PDFObjectReference root = builder.addObject(pdf::PDFObject());
        const pdf::PDFObjectReference web = builder.addObject(pdf::PDFObject());
        const pdf::PDFObjectReference closed = builder.addObject(pdf::PDFObject());
        const pdf::PDFObjectReference child = builder.addObject(pdf::PDFObject());
        builder.setObject(root, dictionary({ { "Type", name("Outlines") }, { "First", reference(web) }, { "Last", reference(closed) }, { "Count", integer(2) } }));
        builder.setObject(web, dictionary({ { "Title", text(QStringLiteral("Web")) }, { "Parent", reference(root) }, { "Next", reference(closed) },
                                            { "A", dictionary({ { "S", name("URI") }, { "URI", pdf::PDFObject::createString(QByteArray("https://example.com/")) } }) },
                                            { "XCustom", integer(42) }, { "C", array({ pdf::PDFObject::createReal(1.0), pdf::PDFObject::createReal(0.0), pdf::PDFObject::createReal(0.0) }) } }));
        builder.setObject(closed, dictionary({ { "Title", text(QStringLiteral("Closed")) }, { "Parent", reference(root) }, { "Prev", reference(web) },
                                               { "First", reference(child) }, { "Last", reference(child) }, { "Count", integer(-1) }, { "Dest", fit(pages[1]) } }));
        builder.setObject(child, dictionary({ { "Title", text(QStringLiteral("Child")) }, { "Parent", reference(closed) }, { "Dest", fit(pages[2]) } }));
        builder.mergeTo(builder.getCatalogReference(), dictionary({ { "Outlines", reference(root) } }));
        return write(builder, folder, QStringLiteral("rich-outline.pdf"));
    }

    /// Outline of \p count items in a chain of children (each item the child of the previous one)
    static QString deepOutline(const QString& folder, int count)
    {
        pdf::PDFDocumentBuilder builder;
        const pdf::PDFObjectReference page = builder.appendPage(QRectF(0, 0, 300, 400));
        const pdf::PDFObjectReference root = builder.addObject(pdf::PDFObject());
        std::vector<pdf::PDFObjectReference> items;
        for (int i = 0; i < count; ++i)
        {
            items.push_back(builder.addObject(pdf::PDFObject()));
        }
        builder.setObject(root, dictionary({ { "Type", name("Outlines") }, { "First", reference(items.front()) }, { "Last", reference(items.front()) } }));
        for (int i = 0; i < count; ++i)
        {
            pdf::PDFDictionaryBuilder entries;
            entries.setEntry(pdf::PDFInplaceOrMemoryString("Title"), text(QStringLiteral("Level %1").arg(i)));
            entries.setEntry(pdf::PDFInplaceOrMemoryString("Parent"), reference(i == 0 ? root : items[size_t(i - 1)]));
            entries.setEntry(pdf::PDFInplaceOrMemoryString("Dest"), fit(page));
            if (i + 1 < count)
            {
                entries.setEntry(pdf::PDFInplaceOrMemoryString("First"), reference(items[size_t(i + 1)]));
                entries.setEntry(pdf::PDFInplaceOrMemoryString("Last"), reference(items[size_t(i + 1)]));
            }
            builder.setObject(items[size_t(i)], pdf::PDFObject::createDictionary(std::move(entries)));
        }
        builder.mergeTo(builder.getCatalogReference(), dictionary({ { "Outlines", reference(root) } }));
        return write(builder, folder, QStringLiteral("deep-outline-%1.pdf").arg(count));
    }

    /// Outlines, which are damaged: kind "cycle" (the second item continues with the
    /// first one - the parser drops it), "shared" (two items share their child),
    /// "notDictionary" (/Outlines is a number), "badTitle" (a title of a number and
    /// control characters), "brokenNext" (/Next of a missing object)
    static QString hostileOutline(const QString& folder, const QString& kind)
    {
        pdf::PDFDocumentBuilder builder;
        const pdf::PDFObjectReference page = builder.appendPage(QRectF(0, 0, 300, 400));
        const pdf::PDFObjectReference root = builder.addObject(pdf::PDFObject());
        const pdf::PDFObjectReference a = builder.addObject(pdf::PDFObject());
        const pdf::PDFObjectReference b = builder.addObject(pdf::PDFObject());
        const pdf::PDFObjectReference c = builder.addObject(pdf::PDFObject());
        builder.setObject(root, dictionary({ { "Type", name("Outlines") }, { "First", reference(a) }, { "Last", reference(b) } }));
        if (kind == QStringLiteral("cycle"))
        {
            builder.setObject(a, dictionary({ { "Title", text(QStringLiteral("A")) }, { "Parent", reference(root) }, { "Next", reference(b) }, { "Dest", fit(page) } }));
            builder.setObject(b, dictionary({ { "Title", text(QStringLiteral("B")) }, { "Parent", reference(root) }, { "Next", reference(a) }, { "Dest", fit(page) } }));
        }
        else if (kind == QStringLiteral("shared"))
        {
            builder.setObject(a, dictionary({ { "Title", text(QStringLiteral("A")) }, { "Parent", reference(root) }, { "Next", reference(b) }, { "First", reference(c) }, { "Last", reference(c) } }));
            builder.setObject(b, dictionary({ { "Title", text(QStringLiteral("B")) }, { "Parent", reference(root) }, { "First", reference(c) }, { "Last", reference(c) } }));
            builder.setObject(c, dictionary({ { "Title", text(QStringLiteral("C")) }, { "Parent", reference(a) } }));
        }
        else if (kind == QStringLiteral("notDictionary"))
        {
            builder.setObject(root, integer(5));
        }
        else if (kind == QStringLiteral("badTitle"))
        {
            builder.setObject(a, dictionary({ { "Title", integer(7) }, { "Parent", reference(root) }, { "Next", reference(b) } }));
            builder.setObject(b, dictionary({ { "Title", text(QStringLiteral("Bad\x01\ntitle")) }, { "Parent", reference(root) } }));
        }
        else if (kind == QStringLiteral("brokenNext"))
        {
            builder.setObject(a, dictionary({ { "Title", text(QStringLiteral("A")) }, { "Parent", reference(root) }, { "Next", reference(pdf::PDFObjectReference(100000, 0)) } }));
        }
        else if (kind == QStringLiteral("pageItem"))
        {
            // /Next reaches the page (the parser shows it as an item without a title)
            builder.setObject(a, dictionary({ { "Title", text(QStringLiteral("A")) }, { "Parent", reference(root) }, { "Next", reference(page) } }));
        }
        else if (kind == QStringLiteral("infoItem"))
        {
            // /Next reaches the document information (a dictionary with a /Title, no /Type)
            builder.setObject(c, dictionary({ { "Title", text(QStringLiteral("Information")) }, { "Author", text(QStringLiteral("Author")) } }));
            builder.setObject(a, dictionary({ { "Title", text(QStringLiteral("A")) }, { "Parent", reference(root) }, { "Next", reference(c) } }));
            builder.setDocumentInfo(c);
        }
        else if (kind == QStringLiteral("annotationItem"))
        {
            // /Next reaches an annotation of the page
            builder.setObject(c, dictionary({ { "Type", name("Annot") }, { "Subtype", name("Text") }, { "Contents", text(QStringLiteral("Note")) },
                                              { "Rect", array({ integer(10), integer(10), integer(30), integer(30) }) }, { "P", reference(page) } }));
            builder.mergeTo(page, dictionary({ { "Annots", array({ reference(c) }) } }));
            builder.setObject(a, dictionary({ { "Title", text(QStringLiteral("A")) }, { "Parent", reference(root) }, { "Next", reference(c) } }));
        }
        else if (kind == QStringLiteral("acroFormItem"))
        {
            // /Next reaches the interactive form (a dictionary without /Type): the
            // outline is editable, but the form is never emptied (review RC-CD)
            builder.setObject(c, dictionary({ { "Fields", array({}) }, { "DA", pdf::PDFObject::createString(QByteArray("/Helv 0 Tf 0 g")) } }));
            builder.mergeTo(builder.getCatalogReference(), dictionary({ { "AcroForm", reference(c) } }));
            builder.setObject(a, dictionary({ { "Title", text(QStringLiteral("A")) }, { "Parent", reference(root) }, { "Next", reference(c) } }));
        }
        else if (kind == QStringLiteral("titledAcroForm"))
        {
            // The same with a form, which looks like an item of this outline (a
            // /Title and the /Parent of the outline): the catalog references it
            builder.setObject(c, dictionary({ { "Fields", array({}) }, { "Title", text(QStringLiteral("Form")) }, { "Parent", reference(root) } }));
            builder.mergeTo(builder.getCatalogReference(), dictionary({ { "AcroForm", reference(c) } }));
            builder.setObject(a, dictionary({ { "Title", text(QStringLiteral("A")) }, { "Parent", reference(root) }, { "Next", reference(c) } }));
        }
        builder.mergeTo(builder.getCatalogReference(), dictionary({ { "Outlines", reference(root) } }));
        return write(builder, folder, QStringLiteral("hostile-%1.pdf").arg(kind));
    }

    /// Selects the item of the path in the outline panel
    static void selectItem(pdf::PDFAppOutlinePanel* panel, const Path& path)
    {
        const QModelIndex index = panel->getItemIndex(path);
        panel->getTreeView()->selectionModel()->setCurrentIndex(index, QItemSelectionModel::ClearAndSelect);
    }

    /// Opens the context menu of the notes panel at the row (the row selected)
    static QMenu* noteMenu(pdf::PDFAppNotesPanel* panel, const std::vector<pdf::PDFObjectReference>& notes)
    {
        panel->selectNotes(notes);
        return panel->createContextMenu();
    }
};

/// Tests of package C of step 4 (Notes and Outline editing)
class AppNotesOutlineTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanup();

    void commandsRegistered();
    void outlineEditorState();
    void outlineOperations();
    void outlineOperationsKeepEntries();
    void outlineHostile();
    void outlineNewItem();
    void outlineNewItemWithoutOutline();
    void outlineInsertTile();
    void outlineRename();
    void outlineDelete();
    void outlineMove();
    void outlineDragDrop();
    void outlineSetTarget();
    void outlineBoldItalic();
    void outlineUndoKeepsExpansion();
    void outlineLocked();
    void notesSelectionSync();
    void notesReplies();
    void notesContextMenu();
    void notesDeleteKey();
    void notesFilterKeepsSelection();
    void notesHiddenRow();
    void notesLockedDocument();
    void notesLargeDocument();
    void deletionMatrix();
    void deleteFromOwnSignals_data();
    void deleteFromOwnSignals();
    void themesAndScales();
    void rightToLeft();
    void minimalWindow();
    void accessibility();
    void languageChange();
    void reviewRenameUnchangedTitle();
    void reviewNewItemWhileRenaming();
    void reviewStaleCurrentItem();
    void reviewGroupMembersNotDeleted();
    void reviewNotesMenuEmptyArea();
    void reviewOutlineEditableCache();
    void screenshots();
};

void AppNotesOutlineTest::initTestCase()
{
    // The style of the application (the rows of the design, the search fields)
    QApplication::setStyle(new pdf::PDFWidgetStyle());
    QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedStates));
}

void AppNotesOutlineTest::cleanup()
{
    QApplication::setLayoutDirection(Qt::LeftToRight);
    pdf::PDFThemeManager::getInstance()->setScheme(pdf::PDFThemeColors::Scheme::Light);
}

void AppNotesOutlineTest::commandsRegistered()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::outline(fixture.folder()));
    QVERIFY(tab);

    // Every command of the package is registered and handled; F2 works only in the
    // panel (no window shortcut, the action of the panel has it)
    const pdf::PDFAppCommandRegistry* registry = fixture.context->getCommandRegistry();
    for (const QString& commandId : pdf::PDFAppDocumentCommandTable::getCommands(pdf::PDFAppDocumentPackage::NotesOutline))
    {
        QVERIFY2(registry->findCommand(commandId), qPrintable(commandId));
        QVERIFY2(tab->findHandler(commandId), qPrintable(commandId));
    }
    const pdf::PDFAppCommand* rename = registry->findCommand(pdf::PDFAppCommands::outlineRename());
    QCOMPARE(rename->scope, pdf::PDFAppCommandScope::Panel);
    QCOMPARE(rename->defaultShortcut, QKeySequence(Qt::Key_F2));
    QVERIFY(fixture.window->getCommandAction(pdf::PDFAppCommands::outlineRename())->shortcut().isEmpty());
    QVERIFY(!tab->getView()->getCanvasAction(pdf::PDFAppCommands::outlineRename()));
    QVERIFY(registry->findConflicts({}).empty());

    pdf::PDFAppOutlinePanel* outline = NotesOutlineTestHelper::outline(tab);
    QVERIFY(outline);
    QCOMPARE(outline->getRenameAction()->shortcut(), QKeySequence(Qt::Key_F2));
    QCOMPARE(outline->getRenameAction()->shortcutContext(), Qt::WidgetWithChildrenShortcut);

    // The states: a new item always, the others with a current item
    QVERIFY(tab->isCommandEnabled(pdf::PDFAppCommands::outlineNewItem()));
    outline->getTreeView()->selectionModel()->clear();
    tab->getView()->getOutlineEditor()->setCurrentItem({});
    QVERIFY(!tab->isCommandEnabled(pdf::PDFAppCommands::outlineDelete()));
    NotesOutlineTestHelper::selectItem(outline, { 0 });
    QVERIFY(tab->isCommandEnabled(pdf::PDFAppCommands::outlineDelete()));
    QVERIFY(tab->isCommandEnabled(pdf::PDFAppCommands::outlineMoveDown()));
    QVERIFY(!tab->isCommandEnabled(pdf::PDFAppCommands::outlineMoveUp()));
    QVERIFY(!tab->isCommandEnabled(pdf::PDFAppCommands::outlineIndent()));
    QVERIFY(!tab->isCommandEnabled(pdf::PDFAppCommands::outlineOutdent()));
    NotesOutlineTestHelper::selectItem(outline, { 1, 0 });
    QVERIFY(tab->isCommandEnabled(pdf::PDFAppCommands::outlineOutdent()));
    QVERIFY(!tab->isCommandEnabled(pdf::PDFAppCommands::outlineMoveDown()));

    // The user shortcut of the rename is applied to the action of the panel
    QVERIFY(!fixture.context->getShortcuts().setShortcut(pdf::PDFAppCommands::outlineRename(), QStringLiteral("Ctrl+R")).isError());
    QTRY_COMPARE(outline->getRenameAction()->shortcut(), QKeySequence(QStringLiteral("Ctrl+R")));
}

void AppNotesOutlineTest::outlineEditorState()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::outline(fixture.folder()));
    QVERIFY(tab);
    pdf::PDFAppOutlineEditor* editor = tab->getView()->getOutlineEditor();
    QVERIFY(editor);
    QCOMPARE(editor->getView(), tab->getView());
    QCOMPARE(editor->getDocument(), tab->getDocument());
    QVERIFY(editor->isEditingAllowed());
    QVERIFY(editor->getEditingRefusalReason().isEmpty());

    // The destination of the current view: XYZ of the current page, the zoom and
    // the left side kept by the reader
    tab->getView()->goToPage(3);
    fixture.settle();
    const std::optional<pdf::PDFDestination> destination = editor->getCurrentViewDestination();
    QVERIFY(destination);
    QCOMPARE(destination->getDestinationType(), pdf::DestinationType::XYZ);
    QCOMPARE(destination->getPageIndex(), pdf::PDFInteger(3));
    QVERIFY(std::isnan(destination->getZoom()));
    QVERIFY(std::isnan(destination->getLeft()));
    QVERIFY(std::isfinite(destination->getTop()));

    // Locked document: no editing of the outline (O20)
    QSignalSpy stateSpy(editor, &pdf::PDFAppOutlineEditor::stateChanged);
    tab->getDocument()->setLocked(true);
    QVERIFY(stateSpy.count() > 0);
    QVERIFY(!editor->isEditingAllowed());
    QCOMPARE(editor->getEditingRefusalReason(), QStringLiteral("The document is locked. Unlock it to make changes."));
    QVERIFY(!editor->createItemAtCurrentView());
    QVERIFY(!editor->renameItem({ 0 }, QStringLiteral("x")));
    QVERIFY(!editor->deleteItem({ 0 }));
    QVERIFY(!editor->moveItem({ 0 }, {}, 3));
    QVERIFY(!editor->setItemStyle({ 0 }, true, false));
    QVERIFY(!editor->setItemTargetToCurrentView({ 0 }));
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(tab->getDocument()), 0);
    tab->getDocument()->setLocked(false);
    QVERIFY(editor->isEditingAllowed());

    // Invalid paths
    QVERIFY(!editor->renameItem({ 7 }, QStringLiteral("x")));
    QVERIFY(!editor->deleteItem({}));
    QVERIFY(!editor->deleteItem({ 1, 5 }));
    QVERIFY(!editor->moveItem({ 9 }, {}, 0));
    QVERIFY(!editor->setItemStyle({ 3, 3 }, true, true));
    QVERIFY(!editor->finishCreation(QStringLiteral("nothing pending")));
    editor->cancelCreation();
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(tab->getDocument()), 0);

    // A view without a document
    pdf::PDFAppDocumentView view(fixture.context.get());
    QVERIFY(!view.getOutlineEditor()->isEditingAllowed());
    QVERIFY(!view.getOutlineEditor()->getCurrentViewDestination());
    QVERIFY(!view.getOutlineEditor()->createItemAtCurrentView());
    QCOMPARE(view.getOutlineEditor()->getEditingRefusalReason(), QStringLiteral("No document is shown."));
}

void AppNotesOutlineTest::outlineOperations()
{
    using Operations = pdf::PDFAppAnnotationOperations;
    using H = NotesOutlineTestHelper;

    QTemporaryDir folder;
    const pdf::PDFDocumentPointer document = H::read(pdftest::AppDocumentSamples::outline(folder.path()));
    QVERIFY(document);
    QCOMPARE(H::titles(document.data()), QStringList({ "Introduction", "Chapter 1", ">Section 1.1", "Appendix" }));
    QVERIFY(Operations::isOutlineEditable(document.data()));
    const std::vector<pdf::PDFObjectReference> references = Operations::getOutlineItemReferences(document.data());
    QCOMPARE(references.size(), size_t(4));

    const pdf::PDFObjectReference page3 = document->getCatalog()->getPage(3)->getPageReference();
    const pdf::PDFDestination destination = pdf::PDFDestination::createXYZ(page3, std::numeric_limits<double>::quiet_NaN(), 200.0, std::numeric_limits<double>::quiet_NaN());

    // Insert (the index past the end = the last child); only the flag Outline
    pdf::PDFAppAnnotationResult result = Operations::insertOutlineItem(document, { 1 }, 99, QStringLiteral("Section 1.2"), destination);
    QVERIFY(result);
    QCOMPARE(result->getFlags(), pdf::PDFModifiedDocument::ModificationFlags(pdf::PDFModifiedDocument::Outline));
    pdf::PDFDocumentPointer changed = H::apply(result);
    QCOMPARE(H::titles(changed.data()), QStringList({ "Introduction", "Chapter 1", ">Section 1.1", ">Section 1.2", "Appendix" }));
    QCOMPARE(H::checkStructure(changed.data()), QString());
    const std::vector<pdf::PDFObjectReference> newReferences = Operations::getOutlineItemReferences(changed.data());
    QCOMPARE(newReferences.size(), size_t(5));
    QCOMPARE(newReferences[0], references[0]);
    QCOMPARE(newReferences[2], references[2]);
    QCOMPARE(newReferences[4], references[3]);
    const pdf::PDFOutlineItem* inserted = changed->getCatalog()->getOutlineRootPtr()->getChild(1)->getChild(1);
    const pdf::PDFActionGoTo* goTo = dynamic_cast<const pdf::PDFActionGoTo*>(inserted->getAction());
    QVERIFY(goTo);
    QCOMPARE(goTo->getDestination().getDestinationType(), pdf::DestinationType::XYZ);
    QCOMPARE(goTo->getDestination().getPageReference(), page3);
    QCOMPARE(goTo->getDestination().getTop(), 200.0);

    // Insert at the start of the top level, the default and invalid arguments
    QCOMPARE(H::titles(H::apply(Operations::insertOutlineItem(document, {}, 0, QStringLiteral("  First\titem\n "), destination)).data()).front(), QStringLiteral("First item"));
    QVERIFY(!Operations::insertOutlineItem(document, {}, 0, QStringLiteral(" \x01 "), destination));
    QVERIFY(!Operations::insertOutlineItem(document, { 5 }, 0, QStringLiteral("x"), destination));
    QVERIFY(!Operations::insertOutlineItem(document, {}, 0, QStringLiteral("x"), pdf::PDFDestination()));
    QVERIFY(!Operations::insertOutlineItem(document, {}, 0, QStringLiteral("x"), pdf::PDFDestination::createFit(pdf::PDFObjectReference(9999, 0))));
    QVERIFY(!Operations::insertOutlineItem(document, {}, 0, QStringLiteral("x"), pdf::PDFDestination::createXYZ(page3, std::numeric_limits<double>::infinity(), 0, 1)));
    QVERIFY(!Operations::insertOutlineItem(pdf::PDFDocumentPointer(), {}, 0, QStringLiteral("x"), destination));
    pdf::PDFDestination byIndex;
    byIndex.setDestinationType(pdf::DestinationType::Fit);
    byIndex.setPageIndex(2);
    QVERIFY(Operations::insertOutlineItem(document, {}, 0, QStringLiteral("By index"), byIndex));
    byIndex.setPageIndex(50);
    QVERIFY(!Operations::insertOutlineItem(document, {}, 0, QStringLiteral("By index"), byIndex));
    QVERIFY(Operations::insertOutlineItem(document, {}, 0, QStringLiteral("Named"), pdf::PDFDestination::createNamed(QByteArray("chapter"))));
    QVERIFY(!Operations::insertOutlineItem(document, {}, 0, QStringLiteral("Named"), pdf::PDFDestination::createNamed(QByteArray())));
    pdf::PDFDestination huge = pdf::PDFDestination::createXYZ(page3, 1.0e20, -1.0e20, 1.0e9);
    changed = H::apply(Operations::insertOutlineItem(document, {}, 0, QStringLiteral("Huge"), huge));
    QVERIFY(changed);
    goTo = dynamic_cast<const pdf::PDFActionGoTo*>(changed->getCatalog()->getOutlineRootPtr()->getChild(0)->getAction());
    QVERIFY(goTo && qAbs(goTo->getDestination().getLeft()) <= 1.0e7 && goTo->getDestination().getZoom() <= 640.0);

    // Rename (the same title = nothing)
    changed = H::apply(Operations::renameOutlineItem(document, { 1, 0 }, QStringLiteral("Renamed section")));
    QCOMPARE(H::titles(changed.data()), QStringList({ "Introduction", "Chapter 1", ">Renamed section", "Appendix" }));
    QCOMPARE(Operations::getOutlineItemReferences(changed.data()), references);
    QVERIFY(!Operations::renameOutlineItem(document, { 1, 0 }, QStringLiteral("Section 1.1")));
    QVERIFY(!Operations::renameOutlineItem(document, {}, QStringLiteral("Root")));
    QVERIFY(!Operations::renameOutlineItem(document, { 0 }, QString()));
    QVERIFY(!Operations::renameOutlineItem(document, { 0, 0 }, QStringLiteral("x")));
    const QString longTitle(5000, QChar(u'w'));
    changed = H::apply(Operations::renameOutlineItem(document, { 0 }, longTitle));
    QCOMPARE(H::titles(changed.data()).front().size(), qsizetype(Operations::MAXIMUM_OUTLINE_TITLE_LENGTH));

    // Remove (with the children; the removed objects become null)
    changed = H::apply(Operations::removeOutlineItem(document, { 1 }));
    QCOMPARE(H::titles(changed.data()), QStringList({ "Introduction", "Appendix" }));
    QCOMPARE(H::checkStructure(changed.data()), QString());
    QVERIFY(changed->getStorage().getObjectByReference(references[1]).isNull());
    QVERIFY(changed->getStorage().getObjectByReference(references[2]).isNull());
    QVERIFY(!Operations::removeOutlineItem(document, {}));
    QVERIFY(!Operations::removeOutlineItem(document, { 4 }));

    // Remove all items: the outline dictionary stays without items
    pdf::PDFDocumentPointer empty = H::apply(Operations::removeOutlineItem(document, { 0 }));
    empty = H::apply(Operations::removeOutlineItem(empty, { 0 }));
    empty = H::apply(Operations::removeOutlineItem(empty, { 0 }));
    QVERIFY(empty);
    QVERIFY(H::titles(empty.data()).isEmpty());
    QVERIFY(Operations::isOutlineEditable(empty.data()));
    changed = H::apply(Operations::insertOutlineItem(empty, {}, 0, QStringLiteral("Again"), destination));
    QCOMPARE(H::titles(changed.data()), QStringList({ "Again" }));
    QCOMPARE(H::checkStructure(changed.data()), QString());

    // Move: up, down, into an item, out of it; the index is of the children before the move
    Operations::OutlinePath newPath;
    changed = H::apply(Operations::moveOutlineItem(document, { 2 }, {}, 0, &newPath));
    QCOMPARE(H::titles(changed.data()), QStringList({ "Appendix", "Introduction", "Chapter 1", ">Section 1.1" }));
    QCOMPARE(newPath, Operations::OutlinePath({ 0 }));
    QCOMPARE(H::checkStructure(changed.data()), QString());
    changed = H::apply(Operations::moveOutlineItem(document, { 0 }, {}, 3, &newPath));
    QCOMPARE(H::titles(changed.data()), QStringList({ "Chapter 1", ">Section 1.1", "Appendix", "Introduction" }));
    QCOMPARE(newPath, Operations::OutlinePath({ 2 }));
    changed = H::apply(Operations::moveOutlineItem(document, { 2 }, { 1 }, 0, &newPath));
    QCOMPARE(H::titles(changed.data()), QStringList({ "Introduction", "Chapter 1", ">Appendix", ">Section 1.1" }));
    QCOMPARE(newPath, Operations::OutlinePath({ 1, 0 }));
    changed = H::apply(Operations::moveOutlineItem(document, { 1, 0 }, {}, 1, &newPath));
    QCOMPARE(H::titles(changed.data()), QStringList({ "Introduction", "Section 1.1", "Chapter 1", "Appendix" }));
    QCOMPARE(H::checkStructure(changed.data()), QString());
    QCOMPARE(Operations::getOutlineItemReferences(changed.data())[1], references[2]);
    QVERIFY(!Operations::moveOutlineItem(document, { 1 }, { 1, 0 }, 0, nullptr));     // into its own child
    QVERIFY(!Operations::moveOutlineItem(document, { 1 }, { 1 }, 0, nullptr));        // into itself
    QVERIFY(!Operations::moveOutlineItem(document, { 1 }, {}, 1, nullptr));           // the same place
    QVERIFY(!Operations::moveOutlineItem(document, { 1 }, {}, 2, nullptr));           // the same place
    QVERIFY(!Operations::moveOutlineItem(document, { 7 }, {}, 0, nullptr));
    QVERIFY(!Operations::moveOutlineItem(document, { 0 }, { 8 }, 0, nullptr));
    QVERIFY(!Operations::moveOutlineItem(document, {}, {}, 0, nullptr));

    // Style: bits 1 and 2 of /F
    changed = H::apply(Operations::setOutlineItemStyle(document, { 0 }, true, false));
    QVERIFY(changed->getCatalog()->getOutlineRootPtr()->getChild(0)->isFontBold());
    QVERIFY(!changed->getCatalog()->getOutlineRootPtr()->getChild(0)->isFontItalics());
    changed = H::apply(Operations::setOutlineItemStyle(changed, { 0 }, false, true));
    QVERIFY(!changed->getCatalog()->getOutlineRootPtr()->getChild(0)->isFontBold());
    QVERIFY(changed->getCatalog()->getOutlineRootPtr()->getChild(0)->isFontItalics());
    QVERIFY(!Operations::setOutlineItemStyle(changed, { 0 }, false, true));
    QVERIFY(!Operations::setOutlineItemStyle(document, { 0 }, false, false));

    // Destination
    changed = H::apply(Operations::setOutlineItemDestination(document, { 0 }, destination));
    QCOMPARE(pdf::PDFAppPanelWidgets::getActionPage(changed->getCatalog(), changed->getCatalog()->getOutlineRootPtr()->getChild(0)->getAction()), pdf::PDFInteger(3));
    QVERIFY(!Operations::setOutlineItemDestination(document, { 0 }, pdf::PDFDestination()));
    QVERIFY(!Operations::setOutlineItemDestination(document, {}, destination));

    // Replace the whole outline (also a null root = no items)
    QSharedPointer<pdf::PDFOutlineItem> root(new pdf::PDFOutlineItem());
    QSharedPointer<pdf::PDFOutlineItem> part(new pdf::PDFOutlineItem());
    part->setTitle(QStringLiteral("Part"));
    part->setFontBold(true);
    part->setTextColor(QColor(Qt::red));
    part->setAction(pdf::PDFActionPtr(new pdf::PDFActionGoTo(destination, pdf::PDFDestination())));
    QSharedPointer<pdf::PDFOutlineItem> part2(new pdf::PDFOutlineItem());
    part2->setTitle(QStringLiteral("Part 2"));
    part->addChild(part2);
    root->addChild(part);
    changed = H::apply(Operations::setOutline(document, root));
    QCOMPARE(H::titles(changed.data()), QStringList({ "Part", ">Part 2" }));
    QCOMPARE(H::checkStructure(changed.data()), QString());
    QVERIFY(changed->getCatalog()->getOutlineRootPtr()->getChild(0)->isFontBold());
    QCOMPARE(changed->getCatalog()->getOutlineRootPtr()->getChild(0)->getTextColor(), QColor(Qt::red));
    for (const pdf::PDFObjectReference& reference : references)
    {
        QVERIFY(changed->getStorage().getObjectByReference(reference).isNull());
    }
    changed = H::apply(Operations::setOutline(document, QSharedPointer<pdf::PDFOutlineItem>()));
    QVERIFY(H::titles(changed.data()).isEmpty());
    QVERIFY(!Operations::setOutline(pdf::PDFDocumentPointer(), root));

    // A document without an outline gets one by the first item
    const pdf::PDFDocumentPointer plain = H::read(pdftest::AppDocumentSamples::simple(folder.path(), QStringLiteral("plain.pdf"), 2));
    QVERIFY(plain);
    QVERIFY(Operations::isOutlineEditable(plain.data()));
    QVERIFY(Operations::getOutlineItemReferences(plain.data()).empty());
    changed = H::apply(Operations::insertOutlineItem(plain, {}, 0, QStringLiteral("First"), pdf::PDFDestination::createFit(plain->getCatalog()->getPage(1)->getPageReference())));
    QCOMPARE(H::titles(changed.data()), QStringList({ "First" }));
    QCOMPARE(H::checkStructure(changed.data()), QString());

    // The result is written and read again
    const QString writtenPath = QDir(folder.path()).filePath(QStringLiteral("written.pdf"));
    changed = H::apply(Operations::moveOutlineItem(document, { 2 }, { 1 }, 99, nullptr));
    QVERIFY(pdftest::AppDocumentSamples::write(*changed, writtenPath));
    const pdf::PDFDocumentPointer reread = H::read(writtenPath);
    QCOMPARE(H::titles(reread.data()), QStringList({ "Introduction", "Chapter 1", ">Section 1.1", ">Appendix" }));
    QCOMPARE(H::checkStructure(reread.data()), QString());

    // Titles of the user
    const QString separators = QStringLiteral(" a") + QChar(0x02) + QChar(0x1B) + QStringLiteral(" b") + QChar(0x2028) + QStringLiteral("c") + QChar(0x200E) + QStringLiteral(" ");
    QCOMPARE(Operations::sanitizeOutlineTitle(separators), QStringLiteral("a b c"));
    QCOMPARE(Operations::sanitizeOutlineTitle(QString(3000, QChar(u'x'))).size(), qsizetype(Operations::MAXIMUM_OUTLINE_TITLE_LENGTH));
}

void AppNotesOutlineTest::outlineOperationsKeepEntries()
{
    using Operations = pdf::PDFAppAnnotationOperations;
    using H = NotesOutlineTestHelper;

    QTemporaryDir folder;
    const pdf::PDFDocumentPointer document = H::read(H::richOutline(folder.path()));
    QVERIFY(document);
    QVERIFY(Operations::isOutlineEditable(document.data()));
    const std::vector<pdf::PDFObjectReference> references = Operations::getOutlineItemReferences(document.data());
    QCOMPARE(references.size(), size_t(3));

    // A rename of another item keeps the URI action, the unknown key, the color
    // and the closed state
    const pdf::PDFDocumentPointer changed = H::apply(Operations::renameOutlineItem(document, { 1, 0 }, QStringLiteral("Child 2")));
    QVERIFY(changed);
    QCOMPARE(H::checkStructure(changed.data()), QString());
    const pdf::PDFObjectStorage& storage = changed->getStorage();
    const pdf::PDFDictionary* web = storage.getDictionaryFromObject(storage.getObjectByReference(references[0]));
    QVERIFY(web);
    QVERIFY(web->hasKey("A"));
    QCOMPARE(web->get("XCustom").getInteger(), pdf::PDFInteger(42));
    QVERIFY(web->hasKey("C"));
    const pdf::PDFDictionary* closed = storage.getDictionaryFromObject(storage.getObjectByReference(references[1]));
    QCOMPARE(closed->get("Count").getInteger(), pdf::PDFInteger(-1));
    const pdf::PDFOutlineItem* webItem = changed->getCatalog()->getOutlineRootPtr()->getChild(0);
    QVERIFY(webItem->getAction());
    QCOMPARE(webItem->getAction()->getType(), pdf::ActionType::URI);

    // A new child of the closed item opens it (the item is visible in other readers)
    const pdf::PDFDocumentPointer inserted = H::apply(Operations::insertOutlineItem(document, { 1 }, 0, QStringLiteral("New child"),
                                                                                    pdf::PDFDestination::createFit(document->getCatalog()->getPage(0)->getPageReference())));
    const pdf::PDFDictionary* opened = inserted->getStorage().getDictionaryFromObject(inserted->getStorage().getObjectByReference(references[1]));
    QCOMPARE(opened->get("Count").getInteger(), pdf::PDFInteger(2));
    QCOMPARE(H::checkStructure(inserted.data()), QString());

    // The target replaces the action
    const pdf::PDFDocumentPointer target = H::apply(Operations::setOutlineItemDestination(document, { 0 }, pdf::PDFDestination::createFit(document->getCatalog()->getPage(2)->getPageReference())));
    const pdf::PDFDictionary* retargeted = target->getStorage().getDictionaryFromObject(target->getStorage().getObjectByReference(references[0]));
    QVERIFY(!retargeted->hasKey("A"));
    QVERIFY(retargeted->hasKey("Dest"));
    QCOMPARE(retargeted->get("XCustom").getInteger(), pdf::PDFInteger(42));
}

void AppNotesOutlineTest::outlineHostile()
{
    using Operations = pdf::PDFAppAnnotationOperations;
    using H = NotesOutlineTestHelper;

    QTemporaryDir folder;

    // A cycle of the items: the parser shows A and B, the edit writes a clean outline
    pdf::PDFDocumentPointer document = H::read(H::hostileOutline(folder.path(), QStringLiteral("cycle")));
    QVERIFY(document);
    QCOMPARE(H::titles(document.data()), QStringList({ "A", "B" }));
    QVERIFY(Operations::isOutlineEditable(document.data()));
    pdf::PDFDocumentPointer changed = H::apply(Operations::renameOutlineItem(document, { 1 }, QStringLiteral("B2")));
    QCOMPARE(H::titles(changed.data()), QStringList({ "A", "B2" }));
    QCOMPARE(H::checkStructure(changed.data()), QString());

    // Shared children: the paths of the parser do not address unique items - read only
    document = H::read(H::hostileOutline(folder.path(), QStringLiteral("shared")));
    QCOMPARE(H::titles(document.data()), QStringList({ "A", ">C", "B", ">C" }));
    QVERIFY(!Operations::isOutlineEditable(document.data()));
    QVERIFY(Operations::getOutlineItemReferences(document.data()).empty());
    QVERIFY(!Operations::renameOutlineItem(document, { 0 }, QStringLiteral("x")));
    QVERIFY(!Operations::insertOutlineItem(document, {}, 0, QStringLiteral("x"), pdf::PDFDestination::createFit(document->getCatalog()->getPage(0)->getPageReference())));

    // /Outlines is not a dictionary: the first item creates a new outline dictionary
    document = H::read(H::hostileOutline(folder.path(), QStringLiteral("notDictionary")));
    QVERIFY(Operations::isOutlineEditable(document.data()));
    changed = H::apply(Operations::insertOutlineItem(document, {}, 0, QStringLiteral("x"), pdf::PDFDestination::createFit(document->getCatalog()->getPage(0)->getPageReference())));
    QCOMPARE(H::titles(changed.data()), QStringList({ "x" }));
    QCOMPARE(H::checkStructure(changed.data()), QString());

    // A title of a number and of control characters; a /Next of a missing object
    document = H::read(H::hostileOutline(folder.path(), QStringLiteral("badTitle")));
    QVERIFY(Operations::isOutlineEditable(document.data()));
    changed = H::apply(Operations::moveOutlineItem(document, { 1 }, {}, 0, nullptr));
    QCOMPARE(H::checkStructure(changed.data()), QString());
    document = H::read(H::hostileOutline(folder.path(), QStringLiteral("brokenNext")));
    QCOMPARE(H::titles(document.data()), QStringList({ "A" }));
    changed = H::apply(Operations::renameOutlineItem(document, { 0 }, QStringLiteral("A2")));
    QCOMPARE(H::titles(changed.data()), QStringList({ "A2" }));
    QCOMPARE(H::checkStructure(changed.data()), QString());

    // An item, which is the page or the document information (security review RS4):
    // the editing would write /Parent, /Prev, /Next into the page (its page tree) or
    // empty the object of a removed item - such an outline is read only, a new
    // outline replacing it leaves the objects untouched
    for (const QString& kind : { QStringLiteral("pageItem"), QStringLiteral("infoItem"), QStringLiteral("annotationItem") })
    {
        document = H::read(H::hostileOutline(folder.path(), kind));
        QVERIFY2(document, qPrintable(kind));
        QCOMPARE(H::titles(document.data()).size(), 2);
        QVERIFY2(!Operations::isOutlineEditable(document.data()), qPrintable(kind));
        QVERIFY(Operations::getOutlineItemReferences(document.data()).empty());
        QVERIFY(!Operations::renameOutlineItem(document, { 0 }, QStringLiteral("x")));
        QVERIFY(!Operations::removeOutlineItem(document, { 1 }));

        const pdf::PDFObjectReference page = document->getCatalog()->getPage(0)->getPageReference();
        const pdf::PDFObject pageObject = document->getObjectByReference(page);
        const pdf::PDFDictionary* trailer = document->getTrailerDictionary();
        QVERIFY(trailer);
        const pdf::PDFObjectReference info = trailer->get("Info").isReference() ? trailer->get("Info").getReference() : pdf::PDFObjectReference();
        const pdf::PDFObject infoObject = info.isValid() ? document->getObjectByReference(info) : pdf::PDFObject();
        changed = H::apply(Operations::setOutline(document, nullptr));
        QVERIFY(changed);
        QCOMPARE(changed->getCatalog()->getPageCount(), size_t(1));
        QCOMPARE(changed->getObjectByReference(page), pageObject);
        if (info.isValid())
        {
            QCOMPARE(changed->getObjectByReference(info), infoObject);
        }
    }

    // An item, which is the interactive form (review RC-CD): the outline is
    // editable, a removed item is emptied only when it is provably an item of
    // this outline referenced by nothing else - the form is only unlinked
    for (const QString& kind : { QStringLiteral("acroFormItem"), QStringLiteral("titledAcroForm") })
    {
        document = H::read(H::hostileOutline(folder.path(), kind));
        QVERIFY2(document, qPrintable(kind));
        QCOMPARE(H::titles(document.data()).size(), 2);
        QVERIFY2(Operations::isOutlineEditable(document.data()), qPrintable(kind));
        const std::vector<pdf::PDFObjectReference> items = Operations::getOutlineItemReferences(document.data());
        QCOMPARE(items.size(), size_t(2));
        const pdf::PDFObject formObject = document->getObjectByReference(items[1]);
        QVERIFY(formObject.isDictionary() && formObject.getDictionary()->hasKey("Fields"));

        // The form removed: unlinked, the object of the form unchanged
        changed = H::apply(Operations::removeOutlineItem(document, { 1 }));
        QVERIFY2(changed, qPrintable(kind));
        QCOMPARE(H::titles(changed.data()), QStringList({ "A" }));
        QCOMPARE(H::checkStructure(changed.data()), QString());
        QCOMPARE(changed->getObjectByReference(items[1]), formObject);

        // The real item removed: emptied (only the outline referenced it)
        changed = H::apply(Operations::removeOutlineItem(document, { 0 }));
        QVERIFY2(changed, qPrintable(kind));
        QCOMPARE(H::titles(changed.data()).size(), 1);
        QVERIFY(changed->getObjectByReference(items[0]).isNull());
        QVERIFY(changed->getObjectByReference(items[1]).isDictionary());
        QVERIFY(changed->getObjectByReference(items[1]).getDictionary()->hasKey("Fields"));

        // A new outline: the old real item emptied, the form untouched
        changed = H::apply(Operations::setOutline(document, nullptr));
        QVERIFY2(changed, qPrintable(kind));
        QVERIFY(H::titles(changed.data()).isEmpty());
        QVERIFY(changed->getObjectByReference(items[0]).isNull());
        QCOMPARE(changed->getObjectByReference(items[1]), formObject);
    }

    // A removed item, which another object of the document references (here an
    // entry of the catalog), is unlinked, not emptied
    {
        document = H::read(H::richOutline(folder.path()));
        QVERIFY(document);
        const std::vector<pdf::PDFObjectReference> items = Operations::getOutlineItemReferences(document.data());
        QCOMPARE(items.size(), size_t(3));
        const pdf::PDFObject childObject = document->getObjectByReference(items[2]);
        pdf::PDFDocumentBuilder builder(document.data());
        builder.mergeTo(builder.getCatalogReference(), H::dictionary({ { "XReferencesChild", H::reference(items[2]) } }));
        pdf::PDFDocumentPointer referencing(new pdf::PDFDocument(builder.build()));
        changed = H::apply(Operations::removeOutlineItem(referencing, { 1 }));
        QVERIFY(changed);
        QCOMPARE(H::titles(changed.data()), QStringList({ "Web" }));
        QVERIFY(changed->getObjectByReference(items[1]).isNull());
        QCOMPARE(changed->getObjectByReference(items[2]), childObject);
    }

    // Too deep: read only; at the limit: editable, but nothing goes deeper
    document = H::read(H::deepOutline(folder.path(), int(Operations::MAXIMUM_OUTLINE_DEPTH) + 1));
    QVERIFY(document);
    QVERIFY(!Operations::isOutlineEditable(document.data()));
    document = H::read(H::deepOutline(folder.path(), int(Operations::MAXIMUM_OUTLINE_DEPTH)));
    QVERIFY(Operations::isOutlineEditable(document.data()));
    Operations::OutlinePath deepest(Operations::MAXIMUM_OUTLINE_DEPTH, 0);
    QVERIFY(!Operations::insertOutlineItem(document, deepest, 0, QStringLiteral("x"), pdf::PDFDestination::createFit(document->getCatalog()->getPage(0)->getPageReference())));
    QVERIFY(Operations::renameOutlineItem(document, deepest, QStringLiteral("Deepest")));
    QVERIFY(!Operations::moveOutlineItem(document, { 0, 0 }, deepest, 0, nullptr));

    // The panel of a read-only outline shows it without the editing
    {
        pdftest::AppDocumentFixture fixture;
        pdf::PDFAppDocumentTab* tab = fixture.open(H::hostileOutline(fixture.folder(), QStringLiteral("shared")));
        QVERIFY(tab);
        pdf::PDFAppOutlinePanel* panel = H::outline(tab);
        QVERIFY(panel);
        fixture.settle();
        QCOMPARE(panel->getItemCount(), 4);
        QVERIFY(!tab->getView()->getOutlineEditor()->isEditingAllowed());
        QCOMPARE(tab->getView()->getOutlineEditor()->getEditingRefusalReason(), QStringLiteral("The outline of this document cannot be edited."));
        QVERIFY(panel->getNewItemButton() && panel->getNewItemButton()->isHidden());
        QVERIFY(!tab->isCommandEnabled(pdf::PDFAppCommands::outlineNewItem()));
        QVERIFY(!panel->editItem({ 0 }));
        QMenu* menu = panel->createContextMenu(panel->getItemIndex({ 0 }));
        QCOMPARE(H::actionNames(menu), QStringList({ "outlineGoTo" }));
        delete menu;
    }

    // Dragged data of another document, another version or garbage
    const QString key = QStringLiteral("DOC:3");
    QMimeData data;
    QVERIFY(!pdf::PDFAppOutlineItemModel::getDraggedPath(nullptr, key));
    QVERIFY(!pdf::PDFAppOutlineItemModel::getDraggedPath(&data, key));
    for (const QByteArray& bytes : { QByteArray("DOC:3"), QByteArray("DOC:4\n0"), QByteArray("DOC:3\n"), QByteArray("DOC:3\n0,-1"), QByteArray("DOC:3\n0,x"),
                                     QByteArray("DOC:3\n99999999999"), QByteArray("DOC:3\n") + QByteArray(20000, '1'), QByteArray("\n0") })
    {
        data.setData(pdf::PDFAppOutlineItemModel::getMimeType(), bytes);
        QVERIFY2(!pdf::PDFAppOutlineItemModel::getDraggedPath(&data, key), bytes.left(40).constData());
    }
    QByteArray deepPath = "DOC:3\n0";
    for (int i = 0; i < 300; ++i)
    {
        deepPath += ",0";
    }
    data.setData(pdf::PDFAppOutlineItemModel::getMimeType(), deepPath);
    QVERIFY(!pdf::PDFAppOutlineItemModel::getDraggedPath(&data, key));
    data.setData(pdf::PDFAppOutlineItemModel::getMimeType(), QByteArray("DOC:3\n1,0"));
    QCOMPARE(*pdf::PDFAppOutlineItemModel::getDraggedPath(&data, key), H::Path({ 1, 0 }));
    QVERIFY(!pdf::PDFAppOutlineItemModel::getDraggedPath(&data, QString()));
}

void AppNotesOutlineTest::outlineNewItem()
{
    using H = NotesOutlineTestHelper;

    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::outline(fixture.folder()));
    QVERIFY(tab);
    pdf::PDFAppOutlinePanel* panel = H::outline(tab);
    QVERIFY(panel);
    fixture.settle();
    pdf::PDFAppDocument* document = tab->getDocument();
    pdf::PDFAppOutlineEditor* editor = tab->getView()->getOutlineEditor();

    // "+" (195:1485) after the current item, Enter writes the typed title
    QToolButton* button = panel->getNewItemButton();
    QVERIFY(button && !button->isHidden());
    QCOMPARE(button->size(), QSize(32, 32));
    H::selectItem(panel, { 1 });
    tab->getView()->goToPage(3);
    fixture.settle();
    H::selectItem(panel, { 1 });
    button->click();
    fixture.settle();
    QVERIFY(editor->isCreationPending());
    QVERIFY(panel->getPendingItem());
    QCOMPARE(H::titles(panel->getSourceModel()), QStringList({ "Introduction", "Chapter 1", ">Section 1.1", "New item", "Appendix" }));
    QLineEdit* edit = H::inlineEditor(panel);
    QVERIFY(edit);
    QCOMPARE(edit->selectedText(), QStringLiteral("New item"));
    QTest::keyClicks(edit, QStringLiteral("Summary"));
    QTest::keyClick(edit, Qt::Key_Enter);
    fixture.settle();
    QVERIFY(!editor->isCreationPending());
    QCOMPARE(H::titles(document->getDocument().data()), QStringList({ "Introduction", "Chapter 1", ">Section 1.1", "Summary", "Appendix" }));
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 1);
    QCOMPARE(document->getUndoStack()->getUndoDescription(), QStringLiteral("New outline item"));
    QCOMPARE(panel->getCurrentItem().data().toString(), QStringLiteral("Summary"));
    QCOMPARE(editor->getCurrentItem(), H::Path({ 2 }));
    const pdf::PDFOutlineItem* summary = document->getDocument()->getCatalog()->getOutlineRootPtr()->getChild(2);
    QCOMPARE(pdf::PDFAppPanelWidgets::getActionPage(document->getDocument()->getCatalog(), summary->getAction()), pdf::PDFInteger(3));
    QCOMPARE(H::checkStructure(document->getDocument().data()), QString());

    // Esc during the creation: no step, no item
    tab->executeCommand(pdf::PDFAppCommands::outlineNewItem());
    fixture.settle();
    QVERIFY(editor->isCreationPending());
    edit = H::inlineEditor(panel);
    QVERIFY(edit);
    QTest::keyClicks(edit, QStringLiteral("Canceled"));
    QTest::keyClick(edit, Qt::Key_Escape);
    fixture.settle();
    QVERIFY(!editor->isCreationPending());
    QVERIFY(!panel->getPendingItem());
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 1);
    QCOMPARE(panel->getItemCount(), 5);
    QCOMPARE(H::titles(panel->getSourceModel()), H::titles(document->getDocument().data()));

    // The focus leaves the editor: the default title is written
    tab->executeCommand(pdf::PDFAppCommands::outlineNewItem());
    fixture.settle();
    edit = H::inlineEditor(panel);
    QVERIFY(edit);
    panel->getSearchField()->setFocus();
    fixture.settle();
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 2);
    QVERIFY(H::titles(document->getDocument().data()).contains(QStringLiteral("New item")));

    // An empty title is the default title; undo removes the items again
    tab->executeCommand(pdf::PDFAppCommands::outlineNewItem());
    fixture.settle();
    edit = H::inlineEditor(panel);
    QVERIFY(edit);
    edit->clear();
    QTest::keyClick(edit, Qt::Key_Return);
    fixture.settle();
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 3);
    QCOMPARE(H::titles(document->getDocument().data()).count(QStringLiteral("New item")), 2);
    tab->executeCommand(pdf::PDFAppCommands::undo());
    tab->executeCommand(pdf::PDFAppCommands::undo());
    tab->executeCommand(pdf::PDFAppCommands::undo());
    fixture.settle();
    QCOMPARE(H::titles(document->getDocument().data()), QStringList({ "Introduction", "Chapter 1", ">Section 1.1", "Appendix" }));
    QCOMPARE(H::titles(panel->getSourceModel()), H::titles(document->getDocument().data()));

    // A change of the document during the creation ends it
    tab->executeCommand(pdf::PDFAppCommands::outlineNewItem());
    fixture.settle();
    QVERIFY(editor->isCreationPending());
    tab->executeCommand(pdf::PDFAppCommands::redo());
    fixture.settle();
    QVERIFY(!editor->isCreationPending());
    QVERIFY(!panel->getPendingItem());
    QCOMPARE(H::titles(panel->getSourceModel()), H::titles(document->getDocument().data()));

    // The search filter is cleared for the new item; Insert of the tree starts it too
    panel->getSearchField()->setText(QStringLiteral("appendix"));
    panel->getTreeView()->setFocus();
    H::selectItem(panel, { 0 });
    QTest::keyClick(panel->getTreeView(), Qt::Key_Insert);
    fixture.settle();
    QVERIFY(editor->isCreationPending());
    QVERIFY(panel->getSearchField()->text().isEmpty());
    editor->cancelCreation();
    fixture.settle();
    QVERIFY(!panel->getPendingItem());
}

void AppNotesOutlineTest::outlineNewItemWithoutOutline()
{
    using H = NotesOutlineTestHelper;

    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("plain.pdf"), 3));
    QVERIFY(tab);
    fixture.settle();
    pdf::PDFAppDocumentSidebar* sidebar = H::sidebar(tab);
    QVERIFY(!sidebar->isTabVisible(pdf::PDFAppDocumentPanelIds::outline()));
    const QString previousPanel = sidebar->getCurrentPanelId();
    pdf::PDFAppOutlineEditor* editor = tab->getView()->getOutlineEditor();
    QVERIFY(editor->isEditingAllowed());
    QVERIFY(tab->isCommandEnabled(pdf::PDFAppCommands::outlineNewItem()));

    // The Outline tab appears for the new item; Esc hides it again
    QVERIFY(tab->executeCommand(pdf::PDFAppCommands::outlineNewItem()));
    fixture.settle();
    QVERIFY(editor->isCreationPending());
    QCOMPARE(sidebar->getCurrentPanelId(), pdf::PDFAppDocumentPanelIds::outline());
    QVERIFY(sidebar->isTabVisible(pdf::PDFAppDocumentPanelIds::outline()));
    pdf::PDFAppOutlinePanel* panel = qobject_cast<pdf::PDFAppOutlinePanel*>(sidebar->findPanel(pdf::PDFAppDocumentPanelIds::outline()));
    QVERIFY(panel);
    QLineEdit* edit = H::inlineEditor(panel);
    QVERIFY(edit);
    QTest::keyClick(edit, Qt::Key_Escape);
    fixture.settle();
    QVERIFY(!editor->isCreationPending());
    QVERIFY(!sidebar->isTabVisible(pdf::PDFAppDocumentPanelIds::outline()));
    QCOMPARE(sidebar->getCurrentPanelId(), previousPanel);
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(tab->getDocument()), 0);

    // Enter creates the outline
    QVERIFY(tab->executeCommand(pdf::PDFAppCommands::outlineNewItem()));
    fixture.settle();
    edit = H::inlineEditor(panel);
    QVERIFY(edit);
    QTest::keyClicks(edit, QStringLiteral("Start"));
    QTest::keyClick(edit, Qt::Key_Enter);
    fixture.settle();
    QCOMPARE(H::titles(tab->getDocument()->getDocument().data()), QStringList({ "Start" }));
    QVERIFY(sidebar->isTabVisible(pdf::PDFAppDocumentPanelIds::outline()));
    QCOMPARE(panel->getSubtitle(), QStringLiteral("1 item"));

    // Undo removes the outline items and the tab
    tab->executeCommand(pdf::PDFAppCommands::undo());
    fixture.settle();
    QVERIFY(!sidebar->isTabVisible(pdf::PDFAppDocumentPanelIds::outline()));
}

void AppNotesOutlineTest::outlineInsertTile()
{
    using H = NotesOutlineTestHelper;

    // Insert › Outline item of package A calls createItemAtCurrentView: the sidebar
    // is shown, the Outline panel replaces the shown panel
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::outline(fixture.folder()));
    QVERIFY(tab);
    pdf::PDFAppDocumentView* view = tab->getView();
    QVERIFY(H::sidebar(tab)->showPanel(pdf::PDFAppDocumentPanelIds::thumbnails()));
    view->setSidebarVisible(false);
    fixture.settle();
    view->goToPage(4);
    fixture.settle();

    pdf::PDFAppOutlineEditor* editor = view->getOutlineEditor();
    editor->setCurrentItem({});
    QVERIFY(editor->createItemAtCurrentView());
    fixture.settle();
    QVERIFY(view->isSidebarVisible());
    QCOMPARE(view->getCurrentPanelId(), pdf::PDFAppDocumentPanelIds::outline());
    pdf::PDFAppOutlinePanel* panel = qobject_cast<pdf::PDFAppOutlinePanel*>(H::sidebar(tab)->findPanel(pdf::PDFAppDocumentPanelIds::outline()));
    QVERIFY(panel && panel->getPendingItem());

    // A second request replaces the first one (one temporary item)
    QVERIFY(editor->createItemAtCurrentView());
    fixture.settle();
    QCOMPARE(panel->getSourceModel()->rowCount(), 4);
    QVERIFY(editor->finishCreation(QStringLiteral("From the tile")));
    fixture.settle();
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(tab->getDocument()), 1);
    const QStringList titles = H::titles(tab->getDocument()->getDocument().data());
    QVERIFY(titles.contains(QStringLiteral("From the tile")));
    QCOMPARE(panel->getCurrentItem().data().toString(), QStringLiteral("From the tile"));
}

void AppNotesOutlineTest::outlineRename()
{
    using H = NotesOutlineTestHelper;

    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::outline(fixture.folder()));
    pdf::PDFAppOutlinePanel* panel = H::outline(tab);
    QVERIFY(panel);
    fixture.settle();
    pdf::PDFAppDocument* document = tab->getDocument();

    // F2 (the action of the panel) opens the inline editor of the current item
    H::selectItem(panel, { 1, 0 });
    panel->getRenameAction()->trigger();
    fixture.settle();
    QLineEdit* edit = H::inlineEditor(panel);
    QVERIFY(edit);
    QCOMPARE(edit->text(), QStringLiteral("Section 1.1"));
    edit->setText(QStringLiteral("Section A"));
    QTest::keyClick(edit, Qt::Key_Enter);
    fixture.settle();
    QCOMPARE(H::titles(document->getDocument().data()), QStringList({ "Introduction", "Chapter 1", ">Section A", "Appendix" }));
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 1);
    QCOMPARE(document->getUndoStack()->getUndoDescription(), QStringLiteral("Rename outline item"));
    QCOMPARE(panel->getCurrentItem().data().toString(), QStringLiteral("Section A"));

    // The key itself in the tree
    fixture.window->activateWindow();
    panel->getTreeView()->setFocus();
    H::selectItem(panel, { 0 });
    QTest::keyClick(panel->getTreeView(), Qt::Key_F2);
    fixture.settle();
    edit = H::inlineEditor(panel);
    if (!edit)
    {
        // The shortcut map needs an active window (some platforms of the tests)
        panel->getRenameAction()->trigger();
        fixture.settle();
        edit = H::inlineEditor(panel);
    }
    QVERIFY(edit);

    // Esc: nothing
    edit->setText(QStringLiteral("Not written"));
    QTest::keyClick(edit, Qt::Key_Escape);
    fixture.settle();
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 1);
    QCOMPARE(panel->getItemIndex({ 0 }).data().toString(), QStringLiteral("Introduction"));

    // An empty title is refused, the title of the document is shown again
    QVERIFY(panel->editItem({ 0 }));
    edit = H::inlineEditor(panel);
    edit->setText(QStringLiteral(" \x01 "));
    QTest::keyClick(edit, Qt::Key_Enter);
    fixture.settle();
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 1);
    QCOMPARE(panel->getItemIndex({ 0 }).data().toString(), QStringLiteral("Introduction"));

    // The same title: no step; control characters are spaces
    QVERIFY(panel->editItem({ 0 }));
    QTest::keyClick(H::inlineEditor(panel), Qt::Key_Enter);
    fixture.settle();
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 1);
    QVERIFY(tab->getView()->getOutlineEditor()->renameItem({ 0 }, QStringLiteral("Intro\tduction\x7f")));
    QCOMPARE(H::titles(document->getDocument().data()).front(), QStringLiteral("Intro duction"));

    // The command opens the panel and its editor (the panel was hidden)
    QVERIFY(H::sidebar(tab)->showPanel(pdf::PDFAppDocumentPanelIds::thumbnails()));
    tab->getView()->getOutlineEditor()->setCurrentItem({ 2 });
    QVERIFY(tab->executeCommand(pdf::PDFAppCommands::outlineRename()));
    fixture.settle();
    QCOMPARE(tab->getView()->getCurrentPanelId(), pdf::PDFAppDocumentPanelIds::outline());
    edit = H::inlineEditor(panel);
    QVERIFY(edit);
    QCOMPARE(edit->text(), QStringLiteral("Appendix"));
    QTest::keyClick(edit, Qt::Key_Escape);
    fixture.settle();

    // A filtered item is shown for the rename
    panel->getSearchField()->setText(QStringLiteral("chapter"));
    QVERIFY(panel->editItem({ 2 }));
    QVERIFY(panel->getSearchField()->text().isEmpty());
    QTest::keyClick(H::inlineEditor(panel), Qt::Key_Escape);
    fixture.settle();
    QVERIFY(!panel->editItem({ 9 }));
}

void AppNotesOutlineTest::outlineDelete()
{
    using H = NotesOutlineTestHelper;

    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::outline(fixture.folder()));
    pdf::PDFAppOutlinePanel* panel = H::outline(tab);
    QVERIFY(panel);
    fixture.settle();
    pdf::PDFAppDocument* document = tab->getDocument();

    // Del in the tree: the item with its children, the next item becomes current
    panel->getTreeView()->setFocus();
    H::selectItem(panel, { 1 });
    QTest::keyClick(panel->getTreeView(), Qt::Key_Delete);
    fixture.settle();
    QCOMPARE(H::titles(document->getDocument().data()), QStringList({ "Introduction", "Appendix" }));
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 1);
    QCOMPARE(document->getUndoStack()->getUndoDescription(), QStringLiteral("Delete outline item"));
    QCOMPARE(panel->getCurrentItem().data().toString(), QStringLiteral("Appendix"));

    // The last item: the previous one becomes current; the context menu
    QMenu* menu = panel->createContextMenu(panel->getItemIndex({ 1 }));
    QAction* remove = H::findAction(menu, QStringLiteral("outlineMenu:") + pdf::PDFAppCommands::outlineDelete());
    QVERIFY(remove && remove->isEnabled());
    QVERIFY(remove->property("destructive").toBool());
    remove->trigger();
    fixture.settle();
    delete menu;
    QCOMPARE(H::titles(document->getDocument().data()), QStringList({ "Introduction" }));
    QCOMPARE(panel->getCurrentItem().data().toString(), QStringLiteral("Introduction"));

    // Undo brings both back with the child
    tab->executeCommand(pdf::PDFAppCommands::undo());
    tab->executeCommand(pdf::PDFAppCommands::undo());
    fixture.settle();
    QCOMPARE(H::titles(panel->getSourceModel()), QStringList({ "Introduction", "Chapter 1", ">Section 1.1", "Appendix" }));

    // The child item; the last item of a level selects its parent
    H::selectItem(panel, { 1, 0 });
    QVERIFY(tab->executeCommand(pdf::PDFAppCommands::outlineDelete()));
    fixture.settle();
    QCOMPARE(panel->getCurrentItem().data().toString(), QStringLiteral("Chapter 1"));

    // Deleting all items hides the Outline tab, the sidebar shows another panel
    while (H::titles(document->getDocument().data()).size() > 0)
    {
        H::selectItem(panel, { 0 });
        QVERIFY(tab->executeCommand(pdf::PDFAppCommands::outlineDelete()));
        fixture.settle();
    }
    QVERIFY(!H::sidebar(tab)->isTabVisible(pdf::PDFAppDocumentPanelIds::outline()));
    QVERIFY(H::sidebar(tab)->getCurrentPanelId() != pdf::PDFAppDocumentPanelIds::outline());
}

void AppNotesOutlineTest::outlineMove()
{
    using H = NotesOutlineTestHelper;

    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::outline(fixture.folder()));
    pdf::PDFAppOutlinePanel* panel = H::outline(tab);
    QVERIFY(panel);
    fixture.settle();
    pdf::PDFAppDocument* document = tab->getDocument();

    H::selectItem(panel, { 2 });
    QVERIFY(tab->executeCommand(pdf::PDFAppCommands::outlineMoveUp()));
    fixture.settle();
    QCOMPARE(H::titles(document->getDocument().data()), QStringList({ "Introduction", "Appendix", "Chapter 1", ">Section 1.1" }));
    QCOMPARE(panel->getCurrentItem().data().toString(), QStringLiteral("Appendix"));
    QCOMPARE(document->getUndoStack()->getUndoDescription(), QStringLiteral("Move outline item"));

    QVERIFY(tab->executeCommand(pdf::PDFAppCommands::outlineMoveDown()));
    fixture.settle();
    QCOMPARE(H::titles(document->getDocument().data()), QStringList({ "Introduction", "Chapter 1", ">Section 1.1", "Appendix" }));

    // Indent: the last child of the previous item; outdent: after its parent
    QVERIFY(tab->executeCommand(pdf::PDFAppCommands::outlineIndent()));
    fixture.settle();
    QCOMPARE(H::titles(document->getDocument().data()), QStringList({ "Introduction", "Chapter 1", ">Section 1.1", ">Appendix" }));
    QCOMPARE(document->getUndoStack()->getUndoDescription(), QStringLiteral("Indent outline item"));
    QCOMPARE(panel->getCurrentItem().data().toString(), QStringLiteral("Appendix"));
    QVERIFY(tab->isCommandEnabled(pdf::PDFAppCommands::outlineOutdent()));
    QVERIFY(tab->isCommandEnabled(pdf::PDFAppCommands::outlineIndent()));
    QVERIFY(tab->executeCommand(pdf::PDFAppCommands::outlineOutdent()));
    fixture.settle();
    QCOMPARE(H::titles(document->getDocument().data()), QStringList({ "Introduction", "Chapter 1", ">Section 1.1", "Appendix" }));
    QCOMPARE(document->getUndoStack()->getUndoDescription(), QStringLiteral("Outdent outline item"));
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 4);
    QCOMPARE(H::checkStructure(document->getDocument().data()), QString());

    // The limits: the first item does not move up or indent, the last does not move down
    H::selectItem(panel, { 0 });
    QVERIFY(!tab->isCommandEnabled(pdf::PDFAppCommands::outlineMoveUp()));
    QVERIFY(!tab->isCommandEnabled(pdf::PDFAppCommands::outlineIndent()));
    QVERIFY(!tab->isCommandEnabled(pdf::PDFAppCommands::outlineOutdent()));
    QVERIFY(!tab->executeCommand(pdf::PDFAppCommands::outlineMoveUp()));
    H::selectItem(panel, { 2 });
    QVERIFY(!tab->isCommandEnabled(pdf::PDFAppCommands::outlineMoveDown()));

    // The context menu has the same commands with their states
    QMenu* menu = panel->createContextMenu(panel->getItemIndex({ 0 }));
    QVERIFY(menu);
    QCOMPARE(H::actionNames(menu), QStringList({ "outlineGoTo", "outlineMenu:outline.newItem", "outlineMenu:outline.rename", "outlineMenu:outline.setTarget",
                                                 "outlineMenu:outline.bold", "outlineMenu:outline.italic", "outlineMenu:outline.moveUp", "outlineMenu:outline.moveDown",
                                                 "outlineMenu:outline.indent", "outlineMenu:outline.outdent", "outlineMenu:outline.delete" }));
    QVERIFY(!H::findAction(menu, QStringLiteral("outlineMenu:outline.moveUp"))->isEnabled());
    QVERIFY(H::findAction(menu, QStringLiteral("outlineMenu:outline.moveDown"))->isEnabled());
    QCOMPARE(H::findAction(menu, QStringLiteral("outlineMenu:outline.rename"))->shortcut(), QKeySequence(Qt::Key_F2));
    H::findAction(menu, QStringLiteral("outlineMenu:outline.moveDown"))->trigger();
    fixture.settle();
    delete menu;
    QCOMPARE(H::titles(document->getDocument().data()), QStringList({ "Chapter 1", ">Section 1.1", "Introduction", "Appendix" }));

    // Go to Page of the menu
    menu = panel->createContextMenu(panel->getItemIndex({ 2 }));
    QVERIFY(H::findAction(menu, QStringLiteral("outlineGoTo")));
    H::findAction(menu, QStringLiteral("outlineGoTo"))->trigger();
    fixture.settle();
    delete menu;
    QTRY_COMPARE(tab->getView()->getCurrentPageIndex(), pdf::PDFInteger(4));

    // The viewport menu: only a new item
    menu = panel->createContextMenu(QModelIndex());
    QCOMPARE(H::actionNames(menu), QStringList({ "outlineMenu:outline.newItem" }));
    delete menu;
}

void AppNotesOutlineTest::outlineDragDrop()
{
    using H = NotesOutlineTestHelper;
    using Position = pdf::PDFAppOutlinePanel::DropPosition;

    // The place of a drop in the rectangle of a row
    const QRect rect(0, 100, 200, 28);
    QCOMPARE(pdf::PDFAppOutlinePanel::getDropPosition(rect, QPoint(10, 101)), Position::Above);
    QCOMPARE(pdf::PDFAppOutlinePanel::getDropPosition(rect, QPoint(10, 114)), Position::OnItem);
    QCOMPARE(pdf::PDFAppOutlinePanel::getDropPosition(rect, QPoint(10, 126)), Position::Below);
    QCOMPARE(pdf::PDFAppOutlinePanel::getDropPosition(rect, QPoint(10, 300)), Position::OnViewport);
    QCOMPARE(pdf::PDFAppOutlinePanel::getDropPosition(QRect(), QPoint(10, 10)), Position::OnViewport);

    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::outline(fixture.folder()));
    pdf::PDFAppOutlinePanel* panel = H::outline(tab);
    QVERIFY(panel);
    fixture.settle();
    pdf::PDFAppDocument* document = tab->getDocument();
    QCOMPARE(panel->getTreeView()->dragDropMode(), QAbstractItemView::DragDrop);

    // dropItem: above, below, into, at the end
    QVERIFY(panel->dropItem(panel->getItemIndex({ 2 }), panel->getItemIndex({ 0 }), Position::Above));
    fixture.settle();
    QCOMPARE(H::titles(document->getDocument().data()), QStringList({ "Appendix", "Introduction", "Chapter 1", ">Section 1.1" }));
    QVERIFY(panel->dropItem(panel->getItemIndex({ 0 }), panel->getItemIndex({ 2 }), Position::OnItem));
    fixture.settle();
    QCOMPARE(H::titles(document->getDocument().data()), QStringList({ "Introduction", "Chapter 1", ">Section 1.1", ">Appendix" }));
    QVERIFY(panel->dropItem(panel->getItemIndex({ 1, 1 }), panel->getItemIndex({ 0 }), Position::Below));
    fixture.settle();
    QCOMPARE(H::titles(document->getDocument().data()), QStringList({ "Introduction", "Appendix", "Chapter 1", ">Section 1.1" }));
    QVERIFY(panel->dropItem(panel->getItemIndex({ 0 }), QModelIndex(), Position::OnViewport));
    fixture.settle();
    QCOMPARE(H::titles(document->getDocument().data()), QStringList({ "Appendix", "Chapter 1", ">Section 1.1", "Introduction" }));
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 4);

    // Into its own child, onto itself: nothing
    QVERIFY(!panel->dropItem(panel->getItemIndex({ 1 }), panel->getItemIndex({ 1, 0 }), Position::OnItem));
    QVERIFY(!panel->dropItem(panel->getItemIndex({ 1 }), panel->getItemIndex({ 1 }), Position::Above));
    QVERIFY(!panel->dropItem(QModelIndex(), panel->getItemIndex({ 1 }), Position::Above));
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 4);

    // A drop event of the view with the data of the model (the change from the event loop)
    QAbstractItemModel* model = panel->getViewModel();
    QWidget* viewport = panel->getTreeView()->viewport();
    const QModelIndex source = panel->getItemIndex({ 2 });
    QVERIFY(source.isValid());
    std::unique_ptr<QMimeData> data(model->mimeData({ source }));
    QVERIFY(data);
    const QRect target = panel->getTreeView()->visualRect(panel->getItemIndex({ 0 }));
    QDragEnterEvent enter(target.center(), Qt::MoveAction, data.get(), Qt::LeftButton, Qt::NoModifier);
    enter.setDropAction(Qt::MoveAction);
    QApplication::sendEvent(viewport, &enter);
    QVERIFY(enter.isAccepted());
    QDropEvent drop(QPointF(target.left() + 10, target.top() + 1), Qt::MoveAction, data.get(), Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(viewport, &drop);
    QVERIFY(drop.isAccepted());
    QCOMPARE(drop.dropAction(), Qt::IgnoreAction);
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 4);
    fixture.settle();
    QCOMPARE(H::titles(document->getDocument().data()), QStringList({ "Introduction", "Appendix", "Chapter 1", ">Section 1.1" }));
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 5);

    // The data of the old version is refused (the paths have changed)
    QDragMoveEvent move(target.center(), Qt::MoveAction, data.get(), Qt::LeftButton, Qt::NoModifier);
    move.setDropAction(Qt::MoveAction);
    QApplication::sendEvent(viewport, &move);
    QVERIFY(!move.isAccepted());
    QDropEvent staleDrop(target.center(), Qt::MoveAction, data.get(), Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(viewport, &staleDrop);
    QVERIFY(!staleDrop.isAccepted());
    fixture.settle();
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 5);

    // Foreign data
    QMimeData foreign;
    foreign.setText(QStringLiteral("text"));
    QDropEvent foreignDrop(target.center(), Qt::CopyAction, &foreign, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(viewport, &foreignDrop);
    QVERIFY(!foreignDrop.isAccepted());

    // The model itself never inserts the dropped data
    std::unique_ptr<QMimeData> fresh(model->mimeData({ panel->getItemIndex({ 0 }) }));
    QVERIFY(panel->getSourceModel()->canDropMimeData(fresh.get(), Qt::MoveAction, 0, 0, QModelIndex()));
    QVERIFY(!panel->getSourceModel()->dropMimeData(fresh.get(), Qt::MoveAction, 0, 0, QModelIndex()));
    QCOMPARE(panel->getSourceModel()->supportedDragActions(), Qt::DropActions(Qt::MoveAction));
    QVERIFY(!model->mimeData({}));

    // Locked: no drag and drop
    document->setLocked(true);
    fixture.settle();
    QCOMPARE(panel->getTreeView()->dragDropMode(), QAbstractItemView::NoDragDrop);
    std::unique_ptr<QMimeData> lockedData(model->mimeData({ panel->getItemIndex({ 0 }) }));
    QDropEvent lockedDrop(target.center(), Qt::MoveAction, lockedData.get(), Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(viewport, &lockedDrop);
    QVERIFY(!lockedDrop.isAccepted());
    QVERIFY(!panel->dropItem(panel->getItemIndex({ 0 }), QModelIndex(), Position::OnViewport));
}

void AppNotesOutlineTest::outlineSetTarget()
{
    using H = NotesOutlineTestHelper;

    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::outline(fixture.folder()));
    pdf::PDFAppOutlinePanel* panel = H::outline(tab);
    QVERIFY(panel);
    fixture.settle();
    pdf::PDFAppDocument* document = tab->getDocument();

    tab->getView()->goToPage(3);
    fixture.settle();
    H::selectItem(panel, { 0 });
    QVERIFY(tab->executeCommand(pdf::PDFAppCommands::outlineSetTarget()));
    fixture.settle();
    const pdf::PDFOutlineItem* item = document->getDocument()->getCatalog()->getOutlineRootPtr()->getChild(0);
    QCOMPARE(pdf::PDFAppPanelWidgets::getActionPage(document->getDocument()->getCatalog(), item->getAction()), pdf::PDFInteger(3));
    const pdf::PDFActionGoTo* goTo = dynamic_cast<const pdf::PDFActionGoTo*>(item->getAction());
    QVERIFY(goTo);
    QCOMPARE(goTo->getDestination().getDestinationType(), pdf::DestinationType::XYZ);
    QCOMPARE(document->getUndoStack()->getUndoDescription(), QStringLiteral("Set outline item target"));
    QCOMPARE(panel->getItemIndex({ 0 }).data(pdf::PDFAppOutlinePanel::PageRole).toLongLong(), 3);

    // The item goes to its new target
    tab->getView()->goToPage(0);
    fixture.settle();
    panel->activateItem(panel->getItemIndex({ 0 }));
    QTRY_COMPARE(tab->getView()->getCurrentPageIndex(), pdf::PDFInteger(3));

    tab->executeCommand(pdf::PDFAppCommands::undo());
    fixture.settle();
    QCOMPARE(panel->getItemIndex({ 0 }).data(pdf::PDFAppOutlinePanel::PageRole).toLongLong(), 0);
}

void AppNotesOutlineTest::outlineBoldItalic()
{
    using H = NotesOutlineTestHelper;

    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::outline(fixture.folder()));
    pdf::PDFAppOutlinePanel* panel = H::outline(tab);
    QVERIFY(panel);
    fixture.settle();
    pdf::PDFAppDocument* document = tab->getDocument();

    H::selectItem(panel, { 1 });
    QVERIFY(!tab->isCommandChecked(pdf::PDFAppCommands::outlineBold()));
    QVERIFY(tab->executeCommand(pdf::PDFAppCommands::outlineBold()));
    fixture.settle();
    QVERIFY(tab->isCommandChecked(pdf::PDFAppCommands::outlineBold()));
    QVERIFY(document->getDocument()->getCatalog()->getOutlineRootPtr()->getChild(1)->isFontBold());
    QVERIFY(panel->getItemIndex({ 1 }).data(Qt::FontRole).value<QFont>().bold());
    QCOMPARE(document->getUndoStack()->getUndoDescription(), QStringLiteral("Change outline item style"));

    QVERIFY(tab->executeCommand(pdf::PDFAppCommands::outlineItalic()));
    fixture.settle();
    QVERIFY(tab->isCommandChecked(pdf::PDFAppCommands::outlineItalic()));
    const QFont font = panel->getItemIndex({ 1 }).data(Qt::FontRole).value<QFont>();
    QVERIFY(font.bold() && font.italic());

    // The menu shows the checks
    QMenu* menu = panel->createContextMenu(panel->getItemIndex({ 1 }));
    QVERIFY(H::findAction(menu, QStringLiteral("outlineMenu:outline.bold"))->isChecked());
    QVERIFY(H::findAction(menu, QStringLiteral("outlineMenu:outline.italic"))->isChecked());
    H::findAction(menu, QStringLiteral("outlineMenu:outline.bold"))->trigger();
    fixture.settle();
    delete menu;
    QVERIFY(!tab->isCommandChecked(pdf::PDFAppCommands::outlineBold()));
    QVERIFY(tab->isCommandChecked(pdf::PDFAppCommands::outlineItalic()));
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 3);

    tab->executeCommand(pdf::PDFAppCommands::undo());
    tab->executeCommand(pdf::PDFAppCommands::undo());
    tab->executeCommand(pdf::PDFAppCommands::undo());
    fixture.settle();
    QVERIFY(!panel->getItemIndex({ 1 }).data(Qt::FontRole).isValid());
}

void AppNotesOutlineTest::outlineUndoKeepsExpansion()
{
    using H = NotesOutlineTestHelper;

    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::outline(fixture.folder()));
    pdf::PDFAppOutlinePanel* panel = H::outline(tab);
    QVERIFY(panel);
    fixture.settle();
    pdf::PDFAppDocument* document = tab->getDocument();
    pdf::PDFTreeView* tree = panel->getTreeView();

    tree->collapseAll();
    tree->expand(panel->getItemIndex({ 1 }));
    QVERIFY(tree->isExpanded(panel->getItemIndex({ 1 })));

    // A rename of another item, a move of the expanded item, undo and redo
    QVERIFY(tab->getView()->getOutlineEditor()->renameItem({ 2 }, QStringLiteral("Appendix A")));
    fixture.settle();
    QVERIFY(tree->isExpanded(panel->getItemIndex({ 1 })));
    QCOMPARE(panel->getCurrentItem().data().toString(), QStringLiteral("Appendix A"));
    QVERIFY(tab->getView()->getOutlineEditor()->moveItem({ 1 }, {}, 0));
    fixture.settle();
    QCOMPARE(H::titles(panel->getSourceModel()), QStringList({ "Chapter 1", ">Section 1.1", "Introduction", "Appendix A" }));
    QVERIFY(tree->isExpanded(panel->getItemIndex({ 0 })));
    QCOMPARE(panel->getCurrentItem().data().toString(), QStringLiteral("Chapter 1"));

    tab->executeCommand(pdf::PDFAppCommands::undo());
    fixture.settle();
    QVERIFY(tree->isExpanded(panel->getItemIndex({ 1 })));
    QCOMPARE(panel->getCurrentItem().data().toString(), QStringLiteral("Chapter 1"));
    tab->executeCommand(pdf::PDFAppCommands::redo());
    fixture.settle();
    QVERIFY(tree->isExpanded(panel->getItemIndex({ 0 })));

    // A change of an annotation keeps the items (no rebuild of the tree)
    tree->collapse(panel->getItemIndex({ 0 }));
    QStandardItem* item = panel->getSourceModel()->item(0);
    pdf::PDFDocumentModifier modifier(document->getDocument().data());
    modifier.getBuilder()->createAnnotationSquare(document->getDocument()->getCatalog()->getPage(0)->getPageReference(), QRectF(10, 10, 50, 50), 1.0, QColor(), QColor(Qt::red),
                                                   QStringLiteral("x"), QStringLiteral("x"), QStringLiteral("x"));
    modifier.markAnnotationsChanged();
    QVERIFY(modifier.finalize());
    QVERIFY(document->commit(pdf::PDFModifiedDocument(modifier.getDocument(), nullptr, modifier.getFlags()), QStringLiteral("Square"), document->getVersion(), pdf::PDFAppChangeSource::User));
    fixture.settle();
    QCOMPARE(panel->getSourceModel()->item(0), item);
    QVERIFY(!tree->isExpanded(panel->getItemIndex({ 0 })));

    // The outline is still editable after the change of the annotation (the key of the version)
    QVERIFY(tab->getView()->getOutlineEditor()->renameItem({ 0 }, QStringLiteral("Chapter One")));
    fixture.settle();
    QCOMPARE(H::titles(document->getDocument().data()).front(), QStringLiteral("Chapter One"));
    QVERIFY(document->getDocument()->getCatalog()->getPage(0)->getAnnotations().size() == 1);
}

void AppNotesOutlineTest::outlineLocked()
{
    using H = NotesOutlineTestHelper;

    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::outline(fixture.folder()));
    pdf::PDFAppOutlinePanel* panel = H::outline(tab);
    QVERIFY(panel);
    fixture.settle();
    pdf::PDFAppDocument* document = tab->getDocument();

    // Locked (O20): "+" hidden, no editing, commands disabled; the navigation works
    document->setLocked(true);
    fixture.settle();
    QVERIFY(panel->getNewItemButton()->isHidden());
    QVERIFY(!panel->getRenameAction()->isEnabled());
    QVERIFY(!(panel->getItemIndex({ 0 }).flags() & Qt::ItemIsEditable));
    for (const QString& commandId : pdf::PDFAppDocumentCommandTable::getCommands(pdf::PDFAppDocumentPackage::NotesOutline))
    {
        QVERIFY2(!tab->isCommandEnabled(commandId), qPrintable(commandId));
    }
    QMenu* menu = panel->createContextMenu(panel->getItemIndex({ 1 }));
    QCOMPARE(H::actionNames(menu), QStringList({ "outlineGoTo" }));
    delete menu;
    QVERIFY(!panel->editItem({ 0 }));
    panel->getTreeView()->setFocus();
    H::selectItem(panel, { 0 });
    QTest::keyClick(panel->getTreeView(), Qt::Key_Delete);
    QTest::keyClick(panel->getTreeView(), Qt::Key_Insert);
    fixture.settle();
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 0);
    panel->activateItem(panel->getItemIndex({ 2 }));
    QTRY_COMPARE(tab->getView()->getCurrentPageIndex(), pdf::PDFInteger(4));

    // Locked while an item waits for its title: the creation ends without a step
    document->setLocked(false);
    fixture.settle();
    QVERIFY(!panel->getNewItemButton()->isHidden());
    QVERIFY(panel->getItemIndex({ 0 }).flags() & Qt::ItemIsEditable);
    QVERIFY(tab->executeCommand(pdf::PDFAppCommands::outlineNewItem()));
    fixture.settle();
    QVERIFY(tab->getView()->getOutlineEditor()->isCreationPending());
    document->setLocked(true);
    QVERIFY(!tab->getView()->getOutlineEditor()->finishCreation(QStringLiteral("Locked")));
    fixture.settle();
    QVERIFY(!panel->getPendingItem());
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 0);
    document->setLocked(false);

    // A document, which does not allow changes (no Modify, no Assemble)
    pdf::PDFAppDocumentTab* restricted = fixture.open(pdftest::AppDocumentSamples::restricted(fixture.folder()));
    QVERIFY(restricted);
    const pdf::PDFSecurityHandler* handler = restricted->getDocument()->getDocument()->getStorage().getSecurityHandler();
    const bool isAllowed = !handler || handler->isAllowed(pdf::PDFSecurityHandler::Permission::Assemble) || handler->isAllowed(pdf::PDFSecurityHandler::Permission::Modify);
    QCOMPARE(restricted->getView()->getOutlineEditor()->isEditingAllowed(), isAllowed);
    if (!isAllowed)
    {
        QCOMPARE(restricted->getView()->getOutlineEditor()->getEditingRefusalReason(), QStringLiteral("The document does not allow changes."));
    }
}

void AppNotesOutlineTest::notesSelectionSync()
{
    using H = NotesOutlineTestHelper;

    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::annotations(fixture.folder()));
    QVERIFY(tab);
    pdf::PDFAppNotesPanel* panel = H::notes(tab);
    QVERIFY(panel);
    fixture.settle();
    pdf::PDFAppNotesModel* model = panel->getModel();
    pdf::PDFAppAnnotationController* controller = tab->getView()->getAnnotationController();
    QCOMPARE(panel->getListView()->selectionMode(), QAbstractItemView::ExtendedSelection);

    // A click on a row selects the annotation on the page and shows it
    const pdf::PDFObjectReference square = H::noteReference(model, QStringLiteral("Square"));
    const pdf::PDFObjectReference circle = H::noteReference(model, QStringLiteral("Circle"));
    const pdf::PDFObjectReference overlapA = H::noteReference(model, QStringLiteral("Overlap A"));
    QVERIFY(square.isValid() && circle.isValid() && overlapA.isValid());
    QListView* list = panel->getListView();
    QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::NoModifier, list->visualRect(model->index(H::findNote(model, QStringLiteral("Overlap A")))).center());
    fixture.settle();
    QCOMPARE(controller->getSelectedAnnotations(), std::vector<pdf::PDFObjectReference>({ overlapA }));
    QTRY_COMPARE(tab->getView()->getCurrentPageIndex(), pdf::PDFInteger(1));

    // Ctrl+click adds a row (also on another page) without moving the view
    tab->getView()->goToPage(0);
    fixture.settle();
    QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::ControlModifier, list->visualRect(model->index(H::findNote(model, QStringLiteral("Square")))).center());
    fixture.settle();
    std::vector<pdf::PDFObjectReference> selection = controller->getSelectedAnnotations();
    std::sort(selection.begin(), selection.end());
    std::vector<pdf::PDFObjectReference> expected = { overlapA, square };
    std::sort(expected.begin(), expected.end());
    QCOMPARE(selection, expected);
    QCOMPARE(tab->getView()->getCurrentPageIndex(), pdf::PDFInteger(0));

    // The page selects: the rows follow (more annotations)
    controller->selectAnnotations({ circle, square });
    fixture.settle();
    std::vector<pdf::PDFObjectReference> rows = panel->getSelectedNotes();
    std::sort(rows.begin(), rows.end());
    expected = { circle, square };
    std::sort(expected.begin(), expected.end());
    QCOMPARE(rows, expected);
    controller->clearAnnotationSelection();
    fixture.settle();
    QVERIFY(panel->getSelectedNotes().empty());

    // A click on the page (the engine selects the square)
    tab->getView()->goToPage(0);
    fixture.settle();
    QVERIFY(pdftest::AppDocumentFixture::clickPage(tab->getView(), 0, QPointF(42, 122)));
    fixture.settle();
    if (controller->hasAnnotationSelection())
    {
        QCOMPARE(panel->getSelectedNotes(), controller->getSelectedAnnotations());
    }

    // A hidden annotation and a reply are selected in the panel only
    const pdf::PDFObjectReference hidden = H::noteReference(model, QStringLiteral("Hidden"));
    const pdf::PDFObjectReference reply = H::noteReference(model, QStringLiteral("Reply 1"));
    QItemSelection itemSelection;
    for (const pdf::PDFObjectReference& reference : { hidden, reply, square })
    {
        const QModelIndex index = model->index(model->findRow(reference));
        itemSelection.select(index, index);
    }
    list->selectionModel()->select(itemSelection, QItemSelectionModel::ClearAndSelect);
    fixture.settle();
    QCOMPARE(controller->getSelectedAnnotations(), std::vector<pdf::PDFObjectReference>({ square }));
    QCOMPARE(panel->getSelectedNotes().size(), size_t(3));

    // The page selects again: the rows of the panel only are replaced
    controller->selectAnnotations({ circle });
    fixture.settle();
    QCOMPARE(panel->getSelectedNotes(), std::vector<pdf::PDFObjectReference>({ circle }));
}

void AppNotesOutlineTest::notesReplies()
{
    using H = NotesOutlineTestHelper;

    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::annotations(fixture.folder()));
    pdf::PDFAppNotesPanel* panel = H::notes(tab);
    QVERIFY(panel);
    fixture.settle();
    pdf::PDFAppNotesModel* model = panel->getModel();

    // 1025:21533: the header counts the notes and the replies apart
    QCOMPARE(panel->getSubtitle(), QStringLiteral("%1 notes and 2 replies on %2 pages").arg(model->getNoteCount() - 2).arg(model->getPageCount()));
    QCOMPARE(model->getReplyCount(), 2);

    // 1025:21920: the replies follow their note, indented by 24, with the reply badge
    const int noteRow = H::findNote(model, QStringLiteral("Note with replies"));
    QVERIFY(noteRow > 0);
    QCOMPARE(model->index(noteRow).data(pdf::PDFComponentRoles::IndentRole).toInt(), 0);
    for (int i = 1; i <= 2; ++i)
    {
        const QModelIndex replyIndex = model->index(noteRow + i);
        QCOMPARE(replyIndex.data(pdf::PDFComponentRoles::TextRole).toString(), QStringLiteral("Reply %1").arg(i));
        QCOMPARE(replyIndex.data(pdf::PDFComponentRoles::IndentRole).toInt(), pdf::PDFAppNotes::REPLY_INDENT);
        QCOMPARE(replyIndex.data(pdf::PDFComponentRoles::IconNameRole).toString(), QStringLiteral("corner-down-right"));
        QCOMPARE(replyIndex.data(pdf::PDFComponentRoles::HueRole).toInt(), int(pdf::PDFAccentHue::Neutral));
        QCOMPARE(replyIndex.data().toString(), QStringLiteral("Replier %1").arg(i));
        QVERIFY(model->getNote(noteRow + i)->isReply);
        QVERIFY(replyIndex.data(Qt::AccessibleTextRole).toString().startsWith(QStringLiteral("Reply - ")));
    }

    // Every note is listed once
    std::set<pdf::PDFObjectReference> listed;
    int noteCount = 0;
    for (int row = 0; row < model->rowCount(); ++row)
    {
        if (const pdf::PDFAppNote* note = model->getNote(row))
        {
            QVERIFY(listed.insert(note->reference).second);
            ++noteCount;
        }
    }
    QCOMPARE(noteCount, model->getNoteCount());

    // The threads (the deletion takes them)
    const pdf::PDFObjectReference note = model->getNote(noteRow)->reference;
    const std::vector<pdf::PDFObjectReference> thread = model->getThreads({ note });
    QCOMPARE(thread.size(), size_t(3));
    QCOMPARE(thread.front(), note);
    QCOMPARE(model->getThreads({ model->getNote(noteRow + 1)->reference }).size(), size_t(1));
    QCOMPARE(model->getThreads({ pdf::PDFObjectReference(99999, 0) }).size(), size_t(1));

    // The filter: a matching reply shows its note; a matching note shows all replies
    panel->getSearchField()->setText(QStringLiteral("Reply 2"));
    QCOMPARE(model->rowCount(), 3);
    QCOMPARE(model->index(1).data(pdf::PDFComponentRoles::TextRole).toString(), QStringLiteral("Note with replies"));
    QCOMPARE(model->index(2).data(pdf::PDFComponentRoles::TextRole).toString(), QStringLiteral("Reply 2"));
    panel->getSearchField()->setText(QStringLiteral("with replies"));
    QCOMPARE(model->rowCount(), 4);
    panel->getSearchField()->clear();

    // The delegate: a reply row has the width of the list, the row is indented
    QStyleOptionViewItem option;
    option.rect = QRect(0, 0, 276, 63);
    option.direction = Qt::LeftToRight;
    pdf::PDFAppNotesDelegate delegate(nullptr);
    QCOMPARE(delegate.sizeHint(option, model->index(noteRow + 1)).width(), 276);
    QCOMPARE(pdf::PDFNoteDelegate::getIndentedRect(QRect(0, 0, 276, 48), 24, Qt::LeftToRight), QRect(24, 0, 252, 48));
    QCOMPARE(pdf::PDFNoteDelegate::getIndentedRect(QRect(0, 0, 276, 48), 24, Qt::RightToLeft), QRect(0, 0, 252, 48));
    QCOMPARE(pdf::PDFNoteDelegate::getIndentedRect(QRect(0, 0, 10, 48), 24, Qt::LeftToRight).width(), 1);

    // Hostile threads: a cycle of replies and a chain of a missing note
    std::vector<pdf::PDFAppNote> notes(4);
    for (int i = 0; i < 4; ++i)
    {
        notes[size_t(i)].reference = pdf::PDFObjectReference(100 + i, 0);
        notes[size_t(i)].type = pdf::AnnotationType::Text;
        notes[size_t(i)].text = QStringLiteral("n%1").arg(i);
    }
    notes[0].isReply = true;
    notes[0].inReplyTo = notes[1].reference;
    notes[1].isReply = true;
    notes[1].inReplyTo = notes[0].reference;
    notes[2].isReply = true;
    notes[2].inReplyTo = pdf::PDFObjectReference(999, 0);
    notes[3].isReply = true;
    notes[3].inReplyTo = notes[3].reference;
    pdf::PDFAppNotesModel hostile(nullptr);
    hostile.setNotes(notes, tab->getDocument()->getDocument());
    QCOMPARE(hostile.rowCount(), 5);
    for (const pdf::PDFAppNote& item : notes)
    {
        QVERIFY(hostile.findRow(item.reference) > 0);
    }
    QCOMPARE(hostile.getThreads({ notes[0].reference }).size(), size_t(2));
}

void AppNotesOutlineTest::notesContextMenu()
{
    using H = NotesOutlineTestHelper;

    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::annotations(fixture.folder()));
    pdf::PDFAppNotesPanel* panel = H::notes(tab);
    QVERIFY(panel);
    fixture.settle();
    pdf::PDFAppNotesModel* model = panel->getModel();
    pdf::PDFAppAnnotationController* controller = tab->getView()->getAnnotationController();
    pdf::PDFAppDocument* document = tab->getDocument();

    // A note on the page (1019:18817)
    const pdf::PDFObjectReference note = H::noteReference(model, QStringLiteral("Note with replies"));
    QMenu* menu = H::noteMenu(panel, { note });
    QCOMPARE(H::actionNames(menu), QStringList({ "notesOpenComment", "notesReply", "notesProperties", "notesHide", "notesDelete" }));
    QSignalSpy commentSpy(controller, &pdf::PDFAppAnnotationController::commentRequested);
    QSignalSpy replySpy(controller, &pdf::PDFAppAnnotationController::replyRequested);
    QSignalSpy propertiesSpy(controller, &pdf::PDFAppAnnotationController::propertiesRequested);
    H::findAction(menu, QStringLiteral("notesOpenComment"))->trigger();
    H::findAction(menu, QStringLiteral("notesReply"))->trigger();
    H::findAction(menu, QStringLiteral("notesProperties"))->trigger();
    delete menu;
    fixture.settle();
    QCOMPARE(commentSpy.count(), 1);
    QCOMPARE(commentSpy.front().at(0).value<pdf::PDFObjectReference>(), note);
    QCOMPARE(commentSpy.front().at(1).toBool(), false);
    QCOMPARE(replySpy.count(), 1);
    QCOMPARE(replySpy.front().at(0).value<pdf::PDFObjectReference>(), note);
    QCOMPARE(propertiesSpy.count(), 1);
    QCOMPARE(propertiesSpy.front().at(0).value<pdf::PDFObjectReference>(), note);

    // Hide on Page: one step, the row gets the eye-off mark; Show on Page again
    const pdf::PDFObjectReference square = H::noteReference(model, QStringLiteral("Square"));
    menu = H::noteMenu(panel, { square });
    H::findAction(menu, QStringLiteral("notesHide"))->trigger();
    delete menu;
    fixture.settle();
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 1);
    QCOMPARE(document->getUndoStack()->getUndoDescription(), QStringLiteral("Hide on page"));
    QCOMPARE(model->index(model->findRow(square)).data(pdf::PDFComponentRoles::StateIconRole).toString(), QStringLiteral("eye-off"));
    menu = H::noteMenu(panel, { square });
    QCOMPARE(H::actionNames(menu), QStringList({ "notesShow", "notesProperties", "notesDelete" }));
    H::findAction(menu, QStringLiteral("notesShow"))->trigger();
    delete menu;
    fixture.settle();
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 2);
    QVERIFY(model->index(model->findRow(square)).data(pdf::PDFComponentRoles::StateIconRole).toString().isEmpty());

    // A reply: Properties and Delete (1026:21995)
    const pdf::PDFObjectReference reply = H::noteReference(model, QStringLiteral("Reply 1"));
    menu = H::noteMenu(panel, { reply });
    QCOMPARE(H::actionNames(menu), QStringList({ "notesProperties", "notesDelete" }));
    delete menu;

    // More notes: Show / Hide and Delete of all of them
    const pdf::PDFObjectReference hidden = H::noteReference(model, QStringLiteral("Hidden"));
    menu = H::noteMenu(panel, { square, hidden, reply });
    QCOMPARE(H::actionNames(menu), QStringList({ "notesShow", "notesHide", "notesDelete" }));
    QCOMPARE(H::findAction(menu, QStringLiteral("notesDelete"))->text(), QStringLiteral("Delete 3 Notes"));
    QVERIFY(H::findAction(menu, QStringLiteral("notesDelete"))->property("destructive").toBool());
    H::findAction(menu, QStringLiteral("notesHide"))->trigger();
    delete menu;
    fixture.settle();
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 3);

    // The menu of the rows built again meanwhile acts on its notes
    menu = H::noteMenu(panel, { square });
    panel->selectNotes({});
    H::findAction(menu, QStringLiteral("notesDelete"))->trigger();
    delete menu;
    fixture.settle();
    QCOMPARE(model->findRow(square), -1);
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 4);

    // No selection: no menu; the right click selects the row first
    panel->selectNotes({});
    QVERIFY(!panel->createContextMenu());
    const int circleRow = H::findNote(model, QStringLiteral("Circle"));
    QListView* list = panel->getListView();
    const QPoint circlePoint = list->visualRect(model->index(circleRow)).center();
    QContextMenuEvent event(QContextMenuEvent::Mouse, circlePoint, list->viewport()->mapToGlobal(circlePoint));
    QApplication::sendEvent(list->viewport(), &event);
    fixture.settle();
    QCOMPARE(panel->getSelectedNotes(), std::vector<pdf::PDFObjectReference>({ H::noteReference(model, QStringLiteral("Circle")) }));
    QMenu* shown = panel->findChild<QMenu*>(QStringLiteral("notesContextMenu"));
    QVERIFY(shown);
    shown->close();
    fixture.settle();
}

void AppNotesOutlineTest::notesDeleteKey()
{
    using H = NotesOutlineTestHelper;

    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::annotations(fixture.folder()));
    pdf::PDFAppNotesPanel* panel = H::notes(tab);
    QVERIFY(panel);
    fixture.settle();
    pdf::PDFAppNotesModel* model = panel->getModel();
    pdf::PDFAppDocument* document = tab->getDocument();
    const int noteCount = model->getNoteCount();

    // Del deletes the selected note with its replies as one step
    const pdf::PDFObjectReference note = H::noteReference(model, QStringLiteral("Note with replies"));
    const pdf::PDFObjectReference circle = H::noteReference(model, QStringLiteral("Circle"));
    panel->selectNotes({ note, circle });
    panel->getListView()->setFocus();
    QTest::keyClick(panel->getListView(), Qt::Key_Delete);
    fixture.settle();
    QCOMPARE(model->getNoteCount(), noteCount - 4);
    QCOMPARE(model->findRow(note), -1);
    QCOMPARE(H::findNote(model, QStringLiteral("Reply 1")), -1);
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 1);
    QCOMPARE(document->getUndoStack()->getUndoDescription(), QStringLiteral("Delete notes"));

    // Undo brings the thread back
    tab->executeCommand(pdf::PDFAppCommands::undo());
    fixture.settle();
    QCOMPARE(model->getNoteCount(), noteCount);
    QVERIFY(H::findNote(model, QStringLiteral("Reply 2")) > 0);

    // A locked annotation is not deleted (the engine skips it); nothing selected: nothing
    panel->selectNotes({});
    QVERIFY(!panel->deleteSelectedNotes());
    panel->selectNotes({ H::noteReference(model, QStringLiteral("Locked")) });
    panel->deleteSelectedNotes();
    fixture.settle();
    QVERIFY(H::findNote(model, QStringLiteral("Locked")) > 0);

    // One note
    panel->selectNotes({ circle });
    QVERIFY(panel->deleteSelectedNotes());
    fixture.settle();
    QCOMPARE(document->getUndoStack()->getUndoDescription(), QStringLiteral("Delete note"));
}

void AppNotesOutlineTest::notesFilterKeepsSelection()
{
    using H = NotesOutlineTestHelper;

    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::annotations(fixture.folder()));
    pdf::PDFAppNotesPanel* panel = H::notes(tab);
    QVERIFY(panel);
    fixture.settle();
    pdf::PDFAppNotesModel* model = panel->getModel();
    pdf::PDFAppAnnotationController* controller = tab->getView()->getAnnotationController();

    const pdf::PDFObjectReference square = H::noteReference(model, QStringLiteral("Square"));
    const pdf::PDFObjectReference circle = H::noteReference(model, QStringLiteral("Circle"));
    controller->selectAnnotations({ square, circle });
    fixture.settle();
    QCOMPARE(panel->getSelectedNotes().size(), size_t(2));

    // The filter hides the square: the page keeps both, the visible row stays selected
    panel->getSearchField()->setText(QStringLiteral("circle"));
    QCOMPARE(panel->getSelectedNotes(), std::vector<pdf::PDFObjectReference>({ circle }));
    QCOMPARE(controller->getSelectedAnnotations().size(), size_t(2));

    // The filter cleared: both rows selected again
    panel->getSearchField()->clear();
    std::vector<pdf::PDFObjectReference> rows = panel->getSelectedNotes();
    std::sort(rows.begin(), rows.end());
    std::vector<pdf::PDFObjectReference> expected = { square, circle };
    std::sort(expected.begin(), expected.end());
    QCOMPARE(rows, expected);
    QCOMPARE(controller->getSelectedAnnotations().size(), size_t(2));

    // A hidden row selected in the panel only survives the filter too
    const pdf::PDFObjectReference hidden = H::noteReference(model, QStringLiteral("Hidden"));
    panel->selectNotes({ hidden });
    panel->getSearchField()->setText(QStringLiteral("hid"));
    QCOMPARE(panel->getSelectedNotes(), std::vector<pdf::PDFObjectReference>({ hidden }));
    panel->getSearchField()->clear();
    QVERIFY(std::find(panel->getSelectedNotes().cbegin(), panel->getSelectedNotes().cend(), hidden) != panel->getSelectedNotes().cend());
}

void AppNotesOutlineTest::notesHiddenRow()
{
    using H = NotesOutlineTestHelper;

    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::annotations(fixture.folder()));
    pdf::PDFAppNotesPanel* panel = H::notes(tab);
    QVERIFY(panel);
    fixture.settle();
    pdf::PDFAppNotesModel* model = panel->getModel();

    // 1026:21995: eye-off at the trailing side, the text of the row says it
    const int row = H::findNote(model, QStringLiteral("Hidden"));
    QVERIFY(row > 0);
    QVERIFY(model->getNote(row)->isHidden);
    QCOMPARE(model->index(row).data(pdf::PDFComponentRoles::StateIconRole).toString(), QStringLiteral("eye-off"));
    QVERIFY(model->index(row).data(Qt::AccessibleTextRole).toString().endsWith(QStringLiteral("(hidden on the page)")));
    QVERIFY(model->index(H::findNote(model, QStringLiteral("Square"))).data(pdf::PDFComponentRoles::StateIconRole).toString().isEmpty());

    // The icon is drawn by the delegate: icon/muted pixels in its rectangle
    pdf::PDFComponentPainter::NoteRow content = panel->findChild<pdf::PDFAppNotesDelegate*>()->getContent(model->index(row));
    QCOMPARE(content.stateIconName, QStringLiteral("eye-off"));
    const QRectF iconRect = pdf::PDFComponentPainter::getNoteRowStateIconRect(QRectF(0, 0, 276, 48), content);
    QCOMPARE(iconRect, QRectF(258, 25.5, 14, 14));
    QImage image(276, 48, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::white);
    {
        QPainter painter(&image);
        pdf::PDFComponentPainter::State state;
        state.flags = QStyle::State_Enabled;
        pdf::PDFComponentPainter::paintNoteRow(&painter, QRectF(0, 0, 276, 48), content, state);
    }
    int inked = 0;
    for (int y = 26; y < 39; ++y)
    {
        for (int x = 259; x < 271; ++x)
        {
            inked += image.pixelColor(x, y) != QColor(Qt::white) ? 1 : 0;
        }
    }
    QVERIFY(inked > 10);
    content.stateIconName.clear();
    QVERIFY(pdf::PDFComponentPainter::getNoteRowStateIconRect(QRectF(0, 0, 276, 48), content).isEmpty());

    // The menu of the row (1026:21995): Show on Page, Properties…, Delete without separators
    QMenu* menu = H::noteMenu(panel, { H::noteReference(model, QStringLiteral("Hidden")) });
    QVERIFY(menu);
    QStringList items;
    for (const QAction* action : menu->actions())
    {
        items << (action->isSeparator() ? QStringLiteral("-") : action->objectName());
    }
    QCOMPARE(items, QStringList({ "notesShow", "notesProperties", "notesDelete" }));
    delete menu;
}

void AppNotesOutlineTest::notesLockedDocument()
{
    using H = NotesOutlineTestHelper;

    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::annotations(fixture.folder()));
    pdf::PDFAppNotesPanel* panel = H::notes(tab);
    QVERIFY(panel);
    fixture.settle();
    pdf::PDFAppNotesModel* model = panel->getModel();
    pdf::PDFAppDocument* document = tab->getDocument();
    document->setLocked(true);
    fixture.settle();

    // 1030:161012: Open Comment and Properties… (read only), no editing
    QMenu* menu = H::noteMenu(panel, { H::noteReference(model, QStringLiteral("Square")) });
    QCOMPARE(H::actionNames(menu), QStringList({ "notesOpenComment", "notesProperties" }));
    delete menu;
    menu = H::noteMenu(panel, { H::noteReference(model, QStringLiteral("Hidden")) });
    QCOMPARE(H::actionNames(menu), QStringList({ "notesProperties" }));
    delete menu;
    menu = H::noteMenu(panel, { H::noteReference(model, QStringLiteral("Square")), H::noteReference(model, QStringLiteral("Circle")) });
    QVERIFY(!menu);

    // Del and Hide refuse with the reason in the status bar
    panel->selectNotes({ H::noteReference(model, QStringLiteral("Square")) });
    QVERIFY(!panel->deleteSelectedNotes());
    QVERIFY(!panel->setSelectedNotesHidden(true));
    QCOMPARE(tab->getView()->getStatusBar()->getMessage(), QStringLiteral("The document is locked. Unlock it to make changes."));
    panel->getListView()->setFocus();
    QTest::keyClick(panel->getListView(), Qt::Key_Delete);
    fixture.settle();
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 0);

    // The selection still works
    QVERIFY(!tab->getView()->getAnnotationController()->getSelectedAnnotations().empty());

    // A document, which does not allow changes of the annotations
    pdf::PDFAppDocumentTab* restricted = fixture.open(pdftest::AppDocumentSamples::restricted(fixture.folder()));
    pdf::PDFAppNotesPanel* restrictedPanel = H::notes(restricted);
    QVERIFY(restrictedPanel);
    fixture.settle();
    menu = H::noteMenu(restrictedPanel, { restrictedPanel->getModel()->getNote(1)->reference });
    QCOMPARE(H::actionNames(menu), QStringList({ "notesOpenComment", "notesProperties" }));
    delete menu;
    QVERIFY(!restrictedPanel->deleteSelectedNotes());
    QCOMPARE(restricted->getView()->getStatusBar()->getMessage(), QStringLiteral("The document does not allow changes."));
}

void AppNotesOutlineTest::notesLargeDocument()
{
    using H = NotesOutlineTestHelper;

    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::annotatedLarge(fixture.folder(), 2000, 5));
    QVERIFY(tab);
    fixture.settle();

    QElapsedTimer timer;
    timer.start();
    pdf::PDFAppNotesPanel* panel = H::notes(tab);
    QVERIFY(panel);
    fixture.settle();
    const qint64 buildTime = timer.elapsed();
    pdf::PDFAppNotesModel* model = panel->getModel();
    QCOMPARE(model->getNoteCount(), 10000);

    // A selection on a far page: the row follows, the view goes there by a click
    timer.restart();
    const pdf::PDFAppNote* last = model->getNote(model->rowCount() - 1);
    QVERIFY(last);
    tab->getView()->getAnnotationController()->selectAnnotations({ last->reference });
    fixture.settle();
    const qint64 syncTime = timer.elapsed();
    if (tab->getView()->getAnnotationController()->hasAnnotationSelection())
    {
        QCOMPARE(panel->getSelectedNotes(), std::vector<pdf::PDFObjectReference>({ last->reference }));
    }
    panel->activateRow(model->rowCount() - 1);
    QTRY_COMPARE(tab->getView()->getCurrentPageIndex(), last->page);

    // A deletion rebuilds the rows of the new version once
    timer.restart();
    const int collects = pdf::PDFAppNotes::getCollectCount();
    panel->selectNotes({ model->getNote(1)->reference });
    QVERIFY(panel->deleteSelectedNotes());
    fixture.settle();
    const qint64 deleteTime = timer.elapsed();
    QCOMPARE(model->getNoteCount(), 9999);
    QCOMPARE(pdf::PDFAppNotes::getCollectCount(), collects + 1);
    qInfo("Notes of 2000 pages x 5: build %lld ms, selection %lld ms, delete and rebuild %lld ms", buildTime, syncTime, deleteTime);
    QVERIFY(deleteTime < 60000);
}

void AppNotesOutlineTest::deletionMatrix()
{
    using H = NotesOutlineTestHelper;

    // The tab, the window or the panel deleted while a new item waits for its
    // title, while the inline editor is open, with a context menu, with a queued
    // drop or a queued end of an edit
    for (int scenario = 0; scenario < 6; ++scenario)
    {
        for (int target = 0; target < 3; ++target)
        {
            pdftest::AppDocumentFixture fixture;
            QPointer<pdf::PDFAppDocumentTab> tab = fixture.open(pdftest::AppDocumentSamples::outline(fixture.folder()));
            QVERIFY(tab);
            QPointer<pdf::PDFAppOutlinePanel> panel = H::outline(tab);
            QVERIFY(panel);
            fixture.settle();

            switch (scenario)
            {
                case 0:
                    QVERIFY(tab->getView()->getOutlineEditor()->createItemAtCurrentView());
                    fixture.settle();
                    break;
                case 1:
                    QVERIFY(panel->editItem({ 1 }));
                    break;
                case 2:
                    panel->createContextMenu(panel->getItemIndex({ 0 }))->popup(QPoint(100, 100));
                    fixture.settle();
                    break;
                case 3:
                {
                    std::unique_ptr<QMimeData> data(panel->getViewModel()->mimeData({ panel->getItemIndex({ 0 }) }));
                    QDropEvent drop(QPointF(5, 5), Qt::MoveAction, data.get(), Qt::LeftButton, Qt::NoModifier);
                    QApplication::sendEvent(panel->getTreeView()->viewport(), &drop);
                    break;
                }
                case 4:
                {
                    QVERIFY(panel->editItem({ 1 }));
                    QLineEdit* edit = H::inlineEditor(panel);
                    edit->setText(QStringLiteral("Queued"));
                    QTest::keyClick(edit, Qt::Key_Enter);
                    break;
                }
                case 5:
                {
                    QVERIFY(tab->getView()->getOutlineEditor()->createItemAtCurrentView());
                    fixture.settle();
                    QTest::keyClick(H::inlineEditor(panel), Qt::Key_Enter);
                    break;
                }
                default:
                    break;
            }

            if (target == 0)
            {
                fixture.window->closeTab(tab);
            }
            else if (target == 1)
            {
                delete fixture.window.data();
            }
            else
            {
                delete panel.data();
            }
            fixture.settle();
            QVERIFY(!panel);
            if (target == 2)
            {
                QVERIFY(tab);
                QVERIFY(!tab->getView()->getOutlineEditor()->isCreationPending());
                QVERIFY(!pdf::PDFAppOutlineEditor::isCreationPending(tab->getDocument()));
                QVERIFY(H::outline(tab));
            }
        }
    }

    // Notes: the menu open, then the tab / window / panel deleted; the document deleted
    for (int target = 0; target < 3; ++target)
    {
        pdftest::AppDocumentFixture fixture;
        QPointer<pdf::PDFAppDocumentTab> tab = fixture.open(pdftest::AppDocumentSamples::annotations(fixture.folder()));
        QPointer<pdf::PDFAppNotesPanel> panel = H::notes(tab);
        QVERIFY(panel);
        fixture.settle();
        QMenu* menu = H::noteMenu(panel, { panel->getModel()->getNote(1)->reference });
        QPointer<QMenu> menuGuard = menu;
        menu->popup(QPoint(100, 100));
        H::findAction(menu, QStringLiteral("notesProperties"))->trigger();
        if (target == 0)
        {
            fixture.window->closeTab(tab);
        }
        else if (target == 1)
        {
            delete fixture.window.data();
        }
        else
        {
            delete panel.data();
        }
        fixture.settle();
        QVERIFY(!menuGuard);
    }

    // The editor and the panels of a standalone view; the document deleted first
    {
        pdftest::AppDocumentFixture fixture;
        pdf::PDFAppDocumentView* view = new pdf::PDFAppDocumentView(fixture.context.get());
        pdf::PDFAppDocument* document = new pdf::PDFAppDocument(fixture.context.get(), nullptr);
        pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::outline(fixture.folder()));
        QVERIFY(document->setCopyOf(tab->getDocument()));
        view->setDocument(document);
        view->show();
        QVERIFY(view->getSidebar()->showPanel(pdf::PDFAppDocumentPanelIds::outline()));
        fixture.settle();
        QVERIFY(view->getOutlineEditor()->createItemAtCurrentView());
        fixture.settle();
        delete document;
        fixture.settle();
        QVERIFY(!view->getOutlineEditor()->isCreationPending());
        delete view;
        fixture.settle();
    }
}

void AppNotesOutlineTest::deleteFromOwnSignals_data()
{
    QTest::addColumn<QString>("signal");
    QTest::addColumn<bool>("deleteWindow");
    // notesSelection: the selection signal of the controller (it comes from the
    // event loop, not inside of the emission of PDFWidgetAnnotationManager and of
    // the selection model of the list)
    for (const char* name : { "outlineChanged", "editorStateChanged", "documentChangedByOutline", "notesDelete", "notesTitleChanged", "notesSelection" })
    {
        QTest::newRow(QStringLiteral("%1-tab").arg(QLatin1String(name)).toLatin1().constData()) << QString::fromLatin1(name) << false;
        QTest::newRow(QStringLiteral("%1-window").arg(QLatin1String(name)).toLatin1().constData()) << QString::fromLatin1(name) << true;
    }
}

void AppNotesOutlineTest::deleteFromOwnSignals()
{
    using H = NotesOutlineTestHelper;

    // The application deletes the tab (or the window) synchronously from a signal
    // of the outline editor, of the document changed by it or by the Notes panel,
    // and of the selection of the annotations made by the panel
    QFETCH(QString, signal);
    QFETCH(bool, deleteWindow);

    pdftest::AppDocumentFixture fixture;
    QPointer<pdf::PDFAppMainWindow> window = fixture.window;
    const bool isNotes = signal.startsWith(QStringLiteral("notes"));
    QPointer<pdf::PDFAppDocumentTab> tab = fixture.open(isNotes ? pdftest::AppDocumentSamples::annotations(fixture.folder()) : pdftest::AppDocumentSamples::outline(fixture.folder()));
    QVERIFY(tab);
    QObject receiver;
    bool isDeleted = false;
    auto deleteNow = [&isDeleted, &tab, &window, deleteWindow]()
    {
        if (isDeleted)
        {
            return;
        }
        isDeleted = true;
        if (deleteWindow)
        {
            delete window.data();
        }
        else
        {
            delete tab.data();
        }
    };

    if (!isNotes)
    {
        pdf::PDFAppOutlinePanel* panel = H::outline(tab);
        QVERIFY(panel);
        fixture.settle();
        pdf::PDFAppOutlineEditor* editor = tab->getView()->getOutlineEditor();
        if (signal == QStringLiteral("outlineChanged"))
        {
            connect(editor, &pdf::PDFAppOutlineEditor::outlineChanged, &receiver, deleteNow);
        }
        else if (signal == QStringLiteral("editorStateChanged"))
        {
            connect(editor, &pdf::PDFAppOutlineEditor::stateChanged, &receiver, deleteNow);
        }
        else
        {
            connect(tab->getDocument(), &pdf::PDFAppDocument::documentChanged, &receiver, deleteNow);
        }

        // A rename by the inline editor (from the event loop)
        QVERIFY(panel->editItem({ 0 }));
        QLineEdit* edit = H::inlineEditor(panel);
        QVERIFY(edit);
        edit->setText(QStringLiteral("Deleting"));
        QTest::keyClick(edit, Qt::Key_Enter);
    }
    else
    {
        pdf::PDFAppNotesPanel* panel = H::notes(tab);
        QVERIFY(panel);
        fixture.settle();
        const pdf::PDFObjectReference square = H::noteReference(panel->getModel(), QStringLiteral("Square"));
        if (signal == QStringLiteral("notesDelete"))
        {
            connect(tab->getDocument(), &pdf::PDFAppDocument::documentChanged, &receiver, deleteNow);
            panel->selectNotes({ square });
            panel->deleteSelectedNotes();
        }
        else if (signal == QStringLiteral("notesSelection"))
        {
            // The rows select the annotation on the page (the list emits its
            // selection, the controller selects in the engine, which emits too)
            connect(tab->getView()->getAnnotationController(), &pdf::PDFAppAnnotationController::selectionChanged, &receiver, deleteNow);
            panel->selectNotes({ square });
            QCOMPARE(tab->getView()->getAnnotationController()->getSelectedAnnotations(), std::vector<pdf::PDFObjectReference>({ square }));
        }
        else
        {
            connect(panel, &pdf::PDFAppDocumentPanel::titleChanged, &receiver, deleteNow);
            panel->selectNotes({ square });
            panel->setSelectedNotesHidden(true);
        }
    }

    QTRY_VERIFY_WITH_TIMEOUT(isDeleted, 10000);
    fixture.settle();
    QVERIFY(deleteWindow ? !window : !tab);
    if (!deleteWindow)
    {
        QVERIFY(window);
        QVERIFY(fixture.open(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("After.pdf"), 1)));
    }
}

void AppNotesOutlineTest::themesAndScales()
{
    using H = NotesOutlineTestHelper;

    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* outlineTab = fixture.open(pdftest::AppDocumentSamples::outline(fixture.folder()));
    pdf::PDFAppOutlinePanel* outline = H::outline(outlineTab);
    QVERIFY(outline);
    fixture.settle();
    QVERIFY(outlineTab->getView()->getOutlineEditor()->setItemStyle({ 1 }, true, true));
    fixture.settle();
    pdf::PDFAppDocumentTab* notesTab = fixture.open(pdftest::AppDocumentSamples::annotations(fixture.folder()));
    pdf::PDFAppNotesPanel* notes = H::notes(notesTab);
    QVERIFY(notes);
    fixture.settle();
    notes->selectNotes({ H::noteReference(notes->getModel(), QStringLiteral("Reply 1")) });
    notes->getListView()->scrollTo(notes->getModel()->index(H::findNote(notes->getModel(), QStringLiteral("Reply 1"))));

    for (const pdf::PDFThemeColors::Scheme scheme : { pdf::PDFThemeColors::Scheme::Light, pdf::PDFThemeColors::Scheme::Dark })
    {
        pdf::PDFThemeManager::getInstance()->setScheme(scheme);
        fixture.settle();
        const pdf::PDFThemeManager* manager = pdf::PDFThemeManager::getInstance();
        for (const qreal ratio : { 1.0, 1.5, 2.0 })
        {
            for (pdf::PDFAppDocumentTab* tab : { outlineTab, notesTab })
            {
                fixture.window->getNavigator()->navigate(pdf::PDFAppRoutes::document(tab->getDocumentId()));
                fixture.settle();
                pdf::PDFAppDocumentSidebar* sidebar = H::sidebar(tab);
                QPixmap pixmap(sidebar->size() * ratio);
                pixmap.setDevicePixelRatio(ratio);
                pixmap.fill(Qt::transparent);
                sidebar->render(&pixmap);
                const int leakPixels = H::countLeakPixels(pixmap.toImage());
                if (leakPixels > 0 && !qEnvironmentVariableIsEmpty("PDF4QT_TEST_FAILURE_IMAGES"))
                {
                    pixmap.save(QDir(qEnvironmentVariable("PDF4QT_TEST_FAILURE_IMAGES")).filePath(QStringLiteral("leak-%1-%2.png").arg(tab == outlineTab ? QStringLiteral("outline") : QStringLiteral("notes")).arg(ratio)));
                }
                QCOMPARE(leakPixels, 0);
            }
        }

        // The selected reply: the indentation of 24 keeps bg/sidebar, the row is selected
        fixture.window->getNavigator()->navigate(pdf::PDFAppRoutes::document(notesTab->getDocumentId()));
        fixture.settle();
        QListView* list = notes->getListView();
        pdf::PDFAppDocumentSidebar* notesSidebar = H::sidebar(notesTab);
        const QRect replyRect = list->visualRect(notes->getModel()->index(H::findNote(notes->getModel(), QStringLiteral("Reply 1"))));
        const QPoint origin = list->viewport()->mapTo(notesSidebar, QPoint(0, 0));
        QPixmap pixmap(notesSidebar->size());
        notesSidebar->render(&pixmap);
        const QImage image = pixmap.toImage();
        const QColor selected = image.pixelColor(origin + QPoint(replyRect.left() + 24 + 4, replyRect.center().y()));
        const QColor indent = image.pixelColor(origin + QPoint(replyRect.left() + 12, replyRect.center().y()));
        QVERIFY2(selected != indent, qPrintable(selected.name() + QLatin1Char(' ') + indent.name()));
        QCOMPARE(indent.name(), manager->getColor(pdf::PDFThemeColors::BgSidebar).name());
    }
}

void AppNotesOutlineTest::rightToLeft()
{
    using H = NotesOutlineTestHelper;

    QApplication::setLayoutDirection(Qt::RightToLeft);
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* notesTab = fixture.open(pdftest::AppDocumentSamples::annotations(fixture.folder()));
    pdf::PDFAppNotesPanel* notes = H::notes(notesTab);
    QVERIFY(notes);
    fixture.settle();
    notes->selectNotes({ H::noteReference(notes->getModel(), QStringLiteral("Reply 1")) });
    QListView* list = notes->getListView();
    const QModelIndex replyIndex = notes->getModel()->index(H::findNote(notes->getModel(), QStringLiteral("Reply 1")));
    list->scrollTo(replyIndex);
    fixture.settle();

    // The indentation lies at the right side
    const QRect replyRect = list->visualRect(replyIndex);
    pdf::PDFAppDocumentSidebar* notesSidebar = H::sidebar(notesTab);
    const QPoint origin = list->viewport()->mapTo(notesSidebar, QPoint(0, 0));
    QPixmap pixmap(notesSidebar->size());
    notesSidebar->render(&pixmap);
    const QImage image = pixmap.toImage();
    const QColor indent = image.pixelColor(origin + QPoint(replyRect.right() - 12, replyRect.center().y()));
    const QColor selected = image.pixelColor(origin + QPoint(replyRect.left() + 6, replyRect.center().y()));
    QCOMPARE(indent.name(), pdf::PDFThemeManager::getInstance()->getColor(pdf::PDFThemeColors::BgSidebar).name());
    QVERIFY(selected != indent);

    // The arrow of a reply points to its text (a directional icon, mirrored as the chevrons)
    const pdf::PDFAppNotesDelegate* delegate = notes->findChild<pdf::PDFAppNotesDelegate*>();
    QVERIFY(delegate);
    QCOMPARE(delegate->getPaintedContent(replyIndex, Qt::RightToLeft).iconName, QStringLiteral("corner-down-left"));
    QCOMPARE(delegate->getPaintedContent(replyIndex, Qt::LeftToRight).iconName, QStringLiteral("corner-down-right"));

    // The "+" of the outline is at the left side of the header
    pdf::PDFAppDocumentTab* outlineTab = fixture.open(pdftest::AppDocumentSamples::outline(fixture.folder()));
    pdf::PDFAppOutlinePanel* outline = H::outline(outlineTab);
    QVERIFY(outline);
    fixture.settle();
    QWidget* header = H::sidebar(outlineTab)->getHeader();
    QVERIFY(outline->getNewItemButton()->mapTo(header, QPoint(0, 0)).x() < header->width() / 2);
    QApplication::setLayoutDirection(Qt::LeftToRight);
}

void AppNotesOutlineTest::minimalWindow()
{
    using H = NotesOutlineTestHelper;

    pdftest::AppDocumentFixture fixture(QSize(720, 480));
    pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::outline(fixture.folder()));
    pdf::PDFAppOutlinePanel* outline = H::outline(tab);
    QVERIFY(outline);
    fixture.settle();

    // The "+" lies whole in the header, the inline editor in the tree
    QWidget* header = H::sidebar(tab)->getHeader();
    const QRect buttonRect(outline->getNewItemButton()->mapTo(header, QPoint(0, 0)), outline->getNewItemButton()->size());
    QVERIFY(header->rect().contains(buttonRect));
    QVERIFY(tab->getView()->getOutlineEditor()->createItemAtCurrentView());
    fixture.settle();
    QLineEdit* edit = H::inlineEditor(outline);
    QVERIFY(edit);
    QVERIFY(outline->getTreeView()->viewport()->rect().contains(QRect(edit->pos(), edit->size()).adjusted(1, 1, -1, -1)) ||
            edit->width() <= outline->getTreeView()->viewport()->width());
    QTest::keyClick(edit, Qt::Key_Escape);
    fixture.settle();

    pdf::PDFAppDocumentTab* notesTab = fixture.open(pdftest::AppDocumentSamples::annotations(fixture.folder()));
    pdf::PDFAppNotesPanel* notes = H::notes(notesTab);
    QVERIFY(notes);
    fixture.settle();
    QListView* list = notes->getListView();
    const QRect replyRect = list->visualRect(notes->getModel()->index(H::findNote(notes->getModel(), QStringLiteral("Reply 1"))));
    QVERIFY(replyRect.right() <= list->viewport()->width());
}

void AppNotesOutlineTest::accessibility()
{
    using H = NotesOutlineTestHelper;

    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::outline(fixture.folder()));
    pdf::PDFAppOutlinePanel* outline = H::outline(tab);
    QVERIFY(outline);
    fixture.settle();
    QCOMPARE(outline->getNewItemButton()->accessibleName(), QStringLiteral("New outline item"));
    QCOMPARE(outline->getNewItemButton()->toolTip(), QStringLiteral("New outline item"));
    QCOMPARE(outline->getNewItemButton()->focusPolicy(), Qt::TabFocus);
    QCOMPARE(outline->getTreeView()->accessibleName(), QStringLiteral("Outline"));
    QMenu* menu = outline->createContextMenu(outline->getItemIndex({ 0 }));
    for (QAction* action : menu->actions())
    {
        QVERIFY(action->isSeparator() || !action->text().isEmpty());
    }
    delete menu;

    pdf::PDFAppDocumentTab* notesTab = fixture.open(pdftest::AppDocumentSamples::annotations(fixture.folder()));
    pdf::PDFAppNotesPanel* notes = H::notes(notesTab);
    QVERIFY(notes);
    fixture.settle();
    QCOMPARE(notes->getListView()->accessibleName(), QStringLiteral("Notes"));
    const pdf::PDFAppNotesModel* model = notes->getModel();
    QCOMPARE(model->index(H::findNote(model, QStringLiteral("Reply 1"))).data(Qt::AccessibleTextRole).toString(), QStringLiteral("Reply - Replier 1: Reply 1"));
    QCOMPARE(model->index(H::findNote(model, QStringLiteral("Hidden"))).data(Qt::AccessibleTextRole).toString(), QStringLiteral("Tester: Hidden (hidden on the page)"));

    // The keyboard: Shift+F10 opens the menu of the current note, Enter goes to it
    QListView* list = notes->getListView();
    list->setFocus();
    list->setCurrentIndex(model->index(H::findNote(model, QStringLiteral("Overlap B"))));
    QContextMenuEvent event(QContextMenuEvent::Keyboard, QPoint(-1, -1), QPoint(-1, -1));
    QApplication::sendEvent(list, &event);
    fixture.settle();
    QMenu* shown = notes->findChild<QMenu*>(QStringLiteral("notesContextMenu"));
    QVERIFY(shown);
    shown->close();
    fixture.settle();
    QTest::keyClick(list, Qt::Key_Return);
    QTRY_COMPARE(notesTab->getView()->getCurrentPageIndex(), pdf::PDFInteger(1));
}

void AppNotesOutlineTest::languageChange()
{
    using H = NotesOutlineTestHelper;

    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::outline(fixture.folder()));
    pdf::PDFAppOutlinePanel* outline = H::outline(tab);
    QVERIFY(outline);
    fixture.settle();
    outline->getNewItemButton()->setToolTip(QString());
    outline->getRenameAction()->setText(QString());
    QEvent event(QEvent::LanguageChange);
    QApplication::sendEvent(outline, &event);
    QCOMPARE(outline->getNewItemButton()->toolTip(), QStringLiteral("New outline item"));
    QCOMPARE(outline->getRenameAction()->text(), QStringLiteral("Rename outline item"));
    QCOMPARE(outline->getSearchField()->placeholderText(), QStringLiteral("Search outline…"));
}

void AppNotesOutlineTest::reviewRenameUnchangedTitle()
{
    using H = NotesOutlineTestHelper;

    // Titles, which the panel shows sanitized (runs of spaces, a tab, a format
    // character, a leading space): F2 and Enter without a change writes nothing
    pdf::PDFDocumentBuilder builder;
    const pdf::PDFObjectReference page = builder.appendPage(QRectF(0, 0, 300, 400));
    const pdf::PDFObjectReference root = builder.addObject(pdf::PDFObject());
    const pdf::PDFObjectReference a = builder.addObject(pdf::PDFObject());
    const pdf::PDFObjectReference b = builder.addObject(pdf::PDFObject());
    builder.setObject(root, H::dictionary({ { "Type", H::name("Outlines") }, { "First", H::reference(a) }, { "Last", H::reference(b) }, { "Count", H::integer(2) } }));
    builder.setObject(a, H::dictionary({ { "Title", H::text(QStringLiteral(" 1.  Introduction\t")) }, { "Parent", H::reference(root) }, { "Next", H::reference(b) }, { "Dest", H::fit(page) } }));
    builder.setObject(b, H::dictionary({ { "Title", H::text((QStringLiteral("Mark") + QChar(0x200E) + QStringLiteral("ed"))) }, { "Parent", H::reference(root) }, { "Prev", H::reference(a) }, { "Dest", H::fit(page) } }));
    builder.mergeTo(builder.getCatalogReference(), H::dictionary({ { "Outlines", H::reference(root) } }));
    pdftest::AppDocumentFixture fixture;
    const QString path = H::write(builder, fixture.folder(), QStringLiteral("spaces-outline.pdf"));
    QVERIFY(!path.isEmpty());

    // The operation itself: the title the user interface shows is no change
    const pdf::PDFDocumentPointer original = H::read(path);
    QVERIFY(original);
    QVERIFY(!pdf::PDFAppAnnotationOperations::renameOutlineItem(original, { 0 }, QStringLiteral("1. Introduction")));
    QVERIFY(!pdf::PDFAppAnnotationOperations::renameOutlineItem(original, { 1 }, QStringLiteral("Mark ed")));
    QVERIFY(pdf::PDFAppAnnotationOperations::renameOutlineItem(original, { 0 }, QStringLiteral("1. Overview")));

    // The inline editor of the panel
    pdf::PDFAppDocumentTab* tab = fixture.open(path);
    pdf::PDFAppOutlinePanel* panel = H::outline(tab);
    QVERIFY(panel);
    fixture.settle();
    for (size_t row : { size_t(0), size_t(1) })
    {
        QVERIFY(panel->editItem({ row }));
        QLineEdit* edit = H::inlineEditor(panel);
        QVERIFY(edit);
        QTest::keyClick(edit, Qt::Key_Enter);
        fixture.settle();
        QCOMPARE(pdftest::AppDocumentFixture::undoCount(tab->getDocument()), 0);
        QVERIFY(!tab->getDocument()->isModified());
    }
}

void AppNotesOutlineTest::reviewNewItemWhileRenaming()
{
    using H = NotesOutlineTestHelper;

    // F2, a typed title and then the "+" button (it takes no focus) or the
    // command: the typed title is written first, then the new item waits for
    // its own title
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::outline(fixture.folder()));
    pdf::PDFAppOutlinePanel* panel = H::outline(tab);
    QVERIFY(panel);
    fixture.settle();
    pdf::PDFAppDocument* document = tab->getDocument();
    pdf::PDFAppOutlineEditor* editor = tab->getView()->getOutlineEditor();

    QVERIFY(panel->editItem({ 0 }));
    QLineEdit* edit = H::inlineEditor(panel);
    QVERIFY(edit);
    edit->setText(QStringLiteral("Preface"));
    QVERIFY(tab->executeCommand(pdf::PDFAppCommands::outlineNewItem()));
    fixture.settle();
    QCOMPARE(H::titles(document->getDocument().data()), QStringList({ "Preface", "Chapter 1", ">Section 1.1", "Appendix" }));
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 1);
    QCOMPARE(document->getUndoStack()->getUndoDescription(), QStringLiteral("Rename outline item"));

    // The new item after the renamed one
    QVERIFY(editor->isCreationPending());
    QVERIFY(panel->getPendingItem());
    edit = H::inlineEditor(panel);
    QVERIFY(edit);
    edit->setText(QStringLiteral("Foreword"));
    QTest::keyClick(edit, Qt::Key_Enter);
    fixture.settle();
    QCOMPARE(H::titles(document->getDocument().data()), QStringList({ "Preface", "Foreword", "Chapter 1", ">Section 1.1", "Appendix" }));
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 2);
    QCOMPARE(H::titles(panel->getSourceModel()), H::titles(document->getDocument().data()));

    // The "+" button of the header the same way
    QVERIFY(panel->editItem({ 2 }));
    edit = H::inlineEditor(panel);
    QVERIFY(edit);
    edit->setText(QStringLiteral("Part 1"));
    QVERIFY(panel->getNewItemButton());
    panel->getNewItemButton()->click();
    fixture.settle();
    QCOMPARE(H::titles(document->getDocument().data()).at(2), QStringLiteral("Part 1"));
    QVERIFY(editor->isCreationPending());
    editor->cancelCreation();
    fixture.settle();
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 3);
}

void AppNotesOutlineTest::reviewStaleCurrentItem()
{
    using H = NotesOutlineTestHelper;

    // The first item lies on the second page: on the first page no item is
    // highlighted - the commands do not act on the item highlighted before
    pdf::PDFDocumentBuilder builder;
    std::vector<pdf::PDFObjectReference> pages;
    for (int i = 0; i < 3; ++i)
    {
        pages.push_back(builder.appendPage(QRectF(0, 0, 300, 400)));
    }
    const pdf::PDFObjectReference root = builder.addObject(pdf::PDFObject());
    const pdf::PDFObjectReference a = builder.addObject(pdf::PDFObject());
    const pdf::PDFObjectReference b = builder.addObject(pdf::PDFObject());
    builder.setObject(root, H::dictionary({ { "Type", H::name("Outlines") }, { "First", H::reference(a) }, { "Last", H::reference(b) }, { "Count", H::integer(2) } }));
    builder.setObject(a, H::dictionary({ { "Title", H::text(QStringLiteral("A")) }, { "Parent", H::reference(root) }, { "Next", H::reference(b) }, { "Dest", H::fit(pages[1]) } }));
    builder.setObject(b, H::dictionary({ { "Title", H::text(QStringLiteral("B")) }, { "Parent", H::reference(root) }, { "Prev", H::reference(a) }, { "Dest", H::fit(pages[2]) } }));
    builder.mergeTo(builder.getCatalogReference(), H::dictionary({ { "Outlines", H::reference(root) } }));
    pdftest::AppDocumentFixture fixture;
    const QString path = H::write(builder, fixture.folder(), QStringLiteral("late-outline.pdf"));
    QVERIFY(!path.isEmpty());

    pdf::PDFAppDocumentTab* tab = fixture.open(path);
    pdf::PDFAppOutlinePanel* panel = H::outline(tab);
    QVERIFY(panel);
    fixture.settle();
    pdf::PDFAppDocumentView* view = tab->getView();
    pdf::PDFAppOutlineEditor* editor = view->getOutlineEditor();

    view->goToPage(1);
    fixture.settle();
    QCOMPARE(panel->getCurrentItem().data().toString(), QStringLiteral("A"));
    QCOMPARE(editor->getCurrentItem(), H::Path({ 0 }));
    QVERIFY(tab->isCommandEnabled(pdf::PDFAppCommands::outlineDelete()));

    view->goToPage(0);
    fixture.settle();
    QVERIFY(!panel->getCurrentItem().isValid());
    QVERIFY(editor->getCurrentItem().empty());
    QVERIFY(!panel->getTreeView()->currentIndex().isValid());
    QVERIFY(!tab->isCommandEnabled(pdf::PDFAppCommands::outlineDelete()));
    QVERIFY(!tab->isCommandEnabled(pdf::PDFAppCommands::outlineRename()));

    // The commands delete nothing then (the focus of the tree would make its first
    // item current - Qt - and show it by the focus ring)
    tab->executeCommand(pdf::PDFAppCommands::outlineDelete());
    tab->executeCommand(pdf::PDFAppCommands::outlineBold());
    fixture.settle();
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(tab->getDocument()), 0);
    QCOMPARE(H::titles(tab->getDocument()->getDocument().data()), QStringList({ "A", "B" }));
}

void AppNotesOutlineTest::reviewGroupMembersNotDeleted()
{
    // A member of a group (/IRT with /RT /Group) is listed as a note of its own:
    // it is not a reply, the deletion of its primary annotation does not take it
    std::vector<pdf::PDFAppNote> notes(4);
    for (int i = 0; i < 4; ++i)
    {
        notes[size_t(i)].reference = pdf::PDFObjectReference(200 + i, 0);
        notes[size_t(i)].type = pdf::AnnotationType::Text;
        notes[size_t(i)].text = QStringLiteral("g%1").arg(i);
    }
    notes[1].inReplyTo = notes[0].reference; // group member (isReply = false)
    notes[2].isReply = true;                 // reply of the primary annotation
    notes[2].inReplyTo = notes[0].reference;
    notes[3].isReply = true;                 // reply of the group member
    notes[3].inReplyTo = notes[1].reference;

    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("group.pdf"), 1));
    QVERIFY(tab);
    pdf::PDFAppNotesModel model(nullptr);
    model.setNotes(notes, tab->getDocument()->getDocument());
    QVERIFY(model.findRow(notes[1].reference) > 0);

    const std::vector<pdf::PDFObjectReference> primaryThread = model.getThreads({ notes[0].reference });
    QCOMPARE(primaryThread, std::vector<pdf::PDFObjectReference>({ notes[0].reference, notes[2].reference }));
    const std::vector<pdf::PDFObjectReference> memberThread = model.getThreads({ notes[1].reference });
    QCOMPARE(memberThread, std::vector<pdf::PDFObjectReference>({ notes[1].reference, notes[3].reference }));
}

void AppNotesOutlineTest::reviewOutlineEditableCache()
{
    using H = NotesOutlineTestHelper;

    // Wrap-up RC-CD: the editability of the outline was read again (the whole
    // outline) at every commit; a change of annotations or of form fields keeps it
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::outline(fixture.folder()));
    QVERIFY(tab);
    fixture.settle();
    pdf::PDFAppDocument* document = tab->getDocument();
    pdf::PDFAppOutlineEditor* editor = tab->getView()->getOutlineEditor();
    QVERIFY(document->isReady());
    QVERIFY(editor->isEditingAllowed());

    // A document, whose outline cannot be edited (shared children), committed as a
    // change of annotations: the outline is not read again (the probe shows it)
    const pdf::PDFDocumentPointer shared = H::read(H::hostileOutline(fixture.folder(), QStringLiteral("shared")));
    QVERIFY(shared);
    QVERIFY(!pdf::PDFAppAnnotationOperations::isOutlineEditable(shared.data()));
    QVERIFY(document->commit(pdf::PDFModifiedDocument(shared, nullptr, pdf::PDFModifiedDocument::ModificationFlags(pdf::PDFModifiedDocument::Annotation)),
                             QStringLiteral("Annotation"), document->getVersion(), pdf::PDFAppChangeSource::User));
    QVERIFY(editor->isEditingAllowed());
    QVERIFY(document->commit(pdf::PDFModifiedDocument(shared, nullptr, pdf::PDFModifiedDocument::ModificationFlags(pdf::PDFModifiedDocument::FormField | pdf::PDFModifiedDocument::Annotation)),
                             QStringLiteral("Form"), document->getVersion(), pdf::PDFAppChangeSource::User));
    QVERIFY(editor->isEditingAllowed());

    // A change of the outline, of the pages or a reset reads it again
    QVERIFY(document->commit(pdf::PDFModifiedDocument(shared, nullptr, pdf::PDFModifiedDocument::ModificationFlags(pdf::PDFModifiedDocument::Outline)),
                             QStringLiteral("Outline"), document->getVersion(), pdf::PDFAppChangeSource::User));
    QVERIFY(!editor->isEditingAllowed());
    QTemporaryDir otherFolder;
    const pdf::PDFDocumentPointer editable = H::read(pdftest::AppDocumentSamples::outline(otherFolder.path()));
    QVERIFY(editable);
    QVERIFY(document->commit(pdf::PDFModifiedDocument(editable, nullptr, pdf::PDFModifiedDocument::ModificationFlags(pdf::PDFModifiedDocument::PageContents)),
                             QStringLiteral("Pages"), document->getVersion(), pdf::PDFAppChangeSource::User));
    QVERIFY(editor->isEditingAllowed());
    QVERIFY(document->commit(pdf::PDFModifiedDocument(shared, nullptr, pdf::PDFModifiedDocument::ModificationFlags(pdf::PDFModifiedDocument::Reset)),
                             QStringLiteral("Reset"), document->getVersion(), pdf::PDFAppChangeSource::User));
    QVERIFY(!editor->isEditingAllowed());
    fixture.settle();
}

void AppNotesOutlineTest::reviewNotesMenuEmptyArea()
{
    using H = NotesOutlineTestHelper;

    // A right click below the last row is not the menu of the selected notes
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::annotations(fixture.folder()));
    pdf::PDFAppNotesPanel* panel = H::notes(tab);
    QVERIFY(panel);
    fixture.settle();
    pdf::PDFAppNotesModel* model = panel->getModel();
    const pdf::PDFObjectReference circle = H::noteReference(model, QStringLiteral("Circle"));
    panel->getSearchField()->setText(QStringLiteral("Circle"));
    fixture.settle();
    panel->selectNotes({ circle });
    QListView* list = panel->getListView();
    QVERIFY(list->currentIndex().isValid());
    const QRect lastRow = list->visualRect(model->index(model->rowCount() - 1));
    const QPoint empty(lastRow.center().x(), list->viewport()->height() - 4);
    QVERIFY(empty.y() > lastRow.bottom());
    QVERIFY(!list->indexAt(empty).isValid());

    QContextMenuEvent event(QContextMenuEvent::Mouse, empty, list->viewport()->mapToGlobal(empty));
    QApplication::sendEvent(list->viewport(), &event);
    fixture.settle();
    QVERIFY(!panel->findChild<QMenu*>(QStringLiteral("notesContextMenu")));
    QCOMPARE(panel->getSelectedNotes(), std::vector<pdf::PDFObjectReference>({ circle }));

    // The keyboard menu (a position outside of the rows) is the menu of the current row
    QContextMenuEvent keyboard(QContextMenuEvent::Keyboard, QPoint(-1, -1), QPoint(-1, -1));
    QApplication::sendEvent(list, &keyboard);
    fixture.settle();
    QMenu* shown = panel->findChild<QMenu*>(QStringLiteral("notesContextMenu"));
    QVERIFY(shown);
    shown->close();
    fixture.settle();
}

void AppNotesOutlineTest::screenshots()
{
    using H = NotesOutlineTestHelper;

    const QString folder = qEnvironmentVariable("PDF4QT_APP_SCREENSHOTS");
    if (folder.isEmpty())
    {
        QSKIP("PDF4QT_APP_SCREENSHOTS is not set");
    }
    QDir().mkpath(folder);

    struct Shot
    {
        QString name;
        QSize size;
        Qt::LayoutDirection direction;
    };
    const std::vector<Shot> shots = { { QStringLiteral(""), QSize(1440, 960), Qt::LeftToRight }, { QStringLiteral("-720"), QSize(720, 480), Qt::LeftToRight },
                                      { QStringLiteral("-rtl"), QSize(1440, 960), Qt::RightToLeft } };
    for (const Shot& shot : shots)
    {
        for (const pdf::PDFThemeColors::Scheme scheme : { pdf::PDFThemeColors::Scheme::Light, pdf::PDFThemeColors::Scheme::Dark })
        {
            pdftest::AppDocumentFixture fixture(shot.size);
            QApplication::setLayoutDirection(shot.direction);
            pdf::PDFThemeManager::getInstance()->setScheme(scheme);
            const QString theme = scheme == pdf::PDFThemeColors::Scheme::Dark ? QStringLiteral("dark") : QStringLiteral("light");

            // Notes with the selected note, its replies and a hidden note (1025:21533, 1026:21995)
            pdf::PDFAppDocumentTab* notesTab = fixture.open(pdftest::AppDocumentSamples::annotations(fixture.folder()));
            pdf::PDFAppNotesPanel* notes = H::notes(notesTab);
            fixture.settle();
            notes->selectNotes({ H::noteReference(notes->getModel(), QStringLiteral("Note with replies")) });
            notes->getListView()->scrollTo(notes->getModel()->index(H::findNote(notes->getModel(), QStringLiteral("Hidden"))));
            fixture.settle();
            QTest::qWait(300);
            fixture.settle();
            for (const qreal ratio : { 1.0, 2.0 })
            {
                pdf::PDFAppDocumentSidebar* sidebar = H::sidebar(notesTab);
                QPixmap pixmap(sidebar->size() * ratio);
                pixmap.setDevicePixelRatio(ratio);
                sidebar->render(&pixmap);
                pixmap.save(QDir(folder).filePath(QStringLiteral("sidebar-notes-%1%2@%3x.png").arg(theme, shot.name).arg(int(ratio))));
            }
            QMenu* menu = notes->createContextMenu();
            menu->popup(notes->mapToGlobal(QPoint(100, 200)));
            fixture.settle();
            menu->grab().save(QDir(folder).filePath(QStringLiteral("notes-menu-%1%2.png").arg(theme, shot.name)));
            fixture.window->grab().save(QDir(folder).filePath(QStringLiteral("window-notes-%1%2.png").arg(theme, shot.name)));
            menu->close();
            fixture.settle();

            // Outline with "+", a bold item and a new item in the inline editor (195:1131)
            pdf::PDFAppDocumentTab* outlineTab = fixture.open(pdftest::AppDocumentSamples::outline(fixture.folder()));
            pdf::PDFAppOutlinePanel* outline = H::outline(outlineTab);
            fixture.settle();
            outlineTab->getView()->getOutlineEditor()->setItemStyle({ 2 }, true, false);
            fixture.settle();
            H::selectItem(outline, { 1, 0 });
            outlineTab->getView()->getOutlineEditor()->createItemAtCurrentView();
            fixture.settle();
            QTest::qWait(300);
            fixture.settle();
            for (const qreal ratio : { 1.0, 2.0 })
            {
                pdf::PDFAppDocumentSidebar* sidebar = H::sidebar(outlineTab);
                QPixmap pixmap(sidebar->size() * ratio);
                pixmap.setDevicePixelRatio(ratio);
                sidebar->render(&pixmap);
                pixmap.save(QDir(folder).filePath(QStringLiteral("sidebar-outline-edit-%1%2@%3x.png").arg(theme, shot.name).arg(int(ratio))));
            }
            outlineTab->getView()->getOutlineEditor()->cancelCreation();
            fixture.settle();
            menu = outline->createContextMenu(outline->getItemIndex({ 1 }));
            menu->popup(outline->mapToGlobal(QPoint(100, 120)));
            fixture.settle();
            menu->grab().save(QDir(folder).filePath(QStringLiteral("outline-menu-%1%2.png").arg(theme, shot.name)));
            fixture.window->grab().save(QDir(folder).filePath(QStringLiteral("window-outline-%1%2.png").arg(theme, shot.name)));
            menu->close();
            fixture.settle();
        }
    }
    QApplication::setLayoutDirection(Qt::LeftToRight);
}

QTEST_MAIN(AppNotesOutlineTest)
#include "tst_appnotesoutlinetest.moc"
