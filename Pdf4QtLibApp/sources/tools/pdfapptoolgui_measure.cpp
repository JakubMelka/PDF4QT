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

#include "pdfapptoolgui_measure.h"

#include "pdfappcolorpicker.h"
#include "pdfappcontext.h"
#include "pdfappdocument.h"
#include "pdfappdocumentpanels_common.h"
#include "pdfappdocumentpermissions.h"
#include "pdfappdocumentsearchindex.h"
#include "pdfapplocale.h"
#include "pdfappmainwindow.h"
#include "pdfappmeasuredialogs.h"
#include "pdfapppagelayout.h"
#include "pdfappsettingcontrol.h"
#include "pdfapptoolhost.h"
#include "pdfapptoolpanelbuilder.h"
#include "pdfapptoolpanelparts.h"
#include "pdfapptoolpanelpopup.h"
#include "pdfapptoolparametermodel.h"
#include "pdfapptoolresultdialog.h"
#include "pdfappworkspace.h"
#include "pdfdocumenttoolguiregistry.h"
#include "pdfdocumenttoolshell.h"
#include "pdfmeasurescalerepository.h"
#include "pdfrepositoryhelper.h"

#include "pdfcatalog.h"
#include "pdfcomponentbadges.h"
#include "pdfcomponentbuttons.h"
#include "pdfcomponentpainter.h"
#include "pdfcomponentpanels.h"
#include "pdfcomponentrows.h"
#include "pdfdocumenttoolregistry.h"
#include "pdfdocumenttooltablewriter.h"
#include "pdffluentwidgets.h"
#include "pdfmessagebox.h"
#include "pdfthemefonts.h"
#include "pdfthemeiconengine.h"
#include "pdfutils.h"
#include "pdfwidgetstyleproperties.h"

#include <QCheckBox>
#include <QFileInfo>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <numeric>
#include <set>

namespace pdf
{

/// Helpers of the GUI of Measure
class PDFAppToolGUIMeasureHelper
{
public:
    PDFAppToolGUIMeasureHelper() = delete;

    static constexpr const char* KIND_PROPERTY = "pdf4qtMeasureKind";
    static constexpr const char* SCALE_PROPERTY = "pdf4qtMeasureScale";

    /// Most temporary measurements (plan 6.2: the list of 10,000)
    static constexpr size_t MAXIMUM_MEASUREMENTS = size_t(PDFDocumentToolMeasure::MAXIMUM_LISTED_MEASUREMENTS);

    /// The widths of the fields of the right panel (471:5490: the combos of Units 150, Font 170, the spin boxes of
    /// Size 110 and of the opacities 100; review RS7CFIGMA L5)
    static constexpr int UNIT_FIELD_WIDTH = 150;
    static constexpr int FONT_FIELD_WIDTH = 170;
    static constexpr int SIZE_FIELD_WIDTH = 110;
    static constexpr int OPACITY_FIELD_WIDTH = 100;

    /// The gap of the list and the hint under it (471:6681: the rows end at 145, the hint starts at 155)
    static constexpr int LIST_HINT_GAP = 10;

    /// A field row of the card with the field of the width of the frame (PDFAppToolCard::createFieldRow gives
    /// FIELD_WIDTH)
    static QWidget* createFieldRow(const QString& label, QWidget* control, int width, QWidget* parent)
    {
        QWidget* row = PDFAppToolCard::createFieldRow(label, control, parent);
        if (control)
        {
            control->setFixedWidth(width);
        }
        return row;
    }

    /// The family of the font of the labels on the canvas without a chosen one (the font of the theme, 471:5490
    /// "Inter")
    static QString getDefaultFontFamily()
    {
        return PDFThemeFonts::getFont(QFont::Normal, 12).family();
    }

    static QWidget* getDialogParent(const PDFDocumentToolGUI* gui)
    {
        const PDFAppToolHost* host = gui ? gui->getHost() : nullptr;
        QWidget* keyWidget = host ? host->getKeyWidget() : nullptr;
        return keyWidget ? keyWidget->window() : nullptr;
    }

    /// The check box of a Bool parameter carries the title of the parameter (471:5490 "Remember the scale per
    /// document", "Bold", "Italic"; the control alone says "Enable", which suits only a row with its own label)
    static void setCheckBoxTitle(PDFAppSettingControl* control, const PDFAppToolParameterModel* model, const char* parameter)
    {
        QCheckBox* checkBox = control ? control->getCheckBox() : nullptr;
        const PDFDocumentToolParameterDefinition* definition = model ? PDFDocumentToolParameters::find(model->getDefinitions(), QString::fromLatin1(parameter)) : nullptr;
        if (checkBox && definition)
        {
            checkBox->setText(definition->getTitle());
        }
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

    /// The name of a color of the swatches of the annotations ("Black"), else its hex value
    static QString getColorName(const QColor& color)
    {
        for (const PDFColorSwatchRow::Entry& entry : PDFColorSwatchRow::getAnnotationColors(PDFColorSwatchRow::Option::White))
        {
            if (entry.canonicalColor.isValid() && entry.canonicalColor.rgb() == color.rgb())
            {
                return entry.name;
            }
        }
        return color.name(QColor::HexRgb).toUpper();
    }

    /// The rows of the list by their pages (a stable sort keeps the annotations of a page before its temporary measurements)
    static bool isRowBefore(const std::pair<PDFInteger, PDFAppMeasureListModel::Row>& left, const std::pair<PDFInteger, PDFAppMeasureListModel::Row>& right)
    {
        return left.first < right.first;
    }

    /// The opacity of a parameter in percent
    static int getOpacity(const PDFAppToolParameterModel* model, const char* name, int defaultValue)
    {
        bool isOk = false;
        const int value = model ? model->getValue(QString::fromLatin1(name)).toInt(&isOk) : defaultValue;
        return isOk ? qBound(0, value, 100) : defaultValue;
    }
};

// ------------------------------------------------------------------ PDFAppMeasureListModel

PDFAppMeasureListModel::PDFAppMeasureListModel(QObject* parent) :
    BaseClass(parent)
{

}

PDFAppMeasureListModel::~PDFAppMeasureListModel() = default;

void PDFAppMeasureListModel::setRows(std::vector<Row> rows)
{
    beginResetModel();
    m_rows = std::move(rows);
    m_selectedRow = -1;
    endResetModel();
}

const PDFAppMeasureListModel::Row* PDFAppMeasureListModel::getRow(int row) const
{
    return (row >= 0 && row < int(m_rows.size())) ? &m_rows[size_t(row)] : nullptr;
}

int PDFAppMeasureListModel::findTemporary(quint64 id) const
{
    for (size_t i = 0; i < m_rows.size(); ++i)
    {
        if (!m_rows[i].isAnnotation && m_rows[i].id == id)
        {
            return int(i);
        }
    }
    return -1;
}

int PDFAppMeasureListModel::findAnnotation(PDFObjectReference annotation) const
{
    for (size_t i = 0; i < m_rows.size(); ++i)
    {
        if (m_rows[i].isAnnotation && m_rows[i].annotation == annotation)
        {
            return int(i);
        }
    }
    return -1;
}

void PDFAppMeasureListModel::setSelectedRow(int row)
{
    if (m_selectedRow == row)
    {
        return;
    }
    m_selectedRow = row;
    if (!m_rows.empty())
    {
        Q_EMIT dataChanged(index(0), index(int(m_rows.size()) - 1), { PDFComponentRoles::TileToneRole });
    }
}

int PDFAppMeasureListModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : int(m_rows.size());
}

QVariant PDFAppMeasureListModel::data(const QModelIndex& index, int role) const
{
    const Row* row = index.isValid() ? getRow(index.row()) : nullptr;
    if (!row)
    {
        return QVariant();
    }

    switch (role)
    {
        case Qt::DisplayRole:
            return row->title;
        case PDFComponentRoles::SubtitleRole:
        case Qt::ToolTipRole:
            return row->description;
        case PDFComponentRoles::IconNameRole:
            return row->iconName;
        case PDFComponentRoles::TileToneRole:
            // icon-tile 471:6127 … 471:6177, 471:7947, 473:90043: bg/accent-subtle with the accent icon in every row
            // (the selection is the background of the row)
            return int(PDFComponentPainter::TileRowTone::Accent);
        case PDFComponentRoles::TagRole:
            // 471:7947: a measurement annotation of the document
            return row->isAnnotation ? tr("Annotation") : QVariant();
        case PDFComponentRoles::StateIconRole:
            return row->isAnnotation ? QVariant() : QVariant(QStringLiteral("x"));
        case PDFComponentRoles::StateIconColorRole:
            return int(PDFThemeColors::IconDefault);
        case Qt::AccessibleDescriptionRole:
            return row->description;
        default:
            return QVariant();
    }
}

Qt::ItemFlags PDFAppMeasureListModel::flags(const QModelIndex& index) const
{
    return getRow(index.row()) ? (Qt::ItemIsEnabled | Qt::ItemIsSelectable) : Qt::NoItemFlags;
}

// ------------------------------------------------------------------ PDFAppMeasureListDelegate

PDFAppMeasureListDelegate::PDFAppMeasureListDelegate(QObject* parent) :
    BaseClass(parent)
{

}

PDFAppMeasureListDelegate::~PDFAppMeasureListDelegate() = default;

QList<PDFRowDelegate::Action> PDFAppMeasureListDelegate::getActions(const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    QList<Action> actions = BaseClass::getActions(option, index);
    if (!index.isValid() || !index.data(PDFComponentRoles::TagRole).toString().isEmpty())
    {
        // An annotation is the document's: it is removed by Undo or by the annotations, not here
        return actions;
    }

    // The × of a temporary measurement (471:5975) and Delete on the current row
    Action remove;
    remove.id = RemoveAction;
    remove.rect = PDFComponentPainter::getTileRowEndIconRect(QRectF(option.rect), getContent(index), getItemState(option, index));
    remove.text = tr("Remove the measurement");
    remove.shortcut = QKeySequence(Qt::Key_Delete);
    actions.append(remove);
    return actions;
}

// ------------------------------------------------------------------ PDFAppMeasureListView

PDFAppMeasureListView::PDFAppMeasureListView(QWidget* parent) :
    BaseClass(parent)
{
    // Never higher than its rows; lower in a short panel (it scrolls then)
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
}

PDFAppMeasureListView::~PDFAppMeasureListView() = default;

QSize PDFAppMeasureListView::sizeHint() const
{
    // The rows have one height (the tile rows of the list): their count × (the row + the spacing around it)
    const QSize hint = BaseClass::sizeHint();
    const int rowCount = model() ? model()->rowCount() : 0;
    const int rowHeight = rowCount > 0 ? sizeHintForRow(0) : 0;
    const QMargins margins = contentsMargins() + viewportMargins();
    const qint64 height = qint64(rowCount) * (rowHeight + 2 * spacing()) + margins.top() + margins.bottom();
    return QSize(hint.width(), int(qMin<qint64>(height, QWIDGETSIZE_MAX)));
}

QSize PDFAppMeasureListView::minimumSizeHint() const
{
    // At least one row, when there is one
    const QSize hint = BaseClass::minimumSizeHint();
    const int rowCount = model() ? model()->rowCount() : 0;
    return QSize(hint.width(), rowCount > 0 ? qMin(sizeHint().height(), sizeHintForRow(0) + 2 * spacing()) : 0);
}

// ------------------------------------------------------------------ PDFAppToolGUIMeasure

PDFAppToolGUIMeasure::PDFAppToolGUIMeasure(QObject* parent) :
    BaseClass(parent)
{
    m_listModel = new PDFAppMeasureListModel(this);
}

PDFAppToolGUIMeasure::~PDFAppToolGUIMeasure()
{
    // The job of the read ends by itself on its copies; its result goes with the watcher (a child)
    if (m_readCancel)
    {
        m_readCancel->cancel();
    }

    // The dialogs of the GUI go with it (their windows are of the main window)
    for (PDFMessageBox* dialog : { static_cast<PDFMessageBox*>(m_scaleDialog.data()), static_cast<PDFMessageBox*>(m_calibrationDialog.data()),
                                   static_cast<PDFMessageBox*>(m_presetsDialog.data()), m_zeroLengthDialog.data() })
    {
        PDFAppToolGUIMeasureHelper::closeDialog(dialog, this);
    }
}

void PDFAppToolGUIMeasure::registerMeasureTools()
{
    const QString toolId = QString::fromLatin1(PDFDocumentToolMeasure::TOOL_ID);
    if (!PDFDocumentToolRegistry::isImplemented(toolId))
    {
        PDFDocumentToolRegistry::registerTool(std::make_shared<PDFDocumentToolMeasure>());
    }
    if (!PDFDocumentToolGUIRegistry::getFactory(toolId))
    {
        PDFDocumentToolGUIRegistry::registerFactory(toolId, std::make_shared<PDFDocumentToolGUIFactoryOf<PDFAppToolGUIMeasure>>());
    }
}

PDFAppMeasureDrawKind PDFAppToolGUIMeasure::getDrawKind() const
{
    const PDFAppToolParameterModel* model = getModel();
    return PDFAppMeasureText::getDrawKind(model ? model->getValue(QString::fromLatin1(PDFDocumentToolMeasure::PARAMETER_KIND)).toString() : QString());
}

void PDFAppToolGUIMeasure::setDrawKind(PDFAppMeasureDrawKind kind)
{
    m_isDrawing = true;
    m_isCalibrating = false;
    QPointer<PDFAppToolGUIMeasure> guard(this);
    if (PDFAppToolParameterModel* model = getModel(); model && getDrawKind() != kind)
    {
        // One step of Undo of the parameters; onParametersChanged updates the parts
        model->setValue(QString::fromLatin1(PDFDocumentToolMeasure::PARAMETER_KIND), PDFAppMeasureText::getDrawKindId(kind));
    }
    if (!guard)
    {
        return;
    }
    updateInput();
    updateToolbar();
    updateStatusText();
}

bool PDFAppToolGUIMeasure::isDocumentIdentifiable() const
{
    return PDFMeasureScaleRepository::isDocumentIdentifiable(getDocumentPath(), getDocumentId());
}

QString PDFAppToolGUIMeasure::getDocumentPath() const
{
    const PDFAppDocument* document = getDocument();
    if (!document || document->getFilePath().isEmpty())
    {
        return QString();
    }
    return PDFRepositoryHelper::normalizePath(document->getFilePath());
}

QByteArray PDFAppToolGUIMeasure::getDocumentId() const
{
    const PDFAppDocument* document = getDocument();
    const PDFDocumentPointer pdf = document ? document->getDocument() : PDFDocumentPointer();
    return pdf ? pdf->getIdPart(0) : QByteArray();
}

PDFMeasureDisplayUnits PDFAppToolGUIMeasure::getUnits() const
{
    const PDFAppToolParameterModel* model = getModel();
    return PDFDocumentToolMeasure::getUnits(model ? model->getValues() : QVariantMap());
}

void PDFAppToolGUIMeasure::loadScale()
{
    PDFAppContext* context = getContext();
    PDFMeasureScaleRepository repository(context ? context->getDatabase() : nullptr);
    const PDFAppToolParameterModel* model = getModel();
    const bool isRemembering = !model || model->getValue(QString::fromLatin1(PDFDocumentToolMeasure::PARAMETER_REMEMBER_SCALE)).toBool();

    // 473:9500: a scale remembered for the document first, then the one scale of its viewports, then the default
    if (isRemembering && isDocumentIdentifiable())
    {
        if (const std::optional<PDFMeasureScale> scale = repository.findDocumentScale(getDocumentPath(), getDocumentId()))
        {
            m_scale = *scale;
            m_scaleSource = ScaleSource::Remembered;

            // Review RS7CCORE L8: the least recently used scales go, not the least recently set ones (a failed
            // write keeps the scale; the next one prunes by the times written before)
            repository.markDocumentScaleUsed(getDocumentPath(), getDocumentId());
            return;
        }
    }

    // The scales of the viewports of every page (12.9.2); several = "Several (from the document)" (FM-5)
    const PDFAppDocument* document = getDocument();
    const PDFDocumentPointer pdf = document ? document->getDocument() : PDFDocumentPointer();
    std::vector<PDFMeasureScale> documentScales;
    if (pdf)
    {
        const PDFCatalog* catalog = pdf->getCatalog();
        for (size_t i = 0; i < catalog->getPageCount() && documentScales.size() < 2; ++i)
        {
            const PDFPage* page = catalog->getPage(i);
            for (const PDFViewport& viewport : PDFViewport::parseViewports(&pdf->getStorage(), page->getViewports(&pdf->getStorage())))
            {
                const PDFMeasureScale scale = PDFMeasureScale::createFromMeasure(viewport.getMeasure(), page->getEffectiveUserUnit());
                bool isKnown = false;
                for (const PDFMeasureScale& knownScale : documentScales)
                {
                    isKnown = isKnown || knownScale.hasSameProportion(scale);
                }
                if (scale.isValid() && !isKnown)
                {
                    documentScales.push_back(scale);
                }
            }
        }
    }
    m_documentScaleCount = int(documentScales.size());
    if (documentScales.size() == 1)
    {
        m_scale = documentScales.front();
        m_scaleSource = ScaleSource::Document;
        return;
    }

    m_scale = repository.getDefaultScale();
    m_scaleSource = ScaleSource::Default;
}

void PDFAppToolGUIMeasure::setScale(const PDFMeasureScale& scale)
{
    if (!scale.isValid())
    {
        return;
    }

    m_scale = scale;
    m_scaleSource = ScaleSource::Session;
    const PDFAppToolParameterModel* model = getModel();
    const bool isRemembering = !model || model->getValue(QString::fromLatin1(PDFDocumentToolMeasure::PARAMETER_REMEMBER_SCALE)).toBool();
    if (isRemembering && isDocumentIdentifiable())
    {
        // Q7.4: the scale lives in the application only (no /VP is written)
        PDFAppContext* context = getContext();
        PDFMeasureScaleRepository repository(context ? context->getDatabase() : nullptr);
        if (!repository.setDocumentScale(getDocumentPath(), getDocumentId(), scale).isError())
        {
            m_scaleSource = ScaleSource::Remembered;
        }
    }

    // The temporary measurements always use the scale of now (471:7608)
    updateParts();
    updateStatusText();
}

void PDFAppToolGUIMeasure::startCalibration()
{
    m_isCalibrating = true;
    updateInput();
    updateToolbar();
    updateStatusText();
}

std::vector<PDFMeasureItem> PDFAppToolGUIMeasure::createItems(const std::vector<PDFAppMeasurement>& measurements) const
{
    std::vector<PDFMeasureItem> items;
    items.reserve(measurements.size());
    for (const PDFAppMeasurement& measurement : measurements)
    {
        PDFMeasureItem item;
        item.pageIndex = measurement.pageIndex;
        item.kind = measurement.kind;
        item.points = measurement.points;
        item.scale = m_scale;
        items.push_back(std::move(item));
    }
    return items;
}

bool PDFAppToolGUIMeasure::canWriteAnnotations() const
{
    return getWriteRefusalText().isEmpty();
}

QString PDFAppToolGUIMeasure::getWriteRefusalText() const
{
    const PDFAppDocument* document = getDocument();
    const PDFDocumentPointer pdf = document ? document->getDocument() : PDFDocumentPointer();
    if (!pdf)
    {
        return tr("There is no document.");
    }
    if (document->isLocked() || document->isPagedXFA() || !document->isReady())
    {
        // FM-1, FM-3: a locked document has only the temporary measurements and the CSV (isEditable is false
        // while the tool is open - the shell suspends the changes of the user; the tool commits by the shell)
        return tr("Document is locked");
    }
    if (!PDFDocumentToolMeasure::isAnnotationAllowedByCertification(pdf.data()))
    {
        // Table 257: a certification of P = 1 or 2 allows no new annotation
        return tr("The certification of this document does not allow new annotations.");
    }
    if (!PDFAppDocumentPermissions::isAllowed(pdf.data(), PDFAppPermissionRequest::create(PDFAppPermissionAction::Annotate)))
    {
        // FM-2, 473:90704: bit 6 of Table 22
        return tr("This document does not allow new annotations.");
    }
    return QString();
}

bool PDFAppToolGUIMeasure::writeAnnotations(const std::vector<PDFMeasureItem>& items, const QString& description, std::vector<size_t>* failed)
{
    PDFAppDocument* document = getDocument();
    const PDFDocumentPointer pdf = document ? document->getDocument() : PDFDocumentPointer();
    if (!pdf || items.empty() || !canWriteAnnotations())
    {
        return false;
    }

    const PDFAppToolParameterModel* model = getModel();
    const PDFMeasureAnnotationStyle style = PDFDocumentToolMeasure::getStyle(model ? model->getValues() : QVariantMap(), PDFAuthorSettings::getAuthorName());
    int added = 0;
    QJsonArray failedItems;
    QString errorMessage;
    std::optional<PDFDocument> changed = PDFDocumentToolMeasure::addAnnotations(pdf.data(), items, getUnits(), style, &added, &failedItems, &errorMessage);
    if (failed)
    {
        failed->clear();
        for (const QJsonValue& value : failedItems)
        {
            failed->push_back(size_t(value.toObject().value(QStringLiteral("index")).toInteger()));
        }
    }
    if (!changed || added == 0)
    {
        if (PDFAppToolHost* host = getHost())
        {
            host->showTransientMessage(errorMessage.isEmpty() ? tr("The measurements cannot be stored in the document.") : errorMessage);
        }
        return false;
    }

    // The change of the tool: one Undo step of the document (hasDocumentUndoSteps); the slots of the
    // document run - this GUI may be deleted (the caller checks its guard)
    const quint64 version = document->getVersion();
    PDFDocumentPointer changedPointer(new PDFDocument(std::move(*changed)));
    const PDFModifiedDocument modified(changedPointer, nullptr, PDFModifiedDocument::ModificationFlags(PDFModifiedDocument::Annotation));
    return commitDocumentChange(modified, description, version);
}

bool PDFAppToolGUIMeasure::addMeasurement(PDFInteger pageIndex, PDFMeasureKind kind, const std::vector<QPointF>& points, PDFAppMeasureDrawKind drawKind)
{
    const PDFAppDocument* document = getDocument();
    if (!document || pageIndex < 0 || pageIndex >= document->getPageCount())
    {
        return false;
    }

    const std::optional<std::vector<QPointF>> normalized = PDFMeasureGeometry::normalizePoints(kind, points, PDFDocumentToolMeasure::MAXIMUM_POINT_COUNT);
    if (!normalized)
    {
        return false;
    }

    PDFAppMeasurement measurement;
    measurement.pageIndex = pageIndex;
    measurement.drawKind = drawKind;
    measurement.kind = kind;
    measurement.points = *normalized;
    m_notice = Notice::None;

    const PDFAppToolParameterModel* model = getModel();
    const bool isAnnotationStorage = model && model->getValue(QString::fromLatin1(PDFDocumentToolMeasure::PARAMETER_STORAGE)).toString() == QLatin1String(PDFDocumentToolMeasure::STORAGE_ANNOTATIONS);
    if (isAnnotationStorage && canWriteAnnotations())
    {
        // 471:7947: each measurement goes into the document at once (one step of Undo)
        QPointer<PDFAppToolGUIMeasure> guard(this);
        const bool isWritten = writeAnnotations(createItems({ measurement }), tr("Add a measurement annotation"), nullptr);
        if (!guard)
        {
            return isWritten;
        }
        if (isWritten)
        {
            return true;
        }
    }

    if (m_measurements.size() >= PDFAppToolGUIMeasureHelper::MAXIMUM_MEASUREMENTS)
    {
        if (PDFAppToolHost* host = getHost())
        {
            host->showTransientMessage(tr("At most %1 measurements can be listed.").arg(PDFAppLocale::formatNumber(PDFDocumentToolMeasure::MAXIMUM_LISTED_MEASUREMENTS)));
        }
        return false;
    }

    measurement.id = m_nextId++;
    m_measurements.push_back(std::move(measurement));
    updateParts();

    QPointer<PDFAppToolGUIMeasure> guard(this);
    updateConfirmState();
    if (guard)
    {
        updateStatusText();
    }
    return true;
}

bool PDFAppToolGUIMeasure::removeMeasurement(quint64 id)
{
    for (auto it = m_measurements.begin(); it != m_measurements.end(); ++it)
    {
        if (it->id == id)
        {
            m_measurements.erase(it);
            if (m_selectedId == id)
            {
                m_selectedId.reset();
            }
            updateParts();

            QPointer<PDFAppToolGUIMeasure> guard(this);
            updateConfirmState();
            if (guard)
            {
                updateStatusText();
            }
            return true;
        }
    }
    return false;
}

void PDFAppToolGUIMeasure::clearMeasurements()
{
    // Clear of the toolbar: the temporary measurements (the annotations are the document's)
    m_measurements.clear();
    m_selectedId.reset();
    m_notice = Notice::None;
    updateParts();

    QPointer<PDFAppToolGUIMeasure> guard(this);
    updateConfirmState();
    if (guard)
    {
        updateStatusText();
    }
}

bool PDFAppToolGUIMeasure::saveAsAnnotations()
{
    if (m_measurements.empty())
    {
        return false;
    }

    PDFAppDocument* document = getDocument();
    const PDFDocumentPointer pdf = document ? document->getDocument() : PDFDocumentPointer();
    if (pdf && !document->isLocked() && PDFDocumentToolMeasure::isAnnotationAllowedByCertification(pdf.data()) &&
        !PDFAppDocumentPermissions::isAllowed(pdf.data(), PDFAppPermissionRequest::create(PDFAppPermissionAction::Annotate)))
    {
        // FM-2: the owner password is offered (no way around a permission); the user saves again after it
        PDFAppDocumentPermissions::request(document, PDFAppPermissionRequest::create(PDFAppPermissionAction::Annotate), PDFAppToolGUIMeasureHelper::getDialogParent(this));
        return false;
    }

    // One step of Undo of all temporary measurements on any pages (FM-9 cancelled); those, which cannot be
    // written, stay temporary (473:90376)
    const std::vector<PDFAppMeasurement> measurements = m_measurements;
    std::vector<size_t> failed;
    QPointer<PDFAppToolGUIMeasure> guard(this);
    const bool isWritten = writeAnnotations(createItems(measurements), tr("Save the measurements as annotations"), &failed);
    if (!guard || !isWritten)
    {
        return isWritten;
    }

    std::vector<PDFAppMeasurement> remaining;
    for (const size_t index : failed)
    {
        if (index < measurements.size())
        {
            remaining.push_back(measurements[index]);
        }
    }
    m_noticeTotal = int(measurements.size());
    m_noticeStored = int(measurements.size() - remaining.size());
    m_notice = remaining.empty() ? Notice::Converted : Notice::PartiallyConverted;
    m_measurements = std::move(remaining);
    m_selectedId.reset();
    updateParts();

    updateConfirmState();
    if (guard)
    {
        updateStatusText();
    }
    return true;
}

void PDFAppToolGUIMeasure::selectRow(int row)
{
    const PDFAppMeasureListModel::Row* listRow = m_listModel ? m_listModel->getRow(row) : nullptr;
    m_selectedId.reset();
    m_selectedAnnotation = PDFObjectReference();
    if (listRow)
    {
        if (listRow->isAnnotation)
        {
            m_selectedAnnotation = listRow->annotation;
        }
        else
        {
            m_selectedId = listRow->id;
        }
    }

    if (m_listModel)
    {
        m_listModel->setSelectedRow(listRow ? row : -1);
    }
    if (m_listView && m_listModel && m_listView->selectionModel())
    {
        const QSignalBlocker blocker(m_listView->selectionModel());
        if (listRow)
        {
            m_listView->setCurrentIndex(m_listModel->index(row));
        }
        else
        {
            m_listView->selectionModel()->clear();
        }
        m_listView->viewport()->update();
    }
    updateOverlay();

    // FM-28: the row shows its measurement on its page
    QPointer<PDFAppToolGUIMeasure> guard(this);
    if (listRow && listRow->pageIndex >= 0)
    {
        const PDFAppToolHost* host = getHost();
        if (PDFAppDocumentView* view = host ? host->getView() : nullptr)
        {
            view->goToPage(listRow->pageIndex);
        }
    }
    if (guard)
    {
        updateStatusText();
    }
}

int PDFAppToolGUIMeasure::getSelectedRow() const
{
    return m_listModel ? m_listModel->getSelectedRow() : -1;
}

QJsonObject PDFAppToolGUIMeasure::createData() const
{
    const PDFAppDocument* document = getDocument();
    const PDFDocumentPointer pdf = document ? document->getDocument() : PDFDocumentPointer();
    if (!pdf)
    {
        return QJsonObject();
    }

    // Every page (FM-9 cancelled): the annotations of the list (read by the job, never again in this thread) and
    // the temporary measurements in the order of the pages
    std::vector<PDFInteger> pages(pdf->getCatalog()->getPageCount());
    std::iota(pages.begin(), pages.end(), PDFInteger(0));
    return PDFDocumentToolMeasure::createData(pdf.data(), pages, m_annotations, m_isAnnotationsTruncated, createItems(m_measurements), getUnits(), nullptr);
}

bool PDFAppToolGUIMeasure::exportCsv()
{
    const PDFDocumentToolShell* shell = getShell();
    const PDFAppDocument* document = getDocument();
    if (!shell || !document || shell->getState() != PDFDocumentToolShell::State::Editing)
    {
        return false;
    }

    const QJsonObject data = createData();
    if (data.value(QString::fromLatin1(PDFDocumentToolMeasure::DATA_MEASUREMENTS)).toArray().isEmpty())
    {
        return false;
    }

    // 473:91038 "Floor_Plan - measurements.csv": the data of the list, never a new run (plan 4.1)
    const QString baseName = QFileInfo(document->getFilePath()).completeBaseName();
    QString errorMessage;
    PDFDocumentToolOutputFile output = PDFDocumentToolMeasure::writeOutput(data, QVariantMap(), PDFDocumentToolTableWriter::Options(),
                                                                          (baseName.isEmpty() ? document->getTitle() : baseName) + PDFDocumentToolMeasure::tr(" - measurements"), &errorMessage);
    if (output.suggestedFileName.isEmpty())
    {
        if (PDFAppToolHost* host = getHost())
        {
            host->showTransientMessage(errorMessage.isEmpty() ? tr("The measurements could not be exported.") : errorMessage);
        }
        return false;
    }

    PDFDocumentToolResult exportResult;
    exportResult.outputs.push_back(std::move(output));
    exportResult.data = data;

    // The dialog is the last thing: its slots may run anything
    return openExport(exportResult);
}

void PDFAppToolGUIMeasure::fillResultInput(PDFAppToolResultInput* input) const
{
    // 473:91038: Measurements are ready to export - the counts, the columns, the format
    input->title = tr("Measurements are ready to export");
    input->statusText = tr("The measurements are ready to be exported.");
    const int annotations = int(m_annotations.size());
    const int temporaries = int(m_measurements.size());
    std::vector<PDFDocumentToolFact> facts;
    const QString annotationText = annotations == 1 ? tr("%Ln annotation", nullptr, annotations) : tr("%Ln annotations", nullptr, annotations);
    facts.push_back({ QStringLiteral("measurements"), tr("Measurements"), tr("%1, %2").arg(tr("%Ln temporary", nullptr, temporaries), annotationText) });
    facts.push_back({ QStringLiteral("columns"), tr("Columns"), tr("Page, Type, Value, Unit, Text, Scale") });
    facts.push_back({ QStringLiteral("format"), tr("Format"), tr("CSV, semicolon separated, UTF-8") });
    input->result.facts = std::move(facts);
}

QString PDFAppToolGUIMeasure::getStatusTextWithoutEscape() const
{
    const PDFAppDocument* document = getDocument();
    if (!document || document->getPageCount() <= 0)
    {
        return tr("The document has no pages");
    }

    const PDFAppMeasureOverlay* overlay = m_overlay.data();
    const size_t pointCount = overlay ? overlay->getPoints().size() : 0;
    const QString preview = overlay ? overlay->getPreviewText() : QString();

    // The dialogs of the calibration say what they want (473:6438, 473:6755)
    if (m_calibrationDialog && m_calibrationDialog->isVisible())
    {
        return tr("Enter the real distance between the two picked points.");
    }
    if (m_zeroLengthDialog && m_zeroLengthDialog->isVisible())
    {
        return tr("The picked line has a zero length.");
    }

    if (m_isCalibrating)
    {
        // 471:7363
        return pointCount == 0 ? tr("Calibrate: click the first point of a known distance. Right-click starts over.")
                               : (preview.isEmpty() ? tr("Calibrate: click the second point.") : tr("Calibrate: click the second point. %1").arg(preview));
    }

    if (m_isDrawing && pointCount > 0)
    {
        const PDFAppMeasureDrawKind kind = getDrawKind();
        const QString title = PDFAppMeasureText::getDrawKindTitle(kind);
        QString text;
        switch (kind)
        {
            case PDFAppMeasureDrawKind::Perimeter:
            case PDFAppMeasureDrawKind::Area:
                // 471:6681
                text = tr("%1: click the next corner, or the first point to close the shape. Right-click starts over.").arg(title);
                break;
            case PDFAppMeasureDrawKind::RectanglePerimeter:
            case PDFAppMeasureDrawKind::RectangleArea:
                text = tr("%1: click the opposite corner.").arg(title);
                break;
            case PDFAppMeasureDrawKind::Angle:
                text = pointCount == 1 ? tr("%1: click the vertex.").arg(title) : tr("%1: click the end of the second arm.").arg(title);
                break;
            default:
                // 471:5731 "Horizontal: click the second point. 4.80 m"
                text = tr("%1: click the second point.").arg(title);
                break;
        }
        return preview.isEmpty() ? text : tr("%1 %2").arg(text, preview);
    }

    switch (m_notice)
    {
        case Notice::Converted:
            // 473:90043
            return m_noticeStored == 1 ? tr("%Ln measurement was stored in the document as an annotation.", nullptr, m_noticeStored)
                                       : tr("%Ln measurements were stored in the document as annotations.", nullptr, m_noticeStored);
        case Notice::PartiallyConverted:
            // 473:90376
            return tr("%1 of %2 measurements were stored as annotations.").arg(PDFAppLocale::formatNumber(m_noticeStored), PDFAppLocale::formatNumber(m_noticeTotal));
        case Notice::None:
            break;
    }

    if (!m_isShown && !m_measurements.empty())
    {
        // 473:91785
        return tr("Measurements are hidden. They are still listed on the left.");
    }

    const PDFAppToolParameterModel* model = getModel();
    const bool isAnnotationStorage = model && model->getValue(QString::fromLatin1(PDFDocumentToolMeasure::PARAMETER_STORAGE)).toString() == QLatin1String(PDFDocumentToolMeasure::STORAGE_ANNOTATIONS);
    if (isAnnotationStorage)
    {
        // 471:7947, 473:90704
        return canWriteAnnotations() ? tr("New measurements are stored as annotations in the document.") : tr("Measurements cannot be stored in this document.");
    }

    if (!m_measurements.empty())
    {
        // 471:5975
        const int count = int(m_measurements.size());
        return count == 1 ? tr("%Ln temporary measurement. Save as Annotations stores it in the document.", nullptr, count)
                          : tr("%Ln temporary measurements. Save as Annotations stores them in the document.", nullptr, count);
    }

    if (!m_isDrawing)
    {
        return tr("Choose a kind of measurement in the toolbar to measure.");
    }

    // 471:5490 "Linear: click the first point. Right-click starts over, C shows the large cross."
    return tr("%1: click the first point. Right-click starts over, C shows the large cross.").arg(PDFAppMeasureText::getDrawKindTitle(getDrawKind()));
}

QString PDFAppToolGUIMeasure::getStatusText() const
{
    // An open menu of the toolbar says what it is (471:6294, 471:6943, 471:7608)
    const QString menuText = getOpenMenuStatusText();
    if (!menuText.isEmpty())
    {
        return menuText;
    }

    // Owner 2026-11-17: every status text says, what Esc does now
    return tr("%1 · %2").arg(getStatusTextWithoutEscape(), getEscapeHint());
}

QString PDFAppToolGUIMeasure::getOpenMenuStatusText() const
{
    // The open panel (the button Scale is checked also while calibrating, K7.5: the panel counts, not the button)
    const std::array<std::pair<const PDFAppToolPanelPopup*, QString>, 3> menus = { std::make_pair(m_kindPanel.data(), tr("Kind of measurement")),
                                                                                   std::make_pair(m_scalePanel.data(), tr("Scale of the drawing.")),
                                                                                   std::make_pair(m_storageMenu ? m_storageMenu->getPanel() : nullptr, tr("Where new measurements are kept.")) };
    for (const auto& [panel, text] : menus)
    {
        if (panel && panel->isVisible())
        {
            return text;
        }
    }
    return QString();
}

void PDFAppToolGUIMeasure::onMenuToggled()
{
    // The toolbar checks the button inside of its opening of the panel: the status bar, whose slots may delete the
    // window, follows from the event loop (once for more toggles)
    if (!m_isMenuStatusQueued)
    {
        m_isMenuStatusQueued = true;
        QMetaObject::invokeMethod(this, &PDFAppToolGUIMeasure::onMenuToggledQueued, Qt::QueuedConnection);
    }
}

void PDFAppToolGUIMeasure::onMenuToggledQueued()
{
    m_isMenuStatusQueued = false;
    updateStatusText();
}

QToolButton* PDFAppToolGUIMeasure::getKindButton() const
{
    return m_kindPanel ? m_kindPanel->getButton() : nullptr;
}

QToolButton* PDFAppToolGUIMeasure::getScaleButton() const
{
    return m_scalePanel ? m_scalePanel->getButton() : nullptr;
}

QToolButton* PDFAppToolGUIMeasure::getStorageButton() const
{
    return m_storageMenu ? m_storageMenu->getButton() : nullptr;
}

PDFAppToolCallout* PDFAppToolGUIMeasure::getCallout(Callout callout) const
{
    const auto it = m_callouts.find(callout);
    return it != m_callouts.end() ? it->second.data() : nullptr;
}

std::optional<PDFAppToolGUIMeasure::Callout> PDFAppToolGUIMeasure::getShownCallout() const
{
    for (const auto& [callout, widget] : m_callouts)
    {
        if (widget && !widget->isHidden())
        {
            return callout;
        }
    }
    return std::nullopt;
}

PDFTileRow* PDFAppToolGUIMeasure::getKindRow(PDFAppMeasureDrawKind kind) const
{
    const auto it = m_kindRows.find(kind);
    return it != m_kindRows.end() ? it->second.data() : nullptr;
}

// ------------------------------------------------------------------ dialogs

PDFAppMeasureScaleDialog* PDFAppToolGUIMeasure::openCustomScaleDialog()
{
    if (m_scaleDialog)
    {
        m_scaleDialog->raise();
        return m_scaleDialog.data();
    }

    QWidget* parent = PDFAppToolGUIMeasureHelper::getDialogParent(this);
    PDFAppMeasureScaleDialog* dialog = new PDFAppMeasureScaleDialog(PDFAppMeasureScaleDialog::Mode::Custom, m_scale, 0.0, parent);
    connect(dialog, &PDFAppMeasureScaleDialog::scaleAccepted, this, &PDFAppToolGUIMeasure::onCustomScaleAccepted);
    m_scaleDialog = dialog;
    PDFAppMainWindow::prepareDialog(dialog, parent);
    QPointer<PDFAppToolGUIMeasure> guard(this);
    dialog->open();
    return guard ? m_scaleDialog.data() : nullptr;
}

PDFAppMeasureScaleDialog* PDFAppToolGUIMeasure::openCalibrationDialog(PDFReal length)
{
    // Review RS7CAPP L3: the picked length belongs to the calibration dialog only - an open Custom / Default scale
    // dialog (m_scaleDialog) never takes it; a closed dialog, which waits for its deletion, takes nothing either
    if (m_calibrationDialog && m_calibrationDialog->isVisible())
    {
        m_calibrationDialog->raise();
        return m_calibrationDialog.data();
    }

    QWidget* parent = PDFAppToolGUIMeasureHelper::getDialogParent(this);
    PDFAppMeasureScaleDialog* dialog = new PDFAppMeasureScaleDialog(PDFAppMeasureScaleDialog::Mode::Calibrate, m_scale, length, parent);
    connect(dialog, &PDFAppMeasureScaleDialog::scaleAccepted, this, &PDFAppToolGUIMeasure::onCustomScaleAccepted);
    // Queued: QDialog emits rejected inside of its done() and goes on with the dialog after it
    connect(dialog, &QDialog::rejected, this, &PDFAppToolGUIMeasure::onCalibrationRejected, Qt::QueuedConnection);
    m_calibrationDialog = dialog;
    PDFAppMainWindow::prepareDialog(dialog, parent);
    QPointer<PDFAppToolGUIMeasure> guard(this);
    dialog->open();
    if (!guard)
    {
        return nullptr;
    }

    // 473:6438: the status bar asks for the real distance (its slots may delete this GUI - the last call)
    QPointer<PDFAppMeasureScaleDialog> openedDialog = m_calibrationDialog;
    updateStatusText();
    return guard ? openedDialog.data() : nullptr;
}

void PDFAppToolGUIMeasure::onCalibrationRejected()
{
    // Review RS7CAPP L5: Cancel of the calibration ends it, as Cancel of 473:6755 (no frame shows Calibrate on after
    // a canceled dialog; Calibrate Scale… of the menu Scale starts it again). A new calibration started meanwhile
    // keeps going. Also the slot of Cancel of 473:6755 (the calibration is off already): the status text follows.
    if (m_isCalibrating && !(m_calibrationDialog && m_calibrationDialog->isVisible()))
    {
        m_isCalibrating = false;
        updateInput();
        updateToolbar();
    }
    updateStatusText();
}

PDFAppMeasureScaleDialog* PDFAppToolGUIMeasure::openDefaultScaleDialog()
{
    if (m_scaleDialog)
    {
        m_scaleDialog->raise();
        return m_scaleDialog.data();
    }

    PDFAppContext* context = getContext();
    PDFMeasureScaleRepository repository(context ? context->getDatabase() : nullptr);
    QWidget* parent = PDFAppToolGUIMeasureHelper::getDialogParent(this);
    PDFAppMeasureScaleDialog* dialog = new PDFAppMeasureScaleDialog(PDFAppMeasureScaleDialog::Mode::Default, repository.getDefaultScale(), 0.0, parent);
    connect(dialog, &PDFAppMeasureScaleDialog::scaleAccepted, this, &PDFAppToolGUIMeasure::onDefaultScaleAccepted);
    m_scaleDialog = dialog;
    PDFAppMainWindow::prepareDialog(dialog, parent);
    QPointer<PDFAppToolGUIMeasure> guard(this);
    dialog->open();
    return guard ? m_scaleDialog.data() : nullptr;
}

PDFAppMeasurePresetsDialog* PDFAppToolGUIMeasure::openPresetsDialog()
{
    if (m_presetsDialog)
    {
        m_presetsDialog->raise();
        return m_presetsDialog.data();
    }

    PDFAppContext* context = getContext();
    PDFMeasureScaleRepository repository(context ? context->getDatabase() : nullptr);
    QWidget* parent = PDFAppToolGUIMeasureHelper::getDialogParent(this);
    PDFAppMeasurePresetsDialog* dialog = new PDFAppMeasurePresetsDialog(repository.getPresets(), parent);
    connect(dialog, &PDFAppMeasurePresetsDialog::presetsAccepted, this, &PDFAppToolGUIMeasure::onPresetsAccepted);
    m_presetsDialog = dialog;
    PDFAppMainWindow::prepareDialog(dialog, parent);
    QPointer<PDFAppToolGUIMeasure> guard(this);
    dialog->open();
    return guard ? m_presetsDialog.data() : nullptr;
}

void PDFAppToolGUIMeasure::onCustomScaleAccepted(pdf::PDFMeasureScale scale)
{
    if (!scale.isValid())
    {
        return;
    }

    // 473:6438, 473:7024: a named scale is saved as a preset too
    if (!scale.getName().isEmpty())
    {
        PDFAppContext* context = getContext();
        PDFMeasureScaleRepository repository(context ? context->getDatabase() : nullptr);
        std::vector<PDFMeasureScale> presets = repository.getPresets();
        if (presets.size() < size_t(PDFMeasureScaleRepository::MAXIMUM_PRESETS))
        {
            presets.push_back(scale);
            const PDFDatabaseError error = repository.setPresets(presets);
            if (error.isError())
            {
                if (PDFAppToolHost* host = getHost())
                {
                    host->showTransientMessage(tr("The preset could not be stored."));
                }
            }
        }
        rebuildScaleRows();
    }
    m_isCalibrating = false;
    updateInput();
    setScale(scale);
}

void PDFAppToolGUIMeasure::onDefaultScaleAccepted(pdf::PDFMeasureScale scale)
{
    PDFAppContext* context = getContext();
    PDFMeasureScaleRepository repository(context ? context->getDatabase() : nullptr);
    if (repository.setDefaultScale(scale).isError())
    {
        if (PDFAppToolHost* host = getHost())
        {
            host->showTransientMessage(tr("The default scale could not be stored."));
        }
        return;
    }

    // A document, which uses the default scale, uses the new one
    if (m_scaleSource == ScaleSource::Default)
    {
        m_scale = scale;
    }
    updateParts();
    updateStatusText();
}

void PDFAppToolGUIMeasure::onPresetsAccepted(std::vector<pdf::PDFMeasureScale> presets, bool isRestored)
{
    PDFAppContext* context = getContext();
    PDFMeasureScaleRepository repository(context ? context->getDatabase() : nullptr);
    const PDFDatabaseError error = isRestored ? repository.restoreDefaultPresets() : repository.setPresets(presets);
    if (error.isError())
    {
        if (PDFAppToolHost* host = getHost())
        {
            host->showTransientMessage(tr("The presets could not be stored."));
        }
    }
    rebuildScaleRows();
    updateToolbar();
}

void PDFAppToolGUIMeasure::onZeroLengthPickAgain()
{
    if (PDFMessageBox* dialog = m_zeroLengthDialog.data())
    {
        dialog->accept();
    }
    startCalibration();
}

// ------------------------------------------------------------------ PDFDocumentToolGUI

QWidget* PDFAppToolGUIMeasure::createOverlay(QWidget* canvasArea)
{
    const PDFAppToolHost* host = getHost();
    PDFWidget* pdfWidget = host ? host->getPdfWidget() : nullptr;
    if (!pdfWidget || !canvasArea)
    {
        return nullptr;
    }

    PDFAppMeasureOverlay* overlay = new PDFAppMeasureOverlay(pdfWidget, canvasArea);
    connect(overlay, &PDFAppMeasureOverlay::measurementPicked, this, &PDFAppToolGUIMeasure::onMeasurementPicked);
    connect(overlay, &PDFAppMeasureOverlay::calibrationPicked, this, &PDFAppToolGUIMeasure::onCalibrationPicked);
    // Queued: the overlay emits it in the dispatch of the mouse input
    connect(overlay, &PDFAppMeasureOverlay::drawingChanged, this, &PDFAppToolGUIMeasure::onOverlayDrawingChanged, Qt::QueuedConnection);
    m_overlay = overlay;
    updateInput();
    updateOverlay();
    return overlay;
}

QString PDFAppToolGUIMeasure::getConfirmText() const
{
    return tr("Save as Annotations");
}

QString PDFAppToolGUIMeasure::getConfirmIconName() const
{
    return QStringLiteral("to-annotation");
}

QString PDFAppToolGUIMeasure::getCancelText() const
{
    // btn-cancel of 471:5490 … 473:92070: Close in every state - the tool has no run to cancel, Save as
    // Annotations is the only change of the document
    return tr("Close");
}

bool PDFAppToolGUIMeasure::isConfirmEnabled() const
{
    // 471:5975: Default while temporary measurements exist and they can be written (473:90704 Disabled)
    return BaseClass::isConfirmEnabled() && !m_measurements.empty() && canWriteAnnotations();
}

bool PDFAppToolGUIMeasure::confirmWithoutRun()
{
    // Save as Annotations is the action of the GUI (one commit), never a run of the tool
    saveAsAnnotations();
    return true;
}

void PDFAppToolGUIMeasure::onParametersChanged(const QString& name)
{
    Q_UNUSED(name);
    updateParts();
    updateStatusText();
}

void PDFAppToolGUIMeasure::onDocumentChanged()
{
    // A new version (an annotation added or removed, Undo, an assistant): the annotations are read again
    refreshDocument();
    updateParts();

    QPointer<PDFAppToolGUIMeasure> guard(this);
    updateConfirmState();
    if (guard)
    {
        updateStatusText();
    }
}

bool PDFAppToolGUIMeasure::handleEscape()
{
    // The order of the owner (2026-11-17): the points of a drawing are canceled by the shell (the overlay is
    // the drag source), then Calibrate, then the drawing mode, then the selected row, then the tool
    if (m_isCalibrating)
    {
        m_isCalibrating = false;
        updateInput();
        updateToolbar();
        updateStatusText();
        return true;
    }
    if (m_isDrawing)
    {
        m_isDrawing = false;
        updateInput();
        updateToolbar();
        updateStatusText();
        return true;
    }
    if (getSelectedRow() >= 0)
    {
        selectRow(-1);
        return true;
    }
    return false;
}

QString PDFAppToolGUIMeasure::getEscapeModeName() const
{
    if (m_isCalibrating)
    {
        return PDFAppMeasureText::getDrawKindTitle(PDFAppMeasureDrawKind::Calibrate);
    }
    return m_isDrawing ? PDFAppMeasureText::getDrawKindTitle(getDrawKind()) : QString();
}

QString PDFAppToolGUIMeasure::getEscapeModeHint() const
{
    const QString modeHint = BaseClass::getEscapeModeHint();
    if (modeHint.isEmpty() && getSelectedRow() >= 0)
    {
        return tr("Esc clears the selection");
    }
    return modeHint;
}

bool PDFAppToolGUIMeasure::hasDocumentUndoSteps() const
{
    // The annotations of the tool are changes of the document: Undo of the tool takes them back (FM-10)
    return true;
}

bool PDFAppToolGUIMeasure::isResultStaleBy(const QString& parameterName) const
{
    Q_UNUSED(parameterName);
    return false;
}

// ------------------------------------------------------------------ parts

void PDFAppToolGUIMeasure::describeToolbar(PDFAppToolPanelBuilder& builder)
{
    if (!m_isInitialized)
    {
        m_isInitialized = true;
        refreshDocument();
        loadScale();

        // The tool stays open on a locked document (PDFDocumentToolLockedAccess::Available): the callouts, Save as
        // Annotations and the storage follow the lock (FM-1, FM-3)
        if (PDFAppDocument* document = getDocument())
        {
            connect(document, &PDFAppDocument::lockChanged, this, &PDFAppToolGUIMeasure::onDocumentLockChanged, Qt::UniqueConnection);
        }
    }

    // tool-actions 471:5490: the kind, the scale | the storage, Show, the big cross, Clear | Export CSV
    PDFAppToolPanelPopup* kindPanel = builder.addMenu(QStringLiteral("measureKind"), PDFAppMeasureText::getDrawKindIconName(getDrawKind()), QString(), KIND_PANEL_WIDTH);
    m_kindPanel = kindPanel;
    m_kindRows.clear();
    const std::vector<std::pair<QString, std::vector<std::pair<PDFAppMeasureDrawKind, QString>>>> groups = {
        // The captions of the groups in capitals (471:6294 "LENGTH", "PERIMETER AND AREA", "ANGLE")
        { tr("LENGTH"), { { PDFAppMeasureDrawKind::Horizontal, tr("Two points, measured horizontally") },
                          { PDFAppMeasureDrawKind::Vertical, tr("Two points, measured vertically") },
                          { PDFAppMeasureDrawKind::Linear, tr("Distance between two points") } } },
        { tr("PERIMETER AND AREA"), { { PDFAppMeasureDrawKind::Perimeter, tr("Click the corners, finish on the first point") },
                                      { PDFAppMeasureDrawKind::RectanglePerimeter, tr("Two opposite corners") },
                                      { PDFAppMeasureDrawKind::Area, tr("Click the corners, finish on the first point") },
                                      { PDFAppMeasureDrawKind::RectangleArea, tr("Two opposite corners") } } },
        { tr("ANGLE"), { { PDFAppMeasureDrawKind::Angle, tr("Three points, the second one is the vertex") } } }
    };
    for (const auto& [caption, kinds] : groups)
    {
        kindPanel->addGroupCaption(caption);
        for (const auto& [kind, description] : kinds)
        {
            PDFTileRow* row = kindPanel->addRow(PDFAppMeasureText::getDrawKindTitle(kind), description, PDFAppMeasureText::getDrawKindIconName(kind), PDFTileRow::Kind::MenuItem);
            row->setObjectName(QStringLiteral("measureKind-%1").arg(PDFAppMeasureText::getDrawKindId(kind)));
            row->setProperty(PDFAppToolGUIMeasureHelper::KIND_PROPERTY, int(kind));
            row->setSelectionBehavior(PDFComponentButton::SelectionBehavior::Exclusive);
            connect(row, &PDFTileRow::clicked, this, &PDFAppToolGUIMeasure::onKindRowClicked);
            m_kindRows[kind] = row;
        }
    }

    // Scale 471:6943: the presets, Calibrate Scale…, Custom Scale…, Manage Presets…
    m_scalePanel = builder.addMenu(QStringLiteral("measureScale"), QStringLiteral("scale"), QString(), SCALE_PANEL_WIDTH);
    rebuildScaleRows();
    builder.addActionDivider();

    // Temporary / Annotations in the document 471:7608
    m_storageMenu = builder.addEnumMenu(QString::fromLatin1(PDFDocumentToolMeasure::PARAMETER_STORAGE), QStringLiteral("pin-off"), QStringLiteral("%1"), STORAGE_PANEL_WIDTH);
    if (m_storageMenu)
    {
        m_storageMenu->setButtonTitle(QString::fromLatin1(PDFDocumentToolMeasure::STORAGE_TEMPORARY), tr("Temporary"));
        m_storageMenu->setButtonTitle(QString::fromLatin1(PDFDocumentToolMeasure::STORAGE_ANNOTATIONS), tr("Annotations"));
        m_storageMenu->setButtonIconFollowsOption(true);
    }

    // Show, the big cross, Clear (icons, 471:5490): icon buttons at every width of the toolbar (the property, not the
    // style of the button - the toolbar sets the style of its buttons by its width, and three long titles beside
    // their icons made the toolbar collapse to icons only at full width); the title is the tooltip and the
    // accessible name
    m_showButton = builder.addAction(tr("Show measurements"), QStringLiteral("eye"), this, &PDFAppToolGUIMeasure::onShowClicked);
    m_showButton->setCheckable(true);
    m_crossButton = builder.addAction(tr("Large cross (C)"), QStringLiteral("crosshair"), this, &PDFAppToolGUIMeasure::onCrossClicked);
    m_crossButton->setCheckable(true);
    m_clearButton = builder.addAction(tr("Clear temporary measurements"), QStringLiteral("trash"), this, &PDFAppToolGUIMeasure::onClearClicked);
    for (QToolButton* button : { m_showButton.data(), m_crossButton.data(), m_clearButton.data() })
    {
        const QString title = button->text();
        button->setText(QString());
        button->setToolTip(title);
        button->setAccessibleName(title);
        PDFWidgetStyleProperties::set(button, PDFWidgetStyleProperties::ICON_ONLY);
    }
    builder.addActionDivider();
    m_exportButton = builder.addAction(tr("Export CSV"), QStringLiteral("csv"), this, &PDFAppToolGUIMeasure::onExportClicked);

    // The status bar names an open menu (471:6294, 471:6943, 471:7608): the toolbar checks the button of the panel
    for (QToolButton* button : { getKindButton(), getScaleButton(), getStorageButton() })
    {
        if (button)
        {
            connect(button, &QAbstractButton::toggled, this, &PDFAppToolGUIMeasure::onMenuToggled, Qt::UniqueConnection);
        }
    }
    updateToolbar();
}

void PDFAppToolGUIMeasure::rebuildScaleRows()
{
    PDFAppToolPanelPopup* panel = m_scalePanel.data();
    if (!panel)
    {
        return;
    }

    // The old rows go later (a row may be the sender of the call); hidden at once
    for (const QPointer<QWidget>& widget : m_scalePanelWidgets)
    {
        if (widget)
        {
            widget->hide();
            widget->deleteLater();
        }
    }
    m_scalePanelWidgets.clear();

    PDFAppContext* context = getContext();
    PDFMeasureScaleRepository repository(context ? context->getDatabase() : nullptr);
    const std::vector<PDFMeasureScale> presets = repository.getPresets();
    for (size_t i = 0; i < presets.size(); ++i)
    {
        const PDFMeasureScale& preset = presets[i];
        const QString ratio = PDFAppMeasureText::formatRatio(preset);
        const QString description = preset.getDescription().isEmpty() ? ratio : tr("%1 · %2").arg(ratio, preset.getDescription());
        PDFTileRow* row = panel->addRow(preset.getDisplayName(), description, QString(), PDFTileRow::Kind::MenuItem);
        row->setObjectName(QStringLiteral("measureScalePreset-%1").arg(i));
        row->setTitleKind(PDFComponentPainter::TextKind::Data);
        row->setTileText(PDFAppMeasureText::formatProportion(preset));
        row->setProperty(PDFAppToolGUIMeasureHelper::SCALE_PROPERTY, int(i));
        row->setSelectionBehavior(PDFComponentButton::SelectionBehavior::Exclusive);
        connect(row, &PDFTileRow::clicked, this, &PDFAppToolGUIMeasure::onScaleRowClicked);
        m_scalePanelWidgets.push_back(row);
    }
    m_scalePanelWidgets.push_back(panel->addGroupDivider());

    PDFTileRow* calibrate = panel->addRow(tr("Calibrate Scale…"), tr("Pick two points of a known distance"), QStringLiteral("calibrate"), PDFTileRow::Kind::MenuItem);
    calibrate->setObjectName(QStringLiteral("measureScaleCalibrate"));
    connect(calibrate, &PDFTileRow::clicked, this, &PDFAppToolGUIMeasure::onCalibrateClicked);
    m_scalePanelWidgets.push_back(calibrate);
    PDFTileRow* custom = panel->addRow(tr("Custom Scale…"), tr("Enter the paper and the real distance"), QStringLiteral("edit"), PDFTileRow::Kind::MenuItem);
    custom->setObjectName(QStringLiteral("measureScaleCustom"));
    connect(custom, &PDFTileRow::clicked, this, &PDFAppToolGUIMeasure::onCustomScaleClicked);
    m_scalePanelWidgets.push_back(custom);
    PDFTileRow* manage = panel->addRow(tr("Manage Presets…"), tr("Add, edit and remove the presets"), QStringLiteral("list"), PDFTileRow::Kind::MenuItem);
    manage->setObjectName(QStringLiteral("measureScaleManage"));
    connect(manage, &PDFTileRow::clicked, this, &PDFAppToolGUIMeasure::onManagePresetsClicked);
    m_scalePanelWidgets.push_back(manage);
    m_scalePresets = presets;
}

void PDFAppToolGUIMeasure::describeLeftPanel(PDFAppToolPanelBuilder& builder)
{
    // 471:5975: Measurements, "6 temporary · 2 stored as annotations", the list, the hint
    m_leftHeader = builder.addPanelHeader(tr("Measurements"), QString());
    m_leftHeader->setObjectName(QStringLiteral("measureListHeader"));

    // measurement-list 471:6123: the rows lie on the panel (no card, as the list of Compare); the list and the empty
    // state fill the panel in turn, the content of the empty state stays at the top (471:5490). The list is as high
    // as its rows and the hint of the temporary measurements lies right under them (471:6681; review RS7CFIGMA L5)
    QWidget* card = new QWidget(builder.getWidget());
    card->setObjectName(QStringLiteral("measureListCard"));
    QVBoxLayout* cardLayout = PDFAppPageWidgets::createColumn(card, PDFAppToolGUIMeasureHelper::LIST_HINT_GAP);
    PDFAppMeasureListDelegate* delegate = new PDFAppMeasureListDelegate(card);
    delegate->setKind(PDFTileRowDelegate::Kind::ListItem);
    delegate->setTitleKind(PDFTileRowDelegate::TextKind::Data);
    connect(delegate, &PDFAppMeasureListDelegate::actionTriggered, this, &PDFAppToolGUIMeasure::onRowActionTriggered);
    PDFAppMeasureListView* view = new PDFAppMeasureListView(card);
    view->setObjectName(QStringLiteral("measureListView"));
    view->setFrameShape(QFrame::NoFrame);
    view->setItemDelegate(delegate);
    view->setModel(m_listModel);
    view->setSelectionMode(QAbstractItemView::SingleSelection);
    view->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    view->viewport()->setAutoFillBackground(false);
    view->setSpacing(1);
    view->setAccessibleName(tr("Measurements"));
    cardLayout->addWidget(view);
    PDFAppParagraph* hint = PDFAppPageWidgets::createText(tr("Temporary measurements are lost when the document is closed. Save them as annotations to keep them in the document."),
                                                          PDFThemeFonts::getFont(QFont::Normal, 11), 13, PDFThemeColors::TextMuted, card);
    hint->setObjectName(QStringLiteral("measureListHint"));
    hint->setMinimumWidth(1);
    cardLayout->addWidget(hint);
    cardLayout->addStretch(1);
    m_listHint = hint;
    connect(view->selectionModel(), &QItemSelectionModel::currentChanged, this, &PDFAppToolGUIMeasure::onRowChanged);
    m_listView = view;
    m_listCard = card;
    builder.addWidget(card);
    builder.setFillsPanel(card);

    // 471:5490 empty-list: the icon 24 at 24, the text at 54 with the margins 12, no card
    QWidget* empty = new QWidget(builder.getWidget());
    empty->setObjectName(QStringLiteral("measureEmptyCard"));
    QVBoxLayout* emptyLayout = PDFAppPageWidgets::createColumn(empty, 6);
    emptyLayout->setContentsMargins(12, 24, 12, 0);
    PDFIconBadge* icon = new PDFIconBadge(QStringLiteral("ruler"), 24, 20, empty);
    icon->setColors(std::nullopt, PDFThemeColors::IconDefault);
    emptyLayout->addWidget(icon, 0, Qt::AlignHCenter);
    PDFAppParagraph* emptyText = PDFAppPageWidgets::createText(tr("Pick points on the page to measure. Points snap to the corners and edges of the drawing."),
                                                               PDFThemeFonts::getFont(QFont::Normal, 12), 15, PDFThemeColors::TextSecondary, empty);
    emptyText->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
    emptyText->setMinimumWidth(1);
    emptyLayout->addWidget(emptyText);
    emptyLayout->addStretch(1);
    m_emptyCard = empty;
    builder.addWidget(empty);
    builder.setFillsPanel(empty);
    updateList();
}

void PDFAppToolGUIMeasure::describeRightPanel(PDFAppToolPanelBuilder& builder)
{
    // The callouts of the state (471:7947, 473:90043, 473:90376, 473:90704, 473:89712), one at a time; the
    // refusal has the owner password (FM-2)
    m_callouts.clear();
    const std::vector<std::tuple<Callout, PDFAccentHue, QString, QString>> callouts = {
        { Callout::Partial, PDFAccentHue::Amber, QStringLiteral("alert-triangle"), QStringLiteral("measureCalloutPartial") },
        { Callout::Converted, PDFAccentHue::Green, QStringLiteral("check-circle"), QStringLiteral("measureCalloutConverted") },
        { Callout::Refusal, PDFAccentHue::Amber, QStringLiteral("alert-triangle"), QStringLiteral("measureCalloutRefusal") },
        { Callout::NotRemembered, PDFAccentHue::Amber, QStringLiteral("alert-triangle"), QStringLiteral("measureCalloutNotRemembered") },
        { Callout::Annotations, PDFAccentHue::Blue, QStringLiteral("info"), QStringLiteral("measureCalloutAnnotations") }
    };
    for (const auto& [callout, hue, iconName, objectName] : callouts)
    {
        PDFAppToolCallout* widget = builder.addCallout(hue, iconName, QString(), QString());
        widget->setObjectName(objectName);
        widget->setCompactStyle();
        widget->setVisible(false);
        m_callouts[callout] = widget;
    }
    if (PDFAppToolCallout* refusal = getCallout(Callout::Refusal))
    {
        PDFTextLink* ownerPassword = new PDFTextLink(tr("Enter Owner Password…"), refusal);
        ownerPassword->setObjectName(QStringLiteral("measureOwnerPassword"));
        connect(ownerPassword, &PDFTextLink::clicked, this, &PDFAppToolGUIMeasure::onOwnerPasswordClicked);
        refusal->getBodyLayout()->addWidget(ownerPassword, 0, Qt::AlignLeading);
        m_ownerPasswordLink = ownerPassword;
    }

    // Scale 471:5490: "1 mm = 50 mm", "Metric 1:50 · remembered for this document", Remember…, Default scale
    builder.addSection(tr("Scale"));
    PDFAppToolCard* scaleCard = new PDFAppToolCard(builder.getWidget());
    scaleCard->setObjectName(QStringLiteral("measureScaleCard"));
    QVBoxLayout* scaleLayout = scaleCard->getBodyLayout();
    scaleLayout->setSpacing(6);
    m_scaleRatio = new PDFLabel(QString(), PDFThemeFonts::Role::Title, PDFThemeColors::TextPrimary, scaleCard);
    m_scaleRatio->setObjectName(QStringLiteral("measureScaleRatio"));
    scaleLayout->addWidget(m_scaleRatio);
    m_scaleSubtitle = new PDFLabel(QString(), PDFThemeFonts::Role::CaptionRegular, PDFThemeColors::TextSecondary, scaleCard);
    m_scaleSubtitle->setObjectName(QStringLiteral("measureScaleSubtitle"));
    m_scaleSubtitle->setWordWrap(true);
    scaleLayout->addWidget(m_scaleSubtitle);
    m_viewportNote = new PDFLabel(tr("Measurements inside a viewport of the drawing use the scale of that viewport."), PDFThemeFonts::Role::CaptionRegular, PDFThemeColors::TextSecondary, scaleCard);
    m_viewportNote->setObjectName(QStringLiteral("measureViewportNote"));
    m_viewportNote->setWordWrap(true);
    scaleLayout->addWidget(m_viewportNote);
    if (PDFAppSettingControl* remember = builder.createValueControl(QString::fromLatin1(PDFDocumentToolMeasure::PARAMETER_REMEMBER_SCALE), scaleCard))
    {
        PDFAppToolGUIMeasureHelper::setCheckBoxTitle(remember, getModel(), PDFDocumentToolMeasure::PARAMETER_REMEMBER_SCALE);
        scaleLayout->addWidget(remember->getWidget());
    }
    QWidget* defaultRow = new QWidget(scaleCard);
    QHBoxLayout* defaultLayout = new QHBoxLayout(defaultRow);
    defaultLayout->setContentsMargins(0, 0, 0, 0);
    m_defaultScale = new PDFLabel(QString(), PDFThemeFonts::Role::Small, PDFThemeColors::TextSecondary, defaultRow);
    m_defaultScale->setObjectName(QStringLiteral("measureDefaultScale"));
    defaultLayout->addWidget(m_defaultScale, 1);
    PDFTextLink* change = new PDFTextLink(tr("Change…"), defaultRow);
    change->setObjectName(QStringLiteral("measureDefaultChange"));
    connect(change, &PDFTextLink::clicked, this, &PDFAppToolGUIMeasure::onChangeDefaultClicked);
    defaultLayout->addWidget(change);
    scaleLayout->addWidget(defaultRow);
    builder.addWidget(scaleCard);
    builder.endSection();

    // Units 473:8597: lengths, areas, angles
    builder.addSection(tr("Units"));
    PDFAppToolCard* unitsCard = new PDFAppToolCard(builder.getWidget());
    unitsCard->setObjectName(QStringLiteral("measureUnitsCard"));
    unitsCard->getBodyLayout()->setSpacing(PDFAppToolCard::FIELD_ROW_GAP);
    const std::vector<std::pair<const char*, QString>> units = { { PDFDocumentToolMeasure::PARAMETER_LENGTH_UNIT, tr("Lengths") },
                                                                 { PDFDocumentToolMeasure::PARAMETER_AREA_UNIT, tr("Areas") },
                                                                 { PDFDocumentToolMeasure::PARAMETER_ANGLE_UNIT, tr("Angles") } };
    for (const auto& [parameter, label] : units)
    {
        if (PDFAppSettingControl* control = builder.createValueControl(QString::fromLatin1(parameter), unitsCard))
        {
            unitsCard->getBodyLayout()->addWidget(PDFAppToolGUIMeasureHelper::createFieldRow(label, control->getWidget(), PDFAppToolGUIMeasureHelper::UNIT_FIELD_WIDTH, unitsCard));
        }
    }
    builder.addWidget(unitsCard);
    builder.endSection();

    // Labels 471:5490: the font, its size, bold, italic, the colors of the text and of the background with
    // their opacities, the note
    builder.addSection(tr("Labels"));
    PDFAppToolCard* labelsCard = new PDFAppToolCard(builder.getWidget());
    labelsCard->setObjectName(QStringLiteral("measureLabelsCard"));
    QVBoxLayout* labelsLayout = labelsCard->getBodyLayout();
    labelsLayout->setSpacing(PDFAppToolCard::FIELD_ROW_GAP);
    m_fontBox = new PDFComboBox(labelsCard);
    m_fontBox->setObjectName(QStringLiteral("measureFont"));
    // 471:5490 "Inter": the default (no chosen font, the data is empty) is named by the font of the theme, which the
    // canvas uses then; the family is not offered a second time (review RS7CFIGMA L5)
    const QString defaultFamily = PDFAppToolGUIMeasureHelper::getDefaultFontFamily();
    m_fontBox->addItem(defaultFamily, QString());
    for (const QString& family : QFontDatabase::families())
    {
        if (family.compare(defaultFamily, Qt::CaseInsensitive) != 0)
        {
            m_fontBox->addItem(family, family);
        }
    }
    connect(m_fontBox.data(), &QComboBox::currentIndexChanged, this, &PDFAppToolGUIMeasure::onFontChanged);
    labelsLayout->addWidget(PDFAppToolGUIMeasureHelper::createFieldRow(tr("Font"), m_fontBox, PDFAppToolGUIMeasureHelper::FONT_FIELD_WIDTH, labelsCard));
    if (PDFAppSettingControl* size = builder.createValueControl(QString::fromLatin1(PDFDocumentToolMeasure::PARAMETER_FONT_SIZE), labelsCard))
    {
        // 471:5490 "10 pt", "100 %": the units of the fields (the parameters of the core have none); a whole size
        // without decimals ("10")
        if (PDFDoubleSpinBox* spinBox = qobject_cast<PDFDoubleSpinBox*>(size->getWidget()))
        {
            spinBox->setDisplaySuffix(tr("pt"));
            spinBox->setTrailingZerosHidden(true);
        }
        labelsLayout->addWidget(PDFAppToolGUIMeasureHelper::createFieldRow(tr("Size"), size->getWidget(), PDFAppToolGUIMeasureHelper::SIZE_FIELD_WIDTH, labelsCard));
    }
    QWidget* styleRow = new QWidget(labelsCard);
    QHBoxLayout* styleLayout = new QHBoxLayout(styleRow);
    styleLayout->setContentsMargins(0, 0, 0, 0);
    styleLayout->setSpacing(16);
    for (const char* parameter : { PDFDocumentToolMeasure::PARAMETER_BOLD, PDFDocumentToolMeasure::PARAMETER_ITALIC })
    {
        if (PDFAppSettingControl* control = builder.createValueControl(QString::fromLatin1(parameter), styleRow))
        {
            PDFAppToolGUIMeasureHelper::setCheckBoxTitle(control, getModel(), parameter);
            styleLayout->addWidget(control->getWidget());
        }
    }
    styleLayout->addStretch(1);
    labelsLayout->addWidget(styleRow);

    const std::vector<std::tuple<QString, const char*, QPointer<PDFColorSwatchRow>*, QPointer<PDFLabel>*>> colors = {
        { tr("Text color"), PDFDocumentToolMeasure::PARAMETER_TEXT_OPACITY, &m_textColorRow, &m_textColorName },
        { tr("Background color"), PDFDocumentToolMeasure::PARAMETER_BACKGROUND_OPACITY, &m_backgroundColorRow, &m_backgroundColorName }
    };
    for (const auto& [title, opacityParameter, swatchRow, nameLabel] : colors)
    {
        QWidget* titleRow = new QWidget(labelsCard);
        QHBoxLayout* titleLayout = new QHBoxLayout(titleRow);
        titleLayout->setContentsMargins(0, 0, 0, 0);
        titleLayout->addWidget(new PDFLabel(title, PDFThemeFonts::Role::Small, PDFThemeColors::TextSecondary, titleRow), 1);
        PDFLabel* name = new PDFLabel(QString(), PDFThemeFonts::Role::CaptionRegular, PDFThemeColors::TextSecondary, titleRow);
        titleLayout->addWidget(name);
        *nameLabel = name;
        labelsLayout->addWidget(titleRow);

        PDFColorSwatchRow* row = new PDFColorSwatchRow(labelsCard);
        row->setOptions(PDFColorSwatchRow::Option::Inline | PDFColorSwatchRow::Option::White | PDFColorSwatchRow::Option::More);
        row->setAccessibleName(title);
        PDFAppColorPicker::attach(row);
        labelsLayout->addWidget(row, 0, Qt::AlignLeading);
        *swatchRow = row;

        if (PDFAppSettingControl* opacity = builder.createValueControl(QString::fromLatin1(opacityParameter), labelsCard))
        {
            if (PDFSpinBox* spinBox = qobject_cast<PDFSpinBox*>(opacity->getWidget()))
            {
                spinBox->setDisplaySuffix(tr("%"));
            }
            labelsLayout->addWidget(PDFAppToolGUIMeasureHelper::createFieldRow(tr("Opacity"), opacity->getWidget(), PDFAppToolGUIMeasureHelper::OPACITY_FIELD_WIDTH, labelsCard));
        }
    }
    m_textColorRow->setObjectName(QStringLiteral("measureTextColor"));
    m_backgroundColorRow->setObjectName(QStringLiteral("measureBackgroundColor"));
    connect(m_textColorRow.data(), &PDFColorSwatchRow::colorSelected, this, &PDFAppToolGUIMeasure::onTextColorSelected);
    connect(m_backgroundColorRow.data(), &PDFColorSwatchRow::colorSelected, this, &PDFAppToolGUIMeasure::onBackgroundColorSelected);

    PDFAppParagraph* note = PDFAppPageWidgets::createText(tr("Saved annotations use the standard annotation font. The colors apply to both."),
                                                          PDFThemeFonts::getFont(QFont::Normal, 11), 13, PDFThemeColors::TextMuted, labelsCard);
    note->setMinimumWidth(1);
    labelsLayout->addWidget(note);
    builder.addWidget(labelsCard);
    builder.endSection();
    updateRightPanel();
    updateLabelParts();
}

void PDFAppToolGUIMeasure::cancelAnnotationRead()
{
    if (m_readCancel)
    {
        m_readCancel->cancel();
        m_readCancel.reset();
    }
    if (QFutureWatcher<PDFAppMeasureAnnotationRead>* watcher = m_readWatcher.data())
    {
        // The job goes on to its end on its copies; its result goes nowhere
        watcher->disconnect(this);
        watcher->deleteLater();
        m_readWatcher = nullptr;
    }
    ++m_readGeneration;
}

PDFAppMeasureAnnotationRead PDFAppToolGUIMeasure::readAnnotations(PDFDocumentPointer document, std::shared_ptr<PDFAppCancelFlag> cancel)
{
    // The worker thread: the document is immutable and held by the copy of its pointer, the flag by its own
    PDFAppMeasureAnnotationRead read;
    if (!document)
    {
        return read;
    }

    // The measurement annotations of every page (also of another producer, FM-4; the reading limit of the vertices,
    // review RS7CCORE L6)
    std::vector<PDFInteger> pages(document->getCatalog()->getPageCount());
    std::iota(pages.begin(), pages.end(), PDFInteger(0));
    read.annotations = PDFMeasureAnnotationInfo::readPages(document.data(), pages, PDFAppToolGUIMeasureHelper::MAXIMUM_MEASUREMENTS, PDFMeasureAnnotationInfo::MAXIMUM_READ_POINT_COUNT,
                                                           &read.isTruncated, cancel.get());
    return read;
}

void PDFAppToolGUIMeasure::refreshDocument()
{
    const PDFAppDocument* document = getDocument();
    const PDFDocumentPointer pdf = document ? document->getDocument() : PDFDocumentPointer();

    // A new version cancels the read of the old one (review RS7CCORE L9: never in the GUI thread)
    cancelAnnotationRead();
    if (!pdf)
    {
        m_annotations.clear();
        m_isAnnotationsTruncated = false;
        return;
    }

    // The list keeps the annotations read before until the job of this version is done
    m_readCancel = std::make_shared<PDFAppCancelFlag>();
    QFutureWatcher<PDFAppMeasureAnnotationRead>* watcher = new QFutureWatcher<PDFAppMeasureAnnotationRead>(this);
    watcher->setProperty("pdf4qtGeneration", m_readGeneration);
    connect(watcher, &QFutureWatcher<PDFAppMeasureAnnotationRead>::finished, this, &PDFAppToolGUIMeasure::onAnnotationsRead);
    m_readWatcher = watcher;
    watcher->setFuture(QtConcurrent::run(&PDFAppToolGUIMeasure::readAnnotations, pdf, m_readCancel));

    // A temporary measurement of a page, which is gone (a page removed by Undo), goes with it
    const PDFInteger pageCount = PDFInteger(pdf->getCatalog()->getPageCount());
    std::vector<PDFAppMeasurement> measurements;
    measurements.reserve(m_measurements.size());
    for (PDFAppMeasurement& measurement : m_measurements)
    {
        if (measurement.pageIndex < pageCount)
        {
            measurements.push_back(std::move(measurement));
        }
    }
    m_measurements = std::move(measurements);
}

void PDFAppToolGUIMeasure::onAnnotationsRead()
{
    QFutureWatcher<PDFAppMeasureAnnotationRead>* watcher = m_readWatcher.data();
    if (!watcher || sender() != watcher || watcher->property("pdf4qtGeneration").toULongLong() != m_readGeneration)
    {
        return;
    }

    // Only the read of the last version is taken
    PDFAppMeasureAnnotationRead read = watcher->result();
    m_readWatcher = nullptr;
    m_readCancel.reset();
    watcher->deleteLater();
    m_annotations = std::move(read.annotations);
    m_isAnnotationsTruncated = read.isTruncated;

    bool isSelectedAnnotationKept = false;
    for (const PDFMeasureAnnotationInfo& annotation : m_annotations)
    {
        isSelectedAnnotationKept = isSelectedAnnotationKept || annotation.annotation == m_selectedAnnotation;
    }
    if (!isSelectedAnnotationKept)
    {
        m_selectedAnnotation = PDFObjectReference();
    }
    updateParts();

    // The slots of the status text may close the tab: the last calls, each one checked
    QPointer<PDFAppToolGUIMeasure> guard(this);
    updateConfirmState();
    if (guard)
    {
        updateStatusText();
    }
}

void PDFAppToolGUIMeasure::updateParts()
{
    updateToolbar();
    updateList();
    updateRightPanel();
    updateLabelParts();
    updateOverlay();
}

QString PDFAppToolGUIMeasure::getScaleButtonText() const
{
    switch (m_scaleSource)
    {
        case ScaleSource::Document:
            // 473:9500
            return tr("From the document");
        case ScaleSource::Default:
            if (m_documentScaleCount > 1)
            {
                // FM-5: the viewports of the document have several scales, each measurement inside of one has its own
                return tr("Several (from the document)");
            }
            break;
        default:
            break;
    }
    return m_scale.getName().isEmpty() ? PDFAppMeasureText::formatProportion(m_scale) : m_scale.getName();
}

void PDFAppToolGUIMeasure::updateToolbar()
{
    const PDFAppMeasureDrawKind drawKind = getDrawKind();
    if (QToolButton* button = getKindButton())
    {
        // K7.5: the kind stays on the last kind also while calibrating (the button Scale is active)
        button->setText(PDFAppMeasureText::getDrawKindTitle(drawKind));
        button->setToolTip(tr("Kind of measurement: %1").arg(button->text()));
        button->setIcon(PDFThemeIcons::getIcon(PDFAppMeasureText::getDrawKindIconName(drawKind)));
    }
    for (const auto& [kind, row] : m_kindRows)
    {
        if (row)
        {
            const QSignalBlocker blocker(row);
            row->setChecked(kind == drawKind);
        }
    }

    if (QToolButton* button = getScaleButton())
    {
        button->setText(getScaleButtonText());
        button->setToolTip(tr("Scale of the drawing: %1").arg(PDFAppMeasureText::formatRatio(m_scale)));
        button->setCheckable(true);
        button->setChecked(m_isCalibrating);
    }
    for (const QPointer<QWidget>& widget : m_scalePanelWidgets)
    {
        PDFTileRow* row = qobject_cast<PDFTileRow*>(widget.data());
        const QVariant index = row ? row->property(PDFAppToolGUIMeasureHelper::SCALE_PROPERTY) : QVariant();
        if (row && index.isValid() && index.toInt() >= 0 && index.toInt() < int(m_scalePresets.size()))
        {
            const QSignalBlocker blocker(row);
            row->setChecked(m_scalePresets[size_t(index.toInt())].hasSameProportion(m_scale));
        }
    }

    // The storage Annotations needs the annotations to be writable (FM-1, FM-2, FM-3)
    if (m_storageMenu)
    {
        if (PDFTileRow* row = m_storageMenu->getRow(QString::fromLatin1(PDFDocumentToolMeasure::STORAGE_ANNOTATIONS)))
        {
            const QString refusal = getWriteRefusalText();
            row->setEnabled(refusal.isEmpty());
            row->setToolTip(refusal);
        }
    }

    const bool hasTemporaries = !m_measurements.empty();
    if (m_showButton)
    {
        const QSignalBlocker blocker(m_showButton.data());
        m_showButton->setChecked(m_isShown);
        m_showButton->setEnabled(hasTemporaries);
    }
    if (m_crossButton)
    {
        const QSignalBlocker blocker(m_crossButton.data());
        m_crossButton->setChecked(m_overlay && m_overlay->isCrossShown());
    }
    if (m_clearButton)
    {
        m_clearButton->setEnabled(hasTemporaries);
    }
    if (m_exportButton)
    {
        // The existing measurement annotations are exported too
        m_exportButton->setEnabled(hasTemporaries || !m_annotations.empty());
    }
}

void PDFAppToolGUIMeasure::updateList()
{
    if (!m_listModel)
    {
        return;
    }

    const PDFAppDocument* document = getDocument();
    const PDFDocumentPointer pdf = document ? document->getDocument() : PDFDocumentPointer();
    const PDFMeasureDisplayUnits units = getUnits();

    // The rows by their pages: the annotations of a page first, then its temporary measurements (as the CSV)
    std::vector<std::pair<PDFInteger, PDFAppMeasureListModel::Row>> rows;
    for (const PDFMeasureAnnotationInfo& annotation : m_annotations)
    {
        PDFAppMeasureListModel::Row row;
        row.isAnnotation = true;
        row.annotation = annotation.annotation;
        row.pageIndex = annotation.pageIndex;
        row.title = annotation.text;
        const QString scale = annotation.scale.isValid() ? PDFAppMeasureText::formatProportion(annotation.scale) : annotation.ratio;
        row.description = scale.isEmpty() ? tr("%1 · page %2").arg(PDFMeasureGeometry::getKindTitle(annotation.kind), PDFAppLocale::formatNumber(annotation.pageIndex + 1))
                                          : tr("%1 · page %2 · %3").arg(PDFMeasureGeometry::getKindTitle(annotation.kind), PDFAppLocale::formatNumber(annotation.pageIndex + 1), scale);
        row.iconName = PDFAppMeasureText::getKindIconName(annotation.kind);
        rows.emplace_back(annotation.pageIndex, std::move(row));
    }
    for (const PDFAppMeasurement& measurement : m_measurements)
    {
        PDFAppMeasureListModel::Row row;
        row.id = measurement.id;
        row.pageIndex = measurement.pageIndex;
        PDFMeasureScale effectiveScale;
        row.title = PDFAppMeasureText::getText(pdf.data(), measurement.pageIndex, measurement.kind, measurement.points, m_scale, units, &effectiveScale);
        row.description = tr("%1 · page %2 · %3").arg(PDFMeasureGeometry::getKindTitle(measurement.kind), PDFAppLocale::formatNumber(measurement.pageIndex + 1),
                                                      PDFAppMeasureText::formatProportion(effectiveScale));
        row.iconName = PDFAppMeasureText::getDrawKindIconName(measurement.drawKind);
        rows.emplace_back(measurement.pageIndex, std::move(row));
    }
    std::stable_sort(rows.begin(), rows.end(), &PDFAppToolGUIMeasureHelper::isRowBefore);

    std::vector<PDFAppMeasureListModel::Row> modelRows;
    modelRows.reserve(rows.size());
    for (auto& row : rows)
    {
        modelRows.push_back(std::move(row.second));
    }
    m_listModel->setRows(std::move(modelRows));
    if (m_listView)
    {
        // The height of the list follows its rows (PDFAppMeasureListView)
        m_listView->updateGeometry();
    }

    const int selectedRow = m_selectedId ? m_listModel->findTemporary(*m_selectedId) : (m_selectedAnnotation.isValid() ? m_listModel->findAnnotation(m_selectedAnnotation) : -1);
    m_listModel->setSelectedRow(selectedRow);
    if (m_listView && m_listView->selectionModel())
    {
        const QSignalBlocker blocker(m_listView->selectionModel());
        if (selectedRow >= 0)
        {
            m_listView->setCurrentIndex(m_listModel->index(selectedRow));
        }
        else
        {
            m_listView->selectionModel()->clear();
        }
    }

    // 471:5975 "6 temporary", 471:7947 "0 temporary · 6 stored as annotations"
    const bool hasRows = m_listModel->rowCount() > 0;
    if (m_leftHeader)
    {
        QString subtitle;
        if (!hasRows)
        {
            subtitle = tr("None yet");
        }
        else if (m_annotations.empty())
        {
            subtitle = tr("%Ln temporary", nullptr, int(m_measurements.size()));
        }
        else
        {
            subtitle = tr("%1 · %2").arg(tr("%Ln temporary", nullptr, int(m_measurements.size())), tr("%Ln stored as annotations", nullptr, int(m_annotations.size())));
        }
        m_leftHeader->setTexts(tr("Measurements"), subtitle);
    }
    if (m_listCard)
    {
        m_listCard->setVisible(hasRows);
    }
    if (m_emptyCard)
    {
        m_emptyCard->setVisible(!hasRows);
    }
    if (m_listHint)
    {
        m_listHint->setVisible(!m_measurements.empty());
    }
}

void PDFAppToolGUIMeasure::updateRightPanel()
{
    if (!m_callouts.empty())
    {
        const PDFAppDocument* document = getDocument();
        const PDFDocumentPointer pdf = document ? document->getDocument() : PDFDocumentPointer();
        const PDFAppToolParameterModel* model = getModel();
        const bool isAnnotationStorage = model && model->getValue(QString::fromLatin1(PDFDocumentToolMeasure::PARAMETER_STORAGE)).toString() == QLatin1String(PDFDocumentToolMeasure::STORAGE_ANNOTATIONS);
        const QString refusal = getWriteRefusalText();
        const bool isPermissionMissing = pdf && !PDFAppDocumentPermissions::isAllowed(pdf.data(), PDFAppPermissionRequest::create(PDFAppPermissionAction::Annotate));
        const bool isRemembering = !model || model->getValue(QString::fromLatin1(PDFDocumentToolMeasure::PARAMETER_REMEMBER_SCALE)).toBool();

        std::optional<Callout> shown;
        if (m_notice == Notice::PartiallyConverted)
        {
            // 473:90376
            shown = Callout::Partial;
            const int failedCount = m_noticeTotal - m_noticeStored;
            getCallout(Callout::Partial)->setTexts(failedCount == 1 ? tr("%Ln measurement could not be stored", nullptr, failedCount) : tr("%Ln measurements could not be stored", nullptr, failedCount),
                                                   tr("Some of the measurements could not be stored in the document as annotations. They are kept as temporary measurements."));
        }
        else if (m_notice == Notice::Converted)
        {
            // 473:90043
            shown = Callout::Converted;
            getCallout(Callout::Converted)->setTexts(m_noticeStored == 1 ? tr("%Ln measurement saved as an annotation", nullptr, m_noticeStored) : tr("%Ln measurements saved as annotations", nullptr, m_noticeStored),
                                                     tr("They are part of the document now and other applications can read them. The document has unsaved changes; Undo takes the annotations back."));
        }
        else if (!refusal.isEmpty() && pdf)
        {
            // 473:90704 (FM-2 the owner password), a locked or a certified document (2.10)
            shown = Callout::Refusal;
            getCallout(Callout::Refusal)->setTexts(refusal, tr("Measurements stay temporary. You can still export them to a CSV file."));
        }
        else if (isRemembering && pdf && !isDocumentIdentifiable())
        {
            // 473:89712
            shown = Callout::NotRemembered;
            getCallout(Callout::NotRemembered)->setTexts(tr("The scale cannot be remembered for this document"),
                                                         tr("The document cannot be identified – it has no file and no identifier. Its scale is used until you close it."));
        }
        else if (isAnnotationStorage)
        {
            // 471:7947
            shown = Callout::Annotations;
            getCallout(Callout::Annotations)->setTexts(tr("Measurements go straight into the document"),
                                                       tr("Each one is added as a measurement annotation. The document has unsaved changes and Undo removes the last one."));
        }

        for (const auto& [callout, widget] : m_callouts)
        {
            if (widget)
            {
                widget->setVisible(shown == callout);
            }
        }
        if (m_ownerPasswordLink)
        {
            m_ownerPasswordLink->setVisible(isPermissionMissing && pdf && PDFAppDocumentPermissions::canUnlock(pdf.data()) && !document->isLocked());
        }
    }

    if (m_scaleRatio)
    {
        m_scaleRatio->setText(PDFAppMeasureText::formatRatio(m_scale));
    }
    if (m_scaleSubtitle)
    {
        // "Metric 1:50 · remembered for this document" (471:5490), "From the document · defined by the document"
        // (473:9500), "Metric 1:50 · not remembered" (473:89712)
        const QString name = m_scale.getName().isEmpty() ? PDFAppMeasureText::formatProportion(m_scale) : m_scale.getName();
        QString subtitle;
        switch (m_scaleSource)
        {
            case ScaleSource::Remembered:
                subtitle = tr("%1 · remembered for this document").arg(name);
                break;
            case ScaleSource::Document:
                subtitle = tr("From the document · defined by the document");
                break;
            case ScaleSource::Default:
                subtitle = tr("%1 · the default scale").arg(name);
                break;
            case ScaleSource::Session:
                subtitle = tr("%1 · not remembered").arg(name);
                break;
        }
        m_scaleSubtitle->setText(subtitle);
    }
    if (m_viewportNote)
    {
        m_viewportNote->setVisible(m_scaleSource == ScaleSource::Document || m_documentScaleCount > 0);
    }
    if (m_defaultScale)
    {
        PDFAppContext* context = getContext();
        PDFMeasureScaleRepository repository(context ? context->getDatabase() : nullptr);
        m_defaultScale->setText(tr("Default scale: %1").arg(repository.getDefaultScale().getDisplayName()));
    }
}

void PDFAppToolGUIMeasure::updateLabelParts()
{
    const PDFAppToolParameterModel* model = getModel();
    if (!model)
    {
        return;
    }

    const PDFMeasureAnnotationStyle style = PDFDocumentToolMeasure::getStyle(model->getValues(), QString());
    if (m_fontBox)
    {
        const QSignalBlocker blocker(m_fontBox.data());
        const int index = m_fontBox->findData(style.fontFamily);
        m_fontBox->setCurrentIndex(index >= 0 ? index : 0);
    }
    if (m_textColorRow)
    {
        m_textColorRow->setColor(QColor(style.textColor.rgb()));
    }
    if (m_backgroundColorRow)
    {
        m_backgroundColorRow->setColor(QColor(style.backgroundColor.rgb()));
    }

    // 471:5490 "Black", "Black, 10 %"
    if (m_textColorName)
    {
        const int opacity = PDFAppToolGUIMeasureHelper::getOpacity(model, PDFDocumentToolMeasure::PARAMETER_TEXT_OPACITY, PDFDocumentToolMeasure::DEFAULT_TEXT_OPACITY);
        const QString name = PDFAppToolGUIMeasureHelper::getColorName(style.textColor);
        m_textColorName->setText(opacity == 100 ? name : tr("%1, %2 %").arg(name, PDFAppLocale::formatNumber(opacity)));
    }
    if (m_backgroundColorName)
    {
        const int opacity = PDFAppToolGUIMeasureHelper::getOpacity(model, PDFDocumentToolMeasure::PARAMETER_BACKGROUND_OPACITY, PDFDocumentToolMeasure::DEFAULT_BACKGROUND_OPACITY);
        const QString name = PDFAppToolGUIMeasureHelper::getColorName(style.backgroundColor);
        m_backgroundColorName->setText(opacity == 100 ? name : tr("%1, %2 %").arg(name, PDFAppLocale::formatNumber(opacity)));
    }
}

PDFAppMeasureLabelStyle PDFAppToolGUIMeasure::getLabelStyle() const
{
    const PDFAppToolParameterModel* model = getModel();
    const PDFMeasureAnnotationStyle style = PDFDocumentToolMeasure::getStyle(model ? model->getValues() : QVariantMap(), QString());
    PDFAppMeasureLabelStyle labelStyle;
    labelStyle.font = style.fontFamily.isEmpty() ? PDFThemeFonts::getFont(QFont::Normal, 12) : QFont(style.fontFamily);
    labelStyle.font.setPointSizeF(style.fontSize);
    labelStyle.font.setBold(style.isBold);
    labelStyle.font.setItalic(style.isItalic);
    labelStyle.textColor = style.textColor;
    labelStyle.backgroundColor = style.backgroundColor;
    return labelStyle;
}

void PDFAppToolGUIMeasure::updateOverlay()
{
    PDFAppMeasureOverlay* overlay = m_overlay.data();
    if (!overlay)
    {
        return;
    }

    const PDFAppDocument* document = getDocument();
    const PDFDocumentPointer pdf = document ? document->getDocument() : PDFDocumentPointer();
    const PDFMeasureDisplayUnits units = getUnits();
    overlay->setMeasureSettings(m_scale, units);
    overlay->setLabelStyle(getLabelStyle());
    overlay->setShown(m_isShown);

    std::vector<PDFAppMeasureShape> shapes;
    shapes.reserve(m_measurements.size() + 1);
    for (const PDFAppMeasurement& measurement : m_measurements)
    {
        PDFAppMeasureShape shape;
        shape.pageIndex = measurement.pageIndex;
        shape.kind = measurement.kind;
        shape.points = measurement.points;
        shape.text = PDFAppMeasureText::getText(pdf.data(), measurement.pageIndex, measurement.kind, measurement.points, m_scale, units, nullptr);
        shape.isSelected = m_selectedId && *m_selectedId == measurement.id;
        shapes.push_back(std::move(shape));
    }
    for (const PDFMeasureAnnotationInfo& annotation : m_annotations)
    {
        if (annotation.annotation == m_selectedAnnotation)
        {
            PDFAppMeasureShape shape;
            shape.pageIndex = annotation.pageIndex;
            shape.kind = annotation.kind;
            shape.points = annotation.points;
            shape.isSelected = true;
            shape.isAnnotation = true;
            shapes.push_back(std::move(shape));
        }
    }
    overlay->setShapes(std::move(shapes));
    updateCanvasCallout();
}

void PDFAppToolGUIMeasure::updateInput()
{
    if (PDFAppMeasureOverlay* overlay = m_overlay.data())
    {
        std::optional<PDFAppMeasureDrawKind> input;
        if (m_isCalibrating)
        {
            input = PDFAppMeasureDrawKind::Calibrate;
        }
        else if (m_isDrawing)
        {
            input = getDrawKind();
        }
        overlay->setInput(input);
    }
    updateCanvasCallout();
}

void PDFAppToolGUIMeasure::updateCanvasCallout()
{
    PDFAppMeasureOverlay* overlay = m_overlay.data();
    if (!overlay)
    {
        return;
    }

    // 471:5490: the empty state - drawing, no measurement yet (the overlay hides it from the first point on); the
    // annotations being read count as measurements (no callout flashes up, while the list waits for them)
    const PDFAppDocument* document = getDocument();
    const bool isEmptyState = m_isDrawing && !m_isCalibrating && m_measurements.empty() && m_annotations.empty() && !isReadingAnnotations() &&
                              document && document->getPageCount() > 0;
    QString title;
    if (isEmptyState)
    {
        switch (getDrawKind())
        {
            case PDFAppMeasureDrawKind::Horizontal:
                title = tr("Pick two points to measure a horizontal distance");
                break;
            case PDFAppMeasureDrawKind::Vertical:
                title = tr("Pick two points to measure a vertical distance");
                break;
            case PDFAppMeasureDrawKind::Linear:
            case PDFAppMeasureDrawKind::Calibrate:
                // 471:5490
                title = tr("Pick two points to measure a distance");
                break;
            case PDFAppMeasureDrawKind::Perimeter:
                title = tr("Click the corners of a shape to measure its perimeter");
                break;
            case PDFAppMeasureDrawKind::RectanglePerimeter:
                title = tr("Pick two opposite corners to measure the perimeter of a rectangle");
                break;
            case PDFAppMeasureDrawKind::Area:
                title = tr("Click the corners of a shape to measure its area");
                break;
            case PDFAppMeasureDrawKind::RectangleArea:
                title = tr("Pick two opposite corners to measure the area of a rectangle");
                break;
            case PDFAppMeasureDrawKind::Angle:
                title = tr("Pick three points to measure an angle");
                break;
        }
    }
    overlay->setCallout(title, isEmptyState ? tr("Points snap to the corners and edges of the drawing. Choose another kind of measurement in the toolbar.") : QString());
}

// ------------------------------------------------------------------ slots

void PDFAppToolGUIMeasure::onKindRowClicked()
{
    QObject* row = sender();
    if (!row)
    {
        return;
    }

    // The panel closes first: the change of the parameter may start anything
    const PDFAppMeasureDrawKind kind = PDFAppMeasureDrawKind(row->property(PDFAppToolGUIMeasureHelper::KIND_PROPERTY).toInt());
    if (m_kindPanel)
    {
        m_kindPanel->close();
    }
    setDrawKind(kind);
}

void PDFAppToolGUIMeasure::onScaleRowClicked()
{
    QObject* row = sender();
    if (!row)
    {
        return;
    }

    const int index = row->property(PDFAppToolGUIMeasureHelper::SCALE_PROPERTY).toInt();
    if (m_scalePanel)
    {
        m_scalePanel->close();
    }
    if (index >= 0 && index < int(m_scalePresets.size()))
    {
        setScale(m_scalePresets[size_t(index)]);
    }
}

void PDFAppToolGUIMeasure::onCalibrateClicked()
{
    if (m_scalePanel)
    {
        m_scalePanel->close();
    }
    startCalibration();
}

void PDFAppToolGUIMeasure::onCustomScaleClicked()
{
    if (m_scalePanel)
    {
        m_scalePanel->close();
    }
    openCustomScaleDialog();
}

void PDFAppToolGUIMeasure::onManagePresetsClicked()
{
    if (m_scalePanel)
    {
        m_scalePanel->close();
    }
    openPresetsDialog();
}

void PDFAppToolGUIMeasure::onShowClicked()
{
    m_isShown = !m_isShown;
    updateToolbar();
    updateOverlay();
    updateStatusText();
}

void PDFAppToolGUIMeasure::onCrossClicked()
{
    if (PDFAppMeasureOverlay* overlay = m_overlay.data())
    {
        overlay->setCrossShown(!overlay->isCrossShown());
    }
    updateToolbar();
}

void PDFAppToolGUIMeasure::onClearClicked()
{
    clearMeasurements();
}

void PDFAppToolGUIMeasure::onExportClicked()
{
    exportCsv();
}

void PDFAppToolGUIMeasure::onChangeDefaultClicked()
{
    openDefaultScaleDialog();
}

void PDFAppToolGUIMeasure::onOwnerPasswordClicked()
{
    // FM-2: the owner password dialog of the document (no way around the permission)
    PDFAppDocument* document = getDocument();
    if (document)
    {
        PDFAppDocumentPermissions::request(document, PDFAppPermissionRequest::create(PDFAppPermissionAction::Annotate), PDFAppToolGUIMeasureHelper::getDialogParent(this));
    }
}

void PDFAppToolGUIMeasure::onFontChanged(int index)
{
    if (PDFAppToolParameterModel* model = getModel(); model && m_fontBox && index >= 0)
    {
        model->setValue(QString::fromLatin1(PDFDocumentToolMeasure::PARAMETER_FONT_FAMILY), m_fontBox->itemData(index).toString());
    }
}

void PDFAppToolGUIMeasure::onTextColorSelected(const QColor& color)
{
    if (PDFAppToolParameterModel* model = getModel(); model && color.isValid())
    {
        model->setValue(QString::fromLatin1(PDFDocumentToolMeasure::PARAMETER_TEXT_COLOR), color);
    }
}

void PDFAppToolGUIMeasure::onBackgroundColorSelected(const QColor& color)
{
    if (PDFAppToolParameterModel* model = getModel(); model && color.isValid())
    {
        model->setValue(QString::fromLatin1(PDFDocumentToolMeasure::PARAMETER_BACKGROUND_COLOR), color);
    }
}

void PDFAppToolGUIMeasure::onRowChanged(const QModelIndex& current)
{
    // Qt reads the selection model after it has emitted currentChanged, and selectRow runs code of the application
    // (the page of the view, the status text), which may delete the tab with the list: the selection is queued
    // (the lifetime sweep reviewmeasure: statusTextChanged deleted the tab inside QItemSelectionModel::setCurrentIndex)
    Q_UNUSED(current);
    QMetaObject::invokeMethod(this, &PDFAppToolGUIMeasure::onQueuedRowChanged, Qt::QueuedConnection);
}

void PDFAppToolGUIMeasure::onQueuedRowChanged()
{
    // The current row of now (the list may have changed since the signal)
    const QModelIndex current = m_listView && m_listView->selectionModel() ? m_listView->selectionModel()->currentIndex() : QModelIndex();
    selectRow(current.isValid() ? current.row() : -1);
}

void PDFAppToolGUIMeasure::onRowActionTriggered(const QModelIndex& index, int action)
{
    const PDFAppMeasureListModel::Row* row = m_listModel ? m_listModel->getRow(index.row()) : nullptr;
    if (action == PDFAppMeasureListDelegate::RemoveAction && row && !row->isAnnotation)
    {
        // The delegate is the sender: the removal of the row resets the model only (the delegate lives)
        removeMeasurement(row->id);
    }
}

void PDFAppToolGUIMeasure::onMeasurementPicked(pdf::PDFInteger pageIndex, pdf::PDFMeasureKind kind, std::vector<QPointF> points)
{
    addMeasurement(pageIndex, kind, points, getDrawKind());
}

void PDFAppToolGUIMeasure::onCalibrationPicked(pdf::PDFInteger pageIndex, pdf::PDFReal length)
{
    Q_UNUSED(pageIndex);
    if (length > 0.0)
    {
        openCalibrationDialog(length);
        return;
    }

    // 473:6755: the scale cannot be calculated - Cancel | Pick Again
    if (m_zeroLengthDialog)
    {
        m_zeroLengthDialog->raise();
        return;
    }
    QWidget* parent = PDFAppToolGUIMeasureHelper::getDialogParent(this);
    PDFMessageBox* dialog = new PDFMessageBox(PDFThemeIcons::Badge::Warning, tr("The scale cannot be calculated"),
                                              tr("The picked line has a zero length. Pick two different points of a known distance."), QDialogButtonBox::NoButton, parent);
    dialog->setObjectName(QStringLiteral("measureZeroLengthDialog"));
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    QPushButton* cancel = dialog->addActionButton(tr("Cancel"), PDFMessageBox::ActionPosition::Trailing, false);
    cancel->setObjectName(QStringLiteral("measureZeroLengthCancel"));
    connect(cancel, &QPushButton::clicked, dialog, &QDialog::reject);
    QPushButton* again = dialog->addActionButton(tr("Pick Again"), PDFMessageBox::ActionPosition::Trailing, true);
    again->setObjectName(QStringLiteral("measureZeroLengthAgain"));
    again->setIcon(PDFThemeIcons::getIcon(QStringLiteral("calibrate")));
    connect(again, &QPushButton::clicked, this, &PDFAppToolGUIMeasure::onZeroLengthPickAgain);
    // Queued: QDialog emits rejected inside of its done() and goes on with the dialog after it
    connect(dialog, &QDialog::rejected, this, &PDFAppToolGUIMeasure::onCalibrationRejected, Qt::QueuedConnection);
    m_zeroLengthDialog = dialog;
    m_isCalibrating = false;
    updateInput();
    updateToolbar();
    PDFAppMainWindow::prepareDialog(dialog, parent);
    QPointer<PDFAppToolGUIMeasure> guard(this);
    dialog->open();
    if (guard)
    {
        // 473:6755: the status bar says why (its slots may delete this GUI - the last call)
        updateStatusText();
    }
}

void PDFAppToolGUIMeasure::onDocumentLockChanged()
{
    updateParts();

    QPointer<PDFAppToolGUIMeasure> guard(this);
    updateConfirmState();
    if (guard)
    {
        updateStatusText();
    }
}

void PDFAppToolGUIMeasure::onOverlayDrawingChanged()
{
    updateToolbar();
    updateStatusText();
}

}   // namespace pdf

#include "moc_pdfapptoolgui_measure.cpp"
