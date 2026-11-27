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

// Edit Content, stage E1, package P13: the cards of the right panel and their stack (zadani_gui.md 4.19;
// zadani_overeni.md 8: the fixed order of the cards, Mixed, the rebuild from the event loop, the value typed and not
// confirmed before Undo). The context of each test has its own registry with the entries of P13 only, over a page
// written by hand; the expected values come from the content of the page (ISO 32000-2: re, rg / RG / k / G, w) and
// from the frames of Figma, never from a run.

#include "appdocumentfixture.h"
#include "contenttestdocuments.h"

#include "pdfappeditcontentcards.h"
#include "pdfappeditcontentcards_appearance.h"
#include "pdfappeditcontentcards_geometry.h"
#include "pdfappeditcontentcontext.h"
#include "pdfappeditcontentregistry.h"
#include "pdfappeditcontentselection.h"

#include "pdfcomponentbuttons.h"
#include "pdfcomponentsections.h"
#include "pdfdeferreddeletion.h"
#include "pdffluentwidgets.h"

#include "pdfcontentdisplay.h"
#include "pdfcontenteditsession.h"
#include "pdfcontentundostack.h"
#include "pdfdocumentbuilder.h"

#include <QCheckBox>
#include <QLocale>
#include <QPushButton>
#include <QSignalSpy>
#include <QToolButton>
#include <QtTest>

#include <memory>

using Context = pdf::PDFAppEditContentContext;
using Registry = pdf::PDFAppEditContentRegistry;
using Stack = pdf::PDFAppEditContentCardStack;
using CardIds = pdf::PDFAppEditContentCardIds;

/// An action of a test registry (Bring to Front of P12, which is not in the registry of this test): counts its runs
class ContentCardsCountingAction : public pdf::PDFAppEditContentActionEntry
{
public:
    explicit ContentCardsCountingAction(int* count) : m_count(count) { }

    virtual QString getId() const override { return QLatin1String(pdf::PDFAppEditContentActionIds::ORDER_BRING_TO_FRONT); }
    virtual QString getTitle(const pdf::PDFAppEditContentContext*) const override { return QStringLiteral("Bring to Front"); }
    virtual void trigger(pdf::PDFAppEditContentContext*) const override { ++*m_count; }

private:
    int* m_count = nullptr;
};

/// One test: the registry of P13, a context over the document and the stack of the cards (shown offscreen)
class ContentCardsSetup
{
public:
    explicit ContentCardsSetup(pdf::PDFDocumentPointer document, int* actionCount = nullptr)
    {
        pdf::PDFAppEditContentCardStack::registerParts(&registry);
        pdf::PDFAppEditContentAppearanceCards::registerParts(&registry);
        pdf::PDFAppEditContentGeometryCards::registerParts(&registry);
        if (actionCount)
        {
            registry.addAction(std::make_unique<ContentCardsCountingAction>(actionCount));
        }
        context = new Context(std::move(document), pdf::PDFCMSPointer(), &registry, nullptr);
    }

    ~ContentCardsSetup()
    {
        if (stack)
        {
            pdf::PDFDeferredDeletion::retire(stack.data());
        }
        if (context)
        {
            pdf::PDFDeferredDeletion::retire(context.data());
        }
        pdf::PDFDeferredDeletion::flush();
    }

    /// Shows the page 0, waits for its container, creates the stack
    bool open()
    {
        QSignalSpy loaded(context.data(), &Context::containerChanged);
        context->setPage(0);
        if (!context->getContainer() && !loaded.wait(15000 * pdftest::AppDocumentFixture::getTimeoutFactor()))
        {
            return false;
        }
        stack = new Stack(context, nullptr);
        stack->resize(320, 900);
        stack->show();
        return context->getContainer() != nullptr;
    }

    /// The rebuild scheduled by a change has run
    bool settle()
    {
        for (int i = 0; i < 20 && stack && stack->isRebuildScheduled(); ++i)
        {
            QCoreApplication::processEvents();
        }
        QCoreApplication::processEvents();
        return stack && !stack->isRebuildScheduled();
    }

    template<typename T>
    T* find(const char* objectName) const
    {
        return stack ? stack->findChild<T*>(QLatin1String(objectName)) : nullptr;
    }

    Registry registry;
    QPointer<Context> context;
    QPointer<Stack> stack;
};

/// Helpers of the test
class ContentCardsHelper
{
public:
    ContentCardsHelper() = delete;

    /// The page of three paths (A, B, C in the order of the content):
    ///  A: "1 0 0 RG 0 0 1 rg 2 w 10 10 50 30 re B" - fill DeviceRGB (0 0 1), stroke DeviceRGB (1 0 0), width 2
    ///  B: "0 0 0 1 k 100 100 20 20 re f" - fill DeviceCMYK (0 0 0 1), no stroke
    ///  C: "0.5 G 1 w 120 20 30 10 re S" - stroke DeviceGray 0.5, width 1, no fill
    static pdf::PDFDocumentPointer createDocument(bool hasAnnotation = false)
    {
        using D = pdftest::ContentTestDocuments;
        const QByteArray content = QByteArrayLiteral("1 0 0 RG 0 0 1 rg 2 w 10 10 50 30 re B\n"
                                                     "0 0 0 1 k 100 100 20 20 re f\n"
                                                     "0.5 G 1 w 120 20 30 10 re S\n");
        D::PageFinisher finisher;
        if (hasAnnotation)
        {
            finisher = [](pdf::PDFDocumentBuilder* builder, pdf::PDFObjectReference page)
            {
                const pdf::PDFObject annotation = D::addObject(builder, D::createDictionaryObject({ { "Type", pdf::PDFObject::createName("Annot") },
                                                                                                   { "Subtype", pdf::PDFObject::createName("Square") },
                                                                                                   { "Rect", D::createNumberArrayObject({ 20, 20, 60, 60 }) } }));
                builder->mergeTo(page, D::createDictionaryObject({ { "Annots", D::createArrayObject({ annotation }) } }));
            };
        }
        return pdf::PDFDocumentPointer(new pdf::PDFDocument(D::createDocumentFromObjects([&content](pdf::PDFDocumentBuilder* builder)
        {
            return D::addStreamObject(builder, D::DictionaryEntries(), content);
        }, &D::createNoResources, finisher)));
    }

    /// The Path nodes among the children of the root, in the order of the content
    static std::vector<pdf::PDFContentNodeReference> getPaths(const Context* context)
    {
        std::vector<pdf::PDFContentNodeReference> paths;
        const pdf::PDFContentContainerPointer container = context->getContainer();
        for (const pdf::PDFContentNodePointer& child : container->getRoot()->getChildren())
        {
            if (child->getKind() == pdf::PDFContentNodeKind::Path)
            {
                paths.push_back(pdf::PDFContentNodeReference{ container->getKey(), child->getId() });
            }
        }
        return paths;
    }

    /// The display state of the node in the current container
    static pdf::PDFContentDisplayState getState(const Context* context, const pdf::PDFContentNodeReference& node)
    {
        return pdf::PDFContentDisplay::getDisplayState(*context->getContainer(), node.node, pdf::PDFContentDisplayContext()).value_or(pdf::PDFContentDisplayState());
    }

    /// Types a value into the field as the user does and confirms it by Enter (valueEntered)
    static void enter(pdf::PDFDoubleSpinBox* field, const QString& text)
    {
        field->setFocus();
        field->selectAll();
        QTest::keyClick(field, Qt::Key_A, Qt::ControlModifier);
        QTest::keyClicks(field, text);
        QTest::keyClick(field, Qt::Key_Return);
    }

    /// Types a value without confirming it
    static void type(pdf::PDFDoubleSpinBox* field, const QString& text)
    {
        field->setFocus();
        QTest::keyClick(field, Qt::Key_A, Qt::ControlModifier);
        QTest::keyClicks(field, text);
    }

    static bool isSame(const QRectF& actual, const QRectF& expected)
    {
        const bool isSame = qAbs(actual.left() - expected.left()) < 1e-6 && qAbs(actual.top() - expected.top()) < 1e-6 &&
                            qAbs(actual.width() - expected.width()) < 1e-6 && qAbs(actual.height() - expected.height()) < 1e-6;
        if (!isSame)
        {
            qWarning() << "rectangle" << actual << "expected" << expected;
        }
        return isSame;
    }

    static QRectF box(const Context* context, const pdf::PDFContentNodeReference& node)
    {
        return pdf::PDFAppEditContentGeometryHelper::getPageBoundingBox(context, node);
    }

    /// The identifiers are in the order of PDFAppEditContentCardIds::getOrder (the fixed order of 4.19)
    static bool isInFixedOrder(const QStringList& ids)
    {
        for (int i = 1; i < ids.size(); ++i)
        {
            if (CardIds::getOrder(ids[i - 1]) >= CardIds::getOrder(ids[i]))
            {
                return false;
            }
        }
        return true;
    }

    static size_t getUndoCount(const Context* context)
    {
        return context->getSession()->getUndoStack()->getCount();
    }
};

class AppToolContentCardsTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();

    void cardsOfTheState();
    void rebuildFromTheEventLoop();
    void selectionTexts();
    void fillAndStrokeValues();
    void mixedValues();
    void colorEntryIsOneCommand();
    void spaceAndSwatches();
    void widthOfTwoObjects();
    void transparency();
    void pendingValueCommittedBeforeUndo();
    void pendingValueForgottenAtRetire();
    void positionAndSize();
    void rotationAndFlip();
    void arrangeAlignToTheKey();
    void arrangeActionsOfTheRegistry();
    void lockedObjectUnlock();
    void annotationsAndPreview();
    void sectionStateSurvivesRebuild();
    void retireInsideOfAClick();
};

void AppToolContentCardsTest::initTestCase()
{
    qRegisterMetaType<pdf::PDFContentNodeReference>();
    qRegisterMetaType<pdf::PDFAppEditContentNoticeRequest>();
    QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedStates));
}

void AppToolContentCardsTest::cardsOfTheState()
{
    // zadani_gui.md 4.19 and the frames: a path - Selection, Fill and stroke, Transparency, Position and size, Arrange; an
    // image-less selection of a path without a paint is not possible here; nothing - Selection, Arrange (-nothing-
    // selected); Preview - only Preview (-preview); Keyboard - only Selection of P13 (-keyboard); a page with an
    // annotation and nothing selected - also Annotations and fields (-annotations-underlay); always in the fixed order
    using H = ContentCardsHelper;
    ContentCardsSetup setup(H::createDocument(true));
    QVERIFY(setup.open());
    const std::vector<pdf::PDFContentNodeReference> paths = H::getPaths(setup.context);
    QCOMPARE(paths.size(), size_t(3));

    // The order of the registry (PDFAppEditContentCardIds::getOrder) puts Annotations and fields after Arrange; the frame
    // -annotations-underlay has it above (a request to the coordinator, pe1p13_state.md)
    QCOMPARE(setup.stack->getCardIds(), QStringList({ CardIds::SELECTION, CardIds::ARRANGE, CardIds::ANNOTATIONS }));
    QVERIFY(H::isInFixedOrder(setup.stack->getCardIds()));

    setup.context->getSelection()->setNode(paths[0]);
    QVERIFY(setup.settle());
    QCOMPARE(setup.stack->getCardIds(), QStringList({ CardIds::SELECTION, CardIds::FILL_AND_STROKE, CardIds::TRANSPARENCY, CardIds::POSITION_AND_SIZE, CardIds::ARRANGE }));
    QVERIFY(H::isInFixedOrder(setup.stack->getCardIds()));

    setup.context->setCardRequested(QLatin1String(CardIds::KEYBOARD), true);
    QVERIFY(setup.settle());
    QCOMPARE(setup.stack->getCardIds(), QStringList({ CardIds::SELECTION }));
    setup.context->setCardRequested(QLatin1String(CardIds::KEYBOARD), false);

    setup.context->setPreviewActive(true);
    QVERIFY(setup.settle());
    QCOMPARE(setup.stack->getCardIds(), QStringList({ CardIds::PREVIEW }));
    setup.context->setPreviewActive(false);
    QVERIFY(setup.settle());
    QCOMPARE(setup.stack->getCardIds().size(), 5);

    // A card kept over a rebuild is the same object (it keeps its fields and its sections)
    const QPointer<pdf::PDFAppEditContentCard> transparency = setup.stack->getCard(QLatin1String(CardIds::TRANSPARENCY));
    setup.context->getSelection()->setNode(paths[1]);
    QVERIFY(setup.settle());
    QVERIFY(transparency && transparency == setup.stack->getCard(QLatin1String(CardIds::TRANSPARENCY)));
}

void AppToolContentCardsTest::rebuildFromTheEventLoop()
{
    // A change only schedules the rebuild: the cards change, when the event loop runs (zadani_gui.md 4.19)
    using H = ContentCardsHelper;
    ContentCardsSetup setup(H::createDocument());
    QVERIFY(setup.open());
    const std::vector<pdf::PDFContentNodeReference> paths = H::getPaths(setup.context);
    QCOMPARE(setup.stack->getCardIds(), QStringList({ CardIds::SELECTION, CardIds::ARRANGE }));

    setup.context->getSelection()->setNode(paths[0]);
    QVERIFY(setup.stack->isRebuildScheduled());
    QCOMPARE(setup.stack->getCardIds(), QStringList({ CardIds::SELECTION, CardIds::ARRANGE }));
    QVERIFY(setup.settle());
    QCOMPARE(setup.stack->getCardIds().size(), 5);

    // A card, which is shown no more, is retired from the event loop, not deleted
    QPointer<pdf::PDFAppEditContentCard> fill = setup.stack->getCard(QLatin1String(CardIds::FILL_AND_STROKE));
    setup.context->getSelection()->clear();
    QVERIFY(setup.settle());
    QVERIFY(!setup.stack->getCard(QLatin1String(CardIds::FILL_AND_STROKE)));
    QVERIFY(!fill || pdf::PDFDeferredDeletion::isRetired(fill));
}

void AppToolContentCardsTest::selectionTexts()
{
    // The card Selection: -nothing-selected, edit-content-v2 (badge, title, path of the types), -multi-selected
    using H = ContentCardsHelper;
    ContentCardsSetup setup(H::createDocument());
    QVERIFY(setup.open());
    const std::vector<pdf::PDFContentNodeReference> paths = H::getPaths(setup.context);

    pdf::PDFLabel* title = setup.find<pdf::PDFLabel>("editContentSelectionTitle");
    pdf::PDFLabel* subtitle = setup.find<pdf::PDFLabel>("editContentSelectionSubtitle");
    pdf::PDFPathBar* path = setup.find<pdf::PDFPathBar>("editContentSelectionPath");
    QVERIFY(title && subtitle && path);
    QCOMPARE(title->text(), QStringLiteral("Nothing selected"));

    // A: "10 10 50 30 re B" - one rectangle, 50 x 30 pt, fill and stroke; the path Page 1 > Path
    setup.context->getSelection()->setNode(paths[0]);
    QVERIFY(setup.settle());
    QCOMPARE(title->text(), QStringLiteral("Path · rectangle"));
    QCOMPARE(subtitle->text(), QStringLiteral("50 × 30 pt · fill and stroke"));
    QCOMPARE(path->getSegments(), QStringList({ QStringLiteral("Page 1"), QStringLiteral("Path") }));
    pdf::PDFAppEditContentCard* card = setup.stack->getCard(QLatin1String(CardIds::SELECTION));
    QCOMPARE(card->getSections().front()->getBadgeText(), QStringLiteral("Path"));

    // The link of the page clears the selection
    Q_EMIT path->segmentActivated(0);
    QVERIFY(setup.context->getSelection()->isEmpty());

    // Two objects: the count and the kinds
    setup.context->getSelection()->setNodes({ paths[0], paths[2] });
    QVERIFY(setup.settle());
    QCOMPARE(title->text(), QStringLiteral("2 object(s)"));     // the source text of %Ln (no translation in the test)
    QCOMPARE(subtitle->text(), QStringLiteral("Path, Path"));
    QVERIFY(card->getSections().front()->getBadgeText().isEmpty());
}

void AppToolContentCardsTest::fillAndStrokeValues()
{
    // A: fill DeviceRGB (0 0 1), stroke DeviceRGB (1 0 0), width 2 (-fill-and-stroke); B: fill DeviceCMYK (0 0 0 1) and
    // no stroke - no Width and no expanded part (-color-cmyk)
    using H = ContentCardsHelper;
    ContentCardsSetup setup(H::createDocument());
    QVERIFY(setup.open());
    const std::vector<pdf::PDFContentNodeReference> paths = H::getPaths(setup.context);
    setup.context->getSelection()->setNode(paths[0]);
    QVERIFY(setup.settle());

    pdf::PDFComboBox* fillSpace = setup.find<pdf::PDFComboBox>("editContentFillSpace");
    pdf::PDFComboBox* strokeSpace = setup.find<pdf::PDFComboBox>("editContentStrokeSpace");
    pdf::PDFDoubleSpinBox* width = setup.find<pdf::PDFDoubleSpinBox>("editContentStrokeWidth");
    QCheckBox* fillCheck = setup.find<QCheckBox>("editContentFillCheck");
    QVERIFY(fillSpace && strokeSpace && width && fillCheck);
    QCOMPARE(fillSpace->currentText(), QStringLiteral("DeviceRGB"));
    QCOMPARE(strokeSpace->currentText(), QStringLiteral("DeviceRGB"));
    QCOMPARE(fillCheck->checkState(), Qt::Checked);
    QVERIFY(!width->isMixed());
    QCOMPARE(width->value(), 2.0);
    const double fillValues[] = { 0.0, 0.0, 1.0 };
    for (int i = 0; i < 3; ++i)
    {
        pdf::PDFDoubleSpinBox* field = setup.find<pdf::PDFDoubleSpinBox>(QStringLiteral("editContentFillValue%1").arg(i).toLatin1().constData());
        QVERIFY(field && !field->isHidden());
        QCOMPARE(field->value(), fillValues[i]);
    }
    QVERIFY(setup.find<pdf::PDFDoubleSpinBox>("editContentFillValue3")->isHidden());
    QVERIFY(setup.find<pdf::PDFComboBox>("editContentCap"));

    setup.context->getSelection()->setNode(paths[1]);
    QVERIFY(setup.settle());
    QCOMPARE(fillSpace->currentText(), QStringLiteral("DeviceCMYK"));
    QCOMPARE(setup.find<pdf::PDFDoubleSpinBox>("editContentFillValue3")->value(), 1.0);
    QCOMPARE(setup.find<QCheckBox>("editContentStrokeCheck")->checkState(), Qt::Unchecked);
    QVERIFY(!strokeSpace->isEnabled());
    QVERIFY(width->parentWidget()->isHidden());
    const pdf::PDFAppEditContentCard* card = setup.stack->getCard(QLatin1String(CardIds::FILL_AND_STROKE));
    QVERIFY(!card->getSections().front()->isExpandable());
}

void AppToolContentCardsTest::mixedValues()
{
    // A and C: the strokes are DeviceRGB and DeviceGray (the space is Mixed), the widths 2 and 1 (Mixed); only A
    // fills (the check box is partly checked, the one fill color is not mixed); Transparency: A with /ca 0.5 by a
    // command and C with 1 - Mixed
    using H = ContentCardsHelper;
    using F = pdf::PDFContentCommandFactory;
    ContentCardsSetup setup(H::createDocument());
    QVERIFY(setup.open());
    const std::vector<pdf::PDFContentNodeReference> paths = H::getPaths(setup.context);
    QVERIFY(setup.context->execute(F::createSetTransparency({ paths[0] }, std::nullopt, 0.5, std::nullopt)));
    setup.context->getSelection()->setNodes({ paths[0], paths[2] });
    QVERIFY(setup.settle());

    QVERIFY(setup.find<pdf::PDFComboBox>("editContentStrokeSpace")->isMixed());
    QVERIFY(setup.find<pdf::PDFDoubleSpinBox>("editContentStrokeWidth")->isMixed());
    QVERIFY(!setup.find<pdf::PDFComboBox>("editContentFillSpace")->isMixed());
    QCOMPARE(setup.find<QCheckBox>("editContentFillCheck")->checkState(), Qt::PartiallyChecked);
    QVERIFY(setup.find<pdf::PDFDoubleSpinBox>("editContentFillOpacity")->isMixed());
    QVERIFY(!setup.find<pdf::PDFDoubleSpinBox>("editContentStrokeOpacity")->isMixed());
    QCOMPARE(setup.find<pdf::PDFDoubleSpinBox>("editContentStrokeOpacity")->value(), 100.0);
    QCOMPARE(setup.stack->getCard(QLatin1String(CardIds::TRANSPARENCY))->getSections().front()->getSummaryText(), QStringLiteral("Opacity Mixed · blend mode Normal"));

    // The same values are not mixed: A alone
    setup.context->getSelection()->setNode(paths[0]);
    QVERIFY(setup.settle());
    QVERIFY(!setup.find<pdf::PDFDoubleSpinBox>("editContentFillOpacity")->isMixed());
    QCOMPARE(setup.find<pdf::PDFDoubleSpinBox>("editContentFillOpacity")->value(), 50.0);
    QCOMPARE(setup.stack->getCard(QLatin1String(CardIds::TRANSPARENCY))->getSections().front()->getSummaryText(), QStringLiteral("Opacity 50 % · blend mode Normal"));
}

void AppToolContentCardsTest::colorEntryIsOneCommand()
{
    // G of the fill of A entered as 1: the fill is DeviceRGB (0 1 1) - one command, one undo step; Undo returns (0 0 1)
    using H = ContentCardsHelper;
    ContentCardsSetup setup(H::createDocument());
    QVERIFY(setup.open());
    const std::vector<pdf::PDFContentNodeReference> paths = H::getPaths(setup.context);
    setup.context->getSelection()->setNode(paths[0]);
    QVERIFY(setup.settle());

    const size_t count = H::getUndoCount(setup.context);
    H::enter(setup.find<pdf::PDFDoubleSpinBox>("editContentFillValue1"), QStringLiteral("1"));
    QCOMPARE(H::getUndoCount(setup.context), count + 1);
    pdf::PDFContentDisplayState state = H::getState(setup.context, paths[0]);
    QVERIFY(state.fillColor.isOverridden);
    QCOMPARE(state.fillColor.value.deviceSpace, QByteArray("DeviceRGB"));
    QCOMPARE(state.fillColor.value.components, std::vector<pdf::PDFReal>({ 0.0, 1.0, 1.0 }));
    QCOMPARE(state.strokeColor.isOverridden, false);

    setup.context->getSession()->undo();
    state = H::getState(setup.context, paths[0]);
    QVERIFY(!state.fillColor.isOverridden);
    QCOMPARE(state.fillColor.color, QColor(0, 0, 255));
}

void AppToolContentCardsTest::spaceAndSwatches()
{
    // The swatches are the device colors of the content in the order of their first use (QE.9): A fill (0 0 1), A
    // stroke (1 0 0), B fill CMYK (0 0 0 1), C stroke gray 0.5 - four swatches; the swatch of the color of the object is
    // selected. Choosing DeviceGray for the fill of A converts its display color: 0.3 x 0 + 0.59 x 0 + 0.11 x 1 = 0.11
    // (ISO 32000-2, 10.4.2.2)
    using H = ContentCardsHelper;
    ContentCardsSetup setup(H::createDocument());
    QVERIFY(setup.open());
    const std::vector<pdf::PDFContentNodeReference> paths = H::getPaths(setup.context);
    setup.context->getSelection()->setNode(paths[0]);
    QVERIFY(setup.settle());

    int shown = 0;
    for (int i = 0; i < 7; ++i)
    {
        pdf::PDFColorSwatch* swatch = setup.find<pdf::PDFColorSwatch>(QStringLiteral("editContentFillSwatch%1").arg(i).toLatin1().constData());
        QVERIFY(swatch);
        shown += swatch->isHidden() ? 0 : 1;
    }
    QCOMPARE(shown, 4);
    QCOMPARE(setup.find<pdf::PDFColorSwatch>("editContentFillSwatch0")->getColor(), QColor(0, 0, 255));
    QVERIFY(setup.find<pdf::PDFColorSwatch>("editContentFillSwatch0")->isSelected());
    QVERIFY(setup.find<pdf::PDFColorSwatch>("editContentStrokeSwatch1")->isSelected());

    // The swatch of B's black: the fill of A becomes DeviceCMYK (0 0 0 1) exactly
    setup.find<pdf::PDFColorSwatch>("editContentFillSwatch2")->click();
    pdf::PDFContentDisplayState state = H::getState(setup.context, paths[0]);
    QCOMPARE(state.fillColor.value.deviceSpace, QByteArray("DeviceCMYK"));
    QCOMPARE(state.fillColor.value.components, std::vector<pdf::PDFReal>({ 0.0, 0.0, 0.0, 1.0 }));
    setup.context->getSession()->undo();
    QVERIFY(setup.settle());

    pdf::PDFComboBox* fillSpace = setup.find<pdf::PDFComboBox>("editContentFillSpace");
    Q_EMIT fillSpace->activated(fillSpace->findData(QByteArray("DeviceGray")));
    state = H::getState(setup.context, paths[0]);
    QCOMPARE(state.fillColor.value.deviceSpace, QByteArray("DeviceGray"));
    QCOMPARE(state.fillColor.value.components.size(), size_t(1));
    QVERIFY(qAbs(state.fillColor.value.components.front() - 0.11) < 1e-9);
}

void AppToolContentCardsTest::widthOfTwoObjects()
{
    // A width typed for A and C (Mixed 2 / 1) is set for both of them by one command
    using H = ContentCardsHelper;
    ContentCardsSetup setup(H::createDocument());
    QVERIFY(setup.open());
    const std::vector<pdf::PDFContentNodeReference> paths = H::getPaths(setup.context);
    setup.context->getSelection()->setNodes({ paths[0], paths[2] });
    QVERIFY(setup.settle());

    const size_t count = H::getUndoCount(setup.context);
    pdf::PDFDoubleSpinBox* width = setup.find<pdf::PDFDoubleSpinBox>("editContentStrokeWidth");
    QVERIFY(width->isMixed());
    H::enter(width, QStringLiteral("3"));
    QCOMPARE(H::getUndoCount(setup.context), count + 1);
    QCOMPARE(H::getState(setup.context, paths[0]).lineWidth, 3.0);
    QCOMPARE(H::getState(setup.context, paths[2]).lineWidth, 3.0);
    QVERIFY(setup.settle());
    QVERIFY(!width->hasFocus() || !width->isMixed());
    width->clearFocus();
    setup.context->getSelection()->setNodes({ paths[2], paths[0] });
    QVERIFY(setup.settle());
    QVERIFY(!width->isMixed());
    QCOMPARE(width->value(), 3.0);
}

void AppToolContentCardsTest::transparency()
{
    // Fill opacity 40 % = /ca 0.4; the blend mode Multiply (ISO 32000-2, Table 134); the summary of the collapsed card
    using H = ContentCardsHelper;
    ContentCardsSetup setup(H::createDocument());
    QVERIFY(setup.open());
    const std::vector<pdf::PDFContentNodeReference> paths = H::getPaths(setup.context);
    setup.context->getSelection()->setNode(paths[1]);
    QVERIFY(setup.settle());
    QCOMPARE(setup.stack->getCard(QLatin1String(CardIds::TRANSPARENCY))->getSections().front()->getSummaryText(), QStringLiteral("Opacity 100 % · blend mode Normal"));

    H::enter(setup.find<pdf::PDFDoubleSpinBox>("editContentFillOpacity"), QStringLiteral("40"));
    QCOMPARE(H::getState(setup.context, paths[1]).alphaFilling, 0.4);
    QCOMPARE(H::getState(setup.context, paths[1]).alphaStroking, 1.0);

    pdf::PDFComboBox* blend = setup.find<pdf::PDFComboBox>("editContentBlendMode");
    Q_EMIT blend->activated(1);
    QCOMPARE(H::getState(setup.context, paths[1]).blendMode, pdf::BlendMode::Multiply);
    setup.find<pdf::PDFDoubleSpinBox>("editContentFillOpacity")->clearFocus();
    QVERIFY(setup.settle());
    QCOMPARE(setup.stack->getCard(QLatin1String(CardIds::TRANSPARENCY))->getSections().front()->getSummaryText(), QStringLiteral("Opacity 40 % · blend mode Multiply"));
}

void AppToolContentCardsTest::pendingValueCommittedBeforeUndo()
{
    // zadani_gui.md 4.19: before Undo (and a page switch, Apply, a change of the tool) the GUI commits the value typed
    // and not confirmed - as its own command
    using H = ContentCardsHelper;
    ContentCardsSetup setup(H::createDocument());
    QVERIFY(setup.open());
    const std::vector<pdf::PDFContentNodeReference> paths = H::getPaths(setup.context);
    setup.context->getSelection()->setNode(paths[0]);
    QVERIFY(setup.settle());

    const size_t count = H::getUndoCount(setup.context);
    H::type(setup.find<pdf::PDFDoubleSpinBox>("editContentStrokeWidth"), QStringLiteral("5"));
    QCOMPARE(H::getUndoCount(setup.context), count);
    setup.context->commitPendingEdits();
    QCOMPARE(H::getUndoCount(setup.context), count + 1);
    QCOMPARE(H::getState(setup.context, paths[0]).lineWidth, 5.0);

    // Undo of that step returns the width of the content
    setup.context->getSession()->undo();
    QCOMPARE(H::getState(setup.context, paths[0]).lineWidth, 2.0);
}

void AppToolContentCardsTest::pendingValueForgottenAtRetire()
{
    // A value typed and not confirmed is forgotten, when the card ends with the tool (R3-9: its own retire hook)
    using H = ContentCardsHelper;
    ContentCardsSetup setup(H::createDocument());
    QVERIFY(setup.open());
    const std::vector<pdf::PDFContentNodeReference> paths = H::getPaths(setup.context);
    setup.context->getSelection()->setNode(paths[0]);
    QVERIFY(setup.settle());

    const size_t count = H::getUndoCount(setup.context);
    pdf::PDFDoubleSpinBox* width = setup.find<pdf::PDFDoubleSpinBox>("editContentStrokeWidth");
    H::type(width, QStringLiteral("7"));
    pdf::PDFDeferredDeletion::retire(setup.stack.data());
    QCoreApplication::processEvents();
    setup.context->commitPendingEdits();
    QCOMPARE(H::getUndoCount(setup.context), count);
    QCOMPARE(H::getState(setup.context, paths[0]).lineWidth, 2.0);
}

void AppToolContentCardsTest::positionAndSize()
{
    // B is "100 100 20 20 re f" on the page [0 0 200 200]: X 100, Y 100, W 20, H 20 pt. X = 50 moves it by -50; W = 40
    // doubles the width about the upper left corner (the left edge 50 and the upper edge 120 stay)
    using H = ContentCardsHelper;
    ContentCardsSetup setup(H::createDocument());
    QVERIFY(setup.open());
    const std::vector<pdf::PDFContentNodeReference> paths = H::getPaths(setup.context);
    setup.context->getSelection()->setNode(paths[1]);
    QVERIFY(setup.settle());

    pdf::PDFAppEditContentCard* card = setup.stack->getCard(QLatin1String(CardIds::POSITION_AND_SIZE));
    QCOMPARE(card->getSections().front()->getSummaryText(), QStringLiteral("X 100 · Y 100 · W 20 · H 20 pt"));
    pdf::PDFDoubleSpinBox* x = setup.find<pdf::PDFDoubleSpinBox>("editContentX");
    pdf::PDFDoubleSpinBox* w = setup.find<pdf::PDFDoubleSpinBox>("editContentW");
    pdf::PDFDoubleSpinBox* h = setup.find<pdf::PDFDoubleSpinBox>("editContentH");
    QCOMPARE(x->value(), 100.0);
    QCOMPARE(w->value(), 20.0);

    H::enter(x, QStringLiteral("50"));
    QVERIFY(H::isSame(H::box(setup.context, paths[1]), QRectF(50, 100, 20, 20)));
    x->clearFocus();
    QVERIFY(setup.settle());

    H::enter(w, QStringLiteral("40"));
    QVERIFY(H::isSame(H::box(setup.context, paths[1]), QRectF(50, 100, 40, 20)));
    w->clearFocus();
    QVERIFY(setup.settle());

    // H = 10 keeps the upper edge 120: the box is (50 110) - (90 120)
    H::enter(h, QStringLiteral("10"));
    QVERIFY(H::isSame(H::box(setup.context, paths[1]), QRectF(50, 110, 40, 10)));
}

void AppToolContentCardsTest::rotationAndFlip()
{
    // Rotation 90 of A (box (10 10) - (60 40), center (35 25)) about its center: the box (20 0) - (50 50); the field
    // shows 90 afterwards (the matrix of the object). Flip horizontally of B about the center of its box keeps the box.
    using H = ContentCardsHelper;
    ContentCardsSetup setup(H::createDocument());
    QVERIFY(setup.open());
    const std::vector<pdf::PDFContentNodeReference> paths = H::getPaths(setup.context);
    setup.context->getSelection()->setNode(paths[0]);
    QVERIFY(setup.settle());

    pdf::PDFDoubleSpinBox* rotation = setup.find<pdf::PDFDoubleSpinBox>("editContentRotation");
    QCOMPARE(rotation->value(), 0.0);
    H::enter(rotation, QStringLiteral("90"));
    QVERIFY(H::isSame(H::box(setup.context, paths[0]), QRectF(20, 0, 30, 50)));
    rotation->clearFocus();
    QVERIFY(setup.settle());
    QVERIFY(qAbs(rotation->value() - 90.0) < 1e-6);
    QCOMPARE(setup.find<pdf::PDFDoubleSpinBox>("editContentScaleX")->value(), 1.0);

    setup.context->getSelection()->setNode(paths[1]);
    QVERIFY(setup.settle());
    setup.find<QToolButton>("editContentFlipHorizontally")->click();
    const pdf::PDFContentDisplayState state = H::getState(setup.context, paths[1]);
    QCOMPARE(state.matrix.m11(), -1.0);
    QVERIFY(H::isSame(H::box(setup.context, paths[1]), QRectF(100, 100, 20, 20)));
}

void AppToolContentCardsTest::arrangeAlignToTheKey()
{
    // Align left of A and B with B as the key object (selected last): A moves to the left edge of B, 100
    // (zadani_jadro.md 4.13: to the key object); the buttons need two objects, Center on page one
    using H = ContentCardsHelper;
    ContentCardsSetup setup(H::createDocument());
    QVERIFY(setup.open());
    const std::vector<pdf::PDFContentNodeReference> paths = H::getPaths(setup.context);

    setup.context->getSelection()->setNode(paths[0]);
    QVERIFY(setup.settle());
    QToolButton* alignLeft = setup.find<QToolButton>("editContentArrange_obj-align-left");
    QToolButton* center = setup.find<QToolButton>("editContentArrange_center-both");
    QVERIFY(alignLeft && center);
    QVERIFY(!alignLeft->isEnabled());
    QVERIFY(center->isEnabled());
    QVERIFY(setup.stack->getCard(QLatin1String(CardIds::ARRANGE))->getSections().front()->isExpandable());

    setup.context->getSelection()->setNodes({ paths[0], paths[1] });
    QVERIFY(setup.settle());
    alignLeft = setup.find<QToolButton>("editContentArrange_obj-align-left");
    QVERIFY(alignLeft && alignLeft->isEnabled());
    QVERIFY(!setup.stack->getCard(QLatin1String(CardIds::ARRANGE))->getSections().front()->isExpandable());
    QCOMPARE(setup.find<pdf::PDFLabel>("editContentArrangeKey")->text(), QStringLiteral("2 object(s) selected · aligned to the key object, the one with the thicker frame"));

    const size_t count = H::getUndoCount(setup.context);
    alignLeft->click();
    QCOMPARE(H::getUndoCount(setup.context), count + 1);
    QVERIFY(H::isSame(H::box(setup.context, paths[0]), QRectF(100, 10, 50, 30)));
    QVERIFY(H::isSame(H::box(setup.context, paths[1]), QRectF(100, 100, 20, 20)));

    // Center on page of A alone: the center of its box at (100 100) of the page [0 0 200 200]
    setup.context->getSelection()->setNode(paths[0]);
    QVERIFY(setup.settle());
    setup.find<QToolButton>("editContentArrange_center-both")->click();
    QVERIFY(H::isSame(H::box(setup.context, paths[0]), QRectF(75, 85, 50, 30)));
}

void AppToolContentCardsTest::arrangeActionsOfTheRegistry()
{
    // Order and Group are actions of P12: a button exists only for a registered action ("hidden, not disabled") and
    // starts it from the event loop (PDFAppEditContentContext::triggerAction)
    using H = ContentCardsHelper;
    int count = 0;
    ContentCardsSetup setup(H::createDocument(), &count);
    QVERIFY(setup.open());
    const std::vector<pdf::PDFContentNodeReference> paths = H::getPaths(setup.context);
    setup.context->getSelection()->setNodes({ paths[0], paths[1] });
    QVERIFY(setup.settle());

    QToolButton* front = setup.find<QToolButton>("editContentArrange_bring-front");
    QToolButton* group = setup.find<QToolButton>("editContentArrange_group");
    QVERIFY(front && group);
    QVERIFY(!front->isHidden());
    QVERIFY(group->isHidden());
    front->click();
    QCOMPARE(count, 0);
    QTRY_COMPARE(count, 1);
}

void AppToolContentCardsTest::lockedObjectUnlock()
{
    // -locked-click: the card Locked object is requested with the node; Unlock unlocks it (the session, no command)
    // and the card goes with the next rebuild
    using H = ContentCardsHelper;
    ContentCardsSetup setup(H::createDocument());
    QVERIFY(setup.open());
    const std::vector<pdf::PDFContentNodeReference> paths = H::getPaths(setup.context);
    setup.context->getSession()->setNodeLocked(paths[2], true);
    setup.context->setLockedClickNode(paths[2]);
    setup.context->setCardRequested(QLatin1String(CardIds::LOCKED_OBJECT), true);
    QVERIFY(setup.settle());
    QVERIFY(setup.stack->getCardIds().contains(QLatin1String(CardIds::LOCKED_OBJECT)));
    QVERIFY(H::isInFixedOrder(setup.stack->getCardIds()));
    QVERIFY(setup.find<pdf::PDFLabel>("editContentLockedText")->text().startsWith(QStringLiteral("Path is locked.")));
    QCOMPARE(setup.find<pdf::PDFLabel>("editContentSelectionNote")->text().left(26), QStringLiteral("The clicked object is lock"));

    const size_t count = H::getUndoCount(setup.context);
    QPointer<QPushButton> unlock = setup.find<QPushButton>("editContentUnlock");
    unlock->click();
    QVERIFY(unlock);
    QVERIFY(!setup.context->getSession()->isNodeLocked(paths[2]));
    QCOMPARE(H::getUndoCount(setup.context), count);
    QVERIFY(setup.settle());
    QVERIFY(!setup.stack->getCardIds().contains(QLatin1String(CardIds::LOCKED_OBJECT)));
}

void AppToolContentCardsTest::annotationsAndPreview()
{
    // The check boxes are display options (no command): Show as a dimmed underlay, Simulate overprint, Show
    // annotations and fields, Paper color
    using H = ContentCardsHelper;
    using O = pdf::PDFAppEditContentOptions;
    ContentCardsSetup setup(H::createDocument(true));
    QVERIFY(setup.open());

    QCheckBox* underlay = setup.find<QCheckBox>("editContentAnnotationsUnderlay");
    QVERIFY(underlay && underlay->isChecked());
    underlay->click();
    QVERIFY(!setup.context->getOptions()->getBool(QLatin1String(O::SHOW_ANNOTATIONS)));
    QVERIFY(setup.settle());
    QVERIFY(setup.stack->getCardIds().contains(QLatin1String(CardIds::ANNOTATIONS)));

    setup.context->setPreviewActive(true);
    QVERIFY(setup.settle());
    QCheckBox* overprint = setup.find<QCheckBox>("editContentPreviewOverprint");
    pdf::PDFComboBox* paper = setup.find<pdf::PDFComboBox>("editContentPreviewPaper");
    QVERIFY(overprint && overprint->isChecked() && paper);
    QCOMPARE(paper->currentText(), QStringLiteral("White"));
    overprint->click();
    QVERIFY(!setup.context->getOptions()->getBool(QLatin1String(O::PREVIEW_OVERPRINT)));
    Q_EMIT paper->activated(1);
    QCOMPARE(setup.context->getOptions()->getValue(QLatin1String(O::PREVIEW_PAPER_COLOR)).toString(), QStringLiteral("#ebebeb"));
    QCOMPARE(H::getUndoCount(setup.context), size_t(0));
}

void AppToolContentCardsTest::sectionStateSurvivesRebuild()
{
    // The expanded state lives in the context (zadani_gui.md 4.26: not stored): a new stack (a change of the language)
    // shows the section expanded again
    using H = ContentCardsHelper;
    ContentCardsSetup setup(H::createDocument());
    QVERIFY(setup.open());
    const std::vector<pdf::PDFContentNodeReference> paths = H::getPaths(setup.context);
    setup.context->getSelection()->setNode(paths[0]);
    QVERIFY(setup.settle());

    pdf::PDFExpanderCard* section = setup.stack->getCard(QLatin1String(CardIds::TRANSPARENCY))->getSections().front();
    QVERIFY(!section->isExpanded());
    section->setExpanded(true);
    QVERIFY(setup.context->isSectionExpanded(QLatin1String(CardIds::TRANSPARENCY)));

    pdf::PDFDeferredDeletion::retire(setup.stack.data());
    setup.stack = new Stack(setup.context, nullptr);
    QVERIFY(setup.stack->getCard(QLatin1String(CardIds::TRANSPARENCY))->getSections().front()->isExpanded());
}

void AppToolContentCardsTest::retireInsideOfAClick()
{
    // A slot of the click of a card button may close the tool: the stack is retired inside of the emission; the card
    // survives the end of the click (deleted later) and nothing reaches a retired card afterwards
    using H = ContentCardsHelper;
    ContentCardsSetup setup(H::createDocument());
    QVERIFY(setup.open());
    const std::vector<pdf::PDFContentNodeReference> paths = H::getPaths(setup.context);
    setup.context->getSelection()->setNodes({ paths[0], paths[1] });
    QVERIFY(setup.settle());

    QPointer<QToolButton> alignLeft = setup.find<QToolButton>("editContentArrange_obj-align-left");
    QPointer<Stack> stack = setup.stack;
    connect(setup.context->getSession(), &pdf::PDFContentEditSession::modelChanged, stack.data(), [stack]() { pdf::PDFDeferredDeletion::retire(stack.data()); });
    alignLeft->click();
    QVERIFY(alignLeft);
    QVERIFY(pdf::PDFDeferredDeletion::isRetired(alignLeft));
    setup.context->getSelection()->clear();
    QCoreApplication::processEvents();
    pdf::PDFDeferredDeletion::flush();
    QVERIFY(!stack);
}

QTEST_MAIN(AppToolContentCardsTest)

#include "tst_apptoolcontentcardstest.moc"
