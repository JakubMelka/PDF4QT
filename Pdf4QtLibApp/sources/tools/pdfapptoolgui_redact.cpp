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

#include "pdfapptoolgui_redact.h"

#include "pdfappannotationoperations.h"
#include "pdfappcolorpicker.h"
#include "pdfappdocument.h"
#include "pdfappdocumentpanels_common.h"
#include "pdfappdocumenttoolbar.h"
#include "pdfappdocumentview.h"
#include "pdfapplocale.h"
#include "pdfappmainwindow.h"
#include "pdfapppagelayout.h"
#include "pdfapptoolhost.h"
#include "pdfapptoolpanelbuilder.h"
#include "pdfapptoolpanelparts.h"
#include "pdfapptoolparametermodel.h"
#include "pdfapptooltoolbar.h"
#include "pdfappworkspace.h"
#include "pdfdocumenttoolguiregistry.h"
#include "pdfdocumenttoolshell.h"

#include "pdfannotation.h"
#include "pdfcatalog.h"
#include "pdfcompiler.h"
#include "pdfcomponentbadges.h"
#include "pdfcomponentpainter.h"
#include "pdfcomponentpanels.h"
#include "pdfcomponentrows.h"
#include "pdfdocumentbuilder.h"
#include "pdfdocumenttextindex.h"
#include "pdfdocumenttool_redact.h"
#include "pdfdocumenttoolregistry.h"
#include "pdfdrawspacecontroller.h"
#include "pdfdrawwidget.h"
#include "pdffluentwidgets.h"
#include "pdfmessagebox.h"
#include "pdfpainterutils.h"
#include "pdfthemefonts.h"
#include "pdfthemeiconengine.h"
#include "pdfthememanager.h"
#include "pdfwidgetsnapshot.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QScrollBar>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <limits>
#include <set>
#include <tuple>

namespace pdf
{

/// Helpers of the GUI of Redact Content
class PDFAppToolGUIRedactHelper
{
public:
    PDFAppToolGUIRedactHelper() = delete;

    static constexpr const char* CONTEXT = "pdf::PDFAppToolGUIRedact";
    static constexpr const char* FILL_PROPERTY = "pdf4qtRedactFill";

    /// Length of the text of a Text mark in its row (the row elides it further)
    static constexpr qsizetype MAXIMUM_DESCRIPTION_LENGTH = 48;

    /// card of section-Result 437:10427: the Key value rows of 28 are 8 apart
    static constexpr int SUMMARY_ROW_GAP = 8;

    /// The row Fill of the Result (Removed, Fill, Title, Metadata, Outline, Saved as)
    static constexpr int SUMMARY_FILL_ROW = 1;

    /// The outline of the selected mark (437:10685, 1599:48898: border/focus 2)
    static constexpr qreal SELECTED_OUTLINE_WIDTH = 2.0;

    /// search-hit 437:12412: the fill accent/amber/bg-pressed at 50 %
    static constexpr qreal SEARCH_HIT_OPACITY = 0.5;

    /// A mark to be written
    struct Request
    {
        PDFInteger pageIndex = -1;
        QRectF rectangle;
        QPolygonF quadrilaterals;
        const char* subject = PDFAppToolRedactMark::SUBJECT_AREA;
    };

    /// A count of a text, the singular right in English also without a translation
    static QString plural(int count, const char* singular, const char* pluralText)
    {
        return QCoreApplication::translate(CONTEXT, count == 1 ? singular : pluralText, nullptr, count);
    }

    /// The outline of the marks written into the document (/C): the canonical red of the annotation
    /// colors, the same in every theme. No /IC: the interior color of 12.5.6.23 fills the area after the
    /// redaction is applied; PDF4QT draws the annotation filled by it, which would hide the marked
    /// content while it is marked (437:10393 shows it framed).
    static QColor getMarkColor()
    {
        for (const PDFColorSwatchRow::Entry& entry : PDFColorSwatchRow::getAnnotationColors(PDFColorSwatchRow::Options()))
        {
            if (entry.token == PDFThemeColors::AnnotationRed && entry.canonicalColor.isValid())
            {
                return entry.canonicalColor;
            }
        }
        return QColor(229, 57, 53);
    }

    /// The crop box of the page (the media box, when the crop box is not valid)
    static QRectF getCropBox(const PDFDocument* document, PDFInteger pageIndex)
    {
        const PDFCatalog* catalog = document ? document->getCatalog() : nullptr;
        if (!catalog || pageIndex < 0 || pageIndex >= PDFInteger(catalog->getPageCount()))
        {
            return QRectF();
        }
        const PDFPage* page = catalog->getPage(size_t(pageIndex));
        const QRectF cropBox = page->getCropBox().normalized();
        return cropBox.isValid() ? cropBox : page->getMediaBox().normalized();
    }

    /// The Redact annotations of the requests in one change
    static std::optional<PDFModifiedDocument> createMarks(const PDFDocumentPointer& document, const std::vector<Request>& requests)
    {
        if (!document || requests.empty())
        {
            return std::nullopt;
        }

        const QColor color = getMarkColor();
        PDFDocumentModifier modifier(document.data());
        modifier.markAnnotationsChanged();
        PDFDocumentBuilder* builder = modifier.getBuilder();
        const PDFCatalog* catalog = document->getCatalog();
        bool isModified = false;
        for (const Request& request : requests)
        {
            if (request.pageIndex < 0 || request.pageIndex >= PDFInteger(catalog->getPageCount()))
            {
                continue;
            }

            const PDFObjectReference page = catalog->getPage(size_t(request.pageIndex))->getPageReference();
            const PDFObjectReference annotation = request.quadrilaterals.isEmpty() ? builder->createAnnotationRedact(page, request.rectangle, QColor(), color)
                                                                                   : builder->createAnnotationRedact(page, request.quadrilaterals, QColor(), color);
            builder->setAnnotationSubject(annotation, QString::fromLatin1(request.subject));
            builder->updateAnnotationAppearanceStreams(annotation);
            isModified = true;
        }

        if (!isModified || !modifier.finalize() || !modifier.getDocument())
        {
            return std::nullopt;
        }
        return PDFModifiedDocument(modifier.getDocument(), nullptr, modifier.getFlags());
    }

    /// The rows of a list of keys and values: the same keys keep their widgets
    static void setRows(PDFKeyValueList* list, const std::vector<std::pair<QString, QString>>& rows)
    {
        if (!list)
        {
            return;
        }

        bool isSameKeys = list->getRowCount() == int(rows.size());
        for (int i = 0; isSameKeys && i < int(rows.size()); ++i)
        {
            isSameKeys = list->getKey(i) == rows[size_t(i)].first;
        }
        if (!isSameKeys)
        {
            list->clear();
            for (const auto& row : rows)
            {
                list->addRow(row.first, row.second);
            }
            return;
        }
        for (int i = 0; i < int(rows.size()); ++i)
        {
            list->setValue(i, rows[size_t(i)].second);
        }
    }

    /// The order of the marks: by their pages (stable, the order of /Annots stays)
    static bool isMarkBefore(const PDFAppToolRedactMark& left, const PDFAppToolRedactMark& right)
    {
        return left.pageIndex < right.pageIndex;
    }

    /// The Bool parameter is on
    static bool isChecked(const PDFAppToolParameterModel* model, const char* parameter)
    {
        return model && model->getValue(QString::fromLatin1(parameter)).toBool();
    }

    /// A change of a mark by the engine of the annotations
    enum class MarkChange
    {
        None,
        Moved,
        Resized,
        Removed
    };

    /// How the engine has changed the mark from \p oldDocument to \p newDocument: removed from its page
    /// (or no annotation any more), resized (/Rect of another size), moved (another object of the same
    /// size), None (the same object). \p pageAnnotations caches /Annots of the pages of the new document.
    static MarkChange getMarkChange(const PDFDocument* oldDocument,
                                    const PDFDocument* newDocument,
                                    const PDFAppToolRedactMark& mark,
                                    std::map<PDFInteger, std::set<PDFObjectReference>>& pageAnnotations)
    {
        if (!oldDocument || !newDocument)
        {
            return MarkChange::None;
        }

        const PDFCatalog* catalog = newDocument->getCatalog();
        auto it = pageAnnotations.find(mark.pageIndex);
        if (it == pageAnnotations.end())
        {
            const PDFPage* page = mark.pageIndex >= 0 && mark.pageIndex < PDFInteger(catalog->getPageCount()) ? catalog->getPage(size_t(mark.pageIndex)) : nullptr;
            const std::vector<PDFObjectReference> annotations = page ? page->getAnnotations() : std::vector<PDFObjectReference>();
            it = pageAnnotations.emplace(mark.pageIndex, std::set<PDFObjectReference>(annotations.cbegin(), annotations.cend())).first;
        }
        if (!it->second.count(mark.annotation))
        {
            return MarkChange::Removed;
        }
        if (oldDocument->getObjectByReference(mark.annotation) == newDocument->getObjectByReference(mark.annotation))
        {
            return MarkChange::None;
        }
        const PDFAnnotationPtr annotation = PDFAnnotation::parse(&newDocument->getStorage(), mark.annotation);
        if (!annotation)
        {
            return MarkChange::Removed;
        }

        const QSizeF oldSize = mark.rectangle.size();
        const QSizeF newSize = annotation->getRectangle().normalized().size();
        const bool isResized = qAbs(oldSize.width() - newSize.width()) > 0.01 || qAbs(oldSize.height() - newSize.height()) > 0.01;
        return isResized ? MarkChange::Resized : MarkChange::Moved;
    }

    /// The window of a dialog of the GUI
    static QWidget* getDialogParent(const PDFDocumentToolGUI* gui)
    {
        const PDFAppToolHost* host = gui ? gui->getHost() : nullptr;
        QWidget* keyWidget = host ? host->getKeyWidget() : nullptr;
        return keyWidget ? keyWidget->window() : nullptr;
    }

    /// Closes a dialog of the GUI, which goes (its slots are not called any more)
    static void closeDialog(PDFMessageBox* dialog, QObject* receiver)
    {
        if (dialog)
        {
            dialog->disconnect(receiver);
            dialog->hide();
            dialog->deleteLater();
        }
    }
};

// ------------------------------------------------------------------ PDFAppToolRedactMark

QPainterPath PDFAppToolRedactMark::getPath() const
{
    QPainterPath path;
    path.setFillRule(Qt::WindingFill);
    if (quadrilaterals.size() >= 4)
    {
        // The points of a quadrilateral in the order of PDF (top left, top right, bottom left, bottom
        // right): the polygon goes around them
        for (qsizetype i = 0; i + 3 < quadrilaterals.size(); i += 4)
        {
            QPolygonF polygon;
            polygon << quadrilaterals[i] << quadrilaterals[i + 1] << quadrilaterals[i + 3] << quadrilaterals[i + 2];
            path.addPolygon(polygon);
            path.closeSubpath();
        }
    }
    if (path.isEmpty() && rectangle.isValid())
    {
        path.addRect(rectangle);
    }
    return path;
}

std::vector<PDFAppToolRedactMark> PDFAppToolRedactMark::readMarks(const PDFDocument* document, size_t maximumCount, bool* isTruncated)
{
    std::vector<PDFAppToolRedactMark> marks;
    if (isTruncated)
    {
        *isTruncated = false;
    }

    const PDFCatalog* catalog = document ? document->getCatalog() : nullptr;
    if (!catalog)
    {
        return marks;
    }

    std::set<PDFObjectReference> seen;
    for (size_t pageIndex = 0, pageCount = catalog->getPageCount(); pageIndex < pageCount; ++pageIndex)
    {
        const PDFPage* page = catalog->getPage(pageIndex);
        for (const PDFObjectReference& reference : page->getAnnotations())
        {
            // The subtype is read first: only the Redact annotations are parsed; an annotation shared by
            // two pages (a hostile file) is one mark of its first page
            const PDFDictionary* dictionary = document->getDictionaryFromObject(document->getObjectByReference(reference));
            const PDFObject& subtype = dictionary ? document->getObject(dictionary->get("Subtype")) : PDFObject();
            if (!subtype.isName() || subtype.getString() != "Redact" || !seen.insert(reference).second)
            {
                continue;
            }

            const PDFAnnotationPtr annotation = PDFAnnotation::parse(&document->getStorage(), reference);
            const PDFRedactAnnotation* redact = dynamic_cast<const PDFRedactAnnotation*>(annotation.data());
            if (!redact)
            {
                continue;
            }

            if (marks.size() >= maximumCount)
            {
                if (isTruncated)
                {
                    *isTruncated = true;
                }
                return marks;
            }

            PDFAppToolRedactMark mark;
            mark.annotation = reference;
            mark.pageIndex = PDFInteger(pageIndex);
            mark.rectangle = redact->getRectangle().normalized();

            // 7.2: a mark of another application may have any count of /QuadPoints; more than a run
            // accepts (MAXIMUM_QUADRILATERAL_COUNT: the run refuses the document) are not copied and
            // drawn - the mark is shown by its rectangle
            const PDFAnnotationQuadrilaterals::Quadrilaterals& quadrilaterals = redact->getRedactionRegion().getQuadrilaterals();
            mark.quadrilateralCount = quadrilaterals.size();
            if (quadrilaterals.size() <= size_t(PDFDocumentToolRedact::MAXIMUM_QUADRILATERAL_COUNT))
            {
                mark.quadrilaterals.reserve(qsizetype(quadrilaterals.size() * 4));
                for (const PDFAnnotationQuadrilaterals::Quadrilateral& quadrilateral : quadrilaterals)
                {
                    mark.quadrilaterals << quadrilateral[0] << quadrilateral[1] << quadrilateral[2] << quadrilateral[3];
                }
            }

            const QString& subject = redact->getSubject();
            if (subject == QLatin1String(SUBJECT_TEXT))
            {
                mark.kind = Kind::Text;
            }
            else if (subject == QLatin1String(SUBJECT_PAGE))
            {
                mark.kind = Kind::Page;
            }
            else if (subject == QLatin1String(SUBJECT_AREA))
            {
                mark.kind = Kind::Area;
            }
            else
            {
                // A mark of another program: the whole page, when it covers the crop box
                const QRectF cropBox = PDFAppToolGUIRedactHelper::getCropBox(document, mark.pageIndex);
                const QRectF bounds = mark.getPath().boundingRect();
                mark.kind = cropBox.isValid() && bounds.adjusted(-0.5, -0.5, 0.5, 0.5).contains(cropBox) ? Kind::Page : Kind::Area;
            }
            marks.push_back(std::move(mark));
        }
    }
    return marks;
}

// ------------------------------------------------------------------ PDFAppToolRedactMarkModel

PDFAppToolRedactMarkModel::PDFAppToolRedactMarkModel(QObject* parent) :
    BaseClass(parent)
{

}

PDFAppToolRedactMarkModel::~PDFAppToolRedactMarkModel() = default;

void PDFAppToolRedactMarkModel::setMarks(std::vector<PDFAppToolRedactMark> marks)
{
    beginResetModel();
    m_marks = std::move(marks);
    std::stable_sort(m_marks.begin(), m_marks.end(), &PDFAppToolGUIRedactHelper::isMarkBefore);

    // The caption of each page (437:10530 "Page 12 · 4 areas"), then its marks
    m_rows.clear();
    for (size_t i = 0; i < m_marks.size(); ++i)
    {
        const PDFInteger pageIndex = m_marks[i].pageIndex;
        if (i == 0 || m_marks[i - 1].pageIndex != pageIndex)
        {
            Row caption;
            caption.pageIndex = pageIndex;
            for (size_t j = i; j < m_marks.size() && m_marks[j].pageIndex == pageIndex; ++j)
            {
                ++caption.pageMarkCount;
            }
            m_rows.push_back(caption);
        }
        Row row;
        row.pageIndex = pageIndex;
        row.markIndex = int(i);
        m_rows.push_back(row);
    }
    endResetModel();
}

const PDFAppToolRedactMark* PDFAppToolRedactMarkModel::getMark(int row) const
{
    if (row < 0 || row >= int(m_rows.size()) || m_rows[size_t(row)].markIndex < 0)
    {
        return nullptr;
    }
    return &m_marks[size_t(m_rows[size_t(row)].markIndex)];
}

int PDFAppToolRedactMarkModel::getRow(PDFObjectReference annotation) const
{
    for (size_t i = 0; i < m_rows.size(); ++i)
    {
        if (m_rows[i].markIndex >= 0 && m_marks[size_t(m_rows[i].markIndex)].annotation == annotation)
        {
            return int(i);
        }
    }
    return -1;
}

void PDFAppToolRedactMarkModel::setSelectedMark(PDFObjectReference annotation)
{
    if (m_selectedMark == annotation)
    {
        return;
    }

    m_selectedMark = annotation;
    if (!m_rows.empty())
    {
        Q_EMIT dataChanged(index(0), index(int(m_rows.size()) - 1), { PDFComponentRoles::TileToneRole });
    }
}

QString PDFAppToolRedactMarkModel::getTitle(const PDFAppToolRedactMark& mark)
{
    switch (mark.kind)
    {
        case PDFAppToolRedactMark::Kind::Text:
            return tr("Text");
        case PDFAppToolRedactMark::Kind::Page:
            return tr("Whole page");
        default:
            return tr("Area");
    }
}

QString PDFAppToolRedactMarkModel::getDescription(const PDFAppToolRedactMark& mark)
{
    switch (mark.kind)
    {
        case PDFAppToolRedactMark::Kind::Text:
            // 437:10541: the text under the mark (it is in the document anyway)
            return mark.description.isEmpty() ? tr("No text found under the mark") : tr("“%1”").arg(mark.description);
        case PDFAppToolRedactMark::Kind::Page:
            return tr("Everything on the page");
        default:
        {
            // 437:10573 "52 × 80 pt"
            const QRectF bounds = mark.getPath().boundingRect();
            return tr("%1 × %2 pt").arg(PDFAppLocale::formatNumber(qRound64(bounds.width())), PDFAppLocale::formatNumber(qRound64(bounds.height())));
        }
    }
}

QString PDFAppToolRedactMarkModel::getIconName(PDFAppToolRedactMark::Kind kind)
{
    switch (kind)
    {
        case PDFAppToolRedactMark::Kind::Text:
            return QStringLiteral("redact-text");
        case PDFAppToolRedactMark::Kind::Page:
            return QStringLiteral("redact-page");
        default:
            return QStringLiteral("redact-area");
    }
}

int PDFAppToolRedactMarkModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : int(m_rows.size());
}

QVariant PDFAppToolRedactMarkModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= int(m_rows.size()))
    {
        return QVariant();
    }

    const Row& row = m_rows[size_t(index.row())];
    const PDFAppToolRedactMark* mark = getMark(index.row());
    if (!mark)
    {
        switch (role)
        {
            case Qt::DisplayRole:
                return tr("Page %1 · %2").arg(PDFAppLocale::formatNumber(row.pageIndex + 1),
                                              PDFAppToolGUIRedactHelper::plural(row.pageMarkCount, QT_TRANSLATE_NOOP("pdf::PDFAppToolGUIRedact", "%Ln area"), QT_TRANSLATE_NOOP("pdf::PDFAppToolGUIRedact", "%Ln areas")));
            case PDFComponentRoles::TileRowKindRole:
                return int(PDFComponentPainter::TileRowKind::Caption);
            default:
                return QVariant();
        }
    }

    const bool isSelected = m_selectedMark.isValid() && mark->annotation == m_selectedMark;
    switch (role)
    {
        case Qt::DisplayRole:
            return getTitle(*mark);
        case PDFComponentRoles::SubtitleRole:
        case Qt::ToolTipRole:
            return getDescription(*mark);
        case PDFComponentRoles::IconNameRole:
            return getIconName(mark->kind);
        case PDFComponentRoles::TileToneRole:
            return int(isSelected ? PDFComponentPainter::TileRowTone::Accent : PDFComponentPainter::TileRowTone::Muted);
        case PDFComponentRoles::StateIconRole:
            return QStringLiteral("x");
        case PDFComponentRoles::StateIconColorRole:
            return int(PDFThemeColors::IconDefault);
        case Qt::AccessibleDescriptionRole:
            return tr("Page %1").arg(PDFAppLocale::formatNumber(mark->pageIndex + 1));
        default:
            return QVariant();
    }
}

Qt::ItemFlags PDFAppToolRedactMarkModel::flags(const QModelIndex& index) const
{
    if (!getMark(index.row()))
    {
        // A caption is neither selected nor the current item
        return Qt::NoItemFlags;
    }
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable;
}

// ------------------------------------------------------------------ PDFAppToolRedactMarkDelegate

PDFAppToolRedactMarkDelegate::PDFAppToolRedactMarkDelegate(QObject* parent) :
    BaseClass(parent)
{

}

PDFAppToolRedactMarkDelegate::~PDFAppToolRedactMarkDelegate() = default;

QList<PDFRowDelegate::Action> PDFAppToolRedactMarkDelegate::getActions(const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    QList<Action> actions = BaseClass::getActions(option, index);
    if (!index.isValid() || index.data(PDFComponentRoles::TileRowKindRole).toInt() == int(PDFComponentPainter::TileRowKind::Caption))
    {
        return actions;
    }

    // The × of the row (437:10535) and Delete on the current row
    Action remove;
    remove.id = RemoveAction;
    remove.rect = PDFComponentPainter::getTileRowEndIconRect(QRectF(option.rect), getContent(index), getItemState(option, index));
    remove.text = tr("Remove the mark");
    remove.shortcut = QKeySequence(Qt::Key_Delete);
    actions.append(remove);
    return actions;
}

// ------------------------------------------------------------------ PDFAppToolRedactOverlay

PDFAppToolRedactOverlay::PDFAppToolRedactOverlay(PDFWidget* pdfWidget, QWidget* canvasArea) :
    BaseClass(canvasArea),
    m_pdfWidget(pdfWidget),
    m_proxy(pdfWidget ? pdfWidget->getDrawWidgetProxy() : nullptr),
    m_drawWidget(pdfWidget && pdfWidget->getDrawWidget() ? pdfWidget->getDrawWidget()->getWidget() : nullptr)
{
    setObjectName(QStringLiteral("redactOverlay"));

    // Marks | Preview result is in the right panel (owner 2026-11-17): the widget has no area of its
    // own and lets the mouse through to the canvas
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setFixedSize(0, 0);

    // preview-bar 437:10607: "Page 308" Semi Bold 13, a second child of the canvas area (it lets the
    // mouse through to the canvas); the pages lie below its band (owner 2026-11-18)
    m_caption = new PDFAppToolPageCaption(canvasArea);
    m_caption->getLabel()->setObjectName(QStringLiteral("redactPageCaption"));

    if (m_proxy)
    {
        m_proxy->registerDrawInterface(this);
    }
    if (m_pdfWidget)
    {
        m_pdfWidget->addInputInterface(this);
    }
    connect(PDFThemeManager::getInstance(), &PDFThemeManager::themeChanged, this, &PDFAppToolRedactOverlay::updateCanvas);
}

PDFAppToolRedactOverlay::~PDFAppToolRedactOverlay()
{
    if (m_pdfWidget)
    {
        m_pdfWidget->removeInputInterface(this);
    }
    if (m_proxy)
    {
        m_proxy->unregisterDrawInterface(this);
    }
    if (PDFAppToolPageCaption* caption = m_caption.data())
    {
        // Its band of the canvas goes with it
        m_caption = nullptr;
        delete caption;
    }
    if (m_drawWidget)
    {
        m_drawWidget->update();
    }
}

void PDFAppToolRedactOverlay::setInput(Input input)
{
    m_input = input;
    m_dragPage = -1;
    m_textSelection = PDFTextSelection();
    m_cursor = input == Input::Area ? std::optional<QCursor>(QCursor(Qt::CrossCursor)) : std::nullopt;
    updateCanvas();
}

void PDFAppToolRedactOverlay::setMarks(const std::vector<PDFAppToolRedactMark>& marks, PDFObjectReference selectedMark)
{
    m_markPaths.clear();
    m_selectedPaths.clear();
    for (const PDFAppToolRedactMark& mark : marks)
    {
        const QPainterPath path = mark.getPath();
        m_markPaths[mark.pageIndex].push_back(path);
        if (selectedMark.isValid() && mark.annotation == selectedMark)
        {
            m_selectedPaths[mark.pageIndex] = path;
        }
    }
    updateCanvas();
}

void PDFAppToolRedactOverlay::setMatches(const std::vector<PDFAppToolRedactSearchPart::Match>& matches)
{
    m_matchQuadrilaterals.clear();
    for (const PDFAppToolRedactSearchPart::Match& match : matches)
    {
        m_matchQuadrilaterals[match.pageIndex].push_back(match.quadrilaterals);
    }
    updateCanvas();
}

void PDFAppToolRedactOverlay::setPreview(bool isPreview, const QColor& fill)
{
    m_isPreview = isPreview;
    m_previewFill = fill;
    updateCanvas();
}

void PDFAppToolRedactOverlay::setPageText(const QString& text)
{
    if (m_caption)
    {
        m_caption->setText(text);
    }
}

QString PDFAppToolRedactOverlay::getPageText() const
{
    return m_caption ? m_caption->getText() : QString();
}

std::optional<QRectF> PDFAppToolRedactOverlay::getDraggedArea() const
{
    if (m_input != Input::Area || m_dragPage < 0)
    {
        return std::nullopt;
    }
    return QRectF(m_dragStart, m_dragEnd).normalized();
}

void PDFAppToolRedactOverlay::drawPage(QPainter* painter,
                                       pdf::PDFInteger pageIndex,
                                       const PDFPrecompiledPage* compiledPage,
                                       PDFTextLayoutGetter& layoutGetter,
                                       const QTransform& pagePointToDevicePointMatrix,
                                       const PDFColorConvertor& convertor,
                                       QList<PDFRenderError>& errors) const
{
    Q_UNUSED(compiledPage);
    Q_UNUSED(errors);

    // The text being selected (437:11317); the other graphics are drawn by drawPostRendering over the
    // annotations of the view (another draw interface, its order against this one is not given)
    if (m_input == Input::Text && m_dragPage == pageIndex && !m_textSelection.isEmpty() && !m_isPreview)
    {
        PDFPainterStateGuard guard(painter);
        PDFTextSelectionPainter selectionPainter(&m_textSelection);
        selectionPainter.draw(painter, pageIndex, layoutGetter, pagePointToDevicePointMatrix, convertor);
    }
}

void PDFAppToolRedactOverlay::drawPostRendering(QPainter* painter, QRect rect) const
{
    Q_UNUSED(rect);

    if (!m_proxy)
    {
        return;
    }

    const PDFWidgetSnapshot snapshot = m_proxy->getSnapshot();
    for (const PDFWidgetSnapshot::SnapshotItem& item : snapshot.items)
    {
        PDFPainterStateGuard guard(painter);
        painter->setClipRect(item.rect, Qt::IntersectClip);
        drawPageGraphics(painter, item.pageIndex, item.pageToDeviceMatrix);
    }
}

void PDFAppToolRedactOverlay::drawPageGraphics(QPainter* painter, PDFInteger pageIndex, const QTransform& pagePointToDevicePointMatrix) const
{
    const PDFThemeManager* manager = PDFThemeManager::getInstance();
    PDFPainterStateGuard guard(painter);
    painter->setRenderHint(QPainter::Antialiasing, true);

    const auto marksIt = m_markPaths.find(pageIndex);
    if (m_isPreview)
    {
        // 437:13219: the areas as the copy shows them, filled by the fill (No fill = the white paper);
        // the view does not draw the marks themselves (their /C border) in the preview
        // (PDFAppToolGUIRedact::isAnnotationShown), the boxes have no edge of their own
        if (marksIt != m_markPaths.cend())
        {
            painter->setBrush(m_previewFill);
            painter->setPen(Qt::NoPen);
            for (const QPainterPath& path : marksIt->second)
            {
                painter->drawPath(pagePointToDevicePointMatrix.map(path));
            }
        }
        return;
    }

    // The marks: mark-area / mark-text 437:10686 (accent/red/bg-pressed), the selected one with
    // border/focus 2 (437:10685)
    if (marksIt != m_markPaths.cend())
    {
        QColor fill = manager->getColor(PDFThemeColors::AccentRedBgPressed);
        fill.setAlphaF(qMin<float>(fill.alphaF(), 0.28f));
        painter->setPen(Qt::NoPen);
        painter->setBrush(fill);
        for (const QPainterPath& path : marksIt->second)
        {
            painter->drawPath(pagePointToDevicePointMatrix.map(path));
        }
    }
    const auto selectedIt = m_selectedPaths.find(pageIndex);
    if (selectedIt != m_selectedPaths.cend())
    {
        painter->setBrush(Qt::NoBrush);
        painter->setPen(QPen(manager->getColor(PDFThemeColors::BorderFocus), PDFAppToolGUIRedactHelper::SELECTED_OUTLINE_WIDTH));
        painter->drawPath(pagePointToDevicePointMatrix.map(selectedIt->second));
    }

    // The matches of the search (437:12412 search-hit): accent/amber/bg-pressed at 50 % with the border
    // accent/amber/border-strong - the amber of the marks to be made, not the cyan of the Find of the view
    const auto matchesIt = m_matchQuadrilaterals.find(pageIndex);
    if (matchesIt != m_matchQuadrilaterals.cend())
    {
        QColor matchFill = manager->getColor(PDFThemeColors::AccentAmberBgPressed);
        matchFill.setAlphaF(PDFAppToolGUIRedactHelper::SEARCH_HIT_OPACITY);
        painter->setBrush(matchFill);
        painter->setPen(QPen(manager->getColor(PDFThemeColors::AccentAmberBorderStrong), 1.0));
        for (const QPolygonF& quadrilaterals : matchesIt->second)
        {
            PDFAppToolRedactMark match;
            match.quadrilaterals = quadrilaterals;
            painter->drawPath(pagePointToDevicePointMatrix.map(match.getPath()));
        }
    }

    // The area being dragged (437:10982: bg/accent-subtle with a dashed border/focus)
    if (m_input == Input::Area && m_dragPage == pageIndex)
    {
        QPen pen(manager->getColor(PDFThemeColors::BorderFocus), 1.0);
        pen.setStyle(Qt::DashLine);
        painter->setPen(pen);
        painter->setBrush(manager->getColor(PDFThemeColors::BgAccentSubtle));
        painter->drawPolygon(pagePointToDevicePointMatrix.map(QPolygonF(QRectF(m_dragStart, m_dragEnd).normalized())));
    }
}

void PDFAppToolRedactOverlay::shortcutOverrideEvent(QWidget* widget, QKeyEvent* event)
{
    // The keys stay the keys of the shell: Esc of a drag goes to the shell, which asks the GUI first
    // (PDFDocumentToolGUI::handleEscape ends the drag, then the mode, then the tool)
    Q_UNUSED(widget);
    Q_UNUSED(event);
}

void PDFAppToolRedactOverlay::keyPressEvent(QWidget* widget, QKeyEvent* event)
{
    Q_UNUSED(widget);

    // Esc of a move or a resize of a mark reaches the engine of the annotations after this interface
    // (it claims the key and cancels its drag): the status text follows after it (queued by the
    // receiver, the last call)
    if (m_input == Input::None && event->key() == Qt::Key_Escape)
    {
        Q_EMIT dragStateChanged();
    }
}

bool PDFAppToolRedactOverlay::isDragging() const
{
    return m_input != Input::None && m_dragPage >= 0;
}

void PDFAppToolRedactOverlay::cancelDrag()
{
    // No rectangle and no selection stay on the page; the release of the button marks nothing
    const bool wasDragging = m_dragPage >= 0;
    m_dragPage = -1;
    m_textSelection = PDFTextSelection();
    updateCanvas();
    if (wasDragging)
    {
        // The last call (queued by the receiver)
        Q_EMIT dragStateChanged();
    }
}

void PDFAppToolRedactOverlay::keyReleaseEvent(QWidget* widget, QKeyEvent* event)
{
    Q_UNUSED(widget);
    Q_UNUSED(event);
}

void PDFAppToolRedactOverlay::mousePressEvent(QWidget* widget, QMouseEvent* event)
{
    Q_UNUSED(widget);

    if (m_input == Input::None && event->button() == Qt::LeftButton)
    {
        // Without a mode the press goes on to the engine of the annotations (a move or a resize of a
        // mark may start): the status text follows after it (queued by the receiver, the last call)
        Q_EMIT dragStateChanged();
        return;
    }

    if (m_input == Input::None || event->button() != Qt::LeftButton || !m_proxy)
    {
        return;
    }

    QPointF pagePoint;
    const PDFInteger pageIndex = m_proxy->getPageUnderPoint(event->position(), &pagePoint);
    if (pageIndex < 0)
    {
        return;
    }

    m_dragPage = pageIndex;
    m_dragStart = pagePoint;
    m_dragEnd = pagePoint;
    m_textSelection = PDFTextSelection();
    event->accept();
    updateCanvas();

    // Queued by the receiver: the status text "Esc cancels the drag" (the last call)
    Q_EMIT dragStateChanged();
}

void PDFAppToolRedactOverlay::mouseDoubleClickEvent(QWidget* widget, QMouseEvent* event)
{
    Q_UNUSED(widget);

    // A double click of the modes is no selection of a word of the view
    if (m_input != Input::None)
    {
        event->accept();
    }
}

void PDFAppToolRedactOverlay::mouseMoveEvent(QWidget* widget, QMouseEvent* event)
{
    Q_UNUSED(widget);

    if (m_input == Input::None || !m_proxy)
    {
        return;
    }

    updateCursor(event->position());
    if (m_dragPage >= 0)
    {
        if (const std::optional<QPointF> pagePoint = mapToPage(m_dragPage, event->position()))
        {
            m_dragEnd = *pagePoint;
        }

        if (m_input == Input::Text && m_proxy->getTextLayoutCompiler())
        {
            PDFTextLayout layout = m_proxy->getTextLayoutCompiler()->getTextLayoutLazy(m_dragPage);
            m_textSelection = layout.createTextSelection(m_dragPage, m_dragStart, getSelectionEnd(), PDFThemeManager::getInstance()->getColor(PDFThemeColors::BgAccentSubtle));
        }
        updateCanvas();
    }
    event->accept();
}

void PDFAppToolRedactOverlay::mouseReleaseEvent(QWidget* widget, QMouseEvent* event)
{
    Q_UNUSED(widget);

    if (m_input == Input::None && event->button() == Qt::LeftButton)
    {
        // The release ends a move or a resize of a mark by the engine (as the press above)
        Q_EMIT dragStateChanged();
        return;
    }

    if (m_input == Input::None || event->button() != Qt::LeftButton || m_dragPage < 0)
    {
        return;
    }

    finishDrag(event->position());
    event->accept();
}

void PDFAppToolRedactOverlay::wheelEvent(QWidget* widget, QWheelEvent* event)
{
    Q_UNUSED(widget);
    Q_UNUSED(event);
}

QString PDFAppToolRedactOverlay::getTooltip() const
{
    return QString();
}

const std::optional<QCursor>& PDFAppToolRedactOverlay::getCursor() const
{
    return m_cursor;
}

int PDFAppToolRedactOverlay::getInputPriority() const
{
    // Before the tools of the view (the selection of the text, the magnifier)
    return ToolPriority - 1;
}

std::optional<QPointF> PDFAppToolRedactOverlay::mapToPage(PDFInteger pageIndex, const QPointF& position) const
{
    if (!m_proxy)
    {
        return std::nullopt;
    }

    // Also a point outside of the page (a drag over its edge) by the matrix of the page
    const PDFWidgetSnapshot snapshot = m_proxy->getSnapshot();
    const PDFWidgetSnapshot::SnapshotItem* item = snapshot.getPageSnapshot(pageIndex);
    if (!item || !item->pageToDeviceMatrix.isInvertible())
    {
        return std::nullopt;
    }
    return item->pageToDeviceMatrix.inverted().map(position);
}

QPointF PDFAppToolRedactOverlay::getSelectionEnd() const
{
    // A drag exactly along a line of the text (the same vertical position of both points) is a
    // selection of it: the rectangle of the points must not be empty for the text layout
    QPointF end = m_dragEnd;
    if (qFuzzyCompare(end.y() + 1.0, m_dragStart.y() + 1.0))
    {
        end.ry() -= 0.01;
    }
    if (qFuzzyCompare(end.x() + 1.0, m_dragStart.x() + 1.0))
    {
        end.rx() += 0.01;
    }
    return end;
}

QRectF PDFAppToolRedactOverlay::getCropBox(PDFInteger pageIndex) const
{
    return PDFAppToolGUIRedactHelper::getCropBox(m_proxy ? m_proxy->getDocument() : nullptr, pageIndex);
}

void PDFAppToolRedactOverlay::updateCursor(const QPointF& position)
{
    if (m_input == Input::Area)
    {
        m_cursor = QCursor(Qt::CrossCursor);
        return;
    }

    // Text: the I-beam over a text of the page
    QPointF pagePoint;
    const PDFInteger pageIndex = m_proxy ? m_proxy->getPageUnderPoint(position, &pagePoint) : -1;
    bool isOverText = false;
    if (pageIndex >= 0 && m_proxy->getTextLayoutCompiler())
    {
        const PDFTextLayout layout = m_proxy->getTextLayoutCompiler()->getTextLayoutLazy(pageIndex);
        isOverText = layout.isHoveringOverTextBlock(pagePoint);
    }
    m_cursor = QCursor(isOverText ? Qt::IBeamCursor : Qt::ArrowCursor);
}

void PDFAppToolRedactOverlay::finishDrag(const QPointF& position)
{
    if (const std::optional<QPointF> pagePoint = mapToPage(m_dragPage, position))
    {
        m_dragEnd = *pagePoint;
    }

    Pick pick;
    pick.pageIndex = m_dragPage;
    if (m_input == Input::Area)
    {
        // The area of the page only (its crop box), a click is no area
        const QRectF area = QRectF(m_dragStart, m_dragEnd).normalized().intersected(getCropBox(m_dragPage));
        if (area.width() >= MINIMUM_AREA_SIZE && area.height() >= MINIMUM_AREA_SIZE)
        {
            pick.kind = Pick::Kind::Area;
            pick.rectangle = area;
        }
    }
    else if (m_input == Input::Text && m_proxy && m_proxy->getTextLayoutCompiler())
    {
        PDFTextLayoutGetter getter = m_proxy->getTextLayoutCompiler()->getTextLayoutLazy(m_dragPage);
        PDFTextLayout layout = getter;
        if (layout.getTextBlocks().empty())
        {
            // A scanned page (2.6): the callout says to mark the area instead
            pick.kind = Pick::Kind::NoText;
        }
        else
        {
            const PDFTextSelection selection = layout.createTextSelection(m_dragPage, m_dragStart, getSelectionEnd(), Qt::black);
            QPolygonF quadrilaterals;
            PDFTextSelectionPainter selectionPainter(&selection);
            const QPainterPath path = selectionPainter.prepareGeometry(m_dragPage, getter, QTransform(), &quadrilaterals);
            if (!path.isEmpty() && !quadrilaterals.isEmpty())
            {
                pick.kind = Pick::Kind::Text;
                pick.quadrilaterals = quadrilaterals;
            }
        }
    }

    m_dragPage = -1;
    m_textSelection = PDFTextSelection();
    updateCanvas();

    if (pick.kind != Pick::Kind::None)
    {
        // The commit of the mark runs from the event loop, never inside of the dispatch of the input
        m_pick = pick;
        if (!m_isPickQueued)
        {
            m_isPickQueued = true;
            QMetaObject::invokeMethod(this, &PDFAppToolRedactOverlay::emitPick, Qt::QueuedConnection);
        }
    }

    // The last call (queued by the receiver): the status text no longer says "Esc cancels the drag"
    Q_EMIT dragStateChanged();
}

void PDFAppToolRedactOverlay::emitPick()
{
    m_isPickQueued = false;
    const Pick pick = m_pick;
    m_pick = Pick();

    switch (pick.kind)
    {
        case Pick::Kind::Area:
            Q_EMIT areaPicked(pick.pageIndex, pick.rectangle);
            break;
        case Pick::Kind::Text:
            Q_EMIT textPicked(pick.pageIndex, pick.quadrilaterals);
            break;
        case Pick::Kind::NoText:
            Q_EMIT textNotFound(pick.pageIndex);
            break;
        default:
            break;
    }
}

void PDFAppToolRedactOverlay::updateCanvas()
{
    if (m_drawWidget)
    {
        m_drawWidget->update();
    }
}

// ------------------------------------------------------------------ PDFAppToolRedactFillPanel

PDFAppToolRedactFillPanel::PDFAppToolRedactFillPanel(PDFAppDocumentView* view, int panelWidth, QWidget* parent) :
    BaseClass(view, panelWidth, parent)
{

}

PDFAppToolRedactFillPanel::~PDFAppToolRedactFillPanel() = default;

PDFColorSwatchRow* PDFAppToolRedactFillPanel::addColorGroup(const QString& caption)
{
    // Colors 437:12794: a caption and the swatches of the annotation colors with white and "+" (K6.5)
    m_colorDivider = addGroupDivider();
    m_colorCaption = addGroupCaption(caption);
    PDFColorSwatchRow* row = new PDFColorSwatchRow(this);
    row->setObjectName(QStringLiteral("redactFillColors"));
    row->setOptions(PDFColorSwatchRow::Option::White | PDFColorSwatchRow::Option::More);
    getContentLayout()->addWidget(row, 0, Qt::AlignLeading);
    m_colorRow = row;
    return row;
}

void PDFAppToolRedactFillPanel::setColorGroupVisible(bool isVisible)
{
    for (QWidget* widget : { m_colorDivider.data(), static_cast<QWidget*>(m_colorCaption.data()), static_cast<QWidget*>(m_colorRow.data()) })
    {
        if (widget)
        {
            widget->setVisible(isVisible);
        }
    }
    if (isVisible)
    {
        adjustSize();
    }
}

bool PDFAppToolRedactFillPanel::isColorGroupVisible() const
{
    return m_colorRow && !m_colorRow->isHidden();
}

// ------------------------------------------------------------------ PDFAppToolGUIRedact

PDFAppToolGUIRedact::PDFAppToolGUIRedact(QObject* parent) :
    BaseClass(parent)
{
    m_markModel = new PDFAppToolRedactMarkModel(this);
}

PDFAppToolGUIRedact::~PDFAppToolGUIRedact()
{
    // The engine of the annotations does not ask this GUI any more (the controller of the view lives
    // on); its slots of the controller are disconnected first (setToolEditing may emit its selection)
    if (PDFAppAnnotationController* controller = m_annotationController.data())
    {
        controller->disconnect(this);
        if (controller->getToolEditing() == this)
        {
            controller->setToolEditing(nullptr);
        }
    }

    // R6-19: the term and the results are forgotten with the tool
    if (m_searchPart)
    {
        m_searchPart->disconnect(this);
        m_searchPart->clear();
    }

    // The dialogs of the GUI go with it (their windows are of the main window)
    for (PDFMessageBox* dialog : { m_pagesDialog.data(), m_closePrompt.data(), m_clearDialog.data() })
    {
        PDFAppToolGUIRedactHelper::closeDialog(dialog, this);
    }
}

void PDFAppToolGUIRedact::registerRedactTools()
{
    const QString toolId = QString::fromLatin1(PDFDocumentToolRedact::TOOL_ID);
    if (!PDFDocumentToolRegistry::isImplemented(toolId))
    {
        PDFDocumentToolRegistry::registerTool(std::make_shared<PDFDocumentToolRedact>());
    }
    if (!PDFDocumentToolGUIRegistry::getFactory(toolId))
    {
        PDFDocumentToolGUIRegistry::registerFactory(toolId, std::make_shared<PDFDocumentToolGUIFactoryOf<PDFAppToolGUIRedact>>());
    }
}

QToolButton* PDFAppToolGUIRedact::getFillButton() const
{
    return m_fillPanel ? m_fillPanel->getButton() : nullptr;
}

QToolButton* PDFAppToolGUIRedact::getKeepButton() const
{
    return m_keepMenu ? m_keepMenu->getButton() : nullptr;
}

PDFTileRow* PDFAppToolGUIRedact::getFillRow(Fill fill) const
{
    const auto it = m_fillRows.find(fill);
    return it != m_fillRows.end() ? it->second.data() : nullptr;
}

const std::vector<PDFAppToolRedactMark>& PDFAppToolGUIRedact::getMarks() const
{
    static const std::vector<PDFAppToolRedactMark> none;
    return m_markModel ? m_markModel->getMarks() : none;
}

int PDFAppToolGUIRedact::getMarkPageCount() const
{
    std::set<PDFInteger> pages;
    for (const PDFAppToolRedactMark& mark : getMarks())
    {
        pages.insert(mark.pageIndex);
    }
    return int(pages.size());
}

// ------------------------------------------------------------------ marks

void PDFAppToolGUIRedact::refreshMarks()
{
    const PDFAppDocument* document = getDocument();
    const PDFDocumentPointer pdf = document ? document->getDocument() : PDFDocumentPointer();
    std::vector<PDFAppToolRedactMark> marks = PDFAppToolRedactMark::readMarks(pdf.data(), size_t(PDFDocumentToolRedact::MAXIMUM_MARK_COUNT), &m_isMarkListTruncated);

    // The texts under the Text marks (kept by their quadrilaterals: a mark is never changed)
    std::map<PDFObjectReference, std::pair<QPolygonF, QString>> textCache;
    std::map<PDFInteger, PDFTextFlows> pageFlows;
    for (PDFAppToolRedactMark& mark : marks)
    {
        if (mark.kind != PDFAppToolRedactMark::Kind::Text)
        {
            continue;
        }

        const auto it = m_textCache.find(mark.annotation);
        if (it != m_textCache.end() && it->second.first == mark.quadrilaterals)
        {
            mark.description = it->second.second;
        }
        else
        {
            // The flows of a page are made once for all its new marks (Mark N Selected Matches)
            auto flowsIt = pageFlows.find(mark.pageIndex);
            if (flowsIt == pageFlows.end())
            {
                flowsIt = pageFlows.emplace(mark.pageIndex, getPageTextFlows(mark.pageIndex)).first;
            }
            mark.description = getMarkText(mark, flowsIt->second);
        }
        textCache[mark.annotation] = std::make_pair(mark.quadrilaterals, mark.description);
    }
    m_textCache = std::move(textCache);

    bool isSelectedFound = false;
    for (const PDFAppToolRedactMark& mark : marks)
    {
        isSelectedFound = isSelectedFound || mark.annotation == m_selectedMark;
    }
    if (!isSelectedFound)
    {
        m_selectedMark = PDFObjectReference();
    }

    if (m_markModel)
    {
        m_markModel->setMarks(std::move(marks));
        m_markModel->setSelectedMark(m_selectedMark);
    }

    // The filter of the engine finds a mark by its annotation (the engine asks at every move of the mouse)
    m_markIndices.clear();
    const std::vector<PDFAppToolRedactMark>& listedMarks = getMarks();
    for (size_t i = 0; i < listedMarks.size(); ++i)
    {
        m_markIndices[listedMarks[i].annotation] = i;
    }

    if (m_isPreview && !isPreviewAvailable())
    {
        m_isPreview = false;
    }
    m_isMarksRead = true;
}

const PDFAppToolRedactMark* PDFAppToolGUIRedact::findMark(PDFObjectReference annotation) const
{
    const auto it = m_markIndices.find(annotation);
    const std::vector<PDFAppToolRedactMark>& marks = getMarks();
    return it != m_markIndices.cend() && it->second < marks.size() ? &marks[it->second] : nullptr;
}

bool PDFAppToolGUIRedact::isAnnotationEditable(const PDFAnnotation* annotation) const
{
    // The marks of the list only (a Redact annotation beyond the listed ones is no mark of the tool)
    return annotation && annotation->getType() == AnnotationType::Redact && isCanvasEditingActive() && findMark(annotation->getSelfReference());
}

PDFAnnotationManipulator::Capabilities PDFAppToolGUIRedact::getAnnotationCapabilities(const PDFAnnotation* annotation) const
{
    // An Area mark of one rectangle is moved and resized (no rotation, no points: its /QuadPoints stay
    // one rectangle); a Text and a Whole page mark have no handles
    const PDFAppToolRedactMark* mark = annotation && isCanvasEditingActive() ? findMark(annotation->getSelfReference()) : nullptr;
    if (mark && mark->isMovable())
    {
        return PDFAnnotationManipulator::Capabilities(PDFAnnotationManipulator::Move | PDFAnnotationManipulator::Resize);
    }
    return PDFAnnotationManipulator::NoCapability;
}

QString PDFAppToolGUIRedact::getEditHint(const PDFAnnotation* annotation) const
{
    const PDFAppToolRedactMark* mark = annotation ? findMark(annotation->getSelfReference()) : nullptr;
    if (!mark)
    {
        return QString();
    }

    switch (mark->kind)
    {
        case PDFAppToolRedactMark::Kind::Text:
            return tr("A text mark covers its text: it cannot be moved, resized or copied. Press Delete to remove it.");
        case PDFAppToolRedactMark::Kind::Page:
            return tr("A whole page mark covers the page: it cannot be moved, resized or copied. Press Delete to remove it.");
        case PDFAppToolRedactMark::Kind::Area:
            break;
    }
    return mark->isMovable() ? QString() : tr("This mark has more areas: it cannot be moved, resized or copied. Press Delete to remove it.");
}

IAnnotationEditFilter::SelectionOutline PDFAppToolGUIRedact::getSelectionOutline(const PDFAnnotation* annotation) const
{
    // 1599:48898 (437:10685): the selected mark has the outline border/focus 2 px; the handles of an
    // Area mark lie on it
    SelectionOutline outline;
    const PDFAppToolRedactMark* mark = annotation ? findMark(annotation->getSelfReference()) : nullptr;
    if (mark)
    {
        outline.path = mark->getPath();
        outline.color = PDFThemeManager::getInstance()->getColor(PDFThemeColors::BorderFocus);
        outline.width = PDFAppToolGUIRedactHelper::SELECTED_OUTLINE_WIDTH;
    }
    return outline;
}

bool PDFAppToolGUIRedact::isAnnotationShown(const PDFAnnotation* annotation) const
{
    return !(m_isPreview && annotation && annotation->getType() == AnnotationType::Redact && findMark(annotation->getSelfReference()));
}

bool PDFAppToolGUIRedact::commitAnnotationEdit(const PDFModifiedDocument& modified)
{
    PDFAppDocument* document = getDocument();
    const PDFDocument* newDocument = modified.getDocument();
    if (!document || !newDocument || m_isRunning)
    {
        return false;
    }

    // The undo step names the change of the mark, which changed (review RSRFCODE L2: also a mark, which
    // is not selected - Delete of the mark under the cursor): removed, resized or moved. The selected
    // mark is asked first (the usual case), then the others; the engine changes one page.
    QString description = tr("Change a redaction mark");
    const PDFDocumentPointer oldDocument = document->getDocument();

    // Ctrl + drag of an Area mark adds its copy (owner 2026-11-18): the limit of the marks (7.2) holds
    const size_t maximum = size_t(PDFDocumentToolRedact::MAXIMUM_MARK_COUNT);
    const std::vector<PDFAppToolRedactMark> newMarks = PDFAppToolRedactMark::readMarks(newDocument, maximum + 1, nullptr);
    if (newMarks.size() > getMarks().size())
    {
        if (!canAddMarks(newMarks.size() - getMarks().size()))
        {
            return false;
        }
        return commitMarks(modified, document->getVersion(), tr("Copy a redaction mark"));
    }
    std::map<PDFInteger, std::set<PDFObjectReference>> pageAnnotations;
    std::vector<const PDFAppToolRedactMark*> candidates;
    if (const PDFAppToolRedactMark* selectedMark = findMark(m_selectedMark))
    {
        candidates.push_back(selectedMark);
    }
    for (const PDFAppToolRedactMark& mark : getMarks())
    {
        if (mark.annotation != m_selectedMark)
        {
            candidates.push_back(&mark);
        }
    }
    using MarkChange = PDFAppToolGUIRedactHelper::MarkChange;
    for (const PDFAppToolRedactMark* mark : candidates)
    {
        const MarkChange change = PDFAppToolGUIRedactHelper::getMarkChange(oldDocument.data(), newDocument, *mark, pageAnnotations);
        if (change != MarkChange::None)
        {
            description = change == MarkChange::Removed ? tr("Remove a redaction mark") : (change == MarkChange::Resized ? tr("Resize a redaction mark") : tr("Move a redaction mark"));
            break;
        }
    }

    // One commit of the tool (its Undo step in the order of the steps of the tool)
    return commitMarks(modified, document->getVersion(), description);
}

PDFTextFlows PDFAppToolGUIRedact::getPageTextFlows(PDFInteger pageIndex) const
{
    // The text layout of the page: the text index of the document, when it is built, otherwise the
    // layout of the view
    PDFTextLayout layout;
    const PDFAppDocument* document = getDocument();
    const std::shared_ptr<const PDFDocumentTextIndex> index = document ? document->getTextIndex() : nullptr;
    if (index && pageIndex < index->getPageCount())
    {
        layout = index->getTextLayout(pageIndex);
    }
    else
    {
        const PDFAppToolHost* host = getHost();
        PDFWidget* widget = host ? host->getPdfWidget() : nullptr;
        PDFDrawWidgetProxy* proxy = widget ? widget->getDrawWidgetProxy() : nullptr;
        if (proxy && proxy->getTextLayoutCompiler())
        {
            layout = proxy->getTextLayoutCompiler()->getTextLayoutLazy(pageIndex);
        }
    }
    return PDFTextFlow::createTextFlows(layout, PDFTextFlow::FlowFlags(PDFTextFlow::SeparateBlocks) | PDFTextFlow::RemoveSoftHyphen, pageIndex);
}

QString PDFAppToolGUIRedact::getMarkText(const PDFAppToolRedactMark& mark, const PDFTextFlows& flows)
{
    // The characters, whose middle lies in the area, in the order of the text flows (the bounding
    // rectangle first: most characters of the page lie outside of a mark)
    const QPainterPath path = mark.getPath();
    const QRectF bounds = path.boundingRect();
    QString text;
    for (const PDFTextFlow& flow : flows)
    {
        const QString flowText = flow.getText();
        const std::vector<QRectF> boxes = flow.getBoundingBoxes();
        bool isSpacePending = false;
        for (qsizetype i = 0; i < flowText.size() && size_t(i) < boxes.size(); ++i)
        {
            const QRectF& box = boxes[size_t(i)];
            if (!box.isValid() || flowText[i].isSpace())
            {
                isSpacePending = !text.isEmpty();
                continue;
            }
            const QPointF center = box.center();
            if (!bounds.contains(center) || !path.contains(center))
            {
                continue;
            }
            if (isSpacePending)
            {
                text += QLatin1Char(' ');
            }
            isSpacePending = false;
            text += flowText[i];
            if (text.size() > PDFAppToolGUIRedactHelper::MAXIMUM_DESCRIPTION_LENGTH)
            {
                return text.left(PDFAppToolGUIRedactHelper::MAXIMUM_DESCRIPTION_LENGTH - 1) + QChar(0x2026);
            }
        }
        isSpacePending = !text.isEmpty();
    }
    return text;
}

bool PDFAppToolGUIRedact::hasPageText(PDFInteger pageIndex) const
{
    const PDFAppToolHost* host = getHost();
    PDFWidget* widget = host ? host->getPdfWidget() : nullptr;
    PDFDrawWidgetProxy* proxy = widget ? widget->getDrawWidgetProxy() : nullptr;
    if (!proxy || !proxy->getTextLayoutCompiler())
    {
        return false;
    }
    const PDFTextLayout layout = proxy->getTextLayoutCompiler()->getTextLayoutLazy(pageIndex);
    return !layout.getTextBlocks().empty();
}

bool PDFAppToolGUIRedact::canAddMarks(size_t count)
{
    // 7.2: at most 10,000 marks of a document
    const size_t current = getMarks().size();
    const size_t maximum = size_t(PDFDocumentToolRedact::MAXIMUM_MARK_COUNT);
    if (!m_isMarkListTruncated && current + count <= maximum)
    {
        return true;
    }

    if (PDFAppToolHost* host = getHost())
    {
        const int refusedCount = int(std::min(count, size_t(std::numeric_limits<int>::max())));
        host->showTransientMessage(tr("A document can have at most %1 marks – %Ln more cannot be added.", nullptr, refusedCount)
                                   .arg(PDFAppLocale::formatNumber(PDFDocumentToolRedact::MAXIMUM_MARK_COUNT)));
    }
    return false;
}

bool PDFAppToolGUIRedact::commitMarks(const std::optional<PDFModifiedDocument>& modified, quint64 version, const QString& description)
{
    PDFAppDocument* document = getDocument();
    if (!modified || !document)
    {
        return false;
    }

    // The change of the tool (the document refuses the changes of the user while the tool is open):
    // one Undo step of the document, the tab becomes unsaved; Undo of the tool takes it back in its
    // order with the parameters (hasDocumentUndoSteps)
    return commitDocumentChange(*modified, description, version);
}

bool PDFAppToolGUIRedact::addAreaMark(PDFInteger pageIndex, const QRectF& rectangle)
{
    PDFAppDocument* document = getDocument();
    const PDFDocumentPointer pdf = document ? document->getDocument() : PDFDocumentPointer();
    if (!pdf || m_isRunning)
    {
        return false;
    }

    const QRectF area = rectangle.normalized().intersected(PDFAppToolGUIRedactHelper::getCropBox(pdf.data(), pageIndex));
    if (area.width() < PDFAppToolRedactOverlay::MINIMUM_AREA_SIZE || area.height() < PDFAppToolRedactOverlay::MINIMUM_AREA_SIZE || !canAddMarks(1))
    {
        return false;
    }

    PDFAppToolGUIRedactHelper::Request request;
    request.pageIndex = pageIndex;
    request.rectangle = area;
    request.subject = PDFAppToolRedactMark::SUBJECT_AREA;
    const quint64 version = document->getVersion();
    return commitMarks(PDFAppToolGUIRedactHelper::createMarks(pdf, { request }), version, tr("Mark an area for redaction"));
}

bool PDFAppToolGUIRedact::addTextMark(PDFInteger pageIndex, const QPolygonF& quadrilaterals)
{
    PDFAppDocument* document = getDocument();
    const PDFDocumentPointer pdf = document ? document->getDocument() : PDFDocumentPointer();
    if (!pdf || m_isRunning || quadrilaterals.size() < 4 || quadrilaterals.size() > 4 * PDFDocumentToolRedact::MAXIMUM_QUADRILATERAL_COUNT || !canAddMarks(1))
    {
        return false;
    }

    PDFAppToolGUIRedactHelper::Request request;
    request.pageIndex = pageIndex;
    request.rectangle = quadrilaterals.boundingRect();
    request.quadrilaterals = quadrilaterals;
    request.subject = PDFAppToolRedactMark::SUBJECT_TEXT;
    const quint64 version = document->getVersion();
    QPointer<PDFAppToolGUIRedact> guard(this);
    const bool isCommitted = commitMarks(PDFAppToolGUIRedactHelper::createMarks(pdf, { request }), version, tr("Mark text for redaction"));
    if (guard && isCommitted && m_noTextPage >= 0)
    {
        m_noTextPage = -1;
        updateLeftPanel();
    }
    return isCommitted;
}

bool PDFAppToolGUIRedact::markPages(const std::vector<PDFInteger>& pageIndices)
{
    PDFAppDocument* document = getDocument();
    const PDFDocumentPointer pdf = document ? document->getDocument() : PDFDocumentPointer();
    if (!pdf || m_isRunning)
    {
        return false;
    }

    // Whole pages (R6-30: the core makes a blank page of a page, whose crop box is marked); a page,
    // which has its mark of the whole page, is not marked twice
    std::set<PDFInteger> markedPages;
    for (const PDFAppToolRedactMark& mark : getMarks())
    {
        if (mark.kind == PDFAppToolRedactMark::Kind::Page)
        {
            markedPages.insert(mark.pageIndex);
        }
    }

    std::vector<PDFAppToolGUIRedactHelper::Request> requests;
    for (const PDFInteger pageIndex : std::set<PDFInteger>(pageIndices.cbegin(), pageIndices.cend()))
    {
        const QRectF cropBox = PDFAppToolGUIRedactHelper::getCropBox(pdf.data(), pageIndex);
        if (!cropBox.isValid() || markedPages.count(pageIndex))
        {
            continue;
        }

        PDFAppToolGUIRedactHelper::Request request;
        request.pageIndex = pageIndex;
        request.rectangle = cropBox;
        request.subject = PDFAppToolRedactMark::SUBJECT_PAGE;
        requests.push_back(request);
    }
    if (requests.empty() || !canAddMarks(requests.size()))
    {
        return false;
    }

    const quint64 version = document->getVersion();
    const QString description = PDFAppToolGUIRedactHelper::plural(int(requests.size()), QT_TR_NOOP("Mark %Ln page for redaction"), QT_TR_NOOP("Mark %Ln pages for redaction"));
    return commitMarks(PDFAppToolGUIRedactHelper::createMarks(pdf, requests), version, description);
}

bool PDFAppToolGUIRedact::markMatches(const std::vector<PDFAppToolRedactSearchPart::Match>& matches)
{
    PDFAppDocument* document = getDocument();
    const PDFDocumentPointer pdf = document ? document->getDocument() : PDFDocumentPointer();
    if (!pdf || m_isRunning)
    {
        return false;
    }

    std::vector<PDFAppToolGUIRedactHelper::Request> requests;
    for (const PDFAppToolRedactSearchPart::Match& match : matches)
    {
        if (match.quadrilaterals.size() < 4 || match.quadrilaterals.size() > 4 * PDFDocumentToolRedact::MAXIMUM_QUADRILATERAL_COUNT)
        {
            continue;
        }

        PDFAppToolGUIRedactHelper::Request request;
        request.pageIndex = match.pageIndex;
        request.rectangle = match.quadrilaterals.boundingRect();
        request.quadrilaterals = match.quadrilaterals;
        request.subject = PDFAppToolRedactMark::SUBJECT_TEXT;
        requests.push_back(request);
    }
    if (requests.empty() || !canAddMarks(requests.size()))
    {
        return false;
    }

    const quint64 version = document->getVersion();
    const int count = int(requests.size());
    const QString description = PDFAppToolGUIRedactHelper::plural(count, QT_TR_NOOP("Mark %Ln match for redaction"), QT_TR_NOOP("Mark %Ln matches for redaction"));
    QPointer<PDFAppToolGUIRedact> guard(this);
    const bool isCommitted = commitMarks(PDFAppToolGUIRedactHelper::createMarks(pdf, requests), version, description);
    if (guard && isCommitted && m_searchPart)
    {
        // 437:12452 "4 Matches Marked", the status counts the areas (the last call)
        m_searchPart->setMarkedCount(count);
        updateStatusText();
    }
    return isCommitted;
}

bool PDFAppToolGUIRedact::removeMark(PDFObjectReference annotation)
{
    PDFAppDocument* document = getDocument();
    const PDFDocumentPointer pdf = document ? document->getDocument() : PDFDocumentPointer();
    if (!pdf || m_isRunning || !m_markModel || m_markModel->getRow(annotation) < 0)
    {
        return false;
    }

    // A locked mark is no change the user may make (the removal would skip it silently)
    if (!PDFAppAnnotationOperations::canModifyAnnotation(pdf, annotation))
    {
        showLockedMarks(1);
        return false;
    }

    const quint64 version = document->getVersion();
    return commitMarks(PDFAppAnnotationOperations::deleteAnnotations(pdf, { annotation }), version, tr("Remove a redaction mark"));
}

bool PDFAppToolGUIRedact::removeAllMarks()
{
    PDFAppDocument* document = getDocument();
    const PDFDocumentPointer pdf = document ? document->getDocument() : PDFDocumentPointer();
    if (!pdf || m_isRunning || getMarks().empty())
    {
        return false;
    }

    std::vector<PDFObjectReference> annotations;
    annotations.reserve(getMarks().size());
    int lockedCount = 0;
    for (const PDFAppToolRedactMark& mark : getMarks())
    {
        if (PDFAppAnnotationOperations::canModifyAnnotation(pdf, mark.annotation))
        {
            annotations.push_back(mark.annotation);
        }
        else
        {
            ++lockedCount;
        }
    }

    // One Undo step of the document (plan 4.5: Remove Marks); the locked marks stay and are named
    QPointer<PDFAppToolGUIRedact> guard(this);
    const quint64 version = document->getVersion();
    const bool isRemoved = !annotations.empty() && commitMarks(PDFAppAnnotationOperations::deleteAnnotations(pdf, annotations), version, tr("Remove all redaction marks"));
    if (guard && lockedCount > 0)
    {
        showLockedMarks(lockedCount);
    }
    return isRemoved;
}

void PDFAppToolGUIRedact::showLockedMarks(int count)
{
    if (PDFAppToolHost* host = getHost())
    {
        host->showTransientMessage(PDFAppToolGUIRedactHelper::plural(count, QT_TR_NOOP("%Ln mark is locked and stays in the document."),
                                                                     QT_TR_NOOP("%Ln marks are locked and stay in the document.")));
    }
}

void PDFAppToolGUIRedact::selectMark(PDFObjectReference annotation)
{
    const PDFAppToolRedactMark* mark = nullptr;
    for (const PDFAppToolRedactMark& item : getMarks())
    {
        if (item.annotation == annotation)
        {
            mark = &item;
            break;
        }
    }

    m_selectedMark = mark ? annotation : PDFObjectReference();
    const PDFInteger pageIndex = mark ? mark->pageIndex : -1;
    if (m_markModel)
    {
        m_markModel->setSelectedMark(m_selectedMark);
    }
    if (m_marksView && m_markModel)
    {
        const int row = m_markModel->getRow(m_selectedMark);
        if (row >= 0 && m_marksView->currentIndex().row() != row)
        {
            const QSignalBlocker blocker(m_marksView->selectionModel());
            m_marksView->setCurrentIndex(m_markModel->index(row));
            m_marksView->viewport()->update();
        }
        else if (row < 0 && m_marksView->selectionModel())
        {
            // No mark (Esc, owner 2026-11-18): no row of the list is selected either
            const QSignalBlocker blocker(m_marksView->selectionModel());
            m_marksView->selectionModel()->clear();
            m_marksView->viewport()->update();
        }
    }
    updateOverlay();

    // The mark is selected on the canvas too (its handles; the echo of the engine is queued)
    QPointer<PDFAppToolGUIRedact> guard(this);
    updateCanvasSelection();
    if (!guard)
    {
        return;
    }

    // 437:10393: the row shows its mark on its page
    if (pageIndex >= 0)
    {
        const PDFAppToolHost* host = getHost();
        if (PDFAppDocumentView* view = host ? host->getView() : nullptr)
        {
            view->goToPage(pageIndex);
        }
        if (guard)
        {
            updateDisplayedPage();
        }
    }

    // 1599:48898 "Area mark selected · …" (the last call)
    if (guard)
    {
        updateStatusText();
    }
}

// ------------------------------------------------------------------ mode, fill, preview

void PDFAppToolGUIRedact::setMode(Mode mode)
{
    if (m_isRunning && mode != Mode::None)
    {
        return;
    }

    m_mode = mode;
    if (mode != Mode::Text)
    {
        m_noTextPage = -1;
    }
    if (mode == Mode::Area || mode == Mode::Text)
    {
        // The input of a mode works on the marks, not on the preview
        m_isPreview = false;
    }
    updateParts();

    if (mode == Mode::Search && m_searchPart && m_searchPart->getField())
    {
        m_searchPart->getField()->setFocus();
    }
    updateStatusText();
}

PDFAppToolGUIRedact::Fill PDFAppToolGUIRedact::getFill() const
{
    const PDFAppToolParameterModel* model = getModel();
    if (!model)
    {
        return Fill::Black;
    }
    if (model->getValue(QString::fromLatin1(PDFDocumentToolRedact::PARAMETER_NO_FILL)).toBool())
    {
        return Fill::None;
    }

    const QRgb color = getFillColor().rgb() & RGB_MASK;
    if (color == (QColor(Qt::black).rgb() & RGB_MASK))
    {
        return Fill::Black;
    }
    if (color == (QColor(Qt::white).rgb() & RGB_MASK))
    {
        return Fill::White;
    }
    return Fill::Custom;
}

QColor PDFAppToolGUIRedact::getFillColor() const
{
    const PDFAppToolParameterModel* model = getModel();
    const QColor color = model ? model->getValue(QString::fromLatin1(PDFDocumentToolRedact::PARAMETER_FILL_COLOR)).value<QColor>() : QColor();
    return color.isValid() ? color : QColor(Qt::black);
}

void PDFAppToolGUIRedact::setFill(Fill fill, const QColor& color)
{
    PDFAppToolParameterModel* model = getModel();
    if (!model)
    {
        return;
    }

    // One undo step of the parameters (the toolbar Undo)
    QVariantMap values;
    switch (fill)
    {
        case Fill::Black:
            values.insert(QString::fromLatin1(PDFDocumentToolRedact::PARAMETER_FILL_COLOR), QColor(Qt::black));
            values.insert(QString::fromLatin1(PDFDocumentToolRedact::PARAMETER_NO_FILL), false);
            break;
        case Fill::White:
            values.insert(QString::fromLatin1(PDFDocumentToolRedact::PARAMETER_FILL_COLOR), QColor(Qt::white));
            values.insert(QString::fromLatin1(PDFDocumentToolRedact::PARAMETER_NO_FILL), false);
            break;
        case Fill::Custom:
            if (!color.isValid())
            {
                return;
            }
            values.insert(QString::fromLatin1(PDFDocumentToolRedact::PARAMETER_FILL_COLOR), QColor::fromRgb(color.rgb()));
            values.insert(QString::fromLatin1(PDFDocumentToolRedact::PARAMETER_NO_FILL), false);
            break;
        case Fill::None:
            values.insert(QString::fromLatin1(PDFDocumentToolRedact::PARAMETER_NO_FILL), true);
            break;
    }

    const QString error = model->setValues(values);
    if (!error.isEmpty())
    {
        if (PDFAppToolHost* host = getHost())
        {
            host->showTransientMessage(error);
        }
    }
}

bool PDFAppToolGUIRedact::isPreviewAvailable() const
{
    // 437:10726, 437:13550 (corrected by P2S6FIGMA): Disabled without marks and while running
    return !getMarks().empty() && !m_isRunning;
}

void PDFAppToolGUIRedact::setPreview(bool isPreview)
{
    m_isPreview = isPreview && isPreviewAvailable();
    if (m_isPreview && (m_mode == Mode::Area || m_mode == Mode::Text))
    {
        m_mode = Mode::None;
    }
    updateParts();
    updateStatusText();
}

// ------------------------------------------------------------------ toolbar and panels

void PDFAppToolGUIRedact::describeToolbar(PDFAppToolPanelBuilder& builder)
{
    using H = PDFAppToolGUIRedactHelper;

    if (!m_isMarksRead)
    {
        refreshMarks();
    }

    // tool-actions 437:10413: Area, Text, Pages…, Search… | Fill, Keep | Clear All
    m_areaButton = builder.addAction(tr("Area"), QStringLiteral("redact-area"), this, &PDFAppToolGUIRedact::onAreaClicked);
    m_areaButton->setCheckable(true);
    m_textButton = builder.addAction(tr("Text"), QStringLiteral("redact-text"), this, &PDFAppToolGUIRedact::onTextClicked);
    m_textButton->setCheckable(true);
    m_pagesButton = builder.addAction(tr("Pages…"), QStringLiteral("redact-page"), this, &PDFAppToolGUIRedact::onPagesClicked);
    m_searchButton = builder.addAction(tr("Search…"), QStringLiteral("redact-search"), this, &PDFAppToolGUIRedact::onSearchClicked);
    m_searchButton->setCheckable(true);
    builder.addActionDivider();

    // Fill 437:12794: Black, White, Custom color…, No fill and the group Colors (K6.5)
    QToolButton* fillButton = PDFAppDocumentToolbar::createToolbarMenuButton(QStringLiteral("toolMenu-fill"), QStringLiteral("droplet"), builder.getWidget());
    builder.addWidget(fillButton);
    const PDFAppToolHost* host = getHost();
    PDFAppToolRedactFillPanel* fillPanel = new PDFAppToolRedactFillPanel(host ? host->getView() : nullptr, FILL_PANEL_WIDTH, fillButton);
    fillPanel->setObjectName(QStringLiteral("toolPanel-fill"));
    if (PDFAppToolToolbar* toolbar = getToolbar())
    {
        toolbar->addPanel(fillButton, fillPanel);
    }
    m_fillPanel = fillPanel;
    m_fillRows.clear();
    fillPanel->addGroupCaption(tr("Fill of the removed areas"));
    const std::vector<std::tuple<Fill, QString, QString, QString>> fills = {
        { Fill::Black, tr("Black"), tr("The classic redaction bar"), QString() },
        { Fill::White, tr("White"), tr("Blends with white paper"), QString() },
        { Fill::Custom, tr("Custom color…"), tr("Pick any color"), QStringLiteral("plus") },
        { Fill::None, tr("No fill"), tr("Content is removed, the area stays empty"), QStringLiteral("ban") },
    };
    for (const auto& [fill, title, description, iconName] : fills)
    {
        if (fill == Fill::None)
        {
            fillPanel->addGroupDivider();
        }
        PDFTileRow* row = fillPanel->addRow(title, description, iconName, PDFTileRow::Kind::MenuItem);
        row->setObjectName(QStringLiteral("redactFill-%1").arg(int(fill)));

        // 437:12794: Black and White are swatches of their color, Custom color… an empty swatch with "+"
        // (1567:19612), No fill keeps its icon tile
        switch (fill)
        {
            case Fill::Black:
                row->setSwatchTile(true, PDFThemeColors::AnnotationBlack);
                break;
            case Fill::White:
                row->setSwatchTile(true, PDFThemeColors::AnnotationWhite);
                break;
            case Fill::Custom:
                row->setSwatchTile(true, std::nullopt);
                break;
            case Fill::None:
                break;
        }
        row->setProperty(H::FILL_PROPERTY, int(fill));
        row->setSelectionBehavior(PDFComponentButton::SelectionBehavior::Exclusive);
        connect(row, &PDFTileRow::clicked, this, &PDFAppToolGUIRedact::onFillRowClicked);
        m_fillRows[fill] = row;
    }
    PDFColorSwatchRow* colorRow = fillPanel->addColorGroup(tr("Colors"));
    connect(colorRow, &PDFColorSwatchRow::colorSelected, this, &PDFAppToolGUIRedact::onFillColorSelected);
    PDFAppColorPicker::attach(colorRow);
    fillPanel->setColorGroupVisible(getFill() == Fill::Custom);

    // Keep 437:12794: the title, the metadata, the outline (R6-6: an unchecked title is removed also
    // with the metadata)
    std::vector<PDFAppToolFlagItem> keepItems = {
        { QString::fromLatin1(PDFDocumentToolRedact::PARAMETER_KEEP_TITLE), tr("Document title"), tr("Copied only when checked, also with the metadata"), QStringLiteral("type"), QString(), QString() },
        { QString::fromLatin1(PDFDocumentToolRedact::PARAMETER_KEEP_METADATA), tr("Metadata"), tr("Author, subject, keywords, dates"), QStringLiteral("metadata"), QString(), QString() },
        { QString::fromLatin1(PDFDocumentToolRedact::PARAMETER_KEEP_OUTLINE), tr("Outline"), tr("Bookmarks may reveal redacted headings"), QStringLiteral("bookmark"), QString(), QString() },
    };
    // tb-keep 437:10413: icon/keep-in-copy 1568:1625 (K6.16)
    m_keepMenu = builder.addFlagsMenu(std::move(keepItems), QStringLiteral("keep-in-copy"), QString(), KEEP_PANEL_WIDTH, tr("Copy into the redacted document"), PDFTileRow::Kind::MenuItem);
    if (QToolButton* keepButton = getKeepButton())
    {
        keepButton->setText(tr("Keep"));
        keepButton->setToolTip(tr("Keep"));
    }
    builder.addActionDivider();

    m_clearButton = builder.addAction(tr("Clear All"), QStringLiteral("eraser"), this, &PDFAppToolGUIRedact::onClearClicked);
    updateToolbar();
    updateFillParts();
}

void PDFAppToolGUIRedact::describeLeftPanel(PDFAppToolPanelBuilder& builder)
{
    if (!m_searchPart)
    {
        m_searchPart = new PDFAppToolRedactSearchPart(getContext(), this);
        connect(m_searchPart.data(), &PDFAppToolRedactSearchPart::markRequested, this, &PDFAppToolGUIRedact::onSearchMarkRequested);
        connect(m_searchPart.data(), &PDFAppToolRedactSearchPart::matchesChanged, this, &PDFAppToolGUIRedact::onSearchMatchesChanged);
        connect(m_searchPart.data(), &PDFAppToolRedactSearchPart::backRequested, this, &PDFAppToolGUIRedact::onSearchBackRequested);
    }
    const PDFAppToolHost* host = getHost();
    m_searchPart->setDocument(getDocument(), host ? host->getView() : nullptr);

    // The marks (437:10420): Marked areas, the card of the marks or the empty state (437:10726), the hint
    QWidget* marks = new QWidget(builder.getWidget());
    marks->setObjectName(QStringLiteral("redactMarksContent"));
    QVBoxLayout* layout = new QVBoxLayout(marks);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);
    m_leftHeader = new PDFAppToolPanelHeader(tr("Marked areas"), QString(), marks);
    m_leftHeader->setObjectName(QStringLiteral("redactMarksHeader"));
    layout->addWidget(m_leftHeader);

    // 2.6: a page without text in the mode Text
    m_noTextCallout = new PDFAppToolCallout(PDFAccentHue::Blue, QStringLiteral("info"), tr("This page has no text. Mark the area instead."), QString(), marks);
    m_noTextCallout->setObjectName(QStringLiteral("redactNoTextCallout"));
    m_noTextCallout->setCompactStyle();
    layout->addWidget(m_noTextCallout);

    // card-marks 437:10529: padding 6 + the border, the rows 2 apart
    PDFAppToolCard* card = new PDFAppToolCard(marks);
    card->setObjectName(QStringLiteral("redactMarksCard"));
    card->getBodyLayout()->setContentsMargins(LIST_PADDING, LIST_PADDING, LIST_PADDING, LIST_PADDING);
    PDFAppToolRedactMarkDelegate* delegate = new PDFAppToolRedactMarkDelegate(card);
    delegate->setKind(PDFTileRowDelegate::Kind::ListItem);
    delegate->setDescriptionKind(PDFTileRowDelegate::TextKind::Data);
    connect(delegate, &PDFAppToolRedactMarkDelegate::actionTriggered, this, &PDFAppToolGUIRedact::onMarkActionTriggered);
    PDFAppPanelListView* view = new PDFAppPanelListView(card);
    view->setObjectName(QStringLiteral("redactMarksView"));
    view->setFrameShape(QFrame::NoFrame);
    view->setItemDelegate(delegate);
    view->setModel(m_markModel);
    view->setSelectionMode(QAbstractItemView::SingleSelection);
    view->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    view->viewport()->setAutoFillBackground(false);
    view->setSpacing(1);
    view->setContentPaddings(QMargins(-1, -1, -1, -1));
    view->setAccessibleName(tr("Marked areas"));
    card->getBodyLayout()->addWidget(view);
    connect(view->selectionModel(), &QItemSelectionModel::currentChanged, this, &PDFAppToolGUIRedact::onMarkRowChanged);
    view->viewport()->installEventFilter(this);
    m_marksViewport = view->viewport();
    m_marksView = view;
    m_marksCard = card;
    layout->addWidget(card);

    // 437:10726: Mark what should disappear
    PDFAppToolCard* empty = new PDFAppToolCard(marks);
    empty->setObjectName(QStringLiteral("redactEmptyCard"));
    empty->getBodyLayout()->setContentsMargins(17, 17, 17, 17);
    empty->getBodyLayout()->setSpacing(8);
    PDFIconBadge* icon = new PDFIconBadge(QStringLiteral("redact-text"), 40, 18, empty);
    icon->setColors(PDFThemeColors::BgAccentSubtle, PDFThemeColors::IconAccent);
    icon->setRadius(8);
    empty->getBodyLayout()->addWidget(icon, 0, Qt::AlignHCenter);
    m_emptyTitle = PDFAppPageWidgets::createLabel(QString(), QFont::DemiBold, 13, PDFThemeColors::TextPrimary, empty);
    m_emptyTitle->setObjectName(QStringLiteral("redactEmptyTitle"));
    empty->getBodyLayout()->addWidget(m_emptyTitle, 0, Qt::AlignHCenter);
    m_emptyText = PDFAppPageWidgets::createText(QString(), PDFThemeFonts::getFont(QFont::Normal, 11), 13, PDFThemeColors::TextSecondary, empty);
    m_emptyText->setObjectName(QStringLiteral("redactEmptyText"));
    m_emptyText->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
    m_emptyText->setMinimumWidth(1);
    empty->getBodyLayout()->addWidget(m_emptyText);
    m_emptyCard = empty;
    layout->addWidget(empty);

    PDFAppParagraph* hint = PDFAppPageWidgets::createText(tr("Click a mark to show it on the page. Marks are saved with the document as redaction annotations."),
                                                          PDFThemeFonts::getFont(QFont::Normal, 11), 13, PDFThemeColors::TextMuted, marks);
    hint->setObjectName(QStringLiteral("redactMarksHint"));
    hint->setMinimumWidth(1);
    layout->addWidget(hint);
    builder.addWidget(marks);
    m_marksContent = marks;

    // The search (437:12113)
    QWidget* search = m_searchPart->createContent(builder.getWidget());
    builder.addWidget(search);
    m_searchContent = search;

    updateLeftPanel();
    updateListHeight();
}

void PDFAppToolGUIRedact::describeRightPanel(PDFAppToolPanelBuilder& builder)
{
    // sidebar-right 437:10427: Redacted copy, the amber callout, Result, the blue callout (K6.23)
    m_rightHeader = builder.addPanelHeader(tr("Redacted copy"), tr("What the new file will contain"));

    // Marks | Preview result (segments 870:20145): the first control of the panel, its full width (owner
    // 2026-11-17: moved from the bar of the canvas); Preview result Disabled without marks and while running
    PDFSegmentedControl* modeControl = new PDFSegmentedControl(builder.getWidget());
    modeControl->setObjectName(QStringLiteral("redactModeControl"));
    modeControl->addSegment(tr("Marks"));
    modeControl->addSegment(tr("Preview result"));
    modeControl->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    modeControl->setAccessibleName(tr("Show the marks or the preview of the result"));
    builder.addWidget(modeControl);
    connect(modeControl, &PDFSegmentedControl::currentIndexChanged, this, &PDFAppToolGUIRedact::onModeControlChanged);
    m_modeControl = modeControl;

    m_marksCallout =builder.addCallout(PDFAccentHue::Amber, QStringLiteral("alert-triangle"), QString(), tr("The content is still in this document. It is removed only in the redacted copy."));
    m_marksCallout->setObjectName(QStringLiteral("redactMarksCallout"));
    m_marksCallout->setCompactStyle();
    builder.addSection(tr("Result"));
    m_summaryRows = builder.addKeyValues();
    m_summaryRows->setObjectName(QStringLiteral("redactSummary"));
    m_summaryRows->setRowGap(PDFAppToolGUIRedactHelper::SUMMARY_ROW_GAP);
    builder.endSection();
    m_pagesOnlyCallout = builder.addCallout(PDFAccentHue::Blue, QStringLiteral("info"), tr("The copy contains pages only"),
                                            tr("Every page is rebuilt. Comments, form fields, links and attachments are not carried over, so nothing hidden survives. Text becomes shapes and cannot be searched."));
    m_pagesOnlyCallout->setObjectName(QStringLiteral("redactPagesOnlyCallout"));
    m_pagesOnlyCallout->setCompactStyle();
    updateRightPanel();
}

QWidget* PDFAppToolGUIRedact::createOverlay(QWidget* canvasArea)
{
    const PDFAppToolHost* host = getHost();
    PDFWidget* pdfWidget = host ? host->getPdfWidget() : nullptr;
    if (!pdfWidget || !canvasArea)
    {
        return nullptr;
    }

    PDFAppToolRedactOverlay* overlay = new PDFAppToolRedactOverlay(pdfWidget, canvasArea);
    connect(overlay, &PDFAppToolRedactOverlay::areaPicked, this, &PDFAppToolGUIRedact::onAreaPicked);
    connect(overlay, &PDFAppToolRedactOverlay::textPicked, this, &PDFAppToolGUIRedact::onTextPicked);
    connect(overlay, &PDFAppToolRedactOverlay::textNotFound, this, &PDFAppToolGUIRedact::onTextNotFound);
    // Queued: the overlay emits it in the dispatch of the mouse input
    connect(overlay, &PDFAppToolRedactOverlay::dragStateChanged, this, &PDFAppToolGUIRedact::onOverlayDragStateChanged, Qt::QueuedConnection);
    m_overlay = overlay;
    PDFAppDocumentView* view = host->getView();
    if (view && !m_isViewConnected)
    {
        connect(view, &PDFAppDocumentView::displayedPageChanged, this, &PDFAppToolGUIRedact::onDisplayedPageChanged);
        m_isViewConnected = true;
    }

    // The marks are edited on the canvas by the engine of the annotations: this GUI is the tool editing
    // of the controller of the view (it removes itself in its destructor)
    PDFAppAnnotationController* controller = view ? view->getAnnotationController() : nullptr;
    if (controller && controller != m_annotationController.data())
    {
        // Review RSRFCODE L3: a previous controller does not ask this GUI any more (its slots first,
        // setToolEditing may emit its selection)
        if (PDFAppAnnotationController* previous = m_annotationController.data())
        {
            previous->disconnect(this);
            if (previous->getToolEditing() == this)
            {
                QPointer<PDFAppToolGUIRedact> guard(this);
                previous->setToolEditing(nullptr);
                if (!guard)
                {
                    // The overlay is a child of the canvas area (the shell deletes it)
                    return overlay;
                }
            }
        }
        m_annotationController = controller;
        connect(controller, &PDFAppAnnotationController::selectionChanged, this, &PDFAppToolGUIRedact::onCanvasSelectionChanged);
    }
    updateDisplayedPage();
    updateOverlay();

    // The engine asks the filter (this GUI) and drops a selection of another annotation: the last call
    // (its selection signal of the controller is queued)
    if (controller)
    {
        controller->setToolEditing(this);
        updateCanvasSelection();
    }
    return overlay;
}

void PDFAppToolGUIRedact::updateCanvasSelection()
{
    PDFAppAnnotationController* controller = m_annotationController.data();
    if (!controller || controller->getToolEditing() != this)
    {
        return;
    }

    // Nothing is edited in Preview result and while running: the engine selects nothing; otherwise it
    // selects the selected mark (the row of the list)
    const std::vector<PDFObjectReference> selection = isCanvasEditingActive() && m_selectedMark.isValid() ? std::vector<PDFObjectReference>{ m_selectedMark } : std::vector<PDFObjectReference>();
    if (controller->getSelectedAnnotations() != selection)
    {
        controller->selectAnnotations(selection);
    }
}

void PDFAppToolGUIRedact::onCanvasSelectionChanged()
{
    // A click on a mark of the canvas selects its row; a click beside the marks deselects it. In Preview
    // result and while running the engine selects nothing, the row stays.
    const PDFAppAnnotationController* controller = m_annotationController.data();
    if (!controller || controller->getToolEditing() != this || !isCanvasEditingActive())
    {
        return;
    }

    const std::vector<PDFObjectReference> selection = controller->getSelectedAnnotations();
    const PDFObjectReference annotation = selection.size() == 1 && findMark(selection.front()) ? selection.front() : PDFObjectReference();
    if (annotation == m_selectedMark)
    {
        return;
    }

    m_selectedMark = annotation;
    if (m_markModel)
    {
        m_markModel->setSelectedMark(m_selectedMark);
    }
    if (m_marksView && m_markModel)
    {
        const QSignalBlocker blocker(m_marksView->selectionModel());
        const int row = m_markModel->getRow(m_selectedMark);
        if (row >= 0)
        {
            m_marksView->setCurrentIndex(m_markModel->index(row));
            m_marksView->scrollTo(m_markModel->index(row));
        }
        else
        {
            m_marksView->selectionModel()->clear();
        }
        m_marksView->viewport()->update();
    }
    updateOverlay();

    // 1599:48898 "Area mark selected · …" (the last call)
    updateStatusText();
}

void PDFAppToolGUIRedact::updateParts()
{
    updateToolbar();
    updateFillParts();
    updateLeftPanel();
    updateRightPanel();
    updateOverlay();
    updateListHeight();

    // The engine selects the mark (none in Preview result and while running): the last call
    updateCanvasSelection();
}

void PDFAppToolGUIRedact::updateToolbar()
{
    const PDFAppDocument* document = getDocument();
    const bool hasPages = document && document->getPageCount() > 0;
    const std::vector<std::pair<QToolButton*, Mode>> modeButtons = { { m_areaButton.data(), Mode::Area }, { m_textButton.data(), Mode::Text }, { m_searchButton.data(), Mode::Search } };
    for (const auto& [button, mode] : modeButtons)
    {
        if (button)
        {
            const QSignalBlocker blocker(button);
            button->setChecked(m_mode == mode);
            button->setEnabled(hasPages);
        }
    }
    if (m_pagesButton)
    {
        m_pagesButton->setEnabled(hasPages);
    }
    if (m_clearButton)
    {
        // 437:10726: Disabled without marks
        m_clearButton->setEnabled(!getMarks().empty());
    }
}

void PDFAppToolGUIRedact::updateFillParts()
{
    const Fill fill = getFill();
    if (QToolButton* button = getFillButton())
    {
        // 437:10508 "Fill: Black"
        QString title;
        switch (fill)
        {
            case Fill::Black:
                title = tr("Black");
                break;
            case Fill::White:
                title = tr("White");
                break;
            case Fill::Custom:
                title = tr("Custom");
                break;
            case Fill::None:
                title = tr("No fill");
                break;
        }
        button->setText(tr("Fill: %1").arg(title));
        button->setToolTip(button->text());
    }
    for (const auto& [rowFill, row] : m_fillRows)
    {
        if (row)
        {
            const QSignalBlocker blocker(row);
            row->setChecked(rowFill == fill);
        }
    }
    if (m_fillPanel && m_fillPanel->getColorRow())
    {
        m_fillPanel->getColorRow()->setColor(fill == Fill::Custom ? getFillColor() : QColor());
        if (fill == Fill::Custom)
        {
            m_fillPanel->setColorGroupVisible(true);
        }
    }
}

void PDFAppToolGUIRedact::updateLeftPanel()
{
    using H = PDFAppToolGUIRedactHelper;

    const bool isSearch = m_mode == Mode::Search;
    if (m_marksContent)
    {
        m_marksContent->setVisible(!isSearch);
    }
    if (m_searchContent)
    {
        m_searchContent->setVisible(isSearch);
    }

    const PDFAppDocument* document = getDocument();
    const bool hasPages = document && document->getPageCount() > 0;
    const int count = int(getMarks().size());
    if (m_leftHeader)
    {
        // 437:10528 "12 areas on 5 pages", 437:10726 "Nothing marked yet"
        QString subtitle;
        if (!hasPages)
        {
            subtitle = tr("The document has no pages");
        }
        else if (count == 0)
        {
            subtitle = tr("Nothing marked yet");
        }
        else
        {
            subtitle = tr("%1 on %2").arg(H::plural(count, QT_TR_NOOP("%Ln area"), QT_TR_NOOP("%Ln areas")), H::plural(getMarkPageCount(), QT_TR_NOOP("%Ln page"), QT_TR_NOOP("%Ln pages")));
            if (m_isMarkListTruncated)
            {
                subtitle = tr("%1 · the first %Ln marks are listed", nullptr, PDFDocumentToolRedact::MAXIMUM_MARK_COUNT).arg(subtitle);
            }
        }
        m_leftHeader->setTexts(tr("Marked areas"), subtitle);
    }
    if (m_marksCard)
    {
        m_marksCard->setVisible(count > 0);
    }
    if (m_emptyCard)
    {
        m_emptyCard->setVisible(count == 0);
    }
    if (m_emptyTitle)
    {
        m_emptyTitle->setText(hasPages ? tr("Mark what should disappear") : tr("Nothing to mark"));
    }
    if (m_emptyText)
    {
        m_emptyText->setText(hasPages ? tr("Drag an area, drag over text, mark whole pages, or search for text and mark every match.")
                                      : tr("The document has no pages, so there is nothing to redact."));
    }
    if (m_noTextCallout)
    {
        m_noTextCallout->setVisible(m_noTextPage >= 0);
    }
}

void PDFAppToolGUIRedact::updateRightPanel()
{
    using H = PDFAppToolGUIRedactHelper;

    const int count = int(getMarks().size());
    if (PDFSegmentedControl* modeControl = m_modeControl.data())
    {
        // The state of the GUI is shown, not chosen (no signal); Preview result Disabled without marks and
        // while running (437:10726, 437:13550)
        const QSignalBlocker blocker(modeControl);
        modeControl->setCurrentIndex(m_isPreview ? 1 : 0);
        const QList<QToolButton*> segments = modeControl->findChildren<QToolButton*>(QString(), Qt::FindDirectChildrenOnly);
        if (segments.size() > 1)
        {
            segments[1]->setEnabled(isPreviewAvailable());
        }
    }
    if (m_marksCallout)
    {
        // 437:10692 "12 areas marked – not removed yet"; none in 437:10726
        m_marksCallout->setTexts(H::plural(count, QT_TR_NOOP("%Ln area marked – not removed yet"), QT_TR_NOOP("%Ln areas marked – not removed yet")),
                                 tr("The content is still in this document. It is removed only in the redacted copy."));
        m_marksCallout->setVisible(count > 0);
    }

    // Result 437:10701: Removed, Fill, Title, Metadata, Outline, Saved as
    const PDFAppToolParameterModel* model = getModel();
    QString fill;
    switch (getFill())
    {
        case Fill::Black:
            fill = tr("Black");
            break;
        case Fill::White:
            fill = tr("White");
            break;
        case Fill::Custom:
            // A dot of the color as the swatches of the color pickers, no hex code (owner 2026-11-17)
            fill = tr("Custom");
            break;
        case Fill::None:
            fill = tr("No fill");
            break;
    }
    const QString kept = tr("Kept");
    const QString notCopied = tr("Not copied");
    H::setRows(m_summaryRows, {
        { tr("Removed"), count > 0 ? tr("%1 · %2").arg(H::plural(count, QT_TR_NOOP("%Ln area"), QT_TR_NOOP("%Ln areas")), H::plural(getMarkPageCount(), QT_TR_NOOP("%Ln page"), QT_TR_NOOP("%Ln pages"))) : tr("Nothing yet") },
        { tr("Fill"), fill },
        { tr("Title"), H::isChecked(model, PDFDocumentToolRedact::PARAMETER_KEEP_TITLE) ? kept : notCopied },
        { tr("Metadata"), H::isChecked(model, PDFDocumentToolRedact::PARAMETER_KEEP_METADATA) ? kept : notCopied },
        { tr("Outline"), H::isChecked(model, PDFDocumentToolRedact::PARAMETER_KEEP_OUTLINE) ? kept : notCopied },
        { tr("Saved as"), tr("New file") },
    });

    // The row Fill (437:10701 as corrected 2026-11-17): the dot of the color of the fill before its text
    // ("Black", "Custom" - no hex code), none for No fill; a screen reader reads a custom color by its
    // hex code
    if (m_summaryRows)
    {
        const Fill currentFill = getFill();
        const QColor color = getFillColor();
        m_summaryRows->setValueColor(H::SUMMARY_FILL_ROW, currentFill != Fill::None ? color : QColor(),
                                     currentFill == Fill::Custom ? tr("Custom color %1").arg(color.name(QColor::HexRgb).toUpper()) : QString());
    }
}

void PDFAppToolGUIRedact::updateOverlay()
{
    if (!m_overlay)
    {
        return;
    }

    // No fill: the area stays empty - the white paper (plan 4.5)
    const Fill fill = getFill();
    m_overlay->setInput(m_mode == Mode::Area ? PDFAppToolRedactOverlay::Input::Area : (m_mode == Mode::Text ? PDFAppToolRedactOverlay::Input::Text : PDFAppToolRedactOverlay::Input::None));
    // One look of the selected mark (1599:48898): the engine of the annotations draws its outline with
    // the handles (getSelectionOutline); the overlay frames it only, when the engine shows no selection
    // (while running)
    m_overlay->setMarks(getMarks(), isCanvasEditingActive() ? PDFObjectReference() : m_selectedMark);
    m_overlay->setMatches(m_mode == Mode::Search && m_searchPart ? m_searchPart->getMatches() : std::vector<PDFAppToolRedactSearchPart::Match>());
    m_overlay->setPreview(m_isPreview, fill == Fill::None ? QColor(Qt::white) : getFillColor());
}

void PDFAppToolGUIRedact::updateListHeight()
{
    if (!m_marksView || !m_markModel)
    {
        return;
    }

    // The card hugs its rows up to its maximum; the rows of a long list scroll
    const int maximumHeight = LIST_MAXIMUM_HEIGHT - 2 * LIST_PADDING;
    int height = 0;
    for (int row = 0, count = m_markModel->rowCount(); row < count && height < maximumHeight; ++row)
    {
        height += m_marksView->sizeHintForRow(row) + 2;
    }
    m_marksView->setFixedHeight(qBound(1, height, maximumHeight));
}

void PDFAppToolGUIRedact::updateDisplayedPage()
{
    const PDFAppToolHost* host = getHost();
    const PDFAppDocumentView* view = host ? host->getView() : nullptr;
    if (m_overlay && view)
    {
        const PDFInteger page = view->getDisplayedPageIndex();
        m_overlay->setPageText(page >= 0 ? tr("Page %1").arg(PDFAppLocale::formatNumber(page + 1)) : QString());
    }
}

bool PDFAppToolGUIRedact::eventFilter(QObject* watched, QEvent* event)
{
    // The address of the viewport only: the view may be in its destructor (no call through it)
    if ((event->type() == QEvent::Resize || event->type() == QEvent::Show) && watched && watched == m_marksViewport.data() && m_marksView)
    {
        updateListHeight();
    }
    return BaseClass::eventFilter(watched, event);
}

// ------------------------------------------------------------------ dialogs

PDFMessageBox* PDFAppToolGUIRedact::openPagesDialog()
{
    if (m_pagesDialog)
    {
        m_pagesDialog->raise();
        return m_pagesDialog.data();
    }

    PDFAppDocument* document = getDocument();
    const PDFDocumentPointer pdf = document ? document->getDocument() : PDFDocumentPointer();
    if (!pdf || m_isRunning || document->getPageCount() <= 0)
    {
        return nullptr;
    }

    // 437:11653: Redact whole pages, the Page selection, Cancel | Mark N Pages
    QWidget* parent = PDFAppToolGUIRedactHelper::getDialogParent(this);
    PDFMessageBox* dialog = new PDFMessageBox(PDFThemeIcons::Badge::Information, tr("Redact whole pages"),
                                              tr("Everything on the selected pages is removed. The fill color from the toolbar is used."), QDialogButtonBox::NoButton, parent);
    dialog->setObjectName(QStringLiteral("redactPagesDialog"));
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setCardWidth(420);

    PDFPageSelection* selection = new PDFPageSelection(dialog);
    selection->setObjectName(QStringLiteral("redactPagesSelection"));
    selection->setLabel(tr("Pages to redact"));
    const PDFCatalog* catalog = pdf->getCatalog();
    const int pageCount = int(qMin<PDFInteger>(PDFInteger(catalog->getPageCount()), PDFPageRange::MAX_PAGE_COUNT));
    selection->setPageCount(pageCount);
    QList<QSizeF> sizes;
    sizes.reserve(pageCount);
    for (int i = 0; i < pageCount; ++i)
    {
        sizes << catalog->getPage(size_t(i))->getRotatedMediaBox().size();
    }
    selection->setPageSizes(sizes);
    const PDFAppToolHost* host = getHost();
    if (const PDFAppDocumentView* view = host ? host->getView() : nullptr)
    {
        selection->setCurrentPage(int(view->getDisplayedPageIndex()) + 1);
        QList<int> selectedPages;
        for (const PDFInteger page : view->getSelectedPages())
        {
            if (page >= 0 && page < pageCount)
            {
                selectedPages << int(page + 1);
            }
        }
        selection->setSelectedPages(selectedPages);
    }
    selection->setMode(PDFPageSelection::Mode::Current);
    dialog->addContent(selection);
    m_pagesSelection = selection;
    connect(selection, &PDFPageSelection::resultChanged, this, &PDFAppToolGUIRedact::onPagesResultChanged);

    QPushButton* cancel = dialog->addActionButton(tr("Cancel"), PDFMessageBox::ActionPosition::Trailing, false);
    cancel->setObjectName(QStringLiteral("redactPagesCancel"));
    connect(cancel, &QPushButton::clicked, dialog, &QDialog::reject);
    m_pagesMarkButton = dialog->addActionButton(QString(), PDFMessageBox::ActionPosition::Trailing, true);
    m_pagesMarkButton->setObjectName(QStringLiteral("redactPagesMark"));
    m_pagesMarkButton->setIcon(PDFThemeIcons::getIcon(QStringLiteral("redact-page")));
    connect(m_pagesMarkButton.data(), &QPushButton::clicked, this, &PDFAppToolGUIRedact::onPagesMarkClicked);
    m_pagesDialog = dialog;
    onPagesResultChanged();

    PDFAppMainWindow::prepareDialog(dialog, parent);
    QPointer<PDFAppToolGUIRedact> guard(this);
    dialog->open();
    return guard ? m_pagesDialog.data() : nullptr;
}

void PDFAppToolGUIRedact::onPagesResultChanged()
{
    if (!m_pagesMarkButton || !m_pagesSelection)
    {
        return;
    }

    // 437:11653 "Mark 3 Pages"
    const int count = int(qMin<qint64>(m_pagesSelection->getResultCount(), std::numeric_limits<int>::max()));
    m_pagesMarkButton->setText(PDFAppToolGUIRedactHelper::plural(count, QT_TR_NOOP("Mark %Ln Page"), QT_TR_NOOP("Mark %Ln Pages")));
    m_pagesMarkButton->setEnabled(count > 0 && m_pagesSelection->isValid());
}

void PDFAppToolGUIRedact::onPagesMarkClicked()
{
    if (!m_pagesSelection || !m_pagesSelection->isValid())
    {
        return;
    }

    std::vector<PDFInteger> pages;
    for (const int page : m_pagesSelection->getResultPages())
    {
        pages.push_back(PDFInteger(page) - 1);
    }

    // The dialog closes first, then the marks are written (one commit)
    QPointer<PDFAppToolGUIRedact> guard(this);
    if (PDFMessageBox* dialog = m_pagesDialog.data())
    {
        dialog->accept();
    }
    if (guard)
    {
        markPages(pages);
    }
}

bool PDFAppToolGUIRedact::askClose()
{
    return openClosePrompt() != nullptr;
}

bool PDFAppToolGUIRedact::handleEscape()
{
    // The mode of the input ends, the tool stays open (a drag of Area or Text has been canceled by
    // the shell before: the overlay is an IPDFAppToolDragSource); the status text says so by the same
    // state (getEscapeModeName)
    if (!getEscapeModeName().isEmpty())
    {
        setMode(Mode::None);
        return true;
    }

    // Owner 2026-11-18: then the selected mark is deselected (on the canvas and in the list), as Esc
    // of the annotations does; the next Esc leaves the tool (getEscapeModeHint by the same state)
    if (isSelectionClearedByEscape())
    {
        selectMark(PDFObjectReference());
        return true;
    }
    return false;
}

bool PDFAppToolGUIRedact::isSelectionClearedByEscape() const
{
    return m_mode == Mode::None && m_selectedMark.isValid();
}

QString PDFAppToolGUIRedact::getEscapeModeName() const
{
    switch (m_mode)
    {
        case Mode::Area:
            return tr("Area");
        case Mode::Text:
            return tr("Text");
        case Mode::Search:
            return tr("Search");
        case Mode::None:
            break;
    }
    return QString();
}

QString PDFAppToolGUIRedact::getEscapeModeHint() const
{
    // The engine claims Esc of its drag (PDFWidgetAnnotationManager::shortcutOverrideEvent) and cancels
    // it: the tool and its mode stay
    if (isMarkDragActive())
    {
        return tr("Esc cancels the drag");
    }
    const QString modeHint = BaseClass::getEscapeModeHint();
    if (modeHint.isEmpty() && isSelectionClearedByEscape())
    {
        // handleEscape: the selected mark is deselected first
        return tr("Esc clears the selection");
    }
    return modeHint;
}

bool PDFAppToolGUIRedact::isMarkDragActive() const
{
    const PDFAppAnnotationController* controller = m_annotationController.data();
    const PDFWidgetAnnotationManager* manager = controller && controller->getToolEditing() == this ? controller->getAnnotationManager() : nullptr;
    return manager && isCanvasEditingActive() && manager->getEditFilter() == this && manager->isInteractionActive();
}

bool PDFAppToolGUIRedact::hasDocumentUndoSteps() const
{
    return true;
}

PDFMessageBox* PDFAppToolGUIRedact::openClosePrompt()
{
    const int count = int(getMarks().size());
    if (count == 0)
    {
        return nullptr;
    }
    if (m_closePrompt)
    {
        m_closePrompt->raise();
        return m_closePrompt.data();
    }

    // 448:17473: Back to Redacting | Keep Marks and Close (Primary) | Remove Marks
    QWidget* parent = PDFAppToolGUIRedactHelper::getDialogParent(this);
    PDFMessageBox* dialog = new PDFMessageBox(PDFThemeIcons::Badge::Warning,
                                              PDFAppToolGUIRedactHelper::plural(count, QT_TR_NOOP("Keep the %Ln mark in the document?"), QT_TR_NOOP("Keep the %Ln marks in the document?")),
                                              tr("No redacted copy was created yet. Marks are redaction annotations – kept marks stay in the document so you can finish later. Nothing is removed from the content either way."),
                                              QDialogButtonBox::NoButton, parent);
    dialog->setObjectName(QStringLiteral("redactClosePrompt"));
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setCardWidth(CLOSE_PROMPT_WIDTH);
    QPushButton* back = dialog->addActionButton(tr("Back to Redacting"), PDFMessageBox::ActionPosition::Leading, false);
    back->setObjectName(QStringLiteral("redactCloseBack"));
    connect(back, &QPushButton::clicked, this, &PDFAppToolGUIRedact::onCloseBackClicked);
    QPushButton* keep = dialog->addActionButton(tr("Keep Marks and Close"), PDFMessageBox::ActionPosition::Trailing, true);
    keep->setObjectName(QStringLiteral("redactCloseKeep"));
    keep->setIcon(PDFThemeIcons::getIcon(QStringLiteral("check")));
    connect(keep, &QPushButton::clicked, this, &PDFAppToolGUIRedact::onCloseKeepClicked);
    QPushButton* remove = dialog->addActionButton(tr("Remove Marks"), PDFMessageBox::ActionPosition::Trailing, false);
    remove->setObjectName(QStringLiteral("redactCloseRemove"));
    remove->setIcon(PDFThemeIcons::getIcon(QStringLiteral("trash")));
    connect(remove, &QPushButton::clicked, this, &PDFAppToolGUIRedact::onCloseRemoveClicked);
    m_closePrompt = dialog;

    PDFAppMainWindow::prepareDialog(dialog, parent);
    QPointer<PDFAppToolGUIRedact> guard(this);
    dialog->open();
    return guard ? m_closePrompt.data() : nullptr;
}

void PDFAppToolGUIRedact::onCloseBackClicked()
{
    if (PDFMessageBox* dialog = m_closePrompt.data())
    {
        dialog->reject();
    }
}

void PDFAppToolGUIRedact::onCloseKeepClicked()
{
    QPointer<PDFAppToolGUIRedact> guard(this);
    if (PDFMessageBox* dialog = m_closePrompt.data())
    {
        dialog->accept();
    }

    // The tool ends; the marks stay in the document (the last call: the shell deletes this GUI)
    if (guard)
    {
        if (PDFDocumentToolShell* shell = getShell())
        {
            shell->closeAfterQuestion();
        }
    }
}

void PDFAppToolGUIRedact::onCloseRemoveClicked()
{
    QPointer<PDFAppToolGUIRedact> guard(this);
    if (PDFMessageBox* dialog = m_closePrompt.data())
    {
        dialog->accept();
    }
    if (!guard)
    {
        return;
    }

    // The marks go in one Undo step of the document, then the tool ends (the last call). Marks, which
    // remain (locked ones - the status bar names them -, those beyond the listed ones), keep the tool open:
    // closing would leave them in the document although the user chose to remove them
    removeAllMarks();
    if (guard && getMarks().empty() && !m_isMarkListTruncated)
    {
        if (PDFDocumentToolShell* shell = getShell())
        {
            shell->closeAfterQuestion();
        }
    }
}

PDFMessageBox* PDFAppToolGUIRedact::clearAll()
{
    const int count = int(getMarks().size());
    if (count == 0 || m_isRunning)
    {
        return nullptr;
    }
    if (m_clearDialog)
    {
        m_clearDialog->raise();
        return m_clearDialog.data();
    }

    // No frame of the design: the question of 448:17473 for Clear All, Cancel is the safe Primary
    // (436:11809)
    QWidget* parent = PDFAppToolGUIRedactHelper::getDialogParent(this);
    PDFMessageBox* dialog = new PDFMessageBox(PDFThemeIcons::Badge::Warning,
                                              PDFAppToolGUIRedactHelper::plural(count, QT_TR_NOOP("Remove the %Ln mark?"), QT_TR_NOOP("Remove all %Ln marks?")),
                                              tr("The redaction annotations are removed from the document. The content of the pages stays as it is."),
                                              QDialogButtonBox::NoButton, parent);
    dialog->setObjectName(QStringLiteral("redactClearDialog"));
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    QPushButton* cancel = dialog->addActionButton(tr("Cancel"), PDFMessageBox::ActionPosition::Trailing, true);
    cancel->setObjectName(QStringLiteral("redactClearCancel"));
    connect(cancel, &QPushButton::clicked, dialog, &QDialog::reject);
    QPushButton* remove = dialog->addActionButton(tr("Remove All Marks"), PDFMessageBox::ActionPosition::Trailing, false);
    remove->setObjectName(QStringLiteral("redactClearRemove"));
    remove->setIcon(PDFThemeIcons::getIcon(QStringLiteral("trash")));
    connect(remove, &QPushButton::clicked, this, &PDFAppToolGUIRedact::onClearConfirmClicked);
    m_clearDialog = dialog;

    PDFAppMainWindow::prepareDialog(dialog, parent);
    QPointer<PDFAppToolGUIRedact> guard(this);
    dialog->open();
    return guard ? m_clearDialog.data() : nullptr;
}

void PDFAppToolGUIRedact::onClearConfirmClicked()
{
    QPointer<PDFAppToolGUIRedact> guard(this);
    if (PDFMessageBox* dialog = m_clearDialog.data())
    {
        dialog->accept();
    }
    if (guard)
    {
        removeAllMarks();
    }
}

// ------------------------------------------------------------------ PDFDocumentToolGUI

QString PDFAppToolGUIRedact::getConfirmText() const
{
    return tr("Create Redacted Copy");
}

bool PDFAppToolGUIRedact::isConfirmEnabled() const
{
    // 437:10726: Disabled without a mark; K6.9: enabled again after a stop (the marks stay)
    const PDFAppDocument* document = getDocument();
    return BaseClass::isConfirmEnabled() && document && document->getPageCount() > 0 && !getMarks().empty();
}

QString PDFAppToolGUIRedact::getStatusText() const
{
    const PDFAppDocument* document = getDocument();
    if (!document)
    {
        return BaseClass::getStatusText();
    }
    if (document->getPageCount() <= 0)
    {
        return tr("The document has no pages");
    }

    // Owner 2026-11-17: every status text says, what Esc does now - "Esc cancels the drag", "Esc ends
    // Area" with a mode on, "Esc leaves the tool" without one (getEscapeHint, the order of the shell)
    return tr("%1 · %2").arg(getStatusTextWithoutEscape(), getEscapeHint());
}

QString PDFAppToolGUIRedact::getStatusTextWithoutEscape() const
{
    using H = PDFAppToolGUIRedactHelper;

    switch (m_mode)
    {
        case Mode::Search:
        {
            // 437:12113 "7 matches of "jane\.novak@\S+" · 4 selected" (shown, never stored - R6-19),
            // 437:12452 "4 matches marked · 16 areas in total"
            if (!m_searchPart)
            {
                break;
            }
            switch (m_searchPart->getState())
            {
                case PDFAppToolRedactSearchPart::State::Indexing:
                    return tr("Indexing the text of the document…");
                case PDFAppToolRedactSearchPart::State::Searching:
                    return tr("Searching…");
                case PDFAppToolRedactSearchPart::State::Failed:
                    return m_searchPart->getError();
                case PDFAppToolRedactSearchPart::State::Finished:
                {
                    if (m_searchPart->getMarkedCount() >= 0)
                    {
                        return tr("%1 · %2 in total").arg(H::plural(m_searchPart->getMarkedCount(), QT_TR_NOOP("%Ln match marked"), QT_TR_NOOP("%Ln matches marked")),
                                                          H::plural(int(getMarks().size()), QT_TR_NOOP("%Ln area"), QT_TR_NOOP("%Ln areas")));
                    }
                    const QString query = m_searchPart->getQuery().simplified().left(64);
                    return tr("%1 of “%2” · %3 selected").arg(H::plural(int(m_searchPart->getMatches().size()), QT_TR_NOOP("%Ln match"), QT_TR_NOOP("%Ln matches")),
                                                              query, PDFAppLocale::formatNumber(m_searchPart->getSelectedCount()));
                }
                case PDFAppToolRedactSearchPart::State::Idle:
                    return tr("Search for a text and mark every match · press Enter to search");
            }
            break;
        }
        case Mode::Area:
            // 437:10982 (owner 2026-11-17: Esc ends the mode, the next Esc leaves the tool)
            return tr("Area · drag a rectangle over the content");
        case Mode::Text:
            // 437:11317
            return tr("Text · drag over the text to mark it");
        case Mode::None:
            break;
    }

    if (m_isPreview)
    {
        // 437:13219
        return tr("Preview of the redacted copy · the open document is not changed");
    }

    const int count = int(getMarks().size());
    if (count == 0)
    {
        // 437:10726
        return tr("Mark the content that should be removed");
    }

    // protect-redact-area-selected 1599:48898: an Area mark with its handles; a Text, a Whole page mark
    // and an area of more quadrilaterals are removed only, a locked mark is selected only (owner
    // 2026-11-18)
    const PDFAppToolRedactMark* selectedMark = findMark(m_selectedMark);
    if (selectedMark && isCanvasEditingActive())
    {
        const PDFAppDocument* appDocument = getDocument();
        if (!appDocument || !PDFAppAnnotationOperations::canModifyAnnotation(appDocument->getDocument(), selectedMark->annotation))
        {
            return tr("Locked mark selected");
        }
        switch (selectedMark->kind)
        {
            case PDFAppToolRedactMark::Kind::Text:
                return tr("Text mark selected · Delete removes it");
            case PDFAppToolRedactMark::Kind::Page:
                return tr("Whole page mark selected · Delete removes it");
            case PDFAppToolRedactMark::Kind::Area:
                break;
        }
        return selectedMark->isMovable() ? tr("Area mark selected · drag a handle to change its size") : tr("Area mark selected · Delete removes it");
    }

    // 437:10393 "12 areas marked on 5 pages · not removed yet – the content is removed only in the redacted copy"
    return tr("%1 marked on %2 · not removed yet – the content is removed only in the redacted copy")
            .arg(H::plural(count, QT_TR_NOOP("%Ln area"), QT_TR_NOOP("%Ln areas")), H::plural(getMarkPageCount(), QT_TR_NOOP("%Ln page"), QT_TR_NOOP("%Ln pages")));
}

QString PDFAppToolGUIRedact::getRunningTitle(int percentage) const
{
    // 437:13550 "Creating the redacted copy…"
    Q_UNUSED(percentage);
    return tr("Creating the redacted copy…");
}

QString PDFAppToolGUIRedact::getRunningStatusText(const QString& stepText) const
{
    // 437:13550 "Creating the redacted copy… page 412 of 1,592"
    return stepText.isEmpty() ? getRunningTitle(0) : tr("Creating the redacted copy… %1").arg(stepText);
}

QVariantMap PDFAppToolGUIRedact::getRunParameters() const
{
    // The marks of the document are the areas; the GUI makes marks of the search, of the areas and of
    // the pages, so the core gets none of them (R6-19: no search term leaves the panel)
    QVariantMap parameters = BaseClass::getRunParameters();
    parameters.insert(QString::fromLatin1(PDFDocumentToolRedact::PARAMETER_USE_MARKS), true);
    parameters.insert(QString::fromLatin1(PDFDocumentToolRedact::PARAMETER_SEARCH), QString());
    parameters.insert(QString::fromLatin1(PDFDocumentToolRedact::PARAMETER_AREAS), QString());
    PDFDocumentToolPageSelection noPage;
    noPage.isNone = true;
    parameters.insert(QString::fromLatin1(PDFDocumentToolRedact::PARAMETER_PAGES), noPage.toVariant());
    return parameters;
}

void PDFAppToolGUIRedact::fillResultInput(PDFAppToolResultInput* input) const
{
    // 437:13887: the FileOnly dialog (_REDACTED of the core)
    input->title = tr("The redacted copy is ready");
    input->subtitle = tr("The marked content is gone from the copy. The open document still contains it and keeps its marks.");
    input->fileConfirmText = tr("Save Redacted Copy");
    input->fileOpenText = tr("Open the redacted copy in a new tab");
    input->statusText = tr("Redacted copy created – save it");
}

void PDFAppToolGUIRedact::onParametersChanged(const QString& name)
{
    Q_UNUSED(name);
    updateFillParts();
    updateRightPanel();
    updateOverlay();
}

void PDFAppToolGUIRedact::onRunStateChanged(bool isRunning)
{
    // 437:13550: the modes end, Preview result is Disabled while running
    m_isRunning = isRunning;
    if (isRunning)
    {
        m_mode = Mode::None;
        m_isPreview = false;
    }
    updateParts();
}

void PDFAppToolGUIRedact::onDocumentChanged()
{
    // A new version (a mark added or removed, an assistant): the marks are read again
    refreshMarks();
    if (m_searchPart)
    {
        const PDFAppToolHost* host = getHost();
        m_searchPart->setDocument(getDocument(), host ? host->getView() : nullptr);
    }
    updateParts();

    QPointer<PDFAppToolGUIRedact> guard(this);
    updateConfirmState();
    if (guard)
    {
        updateStatusText();
    }
}

// ------------------------------------------------------------------ slots

void PDFAppToolGUIRedact::onAreaClicked()
{
    setMode(m_mode == Mode::Area ? Mode::None : Mode::Area);
}

void PDFAppToolGUIRedact::onTextClicked()
{
    setMode(m_mode == Mode::Text ? Mode::None : Mode::Text);
}

void PDFAppToolGUIRedact::onSearchClicked()
{
    setMode(m_mode == Mode::Search ? Mode::None : Mode::Search);
}

void PDFAppToolGUIRedact::onPagesClicked()
{
    openPagesDialog();
}

void PDFAppToolGUIRedact::onClearClicked()
{
    clearAll();
}

void PDFAppToolGUIRedact::onFillRowClicked()
{
    PDFTileRow* row = qobject_cast<PDFTileRow*>(sender());
    if (!row)
    {
        return;
    }

    const Fill fill = static_cast<Fill>(row->property(PDFAppToolGUIRedactHelper::FILL_PROPERTY).toInt());
    if (fill == Fill::Custom)
    {
        // Custom color… opens the colors below the rows (437:12794); a swatch chooses the color
        if (m_fillPanel)
        {
            m_fillPanel->setColorGroupVisible(true);
        }
        updateFillParts();
        return;
    }

    QPointer<PDFAppToolGUIRedact> guard(this);
    if (m_fillPanel)
    {
        m_fillPanel->close();
    }
    if (guard)
    {
        setFill(fill);
    }
}

void PDFAppToolGUIRedact::onFillColorSelected(const QColor& color)
{
    if (!color.isValid())
    {
        return;
    }

    QPointer<PDFAppToolGUIRedact> guard(this);
    if (m_fillPanel)
    {
        m_fillPanel->close();
    }
    if (guard)
    {
        setFill(Fill::Custom, color);
    }
}

void PDFAppToolGUIRedact::onAreaPicked(pdf::PDFInteger pageIndex, QRectF rectangle)
{
    addAreaMark(pageIndex, rectangle);
}

void PDFAppToolGUIRedact::onTextPicked(pdf::PDFInteger pageIndex, QPolygonF quadrilaterals)
{
    addTextMark(pageIndex, quadrilaterals);
}

void PDFAppToolGUIRedact::onTextNotFound(pdf::PDFInteger pageIndex)
{
    // 2.6: "This page has no text. Mark the area instead."
    if (m_mode == Mode::Text && !hasPageText(pageIndex))
    {
        m_noTextPage = pageIndex;
        updateLeftPanel();
    }
}

void PDFAppToolGUIRedact::onOverlayDragStateChanged()
{
    // The status text says, what Esc does now (getEscapeHint)
    updateStatusText();
}

void PDFAppToolGUIRedact::onModeControlChanged(int index)
{
    setPreview(index == 1);
}

void PDFAppToolGUIRedact::onDisplayedPageChanged()
{
    updateDisplayedPage();
}

void PDFAppToolGUIRedact::onMarkRowChanged(const QModelIndex& current)
{
    const PDFAppToolRedactMark* mark = m_markModel ? m_markModel->getMark(current.row()) : nullptr;
    if (mark)
    {
        selectMark(mark->annotation);
    }
}

void PDFAppToolGUIRedact::onMarkActionTriggered(const QModelIndex& index, int action)
{
    const PDFAppToolRedactMark* mark = m_markModel ? m_markModel->getMark(index.row()) : nullptr;
    if (mark && action == PDFAppToolRedactMarkDelegate::RemoveAction)
    {
        removeMark(mark->annotation);
    }
}

void PDFAppToolGUIRedact::onSearchMarkRequested()
{
    if (m_searchPart)
    {
        markMatches(m_searchPart->getSelectedMatches());
    }
}

void PDFAppToolGUIRedact::onSearchMatchesChanged()
{
    updateOverlay();
    updateStatusText();
}

void PDFAppToolGUIRedact::onSearchBackRequested()
{
    setMode(Mode::None);
}

}   // namespace pdf

#include "moc_pdfapptoolgui_redact.cpp"
