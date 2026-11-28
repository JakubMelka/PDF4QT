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

#include "pdfappeditcontentpanels_objects.h"

#include "pdfappdocumentpanels_common.h"
#include "pdfappeditcontentcontext.h"
#include "pdfappeditcontentregistry.h"
#include "pdfappeditcontentselection.h"
#include "pdfappeditcontenttools.h"
#include "pdfapplocale.h"
#include "pdfappworkspace.h"

#include "pdfcomponentdelegates.h"
#include "pdfcomponentpainter.h"
#include "pdffluentwidgets.h"
#include "pdfstylepainter.h"
#include "pdfthemecolors.h"
#include "pdfthemefonts.h"
#include "pdfthemeiconengine.h"

#include "pdfcatalog.h"
#include "pdfcontenteditsession.h"
#include "pdfdocument.h"
#include "pdffont.h"
#include "pdfpainterutils.h"

#include <QCoreApplication>
#include <QDataStream>
#include <QHBoxLayout>
#include <QIODevice>
#include <QItemSelection>
#include <QKeyEvent>
#include <QMimeData>
#include <QPainter>
#include <QPainterPath>
#include <QTimer>
#include <QToolButton>
#include <QTransform>
#include <QVBoxLayout>

#include <algorithm>
#include <map>
#include <memory>

namespace pdf
{

/// The entry of the tab Objects (zadani_gui.md 4.18): the model of the tree lives as long as the tool
class PDFAppEditContentObjectsTabEntry : public PDFAppEditContentTabEntry
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFAppEditContentObjectsTabEntry)

public:
    virtual QString getId() const override { return QString::fromLatin1(TAB_OBJECTS); }
    virtual int getOrder() const override { return 100; }
    virtual QString getTitle() const override { return tr("Objects"); }
    virtual QString getIconName() const override { return QStringLiteral("object-tree"); }

    virtual QObject* createState(PDFAppEditContentContext* context, QObject* parent) const override
    {
        return new PDFAppEditContentObjectsModel(context, parent);
    }

    virtual QWidget* createPanel(PDFAppEditContentContext* context, QObject* state, QWidget* parent) const override
    {
        return new PDFAppEditContentObjectsPanel(context, qobject_cast<PDFAppEditContentObjectsModel*>(state), parent);
    }
};

/// Helpers of the rows: icons, texts read from the nodes and the document, the drag data
class PDFAppEditContentObjectsHelper
{
public:
    PDFAppEditContentObjectsHelper() = delete;

    /// Levels of the walks over the tree (the builder goes to PDFContentContainer::MAXIMUM_DEPTH)
    static constexpr int MAXIMUM_DEPTH = PDFContentContainer::MAXIMUM_DEPTH + 2;

    static QString getIconName(const PDFContentNode& node, const PDFDocument* document)
    {
        switch (node.getKind())
        {
            case PDFContentNodeKind::TextObject:
                return QStringLiteral("type");
            case PDFContentNodeKind::Path:
                return node.getData().paint.isClippingPath() ? QStringLiteral("square") : QStringLiteral("bezier-pen");
            case PDFContentNodeKind::Image:
                return QStringLiteral("image");
            case PDFContentNodeKind::Shading:
                return QStringLiteral("gradient");
            case PDFContentNodeKind::FormUse:
                return isTransparencyGroup(node, document) ? QStringLiteral("transparency-group") : QStringLiteral("xobject-form");
            case PDFContentNodeKind::Group:
                return PDFAppEditContentObjectsModel::isClippingGroup(node) ? QStringLiteral("clip-path") : QStringLiteral("group");
            case PDFContentNodeKind::MarkedContent:
                return PDFAppEditContentObjectsModel::isLayerContent(node) ? QStringLiteral("layers") : QStringLiteral("marked-content");
            case PDFContentNodeKind::MarkedPoint:
                return QStringLiteral("marked-content");
            case PDFContentNodeKind::Opaque:
                return QStringLiteral("help-circle");
            default:
                return QStringLiteral("object");
        }
    }

    /// The dictionary of an object of the document (a stream gives its dictionary)
    static const PDFDictionary* getDictionary(const PDFDocument* document, PDFObjectReference reference)
    {
        if (!document || !reference.isValid())
        {
            return nullptr;
        }
        const PDFObject& object = document->getObjectByReference(reference);
        if (object.isStream())
        {
            return object.getStream()->getDictionary();
        }
        return object.isDictionary() ? object.getDictionary() : nullptr;
    }

    /// The form of the use has a transparency group (/Group << /S /Transparency >>, ISO 32000-2, 11.6.6)
    static bool isTransparencyGroup(const PDFContentNode& node, const PDFDocument* document)
    {
        const PDFDictionary* form = getDictionary(document, node.getData().xobject);
        if (!form)
        {
            return false;
        }
        const PDFObject& group = document->getObject(form->get("Group"));
        if (!group.isDictionary())
        {
            return false;
        }
        const PDFObject& subtype = document->getObject(group.getDictionary()->get("S"));
        return subtype.isName() && subtype.getString() == "Transparency";
    }

    /// The previous glyph of a walk over a text (the line rule of PDFContentDisplay::getText)
    struct GlyphLine
    {
        bool hasPrevious = false;
        QTransform matrix;              ///< Tm before the previous glyph
        PDFReal fontSize = 0.0;         ///< |Tfs| of the previous glyph
        bool isVertical = false;        ///< The writing mode of its font
    };

    /// The glyph of the text matrix \p matrix starts a new line (PDFContentDisplayHelper::splitLines, the relative
    /// tolerance of a baseline 0.1 % of the font size, zadani_jadro.md 4.7): the first glyph, a glyph of another
    /// writing mode, a glyph after a glyph of a singular matrix, a glyph whose text space is not the text space of the
    /// previous one moved along the writing direction (another baseline, a rotation, a move backwards of the axis)
    static bool isNewLine(const GlyphLine& previous, const QTransform& matrix, bool isVertical)
    {
        constexpr PDFReal TOLERANCE = 0.001;
        if (!previous.hasPrevious || previous.isVertical != isVertical || !previous.matrix.isInvertible())
        {
            return true;
        }

        // The text space of the glyph in the text space of the previous one; the CTM is the same for the whole text
        // object (cm is not allowed in a text object, ISO 32000-2, 8.2, Figure 9), so it drops out
        const QTransform relative = matrix * previous.matrix.inverted();
        const PDFReal limit = TOLERANCE * previous.fontSize;
        if (!isVertical)
        {
            return !(qAbs(relative.dy()) <= limit && relative.m11() > 0.0 && qAbs(relative.m12()) <= TOLERANCE * relative.m11());
        }
        return !(qAbs(relative.dx()) <= limit && relative.m22() > 0.0 && qAbs(relative.m21()) <= TOLERANCE * relative.m22());
    }

    /// Walks the glyphs of a text object in the order of the content: its text up to
    /// PDFAppEditContentObjectsModel::MAXIMUM_TEXT_LENGTH characters (\p text, a space between two lines and at a
    /// large move of TJ) and the number of its lines (\p lineCount; then every glyph is visited). Bounds: every node of
    /// the subtree once, PDFContentContainer::MAXIMUM_DEPTH + 2 levels.
    static void walkText(const PDFContentNode& textObject, QString* text, int* lineCount)
    {
        constexpr int MAXIMUM_LENGTH = PDFAppEditContentObjectsModel::MAXIMUM_TEXT_LENGTH;

        GlyphLine line;
        int lines = 0;
        std::vector<std::pair<const PDFContentNode*, int>> stack;
        stack.emplace_back(&textObject, 0);
        while (!stack.empty() && (lineCount || (text && text->size() < MAXIMUM_LENGTH)))
        {
            const auto [node, depth] = stack.back();
            stack.pop_back();
            if (node->getKind() == PDFContentNodeKind::TextShow)
            {
                const PDFContentTextState& state = node->getData().textState;
                const bool isVertical = state.font && state.font->getCMap() && state.font->getCMap()->isVertical();
                const PDFReal fontSize = qAbs(state.fontSize);
                for (const PDFContentTextItem& item : node->getData().textItems)
                {
                    const bool isTextFull = !text || text->size() >= MAXIMUM_LENGTH;
                    if (isTextFull && !lineCount)
                    {
                        break;
                    }
                    if (item.kind != PDFContentTextItem::Kind::Glyph)
                    {
                        if (!isTextFull && item.adjustment < -250.0 && !text->isEmpty() && !text->endsWith(QLatin1Char(' ')))
                        {
                            // A large move of TJ is a gap between words
                            *text += QLatin1Char(' ');
                        }
                        continue;
                    }

                    if (isNewLine(line, item.textMatrix, isVertical))
                    {
                        ++lines;
                        if (!isTextFull && line.hasPrevious && !text->isEmpty() && !text->endsWith(QLatin1Char(' ')))
                        {
                            *text += QLatin1Char(' ');
                        }
                    }
                    line.hasPrevious = true;
                    line.matrix = item.textMatrix;
                    line.fontSize = fontSize;
                    line.isVertical = isVertical;
                    if (text && text->size() < MAXIMUM_LENGTH)
                    {
                        *text += item.unicode;
                    }
                }
                continue;
            }
            if (depth >= MAXIMUM_DEPTH)
            {
                continue;
            }
            const std::vector<PDFContentNodePointer>& children = node->getChildren();
            for (auto it = children.crbegin(); it != children.crend(); ++it)
            {
                stack.emplace_back(it->get(), depth + 1);
            }
        }
        if (lineCount)
        {
            *lineCount = lines;
        }
    }

    /// The text of a text object (up to PDFAppEditContentObjectsModel::MAXIMUM_TEXT_LENGTH characters)
    static QString getText(const PDFContentNode& textObject)
    {
        QString text;
        walkText(textObject, &text, nullptr);
        return text.simplified();
    }

    /// The indices of the node and of its ancestors from the root (the order of the content of
    /// two nodes is the order of these lists)
    static std::vector<PDFInteger> getPosition(const PDFContentContainer& container, PDFContentNodeId nodeId)
    {
        std::vector<PDFInteger> position;
        PDFContentNodeId current = nodeId;
        for (int depth = 0; depth < MAXIMUM_DEPTH + 2 && current.isValid(); ++depth)
        {
            const PDFInteger index = container.getIndexInParent(current);
            if (index < 0)
            {
                break;
            }
            position.push_back(index);
            const PDFContentNodePointer parent = container.getParent(current);
            current = parent ? parent->getId() : PDFContentNodeId();
        }
        std::reverse(position.begin(), position.end());
        return position;
    }

    /// The node \p nodeId is \p ancestorId or lies inside of it
    static bool isInside(const PDFContentContainer& container, PDFContentNodeId nodeId, PDFContentNodeId ancestorId)
    {
        PDFContentNodeId current = nodeId;
        for (int depth = 0; depth < MAXIMUM_DEPTH + 2 && current.isValid(); ++depth)
        {
            if (current == ancestorId)
            {
                return true;
            }
            const PDFContentNodePointer parent = container.getParent(current);
            current = parent ? parent->getId() : PDFContentNodeId();
        }
        return false;
    }

    static bool contains(const QString& text, const QString& part)
    {
        return text.contains(part, Qt::CaseInsensitive);
    }

    static PDFAppEditContentObjectsModel::FilterKind getFilterKind(const PDFContentNode& node)
    {
        using K = PDFAppEditContentObjectsModel::FilterKind;
        switch (node.getKind())
        {
            case PDFContentNodeKind::TextObject:
                return K::Text;
            case PDFContentNodeKind::Path:
                return K::Path;
            case PDFContentNodeKind::Image:
                return K::Image;
            case PDFContentNodeKind::Shading:
                return K::Shading;
            case PDFContentNodeKind::FormUse:
                return K::Form;
            case PDFContentNodeKind::Group:
            case PDFContentNodeKind::MarkedContent:
                return K::Group;
            default:
                return K::Other;
        }
    }

    /// The order of the node references, which counts its comparisons (the work of the tree selection, R7-3)
    struct CountingLess
    {
        quint64* counter = nullptr;

        bool operator()(const PDFContentNodeReference& left, const PDFContentNodeReference& right) const
        {
            ++*counter;
            return left < right;
        }
    };

    using CountedNodeSet = std::set<PDFContentNodeReference, CountingLess>;
};

/// The rows of the tree Objects (edit-content-v2-objects-nested 1647:22520): the icon of the kind, the kind, the
/// description, the badge (a tag of the structure, "New"), the toggles lock and eye at the trailing side (shown
/// permanently while on, else under the pointer); one height for every row (a tree of 100 000 rows)
class PDFAppEditContentObjectsDelegate : public PDFRowDelegate
{
private:
    using BaseClass = PDFRowDelegate;

public:
    using Model = PDFAppEditContentObjectsModel;

    static constexpr int ROW_HEIGHT = 30;
    static constexpr int ROW_RADIUS = 6;
    static constexpr int PADDING = 8;
    static constexpr int ICON = 14;
    static constexpr int GAP = 6;
    static constexpr int ACTION_EXTENT = 20;
    static constexpr int ACTION_GAP = 2;
    static constexpr int ACTION_MARGIN = 4;
    static constexpr int LOCK_ACTION = 1;
    static constexpr int HIDE_ACTION = 2;
    static constexpr const char* CLIP_BADGE_ICON = "clip-path";

    using BaseClass::BaseClass;

    virtual QList<Action> getActions(const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        QList<Action> actions;
        if (!index.isValid())
        {
            return actions;
        }

        const QRectF rect(option.rect);
        const bool isRightToLeft = option.direction == Qt::RightToLeft;
        const qreal top = rect.top() + qFloor((rect.height() - ACTION_EXTENT) / 2.0);
        for (int i = 0; i < 2; ++i)
        {
            const bool isLock = i == 0;
            const qreal offset = ACTION_MARGIN + i * (ACTION_EXTENT + ACTION_GAP);
            const qreal left = isRightToLeft ? rect.left() + offset : rect.right() + 1.0 - offset - ACTION_EXTENT;

            Action action;
            action.id = isLock ? LOCK_ACTION : HIDE_ACTION;
            action.rect = QRectF(left, top, ACTION_EXTENT, ACTION_EXTENT);
            action.checkable = true;
            action.role = isLock ? Model::IsLockedRole : Model::IsHiddenRole;
            action.checked = index.data(action.role).toBool();
            action.text = isLock ? Model::tr("Lock") : Model::tr("Hide");
            action.toolTip = isLock ? (action.checked ? Model::tr("Unlock") : Model::tr("Lock")) : (action.checked ? Model::tr("Show") : Model::tr("Hide"));
            action.iconName = isLock ? QStringLiteral("unlock") : QStringLiteral("eye");
            action.checkedIconName = isLock ? QStringLiteral("lock") : QStringLiteral("eye-off");
            actions.append(action);
        }
        return actions;
    }

protected:
    virtual bool hasActions() const override { return true; }

    virtual QSize getItemSize(const QStyleOptionViewItem& option, const QModelIndex& index, int width) const override
    {
        Q_UNUSED(option);
        Q_UNUSED(index);
        return QSize(width > 0 ? width : 240, ROW_HEIGHT);
    }

    virtual void paintItem(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index, const PDFComponentPainter::State& state) const override
    {
        using Color = PDFThemeColors::Color;

        const QRectF rect(option.rect);
        const bool isEnabled = state.isEnabled();
        const bool isSelected = state.isSelected();
        {
            PDFPainterStateGuard guard(painter);
            painter->setRenderHint(QPainter::Antialiasing, true);
            std::optional<Color> fill;
            if (isSelected)
            {
                fill = state.isPressed() ? Color::BgAccentSubtlePressed : (state.isHovered() ? Color::BgAccentSubtleHover : Color::BgAccentSubtle);
            }
            else if (state.isPressed())
            {
                fill = Color::BgPressed;
            }
            else if (state.isHovered())
            {
                fill = Color::BgHover;
            }
            if (fill)
            {
                QPainterPath shape;
                shape.addRoundedRect(rect, ROW_RADIUS, ROW_RADIUS);
                painter->fillPath(shape, PDFStylePainter::color(*fill));
            }
        }

        // The trailing side: the toggles, which are shown, then the badge
        qreal right = rect.right() - PADDING;
        int shownActions = 0;
        for (const Action& action : getActions(option, index))
        {
            if (isActionShown(action, state, getActionFlags(option, index, action.id)))
            {
                ++shownActions;
            }
        }
        if (shownActions > 0)
        {
            right = rect.right() - ACTION_MARGIN - 2 * ACTION_EXTENT - ACTION_GAP - GAP;
        }

        const QString badge = index.data(Model::BadgeRole).toString();
        if (!badge.isEmpty())
        {
            const PDFComponentPainter::Tag tag{ badge };
            const QSize tagSize = PDFComponentPainter::getTagSize(tag);
            const QRectF tagRect(right - tagSize.width(), rect.top() + (rect.height() - tagSize.height()) / 2.0, tagSize.width(), tagSize.height());
            if (tagRect.left() > rect.left() + PADDING + ICON + 3 * GAP + 40)
            {
                PDFComponentPainter::paintTag(painter, state.visualRect(rect, tagRect), tag, PDFComponentPainter::State(isEnabled ? QStyle::State_Enabled : QStyle::State_None, state.direction));
                right = tagRect.left() - GAP;
            }
        }

        // The clipping badge (zadani_gui.md 4.18): a text or a path, which sets a clipping path for the content behind
        // it; the kind alone tells only a path, which clips and paints nothing
        if (index.data(Model::IsClippingRole).toBool())
        {
            const QRectF clipRect(right - ICON, rect.top() + (rect.height() - ICON) / 2.0, ICON, ICON);
            if (clipRect.left() > rect.left() + PADDING + ICON + 3 * GAP + 40)
            {
                const Color clipColor = !isEnabled ? Color::IconDisabled : (isSelected ? Color::IconAccent : Color::IconMuted);
                PDFThemeIcons::paint(painter, state.visualRect(rect, clipRect), QString::fromLatin1(CLIP_BADGE_ICON), PDFStylePainter::color(clipColor));
                right = clipRect.left() - GAP;
            }
        }

        // The icon of the kind
        const QRectF iconRect(rect.left() + PADDING, rect.top() + (rect.height() - ICON) / 2.0, ICON, ICON);
        const Color iconColor = !isEnabled ? Color::IconDisabled : (isSelected ? Color::IconAccent : Color::IconDefault);
        PDFThemeIcons::paint(painter, state.visualRect(rect, iconRect), index.data(PDFComponentRoles::IconNameRole).toString(), PDFStylePainter::color(iconColor));

        // The kind, then the description
        const QString kind = index.data(Model::KindTextRole).toString();
        const QString description = index.data(Model::DescriptionRole).toString();
        const QFont kindFont = PDFThemeFonts::getFont(PDFThemeFonts::Role::BodyMedium);
        const QFont descriptionFont = PDFThemeFonts::getFont(PDFThemeFonts::Role::Small);
        const qreal left = iconRect.right() + GAP;
        const qreal kindWidth = qMin(qMax(right - left, 0.0), QFontMetricsF(kindFont).horizontalAdvance(kind) + 1.0);
        const QRectF kindRect(left, rect.top(), kindWidth, rect.height());
        const Color kindColor = !isEnabled ? Color::TextDisabled : (isSelected ? Color::TextAccent : Color::TextPrimary);
        PDFComponentPainter::drawElidedText(painter, state, state.visualRect(rect, kindRect), kind, kindFont, kindColor);
        const qreal descriptionLeft = kindRect.right() + GAP;
        if (!description.isEmpty() && right - descriptionLeft > 12.0)
        {
            const QRectF descriptionRect(descriptionLeft, rect.top(), right - descriptionLeft, rect.height());
            PDFComponentPainter::drawElidedText(painter, state, state.visualRect(rect, descriptionRect), description, descriptionFont,
                                                isEnabled ? Color::TextSecondary : Color::TextDisabled, PDFComponentPainter::TextKind::Data);
        }

        paintToggleActions(painter, option, index, state);

        if (state.hasFocusRing())
        {
            PDFComponentPainter::paintFocusRingInside(painter, rect, PDFStylePainter::Radii(ROW_RADIUS));
        }
    }
};

// ---------------------------------------------------------------- PDFAppEditContentObjectsModel

PDFAppEditContentObjectsModel::PDFAppEditContentObjectsModel(PDFAppEditContentContext* context, QObject* parent) :
    BaseClass(parent),
    m_context(context)
{
    if (!context)
    {
        return;
    }

    // The context is the parent and the session a sibling: both are foreign senders
    PDFDeferredDeletion::connect(context, &PDFAppEditContentContext::pageChanged, this, &PDFAppEditContentObjectsModel::onPageChanged);
    PDFDeferredDeletion::connect(context, &PDFAppEditContentContext::containerChanged, this, &PDFAppEditContentObjectsModel::onContainerChanged);
    PDFDeferredDeletion::connect(context, &PDFAppEditContentContext::sessionReplaced, this, &PDFAppEditContentObjectsModel::onSessionReplaced);
    connectSession();
}

PDFAppEditContentObjectsModel::~PDFAppEditContentObjectsModel() = default;

void PDFAppEditContentObjectsModel::connectSession()
{
    if (m_session)
    {
        m_session->disconnect(this);
    }
    m_session = m_context ? m_context->getSession() : nullptr;
    if (m_session)
    {
        PDFDeferredDeletion::connect(m_session.data(), &PDFContentEditSession::containerLoaded, this, &PDFAppEditContentObjectsModel::onContainerLoaded);
        PDFDeferredDeletion::connect(m_session.data(), &PDFContentEditSession::modelChanged, this, &PDFAppEditContentObjectsModel::onModelChanged);
        PDFDeferredDeletion::connect(m_session.data(), &PDFContentEditSession::editorStateChanged, this, &PDFAppEditContentObjectsModel::onEditorStateChanged);
    }
}

void PDFAppEditContentObjectsModel::onSessionReplaced()
{
    // A new version of the document (PDFAppEditContentContext::reloadDocument): its nodes are other objects, the
    // expanded rows and the texts belong to the old one; the rows follow from the event loop (pageChanged follows)
    connectSession();
    m_expanded.clear();
    m_textCache.clear();
    m_isResetQueued = true;
    queueRefresh();
}

bool PDFAppEditContentObjectsModel::isClippingGroup(const PDFContentNode& node)
{
    if (node.getKind() != PDFContentNodeKind::Group)
    {
        return false;
    }
    for (const PDFContentNodePointer& child : node.getChildren())
    {
        if (child->getKind() != PDFContentNodeKind::StateRun && child->hasClipEffect())
        {
            return true;
        }
    }
    return false;
}

bool PDFAppEditContentObjectsModel::isLayerContent(const PDFContentNode& node)
{
    return node.getKind() == PDFContentNodeKind::MarkedContent && (node.getData().optionalContent.isValid() || node.getData().tag == "OC");
}

int PDFAppEditContentObjectsModel::getSegmentCount(const PDFContentNode& node)
{
    if (node.getKind() != PDFContentNodeKind::Path)
    {
        return 0;
    }

    // ISO 32000-2, 8.5.2.1: h appends a straight line from the current point to the start of the subpath; when the
    // subpath is closed already (the current point is its start), it does nothing
    int count = 0;
    QPointF start;
    QPointF current;
    for (const PDFContentPath::Segment& segment : node.getData().path.getSegments())
    {
        switch (segment.type)
        {
            case PDFContentPath::SegmentType::MoveTo:
                start = segment.points[0];
                current = start;
                break;
            case PDFContentPath::SegmentType::LineTo:
                ++count;
                current = segment.points[0];
                break;
            case PDFContentPath::SegmentType::CurveTo:
                ++count;
                current = segment.points[2];
                break;
            case PDFContentPath::SegmentType::Close:
                if (current != start)
                {
                    ++count;
                }
                current = start;
                break;
        }
    }
    return count;
}

int PDFAppEditContentObjectsModel::getLineCount(const PDFContentNode& textObject)
{
    if (textObject.getKind() != PDFContentNodeKind::TextObject)
    {
        return 0;
    }
    int lines = 0;
    PDFAppEditContentObjectsHelper::walkText(textObject, nullptr, &lines);
    return lines;
}

int PDFAppEditContentObjectsModel::getChildRowCount(const PDFContentNode& node)
{
    int count = 0;
    for (const PDFContentNodePointer& child : node.getChildren())
    {
        if (getRowNodeOfChild(*child))
        {
            ++count;
        }
    }
    return count;
}

void PDFAppEditContentObjectsModel::clearTextCache()
{
    m_textCache.clear();
}

const PDFAppEditContentObjectsModel::TextEntry& PDFAppEditContentObjectsModel::getTexts(const PDFContentContainer& container, quint64 id) const
{
    // Valid, while the container has the same node object (a node, which no command changed, is shared by the
    // versions); the entry holds the node, so its address is never reused meanwhile
    const PDFContentNodePointer node = container.getNode(PDFContentNodeId{ id });
    TextEntry& entry = m_textCache[id];
    if (entry.node != node)
    {
        ++m_textComputationCount;
        entry.node = node;
        entry.kind = node ? getKindText(*node, getDocument()) : QString();
        entry.description = node ? getDescription(*node, getDocument()) : QString();
    }
    return entry;
}

void PDFAppEditContentObjectsModel::pruneTextCache()
{
    for (auto it = m_textCache.begin(); it != m_textCache.end();)
    {
        if (!m_container || m_container->getNode(PDFContentNodeId{ it->first }) != it->second.node)
        {
            it = m_textCache.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

const PDFContentNode* PDFAppEditContentObjectsModel::getRowNodeOfChild(const PDFContentNode& child)
{
    switch (child.getKind())
    {
        case PDFContentNodeKind::StateRun:
        case PDFContentNodeKind::TextShow:
        case PDFContentNodeKind::Root:
            return nullptr;

        case PDFContentNodeKind::MarkedContent:
        {
            if (!child.getData().mcid.has_value() || isLayerContent(child))
            {
                return &child;
            }

            // A tagged sequence of one object: the object is the row and shows the tag
            const PDFContentNode* only = nullptr;
            for (const PDFContentNodePointer& grandChild : child.getChildren())
            {
                if (grandChild->getKind() == PDFContentNodeKind::StateRun)
                {
                    continue;
                }
                if (only || grandChild->getKind() == PDFContentNodeKind::MarkedContent || grandChild->getKind() == PDFContentNodeKind::TextShow)
                {
                    return &child;
                }
                only = grandChild.get();
            }
            return only ? only : &child;
        }

        default:
            return &child;
    }
}

PDFContentNodeId PDFAppEditContentObjectsModel::getPlaceId(const PDFContentContainer& container, PDFContentNodeId nodeId)
{
    const PDFContentNodePointer parent = container.getParent(nodeId);
    if (parent && parent->getKind() == PDFContentNodeKind::MarkedContent && getRowNodeOfChild(*parent) != parent.get())
    {
        return parent->getId();
    }
    return nodeId;
}

bool PDFAppEditContentObjectsModel::hasChildRows(const PDFContentNode& node)
{
    switch (node.getKind())
    {
        case PDFContentNodeKind::Root:
        case PDFContentNodeKind::Group:
        case PDFContentNodeKind::MarkedContent:
            return true;
        default:
            return false;
    }
}

QString PDFAppEditContentObjectsModel::getKindText(const PDFContentNode& node, const PDFDocument* document)
{
    using Helper = PDFAppEditContentObjectsHelper;

    // The kinds of zadani_gui.md 4.18 (edit-content-v2-objects-nested)
    switch (node.getKind())
    {
        case PDFContentNodeKind::TextObject:
            return tr("Text");
        case PDFContentNodeKind::Path:
            return node.getData().paint.isClippingPath() ? tr("Clipping path") : tr("Path");
        case PDFContentNodeKind::Image:
            return tr("Image");
        case PDFContentNodeKind::Shading:
            return tr("Shading");
        case PDFContentNodeKind::FormUse:
            return Helper::isTransparencyGroup(node, document) ? tr("Transparency group") : tr("Form XObject");
        case PDFContentNodeKind::Group:
            return isClippingGroup(node) ? tr("Clipping group") : tr("Group");
        case PDFContentNodeKind::MarkedContent:
            if (isLayerContent(node))
            {
                return tr("Layer content");
            }
            return node.getData().mcid.has_value() ? tr("Tagged content") : tr("Marked content");
        case PDFContentNodeKind::MarkedPoint:
            return tr("Marked content");
        default:
            return tr("Unknown content");
    }
}

QString PDFAppEditContentObjectsModel::getDescription(const PDFContentNode& node, const PDFDocument* document)
{
    using Helper = PDFAppEditContentObjectsHelper;

    const PDFContentNode::Data& data = node.getData();
    switch (node.getKind())
    {
        case PDFContentNodeKind::TextObject:
        {
            const int lines = getLineCount(node);
            if (lines > 1)
            {
                return tr("Paragraph · %Ln lines", nullptr, lines);
            }
            const QString text = Helper::getText(node);
            if (text.isEmpty())
            {
                return QString();
            }
            return QStringLiteral("“%1”").arg(text);
        }

        case PDFContentNodeKind::Path:
        {
            if (data.path.isRectangle())
            {
                return tr("Rectangle");
            }
            return tr("%Ln segments", nullptr, getSegmentCount(node));
        }

        case PDFContentNodeKind::Image:
        {
            if (data.isInlineImage)
            {
                return tr("Inline image");
            }
            const PDFDictionary* image = Helper::getDictionary(document, data.xobject);
            if (image)
            {
                PDFDocumentDataLoaderDecorator loader(document);
                const PDFInteger width = loader.readIntegerFromDictionary(image, "Width", 0);
                const PDFInteger height = loader.readIntegerFromDictionary(image, "Height", 0);
                if (width > 0 && height > 0)
                {
                    return tr("%1 × %2 px").arg(PDFAppLocale::formatNumber(width), PDFAppLocale::formatNumber(height));
                }
            }
            return QString::fromLatin1(data.resourceName);
        }

        case PDFContentNodeKind::Shading:
        case PDFContentNodeKind::FormUse:
            return QString::fromLatin1(data.resourceName);

        case PDFContentNodeKind::Group:
            return tr("%Ln objects", nullptr, getChildRowCount(node));

        case PDFContentNodeKind::MarkedContent:
        case PDFContentNodeKind::MarkedPoint:
        {
            if (data.optionalContent.isValid())
            {
                const PDFDictionary* layer = Helper::getDictionary(document, data.optionalContent);
                if (layer)
                {
                    PDFDocumentDataLoaderDecorator loader(document);
                    const QString name = loader.readTextStringFromDictionary(layer, "Name", QString());
                    if (!name.isEmpty())
                    {
                        return name;
                    }
                }
            }
            if (isLayerContent(node) && !data.optionalContent.isValid() && !data.propertiesName.isEmpty())
            {
                // An OCG, which the resources do not have: its name of /Properties
                return QString::fromLatin1(data.propertiesName);
            }
            if (data.isArtifact)
            {
                return tr("Artifact");
            }
            return QString::fromLatin1(data.tag);
        }

        case PDFContentNodeKind::Opaque:
            return data.opaqueReason;

        default:
            return QString();
    }
}

const PDFDocument* PDFAppEditContentObjectsModel::getDocument() const
{
    const PDFContentEditSession* session = m_context ? m_context->getSession() : nullptr;
    return session ? session->getDocument() : nullptr;
}

quint64 PDFAppEditContentObjectsModel::getNodeId(const QModelIndex& index) const
{
    if (!index.isValid())
    {
        return m_root.node.value;
    }
    if (index.model() != this)
    {
        return 0;
    }
    const auto it = m_serialToNode.find(index.internalId());
    return it != m_serialToNode.cend() ? it->second : 0;
}

int PDFAppEditContentObjectsModel::getRow(quint64 id) const
{
    const auto it = m_entries.find(id);
    if (it == m_entries.cend() || id == m_root.node.value)
    {
        return -1;
    }

    const quint64 parentId = it->second.parentId;
    if (m_dirtyParents.erase(parentId) > 0)
    {
        const auto parentIt = m_entries.find(parentId);
        if (parentIt != m_entries.cend())
        {
            const std::vector<quint64>& children = parentIt->second.children;
            for (size_t i = 0; i < children.size(); ++i)
            {
                const auto childIt = m_entries.find(children[i]);
                if (childIt != m_entries.cend())
                {
                    childIt->second.row = int(i);
                }
            }
        }
    }
    return it->second.row;
}

QModelIndex PDFAppEditContentObjectsModel::createParentIndex(quint64 id) const
{
    if (id == m_root.node.value)
    {
        return QModelIndex();
    }
    const auto it = m_entries.find(id);
    if (it == m_entries.cend())
    {
        return QModelIndex();
    }
    return createIndex(getRow(id), 0, it->second.serial);
}

const PDFContentNode* PDFAppEditContentObjectsModel::getRowNode(const QModelIndex& index) const
{
    if (!index.isValid() || !m_container)
    {
        return nullptr;
    }
    const quint64 id = getNodeId(index);
    if (id == 0)
    {
        return nullptr;
    }
    // The container keeps its nodes as long as it is held (m_container)
    return m_container->getNode(PDFContentNodeId{ id }).get();
}

QModelIndex PDFAppEditContentObjectsModel::index(int row, int column, const QModelIndex& parent) const
{
    if (column != 0 || row < 0 || parent.column() > 0)
    {
        return QModelIndex();
    }
    const auto it = m_entries.find(getNodeId(parent));
    if (it == m_entries.cend() || size_t(row) >= it->second.children.size())
    {
        return QModelIndex();
    }
    const auto childIt = m_entries.find(it->second.children[size_t(row)]);
    return childIt != m_entries.cend() ? createIndex(row, 0, childIt->second.serial) : QModelIndex();
}

QModelIndex PDFAppEditContentObjectsModel::parent(const QModelIndex& child) const
{
    if (!child.isValid())
    {
        return QModelIndex();
    }
    const auto it = m_entries.find(getNodeId(child));
    if (it == m_entries.cend())
    {
        return QModelIndex();
    }
    return createParentIndex(it->second.parentId);
}

int PDFAppEditContentObjectsModel::rowCount(const QModelIndex& parent) const
{
    if (parent.column() > 0)
    {
        return 0;
    }
    const auto it = m_entries.find(getNodeId(parent));
    return it != m_entries.cend() ? int(it->second.children.size()) : 0;
}

int PDFAppEditContentObjectsModel::columnCount(const QModelIndex& parent) const
{
    Q_UNUSED(parent);
    return 1;
}

QVariant PDFAppEditContentObjectsModel::data(const QModelIndex& index, int role) const
{
    using Helper = PDFAppEditContentObjectsHelper;

    const PDFContentNode* node = getRowNode(index);
    if (!node)
    {
        return QVariant();
    }

    const quint64 id = node->getId().value;
    const PDFContentNodeReference reference{ m_root.container, node->getId() };
    const PDFContentEditSession* session = m_context ? m_context->getSession() : nullptr;
    const bool isNew = id >= PDFContentEditSession::FIRST_SESSION_NODE_ID;
    switch (role)
    {
        case Qt::DisplayRole:
        case Qt::AccessibleTextRole:
        {
            const TextEntry& texts = getTexts(*m_container, id);
            return texts.description.isEmpty() ? texts.kind : texts.kind + QLatin1Char(' ') + texts.description;
        }

        case Qt::AccessibleDescriptionRole:
        {
            QStringList parts;
            const QString badge = data(index, BadgeRole).toString();
            if (!badge.isEmpty())
            {
                parts << badge;
            }
            if (node->hasClipEffect())
            {
                // The clipping badge of the row (R7-7)
                parts << tr("Clipping");
            }
            if (session && session->isNodeLocked(reference))
            {
                parts << tr("Locked");
            }
            if (session && session->isNodeHidden(reference))
            {
                parts << tr("Hidden");
            }
            return parts.join(PDFComponentPainter::getDetailSeparator());
        }

        case Qt::ToolTipRole:
            return node->getKind() == PDFContentNodeKind::Opaque ? QVariant(node->getData().opaqueReason) : QVariant();

        case PDFComponentRoles::IconNameRole:
            return Helper::getIconName(*node, getDocument());

        case NodeIdRole:
            return id;

        case KindTextRole:
            return getTexts(*m_container, id).kind;

        case DescriptionRole:
            return getTexts(*m_container, id).description;

        case BadgeRole:
        {
            if (isNew)
            {
                return tr("New");
            }
            const auto it = m_entries.find(id);
            if (it != m_entries.cend() && it->second.placeId != id)
            {
                const PDFContentNodePointer place = m_container->getNode(PDFContentNodeId{ it->second.placeId });
                return place ? QString::fromLatin1(place->getData().tag) : QString();
            }
            return QString();
        }

        case IsNewRole:
            return isNew;

        case IsLockedRole:
            return session && session->isNodeLocked(reference);

        case IsHiddenRole:
            return session && session->isNodeHidden(reference);

        case IsClippingRole:
            return node->hasClipEffect();

        case IsSelectableRole:
            return node->getKind() != PDFContentNodeKind::Opaque;

        default:
            break;
    }
    return QVariant();
}

bool PDFAppEditContentObjectsModel::setData(const QModelIndex& index, const QVariant& value, int role)
{
    const PDFContentNode* node = getRowNode(index);
    PDFContentEditSession* session = m_context ? m_context->getSession() : nullptr;
    if (!node || !session || (role != IsLockedRole && role != IsHiddenRole))
    {
        return false;
    }

    // The state of the editor, no command and no undo step (zadani_gui.md 4.18, 4.27): the session tells the change
    // by editorStateChanged, which updates the row
    const PDFContentNodeReference reference{ m_root.container, node->getId() };
    if (role == IsLockedRole)
    {
        session->setNodeLocked(reference, value.toBool());
    }
    else
    {
        session->setNodeHidden(reference, value.toBool());
    }
    return true;
}

Qt::ItemFlags PDFAppEditContentObjectsModel::flags(const QModelIndex& index) const
{
    if (!index.isValid())
    {
        // Between the top rows
        return Qt::ItemIsDropEnabled;
    }

    const PDFContentNode* node = getRowNode(index);
    if (!node)
    {
        return Qt::NoItemFlags;
    }
    if (node->getKind() == PDFContentNodeKind::Opaque)
    {
        // Unknown content: never selected, edited or moved alone (zadani_gui.md 4.27)
        return Qt::ItemIsEnabled;
    }

    // R7-4, the one rule of the lock (PDFAppEditContentContext::isNodeChangeable): a locked row, a row inside of a
    // locked group, is not dragged, and nothing is dropped into it
    const bool isChangeable = m_context && m_context->isNodeChangeable(PDFContentNodeReference{ m_root.container, node->getId() });
    Qt::ItemFlags flags = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    if (isChangeable)
    {
        flags |= Qt::ItemIsDragEnabled;
        if (hasChildRows(*node))
        {
            flags |= Qt::ItemIsDropEnabled;
        }
    }
    return flags;
}

Qt::DropActions PDFAppEditContentObjectsModel::supportedDropActions() const
{
    return Qt::MoveAction;
}

QStringList PDFAppEditContentObjectsModel::mimeTypes() const
{
    return QStringList{ QString::fromLatin1(MIME_TYPE) };
}

QMimeData* PDFAppEditContentObjectsModel::mimeData(const QModelIndexList& indexes) const
{
    using Helper = PDFAppEditContentObjectsHelper;

    if (!m_container)
    {
        return nullptr;
    }

    // The moved nodes in the order of the content (the command keeps their mutual order)
    std::vector<std::pair<std::vector<PDFInteger>, quint64>> nodes;
    std::unordered_set<quint64> added;
    for (const QModelIndex& index : indexes)
    {
        const PDFContentNode* node = getRowNode(index);
        const bool isChangeable = node && m_context && m_context->isNodeChangeable(PDFContentNodeReference{ m_root.container, node->getId() });
        if (isChangeable && added.insert(node->getId().value).second)
        {
            nodes.emplace_back(Helper::getPosition(*m_container, node->getId()), node->getId().value);
        }
    }
    if (nodes.empty())
    {
        return nullptr;
    }
    std::sort(nodes.begin(), nodes.end());

    QByteArray bytes;
    QDataStream stream(&bytes, QIODevice::WriteOnly);
    const PDFContentContainerKey& key = m_container->getKey();
    stream << qint32(key.kind) << qint64(key.pageIndex) << qint64(key.reference.objectNumber) << qint64(key.reference.generation) << quint64(m_container->getVersion())
           << quint32(nodes.size());
    for (const auto& node : nodes)
    {
        stream << quint64(node.second);
    }

    QMimeData* data = new QMimeData();
    data->setData(QString::fromLatin1(MIME_TYPE), bytes);
    return data;
}

std::vector<PDFContentNodeId> PDFAppEditContentObjectsModel::decodeNodes(const QMimeData* data) const
{
    std::vector<PDFContentNodeId> nodes;
    if (!data || !m_container || !data->hasFormat(QString::fromLatin1(MIME_TYPE)))
    {
        return nodes;
    }

    QDataStream stream(data->data(QString::fromLatin1(MIME_TYPE)));
    qint32 kind = 0;
    qint64 pageIndex = 0;
    qint64 objectNumber = 0;
    qint64 generation = 0;
    quint64 version = 0;
    quint32 count = 0;
    stream >> kind >> pageIndex >> objectNumber >> generation >> version >> count;
    const PDFContentContainerKey& key = m_container->getKey();
    if (stream.status() != QDataStream::Ok || kind != qint32(key.kind) || pageIndex != key.pageIndex || objectNumber != key.reference.objectNumber ||
        generation != key.reference.generation || version != m_container->getVersion() || count == 0 || count > PDFContentCommand::MAXIMUM_NODES_OF_COMMAND)
    {
        // Rows of another container or of an older version of it
        return nodes;
    }

    for (quint32 i = 0; i < count; ++i)
    {
        quint64 id = 0;
        stream >> id;
        if (stream.status() != QDataStream::Ok || !m_container->getNode(PDFContentNodeId{ id }))
        {
            return std::vector<PDFContentNodeId>();
        }
        nodes.push_back(PDFContentNodeId{ id });
    }
    return nodes;
}

bool PDFAppEditContentObjectsModel::canDropMimeData(const QMimeData* data, Qt::DropAction action, int row, int column, const QModelIndex& parent) const
{
    using Helper = PDFAppEditContentObjectsHelper;

    Q_UNUSED(row);
    if (action != Qt::MoveAction || column > 0 || !m_context || !m_context->isEditingAllowed() || !m_container)
    {
        return false;
    }

    // The target is the root or a row with rows of children (a group, a marked content)
    const quint64 targetId = getNodeId(parent);
    const PDFContentNodePointer target = m_container->getNode(PDFContentNodeId{ targetId });
    if (!target || !hasChildRows(*target) || (parent.isValid() && m_entries.find(targetId) == m_entries.cend()))
    {
        return false;
    }

    // R7-4: never into a locked group (or a group inside of one); the edited root itself takes the rows
    if (target->getId() != m_root.node && !m_context->isNodeChangeable(PDFContentNodeReference{ m_root.container, target->getId() }))
    {
        return false;
    }

    // Never into a moved node or its subtree; a locked node (or one under a locked group) is never moved, also when it
    // was locked after the drag started
    const std::vector<PDFContentNodeId> nodes = decodeNodes(data);
    if (nodes.empty())
    {
        return false;
    }
    for (const PDFContentNodeId& node : nodes)
    {
        if (Helper::isInside(*m_container, target->getId(), node) || !m_context->isNodeChangeable(PDFContentNodeReference{ m_root.container, node }))
        {
            return false;
        }
    }
    return true;
}

bool PDFAppEditContentObjectsModel::dropMimeData(const QMimeData* data, Qt::DropAction action, int row, int column, const QModelIndex& parent)
{
    if (!canDropMimeData(data, action, row, column, parent))
    {
        return false;
    }

    const std::vector<PDFContentNodeId> nodes = decodeNodes(data);
    const quint64 targetId = getNodeId(parent);
    const PDFContentNodePointer target = m_container->getNode(PDFContentNodeId{ targetId });
    const auto entryIt = m_entries.find(targetId);

    // Between two rows: in front of the node of the row below the drop, its index among the children of the target
    // before the command (a moved node there is skipped by the command); on a row: at its end
    PDFInteger index = -1;
    if (row >= 0 && entryIt != m_entries.cend() && size_t(row) < entryIt->second.children.size())
    {
        const auto rowIt = m_entries.find(entryIt->second.children[size_t(row)]);
        const PDFContentNodeId placeId{ rowIt != m_entries.cend() ? rowIt->second.placeId : 0 };
        index = m_container->getIndexInParent(placeId);
    }

    std::vector<PDFContentNodeReference> references;
    for (const PDFContentNodeId& node : nodes)
    {
        references.push_back(PDFContentNodeReference{ m_root.container, node });
    }

    // The model changes only by the command, from the event loop (RE-21): the view must not move the row itself
    m_context->requestExecute(PDFContentCommandFactory::createReorder(std::move(references), PDFContentNodeReference{ m_root.container, target->getId() },
                                                                      index, PDFContentCommandFactory::ReorderMode::FollowParent));
    return false;
}

PDFContentNodeReference PDFAppEditContentObjectsModel::getNode(const QModelIndex& index) const
{
    if (!index.isValid() || index.model() != this)
    {
        return PDFContentNodeReference();
    }
    const quint64 id = getNodeId(index);
    return id != 0 ? PDFContentNodeReference{ m_root.container, PDFContentNodeId{ id } } : PDFContentNodeReference();
}

QModelIndex PDFAppEditContentObjectsModel::getIndex(const PDFContentNodeReference& node) const
{
    if (!node.isValid() || node.container != m_root.container || node.node == m_root.node || !m_container)
    {
        return QModelIndex();
    }

    quint64 id = node.node.value;
    if (m_entries.find(id) == m_entries.cend())
    {
        // The tagged sequence of one object is the row of its object
        const PDFContentNodePointer place = m_container->getNode(node.node);
        const PDFContentNode* rowNode = place ? getRowNodeOfChild(*place) : nullptr;
        if (!rowNode || rowNode == place.get())
        {
            return QModelIndex();
        }
        id = rowNode->getId().value;
    }
    return createParentIndex(id);
}

void PDFAppEditContentObjectsModel::setFilter(const QString& text, FilterKinds kinds)
{
    if (text == m_filterText && kinds == m_filterKinds)
    {
        return;
    }
    m_filterText = text;
    m_filterKinds = kinds;
    refresh(true);
}

bool PDFAppEditContentObjectsModel::isFiltered() const
{
    return !m_filterText.isEmpty() || m_filterKinds != FilterKinds(0x7F);
}

int PDFAppEditContentObjectsModel::getObjectCount() const
{
    return m_objectCount;
}

int PDFAppEditContentObjectsModel::getNewObjectCount() const
{
    return m_newObjectCount;
}

void PDFAppEditContentObjectsModel::setNodeExpanded(const PDFContentNodeReference& node, bool isExpanded)
{
    if (isExpanded)
    {
        m_expanded.insert(node);
    }
    else
    {
        m_expanded.erase(node);
    }
}

bool PDFAppEditContentObjectsModel::isMatched(const PDFContentContainer& container, const PDFContentNode& node) const
{
    using Helper = PDFAppEditContentObjectsHelper;

    if (!m_filterKinds.testFlag(Helper::getFilterKind(node)))
    {
        return false;
    }
    if (m_filterText.isEmpty())
    {
        return true;
    }

    // The texts of the rows (cached: a filtered tree is built again after every command)
    const TextEntry& texts = getTexts(container, node.getId().value);
    return Helper::contains(texts.kind, m_filterText) || Helper::contains(texts.description, m_filterText);
}

bool PDFAppEditContentObjectsModel::buildRows(Tree& tree, const PDFContentContainer& container, const PDFContentNode& node, quint64 rowId, int depth,
                                              std::vector<quint64>* keptChildren) const
{
    using Helper = PDFAppEditContentObjectsHelper;

    const bool isFilterActive = isFiltered();
    bool isAnyKept = false;
    for (const PDFContentNodePointer& child : node.getChildren())
    {
        const PDFContentNode* rowNode = getRowNodeOfChild(*child);
        if (!rowNode)
        {
            continue;
        }

        const quint64 id = rowNode->getId().value;
        ++tree.objectCount;
        if (id >= PDFContentEditSession::FIRST_SESSION_NODE_ID)
        {
            ++tree.newObjectCount;
        }

        const bool isRowMatched = !isFilterActive || isMatched(container, *rowNode);
        if (isFilterActive && isRowMatched)
        {
            ++tree.matchCount;
        }

        std::vector<quint64> grandChildren;
        bool isChildKept = false;
        if (hasChildRows(*rowNode) && depth < Helper::MAXIMUM_DEPTH)
        {
            isChildKept = buildRows(tree, container, *rowNode, id, depth + 1, &grandChildren);
        }

        if (isRowMatched || isChildKept)
        {
            Entry entry;
            entry.placeId = child->getId().value;
            entry.parentId = rowId;
            entry.children = std::move(grandChildren);
            tree.entries.emplace(id, std::move(entry));
            keptChildren->push_back(id);
            isAnyKept = true;
        }
    }
    return isAnyKept;
}

PDFAppEditContentObjectsModel::Tree PDFAppEditContentObjectsModel::buildTree(const PDFContentContainerPointer& container, PDFContentNodeId rootId) const
{
    Tree tree;
    tree.rootId = rootId.value;
    const PDFContentNodePointer root = container ? container->getNode(rootId) : PDFContentNodePointer();
    if (!root)
    {
        return tree;
    }

    Entry rootEntry;
    rootEntry.placeId = rootId.value;
    buildRows(tree, *container, *root, rootId.value, 0, &rootEntry.children);
    tree.entries.emplace(rootId.value, std::move(rootEntry));
    return tree;
}

void PDFAppEditContentObjectsModel::insertSubtree(const Tree& tree, quint64 id, quint64 parentId, std::vector<quint64>* siblings, size_t position)
{
    const auto sourceIt = tree.entries.find(id);
    if (sourceIt == tree.entries.cend() || m_entries.count(id))
    {
        return;
    }

    Entry entry;
    entry.placeId = sourceIt->second.placeId;
    entry.parentId = parentId;
    entry.serial = m_nextSerial++;
    m_serialToNode[entry.serial] = id;
    Entry& inserted = m_entries.emplace(id, std::move(entry)).first->second;
    if (siblings)
    {
        siblings->insert(siblings->begin() + qMin(position, siblings->size()), id);
        m_dirtyParents.insert(parentId);
    }

    // The unordered map keeps the references of its elements on insertion
    for (quint64 child : sourceIt->second.children)
    {
        insertSubtree(tree, child, id, &inserted.children, inserted.children.size());
    }
}

void PDFAppEditContentObjectsModel::removeSubtree(quint64 id)
{
    const auto it = m_entries.find(id);
    if (it == m_entries.cend())
    {
        return;
    }
    const std::vector<quint64> children = it->second.children;
    for (quint64 child : children)
    {
        removeSubtree(child);
    }
    m_serialToNode.erase(it->second.serial);
    m_dirtyParents.erase(id);
    m_entries.erase(id);
}

void PDFAppEditContentObjectsModel::resetTo(Tree tree, PDFContentContainerPointer container, const PDFContentNodeReference& root)
{
    beginResetModel();
    m_entries.clear();
    m_serialToNode.clear();
    m_dirtyParents.clear();
    m_container = std::move(container);
    m_root = root;
    m_objectCount = tree.objectCount;
    m_newObjectCount = tree.newObjectCount;
    m_matchCount = tree.matchCount;
    if (tree.entries.count(tree.rootId))
    {
        insertSubtree(tree, tree.rootId, 0, nullptr, 0);
    }
    endResetModel();
}

bool PDFAppEditContentObjectsModel::isSameNode(quint64 id, const PDFContentContainer* oldContainer, const PDFContentContainer& newContainer) const
{
    if (!oldContainer)
    {
        return false;
    }
    const PDFContentNodeId nodeId{ id };
    return oldContainer->getNode(nodeId).get() == newContainer.getNode(nodeId).get();
}

void PDFAppEditContentObjectsModel::countChanges(const Tree& tree, quint64 parentId, const PDFContentContainer* oldContainer, const PDFContentContainer& newContainer,
                                                 ChangeCount* count) const
{
    const auto currentIt = m_entries.find(parentId);
    const auto targetIt = tree.entries.find(parentId);
    if (currentIt == m_entries.cend() || targetIt == tree.entries.cend())
    {
        count->structure += LARGE_CHANGE + 1;
        return;
    }

    const std::vector<quint64>& current = currentIt->second.children;
    const std::vector<quint64>& target = targetIt->second.children;
    const std::unordered_set<quint64> currentSet(current.cbegin(), current.cend());
    const std::unordered_set<quint64> targetSet(target.cbegin(), target.cend());

    // Removed and inserted rows, and the kept rows out of their place (at most as many moves)
    std::vector<quint64> currentKept;
    std::vector<quint64> targetKept;
    for (quint64 id : current)
    {
        if (targetSet.count(id))
        {
            currentKept.push_back(id);
        }
        else
        {
            ++count->structure;
        }
    }
    for (quint64 id : target)
    {
        if (currentSet.count(id))
        {
            targetKept.push_back(id);
        }
        else
        {
            ++count->structure;
        }
    }
    for (size_t i = 0; i < currentKept.size() && count->structure <= LARGE_CHANGE; ++i)
    {
        if (currentKept[i] != targetKept[i])
        {
            ++count->structure;
        }
    }

    // The kept rows, whose node changed: a dataChanged each (R7-9: counted, a Transform of many objects changes no
    // row's place); the rows inside of them
    for (quint64 id : targetKept)
    {
        if (count->structure > LARGE_CHANGE)
        {
            break;
        }
        const auto childIt = tree.entries.find(id);
        const auto oldIt = m_entries.find(id);
        if (childIt == tree.entries.cend() || oldIt == m_entries.cend())
        {
            continue;
        }
        const bool isNodeSame = isSameNode(id, oldContainer, newContainer);
        const bool isSame = isNodeSame && oldIt->second.placeId == childIt->second.placeId && isSameNode(childIt->second.placeId, oldContainer, newContainer);
        if (!isSame)
        {
            ++count->data;
        }
        if (!isNodeSame && (!childIt->second.children.empty() || !oldIt->second.children.empty()))
        {
            countChanges(tree, id, oldContainer, newContainer, count);
        }
    }
}

void PDFAppEditContentObjectsModel::removeMissing(const Tree& tree, quint64 parentId, const PDFContentContainer* oldContainer, const PDFContentContainer& newContainer)
{
    const auto targetIt = tree.entries.find(parentId);
    const auto currentIt = m_entries.find(parentId);
    if (targetIt == tree.entries.cend() || currentIt == m_entries.cend())
    {
        return;
    }

    Entry& current = currentIt->second;
    const std::vector<quint64>& target = targetIt->second.children;
    const std::unordered_set<quint64> targetSet(target.cbegin(), target.cend());
    const QModelIndex parentIndex = createParentIndex(parentId);

    // Runs of removed rows from the end (the rows before them keep their numbers)
    int i = int(current.children.size()) - 1;
    while (i >= 0)
    {
        if (targetSet.count(current.children[size_t(i)]))
        {
            --i;
            continue;
        }
        const int last = i;
        while (i > 0 && !targetSet.count(current.children[size_t(i - 1)]))
        {
            --i;
        }
        const int first = i;
        beginRemoveRows(parentIndex, first, last);
        for (int k = first; k <= last; ++k)
        {
            removeSubtree(current.children[size_t(k)]);
        }
        current.children.erase(current.children.begin() + first, current.children.begin() + last + 1);
        m_dirtyParents.insert(parentId);
        endRemoveRows();
        --i;
    }

    // The rows kept here, whose nodes changed, may have lost rows inside
    const std::vector<quint64> kept = current.children;
    for (quint64 id : kept)
    {
        const auto childIt = tree.entries.find(id);
        const auto oldIt = m_entries.find(id);
        if (childIt != tree.entries.cend() && oldIt != m_entries.cend() && !oldIt->second.children.empty() && !isSameNode(id, oldContainer, newContainer))
        {
            removeMissing(tree, id, oldContainer, newContainer);
        }
    }
}

void PDFAppEditContentObjectsModel::insertAndMove(const Tree& tree, quint64 parentId, const PDFContentContainer* oldContainer, const PDFContentContainer& newContainer)
{
    const auto targetIt = tree.entries.find(parentId);
    const auto currentIt = m_entries.find(parentId);
    if (targetIt == tree.entries.cend() || currentIt == m_entries.cend())
    {
        return;
    }

    Entry& current = currentIt->second;
    const std::vector<quint64>& target = targetIt->second.children;
    const QModelIndex parentIndex = createParentIndex(parentId);
    std::unordered_set<quint64> inserted;
    for (size_t i = 0; i < target.size(); ++i)
    {
        const quint64 id = target[i];
        if (i < current.children.size() && current.children[i] == id)
        {
            continue;
        }

        const auto found = std::find(current.children.begin() + qMin(i, current.children.size()), current.children.end(), id);
        if (found != current.children.end())
        {
            // A kept row out of its place: up to the place i (the rows before i are final)
            const int from = int(found - current.children.begin());
            beginMoveRows(parentIndex, from, from, parentIndex, int(i));
            current.children.erase(found);
            current.children.insert(current.children.begin() + i, id);
            m_dirtyParents.insert(parentId);
            endMoveRows();
        }
        else
        {
            beginInsertRows(parentIndex, int(i), int(i));
            insertSubtree(tree, id, parentId, &current.children, i);
            inserted.insert(id);
            endInsertRows();
        }
    }

    // The rows kept here: their nodes and places may have changed (dataChanged), their rows inside too. Many changed
    // rows (m_isBulkDataChange) are one dataChanged of the range from the first to the last changed row of the parent.
    const std::vector<quint64> children = current.children;
    int firstChanged = -1;
    int lastChanged = -1;
    for (size_t i = 0; i < children.size(); ++i)
    {
        const quint64 id = children[i];
        if (inserted.count(id))
        {
            continue;
        }
        const auto childIt = tree.entries.find(id);
        const auto entryIt = m_entries.find(id);
        if (childIt == tree.entries.cend() || entryIt == m_entries.cend())
        {
            continue;
        }

        const bool isPlaceSame = entryIt->second.placeId == childIt->second.placeId && isSameNode(childIt->second.placeId, oldContainer, newContainer);
        const bool isNodeSame = isSameNode(id, oldContainer, newContainer);
        entryIt->second.placeId = childIt->second.placeId;
        if (!isNodeSame || !isPlaceSame)
        {
            if (m_isBulkDataChange)
            {
                firstChanged = firstChanged < 0 ? int(i) : firstChanged;
                lastChanged = int(i);
            }
            else
            {
                const QModelIndex index = createParentIndex(id);
                Q_EMIT dataChanged(index, index);
            }
        }
        if (!isNodeSame)
        {
            insertAndMove(tree, id, oldContainer, newContainer);
        }
    }
    if (firstChanged >= 0)
    {
        Q_EMIT dataChanged(index(firstChanged, 0, parentIndex), index(lastChanged, 0, parentIndex));
    }
}

void PDFAppEditContentObjectsModel::refresh(bool isReset)
{
    const QPointer<PDFAppEditContentObjectsModel> self(this);
    const int objectCount = m_objectCount;
    const int newObjectCount = m_newObjectCount;
    const int matchCount = m_matchCount;

    PDFContentContainerPointer container = m_context ? m_context->getContainer() : PDFContentContainerPointer();
    const PDFContentNodeReference root = m_context ? m_context->getEditedRoot() : PDFContentNodeReference();
    if (!container || !root.isValid() || !container->getNode(root.node))
    {
        if (m_container || !m_entries.empty() || m_root != root)
        {
            resetTo(Tree(), nullptr, root);
        }
    }
    else if (isReset || !m_container || root != m_root)
    {
        resetTo(buildTree(container, root.node), container, root);
    }
    else if (m_container != container)
    {
        Tree tree = buildTree(container, root.node);
        ChangeCount count;
        countChanges(tree, root.node.value, m_container.get(), *container, &count);
        if (count.structure > LARGE_CHANGE)
        {
            resetTo(std::move(tree), container, root);
        }
        else
        {
            // The rows of the old version stay readable until the changes are told: the old container is held here
            const PDFContentContainerPointer oldContainer = m_container;
            m_container = container;
            m_objectCount = tree.objectCount;
            m_newObjectCount = tree.newObjectCount;
            m_matchCount = tree.matchCount;
            m_isBulkDataChange = count.data > LARGE_CHANGE;
            removeMissing(tree, root.node.value, oldContainer.get(), *container);
            insertAndMove(tree, root.node.value, oldContainer.get(), *container);
            if (self)
            {
                m_isBulkDataChange = false;
            }
        }
    }
    if (self)
    {
        pruneTextCache();
    }

    // Emit last (a slot may close the tool)
    if (self && (objectCount != m_objectCount || newObjectCount != m_newObjectCount || matchCount != m_matchCount))
    {
        Q_EMIT countsChanged();
    }
    if (self)
    {
        Q_EMIT refreshed();
    }
}

void PDFAppEditContentObjectsModel::queueRefresh()
{
    if (!m_isRefreshQueued)
    {
        m_isRefreshQueued = true;
        QMetaObject::invokeMethod(this, &PDFAppEditContentObjectsModel::onQueuedRefresh, Qt::QueuedConnection);
    }
}

void PDFAppEditContentObjectsModel::onQueuedRefresh()
{
    const bool isReset = m_isResetQueued;
    m_isRefreshQueued = false;
    m_isResetQueued = false;
    if (!PDFDeferredDeletion::isRetired(this))
    {
        refresh(isReset);
    }
}

void PDFAppEditContentObjectsModel::onPageChanged()
{
    // Another page: its rows from the beginning (the expanded rows of the page stay in m_expanded)
    m_isResetQueued = true;
    queueRefresh();
}

void PDFAppEditContentObjectsModel::onContainerChanged()
{
    m_isResetQueued = true;
    queueRefresh();
}

void PDFAppEditContentObjectsModel::onContainerLoaded(const PDFContentContainerKey& key)
{
    if (m_context && key == m_context->getContainerKey())
    {
        queueRefresh();
    }
}

void PDFAppEditContentObjectsModel::onModelChanged(const PDFContentChange& change)
{
    // Rows from the event loop: the change may come inside of a signal of a view (a command of a drop, of a menu)
    if (std::find(change.containers.cbegin(), change.containers.cend(), m_root.container) != change.containers.cend() || !m_container)
    {
        queueRefresh();
    }
}

void PDFAppEditContentObjectsModel::onEditorStateChanged(const PDFContentNodeReference& node)
{
    // R6-2: the session tells every node of a lock of many objects (it has no batch call); one lookup of the row and
    // one dataChanged each, up to LARGE_CHANGE rows in a turn of the event loop - then one dataChanged of all rows from
    // the event loop (no rebuild, no search)
    if (!m_isStateRefreshQueued)
    {
        m_isStateRefreshQueued = true;
        QMetaObject::invokeMethod(this, &PDFAppEditContentObjectsModel::onQueuedStateRefresh, Qt::QueuedConnection);
    }
    if (m_stateChangeCount > LARGE_CHANGE)
    {
        return;
    }

    ++m_stateChangeCount;
    const QModelIndex index = m_stateChangeCount <= LARGE_CHANGE ? getIndex(node) : QModelIndex();
    if (index.isValid())
    {
        Q_EMIT dataChanged(index, index);
    }
}

void PDFAppEditContentObjectsModel::onQueuedStateRefresh()
{
    const bool isMany = m_stateChangeCount > LARGE_CHANGE;
    m_isStateRefreshQueued = false;
    m_stateChangeCount = 0;
    const int count = rowCount();
    if (isMany && count > 0 && !PDFDeferredDeletion::isRetired(this))
    {
        // The rows inside of the groups are repainted with them (a range of more rows repaints the view)
        Q_EMIT dataChanged(index(0, 0), index(count - 1, 0), { IsLockedRole, IsHiddenRole, Qt::AccessibleDescriptionRole });
    }
}

// ---------------------------------------------------------------- PDFAppEditContentObjectsPanel

PDFAppEditContentObjectsPanel::PDFAppEditContentObjectsPanel(PDFAppEditContentContext* context, PDFAppEditContentObjectsModel* model, QWidget* parent) :
    BaseClass(parent),
    PDFRetirable(this),
    m_context(context),
    m_model(model)
{
    setObjectName(QStringLiteral("editContentObjects"));

    // The header (Objects, "Page 3 · 20 objects", the filter button), the filter field, the tree and the hint
    // (edit-content-v2-objects-nested, -complex-page); PDFTreeView keeps the margins 6 around its rows
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(PDFAppPanelWidgets::CONTENT_PADDING_SIDE - 6, 8, PDFAppPanelWidgets::CONTENT_PADDING_SIDE - 6, PDFAppPanelWidgets::CONTENT_PADDING_BOTTOM - 6);
    layout->setSpacing(8);

    QHBoxLayout* headerLayout = new QHBoxLayout();
    headerLayout->setContentsMargins(6, 0, 6, 0);
    headerLayout->setSpacing(8);
    QVBoxLayout* titleLayout = new QVBoxLayout();
    titleLayout->setContentsMargins(0, 0, 0, 0);
    titleLayout->setSpacing(0);
    m_title = new PDFAppHeaderText(PDFThemeFonts::getFont(QFont::DemiBold, 14), PDFThemeColors::TextPrimary, this);
    m_title->setObjectName(QStringLiteral("editContentObjectsTitle"));
    m_subtitle = new PDFAppHeaderText(PDFThemeFonts::getFont(QFont::Normal, 11), PDFThemeColors::TextMuted, this);
    m_subtitle->setObjectName(QStringLiteral("editContentObjectsSubtitle"));
    titleLayout->addWidget(m_title);
    titleLayout->addWidget(m_subtitle);
    headerLayout->addLayout(titleLayout, 1);
    m_filterButton = PDFAppPanelWidgets::createHeaderButton(this, QStringLiteral("filter"), tr("Filter objects"), QStringLiteral("editContentObjectsFilterButton"));
    m_filterButton->setCheckable(true);
    headerLayout->addWidget(m_filterButton, 0, Qt::AlignTop);
    layout->addLayout(headerLayout);

    QHBoxLayout* filterLayout = new QHBoxLayout();
    filterLayout->setContentsMargins(6, 0, 6, 0);
    m_filterField = PDFAppPanelWidgets::createSearchField(this, tr("Filter by type or name…"), QStringLiteral("editContentObjectsFilter"));
    filterLayout->addWidget(m_filterField);
    layout->addLayout(filterLayout);

    m_tree = new PDFTreeView(this);
    m_tree->setObjectName(QStringLiteral("editContentObjectsTree"));
    m_tree->setHeaderHidden(true);
    m_tree->setRootIsDecorated(true);

    // A tree of 100 000 rows: one height asked once, no widget per row (zadani_gui.md 4.24)
    m_tree->setUniformRowHeights(true);
    m_tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_tree->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tree->setEditTriggers(QAbstractItemView::NoEditTriggers);

    // A double click enters a group (zadani_gui.md 4.16), the arrow of the row expands it
    m_tree->setExpandsOnDoubleClick(false);
    m_tree->setDragEnabled(true);
    m_tree->setAcceptDrops(true);
    m_tree->setDropIndicatorShown(true);
    m_tree->setDragDropMode(QAbstractItemView::InternalMove);
    m_tree->setDefaultDropAction(Qt::MoveAction);

    // The menu of the tool, not the menu of the toggles of the delegate (they are Hide and Lock of the menu)
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    PDFAppPanelWidgets::prepareView(m_tree);
    m_tree->setItemDelegate(new PDFAppEditContentObjectsDelegate(m_tree));
    m_tree->setModel(model);
    layout->addWidget(m_tree, 1);

    m_hint = new PDFAppParagraph(PDFThemeFonts::getFont(QFont::Normal, 11), 13, PDFThemeColors::TextTertiary, this);
    m_hint->setObjectName(QStringLiteral("editContentObjectsHint"));
    QHBoxLayout* hintLayout = new QHBoxLayout();
    hintLayout->setContentsMargins(6, 0, 6, 0);
    hintLayout->addWidget(m_hint);
    layout->addLayout(hintLayout);

    // The filter is given to the model, when the typing pauses (no rebuild of 100 000 rows at every key)
    m_filterTimer = new QTimer(this);
    m_filterTimer->setSingleShot(true);
    m_filterTimer->setInterval(FILTER_DELAY);
    connect(m_filterTimer.data(), &QTimer::timeout, this, &PDFAppEditContentObjectsPanel::onFilterTimeout);
    connect(m_filterField.data(), &QLineEdit::textEdited, this, &PDFAppEditContentObjectsPanel::onFilterTextEdited);
    connect(m_filterButton.data(), &QToolButton::clicked, this, &PDFAppEditContentObjectsPanel::onFilterButtonClicked);

    if (model)
    {
        // The signals of the view from the event loop: a slot changes the selection of the tool, whose slots may
        // close the tool
        PDFAppItemViewSignalRelay* relay = new PDFAppItemViewSignalRelay(m_tree, this);
        connect(relay, &PDFAppItemViewSignalRelay::selectionChanged, this, &PDFAppEditContentObjectsPanel::onTreeSelectionChanged);
        connect(relay, &PDFAppItemViewSignalRelay::activated, this, &PDFAppEditContentObjectsPanel::onTreeActivated);
        connect(m_tree.data(), &QWidget::customContextMenuRequested, this, &PDFAppEditContentObjectsPanel::onContextMenuRequested);
        connect(m_tree.data(), &QTreeView::expanded, this, &PDFAppEditContentObjectsPanel::onExpanded);
        connect(m_tree.data(), &QTreeView::collapsed, this, &PDFAppEditContentObjectsPanel::onCollapsed);

        PDFDeferredDeletion::connect(model, &PDFAppEditContentObjectsModel::countsChanged, this, &PDFAppEditContentObjectsPanel::onCountsChanged);
        PDFDeferredDeletion::connect(model, &PDFAppEditContentObjectsModel::refreshed, this, &PDFAppEditContentObjectsPanel::onRefreshed);
        PDFDeferredDeletion::connect(model, &QAbstractItemModel::modelReset, this, &PDFAppEditContentObjectsPanel::onModelReset);
        PDFDeferredDeletion::connect(model, &QAbstractItemModel::rowsInserted, this, &PDFAppEditContentObjectsPanel::onRowsInserted);
        m_filterField->setText(model->getFilterText());

        // A panel is built again after a change of the language: the texts of the rows follow it
        model->clearTextCache();
    }
    if (context)
    {
        PDFDeferredDeletion::connect(context, &PDFAppEditContentContext::pageChanged, this, &PDFAppEditContentObjectsPanel::onCountsChanged);
        PDFDeferredDeletion::connect(context, &PDFAppEditContentContext::previewActiveChanged, this, &PDFAppEditContentObjectsPanel::updateEnabled);
        if (PDFAppEditContentSelection* selection = context->getSelection())
        {
            PDFDeferredDeletion::connect(selection, &PDFAppEditContentSelection::changed, this, &PDFAppEditContentObjectsPanel::onSelectionChanged);
        }
    }
    m_tree->installEventFilter(this);

    restoreExpanded(QModelIndex());
    syncTreeFromSelection();
    updateTexts();
    updateEnabled();
}

PDFAppEditContentObjectsPanel::~PDFAppEditContentObjectsPanel()
{
    // The model outlives the panel: nothing of it reaches the panel after this
    if (m_model)
    {
        m_model->disconnect(this);
    }
    if (m_tree)
    {
        m_tree->removeEventFilter(this);
    }
}

PDFTreeView* PDFAppEditContentObjectsPanel::getTree() const
{
    return m_tree.data();
}

PDFLineEdit* PDFAppEditContentObjectsPanel::getFilterField() const
{
    return m_filterField.data();
}

QToolButton* PDFAppEditContentObjectsPanel::getFilterButton() const
{
    return m_filterButton.data();
}

PDFAppHeaderText* PDFAppEditContentObjectsPanel::getSubtitle() const
{
    return m_subtitle.data();
}

PDFAppParagraph* PDFAppEditContentObjectsPanel::getHint() const
{
    return m_hint.data();
}

void PDFAppEditContentObjectsPanel::registerParts(PDFAppEditContentRegistry* registry)
{
    registry->addTab(std::make_unique<PDFAppEditContentObjectsTabEntry>());
}

void PDFAppEditContentObjectsPanel::retire()
{
    // A pending filter is given to the model (the text typed is not lost with the panel)
    if (m_filterTimer && m_filterTimer->isActive())
    {
        m_filterTimer->stop();
        if (m_model && m_filterField)
        {
            m_model->setFilter(m_filterField->text(), m_model->getFilterKinds());
        }
    }
}

bool PDFAppEditContentObjectsPanel::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_tree && (event->type() == QEvent::ShortcutOverride || event->type() == QEvent::KeyPress))
    {
        QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);
        const int key = keyEvent->key();
        const bool isPlain = (keyEvent->modifiers() & ~Qt::KeypadModifier) == Qt::NoModifier;
        const bool isOwnKey = isPlain && (key == Qt::Key_Delete || key == Qt::Key_F2 || key == Qt::Key_Return || key == Qt::Key_Enter);
        if (!isOwnKey)
        {
            return BaseClass::eventFilter(watched, event);
        }
        if (event->type() == QEvent::ShortcutOverride)
        {
            // The keys of the tree are its own (no shortcut of the application takes them here)
            event->accept();
            return true;
        }

        if (key == Qt::Key_Delete && m_context)
        {
            // The one owner of what is deleted (pe1_skeleton_notes.md 11, R3-7); it runs from the event loop
            m_context->triggerAction(QString::fromLatin1(PDFAppEditContentActionIds::DELETE_SELECTION));
        }
        else if ((key == Qt::Key_Return || key == Qt::Key_Enter) && m_tree)
        {
            // From the event loop: the slots of the isolation and of the tools may rebuild the panel
            QMetaObject::invokeMethod(this, &PDFAppEditContentObjectsPanel::onEnterKey, Qt::QueuedConnection);
        }

        // F2: nothing (zadani_gui.md 4.18)
        return true;
    }
    return BaseClass::eventFilter(watched, event);
}

void PDFAppEditContentObjectsPanel::changeEvent(QEvent* event)
{
    BaseClass::changeEvent(event);
    if (event->type() == QEvent::LanguageChange)
    {
        // The kinds and the descriptions of the rows are translated texts
        if (m_model)
        {
            m_model->clearTextCache();
        }
        if (m_tree)
        {
            m_tree->viewport()->update();
        }
        updateTexts();
    }
}

void PDFAppEditContentObjectsPanel::updateTexts()
{
    if (m_title)
    {
        m_title->setHeaderText(tr("Objects"));
    }

    QStringList parts;
    const PDFContentEditSession* session = m_context ? m_context->getSession() : nullptr;
    const PDFDocument* document = session ? session->getDocument() : nullptr;
    if (document && m_context->getPageIndex() >= 0)
    {
        parts << PDFAppPanelWidgets::getPageTitle(document->getCatalog(), m_context->getPageIndex());
    }
    if (m_model && m_model->getContainer())
    {
        const int count = m_model->getObjectCount();
        if (m_model->isFiltered())
        {
            parts << tr("%1 of %Ln objects match", nullptr, count).arg(PDFAppLocale::formatNumber(m_model->getMatchCount()));
        }
        else
        {
            parts << tr("%Ln objects", nullptr, count);
            if (m_model->getNewObjectCount() > 0)
            {
                parts << tr("%Ln new", nullptr, m_model->getNewObjectCount());
            }
        }
    }
    if (m_subtitle)
    {
        m_subtitle->setHeaderText(parts.join(PDFComponentPainter::getDetailSeparator()));
    }

    const bool isFilterShown = m_model && (m_model->isFilterShown() || m_model->isFiltered());
    if (m_filterField)
    {
        m_filterField->setVisible(isFilterShown);
    }
    if (m_filterButton)
    {
        m_filterButton->setChecked(isFilterShown);
    }
    if (m_hint)
    {
        m_hint->setText(m_model && m_model->isFiltered()
                        ? tr("The filter looks at the type and the name of an object. A group stays listed when something inside it matches. Clear the field to see every object again.")
                        : tr("Rows follow the content stream: the last row is painted on top. Drag a row to reorder it or to move it into a group; double-click enters a group."));
    }
}

void PDFAppEditContentObjectsPanel::updateEnabled()
{
    // Preview: the tree in the colors of the disabled items (zadani_gui.md 4.15, -preview)
    if (m_tree && m_context)
    {
        m_tree->setEnabled(!m_context->isPreviewActive());
    }
}

void PDFAppEditContentObjectsPanel::syncTreeFromSelection()
{
    const PDFAppEditContentSelection* selection = m_context ? m_context->getSelection() : nullptr;
    QItemSelectionModel* selectionModel = m_tree ? m_tree->selectionModel() : nullptr;
    if (!selection || !m_model || !selectionModel)
    {
        return;
    }

    // Ranges of neighbor rows (Select All of 100 000 objects is one range of the top rows)
    std::map<std::pair<quintptr, int>, QModelIndex> rows;
    std::map<quintptr, QModelIndex> parents;
    for (const PDFContentNodeReference& node : selection->getNodes())
    {
        const QModelIndex index = m_model->getIndex(node);
        if (index.isValid())
        {
            const QModelIndex parent = index.parent();
            const quintptr parentKey = parent.isValid() ? parent.internalId() : 0;
            rows.emplace(std::make_pair(parentKey, index.row()), index);
        }
    }
    QItemSelection itemSelection;
    for (auto it = rows.cbegin(); it != rows.cend();)
    {
        auto last = it;
        auto next = std::next(it);
        while (next != rows.cend() && next->first.first == it->first.first && next->first.second == last->first.second + 1)
        {
            last = next;
            ++next;
        }
        itemSelection.select(it->second, last->second);
        it = next;
    }

    m_isSyncing = true;
    selectionModel->select(itemSelection, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    const QModelIndex keyIndex = m_model->getIndex(selection->getKeyNode());
    if (keyIndex.isValid())
    {
        // The key object is shown: scrollTo expands its groups (QTreeView), the expansion is kept by onExpanded
        selectionModel->setCurrentIndex(keyIndex, QItemSelectionModel::NoUpdate);
        m_tree->scrollTo(keyIndex);
    }
    m_isSyncing = false;
}

void PDFAppEditContentObjectsPanel::restoreExpanded(const QModelIndex& parent)
{
    if (!m_model || !m_tree)
    {
        return;
    }

    // Only the expanded rows are entered (a collapsed group of 100 000 rows costs nothing)
    const int count = m_model->rowCount(parent);
    for (int row = 0; row < count; ++row)
    {
        const QModelIndex index = m_model->index(row, 0, parent);
        if (m_model->isNodeExpanded(m_model->getNode(index)))
        {
            m_tree->expand(index);
            restoreExpanded(index);
        }
    }
}

void PDFAppEditContentObjectsPanel::activate(const QModelIndex& index)
{
    const PDFContentNodeReference node = m_model ? m_model->getNode(index) : PDFContentNodeReference();
    const PDFContentContainerPointer container = m_model ? m_model->getContainer() : PDFContentContainerPointer();
    const PDFContentNodePointer contentNode = container ? container->getNode(node.node) : PDFContentNodePointer();
    if (!m_context || !contentNode)
    {
        return;
    }

    switch (contentNode->getKind())
    {
        case PDFContentNodeKind::Group:
        case PDFContentNodeKind::MarkedContent:
        case PDFContentNodeKind::FormUse:
            // Enter the group (stage E4, P43 answers the request)
            m_context->requestIsolation(node);
            break;

        default:
        {
            // The editor of the object (typing, nodes, crop; zadani_gui.md 4.16), when one is registered
            const PDFAppEditContentRegistry* registry = m_context->getRegistry();
            PDFAppEditContentToolController* controller = m_context->getToolController();
            if (registry && controller && m_context->isEditingAllowed() && registry->findEditorTool(*contentNode, *container))
            {
                controller->activateEditorTool(node);
            }
            break;
        }
    }
}

void PDFAppEditContentObjectsPanel::onCountsChanged()
{
    updateTexts();
}

void PDFAppEditContentObjectsPanel::onRefreshed()
{
    updateTexts();
    syncTreeFromSelection();
}

void PDFAppEditContentObjectsPanel::onSelectionChanged()
{
    if (!m_isSyncing)
    {
        syncTreeFromSelection();
    }
}

void PDFAppEditContentObjectsPanel::onTreeSelectionChanged()
{
    PDFAppEditContentSelection* selection = m_context ? m_context->getSelection() : nullptr;
    QItemSelectionModel* selectionModel = m_tree ? m_tree->selectionModel() : nullptr;
    if (!selection || !m_model || !selectionModel || m_isSyncing)
    {
        return;
    }

    // R7-3: O(n log n) in the rows and the selected nodes - the rows of the ranges of the selection of the view (not
    // QItemSelectionModel::selectedRows, which asks every range for every row) and sets of the nodes; the work is counted
    // (the rows and the nodes read, the comparisons of the sets)
    using Helper = PDFAppEditContentObjectsHelper;
    m_treeSelectionWork = 0;
    const Helper::CountingLess less{ &m_treeSelectionWork };

    // The rows selected in the tree, in the order of the ranges (the order, in which the view selected them)
    Helper::CountedNodeSet treeNodes(less);
    std::vector<PDFContentNodeReference> treeOrder;
    for (const QItemSelectionRange& range : selectionModel->selection())
    {
        for (int row = range.top(); row <= range.bottom(); ++row)
        {
            ++m_treeSelectionWork;
            const QModelIndex index = m_model->index(row, 0, range.parent());
            const PDFContentNodeReference node = m_model->getNode(index);
            if (node.isValid() && (m_model->flags(index) & Qt::ItemIsSelectable) && treeNodes.insert(node).second)
            {
                treeOrder.push_back(node);
            }
        }
    }

    // The selection of the tool keeps the nodes, which the tree does not show (R7-12: rows of the filter; nodes without a
    // row), so only a change of the shown rows is a change of the user
    Helper::CountedNodeSet selected(less);
    Helper::CountedNodeSet shownSelected(less);
    const PDFContentContainerPointer container = m_model->getContainer();
    for (const PDFContentNodeReference& node : selection->getNodes())
    {
        ++m_treeSelectionWork;
        selected.insert(node);
        if (m_model->getIndex(node).isValid())
        {
            shownSelected.insert(node);
        }
    }
    const PDFContentNodeReference current = m_model->getNode(m_tree->currentIndex());
    const bool isKeyChange = current.isValid() && treeNodes.count(current) && current != selection->getKeyNode();
    if (treeNodes.size() == shownSelected.size() && std::equal(treeNodes.cbegin(), treeNodes.cend(), shownSelected.cbegin()) && !isKeyChange)
    {
        return;
    }

    // The order of the selection: the nodes selected before keep their order (the selected rows and the nodes without a
    // shown row of the container of the rows), the new ones follow in the order of the ranges of the view; the current
    // row is the key object
    std::vector<PDFContentNodeReference> nodes;
    nodes.reserve(treeNodes.size() + selected.size());
    for (const PDFContentNodeReference& node : selection->getNodes())
    {
        ++m_treeSelectionWork;
        const bool isHidden = container && node.container == container->getKey() && !shownSelected.count(node);
        if (treeNodes.count(node) || isHidden)
        {
            nodes.push_back(node);
        }
    }
    for (const PDFContentNodeReference& node : treeOrder)
    {
        ++m_treeSelectionWork;
        if (!selected.count(node))
        {
            nodes.push_back(node);
        }
    }

    const QPointer<PDFAppEditContentObjectsPanel> self(this);
    const QPointer<PDFAppEditContentSelection> guard(selection);
    const bool isKeyRow = current.isValid() && treeNodes.count(current);
    m_isSyncing = true;
    selection->setNodes(nodes);
    if (self && guard && isKeyRow)
    {
        guard->setKeyNode(current);
    }
    if (self)
    {
        m_isSyncing = false;
    }
}

void PDFAppEditContentObjectsPanel::onTreeActivated(const QModelIndex& index)
{
    activate(index);
}

void PDFAppEditContentObjectsPanel::onEnterKey()
{
    if (m_tree)
    {
        activate(m_tree->currentIndex());
    }
}

void PDFAppEditContentObjectsPanel::onContextMenuRequested(const QPoint& position)
{
    if (!m_tree || !m_model || !m_context || !m_context->getSelection())
    {
        return;
    }

    // A row outside of the selection is selected first (the menu acts on the selection)
    const QPoint globalPosition = m_tree->viewport()->mapToGlobal(position);
    const QModelIndex index = m_tree->indexAt(position);
    const PDFContentNodeReference node = m_model->getNode(index);
    const QPointer<PDFAppEditContentObjectsPanel> self(this);
    if (node.isValid() && (m_model->flags(index) & Qt::ItemIsSelectable) && !m_context->getSelection()->contains(node))
    {
        m_context->getSelection()->setNode(node);
    }
    if (self && m_context && !m_context->getSelection()->isEmpty())
    {
        m_context->requestContextMenu(QString::fromLatin1(PDFAppEditContentMenuIds::CONTEXT), globalPosition);
    }
}

void PDFAppEditContentObjectsPanel::onFilterTextEdited(const QString& text)
{
    Q_UNUSED(text);
    if (m_filterTimer)
    {
        m_filterTimer->start();
    }
}

void PDFAppEditContentObjectsPanel::onFilterTimeout()
{
    if (m_model && m_filterField)
    {
        m_model->setFilter(m_filterField->text(), m_model->getFilterKinds());
        updateTexts();
    }
}

void PDFAppEditContentObjectsPanel::onFilterButtonClicked()
{
    if (!m_model)
    {
        return;
    }

    const bool isShown = !(m_model->isFilterShown() || m_model->isFiltered());
    m_model->setFilterShown(isShown);
    if (!isShown)
    {
        // Closing the field ends the filter
        if (m_filterTimer)
        {
            m_filterTimer->stop();
        }
        if (m_filterField)
        {
            m_filterField->clear();
        }
        m_model->setFilter(QString(), m_model->getFilterKinds());
    }
    updateTexts();
    if (isShown && m_filterField)
    {
        m_filterField->setFocus(Qt::OtherFocusReason);
    }
}

void PDFAppEditContentObjectsPanel::onExpanded(const QModelIndex& index)
{
    if (m_model)
    {
        m_model->setNodeExpanded(m_model->getNode(index), true);
    }
}

void PDFAppEditContentObjectsPanel::onCollapsed(const QModelIndex& index)
{
    if (m_model)
    {
        m_model->setNodeExpanded(m_model->getNode(index), false);
    }
}

void PDFAppEditContentObjectsPanel::onModelReset()
{
    restoreExpanded(QModelIndex());
}

void PDFAppEditContentObjectsPanel::onRowsInserted(const QModelIndex& parent, int first, int last)
{
    if (!m_model || !m_tree)
    {
        return;
    }
    for (int row = first; row <= last; ++row)
    {
        const QModelIndex index = m_model->index(row, 0, parent);
        if (m_model->isNodeExpanded(m_model->getNode(index)))
        {
            m_tree->expand(index);
            restoreExpanded(index);
        }
    }
}

}   // namespace pdf

#include "moc_pdfappeditcontentpanels_objects.cpp"
