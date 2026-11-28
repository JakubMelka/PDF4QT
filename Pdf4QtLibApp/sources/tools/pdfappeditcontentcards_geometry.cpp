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

#include "pdfappeditcontentcards_geometry.h"

#include "pdfappdocumenttoolbar.h"
#include "pdfappeditcontentcards.h"
#include "pdfappeditcontentcontext.h"
#include "pdfappeditcontentregistry.h"
#include "pdfappeditcontentselection.h"
#include "pdfcomponentsections.h"
#include "pdfcontentarrange.h"
#include "pdfcontentcommands.h"
#include "pdfcontentdisplay.h"
#include "pdfcontenteditsession.h"
#include "pdffluentwidgets.h"

#include <QCheckBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QToolButton>
#include <QVBoxLayout>
#include <QtMath>

#include <cmath>
#include <memory>

namespace pdf
{

// Edit Content, stage E1, package P13: the cards Position and size and Arrange (zadani_gui.md 4.19; the frames
// -transform, -multi-selected, -nothing-selected). Every box comes from PDFAppEditContentGeometryHelper (the one policy
// of the box, shared with the canvas); every change is a Transform or an Arrange command of the core in the default
// space of the container (a matrix T of the page space becomes C x T x C^-1, C = the container to the page).

/// The parts of the matrix of an object (row vectors as in PDF: M = Scale(sx, sy) x Shear(k) x Rotate(angle) and a
/// translation): the rows Rotation, Scale in X, Scale in Y, Shear of the card
class PDFAppEditContentMatrixParts
{
public:
    double scaleX = 1.0;
    double scaleY = 1.0;
    double shear = 0.0;
    double angle = 0.0;     ///< Degrees, counterclockwise in the space with the y axis up

    /// The parts of a matrix (scaleX > 0; a mirrored matrix has scaleY < 0); no value for a singular matrix
    static std::optional<PDFAppEditContentMatrixParts> decompose(const QTransform& matrix)
    {
        const double a = matrix.m11();
        const double b = matrix.m12();
        const double c = matrix.m21();
        const double d = matrix.m22();
        PDFAppEditContentMatrixParts parts;
        parts.scaleX = std::hypot(a, b);
        if (!(parts.scaleX > 1e-12) || !std::isfinite(parts.scaleX))
        {
            return std::nullopt;
        }

        const double cosine = a / parts.scaleX;
        const double sine = b / parts.scaleX;
        parts.angle = qRadiansToDegrees(std::atan2(sine, cosine));
        parts.scaleY = -c * sine + d * cosine;
        if (std::abs(parts.scaleY) < 1e-12)
        {
            return std::nullopt;
        }
        parts.shear = (c * cosine + d * sine) / parts.scaleY;
        return parts;
    }

    /// The 2 x 2 part of the matrix of the parts (no translation)
    QTransform compose() const
    {
        const double radians = qDegreesToRadians(angle);
        const QTransform scale(scaleX, 0.0, 0.0, scaleY, 0.0, 0.0);
        const QTransform shearing(1.0, 0.0, shear, 1.0, 0.0, 0.0);
        const QTransform rotation(std::cos(radians), std::sin(radians), -std::sin(radians), std::cos(radians), 0.0, 0.0);
        return scale * shearing * rotation;
    }
};

/// Helpers of the geometry cards
class PDFAppEditContentGeometryCardHelper
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFAppEditContentGeometryCardHelper)

public:
    PDFAppEditContentGeometryCardHelper() = delete;

    /// The matrix of the default space of the container to the space the user sees (R5-6: the default space of the page
    /// turned by its /Rotate; PDFAppEditContentCardTools::getContainerToDisplayed). Every value and every matrix of the
    /// cards of this file is in that space, so X, Y, W, H, Rotation, the flips and Arrange follow the screen.
    static std::optional<QTransform> getContainerToPage(const PDFAppEditContentContext* context)
    {
        return PDFAppEditContentCardTools::getContainerToDisplayed(context);
    }

    /// The box of the node in the space the user sees (PDFAppEditContentGeometryHelper::getPageBoundingBox, mapped by
    /// the displayed page matrix \p displayed); null without a box
    static QRectF getDisplayedBox(const PDFAppEditContentContext* context, const PDFContentNodeReference& node, const QTransform& displayed)
    {
        const QRectF box = PDFAppEditContentGeometryHelper::getPageBoundingBox(context, node);
        return box.isNull() ? box : displayed.mapRect(box);
    }

    /// The union of the boxes of the nodes in the space the user sees (as PDFAppEditContentSelection::getBoundingBox
    /// for the whole selection)
    static QRectF getDisplayedBox(const PDFAppEditContentContext* context, const std::vector<PDFAppEditContentCardTools::SelectedNode>& nodes)
    {
        QRectF box;
        for (const PDFAppEditContentCardTools::SelectedNode& node : nodes)
        {
            box |= PDFAppEditContentGeometryHelper::getPageBoundingBox(context, node.reference);
        }
        return box.isNull() ? box : PDFAppEditContentCardTools::getDisplayedPageMatrix(context).mapRect(box);
    }

    /// A matrix T of the page space in the container: C x T x C^-1
    static std::optional<QTransform> toContainer(const QTransform& pageMatrix, const QTransform& containerToPage)
    {
        bool isInvertible = false;
        const QTransform pageToContainer = containerToPage.inverted(&isInvertible);
        if (!isInvertible)
        {
            return std::nullopt;
        }
        return containerToPage * pageMatrix * pageToContainer;
    }

    /// The move of a box by a scale about its upper left corner (the left edge and the upper edge stay; in the space
    /// with the y axis up the upper edge is QRectF::bottom)
    static QTransform scaleAboutUpperLeft(const QRectF& box, double scaleX, double scaleY)
    {
        const QPointF anchor(box.left(), box.bottom());
        return QTransform::fromTranslate(-anchor.x(), -anchor.y()) * QTransform::fromScale(scaleX, scaleY) * QTransform::fromTranslate(anchor.x(), anchor.y());
    }

    /// The matrix of the node to the space the user sees: of the node, or of its first drawing leaf for a group (the
    /// transform of a group goes to its leaves, R-1); a text by the text space of its first glyph, Tm x CTM (R7-5: a
    /// text is mostly rotated by its text matrix, PDFAppEditContentCardTools::getLeafMatrix)
    static std::optional<QTransform> getNodeMatrix(const PDFAppEditContentContext* context, const PDFAppEditContentCardTools::SelectedNode& node, const QTransform& containerToPage)
    {
        const PDFContentContainerPointer container = context->getContainer();
        PDFContentNodePointer leaf = node.node;
        if (!PDFContentNode::isLeafKind(node.node->getKind()) && node.node->getKind() != PDFContentNodeKind::TextObject)
        {
            bool isTruncated = false;
            const std::vector<PDFContentNodePointer> leaves = PDFAppEditContentCardTools::getLeaves(*container, { node }, &PDFAppEditContentCardTools::isDrawingLeaf, &isTruncated);
            if (leaves.empty())
            {
                return std::nullopt;
            }
            leaf = leaves.front();
        }

        const std::optional<QTransform> matrix = PDFAppEditContentCardTools::getLeafMatrix(*container, *leaf, PDFAppEditContentCardTools::getDisplayContext(context));
        if (!matrix)
        {
            return std::nullopt;
        }
        return *matrix * containerToPage;
    }

    /// A field of a length or of an angle
    static PDFDoubleSpinBox* createField(QWidget* parent, const QString& objectName, double minimum, double maximum, const QString& suffix, int decimals = 2)
    {
        PDFDoubleSpinBox* field = new PDFDoubleSpinBox(parent);
        field->setObjectName(objectName);
        field->setRange(minimum, maximum);
        field->setDecimals(decimals);
        field->setKeyboardTracking(false);
        field->setTrailingZerosHidden(true);
        if (!suffix.isEmpty())
        {
            field->setDisplaySuffix(suffix);
        }
        return field;
    }

    /// A short field with its one-letter label (X, Y, W, H of -transform)
    static QWidget* createLetterField(const QString& letter, PDFDoubleSpinBox* field, QWidget* parent)
    {
        QWidget* widget = new QWidget(parent);
        QHBoxLayout* layout = new QHBoxLayout(widget);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(6);
        PDFLabel* label = new PDFLabel(letter, PDFThemeFonts::Role::CaptionRegular, PDFThemeColors::TextSecondary, widget);
        label->setBuddy(field);
        field->setParent(widget);
        field->setAccessibleName(letter);
        layout->addWidget(label);
        layout->addWidget(field, 1);
        return widget;
    }

    /// Two parts of matrices are the same up to the rounding of the matrices
    static bool isSameReal(const double& left, const double& right)
    {
        return std::abs(left - right) < 1e-6;
    }

    static void setFieldValue(PDFDoubleSpinBox* field, const PDFAppEditContentMixedValue<double>& value)
    {
        if (field->hasFocus())
        {
            return;
        }
        field->setValue(value.getValue().value_or(0.0));
        field->setMixed(value.isMixed());
    }
};

/// The card Position and size (-transform): X, Y, W, H, Rotation, the flips, the matrix
class PDFAppEditContentPositionCard : public PDFAppEditContentCard
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFAppEditContentPositionCard)

public:
    explicit PDFAppEditContentPositionCard(PDFAppEditContentContext* context, QWidget* parent) :
        PDFAppEditContentCard(context, QLatin1String(PDFAppEditContentCardIds::POSITION_AND_SIZE), parent)
    {
        using H = PDFAppEditContentGeometryCardHelper;
        m_section = addSection(tr("Position and size"));
        QVBoxLayout* layout = nullptr;
        QWidget* expanded = PDFAppEditContentCardTools::createColumn(m_section, &layout);
        const QString unit = PDFAppEditContentCardTools::getUnitSuffix(context);
        constexpr double LIMIT = 1.0e6;
        m_x = H::createField(expanded, QStringLiteral("editContentX"), -LIMIT, LIMIT, unit);
        m_y = H::createField(expanded, QStringLiteral("editContentY"), -LIMIT, LIMIT, unit);
        m_width = H::createField(expanded, QStringLiteral("editContentW"), 0.0, LIMIT, unit);
        m_height = H::createField(expanded, QStringLiteral("editContentH"), 0.0, LIMIT, unit);
        layout->addWidget(createPair(H::createLetterField(tr("X"), m_x, expanded), H::createLetterField(tr("Y"), m_y, expanded), expanded));
        layout->addWidget(createPair(H::createLetterField(tr("W"), m_width, expanded), H::createLetterField(tr("H"), m_height, expanded), expanded));

        // Rotation and the flips
        QWidget* rotateRow = new QWidget(expanded);
        QHBoxLayout* rotateLayout = new QHBoxLayout(rotateRow);
        rotateLayout->setContentsMargins(0, 0, 0, 0);
        rotateLayout->setSpacing(4);
        m_rotation = H::createField(rotateRow, QStringLiteral("editContentRotation"), -360.0, 360.0, tr("°"));
        m_rotation->setAccessibleName(tr("Rotation"));
        m_rotation->setFixedWidth(86);
        m_flipHorizontally = PDFAppDocumentToolbar::createToolbarButton(QStringLiteral("editContentFlipHorizontally"), QStringLiteral("flip-h"), rotateRow);
        m_flipHorizontally->setToolTip(tr("Flip horizontally"));
        m_flipHorizontally->setAccessibleName(tr("Flip horizontally"));
        m_flipVertically = PDFAppDocumentToolbar::createToolbarButton(QStringLiteral("editContentFlipVertically"), QStringLiteral("flip-v"), rotateRow);
        m_flipVertically->setToolTip(tr("Flip vertically"));
        m_flipVertically->setAccessibleName(tr("Flip vertically"));
        rotateLayout->addWidget(new PDFLabel(tr("Rotation"), PDFThemeFonts::Role::Small, PDFThemeColors::TextSecondary, rotateRow), 1);
        rotateLayout->addWidget(m_rotation);
        rotateLayout->addWidget(m_flipHorizontally);
        rotateLayout->addWidget(m_flipVertically);
        layout->addWidget(rotateRow);

        // The matrix (shown by its check box; the state lives in the context as the expanded state of a section)
        m_matrixCheck = new QCheckBox(tr("Matrix"), expanded);
        m_matrixCheck->setObjectName(QStringLiteral("editContentMatrix"));
        layout->addWidget(m_matrixCheck);
        QVBoxLayout* matrixLayout = nullptr;
        m_matrix = PDFAppEditContentCardTools::createColumn(expanded, &matrixLayout);
        m_scaleX = H::createField(m_matrix, QStringLiteral("editContentScaleX"), -1000.0, 1000.0, QString());
        m_scaleY = H::createField(m_matrix, QStringLiteral("editContentScaleY"), -1000.0, 1000.0, QString());
        m_shear = H::createField(m_matrix, QStringLiteral("editContentShear"), -1000.0, 1000.0, QString());
        for (PDFDoubleSpinBox* field : { m_scaleX, m_scaleY, m_shear })
        {
            field->setTrailingZerosHidden(false);
        }
        matrixLayout->addWidget(PDFAppEditContentCardTools::createFieldRow(tr("Scale in X"), m_scaleX, m_matrix));
        matrixLayout->addWidget(PDFAppEditContentCardTools::createFieldRow(tr("Scale in Y"), m_scaleY, m_matrix));
        matrixLayout->addWidget(PDFAppEditContentCardTools::createFieldRow(tr("Shear"), m_shear, m_matrix));
        layout->addWidget(m_matrix);
        layout->addWidget(PDFAppEditContentCardTools::createNote(tr("The values describe the matrix of the object. Position is measured from the lower left corner of the page."), expanded));
        m_section->setExpandedWidget(expanded);
        m_expanded = expanded;

        connect(m_x, &PDFDoubleSpinBox::valueEntered, this, &PDFAppEditContentPositionCard::onXEntered);
        connect(m_y, &PDFDoubleSpinBox::valueEntered, this, &PDFAppEditContentPositionCard::onYEntered);
        connect(m_width, &PDFDoubleSpinBox::valueEntered, this, &PDFAppEditContentPositionCard::onWidthEntered);
        connect(m_height, &PDFDoubleSpinBox::valueEntered, this, &PDFAppEditContentPositionCard::onHeightEntered);
        connect(m_rotation, &PDFDoubleSpinBox::valueEntered, this, &PDFAppEditContentPositionCard::onRotationEntered);
        connect(m_scaleX, &PDFDoubleSpinBox::valueEntered, this, &PDFAppEditContentPositionCard::onScaleXEntered);
        connect(m_scaleY, &PDFDoubleSpinBox::valueEntered, this, &PDFAppEditContentPositionCard::onScaleYEntered);
        connect(m_shear, &PDFDoubleSpinBox::valueEntered, this, &PDFAppEditContentPositionCard::onShearEntered);
        connect(m_flipHorizontally, &QToolButton::clicked, this, &PDFAppEditContentPositionCard::onFlipHorizontallyClicked);
        connect(m_flipVertically, &QToolButton::clicked, this, &PDFAppEditContentPositionCard::onFlipVerticallyClicked);
        connect(m_matrixCheck, &QCheckBox::clicked, this, &PDFAppEditContentPositionCard::onMatrixClicked);
    }

    /// The key of the state of the check box Matrix in the context
    static QString getMatrixKey() { return QLatin1String(PDFAppEditContentCardIds::POSITION_AND_SIZE) + QStringLiteral("/matrix"); }

    virtual void updateCard() override
    {
        using H = PDFAppEditContentGeometryCardHelper;
        const PDFAppEditContentContext* context = getContext();
        if (!context || !context->getContainer())
        {
            return;
        }

        // The values of the inspected nodes (bounded); the box is the box of the nodes a command moves - the whole
        // changeable selection (R7-1, R7-4), or the selection read only, when only locked objects are selected. Every
        // value is in the space the user sees (R5-6: the page turned by its /Rotate).
        bool isTruncated = false;
        bool isChangeable = false;
        const std::vector<PDFAppEditContentCardTools::SelectedNode> nodes = PDFAppEditContentCardTools::getInspectedNodes(context, &isTruncated, &isChangeable);
        m_expanded->setEnabled(isChangeable);
        m_box = isChangeable ? getBox(context) : PDFAppEditContentCardTools::getDisplayedPageMatrix(context).mapRect(context->getSelection()->getBoundingBox());
        m_pageBox = getPageBox(context);
        const double factor = PDFAppEditContentCardTools::getUnitFactor(context);
        const QPointF origin = m_pageBox.isNull() ? QPointF() : m_pageBox.topLeft();

        // X, Y, W, H of the box of the selection (from the lower left corner of the page, in the units)
        const bool hasBox = !m_box.isNull();
        for (PDFDoubleSpinBox* field : { m_x, m_y, m_width, m_height })
        {
            field->setEnabled(hasBox);
            field->setDisplaySuffix(PDFAppEditContentCardTools::getUnitSuffix(context));
        }
        if (hasBox)
        {
            const double values[] = { (m_box.left() - origin.x()) / factor, (m_box.top() - origin.y()) / factor, m_box.width() / factor, m_box.height() / factor };
            PDFDoubleSpinBox* fields[] = { m_x, m_y, m_width, m_height };
            for (int i = 0; i < 4; ++i)
            {
                PDFAppEditContentMixedValue<double> value;
                value.add(values[i]);
                H::setFieldValue(fields[i], value);
            }
            m_section->setSummaryText(tr("X %1 · Y %2 · W %3 · H %4 %5").arg(PDFAppEditContentCardTools::formatReal(values[0], 1), PDFAppEditContentCardTools::formatReal(values[1], 1),
                                                                            PDFAppEditContentCardTools::formatReal(values[2], 1), PDFAppEditContentCardTools::formatReal(values[3], 1),
                                                                            PDFAppEditContentCardTools::getUnitSuffix(context)));
        }
        else
        {
            m_section->setSummaryText(tr("The object has no extent on the page"));
        }

        // The matrices of the objects (Mixed, when they differ)
        PDFAppEditContentMixedValue<double> angle;
        PDFAppEditContentMixedValue<double> scaleX;
        PDFAppEditContentMixedValue<double> scaleY;
        PDFAppEditContentMixedValue<double> shear;
        for (const Parts& node : getParts(context, nodes))
        {
            const std::optional<PDFAppEditContentMatrixParts>& parts = node.parts;
            if (parts)
            {
                angle.add(parts->angle, &PDFAppEditContentGeometryCardHelper::isSameReal);
                scaleX.add(parts->scaleX, &PDFAppEditContentGeometryCardHelper::isSameReal);
                scaleY.add(parts->scaleY, &PDFAppEditContentGeometryCardHelper::isSameReal);
                shear.add(parts->shear, &PDFAppEditContentGeometryCardHelper::isSameReal);
            }
        }
        if (isTruncated)
        {
            angle.add(1.0e9);
            scaleX.add(1.0e9);
            scaleY.add(1.0e9);
            shear.add(1.0e9);
        }

        H::setFieldValue(m_rotation, angle);
        H::setFieldValue(m_scaleX, scaleX);
        H::setFieldValue(m_scaleY, scaleY);
        H::setFieldValue(m_shear, shear);
        const bool hasMatrix = !angle.isEmpty();
        for (PDFDoubleSpinBox* field : { m_rotation, m_scaleX, m_scaleY, m_shear })
        {
            field->setEnabled(hasMatrix);
        }
        m_flipHorizontally->setEnabled(hasBox);
        m_flipVertically->setEnabled(hasBox);

        const bool isMatrixShown = context->isSectionExpanded(getMatrixKey());
        m_matrixCheck->setChecked(isMatrixShown);
        m_matrix->setVisible(isMatrixShown);
    }

    virtual void commitPendingEdit() override
    {
        // Each field, which has a typed value, commits it as its own command; the card may be retired by a command
        const std::vector<QPointer<PDFDoubleSpinBox>> fields = { m_x, m_y, m_width, m_height, m_rotation, m_scaleX, m_scaleY, m_shear };
        for (const QPointer<PDFDoubleSpinBox>& field : fields)
        {
            if (field && !PDFDeferredDeletion::isRetired(this))
            {
                field->commitEntry();
            }
        }
    }

    virtual void discardPendingEdit() override
    {
        for (PDFDoubleSpinBox* field : { m_x, m_y, m_width, m_height, m_rotation, m_scaleX, m_scaleY, m_shear })
        {
            field->setValue(field->value());
        }
    }

private:
    /// The matrix of a selected node to the page and its parts (no value: the node has none)
    struct Parts
    {
        PDFAppEditContentCardTools::SelectedNode node;
        std::optional<QTransform> matrix;
        std::optional<PDFAppEditContentMatrixParts> parts;
    };

    static QWidget* createPair(QWidget* left, QWidget* right, QWidget* parent)
    {
        QWidget* row = new QWidget(parent);
        QHBoxLayout* layout = new QHBoxLayout(row);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(9);
        layout->addWidget(left, 1);
        layout->addWidget(right, 1);
        return row;
    }

    /// The matrices of the nodes to the space the user sees and their parts, in the order of \p nodes
    static std::vector<Parts> getParts(const PDFAppEditContentContext* context, const std::vector<PDFAppEditContentCardTools::SelectedNode>& nodes)
    {
        std::vector<Parts> result;
        const std::optional<QTransform> containerToPage = PDFAppEditContentGeometryCardHelper::getContainerToPage(context);
        result.reserve(nodes.size());
        for (const PDFAppEditContentCardTools::SelectedNode& node : nodes)
        {
            Parts parts;
            parts.node = node;
            parts.matrix = containerToPage ? PDFAppEditContentGeometryCardHelper::getNodeMatrix(context, node, *containerToPage) : std::nullopt;
            parts.parts = parts.matrix ? PDFAppEditContentMatrixParts::decompose(*parts.matrix) : std::nullopt;
            result.push_back(std::move(parts));
        }
        return result;
    }

    /// The box of the whole changeable selection in the space the user sees; computed once per container version and
    /// selection (R7-9: a selection of 100 000 objects is not measured again at every rebuild of the stack)
    QRectF getBox(const PDFAppEditContentContext* context)
    {
        const std::vector<PDFAppEditContentCardTools::SelectedNode> nodes = PDFAppEditContentCardTools::getCommandNodes(context);
        std::vector<PDFContentNodeReference> references = PDFAppEditContentCardTools::getReferences(nodes);
        const PDFContentContainerPointer container = context ? context->getContainer() : nullptr;
        const QTransform displayed = PDFAppEditContentCardTools::getDisplayedPageMatrix(context);
        if (!container || m_boxContainer.lock() != container || m_boxNodes != references || m_boxDisplayed != displayed ||
            m_boxIsolation != context->getIsolationPath())
        {
            m_boxContainer = container;
            m_boxNodes = std::move(references);
            m_boxDisplayed = displayed;
            m_boxIsolation = context ? context->getIsolationPath() : std::vector<PDFAppEditContentIsolationLevel>();
            m_boxValue = PDFAppEditContentGeometryCardHelper::getDisplayedBox(context, nodes);
        }
        return m_boxValue;
    }

    /// The box of the page (its crop box) in the space the user sees
    static QRectF getPageBox(const PDFAppEditContentContext* context)
    {
        const QRectF pageBox = PDFAppEditContentCardTools::getPageBox(context);
        return pageBox.isNull() ? pageBox : PDFAppEditContentCardTools::getDisplayedPageMatrix(context).mapRect(pageBox);
    }

    /// One Transform command of the whole changeable selection by the matrix of the space the user sees
    void transformAll(const QTransform& pageMatrix)
    {
        PDFAppEditContentContext* context = getContext();
        const std::vector<PDFContentNodeReference> nodes = PDFAppEditContentCardTools::getReferences(PDFAppEditContentCardTools::getCommandNodes(context));
        const std::optional<QTransform> containerToPage = PDFAppEditContentGeometryCardHelper::getContainerToPage(context);
        const std::optional<QTransform> matrix = containerToPage ? PDFAppEditContentGeometryCardHelper::toContainer(pageMatrix, *containerToPage) : std::nullopt;
        if (!context || !matrix || nodes.empty())
        {
            return;
        }
        context->execute(PDFContentCommandFactory::createTransform(nodes, *matrix));
    }

    /// Each object by its own matrix of the space the user sees (\p pageMatrices: one per item of \p nodes): one undo
    /// step. Cost: one command per object, which changes (a macro).
    void transformEach(const std::vector<Parts>& nodes, const std::vector<std::optional<QTransform>>& pageMatrices, const QString& text)
    {
        PDFAppEditContentContext* context = getContext();
        const std::optional<QTransform> containerToPage = PDFAppEditContentGeometryCardHelper::getContainerToPage(context);
        if (!context || !containerToPage)
        {
            return;
        }

        std::vector<std::pair<PDFContentNodeReference, QTransform>> moves;
        for (size_t i = 0; i < pageMatrices.size() && i < nodes.size(); ++i)
        {
            if (!pageMatrices[i] || pageMatrices[i]->isIdentity())
            {
                continue;
            }
            const std::optional<QTransform> matrix = PDFAppEditContentGeometryCardHelper::toContainer(*pageMatrices[i], *containerToPage);
            if (matrix)
            {
                moves.emplace_back(nodes[i].node.reference, *matrix);
            }
        }

        if (moves.empty())
        {
            return;
        }

        QPointer<PDFAppEditContentContext> guard(context);
        const bool isMacro = moves.size() > 1;
        if (isMacro)
        {
            context->beginMacro(text);
        }
        for (const auto& move : moves)
        {
            if (!guard || PDFDeferredDeletion::isRetired(guard))
            {
                break;
            }
            guard->execute(PDFContentCommandFactory::createTransform({ move.first }, move.second));
        }
        if (isMacro && guard)
        {
            guard->endMacro();
        }
    }

    /// A row of the matrix
    enum class Part
    {
        ScaleX,
        ScaleY,
        Shear
    };

    /// The new matrix of each object from its parts with \p part set to \p value: D = M^-1 x L x M, where L = the new
    /// 2 x 2 part x the old 2 x 2 part^-1 (the origin of the object stays)
    void changeParts(Part part, double value, const QString& text)
    {
        // The whole changeable selection, read when the user acts (R7-1)
        const std::vector<Parts> nodes = getParts(getContext(), PDFAppEditContentCardTools::getCommandNodes(getContext()));
        std::vector<std::optional<QTransform>> matrices;
        for (const Parts& parts : nodes)
        {
            std::optional<QTransform> result;
            if (parts.matrix && parts.parts)
            {
                PDFAppEditContentMatrixParts newParts = *parts.parts;
                switch (part)
                {
                    case Part::ScaleX:
                        newParts.scaleX = std::abs(value);
                        break;
                    case Part::ScaleY:
                        newParts.scaleY = value;
                        break;
                    case Part::Shear:
                        newParts.shear = value;
                        break;
                }
                bool isInvertible = false;
                const QTransform oldLinear = parts.parts->compose();
                const QTransform inverseOld = oldLinear.inverted(&isInvertible);
                bool isMatrixInvertible = false;
                const QTransform inverseMatrix = parts.matrix->inverted(&isMatrixInvertible);
                if (isInvertible && isMatrixInvertible)
                {
                    result = inverseMatrix * (newParts.compose() * inverseOld) * *parts.matrix;
                }
            }
            matrices.push_back(result);
        }
        transformEach(nodes, matrices, text);
    }

    void onXEntered(double value)
    {
        // The box of the whole changeable selection now (the box the field shows, R7-1)
        PDFAppEditContentContext* context = getContext();
        const QRectF box = getBox(context);
        const QRectF pageBox = getPageBox(context);
        const double factor = PDFAppEditContentCardTools::getUnitFactor(context);
        const double left = value * factor + (pageBox.isNull() ? 0.0 : pageBox.left());
        if (!box.isNull())
        {
            transformAll(QTransform::fromTranslate(left - box.left(), 0.0));
        }
    }

    void onYEntered(double value)
    {
        PDFAppEditContentContext* context = getContext();
        const QRectF box = getBox(context);
        const QRectF pageBox = getPageBox(context);
        const double factor = PDFAppEditContentCardTools::getUnitFactor(context);
        const double bottom = value * factor + (pageBox.isNull() ? 0.0 : pageBox.top());
        if (!box.isNull())
        {
            transformAll(QTransform::fromTranslate(0.0, bottom - box.top()));
        }
    }

    void onWidthEntered(double value)
    {
        // QE.6: until stage E3 a path is resized by a matrix too; a box without a width cannot be scaled
        PDFAppEditContentContext* context = getContext();
        const QRectF box = getBox(context);
        const double width = value * PDFAppEditContentCardTools::getUnitFactor(context);
        if (!box.isNull() && box.width() > 0.0 && width > 0.0)
        {
            transformAll(PDFAppEditContentGeometryCardHelper::scaleAboutUpperLeft(box, width / box.width(), 1.0));
        }
    }

    void onHeightEntered(double value)
    {
        PDFAppEditContentContext* context = getContext();
        const QRectF box = getBox(context);
        const double height = value * PDFAppEditContentCardTools::getUnitFactor(context);
        if (!box.isNull() && box.height() > 0.0 && height > 0.0)
        {
            transformAll(PDFAppEditContentGeometryCardHelper::scaleAboutUpperLeft(box, 1.0, height / box.height()));
        }
    }

    void onRotationEntered(double value)
    {
        // Each object turns about the center of its own box (in the space the user sees) to the angle (one undo step)
        const PDFAppEditContentContext* context = getContext();
        const std::vector<Parts> nodes = getParts(context, PDFAppEditContentCardTools::getCommandNodes(context));
        const QTransform displayed = PDFAppEditContentCardTools::getDisplayedPageMatrix(context);
        std::vector<std::optional<QTransform>> matrices;
        for (const Parts& node : nodes)
        {
            std::optional<QTransform> result;
            const QRectF box = PDFAppEditContentGeometryCardHelper::getDisplayedBox(context, node.node.reference, displayed);
            if (node.parts && !box.isNull())
            {
                const QPointF center = box.center();
                QTransform rotation;
                rotation.rotate(value - node.parts->angle);
                result = QTransform::fromTranslate(-center.x(), -center.y()) * rotation * QTransform::fromTranslate(center.x(), center.y());
            }
            matrices.push_back(result);
        }
        transformEach(nodes, matrices, tr("Rotate Objects"));
    }

    void onScaleXEntered(double value)
    {
        // A zero scale would make the matrix singular (the core refuses it)
        if (std::abs(value) > 1e-6)
        {
            changeParts(Part::ScaleX, value, tr("Scale Objects"));
        }
    }

    void onScaleYEntered(double value)
    {
        if (std::abs(value) > 1e-6)
        {
            changeParts(Part::ScaleY, value, tr("Scale Objects"));
        }
    }

    void onShearEntered(double value)
    {
        changeParts(Part::Shear, value, tr("Shear Objects"));
    }

    void onFlipHorizontallyClicked()
    {
        // The selection flips as a whole about the center of its box (the frame of the selection), left and right as
        // the user sees them
        const QRectF box = getBox(getContext());
        if (!box.isNull())
        {
            const QPointF center = box.center();
            transformAll(QTransform::fromTranslate(-center.x(), -center.y()) * QTransform::fromScale(-1.0, 1.0) * QTransform::fromTranslate(center.x(), center.y()));
        }
    }

    void onFlipVerticallyClicked()
    {
        const QRectF box = getBox(getContext());
        if (!box.isNull())
        {
            const QPointF center = box.center();
            transformAll(QTransform::fromTranslate(-center.x(), -center.y()) * QTransform::fromScale(1.0, -1.0) * QTransform::fromTranslate(center.x(), center.y()));
        }
    }

    void onMatrixClicked(bool isChecked)
    {
        if (PDFAppEditContentContext* context = getContext())
        {
            context->setSectionExpanded(getMatrixKey(), isChecked);
        }
        m_matrix->setVisible(isChecked);
    }

    PDFExpanderCard* m_section = nullptr;
    PDFDoubleSpinBox* m_x = nullptr;
    PDFDoubleSpinBox* m_y = nullptr;
    PDFDoubleSpinBox* m_width = nullptr;
    PDFDoubleSpinBox* m_height = nullptr;
    PDFDoubleSpinBox* m_rotation = nullptr;
    QToolButton* m_flipHorizontally = nullptr;
    QToolButton* m_flipVertically = nullptr;
    QCheckBox* m_matrixCheck = nullptr;
    QWidget* m_matrix = nullptr;
    PDFDoubleSpinBox* m_scaleX = nullptr;
    PDFDoubleSpinBox* m_scaleY = nullptr;
    PDFDoubleSpinBox* m_shear = nullptr;
    QWidget* m_expanded = nullptr;
    QRectF m_box;               ///< Shown by X, Y, W, H (the space the user sees)
    QRectF m_pageBox;

    // The cache of getBox
    std::weak_ptr<const PDFContentContainer> m_boxContainer;
    std::vector<PDFContentNodeReference> m_boxNodes;
    QTransform m_boxDisplayed;
    std::vector<PDFAppEditContentIsolationLevel> m_boxIsolation;
    QRectF m_boxValue;
};

/// The card Arrange (-multi-selected, -nothing-selected): Align, Distribute, Match size of the core
/// (pdfcontentarrange.h), Order and Group by the actions of the menus (P12)
class PDFAppEditContentArrangeCard : public PDFAppEditContentCard
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFAppEditContentArrangeCard)

public:
    explicit PDFAppEditContentArrangeCard(PDFAppEditContentContext* context, QWidget* parent) :
        PDFAppEditContentCard(context, QLatin1String(PDFAppEditContentCardIds::ARRANGE), parent)
    {
        m_section = addSection(tr("Arrange"));
    }

    /// The button of the operation (tests)
    QToolButton* getOperationButton(PDFContentArrange::Operation operation) const
    {
        for (const Button& button : m_buttons)
        {
            if (button.button && button.operation == operation && button.actionId.isEmpty())
            {
                return button.button;
            }
        }
        return nullptr;
    }

    virtual void updateCard() override
    {
        PDFAppEditContentContext* context = getContext();
        if (!context)
        {
            return;
        }

        // Two or more objects: an open section with the line of the key object (-multi-selected); otherwise the
        // collapsed card with its summary (-nothing-selected). The blocks are made again when the form changes
        // (updateCard runs from the event loop; the old ones are retired by the section).
        const int count = context->getSelection()->getCount();
        const bool isOpen = count >= 2;
        if (!m_blocks || isOpen != m_isOpen)
        {
            m_isOpen = isOpen;
            createBlocks();
        }

        if (m_keyLine)
        {
            m_keyLine->setText(tr("%Ln object(s) selected · aligned to the key object, the one with the thicker frame", nullptr, count));
        }

        // An operation needs its count of objects, which may be changed (R7-4: a locked object is left alone)
        const bool isEditing = context->isEditingAllowed();
        const int changeableCount = isEditing ? int(context->getChangeableSelection().size()) : 0;
        for (const Button& button : m_buttons)
        {
            if (!button.button)
            {
                continue;
            }

            if (button.actionId.isEmpty())
            {
                const int minimum = int(PDFContentArrange::getMinimumBoxCount(button.operation));
                button.button->setEnabled(isEditing && changeableCount >= minimum);
            }
            else
            {
                // An action of another package: shown when it is registered, enabled by its entry
                const PDFAppEditContentActionEntry* entry = context->getRegistry()->findAction(button.actionId);
                button.button->setVisible(entry != nullptr);
                button.button->setEnabled(entry && entry->isVisible(context) && entry->isEnabled(context));
            }
        }
    }

private:
    struct Button
    {
        QPointer<QToolButton> button;
        PDFContentArrange::Operation operation = PDFContentArrange::Operation::AlignLeft;
        QString actionId;   ///< An action of the registry instead of an operation
    };

    void createBlocks()
    {
        using Operation = PDFContentArrange::Operation;
        m_buttons.clear();

        QVBoxLayout* layout = nullptr;
        QWidget* blocks = PDFAppEditContentCardTools::createColumn(m_section, &layout);
        m_blocks = blocks;
        m_keyLine = nullptr;
        if (m_isOpen)
        {
            m_keyLine = PDFAppEditContentCardTools::createNote(QString(), blocks);
            m_keyLine->setObjectName(QStringLiteral("editContentArrangeKey"));
            layout->addWidget(m_keyLine);
        }

        QWidget* top = new QWidget(blocks);
        QHBoxLayout* topLayout = new QHBoxLayout(top);
        topLayout->setContentsMargins(0, 0, 0, 0);
        topLayout->setSpacing(29);
        topLayout->addWidget(createBlock(tr("Align"), 3, {
            { Operation::AlignLeft, QStringLiteral("obj-align-left"), tr("Align left") },
            { Operation::AlignHorizontalCenter, QStringLiteral("obj-align-hcenter"), tr("Align horizontal center") },
            { Operation::AlignRight, QStringLiteral("obj-align-right"), tr("Align right") },
            { Operation::AlignTop, QStringLiteral("obj-align-top"), tr("Align top") },
            { Operation::AlignVerticalCenter, QStringLiteral("obj-align-vcenter"), tr("Align vertical center") },
            { Operation::AlignBottom, QStringLiteral("obj-align-bottom"), tr("Align bottom") },
            { Operation::CenterHorizontallyOnPage, QStringLiteral("center-h"), tr("Center horizontally on page") },
            { Operation::CenterVerticallyOnPage, QStringLiteral("center-v"), tr("Center vertically on page") },
            { Operation::CenterOnPage, QStringLiteral("center-both"), tr("Center on page") } }, top), 0, Qt::AlignTop);

        QWidget* more = new QWidget(top);
        QVBoxLayout* moreLayout = new QVBoxLayout(more);
        moreLayout->setContentsMargins(0, 0, 0, 0);
        moreLayout->setSpacing(8);
        moreLayout->addWidget(createBlock(tr("Distribute"), 2, {
            { Operation::DistributeInRow, QStringLiteral("layout-rows"), tr("Distribute in a row") },
            { Operation::DistributeInColumn, QStringLiteral("layout-columns"), tr("Distribute in a column") },
            { Operation::DistributeAsForm, QStringLiteral("layout-form"), tr("Distribute as a form") },
            { Operation::DistributeAsGrid, QStringLiteral("layout-grid"), tr("Distribute as a grid") } }, more));
        moreLayout->addWidget(createBlock(tr("Match size"), 3, {
            { Operation::MatchWidth, QStringLiteral("same-width"), tr("Match width") },
            { Operation::MatchHeight, QStringLiteral("same-height"), tr("Match height") },
            { Operation::MatchSize, QStringLiteral("same-size"), tr("Match size") } }, more));
        topLayout->addWidget(more, 0, Qt::AlignTop);
        topLayout->addStretch(1);
        layout->addWidget(top);

        // Order and Group: the actions of the context menu (P12), started by their identifiers
        QWidget* bottom = new QWidget(blocks);
        QHBoxLayout* bottomLayout = new QHBoxLayout(bottom);
        bottomLayout->setContentsMargins(0, 0, 0, 0);
        bottomLayout->setSpacing(8);
        bottomLayout->addWidget(createActionBlock(tr("Order"), {
            { PDFAppEditContentActionIds::ORDER_BRING_TO_FRONT, QStringLiteral("bring-front"), tr("Bring to front") },
            { PDFAppEditContentActionIds::ORDER_BRING_FORWARD, QStringLiteral("bring-forward"), tr("Bring forward") },
            { PDFAppEditContentActionIds::ORDER_SEND_BACKWARD, QStringLiteral("send-backward"), tr("Send backward") },
            { PDFAppEditContentActionIds::ORDER_SEND_TO_BACK, QStringLiteral("send-back"), tr("Send to back") } }, bottom), 0, Qt::AlignTop);
        bottomLayout->addWidget(createActionBlock(tr("Group"), {
            { PDFAppEditContentActionIds::GROUP, QStringLiteral("group"), tr("Group") },
            { PDFAppEditContentActionIds::UNGROUP, QStringLiteral("ungroup"), tr("Ungroup") } }, bottom), 0, Qt::AlignTop);
        bottomLayout->addStretch(1);
        layout->addWidget(bottom);

        // Path operations come with stage E3 (P34): hidden until then, not disabled (zadani_gui.md 4.16)
        if (m_isOpen)
        {
            m_section->setSummaryText(QString());
            m_section->setExpandedWidget(nullptr);
            m_section->setContentWidget(blocks);
        }
        else
        {
            m_section->setContentWidget(nullptr);
            m_section->setSummaryText(tr("Align · order · group · path operations"));
            m_section->setExpandedWidget(blocks);
        }
    }

    struct OperationButton
    {
        PDFContentArrange::Operation operation;
        QString iconName;
        QString title;
    };

    struct ActionButton
    {
        const char* actionId;
        QString iconName;
        QString title;
    };

    QWidget* createBlock(const QString& title, int columns, const std::vector<OperationButton>& buttons, QWidget* parent)
    {
        QWidget* block = new QWidget(parent);
        QVBoxLayout* layout = new QVBoxLayout(block);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(4);
        layout->addWidget(new PDFLabel(title, PDFThemeFonts::Role::Caption, PDFThemeColors::TextSecondary, block));
        QWidget* group = new QWidget(block);
        QGridLayout* grid = new QGridLayout(group);
        grid->setContentsMargins(0, 0, 0, 0);
        grid->setSpacing(2);
        for (size_t i = 0; i < buttons.size(); ++i)
        {
            QToolButton* button = createButton(buttons[i].iconName, buttons[i].title, group);
            grid->addWidget(button, int(i) / columns, int(i) % columns);
            Button item;
            item.button = button;
            item.operation = buttons[i].operation;
            m_buttons.push_back(item);
            connect(button, &QToolButton::clicked, this, &PDFAppEditContentArrangeCard::onButtonClicked);
        }
        layout->addWidget(group);
        return block;
    }

    QWidget* createActionBlock(const QString& title, const std::vector<ActionButton>& buttons, QWidget* parent)
    {
        QWidget* block = new QWidget(parent);
        QVBoxLayout* layout = new QVBoxLayout(block);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(4);
        layout->addWidget(new PDFLabel(title, PDFThemeFonts::Role::Caption, PDFThemeColors::TextSecondary, block));
        QWidget* group = new QWidget(block);
        QHBoxLayout* row = new QHBoxLayout(group);
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(2);
        for (const ActionButton& action : buttons)
        {
            QToolButton* button = createButton(action.iconName, action.title, group);
            row->addWidget(button);
            Button item;
            item.button = button;
            item.actionId = QLatin1String(action.actionId);
            m_buttons.push_back(item);
            connect(button, &QToolButton::clicked, this, &PDFAppEditContentArrangeCard::onButtonClicked);
        }
        row->addStretch(1);
        layout->addWidget(group);
        return block;
    }

    static QToolButton* createButton(const QString& iconName, const QString& title, QWidget* parent)
    {
        QToolButton* button = PDFAppDocumentToolbar::createToolbarButton(QStringLiteral("editContentArrange_") + iconName, iconName, parent);
        button->setToolTip(title);
        button->setAccessibleName(title);
        return button;
    }

    void onButtonClicked()
    {
        PDFAppEditContentContext* context = getContext();
        const Button* clicked = nullptr;
        for (const Button& button : m_buttons)
        {
            if (button.button && button.button == sender())
            {
                clicked = &button;
                break;
            }
        }
        if (!context || !clicked)
        {
            return;
        }

        // An action of the registry runs from the event loop (PDFAppEditContentContext::triggerAction)
        if (!clicked->actionId.isEmpty())
        {
            context->triggerAction(clicked->actionId);
            return;
        }

        arrange(context, clicked->operation);
    }

    /// The command of the core over the boxes of the selection (PDFAppEditContentGeometryHelper) and the key object
    static void arrange(PDFAppEditContentContext* context, PDFContentArrange::Operation operation)
    {
        // The whole changeable selection (R7-1, R7-4); the key object is the key node of the selection (the last
        // selected one), wherever it lies in the selection; when it is locked, the last changeable node is the key
        const std::vector<PDFAppEditContentCardTools::SelectedNode> nodes = PDFAppEditContentCardTools::getCommandNodes(context);
        const std::optional<QTransform> containerToPage = PDFAppEditContentGeometryCardHelper::getContainerToPage(context);
        if (nodes.empty() || !containerToPage)
        {
            return;
        }

        // R5-6: the boxes and the page box in the space the user sees, the matrix of the container to that space
        // (PDFContentArrange: containerToPage x getDisplayedPageMatrix)
        const QTransform displayed = PDFAppEditContentCardTools::getDisplayedPageMatrix(context);
        std::vector<PDFContentNodeReference> references;
        std::vector<QRectF> boxes;
        references.reserve(nodes.size());
        boxes.reserve(nodes.size());
        size_t keyIndex = nodes.size() - 1;
        const PDFContentNodeReference key = context->getSelection()->getKeyNode();
        for (size_t i = 0; i < nodes.size(); ++i)
        {
            references.push_back(nodes[i].reference);
            boxes.push_back(PDFAppEditContentGeometryCardHelper::getDisplayedBox(context, nodes[i].reference, displayed));
            if (nodes[i].reference == key)
            {
                keyIndex = i;
            }
        }

        QString errorText;
        const QRectF pageBox = PDFAppEditContentCardTools::getPageBox(context);
        const PDFContentCommandPointer command = PDFContentArrange::createCommand(operation, std::move(references), boxes, keyIndex, pageBox.isNull() ? pageBox : displayed.mapRect(pageBox),
                                                                                  *containerToPage, &errorText);
        if (!command)
        {
            // The operation cannot be computed (a box too large, a form of too many objects): told by a notice
            PDFAppEditContentNoticeRequest notice;
            notice.id = QStringLiteral("arrange");
            notice.kind = PDFAppEditContentNoticeRequest::Kind::Event;
            notice.severity = PDFAppEditContentNoticeRequest::Severity::Warning;
            notice.text = errorText;
            context->showNotice(notice);
            return;
        }
        context->execute(command);
    }

    PDFExpanderCard* m_section = nullptr;
    QPointer<QWidget> m_blocks;
    QPointer<PDFLabel> m_keyLine;
    std::vector<Button> m_buttons;
    bool m_isOpen = false;
};

/// The entry of the card Position and size: an object is selected
class PDFAppEditContentPositionCardEntry : public PDFAppEditContentCardEntry
{
public:
    virtual QString getId() const override { return QLatin1String(PDFAppEditContentCardIds::POSITION_AND_SIZE); }

    virtual bool isShown(const PDFAppEditContentContext* context) const override
    {
        return PDFAppEditContentCardTools::isSelectionStateShown(context) && !context->getSelection()->isEmpty();
    }

    virtual PDFAppEditContentCard* createCard(PDFAppEditContentContext* context, QWidget* parent) const override { return new PDFAppEditContentPositionCard(context, parent); }
};

/// The entry of the card Arrange: always the last card of the selection, also with nothing selected
class PDFAppEditContentArrangeCardEntry : public PDFAppEditContentCardEntry
{
public:
    virtual QString getId() const override { return QLatin1String(PDFAppEditContentCardIds::ARRANGE); }
    virtual bool isShown(const PDFAppEditContentContext* context) const override { return PDFAppEditContentCardTools::isSelectionStateShown(context); }
    virtual PDFAppEditContentCard* createCard(PDFAppEditContentContext* context, QWidget* parent) const override { return new PDFAppEditContentArrangeCard(context, parent); }
};

void PDFAppEditContentGeometryCards::registerParts(PDFAppEditContentRegistry* registry)
{
    registry->addCard(std::make_unique<PDFAppEditContentPositionCardEntry>());
    registry->addCard(std::make_unique<PDFAppEditContentArrangeCardEntry>());
}

}   // namespace pdf
