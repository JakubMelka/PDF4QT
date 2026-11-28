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

    /// A page [0 0 200 200] of the content, without resources or with the fonts of ContentTestDocuments (/F1 ...), and
    /// the /Rotate of the page (0 = none)
    static pdf::PDFDocumentPointer createContentDocument(const QByteArray& content, bool hasFonts = false, int rotate = 0)
    {
        using D = pdftest::ContentTestDocuments;
        D::PageFinisher finisher;
        if (rotate != 0)
        {
            finisher = [rotate](pdf::PDFDocumentBuilder* builder, pdf::PDFObjectReference page)
            {
                builder->mergeTo(page, D::createDictionaryObject({ { "Rotate", pdf::PDFObject::createInteger(rotate) } }));
            };
        }
        D::ResourcesFactory resources = &D::createNoResources;
        if (hasFonts)
        {
            resources = &D::createFontResources;
        }
        return pdf::PDFDocumentPointer(new pdf::PDFDocument(D::createDocumentFromObjects([&content](pdf::PDFDocumentBuilder* builder)
        {
            return D::addStreamObject(builder, D::DictionaryEntries(), content);
        }, resources, finisher)));
    }

    /// The nodes of the kind in the order of the content, also inside of groups and texts
    static std::vector<pdf::PDFContentNodeReference> getNodes(const Context* context, pdf::PDFContentNodeKind kind)
    {
        std::vector<pdf::PDFContentNodeReference> nodes;
        const pdf::PDFContentContainerPointer container = context->getContainer();
        std::vector<const pdf::PDFContentNode*> stack = { container->getRoot().get() };
        while (!stack.empty())
        {
            const pdf::PDFContentNode* node = stack.back();
            stack.pop_back();
            if (node->getKind() == kind)
            {
                nodes.push_back(pdf::PDFContentNodeReference{ container->getKey(), node->getId() });
            }
            for (auto it = node->getChildren().crbegin(); it != node->getChildren().crend(); ++it)
            {
                stack.push_back(it->get());
            }
        }
        return nodes;
    }

    /// The paint of a path of the current container
    static pdf::PDFContentPathPaint getPaint(const Context* context, const pdf::PDFContentNodeReference& node)
    {
        const pdf::PDFContentNodePointer path = context->getContainer()->getNode(node.node);
        return path ? path->getData().paint : pdf::PDFContentPathPaint();
    }

    /// The color of the fill of the node (the override of a command, else the color of the content)
    static std::pair<QByteArray, std::vector<pdf::PDFReal>> getFill(const Context* context, const pdf::PDFContentNodeReference& node)
    {
        const pdf::PDFContentDisplayState state = getState(context, node);
        if (state.fillColor.isOverridden)
        {
            return { state.fillColor.value.deviceSpace, state.fillColor.value.components };
        }
        return { QByteArray("content"), { } };
    }

    static bool isNear(const std::vector<pdf::PDFReal>& actual, const std::vector<pdf::PDFReal>& expected)
    {
        bool isSame = actual.size() == expected.size();
        for (size_t i = 0; isSame && i < actual.size(); ++i)
        {
            isSame = qAbs(actual[i] - expected[i]) < 1e-9;
        }
        if (!isSame)
        {
            qWarning() << "components" << actual << "expected" << expected;
        }
        return isSame;
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

    // Fix package FX3B (pe1_fixes.md FX3 items 1, 8, 9; review R7, R5-6)
    void paintSwitchesOfPaths();
    void paintSwitchOfTextRefused();
    void commandsTakeTheWholeSelection();
    void spaceKeepsTheColorOfEachObject();
    void lockedObjectsLeftAlone();
    void rotationOfTextIncludesTextMatrix();
    void lineWidthInTheSpaceOfThePage();
    void displayCardsStayUsable();
    void rotatedPageDirections();
    void selectionCountsAsTheTree();
    void reloadedSessionFollowed();
    void keyObjectAnywhere();
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

    // The frame -annotations-underlay: Annotations and fields is between Selection and Arrange
    QCOMPARE(setup.stack->getCardIds(), QStringList({ CardIds::SELECTION, CardIds::ANNOTATIONS, CardIds::ARRANGE }));
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

    // Changed by FX3B (FX3 item 1: the fields of a switched-off paint are disabled, not hidden; was: no Width and no
    // expanded part as -color-cmyk shows them): Width and the expanded part are shown and disabled
    QVERIFY(!width->parentWidget()->isHidden());
    QVERIFY(!width->parentWidget()->isEnabled());
    QVERIFY(!setup.find<pdf::PDFComboBox>("editContentCap")->isEnabled());
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

void AppToolContentCardsTest::paintSwitchesOfPaths()
{
    // FX3 item 1 (zadani_gui.md 4.19 "for each color: a check box"; set_appearance with fill / stroke): A "re B" fills
    // and strokes - a click on Fill switches its fill off (S, ISO 32000-2 Table 59), one undo step; the fields of the
    // switched-off fill stay shown and are disabled. A and C (C strokes only): Fill is partly checked, a click fills both.
    using H = ContentCardsHelper;
    ContentCardsSetup setup(H::createDocument());
    QVERIFY(setup.open());
    const std::vector<pdf::PDFContentNodeReference> paths = H::getPaths(setup.context);
    setup.context->getSelection()->setNode(paths[0]);
    QVERIFY(setup.settle());

    QCheckBox* fill = setup.find<QCheckBox>("editContentFillCheck");
    QVERIFY(fill && fill->isEnabled());
    QCOMPARE(fill->checkState(), Qt::Checked);
    const size_t count = H::getUndoCount(setup.context);
    fill->click();
    QCOMPARE(H::getUndoCount(setup.context), count + 1);
    QVERIFY(!H::getPaint(setup.context, paths[0]).fill);
    QVERIFY(H::getPaint(setup.context, paths[0]).stroke);
    QVERIFY(setup.settle());
    QCOMPARE(fill->checkState(), Qt::Unchecked);
    QVERIFY(!setup.find<pdf::PDFComboBox>("editContentFillSpace")->isEnabled());
    QVERIFY(!setup.find<QWidget>("editContentFillSwatches")->isHidden());
    QVERIFY(!setup.find<QWidget>("editContentFillSwatches")->isEnabled());
    QVERIFY(setup.find<pdf::PDFDoubleSpinBox>("editContentFillValue2")->isVisibleTo(setup.stack));
    QCOMPARE(setup.find<pdf::PDFDoubleSpinBox>("editContentFillValue2")->value(), 1.0);     // the color a switch on uses: 0 0 1 rg
    QVERIFY(!setup.find<pdf::PDFDoubleSpinBox>("editContentFillValue2")->isEnabled());

    // On again: B. The second switch of the same objects merges with the first (PDFContentCommand::mergeWith: the same
    // nodes and fields, the last value wins - pe1fx1_state.md), so it is still one undo step; Undo returns the B of the
    // content
    fill->click();
    QVERIFY(H::getPaint(setup.context, paths[0]).fill && H::getPaint(setup.context, paths[0]).stroke);
    QCOMPARE(H::getUndoCount(setup.context), count + 1);
    setup.context->getSession()->undo();
    QVERIFY(H::getPaint(setup.context, paths[0]).fill && H::getPaint(setup.context, paths[0]).stroke);

    // A and C: partly checked; a click switches the fill on for both (C keeps its stroke)
    setup.context->getSelection()->setNodes({ paths[0], paths[2] });
    QVERIFY(setup.settle());
    QCOMPARE(fill->checkState(), Qt::PartiallyChecked);
    fill->click();
    QVERIFY(H::getPaint(setup.context, paths[0]).fill);
    QVERIFY(H::getPaint(setup.context, paths[2]).fill && H::getPaint(setup.context, paths[2]).stroke);
    QVERIFY(setup.settle());
    QCOMPARE(fill->checkState(), Qt::Checked);

    // The stroke of B (fill only) is off: Width and the expanded part are shown and disabled, not hidden
    setup.context->getSelection()->setNode(paths[1]);
    QVERIFY(setup.settle());
    pdf::PDFDoubleSpinBox* width = setup.find<pdf::PDFDoubleSpinBox>("editContentStrokeWidth");
    QVERIFY(!width->parentWidget()->isHidden());
    QVERIFY(!width->isEnabled());
    QVERIFY(!setup.find<pdf::PDFComboBox>("editContentCap")->isEnabled());
    setup.find<QCheckBox>("editContentStrokeCheck")->click();
    QVERIFY(H::getPaint(setup.context, paths[1]).stroke && H::getPaint(setup.context, paths[1]).fill);
    QVERIFY(setup.settle());
    QVERIFY(width->isEnabled());
}

void AppToolContentCardsTest::paintSwitchOfTextRefused()
{
    // A text object of two shows of the modes 0 (fill) and 1 (stroke, ISO 32000-2 Table 104): Fill is partly checked; a
    // click would give the shows the modes 0 and 2 - one text cannot have two modes by one switch, the core refuses
    // (pe1fx1_state.md); the refusal is told by the context (commandRefused) and the check box shows the state again
    using H = ContentCardsHelper;
    ContentCardsSetup setup(H::createContentDocument(QByteArrayLiteral("BT /F1 12 Tf 20 100 Td (A) Tj 1 Tr (B) Tj ET\n"), true));
    QVERIFY(setup.open());
    const std::vector<pdf::PDFContentNodeReference> texts = H::getNodes(setup.context, pdf::PDFContentNodeKind::TextObject);
    QCOMPARE(texts.size(), size_t(1));
    setup.context->getSelection()->setNode(texts[0]);
    QVERIFY(setup.settle());

    QCheckBox* fill = setup.find<QCheckBox>("editContentFillCheck");
    QVERIFY(fill);
    QCOMPARE(fill->checkState(), Qt::PartiallyChecked);
    QSignalSpy refused(setup.context.data(), &Context::commandRefused);
    const size_t count = H::getUndoCount(setup.context);
    fill->click();
    QCOMPARE(refused.count(), 1);
    QCOMPARE(H::getUndoCount(setup.context), count);
    QCOMPARE(fill->checkState(), Qt::PartiallyChecked);
}

void AppToolContentCardsTest::commandsTakeTheWholeSelection()
{
    // R7-1: MAXIMUM_INSPECTED_NODES + 1 paths selected (the last one, the key object, at x = 5, the others at x >= 20):
    // a cap of what the cards read never cuts what a command changes - the opacity, Align left (to the key object) and X
    // reach the last path too
    using H = ContentCardsHelper;
    using T = pdf::PDFAppEditContentCardTools;
    constexpr int COUNT = int(T::MAXIMUM_INSPECTED_NODES) + 1;
    QByteArray content("0 0 1 rg\n");
    for (int i = 0; i < COUNT - 1; ++i)
    {
        content += QByteArray::number(20 + i % 100) + ' ' + QByteArray::number(20 + i / 100) + " 1 1 re f\n";
    }
    content += "5 150 1 1 re f\n";
    ContentCardsSetup setup(H::createContentDocument(content));
    QVERIFY(setup.open());
    const std::vector<pdf::PDFContentNodeReference> paths = H::getPaths(setup.context);
    QCOMPARE(paths.size(), size_t(COUNT));
    setup.context->getSelection()->setNodes(paths);
    QCOMPARE(setup.context->getSelection()->getKeyNode(), paths.back());
    QVERIFY(setup.settle());

    H::enter(setup.find<pdf::PDFDoubleSpinBox>("editContentFillOpacity"), QStringLiteral("40"));
    QCOMPARE(H::getState(setup.context, paths.front()).alphaFilling, 0.4);
    QCOMPARE(H::getState(setup.context, paths.back()).alphaFilling, 0.4);
    setup.find<pdf::PDFDoubleSpinBox>("editContentFillOpacity")->clearFocus();
    QVERIFY(setup.settle());

    // Align left to the key object, the last one: every path has its left edge at 5, the key does not move
    QToolButton* alignLeft = setup.find<QToolButton>("editContentArrange_obj-align-left");
    QVERIFY(alignLeft && alignLeft->isEnabled());
    alignLeft->click();
    QVERIFY(H::isSame(H::box(setup.context, paths.front()), QRectF(5, 20, 1, 1)));
    QVERIFY(H::isSame(H::box(setup.context, paths[paths.size() - 2]), QRectF(5, 119, 1, 1)));
    QVERIFY(H::isSame(H::box(setup.context, paths.back()), QRectF(5, 150, 1, 1)));
    QVERIFY(setup.settle());

    // X = 50: the box of the whole selection (left 5) moves by 45 - the last path too
    pdf::PDFDoubleSpinBox* x = setup.find<pdf::PDFDoubleSpinBox>("editContentX");
    QCOMPARE(x->value(), 5.0);
    H::enter(x, QStringLiteral("50"));
    QVERIFY(H::isSame(H::box(setup.context, paths.front()), QRectF(50, 20, 1, 1)));
    QVERIFY(H::isSame(H::box(setup.context, paths.back()), QRectF(50, 150, 1, 1)));
}

void AppToolContentCardsTest::spaceKeepsTheColorOfEachObject()
{
    // R7-2: a group of a red and a blue path, a CMYK path (0.1 0.2 0.3 0.4) and a gray path 0.5. DeviceCMYK for the fill
    // converts each object by ISO 32000-2, 10.4.2 from its own components: red (1 0 0) -> (0 1 1 0), blue (0 0 1) -> (1
    // 1 0 0), gray 0.5 -> (0 0 0 0.5); the CMYK path stays as it is. Then DeviceRGB: (0 1 1 0) -> r = 1 - min(1, C + K)
    // ... = (1 0 0), (0.1 0.2 0.3 0.4) -> (0.5 0.4 0.3), (0 0 0 0.5) -> (0.5 0.5 0.5). Choosing the space every object has
    // already changes nothing.
    using H = ContentCardsHelper;
    ContentCardsSetup setup(H::createContentDocument(QByteArrayLiteral("q 1 0 0 rg 10 10 20 20 re f 0 0 1 rg 50 10 20 20 re f Q\n"
                                                                       "0.1 0.2 0.3 0.4 k 100 100 20 20 re f\n"
                                                                       "0.5 g 120 20 10 10 re f\n")));
    QVERIFY(setup.open());
    const std::vector<pdf::PDFContentNodeReference> paths = H::getNodes(setup.context, pdf::PDFContentNodeKind::Path);
    const std::vector<pdf::PDFContentNodeReference> groups = H::getNodes(setup.context, pdf::PDFContentNodeKind::Group);
    QCOMPARE(paths.size(), size_t(4));
    QCOMPARE(groups.size(), size_t(1));
    setup.context->getSelection()->setNodes({ groups[0], paths[2], paths[3] });
    QVERIFY(setup.settle());

    pdf::PDFComboBox* space = setup.find<pdf::PDFComboBox>("editContentFillSpace");
    const size_t count = H::getUndoCount(setup.context);
    Q_EMIT space->activated(space->findData(QByteArray("DeviceCMYK")));
    QCOMPARE(H::getUndoCount(setup.context), count + 1);
    QCOMPARE(H::getFill(setup.context, paths[0]).first, QByteArray("DeviceCMYK"));
    QVERIFY(H::isNear(H::getFill(setup.context, paths[0]).second, { 0.0, 1.0, 1.0, 0.0 }));
    QVERIFY(H::isNear(H::getFill(setup.context, paths[1]).second, { 1.0, 1.0, 0.0, 0.0 }));
    QCOMPARE(H::getFill(setup.context, paths[2]).first, QByteArray("content"));
    QVERIFY(H::isNear(H::getFill(setup.context, paths[3]).second, { 0.0, 0.0, 0.0, 0.5 }));
    QVERIFY(setup.settle());

    Q_EMIT space->activated(space->findData(QByteArray("DeviceRGB")));
    QCOMPARE(H::getUndoCount(setup.context), count + 2);
    QCOMPARE(H::getFill(setup.context, paths[0]).first, QByteArray("DeviceRGB"));
    QVERIFY(H::isNear(H::getFill(setup.context, paths[0]).second, { 1.0, 0.0, 0.0 }));
    QVERIFY(H::isNear(H::getFill(setup.context, paths[1]).second, { 0.0, 0.0, 1.0 }));
    QVERIFY(H::isNear(H::getFill(setup.context, paths[2]).second, { 0.5, 0.4, 0.3 }));
    QVERIFY(H::isNear(H::getFill(setup.context, paths[3]).second, { 0.5, 0.5, 0.5 }));
    QVERIFY(setup.settle());

    // Every object is DeviceRGB now: DeviceRGB again is no command (no rounding through the display color)
    Q_EMIT space->activated(space->findData(QByteArray("DeviceRGB")));
    QCOMPARE(H::getUndoCount(setup.context), count + 2);
}

void AppToolContentCardsTest::lockedObjectsLeftAlone()
{
    // R7-4: the one rule of the lock (PDFAppEditContentContext::getChangeableSelection). A and B selected, B locked: the
    // opacity changes A only; Align needs two objects, which may be changed (disabled). B alone: the cards show it read
    // only.
    using H = ContentCardsHelper;
    ContentCardsSetup setup(H::createDocument());
    QVERIFY(setup.open());
    const std::vector<pdf::PDFContentNodeReference> paths = H::getPaths(setup.context);
    setup.context->getSession()->setNodeLocked(paths[1], true);
    setup.context->getSelection()->setNodes({ paths[0], paths[1] });
    QVERIFY(setup.settle());

    H::enter(setup.find<pdf::PDFDoubleSpinBox>("editContentFillOpacity"), QStringLiteral("40"));
    QCOMPARE(H::getState(setup.context, paths[0]).alphaFilling, 0.4);
    QCOMPARE(H::getState(setup.context, paths[1]).alphaFilling, 1.0);
    setup.find<pdf::PDFDoubleSpinBox>("editContentFillOpacity")->clearFocus();
    QVERIFY(setup.settle());
    QVERIFY(!setup.find<QToolButton>("editContentArrange_obj-align-left")->isEnabled());

    setup.find<QToolButton>("editContentFlipHorizontally")->click();
    QCOMPARE(H::getState(setup.context, paths[0]).matrix.m11(), -1.0);
    QCOMPARE(H::getState(setup.context, paths[1]).matrix.m11(), 1.0);

    setup.context->getSelection()->setNode(paths[1]);
    QVERIFY(setup.settle());
    QVERIFY(!setup.find<pdf::PDFDoubleSpinBox>("editContentFillOpacity")->isEnabled());
    QVERIFY(!setup.find<pdf::PDFDoubleSpinBox>("editContentX")->isEnabled());
    QVERIFY(!setup.find<QCheckBox>("editContentFillCheck")->isEnabled());
    QCOMPARE(setup.find<pdf::PDFDoubleSpinBox>("editContentX")->value(), 100.0);
}

void AppToolContentCardsTest::rotationOfTextIncludesTextMatrix()
{
    // R7-5: a text rotated by its text matrix "0 1 -1 0 100 50 Tm" under the identity CTM is at 90 degrees (ISO 32000-2,
    // 9.4.2: the text space -> user space by Tm); typing 0 straightens it - the text space of its first glyph becomes
    // the identity rotation (Tm x the new CTM = [1 0 0 1]). Stroke on: the rendering mode 2 (fill and stroke, Table 104).
    using H = ContentCardsHelper;
    ContentCardsSetup setup(H::createContentDocument(QByteArrayLiteral("BT /F1 12 Tf 0 1 -1 0 100 50 Tm (AB) Tj ET\n"), true));
    QVERIFY(setup.open());
    const std::vector<pdf::PDFContentNodeReference> texts = H::getNodes(setup.context, pdf::PDFContentNodeKind::TextObject);
    QCOMPARE(texts.size(), size_t(1));
    setup.context->getSelection()->setNode(texts[0]);
    QVERIFY(setup.settle());

    pdf::PDFDoubleSpinBox* rotation = setup.find<pdf::PDFDoubleSpinBox>("editContentRotation");
    QVERIFY(qAbs(rotation->value() - 90.0) < 1e-6);
    H::enter(rotation, QStringLiteral("0"));
    const pdf::PDFContentDisplayTextPointer text = pdf::PDFContentDisplay::getText(*setup.context->getContainer(), texts[0].node, pdf::PDFContentDisplayContext());
    QVERIFY(text && !text->glyphs.empty());
    const QTransform& glyph = text->glyphs.front().textMatrix;
    QVERIFY2(qAbs(glyph.m11() - 1.0) < 1e-9 && qAbs(glyph.m12()) < 1e-9 && qAbs(glyph.m21()) < 1e-9 && qAbs(glyph.m22() - 1.0) < 1e-9,
             qPrintable(QStringLiteral("%1 %2 %3 %4").arg(glyph.m11()).arg(glyph.m12()).arg(glyph.m21()).arg(glyph.m22())));
    rotation->clearFocus();
    QVERIFY(setup.settle());
    QVERIFY(qAbs(rotation->value()) < 1e-6);

    setup.find<QCheckBox>("editContentStrokeCheck")->click();
    const std::vector<pdf::PDFContentNodeReference> shows = H::getNodes(setup.context, pdf::PDFContentNodeKind::TextShow);
    QVERIFY(!shows.empty());
    QCOMPARE(H::getState(setup.context, shows.front()).textRenderingMode, pdf::TextRenderingMode::FillStroke);
}

void AppToolContentCardsTest::lineWidthInTheSpaceOfThePage()
{
    // R7-6 (QE.6: the card shows the resulting width): P "q 2 0 0 2 0 0 cm 1 w ... S Q" strokes 1 x 2 = 2 pt on the page;
    // U "q 2 0 0 1 0 0 cm ..." scales the line unevenly (Mixed and the note); Q "3 w ... S" is 3 pt. In mm: 2 pt = 2 x 25.4
    // / 72 = 0.7056 mm, shown 0.71. W = 4 pt for P sets its line width 2 in its own space; 6 pt for P and Q (2 and 3 pt,
    // Mixed) sets 3 and 6 - one undo step.
    using H = ContentCardsHelper;
    ContentCardsSetup setup(H::createContentDocument(QByteArrayLiteral("q 2 0 0 2 0 0 cm 1 w 10 10 20 20 re S Q\n"
                                                                       "q 2 0 0 1 0 0 cm 1 w 30 60 10 10 re S Q\n"
                                                                       "3 w 100 100 10 10 re S\n")));
    QVERIFY(setup.open());
    const std::vector<pdf::PDFContentNodeReference> paths = H::getNodes(setup.context, pdf::PDFContentNodeKind::Path);
    QCOMPARE(paths.size(), size_t(3));
    setup.context->getSelection()->setNode(paths[0]);
    QVERIFY(setup.settle());

    pdf::PDFDoubleSpinBox* width = setup.find<pdf::PDFDoubleSpinBox>("editContentStrokeWidth");
    QVERIFY(!width->isMixed());
    QCOMPARE(width->value(), 2.0);
    setup.context->getOptions()->setValue(QLatin1String(pdf::PDFAppEditContentOptions::UNITS), QStringLiteral("mm"));
    QVERIFY(setup.settle());
    QCOMPARE(width->value(), 0.71);
    setup.context->getOptions()->setValue(QLatin1String(pdf::PDFAppEditContentOptions::UNITS), QStringLiteral("pt"));
    QVERIFY(setup.settle());

    H::enter(width, QStringLiteral("4"));
    QCOMPARE(H::getState(setup.context, paths[0]).lineWidth, 2.0);
    width->clearFocus();
    QVERIFY(setup.settle());

    setup.context->getSelection()->setNodes({ paths[0], paths[2] });
    QVERIFY(setup.settle());
    QVERIFY(width->isMixed());
    const size_t count = H::getUndoCount(setup.context);
    H::enter(width, QStringLiteral("6"));
    QCOMPARE(H::getUndoCount(setup.context), count + 1);
    QCOMPARE(H::getState(setup.context, paths[0]).lineWidth, 3.0);
    QCOMPARE(H::getState(setup.context, paths[2]).lineWidth, 6.0);
    width->clearFocus();

    setup.context->getSelection()->setNode(paths[1]);
    QVERIFY(setup.settle());
    QVERIFY(width->isMixed());
    QVERIFY(setup.find<pdf::PDFLabel>("editContentFillAndStrokeNote")->text().startsWith(QStringLiteral("The matrix of an object scales its line differently")));
}

/// A card of the display of a test registry (as P14's Keyboard, whose Close link must work in Preview and during a run)
class ContentCardsDisplayCard : public pdf::PDFAppEditContentCard
{
public:
    explicit ContentCardsDisplayCard(pdf::PDFAppEditContentContext* context, QWidget* parent) :
        pdf::PDFAppEditContentCard(context, QLatin1String(pdf::PDFAppEditContentCardIds::KEYBOARD), parent)
    {
    }

    virtual void updateCard() override { }
};

/// Its entry: it changes no model
class ContentCardsDisplayEntry : public pdf::PDFAppEditContentCardEntry
{
public:
    virtual QString getId() const override { return QLatin1String(pdf::PDFAppEditContentCardIds::KEYBOARD); }
    virtual bool isShown(const pdf::PDFAppEditContentContext* context) const override { return context && context->isCardRequested(getId()); }
    virtual bool isChangingModel() const override { return false; }
    virtual pdf::PDFAppEditContentCard* createCard(pdf::PDFAppEditContentContext* context, QWidget* parent) const override { return new ContentCardsDisplayCard(context, parent); }
};

void AppToolContentCardsTest::displayCardsStayUsable()
{
    // R7-8: during a run (Apply Changes) the cards, which change the model, are disabled; the cards of the display and
    // of the state of the session stay usable - Selection, Locked object with Unlock (QE.8, no command), a card of the
    // display like Keyboard (the entry says isChangingModel false); in Preview the Preview card and Keyboard too
    using H = ContentCardsHelper;
    ContentCardsSetup setup(H::createDocument());
    setup.registry.addCard(std::make_unique<ContentCardsDisplayEntry>());
    QVERIFY(setup.open());
    const std::vector<pdf::PDFContentNodeReference> paths = H::getPaths(setup.context);
    setup.context->getSession()->setNodeLocked(paths[2], true);
    setup.context->setLockedClickNode(paths[2]);
    setup.context->setCardRequested(QLatin1String(CardIds::LOCKED_OBJECT), true);
    QVERIFY(setup.settle());
    QVERIFY(setup.stack->getCard(QLatin1String(CardIds::LOCKED_OBJECT)));

    setup.context->setRunning(true);
    QVERIFY(setup.settle());
    QVERIFY(setup.stack->getCard(QLatin1String(CardIds::LOCKED_OBJECT))->isEnabled());
    QVERIFY(setup.find<QPushButton>("editContentUnlock")->isEnabled());
    QVERIFY(setup.stack->getCard(QLatin1String(CardIds::SELECTION))->isEnabled());
    QVERIFY(!setup.stack->getCard(QLatin1String(CardIds::ARRANGE))->isEnabled());
    setup.context->setRunning(false);
    QVERIFY(setup.settle());
    QVERIFY(setup.stack->getCard(QLatin1String(CardIds::ARRANGE))->isEnabled());

    setup.context->setCardRequested(QLatin1String(CardIds::KEYBOARD), true);
    setup.context->setPreviewActive(true);
    QVERIFY(setup.settle());
    QVERIFY(setup.stack->getCard(QLatin1String(CardIds::KEYBOARD)) && setup.stack->getCard(QLatin1String(CardIds::KEYBOARD))->isEnabled());
    QVERIFY(setup.stack->getCard(QLatin1String(CardIds::PREVIEW))->isEnabled());
}

void AppToolContentCardsTest::rotatedPageDirections()
{
    // R5-6: /Rotate 90 shows the page turned clockwise (ISO 32000-2, Table 31): a point (x, y) of the page is (y, -x) on
    // the screen (PDFContentArrange::getDisplayedPageMatrix). A "10 10 50 30 re" is X = 10 (its lower edge), Y = 200 - 60
    // = 140 (from the screen's lower edge, the page's right edge 200), W = 30, H = 50. X = 50 moves it up the page by 40:
    // (10 50) - (60 80). Align left of A to B (100 100 20 20, the key): the screen left edges = the lower edges of the page
    // - A goes to (10 100). Flip horizontally on the screen mirrors the page y.
    using H = ContentCardsHelper;
    ContentCardsSetup setup(H::createContentDocument(QByteArrayLiteral("10 10 50 30 re f\n100 100 20 20 re f\n"), false, 90));
    QVERIFY(setup.open());
    const std::vector<pdf::PDFContentNodeReference> paths = H::getPaths(setup.context);
    QCOMPARE(paths.size(), size_t(2));
    setup.context->getSelection()->setNode(paths[0]);
    QVERIFY(setup.settle());

    pdf::PDFDoubleSpinBox* x = setup.find<pdf::PDFDoubleSpinBox>("editContentX");
    QCOMPARE(x->value(), 10.0);
    QCOMPARE(setup.find<pdf::PDFDoubleSpinBox>("editContentY")->value(), 140.0);
    QCOMPARE(setup.find<pdf::PDFDoubleSpinBox>("editContentW")->value(), 30.0);
    QCOMPARE(setup.find<pdf::PDFDoubleSpinBox>("editContentH")->value(), 50.0);
    QCOMPARE(setup.find<pdf::PDFLabel>("editContentSelectionSubtitle")->text(), QStringLiteral("30 × 50 pt · fill only"));
    H::enter(x, QStringLiteral("50"));
    QVERIFY(H::isSame(H::box(setup.context, paths[0]), QRectF(10, 50, 50, 30)));
    x->clearFocus();

    setup.context->getSelection()->setNodes({ paths[0], paths[1] });
    QVERIFY(setup.settle());
    setup.find<QToolButton>("editContentArrange_obj-align-left")->click();
    QVERIFY(H::isSame(H::box(setup.context, paths[0]), QRectF(10, 100, 50, 30)));
    QVERIFY(H::isSame(H::box(setup.context, paths[1]), QRectF(100, 100, 20, 20)));

    setup.context->getSelection()->setNode(paths[0]);
    QVERIFY(setup.settle());
    setup.find<QToolButton>("editContentFlipHorizontally")->click();
    const pdf::PDFContentDisplayState state = H::getState(setup.context, paths[0]);
    QCOMPARE(state.matrix.m11(), 1.0);
    QCOMPARE(state.matrix.m22(), -1.0);
    QVERIFY(H::isSame(H::box(setup.context, paths[0]), QRectF(10, 100, 50, 30)));
}

void AppToolContentCardsTest::selectionCountsAsTheTree()
{
    // R7-10: the card Selection and the row of the tree tell one fact one way (PDFAppEditContentObjectsModel): a closed
    // triangle "m l l h" draws 3 segments (not 4 operators); a group, whose second object clips, is a clipping group; the
    // values of the accessible names say the paint (R7-13: "Fill R"); the swatch note says "this page" (R7-11)
    using H = ContentCardsHelper;
    ContentCardsSetup setup(H::createContentDocument(QByteArrayLiteral("0 0 1 rg 10 10 m 50 10 l 30 40 l h f\n"
                                                                       "q 0 0 1 rg 60 60 20 20 re f 0 0 100 100 re W n Q\n")));
    QVERIFY(setup.open());
    const std::vector<pdf::PDFContentNodeReference> paths = H::getNodes(setup.context, pdf::PDFContentNodeKind::Path);
    const std::vector<pdf::PDFContentNodeReference> groups = H::getNodes(setup.context, pdf::PDFContentNodeKind::Group);
    QCOMPARE(groups.size(), size_t(1));
    setup.context->getSelection()->setNode(paths[0]);
    QVERIFY(setup.settle());
    QCOMPARE(setup.find<pdf::PDFLabel>("editContentSelectionTitle")->text(), QStringLiteral("Path · 3 segment(s)"));
    QCOMPARE(setup.find<pdf::PDFDoubleSpinBox>("editContentFillValue0")->accessibleName(), QStringLiteral("Fill R"));
    QCOMPARE(setup.find<pdf::PDFLabel>("editContentFillAndStrokeNote")->text().left(46), QStringLiteral("The swatches are the colors used on this page."));

    setup.context->getSelection()->setNode(groups[0]);
    QVERIFY(setup.settle());
    QVERIFY(setup.find<pdf::PDFLabel>("editContentSelectionTitle")->text().startsWith(QStringLiteral("Clipping group · ")));
}

void AppToolContentCardsTest::reloadedSessionFollowed()
{
    // PDFAppEditContentContext::reloadDocument (FX3 item 2): the stack connects to the new session - a command of the new
    // session rebuilds the cards; the swatches are of the new document (A filled green), although the container has the
    // same key and version as the old one
    using H = ContentCardsHelper;
    ContentCardsSetup setup(H::createDocument());
    QVERIFY(setup.open());
    setup.context->getSelection()->setNode(H::getPaths(setup.context)[0]);
    QVERIFY(setup.settle());
    QCOMPARE(setup.find<pdf::PDFColorSwatch>("editContentFillSwatch0")->getColor(), QColor(0, 0, 255));

    const pdf::PDFDocumentPointer document = H::createContentDocument(QByteArrayLiteral("0 1 0 rg 10 10 50 30 re f\n"));
    setup.context->reloadDocument(document);
    QVERIFY(QTest::qWaitFor([&setup, &document]() { return setup.context->getSession()->getDocument() == document.data() && setup.context->getContainer(); },
                            15000 * pdftest::AppDocumentFixture::getTimeoutFactor()));
    QVERIFY(setup.settle());
    const std::vector<pdf::PDFContentNodeReference> paths = H::getPaths(setup.context);
    setup.context->getSelection()->setNode(paths[0]);
    QVERIFY(setup.settle());
    QCOMPARE(setup.find<pdf::PDFColorSwatch>("editContentFillSwatch0")->getColor(), QColor(0, 255, 0));

    QVERIFY(setup.context->execute(pdf::PDFContentCommandFactory::createSetTransparency({ paths[0] }, std::nullopt, 0.5, std::nullopt)));
    QVERIFY(setup.stack->isRebuildScheduled());
    QVERIFY(setup.settle());
    QCOMPARE(setup.stack->getCard(QLatin1String(CardIds::TRANSPARENCY))->getSections().front()->getSummaryText(), QStringLiteral("Opacity 50 % · blend mode Normal"));
}

void AppToolContentCardsTest::keyObjectAnywhere()
{
    // R7-1 (coordinator: the key object is the key node of the selection, wherever it lies in it): A and B selected, A
    // made the key object (a click on it, PDFAppEditContentSelection::setKeyNode) - Align left moves B to the left edge of
    // A, 10; A does not move
    using H = ContentCardsHelper;
    ContentCardsSetup setup(H::createDocument());
    QVERIFY(setup.open());
    const std::vector<pdf::PDFContentNodeReference> paths = H::getPaths(setup.context);
    setup.context->getSelection()->setNodes({ paths[0], paths[1] });
    setup.context->getSelection()->setKeyNode(paths[0]);
    QVERIFY(setup.settle());
    setup.find<QToolButton>("editContentArrange_obj-align-left")->click();
    QVERIFY(H::isSame(H::box(setup.context, paths[0]), QRectF(10, 10, 50, 30)));
    QVERIFY(H::isSame(H::box(setup.context, paths[1]), QRectF(10, 100, 20, 20)));
}

QTEST_MAIN(AppToolContentCardsTest)

#include "tst_apptoolcontentcardstest.moc"
