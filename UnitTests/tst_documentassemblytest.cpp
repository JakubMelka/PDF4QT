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

// The assembly of Manage Pages (step 3b of phase 2, package M1; plan 4.1, 4.2, 4.3, 6, 8, RB2, RB4,
// RB7, RB8, RB10 b, QB3, QB5). The expectations come from the frames of Figma (387:12569 the
// summary and the parts, 387:15926 the dialogs of Regroup, 387:13288 the menus, 387:16980 the
// changed source), from the plan (4.1 the rules of the operations, 4.3 the file, 6 the limits, the
// old bugs B17-B53) and from ISO 32000-2 (the page objects of a document, 7.7.3.2) - never from a
// run of the code.

#include "pdfdocumentassembly.h"
#include "pdfdocumentassemblyfile.h"
#include "pdfdocumentmerger.h"
#include "pdfdocumentpageplan.h"
#include "pdfdocumenttoolparameter.h"
#include "pdfpagerangelist.h"

#include "mergetestpdf.h"

#include <QBuffer>
#include <QElapsedTimer>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest>

class DocumentAssemblyTest : public QObject
{
    Q_OBJECT

private slots:
    void valueIdentity();
    void valueParameter();
    void planItemsCarryCropAndClone();

    void figmaExampleSummaryAndParts();
    void groupAndUngroup();
    void regroupEveryPages();
    void regroupAtPageNumbers();
    void regroupOddEvenAndPairs();
    void regroupAlternate();
    void regroupByBookmarks();
    void regroupAtPlaceOfSelection();
    void sortNaturalStableInPositions();
    void cloneAndOriginal();
    void insertAfterSelection();
    void replaceIsOneOperation();
    void binRestoresPlaces();
    void checkpoints();
    void selectByPositionAndOrientation();
    void selectPageRangeAndSearch();
    void remapAfterDocumentChange();
    void checkpointOfDocumentHost();
    void changedSourceGoesToBin();
    void sameFileIsSameSource();
    void limitsOfOperations();

    void fileRoundTrip();
    void fileHostile();
    void fileHostile_data();
    void fileLimits();
    void filePageLimit();
    void fileAndCheckpointPageLimits();
    void fileChangedPageCount();
    void fileManyItemsInTime();

private:
    using Assembly = pdf::PDFDocumentAssembly;
    using Source = pdf::PDFDocumentAssembly::Source;
    using Page = pdf::PDFDocumentAssembly::Page;
    using Item = pdf::PDFDocumentAssembly::Item;
    using Edit = pdf::PDFDocumentAssembly::Edit;

    static Source createSource(const QString& id, const QString& name, int pageCount, QSizeF pageSize = QSizeF(595.0, 842.0), qint64 size = -1)
    {
        Source source;
        source.id = id;
        source.name = name;
        source.pageCount = pageCount;
        source.size = size;
        source.pageSizes.assign(size_t(pageCount), pageSize);
        return source;
    }

    static Page createPage(int input, pdf::PDFInteger pageIndex, pdf::PageRotation turn = pdf::PageRotation::None)
    {
        Page page;
        page.plan.input = input;
        page.plan.pageIndex = pageIndex;
        page.plan.turn = turn;
        return page;
    }

    static Page createBlankPage(QSizeF size)
    {
        Page page;
        page.plan.input = Assembly::BLANK_INPUT;
        page.plan.pageSize = size;
        return page;
    }

    static Item createItem(const QString& id, std::vector<Page> pages, const QString& name = QString(), bool isGroup = false)
    {
        Item item;
        item.id = id;
        item.pages = std::move(pages);
        item.name = name;
        item.isCreatedGroup = isGroup;
        return item;
    }

    /// The single pages "p1" ... of the pages of the input 0 in their order
    static std::vector<Item> createPageItems(int count, int input = 0)
    {
        std::vector<Item> items;
        for (int i = 0; i < count; ++i)
        {
            items.push_back(createItem(QStringLiteral("p%1").arg(i + 1), { createPage(input, i) }));
        }
        return items;
    }

    static Assembly createAssembly(std::vector<Source> sources, std::vector<Item> items, std::vector<pdf::PDFDocumentAssembly::RemovedItem> removed = { })
    {
        QString error;
        Assembly assembly = Assembly::create(std::move(sources), std::move(items), std::move(removed), &error);
        if (!error.isEmpty())
        {
            qWarning() << "The assembly of the test is not valid:" << error;
        }
        return assembly;
    }

    /// A page as the tiles name it: "A3", "B2@90", "A1+" (a clone), "*" (a blank page)
    static QString describePage(const Page& page)
    {
        if (page.plan.input < 0)
        {
            return QStringLiteral("*");
        }
        QString text = pdf::PDFDocumentPagePlan::getInputLetters(page.plan.input) + QString::number(page.plan.pageIndex + 1);
        switch (page.plan.turn)
        {
            case pdf::PageRotation::Rotate90:
                text += QStringLiteral("@90");
                break;
            case pdf::PageRotation::Rotate180:
                text += QStringLiteral("@180");
                break;
            case pdf::PageRotation::Rotate270:
                text += QStringLiteral("@270");
                break;
            default:
                break;
        }
        if (page.plan.isClone)
        {
            text += QLatin1Char('+');
        }
        return text;
    }

    /// The items: a single page "A3", a group "{A1,A2}" with its own name before it
    static QStringList describe(const std::vector<Item>& items)
    {
        QStringList result;
        for (const Item& item : items)
        {
            QStringList pages;
            for (const Page& page : item.pages)
            {
                pages << describePage(page);
            }
            result << (item.isGroup() ? item.name + QLatin1Char('{') + pages.join(QLatin1Char(',')) + QLatin1Char('}') : pages.join(QLatin1Char(',')));
        }
        return result;
    }

    static QStringList describe(const Assembly& assembly)
    {
        return describe(assembly.getItems());
    }

    /// The ids of the items at the indices
    static Assembly::Selection select(const Assembly& assembly, std::initializer_list<int> indices)
    {
        Assembly::Selection selection;
        for (const int index : indices)
        {
            selection.push_back(assembly.getItems()[size_t(index)].id);
        }
        return selection;
    }

    /// The indices of the selected items
    static QList<int> indicesOf(const Assembly& assembly, const Assembly::Selection& selection)
    {
        QList<int> indices;
        for (size_t i = 0; i < assembly.getItems().size(); ++i)
        {
            if (std::find(selection.cbegin(), selection.cend(), assembly.getItems()[i].id) != selection.cend())
            {
                indices << int(i);
            }
        }
        return indices;
    }

    /// The bytes of a PNG image of the size
    static QByteArray createPng(int width, int height)
    {
        QImage image(width, height, QImage::Format_RGB32);
        image.fill(Qt::white);
        QByteArray data;
        QBuffer buffer(&data);
        buffer.open(QBuffer::WriteOnly);
        image.save(&buffer, "PNG");
        return data;
    }

    /// The assembly of 387:12569 (Tender_2026): A Contract.pdf 6 pages, B Annex_A.pdf 24 pages, C
    /// signature_page.png; Introduction = A 1-5, A 6, B 1-3 (B 3 rotated 90°), B 4 "Site plan", C,
    /// a blank page, B 5 cropped, Appendix = B 6-9; the 15 pages B 10-24 in the bin
    static Assembly createFigmaExample(const QByteArray& png)
    {
        std::vector<Source> sources;
        sources.push_back(createSource(QStringLiteral("s1"), QStringLiteral("Contract.pdf"), 6, QSizeF(595.0, 842.0), 600000));
        sources.push_back(createSource(QStringLiteral("s2"), QStringLiteral("Annex_A.pdf"), 24, QSizeF(595.0, 842.0), 2400000));
        QString error;
        Source image = Assembly::createImageSource(png, QByteArray(), QStringLiteral("signature_page.png"), QString(), &error);
        image.id = QStringLiteral("s3");
        sources.push_back(image);

        std::vector<Item> items;
        items.push_back(createItem(QStringLiteral("p1"), { createPage(0, 0), createPage(0, 1), createPage(0, 2), createPage(0, 3), createPage(0, 4) }, QStringLiteral("Introduction"), true));
        items.push_back(createItem(QStringLiteral("p2"), { createPage(0, 5) }));
        items.push_back(createItem(QStringLiteral("p3"), { createPage(1, 0) }));
        items.push_back(createItem(QStringLiteral("p4"), { createPage(1, 1) }));
        items.push_back(createItem(QStringLiteral("p5"), { createPage(1, 2, pdf::PageRotation::Rotate90) }));
        items.push_back(createItem(QStringLiteral("p6"), { createPage(1, 3) }, QStringLiteral("Site plan")));
        items.push_back(createItem(QStringLiteral("p7"), { createPage(2, 0) }));
        items.push_back(createItem(QStringLiteral("p8"), { createBlankPage(QSizeF(595.0, 842.0)) }));
        Page cropped = createPage(1, 4);
        cropped.plan.cropMargins = QMarginsF(20.0, 20.0, 20.0, 20.0);
        items.push_back(createItem(QStringLiteral("p9"), { cropped }));
        items.push_back(createItem(QStringLiteral("p10"), { createPage(1, 5), createPage(1, 6), createPage(1, 7), createPage(1, 8) }, QStringLiteral("Appendix"), true));
        for (int i = 9; i < 24; ++i)
        {
            items.push_back(createItem(QStringLiteral("b%1").arg(i + 1), { createPage(1, i) }));
        }
        Assembly assembly = createAssembly(std::move(sources), std::move(items));

        Assembly::Selection removed;
        for (int i = 9; i < 24; ++i)
        {
            removed.push_back(QStringLiteral("b%1").arg(i + 1));
        }
        return Assembly::remove(assembly, removed, nullptr);
    }
};

void DocumentAssemblyTest::valueIdentity()
{
    // RB7: a copy shares the data (equal), two values made apart are different values (a new undo
    // step) with an equal content
    const pdf::PDFDocumentAssembly first;
    const pdf::PDFDocumentAssembly copy = first;
    const pdf::PDFDocumentAssembly other;
    QVERIFY(first == copy);
    QVERIFY(!(first == other));
    QVERIFY(first.isEqual(other));
    QCOMPARE(copy.getInstanceId(), first.getInstanceId());
    QVERIFY(other.getInstanceId() != first.getInstanceId());

    // In a QVariant (the parameter model compares QVariantMap): the same identity rule
    QVERIFY(QVariant::fromValue(first) == QVariant::fromValue(copy));
    QVERIFY(QVariant::fromValue(first) != QVariant::fromValue(other));

    // An operation gives a new value of the same instance (B18: the clipboard knows its assembly);
    // a refused one gives the same value
    const Assembly assembly = createAssembly({ createSource(QStringLiteral("s1"), QStringLiteral("a.pdf"), 3) }, createPageItems(3));
    Edit edit;
    const Assembly rotated = Assembly::rotate(assembly, select(assembly, { 0 }), pdf::PageRotation::Rotate90, &edit);
    QVERIFY(!(rotated == assembly));
    QCOMPARE(rotated.getInstanceId(), assembly.getInstanceId());
    const Assembly refused = Assembly::group(assembly, { }, &edit);
    QVERIFY(refused == assembly);
    QVERIFY(!edit.errorMessage.isEmpty());
}

void DocumentAssemblyTest::valueParameter()
{
    const pdf::PDFDocumentAssembly assembly;
    const pdf::PDFDocumentToolParameterDefinition definition = pdf::PDFDocumentToolParameterDefinition::createValue(&DocumentAssemblyTest::tr, QStringLiteral("assembly"),
                                                                                                                   QT_TR_NOOP("Assembly"), nullptr, QVariant::fromValue(pdf::PDFDocumentAssembly()));
    QString error;
    QVERIFY2(definition.isValid(&error), qPrintable(error));
    QCOMPARE(definition.getType(), pdf::PDFDocumentToolParameterType::Value);

    // Only the panels give it, it is never stored (RB7: the value belongs to one open tool)
    QVERIFY(definition.isPanelOnly());
    QVERIFY(!definition.isStored());
    QVERIFY(!definition.toStorageValue(QVariant::fromValue(assembly)).isValid());
    QVERIFY(!definition.fromStorageValue(QStringLiteral("{}")).has_value());

    // A value of its type stays the same value (its identity), another type is refused
    QVariant normalized;
    QVERIFY(definition.normalize(QVariant::fromValue(assembly), &normalized).isEmpty());
    QVERIFY(normalized == QVariant::fromValue(assembly));
    QVERIFY(!definition.normalize(QStringLiteral("A1-3"), &normalized).isEmpty());
    QVERIFY(!definition.normalize(QVariantMap(), &normalized).isEmpty());

    // An assistant never sees it (the schema of MCP)
    const QJsonObject schema = pdf::PDFDocumentToolParameters::toJsonSchema({ definition });
    QVERIFY(!schema.value(QStringLiteral("properties")).toObject().contains(QStringLiteral("assembly")));

    // A value parameter without a typed default is no valid definition
    const pdf::PDFDocumentToolParameterDefinition untyped = pdf::PDFDocumentToolParameterDefinition::createValue(&DocumentAssemblyTest::tr, QStringLiteral("assembly"),
                                                                                                                QT_TR_NOOP("Assembly"), nullptr, QVariant());
    QVERIFY(!untyped.isValid(&error));
}

void DocumentAssemblyTest::planItemsCarryCropAndClone()
{
    // RB10 b: the crop of a page of the plan goes to the merger; RB4: the clone flag of the plan goes
    // to the merger item (arrange keeps the original by it); a plain item keeps the defaults
    pdf::PDFDocumentPagePlan::Item cropped;
    cropped.input = 1;
    cropped.pageIndex = 4;
    cropped.cropMargins = QMarginsF(10.0, 20.0, 30.0, 40.0);
    cropped.isClone = true;

    pdf::PDFDocumentPagePlan::Item plain;
    plain.input = 0;
    plain.pageIndex = 2;

    const std::vector<pdf::PDFDocumentMerger::Item> items = pdf::PDFDocumentPagePlan::createMergerItems({ cropped, plain });
    QCOMPARE(items.size(), size_t(2));
    QCOMPARE(items[0].source, size_t(1));
    QCOMPARE(items[0].pageIndex, pdf::PDFInteger(4));
    QCOMPARE(items[0].cropMargins, QMarginsF(10.0, 20.0, 30.0, 40.0));
    QVERIFY(items[0].isClone);
    QCOMPARE(items[1].cropMargins, QMarginsF());
    QVERIFY(!items[1].isClone);
}

void DocumentAssemblyTest::figmaExampleSummaryAndParts()
{
    const QByteArray png = createPng(300, 400);
    const Assembly assembly = createFigmaExample(png);

    // 387:12569: "Sources · 3 files · 16 of 31 pages used", "Removed pages · 15 pages", "10 items ·
    // 17 pages", Result "Pages 17", "Outline 5 document parts" (K4, QB3)
    const Assembly::Summary summary = Assembly::getSummary(assembly);
    QCOMPARE(summary.sourceCount, 3);
    QCOMPARE(summary.sourcePageCount, qint64(31));
    QCOMPARE(summary.usedPageCount, qint64(16));
    QCOMPARE(summary.removedPageCount, qint64(15));
    QCOMPARE(summary.itemCount, 10);
    QCOMPARE(summary.resultPageCount, qint64(17));
    QCOMPARE(summary.partCount, 5);

    // KB5: a PDF page = the file size / its pages, an image = its bytes, a blank page = 1 KB
    QCOMPARE(summary.estimatedSize, qint64(6 * 100000 + 9 * 100000 + png.size() + 1024));

    // QB3 b: the groups with their names; outside of groups the runs of one file; the image and the
    // blank page belong to the part before them, so B 5 after them starts a run of its own; the
    // renamed single page "Site plan" starts no part (RB6)
    const Assembly::Plan plan = Assembly::toPlan(assembly);
    QCOMPARE(plan.items.size(), size_t(17));
    QCOMPARE(plan.parts.size(), size_t(5));
    const std::vector<size_t> firstItems = { 0, 5, 6, 12, 13 };
    const QStringList titles = { QStringLiteral("Introduction"), QStringLiteral("Contract.pdf, p. 6"), QString(u"Annex_A.pdf, p. 1\u20134"),
                                 QStringLiteral("Annex_A.pdf, p. 5"), QStringLiteral("Appendix") };
    for (size_t i = 0; i < plan.parts.size(); ++i)
    {
        QCOMPARE(plan.parts[i].firstItem, firstItems[i]);
        QCOMPARE(plan.parts[i].title, titles[qsizetype(i)]);
    }

    // The pages of the plan in the order of the canvas, each with its item (the files per group)
    QCOMPARE(plan.itemOfPage, std::vector<int>({ 0, 0, 0, 0, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 9, 9, 9 }));
    QCOMPARE(plan.items[8].turn, pdf::PageRotation::Rotate90);
    QCOMPARE(plan.items[11].input, Assembly::BLANK_INPUT);
    QCOMPARE(plan.items[12].cropMargins, QMarginsF(20.0, 20.0, 20.0, 20.0));

    // The names of the items ({group_name}): own names, else derived
    QCOMPARE(Assembly::getItemName(assembly, assembly.getItems()[0]), QStringLiteral("Introduction"));
    QCOMPARE(Assembly::getItemName(assembly, assembly.getItems()[1]), QStringLiteral("Contract.pdf, p. 6"));
    QCOMPARE(Assembly::getItemName(assembly, assembly.getItems()[5]), QStringLiteral("Site plan"));
    QCOMPARE(Assembly::getItemName(assembly, assembly.getItems()[6]), QStringLiteral("signature_page.png"));
    QCOMPARE(Assembly::getItemName(assembly, assembly.getItems()[7]), QStringLiteral("Blank page"));
}

void DocumentAssemblyTest::groupAndUngroup()
{
    const Assembly assembly = createAssembly({ createSource(QStringLiteral("s1"), QStringLiteral("a.pdf"), 5) }, createPageItems(5));

    // Group (Ctrl+G) at the place of the first selected item
    Edit edit;
    const Assembly grouped = Assembly::group(assembly, select(assembly, { 1, 3 }), &edit);
    QCOMPARE(describe(grouped), QStringList({ QStringLiteral("A1"), QStringLiteral("{A2,A4}"), QStringLiteral("A3"), QStringLiteral("A5") }));
    QCOMPARE(indicesOf(grouped, edit.selection), QList<int>({ 1 }));
    QVERIFY(grouped.getItems()[1].isCreatedGroup);

    // A group of one page is a group (QB3), a renamed single page is not (RB6)
    const Assembly single = Assembly::group(assembly, select(assembly, { 0 }), &edit);
    QVERIFY(single.getItems()[0].isGroup());
    const Assembly renamed = Assembly::rename(assembly, assembly.getItems()[0].id, QStringLiteral("  Site plan "), &edit);
    QCOMPARE(renamed.getItems()[0].name, QStringLiteral("Site plan"));
    QVERIFY(!renamed.getItems()[0].isGroup());
    QVERIFY(!Assembly::rename(assembly, assembly.getItems()[0].id, QString(1025, QLatin1Char('x')), &edit).getItems()[0].name.size());
    QVERIFY(!edit.errorMessage.isEmpty());

    // Ungroup keeps the pages in place, each an item, without the name of the group
    const Assembly named = Assembly::rename(grouped, grouped.getItems()[1].id, QStringLiteral("Annex"), &edit);
    const Assembly ungrouped = Assembly::ungroup(named, select(named, { 1 }), &edit);
    QCOMPARE(describe(ungrouped), QStringList({ QStringLiteral("A1"), QStringLiteral("A2"), QStringLiteral("A4"), QStringLiteral("A3"), QStringLiteral("A5") }));
    QCOMPARE(indicesOf(ungrouped, edit.selection), QList<int>({ 1, 2 }));
    QVERIFY(ungrouped.getItems()[1].name.isEmpty());

    // No group selected: refused, the value stays
    QVERIFY(Assembly::ungroup(assembly, select(assembly, { 0 }), &edit) == assembly);
    QVERIFY(!edit.errorMessage.isEmpty());
}

void DocumentAssemblyTest::regroupEveryPages()
{
    // 387:15926: "17 selected pages → 3 groups (6, 6 and 5 pages)"
    const Assembly assembly = createAssembly({ createSource(QStringLiteral("s1"), QStringLiteral("a.pdf"), 17) }, createPageItems(17));
    Edit edit;
    Assembly::RegroupSettings settings;
    settings.pageCount = 6;
    const Assembly regrouped = Assembly::regroup(assembly, { }, Assembly::RegroupMode::EveryPages, settings, &edit);
    QCOMPARE(describe(regrouped), QStringList({ QStringLiteral("{A1,A2,A3,A4,A5,A6}"), QStringLiteral("{A7,A8,A9,A10,A11,A12}"), QStringLiteral("{A13,A14,A15,A16,A17}") }));
    QCOMPARE(edit.selection.size(), size_t(3));

    // N out of 1 to the pages of the selection is refused
    settings.pageCount = 18;
    QVERIFY(Assembly::regroup(assembly, { }, Assembly::RegroupMode::EveryPages, settings, &edit) == assembly);
    QVERIFY(!edit.errorMessage.isEmpty());
    settings.pageCount = 0;
    QVERIFY(Assembly::regroup(assembly, { }, Assembly::RegroupMode::EveryPages, settings, &edit) == assembly);
}

void DocumentAssemblyTest::regroupAtPageNumbers()
{
    // 387:15926: "7, 13" over 17 pages → "3 groups (1–6, 7–12, 13–17)"; the positions are
    // positions in the selection (K27), not original page numbers
    std::vector<Item> items = createPageItems(17);
    std::reverse(items.begin(), items.end());
    const Assembly assembly = createAssembly({ createSource(QStringLiteral("s1"), QStringLiteral("a.pdf"), 17) }, std::move(items));
    Edit edit;
    Assembly::RegroupSettings settings;
    settings.positions = { 13, 7 };
    const Assembly regrouped = Assembly::regroup(assembly, { }, Assembly::RegroupMode::AtPageNumbers, settings, &edit);
    QCOMPARE(describe(regrouped), QStringList({ QStringLiteral("{A17,A16,A15,A14,A13,A12}"), QStringLiteral("{A11,A10,A9,A8,A7,A6}"), QStringLiteral("{A5,A4,A3,A2,A1}") }));

    // A position outside of the selection is refused
    settings.positions = { 7, 18 };
    QVERIFY(Assembly::regroup(assembly, { }, Assembly::RegroupMode::AtPageNumbers, settings, &edit) == assembly);
    QVERIFY(!edit.errorMessage.isEmpty());
}

void DocumentAssemblyTest::regroupOddEvenAndPairs()
{
    // Plan 8: 17 pages → odd and even 9 and 8 pages; page pairs 8 × 2 + 1
    const Assembly assembly = createAssembly({ createSource(QStringLiteral("s1"), QStringLiteral("a.pdf"), 17) }, createPageItems(17));
    Edit edit;
    const Assembly oddEven = Assembly::regroup(assembly, { }, Assembly::RegroupMode::OddAndEven, { }, &edit);
    QCOMPARE(oddEven.getItems().size(), size_t(2));
    QCOMPARE(describe(oddEven).front(), QStringLiteral("{A1,A3,A5,A7,A9,A11,A13,A15,A17}"));
    QCOMPARE(describe(oddEven).back(), QStringLiteral("{A2,A4,A6,A8,A10,A12,A14,A16}"));

    const Assembly pairs = Assembly::regroup(assembly, { }, Assembly::RegroupMode::PagePairs, { }, &edit);
    QCOMPARE(pairs.getItems().size(), size_t(9));
    for (size_t i = 0; i < 8; ++i)
    {
        QCOMPARE(pairs.getItems()[i].pages.size(), size_t(2));
    }
    QCOMPARE(describe(pairs).back(), QStringLiteral("{A17}"));

    // Reverse order: one group of the pages from the last one
    const Assembly reversed = Assembly::regroup(assembly, select(assembly, { 0, 1, 2 }), Assembly::RegroupMode::ReverseOrder, { }, &edit);
    QCOMPARE(describe(reversed).front(), QStringLiteral("{A3,A2,A1}"));
}

void DocumentAssemblyTest::regroupAlternate()
{
    // Plan 8: A 3 pages, B 2 → A1 B1 A2 B2 A3; second reversed → A1 B2 A2 B1 A3
    std::vector<Item> items = createPageItems(3, 0);
    for (int i = 0; i < 2; ++i)
    {
        items.push_back(createItem(QStringLiteral("q%1").arg(i + 1), { createPage(1, i) }));
    }
    const Assembly assembly = createAssembly({ createSource(QStringLiteral("s1"), QStringLiteral("a.pdf"), 3), createSource(QStringLiteral("s2"), QStringLiteral("b.pdf"), 2) }, std::move(items));
    Edit edit;
    QCOMPARE(describe(Assembly::regroup(assembly, { }, Assembly::RegroupMode::AlternateTwoFiles, { }, &edit)), QStringList({ QStringLiteral("{A1,B1,A2,B2,A3}") }));
    QCOMPARE(describe(Assembly::regroup(assembly, { }, Assembly::RegroupMode::AlternateSecondReversed, { }, &edit)), QStringList({ QStringLiteral("{A1,B2,A2,B1,A3}") }));

    // Pages of three files are no two files
    const Assembly three = Assembly::insertBlank(assembly, { }, &edit);
    QVERIFY(Assembly::regroup(three, { }, Assembly::RegroupMode::AlternateTwoFiles, { }, &edit) == three);
    QVERIFY(!edit.errorMessage.isEmpty());
}

void DocumentAssemblyTest::regroupByBookmarks()
{
    // Plan 8: bookmarks at the pages 1, 5, 9 of 12 → 1–4, 5–8, 9–12; a bookmark without a page
    // (PDFOutlineBreakPoints: pageIndex -1) is left out
    const Assembly assembly = createAssembly({ createSource(QStringLiteral("s1"), QStringLiteral("a.pdf"), 12) }, createPageItems(12));
    Assembly::RegroupSettings settings;
    settings.breakPages = { 4, -1, 0, 8 };
    settings.groupNames = { QStringLiteral("Chapter 2"), QStringLiteral("Lost"), QStringLiteral("Chapter 1"), QStringLiteral("Chapter 3") };
    Edit edit;
    const Assembly regrouped = Assembly::regroup(assembly, { }, Assembly::RegroupMode::ByBookmarks, settings, &edit);
    QCOMPARE(describe(regrouped), QStringList({ QStringLiteral("Chapter 1{A1,A2,A3,A4}"), QStringLiteral("Chapter 2{A5,A6,A7,A8}"), QStringLiteral("Chapter 3{A9,A10,A11,A12}") }));

    // Pages of another file or a blank page: the outline of one PDF does not apply
    const Assembly withBlank = Assembly::insertBlank(assembly, { }, &edit);
    QVERIFY(Assembly::regroup(withBlank, { }, Assembly::RegroupMode::ByBookmarks, settings, &edit) == withBlank);
    QVERIFY(!edit.errorMessage.isEmpty());
}

void DocumentAssemblyTest::regroupAtPlaceOfSelection()
{
    // B50: the new groups take the place of the selection, not the end of the list
    const Assembly assembly = createAssembly({ createSource(QStringLiteral("s1"), QStringLiteral("a.pdf"), 6) }, createPageItems(6));
    Edit edit;
    const Assembly regrouped = Assembly::regroup(assembly, select(assembly, { 1, 2, 4 }), Assembly::RegroupMode::PagePairs, { }, &edit);
    QCOMPARE(describe(regrouped), QStringList({ QStringLiteral("A1"), QStringLiteral("{A2,A3}"), QStringLiteral("{A5}"), QStringLiteral("A4"), QStringLiteral("A6") }));
    QCOMPARE(indicesOf(regrouped, edit.selection), QList<int>({ 1, 2 }));
}

void DocumentAssemblyTest::sortNaturalStableInPositions()
{
    // Plan 8: "file2" before "file10"; stable; only the positions of the selection; less than
    // two selected = all (387:13288)
    std::vector<Source> sources = { createSource(QStringLiteral("s1"), QStringLiteral("file10.pdf"), 1), createSource(QStringLiteral("s2"), QStringLiteral("file2.pdf"), 2),
                                    createSource(QStringLiteral("s3"), QStringLiteral("File1.pdf"), 1) };
    std::vector<Item> items = { createItem(QStringLiteral("p1"), { createPage(0, 0) }), createItem(QStringLiteral("p2"), { createPage(1, 1) }),
                                createItem(QStringLiteral("p3"), { createPage(2, 0) }), createItem(QStringLiteral("p4"), { createPage(1, 0) }) };
    const Assembly assembly = createAssembly(std::move(sources), std::move(items));
    Edit edit;
    QCOMPARE(describe(Assembly::sort(assembly, { }, Assembly::SortKey::FileName, &edit)), QStringList({ QStringLiteral("C1"), QStringLiteral("B2"), QStringLiteral("B1"), QStringLiteral("A1") }));
    QCOMPARE(describe(Assembly::sort(assembly, select(assembly, { 0 }), Assembly::SortKey::PageNumber, &edit)), QStringList({ QStringLiteral("A1"), QStringLiteral("C1"), QStringLiteral("B1"), QStringLiteral("B2") }));
    QCOMPARE(describe(Assembly::sort(assembly, { }, Assembly::SortKey::Source, &edit)), QStringList({ QStringLiteral("A1"), QStringLiteral("B2"), QStringLiteral("B1"), QStringLiteral("C1") }));

    // Only the selected positions 1 and 3 are permuted
    QCOMPARE(describe(Assembly::sort(assembly, select(assembly, { 1, 3 }), Assembly::SortKey::Reverse, &edit)), QStringList({ QStringLiteral("A1"), QStringLiteral("B1"), QStringLiteral("C1"), QStringLiteral("B2") }));

    // Type: PDF pages, images, blank pages
    const Assembly withBlank = Assembly::insertBlank(assembly, select(assembly, { 0 }), &edit);
    QCOMPARE(describe(Assembly::sort(withBlank, { }, Assembly::SortKey::Type, &edit)).back(), QStringLiteral("*"));
}

void DocumentAssemblyTest::cloneAndOriginal()
{
    const pdf::PDFDocument document = mergetest::MergeTestPdf::createPages("A", 3).read();
    const Assembly assembly = Assembly::createForDocument(&document, 7, QStringLiteral("a.pdf"));
    QVERIFY(assembly.getSources().front().isTarget);
    QCOMPARE(describe(assembly), QStringList({ QStringLiteral("A1"), QStringLiteral("A2"), QStringLiteral("A3") }));

    // Clone (Ctrl+L) after the selection, carrying the clone flag (RB4)
    Edit edit;
    const Assembly cloned = Assembly::clone(assembly, select(assembly, { 1 }), &edit);
    QCOMPARE(describe(cloned), QStringList({ QStringLiteral("A1"), QStringLiteral("A2"), QStringLiteral("A2+"), QStringLiteral("A3") }));
    QCOMPARE(indicesOf(cloned, edit.selection), QList<int>({ 2 }));

    // A clone moved before its original stays the clone: the original keeps the page object
    const Assembly moved = Assembly::move(cloned, select(cloned, { 2 }), cloned.getItems()[0].id, &edit);
    QCOMPARE(describe(moved), QStringList({ QStringLiteral("A2+"), QStringLiteral("A1"), QStringLiteral("A2"), QStringLiteral("A3") }));

    // The original in the bin: the clone in the items becomes the original (one page object)
    const Assembly removed = Assembly::remove(moved, select(moved, { 2 }), &edit);
    QCOMPARE(describe(removed), QStringList({ QStringLiteral("A2"), QStringLiteral("A1"), QStringLiteral("A3") }));

    // A page of the target added again: a clone, never a second original
    const Assembly added = Assembly::insertPages(assembly, { }, assembly.getSources().front().id, { 0 }, &edit);
    QCOMPARE(describe(added), QStringList({ QStringLiteral("A1"), QStringLiteral("A2"), QStringLiteral("A3"), QStringLiteral("A1+") }));

    // The page objects of the version (RB2): every page of the target in its order
    const std::vector<pdf::PDFObjectReference> targetPages = Assembly::getTargetPages(removed);
    QCOMPARE(targetPages.size(), size_t(3));
    for (size_t i = 0; i < 3; ++i)
    {
        QCOMPARE(targetPages[i], document.getCatalog()->getPage(i)->getPageReference());
    }
    QCOMPARE(removed.getItems()[0].pages.front().targetVersion, quint64(7));

    // A clone of a page of another file carries the flag too (the item made by Clone, RB4)
    const Assembly plain = createAssembly({ createSource(QStringLiteral("s1"), QStringLiteral("b.pdf"), 2) }, createPageItems(2));
    QCOMPARE(describe(Assembly::clone(plain, select(plain, { 0 }), &edit)), QStringList({ QStringLiteral("A1"), QStringLiteral("A1+"), QStringLiteral("A2") }));
}

void DocumentAssemblyTest::insertAfterSelection()
{
    const Assembly assembly = createAssembly({ createSource(QStringLiteral("s1"), QStringLiteral("a.pdf"), 4), createSource(QStringLiteral("s2"), QStringLiteral("b.pdf"), 3) }, createPageItems(4));

    // B19: after the selection (not before the last item), without a selection at the end
    Edit edit;
    const Assembly inserted = Assembly::insertPages(assembly, select(assembly, { 0, 1 }), QStringLiteral("s2"), { 2, 0, 2 }, &edit);
    QCOMPARE(describe(inserted), QStringList({ QStringLiteral("A1"), QStringLiteral("A2"), QStringLiteral("B3"), QStringLiteral("B1"), QStringLiteral("B3"), QStringLiteral("A3"), QStringLiteral("A4") }));
    QCOMPARE(indicesOf(inserted, edit.selection), QList<int>({ 2, 3, 4 }));
    QCOMPARE(describe(Assembly::insertPages(assembly, { }, QStringLiteral("s2"), { 1 }, &edit)).back(), QStringLiteral("B2"));

    // A page the file does not have is refused
    QVERIFY(Assembly::insertPages(assembly, { }, QStringLiteral("s2"), { 3 }, &edit) == assembly);
    QVERIFY(!edit.errorMessage.isEmpty());

    // Paste: copies of items after the selection with new ids
    const Assembly pasted = Assembly::insertItems(assembly, select(assembly, { 2 }), { assembly.getItems()[0], assembly.getItems()[1] }, &edit);
    QCOMPARE(describe(pasted), QStringList({ QStringLiteral("A1"), QStringLiteral("A2"), QStringLiteral("A3"), QStringLiteral("A1"), QStringLiteral("A2"), QStringLiteral("A4") }));
    QVERIFY(pasted.getItems()[3].id != assembly.getItems()[0].id);
    QCOMPARE(indicesOf(pasted, edit.selection), QList<int>({ 3, 4 }));

    // A blank page has the size and the orientation of the page before it as it is shown, A4 in
    // an empty assembly
    const Assembly turned = Assembly::rotate(assembly, select(assembly, { 1 }), pdf::PageRotation::Rotate90, &edit);
    const Assembly blank = Assembly::insertBlank(turned, select(turned, { 1 }), &edit);
    QCOMPARE(blank.getItems()[2].pages.front().plan.pageSize, QSizeF(842.0, 595.0));
    const Assembly empty = Assembly::insertBlank(Assembly(), { }, &edit);
    QCOMPARE(empty.getItems().front().pages.front().plan.pageSize.toSize(), QSize(595, 842));
}

void DocumentAssemblyTest::replaceIsOneOperation()
{
    const Assembly assembly = createAssembly({ createSource(QStringLiteral("s1"), QStringLiteral("a.pdf"), 4), createSource(QStringLiteral("s2"), QStringLiteral("b.pdf"), 2) }, createPageItems(4));

    // B20: a replacement, which cannot be done, changes nothing (the selection stays)
    Edit edit;
    QVERIFY(Assembly::replace(assembly, select(assembly, { 1, 2 }), QStringLiteral("s2"), { 5 }, &edit) == assembly);
    QVERIFY(!edit.errorMessage.isEmpty());
    QVERIFY(Assembly::replace(assembly, select(assembly, { 1, 2 }), QStringLiteral("missing"), { 0 }, &edit) == assembly);

    // One operation: the new pages at the place of the selection, the replaced ones in the bin
    const Assembly replaced = Assembly::replace(assembly, select(assembly, { 1, 2 }), QStringLiteral("s2"), { 1, 0 }, &edit);
    QCOMPARE(describe(replaced), QStringList({ QStringLiteral("A1"), QStringLiteral("B2"), QStringLiteral("B1"), QStringLiteral("A4") }));
    QCOMPARE(describe(Assembly::restoreAll(replaced, &edit)), QStringList({ QStringLiteral("A1"), QStringLiteral("A2"), QStringLiteral("A3"), QStringLiteral("B2"), QStringLiteral("B1"), QStringLiteral("A4") }));
}

void DocumentAssemblyTest::binRestoresPlaces()
{
    const Assembly assembly = createAssembly({ createSource(QStringLiteral("s1"), QStringLiteral("a.pdf"), 10) }, createPageItems(10));
    const QStringList original = describe(assembly);
    Edit edit;

    // Plan 8, B21: remove 3 and 7, restore → their places
    const Assembly removed = Assembly::remove(assembly, select(assembly, { 2, 6 }), &edit);
    QCOMPARE(removed.getItems().size(), size_t(8));
    QCOMPARE(removed.getRemovedItems().size(), size_t(2));
    QCOMPARE(describe(Assembly::restoreAll(removed, &edit)), original);

    // Removed one by one in any order, also neighbors and the first items
    Assembly current = assembly;
    for (const int index : { 6, 2, 0, 0, 3 })
    {
        current = Assembly::remove(current, select(current, { index }), &edit);
    }
    QCOMPARE(current.getItems().size(), size_t(5));
    QCOMPARE(describe(Assembly::restoreAll(current, &edit)), original);
    QCOMPARE(edit.selection.size(), size_t(5));

    // One item restored, whose neighbor is still in the bin: its index
    const Assembly two = Assembly::remove(assembly, select(assembly, { 3, 4 }), &edit);
    const Assembly one = Assembly::restore(two, { two.getRemovedItems()[1].item.id }, &edit);
    QCOMPARE(describe(one), QStringList({ QStringLiteral("A1"), QStringLiteral("A2"), QStringLiteral("A3"), QStringLiteral("A5"), QStringLiteral("A6"), QStringLiteral("A7"), QStringLiteral("A8"), QStringLiteral("A9"), QStringLiteral("A10") }));
    QCOMPARE(describe(Assembly::restoreAll(one, &edit)), original);

    // Restore of nothing is refused
    QVERIFY(Assembly::restoreAll(assembly, &edit) == assembly);
    QVERIFY(!edit.errorMessage.isEmpty());
}

void DocumentAssemblyTest::checkpoints()
{
    const Assembly assembly = createAssembly({ createSource(QStringLiteral("s1"), QStringLiteral("a.pdf"), 3), createSource(QStringLiteral("s2"), QStringLiteral("b.pdf"), 2) }, createPageItems(3));
    Edit edit;
    const Assembly withB = Assembly::insertPages(assembly, { }, QStringLiteral("s2"), { 0, 1 }, &edit);

    // B23: two checkpoints of the same name are two checkpoints
    const Assembly::Checkpoint first = Assembly::createCheckpoint(withB, QStringLiteral("Before grouping"));
    const Assembly grouped = Assembly::group(withB, select(withB, { 0, 1 }), &edit);
    const Assembly::Checkpoint second = Assembly::createCheckpoint(grouped, QStringLiteral("Before grouping"));
    QVERIFY(first.id != second.id);
    QCOMPARE(first.name, second.name);

    // B51: the source B removed later (parked) - the checkpoint still restores it; the bin stays
    const Assembly withoutB = Assembly::removeSource(grouped, QStringLiteral("s2"), &edit);
    QVERIFY(withoutB.getSources()[1].isParked);
    QCOMPARE(describe(withoutB), QStringList({ QStringLiteral("{A1,A2}"), QStringLiteral("A3") }));
    QCOMPARE(withoutB.getRemovedItems().size(), size_t(2));

    const Assembly restored = Assembly::restoreCheckpoint(withoutB, first, &edit);
    QVERIFY(edit.errorMessage.isEmpty());
    QCOMPARE(describe(restored), describe(withB));
    QVERIFY(!restored.getSources()[1].isParked);
    QCOMPARE(restored.getRemovedItems().size(), size_t(2));
    QCOMPARE(describe(Assembly::restoreCheckpoint(restored, second, &edit)), describe(grouped));

    // B22: the restore is a new value (an undo step of the caller), the value before it stays
    QVERIFY(!(restored == withoutB));
    QCOMPARE(describe(withoutB), QStringList({ QStringLiteral("{A1,A2}"), QStringLiteral("A3") }));
}

void DocumentAssemblyTest::selectByPositionAndOrientation()
{
    // KB2, QB5, B17: Even = the items at the positions 2 and 4 of the assembly
    const Assembly assembly = createAssembly({ createSource(QStringLiteral("s1"), QStringLiteral("a.pdf"), 5), createSource(QStringLiteral("s2"), QStringLiteral("square.pdf"), 1, QSizeF(500.0, 500.0)) },
                                             createPageItems(5));
    QCOMPARE(indicesOf(assembly, Assembly::select(assembly, { }, Assembly::SelectRule::Even, { })), QList<int>({ 1, 3 }));
    QCOMPARE(indicesOf(assembly, Assembly::select(assembly, { }, Assembly::SelectRule::Odd, { })), QList<int>({ 0, 2, 4 }));

    // Positions, not the original pages: the reversed order selects A4 and A2 by their places
    Edit edit;
    const Assembly reversed = Assembly::sort(assembly, { }, Assembly::SortKey::Reverse, &edit);
    QCOMPARE(indicesOf(reversed, Assembly::select(reversed, { }, Assembly::SelectRule::Even, { })), QList<int>({ 1, 3 }));
    QCOMPARE(describe({ reversed.getItems()[1], reversed.getItems()[3] }), QStringList({ QStringLiteral("A4"), QStringLiteral("A2") }));

    // A group of two pages has an odd and an even position: neither selects it
    const Assembly grouped = Assembly::group(assembly, select(assembly, { 0, 1 }), &edit);
    QCOMPARE(indicesOf(grouped, Assembly::select(grouped, { }, Assembly::SelectRule::Odd, { })), QList<int>({ 1, 3 }));
    QCOMPARE(indicesOf(grouped, Assembly::select(grouped, { }, Assembly::SelectRule::Even, { })), QList<int>({ 2 }));

    // B31: a portrait page turned by 90° is landscape; a square page is both
    const Assembly turned = Assembly::rotate(assembly, select(assembly, { 2 }), pdf::PageRotation::Rotate90, &edit);
    const Assembly withSquare = Assembly::insertPages(turned, { }, QStringLiteral("s2"), { 0 }, &edit);
    QCOMPARE(indicesOf(withSquare, Assembly::select(withSquare, { }, Assembly::SelectRule::Landscape, { })), QList<int>({ 2, 5 }));
    QCOMPARE(indicesOf(withSquare, Assembly::select(withSquare, { }, Assembly::SelectRule::Portrait, { })), QList<int>({ 0, 1, 3, 4, 5 }));

    // A crop changes the orientation too (the visible area): 595 × 842 less 300 on the sides
    const Assembly cropped = Assembly::crop(assembly, select(assembly, { 0 }), QMarginsF(0.0, 300.0, 0.0, 0.0), false, &edit);
    QCOMPARE(indicesOf(cropped, Assembly::select(cropped, { }, Assembly::SelectRule::Landscape, { })), QList<int>({ 0 }));
    QCOMPARE(Assembly::getPageSize(cropped, cropped.getItems()[0].pages.front()), QSizeF(595.0, 542.0));

    // Margins, which leave nothing of the page, are refused
    QVERIFY(Assembly::crop(assembly, select(assembly, { 0 }), QMarginsF(300.0, 0.0, 300.0, 0.0), false, &edit) == assembly);
    QVERIFY(!edit.errorMessage.isEmpty());

    // All, None, Invert
    QCOMPARE(indicesOf(assembly, Assembly::select(assembly, { }, Assembly::SelectRule::All, { })), QList<int>({ 0, 1, 2, 3, 4 }));
    QVERIFY(Assembly::select(assembly, select(assembly, { 1 }), Assembly::SelectRule::None, { }).empty());
    QCOMPARE(indicesOf(assembly, Assembly::select(assembly, select(assembly, { 1, 2 }), Assembly::SelectRule::Invert, { })), QList<int>({ 0, 3, 4 }));
}

void DocumentAssemblyTest::selectPageRangeAndSearch()
{
    std::vector<Item> items = createPageItems(4, 0);
    items.push_back(createItem(QStringLiteral("q1"), { createPage(1, 2) }));
    const Assembly assembly = createAssembly({ createSource(QStringLiteral("s1"), QStringLiteral("Contract.pdf"), 4), createSource(QStringLiteral("s2"), QStringLiteral("Annex_A.pdf"), 3) }, std::move(items));

    // 387:15926 Select page range: positions in the assembly, or the original pages of one file
    Assembly::SelectSettings settings;
    settings.pageRange = QStringLiteral("2-3, 5");
    QCOMPARE(indicesOf(assembly, Assembly::select(assembly, { }, Assembly::SelectRule::PageRange, settings)), QList<int>({ 1, 2, 4 }));
    settings.pageRange = QStringLiteral("3");
    settings.sourceId = QStringLiteral("s2");
    QCOMPARE(indicesOf(assembly, Assembly::select(assembly, { }, Assembly::SelectRule::PageRange, settings)), QList<int>({ 4 }));
    settings.isAdded = true;
    QCOMPARE(indicesOf(assembly, Assembly::select(assembly, select(assembly, { 0 }), Assembly::SelectRule::PageRange, settings)), QList<int>({ 0, 4 }));

    // 387:15594: the words of the search (grouped, ungrouped, image, blank, a name)
    Edit edit;
    const Assembly grouped = Assembly::insertBlank(Assembly::group(assembly, select(assembly, { 0, 1 }), &edit), { }, &edit);
    Assembly::SelectSettings search;
    search.searchText = QStringLiteral("grouped");
    QCOMPARE(indicesOf(grouped, Assembly::select(grouped, { }, Assembly::SelectRule::SearchResults, search)), QList<int>({ 0 }));
    search.searchText = QStringLiteral("blank");
    QCOMPARE(indicesOf(grouped, Assembly::select(grouped, { }, Assembly::SelectRule::SearchResults, search)), QList<int>({ 4 }));
    search.searchText = QStringLiteral("annex ungrouped");
    QCOMPARE(indicesOf(grouped, Assembly::select(grouped, { }, Assembly::SelectRule::SearchResults, search)), QList<int>({ 3 }));
}

void DocumentAssemblyTest::remapAfterDocumentChange()
{
    // RB2: version 1 has the pages 3 4 5 6 7 (objects of createPages); version 2 lost the page 4,
    // got the page 50 and keeps the others in their order (7.7.3.2: the page objects stay)
    const pdf::PDFDocument version1 = mergetest::MergeTestPdf::createPages("V", 5).read();
    mergetest::MergeTestPdf changed = mergetest::MergeTestPdf::createPages("V", 5);
    changed.set(2, "<< /Type /Pages /Kids [3 0 R 5 0 R 50 0 R 6 0 R 7 0 R] /Count 5 /MediaBox [0 0 600 800] /Resources << >> >>");
    changed.set(50, "<< /Type /Page /Parent 2 0 R /Contents 150 0 R >>");
    changed.set(150, mergetest::MergeTestPdf::stream("% NEW\n0 0 m 5 5 l S"));
    const pdf::PDFDocument version2 = changed.read();
    QCOMPARE(version2.getCatalog()->getPageCount(), size_t(5));

    Edit edit;
    const Assembly assembly = Assembly::createForDocument(&version1, 1, QStringLiteral("doc.pdf"));
    const Assembly moved = Assembly::move(assembly, select(assembly, { 4 }), assembly.getItems()[0].id, &edit);      // A5 A1 A2 A3 A4
    const Assembly binned = Assembly::remove(moved, select(moved, { 3 }), &edit);                                       // A5 A1 A2 A4, bin A3
    QCOMPARE(describe(binned), QStringList({ QStringLiteral("A5"), QStringLiteral("A1"), QStringLiteral("A2"), QStringLiteral("A4") }));

    pdf::PDFInteger added = 0;
    const Assembly mapped = Assembly::mapToDocument(binned, &version2, 2, &added);

    // The pages follow their objects: object 7 is now page 5, object 3 page 1, object 4 (A2) is
    // gone, object 6 page 4; the new page 50 (page 3) at the end; the bin keeps object 5 (page 2)
    QCOMPARE(added, pdf::PDFInteger(1));
    QCOMPARE(describe(mapped), QStringList({ QStringLiteral("A5"), QStringLiteral("A1"), QStringLiteral("A4"), QStringLiteral("A3") }));
    QCOMPARE(mapped.getItems()[3].pages.front().targetPage, pdf::PDFObjectReference(50, 0));
    QCOMPARE(mapped.getRemovedItems().size(), size_t(1));
    QCOMPARE(describePage(mapped.getRemovedItems().front().item.pages.front()), QStringLiteral("A2"));
    QCOMPARE(mapped.getRemovedItems().front().item.pages.front().targetPage, pdf::PDFObjectReference(5, 0));
    for (const Item& item : mapped.getItems())
    {
        QCOMPARE(item.pages.front().targetVersion, quint64(2));
    }

    // The plan of the new version names its pages in their order (arrange accepts only it)
    const std::vector<pdf::PDFObjectReference> targetPages = Assembly::getTargetPages(mapped);
    QCOMPARE(targetPages, std::vector<pdf::PDFObjectReference>({ { 3, 0 }, { 5, 0 }, { 50, 0 }, { 6, 0 }, { 7, 0 } }));

    // The same version again: nothing changes, the same value
    QVERIFY(Assembly::mapToDocument(mapped, &version2, 2, &added) == mapped);
    QCOMPARE(added, pdf::PDFInteger(0));
}

void DocumentAssemblyTest::checkpointOfDocumentHost()
{
    // RB2 with a checkpoint of the document host: arrange accepts only a plan, which names every
    // page of this version of the document (getTargetPages equal to its page objects), and the
    // pages of a checkpoint are the page objects it was saved with
    const pdf::PDFDocument version1 = mergetest::MergeTestPdf::createPages("V", 3).read();
    const std::vector<pdf::PDFObjectReference> pages1 = { { 3, 0 }, { 4, 0 }, { 5, 0 } };
    Edit edit;
    const Assembly assembly = Assembly::createForDocument(&version1, 1, QStringLiteral("doc.pdf"));

    // A3 is in the bin at the checkpoint, the user restores it later: the checkpoint has no A3 -
    // after its restore A3 is in the bin again (Removed pages lists it), never unknown
    const Assembly binned = Assembly::remove(assembly, select(assembly, { 2 }), &edit);
    const Assembly::Checkpoint checkpoint = Assembly::createCheckpoint(binned, QStringLiteral("Two pages"));
    const Assembly restoredA3 = Assembly::restoreAll(binned, &edit);
    QCOMPARE(describe(restoredA3), QStringList({ QStringLiteral("A1"), QStringLiteral("A2"), QStringLiteral("A3") }));
    const Assembly restored = Assembly::restoreCheckpoint(restoredA3, checkpoint, &edit);
    QVERIFY2(edit.errorMessage.isEmpty(), qPrintable(edit.errorMessage));
    QCOMPARE(describe(restored), QStringList({ QStringLiteral("A1"), QStringLiteral("A2") }));
    QCOMPARE(restored.getRemovedItems().size(), size_t(1));
    QCOMPARE(restored.getRemovedItems().front().item.pages.front().targetPage, pdf::PDFObjectReference(5, 0));
    QCOMPARE(Assembly::getTargetPages(restored), pages1);

    // Version 2 got the page 50 at the beginning (7.7.3.2: the page objects stay): the checkpoint of
    // version 1 names object 3 as page 1, which is page 2 now - its pages follow their objects
    mergetest::MergeTestPdf changed = mergetest::MergeTestPdf::createPages("V", 3);
    changed.set(2, "<< /Type /Pages /Kids [50 0 R 3 0 R 4 0 R 5 0 R] /Count 4 /MediaBox [0 0 600 800] /Resources << >> >>");
    changed.set(50, "<< /Type /Page /Parent 2 0 R /Contents 150 0 R >>");
    changed.set(150, mergetest::MergeTestPdf::stream("% NEW\n0 0 m 5 5 l S"));
    const pdf::PDFDocument version2 = changed.read();
    const Assembly mapped = Assembly::mapToDocument(restoredA3, &version2, 2, nullptr);
    QCOMPARE(describe(mapped), QStringList({ QStringLiteral("A2"), QStringLiteral("A3"), QStringLiteral("A4"), QStringLiteral("A1") }));

    const Assembly restored2 = Assembly::restoreCheckpoint(mapped, checkpoint, &edit);
    QVERIFY2(edit.errorMessage.isEmpty(), qPrintable(edit.errorMessage));
    QCOMPARE(describe(restored2), QStringList({ QStringLiteral("A2"), QStringLiteral("A3") }));
    QCOMPARE(restored2.getItems()[0].pages.front().targetPage, pdf::PDFObjectReference(3, 0));
    QCOMPARE(restored2.getItems()[0].pages.front().targetVersion, quint64(2));
    QCOMPARE(Assembly::getTargetPages(restored2), std::vector<pdf::PDFObjectReference>({ { 50, 0 }, { 3, 0 }, { 4, 0 }, { 5, 0 } }));
    QCOMPARE(restored2.getRemovedItems().size(), size_t(2));

    // An assembly of files (no document host) restores the checkpoint as it is
    const Assembly files = createAssembly({ createSource(QStringLiteral("s1"), QStringLiteral("a.pdf"), 3) }, createPageItems(3));
    const Assembly::Checkpoint filesCheckpoint = Assembly::createCheckpoint(Assembly::remove(files, select(files, { 2 }), &edit), QStringLiteral("Two"));
    const Assembly filesRestored = Assembly::restoreCheckpoint(files, filesCheckpoint, &edit);
    QCOMPARE(describe(filesRestored), QStringList({ QStringLiteral("A1"), QStringLiteral("A2") }));
    QVERIFY(filesRestored.getRemovedItems().empty());
}

void DocumentAssemblyTest::changedSourceGoesToBin()
{
    // 387:16980: "Changed … now 5 pages instead of 6 · Page 6 is no longer available" - the page
    // goes to Removed pages (also from a group), the rest stays
    std::vector<Item> items = createPageItems(4);
    items.push_back(createItem(QStringLiteral("g"), { createPage(0, 4), createPage(0, 5) }, QStringLiteral("End"), true));
    const Assembly assembly = createAssembly({ createSource(QStringLiteral("s1"), QStringLiteral("Contract.pdf"), 6) }, std::move(items));

    Source changed = createSource(QStringLiteral("s1"), QStringLiteral("Contract.pdf"), 5);
    Edit edit;
    const Assembly updated = Assembly::updateSource(assembly, changed, &edit);
    QCOMPARE(describe(updated), QStringList({ QStringLiteral("A1"), QStringLiteral("A2"), QStringLiteral("A3"), QStringLiteral("A4"), QStringLiteral("End{A5}") }));
    QCOMPARE(updated.getRemovedItems().size(), size_t(1));
    QCOMPARE(describePage(updated.getRemovedItems().front().item.pages.front()), QStringLiteral("A6"));
    QCOMPARE(updated.getSources().front().pageCount, pdf::PDFInteger(5));

    // The page is not in the file: it cannot go back
    QVERIFY(Assembly::restoreAll(updated, &edit) == updated);
    QVERIFY(!edit.errorMessage.isEmpty());

    // Open Without Them: every page of a missing source in the bin, the source parked
    const Assembly without = Assembly::removeSource(assembly, QStringLiteral("s1"), &edit);
    QVERIFY(without.getItems().empty());
    QCOMPARE(Assembly::getSummary(without).removedPageCount, qint64(6));
    QCOMPARE(Assembly::getSummary(without).sourceCount, 0);
}

void DocumentAssemblyTest::sameFileIsSameSource()
{
    // B52: the same file is the same source - its pages are added again, never refused
    Source source = createSource(QString(), QStringLiteral("a.pdf"), 3);
    source.path = QStringLiteral("C:/Docs/a.pdf");
    QString firstId;
    QString secondId;
    QString error;
    const Assembly first = Assembly::addSource(Assembly(), source, &firstId, &error);
    QVERIFY(!firstId.isEmpty());
    source.path = QStringLiteral("C:/Docs/./a.pdf");
    const Assembly second = Assembly::addSource(first, source, &secondId, &error);
    QCOMPARE(secondId, firstId);
    QCOMPARE(second.getSources().size(), size_t(1));

    // A given unique id is kept (the id of the input of the panel), a used one is replaced
    Source other = createSource(QStringLiteral("i7"), QStringLiteral("b.pdf"), 1);
    const Assembly withOther = Assembly::addSource(second, other, &secondId, &error);
    QCOMPARE(secondId, QStringLiteral("i7"));
    const Assembly withThird = Assembly::addSource(withOther, other, &secondId, &error);
    QVERIFY(secondId != QStringLiteral("i7"));
    QCOMPARE(withThird.getSources().size(), size_t(3));

    // An image needs its bytes
    Source image = createSource(QString(), QStringLiteral("c.png"), 1);
    image.kind = Assembly::SourceKind::Image;
    QVERIFY(Assembly::addSource(withThird, image, &secondId, &error) == withThird);
    QVERIFY(!error.isEmpty());
}

void DocumentAssemblyTest::limitsOfOperations()
{
    // Owner 2026-11-12 (plan of step 3b, 13 point 3): an assembly has at most 65 536 pages, the bin
    // included, in its own tab and in the tab of a document; Merge, Insert and the lists of pages
    // keep 1 000 000. Every item has a page, so the pages bound the items (MAXIMUM_ITEM_COUNT).
    constexpr int PAGE_LIMIT = 65536;
    QCOMPARE(Assembly::MAXIMUM_PAGE_COUNT, qint64(PAGE_LIMIT));
    QCOMPARE(pdf::PDFDocumentMerger::MAXIMUM_PAGE_COUNT, qint64(1000000));
    QCOMPARE(pdf::PDFPageRangeList::MAXIMUM_PAGE_COUNT, pdf::PDFInteger(1000000));

    // 65 536 pages: accepted; a page more by a clone, an insert or create is refused with the text
    const Assembly assembly = createAssembly({ createSource(QStringLiteral("s1"), QStringLiteral("a.pdf"), PAGE_LIMIT, QSizeF()) }, createPageItems(PAGE_LIMIT));
    QCOMPARE(assembly.getItems().size(), size_t(PAGE_LIMIT));
    QString error;
    QVERIFY(Assembly::checkLimits(assembly, &error));
    QVERIFY(error.isEmpty());
    Edit edit;
    QVERIFY(Assembly::clone(assembly, select(assembly, { 0 }), &edit) == assembly);
    QVERIFY2(edit.errorMessage.startsWith(QStringLiteral("An assembly has at most ")), qPrintable(edit.errorMessage));
    QVERIFY(Assembly::insertPages(assembly, { }, QStringLiteral("s1"), { 0 }, &edit) == assembly);
    QVERIFY2(edit.errorMessage.startsWith(QStringLiteral("An assembly has at most ")), qPrintable(edit.errorMessage));
    std::vector<Item> overLimit = createPageItems(PAGE_LIMIT);
    overLimit.push_back(createItem(QStringLiteral("blank"), { createBlankPage(QSizeF(612.0, 792.0)) }));
    QVERIFY(Assembly::create({ createSource(QStringLiteral("s1"), QStringLiteral("a.pdf"), PAGE_LIMIT, QSizeF()) }, overLimit, { }, &error).isEmpty());
    QVERIFY2(error.startsWith(QStringLiteral("An assembly has at most ")), qPrintable(error));

    // The bin counts: a removed page keeps its place in the limit
    const Assembly removed = Assembly::remove(assembly, select(assembly, { 0 }), &edit);
    QCOMPARE(removed.getItems().size(), size_t(PAGE_LIMIT - 1));
    QVERIFY(Assembly::insertPages(removed, { }, QStringLiteral("s1"), { 0 }, &edit) == removed);
    QVERIFY2(edit.errorMessage.startsWith(QStringLiteral("An assembly has at most ")), qPrintable(edit.errorMessage));

    // A source and a document of 65 537 pages are refused, of 65 536 accepted
    QString sourceId;
    QVERIFY(Assembly::addSource(Assembly(), createSource(QString(), QStringLiteral("big.pdf"), PAGE_LIMIT + 1, QSizeF()), &sourceId, &error).isEmpty());
    QVERIFY(sourceId.isEmpty());
    QVERIFY2(error.startsWith(QStringLiteral("big.pdf has more than ")), qPrintable(error));
    QVERIFY(!Assembly::addSource(Assembly(), createSource(QString(), QStringLiteral("big.pdf"), PAGE_LIMIT, QSizeF()), &sourceId, &error).getSources().empty());
    QVERIFY(!sourceId.isEmpty());
    QVERIFY(Assembly::updateSource(assembly, createSource(QStringLiteral("s1"), QStringLiteral("a.pdf"), PAGE_LIMIT + 1, QSizeF()), &edit) == assembly);
    QVERIFY2(edit.errorMessage.startsWith(QStringLiteral("a.pdf has more than ")), qPrintable(edit.errorMessage));
    QVERIFY(Assembly::checkDocumentPageCount(PAGE_LIMIT, &error));
    QVERIFY(error.isEmpty());
    QVERIFY(!Assembly::checkDocumentPageCount(PAGE_LIMIT + 1, &error));
    QVERIFY2(error.startsWith(QStringLiteral("The document has more than ")), qPrintable(error));

    // create refuses duplicate ids and a page outside of its source
    QVERIFY(Assembly::create({ createSource(QStringLiteral("s1"), QStringLiteral("a.pdf"), 2) }, { createItem(QStringLiteral("x"), { createPage(0, 0) }), createItem(QStringLiteral("x"), { createPage(0, 1) }) }, { }, &error).isEmpty());
    QVERIFY(!error.isEmpty());
    QVERIFY(Assembly::create({ createSource(QStringLiteral("s1"), QStringLiteral("a.pdf"), 2) }, { createItem(QStringLiteral("x"), { createPage(0, 2) }) }, { }, &error).isEmpty());
    QVERIFY(!error.isEmpty());
    QVERIFY(Assembly::create({ createSource(QStringLiteral("s1"), QStringLiteral("a.pdf"), 2) }, { createItem(QStringLiteral("x"), { createPage(1, 0) }) }, { }, &error).isEmpty());
    QVERIFY(!error.isEmpty());
}

void DocumentAssemblyTest::fileRoundTrip()
{
    // Plan 4.3: write → read = the same sources, items and checkpoints (the sizes of the pages are
    // not in the file, the application fills them by updateSource)
    const QByteArray png = createPng(30, 40);
    std::vector<Source> sources;
    Source a = createSource(QStringLiteral("s1"), QStringLiteral("Contract.pdf"), 6, QSizeF(), 600000);
    a.pageSizes.clear();
    a.path = QStringLiteral("E:/Tender/Contract.pdf");
    a.modified = QDateTime(QDate(2026, 9, 28), QTime(10, 42, 0), QTimeZone::UTC);
    a.sha256 = QByteArray(32, '\x5A');
    Source b = createSource(QStringLiteral("s2"), QStringLiteral("Annex_A.pdf"), 24, QSizeF(), -1);
    b.pageSizes.clear();
    b.isParked = true;
    QString error;
    Source c = Assembly::createImageSource(png, QByteArray(), QStringLiteral("signature_page.png"), QString(), &error);
    c.id = QStringLiteral("s3");
    c.pageSizes.clear();
    sources = { a, b, c };

    Page cropped = createPage(0, 3, pdf::PageRotation::Rotate270);
    cropped.plan.cropMargins = QMarginsF(1.5, 2.0, 3.25, 0.0);
    cropped.plan.isClone = true;
    std::vector<Item> items = { createItem(QStringLiteral("p1"), { createPage(0, 0), createPage(0, 1), createPage(0, 2), createPage(0, 5), createPage(0, 4) }, QStringLiteral("Introduction, \"quoted\""), true),
                                createItem(QStringLiteral("p2"), { cropped }, QStringLiteral("Site plan")),
                                createItem(QStringLiteral("p3"), { createBlankPage(QSizeF(612.0, 792.0)) }),
                                createItem(QStringLiteral("p4"), { createPage(2, 0, pdf::PageRotation::Rotate180) }),
                                createItem(QStringLiteral("p5"), { createPage(1, 23), createPage(1, 22), createPage(1, 21) }) };
    pdf::PDFDocumentAssemblyFile::Content content;
    content.assembly = createAssembly(sources, items);
    pdf::PDFDocumentAssembly::Checkpoint checkpoint;
    checkpoint.id = QStringLiteral("c1");
    checkpoint.name = QStringLiteral("All files added");
    checkpoint.time = QDateTime(QDate(2026, 10, 2), QTime(10, 31, 0), QTimeZone::UTC);
    checkpoint.items = { createItem(QStringLiteral("x"), { createPage(1, 0) }) };
    pdf::PDFDocumentAssembly::Checkpoint firstCheckpoint = checkpoint;
    firstCheckpoint.id = QStringLiteral("c0");      // B23: the names may repeat, the ids never (L3)
    content.checkpoints = { firstCheckpoint, checkpoint };
    content.outputParameters.insert(QStringLiteral("output"), QStringLiteral("per-group"));
    content.outputParameters.insert(QStringLiteral("fileName"), QStringLiteral("##_{group_name}"));
    content.outputParameters.insert(QStringLiteral("password"), QStringLiteral("secret"));

    const QByteArray bytes = pdf::PDFDocumentAssemblyFile::write(content, QStringLiteral("E:/Tender"));
    const QJsonObject json = QJsonDocument::fromJson(bytes).object();
    QCOMPARE(json.value(QStringLiteral("format")).toString(), QStringLiteral("PDF4QT Assembly"));
    QCOMPARE(json.value(QStringLiteral("version")).toInt(), 1);
    QCOMPARE(json.value(QStringLiteral("sources")).toArray().at(0).toObject().value(QStringLiteral("relativePath")).toString(), QStringLiteral("Contract.pdf"));
    QVERIFY(!bytes.contains("secret"));     // nothing of passwords (4.3): only the output keys

    const pdf::PDFDocumentAssemblyFile::ReadResult result = pdf::PDFDocumentAssemblyFile::read(bytes, pdf::PDFDocumentAssemblyFile::Limits());
    QVERIFY2(result.content.has_value(), qPrintable(result.errorMessage));
    QVERIFY(result.problems.empty());
    std::vector<Source> expectedSources = sources;
    expectedSources[0].relativePath = QStringLiteral("Contract.pdf");
    QCOMPARE(result.content->assembly.getSources().size(), expectedSources.size());
    for (size_t i = 0; i < expectedSources.size(); ++i)
    {
        QVERIFY2(result.content->assembly.getSources()[i] == expectedSources[i], qPrintable(QString::number(i)));
    }
    QVERIFY(result.content->assembly.getItems() == items);
    QVERIFY(result.content->assembly.getRemovedItems().empty());
    QCOMPARE(result.content->checkpoints.size(), size_t(2));
    QVERIFY(result.content->checkpoints[1] == checkpoint);
    QCOMPARE(result.content->outputParameters.value(QStringLiteral("output")).toString(), QStringLiteral("per-group"));
    QCOMPARE(result.content->outputParameters.value(QStringLiteral("fileName")).toString(), QStringLiteral("##_{group_name}"));
    QVERIFY(!result.content->outputParameters.contains(QStringLiteral("password")));
}

void DocumentAssemblyTest::fileHostile_data()
{
    QTest::addColumn<QByteArray>("data");
    QTest::addColumn<QString>("error");

    const QByteArray source = R"({"id":"s1","kind":"pdf","name":"a.pdf","pageCount":3})";
    auto file = [&source](const QByteArray& sources, const QByteArray& items, const QByteArray& extra = QByteArray())
    {
        return R"({"format":"PDF4QT Assembly","version":1,"sources":[)" + (sources.isNull() ? source : sources) + R"(],"items":[)" + items + "]" + extra + "}";
    };

    // 4.3, 6: the texts of the refusals
    QTest::newRow("broken JSON") << QByteArray(R"({"format":"PDF4QT Assembly","version":1,"sources":[)") << QStringLiteral("The assembly file is damaged");
    QTest::newRow("no object") << QByteArray("[1,2]") << QStringLiteral("The assembly file is damaged");
    QTest::newRow("another format") << QByteArray(R"({"format":"PageMaster","version":1,"sources":[],"items":[]})") << QStringLiteral("This is not an assembly file.");
    QTest::newRow("version 2") << QByteArray(R"({"format":"PDF4QT Assembly","version":2,"sources":[],"items":[]})") << QStringLiteral("This assembly was saved by a newer version of PDF4QT.");
    QTest::newRow("version text") << QByteArray(R"({"format":"PDF4QT Assembly","version":"1","sources":[],"items":[]})") << QStringLiteral("This is not an assembly file.");
    QTest::newRow("version fraction") << QByteArray(R"({"format":"PDF4QT Assembly","version":1.5,"sources":[],"items":[]})") << QStringLiteral("This is not an assembly file.");
    QTest::newRow("duplicate item") << file(QByteArray(), R"({"id":"p1","pages":"A1"},{"id":"p1","pages":"A2"})") << QStringLiteral("An item of the assembly is not valid.");
    // L3 of RS3BCORE: a restore names its checkpoint by the id (B23) - two checkpoints of one id are refused as two items
    QTest::newRow("duplicate checkpoint") << file(QByteArray(), R"({"id":"p1","pages":"A1"})", R"(,"checkpoints":[{"id":"c1","name":"x","items":[{"id":"p1","pages":"A1"}]},{"id":"c1","name":"y","items":[{"id":"p1","pages":"A2"}]}])")
                                          << QStringLiteral("A checkpoint of the assembly is not valid.");
    QTest::newRow("duplicate source") << file(source + "," + source, R"({"id":"p1","pages":"A1"})") << QStringLiteral("A file of the assembly is not valid.");
    QTest::newRow("unknown letters") << file(QByteArray(), R"({"id":"p1","pages":"B1"})") << QStringLiteral("A page belongs to no file of the assembly.");
    QTest::newRow("page zero") << file(QByteArray(), R"({"id":"p1","pages":"A0"})") << QStringLiteral("A page of the assembly has no valid number.");
    QTest::newRow("negative page") << file(QByteArray(), R"({"id":"p1","pages":"A-1"})") << QStringLiteral("A page of the assembly has no valid number.");
    QTest::newRow("huge number") << file(QByteArray(), R"({"id":"p1","pages":"A99999999999"})") << QStringLiteral("A page of the assembly has no valid number.");
    QTest::newRow("bad rotation") << file(QByteArray(), R"({"id":"p1","pages":"A1@45"})") << QStringLiteral("A page has an invalid rotation.");
    QTest::newRow("bad crop") << file(QByteArray(), R"({"id":"p1","pages":"A1[1 2 3]"})") << QStringLiteral("A page has an invalid crop.");
    QTest::newRow("negative crop") << file(QByteArray(), R"({"id":"p1","pages":"A1[1 2 3 -4]"})") << QStringLiteral("A page has an invalid crop.");
    QTest::newRow("blank too small") << file(QByteArray(), R"({"id":"p1","pages":"*2x800"})") << QStringLiteral("A blank page has no valid size.");
    QTest::newRow("blank infinite") << file(QByteArray(), R"({"id":"p1","pages":"*infx800"})") << QStringLiteral("A blank page has no valid size.");
    QTest::newRow("pages of numbers") << file(QByteArray(), R"({"id":"p1","pages":5})") << QStringLiteral("An item of the assembly is not valid.");
    QTest::newRow("name of a number") << file(QByteArray(), R"({"id":"p1","name":7,"pages":"A1"})") << QStringLiteral("An item of the assembly is not valid.");
    QTest::newRow("empty page") << file(QByteArray(), R"({"id":"p1","pages":"A1,,A2"})") << QStringLiteral("An item has an empty page.");
    QTest::newRow("negative count") << file(R"({"id":"s1","kind":"pdf","name":"a.pdf","pageCount":-3})", R"({"id":"p1","pages":"A1"})") << QStringLiteral("A file of the assembly is not valid.");
    QTest::newRow("pdf without pages") << file(R"({"id":"s1","kind":"pdf","name":"a.pdf","pageCount":0})", R"({"id":"p1","pages":"A1"})") << QStringLiteral("A file of the assembly has no pages.");
    QTest::newRow("unknown kind") << file(R"({"id":"s1","kind":"video","name":"a.mp4","pageCount":1})", R"({"id":"p1","pages":"A1"})") << QStringLiteral("A file of the assembly has an unknown kind.");
    QTest::newRow("bad hash") << file(R"({"id":"s1","kind":"pdf","name":"a.pdf","pageCount":1,"sha256":"xyz"})", R"({"id":"p1","pages":"A1"})") << QStringLiteral("A file of the assembly has an invalid hash.");
    QTest::newRow("bad base64") << file(R"({"id":"s1","kind":"image","name":"a.png","pageCount":1,"data":"!!!"})", R"({"id":"p1","pages":"A1"})") << QStringLiteral("An image of the assembly has no valid data.");
    // Owner 2026-11-12: an assembly and a source of at most 65 536 pages
    QTest::newRow("range over pages") << file(R"({"id":"s1","kind":"pdf","name":"a.pdf","pageCount":65536})", R"({"id":"p1","pages":"A1-65536,A1"})")
                                      << QStringLiteral("The assembly has more than");
    QTest::newRow("source over pages") << file(R"({"id":"s1","kind":"pdf","name":"a.pdf","pageCount":65537})", R"({"id":"p1","pages":"A1"})")
                                       << QStringLiteral("A file of the assembly is not valid.");
    QTest::newRow("items missing") << QByteArray(R"({"format":"PDF4QT Assembly","version":1,"sources":[]})") << QStringLiteral("The items of the assembly are missing.");
}

void DocumentAssemblyTest::fileHostile()
{
    QFETCH(QByteArray, data);
    QFETCH(QString, error);

    // A hostile file is refused with its reason - never a crash, never a partial assembly
    const pdf::PDFDocumentAssemblyFile::ReadResult result = pdf::PDFDocumentAssemblyFile::read(data, pdf::PDFDocumentAssemblyFile::Limits());
    QVERIFY(!result.content.has_value());
    QVERIFY2(result.errorMessage.startsWith(error), qPrintable(result.errorMessage));
}

void DocumentAssemblyTest::fileLimits()
{
    // Plan 6 with RB8: every limit by one over (the limits are values, the defaults are tested by
    // their size in fileManyItemsInTime)
    auto createFile = [](int sourceCount, int itemCount, int checkpointCount, int nameLength, int depth)
    {
        QByteArray sources;
        for (int i = 0; i < sourceCount; ++i)
        {
            sources += (i ? "," : "") + QByteArray(R"({"id":"s)") + QByteArray::number(i) + R"(","kind":"pdf","name":"a.pdf","pageCount":5})";
        }
        QByteArray items;
        for (int i = 0; i < itemCount; ++i)
        {
            items += (i ? "," : "") + QByteArray(R"({"id":"p)") + QByteArray::number(i) + R"(","name":")" + QByteArray(i == 0 ? nameLength : 1, 'n') + R"(","pages":"A1"})";
        }
        QByteArray checkpoints;
        for (int i = 0; i < checkpointCount; ++i)
        {
            checkpoints += (i ? "," : "") + QByteArray(R"({"id":"c)") + QByteArray::number(i) + R"(","name":"x","time":"2026-10-02T10:31:00Z","items":[]})";
        }
        QByteArray nested = "1";
        for (int i = 0; i < depth; ++i)
        {
            nested = "[" + nested + "]";
        }
        return R"({"format":"PDF4QT Assembly","version":1,"sources":[)" + sources + R"(],"items":[)" + items + R"(],"checkpoints":[)" + checkpoints + R"(],"extra":)" + nested + "}";
    };

    pdf::PDFDocumentAssemblyFile::Limits limits;
    limits.maximumSourceCount = 3;
    limits.maximumItemCount = 4;
    limits.maximumCheckpointCount = 2;
    limits.maximumNameLength = 10;
    limits.maximumDepth = 5;

    // At the limits: read (the root is level 1, "extra" level 2, its arrays the levels 3 to 5)
    const pdf::PDFDocumentAssemblyFile::ReadResult accepted = pdf::PDFDocumentAssemblyFile::read(createFile(3, 4, 2, 10, 3), limits);
    QVERIFY2(accepted.content.has_value(), qPrintable(accepted.errorMessage));

    QVERIFY(!pdf::PDFDocumentAssemblyFile::read(createFile(4, 4, 2, 10, 3), limits).content.has_value());
    QVERIFY(!pdf::PDFDocumentAssemblyFile::read(createFile(3, 5, 2, 10, 3), limits).content.has_value());
    QVERIFY(!pdf::PDFDocumentAssemblyFile::read(createFile(3, 4, 3, 10, 3), limits).content.has_value());
    QVERIFY(!pdf::PDFDocumentAssemblyFile::read(createFile(3, 4, 2, 11, 3), limits).content.has_value());
    const pdf::PDFDocumentAssemblyFile::ReadResult deep = pdf::PDFDocumentAssemblyFile::read(createFile(3, 4, 2, 10, 4), limits);
    QVERIFY(!deep.content.has_value());
    QCOMPARE(deep.errorMessage, QStringLiteral("The assembly file is damaged: it is nested too deep."));

    // The size before the parsing
    const QByteArray file = createFile(1, 1, 0, 1, 0);
    limits = pdf::PDFDocumentAssemblyFile::Limits();
    limits.maximumFileSize = file.size();
    QVERIFY(pdf::PDFDocumentAssemblyFile::read(file, limits).content.has_value());
    limits.maximumFileSize = file.size() - 1;
    QVERIFY(pdf::PDFDocumentAssemblyFile::read(file, limits).errorMessage.startsWith(QStringLiteral("The assembly file is larger than")));

    // The defaults of plan 6 / RB8
    const pdf::PDFDocumentAssemblyFile::Limits defaults;
    QCOMPARE(defaults.maximumFileSize, qint64(256) * 1024 * 1024);
    QCOMPARE(defaults.maximumDepth, 16);
    QCOMPARE(defaults.maximumItemCount, 100000);
    QCOMPARE(defaults.maximumSourceCount, 1000);
    QCOMPARE(defaults.maximumCheckpointCount, 100);
    QCOMPARE(defaults.maximumNameLength, 1024);
}

void DocumentAssemblyTest::filePageLimit()
{
    // Owner 2026-11-12 (plan of step 3b, 13 point 3): the pages of the items and of all
    // checkpoints together are at most 16 x 65 536 = 1 048 576, counted before a range is
    // expanded; the items and each checkpoint alone at most the pages of an assembly (65 536). The
    // boundaries by small limits (a blank page counts one page, a range its length, a page out of
    // its source counts too - it is read into the bin)
    auto createFile = [](const QByteArray& items, const QByteArrayList& checkpoints)
    {
        QByteArray checkpointArray;
        for (int i = 0; i < int(checkpoints.size()); ++i)
        {
            checkpointArray += (i ? "," : "") + QByteArray(R"({"id":"c)") + QByteArray::number(i) + R"(","name":"x","items":[{"id":"q","pages":")" + checkpoints[i] + R"("}]})";
        }
        return R"({"format":"PDF4QT Assembly","version":1,"sources":[{"id":"s1","kind":"pdf","name":"a.pdf","pageCount":6}],"items":[{"id":"p1","pages":")" + items +
               R"("}],"checkpoints":[)" + checkpointArray + "]}";
    };

    pdf::PDFDocumentAssemblyFile::Limits limits;
    limits.maximumPageCount = 6;
    limits.maximumFilePageCount = 10;

    // 6 + 4 pages: at the limits
    const pdf::PDFDocumentAssemblyFile::ReadResult accepted = pdf::PDFDocumentAssemblyFile::read(createFile("A6-1", { "A1-2", "A3,*612x792" }), limits);
    QVERIFY2(accepted.content.has_value(), qPrintable(accepted.errorMessage));
    QCOMPARE(accepted.content->assembly.getItems().front().pages.size(), size_t(6));
    QCOMPARE(accepted.content->checkpoints.size(), size_t(2));

    // One page over the joint limit by a checkpoint, by a blank page, by a page out of its source
    for (const QByteArrayList& checkpoints : { QByteArrayList({ "A1-2", "A3-5" }), QByteArrayList({ "A1-2", "A3,*612x792,*612x792" }), QByteArrayList({ "A1-2", "A3,A7,A8" }) })
    {
        const pdf::PDFDocumentAssemblyFile::ReadResult refused = pdf::PDFDocumentAssemblyFile::read(createFile("A6-1", checkpoints), limits);
        QVERIFY(!refused.content.has_value());
        QVERIFY2(refused.errorMessage.startsWith(QStringLiteral("The assembly and its checkpoints have more than")), qPrintable(refused.errorMessage));
    }

    // One page over the limit of the items
    const pdf::PDFDocumentAssemblyFile::ReadResult items = pdf::PDFDocumentAssemblyFile::read(createFile("A1-6,A1", { }), limits);
    QVERIFY(!items.content.has_value());
    QVERIFY2(items.errorMessage.startsWith(QStringLiteral("The assembly has more than")), qPrintable(items.errorMessage));

    // One page over the limit of a checkpoint (1 + 7 pages: the file is under its limit)
    const pdf::PDFDocumentAssemblyFile::ReadResult checkpoint = pdf::PDFDocumentAssemblyFile::read(createFile("A1", { "A1-6,*612x792" }), limits);
    QVERIFY(!checkpoint.content.has_value());
    QVERIFY2(checkpoint.errorMessage.startsWith(QStringLiteral("A checkpoint of the assembly has more than")), qPrintable(checkpoint.errorMessage));
}

void DocumentAssemblyTest::fileAndCheckpointPageLimits()
{
    using File = pdf::PDFDocumentAssemblyFile;

    // Owner 2026-11-12 (plan of step 3b, 13 point 3): the defaults - 65 536 pages of the items and
    // of each checkpoint, 1 048 576 (16 x 65 536) of the file; Save checkpoint keeps the room of a
    // whole assembly, so the checkpoints have at most 1 048 576 - 65 536 = 983 040 pages together
    // and a file of an assembly, which grows to its limit later, is still read
    const File::Limits defaults;
    QCOMPARE(defaults.maximumPageCount, qint64(65536));
    QCOMPARE(defaults.maximumFilePageCount, qint64(1048576));
    QCOMPARE(File::MAXIMUM_FILE_PAGE_COUNT, qint64(1048576));
    QCOMPARE(File::MAXIMUM_CHECKPOINT_PAGE_COUNT, qint64(983040));

    auto createFile = [](const QByteArrayList& checkpoints)
    {
        QByteArray checkpointArray;
        for (int i = 0; i < int(checkpoints.size()); ++i)
        {
            checkpointArray += (i ? "," : "") + QByteArray(R"({"id":"c)") + QByteArray::number(i) + R"(","name":"x","items":[{"id":"q","pages":")" + checkpoints[i] + R"("}]})";
        }
        return R"({"format":"PDF4QT Assembly","version":1,"sources":[{"id":"s1","kind":"pdf","name":"a.pdf","pageCount":65536}],"items":[{"id":"p1","pages":"A1-65536"}],"checkpoints":[)" +
               checkpointArray + "]}";
    };
    const QByteArrayList fullCheckpoints(15, QByteArray("A1-65536"));

    // 65 536 pages of the items and 15 checkpoints of 65 536 pages: 1 048 576 pages, read
    File::ReadResult accepted = File::read(createFile(fullCheckpoints), defaults);
    QVERIFY2(accepted.content.has_value(), qPrintable(accepted.errorMessage));
    QCOMPARE(accepted.content->assembly.getItems().front().pages.size(), size_t(65536));
    QCOMPARE(accepted.content->checkpoints.size(), size_t(15));
    QCOMPARE(accepted.content->checkpoints.back().items.front().pages.size(), size_t(65536));

    // A page more - by a sixteenth checkpoint - is refused by the count before a page is expanded:
    // a file of a kilobyte (the reading by each range would expand the pages and then refuse
    // with another text, "The assembly has too many pages.")
    QByteArrayList overCheckpoints = fullCheckpoints;
    overCheckpoints << "A1";
    const QByteArray over = createFile(overCheckpoints);
    QVERIFY(over.size() < 2048);
    const File::ReadResult overFile = File::read(over, defaults);
    QVERIFY(!overFile.content.has_value());
    QVERIFY2(overFile.errorMessage.startsWith(QStringLiteral("The assembly and its checkpoints have more than")), qPrintable(overFile.errorMessage));

    // A checkpoint of 65 537 pages is refused (the file has room for it)
    const File::ReadResult overCheckpoint = File::read(createFile({ "A1-65536,A1" }), defaults);
    QVERIFY(!overCheckpoint.content.has_value());
    QVERIFY2(overCheckpoint.errorMessage.startsWith(QStringLiteral("A checkpoint of the assembly has more than")), qPrintable(overCheckpoint.errorMessage));

    // Save checkpoint: the assembly of 65 536 pages has room for 15 checkpoints of itself
    // (15 x 65 536 = 983 040), not for a sixteenth; the content read is saved again
    File::Content& content = *accepted.content;
    QString error;
    QVERIFY2(File::checkPageCount(content, &error), qPrintable(error));
    QVERIFY(!File::canCreateCheckpoint(content.assembly, content.checkpoints, &error));
    QVERIFY2(error.startsWith(QStringLiteral("The checkpoints of an assembly have at most ")), qPrintable(error));
    Assembly::Checkpoint last = std::move(content.checkpoints.back());
    content.checkpoints.pop_back();
    QVERIFY2(File::canCreateCheckpoint(content.assembly, content.checkpoints, &error), qPrintable(error));
    QVERIFY(error.isEmpty());

    // Save Assembly refuses a file, which would not be read again: a checkpoint of a page more than
    // the room (checkpoints of another file), a checkpoint of 65 537 pages
    content.checkpoints.push_back(std::move(last));
    content.checkpoints.push_back(Assembly::createCheckpoint(createAssembly({ createSource(QStringLiteral("s1"), QStringLiteral("a.pdf"), 1) }, createPageItems(1)), QStringLiteral("One")));
    QVERIFY(!File::checkPageCount(content, &error));
    QVERIFY2(error.startsWith(QStringLiteral("The assembly and its checkpoints have more than")), qPrintable(error));
    content.checkpoints.pop_back();
    content.checkpoints.front().items.front().pages.push_back(createPage(0, 0));
    QVERIFY(!File::checkPageCount(content, &error));
    QVERIFY2(error.startsWith(QStringLiteral("A checkpoint of the assembly has more than")), qPrintable(error));

    // At most 100 checkpoints (plan 6)
    const Assembly small = createAssembly({ createSource(QStringLiteral("s1"), QStringLiteral("a.pdf"), 1) }, createPageItems(1));
    std::vector<Assembly::Checkpoint> manyCheckpoints(99, Assembly::createCheckpoint(small, QStringLiteral("x")));
    QVERIFY2(File::canCreateCheckpoint(small, manyCheckpoints, &error), qPrintable(error));
    manyCheckpoints.push_back(manyCheckpoints.front());
    QVERIFY(!File::canCreateCheckpoint(small, manyCheckpoints, &error));
    QVERIFY2(error.startsWith(QStringLiteral("An assembly has at most ")), qPrintable(error));
}

void DocumentAssemblyTest::fileChangedPageCount()
{
    // 4.3, 6: a page over the page count of its source goes to the bin with a problem (not an
    // error); a network path is only read - the core never opens or checks it (RB8: the
    // application flags it as "On another computer")
    const QByteArray data = R"({"format":"PDF4QT Assembly","version":1,
        "sources":[{"id":"s1","kind":"pdf","name":"Contract.pdf","path":"//server/share/Contract.pdf","pageCount":5}],
        "items":[{"id":"p1","name":"Intro","group":true,"pages":"A1-6"},{"id":"p2","pages":"A6"},{"id":"p3","pages":"A2"}]})";
    const pdf::PDFDocumentAssemblyFile::ReadResult result = pdf::PDFDocumentAssemblyFile::read(data, pdf::PDFDocumentAssemblyFile::Limits());
    QVERIFY2(result.content.has_value(), qPrintable(result.errorMessage));
    const Assembly& assembly = result.content->assembly;
    QCOMPARE(assembly.getSources().front().path, QStringLiteral("//server/share/Contract.pdf"));
    QCOMPARE(describe(assembly), QStringList({ QStringLiteral("Intro{A1,A2,A3,A4,A5}"), QStringLiteral("A2") }));
    QCOMPARE(assembly.getRemovedItems().size(), size_t(2));
    QCOMPARE(Assembly::getSummary(assembly).removedPageCount, qint64(2));
    QCOMPARE(result.problems.size(), size_t(1));
    QCOMPARE(result.problems.front().sourceId, QStringLiteral("s1"));
    QVERIFY(result.problems.front().text.contains(QStringLiteral("Contract.pdf")));
}

void DocumentAssemblyTest::fileManyItemsInTime()
{
    // Plan 8: the most items under a time limit (PDF4QT_TEST_TIMEOUT_FACTOR) - 65 536, as every item
    // has a page and an assembly has at most 65 536 pages (owner 2026-11-12)
    const int count = int(pdf::PDFDocumentAssembly::MAXIMUM_PAGE_COUNT);
    pdf::PDFDocumentAssemblyFile::Content content;
    content.assembly = createAssembly({ createSource(QStringLiteral("s1"), QStringLiteral("a.pdf"), count, QSizeF()) }, createPageItems(count));

    QElapsedTimer timer;
    timer.start();
    const QByteArray data = pdf::PDFDocumentAssemblyFile::write(content, QString());
    const pdf::PDFDocumentAssemblyFile::ReadResult result = pdf::PDFDocumentAssemblyFile::read(data, pdf::PDFDocumentAssemblyFile::Limits());
    QVERIFY2(result.content.has_value(), qPrintable(result.errorMessage));
    QCOMPARE(result.content->assembly.getItems().size(), size_t(count));

    // Operations over all of them: Every N, Sort, a remove and a restore
    Edit edit;
    pdf::PDFDocumentAssembly::RegroupSettings settings;
    settings.pageCount = 7;
    const Assembly regrouped = Assembly::regroup(result.content->assembly, { }, Assembly::RegroupMode::EveryPages, settings, &edit);
    QCOMPARE(regrouped.getItems().size(), size_t((count + 6) / 7));
    const Assembly sorted = Assembly::sort(result.content->assembly, { }, Assembly::SortKey::Reverse, &edit);
    const Assembly restored = Assembly::restoreAll(Assembly::remove(sorted, Assembly::select(sorted, { }, Assembly::SelectRule::Even, { }), &edit), &edit);
    QVERIFY(restored.getItems() == sorted.getItems());

    const double factor = qMax(1.0, qEnvironmentVariable("PDF4QT_TEST_TIMEOUT_FACTOR", QStringLiteral("1")).toDouble());
    QVERIFY2(timer.elapsed() < qint64(20000 * factor), qPrintable(QString::number(timer.elapsed())));
}

QTEST_GUILESS_MAIN(DocumentAssemblyTest)

#include "tst_documentassemblytest.moc"
