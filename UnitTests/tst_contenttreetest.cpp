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

// Edit Content, stage E0: the tree of the nodes of a content, its states and the tree builder
// (package P02; GUI_overhaul/edit_tool/zadani_jadro.md 4.2, 4.3, zadani_overeni.md 6.2, chapter 8).
//
// Placeholder of the skeleton: the single function checks only what has real code in the
// skeleton - the identifiers, the keys of the containers, the limits, the set of properties, the
// plain predicates and the constructors, which store their data - and never calls a function of
// the package (they are stubs ending the process). Package P02 replaces it by its tests. The
// expected values are written by hand from the plan, never taken from a run.

#include "pdfcontentnode.h"
#include "pdfcontentstate.h"
#include "pdfcontenttreebuilder.h"
#include "pdfobject.h"
#include "pdfpagecontentprocessor.h"

#include <QtTest>
#include <QRectF>
#include <QTransform>

#include <memory>
#include <vector>

using namespace pdf;

class ContentTreeTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void skeletonPlainTypes();
};

void ContentTreeTest::skeletonPlainTypes()
{
    using Key = PDFContentContainerKey;
    using Property = PDFContentStateProperty;

    // ---- The identifier of a node: 0 is no node, the builder numbers from 1 (the root) ----
    QVERIFY(!PDFContentNodeId().isValid());
    QVERIFY(!(PDFContentNodeId{ 0 }).isValid());
    QVERIFY((PDFContentNodeId{ 1 }).isValid());
    QVERIFY((PDFContentNodeId{ 1 }) < (PDFContentNodeId{ 2 }));
    QVERIFY((PDFContentNodeId{ 57 }) > (PDFContentNodeId{ 12 }));
    QVERIFY((PDFContentNodeId{ 57 }) == (PDFContentNodeId{ 57 }));
    QVERIFY((PDFContentNodeId{ 57 }) != (PDFContentNodeId{ 58 }));

    // ---- The key of a container: a page (index and reference), a form, a cell of a pattern ----
    const PDFObjectReference pageReference(12, 0);
    const Key page = Key::createPage(3, pageReference);
    QVERIFY(page.kind == Key::Kind::Page);
    QCOMPARE(page.pageIndex, PDFInteger(3));
    QVERIFY(page.reference == pageReference);
    QVERIFY(page.isValid());

    const PDFObjectReference formReference(40, 0);
    const Key form = Key::createForm(formReference);
    QVERIFY(form.kind == Key::Kind::Form);
    QCOMPARE(form.pageIndex, PDFInteger(-1));
    QVERIFY(form.reference == formReference);
    QVERIFY(form.isValid());

    const PDFObjectReference patternReference(41, 2);
    const Key patternCell = Key::createPatternCell(patternReference);
    QVERIFY(patternCell.kind == Key::Kind::PatternCell);
    QCOMPARE(patternCell.pageIndex, PDFInteger(-1));
    QVERIFY(patternCell.reference == patternReference);
    QVERIFY(patternCell.isValid());

    // A key without an object is no container; a page needs its index too
    QVERIFY(!Key().isValid());
    QVERIFY(!Key::createPage(-1, pageReference).isValid());
    QVERIFY(!Key::createPage(0, PDFObjectReference()).isValid());
    QVERIFY(Key::createPage(0, pageReference).isValid());
    QVERIFY(!Key::createForm(PDFObjectReference()).isValid());
    QVERIFY(!Key::createPatternCell(PDFObjectReference()).isValid());

    // The order: the kind first (Page, Form, PatternCell), the index of the page then, the
    // reference (object number, generation) last
    QVERIFY(Key::createPage(99, PDFObjectReference(99, 0)) < Key::createForm(PDFObjectReference(1, 0)));
    QVERIFY(Key::createForm(PDFObjectReference(99, 0)) < Key::createPatternCell(PDFObjectReference(1, 0)));
    QVERIFY(Key::createPage(1, PDFObjectReference(50, 0)) < Key::createPage(2, PDFObjectReference(10, 0)));
    QVERIFY(Key::createPage(2, PDFObjectReference(10, 0)) < Key::createPage(2, PDFObjectReference(11, 0)));
    QVERIFY(Key::createForm(PDFObjectReference(5, 0)) < Key::createForm(PDFObjectReference(6, 0)));
    QVERIFY(Key::createForm(PDFObjectReference(5, 0)) < Key::createForm(PDFObjectReference(5, 1)));
    QVERIFY(Key::createForm(PDFObjectReference(6, 0)) > Key::createForm(PDFObjectReference(5, 1)));
    QVERIFY(!(Key::createForm(formReference) < Key::createForm(formReference)));
    QVERIFY(Key::createForm(formReference) == form);
    QVERIFY(Key::createForm(formReference) != Key::createPatternCell(formReference));
    QVERIFY(Key::createPage(3, pageReference) == page);
    QVERIFY(Key::createPage(4, pageReference) != page);

    // ---- The limits (zadani_overeni.md 6.2) ----
    QCOMPARE(PDFContentContainer::MAXIMUM_DEPTH, 256);
    QCOMPARE(PDFContentContainer::MAXIMUM_OPERATION_COUNT, size_t(5000000));
    QCOMPARE(PDFContentContainer::MAXIMUM_OPERAND_COUNT, size_t(100000));
    QCOMPARE(PDFContentContainer::MAXIMUM_MARKED_CONTENT_ID, PDFInteger(2147483647));    // 2^31 - 1

    // ---- A span of operations ----
    QVERIFY(PDFContentOperationSpan().isEmpty());
    QVERIFY(!(PDFContentOperationSpan{ 4, 1 }).isEmpty());
    QVERIFY((PDFContentOperationSpan{ 4, 0 }).isEmpty());
    QVERIFY((PDFContentOperationSpan{ 4, 2 }) == (PDFContentOperationSpan{ 4, 2 }));
    QVERIFY(!((PDFContentOperationSpan{ 4, 2 }) == (PDFContentOperationSpan{ 5, 2 })));

    // ---- The set of properties ----
    PDFContentStatePropertySet set;
    QVERIFY(set.isEmpty());
    QVERIFY(set.getProperties().empty());
    QVERIFY(!set.contains(Property::LineWidth));

    // The properties come back in the order of the enum, whatever the order of the inserts
    set.insert(Property::SoftMask);
    set.insert(Property::LineWidth);
    set.insert(Property::Font);
    set.insert(Property::Font);
    QVERIFY(!set.isEmpty());
    QVERIFY(set.contains(Property::LineWidth));
    QVERIFY(set.contains(Property::Font));
    QVERIFY(set.contains(Property::SoftMask));
    QVERIFY(!set.contains(Property::FillColor));
    QVERIFY(set.getProperties() == std::vector<Property>({ Property::LineWidth, Property::Font, Property::SoftMask }));

    set.remove(Property::Font);
    set.remove(Property::FillColor);    // Not in the set: nothing changes
    QVERIFY(set.getProperties() == std::vector<Property>({ Property::LineWidth, Property::SoftMask }));

    PDFContentStatePropertySet other;
    other.insert(Property::FillColor);
    other.insert(Property::LineWidth);
    QVERIFY(!(set == other));
    set.unite(other);
    QVERIFY(set.getProperties() == std::vector<Property>({ Property::LineWidth, Property::FillColor, Property::SoftMask }));
    QVERIFY(other.getProperties() == std::vector<Property>({ Property::LineWidth, Property::FillColor }));

    PDFContentStatePropertySet same;
    same.insert(Property::SoftMask);
    same.insert(Property::FillColor);
    same.insert(Property::LineWidth);
    QVERIFY(set == same);

    // Every property is a member of its own: the last one does not fall onto the first one
    PDFContentStatePropertySet last;
    last.insert(Property::BlackPointCompensation);
    QVERIFY(last.contains(Property::BlackPointCompensation));
    QVERIFY(!last.contains(Property::LineWidth));
    QVERIFY(last.getProperties() == std::vector<Property>({ Property::BlackPointCompensation }));

    PDFContentStatePropertySet all;
    for (int i = 0; i < int(Property::Count); ++i)
    {
        all.insert(Property(i));
    }
    const std::vector<Property> allProperties = all.getProperties();
    QCOMPARE(allProperties.size(), size_t(Property::Count));
    for (size_t i = 0; i < allProperties.size(); ++i)
    {
        QCOMPARE(int(allProperties[i]), int(i));
    }

    // ---- An override (R-1): empty = no matrix and no property ----
    PDFContentStateOverride override;
    QVERIFY(override.isEmpty());
    override.transform = QTransform::fromTranslate(10.0, 20.0);
    QVERIFY(!override.isEmpty());
    override.transform = QTransform();
    QVERIFY(override.isEmpty());
    PDFContentStateValue lineWidth;
    lineWidth.kind = PDFContentStateValue::Kind::Real;
    lineWidth.real = 2.5;
    override.properties[Property::LineWidth] = lineWidth;
    QVERIFY(!override.isEmpty());
    QVERIFY(PDFContentStateValue().kind == PDFContentStateValue::Kind::None);

    // ---- A leak (R-3): empty = nothing of the state differs at the end of the node ----
    QVERIFY(PDFContentStateLeak().isEmpty());
    PDFContentStateLeak propertyLeak;
    propertyLeak.properties.insert(Property::FillColor);
    QVERIFY(!propertyLeak.isEmpty());
    PDFContentStateLeak transformLeak;
    transformLeak.isTransformChanged = true;
    QVERIFY(!transformLeak.isEmpty());
    PDFContentStateLeak clipLeak;
    clipLeak.isClipChanged = true;
    QVERIFY(!clipLeak.isEmpty());
    PDFContentStateLeak unknownLeak;
    unknownLeak.isUnknown = true;
    QVERIFY(!unknownLeak.isEmpty());

    // ---- A clipping path is only "n" with W or W* (zadani_jadro.md 4.2): a painting with W
    //      ("re W f") is a path, which paints and clips ----
    QVERIFY(!PDFContentPathPaint().isClippingPath());
    PDFContentPathPaint paint;
    paint.clip = PDFContentPathPaint::Clip::NonZero;
    QVERIFY(paint.isClippingPath());
    paint.clip = PDFContentPathPaint::Clip::EvenOdd;
    QVERIFY(paint.isClippingPath());
    paint.fill = true;
    QVERIFY(!paint.isClippingPath());
    paint.fill = false;
    paint.stroke = true;
    QVERIFY(!paint.isClippingPath());
    paint.clip = PDFContentPathPaint::Clip::None;
    QVERIFY(!paint.isClippingPath());

    // ---- A node stores its data and its children ----
    // The data are plain aggregates, which can be written {} (a compile-time check: no member
    // has an explicit default constructor - path, operation list, resources, dash pattern)
    QVERIFY(PDFContentNode::Data{}.kind == PDFContentNodeKind::Opaque);
    QVERIFY(PDFContentContainer::Data{}.root == nullptr);
    QVERIFY(PDFContentStateValue{}.kind == PDFContentStateValue::Kind::None);
    QVERIFY(PDFContentPropertySource{}.extGStateKey.isEmpty());

    // A build context names no page: a form or a cell of a pattern needs one, a page ignores it
    QCOMPARE(PDFContentBuildContext{}.pageIndex, PDFInteger(-1));

    PDFContentNode::Data defaultData;
    QVERIFY(defaultData.kind == PDFContentNodeKind::Opaque);
    QVERIFY(!defaultData.id.isValid());
    QVERIFY(!defaultData.override.has_value());
    QVERIFY(!defaultData.isModified);
    QVERIFY(!defaultData.mcid.has_value());
    QVERIFY(defaultData.textItems.empty());
    QVERIFY(defaultData.isTextComplete);

    PDFContentNode::Data leafData;
    leafData.kind = PDFContentNodeKind::Path;
    leafData.id = PDFContentNodeId{ 3 };
    leafData.parentId = PDFContentNodeId{ 2 };
    leafData.prefix = PDFContentOperationSpan{ 5, 4 };
    leafData.isModified = true;
    leafData.hasClipEffect = true;
    leafData.override = PDFContentStateOverride();
    leafData.override->transform = QTransform::fromTranslate(7.0, 8.0);
    const PDFContentNodePointer leaf = std::make_shared<const PDFContentNode>(leafData, std::vector<PDFContentNodePointer>());
    QVERIFY(leaf->getKind() == PDFContentNodeKind::Path);
    QVERIFY(leaf->getId() == (PDFContentNodeId{ 3 }));
    QVERIFY(leaf->getParentId() == (PDFContentNodeId{ 2 }));
    QVERIFY(leaf->getPrefix() == (PDFContentOperationSpan{ 5, 4 }));
    QVERIFY(leaf->getSuffix().isEmpty());
    QVERIFY(leaf->getChildren().empty());
    QVERIFY(leaf->isModified());
    QVERIFY(leaf->hasClipEffect());
    QVERIFY(leaf->getOverride() != nullptr);
    QCOMPARE(leaf->getOverride()->transform, QTransform::fromTranslate(7.0, 8.0));

    PDFContentNode::Data groupData;
    groupData.kind = PDFContentNodeKind::Group;
    groupData.id = PDFContentNodeId{ 2 };
    groupData.parentId = PDFContentNodeId{ 1 };
    groupData.prefix = PDFContentOperationSpan{ 4, 1 };
    groupData.suffix = PDFContentOperationSpan{ 9, 1 };
    groupData.boundingBox = QRectF(10.0, 20.0, 30.0, 40.0);
    groupData.saveDepth = 3;
    const PDFContentNodePointer group = std::make_shared<const PDFContentNode>(groupData, std::vector<PDFContentNodePointer>({ leaf }));
    QVERIFY(group->getKind() == PDFContentNodeKind::Group);
    QVERIFY(group->getId() == (PDFContentNodeId{ 2 }));
    QVERIFY(group->getParentId() == (PDFContentNodeId{ 1 }));
    QVERIFY(group->getPrefix() == (PDFContentOperationSpan{ 4, 1 }));
    QVERIFY(group->getSuffix() == (PDFContentOperationSpan{ 9, 1 }));
    QCOMPARE(group->getBoundingBox(), QRectF(10.0, 20.0, 30.0, 40.0));
    QCOMPARE(group->getData().saveDepth, 3);
    QCOMPARE(group->getChildren().size(), size_t(1));
    QVERIFY(group->getChildren().front() == leaf);
    QVERIFY(group->getOverride() == nullptr);
    QVERIFY(!group->isModified());
    QVERIFY(!group->hasClipEffect());
    QVERIFY(group->getState() == nullptr);
    QVERIFY(group->getLeak().isEmpty());

    // ---- A container stores its data ----
    PDFContentContainer::Data defaultContainerData;
    QVERIFY(!defaultContainerData.key.isValid());
    QVERIFY(defaultContainerData.root == nullptr);
    QCOMPARE(defaultContainerData.structuralParentKey, PDFInteger(-1));
    QVERIFY(defaultContainerData.matrix.isIdentity());
    QVERIFY(!defaultContainerData.isReadOnly);

    PDFContentNode::Data rootData;
    rootData.kind = PDFContentNodeKind::Root;
    rootData.id = PDFContentNodeId{ 1 };
    PDFContentContainer::Data containerData;
    containerData.key = page;
    containerData.root = std::make_shared<const PDFContentNode>(rootData, std::vector<PDFContentNodePointer>({ group }));
    containerData.boundingBox = QRectF(0.0, 0.0, 595.0, 842.0);
    containerData.structuralParentKey = 5;
    containerData.fingerprint = QByteArrayLiteral("fingerprint");
    containerData.issues.push_back(PDFContentIssue{ PDFContentIssue::Kind::UnknownOperator, PDFContentNodeId{ 3 }, 7, QStringLiteral("issue") });
    containerData.isReadOnly = true;
    const PDFContentContainer container(containerData);
    QVERIFY(container.getKey() == page);
    QVERIFY(container.getRoot() == containerData.root);
    QVERIFY(container.getRoot()->getKind() == PDFContentNodeKind::Root);
    QVERIFY(container.getRoot()->getChildren().front() == group);
    QCOMPARE(container.getBoundingBox(), QRectF(0.0, 0.0, 595.0, 842.0));
    QVERIFY(container.getMatrix().isIdentity());
    QCOMPARE(container.getStructuralParentKey(), PDFInteger(5));
    QCOMPARE(container.getFingerprint(), QByteArrayLiteral("fingerprint"));
    QCOMPARE(container.getIssues().size(), size_t(1));
    QVERIFY(container.getIssues().front().kind == PDFContentIssue::Kind::UnknownOperator);
    QVERIFY(container.getIssues().front().nodeId == (PDFContentNodeId{ 3 }));
    QCOMPARE(container.getIssues().front().operationIndex, size_t(7));
    QVERIFY(container.isReadOnly());
    QVERIFY(container.getInitialState() == nullptr);

    // ---- The resolved state keeps the state of the processor, but not its pointer to the soft
    //      mask dictionary of the document: it only says, that a mask is set ----
    QVERIFY(!PDFContentResolvedState().hasSoftMask());

    PDFPageContentProcessorState processorState;
    processorState.setCurrentTransformationMatrix(QTransform(2.0, 0.0, 0.0, 3.0, 10.0, 20.0));
    processorState.setLineWidth(4.0);
    const PDFContentResolvedState withoutMask(processorState);
    QVERIFY(!withoutMask.hasSoftMask());
    QVERIFY(withoutMask.getState().getSoftMask() == nullptr);
    QCOMPARE(withoutMask.getCurrentTransformationMatrix(), QTransform(2.0, 0.0, 0.0, 3.0, 10.0, 20.0));
    QCOMPARE(withoutMask.getState().getLineWidth(), 4.0);

    const PDFObject softMaskObject = PDFObject::createDictionary(PDFDictionaryBuilder());
    const PDFDictionary* softMask = softMaskObject.getDictionary();
    QVERIFY(softMask != nullptr);
    processorState.setSoftMask(softMask);
    const PDFContentResolvedState withMask(processorState);
    QVERIFY(withMask.hasSoftMask());
    QVERIFY(withMask.getState().getSoftMask() == nullptr);
    QCOMPARE(withMask.getCurrentTransformationMatrix(), QTransform(2.0, 0.0, 0.0, 3.0, 10.0, 20.0));
    QCOMPARE(withMask.getState().getLineWidth(), 4.0);
    QVERIFY(processorState.getSoftMask() == softMask);      // The state of the processor is not touched
}

QTEST_MAIN(ContentTreeTest)

#include "tst_contenttreetest.moc"
