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

#include "pdfapphomedialogs.h"
#include "pdfapphomewidgets.h"
#include "pdfappinfowidgets.h"
#include "pdfappmainwindow.h"
#include "pdfappworkspace.h"
#include "pdfrecentrepository.h"
#include "pdfrepositoryhelper.h"
#include "pdfworkspacerepository.h"

#include "pdfcomponentbadges.h"
#include "pdfcomponentbuttons.h"
#include "pdfcomponentpanels.h"
#include "pdffluentwidgets.h"
#include "pdfthemefonts.h"
#include "pdfwidgetstyleproperties.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QPushButton>
#include <QVBoxLayout>

namespace pdf
{

/// Helpers of the dialogs of Home, Open and Recent
class PDFAppHomeDialogsHelper
{
public:
    PDFAppHomeDialogsHelper() = delete;

    static QVBoxLayout* createColumn(QWidget* widget, const QMargins& margins, int spacing)
    {
        QVBoxLayout* layout = new QVBoxLayout(widget);
        layout->setContentsMargins(margins);
        layout->setSpacing(spacing);
        return layout;
    }

    /// Label of a field (Semi Bold 13 text/primary)
    static PDFLabel* createFieldLabel(const QString& text, QWidget* parent)
    {
        return new PDFLabel(text, PDFThemeFonts::Role::BodySemiBold, PDFThemeColors::TextPrimary, parent);
    }

    /// Error row of a field: the icon alert-circle 14 and the text Regular 12 text/danger, gap 6
    static QWidget* createErrorRow(QWidget* parent, PDFAppParagraph** text)
    {
        QWidget* row = new QWidget(parent);
        row->setObjectName(QStringLiteral("errorRow"));
        QHBoxLayout* layout = new QHBoxLayout(row);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(6);
        layout->addWidget(createIcon(QStringLiteral("alert-circle"), 14, PDFThemeColors::IconDanger, row), 0, Qt::AlignTop);
        *text = PDFAppPageWidgets::createText(QString(), PDFThemeFonts::getFont(PDFThemeFonts::Role::Small), 15, PDFThemeColors::TextDanger, row);
        (*text)->setObjectName(QStringLiteral("errorText"));
        layout->addWidget(*text, 1);
        row->hide();
        return row;
    }

    /// Plain icon of the design (an icon badge without the fill)
    static PDFIconBadge* createIcon(const QString& iconName, int size, PDFThemeColors::Color color, QWidget* parent)
    {
        PDFIconBadge* icon = new PDFIconBadge(iconName, size, size, parent);
        icon->setRadius(0.0);
        icon->setColors(std::nullopt, color);
        return icon;
    }

    /// Field of a name (34 px, the focus ring of the style)
    static PDFLineEdit* createNameField(const QString& name, QWidget* parent)
    {
        PDFLineEdit* field = new PDFLineEdit(parent);
        field->setObjectName(QStringLiteral("nameField"));
        field->setMaxLength(PDFRepositoryHelper::MAXIMUM_NAME_LENGTH);
        field->setText(name);
        field->selectAll();
        return field;
    }
};

PDFAppCardDialog::PDFAppCardDialog(PDFThemeIcons::Badge badge, bool hasBadge, const QString& title, const QString& text, int width, QWidget* parent) :
    BaseClass(badge, title, text, QDialogButtonBox::NoButton, parent)
{
    // The message box of the library: the badge of the design or none (a form),
    // the width of the frame (the texts wrap, the height follows the content);
    // the scrim covers the body of the window, not its top bar
    setBadgeVisible(hasBadge);
    setCardWidth(qMax(240, width));
    PDFAppMainWindow::prepareDialog(this, parent);
}

PDFAppCardDialog::~PDFAppCardDialog() = default;

QPushButton* PDFAppCardDialog::addActionButton(const QString& text, bool isPrimary, bool isLeading)
{
    // The own row of the message box in the order of the design
    return BaseClass::addActionButton(text, isLeading ? ActionPosition::Leading : ActionPosition::Trailing, isPrimary);
}

PDFAppNameDialog::PDFAppNameDialog(const QString& title, const QString& text, const QString& name, const QString& actionText, QWidget* parent) :
    BaseClass(PDFThemeIcons::Badge::Information, false, title, text, 440, parent)
{
    // field-name: label and field, gap 6; the error below the field
    QWidget* field = new QWidget(this);
    field->setObjectName(QStringLiteral("fieldName"));
    QVBoxLayout* fieldLayout = PDFAppHomeDialogsHelper::createColumn(field, QMargins(), 6);
    fieldLayout->addWidget(PDFAppHomeDialogsHelper::createFieldLabel(tr("Name"), field));
    m_nameField = PDFAppHomeDialogsHelper::createNameField(name, field);
    m_nameField->setAccessibleName(tr("Name"));
    fieldLayout->addWidget(m_nameField);
    PDFAppParagraph* errorText = nullptr;
    m_errorRow = PDFAppHomeDialogsHelper::createErrorRow(field, &errorText);
    m_errorText = errorText;
    fieldLayout->addWidget(m_errorRow);
    addContent(field);

    m_cancelButton = addActionButton(tr("Cancel"), false, false);
    m_cancelButton->setObjectName(QStringLiteral("cancelButton"));
    m_actionButton = addActionButton(actionText, true, false);
    m_actionButton->setObjectName(QStringLiteral("actionButton"));

    connect(m_cancelButton, &QPushButton::clicked, this, &PDFAppNameDialog::reject);
    connect(m_actionButton, &QPushButton::clicked, this, &PDFAppNameDialog::onActionClicked);
    connect(m_nameField, &QLineEdit::textChanged, this, &PDFAppNameDialog::onNameChanged);
    // Enter clicks the default button (QDialog::keyPressEvent - the field ignores the
    // key); a connection of returnPressed would perform the action a second time
    onNameChanged(m_nameField->text());
    m_nameField->setFocus();
}

PDFAppNameDialog::~PDFAppNameDialog() = default;

QString PDFAppNameDialog::getErrorText() const
{
    return m_errorRow && !m_errorRow->isHidden() && m_errorText ? m_errorText->text() : QString();
}

void PDFAppNameDialog::setError(const QString& error)
{
    if (m_errorText)
    {
        m_errorText->setText(error);
    }
    if (m_errorRow)
    {
        m_errorRow->setVisible(!error.isEmpty());
    }
    if (m_nameField)
    {
        m_nameField->setInvalid(!error.isEmpty(), error);
    }
    adjustSize();
}

void PDFAppNameDialog::onActionClicked()
{
    if (!m_nameField || !m_actionButton || !m_actionButton->isEnabled())
    {
        return;
    }

    QString error;
    QPointer<PDFAppNameDialog> guard(this);
    const bool isSubmitted = submitName(m_nameField->text(), &error);
    if (!guard)
    {
        return;
    }

    if (isSubmitted)
    {
        accept();
        return;
    }
    setError(error);
}

void PDFAppNameDialog::onNameChanged(const QString& text)
{
    setError(QString());
    if (m_actionButton)
    {
        m_actionButton->setEnabled(!PDFWorkspaceRepository::normalizeName(text).isEmpty());
    }
}

PDFAppRenameWorkspaceDialog::PDFAppRenameWorkspaceDialog(PDFAppContext* context, qint64 workspaceId, const QString& name, QWidget* parent) :
    BaseClass(tr("Rename workspace"), tr("The new name is shown in Open › Workspaces and in Recent. The documents are not changed."), name, tr("Rename"), parent),
    m_context(context),
    m_workspaceId(workspaceId)
{
    setObjectName(QStringLiteral("renameWorkspaceDialog"));
}

PDFAppRenameWorkspaceDialog::~PDFAppRenameWorkspaceDialog() = default;

bool PDFAppRenameWorkspaceDialog::submitName(const QString& name, QString* error)
{
    if (!m_context || !m_context->getDatabase())
    {
        *error = tr("The settings of PDF4QT are not available.");
        return false;
    }

    bool isDuplicate = false;
    const PDFDatabaseError result = m_context->getWorkspaces().renameWorkspace(m_workspaceId, name, &isDuplicate);
    if (isDuplicate)
    {
        *error = tr("A workspace named “%1” already exists. Choose another name.").arg(PDFAppHomeFormat::toDisplayText(PDFWorkspaceRepository::normalizeName(name)));
        return false;
    }
    if (result.isError())
    {
        // Deleted meanwhile (another PDF4QT window): said, not a silent success
        const bool isDeleted = !result.isBusy() && !m_context->getWorkspaces().getWorkspace(m_workspaceId);
        *error = isDeleted ? tr("The workspace no longer exists. It may have been deleted in another PDF4QT window.") : result.getMessage();
        return false;
    }
    return true;
}

PDFAppRenameLocationDialog::PDFAppRenameLocationDialog(PDFAppContext* context, qint64 locationId, const QString& name, QWidget* parent) :
    BaseClass(tr("Rename location"), tr("The new name is shown in Open › Pinned Locations. The folder is not changed."), name, tr("Rename"), parent),
    m_context(context),
    m_locationId(locationId)
{
    setObjectName(QStringLiteral("renameLocationDialog"));
}

PDFAppRenameLocationDialog::~PDFAppRenameLocationDialog() = default;

bool PDFAppRenameLocationDialog::submitName(const QString& name, QString* error)
{
    if (!m_context || !m_context->getDatabase())
    {
        *error = tr("The settings of PDF4QT are not available.");
        return false;
    }

    const PDFDatabaseError result = m_context->getRecent().renamePinnedLocation(m_locationId, name);
    if (result.isError())
    {
        // Removed meanwhile (another PDF4QT window): said, not a silent success
        bool isRemoved = !result.isBusy();
        for (const PDFPinnedLocation& location : m_context->getRecent().getPinnedLocations())
        {
            isRemoved = isRemoved && location.id != m_locationId;
        }
        *error = isRemoved ? tr("The pinned location no longer exists. It may have been removed in another PDF4QT window.") : result.getMessage();
        return false;
    }
    return true;
}

PDFAppSaveWorkspaceDialog::PDFAppSaveWorkspaceDialog(PDFAppContext* context, const std::vector<PDFAppOpenDocument>& documents, QWidget* parent) :
    BaseClass(PDFThemeIcons::Badge::Information, false, tr("Save as workspace"),
              tr("Saves the open documents together, so you can open all of them again from Open › Workspaces, each at its page."), 520, parent),
    m_context(context)
{
    setObjectName(QStringLiteral("saveWorkspaceDialog"));

    // field-name
    QWidget* nameField = new QWidget(this);
    nameField->setObjectName(QStringLiteral("fieldName"));
    QVBoxLayout* nameLayout = PDFAppHomeDialogsHelper::createColumn(nameField, QMargins(), 6);
    nameLayout->addWidget(PDFAppHomeDialogsHelper::createFieldLabel(tr("Name"), nameField));
    m_nameField = PDFAppHomeDialogsHelper::createNameField(QString(), nameField);
    m_nameField->setAccessibleName(tr("Name"));
    m_nameField->setPlaceholderText(tr("Name of the workspace"));
    nameLayout->addWidget(m_nameField);
    PDFAppParagraph* errorText = nullptr;
    m_errorRow = PDFAppHomeDialogsHelper::createErrorRow(nameField, &errorText);
    m_errorText = errorText;
    nameLayout->addWidget(m_errorRow);
    addContent(nameField);

    // field-documents: the label with the count, the list in a note box, the note
    QWidget* documentsField = new QWidget(this);
    documentsField->setObjectName(QStringLiteral("fieldDocuments"));
    QVBoxLayout* documentsLayout = PDFAppHomeDialogsHelper::createColumn(documentsField, QMargins(), 6);
    QWidget* labelRow = new QWidget(documentsField);
    QHBoxLayout* labelLayout = new QHBoxLayout(labelRow);
    labelLayout->setContentsMargins(0, 0, 0, 0);
    labelLayout->setSpacing(8);
    labelLayout->addWidget(PDFAppHomeDialogsHelper::createFieldLabel(tr("Documents"), labelRow));
    m_selectionLabel = new PDFLabel(QString(), PDFThemeFonts::Role::CaptionRegular, PDFThemeColors::TextMuted, labelRow);
    m_selectionLabel->setObjectName(QStringLiteral("selectionLabel"));
    labelLayout->addWidget(m_selectionLabel, 0, Qt::AlignBottom);
    labelLayout->addStretch(1);
    documentsLayout->addWidget(labelRow);

    QFrame* list = new QFrame(documentsField);
    list->setObjectName(QStringLiteral("documentList"));
    PDFWidgetStyleProperties::set(list, PDFWidgetStyleProperties::NOTE);
    QVBoxLayout* listLayout = PDFAppHomeDialogsHelper::createColumn(list, QMargins(12, 4, 12, 4), 0);

    int index = 0;
    for (const PDFAppOpenDocument& document : documents)
    {
        if (index++ >= PDFWorkspaceRepository::MAXIMUM_DOCUMENTS)
        {
            break;
        }

        const bool hasFile = !document.path.isEmpty();
        const QString title = PDFAppHomeFormat::toDisplayText(!document.title.isEmpty() ? document.title : QFileInfo(document.path).fileName());

        // doc row: check box 18, icon file 16, the texts, gap 10, paddings 8
        QWidget* row = new QWidget(list);
        row->setObjectName(QStringLiteral("documentRow"));
        QHBoxLayout* rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 8, 0, 8);
        rowLayout->setSpacing(10);

        QCheckBox* checkBox = new QCheckBox(row);
        checkBox->setObjectName(QStringLiteral("documentCheckBox"));
        checkBox->setAccessibleName(title);
        checkBox->setChecked(hasFile);
        checkBox->setEnabled(hasFile);
        rowLayout->addWidget(checkBox, 0, Qt::AlignVCenter);
        rowLayout->addWidget(PDFAppHomeDialogsHelper::createIcon(QStringLiteral("file"), 16, hasFile ? PDFThemeColors::IconMuted : PDFThemeColors::IconDisabled, row), 0, Qt::AlignVCenter);

        QWidget* texts = new QWidget(row);
        QVBoxLayout* textsLayout = PDFAppHomeDialogsHelper::createColumn(texts, QMargins(), 1);
        QWidget* titleRow = new QWidget(texts);
        QHBoxLayout* titleLayout = new QHBoxLayout(titleRow);
        titleLayout->setContentsMargins(0, 0, 0, 0);
        titleLayout->setSpacing(8);
        // A document without a file cannot be saved: its row is disabled (text/disabled, 941:136915)
        PDFAppElidedLabel* titleLabel = new PDFAppElidedLabel(PDFThemeFonts::getFont(PDFThemeFonts::Role::BodySemiBold), hasFile ? PDFThemeColors::TextPrimary : PDFThemeColors::TextDisabled,
                                                              PDFComponentPainter::TextKind::FileName, titleRow);
        titleLabel->setText(title);
        titleLabel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        titleLayout->addWidget(titleLabel, 0);
        if (hasFile && document.isModified)
        {
            PDFTag* tag = new PDFTag(tr("Unsaved changes"), titleRow);
            tag->setObjectName(QStringLiteral("unsavedTag"));
            titleLayout->addWidget(tag, 0, Qt::AlignVCenter);
        }
        titleLayout->addStretch(1);
        textsLayout->addWidget(titleRow);

        QString details;
        if (!hasFile)
        {
            details = tr("Not saved to a file yet – save it first");
        }
        else
        {
            QStringList parts;
            parts << PDFAppHomeFormat::toDisplayPath(QFileInfo(document.path).absolutePath());
            if (document.page > 0)
            {
                parts << (document.pageCount > 0 ? tr("page %1 of %2").arg(document.page).arg(document.pageCount) : tr("page %1").arg(document.page));
            }
            if (document.isModified)
            {
                parts << tr("the saved file is used");
            }
            details = parts.join(QStringLiteral(" · "));
        }
        PDFAppElidedLabel* detailsLabel = new PDFAppElidedLabel(PDFThemeFonts::getFont(PDFThemeFonts::Role::CaptionRegular), hasFile ? PDFThemeColors::TextMuted : PDFThemeColors::TextDisabled,
                                                                PDFComponentPainter::TextKind::Data, texts);
        detailsLabel->setText(details);
        textsLayout->addWidget(detailsLabel);
        rowLayout->addWidget(texts, 1);
        listLayout->addWidget(row);

        DocumentRow documentRow;
        documentRow.checkBox = checkBox;
        documentRow.document.path = document.path;
        documentRow.document.page = qMax(0, document.page);
        m_documents.push_back(documentRow);
        connect(checkBox, &QCheckBox::toggled, this, &PDFAppSaveWorkspaceDialog::onSelectionChanged);
    }

    list->setVisible(!m_documents.empty());
    documentsLayout->addWidget(list);

    // note: info 14 and the text Regular 11 text/muted, gap 8
    QWidget* note = new QWidget(documentsField);
    note->setObjectName(QStringLiteral("note"));
    QHBoxLayout* noteLayout = new QHBoxLayout(note);
    noteLayout->setContentsMargins(0, 0, 0, 0);
    noteLayout->setSpacing(8);
    noteLayout->addWidget(PDFAppHomeDialogsHelper::createIcon(QStringLiteral("info"), 14, PDFThemeColors::IconMuted, note), 0, Qt::AlignTop);
    noteLayout->addWidget(PDFAppPageWidgets::createText(tr("Stored in PDF4QT settings. A workspace keeps the files and the pages, not unsaved changes."),
                                                     PDFThemeFonts::getFont(PDFThemeFonts::Role::CaptionRegular), 13, PDFThemeColors::TextMuted, note), 1);
    documentsLayout->addWidget(note);
    addContent(documentsField);

    // Buttons: Cancel / Save; with a duplicate name Cancel (primary) / Replace
    m_secondaryCancelButton = addActionButton(tr("Cancel"), false, false);
    m_secondaryCancelButton->setObjectName(QStringLiteral("cancelButton"));
    m_saveButton = addActionButton(tr("Save"), true, false);
    m_saveButton->setObjectName(QStringLiteral("saveButton"));
    m_cancelButton = addActionButton(tr("Cancel"), true, false);
    m_cancelButton->setObjectName(QStringLiteral("duplicateCancelButton"));
    m_replaceButton = addActionButton(tr("Replace"), false, false);
    m_replaceButton->setObjectName(QStringLiteral("replaceButton"));
    m_cancelButton->hide();
    m_replaceButton->hide();

    connect(m_secondaryCancelButton, &QPushButton::clicked, this, &PDFAppSaveWorkspaceDialog::reject);
    connect(m_cancelButton, &QPushButton::clicked, this, &PDFAppSaveWorkspaceDialog::reject);
    connect(m_saveButton, &QPushButton::clicked, this, &PDFAppSaveWorkspaceDialog::onSaveClicked);
    connect(m_replaceButton, &QPushButton::clicked, this, &PDFAppSaveWorkspaceDialog::onReplaceClicked);
    connect(m_nameField, &QLineEdit::textChanged, this, &PDFAppSaveWorkspaceDialog::onNameChanged);
    // Enter clicks the default button (QDialog::keyPressEvent): Save, or the primary
    // Cancel after a duplicate name. A connection of returnPressed would save first and
    // the same key would then click the new default Cancel - the dialog would close.

    onSelectionChanged();
    m_nameField->setFocus();
}

PDFAppSaveWorkspaceDialog::~PDFAppSaveWorkspaceDialog() = default;

QList<QCheckBox*> PDFAppSaveWorkspaceDialog::getDocumentCheckBoxes() const
{
    QList<QCheckBox*> checkBoxes;
    for (const DocumentRow& row : m_documents)
    {
        if (row.checkBox)
        {
            checkBoxes << row.checkBox.data();
        }
    }
    return checkBoxes;
}

QString PDFAppSaveWorkspaceDialog::getErrorText() const
{
    return m_errorRow && !m_errorRow->isHidden() && m_errorText ? m_errorText->text() : QString();
}

QString PDFAppSaveWorkspaceDialog::getSelectionText() const
{
    return m_selectionLabel ? m_selectionLabel->text() : QString();
}

std::vector<PDFWorkspaceDocument> PDFAppSaveWorkspaceDialog::getSelectedDocuments() const
{
    std::vector<PDFWorkspaceDocument> documents;
    for (const DocumentRow& row : m_documents)
    {
        if (row.checkBox && row.checkBox->isChecked() && row.checkBox->isEnabled() && !row.document.path.isEmpty())
        {
            documents.push_back(row.document);
        }
    }
    return documents;
}

void PDFAppSaveWorkspaceDialog::onSaveClicked()
{
    if (m_saveButton && m_saveButton->isVisible() && m_saveButton->isEnabled())
    {
        save(false);
    }
}

void PDFAppSaveWorkspaceDialog::onReplaceClicked()
{
    save(true);
}

void PDFAppSaveWorkspaceDialog::onNameChanged(const QString& text)
{
    Q_UNUSED(text);
    setDuplicate(false, QString());
}

void PDFAppSaveWorkspaceDialog::onSelectionChanged()
{
    int selected = 0;
    for (const DocumentRow& row : m_documents)
    {
        selected += (row.checkBox && row.checkBox->isChecked()) ? 1 : 0;
    }

    if (m_selectionLabel)
    {
        m_selectionLabel->setText(tr("%1 of %2 selected").arg(selected).arg(m_documents.size()));
    }
    updateButtons();
}

void PDFAppSaveWorkspaceDialog::save(bool replaceExisting)
{
    if (!m_context || !m_context->getDatabase() || !m_nameField)
    {
        setDuplicate(false, tr("The settings of PDF4QT are not available."));
        return;
    }

    const std::vector<PDFWorkspaceDocument> documents = getSelectedDocuments();
    if (documents.empty())
    {
        updateButtons();
        return;
    }

    bool isDuplicate = false;
    qint64 id = 0;
    const QString name = m_nameField->text();
    const PDFDatabaseError error = m_context->getWorkspaces().saveWorkspace(name, documents, replaceExisting, &id, &isDuplicate);
    if (isDuplicate)
    {
        // 955:147918: "A workspace named “Invoices 2026” already exists (12 documents). Choose another name, or replace it."
        const std::optional<PDFWorkspace> existing = m_context->getWorkspaces().findWorkspace(name);
        const int count = existing ? existing->documentCount : 0;
        setDuplicate(true, tr("A workspace named “%1” already exists (%2). Choose another name, or replace it.")
                               .arg(PDFAppHomeFormat::toDisplayText(PDFWorkspaceRepository::normalizeName(name)), PDFAppHomeFormat::formatDocumentCount(count)));
        return;
    }

    if (error.isError())
    {
        setDuplicate(false, error.getMessage());
        return;
    }

    m_savedId = id;
    accept();
}

void PDFAppSaveWorkspaceDialog::setDuplicate(bool isDuplicate, const QString& error)
{
    m_isDuplicateShown = isDuplicate;
    if (m_errorText)
    {
        m_errorText->setText(error);
    }
    if (m_errorRow)
    {
        m_errorRow->setVisible(!error.isEmpty());
    }
    if (m_nameField)
    {
        m_nameField->setInvalid(!error.isEmpty(), error);
    }
    updateButtons();
    adjustSize();
}

void PDFAppSaveWorkspaceDialog::updateButtons()
{
    const bool hasName = m_nameField && !PDFWorkspaceRepository::normalizeName(m_nameField->text()).isEmpty();
    const bool hasDocuments = !getSelectedDocuments().empty();

    if (m_secondaryCancelButton)
    {
        m_secondaryCancelButton->setVisible(!m_isDuplicateShown);
    }
    if (m_saveButton)
    {
        m_saveButton->setVisible(!m_isDuplicateShown);
        m_saveButton->setEnabled(hasName && hasDocuments);
        m_saveButton->setDefault(!m_isDuplicateShown);
    }
    if (m_cancelButton)
    {
        m_cancelButton->setVisible(m_isDuplicateShown);
        m_cancelButton->setDefault(m_isDuplicateShown);
    }
    if (m_replaceButton)
    {
        m_replaceButton->setVisible(m_isDuplicateShown);
        m_replaceButton->setEnabled(hasName && hasDocuments);
    }
}

PDFAppClearHistoryDialog::PDFAppClearHistoryDialog(PDFAppContext* context, QWidget* parent) :
    BaseClass(PDFThemeIcons::Badge::Warning, true, tr("Clear the Recent history?"),
              tr("Recent forgets which documents, folders and workspaces you opened and where you stopped reading. "
                 "Your files are not touched, and saved workspaces stay in PDF4QT settings."), 520, parent),
    m_context(context)
{
    setObjectName(QStringLiteral("clearHistoryDialog"));

    // facts: bg/subtle, radius 10, paddings 4 / 12, rows of PDFKeyValueList
    QFrame* facts = new QFrame(this);
    facts->setObjectName(QStringLiteral("facts"));
    PDFWidgetStyleProperties::set(facts, PDFWidgetStyleProperties::NOTE);
    QVBoxLayout* factsLayout = PDFAppHomeDialogsHelper::createColumn(facts, QMargins(12, 4, 12, 4), 0);
    m_facts = new PDFKeyValueList(facts);
    m_facts->setObjectName(QStringLiteral("factsList"));
    factsLayout->addWidget(m_facts);
    addContent(facts);

    m_pinnedCheckBox = new QCheckBox(this);
    m_pinnedCheckBox->setObjectName(QStringLiteral("pinnedCheckBox"));
    addContent(m_pinnedCheckBox);

    m_clearButton = addActionButton(tr("Clear Recent History"), false, true);
    m_clearButton->setObjectName(QStringLiteral("clearButton"));
    m_cancelButton = addActionButton(tr("Cancel"), true, false);
    m_cancelButton->setObjectName(QStringLiteral("cancelButton"));

    connect(m_clearButton, &QPushButton::clicked, this, &PDFAppClearHistoryDialog::onClearClicked);
    connect(m_cancelButton, &QPushButton::clicked, this, &PDFAppClearHistoryDialog::reject);
    connect(m_pinnedCheckBox, &QCheckBox::toggled, this, &PDFAppClearHistoryDialog::updateFacts);
    updateFacts();
}

PDFAppClearHistoryDialog::~PDFAppClearHistoryDialog() = default;

bool PDFAppClearHistoryDialog::isPinnedIncluded() const
{
    return m_pinnedCheckBox && m_pinnedCheckBox->isChecked();
}

void PDFAppClearHistoryDialog::updateFacts()
{
    PDFRecentClearSummary summary;
    if (m_context && m_context->getDatabase())
    {
        summary = m_context->getRecent().getClearHistorySummary(isPinnedIncluded());
    }

    if (m_pinnedCheckBox)
    {
        m_pinnedCheckBox->setText(tr("Also remove the pinned items (%1)").arg(summary.pinnedFiles));
        m_pinnedCheckBox->setVisible(summary.pinnedFiles > 0 || m_pinnedCheckBox->isChecked());
    }

    if (!m_facts)
    {
        return;
    }

    QStringList alsoCleared;
    if (summary.hasContinueReading)
    {
        alsoCleared << tr("Continue where you left off");
    }
    if (summary.hasLastSession)
    {
        alsoCleared << tr("Restore last session");
    }

    QStringList kept;
    if (!isPinnedIncluded() && summary.pinnedFiles > 0)
    {
        kept << (summary.pinnedFiles == 1 ? tr("%n pinned item", nullptr, 1) : tr("%n pinned items", nullptr, summary.pinnedFiles));
    }
    kept << tr("saved workspaces");

    m_facts->clear();
    const int removed = summary.getRemovedEntries();
    m_facts->addRow(tr("Removed"), removed == 1 ? tr("%n entry", nullptr, 1) : tr("%n entries", nullptr, removed));
    m_facts->addRow(tr("Also cleared"), alsoCleared.isEmpty() ? tr("Nothing else") : alsoCleared.join(QStringLiteral(", ")));
    m_facts->addRow(tr("Kept"), kept.join(QStringLiteral(", ")));
    adjustSize();
}

void PDFAppClearHistoryDialog::onClearClicked()
{
    // The page clears the history, when the dialog is accepted
    accept();
}

PDFAppDeleteWorkspaceDialog::PDFAppDeleteWorkspaceDialog(const QString& name, int documentCount, qint64 workspaceId, QWidget* parent) :
    BaseClass(PDFThemeIcons::Badge::Warning, true, tr("Delete the workspace “%1”?").arg(PDFAppHomeFormat::toDisplayText(name, 64)),
              tr("The workspace is removed from PDF4QT settings and from Recent. Its %1 are not touched – they stay where they are on the disk.")
                  .arg(PDFAppHomeFormat::formatDocumentCount(documentCount)), 440, parent),
    m_workspaceId(workspaceId)
{
    setObjectName(QStringLiteral("deleteWorkspaceDialog"));
    m_deleteButton = addActionButton(tr("Delete Workspace"), false, true);
    m_deleteButton->setObjectName(QStringLiteral("deleteButton"));
    m_cancelButton = addActionButton(tr("Cancel"), true, false);
    m_cancelButton->setObjectName(QStringLiteral("cancelButton"));
    connect(m_deleteButton, &QPushButton::clicked, this, &PDFAppDeleteWorkspaceDialog::accept);
    connect(m_cancelButton, &QPushButton::clicked, this, &PDFAppDeleteWorkspaceDialog::reject);
}

PDFAppDeleteWorkspaceDialog::~PDFAppDeleteWorkspaceDialog() = default;

}   // namespace pdf
