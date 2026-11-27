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

#include "pdfappeditcontentcards.h"

#include "pdfappeditcontentcontext.h"
#include "pdfappeditcontentregistry.h"
#include "pdfappeditcontentselection.h"
#include "pdfapplocale.h"
#include "pdfcatalog.h"
#include "pdfcomponentsections.h"
#include "pdfcontenteditsession.h"
#include "pdfdocument.h"
#include "pdffluentwidgets.h"

#include <QCheckBox>
#include <QColor>
#include <QHBoxLayout>
#include <QPushButton>
#include <QVBoxLayout>
#include <QtGlobal>

#include <algorithm>
#include <cmath>
#include <set>

namespace pdf
{

// Edit Content, stage E1, package P13. The base of the cards, the stack and the cards Selection, Locked object,
// Annotations and fields, Preview (zadani_gui.md 4.19; the frames edit-content-v2, -nothing-selected, -locked-click,
// -multi-selected, -annotations-underlay, -preview). Texts are those of the frames.

// ---------------------------------------------------------------- PDFAppEditContentCardTools

std::vector<PDFAppEditContentCardTools::SelectedNode> PDFAppEditContentCardTools::getSelectedNodes(const PDFAppEditContentContext* context, bool* isTruncated)
{
    std::vector<SelectedNode> result;
    if (isTruncated)
    {
        *isTruncated = false;
    }

    if (!context)
    {
        return result;
    }

    const PDFContentContainerPointer container = context->getContainer();
    const PDFAppEditContentSelection* selection = context->getSelection();
    if (!container || !selection)
    {
        return result;
    }

    // Bound: at most MAXIMUM_INSPECTED_NODES selected nodes are read
    for (const PDFContentNodeReference& reference : selection->getNodes())
    {
        if (result.size() >= MAXIMUM_INSPECTED_NODES)
        {
            if (isTruncated)
            {
                *isTruncated = true;
            }
            break;
        }

        if (reference.container != container->getKey())
        {
            continue;
        }

        PDFContentNodePointer node = container->getNode(reference.node);
        if (node)
        {
            result.push_back(SelectedNode{ reference, std::move(node) });
        }
    }

    return result;
}

std::vector<PDFContentNodePointer> PDFAppEditContentCardTools::getLeaves(const PDFContentContainer& container,
                                                                        const std::vector<SelectedNode>& nodes,
                                                                        bool (*isKind)(const PDFContentNode& node),
                                                                        bool* isTruncated)
{
    std::vector<PDFContentNodePointer> leaves;
    if (isTruncated)
    {
        *isTruncated = false;
    }

    // A walk over the trees of the selected nodes: one visited set (a node selected together with its group is
    // visited once), a depth limit and a limit of the visits (MAXIMUM_INSPECTED_NODES)
    std::set<quint64> visited;
    std::vector<std::pair<const PDFContentNode*, int>> stack;
    size_t visits = 0;
    const int maximumDepth = PDFContentContainer::MAXIMUM_DEPTH + 2;

    for (const SelectedNode& selected : nodes)
    {
        stack.emplace_back(selected.node.get(), 0);
        while (!stack.empty())
        {
            const auto [node, depth] = stack.back();
            stack.pop_back();

            if (!visited.insert(node->getId().value).second)
            {
                continue;
            }

            if (++visits > MAXIMUM_INSPECTED_NODES)
            {
                if (isTruncated)
                {
                    *isTruncated = true;
                }
                return leaves;
            }

            if (PDFContentNode::isLeafKind(node->getKind()))
            {
                if (isKind(*node))
                {
                    leaves.push_back(container.getNode(node->getId()));
                }
                continue;
            }

            if (depth >= maximumDepth)
            {
                if (isTruncated)
                {
                    *isTruncated = true;
                }
                continue;
            }

            const std::vector<PDFContentNodePointer>& children = node->getChildren();
            for (auto it = children.crbegin(); it != children.crend(); ++it)
            {
                stack.emplace_back(it->get(), depth + 1);
            }
        }
    }

    return leaves;
}

bool PDFAppEditContentCardTools::isPaintedLeaf(const PDFContentNode& node)
{
    switch (node.getKind())
    {
        case PDFContentNodeKind::Path:
        {
            const PDFContentPathPaint& paint = node.getData().paint;
            return paint.fill || paint.stroke;
        }

        case PDFContentNodeKind::TextShow:
            return true;

        default:
            return false;
    }
}

bool PDFAppEditContentCardTools::isDrawingLeaf(const PDFContentNode& node)
{
    switch (node.getKind())
    {
        case PDFContentNodeKind::Path:
            return !node.getData().paint.isClippingPath();

        case PDFContentNodeKind::TextShow:
        case PDFContentNodeKind::Image:
        case PDFContentNodeKind::FormUse:
        case PDFContentNodeKind::Shading:
            return true;

        default:
            return false;
    }
}

PDFContentDisplayContext PDFAppEditContentCardTools::getDisplayContext(const PDFAppEditContentContext* context)
{
    PDFContentDisplayContext displayContext;
    if (context && context->getSession())
    {
        displayContext.document = context->getSession()->getDocument();
    }
    return displayContext;
}

QString PDFAppEditContentCardTools::getKindTitle(const PDFContentNode& node)
{
    const PDFContentNode::Data& data = node.getData();
    switch (node.getKind())
    {
        case PDFContentNodeKind::Root:
            return tr("Page content");

        case PDFContentNodeKind::Group:
        {
            // A group, whose first object is a clipping path, is a clipping group (the tab Objects)
            for (const PDFContentNodePointer& child : node.getChildren())
            {
                if (child->getKind() == PDFContentNodeKind::StateRun)
                {
                    continue;
                }
                if (child->getKind() == PDFContentNodeKind::Path && child->getData().paint.isClippingPath())
                {
                    return tr("Clipping group");
                }
                break;
            }
            return tr("Group");
        }

        case PDFContentNodeKind::MarkedContent:
            if (data.optionalContent.isValid() || data.tag == "OC")
            {
                return tr("Layer content");
            }
            if (data.isArtifact)
            {
                return tr("Artifact");
            }
            if (data.mcid.has_value())
            {
                return tr("Tagged content");
            }
            return tr("Marked content");

        case PDFContentNodeKind::MarkedPoint:
            return tr("Marked point");

        case PDFContentNodeKind::TextObject:
        case PDFContentNodeKind::TextShow:
            return tr("Text");

        case PDFContentNodeKind::Path:
            return data.paint.isClippingPath() ? tr("Clipping path") : tr("Path");

        case PDFContentNodeKind::Image:
            return tr("Image");

        case PDFContentNodeKind::FormUse:
            return tr("Form XObject");

        case PDFContentNodeKind::Shading:
            return tr("Shading");

        case PDFContentNodeKind::StateRun:
            return tr("Graphics state");

        case PDFContentNodeKind::Opaque:
            return tr("Unknown content");
    }

    return QString();
}

bool PDFAppEditContentCardTools::isSelectionStateShown(const PDFAppEditContentContext* context)
{
    return context && !context->isPreviewActive() && !context->isCardRequested(QLatin1String(PDFAppEditContentCardIds::KEYBOARD));
}

QRectF PDFAppEditContentCardTools::getPageBox(const PDFAppEditContentContext* context)
{
    if (!context || !context->getSession() || context->getPageIndex() < 0)
    {
        return QRectF();
    }

    const PDFDocument* document = context->getSession()->getDocument();
    if (!document || context->getPageIndex() >= PDFInteger(document->getCatalog()->getPageCount()))
    {
        return QRectF();
    }

    return document->getCatalog()->getPage(size_t(context->getPageIndex()))->getCropBox();
}

double PDFAppEditContentCardTools::getUnitFactor(const PDFAppEditContentContext* context)
{
    const QString unit = context && context->getOptions() ? context->getOptions()->getValue(QLatin1String(PDFAppEditContentOptions::UNITS)).toString() : QString();
    if (unit == QLatin1String("mm"))
    {
        return 72.0 / 25.4;
    }
    if (unit == QLatin1String("in"))
    {
        return 72.0;
    }
    return 1.0;
}

QString PDFAppEditContentCardTools::getUnitSuffix(const PDFAppEditContentContext* context)
{
    const QString unit = context && context->getOptions() ? context->getOptions()->getValue(QLatin1String(PDFAppEditContentOptions::UNITS)).toString() : QString();
    if (unit == QLatin1String("mm"))
    {
        return tr("mm");
    }
    if (unit == QLatin1String("in"))
    {
        return tr("in");
    }
    return tr("pt");
}

QString PDFAppEditContentCardTools::formatReal(double value, int decimals)
{
    if (!std::isfinite(value))
    {
        return QString();
    }

    // Negative zero after the rounding is written as zero
    const double factor = std::pow(10.0, decimals);
    double rounded = std::round(value * factor) / factor;
    if (rounded == 0.0)
    {
        rounded = 0.0;
    }

    QString text = PDFAppLocale::getUiLocale().toString(rounded, 'f', decimals);
    const QString decimalPoint = PDFAppLocale::getUiLocale().decimalPoint();
    if (decimals > 0 && text.contains(decimalPoint))
    {
        while (text.endsWith(QLatin1Char('0')))
        {
            text.chop(1);
        }
        if (text.endsWith(decimalPoint))
        {
            text.chop(decimalPoint.size());
        }
    }
    return text;
}

PDFLabel* PDFAppEditContentCardTools::createLabel(const QString& text, PDFThemeFonts::Role role, PDFThemeColors::Color color, QWidget* parent)
{
    PDFLabel* label = new PDFLabel(text, role, color, parent);
    label->setWordWrap(true);
    return label;
}

PDFLabel* PDFAppEditContentCardTools::createNote(const QString& text, QWidget* parent)
{
    return createLabel(text, PDFThemeFonts::Role::CaptionRegular, PDFThemeColors::TextTertiary, parent);
}

QWidget* PDFAppEditContentCardTools::createFieldRow(const QString& label, QWidget* field, QWidget* parent, int fieldWidth)
{
    QWidget* row = new QWidget(parent);
    QHBoxLayout* layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);
    PDFLabel* text = new PDFLabel(label, PDFThemeFonts::Role::Small, PDFThemeColors::TextSecondary, row);
    text->setBuddy(field);
    layout->addWidget(text, 1);
    field->setParent(row);
    if (fieldWidth > 0)
    {
        field->setFixedWidth(fieldWidth);
    }
    layout->addWidget(field);
    if (field->accessibleName().isEmpty())
    {
        field->setAccessibleName(label);
    }
    return row;
}

QWidget* PDFAppEditContentCardTools::createColumn(QWidget* parent, QVBoxLayout** layout)
{
    QWidget* column = new QWidget(parent);
    QVBoxLayout* columnLayout = new QVBoxLayout(column);
    columnLayout->setContentsMargins(0, 0, 0, 0);
    columnLayout->setSpacing(8);
    if (layout)
    {
        *layout = columnLayout;
    }
    return column;
}

// ---------------------------------------------------------------- PDFAppEditContentCard

PDFAppEditContentCard::PDFAppEditContentCard(PDFAppEditContentContext* context, const QString& cardId, QWidget* parent) :
    BaseClass(parent),
    PDFRetirable(this),
    m_context(context),
    m_cardId(cardId)
{
    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(SECTION_GAP);

    if (context)
    {
        // The context lives longer than the card (a part): the connections are cut by the retire of the card
        PDFDeferredDeletion::connect(context, &PDFAppEditContentContext::pendingEditsCommitRequested, this, &PDFAppEditContentCard::onPendingEditsCommitRequested);
        PDFDeferredDeletion::connect(context, &PDFAppEditContentContext::pendingEditsDiscardRequested, this, &PDFAppEditContentCard::onPendingEditsDiscardRequested);
    }
}

PDFAppEditContentCard::~PDFAppEditContentCard() = default;

void PDFAppEditContentCard::commitPendingEdit()
{
    // The default: a card without fields has nothing to commit
}

void PDFAppEditContentCard::discardPendingEdit()
{
    // The default: a card without fields has nothing to discard
}

PDFExpanderCard* PDFAppEditContentCard::addSection(const QString& title, const QString& sectionName)
{
    const QString key = sectionName.isEmpty() ? m_cardId : m_cardId + QLatin1Char('/') + sectionName;

    PDFExpanderCard* section = new PDFExpanderCard(title, this);
    section->setObjectName(QStringLiteral("editContentSection_") + key);
    section->setProperty("sectionKey", key);
    section->setExpanded(m_context && m_context->isSectionExpanded(key));
    connect(section, &PDFExpanderCard::expandedChanged, this, &PDFAppEditContentCard::onSectionExpandedChanged);

    m_layout->addWidget(section);
    m_sections.emplace_back(section);
    return section;
}

void PDFAppEditContentCard::retire()
{
    // The card ends (the stack shows it no more, or the tool closes): a value typed and not confirmed is forgotten
    // (R3-9: the discard signal of the context does not reach a retired card)
    discardPendingEdit();
}

void PDFAppEditContentCard::onSectionExpandedChanged(bool isExpanded)
{
    // The state lives in the context, so it survives the rebuild of the stack and the change of the language
    const PDFExpanderCard* section = qobject_cast<const PDFExpanderCard*>(sender());
    if (section && m_context)
    {
        m_context->setSectionExpanded(section->property("sectionKey").toString(), isExpanded);
    }
}

void PDFAppEditContentCard::onPendingEditsCommitRequested()
{
    commitPendingEdit();
}

void PDFAppEditContentCard::onPendingEditsDiscardRequested()
{
    discardPendingEdit();
}

// ---------------------------------------------------------------- the cards of this file

/// The card Selection (always the first one): the kind, the name, a subtitle, the path of the types and a note
/// (edit-content-v2 1647:21722, -nothing-selected, -multi-selected, -path-selected, -locked-click)
class PDFAppEditContentSelectionCard : public PDFAppEditContentCard
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFAppEditContentSelectionCard)

public:
    explicit PDFAppEditContentSelectionCard(PDFAppEditContentContext* context, QWidget* parent) :
        PDFAppEditContentCard(context, QLatin1String(PDFAppEditContentCardIds::SELECTION), parent)
    {
        m_section = addSection(tr("Selection"));
        QVBoxLayout* layout = nullptr;
        QWidget* content = PDFAppEditContentCardTools::createColumn(m_section, &layout);
        m_title = PDFAppEditContentCardTools::createLabel(QString(), PDFThemeFonts::Role::BodySemiBold, PDFThemeColors::TextPrimary, content);
        m_title->setObjectName(QStringLiteral("editContentSelectionTitle"));
        m_subtitle = PDFAppEditContentCardTools::createLabel(QString(), PDFThemeFonts::Role::CaptionRegular, PDFThemeColors::TextTertiary, content);
        m_subtitle->setObjectName(QStringLiteral("editContentSelectionSubtitle"));
        m_path = new PDFPathBar(content);
        m_path->setObjectName(QStringLiteral("editContentSelectionPath"));
        m_path->setBarSize(PDFPathBar::Size::Small);
        m_note = PDFAppEditContentCardTools::createNote(QString(), content);
        m_note->setObjectName(QStringLiteral("editContentSelectionNote"));
        layout->addWidget(m_title);
        layout->addWidget(m_subtitle);
        layout->addWidget(m_path);
        layout->addWidget(m_note);
        m_section->setContentWidget(content);

        connect(m_path, &PDFPathBar::segmentActivated, this, &PDFAppEditContentSelectionCard::onSegmentActivated);
    }

    virtual void updateCard() override
    {
        PDFAppEditContentContext* context = getContext();
        m_targets.clear();
        if (!context)
        {
            return;
        }

        bool isTruncated = false;
        const std::vector<PDFAppEditContentCardTools::SelectedNode> nodes = PDFAppEditContentCardTools::getSelectedNodes(context, &isTruncated);
        const int count = context->getSelection() ? context->getSelection()->getCount() : 0;

        if (nodes.empty())
        {
            showNothing(context);
        }
        else if (count == 1)
        {
            showOne(context, nodes.front());
        }
        else
        {
            showMany(nodes, count);
        }
    }

private:
    /// A segment of the path: the node it selects, or an empty reference for the page
    struct Target
    {
        PDFContentNodeReference node;
        int leaveLevels = 0;    ///< Levels of the isolation to leave (a segment of a level above the edited one)
    };

    void showNothing(const PDFAppEditContentContext* context)
    {
        m_section->setBadgeText(QString());
        m_title->setText(tr("Nothing selected"));
        m_subtitle->setVisible(false);
        m_path->setVisible(false);
        m_note->setVisible(true);

        const bool isLockedClick = context->isCardRequested(QLatin1String(PDFAppEditContentCardIds::LOCKED_OBJECT)) && context->getLockedClickNode().isValid();
        if (isLockedClick)
        {
            m_note->setText(tr("The clicked object is locked: it cannot be selected, moved or changed on the page, and a click goes through it to the object below. "
                               "The gray frame with its name stays until the next click."));
        }
        else
        {
            m_note->setText(tr("Click an object on the page or in the Objects tab. Double-click a text to type in it, a path to edit its nodes, a group to enter it. "
                               "Tools in the toolbar add new content."));
        }
    }

    void showOne(const PDFAppEditContentContext* context, const PDFAppEditContentCardTools::SelectedNode& selected)
    {
        const PDFContentContainerPointer container = context->getContainer();
        const PDFContentNode& node = *selected.node;
        const QString kind = PDFAppEditContentCardTools::getKindTitle(node);
        m_section->setBadgeText(kind);

        QString title = kind;
        QStringList subtitle;
        const QRectF box = PDFAppEditContentGeometryHelper::getPageBoundingBox(context, selected.reference);
        const double factor = PDFAppEditContentCardTools::getUnitFactor(context);
        const QString size = box.isNull() ? QString() : tr("%1 × %2 %3").arg(PDFAppEditContentCardTools::formatReal(box.width() / factor, 1),
                                                                             PDFAppEditContentCardTools::formatReal(box.height() / factor, 1),
                                                                             PDFAppEditContentCardTools::getUnitSuffix(context));

        switch (node.getKind())
        {
            case PDFContentNodeKind::Path:
            {
                const PDFContentNode::Data& nodeData = node.getData();
                if (nodeData.path.isRectangle())
                {
                    title = tr("%1 · rectangle").arg(kind);
                }
                else
                {
                    title = tr("%1 · %Ln segment(s)", nullptr, int(nodeData.path.getSegments().size())).arg(kind);
                }
                if (!size.isEmpty())
                {
                    subtitle << size;
                }
                if (nodeData.paint.fill && nodeData.paint.stroke)
                {
                    subtitle << tr("fill and stroke");
                }
                else if (nodeData.paint.fill)
                {
                    subtitle << tr("fill only");
                }
                else if (nodeData.paint.stroke)
                {
                    subtitle << tr("stroke only");
                }
                break;
            }

            case PDFContentNodeKind::TextObject:
            {
                // The lines and the characters of the core (the display queries): one text object only
                const PDFContentDisplayTextPointer text = PDFContentDisplay::getText(*container, node.getId(), PDFAppEditContentCardTools::getDisplayContext(context));
                if (text)
                {
                    title = tr("%1 · %Ln line(s)", nullptr, int(text->lines.size())).arg(kind);
                    subtitle << tr("%Ln character(s)", nullptr, int(text->glyphs.size()));
                }
                break;
            }

            case PDFContentNodeKind::Group:
            case PDFContentNodeKind::MarkedContent:
            {
                int objects = 0;
                for (const PDFContentNodePointer& child : node.getChildren())
                {
                    if (child->getKind() != PDFContentNodeKind::StateRun && child->getKind() != PDFContentNodeKind::MarkedPoint)
                    {
                        ++objects;
                    }
                }
                title = tr("%1 · %Ln object(s)", nullptr, objects).arg(kind);
                if (!size.isEmpty())
                {
                    subtitle << size;
                }
                break;
            }

            case PDFContentNodeKind::Opaque:
                subtitle << node.getData().opaqueReason;
                break;

            default:
                if (!size.isEmpty())
                {
                    subtitle << size;
                }
                break;
        }

        m_title->setText(title);
        subtitle.removeAll(QString());
        m_subtitle->setText(subtitle.join(QStringLiteral(" · ")));
        m_subtitle->setVisible(!subtitle.isEmpty());

        // The path of the types: the page, the levels of the isolation, the groups above the node, the node
        QStringList segments;
        segments << tr("Page %1").arg(PDFAppLocale::formatNumber(context->getPageIndex() + 1));
        m_targets.push_back(Target());
        const std::vector<PDFAppEditContentIsolationLevel>& levels = context->getIsolationPath();
        for (size_t i = 0; i < levels.size(); ++i)
        {
            segments << levels[i].title;
            Target target;
            target.leaveLevels = int(levels.size() - i - 1);
            m_targets.push_back(target);
        }
        if (!m_targets.empty())
        {
            m_targets.front().leaveLevels = int(levels.size());
        }

        // The ancestors from the root (not the root itself; bounded by the depth of the tree)
        std::vector<PDFContentNodePointer> ancestors;
        PDFContentNodeId parentId = node.getParentId();
        const PDFContentNodeId editedRoot = context->getEditedRoot().node;
        for (int depth = 0; parentId.isValid() && depth <= PDFContentContainer::MAXIMUM_DEPTH + 2; ++depth)
        {
            PDFContentNodePointer parent = container->getNode(parentId);
            if (!parent || parent->getKind() == PDFContentNodeKind::Root || parentId == editedRoot)
            {
                break;
            }
            ancestors.push_back(parent);
            parentId = parent->getParentId();
        }
        for (auto it = ancestors.crbegin(); it != ancestors.crend(); ++it)
        {
            segments << PDFAppEditContentCardTools::getKindTitle(**it);
            Target target;
            target.node = PDFContentNodeReference{ container->getKey(), (*it)->getId() };
            m_targets.push_back(target);
        }
        segments << kind;
        Target target;
        target.node = selected.reference;
        m_targets.push_back(target);
        m_path->setSegments(segments);
        m_path->setVisible(true);

        // The contextual transition of the node (zadani_gui.md 4.16): only when a tool edits it
        const PDFAppEditContentRegistry* registry = context->getRegistry();
        const bool hasEditor = registry && registry->findEditorTool(node, *container);
        m_note->setVisible(hasEditor);
        if (hasEditor)
        {
            m_note->setText(node.getKind() == PDFContentNodeKind::Path ? tr("Double-click the path, or press Enter, to edit its nodes.")
                                                                       : tr("Double-click the object, or press Enter, to edit it."));
        }
    }

    void showMany(const std::vector<PDFAppEditContentCardTools::SelectedNode>& nodes, int count)
    {
        m_section->setBadgeText(QString());
        m_title->setText(tr("%Ln object(s)", nullptr, count));

        // The kinds of the first objects (a list of 100 000 kinds would say nothing)
        constexpr size_t MAXIMUM_LISTED = 6;
        QStringList kinds;
        for (size_t i = 0; i < nodes.size() && i < MAXIMUM_LISTED; ++i)
        {
            kinds << PDFAppEditContentCardTools::getKindTitle(*nodes[i].node);
        }
        QString list = kinds.join(QStringLiteral(", "));
        if (size_t(count) > MAXIMUM_LISTED)
        {
            list += QStringLiteral(", …");
        }
        m_subtitle->setText(list);
        m_subtitle->setVisible(true);
        m_path->setVisible(false);
        m_note->setText(tr("A value that differs between the objects shows as Mixed; typing a value sets it for all of them."));
        m_note->setVisible(true);
    }

    void onSegmentActivated(int index)
    {
        // A link of the path selects that level (the last segment is the node itself, no link)
        PDFAppEditContentContext* context = getContext();
        if (!context || index < 0 || size_t(index) >= m_targets.size())
        {
            return;
        }

        const Target target = m_targets[size_t(index)];
        if (target.node.isValid())
        {
            context->getSelection()->setNode(target.node);
        }
        else if (target.leaveLevels > 0)
        {
            context->requestLeaveIsolation(target.leaveLevels);
        }
        else
        {
            context->getSelection()->clear();
        }
    }

    PDFExpanderCard* m_section = nullptr;
    PDFLabel* m_title = nullptr;
    PDFLabel* m_subtitle = nullptr;
    PDFPathBar* m_path = nullptr;
    PDFLabel* m_note = nullptr;
    std::vector<Target> m_targets;
};

/// The card Locked object (-locked-click): the clicked object is locked; Unlock
class PDFAppEditContentLockedObjectCard : public PDFAppEditContentCard
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFAppEditContentLockedObjectCard)

public:
    explicit PDFAppEditContentLockedObjectCard(PDFAppEditContentContext* context, QWidget* parent) :
        PDFAppEditContentCard(context, QLatin1String(PDFAppEditContentCardIds::LOCKED_OBJECT), parent)
    {
        PDFExpanderCard* section = addSection(tr("Locked object"));
        QVBoxLayout* layout = nullptr;
        QWidget* content = PDFAppEditContentCardTools::createColumn(section, &layout);
        m_text = PDFAppEditContentCardTools::createLabel(QString(), PDFThemeFonts::Role::Small, PDFThemeColors::TextSecondary, content);
        m_text->setObjectName(QStringLiteral("editContentLockedText"));
        m_unlock = new QPushButton(tr("Unlock"), content);
        m_unlock->setObjectName(QStringLiteral("editContentUnlock"));
        QHBoxLayout* actions = new QHBoxLayout();
        actions->setContentsMargins(0, 0, 0, 0);
        actions->addWidget(m_unlock);
        actions->addStretch(1);
        layout->addWidget(m_text);
        layout->addLayout(actions);
        section->setContentWidget(content);

        connect(m_unlock, &QPushButton::clicked, this, &PDFAppEditContentLockedObjectCard::onUnlockClicked);
    }

    virtual void updateCard() override
    {
        const PDFAppEditContentContext* context = getContext();
        if (!context)
        {
            return;
        }

        const PDFContentNodeReference& reference = context->getLockedClickNode();
        const PDFContentContainerPointer container = context->getSession()->getContainer(reference.container);
        const PDFContentNodePointer node = container ? container->getNode(reference.node) : PDFContentNodePointer();
        const QString kind = node ? PDFAppEditContentCardTools::getKindTitle(*node) : tr("The object");

        // The key of the action Lock, as the registry has it (the frame shows the key of a draft)
        QString key;
        if (const PDFAppEditContentActionEntry* lock = context->getRegistry()->findAction(QLatin1String(PDFAppEditContentActionIds::LOCK)))
        {
            const std::vector<PDFAppEditContentActionEntry::Key> keys = lock->getKeys();
            if (!keys.empty())
            {
                key = keys.front().sequence.toString(QKeySequence::NativeText);
            }
        }

        if (key.isEmpty())
        {
            m_text->setText(tr("%1 is locked. Unlock it here, or by the lock in its row of the Objects tab.").arg(kind));
        }
        else
        {
            m_text->setText(tr("%1 is locked. Unlock it here, by the lock in its row of the Objects tab, or select the row and press %2.").arg(kind, key));
        }
        m_unlock->setEnabled(node && context->getSession()->isNodeLocked(reference));
    }

private:
    void onUnlockClicked()
    {
        // The lock is a state of the session, no command (QE.8); the card goes with the next rebuild of the stack
        PDFAppEditContentContext* context = getContext();
        if (!context)
        {
            return;
        }

        const PDFContentNodeReference reference = context->getLockedClickNode();
        QPointer<PDFAppEditContentContext> guard(context);
        context->getSession()->setNodeLocked(reference, false);
        if (guard && !PDFDeferredDeletion::isRetired(guard))
        {
            guard->setLockedClickNode(PDFContentNodeReference());
        }
        if (guard && !PDFDeferredDeletion::isRetired(guard))
        {
            guard->setCardRequested(QLatin1String(PDFAppEditContentCardIds::LOCKED_OBJECT), false);
        }
    }

    PDFLabel* m_text = nullptr;
    QPushButton* m_unlock = nullptr;
};

/// The card Annotations and fields (-annotations-underlay, -annotations-hidden): the dimmed underlay
class PDFAppEditContentAnnotationsCard : public PDFAppEditContentCard
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFAppEditContentAnnotationsCard)

public:
    explicit PDFAppEditContentAnnotationsCard(PDFAppEditContentContext* context, QWidget* parent) :
        PDFAppEditContentCard(context, QLatin1String(PDFAppEditContentCardIds::ANNOTATIONS), parent)
    {
        PDFExpanderCard* section = addSection(tr("Annotations and fields"));
        QVBoxLayout* layout = nullptr;
        QWidget* content = PDFAppEditContentCardTools::createColumn(section, &layout);
        m_underlay = new QCheckBox(tr("Show as a dimmed underlay"), content);
        m_underlay->setObjectName(QStringLiteral("editContentAnnotationsUnderlay"));
        layout->addWidget(m_underlay);
        layout->addWidget(PDFAppEditContentCardTools::createNote(tr("Comments, links and form fields are not page content. They are drawn dimmed so that you can see where "
                                                                    "they are; they cannot be selected here, and a link does not move with the text under it."), content));
        section->setContentWidget(content);

        connect(m_underlay, &QCheckBox::clicked, this, &PDFAppEditContentAnnotationsCard::onUnderlayClicked);
    }

    virtual void updateCard() override
    {
        if (const PDFAppEditContentContext* context = getContext())
        {
            m_underlay->setChecked(context->getOptions()->getBool(QLatin1String(PDFAppEditContentOptions::SHOW_ANNOTATIONS)));
        }
    }

private:
    void onUnderlayClicked(bool isChecked)
    {
        // A display option: no command, no undo step (zadani_gui.md 4.26)
        if (PDFAppEditContentContext* context = getContext())
        {
            context->getOptions()->setValue(QLatin1String(PDFAppEditContentOptions::SHOW_ANNOTATIONS), isChecked);
        }
    }

    QCheckBox* m_underlay = nullptr;
};

/// The card Preview (-preview): the options of the exact rendering
class PDFAppEditContentPreviewCard : public PDFAppEditContentCard
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFAppEditContentPreviewCard)

public:
    explicit PDFAppEditContentPreviewCard(PDFAppEditContentContext* context, QWidget* parent) :
        PDFAppEditContentCard(context, QLatin1String(PDFAppEditContentCardIds::PREVIEW), parent)
    {
        PDFExpanderCard* section = addSection(tr("Preview"));
        QVBoxLayout* layout = nullptr;
        QWidget* content = PDFAppEditContentCardTools::createColumn(section, &layout);
        m_overprint = new QCheckBox(tr("Simulate overprint"), content);
        m_overprint->setObjectName(QStringLiteral("editContentPreviewOverprint"));
        m_annotations = new QCheckBox(tr("Show annotations and fields"), content);
        m_annotations->setObjectName(QStringLiteral("editContentPreviewAnnotations"));
        m_paper = new PDFComboBox(content);
        m_paper->setObjectName(QStringLiteral("editContentPreviewPaper"));
        m_paper->setCompact(true);
        m_paper->addItem(tr("White"), QStringLiteral("#ffffff"));
        m_paper->addItem(tr("Light gray"), QStringLiteral("#ebebeb"));
        m_paper->addItem(tr("Cream"), QStringLiteral("#f8f3e6"));
        layout->addWidget(m_overprint);
        layout->addWidget(m_annotations);
        layout->addWidget(PDFAppEditContentCardTools::createFieldRow(tr("Paper color"), m_paper, content, 150));
        layout->addWidget(PDFAppEditContentCardTools::createNote(tr("The editing view draws every object with simple painting so that it stays easy to select and move. "
                                                                    "Preview shows the true result before you apply the changes."), content));
        section->setContentWidget(content);

        connect(m_overprint, &QCheckBox::clicked, this, &PDFAppEditContentPreviewCard::onOverprintClicked);
        connect(m_annotations, &QCheckBox::clicked, this, &PDFAppEditContentPreviewCard::onAnnotationsClicked);
        connect(m_paper, &PDFComboBox::activated, this, &PDFAppEditContentPreviewCard::onPaperActivated);
    }

    virtual void updateCard() override
    {
        const PDFAppEditContentContext* context = getContext();
        if (!context)
        {
            return;
        }

        const PDFAppEditContentOptions* options = context->getOptions();
        m_overprint->setChecked(options->getBool(QLatin1String(PDFAppEditContentOptions::PREVIEW_OVERPRINT)));
        m_annotations->setChecked(options->getBool(QLatin1String(PDFAppEditContentOptions::PREVIEW_ANNOTATIONS)));

        // A color of the stored parameter, which is none of the items, is shown as its own item
        const QString paper = options->getValue(QLatin1String(PDFAppEditContentOptions::PREVIEW_PAPER_COLOR)).toString().toLower();
        int index = m_paper->findData(paper);
        if (index < 0 && QColor::isValidColorName(paper))
        {
            m_paper->addItem(paper, paper);
            index = m_paper->count() - 1;
        }
        m_paper->setCurrentIndex(index);
    }

private:
    void onOverprintClicked(bool isChecked)
    {
        if (PDFAppEditContentContext* context = getContext())
        {
            context->getOptions()->setValue(QLatin1String(PDFAppEditContentOptions::PREVIEW_OVERPRINT), isChecked);
        }
    }

    void onAnnotationsClicked(bool isChecked)
    {
        if (PDFAppEditContentContext* context = getContext())
        {
            context->getOptions()->setValue(QLatin1String(PDFAppEditContentOptions::PREVIEW_ANNOTATIONS), isChecked);
        }
    }

    void onPaperActivated(int index)
    {
        PDFAppEditContentContext* context = getContext();
        if (context && index >= 0)
        {
            context->getOptions()->setValue(QLatin1String(PDFAppEditContentOptions::PREVIEW_PAPER_COLOR), m_paper->itemData(index).toString());
        }
    }

    QCheckBox* m_overprint = nullptr;
    QCheckBox* m_annotations = nullptr;
    PDFComboBox* m_paper = nullptr;
};

// ---------------------------------------------------------------- the entries of this file

/// The entry of the card Selection: always the first card, also with nothing selected (not in Preview)
class PDFAppEditContentSelectionCardEntry : public PDFAppEditContentCardEntry
{
public:
    virtual QString getId() const override { return QLatin1String(PDFAppEditContentCardIds::SELECTION); }
    virtual bool isShown(const PDFAppEditContentContext* context) const override { return context && !context->isPreviewActive(); }
    virtual PDFAppEditContentCard* createCard(PDFAppEditContentContext* context, QWidget* parent) const override { return new PDFAppEditContentSelectionCard(context, parent); }
};

/// The entry of the card Locked object: requested by a click on a locked object (P11 sets the node)
class PDFAppEditContentLockedObjectCardEntry : public PDFAppEditContentCardEntry
{
public:
    virtual QString getId() const override { return QLatin1String(PDFAppEditContentCardIds::LOCKED_OBJECT); }

    virtual bool isShown(const PDFAppEditContentContext* context) const override
    {
        return PDFAppEditContentCardTools::isSelectionStateShown(context) && context->isCardRequested(getId()) && context->getLockedClickNode().isValid();
    }

    virtual PDFAppEditContentCard* createCard(PDFAppEditContentContext* context, QWidget* parent) const override { return new PDFAppEditContentLockedObjectCard(context, parent); }
};

/// The entry of the card Annotations and fields: nothing selected on a page with annotations
class PDFAppEditContentAnnotationsCardEntry : public PDFAppEditContentCardEntry
{
public:
    virtual QString getId() const override { return QLatin1String(PDFAppEditContentCardIds::ANNOTATIONS); }

    virtual bool isShown(const PDFAppEditContentContext* context) const override
    {
        if (!PDFAppEditContentCardTools::isSelectionStateShown(context) || !context->getSelection()->isEmpty())
        {
            return false;
        }

        const PDFDocument* document = context->getSession()->getDocument();
        const PDFInteger pageIndex = context->getPageIndex();
        return document && pageIndex >= 0 && pageIndex < PDFInteger(document->getCatalog()->getPageCount()) &&
               !document->getCatalog()->getPage(size_t(pageIndex))->getAnnotations().empty();
    }

    virtual PDFAppEditContentCard* createCard(PDFAppEditContentContext* context, QWidget* parent) const override { return new PDFAppEditContentAnnotationsCard(context, parent); }
};

/// The entry of the card Preview: Preview is on (it is the only card then)
class PDFAppEditContentPreviewCardEntry : public PDFAppEditContentCardEntry
{
public:
    virtual QString getId() const override { return QLatin1String(PDFAppEditContentCardIds::PREVIEW); }
    virtual bool isShown(const PDFAppEditContentContext* context) const override { return context && context->isPreviewActive(); }
    virtual PDFAppEditContentCard* createCard(PDFAppEditContentContext* context, QWidget* parent) const override { return new PDFAppEditContentPreviewCard(context, parent); }
};

// ---------------------------------------------------------------- PDFAppEditContentCardStack

PDFAppEditContentCardStack::PDFAppEditContentCardStack(PDFAppEditContentContext* context, QWidget* parent) :
    BaseClass(parent),
    PDFRetirable(this),
    m_context(context)
{
    setObjectName(QStringLiteral("editContentCards"));
    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(PDFAppEditContentCard::SECTION_GAP);
    m_layout->addStretch(1);

    if (!context)
    {
        return;
    }

    // Every change only schedules the rebuild (from the event loop). The context and its children live longer than
    // the stack (a part): the connections are cut by the retire of the stack.
    PDFDeferredDeletion::connect(context, &PDFAppEditContentContext::cardsChanged, this, &PDFAppEditContentCardStack::onStateChanged);
    PDFDeferredDeletion::connect(context, &PDFAppEditContentContext::previewActiveChanged, this, &PDFAppEditContentCardStack::onStateChanged);
    PDFDeferredDeletion::connect(context, &PDFAppEditContentContext::editingAllowedChanged, this, &PDFAppEditContentCardStack::onStateChanged);
    PDFDeferredDeletion::connect(context, &PDFAppEditContentContext::pageChanged, this, &PDFAppEditContentCardStack::onStateChanged);
    PDFDeferredDeletion::connect(context, &PDFAppEditContentContext::containerChanged, this, &PDFAppEditContentCardStack::onStateChanged);
    PDFDeferredDeletion::connect(context->getSelection(), &PDFAppEditContentSelection::changed, this, &PDFAppEditContentCardStack::onStateChanged);
    PDFDeferredDeletion::connect(context->getOptions(), &PDFAppEditContentOptions::changed, this, &PDFAppEditContentCardStack::onStateChanged);
    PDFDeferredDeletion::connect(context->getSession(), &PDFContentEditSession::modelChanged, this, &PDFAppEditContentCardStack::onStateChanged);
    PDFDeferredDeletion::connect(context->getSession(), &PDFContentEditSession::editorStateChanged, this, &PDFAppEditContentCardStack::onStateChanged);

    // The first cards now: the stack is created by the shell, not inside of a signal of a card
    onRebuild();
}

PDFAppEditContentCardStack::~PDFAppEditContentCardStack() = default;

QStringList PDFAppEditContentCardStack::getCardIds() const
{
    QStringList ids;
    for (const QPointer<PDFAppEditContentCard>& card : m_cards)
    {
        if (card)
        {
            ids << card->getCardId();
        }
    }
    return ids;
}

PDFAppEditContentCard* PDFAppEditContentCardStack::getCard(const QString& cardId) const
{
    for (const QPointer<PDFAppEditContentCard>& card : m_cards)
    {
        if (card && card->getCardId() == cardId)
        {
            return card.data();
        }
    }
    return nullptr;
}

void PDFAppEditContentCardStack::registerParts(PDFAppEditContentRegistry* registry)
{
    registry->addCard(std::make_unique<PDFAppEditContentSelectionCardEntry>());
    registry->addCard(std::make_unique<PDFAppEditContentLockedObjectCardEntry>());
    registry->addCard(std::make_unique<PDFAppEditContentAnnotationsCardEntry>());
    registry->addCard(std::make_unique<PDFAppEditContentPreviewCardEntry>());
}

void PDFAppEditContentCardStack::retire()
{
    // The scheduled rebuild is dropped (step 8 of the retire removes the posted call as well)
    m_isRebuildScheduled = false;
}

void PDFAppEditContentCardStack::scheduleRebuild()
{
    if (m_isRebuildScheduled || PDFDeferredDeletion::isRetired(this))
    {
        return;
    }

    m_isRebuildScheduled = true;
    QMetaObject::invokeMethod(this, &PDFAppEditContentCardStack::onRebuild, Qt::QueuedConnection);
}

void PDFAppEditContentCardStack::onStateChanged()
{
    scheduleRebuild();
}

void PDFAppEditContentCardStack::onRebuild()
{
    m_isRebuildScheduled = false;
    if (!m_context || PDFDeferredDeletion::isRetired(this) || PDFDeferredDeletion::isRetired(m_context))
    {
        return;
    }

    setEnabled(!m_context->isRunning());

    // The cards of the entries shown, in the fixed order of the registry; a card shown before is kept
    std::vector<QPointer<PDFAppEditContentCard>> cards;
    std::vector<QPointer<PDFAppEditContentCard>> created;
    for (const PDFAppEditContentCardEntry* entry : m_context->getRegistry()->getCards())
    {
        if (!entry->isShown(m_context))
        {
            continue;
        }

        PDFAppEditContentCard* card = getCard(entry->getId());
        if (!card)
        {
            card = entry->createCard(m_context, this);
            if (!card)
            {
                continue;
            }
            created.emplace_back(card);
        }
        cards.emplace_back(card);
    }

    // The cards, which are not shown any more, go (the rebuild runs from the event loop, never inside of a signal of
    // a card)
    for (const QPointer<PDFAppEditContentCard>& card : m_cards)
    {
        if (card && std::find(cards.cbegin(), cards.cend(), card) == cards.cend())
        {
            m_layout->removeWidget(card);
            PDFDeferredDeletion::retireLater(card);
        }
    }
    m_cards = cards;

    // The order of the layout: the cards from the top, then the stretch
    for (int i = 0; i < int(m_cards.size()); ++i)
    {
        m_layout->removeWidget(m_cards[size_t(i)]);
        m_layout->insertWidget(i, m_cards[size_t(i)]);
    }

    // Every card reads the state again; a card may not survive a foreign slot of an update (a QPointer is checked)
    const std::vector<QPointer<PDFAppEditContentCard>> shown = m_cards;
    for (const QPointer<PDFAppEditContentCard>& card : shown)
    {
        if (card && !PDFDeferredDeletion::isRetired(card))
        {
            card->updateCard();
            if (!card || !m_context)
            {
                continue;
            }

            // The fields, which change the model, are disabled while the editing is paused (a run, a read-only page);
            // the cards of the display options and the Selection stay usable
            const QString& cardId = card->getCardId();
            const bool isDisplayCard = cardId == QLatin1String(PDFAppEditContentCardIds::PREVIEW) || cardId == QLatin1String(PDFAppEditContentCardIds::SELECTION) ||
                                       cardId == QLatin1String(PDFAppEditContentCardIds::ANNOTATIONS);
            card->setEnabled(isDisplayCard || m_context->isEditingAllowed());
            card->show();
        }
    }
}

}   // namespace pdf

#include "moc_pdfappeditcontentcards.cpp"
