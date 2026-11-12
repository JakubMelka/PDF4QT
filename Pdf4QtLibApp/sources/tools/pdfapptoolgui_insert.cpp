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

#include "pdfapptoolgui_insert.h"

#include "pdfappcontext.h"
#include "pdfappdocument.h"
#include "pdfappdocumentpanels_common.h"
#include "pdfappdocumentregistry.h"
#include "pdfappdocumenttoolbar.h"
#include "pdfappdocumentview.h"
#include "pdfappfiledrop.h"
#include "pdfappimagepagepreview.h"
#include "pdfapplocale.h"
#include "pdfappmainwindow.h"
#include "pdfapppagelayout.h"
#include "pdfapppassworddialog.h"
#include "pdfappsettingcontrol.h"
#include "pdfapptooldocumentchooser.h"
#include "pdfapptoolgui_blank.h"
#include "pdfapptoolgui_geometry.h"
#include "pdfapptoolgui_images.h"
#include "pdfapptoolhost.h"
#include "pdfapptoolinputrow.h"
#include "pdfapptoolinsertstrip.h"
#include "pdfapptoolmergeview.h"
#include "pdfapptoolpanelbuilder.h"
#include "pdfapptoolpanelparts.h"
#include "pdfapptoolpanelpopup.h"
#include "pdfapptoolparametermodel.h"
#include "pdfapptooltoolbar.h"
#include "pdfdocumenttoolguiregistry.h"

#include "pdfcatalog.h"
#include "pdfcomponentbadges.h"
#include "pdfcomponentbuttons.h"
#include "pdfcomponentdelegates.h"
#include "pdfcomponentpanels.h"
#include "pdfcomponentrows.h"
#include "pdfdocumentpageoperations.h"
#include "pdfdocumenttool_insert.h"
#include "pdfdocumenttoolcatalog.h"
#include "pdfdocumenttoolregistry.h"
#include "pdfpagerangelist.h"
#include "pdfpagesizepresets.h"
#include "pdfstylepainter.h"
#include "pdfstylepainterguard.h"
#include "pdfthemefonts.h"
#include "pdfthemeiconengine.h"
#include "pdfthememanager.h"

#include <QAction>
#include <QBuffer>
#include <QButtonGroup>
#include <QClipboard>
#include <QDropEvent>
#include <QGridLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QImageReader>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>

namespace pdf
{

/// Names, keys and texts shared by Insert Pages and Add Image as Page
class PDFAppInsertToolGUIHelper
{
public:
    PDFAppInsertToolGUIHelper() = delete;

    /// Property of a row of a panel or a button: its key (a position, a document, a size)
    static constexpr const char* ROW_KEY = "pdf4qtInsertKey";

    static QString name(const char* parameter) { return QString::fromLatin1(parameter); }

    /// The reserved input id of the blank pages of Insert ("blank")
    static QString blankId() { return QString::fromLatin1(PDFDocumentToolInsertBase::BLANK_INPUT_ID); }

    /// Ids of the text of "inputs" (the reserved id blank included)
    static QStringList splitIds(const QString& text)
    {
        QStringList ids;
        for (const QString& id : text.split(QLatin1Char(','), Qt::SkipEmptyParts))
        {
            const QString trimmed = id.trimmed();
            if (!trimmed.isEmpty() && !ids.contains(trimmed))
            {
                ids << trimmed;
            }
        }
        return ids;
    }

    static int toInt(PDFInteger value)
    {
        return int(qBound(PDFInteger(0), value, PDFInteger(std::numeric_limits<int>::max())));
    }

    /// "1 page", "6 pages" (both texts: English has no plural forms without its translation)
    static QString pages(PDFInteger count)
    {
        const int value = toInt(count);
        return value == 1 ? PDFAppInsertToolGUIBase::tr("%Ln page", nullptr, value) : PDFAppInsertToolGUIBase::tr("%Ln pages", nullptr, value);
    }

    static QString files(int count)
    {
        return count == 1 ? PDFAppInsertToolGUIBase::tr("%Ln file", nullptr, count) : PDFAppInsertToolGUIBase::tr("%Ln files", nullptr, count);
    }

    static QString images(int count)
    {
        return count == 1 ? PDFAppInsertToolGUIBase::tr("%Ln image", nullptr, count) : PDFAppInsertToolGUIBase::tr("%Ln images", nullptr, count);
    }

    /// The keys of the frames (388:16725, 388:19607): PDF files Ctrl+I, images Ctrl+Alt+I, blank
    /// pages Ctrl+Shift+I, the paste of the platform
    static QKeySequence getPdfKeys() { return QKeySequence(Qt::CTRL | Qt::Key_I); }
    static QKeySequence getImageKeys() { return QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_I); }
    static QKeySequence getBlankKeys() { return QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_I); }
    static QKeySequence getPasteKeys() { return QKeySequence(QKeySequence::Paste); }
    static QString getKeysText(const QKeySequence& keys) { return keys.toString(QKeySequence::NativeText); }

    /// "48 → 54 pages", "48 pages"
    static QString getDocumentChangeText(PDFInteger before, PDFInteger after)
    {
        if (before == after)
        {
            return pages(after);
        }
        return PDFAppInsertToolGUIBase::tr("%1 → %2").arg(PDFAppLocale::formatNumber(before), pages(after));
    }

    /// The text of an ordered page list ("1-5, 7, 9-7"), which PDFPageRangeList::parse reads back
    static QString createOrderedRangeText(const std::vector<PDFInteger>& pageIndices)
    {
        QStringList parts;
        size_t i = 0;
        while (i < pageIndices.size())
        {
            size_t j = i;
            const PDFInteger step = i + 1 < pageIndices.size() && std::abs(pageIndices[i + 1] - pageIndices[i]) == 1 ? pageIndices[i + 1] - pageIndices[i] : 0;
            while (step != 0 && j + 1 < pageIndices.size() && pageIndices[j + 1] - pageIndices[j] == step)
            {
                ++j;
            }
            if (j > i)
            {
                parts << QStringLiteral("%1-%2").arg(pageIndices[i] + 1).arg(pageIndices[j] + 1);
            }
            else
            {
                parts << QString::number(pageIndices[i] + 1);
            }
            i = j + 1;
        }
        return parts.join(QStringLiteral(", "));
    }

    /// The value of a source of "sourcePages" means all its pages (empty or "all")
    static bool isAllPages(const QString& value)
    {
        const QString trimmed = value.trimmed();
        return trimmed.isEmpty() || trimmed == QLatin1String("all");
    }

    /// The turn of a value of "turns" ("90")
    static PageRotation getTurn(const QString& value)
    {
        const QString trimmed = value.trimmed();
        if (trimmed == QLatin1String("90"))
        {
            return PageRotation::Rotate90;
        }
        if (trimmed == QLatin1String("180"))
        {
            return PageRotation::Rotate180;
        }
        if (trimmed == QLatin1String("270"))
        {
            return PageRotation::Rotate270;
        }
        return PageRotation::None;
    }

    static QString getTurnValue(PageRotation turn)
    {
        switch (turn)
        {
            case PageRotation::Rotate90:
                return QStringLiteral("90");
            case PageRotation::Rotate180:
                return QStringLiteral("180");
            case PageRotation::Rotate270:
                return QStringLiteral("270");
            default:
                return QStringLiteral("0");
        }
    }

    static int getTurnAngle(PageRotation turn)
    {
        switch (turn)
        {
            case PageRotation::Rotate90:
                return 90;
            case PageRotation::Rotate180:
                return 180;
            case PageRotation::Rotate270:
                return 270;
            default:
                return 0;
        }
    }

    /// "A4, 210 × 297 mm", "100 × 120 mm" of a page in points
    static QString getSizeDescription(const QSizeF& size)
    {
        if (size.isEmpty())
        {
            return QString();
        }
        const QString millimeters = PDFAppInsertToolGUIBase::tr("%1 × %2 mm").arg(PDFAppBlankToolGUI::formatMillimeters(PDFPageSizePresets::getMillimeters(size.width())),
                                                                                  PDFAppBlankToolGUI::formatMillimeters(PDFPageSizePresets::getMillimeters(size.height())));
        const QString preset = PDFAppToolGUIImages::getPresetName(size);
        return preset.isEmpty() ? millimeters : PDFAppInsertToolGUIBase::tr("%1, %2").arg(preset, millimeters);
    }

    /// A row of a label and a control in the grid of a card (card 388:16811: rows of 42, the
    /// label Regular 13 text/secondary, the control 140 at the trailing edge)
    static PDFLabel* addFieldRow(QGridLayout* grid, int row, const QString& label, QWidget* control, QWidget* parent)
    {
        PDFLabel* labelWidget = PDFAppPageWidgets::createLabel(label, QFont::Normal, 13, PDFThemeColors::TextSecondary, parent);
        labelWidget->setBuddy(control);
        grid->addWidget(labelWidget, row, 0);
        if (control)
        {
            control->setFixedWidth(FIELD_WIDTH);
            control->setAccessibleName(label);
            grid->addWidget(control, row, 1);
        }
        return labelWidget;
    }

    /// Width of a control of a card (388:16811)
    static constexpr int FIELD_WIDTH = 140;

    /// The tiles of the blank pages (388:16811: the slot 161 x 220 of the tile 173 x 264, the
    /// slot of the size Small 73 x 100 scaled)
    static constexpr double BLANK_TILE_SCALE = 2.2;

    /// The buttons of the card of a source, which cannot be read (388:17789)
    static inline const QString ACTION_PASSWORD = QStringLiteral("password");
    static inline const QString ACTION_REMOVE = QStringLiteral("remove");

    /// Gap of the rows of the cards of Summary (388:16093: rows of 28, a pitch of 36)
    static constexpr int KEY_VALUE_ROW_GAP = 8;
};

/// The card of the drop zone (398:29210): bg/surface with the dashed border/strong
class PDFAppInsertEmptyCard : public QWidget
{
public:
    explicit PDFAppInsertEmptyCard(QWidget* parent) :
        QWidget(parent)
    {

    }

protected:
    virtual void paintEvent(QPaintEvent* event) override
    {
        Q_UNUSED(event);

        QPainter painter(this);
        const PDFStylePainterGuard guard(&painter);
        painter.setRenderHint(QPainter::Antialiasing, true);
        const QRectF shape = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        PDFStylePainter::fill(&painter, shape, PDFStylePainter::Radii(PDFAppInsertEmptyState::RADIUS), PDFThemeColors::BgSurface);
        QPen pen(PDFStylePainter::color(PDFThemeColors::BorderStrong), 1.0);
        pen.setDashPattern({ 4.0, 3.0 });
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(PDFStylePainter::roundedPath(shape, PDFStylePainter::Radii(PDFAppInsertEmptyState::RADIUS)));
    }
};

// ------------------------------------------------------------- registration

void PDFAppToolGUIInsert::registerInsertTools()
{
    const QString insertId = QString::fromLatin1(PDFDocumentToolInsertPages::TOOL_ID);
    if (!PDFDocumentToolRegistry::isImplemented(insertId))
    {
        PDFDocumentToolRegistry::registerTool(std::make_shared<PDFDocumentToolInsertPages>());
    }
    if (!PDFDocumentToolGUIRegistry::getFactory(insertId))
    {
        PDFDocumentToolGUIRegistry::registerFactory(insertId, std::make_shared<PDFDocumentToolGUIFactoryOf<PDFAppInsertToolGUI>>());
    }

    const QString addImageId = QString::fromLatin1(PDFDocumentToolAddImage::TOOL_ID);
    if (!PDFDocumentToolRegistry::isImplemented(addImageId))
    {
        PDFDocumentToolRegistry::registerTool(std::make_shared<PDFDocumentToolAddImage>());
    }
    if (!PDFDocumentToolGUIRegistry::getFactory(addImageId))
    {
        PDFDocumentToolGUIRegistry::registerFactory(addImageId, std::make_shared<PDFDocumentToolGUIFactoryOf<PDFAppAddImageToolGUI>>());
    }
}

// ------------------------------------------------------------- PDFAppToolChoiceSwitch

PDFAppToolChoiceSwitch* PDFAppToolPanelBuilder::addChoiceSwitch(const QString& parameter, const QString& label)
{
    // Plan of step 3, R1 d: the element of package C (its class lives in this file)
    PDFAppToolChoiceSwitch* choiceSwitch = new PDFAppToolChoiceSwitch(getWidget());
    choiceSwitch->setObjectName(QStringLiteral("toolChoice-") + parameter);
    choiceSwitch->setAccessibleName(label);
    if (m_gui && m_gui->getModel())
    {
        choiceSwitch->bindParameter(m_gui->getModel(), parameter);
    }
    addWidget(choiceSwitch);
    return choiceSwitch;
}

PDFAppToolChoiceSwitch::PDFAppToolChoiceSwitch(QWidget* parent) :
    BaseClass(parent)
{
    setObjectName(QStringLiteral("toolChoiceSwitch"));
    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(GAP);
    m_group = new QButtonGroup(this);
    m_group->setExclusive(true);
}

PDFAppToolChoiceSwitch::~PDFAppToolChoiceSwitch() = default;

void PDFAppToolChoiceSwitch::addOption(const QString& id, const QString& title)
{
    QRadioButton* button = new QRadioButton(title, this);
    button->setObjectName(QStringLiteral("toolChoice-") + id);
    button->setProperty(PDFAppInsertToolGUIHelper::ROW_KEY, id);
    button->setMinimumHeight(ROW_HEIGHT);
    m_group->addButton(button);
    connect(button, &QRadioButton::clicked, this, &PDFAppToolChoiceSwitch::onButtonClicked);
    m_layout->addWidget(button);
    m_buttons.emplace_back(id, button);
}

void PDFAppToolChoiceSwitch::setChoice(const QString& id)
{
    // An exclusive group keeps one button checked: no choice needs it open for a moment
    m_group->setExclusive(false);
    for (const auto& [buttonId, button] : m_buttons)
    {
        if (button)
        {
            const QSignalBlocker blocker(button.data());
            button->setChecked(buttonId == id);
        }
    }
    m_group->setExclusive(true);
}

QString PDFAppToolChoiceSwitch::getChoice() const
{
    for (const auto& [id, button] : m_buttons)
    {
        if (button && button->isChecked())
        {
            return id;
        }
    }
    return QString();
}

QStringList PDFAppToolChoiceSwitch::getOptionIds() const
{
    QStringList ids;
    for (const auto& item : m_buttons)
    {
        ids << item.first;
    }
    return ids;
}

QRadioButton* PDFAppToolChoiceSwitch::getButton(const QString& id) const
{
    for (const auto& [buttonId, button] : m_buttons)
    {
        if (buttonId == id)
        {
            return button.data();
        }
    }
    return nullptr;
}

void PDFAppToolChoiceSwitch::bindParameter(PDFAppToolParameterModel* model, const QString& parameter)
{
    m_model = model;
    m_parameter = parameter;
    const PDFDocumentToolParameterDefinition* definition = model ? PDFDocumentToolParameters::find(model->getDefinitions(), parameter) : nullptr;
    if (!definition)
    {
        return;
    }
    if (m_buttons.empty())
    {
        for (const PDFDocumentToolParameterOption& option : definition->getOptions())
        {
            addOption(option.id, option.getTitle());
        }
    }
    connect(model, &PDFAppToolParameterModel::changed, this, &PDFAppToolChoiceSwitch::onParameterChanged);
    setChoice(model->getValue(parameter).toString());
}

void PDFAppToolChoiceSwitch::onButtonClicked()
{
    const QObject* button = sender();
    const QString id = button ? button->property(PDFAppInsertToolGUIHelper::ROW_KEY).toString() : QString();
    if (id.isEmpty())
    {
        return;
    }

    // A bound switch writes its parameter (one undo step); the model announces it to its
    // readers, which may delete the switch
    QPointer<PDFAppToolChoiceSwitch> guard(this);
    if (m_model && !m_parameter.isEmpty())
    {
        m_model->setValue(m_parameter, id);
        if (!guard)
        {
            return;
        }
    }
    Q_EMIT choiceChanged(id);
}

void PDFAppToolChoiceSwitch::onParameterChanged(const QString& name)
{
    if (name == m_parameter && m_model)
    {
        setChoice(m_model->getValue(m_parameter).toString());
    }
}

// ------------------------------------------------------------- PDFAppToolPageRangeField

PDFAppToolPageRangeField* PDFAppToolPanelBuilder::addPageRangeField(const QString& parameter, const QString& label, const QString& hint)
{
    // Plan of step 3, R1 d: the element of package C (its class lives in this file)
    PDFAppToolPageRangeField* field = new PDFAppToolPageRangeField(getWidget());
    field->setObjectName(QStringLiteral("toolPageRange-") + parameter);
    field->getField()->setAccessibleName(label);
    if (!hint.isEmpty())
    {
        field->setExamples(hint);
    }
    if (m_gui)
    {
        field->bindParameter(m_gui, parameter);
    }
    addWidget(field);
    return field;
}

PDFAppToolPageRangeField::PDFAppToolPageRangeField(QWidget* parent) :
    BaseClass(parent)
{
    setObjectName(QStringLiteral("toolPageRangeField"));

    // card 388:16076: the field 34 high, the examples 8 below it, the result 8 below them
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(GAP);
    m_field = new PDFLineEdit(this);
    m_field->setObjectName(QStringLiteral("toolPageRangeEdit"));
    m_field->setFixedHeight(FIELD_HEIGHT);
    m_field->setAccessibleName(tr("Pages"));
    layout->addWidget(m_field);

    m_examples = PDFAppPageWidgets::createLabel(tr("Examples: 1-5, 7, 10-, odd, even"), QFont::Normal, 11, PDFThemeColors::TextTertiary, this);
    m_examples->setObjectName(QStringLiteral("toolPageRangeExamples"));
    m_examples->setLineHeight(LINE_HEIGHT);
    m_examples->setWordWrap(true);
    m_examples->setMinimumWidth(1);
    layout->addWidget(m_examples);

    m_status = PDFAppPageWidgets::createLabel(QString(), QFont::Normal, 11, PDFThemeColors::TextAccent, this);
    m_status->setObjectName(QStringLiteral("toolPageRangeStatus"));
    m_status->setLineHeight(LINE_HEIGHT);
    m_status->setWordWrap(true);
    m_status->setMinimumWidth(1);
    layout->addWidget(m_status);

    m_timer = new QTimer(this);
    m_timer->setSingleShot(true);
    m_timer->setInterval(WRITE_DELAY);
    connect(m_timer.data(), &QTimer::timeout, this, &PDFAppToolPageRangeField::onWriteTimer);
    connect(m_field.data(), &QLineEdit::textEdited, this, &PDFAppToolPageRangeField::onTextEdited);
    connect(m_field.data(), &QLineEdit::editingFinished, this, &PDFAppToolPageRangeField::onEditingFinished);
    validate();
}

PDFAppToolPageRangeField::~PDFAppToolPageRangeField() = default;

void PDFAppToolPageRangeField::setPageCount(PDFInteger pageCount)
{
    m_pageCount = qMax<PDFInteger>(0, pageCount);
    validate();
}

void PDFAppToolPageRangeField::setText(const QString& text)
{
    if (m_field && m_field->text() != text)
    {
        m_field->setText(text);
    }
    m_chosenText = text.trimmed();
    m_timer->stop();
    validate();
}

QString PDFAppToolPageRangeField::getText() const
{
    return m_field ? m_field->text() : QString();
}

void PDFAppToolPageRangeField::setEmptyAllowed(bool isEmptyAllowed)
{
    m_isEmptyAllowed = isEmptyAllowed;
    validate();
}

void PDFAppToolPageRangeField::setExamples(const QString& text)
{
    m_examples->setText(text);
}

void PDFAppToolPageRangeField::validate()
{
    // The core decides (K23 with R13): the order of the text, odd, even, 10-, the bounds
    m_pages.clear();
    QString error;
    const QString text = getText().trimmed();
    if (text.isEmpty())
    {
        if (!m_isEmptyAllowed)
        {
            error = tr("Enter the pages, for example 1-5");
        }
    }
    else
    {
        const std::optional<PDFPageRangeList> list = PDFPageRangeList::parse(text, m_pageCount, &error);
        if (list)
        {
            m_pages = list->getPageIndices();
        }
    }
    m_error = error;
    if (m_field)
    {
        m_field->setInvalid(!m_error.isEmpty(), m_error);
    }
    updateStatus();
}

void PDFAppToolPageRangeField::updateStatus()
{
    // 388:16089 "5 of 24 pages selected" text/accent; an error in text/danger
    if (!m_error.isEmpty())
    {
        m_status->setText(m_error);
        m_status->setTextColor(PDFThemeColors::TextDanger);
        return;
    }
    const int count = PDFAppInsertToolGUIHelper::toInt(PDFInteger(m_pages.size()));
    m_status->setText(m_pages.empty() ? tr("No page selected")
                                      : tr("%1 of %Ln pages selected", nullptr, PDFAppInsertToolGUIHelper::toInt(m_pageCount)).arg(PDFAppLocale::formatNumber(count)));
    m_status->setTextColor(PDFThemeColors::TextAccent);
}

void PDFAppToolPageRangeField::bindParameter(PDFDocumentToolGUI* gui, const QString& parameter)
{
    m_gui = gui;
    m_model = gui ? gui->getModel() : nullptr;
    m_parameter = parameter;
    m_isEmptyAllowed = true;
    const PDFAppDocument* document = gui ? gui->getDocument() : nullptr;
    m_pageCount = document ? document->getPageCount() : 0;
    if (m_model)
    {
        connect(m_model.data(), &PDFAppToolParameterModel::changed, this, &PDFAppToolPageRangeField::onParameterChanged);
        setText(m_model->getValue(parameter).toString());
    }
}

void PDFAppToolPageRangeField::onTextEdited(const QString& text)
{
    Q_UNUSED(text);

    const QString previousError = m_error;
    validate();
    m_timer->start();

    // A bound field says its error to the GUI (the Primary button), then to its readers - last
    QPointer<PDFAppToolPageRangeField> guard(this);
    if (m_gui && !m_parameter.isEmpty())
    {
        m_gui->setInputError(m_parameter, m_error);
        if (!guard)
        {
            return;
        }
    }
    if (previousError != m_error)
    {
        Q_EMIT errorChanged(m_error);
    }
}

void PDFAppToolPageRangeField::onEditingFinished()
{
    flush();
}

void PDFAppToolPageRangeField::flush()
{
    m_timer->stop();
    onWriteTimer();
}

void PDFAppToolPageRangeField::onWriteTimer()
{
    const QString text = getText().trimmed();
    if (!m_error.isEmpty() || text == m_chosenText)
    {
        return;
    }

    m_chosenText = text;
    QPointer<PDFAppToolPageRangeField> guard(this);
    if (m_model && !m_parameter.isEmpty())
    {
        m_model->setValue(m_parameter, text);
        if (!guard)
        {
            return;
        }
    }
    Q_EMIT rangeChosen(text);
}

void PDFAppToolPageRangeField::onParameterChanged(const QString& name)
{
    // Undo and Redo of the bound parameter; a text being typed stays
    if (name == m_parameter && m_model && !m_timer->isActive())
    {
        const QString value = m_model->getValue(m_parameter).toString();
        if (value != getText().trimmed())
        {
            setText(value);
        }
    }
}

// ------------------------------------------------------------- PDFAppInsertSourceModel

PDFAppInsertSourceModel::PDFAppInsertSourceModel(QObject* parent) :
    BaseClass(parent)
{

}

PDFAppInsertSourceModel::~PDFAppInsertSourceModel() = default;

void PDFAppInsertSourceModel::setInputList(PDFAppToolInputList* list)
{
    m_list = list;
}

void PDFAppInsertSourceModel::setContent(std::vector<Group> groups, std::vector<Row> rows)
{
    // The same pages keep their rows (one dataChanged), another set of pages is a reset
    bool isSameRows = rows.size() == m_rows.size();
    for (size_t i = 0; isSameRows && i < rows.size(); ++i)
    {
        isSameRows = rows[i].inputId == m_rows[i].inputId && rows[i].pageIndex == m_rows[i].pageIndex;
    }

    const bool isGroupsChanged = groups != m_groups || !isSameRows;
    QPointer<PDFAppInsertSourceModel> guard(this);
    if (!isSameRows)
    {
        beginResetModel();
        m_rows = std::move(rows);
        m_groups = std::move(groups);
        endResetModel();
    }
    else
    {
        const bool isRowsChanged = rows != m_rows;
        m_rows = std::move(rows);
        m_groups = std::move(groups);
        if (isRowsChanged && !m_rows.empty())
        {
            Q_EMIT dataChanged(index(0), index(int(m_rows.size()) - 1));
        }
    }

    if (guard && isGroupsChanged)
    {
        Q_EMIT groupsChanged();
    }
}

const PDFAppInsertSourceModel::Row* PDFAppInsertSourceModel::getRow(int row) const
{
    return row >= 0 && size_t(row) < m_rows.size() ? &m_rows[size_t(row)] : nullptr;
}

int PDFAppInsertSourceModel::getGroupOfRow(int row) const
{
    for (size_t i = 0; i < m_groups.size(); ++i)
    {
        if (row >= m_groups[i].firstRow && row < m_groups[i].firstRow + m_groups[i].rowCount)
        {
            return int(i);
        }
    }
    return -1;
}

bool PDFAppInsertSourceModel::isRowSelected(int row) const
{
    const Row* item = getRow(row);
    return item && item->state == PDFComponentPainter::PageTileState::Selected;
}

int PDFAppInsertSourceModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : int(m_rows.size());
}

QVariant PDFAppInsertSourceModel::data(const QModelIndex& index, int role) const
{
    const Row* row = index.isValid() ? getRow(index.row()) : nullptr;
    if (!row)
    {
        return QVariant();
    }

    switch (role)
    {
        case Qt::DisplayRole:
            return row->label;

        case Qt::ToolTipRole:
        case Qt::AccessibleTextRole:
        {
            const PDFAppToolInput* input = m_list && !row->inputId.isEmpty() ? m_list->findInput(row->inputId) : nullptr;
            return input ? tr("%1 · page %2").arg(input->name, row->label) : row->label;
        }

        case PDFComponentRoles::SubtitleRole:
            return row->caption.isEmpty() ? QVariant() : QVariant(row->caption);

        case PDFComponentRoles::PageTileStateRole:
            return int(row->state);

        case PDFAppToolPageTileDelegate::DimmedRole:
            return row->isDimmed;

        case PDFAppToolPageTileDelegate::PaperBorderRole:
            // A blank page is its paper (388:17078: the border border/strong)
            return row->inputId.isEmpty();

        case Qt::AccessibleDescriptionRole:
            return row->isDimmed ? tr("not inserted") : row->caption;

        case PDFAppToolPageTileDelegate::SlotScaleRole:
        {
            // The tiles of the group (the large blank pages of 388:16811)
            const int group = getGroupOfRow(index.row());
            return group >= 0 ? QVariant(m_groups[size_t(group)].tileScale) : QVariant();
        }

        case PDFComponentRoles::PageSizeRole:
            return row->pageSize.isValid() ? QVariant(row->pageSize) : QVariant();

        case PDFComponentRoles::PixmapRole:
            // A blank page has no picture (the paper of the tile is the page)
            return row->inputId.isEmpty() ? QVariant() : BaseClass::data(index, role);

        case Qt::TextAlignmentRole:
            return QVariant(Qt::AlignLeading | Qt::AlignVCenter);

        default:
            return QVariant();
    }
}

Qt::ItemFlags PDFAppInsertSourceModel::flags(const QModelIndex& index) const
{
    return index.isValid() ? Qt::ItemIsEnabled | Qt::ItemNeverHasChildren : Qt::NoItemFlags;
}

QString PDFAppInsertSourceModel::getKey(int pageIndex) const
{
    // The picture of the page of the input (its content generation) of the size of the model
    const Row* row = getRow(pageIndex);
    if (!row || row->inputId.isEmpty() || !m_list)
    {
        return QString();
    }
    return m_list->getPictureKey(row->inputId, row->pageIndex, getPictureSize(), getDevicePixelRatio()) + QStringLiteral("/") + QString::number(getGeneration());
}

bool PDFAppInsertSourceModel::hasPictureSources() const
{
    return !m_list.isNull();
}

PDFAppThumbnailSource PDFAppInsertSourceModel::getPictureSource(int row) const
{
    const Row* item = getRow(row);
    if (!item || item->inputId.isEmpty() || !m_list)
    {
        return PDFAppThumbnailSource();
    }
    return m_list->getPictureSource(item->inputId, item->pageIndex);
}

// ------------------------------------------------------------- PDFAppInsertEmptyState

PDFAppInsertEmptyState::PDFAppInsertEmptyState(const Content& content, QWidget* parent) :
    BaseClass(parent)
{
    setObjectName(QStringLiteral("insertEmptyState"));

    // The card in the middle of the stage (398:29210 at 117, 250.5 of 774 x 802)
    QVBoxLayout* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addStretch(1);
    QHBoxLayout* center = new QHBoxLayout();
    center->addStretch(1);
    PDFAppInsertEmptyCard* card = new PDFAppInsertEmptyCard(this);
    card->setObjectName(QStringLiteral("insertDropZone"));
    card->setFixedWidth(CARD_WIDTH);
    m_card = card;
    center->addWidget(card);
    center->addStretch(1);
    outer->addLayout(center);
    outer->addStretch(1);

    QVBoxLayout* layout = new QVBoxLayout(card);
    layout->setContentsMargins(PADDING_SIDE, PADDING_TOP, PADDING_SIDE, PADDING_BOTTOM);
    layout->setSpacing(GAP);

    // icon-tile 398:29211: bg/accent-subtle, the icon 16
    PDFIconBadge* icon = new PDFIconBadge(content.iconName, ICON_TILE, 16, card);
    icon->setRadius(8);
    icon->setColors(PDFThemeColors::BgAccentSubtle, PDFThemeColors::IconAccent);
    layout->addWidget(icon, 0, Qt::AlignHCenter);

    m_title = PDFAppPageWidgets::createLabel(content.title, QFont::DemiBold, 16, PDFThemeColors::TextPrimary, card);
    m_title->setObjectName(QStringLiteral("insertEmptyTitle"));
    m_title->setAlignment(Qt::AlignCenter);
    layout->addWidget(m_title);
    m_text = PDFAppPageWidgets::createLabel(content.text, QFont::Normal, 12, PDFThemeColors::TextSecondary, card);
    m_text->setObjectName(QStringLiteral("insertEmptyText"));
    m_text->setAlignment(Qt::AlignCenter);
    m_text->setWordWrap(true);
    m_text->setMinimumWidth(1);
    layout->addWidget(m_text);

    // buttons 398:29216: Add Files… (Primary) and Blank Pages, 8 apart
    QHBoxLayout* buttons = new QHBoxLayout();
    buttons->setContentsMargins(0, 0, 0, 0);
    buttons->setSpacing(8);
    buttons->addStretch(1);
    m_primaryButton = PDFAppPageWidgets::createButton(content.primaryText, content.primaryIconName, true, card);
    m_primaryButton->setObjectName(QStringLiteral("insertEmptyPrimary"));
    connect(m_primaryButton.data(), &QPushButton::clicked, this, &PDFAppInsertEmptyState::primaryRequested);
    buttons->addWidget(m_primaryButton);
    if (!content.secondaryText.isEmpty())
    {
        m_secondaryButton = PDFAppPageWidgets::createButton(content.secondaryText, content.secondaryIconName, false, card);
        m_secondaryButton->setObjectName(QStringLiteral("insertEmptySecondary"));
        connect(m_secondaryButton.data(), &QPushButton::clicked, this, &PDFAppInsertEmptyState::secondaryRequested);
        buttons->addWidget(m_secondaryButton);
    }
    buttons->addStretch(1);
    layout->addLayout(buttons);

    if (!content.documents.empty())
    {
        // caption 398:29230 and a row of each open document (398:29243, 52 high)
        PDFLabel* caption = PDFAppPageWidgets::createLabel(tr("Open documents"), QFont::DemiBold, 11, PDFThemeColors::TextTertiary, card);
        caption->setObjectName(QStringLiteral("insertEmptyCaption"));
        caption->setContentsMargins(0, 8, 0, 0);
        layout->addWidget(caption);

        for (const Document& document : content.documents)
        {
            // tab-1 398:29243: the shared row of an open document
            PDFAppToolOpenDocumentRow* row = new PDFAppToolOpenDocumentRow(document, card);
            row->setObjectName(QStringLiteral("insertEmptyDocument-") + document.documentId);
            row->getAddButton()->setObjectName(QStringLiteral("insertEmptyAdd"));
            if (PDFTag* tag = row->getTag())
            {
                tag->setObjectName(QStringLiteral("insertEmptyUnsavedTag"));
            }
            connect(row, &PDFAppToolOpenDocumentRow::addRequested, this, &PDFAppInsertEmptyState::addDocumentRequested);
            m_addButtons.push_back(row->getAddButton());
            layout->addWidget(row);
        }
    }
}

PDFAppInsertEmptyState::~PDFAppInsertEmptyState() = default;

std::vector<QPushButton*> PDFAppInsertEmptyState::getAddButtons() const
{
    std::vector<QPushButton*> buttons;
    for (const QPointer<QPushButton>& button : m_addButtons)
    {
        if (button)
        {
            buttons.push_back(button.data());
        }
    }
    return buttons;
}

// ------------------------------------------------------------- PDFAppInsertCanvas

PDFAppInsertCanvas::PDFAppInsertCanvas(QWidget* parent) :
    BaseClass(parent)
{
    setObjectName(QStringLiteral("insertCanvas"));

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(PADDING, PADDING, PADDING, PADDING);
    layout->setSpacing(0);

    // preview-bar 388:15744: "Pages to insert · 6 pages from 2 files" Semi Bold 13
    m_bar = new QWidget(this);
    m_bar->setObjectName(QStringLiteral("insertPreviewBar"));
    m_bar->setFixedHeight(BAR_HEIGHT);
    QHBoxLayout* barLayout = new QHBoxLayout(m_bar);
    barLayout->setContentsMargins(0, 0, 0, 0);
    m_header = PDFAppPageWidgets::createLabel(QString(), QFont::DemiBold, 13, PDFThemeColors::TextPrimary, m_bar);
    m_header->setObjectName(QStringLiteral("insertPreviewTitle"));
    m_header->setMinimumWidth(1);
    barLayout->addWidget(m_header, 1);
    layout->addWidget(m_bar);
    layout->addSpacing(BAR_GAP);

    // stage 388:15746: the source cards (the grouped view) and the hint 10 below them; the view
    // is as high as its cards while they fit (the rest of the stage stays empty), a longer
    // content scrolls
    m_stage = new QWidget(this);
    m_stage->setObjectName(QStringLiteral("insertStage"));
    QVBoxLayout* stageLayout = new QVBoxLayout(m_stage);
    stageLayout->setContentsMargins(0, 0, 0, 0);
    stageLayout->setSpacing(HINT_GAP);
    m_model = new PDFAppInsertSourceModel(this);
    // The tiles with their marks (the muted pages, the large blank pages) on the cards
    PDFAppToolPageTileDelegate* delegate = new PDFAppToolPageTileDelegate(this);
    delegate->setBackgroundColor(PDFThemeColors::BgSurface);
    m_delegate = delegate;
    m_pageView = new PDFAppToolMergePageView(m_stage);
    m_pageView->setObjectName(QStringLiteral("insertSourceView"));
    m_pageView->setModel(m_model);
    m_pageView->setTileDelegate(m_delegate);
    m_pageView->setEditable(false);
    m_pageView->setMaximumHeight(1);
    // The view dies only with the canvas (a child of the stage); the filter compares only the
    // address of the viewport (no call through the view, which may be in its destructor)
    m_pageViewport = m_pageView->viewport();
    m_pageViewport->installEventFilter(this);
    stageLayout->addWidget(m_pageView, 1);
    connect(m_pageView.data(), &PDFAppToolMergePageView::contentHeightChanged, this, &PDFAppInsertCanvas::onContentHeightChanged);
    connect(m_pageView.data(), &PDFAppToolMergePageView::actionTriggered, this, &PDFAppInsertCanvas::groupActionTriggered);
    m_scheduler = new PDFAppThumbnailScheduler(m_model, m_pageView, this);
    m_hint = PDFAppPageWidgets::createText(QString(), PDFThemeFonts::getFont(QFont::Normal, 11), 13, PDFThemeColors::TextTertiary, m_stage);
    m_hint->setObjectName(QStringLiteral("insertHint"));
    m_hint->setMinimumWidth(1);
    stageLayout->addWidget(m_hint);
    stageLayout->addStretch(0);
    layout->addWidget(m_stage, 1);

    setTileSize(m_tileSize);
}

PDFAppInsertCanvas::~PDFAppInsertCanvas() = default;

void PDFAppInsertCanvas::setHeaderText(const QString& text)
{
    m_header->setText(text);
}

void PDFAppInsertCanvas::setHintText(const QString& text)
{
    m_hint->setText(text);
    m_hint->setVisible(!text.isEmpty());
}

void PDFAppInsertCanvas::setEmptyState(QWidget* emptyState)
{
    if (m_emptyState == emptyState)
    {
        return;
    }
    if (m_emptyState)
    {
        // The old one may be the sender of the change (Add of an open document): later
        m_emptyState->hide();
        m_emptyState->deleteLater();
    }

    m_emptyState = emptyState;
    QVBoxLayout* canvasLayout = qobject_cast<QVBoxLayout*>(layout());
    if (m_emptyState)
    {
        m_emptyState->setParent(this);
        canvasLayout->addWidget(m_emptyState, 1);
        m_emptyState->show();
    }

    // 398:29209: the stage without the bar of the preview
    const bool isEmpty = !m_emptyState.isNull();
    m_bar->setVisible(!isEmpty);
    m_stage->setVisible(!isEmpty);
    canvasLayout->itemAt(1)->spacerItem()->changeSize(0, isEmpty ? 0 : BAR_GAP, QSizePolicy::Minimum, QSizePolicy::Fixed);
    canvasLayout->invalidate();
}

void PDFAppInsertCanvas::setTileSize(PDFAppToolPageGrid::TileSize size)
{
    m_tileSize = size;
    const double scale = PDFAppToolPageGrid::getTileScale(size);
    m_delegate->setSlotSize(QSize(int(std::lround(SMALL_SLOT.width() * scale)), int(std::lround(SMALL_SLOT.height() * scale))));
    updatePictureSize();
    m_pageView->setTileDelegate(m_delegate);
}

void PDFAppInsertCanvas::setEditable(bool isEditable)
{
    m_isEditable = isEditable;
    m_pageView->setActionsEnabled(isEditable);
}

void PDFAppInsertCanvas::onContentHeightChanged(int height)
{
    // The view hugs its cards (the hint 10 below them, 388:15746), at most the rest of the stage
    m_pageView->setMaximumHeight(qMax(height, 1));
}

void PDFAppInsertCanvas::updatePictureSize()
{
    m_model->setPictureSize(m_delegate->getSlotSize(), devicePixelRatioF());
}

void PDFAppInsertCanvas::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    // document-canvas 388:15520: bg/track
    QPainter painter(this);
    painter.fillRect(rect(), PDFThemeManager::getInstance()->getColor(PDFThemeColors::BgTrack));
}

void PDFAppInsertCanvas::changeEvent(QEvent* event)
{
    BaseClass::changeEvent(event);
    if (event->type() == QEvent::DevicePixelRatioChange)
    {
        updatePictureSize();
    }
}

bool PDFAppInsertCanvas::eventFilter(QObject* watched, QEvent* event)
{
    // The type first, then the address of the viewport: the mouse events come only to a living view
    const bool isMouseButton = event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseButtonRelease;
    if (isMouseButton && m_pageViewport.isObject(watched) && m_pageView)
    {
        switch (event->type())
        {
            case QEvent::MouseButtonPress:
                m_pressPosition = static_cast<QMouseEvent*>(event)->position().toPoint();
                break;

            case QEvent::MouseButtonRelease:
            {
                const QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);
                const QPoint position = mouseEvent->position().toPoint();
                if (m_isEditable && mouseEvent->button() == Qt::LeftButton && (position - m_pressPosition).manhattanLength() < 4)
                {
                    // The tile or the card under the pointer; the view uses itself after its
                    // mouse events, so the click is said from the event loop
                    const QModelIndex index = m_pageView->indexAt(position);
                    int group = index.isValid() ? m_model->getGroupOfRow(index.row()) : -1;
                    for (int i = 0; !index.isValid() && i < int(m_model->getGroups().size()); ++i)
                    {
                        if (m_pageView->getGroupRect(i).contains(position))
                        {
                            group = i;
                        }
                    }
                    if (group >= 0)
                    {
                        m_queuedRow = index.isValid() ? index.row() : -1;
                        m_queuedGroup = group;
                        if (!m_isClickQueued)
                        {
                            m_isClickQueued = true;
                            QMetaObject::invokeMethod(this, &PDFAppInsertCanvas::emitQueuedClick, Qt::QueuedConnection);
                        }
                    }
                }
                break;
            }

            default:
                break;
        }
    }
    return BaseClass::eventFilter(watched, event);
}

void PDFAppInsertCanvas::emitQueuedClick()
{
    m_isClickQueued = false;
    const int row = m_queuedRow;
    const int group = m_queuedGroup;
    m_queuedRow = -1;
    m_queuedGroup = -1;
    if (group >= 0)
    {
        Q_EMIT pageClicked(row, group);
    }
}

// ------------------------------------------------------------- PDFAppAddImagePageCard

PDFAppAddImagePageCard::PDFAppAddImagePageCard(const QString& inputId, QWidget* parent) :
    BaseClass(parent),
    m_inputId(inputId)
{
    setObjectName(QStringLiteral("addImageCard-") + inputId);
    setCursor(Qt::PointingHandCursor);

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(PADDING, PADDING, PADDING, PADDING);
    layout->setSpacing(0);
    m_preview = new PDFAppImagePagePreview(this);
    m_preview->setObjectName(QStringLiteral("addImagePreview"));
    m_preview->setShadowVisible(false);
    m_preview->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_preview, 0, Qt::AlignHCenter);
    layout->addSpacing(CAPTION_GAP);
    m_title = PDFAppPageWidgets::createLabel(QString(), QFont::Medium, 11, PDFThemeColors::TextPrimary, this);
    m_title->setObjectName(QStringLiteral("addImageCardTitle"));
    m_title->setAlignment(Qt::AlignCenter);
    layout->addWidget(m_title);
    m_caption = PDFAppPageWidgets::createLabel(QString(), QFont::Normal, 10, PDFThemeColors::TextTertiary, this);
    m_caption->setObjectName(QStringLiteral("addImageCardCaption"));
    m_caption->setAlignment(Qt::AlignCenter);
    layout->addWidget(m_caption);
}

PDFAppAddImagePageCard::~PDFAppAddImagePageCard() = default;

void PDFAppAddImagePageCard::setTexts(const QString& title, const QString& caption)
{
    m_title->setText(title);
    m_caption->setText(caption);
}

void PDFAppAddImagePageCard::setSelected(bool isSelected)
{
    if (m_isSelected != isSelected)
    {
        m_isSelected = isSelected;
        m_title->setTextColor(isSelected ? PDFThemeColors::TextAccent : PDFThemeColors::TextPrimary);
        update();
    }
}

void PDFAppAddImagePageCard::setPageHeight(int pageHeight)
{
    // The page of the preview by its ratio (the page of the layout in points)
    const QSizeF pageSize = m_preview->getPageSize();
    const double ratio = pageSize.isEmpty() ? 210.0 / 297.0 : pageSize.width() / pageSize.height();
    const int width = qBound(1, int(std::lround(pageHeight * ratio)), 4096);
    m_preview->setFixedSize(width, qBound(1, pageHeight, 4096));
}

void PDFAppAddImagePageCard::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    if (!m_isSelected)
    {
        return;
    }

    // The selected image (388:18837): bg/accent-subtle with the border border/focus, radius 8
    QPainter painter(this);
    const PDFStylePainterGuard guard(&painter);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QRectF shape = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    PDFStylePainter::fill(&painter, shape, PDFStylePainter::Radii(RADIUS), PDFThemeColors::BgAccentSubtle);
    painter.setPen(QPen(PDFStylePainter::color(PDFThemeColors::BorderFocus), 1.0));
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(PDFStylePainter::roundedPath(shape, PDFStylePainter::Radii(RADIUS)));
}

void PDFAppAddImagePageCard::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && rect().contains(event->position().toPoint()))
    {
        event->accept();
        Q_EMIT clicked(m_inputId);
        return;
    }
    BaseClass::mouseReleaseEvent(event);
}

// ------------------------------------------------------------- PDFAppAddImageCanvas

PDFAppAddImageCanvas::PDFAppAddImageCanvas(QWidget* parent) :
    BaseClass(parent)
{
    setObjectName(QStringLiteral("addImageCanvas"));

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(PADDING, PADDING, PADDING, PADDING);
    layout->setSpacing(0);

    // preview-bar: "New pages · 2", the segments Fit / 100 %
    m_bar = new QWidget(this);
    m_bar->setObjectName(QStringLiteral("addImagePreviewBar"));
    m_bar->setFixedHeight(BAR_HEIGHT);
    QHBoxLayout* barLayout = new QHBoxLayout(m_bar);
    barLayout->setContentsMargins(0, 0, 0, 0);
    barLayout->setSpacing(8);
    m_header = PDFAppPageWidgets::createLabel(QString(), QFont::DemiBold, 13, PDFThemeColors::TextPrimary, m_bar);
    m_header->setObjectName(QStringLiteral("addImagePreviewTitle"));
    m_header->setMinimumWidth(1);
    barLayout->addWidget(m_header, 1);
    layout->addWidget(m_bar);
    layout->addSpacing(BAR_GAP);

    // The pages in a flow (they scroll at 100 %) and the hint below them
    m_scrollArea = new QScrollArea(this);
    m_scrollArea->setObjectName(QStringLiteral("addImageScrollArea"));
    m_scrollArea->setFrameShape(QFrame::NoFrame);
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->viewport()->setAutoFillBackground(false);
    QWidget* content = new QWidget(m_scrollArea);
    content->setObjectName(QStringLiteral("addImageContent"));
    content->setAutoFillBackground(false);
    QVBoxLayout* contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(HINT_GAP);
    m_flow = new QWidget(content);
    m_flow->setObjectName(QStringLiteral("addImageFlow"));
    m_flow->installEventFilter(this);
    contentLayout->addWidget(m_flow);
    m_hint = PDFAppPageWidgets::createText(QString(), PDFThemeFonts::getFont(QFont::Normal, 11), 13, PDFThemeColors::TextSecondary, content);
    m_hint->setObjectName(QStringLiteral("addImageHint"));
    m_hint->setMinimumWidth(1);
    contentLayout->addWidget(m_hint);
    contentLayout->addStretch(1);
    m_scrollArea->setWidget(content);
    layout->addWidget(m_scrollArea, 1);

    retranslate();
}

PDFAppAddImageCanvas::~PDFAppAddImageCanvas() = default;

void PDFAppAddImageCanvas::setHeaderText(const QString& text)
{
    m_header->setText(text);
}

void PDFAppAddImageCanvas::setHintText(const QString& text)
{
    m_hint->setText(text);
}

void PDFAppAddImageCanvas::setCards(const std::vector<PDFAppAddImagePageCard*>& cards)
{
    // The old cards may be the sender of a change (a click selects another image): later
    for (const QPointer<PDFAppAddImagePageCard>& card : m_cards)
    {
        if (card && std::find(cards.cbegin(), cards.cend(), card.data()) == cards.cend())
        {
            card->hide();
            card->deleteLater();
        }
    }
    m_cards.clear();
    for (PDFAppAddImagePageCard* card : cards)
    {
        card->setParent(m_flow);
        connect(card, &PDFAppAddImagePageCard::clicked, this, &PDFAppAddImageCanvas::onCardClicked, Qt::UniqueConnection);
        card->show();
        m_cards.push_back(card);
    }
    layoutCards();
}

std::vector<PDFAppAddImagePageCard*> PDFAppAddImageCanvas::getCards() const
{
    std::vector<PDFAppAddImagePageCard*> cards;
    for (const QPointer<PDFAppAddImagePageCard>& card : m_cards)
    {
        if (card)
        {
            cards.push_back(card.data());
        }
    }
    return cards;
}

void PDFAppAddImageCanvas::setFit(bool isFit)
{
    m_isFit = isFit;
    const QSignalBlocker blocker(m_zoomSwitch.data());
    m_zoomSwitch->setCurrentIndex(isFit ? 0 : 1);
    layoutCards();
}

void PDFAppAddImageCanvas::setEmptyState(QWidget* emptyState)
{
    if (m_emptyState == emptyState)
    {
        return;
    }
    if (m_emptyState)
    {
        m_emptyState->hide();
        m_emptyState->deleteLater();
    }

    m_emptyState = emptyState;
    QVBoxLayout* canvasLayout = qobject_cast<QVBoxLayout*>(layout());
    if (m_emptyState)
    {
        m_emptyState->setParent(this);
        canvasLayout->addWidget(m_emptyState, 1);
        m_emptyState->show();
    }

    // 388:19253: the stage without the bar of the preview
    const bool isEmpty = !m_emptyState.isNull();
    m_bar->setVisible(!isEmpty);
    m_scrollArea->setVisible(!isEmpty);
    canvasLayout->itemAt(1)->spacerItem()->changeSize(0, isEmpty ? 0 : BAR_GAP, QSizePolicy::Minimum, QSizePolicy::Fixed);
    canvasLayout->invalidate();
}

void PDFAppAddImageCanvas::retranslate()
{
    // The segments have no setter of a text: the control is made again with the choice kept
    QHBoxLayout* barLayout = qobject_cast<QHBoxLayout*>(m_bar->layout());
    PDFSegmentedControl* zoomSwitch = new PDFSegmentedControl(m_bar);
    zoomSwitch->setObjectName(QStringLiteral("addImageZoomSwitch"));
    zoomSwitch->addSegment(tr("Fit"));
    zoomSwitch->addSegment(tr("100 %"));
    zoomSwitch->setFixedSize(2 * SEGMENT_WIDTH, SEGMENT_HEIGHT);
    zoomSwitch->setCurrentIndex(m_isFit ? 0 : 1);
    connect(zoomSwitch, &PDFSegmentedControl::currentIndexChanged, this, &PDFAppAddImageCanvas::onZoomSwitchChanged);
    if (m_zoomSwitch)
    {
        // The layout gives back the item of the old control (owned by the caller)
        zoomSwitch->setEnabled(m_zoomSwitch->isEnabled());
        delete barLayout->replaceWidget(m_zoomSwitch, zoomSwitch);
        m_zoomSwitch->hide();
        m_zoomSwitch->deleteLater();
    }
    else
    {
        barLayout->addWidget(zoomSwitch);
    }
    m_zoomSwitch = zoomSwitch;
}

void PDFAppAddImageCanvas::onZoomSwitchChanged(int index)
{
    m_isFit = index == 0;
    layoutCards();
    Q_EMIT zoomChanged(m_isFit);
}

void PDFAppAddImageCanvas::onCardClicked(const QString& inputId)
{
    Q_EMIT cardClicked(inputId);
}

void PDFAppAddImageCanvas::layoutCards()
{
    if (!m_flow)
    {
        return;
    }

    // The flow of 388:18837: the cards 20 apart in rows (from the right edge for right-to-left);
    // Fit gives each page the height of the frame, 100 % the size of the page at 96 dpi
    const int width = qMax(1, m_flow->width());
    const bool isRightToLeft = layoutDirection() == Qt::RightToLeft;
    int x = 0;
    int y = 0;
    int rowHeight = 0;
    for (const QPointer<PDFAppAddImagePageCard>& card : m_cards)
    {
        if (!card)
        {
            continue;
        }
        // Fit: the height of the frame, a wide page fits the width of the canvas
        const QSizeF pageSize = card->getPreview()->getPageSize();
        int pageHeight = FIT_PAGE_HEIGHT;
        if (!pageSize.isEmpty())
        {
            const double fitWidthHeight = double(width - 2 * PDFAppAddImagePageCard::PADDING) * pageSize.height() / pageSize.width();
            pageHeight = m_isFit ? qBound(1, int(fitWidthHeight), FIT_PAGE_HEIGHT) : int(std::lround(qMin(pageSize.height() * PDFAppImagePagePreview::ACTUAL_DPI / 72.0, 4096.0)));
        }
        card->setPageHeight(pageHeight);
        const QSize size = card->sizeHint();
        if (x > 0 && x + size.width() > width)
        {
            x = 0;
            y += rowHeight + CARD_GAP;
            rowHeight = 0;
        }
        const int left = isRightToLeft ? width - x - size.width() : x;
        card->setGeometry(QRect(QPoint(left, y), size));
        x += size.width() + CARD_GAP;
        rowHeight = qMax(rowHeight, size.height());
    }
    const int height = y + rowHeight;
    if (m_flow->minimumHeight() != height)
    {
        m_flow->setMinimumHeight(height);
    }
    if (!m_isFit)
    {
        int contentWidth = 0;
        for (const QPointer<PDFAppAddImagePageCard>& card : m_cards)
        {
            contentWidth = card ? qMax(contentWidth, card->sizeHint().width()) : contentWidth;
        }
        m_flow->setMinimumWidth(contentWidth);
    }
    else
    {
        m_flow->setMinimumWidth(0);
    }
}

void PDFAppAddImageCanvas::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.fillRect(rect(), PDFThemeManager::getInstance()->getColor(PDFThemeColors::BgTrack));
}

void PDFAppAddImageCanvas::changeEvent(QEvent* event)
{
    BaseClass::changeEvent(event);
    if (event->type() == QEvent::LanguageChange)
    {
        retranslate();
    }
    if (event->type() == QEvent::LayoutDirectionChange)
    {
        layoutCards();
    }
}

bool PDFAppAddImageCanvas::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_flow && event->type() == QEvent::Resize)
    {
        layoutCards();
    }
    return BaseClass::eventFilter(watched, event);
}

// ------------------------------------------------------------- PDFAppInsertToolGUIBase

PDFAppInsertToolGUIBase::PDFAppInsertToolGUIBase(QObject* parent) :
    BaseClass(parent)
{
    // The sources live for the whole session of the tool: the list is a child of this GUI, the
    // parts made again after a change of the language find it here (QPointer)
    m_list = new PDFAppToolInputList(this);
    m_list->setObjectName(QStringLiteral("insertSources"));
    connect(m_list.data(), &PDFAppToolInputList::inputsChanged, this, &PDFAppInsertToolGUIBase::onListInputsChanged);
    connect(m_list.data(), &PDFAppToolInputList::inputChanged, this, &PDFAppInsertToolGUIBase::onListInputChanged);

    m_progressTimer = new QTimer(this);
    m_progressTimer->setInterval(PROGRESS_INTERVAL);
    connect(m_progressTimer.data(), &QTimer::timeout, this, &PDFAppInsertToolGUIBase::onProgressTimer);
}

PDFAppInsertToolGUIBase::~PDFAppInsertToolGUIBase()
{
    // The key widget gets back what it had: its drops and no actions of the tool
    if (m_keyWidget)
    {
        m_keyWidget->removeEventFilter(this);
        m_keyWidget->setAcceptDrops(m_wasDropAccepted);
    }
    for (const QPointer<QAction>& action : m_keyActions)
    {
        delete action.data();
    }
    if (m_passwordDialog)
    {
        // The dialog holds the list by a QPointer; it closes itself (deleted later)
        m_passwordDialog->disconnect(this);
        m_passwordDialog->close();
    }
}

// ------------------------------------------------------------- values and the list

void PDFAppInsertToolGUIBase::connectList()
{
    if (!m_list)
    {
        return;
    }

    if (!m_isShellConnected && getShell())
    {
        m_isShellConnected = true;
        connect(getShell(), &PDFDocumentToolShell::stateChanged, this, &PDFAppInsertToolGUIBase::onShellStateChanged);
    }
    if (!m_isHostConnected && getHost())
    {
        // Replace selected pages and the current page of the menu Position follow the thumbnails
        m_isHostConnected = true;
        connect(getHost(), &PDFAppToolHost::pagesChanged, this, &PDFAppInsertToolGUIBase::onHostPagesChanged);
    }
    if (!m_isRegistryConnected && getContext() && getContext()->getDocumentRegistry())
    {
        // The open documents of Add and of the empty state follow the registry
        m_isRegistryConnected = true;
        PDFAppDocumentRegistry* registry = getContext()->getDocumentRegistry();
        connect(registry, &PDFAppDocumentRegistry::documentAdded, this, &PDFAppInsertToolGUIBase::onRegistryChanged);
        connect(registry, &PDFAppDocumentRegistry::documentChanged, this, &PDFAppInsertToolGUIBase::onRegistryChanged);
        connect(registry, &PDFAppDocumentRegistry::documentRemoved, this, &PDFAppInsertToolGUIBase::onRegistryChanged);
    }

    initializeValues();
    installKeys();
}

void PDFAppInsertToolGUIBase::initializeValues()
{
    using Helper = PDFAppInsertToolGUIHelper;

    PDFAppToolParameterModel* model = getModel();
    if (m_isValuesInitialized || !model || !m_list)
    {
        return;
    }
    m_isValuesInitialized = true;

    // K41: what the list takes (Add Image: images only - a PDF stays as Unsupported with the reason)
    const PDFAppToolInputRules rules = getInputRules();
    m_list->setAcceptedInputs(rules.maximum, rules.acceptsPdf, rules.acceptsImages,
                              rules.acceptsPdf ? QString() : tr("Add Image as Page takes images – insert PDF files with Insert Pages"));

    // 388:15487 "Position: After page 12": the place starts after the current page; no change of
    // the user (no undo step, K9)
    const PDFAppToolHost* host = getHost();
    const PDFInteger pageCount = getDocumentPageCount();
    QPointer<PDFAppInsertToolGUIBase> guard(this);
    if (host && pageCount > 0)
    {
        const PDFInteger current = qBound<PDFInteger>(0, host->getCurrentPage(), pageCount - 1);
        model->setFollowingValue(Helper::name(PDFDocumentToolInsertBase::PARAMETER_POSITION), QString::fromLatin1(PDFDocumentToolInsertBase::POSITION_AFTER));
        if (!guard)
        {
            return;
        }
        model->setFollowingValue(Helper::name(PDFDocumentToolInsertBase::PARAMETER_PAGE), qint64(current + 1));
        if (!guard)
        {
            return;
        }
    }
    readValues();
}

QStringList PDFAppInsertToolGUIBase::getModelInputIds() const
{
    using Helper = PDFAppInsertToolGUIHelper;
    const PDFAppToolParameterModel* model = getModel();
    return model ? Helper::splitIds(model->getValue(Helper::name(PDFDocumentToolInsertBase::PARAMETER_INPUTS)).toString()) : QStringList();
}

std::map<QString, QString> PDFAppInsertToolGUIBase::getSourceValues() const
{
    using Helper = PDFAppInsertToolGUIHelper;

    // One value of each source in the order of "inputs" (the blank pages have none)
    std::map<QString, QString> values;
    const PDFAppToolParameterModel* model = getModel();
    if (!model)
    {
        return values;
    }
    const QStringList parts = model->getValue(getSourceValuesParameter()).toString().split(getSourceValuesSeparator());
    int index = 0;
    for (const QString& id : getModelInputIds())
    {
        if (id == Helper::blankId())
        {
            continue;
        }
        values[id] = index < parts.size() ? parts[index].trimmed() : QString();
        ++index;
    }
    return values;
}

void PDFAppInsertToolGUIBase::applySources(const QStringList& inputIds, const std::map<QString, QString>& values, const QVariantMap& extra)
{
    using Helper = PDFAppInsertToolGUIHelper;

    PDFAppToolParameterModel* model = getModel();
    if (!model)
    {
        return;
    }

    // One undo step: the inputs, their values in the same order and the further values
    QVariantMap map = extra;
    map.insert(Helper::name(PDFDocumentToolInsertBase::PARAMETER_INPUTS), inputIds.join(QLatin1Char(',')));
    QStringList parts;
    bool hasValue = false;
    for (const QString& id : inputIds)
    {
        if (id == Helper::blankId())
        {
            continue;
        }
        const auto it = values.find(id);
        const QString value = it != values.cend() ? it->second : QString();
        hasValue = hasValue || !value.isEmpty();
        parts << value;
    }
    map.insert(getSourceValuesParameter(), hasValue ? parts.join(getSourceValuesSeparator()) : QString());

    QPointer<PDFAppInsertToolGUIBase> guard(this);
    const QString error = model->setValues(map);
    if (!guard || error.isEmpty())
    {
        return;
    }

    // Refused (a list too long for the parameter): the list follows the values again, the
    // reason is said last
    readValues();
    if (guard && getHost())
    {
        getHost()->showTransientMessage(error);
    }
}

void PDFAppInsertToolGUIBase::readValues()
{
    using Helper = PDFAppInsertToolGUIHelper;

    const PDFAppToolParameterModel* model = getModel();
    if (!model || !m_list)
    {
        return;
    }

    // The inputs of the list follow the values (Undo / Redo: a parked input comes back without a
    // second reading, a removed one is parked); the list announces it, the slot waits
    QStringList ids = getModelInputIds();
    ids.removeAll(Helper::blankId());
    QPointer<PDFAppInsertToolGUIBase> guard(this);
    if (ids != m_list->getInputIds())
    {
        m_isChangingList = true;
        m_list->setInputIds(ids);
        if (!guard)
        {
            return;
        }
        m_isChangingList = false;
        if (!m_list)
        {
            return;
        }
    }

    // The parked inputs, which no step of the history names, are freed (R12 of step 2)
    QStringList historyIds;
    for (const QVariant& value : model->getHistoryValues(Helper::name(PDFDocumentToolInsertBase::PARAMETER_INPUTS)))
    {
        historyIds << Helper::splitIds(value.toString());
    }
    m_list->dropParkedInputs(historyIds);
    if (!guard || !m_list)
    {
        return;
    }

    // The current source stays while it is a source
    const QStringList modelIds = getModelInputIds();
    if (!modelIds.contains(m_currentInput))
    {
        m_currentInput = modelIds.value(0);
    }
    if (m_list->getLoadingCount() > 0 && m_progressTimer && !m_progressTimer->isActive())
    {
        m_progressTimer->start();
    }

    updateParts();
    if (guard)
    {
        updateCommon();
    }
}

void PDFAppInsertToolGUIBase::updateCommon()
{
    QPointer<PDFAppInsertToolGUIBase> guard(this);
    updateStrip();
    updateToolbar();
    updatePositionRows();
    updateOpenDocumentRows(getPanel(QStringLiteral("add")));

    // The shell says the Primary button and the status (its slots may delete this GUI)
    updateConfirmState();
    if (guard)
    {
        updateStatusText();
    }
}

void PDFAppInsertToolGUIBase::announceSources()
{
    // The list is no list of the host: the shell does not see its changes. A reading finished,
    // a password accepted: the preview is computed again (prepareEnvironment gives the runner the
    // generation of the inputs, K33), then the parts say the new state
    QPointer<PDFAppInsertToolGUIBase> guard(this);
    if (PDFDocumentToolShell* shell = getShell())
    {
        shell->refreshPreview();
        if (!guard)
        {
            return;
        }
    }
    updateCommon();
}

QStringList PDFAppInsertToolGUIBase::getRunInputIds() const
{
    QStringList ids;
    for (const QString& id : getModelInputIds())
    {
        if (isUsableInput(id))
        {
            ids << id;
        }
    }
    return ids;
}

bool PDFAppInsertToolGUIBase::isUsableInput(const QString& inputId) const
{
    const PDFAppToolInput* input = m_list && m_list->indexOf(inputId) >= 0 ? m_list->findInput(inputId) : nullptr;
    return input && input->isUsable();
}

void PDFAppInsertToolGUIBase::onListInputsChanged()
{
    // The changes of this GUI come with their values; another change is shown as it is
    if (!m_isChangingList)
    {
        QPointer<PDFAppInsertToolGUIBase> guard(this);
        readValues();
        if (guard)
        {
            announceSources();
        }
    }
}

void PDFAppInsertToolGUIBase::onListInputChanged(const QString& inputId)
{
    Q_UNUSED(inputId);

    // A reading has finished or a password was accepted: one update for the readings of one pass
    // of the event loop (many files finish together)
    if (!m_isChangingList && !m_isReadQueued)
    {
        m_isReadQueued = true;
        QMetaObject::invokeMethod(this, &PDFAppInsertToolGUIBase::onReadQueued, Qt::QueuedConnection);
    }
}

void PDFAppInsertToolGUIBase::onReadQueued()
{
    m_isReadQueued = false;
    QPointer<PDFAppInsertToolGUIBase> guard(this);
    readValues();
    if (guard)
    {
        announceSources();
    }
}

void PDFAppInsertToolGUIBase::onProgressTimer()
{
    // The reading of the files: the bars of the rows and the callouts of the cards
    if (!m_list || m_list->getLoadingCount() == 0)
    {
        if (m_progressTimer)
        {
            m_progressTimer->stop();
        }
        return;
    }
    updateParts();
}

// ------------------------------------------------------------- the place

PDFInteger PDFAppInsertToolGUIBase::getDocumentPageCount() const
{
    const PDFAppDocument* document = getDocument();
    return document ? document->getPageCount() : 0;
}

PDFInteger PDFAppInsertToolGUIBase::getResultPageCount() const
{
    return getDocumentPageCount() + getInsertedPageCount() - PDFInteger(getReplacedPages().size());
}

int PDFAppInsertToolGUIBase::getGap() const
{
    using Helper = PDFAppInsertToolGUIHelper;

    // The gap of the strip of the parameters (PDFDocumentToolInsertBase::resolvePlacement)
    const int pageCount = Helper::toInt(getDocumentPageCount());
    const QString position = getParameter(PDFDocumentToolInsertBase::PARAMETER_POSITION);
    const int page = Helper::toInt(getParameter(PDFDocumentToolInsertBase::PARAMETER_PAGE).toLongLong());
    if (position == QLatin1String(PDFDocumentToolInsertBase::POSITION_BEFORE))
    {
        return qBound(0, page - 1, pageCount);
    }
    if (position == QLatin1String(PDFDocumentToolInsertBase::POSITION_AFTER))
    {
        return qBound(0, page, pageCount);
    }
    if (position == QLatin1String(PDFDocumentToolInsertBase::POSITION_START))
    {
        return 0;
    }
    if (position == QLatin1String(PDFDocumentToolInsertBase::POSITION_REPLACE))
    {
        return -1;
    }
    return pageCount;
}

std::vector<PDFInteger> PDFAppInsertToolGUIBase::getReplacedPages() const
{
    const PDFAppToolParameterModel* model = getModel();
    const PDFAppDocument* document = getDocument();
    if (!model || !document || !document->getDocument() || getParameter(PDFDocumentToolInsertBase::PARAMETER_POSITION) != QLatin1String(PDFDocumentToolInsertBase::POSITION_REPLACE))
    {
        return std::vector<PDFInteger>();
    }

    QString error;
    const std::optional<PDFDocumentToolPageSelection> selection = PDFDocumentToolPageSelection::fromVariant(model->getValue(PDFAppInsertToolGUIHelper::name(PDFDocumentToolInsertPages::PARAMETER_REPLACE_PAGES)), &error);
    if (!selection || selection->isNone)
    {
        return std::vector<PDFInteger>();
    }
    std::vector<PDFInteger> pages = selection->getPageIndices(document->getDocument().data(), &error);
    std::sort(pages.begin(), pages.end());
    pages.erase(std::unique(pages.begin(), pages.end()), pages.end());
    return pages;
}

PDFInteger PDFAppInsertToolGUIBase::getInsertionIndex() const
{
    // The first new page lies at the gap; Replace puts it in the place of the first replaced page
    const int gap = getGap();
    if (gap >= 0)
    {
        return gap;
    }
    const std::vector<PDFInteger> replaced = getReplacedPages();
    return replaced.empty() ? 0 : replaced.front();
}

PDFInteger PDFAppInsertToolGUIBase::getNeighborPage() const
{
    // PDFDocumentToolInsertBase::resolvePlacement: before / after page N - page N; the start - the
    // first page; the end - the last page; Replace - the first replaced page
    const PDFInteger pageCount = getDocumentPageCount();
    const QString position = getParameter(PDFDocumentToolInsertBase::PARAMETER_POSITION);
    const PDFInteger page = getParameter(PDFDocumentToolInsertBase::PARAMETER_PAGE).toLongLong();
    PDFInteger neighbor = 0;
    if (position == QLatin1String(PDFDocumentToolInsertBase::POSITION_BEFORE) || position == QLatin1String(PDFDocumentToolInsertBase::POSITION_AFTER))
    {
        neighbor = page - 1;
    }
    else if (position == QLatin1String(PDFDocumentToolInsertBase::POSITION_END))
    {
        neighbor = pageCount - 1;
    }
    else if (position == QLatin1String(PDFDocumentToolInsertBase::POSITION_REPLACE))
    {
        const std::vector<PDFInteger> replaced = getReplacedPages();
        neighbor = replaced.empty() ? 0 : replaced.front();
    }
    return qBound<PDFInteger>(0, neighbor, qMax<PDFInteger>(0, pageCount - 1));
}

QVariantMap PDFAppInsertToolGUIBase::createGapValues(int gap) const
{
    using Helper = PDFAppInsertToolGUIHelper;

    // The gap 0 is At the beginning, the page count At the end, else After page <gap>
    const int pageCount = Helper::toInt(getDocumentPageCount());
    QVariantMap values;
    if (gap <= 0)
    {
        values.insert(Helper::name(PDFDocumentToolInsertBase::PARAMETER_POSITION), QString::fromLatin1(PDFDocumentToolInsertBase::POSITION_START));
    }
    else if (gap >= pageCount)
    {
        values.insert(Helper::name(PDFDocumentToolInsertBase::PARAMETER_POSITION), QString::fromLatin1(PDFDocumentToolInsertBase::POSITION_END));
    }
    else
    {
        values.insert(Helper::name(PDFDocumentToolInsertBase::PARAMETER_POSITION), QString::fromLatin1(PDFDocumentToolInsertBase::POSITION_AFTER));
        values.insert(Helper::name(PDFDocumentToolInsertBase::PARAMETER_PAGE), qint64(gap));
    }
    return values;
}

void PDFAppInsertToolGUIBase::setGap(int gap)
{
    PDFAppToolParameterModel* model = getModel();
    if (!model || !isEditable() || gap < 0)
    {
        return;
    }

    // One undo step; the model announces it (the strip, the menu, the texts)
    QPointer<PDFAppInsertToolGUIBase> guard(this);
    const QString error = model->setValues(createGapValues(gap));
    if (guard && !error.isEmpty() && getHost())
    {
        getHost()->showTransientMessage(error);
    }
}

void PDFAppInsertToolGUIBase::choosePosition(const QString& positionId)
{
    using Helper = PDFAppInsertToolGUIHelper;

    PDFAppToolParameterModel* model = getModel();
    PDFAppToolHost* host = getHost();
    if (!model || !host || !isEditable())
    {
        return;
    }

    const PDFInteger pageCount = getDocumentPageCount();
    const PDFInteger current = qBound<PDFInteger>(0, host->getCurrentPage(), qMax<PDFInteger>(0, pageCount - 1)) + 1;
    QVariantMap values;
    values.insert(Helper::name(PDFDocumentToolInsertBase::PARAMETER_POSITION), positionId);
    if (positionId == QLatin1String(PDFDocumentToolInsertBase::POSITION_BEFORE) || positionId == QLatin1String(PDFDocumentToolInsertBase::POSITION_AFTER))
    {
        values.insert(Helper::name(PDFDocumentToolInsertBase::PARAMETER_PAGE), qint64(current));
    }
    else if (positionId == QLatin1String(PDFDocumentToolInsertBase::POSITION_REPLACE))
    {
        // K36: the pages selected in Thumbnails now (a copy of their indices); the current page
        // alone is no selection (388:16103 "Available when pages are selected in Thumbnails")
        std::vector<PDFInteger> pages = host->getExplicitlySelectedPages();
        std::sort(pages.begin(), pages.end());
        pages.erase(std::unique(pages.begin(), pages.end()), pages.end());
        if (pages.empty())
        {
            host->showTransientMessage(tr("Select the pages to replace in Thumbnails"));
            return;
        }
        PDFDocumentToolPageSelection selection;
        selection.pages = PDFDocumentToolPageSelection::createRangeText(pages);
        values.insert(Helper::name(PDFDocumentToolInsertPages::PARAMETER_REPLACE_PAGES), selection.toVariant());
    }

    QPointer<PDFAppInsertToolGUIBase> guard(this);
    const QString error = model->setValues(values);
    if (guard && !error.isEmpty() && getHost())
    {
        getHost()->showTransientMessage(error);
    }
}

QString PDFAppInsertToolGUIBase::getPositionValueText() const
{
    // Summary 388:16097 "After page 12", 388:17167 "Replace 13–14"
    const QString position = getParameter(PDFDocumentToolInsertBase::PARAMETER_POSITION);
    const QString page = PDFAppLocale::formatNumber(getParameter(PDFDocumentToolInsertBase::PARAMETER_PAGE).toLongLong());
    if (position == QLatin1String(PDFDocumentToolInsertBase::POSITION_BEFORE))
    {
        return tr("Before page %1").arg(page);
    }
    if (position == QLatin1String(PDFDocumentToolInsertBase::POSITION_AFTER))
    {
        return tr("After page %1").arg(page);
    }
    if (position == QLatin1String(PDFDocumentToolInsertBase::POSITION_START))
    {
        return tr("At the beginning");
    }
    if (position == QLatin1String(PDFDocumentToolInsertBase::POSITION_REPLACE))
    {
        return tr("Replace %1").arg(PDFAppLocale::formatPageNumbers(getReplacedPages()));
    }
    return tr("At the end");
}

QString PDFAppInsertToolGUIBase::getPlaceClause() const
{
    // The status bar 388:15522 "6 pages after page 12", 388:17167 "6 pages replace pages 13–14"
    const QString position = getParameter(PDFDocumentToolInsertBase::PARAMETER_POSITION);
    const QString page = PDFAppLocale::formatNumber(getParameter(PDFDocumentToolInsertBase::PARAMETER_PAGE).toLongLong());
    if (position == QLatin1String(PDFDocumentToolInsertBase::POSITION_BEFORE))
    {
        return tr("before page %1").arg(page);
    }
    if (position == QLatin1String(PDFDocumentToolInsertBase::POSITION_AFTER))
    {
        return tr("after page %1").arg(page);
    }
    if (position == QLatin1String(PDFDocumentToolInsertBase::POSITION_START))
    {
        return tr("at the beginning");
    }
    if (position == QLatin1String(PDFDocumentToolInsertBase::POSITION_REPLACE))
    {
        return tr("replace pages %1").arg(PDFAppLocale::formatPageNumbers(getReplacedPages()));
    }
    return tr("at the end");
}

// ------------------------------------------------------------- the actions of the user

void PDFAppInsertToolGUIBase::setCurrentSource(const QString& inputId)
{
    if (m_currentInput == inputId || !getModelInputIds().contains(inputId))
    {
        return;
    }
    m_currentInput = inputId;
    QPointer<PDFAppInsertToolGUIBase> guard(this);
    updateParts();
    if (guard)
    {
        updateCommon();
    }
}

void PDFAppInsertToolGUIBase::addResults(const std::vector<PDFAppToolInputList::AddResult>& results, int gap, const QStringList& refusedTexts)
{
    if (!m_list)
    {
        return;
    }

    QStringList refused = refusedTexts;
    QStringList addedIds;
    for (const PDFAppToolInputList::AddResult& result : results)
    {
        if (result.id.isEmpty())
        {
            refused << PDFAppToolInputList::getRefusalText(result.refusal, result.name, m_list->getMaximumCount());
        }
        else
        {
            addedIds << result.id;
        }
    }

    // The new sources go to the end of the order (a drop at a place of the strip moves the place
    // there in the same undo step); the refusals are said in the status bar, last
    QPointer<PDFAppInsertToolGUIBase> guard(this);
    if (!addedIds.isEmpty())
    {
        QStringList ids = getModelInputIds();
        ids << addedIds;
        m_currentInput = addedIds.front();
        applySources(ids, getSourceValues(), gap >= 0 ? createGapValues(gap) : QVariantMap());
    }
    if (guard && !refused.isEmpty() && getHost())
    {
        getHost()->showTransientMessage(refused.join(QStringLiteral(" · ")));
    }
}

void PDFAppInsertToolGUIBase::addFiles(const QStringList& filePaths, int gap, const QStringList& refusedTexts)
{
    if (!m_list || !isEditable() || (filePaths.isEmpty() && refusedTexts.isEmpty()))
    {
        return;
    }

    // The list announces each addition, the values follow at once as one undo step
    QPointer<PDFAppInsertToolGUIBase> guard(this);
    std::vector<PDFAppToolInputList::AddResult> results;
    if (!filePaths.isEmpty())
    {
        m_isChangingList = true;
        results = m_list->addFiles(filePaths);
        if (!guard)
        {
            return;
        }
        m_isChangingList = false;
    }
    addResults(results, gap, refusedTexts);
}

void PDFAppInsertToolGUIBase::addFilesFromDialog()
{
    if (!isEditable())
    {
        return;
    }

    // The native dialog runs its own event loop: the GUI may go meanwhile
    QPointer<PDFAppInsertToolGUIBase> guard(this);
    QWidget* area = getHost() ? getHost()->getCanvasArea() : nullptr;
    const QStringList files = PDFAppFileDialogs::getOpenInputFileNames(area ? area->window() : nullptr, QString());
    if (guard && !files.isEmpty())
    {
        addFiles(files);
    }
}

void PDFAppInsertToolGUIBase::pasteImage()
{
    if (!m_list || !isEditable())
    {
        return;
    }

    const QClipboard* clipboard = QGuiApplication::clipboard();
    const QImage image = clipboard ? clipboard->image() : QImage();
    if (image.isNull())
    {
        if (getHost())
        {
            getHost()->showTransientMessage(tr("The clipboard has no image"));
        }
        return;
    }

    QPointer<PDFAppInsertToolGUIBase> guard(this);
    m_isChangingList = true;
    const PDFAppToolInputList::AddResult result = m_list->addImage(image, QString());
    if (!guard)
    {
        return;
    }
    m_isChangingList = false;
    addResults({ result }, -1, QStringList());
}

void PDFAppInsertToolGUIBase::addOpenDocument(const QString& documentId)
{
    const PDFAppDocumentRegistry* registry = getContext() ? getContext()->getDocumentRegistry() : nullptr;
    PDFAppDocument* document = registry ? registry->findDocument(documentId) : nullptr;
    if (!m_list || !document || !isEditable())
    {
        return;
    }

    // K24, R11 of step 2: a snapshot of now (an unsaved tab: its working copy), the tab is not followed
    QPointer<PDFAppInsertToolGUIBase> guard(this);
    m_isChangingList = true;
    const PDFAppToolInputList::AddResult result = m_list->addOpenDocument(document);
    if (!guard)
    {
        return;
    }
    m_isChangingList = false;
    addResults({ result }, -1, QStringList());
}

void PDFAppInsertToolGUIBase::removeInput(const QString& inputId)
{
    using Helper = PDFAppInsertToolGUIHelper;

    QStringList ids = getModelInputIds();
    const int index = int(ids.indexOf(inputId));
    if (!isEditable() || index < 0)
    {
        return;
    }

    // The list parks the input (Undo takes it back); the blank pages lose their count
    std::map<QString, QString> values = getSourceValues();
    values.erase(inputId);
    ids.removeAt(index);
    QVariantMap extra;
    if (inputId == Helper::blankId())
    {
        extra.insert(Helper::name(PDFDocumentToolInsertPages::PARAMETER_BLANK_PAGES), 0);
    }
    if (m_currentInput == inputId)
    {
        m_currentInput = ids.value(qMin(index, int(ids.size()) - 1));
    }
    applySources(ids, values, extra);
}

void PDFAppInsertToolGUIBase::moveInput(const QString& inputId, int targetIndex)
{
    QStringList ids = getModelInputIds();
    const int index = int(ids.indexOf(inputId));
    if (!isEditable() || index < 0 || targetIndex < 0 || targetIndex > ids.size() || targetIndex == index || targetIndex == index + 1)
    {
        return;
    }

    // The source goes in front of the target (the order of the new pages)
    const std::map<QString, QString> values = getSourceValues();
    ids.removeAt(index);
    ids.insert(targetIndex > index ? targetIndex - 1 : targetIndex, inputId);
    applySources(ids, values);
}

void PDFAppInsertToolGUIBase::removeCurrentSource()
{
    removeInput(m_currentInput);
}

void PDFAppInsertToolGUIBase::openPasswordDialog(const QString& inputId)
{
    if (!m_list)
    {
        return;
    }
    if (m_passwordDialog)
    {
        m_passwordDialog->raise();
        return;
    }

    // 388:17789: the dialog of Merge - a popup (new + QPointer, deleted when it is closed); Remove
    // File asks this GUI, which removes the input by its Undo history
    QWidget* area = getHost() ? getHost()->getCanvasArea() : nullptr;
    m_passwordDialog = PDFAppPasswordDialog::openDialog(m_list, inputId, area ? area->window() : nullptr);
    if (m_passwordDialog)
    {
        connect(m_passwordDialog.data(), &PDFAppPasswordDialog::removeRequested, this, &PDFAppInsertToolGUIBase::onPasswordRemoveRequested);
    }
}

void PDFAppInsertToolGUIBase::onPasswordRemoveRequested(const QString& inputId)
{
    removeInput(inputId);
}

bool PDFAppInsertToolGUIBase::dropFiles(const QMimeData* mimeData)
{
    if (!PDFAppFileDrop::hasLocalFiles(mimeData) || !isEditable())
    {
        return false;
    }

    // K8 of step 2: PDF files and images, in the dropped order; the refused items are said
    const PDFAppFileDrop::Result result = PDFAppFileDrop::getFiles(mimeData, PDFAppFileDrop::MAXIMUM_FILES, true);
    QStringList refused;
    for (const PDFAppFileDrop::RefusedItem& item : result.refused)
    {
        refused << tr("%1 – %2").arg(item.text, PDFAppFileDrop::getRefusalText(item.refusal));
    }
    addFiles(result.files, -1, refused);
    return true;
}

// ------------------------------------------------------------- the parts

void PDFAppInsertToolGUIBase::installKeys()
{
    PDFAppToolHost* host = getHost();
    if (m_keyWidget || !host || !host->getKeyWidget())
    {
        return;
    }

    // The keys of the frames on the key widget of the host (they reach every part of the tab) and
    // the drop of files anywhere in the tab; the destructor takes them back
    m_keyWidget = host->getKeyWidget();
    for (const KeyAction& keyAction : getKeyActions())
    {
        QAction* action = new QAction(m_keyWidget);
        action->setObjectName(QStringLiteral("insertKey-") + keyAction.name);
        action->setShortcut(keyAction.keys);
        action->setShortcutContext(Qt::WidgetWithChildrenShortcut);
        connect(action, &QAction::triggered, this, keyAction.slot);
        m_keyWidget->addAction(action);
        m_keyActions.push_back(action);
    }
    m_wasDropAccepted = m_keyWidget->acceptDrops();
    m_keyWidget->setAcceptDrops(true);
    m_keyWidget->installEventFilter(this);
}

void PDFAppInsertToolGUIBase::describeToolbar(PDFAppToolPanelBuilder& builder)
{
    connectList();
    m_actions.clear();
    m_panels.clear();
    m_panelRows.clear();
    m_openDocumentRows.clear();
    m_openDocumentsCaption = nullptr;

    // tb-add 388:15580 with panel-add 388:16725
    PDFAppToolPanelPopup* addPanel = builder.addMenu(QStringLiteral("add"), QStringLiteral("plus"), getAddButtonText(), ADD_PANEL_WIDTH);
    addPanel->getButton()->setObjectName(QStringLiteral("insertAction-add"));
    addPanel->getButton()->setToolTip(getAddButtonToolTip());
    m_panels[QStringLiteral("add")] = addPanel;
    m_actions[QStringLiteral("add")] = addPanel->getButton();
    describeAddMenu(addPanel);

    describeActionsBeforePosition(builder);

    // tb-position 388:15585 with panel-position 388:16769
    PDFAppToolPanelPopup* positionPanel = builder.addMenu(QStringLiteral("position"), QStringLiteral("insert-after"), QString(), POSITION_PANEL_WIDTH);
    positionPanel->getButton()->setObjectName(QStringLiteral("insertAction-position"));
    m_panels[QStringLiteral("position")] = positionPanel;
    m_actions[QStringLiteral("position")] = positionPanel->getButton();
    struct PositionRow
    {
        const char* id;
        const char* iconName;
    };
    const PositionRow positionRows[] = {
        { PDFDocumentToolInsertBase::POSITION_BEFORE, "insert-before" },
        { PDFDocumentToolInsertBase::POSITION_AFTER, "insert-after" },
        { PDFDocumentToolInsertBase::POSITION_START, "insert-first" },
        { PDFDocumentToolInsertBase::POSITION_END, "insert-last" },
        { PDFDocumentToolInsertBase::POSITION_REPLACE, "replace" },
    };
    for (const PositionRow& positionRow : positionRows)
    {
        const QString id = QString::fromLatin1(positionRow.id);
        if (id == QLatin1String(PDFDocumentToolInsertBase::POSITION_REPLACE))
        {
            if (!hasReplace())
            {
                continue;
            }
            positionPanel->addGroupDivider();
        }
        PDFTileRow* row = positionPanel->addRow(QString(), QString(), QString::fromLatin1(positionRow.iconName), PDFTileRow::Kind::MenuItem);
        row->setObjectName(QStringLiteral("insertPosition-") + id);
        row->setProperty(PDFAppInsertToolGUIHelper::ROW_KEY, id);
        row->setSelectionBehavior(PDFComponentButton::SelectionBehavior::Exclusive);
        connect(row, &PDFTileRow::clicked, this, &PDFAppInsertToolGUIBase::onPositionRowClicked);
        m_panelRows[QStringLiteral("position-") + id] = row;
    }

    describeActionsAfterPosition(builder);
    updateToolbar();
    updatePositionRows();
}

void PDFAppInsertToolGUIBase::describeActionsBeforePosition(PDFAppToolPanelBuilder& builder)
{
    Q_UNUSED(builder);
}

void PDFAppInsertToolGUIBase::describeActionsAfterPosition(PDFAppToolPanelBuilder& builder)
{
    Q_UNUSED(builder);
}

void PDFAppInsertToolGUIBase::describeLeftPanelTop(PDFAppToolPanelBuilder& builder)
{
    Q_UNUSED(builder);
}

void PDFAppInsertToolGUIBase::describeLeftPanel(PDFAppToolPanelBuilder& builder)
{
    connectList();
    describeLeftPanelTop(builder);

    // sidebar-left 388:15514: "Insert position" / "Handbook.pdf · 48 pages", the strip (position
    // 388:15601: 275 wide, 12 from the edge - 2 more than the padding of the panel) and the hint
    QWidget* box = new QWidget(builder.getWidget());
    box->setObjectName(QStringLiteral("insertPositionBox"));
    QVBoxLayout* boxLayout = new QVBoxLayout(box);
    boxLayout->setContentsMargins(2, 0, 2, 0);
    boxLayout->setSpacing(10);
    m_positionHeader = new PDFAppToolPanelHeader(tr("Insert position"), QString(), box);
    m_positionHeader->setObjectName(QStringLiteral("insertPositionHeader"));
    boxLayout->addWidget(m_positionHeader);
    m_strip = new PDFAppInsertPositionStrip(box);
    const PDFAppDocument* document = getDocument();
    m_strip->setDocument(document ? document->getDocument() : PDFDocumentPointer(), document ? document->getId() : QString(), getHost() ? getHost()->getView() : nullptr);
    connect(m_strip.data(), &PDFAppInsertPositionStrip::gapChosen, this, &PDFAppInsertToolGUIBase::onStripGapChosen);
    connect(m_strip.data(), &PDFAppInsertPositionStrip::filesDropped, this, &PDFAppInsertToolGUIBase::onStripFilesDropped);
    boxLayout->addWidget(m_strip);
    // 388:15743: Regular 11 text/muted in lines of 13
    m_stripHint = PDFAppPageWidgets::createText(QString(), PDFThemeFonts::getFont(QFont::Normal, 11), 13, PDFThemeColors::TextMuted, box);
    m_stripHint->setObjectName(QStringLiteral("insertPositionHint"));
    m_stripHint->setMinimumWidth(1);
    boxLayout->addWidget(m_stripHint);
    builder.addWidget(box);
    updateStrip();
}

void PDFAppInsertToolGUIBase::updateStrip()
{
    using Helper = PDFAppInsertToolGUIHelper;

    if (!m_strip)
    {
        return;
    }

    const std::vector<PDFInteger> replaced = getReplacedPages();
    m_strip->setPlace(getGap(), replaced);
    m_strip->setEnabled(isEditable());
    if (m_positionHeader)
    {
        m_positionHeader->setTexts(tr("Insert position"), tr("%1 · %2").arg(getDocumentTitle(), Helper::pages(getDocumentPageCount())));
    }
    if (m_stripHint)
    {
        // 388:15743; 388:17167 "Pages 13–14 are selected in Thumbnails and will be replaced by the inserted pages."
        const QString pages = PDFAppLocale::formatPageNumbers(replaced);
        m_stripHint->setText(replaced.empty() ? tr("Click between two pages to move the insertion line, or drop files there.")
                                              : (replaced.size() == 1 ? tr("Page %1 is selected in Thumbnails and will be replaced by the inserted pages.").arg(pages)
                                                                      : tr("Pages %1 are selected in Thumbnails and will be replaced by the inserted pages.").arg(pages)));
    }
}

void PDFAppInsertToolGUIBase::updateToolbar()
{
    for (const char* name : { "add", "position" })
    {
        if (QToolButton* button = getAction(QString::fromLatin1(name)))
        {
            button->setEnabled(isEditable());
        }
    }
}

void PDFAppInsertToolGUIBase::updatePositionRows()
{
    using Helper = PDFAppInsertToolGUIHelper;

    const PDFAppToolHost* host = getHost();
    const PDFInteger pageCount = getDocumentPageCount();
    const PDFInteger current = host ? qBound<PDFInteger>(0, host->getCurrentPage(), qMax<PDFInteger>(0, pageCount - 1)) + 1 : 1;
    const QString currentText = PDFAppLocale::formatNumber(current);
    const QString position = getParameter(PDFDocumentToolInsertBase::PARAMETER_POSITION);
    const PDFInteger page = getParameter(PDFDocumentToolInsertBase::PARAMETER_PAGE).toLongLong();
    const bool hasDescriptions = hasPositionDescriptions();

    // panel-position 388:16769 (Add Image 388:19607: no descriptions, no Replace)
    struct RowText
    {
        const char* id;
        QString title;
        QString description;
        bool isChecked;
    };
    const RowText rows[] = {
        { PDFDocumentToolInsertBase::POSITION_BEFORE, tr("Before page %1").arg(currentText), tr("Ahead of the current page"), position == QLatin1String(PDFDocumentToolInsertBase::POSITION_BEFORE) && page == current },
        { PDFDocumentToolInsertBase::POSITION_AFTER, tr("After page %1").arg(currentText), tr("Following the current page"), position == QLatin1String(PDFDocumentToolInsertBase::POSITION_AFTER) && page == current },
        { PDFDocumentToolInsertBase::POSITION_START, tr("At the beginning"), tr("Before page 1"), position == QLatin1String(PDFDocumentToolInsertBase::POSITION_START) },
        { PDFDocumentToolInsertBase::POSITION_END, tr("At the end"), tr("After page %1").arg(PDFAppLocale::formatNumber(pageCount)), position == QLatin1String(PDFDocumentToolInsertBase::POSITION_END) },
        { PDFDocumentToolInsertBase::POSITION_REPLACE, tr("Replace selected pages"), tr("Available when pages are selected in Thumbnails"), position == QLatin1String(PDFDocumentToolInsertBase::POSITION_REPLACE) },
    };
    for (const RowText& rowText : rows)
    {
        PDFTileRow* row = getPanelRow(QStringLiteral("position-") + QString::fromLatin1(rowText.id));
        if (!row)
        {
            continue;
        }
        const QSignalBlocker blocker(row);
        row->setTitle(rowText.title);
        row->setDescription(hasDescriptions ? rowText.description : QString());
        row->setChecked(rowText.isChecked);
        if (QLatin1String(rowText.id) == QLatin1String(PDFDocumentToolInsertBase::POSITION_REPLACE))
        {
            // K36: an explicit selection of Thumbnails (the current page is none)
            row->setEnabled(host && !host->getExplicitlySelectedPages().empty());
        }
    }

    if (QToolButton* button = getAction(QStringLiteral("position")))
    {
        // tb-position 388:15585 "Position: After page 12", 388:17167 "Position: Replace pages 13–14"
        const bool isReplace = position == QLatin1String(PDFDocumentToolInsertBase::POSITION_REPLACE);
        const QString value = isReplace ? tr("Replace pages %1").arg(PDFAppLocale::formatPageNumbers(getReplacedPages())) : getPositionValueText();
        button->setText(tr("Position: %1").arg(value));
        button->setToolTip(button->text());
        QString iconName = QStringLiteral("insert-after");
        if (position == QLatin1String(PDFDocumentToolInsertBase::POSITION_BEFORE))
        {
            iconName = QStringLiteral("insert-before");
        }
        else if (position == QLatin1String(PDFDocumentToolInsertBase::POSITION_START))
        {
            iconName = QStringLiteral("insert-first");
        }
        else if (position == QLatin1String(PDFDocumentToolInsertBase::POSITION_END))
        {
            iconName = QStringLiteral("insert-last");
        }
        else if (isReplace)
        {
            iconName = QStringLiteral("replace");
        }
        if (button->property(Helper::ROW_KEY).toString() != iconName)
        {
            button->setProperty(Helper::ROW_KEY, iconName);
            button->setIcon(PDFThemeIcons::getIcon(iconName));
        }
    }
}

void PDFAppInsertToolGUIBase::onPositionRowClicked()
{
    const QObject* row = sender();
    const QString positionId = row ? row->property(PDFAppInsertToolGUIHelper::ROW_KEY).toString() : QString();

    // The panel closes first (it may run code of the application), then the place changes
    QPointer<PDFAppInsertToolGUIBase> guard(this);
    closePanel(QStringLiteral("position"));
    if (guard && !positionId.isEmpty())
    {
        choosePosition(positionId);
    }
}

void PDFAppInsertToolGUIBase::onAddFilesRowClicked()
{
    QPointer<PDFAppInsertToolGUIBase> guard(this);
    closePanel(QStringLiteral("add"));
    if (guard)
    {
        addFilesFromDialog();
    }
}

void PDFAppInsertToolGUIBase::onPasteRowClicked()
{
    QPointer<PDFAppInsertToolGUIBase> guard(this);
    closePanel(QStringLiteral("add"));
    if (guard)
    {
        pasteImage();
    }
}

void PDFAppInsertToolGUIBase::onOpenDocumentRowClicked()
{
    const QObject* row = sender();
    const QString documentId = row ? row->property(PDFAppInsertToolGUIHelper::ROW_KEY).toString() : QString();
    QPointer<PDFAppInsertToolGUIBase> guard(this);
    closePanel(QStringLiteral("add"));
    if (guard && !documentId.isEmpty())
    {
        addOpenDocument(documentId);
    }
}

void PDFAppInsertToolGUIBase::onEmptyStateAddFiles()
{
    addFilesFromDialog();
}

void PDFAppInsertToolGUIBase::onEmptyStateAddDocument(const QString& documentId)
{
    addOpenDocument(documentId);
}

void PDFAppInsertToolGUIBase::onStripGapChosen(int gap)
{
    setGap(gap);
}

void PDFAppInsertToolGUIBase::onStripFilesDropped(int gap, const QStringList& filePaths, const QStringList& refusedTexts)
{
    addFiles(filePaths, gap, refusedTexts);
}

void PDFAppInsertToolGUIBase::onShellStateChanged(PDFDocumentToolShell::State state)
{
    m_isDeciding = state == PDFDocumentToolShell::State::Deciding;
    QPointer<PDFAppInsertToolGUIBase> guard(this);
    updateParts();
    if (guard)
    {
        updateCommon();
    }
}

void PDFAppInsertToolGUIBase::onHostPagesChanged()
{
    updatePositionRows();
}

void PDFAppInsertToolGUIBase::onRegistryChanged()
{
    // A document opened, changed or closed: the rows of the open documents and the empty state
    QPointer<PDFAppInsertToolGUIBase> guard(this);
    updateOpenDocumentRows(getPanel(QStringLiteral("add")));
    if (guard)
    {
        updateParts();
    }
}

std::vector<PDFAppDocument*> PDFAppInsertToolGUIBase::getOpenAppDocuments() const
{
    // The ready documents of the tabs of this window, but the document of the tool
    std::vector<PDFAppDocument*> documents;
    const PDFAppDocumentRegistry* registry = getContext() ? getContext()->getDocumentRegistry() : nullptr;
    const QWidget* area = getHost() ? getHost()->getCanvasArea() : nullptr;
    const PDFAppMainWindow* window = area ? PDFAppPanelWidgets::findMainWindow(area) : nullptr;
    const PDFAppDocument* own = getDocument();
    if (registry)
    {
        for (PDFAppDocument* document : registry->getDocuments())
        {
            if (document != own && document->isReady() && (!window || window->findDocumentTab(document->getId())))
            {
                documents.push_back(document);
            }
        }
    }
    return documents;
}

std::vector<PDFAppInsertEmptyState::Document> PDFAppInsertToolGUIBase::getOpenDocuments() const
{
    using Helper = PDFAppInsertToolGUIHelper;

    std::vector<PDFAppInsertEmptyState::Document> documents;
    for (const PDFAppDocument* document : getOpenAppDocuments())
    {
        PDFAppInsertEmptyState::Document item;
        item.documentId = document->getId();
        item.name = document->getTitle();
        item.isModified = document->isModified();
        // 398:29249 "3 pages · the current unsaved state is used"
        item.details = item.isModified ? tr("%1 · the current unsaved state is used").arg(Helper::pages(document->getPageCount())) : PDFAppToolDocumentChooser::createNumbers(document);
        documents.push_back(item);
    }
    return documents;
}

void PDFAppInsertToolGUIBase::updateOpenDocumentRows(PDFAppToolPanelPopup* panel)
{
    using Helper = PDFAppInsertToolGUIHelper;

    if (!panel)
    {
        return;
    }

    // "Open documents" 388:16760: a row of each ready document of the window; a document in the
    // list is disabled ("already in the list", K29), an unsaved one uses its state now (K24)
    const std::vector<PDFAppDocument*> documents = getOpenAppDocuments();
    if (!m_openDocumentsCaption && !documents.empty())
    {
        panel->addGroupDivider();
        m_openDocumentsCaption = panel->addGroupCaption(tr("Open documents"));
    }
    for (size_t i = 0; i < documents.size(); ++i)
    {
        PDFAppDocument* document = documents[i];
        if (i >= m_openDocumentRows.size() || !m_openDocumentRows[i])
        {
            PDFTileRow* row = panel->addRow(QString(), QString(), QStringLiteral("tab"), PDFTileRow::Kind::MenuItem);
            connect(row, &PDFTileRow::clicked, this, &PDFAppInsertToolGUIBase::onOpenDocumentRowClicked);
            if (i < m_openDocumentRows.size())
            {
                m_openDocumentRows[i] = row;
            }
            else
            {
                m_openDocumentRows.push_back(row);
            }
        }

        PDFTileRow* row = m_openDocumentRows[i].data();
        const bool isInList = m_list && m_list->containsDocument(document->getId());
        const QString pages = Helper::pages(document->getPageCount());
        row->setObjectName(QStringLiteral("insertOpenDocument-") + document->getId());
        row->setProperty(Helper::ROW_KEY, document->getId());
        row->setTitle(document->getTitle());
        row->setTitleKind(PDFComponentPainter::TextKind::FileName);
        row->setDescription(isInList ? tr("%1 · already in the list").arg(pages) : (document->isModified() ? tr("%1 · unsaved state is used").arg(pages) : pages));
        row->setEnabled(!isInList && m_list && m_list->isPdfAccepted());
        row->show();
    }
    for (size_t i = documents.size(); i < m_openDocumentRows.size(); ++i)
    {
        if (m_openDocumentRows[i])
        {
            m_openDocumentRows[i]->hide();
        }
    }
    if (m_openDocumentsCaption)
    {
        m_openDocumentsCaption->setVisible(!documents.empty());
    }
}

void PDFAppInsertToolGUIBase::closePanel(const QString& name)
{
    const auto it = m_panels.find(name);
    if (it != m_panels.cend() && it->second)
    {
        it->second->close();
    }
}

QToolButton* PDFAppInsertToolGUIBase::getAction(const QString& name) const
{
    const auto it = m_actions.find(name);
    return it != m_actions.cend() ? it->second.data() : nullptr;
}

PDFAppToolPanelPopup* PDFAppInsertToolGUIBase::getPanel(const QString& name) const
{
    const auto it = m_panels.find(name);
    return it != m_panels.cend() ? it->second.data() : nullptr;
}

PDFTileRow* PDFAppInsertToolGUIBase::getPanelRow(const QString& name) const
{
    const auto it = m_panelRows.find(name);
    return it != m_panelRows.cend() ? it->second.data() : nullptr;
}

QString PDFAppInsertToolGUIBase::getParameter(const char* name) const
{
    const PDFAppToolParameterModel* model = getModel();
    return model ? model->getValue(PDFAppInsertToolGUIHelper::name(name)).toString() : QString();
}

QString PDFAppInsertToolGUIBase::getDocumentTitle() const
{
    const PDFAppDocument* document = getDocument();
    return document ? document->getTitle() : QString();
}

bool PDFAppInsertToolGUIBase::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_keyWidget)
    {
        switch (event->type())
        {
            case QEvent::DragEnter:
            case QEvent::DragMove:
            {
                QDropEvent* dropEvent = static_cast<QDropEvent*>(event);
                if (isEditable() && PDFAppFileDrop::hasLocalFiles(dropEvent->mimeData()))
                {
                    dropEvent->acceptProposedAction();
                    return true;
                }
                break;
            }

            case QEvent::Drop:
            {
                QDropEvent* dropEvent = static_cast<QDropEvent*>(event);
                if (isEditable() && PDFAppFileDrop::hasLocalFiles(dropEvent->mimeData()))
                {
                    // The data of the drop are read now; the files are added from the event
                    // handler, the last thing (a slot may close the tab)
                    dropEvent->acceptProposedAction();
                    dropFiles(dropEvent->mimeData());
                    return true;
                }
                break;
            }

            default:
                break;
        }
    }
    return BaseClass::eventFilter(watched, event);
}

// ------------------------------------------------------------- flow

void PDFAppInsertToolGUIBase::setContentEnabled(bool isEnabled)
{
    BaseClass::setContentEnabled(isEnabled);
}

PDFAppToolInputRules PDFAppInsertToolGUIBase::getInputRules() const
{
    // K41: the sources of Insert Pages - PDF files, open documents and images; none is needed
    // (blank pages alone)
    PDFAppToolInputRules rules;
    rules.minimum = 0;
    rules.maximum = PDFDocumentToolInsertBase::MAXIMUM_SOURCE_COUNT;
    rules.acceptsPdf = true;
    rules.acceptsImages = true;
    return rules;
}

QVariantMap PDFAppInsertToolGUIBase::getRunParameters() const
{
    using Helper = PDFAppInsertToolGUIHelper;

    // R16 of step 2: the run gets the usable inputs only - their ids and their values in the same
    // order; the core refuses an input of the context, which its order does not name
    QVariantMap parameters = BaseClass::getRunParameters();
    const QStringList runIds = getRunInputIds();
    const std::map<QString, QString> values = getSourceValues();
    QStringList parts;
    bool hasValue = false;
    for (const QString& id : runIds)
    {
        if (id == Helper::blankId())
        {
            continue;
        }
        const auto it = values.find(id);
        const QString value = it != values.cend() ? it->second : QString();
        hasValue = hasValue || !value.isEmpty();
        parts << value;
    }
    parameters.insert(Helper::name(PDFDocumentToolInsertBase::PARAMETER_INPUTS), runIds.join(QLatin1Char(',')));
    parameters.insert(getSourceValuesParameter(), hasValue ? parts.join(getSourceValuesSeparator()) : QString());
    return parameters;
}

void PDFAppInsertToolGUIBase::prepareEnvironment(PDFAppToolRunEnvironment* environment) const
{
    // The inputs of the run (getRunParameters) are the documents and the images of the context;
    // the documents are immutable shared pointers, the bytes are implicitly shared: values
    const QStringList runIds = getRunInputIds();
    for (const QString& id : runIds)
    {
        const PDFAppToolInput* input = m_list ? m_list->findInput(id) : nullptr;
        if (!input || !input->isUsable())
        {
            continue;
        }
        if (input->isImage())
        {
            environment->images.push_back(input->createToolInputImage());
        }
        else
        {
            environment->additionalDocuments.push_back(input->createToolInputDocument());
        }
    }

    // K33: the content of the inputs is a part of the key of the previews. The list is no list
    // of the host (PDFDocumentToolShell::applyEnvironment sets the generation of the list of the
    // host only), the runner gets it here, before the environment is set
    if (PDFDocumentToolRunner* runner = getRunner(); runner && m_list)
    {
        runner->setInputGeneration(m_list->getGeneration());
    }
}

void PDFAppInsertToolGUIBase::onParametersChanged(const QString& name)
{
    BaseClass::onParametersChanged(name);
    if (name == QLatin1String(PDFDocumentToolInsertBase::PARAMETER_INPUTS) || name == getSourceValuesParameter())
    {
        // Both values are set before the first announcement: one reading of both
        readValues();
        return;
    }

    QPointer<PDFAppInsertToolGUIBase> guard(this);
    updateParts();
    if (guard)
    {
        updateCommon();
    }
}

void PDFAppInsertToolGUIBase::retranslate()
{
    // The shell makes the parts again after it; a part, which stays (the canvas page), gets the
    // texts of the new language here
    BaseClass::retranslate();
    QPointer<PDFAppInsertToolGUIBase> guard(this);
    updateParts();
    if (guard)
    {
        updateCommon();
    }
}

void PDFAppInsertToolGUIBase::onRunStateChanged(bool isRunning)
{
    BaseClass::onRunStateChanged(isRunning);
    m_isRunning = isRunning;
    QPointer<PDFAppInsertToolGUIBase> guard(this);
    updateParts();
    if (guard)
    {
        updateCommon();
    }
}

void PDFAppInsertToolGUIBase::onDocumentChanged()
{
    BaseClass::onDocumentChanged();

    // A new version of the document: the pages of the strip, the counts of the texts
    const PDFAppDocument* document = getDocument();
    if (m_strip)
    {
        m_strip->setDocument(document ? document->getDocument() : PDFDocumentPointer(), document ? document->getId() : QString(), getHost() ? getHost()->getView() : nullptr);
    }
    QPointer<PDFAppInsertToolGUIBase> guard(this);
    updateParts();
    if (guard)
    {
        updateCommon();
    }
}

bool PDFAppInsertToolGUIBase::isConfirmEnabled() const
{
    // 388:17789 "Insert Pages is available when every source can be read" (K53: the button
    // without a number), 398:28602 nothing to insert; Replace needs its pages
    const bool isReplace = getParameter(PDFDocumentToolInsertBase::PARAMETER_POSITION) == QLatin1String(PDFDocumentToolInsertBase::POSITION_REPLACE);
    return BaseClass::isConfirmEnabled() && isEditable() && m_list && m_list->getLoadingCount() == 0 && m_list->getAttentionCount() == 0 &&
           getInsertedPageCount() > 0 && (!isReplace || !getReplacedPages().empty());
}

// ------------------------------------------------------------- PDFAppInsertToolGUI

PDFAppInsertToolGUI::PDFAppInsertToolGUI(QObject* parent) :
    BaseClass(parent)
{

}

PDFAppInsertToolGUI::~PDFAppInsertToolGUI() = default;

QString PDFAppInsertToolGUI::getAddButtonText() const
{
    return tr("Add");
}

QString PDFAppInsertToolGUI::getAddButtonToolTip() const
{
    return tr("Add pages to insert (%1)").arg(PDFAppInsertToolGUIHelper::getKeysText(PDFAppInsertToolGUIHelper::getPdfKeys()));
}

QString PDFAppInsertToolGUI::getSourceValuesParameter() const
{
    return QString::fromLatin1(PDFDocumentToolInsertPages::PARAMETER_SOURCE_PAGES);
}

QChar PDFAppInsertToolGUI::getSourceValuesSeparator() const
{
    return QLatin1Char(';');
}

std::vector<PDFAppInsertToolGUIBase::KeyAction> PDFAppInsertToolGUI::getKeyActions() const
{
    using Helper = PDFAppInsertToolGUIHelper;

    // 388:16725: PDF files… Ctrl+I, Images… Ctrl+Alt+I, Blank pages Ctrl+Shift+I, Paste image Ctrl+V
    return {
        { QStringLiteral("pdf"), Helper::getPdfKeys(), &PDFAppInsertToolGUIBase::addFilesFromDialog },
        { QStringLiteral("images"), Helper::getImageKeys(), &PDFAppInsertToolGUIBase::addFilesFromDialog },
        { QStringLiteral("blank"), Helper::getBlankKeys(), static_cast<void (PDFAppInsertToolGUIBase::*)()>(&PDFAppInsertToolGUI::addBlankPages) },
        { QStringLiteral("paste"), Helper::getPasteKeys(), &PDFAppInsertToolGUIBase::pasteImage },
    };
}

void PDFAppInsertToolGUI::describeAddMenu(PDFAppToolPanelPopup* panel)
{
    using Helper = PDFAppInsertToolGUIHelper;

    // panel-add 388:16725: the rows with their shortcuts (the native texts of the platform), the
    // open documents
    struct AddRow
    {
        const char* key;
        QString title;
        QString description;
        const char* iconName;
        QKeySequence keys;
        void (PDFAppInsertToolGUI::*slot)();
    };
    const AddRow rows[] = {
        { "pdf", tr("PDF files…"), tr("Whole documents or chosen pages"), "folder-open", Helper::getPdfKeys(), &PDFAppInsertToolGUI::onAddFilesRowClicked },
        { "images", tr("Images…"), tr("Each image becomes a page"), "image", Helper::getImageKeys(), &PDFAppInsertToolGUI::onAddFilesRowClicked },
        { "blank", tr("Blank pages"), tr("Empty pages of a chosen size"), "file", Helper::getBlankKeys(), &PDFAppInsertToolGUI::onBlankRowClicked },
        { "paste", tr("Paste image"), tr("Picture from the clipboard"), "clipboard", Helper::getPasteKeys(), &PDFAppInsertToolGUI::onPasteRowClicked },
    };
    for (const AddRow& addRow : rows)
    {
        PDFTileRow* row = panel->addRow(addRow.title, addRow.description, QString::fromLatin1(addRow.iconName), PDFTileRow::Kind::MenuItem);
        row->setObjectName(QStringLiteral("insertAdd-") + QString::fromLatin1(addRow.key));
        row->setShortcutText(Helper::getKeysText(addRow.keys));
        connect(row, &PDFTileRow::clicked, this, addRow.slot);
        m_panelRows[QStringLiteral("add-") + QString::fromLatin1(addRow.key)] = row;
    }
    updateOpenDocumentRows(panel);
}

void PDFAppInsertToolGUI::describeActionsAfterPosition(PDFAppToolPanelBuilder& builder)
{
    // divider, tb-remove-source 388:15592, divider, tb-thumbnail-size 1359:34421
    builder.addActionDivider();
    QToolButton* remove = PDFAppDocumentToolbar::createToolbarButton(QStringLiteral("insertAction-remove"), QStringLiteral("trash"), builder.getWidget());
    remove->setToolTip(tr("Remove the selected source"));
    remove->setAccessibleName(remove->toolTip());
    connect(remove, &QToolButton::clicked, this, &PDFAppInsertToolGUI::removeCurrentSource);
    builder.addWidget(remove);
    m_actions[QStringLiteral("remove")] = remove;
    builder.addActionDivider();

    if (!m_isTileSizeRead)
    {
        m_isTileSizeRead = true;
        m_tileSize = PDFAppToolTileSizeMenu::readSize(getContext());
    }
    // The menu belongs to its panel: the parts built again after a change of the language take
    // the old menu with the old panel
    PDFAppToolPanelPopup* panel = builder.addMenu(QStringLiteral("thumbnailSize"), QStringLiteral("pages"), QString(), PDFAppToolTileSizeMenu::PANEL_WIDTH);
    panel->getButton()->setObjectName(QStringLiteral("insertAction-thumbnailSize"));
    m_panels[QStringLiteral("thumbnailSize")] = panel;
    m_actions[QStringLiteral("thumbnailSize")] = panel->getButton();
    m_tileSizeMenu = new PDFAppToolTileSizeMenu(panel, tr("Thumbnails: %1"), panel);
    m_tileSizeMenu->setSize(m_tileSize);
    connect(m_tileSizeMenu.data(), &PDFAppToolTileSizeMenu::sizeChosen, this, &PDFAppInsertToolGUI::onTileSizeChosen);
}

void PDFAppInsertToolGUI::describeRightPanel(PDFAppToolPanelBuilder& builder)
{
    using Helper = PDFAppInsertToolGUIHelper;

    connectList();
    QWidget* widget = builder.getWidget();
    PDFAppToolParameterModel* model = getModel();

    // 388:17789: the sources, which need attention (the compact callout of Merge)
    m_attentionCallout = builder.addCallout(PDFAccentHue::Red, QStringLiteral("alert-circle"), QString(), QString());
    m_attentionCallout->setObjectName(QStringLiteral("insertAttention"));
    m_attentionCallout->setCompactStyle();

    // section-Annex_A.pdf 388:16071: the name and the Tag of the pages, the card of All pages /
    // Page range with the field of the range (K23)
    m_sourceSection = new QWidget(widget);
    m_sourceSection->setObjectName(QStringLiteral("insertSourceSection"));
    QVBoxLayout* sourceLayout = new QVBoxLayout(m_sourceSection);
    sourceLayout->setContentsMargins(0, 0, 0, 0);
    sourceLayout->setSpacing(PDFAppToolPanelBuilder::SECTION_GAP);
    QWidget* sourceHeader = new QWidget(m_sourceSection);
    QHBoxLayout* sourceHeaderLayout = new QHBoxLayout(sourceHeader);
    sourceHeaderLayout->setContentsMargins(0, 0, 0, 0);
    sourceHeaderLayout->setSpacing(8);
    m_sourceTitle = PDFAppPageWidgets::createLabel(QString(), QFont::DemiBold, 13, PDFThemeColors::TextPrimary, sourceHeader);
    m_sourceTitle->setObjectName(QStringLiteral("insertSourceTitle"));
    m_sourceTitle->setMinimumWidth(1);
    sourceHeaderLayout->addWidget(m_sourceTitle);
    m_sourceTag = new PDFTag(sourceHeader);
    m_sourceTag->setObjectName(QStringLiteral("insertSourceTag"));
    m_sourceTag->setFocusPolicy(Qt::NoFocus);
    m_sourceTag->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    sourceHeaderLayout->addWidget(m_sourceTag);
    sourceHeaderLayout->addStretch(1);
    sourceLayout->addWidget(sourceHeader);
    PDFAppToolCard* sourceCard = new PDFAppToolCard(m_sourceSection);
    sourceCard->getBodyLayout()->setContentsMargins(PDFAppToolCard::PADDING_H, PDFAppToolCard::PADDING_H, PDFAppToolCard::PADDING_H, PDFAppToolCard::PADDING_H);
    sourceCard->getBodyLayout()->setSpacing(PDFAppToolChoiceSwitch::GAP);
    m_choiceSwitch = new PDFAppToolChoiceSwitch(sourceCard);
    m_choiceSwitch->setObjectName(QStringLiteral("insertSourceChoice"));
    m_choiceSwitch->addOption(QStringLiteral("all"), tr("All pages"));
    m_choiceSwitch->addOption(QStringLiteral("range"), tr("Page range"));
    connect(m_choiceSwitch.data(), &PDFAppToolChoiceSwitch::choiceChanged, this, &PDFAppInsertToolGUI::onChoiceChanged);
    sourceCard->getBodyLayout()->addWidget(m_choiceSwitch);
    m_rangeField = new PDFAppToolPageRangeField(sourceCard);
    m_rangeField->setObjectName(QStringLiteral("insertSourceRange"));
    connect(m_rangeField.data(), &PDFAppToolPageRangeField::rangeChosen, this, &PDFAppInsertToolGUI::onRangeChosen);
    connect(m_rangeField.data(), &PDFAppToolPageRangeField::errorChanged, this, &PDFAppInsertToolGUI::onRangeErrorChanged);
    sourceCard->getBodyLayout()->addWidget(m_rangeField);
    sourceLayout->addWidget(sourceCard);
    builder.addWidget(m_sourceSection);
    m_rangeSource.clear();

    // Blank pages 388:16811: Count, Size "As page 12" and the presets, Orientation, the note
    m_blankSection = new QWidget(widget);
    m_blankSection->setObjectName(QStringLiteral("insertBlankSection"));
    QVBoxLayout* blankLayout = new QVBoxLayout(m_blankSection);
    blankLayout->setContentsMargins(0, 0, 0, 0);
    blankLayout->setSpacing(PDFAppToolPanelBuilder::SECTION_GAP);
    blankLayout->addWidget(new PDFAppToolSectionHeader(tr("Blank pages"), QString(), m_blankSection));
    PDFAppToolCard* blankCard = new PDFAppToolCard(m_blankSection);
    QGridLayout* grid = new QGridLayout();
    grid->setContentsMargins(0, 6, 0, 6);
    grid->setHorizontalSpacing(12);
    grid->setVerticalSpacing(8);
    grid->setColumnStretch(0, 1);
    blankCard->getBodyLayout()->addLayout(grid);
    const std::optional<PDFSettingDefinition> countDefinition = model ? model->toSettingDefinition(Helper::name(PDFDocumentToolInsertPages::PARAMETER_BLANK_PAGES)) : std::nullopt;
    if (countDefinition)
    {
        PDFAppSettingControl* control = new PDFAppSettingControl(getContext(), *countDefinition, blankCard, model->createValueStore());
        connect(model, &PDFAppToolParameterModel::changed, control, &PDFAppSettingControl::refresh);
        QWidget* editor = control->getSpinBox() ? static_cast<QWidget*>(control->getSpinBox()) : control->getWidget();
        editor->setObjectName(QStringLiteral("insertBlankCount"));
        Helper::addFieldRow(grid, 0, tr("Count"), editor, blankCard);
    }
    m_blankSizeCombo = new PDFComboBox(blankCard);
    m_blankSizeCombo->setObjectName(QStringLiteral("insertBlankSize"));
    connect(m_blankSizeCombo.data(), &QComboBox::activated, this, &PDFAppInsertToolGUI::onBlankSizeActivated);
    Helper::addFieldRow(grid, 1, tr("Size"), m_blankSizeCombo, blankCard);
    const std::optional<PDFSettingDefinition> orientationDefinition = model ? model->toSettingDefinition(Helper::name(PDFDocumentToolInsertPages::PARAMETER_BLANK_ORIENTATION)) : std::nullopt;
    if (orientationDefinition)
    {
        PDFAppSettingControl* control = new PDFAppSettingControl(getContext(), *orientationDefinition, blankCard, model->createValueStore());
        connect(model, &PDFAppToolParameterModel::changed, control, &PDFAppSettingControl::refresh);
        QWidget* editor = control->getComboBox() ? static_cast<QWidget*>(control->getComboBox()) : control->getWidget();
        editor->setObjectName(QStringLiteral("insertBlankOrientation"));
        m_blankOrientationControl = editor;
        Helper::addFieldRow(grid, 2, tr("Orientation"), editor, blankCard);
    }
    m_blankNote = PDFAppPageWidgets::createLabel(QString(), QFont::Normal, 11, PDFThemeColors::TextTertiary, blankCard);
    m_blankNote->setObjectName(QStringLiteral("insertBlankNote"));
    m_blankNote->setWordWrap(true);
    m_blankNote->setMinimumWidth(1);
    grid->addWidget(m_blankNote, 3, 0, 1, 2);
    blankLayout->addWidget(blankCard);
    builder.addWidget(m_blankSection);

    // A locked source has its buttons in its card of the canvas (388:17789); the right panel has
    // the callout of the attention and Summary only

    // section-Summary 388:16090: Insert, Position, Document
    builder.addSection(tr("Summary"));
    m_summary = builder.addKeyValues();
    m_summary->setObjectName(QStringLiteral("insertSummary"));
    m_summary->setRowGap(Helper::KEY_VALUE_ROW_GAP);
    m_summary->addRow(tr("Insert"), QString());
    m_summary->addRow(tr("Position"), QString());
    m_summary->addRow(tr("Document"), QString());
    builder.endSection();

    updateRightPanel();
}

QWidget* PDFAppInsertToolGUI::createCanvasPage(QWidget* parent)
{
    connectList();
    if (!m_isTileSizeRead)
    {
        m_isTileSizeRead = true;
        m_tileSize = PDFAppToolTileSizeMenu::readSize(getContext());
    }

    // The canvas of the sources (388:15520); it lives with the parts of the shell
    m_canvas = new PDFAppInsertCanvas(parent);
    m_canvas->getModel()->setInputList(getSourceList());
    m_canvas->setTileSize(m_tileSize);
    connect(m_canvas.data(), &PDFAppInsertCanvas::pageClicked, this, &PDFAppInsertToolGUI::onCanvasPageClicked);
    connect(m_canvas.data(), &PDFAppInsertCanvas::groupActionTriggered, this, &PDFAppInsertToolGUI::onCanvasActionTriggered);

    // The reset of the model is said to its observers, which may delete this GUI
    QPointer<PDFAppInsertToolGUI> guard(this);
    QPointer<PDFAppInsertCanvas> canvas = m_canvas;
    updateCanvas();
    if (guard)
    {
        updateEmptyState();
    }
    return canvas.data();
}

PDFInteger PDFAppInsertToolGUI::getBlankCount() const
{
    if (!getModelInputIds().contains(PDFAppInsertToolGUIHelper::blankId()))
    {
        return 0;
    }
    // PDFInteger is long on Linux, qint64 long long: every argument converted explicitly (GCC)
    return qBound(PDFInteger(0), static_cast<PDFInteger>(getParameter(PDFDocumentToolInsertPages::PARAMETER_BLANK_PAGES).toLongLong()), static_cast<PDFInteger>(PDFDocumentToolInsertPages::MAXIMUM_BLANK_PAGES));
}

QSizeF PDFAppInsertToolGUI::getBlankPageSize() const
{
    // 388:16811 "As page 12": the page as it is shown; both it and a named size in the orientation
    // (owner 2026-11-09: A4 portrait + Landscape = A4 landscape) - the rule of the core
    const QString sizeId = getParameter(PDFDocumentToolInsertPages::PARAMETER_BLANK_SIZE);
    const PDFAppDocument* document = getDocument();
    const QSizeF size = sizeId == QLatin1String(PDFDocumentToolInsertPages::SIZE_SAME_AS_PAGE) || sizeId.isEmpty()
                            ? PDFDocumentToolInsertBase::getShownPageSize(document ? document->getDocument().data() : nullptr, getNeighborPage())
                            : PDFPageSizePresets::getSize(sizeId).value_or(QSizeF());
    const PDFAppToolParameterModel* model = getModel();
    return PDFDocumentToolInsertPages::getOrientedSize(size, model ? model->getValues() : QVariantMap());
}

QString PDFAppInsertToolGUI::getNeighborOrientation() const
{
    const PDFAppDocument* document = getDocument();
    const QSizeF shown = PDFDocumentToolInsertBase::getShownPageSize(document ? document->getDocument().data() : nullptr, getNeighborPage());
    return QString::fromLatin1(shown.width() > shown.height() ? PDFDocumentToolInsertPages::ORIENTATION_LANDSCAPE : PDFDocumentToolInsertPages::ORIENTATION_PORTRAIT);
}

std::optional<std::vector<PDFInteger>> PDFAppInsertToolGUI::getSourcePages(const QString& inputId) const
{
    using Helper = PDFAppInsertToolGUIHelper;

    std::vector<PDFInteger> pages;
    if (inputId == Helper::blankId())
    {
        const PDFInteger count = getBlankCount();
        for (PDFInteger i = 0; i < count; ++i)
        {
            pages.push_back(i);
        }
        return pages;
    }

    const PDFAppToolInputList* list = getSourceList();
    const PDFAppToolInput* input = list && list->indexOf(inputId) >= 0 ? list->findInput(inputId) : nullptr;
    if (!input || !input->isUsable())
    {
        return pages;
    }

    const std::map<QString, QString> values = getSourceValues();
    const auto it = values.find(inputId);
    const QString value = it != values.cend() ? it->second : QString();
    if (input->isImage() || Helper::isAllPages(value))
    {
        // K35: an image gives its frames; a PDF without a range all its pages
        for (PDFInteger i = 0; i < input->pageCount; ++i)
        {
            pages.push_back(i);
        }
        return pages;
    }

    const std::optional<PDFPageRangeList> range = PDFPageRangeList::parse(value, input->pageCount, nullptr);
    if (!range)
    {
        return std::nullopt;
    }
    return range->getPageIndices();
}

PDFInteger PDFAppInsertToolGUI::getInsertedPageCount() const
{
    PDFInteger count = 0;
    for (const QString& id : getRunInputIds())
    {
        const std::optional<std::vector<PDFInteger>> pages = getSourcePages(id);
        count += pages ? PDFInteger(pages->size()) : 0;
    }
    return count;
}

QStringList PDFAppInsertToolGUI::getRunInputIds() const
{
    QStringList ids;
    for (const QString& id : getModelInputIds())
    {
        if (id == PDFAppInsertToolGUIHelper::blankId() ? getBlankCount() > 0 : isUsableInput(id))
        {
            ids << id;
        }
    }
    return ids;
}

QString PDFAppInsertToolGUI::getConfirmText() const
{
    // btn-confirm 388:15512 "Insert 6 Pages", 388:17167 "Replace with 6 Pages"; without pages the
    // title of the tool (388:17789, 398:28602, K53)
    const int count = PDFAppInsertToolGUIHelper::toInt(getInsertedPageCount());
    const PDFAppToolInputList* list = getSourceList();
    if (count == 0 || !list || list->getAttentionCount() > 0 || list->getLoadingCount() > 0)
    {
        return BaseClass::getConfirmText();
    }
    if (getParameter(PDFDocumentToolInsertBase::PARAMETER_POSITION) == QLatin1String(PDFDocumentToolInsertBase::POSITION_REPLACE))
    {
        return count == 1 ? tr("Replace with %Ln Page", nullptr, count) : tr("Replace with %Ln Pages", nullptr, count);
    }
    return count == 1 ? tr("Insert %Ln Page", nullptr, count) : tr("Insert %Ln Pages", nullptr, count);
}

bool PDFAppInsertToolGUI::isConfirmEnabled() const
{
    // Every range of the sources is valid
    if (!BaseClass::isConfirmEnabled())
    {
        return false;
    }
    for (const QString& id : getRunInputIds())
    {
        if (!getSourcePages(id))
        {
            return false;
        }
    }
    return true;
}

QString PDFAppInsertToolGUI::getStatusText() const
{
    using Helper = PDFAppInsertToolGUIHelper;

    const PDFDocumentToolCatalogEntry* tool = getTool();
    const QString title = tool ? tool->getTitle() : QString();
    const PDFAppToolInputList* list = getSourceList();
    if (getModelInputIds().isEmpty())
    {
        // 398:28800 "Insert Pages · choose what to insert after page 12 of Handbook.pdf"
        return tr("%1 · choose what to insert %2 of %3").arg(title, getPlaceClause(), getDocumentTitle());
    }

    const int loading = list ? list->getLoadingCount() : 0;
    if (loading > 0)
    {
        return tr("%1 · reading %2").arg(title, Helper::files(loading));
    }
    for (int i = 0; list && i < list->getCount(); ++i)
    {
        const PDFAppToolInput* input = list->getInput(i);
        if (input->needsAttention() || input->state == PDFAppToolInputState::Unsupported)
        {
            // 388:17789 "Insert Pages · Offer_final.pdf is protected by a password"
            if (input->state == PDFAppToolInputState::PasswordRequired)
            {
                return tr("%1 · %2 is protected by a password").arg(title, input->name);
            }
            return tr("%1 · %2 cannot be used – %3").arg(title, input->name, PDFAppToolInputList::getStateText(input->state));
        }
    }

    // 388:15522 "Insert Pages · 6 pages after page 12 · 48 → 54 pages"
    const PDFInteger count = getInsertedPageCount();
    return tr("%1 · %2 %3 · %4").arg(title, Helper::pages(count), getPlaceClause(), Helper::getDocumentChangeText(getDocumentPageCount(), getResultPageCount()));
}

void PDFAppInsertToolGUI::fillResultInput(PDFAppToolResultInput* input) const
{
    // 388:18134 "Pages inserted"
    input->title = tr("Pages inserted");
}

void PDFAppInsertToolGUI::updateToolbar()
{
    BaseClass::updateToolbar();
    if (QToolButton* remove = getAction(QStringLiteral("remove")))
    {
        // 388:16811: the blank pages alone are no card of a selected source (they lie on the
        // stage), the button removing the selected source is Disabled; their Count sets them
        const QStringList ids = getModelInputIds();
        const bool isBlankOnly = ids.size() == 1 && ids.front() == PDFAppInsertToolGUIHelper::blankId();
        remove->setEnabled(isEditable() && !m_currentInput.isEmpty() && !isBlankOnly);
    }
    if (QToolButton* size = getAction(QStringLiteral("thumbnailSize")))
    {
        size->setEnabled(isEditable());
    }
}

void PDFAppInsertToolGUI::updateParts()
{
    QPointer<PDFAppInsertToolGUI> guard(this);
    updateCanvas();
    if (guard)
    {
        updateRightPanel();
    }
    if (guard)
    {
        updateEmptyState();
    }
}

QString PDFAppInsertToolGUI::getHeaderText() const
{
    using Helper = PDFAppInsertToolGUIHelper;

    // preview-bar 388:15745 "Pages to insert · 6 pages from 2 files", 388:16811 "· 2 blank pages",
    // 388:17789 "· 1 file needs attention"
    const PDFAppToolInputList* list = getSourceList();
    const int attention = list ? list->getAttentionCount() : 0;
    if (attention > 0)
    {
        return attention == 1 ? tr("Pages to insert · %Ln file needs attention", nullptr, attention) : tr("Pages to insert · %Ln files need attention", nullptr, attention);
    }

    const int count = Helper::toInt(getInsertedPageCount());
    int files = 0;
    for (const QString& id : getRunInputIds())
    {
        files += id == Helper::blankId() ? 0 : 1;
    }
    if (files == 0)
    {
        return count == 1 ? tr("Pages to insert · %Ln blank page", nullptr, count) : tr("Pages to insert · %Ln blank pages", nullptr, count);
    }
    return tr("Pages to insert · %1 from %2").arg(Helper::pages(count), Helper::files(files));
}

void PDFAppInsertToolGUI::updateCanvas()
{
    using Helper = PDFAppInsertToolGUIHelper;
    using Group = PDFAppToolGroupedPageModel::Group;
    using Row = PDFAppInsertSourceModel::Row;

    const PDFAppToolInputList* list = getSourceList();
    if (!m_canvas || !list)
    {
        return;
    }

    // The source cards in the order of the insertion (388:15746): the pages of a PDF, the inserted
    // ones selected and the others dimmed (388:15756), an image with its frames, the blank pages
    // "New page 13" (388:16811), a source being read or locked with the callout of the view
    std::vector<Group> groups;
    std::vector<Row> rows;
    const std::map<QString, QString> values = getSourceValues();
    const QStringList ids = getModelInputIds();
    PDFInteger nextPage = getInsertionIndex() + 1;
    for (int i = 0; i < ids.size(); ++i)
    {
        const QString& id = ids[i];
        Group group;
        group.inputId = id;
        group.inputIndex = i;
        group.firstRow = int(rows.size());
        group.tag = id == m_currentInput ? tr("selected source") : QString();

        if (id == Helper::blankId())
        {
            // 388:16811: the large tiles "New page 13" / "blank · A4 portrait" (173 x 264 in the
            // size Small); alone they lie on the stage without a card
            const PDFInteger count = getBlankCount();
            const QSizeF size = getBlankPageSize();
            const QString description = PDFAppToolGUIImages::getPageDescription(size);
            group.iconName = QStringLiteral("file");
            group.title = tr("Blank pages");
            group.subtitle = tr("%1 · %2").arg(Helper::pages(count), description);
            group.tileScale = Helper::BLANK_TILE_SCALE;
            group.hasTileCaptions = true;
            group.isPlain = ids.size() == 1;
            for (PDFInteger j = 0; j < count; ++j)
            {
                Row row;
                row.pageIndex = int(j);
                row.label = tr("New page %1").arg(PDFAppLocale::formatNumber(nextPage + j));
                row.caption = tr("blank · %1").arg(description);
                row.pageSize = size;
                rows.push_back(std::move(row));
            }
            nextPage += count;
        }
        else if (const PDFAppToolInput* input = list->indexOf(id) >= 0 ? list->findInput(id) : nullptr)
        {
            // 388:17789: the icon of the kind of the source in front of its name
            group.iconName = input->isImage() ? QStringLiteral("image") : QStringLiteral("file");
            group.title = input->name;
            group.hue = PDFAccentHue::Neutral;
            switch (input->state)
            {
                case PDFAppToolInputState::Loading:
                {
                    const int progress = list->getLoadingProgress(id);
                    group.subtitle = PDFAppToolInputList::getStateText(input->state);
                    group.callout = PDFAppToolGroupedPageModel::GroupCallout::Reading;
                    group.calloutTitle = progress >= 0 ? tr("Reading the file – %1 %").arg(progress) : tr("Reading the file");
                    group.progress = progress >= 0 ? progress / 100.0 : -1.0;
                    break;
                }

                case PDFAppToolInputState::PasswordRequired:
                case PDFAppToolInputState::Restricted:
                    // 388:17789 "Protected by a password", the callout Password required and the
                    // buttons Enter Password… and Remove File below it
                    group.subtitle = PDFAppToolInputList::getStateText(input->state);
                    group.callout = PDFAppToolGroupedPageModel::GroupCallout::Locked;
                    group.calloutTitle = input->state == PDFAppToolInputState::PasswordRequired ? tr("Password required") : tr("Owner password required");
                    group.calloutText = input->state == PDFAppToolInputState::PasswordRequired ? tr("%1 is encrypted. Enter its password to read the pages, or remove the file from the list.").arg(input->name) : input->reason;
                    group.actions = { { Helper::ACTION_PASSWORD, tr("Enter Password…"), QStringLiteral("key") }, { Helper::ACTION_REMOVE, tr("Remove File"), QStringLiteral("trash") } };
                    break;

                case PDFAppToolInputState::Ready:
                    if (input->pageCount > 0)
                    {
                        const std::optional<std::vector<PDFInteger>> pages = getSourcePages(id);
                        if (input->isImage())
                        {
                            // 388:16039 "Image · becomes 1 page"
                            group.subtitle = tr("Image · becomes %1").arg(Helper::pages(input->pageCount));
                            for (PDFInteger page = 0; page < input->pageCount; ++page)
                            {
                                Row row;
                                row.inputId = id;
                                row.pageIndex = int(page);
                                row.label = PDFAppLocale::formatNumber(page + 1);
                                rows.push_back(std::move(row));
                            }
                            nextPage += input->pageCount;
                            break;
                        }

                        // 388:15752 "5 of 24 pages · range 1-5"
                        const auto it = values.find(id);
                        const QString value = it != values.cend() ? it->second : QString();
                        if (Helper::isAllPages(value))
                        {
                            group.subtitle = tr("all %1").arg(Helper::pages(input->pageCount));
                        }
                        else if (pages)
                        {
                            group.subtitle = tr("%1 of %2 · range %3").arg(PDFAppLocale::formatNumber(qint64(pages->size())), Helper::pages(input->pageCount), value);
                        }
                        else
                        {
                            group.subtitle = tr("%1 · the range has an error").arg(Helper::pages(input->pageCount));
                        }
                        std::set<PDFInteger> taken;
                        if (pages)
                        {
                            taken.insert(pages->cbegin(), pages->cend());
                            nextPage += PDFInteger(pages->size());
                        }
                        const PDFCatalog* catalog = input->document ? input->document->getCatalog() : nullptr;
                        for (PDFInteger page = 0; page < input->pageCount; ++page)
                        {
                            Row row;
                            row.inputId = id;
                            row.pageIndex = int(page);
                            row.label = PDFAppLocale::formatNumber(page + 1);
                            row.state = taken.count(page) ? PDFComponentPainter::PageTileState::Selected : PDFComponentPainter::PageTileState::Normal;
                            row.isDimmed = !taken.count(page);
                            const PDFPage* pageObject = catalog && size_t(page) < catalog->getPageCount() ? catalog->getPage(size_t(page)) : nullptr;
                            row.pageSize = pageObject ? pageObject->getRotatedCropBox().size() : QSizeF();
                            rows.push_back(std::move(row));
                        }
                        break;
                    }
                    [[fallthrough]];

                default:
                    // Damaged, Unsupported, No pages: the reason in the callout and Remove File (no
                    // frame: the actions of 388:17789 without the password)
                    group.subtitle = PDFAppToolInputList::getStateText(input->state);
                    group.callout = PDFAppToolGroupedPageModel::GroupCallout::Locked;
                    group.calloutTitle = PDFAppToolInputList::getStateText(input->state);
                    group.calloutText = input->reason;
                    group.actions = { { Helper::ACTION_REMOVE, tr("Remove File"), QStringLiteral("trash") } };
                    break;
            }
        }
        else
        {
            continue;
        }

        group.rowCount = int(rows.size()) - group.firstRow;
        groups.push_back(std::move(group));
    }

    // 388:16811: the blank pages alone have no hint (no order, no pages to choose)
    const bool isBlankOnly = groups.size() == 1 && groups.front().isPlain;
    QPointer<PDFAppInsertToolGUI> guard(this);
    m_canvas->getModel()->setContent(std::move(groups), std::move(rows));
    if (!guard || !m_canvas)
    {
        return;
    }
    m_canvas->setHeaderText(getHeaderText());
    m_canvas->setHintText(isBlankOnly ? QString() : tr("Sources are inserted in this order. Click pages of a PDF to choose them, or type a range on the right. Dimmed pages are not inserted."));
    m_canvas->setEditable(isEditable());
}

void PDFAppInsertToolGUI::updateRightPanel()
{
    using Helper = PDFAppInsertToolGUIHelper;

    const PDFAppToolInputList* list = getSourceList();
    if (!m_summary || !list)
    {
        return;
    }

    // 388:17789: "1 file needs attention" / "Insert Pages is available when every source can be read."
    const int attention = list->getAttentionCount();
    if (m_attentionCallout)
    {
        m_attentionCallout->setVisible(attention > 0);
        m_attentionCallout->setTexts(attention == 1 ? tr("%Ln file needs attention", nullptr, attention) : tr("%Ln files need attention", nullptr, attention),
                                     tr("Insert Pages is available when every source can be read."));
    }

    const QStringList ids = getModelInputIds();
    const QString current = m_currentInput;
    const PDFAppToolInput* input = current != Helper::blankId() && list->indexOf(current) >= 0 ? list->findInput(current) : nullptr;
    const bool isPdf = input && input->isUsable() && input->isPdf();
    const bool isBlank = current == Helper::blankId() && ids.contains(current);
    if (m_sourceSection)
    {
        m_sourceSection->setVisible(isPdf);
    }
    if (m_blankSection)
    {
        m_blankSection->setVisible(isBlank);
    }

    QString rangeError;
    if (isPdf && m_choiceSwitch && m_rangeField)
    {
        // 388:16071: the name, the Tag "24 pages", All pages / Page range, the range
        const std::map<QString, QString> values = getSourceValues();
        const auto it = values.find(current);
        const QString value = it != values.cend() ? it->second : QString();
        const bool isAll = Helper::isAllPages(value);
        m_sourceTitle->setText(input->name);
        m_sourceTag->setText(Helper::pages(input->pageCount));
        m_choiceSwitch->setChoice(isAll ? QStringLiteral("all") : QStringLiteral("range"));
        m_choiceSwitch->setEnabled(isEditable());
        m_rangeField->setPageCount(input->pageCount);

        // The field shows the range of the model; a text being typed with an error stays, until
        // another source is selected (the model never holds an invalid range)
        const auto typed = m_typedRanges.find(current);
        const QString rangeText = isAll ? (typed != m_typedRanges.cend() ? typed->second : QString()) : value;
        const bool isOtherSource = m_rangeSource != current;
        if (isOtherSource || (m_rangeField->getError().isEmpty() && m_rangeField->getText().trimmed() != rangeText) || isAll)
        {
            m_rangeField->setText(rangeText);
        }
        m_rangeSource = current;
        m_rangeField->setEnabled(!isAll && isEditable());
        rangeError = isAll ? QString() : m_rangeField->getError();
    }
    else
    {
        m_rangeSource.clear();
    }

    if (isBlank && m_blankSizeCombo)
    {
        // Size 388:16811: "As page 12" and the named sizes of the core
        const QString sizeId = getParameter(PDFDocumentToolInsertPages::PARAMETER_BLANK_SIZE);
        const PDFInteger neighbor = getNeighborPage() + 1;
        const QSignalBlocker blocker(m_blankSizeCombo.data());
        m_blankSizeCombo->clear();
        const PDFAppToolParameterModel* model = getModel();
        const PDFDocumentToolParameterDefinition* definition = model ? PDFDocumentToolParameters::find(model->getDefinitions(), Helper::name(PDFDocumentToolInsertPages::PARAMETER_BLANK_SIZE)) : nullptr;
        for (const PDFDocumentToolParameterOption& option : definition ? definition->getOptions() : std::vector<PDFDocumentToolParameterOption>())
        {
            const bool isSame = option.id == QLatin1String(PDFDocumentToolInsertPages::SIZE_SAME_AS_PAGE);
            m_blankSizeCombo->addItem(isSame ? tr("As page %1").arg(PDFAppLocale::formatNumber(neighbor)) : option.getTitle(), option.id);
        }
        m_blankSizeCombo->setCurrentIndex(qMax(0, m_blankSizeCombo->findData(sizeId)));
        m_blankSizeCombo->setEnabled(isEditable());
        const bool isSame = sizeId == QLatin1String(PDFDocumentToolInsertPages::SIZE_SAME_AS_PAGE) || sizeId.isEmpty();
        if (m_blankOrientationControl)
        {
            // 388:16811: the orientation turns also the size of "As page N" (owner 2026-11-09)
            m_blankOrientationControl->setEnabled(isEditable());
        }
        if (m_blankNote)
        {
            // 388:16811 "Same as page 12: A4, 210 × 297 mm."
            const QString description = Helper::getSizeDescription(getBlankPageSize());
            m_blankNote->setText(isSame ? tr("Same as page %1: %2.").arg(PDFAppLocale::formatNumber(neighbor), description) : tr("%1.").arg(description));
        }
    }

    // Summary 388:16093: Insert "6 pages", Position "After page 12", Document "48 → 54 pages";
    // without pages "–", with a source, which cannot be used, the document "–" (388:17789)
    const PDFInteger count = getInsertedPageCount();
    QStringList runIds = getRunInputIds();
    runIds.removeAll(Helper::blankId());
    const QString insertText = count == 0 ? QStringLiteral("–")
                                          : (runIds.isEmpty() ? (count == 1 ? tr("%Ln blank page", nullptr, int(count)) : tr("%Ln blank pages", nullptr, Helper::toInt(count))) : Helper::pages(count));
    const bool isPending = attention > 0 || list->getLoadingCount() > 0;
    m_summary->setValue(0, insertText);
    m_summary->setValue(1, getPositionValueText());
    m_summary->setValue(2, isPending ? QStringLiteral("–") : (count == 0 ? Helper::pages(getDocumentPageCount()) : Helper::getDocumentChangeText(getDocumentPageCount(), getResultPageCount())));

    // An invalid range of the selected source disables the Primary button (the last call: the
    // shell is told)
    setInputError(getSourceValuesParameter(), rangeError);
}

void PDFAppInsertToolGUI::updateEmptyState()
{
    using Helper = PDFAppInsertToolGUIHelper;

    if (!m_canvas)
    {
        return;
    }

    // 398:28602: without a source the drop zone with the open documents takes the stage
    const bool isEmpty = getModelInputIds().isEmpty();
    if (!isEmpty)
    {
        if (getEmptyState())
        {
            m_canvas->setEmptyState(nullptr);
        }
        return;
    }

    const std::vector<PDFAppInsertEmptyState::Document> documents = getOpenDocuments();
    QStringList signatureParts;
    for (const PDFAppInsertEmptyState::Document& document : documents)
    {
        signatureParts << document.documentId + (document.isModified ? QStringLiteral("*") : QString());
    }
    const QString signature = signatureParts.join(QLatin1Char(',')) + QStringLiteral("|") + QString::number(int(isEditable()));
    PDFAppInsertEmptyState* emptyState = getEmptyState();
    if (emptyState && emptyState->property(Helper::ROW_KEY).toString() == signature)
    {
        return;
    }

    PDFAppInsertEmptyState::Content content;
    content.iconName = QStringLiteral("page-insert");
    content.title = tr("Choose what to insert");
    content.text = tr("PDF documents and images. Drop them here, or onto a place between two thumbnails.");
    content.primaryText = tr("Add Files…");
    content.primaryIconName = QStringLiteral("plus");
    content.secondaryText = tr("Blank Pages");
    content.secondaryIconName = QStringLiteral("file");
    content.documents = documents;
    PDFAppInsertEmptyState* newState = new PDFAppInsertEmptyState(content, m_canvas);
    newState->setProperty(Helper::ROW_KEY, signature);
    newState->setEnabled(isEditable());
    connect(newState, &PDFAppInsertEmptyState::primaryRequested, this, &PDFAppInsertToolGUI::onEmptyStateAddFiles);
    connect(newState, &PDFAppInsertEmptyState::secondaryRequested, this, &PDFAppInsertToolGUI::onEmptyStateBlankPages);
    connect(newState, &PDFAppInsertEmptyState::addDocumentRequested, this, &PDFAppInsertToolGUI::onEmptyStateAddDocument);
    m_canvas->setEmptyState(newState);
}

PDFAppInsertEmptyState* PDFAppInsertToolGUI::getEmptyState() const
{
    return m_canvas ? qobject_cast<PDFAppInsertEmptyState*>(m_canvas->getEmptyState()) : nullptr;
}

void PDFAppInsertToolGUI::addBlankPages()
{
    using Helper = PDFAppInsertToolGUIHelper;

    if (!isEditable())
    {
        return;
    }

    // Blank pages (Ctrl+Shift+I, 388:16811): the source of the blank pages with one page more;
    // it is selected (the right panel shows Count, Size, Orientation)
    QStringList ids = getModelInputIds();
    PDFInteger count = getBlankCount();
    if (!ids.contains(Helper::blankId()))
    {
        ids << Helper::blankId();
        count = 0;
    }
    QVariantMap extra;
    extra.insert(Helper::name(PDFDocumentToolInsertPages::PARAMETER_BLANK_PAGES), qint64(qMin(PDFInteger(count + 1), static_cast<PDFInteger>(PDFDocumentToolInsertPages::MAXIMUM_BLANK_PAGES))));
    const QString sizeId = getParameter(PDFDocumentToolInsertPages::PARAMETER_BLANK_SIZE);
    if (count == 0 && (sizeId.isEmpty() || sizeId == QLatin1String(PDFDocumentToolInsertPages::SIZE_SAME_AS_PAGE)))
    {
        // New blank pages "As page N" start in the orientation of the page (one step of Undo)
        extra.insert(Helper::name(PDFDocumentToolInsertPages::PARAMETER_BLANK_ORIENTATION), getNeighborOrientation());
    }
    m_currentInput = Helper::blankId();
    applySources(ids, getSourceValues(), extra);
}

void PDFAppInsertToolGUI::setAllPages(bool isAllPages)
{
    using Helper = PDFAppInsertToolGUIHelper;

    const PDFAppToolInputList* list = getSourceList();
    const PDFAppToolInput* input = list ? list->findInput(m_currentInput) : nullptr;
    if (!input || !input->isUsable() || !input->isPdf() || !isEditable())
    {
        return;
    }

    // Page range starts with the range typed before, else with all pages as a range ("1-24")
    std::map<QString, QString> values = getSourceValues();
    QString value;
    if (!isAllPages)
    {
        const auto typed = m_typedRanges.find(m_currentInput);
        value = typed != m_typedRanges.cend() && !typed->second.trimmed().isEmpty() ? typed->second.trimmed()
                                                                                    : (input->pageCount > 1 ? QStringLiteral("1-%1").arg(input->pageCount) : QStringLiteral("1"));
        if (!PDFPageRangeList::parse(value, input->pageCount, nullptr))
        {
            value = input->pageCount > 1 ? QStringLiteral("1-%1").arg(input->pageCount) : QStringLiteral("1");
        }
    }
    else if (!Helper::isAllPages(values[m_currentInput]))
    {
        m_typedRanges[m_currentInput] = values[m_currentInput];
    }
    values[m_currentInput] = value;
    m_rangeSource.clear();
    applySources(getModelInputIds(), values);
}

void PDFAppInsertToolGUI::setSourceRange(const QString& text)
{
    const PDFAppToolInputList* list = getSourceList();
    const PDFAppToolInput* input = list ? list->findInput(m_currentInput) : nullptr;
    const QString value = text.trimmed();
    if (!input || !input->isUsable() || !input->isPdf() || !isEditable() || value.isEmpty() || !PDFPageRangeList::parse(value, input->pageCount, nullptr))
    {
        return;
    }

    // One undo step; the field keeps the text for All pages / Page range
    m_typedRanges[m_currentInput] = value;
    std::map<QString, QString> values = getSourceValues();
    values[m_currentInput] = value;
    applySources(getModelInputIds(), values);
}

void PDFAppInsertToolGUI::clickSourcePage(int row, int group)
{
    using Helper = PDFAppInsertToolGUIHelper;

    const PDFAppInsertSourceModel* model = m_canvas ? m_canvas->getModel() : nullptr;
    if (!model || group < 0 || size_t(group) >= model->getGroups().size() || !isEditable())
    {
        return;
    }

    // The card selects its source; a page of a PDF is added to its range or taken away
    const QString inputId = model->getGroups()[size_t(group)].inputId;
    const PDFAppInsertSourceModel::Row* item = model->getRow(row);
    const PDFAppToolInputList* list = getSourceList();
    const PDFAppToolInput* input = list && inputId != Helper::blankId() ? list->findInput(inputId) : nullptr;
    if (!item || item->inputId.isEmpty() || !input || !input->isUsable() || !input->isPdf())
    {
        setCurrentSource(inputId);
        return;
    }

    const std::optional<std::vector<PDFInteger>> pages = getSourcePages(inputId);
    std::vector<PDFInteger> newPages;
    std::map<QString, QString> values = getSourceValues();
    if (Helper::isAllPages(values[inputId]) || !pages)
    {
        // All pages: the click chooses the page alone (Page range)
        newPages = { item->pageIndex };
    }
    else
    {
        newPages = *pages;
        const auto it = std::find(newPages.begin(), newPages.end(), PDFInteger(item->pageIndex));
        if (it != newPages.end())
        {
            newPages.erase(std::remove(newPages.begin(), newPages.end(), PDFInteger(item->pageIndex)), newPages.end());
        }
        else
        {
            newPages.push_back(item->pageIndex);
        }
    }
    if (newPages.empty())
    {
        // No page is no range (an empty part means all pages): the source is removed instead
        if (getHost())
        {
            getHost()->showTransientMessage(tr("A source inserts at least one page – remove the source to insert none of its pages"));
        }
        return;
    }

    const QString text = Helper::createOrderedRangeText(newPages);
    m_currentInput = inputId;
    m_typedRanges[inputId] = text;
    m_rangeSource.clear();
    values[inputId] = text;
    applySources(getModelInputIds(), values);
}

void PDFAppInsertToolGUI::setTileSize(PDFAppToolPageGrid::TileSize size)
{
    m_isTileSizeRead = true;
    m_tileSize = size;
    if (m_tileSizeMenu)
    {
        m_tileSizeMenu->setSize(size);
    }
    if (m_canvas)
    {
        m_canvas->setTileSize(size);
    }

    // The store announces the change of the setting to its readers (the last call)
    PDFAppToolTileSizeMenu::storeSize(getContext(), size);
}

void PDFAppInsertToolGUI::onChoiceChanged(const QString& id)
{
    setAllPages(id == QLatin1String("all"));
}

void PDFAppInsertToolGUI::onRangeChosen(const QString& text)
{
    setSourceRange(text);
}

void PDFAppInsertToolGUI::onRangeErrorChanged(const QString& error)
{
    // The Primary button is disabled while the range has an error (the last call)
    setInputError(getSourceValuesParameter(), error);
}

void PDFAppInsertToolGUI::onCanvasPageClicked(int row, int group)
{
    clickSourcePage(row, group);
}

void PDFAppInsertToolGUI::onBlankRowClicked()
{
    QPointer<PDFAppInsertToolGUI> guard(this);
    closePanel(QStringLiteral("add"));
    if (guard)
    {
        addBlankPages();
    }
}

void PDFAppInsertToolGUI::onBlankSizeActivated(int index)
{
    PDFAppToolParameterModel* model = getModel();
    if (!model || !m_blankSizeCombo || !isEditable())
    {
        return;
    }
    const QString sizeId = m_blankSizeCombo->itemData(index).toString();
    QVariantMap values;
    values.insert(PDFAppInsertToolGUIHelper::name(PDFDocumentToolInsertPages::PARAMETER_BLANK_SIZE), sizeId);
    if (sizeId == QLatin1String(PDFDocumentToolInsertPages::SIZE_SAME_AS_PAGE))
    {
        // "As page N" starts in the orientation of the page; Orientation can turn it (owner 2026-11-09)
        values.insert(PDFAppInsertToolGUIHelper::name(PDFDocumentToolInsertPages::PARAMETER_BLANK_ORIENTATION), getNeighborOrientation());
    }
    QPointer<PDFAppInsertToolGUI> guard(this);
    const QString error = model->setValues(values);
    if (guard && !error.isEmpty() && getHost())
    {
        getHost()->showTransientMessage(error);
    }
}

void PDFAppInsertToolGUI::onCanvasActionTriggered(const QString& inputId, const QString& actionId)
{
    // The buttons of the card of the source (388:17789); the source is selected first
    using Helper = PDFAppInsertToolGUIHelper;

    if (!isEditable() || !getModelInputIds().contains(inputId))
    {
        return;
    }
    QPointer<PDFAppInsertToolGUI> guard(this);
    setCurrentSource(inputId);
    if (!guard)
    {
        return;
    }
    if (actionId == Helper::ACTION_PASSWORD)
    {
        openPasswordDialog(inputId);
    }
    else if (actionId == Helper::ACTION_REMOVE)
    {
        removeInput(inputId);
    }
}

void PDFAppInsertToolGUI::onTileSizeChosen(PDFAppToolPageGrid::TileSize size)
{
    setTileSize(size);
}

void PDFAppInsertToolGUI::onEmptyStateBlankPages()
{
    addBlankPages();
}

// ------------------------------------------------------------- PDFAppAddImageToolGUI

PDFAppAddImageToolGUI::PDFAppAddImageToolGUI(QObject* parent) :
    BaseClass(parent),
    m_thumbnailCache(new PDFAppImageThumbnailCache(this))
{
    connect(m_thumbnailCache.data(), &PDFAppImageThumbnailCache::thumbnailDecoded, this, &PDFAppAddImageToolGUI::onThumbnailDecoded);
}

PDFAppAddImageToolGUI::~PDFAppAddImageToolGUI()
{
    // The cache (a child) cuts its link first: a late thumbnail is dropped, nobody waits
    delete m_thumbnailCache.data();
}

void PDFAppAddImageToolGUI::onThumbnailDecoded()
{
    updateImages();
}

PDFAppImageThumbnailCache* PDFAppAddImageToolGUI::getThumbnailCache() const
{
    return m_thumbnailCache.data();
}

PDFAppToolInputRules PDFAppAddImageToolGUI::getInputRules() const
{
    // K41, K57: the preset of Insert for images - images only, at least one
    PDFAppToolInputRules rules;
    rules.minimum = 1;
    rules.maximum = PDFDocumentToolInsertBase::MAXIMUM_SOURCE_COUNT;
    rules.acceptsPdf = false;
    rules.acceptsImages = true;
    return rules;
}

QString PDFAppAddImageToolGUI::getAddButtonText() const
{
    return tr("Add Images");
}

QString PDFAppAddImageToolGUI::getAddButtonToolTip() const
{
    return tr("Add images (%1)").arg(PDFAppInsertToolGUIHelper::getKeysText(PDFAppInsertToolGUIHelper::getImageKeys()));
}

QString PDFAppAddImageToolGUI::getSourceValuesParameter() const
{
    return QString::fromLatin1(PDFDocumentToolAddImage::PARAMETER_TURNS);
}

QChar PDFAppAddImageToolGUI::getSourceValuesSeparator() const
{
    return QLatin1Char(',');
}

std::vector<PDFAppInsertToolGUIBase::KeyAction> PDFAppAddImageToolGUI::getKeyActions() const
{
    using Helper = PDFAppInsertToolGUIHelper;

    // 388:19607: Image files… Ctrl+Alt+I, Paste from clipboard Ctrl+V
    return {
        { QStringLiteral("images"), Helper::getImageKeys(), &PDFAppInsertToolGUIBase::addFilesFromDialog },
        { QStringLiteral("paste"), Helper::getPasteKeys(), &PDFAppInsertToolGUIBase::pasteImage },
    };
}

void PDFAppAddImageToolGUI::describeAddMenu(PDFAppToolPanelPopup* panel)
{
    using Helper = PDFAppInsertToolGUIHelper;

    // panel-add of 388:19607: Image files…, Paste from clipboard
    PDFTileRow* files = panel->addRow(tr("Image files…"), tr("Pick one or more files"), QStringLiteral("folder-open"), PDFTileRow::Kind::MenuItem);
    files->setObjectName(QStringLiteral("insertAdd-images"));
    files->setShortcutText(Helper::getKeysText(Helper::getImageKeys()));
    connect(files, &PDFTileRow::clicked, this, &PDFAppAddImageToolGUI::onAddFilesRowClicked);
    m_panelRows[QStringLiteral("add-images")] = files;
    PDFTileRow* paste = panel->addRow(tr("Paste from clipboard"), tr("Adds the copied picture"), QStringLiteral("clipboard"), PDFTileRow::Kind::MenuItem);
    paste->setObjectName(QStringLiteral("insertAdd-paste"));
    paste->setShortcutText(Helper::getKeysText(Helper::getPasteKeys()));
    connect(paste, &PDFTileRow::clicked, this, &PDFAppAddImageToolGUI::onPasteRowClicked);
    m_panelRows[QStringLiteral("add-paste")] = paste;
}

void PDFAppAddImageToolGUI::describeActionsBeforePosition(PDFAppToolPanelBuilder& builder)
{
    // 388:18837: divider, Rotate Left, Rotate Right, Remove, divider
    builder.addActionDivider();
    struct IconAction
    {
        const char* name;
        const char* iconName;
        QString text;
        void (PDFAppAddImageToolGUI::*slot)();
    };
    const IconAction actions[] = {
        { "rotateLeft", "rotate-ccw", tr("Rotate Left"), &PDFAppAddImageToolGUI::rotateLeft },
        { "rotateRight", "rotate-cw", tr("Rotate Right"), &PDFAppAddImageToolGUI::rotateRight },
        { "remove", "trash", tr("Remove the selected image"), &PDFAppAddImageToolGUI::removeImage },
    };
    for (const IconAction& action : actions)
    {
        QToolButton* button = PDFAppDocumentToolbar::createToolbarButton(QStringLiteral("insertAction-") + QString::fromLatin1(action.name), QString::fromLatin1(action.iconName), builder.getWidget());
        button->setToolTip(action.text);
        button->setAccessibleName(action.text);
        connect(button, &QToolButton::clicked, this, action.slot);
        builder.addWidget(button);
        m_actions[QString::fromLatin1(action.name)] = button;
    }
    builder.addActionDivider();
}

void PDFAppAddImageToolGUI::describeActionsAfterPosition(PDFAppToolPanelBuilder& builder)
{
    // tb-page with the panel Page of 388:19607: Same as page 12, Fit to image, A4, A3, Letter, Custom
    PDFAppToolPanelPopup* panel = builder.addMenu(QStringLiteral("pageSize"), QStringLiteral("file"), QString(), PAGE_PANEL_WIDTH);
    panel->getButton()->setObjectName(QStringLiteral("insertAction-pageSize"));
    m_panels[QStringLiteral("pageSize")] = panel;
    m_actions[QStringLiteral("pageSize")] = panel->getButton();
    const PDFAppToolParameterModel* model = getModel();
    const PDFDocumentToolParameterDefinition* definition = model ? PDFDocumentToolParameters::find(model->getDefinitions(), QString::fromLatin1(PDFDocumentToolAddImage::PARAMETER_PAGE_SIZE)) : nullptr;
    for (const PDFDocumentToolParameterOption& option : definition ? definition->getOptions() : std::vector<PDFDocumentToolParameterOption>())
    {
        QString iconName = QStringLiteral("file");
        if (option.id == QLatin1String(PDFDocumentToolAddImage::SIZE_FIT_TO_IMAGE))
        {
            iconName = QStringLiteral("fit-image");
        }
        else if (option.id == QLatin1String(PDFDocumentToolAddImage::SIZE_CUSTOM))
        {
            iconName = QStringLiteral("sliders");
        }
        // The named sizes have the millimeters of the core ("216 × 279 mm"); the other rows get
        // the texts of the frame in updatePageRows
        PDFTileRow* row = panel->addRow(option.getTitle(), option.getDescription(), iconName, PDFTileRow::Kind::MenuItem);
        row->setObjectName(QStringLiteral("insertPageSize-") + option.id);
        row->setProperty(PDFAppInsertToolGUIHelper::ROW_KEY, option.id);
        row->setSelectionBehavior(PDFComponentButton::SelectionBehavior::Exclusive);
        connect(row, &PDFTileRow::clicked, this, &PDFAppAddImageToolGUI::onPageRowClicked);
        m_panelRows[QStringLiteral("page-") + option.id] = row;
    }
    updatePageRows();
}

void PDFAppAddImageToolGUI::describeLeftPanelTop(PDFAppToolPanelBuilder& builder)
{
    // sidebar-left 388:18837: Images "2 images · 5.3 MB", the rows of the images (the rows of
    // Merge and Images to PDF), then the position
    m_imagesPanel = new PDFAppToolInputFilesPanel(builder.getWidget());
    m_imagesPanel->setObjectName(QStringLiteral("addImageImagesPanel"));
    connect(m_imagesPanel.data(), &PDFAppToolInputFilesPanel::rowClicked, this, &PDFAppAddImageToolGUI::onImageRowClicked);
    connect(m_imagesPanel.data(), &PDFAppToolInputFilesPanel::linkActivated, this, &PDFAppAddImageToolGUI::onImageLinkActivated);
    connect(m_imagesPanel.data(), &PDFAppToolInputFilesPanel::removeRequested, this, &PDFAppAddImageToolGUI::removeInput);
    connect(m_imagesPanel.data(), &PDFAppToolInputFilesPanel::moveRequested, this, &PDFAppAddImageToolGUI::moveInput);
    builder.addWidget(m_imagesPanel);
    updateImages();
}

void PDFAppAddImageToolGUI::describeRightPanel(PDFAppToolPanelBuilder& builder)
{
    using Helper = PDFAppInsertToolGUIHelper;

    connectList();
    PDFAppToolParameterModel* model = getModel();

    // section-Page layout 388:18837: Width and Height of Custom (K16: "Width and height appear in
    // Page layout"), Margins, Image, Anchor (the field of package B, K14)
    builder.addSection(tr("Page layout"));
    PDFAppToolCard* card = new PDFAppToolCard(builder.getWidget());
    card->setObjectName(QStringLiteral("addImageLayoutCard"));
    m_sizeFields = new QWidget(card);
    m_sizeFields->setObjectName(QStringLiteral("addImageSizeFields"));
    QGridLayout* sizeGrid = new QGridLayout(m_sizeFields);
    sizeGrid->setContentsMargins(0, 6, 0, 0);
    sizeGrid->setHorizontalSpacing(12);
    sizeGrid->setVerticalSpacing(8);
    sizeGrid->setColumnStretch(0, 1);
    card->getBodyLayout()->addWidget(m_sizeFields);
    QGridLayout* grid = new QGridLayout();
    grid->setContentsMargins(0, 6, 0, 6);
    grid->setHorizontalSpacing(12);
    grid->setVerticalSpacing(8);
    grid->setColumnStretch(0, 1);
    card->getBodyLayout()->addLayout(grid);

    struct Field
    {
        QGridLayout* grid;
        int row;
        const char* parameter;
        QString label;
    };
    const Field fields[] = {
        { sizeGrid, 0, PDFDocumentToolAddImage::PARAMETER_WIDTH, tr("Width") },
        { sizeGrid, 1, PDFDocumentToolAddImage::PARAMETER_HEIGHT, tr("Height") },
        { grid, 0, PDFDocumentToolAddImage::PARAMETER_MARGINS, tr("Margins") },
        { grid, 1, PDFDocumentToolAddImage::PARAMETER_IMAGE_SCALING, tr("Image") },
    };
    for (const Field& field : fields)
    {
        const std::optional<PDFSettingDefinition> definition = model ? model->toSettingDefinition(Helper::name(field.parameter)) : std::nullopt;
        if (!definition)
        {
            continue;
        }
        PDFAppSettingControl* control = new PDFAppSettingControl(getContext(), *definition, card, model->createValueStore());
        connect(model, &PDFAppToolParameterModel::changed, control, &PDFAppSettingControl::refresh);
        QWidget* editor = control->getWidget();
        if (PDFDoubleSpinBox* spinBox = control->getDoubleSpinBox())
        {
            // 388:18837 "10 mm"
            spinBox->setSuffix(tr(" mm"));
            spinBox->setDecimals(1);
            editor = spinBox;
        }
        else if (PDFComboBox* comboBox = control->getComboBox())
        {
            editor = comboBox;
        }
        editor->setObjectName(QStringLiteral("addImageField-") + Helper::name(field.parameter));
        Helper::addFieldRow(field.grid, field.row, field.label, editor, card);
    }
    // Anchor: the field of PDFAnchorGrid (K14; PDFAppToolPanelBuilder::addAnchorField puts it below
    // the card, 388:18837 has it as the third row of the card - the same field, its combo box of
    // 140 at the end as the other fields, in the grid)
    m_anchorField = new PDFAppToolAnchorField(this, Helper::name(PDFDocumentToolAddImage::PARAMETER_ANCHOR), tr("Anchor"), card);
    m_anchorField->setObjectName(QStringLiteral("addImageAnchor"));
    grid->addWidget(m_anchorField, 2, 0, 1, 2);
    builder.addWidget(card);
    builder.endSection();

    // section-Summary: New pages, Position, Page size, Document
    builder.addSection(tr("Summary"));
    m_summary = builder.addKeyValues();
    m_summary->setObjectName(QStringLiteral("addImageSummary"));
    m_summary->setRowGap(Helper::KEY_VALUE_ROW_GAP);
    m_summary->addRow(tr("New pages"), QString());
    m_summary->addRow(tr("Position"), QString());
    m_summary->addRow(tr("Page size"), QString());
    m_summary->addRow(tr("Document"), QString());
    builder.endSection();

    // The note below Summary (Regular 11 text/tertiary)
    m_note = builder.addHint(tr("Looking for something else? Images to PDF creates a new document from images. Edit Content places a picture onto an existing page."));
    m_note->setObjectName(QStringLiteral("addImageNote"));

    updateRightPanel();
}

QWidget* PDFAppAddImageToolGUI::createCanvasPage(QWidget* parent)
{
    connectList();

    // The canvas of the new pages (388:18837); it lives with the parts of the shell
    m_canvas = new PDFAppAddImageCanvas(parent);
    m_canvas->setFit(m_isFit);
    connect(m_canvas.data(), &PDFAppAddImageCanvas::zoomChanged, this, &PDFAppAddImageToolGUI::onZoomChanged);
    connect(m_canvas.data(), &PDFAppAddImageCanvas::cardClicked, this, &PDFAppAddImageToolGUI::onCardClicked);
    QPointer<PDFAppAddImageToolGUI> guard(this);
    QPointer<PDFAppAddImageCanvas> canvas = m_canvas;
    updateCanvas();
    if (guard)
    {
        updateEmptyState();
    }
    return canvas.data();
}

PDFAppToolAnchorField* PDFAppAddImageToolGUI::getAnchorField() const
{
    return m_anchorField.data();
}

PageRotation PDFAppAddImageToolGUI::getTurn(const QString& inputId) const
{
    const std::map<QString, QString> values = getSourceValues();
    const auto it = values.find(inputId);
    return it != values.cend() ? PDFAppInsertToolGUIHelper::getTurn(it->second) : PageRotation::None;
}

QSizeF PDFAppAddImageToolGUI::getNeighborPageSize() const
{
    const PDFAppDocument* document = getDocument();
    return PDFDocumentToolInsertBase::getShownPageSize(document ? document->getDocument().data() : nullptr, getNeighborPage());
}

PDFImagePageLayout PDFAppAddImageToolGUI::getImageLayout(const QString& inputId) const
{
    // The layout of the core (PDFDocumentToolAddImage::createInsertionPlan, 5.8)
    const QString sizeId = getParameter(PDFDocumentToolAddImage::PARAMETER_PAGE_SIZE);
    QSizeF pageSize;
    if (sizeId == QLatin1String(PDFDocumentToolAddImage::SIZE_SAME_AS_PAGE) || sizeId.isEmpty())
    {
        pageSize = getNeighborPageSize();
    }
    else if (sizeId == QLatin1String(PDFDocumentToolAddImage::SIZE_CUSTOM))
    {
        pageSize = QSizeF(PDFPageSizePresets::getPoints(getParameter(PDFDocumentToolAddImage::PARAMETER_WIDTH).toDouble()),
                          PDFPageSizePresets::getPoints(getParameter(PDFDocumentToolAddImage::PARAMETER_HEIGHT).toDouble()));
    }
    else if (sizeId != QLatin1String(PDFDocumentToolAddImage::SIZE_FIT_TO_IMAGE))
    {
        pageSize = PDFPageSizePresets::getSize(sizeId).value_or(QSizeF());
    }
    const PDFImagePageLayout::Scaling scaling = PDFDocumentToolAddImage::getScaling(getParameter(PDFDocumentToolAddImage::PARAMETER_IMAGE_SCALING)).value_or(PDFImagePageLayout::Scaling::Fit);
    const Qt::Alignment anchor = PDFDocumentToolAddImage::getAnchor(getParameter(PDFDocumentToolAddImage::PARAMETER_ANCHOR)).value_or(Qt::AlignCenter);
    return PDFDocumentToolInsertBase::createImageLayout(pageSize, getParameter(PDFDocumentToolAddImage::PARAMETER_MARGINS).toDouble(), scaling, anchor, getTurn(inputId));
}

QSizeF PDFAppAddImageToolGUI::getImagePageSize(const QString& inputId) const
{
    const PDFAppToolInputList* list = getSourceList();
    const PDFAppToolInput* input = list ? list->findInput(inputId) : nullptr;
    if (!input || !input->isImage())
    {
        return QSizeF();
    }
    const QSizeF imageSize = PDFImagePageFactory::getPageSize(input->imageSize, input->imageDpiX, input->imageDpiY);
    return getImageLayout(inputId).getPageSize(imageSize);
}

QString PDFAppAddImageToolGUI::getPageSizeValueText() const
{
    // Summary 388:18837 "A4, as page 12"
    const QString sizeId = getParameter(PDFDocumentToolAddImage::PARAMETER_PAGE_SIZE);
    if (sizeId == QLatin1String(PDFDocumentToolAddImage::SIZE_SAME_AS_PAGE) || sizeId.isEmpty())
    {
        const QSizeF size = getNeighborPageSize();
        const QString name = PDFAppToolGUIImages::getPresetName(size);
        const QString sizeText = name.isEmpty() ? tr("%1 × %2 mm").arg(PDFAppBlankToolGUI::formatMillimeters(PDFPageSizePresets::getMillimeters(size.width())),
                                                                      PDFAppBlankToolGUI::formatMillimeters(PDFPageSizePresets::getMillimeters(size.height())))
                                                : name;
        return tr("%1, as page %2").arg(sizeText, PDFAppLocale::formatNumber(getNeighborPage() + 1));
    }
    if (sizeId == QLatin1String(PDFDocumentToolAddImage::SIZE_FIT_TO_IMAGE))
    {
        return tr("Fit to image");
    }
    if (sizeId == QLatin1String(PDFDocumentToolAddImage::SIZE_CUSTOM))
    {
        return tr("%1 × %2 mm").arg(PDFAppBlankToolGUI::formatMillimeters(getParameter(PDFDocumentToolAddImage::PARAMETER_WIDTH).toDouble()),
                                    PDFAppBlankToolGUI::formatMillimeters(getParameter(PDFDocumentToolAddImage::PARAMETER_HEIGHT).toDouble()));
    }
    const std::optional<QSizeF> preset = PDFPageSizePresets::getSize(sizeId);
    return preset ? PDFAppToolGUIImages::getPresetName(*preset) : sizeId;
}

PDFInteger PDFAppAddImageToolGUI::getInsertedPageCount() const
{
    // Each frame of each usable image is a page
    const PDFAppToolInputList* list = getSourceList();
    PDFInteger count = 0;
    for (const QString& id : getRunInputIds())
    {
        const PDFAppToolInput* input = list ? list->findInput(id) : nullptr;
        count += input ? input->pageCount : 0;
    }
    return count;
}

QString PDFAppAddImageToolGUI::getConfirmText() const
{
    // btn-confirm 388:18837 "Add 2 Pages"; without an image "Add Pages" (388:19253, K53)
    const int count = PDFAppInsertToolGUIHelper::toInt(getInsertedPageCount());
    const PDFAppToolInputList* list = getSourceList();
    if (count == 0 || !list || list->getAttentionCount() > 0 || list->getLoadingCount() > 0)
    {
        return tr("Add Pages");
    }
    return count == 1 ? tr("Add %Ln Page", nullptr, count) : tr("Add %Ln Pages", nullptr, count);
}

QString PDFAppAddImageToolGUI::getStatusText() const
{
    using Helper = PDFAppInsertToolGUIHelper;

    const PDFDocumentToolCatalogEntry* tool = getTool();
    const QString title = tool ? tool->getTitle() : QString();
    const PDFAppToolInputList* list = getSourceList();
    if (!list || list->getCount() == 0)
    {
        // 388:19253 "Add Image as Page · choose the images that become new pages of Handbook.pdf"
        return tr("%1 · choose the images that become new pages of %2").arg(title, getDocumentTitle());
    }
    if (list->getLoadingCount() > 0)
    {
        return tr("%1 · reading %2").arg(title, Helper::images(list->getLoadingCount()));
    }
    for (int i = 0; i < list->getCount(); ++i)
    {
        const PDFAppToolInput* input = list->getInput(i);
        if (!input->isUsable())
        {
            return tr("%1 · %2 cannot be used – %3").arg(title, input->name, input->reason.isEmpty() ? PDFAppToolInputList::getStateText(input->state) : input->reason);
        }
    }

    // 388:18837 "Add Image as Page · 2 new pages after page 12 · 48 → 50 pages"
    const int count = Helper::toInt(getInsertedPageCount());
    const QString pages = count == 1 ? tr("%Ln new page", nullptr, count) : tr("%Ln new pages", nullptr, count);
    return tr("%1 · %2 %3 · %4").arg(title, pages, getPlaceClause(), Helper::getDocumentChangeText(getDocumentPageCount(), getResultPageCount()));
}

void PDFAppAddImageToolGUI::fillResultInput(PDFAppToolResultInput* input) const
{
    // 388:20121 "Pages added", the fact "New pages · 2 images after page 12"
    input->title = tr("Pages added");
    for (PDFDocumentToolFact& fact : input->result.facts)
    {
        if (fact.id == QLatin1String("added"))
        {
            fact.title = tr("New pages");
        }
    }
}

void PDFAppAddImageToolGUI::rotateLeft()
{
    turnCurrent(false);
}

void PDFAppAddImageToolGUI::rotateRight()
{
    turnCurrent(true);
}

void PDFAppAddImageToolGUI::removeImage()
{
    removeCurrentSource();
}

void PDFAppAddImageToolGUI::turnCurrent(bool isClockwise)
{
    using Helper = PDFAppInsertToolGUIHelper;

    if (!isEditable() || !isUsableInput(m_currentInput))
    {
        return;
    }

    // One undo step: the turn of the selected image by 90° (the page follows the image: Auto)
    const int angle = (Helper::getTurnAngle(getTurn(m_currentInput)) + (isClockwise ? 90 : 270)) % 360;
    std::map<QString, QString> values = getSourceValues();
    values[m_currentInput] = QString::number(angle);
    applySources(getModelInputIds(), values);
}

void PDFAppAddImageToolGUI::setFit(bool isFit)
{
    m_isFit = isFit;
    if (m_canvas)
    {
        m_canvas->setFit(isFit);
    }
}

QImage PDFAppAddImageToolGUI::getPreviewImage(const PDFAppToolInput& input)
{
    // A decoded preview of the image (the orientation of EXIF applied), at most
    // MAXIMUM_PREVIEW_SIDE pixels, kept by the content generation of the input
    const auto it = m_previews.find(input.id);
    if (it != m_previews.cend() && it->second.first == input.contentGeneration)
    {
        return it->second.second;
    }

    QBuffer buffer;
    buffer.setData(input.imageData);
    QImage image;
    if (buffer.open(QIODevice::ReadOnly))
    {
        QImageReader reader(&buffer, input.imageFormat);
        reader.setAutoTransform(true);
        const QSize size = reader.size();
        if (size.isValid() && (size.width() > MAXIMUM_PREVIEW_SIDE || size.height() > MAXIMUM_PREVIEW_SIDE))
        {
            reader.setScaledSize(size.scaled(QSize(MAXIMUM_PREVIEW_SIDE, MAXIMUM_PREVIEW_SIDE), Qt::KeepAspectRatio));
        }
        image = reader.read();
    }
    m_previews[input.id] = { input.contentGeneration, image };
    return image;
}

void PDFAppAddImageToolGUI::updateParts()
{
    QPointer<PDFAppAddImageToolGUI> guard(this);
    updateImages();
    if (guard)
    {
        updateCanvas();
    }
    if (guard)
    {
        updateRightPanel();
    }
    if (guard)
    {
        updateEmptyState();
    }
    if (guard)
    {
        updatePageRows();
    }
}

void PDFAppAddImageToolGUI::updateToolbar()
{
    BaseClass::updateToolbar();
    const bool isImage = isEditable() && isUsableInput(m_currentInput);
    for (const char* name : { "rotateLeft", "rotateRight" })
    {
        if (QToolButton* button = getAction(QString::fromLatin1(name)))
        {
            button->setEnabled(isImage);
        }
    }
    if (QToolButton* remove = getAction(QStringLiteral("remove")))
    {
        remove->setEnabled(isEditable() && !m_currentInput.isEmpty());
    }
    if (QToolButton* page = getAction(QStringLiteral("pageSize")))
    {
        page->setEnabled(isEditable());
    }
}

void PDFAppAddImageToolGUI::updatePageRows()
{
    // panel Page 388:19607: "Same as page 12" / "A4 · 210 × 297 mm", the sizes with their
    // millimeters; the button "Page: Same as page 12"
    const QString sizeId = getParameter(PDFDocumentToolAddImage::PARAMETER_PAGE_SIZE);
    const QString neighbor = PDFAppLocale::formatNumber(getNeighborPage() + 1);
    const QSizeF neighborSize = getNeighborPageSize();
    const QString neighborName = PDFAppToolGUIImages::getPresetName(neighborSize);
    const QString neighborMillimeters = tr("%1 × %2 mm").arg(PDFAppBlankToolGUI::formatMillimeters(PDFPageSizePresets::getMillimeters(neighborSize.width())),
                                                             PDFAppBlankToolGUI::formatMillimeters(PDFPageSizePresets::getMillimeters(neighborSize.height())));
    QString buttonText;
    for (const auto& [name, row] : m_panelRows)
    {
        if (!row || !name.startsWith(QLatin1String("page-")))
        {
            continue;
        }
        const QString id = row->property(PDFAppInsertToolGUIHelper::ROW_KEY).toString();
        const QSignalBlocker blocker(row.data());
        QString title = row->getTitle();
        if (id == QLatin1String(PDFDocumentToolAddImage::SIZE_SAME_AS_PAGE))
        {
            title = tr("Same as page %1").arg(neighbor);
            row->setTitle(title);
            row->setDescription(neighborName.isEmpty() ? neighborMillimeters : tr("%1 · %2").arg(neighborName, neighborMillimeters));
        }
        else if (id == QLatin1String(PDFDocumentToolAddImage::SIZE_FIT_TO_IMAGE))
        {
            row->setDescription(tr("The page is exactly as large as the image"));
        }
        else if (id == QLatin1String(PDFDocumentToolAddImage::SIZE_CUSTOM))
        {
            row->setDescription(tr("Width and height appear in Page layout"));
        }
        const bool isChosen = id == sizeId || (sizeId.isEmpty() && id == QLatin1String(PDFDocumentToolAddImage::SIZE_SAME_AS_PAGE));
        row->setChecked(isChosen);
        if (isChosen)
        {
            buttonText = title;
        }
    }
    if (QToolButton* button = getAction(QStringLiteral("pageSize")))
    {
        button->setText(tr("Page: %1").arg(buttonText));
        button->setToolTip(button->text());
    }
}

void PDFAppAddImageToolGUI::updateImages()
{
    using Helper = PDFAppInsertToolGUIHelper;

    const PDFAppToolInputList* list = getSourceList();
    if (!m_imagesPanel || !list)
    {
        return;
    }

    // 388:18837: "Images" / "2 images · 5.3 MB"; a row "4032 × 3024 px · 3.1 MB", "New page 13 · A4
    // landscape"; 388:19253 "No images yet"
    const int count = list->getCount();
    m_imagesPanel->setHeader(tr("Images"), count == 0 ? tr("No images yet") : tr("%1 · %2").arg(Helper::images(count), PDFDocumentToolFormat::formatSize(list->getTotalSize())));
    m_imagesPanel->setHint(QString());

    std::vector<PDFAppToolInputRow::Content> rows;
    PDFInteger nextPage = getInsertionIndex() + 1;
    for (const QString& id : getModelInputIds())
    {
        const int index = list->indexOf(id);
        const PDFAppToolInput* input = index >= 0 ? list->getInput(index) : nullptr;
        if (!input)
        {
            continue;
        }

        PDFAppToolInputRow::Content content;
        content.inputId = input->id;
        content.letter = PDFAppToolInputList::getLetter(index);
        content.hue = PDFAppToolInputList::getHue(index, input);
        content.name = input->name;
        if (input->isUsable() && input->isImage() && m_thumbnailCache)
        {
            // M10, thumb 336:2128 as Images to PDF: the picture of the image in its turn (decoded
            // in the pool by the cache of Images to PDF)
            content.hasThumbnail = true;
            content.thumbnail = m_thumbnailCache->getThumbnail(*input);
            const int angle = Helper::getTurnAngle(getTurn(id));
            if (angle != 0 && !content.thumbnail.isNull())
            {
                content.thumbnail = content.thumbnail.transformed(QTransform().rotate(angle));
            }
        }
        if (input->state == PDFAppToolInputState::Loading)
        {
            content.firstLine = PDFAppToolInputList::getStateText(input->state);
            const int progress = list->getLoadingProgress(id);
            content.progress = progress >= 0 ? progress / 100.0 : 0.0;
        }
        else if (!input->isUsable())
        {
            content.firstLine = input->reason.isEmpty() ? PDFAppToolInputList::getStateText(input->state) : input->reason;
            content.isFirstLineProblem = true;
            content.toolTip = content.firstLine;
            content.linkText = isEditable() ? tr("Remove file") : QString();
        }
        else
        {
            QStringList first = { tr("%1 × %2 px").arg(PDFAppLocale::formatNumber(input->imageSize.width()), PDFAppLocale::formatNumber(input->imageSize.height())) };
            if (input->fileSize > 0)
            {
                first << PDFDocumentToolFormat::formatSize(input->fileSize);
            }
            content.firstLine = first.join(QStringLiteral(" · "));
            const QString page = PDFAppToolGUIImages::getPageDescription(getImagePageSize(id));
            content.secondLine = input->pageCount > 1 ? tr("New pages %1–%2 · %3").arg(PDFAppLocale::formatNumber(nextPage), PDFAppLocale::formatNumber(nextPage + input->pageCount - 1), page)
                                                      : tr("New page %1 · %2").arg(PDFAppLocale::formatNumber(nextPage), page);
            nextPage += input->pageCount;
            const PageRotation turn = getTurn(id);
            if (turn != PageRotation::None)
            {
                content.tag = tr("rotated %1°").arg(Helper::getTurnAngle(turn));
            }
        }
        rows.push_back(content);
    }

    // The thumbnails of the images, which are not in the list any more, are forgotten
    std::set<QString> keys;
    for (int i = 0; i < count; ++i)
    {
        keys.insert(PDFAppImageThumbnailCache::getKey(*list->getInput(i)));
    }
    if (m_thumbnailCache)
    {
        m_thumbnailCache->retain(keys);
    }

    m_imagesPanel->setRows(rows);
    m_imagesPanel->setCurrentInput(m_currentInput);
    m_imagesPanel->setEditable(isEditable());
}

void PDFAppAddImageToolGUI::updateCanvas()
{
    const PDFAppToolInputList* list = getSourceList();
    if (!m_canvas || !list)
    {
        return;
    }

    // The new pages (388:18837): the page with its image laid out, "New page 13",
    // "site_photo.jpg · A4 landscape"; the cards of the images kept by their ids
    std::map<QString, PDFAppAddImagePageCard*> oldCards;
    for (PDFAppAddImagePageCard* card : m_canvas->getCards())
    {
        oldCards[card->getInputId()] = card;
    }
    std::vector<PDFAppAddImagePageCard*> cards;
    PDFInteger nextPage = getInsertionIndex() + 1;
    for (const QString& id : getRunInputIds())
    {
        const PDFAppToolInput* input = list->findInput(id);
        if (!input)
        {
            continue;
        }
        const auto it = oldCards.find(id);
        PDFAppAddImagePageCard* card = it != oldCards.cend() ? it->second : new PDFAppAddImagePageCard(id, m_canvas);
        const QSizeF imageSize = PDFImagePageFactory::getPageSize(input->imageSize, input->imageDpiX, input->imageDpiY);
        card->getPreview()->setImage(getPreviewImage(*input), imageSize);
        card->getPreview()->setPageLayout(getImageLayout(id));
        const QString title = input->pageCount > 1 ? tr("New pages %1–%2").arg(PDFAppLocale::formatNumber(nextPage), PDFAppLocale::formatNumber(nextPage + input->pageCount - 1))
                                                   : tr("New page %1").arg(PDFAppLocale::formatNumber(nextPage));
        card->setTexts(title, tr("%1 · %2").arg(input->name, PDFAppToolGUIImages::getPageDescription(getImagePageSize(id))));
        card->setSelected(id == m_currentInput);
        card->setEnabled(isEditable());
        cards.push_back(card);
        nextPage += input->pageCount;
    }
    m_canvas->setCards(cards);

    // The header "New pages · 2" and the hint of the scaling (388:18837: Fit, keep ratio)
    m_canvas->setHeaderText(tr("New pages · %1").arg(PDFAppLocale::formatNumber(getInsertedPageCount())));
    const QString scaling = getParameter(PDFDocumentToolAddImage::PARAMETER_IMAGE_SCALING);
    QString hint = tr("Orientation follows each image. The image is fitted inside the margins and keeps its proportions.");
    if (scaling == QLatin1String(PDFDocumentToolAddImage::SCALING_FILL))
    {
        hint = tr("Orientation follows each image. The image fills the area inside the margins and keeps its proportions; the rest is clipped.");
    }
    else if (scaling == QLatin1String(PDFDocumentToolAddImage::SCALING_STRETCH))
    {
        hint = tr("Orientation follows each image. The image is stretched over the area inside the margins.");
    }
    else if (scaling == QLatin1String(PDFDocumentToolAddImage::SCALING_ORIGINAL_SIZE))
    {
        hint = tr("Orientation follows each image. The image has its size by its resolution; a larger one is clipped at the margins.");
    }
    m_canvas->setHintText(hint);
}

void PDFAppAddImageToolGUI::updateRightPanel()
{
    using Helper = PDFAppInsertToolGUIHelper;

    if (m_sizeFields)
    {
        m_sizeFields->setVisible(getParameter(PDFDocumentToolAddImage::PARAMETER_PAGE_SIZE) == QLatin1String(PDFDocumentToolAddImage::SIZE_CUSTOM));
    }
    if (!m_summary)
    {
        return;
    }

    // Summary 388:18837: New pages "2", Position "After page 12", Page size "A4, as page 12",
    // Document "48 → 50 pages"; 388:19253 "–" and "48 pages"
    const PDFAppToolInputList* list = getSourceList();
    const PDFInteger count = getInsertedPageCount();
    const bool isPending = list && (list->getLoadingCount() > 0 || list->getAttentionCount() > 0);
    m_summary->setValue(0, count == 0 ? QStringLiteral("–") : PDFAppLocale::formatNumber(count));
    m_summary->setValue(1, getPositionValueText());
    m_summary->setValue(2, getPageSizeValueText());
    m_summary->setValue(3, isPending ? QStringLiteral("–") : (count == 0 ? Helper::pages(getDocumentPageCount()) : Helper::getDocumentChangeText(getDocumentPageCount(), getResultPageCount())));
}

void PDFAppAddImageToolGUI::updateEmptyState()
{
    using Helper = PDFAppInsertToolGUIHelper;

    if (!m_canvas)
    {
        return;
    }

    // 388:19253: without an image the drop zone "Add the images" with Choose Images…
    const bool isEmpty = !getSourceList() || getSourceList()->getCount() == 0;
    if (!isEmpty)
    {
        if (getEmptyState())
        {
            m_canvas->setEmptyState(nullptr);
        }
        return;
    }
    const QString signature = getDocumentTitle() + QStringLiteral("|") + QString::number(int(isEditable()));
    PDFAppInsertEmptyState* emptyState = getEmptyState();
    if (emptyState && emptyState->property(Helper::ROW_KEY).toString() == signature)
    {
        return;
    }

    PDFAppInsertEmptyState::Content content;
    content.iconName = QStringLiteral("page-image");
    content.title = tr("Add the images");
    content.text = tr("Each image becomes a new page of %1. Drop image files here or paste a picture with %2.").arg(getDocumentTitle(), Helper::getKeysText(Helper::getPasteKeys()));
    content.primaryText = tr("Choose Images…");
    content.primaryIconName = QStringLiteral("folder-open");
    PDFAppInsertEmptyState* newState = new PDFAppInsertEmptyState(content, m_canvas);
    newState->setProperty(Helper::ROW_KEY, signature);
    newState->setEnabled(isEditable());
    connect(newState, &PDFAppInsertEmptyState::primaryRequested, this, &PDFAppAddImageToolGUI::onEmptyStateAddFiles);
    m_canvas->setEmptyState(newState);
}

PDFAppInsertEmptyState* PDFAppAddImageToolGUI::getEmptyState() const
{
    return m_canvas ? qobject_cast<PDFAppInsertEmptyState*>(m_canvas->getEmptyState()) : nullptr;
}

void PDFAppAddImageToolGUI::onImageRowClicked(const QString& inputId)
{
    setCurrentSource(inputId);
}

void PDFAppAddImageToolGUI::onImageLinkActivated(const QString& inputId)
{
    // The link of a row, which cannot be used: Remove file
    removeInput(inputId);
}

void PDFAppAddImageToolGUI::onCardClicked(const QString& inputId)
{
    setCurrentSource(inputId);
}

void PDFAppAddImageToolGUI::onZoomChanged(bool isFit)
{
    m_isFit = isFit;
}

void PDFAppAddImageToolGUI::onPageRowClicked()
{
    const QObject* row = sender();
    const QString sizeId = row ? row->property(PDFAppInsertToolGUIHelper::ROW_KEY).toString() : QString();
    QPointer<PDFAppAddImageToolGUI> guard(this);
    closePanel(QStringLiteral("pageSize"));
    PDFAppToolParameterModel* model = getModel();
    if (!guard || sizeId.isEmpty() || !model || !isEditable())
    {
        return;
    }
    const QString error = model->setValue(QString::fromLatin1(PDFDocumentToolAddImage::PARAMETER_PAGE_SIZE), sizeId);
    if (guard && !error.isEmpty() && getHost())
    {
        getHost()->showTransientMessage(error);
    }
}

}   // namespace pdf

#include "moc_pdfapptoolgui_insert.cpp"
