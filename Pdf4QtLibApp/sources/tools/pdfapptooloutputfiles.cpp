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

#include "pdfapptooloutputfiles.h"
#include "pdfappdocument.h"
#include "pdfappfiledrop.h"
#include "pdfapplocale.h"
#include "pdfapppagelayout.h"
#include "pdfappsettingcontrol.h"
#include "pdfapptoolpanelbuilder.h"
#include "pdfapptoolpanelparts.h"
#include "pdfapptoolpanelpopup.h"
#include "pdfapptoolparametermodel.h"
#include "pdfapptooltoolbar.h"
#include "pdfdocumenttool_split.h"
#include "pdfdocumenttoolgui.h"

#include "pdfcomponentbuttons.h"
#include "pdfcomponentrows.h"
#include "pdffluentwidgets.h"
#include "pdfstylepainter.h"
#include "pdfthemefonts.h"
#include "pdfthemeiconengine.h"

#include <QDate>
#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>

namespace pdf
{

/// Geometry and the tokens of the section Output files (383:3315, 383:4447)
class PDFAppToolOutputFilesHelper
{
public:
    PDFAppToolOutputFilesHelper() = delete;

    /// card 383:3315: padding 12 + the border, gap 8; the folder row 6; Existing files 10, its
    /// combo box 140
    static constexpr int CARD_PADDING = 13;
    static constexpr int CARD_GAP = 8;
    static constexpr int FOLDER_GAP = 6;
    static constexpr int EXISTING_GAP = 10;
    static constexpr int EXISTING_COMBO_WIDTH = 140;

    /// names 383:3315: paddings 10 / 8, gap 2
    static constexpr int NAMES_PADDING_H = 10;
    static constexpr int NAMES_PADDING_V = 8;
    static constexpr int NAMES_GAP = 2;
    static constexpr qreal NAMES_RADIUS = 8.0;

    /// panel-tokens 383:4447
    static constexpr int TOKENS_PANEL_WIDTH = 280;
    static constexpr const char* TOKEN_PROPERTY = "pdf4qtNameToken";

    /// A row of the panel of the tokens: the token, the icon of its tile (none = the token is
    /// the text of the tile) and the description
    struct Token
    {
        const char* token;
        const char* iconName;
        QString description;

        const QString& getDescription() const { return description; }
    };

    static std::vector<Token> getTokens()
    {
        // The tokens of the frame; the core knows more of them (K25), the panel lists these
        return {
            { "#", "", PDFAppToolOutputFilesSection::tr("File number · ## pads with zeros (01, 02)") },
            { "@", "", PDFAppToolOutputFilesSection::tr("Number of the first page in the file") },
            { "{source_base}", "file", PDFAppToolOutputFilesSection::tr("Name of the source file without .pdf") },
            { "{group_name}", "group", PDFAppToolOutputFilesSection::tr("Bookmark title or group name") },
            { "{date}", "clock", PDFAppToolOutputFilesSection::tr("Today’s date, %1").arg(QDate::currentDate().toString(Qt::ISODate)) },
        };
    }

    /// A control of the settings has the fixed width of the page of the settings (260): in the
    /// card it takes the free width of its row
    static void makeFlexible(QWidget* widget)
    {
        widget->setMinimumWidth(0);
        widget->setMaximumWidth(QWIDGETSIZE_MAX);
        widget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }
};

/// The box of the preview of the names (bg/subtle, radius 8)
class PDFAppToolOutputNamesBox : public QWidget
{
public:
    explicit PDFAppToolOutputNamesBox(QWidget* parent) :
        QWidget(parent)
    {

    }

protected:
    virtual void paintEvent(QPaintEvent* event) override
    {
        Q_UNUSED(event);
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        PDFStylePainter::fill(&painter, QRectF(rect()), PDFStylePainter::Radii(PDFAppToolOutputFilesHelper::NAMES_RADIUS), PDFThemeColors::BgSubtle);
    }
};

PDFAppToolOutputFilesSection::PDFAppToolOutputFilesSection(PDFDocumentToolGUI* gui) :
    BaseClass(gui),
    m_gui(gui)
{

}

PDFAppToolOutputFilesSection::~PDFAppToolOutputFilesSection() = default;

void PDFAppToolOutputFilesSection::describe(PDFAppToolPanelBuilder& builder)
{
    if (!m_gui)
    {
        return;
    }

    using Helper = PDFAppToolOutputFilesHelper;

    // callout-red 383:9088: the error of the template at the top of the panel, above the section
    m_errorCallout = builder.addCallout(PDFAccentHue::Red, QStringLiteral("alert-circle"), QString());
    m_errorCallout->setObjectName(QStringLiteral("toolOutputError"));
    m_errorCallout->setVisible(false);
    if (PDFLabel* title = m_errorCallout->getTitleLabel())
    {
        // 383:9088: the title "Every file would be named Handbook.pdf" wraps to its second line
        title->setWordWrap(true);
        title->setMinimumWidth(1);
    }

    // One container: Extract hides the whole section for One file (385:9405)
    QWidget* container = new QWidget();
    container->setObjectName(QStringLiteral("toolOutputFiles"));
    builder.addWidget(container);
    m_container = container;

    // section-Output files 383:3315: the header with the link Name tokens, the card 8 below it
    PDFAppToolPanelBuilder sectionBuilder(m_gui, container, Qt::Vertical, PDFAppToolPanelBuilder::SECTION_GAP);
    PDFAppToolSectionHeader* header = sectionBuilder.addSection(tr("Output files"));
    m_tokensLink = new PDFTextLink(tr("Name tokens"), header);
    m_tokensLink->setObjectName(QStringLiteral("toolOutputNameTokens"));
    header->addTrailingWidget(m_tokensLink);
    connect(m_tokensLink.data(), &PDFTextLink::clicked, this, &PDFAppToolOutputFilesSection::onTokensClicked);

    // The card: padding 12 + the border, the items 8 apart; the widths of the controls of the
    // settings (260) are replaced by the widths of the frame - the panel never scrolls sideways
    PDFAppToolCard* card = new PDFAppToolCard(container);
    card->setObjectName(QStringLiteral("toolOutputCard"));
    QVBoxLayout* cardLayout = card->getBodyLayout();
    cardLayout->setContentsMargins(Helper::CARD_PADDING, Helper::CARD_PADDING, Helper::CARD_PADDING, Helper::CARD_PADDING);
    sectionBuilder.addWidget(card);
    QWidget* cardBody = new QWidget(card);
    cardBody->setObjectName(QStringLiteral("toolOutputCardBody"));
    cardLayout->addWidget(cardBody);
    PDFAppToolPanelBuilder cardBuilder(m_gui, cardBody, Qt::Vertical, Helper::CARD_GAP);
    QFont captionFont = PDFThemeFonts::getFont(QFont::Normal, 12);

    // Folder: the field and Browse… 6 apart (383:3315)
    cardBuilder.addWidget(PDFAppPageWidgets::createText(tr("Folder"), captionFont, 15, PDFThemeColors::TextSecondary, nullptr));
    QWidget* folderRow = new QWidget();
    folderRow->setObjectName(QStringLiteral("toolOutputFolderRow"));
    PDFAppToolPanelBuilder folderBuilder(m_gui, folderRow, Qt::Horizontal, Helper::FOLDER_GAP);
    if (PDFAppSettingControl* folderControl = folderBuilder.addValueControl(QString::fromLatin1(PDFDocumentToolFileOutput::PARAMETER_FOLDER)))
    {
        QWidget* field = folderControl->getWidget();
        field->setObjectName(QStringLiteral("toolOutputFolder"));
        Helper::makeFlexible(field);
        m_folderField = qobject_cast<QLineEdit*>(field);
        if (PDFLineEdit* lineEdit = qobject_cast<PDFLineEdit*>(field))
        {
            // 383:4410: a long folder ends with "…" (its start stays visible) without the focus
            lineEdit->setElidedWhenInactive(true);
        }
    }
    m_browseButton = new QPushButton(tr("Browse…"), folderRow);
    m_browseButton->setObjectName(QStringLiteral("toolOutputBrowse"));
    folderBuilder.addWidget(m_browseButton);
    connect(m_browseButton.data(), &QPushButton::clicked, this, &PDFAppToolOutputFilesSection::onBrowseClicked);
    cardBuilder.addWidget(folderRow);

    // File name with the tokens
    cardBuilder.addWidget(PDFAppPageWidgets::createText(tr("File name"), captionFont, 15, PDFThemeColors::TextSecondary, nullptr));
    if (PDFAppSettingControl* nameControl = cardBuilder.addValueControl(QString::fromLatin1(PDFDocumentToolFileOutput::PARAMETER_FILE_NAME)))
    {
        QWidget* field = nameControl->getWidget();
        field->setObjectName(QStringLiteral("toolOutputFileName"));
        Helper::makeFlexible(field);
        m_nameField = qobject_cast<QLineEdit*>(field);
    }

    // The preview of the names in a box of bg/subtle (names 383:3315: radius 8, paddings 10 / 8,
    // gap 2), or the error of the template (383:9088)
    PDFAppToolOutputNamesBox* namesBox = new PDFAppToolOutputNamesBox(nullptr);
    namesBox->setObjectName(QStringLiteral("toolOutputNames"));
    QVBoxLayout* namesLayout = new QVBoxLayout(namesBox);
    namesLayout->setContentsMargins(Helper::NAMES_PADDING_H, Helper::NAMES_PADDING_V, Helper::NAMES_PADDING_H, Helper::NAMES_PADDING_V);
    namesLayout->setSpacing(Helper::NAMES_GAP);
    m_preview = PDFAppPageWidgets::createText(QString(), PDFThemeFonts::getFont(QFont::Medium, 12), 15, PDFThemeColors::TextPrimary, namesBox);
    m_preview->setObjectName(QStringLiteral("toolOutputPreview"));
    m_preview->setMinimumWidth(1);
    namesLayout->addWidget(m_preview);
    m_previewHint = PDFAppPageWidgets::createText(QString(), PDFThemeFonts::getFont(QFont::Normal, 11), 13, PDFThemeColors::TextTertiary, namesBox);
    m_previewHint->setObjectName(QStringLiteral("toolOutputPreviewHint"));
    m_previewHint->setMinimumWidth(1);
    namesLayout->addWidget(m_previewHint);
    cardBuilder.addWidget(namesBox);
    m_namesBox = namesBox;

    // Existing files: the label and the combo box of 140 in one row, 10 apart (field-Existing
    // files 383:3315); Ask, Overwrite, Skip, Rename (383:4447)
    QWidget* existingRow = new QWidget();
    existingRow->setObjectName(QStringLiteral("toolOutputExistingRow"));
    PDFAppToolPanelBuilder existingBuilder(m_gui, existingRow, Qt::Horizontal, Helper::EXISTING_GAP);
    PDFAppParagraph* existingLabel = PDFAppPageWidgets::createText(tr("Existing files"), captionFont, 15, PDFThemeColors::TextSecondary, nullptr);
    existingLabel->setObjectName(QStringLiteral("toolOutputExistingLabel"));
    existingLabel->setMinimumWidth(1);
    existingBuilder.addWidget(existingLabel);
    if (PDFAppSettingControl* existingControl = existingBuilder.addValueControl(QString::fromLatin1(PDFDocumentToolFileOutput::PARAMETER_EXISTING_FILES)))
    {
        existingControl->getWidget()->setObjectName(QStringLiteral("toolOutputExistingFiles"));
        existingControl->getWidget()->setFixedWidth(Helper::EXISTING_COMBO_WIDTH);
    }
    existingRow->layout()->setAlignment(existingLabel, Qt::AlignVCenter);
    cardBuilder.addWidget(existingRow);
    sectionBuilder.endSection();

    updateFolderPlaceholder();
    container->setVisible(m_isVisible);
    updatePreview();
}

void PDFAppToolOutputFilesSection::updateFolderPlaceholder()
{
    // An empty folder is the folder of the document: the field says which one it is
    if (m_folderField)
    {
        m_folderField->setPlaceholderText(QDir::toNativeSeparators(getFolder()));
    }
}

void PDFAppToolOutputFilesSection::onTokensClicked()
{
    using Helper = PDFAppToolOutputFilesHelper;

    if (!m_tokensLink)
    {
        return;
    }

    // panel-tokens 383:4447: the caption Name tokens and a row per token; a row inserts its
    // token at the cursor of the field File name. The panel is a child of the link (it dies
    // with the panel of the tool; describe builds both again after a change of the language)
    if (m_tokensPanel && m_tokensPanel->isVisible())
    {
        m_tokensPanel->close();
        return;
    }

    if (!m_tokensPanel)
    {
        PDFAppToolPanelPopup* panel = new PDFAppToolPanelPopup(nullptr, Helper::TOKENS_PANEL_WIDTH, m_tokensLink);
        panel->setObjectName(QStringLiteral("toolPanel-nameTokens"));
        panel->addCaption(tr("Name tokens"));
        for (const PDFAppToolOutputFilesHelper::Token& token : Helper::getTokens())
        {
            PDFTileRow* row = panel->addRow(QString::fromLatin1(token.token), token.getDescription(), QString::fromLatin1(token.iconName), PDFTileRow::Kind::MenuItem);
            if (!token.iconName[0])
            {
                // # and @ are the text of their tile (383:4447)
                row->setTileText(QString::fromLatin1(token.token));
            }
            row->setObjectName(QStringLiteral("toolNameToken-%1").arg(QString::fromLatin1(token.token)));
            row->setProperty(Helper::TOKEN_PROPERTY, QString::fromLatin1(token.token));
            connect(row, &PDFTileRow::clicked, this, &PDFAppToolOutputFilesSection::onTokenClicked);
        }
        m_tokensPanel = panel;
    }
    m_tokensPanel->popup(m_tokensLink, m_tokensLink->mapToGlobal(QPoint(0, m_tokensLink->height())).y() + PDFAppToolToolbar::PANEL_GAP);
}

void PDFAppToolOutputFilesSection::onTokenClicked()
{
    using Helper = PDFAppToolOutputFilesHelper;

    const QObject* row = sender();
    const QString token = row ? row->property(Helper::TOKEN_PROPERTY).toString() : QString();
    PDFAppToolParameterModel* model = m_gui ? m_gui->getModel() : nullptr;
    if (token.isEmpty() || !model)
    {
        return;
    }

    // The token at the cursor of the field (at the end, when the field has no focus position)
    const QString key = QString::fromLatin1(PDFDocumentToolFileOutput::PARAMETER_FILE_NAME);
    QString text = model->getValue(key).toString();
    const int position = m_nameField ? std::clamp(int(m_nameField->cursorPosition()), 0, int(text.size())) : int(text.size());
    text.insert(position, token);

    if (m_tokensPanel)
    {
        m_tokensPanel->close();
    }

    // One undo step; the model announces it last (its slots may rebuild the panel)
    model->setValue(key, text);
}

void PDFAppToolOutputFilesSection::setPreview(const QStringList& fileNames, const QString& hint, const QString& error, const QString& errorText)
{
    m_fileNames = fileNames;
    m_hint = hint;
    m_error = error;
    m_errorText = errorText;
    m_duplicateName.clear();
    m_duplicateCount = 0;
    updatePreview();
}

void PDFAppToolOutputFilesSection::setDuplicateNames(const QString& fileName, int count, const QString& error, const QString& errorText)
{
    m_fileNames.clear();
    m_hint.clear();
    m_error = error;
    m_errorText = errorText;
    m_duplicateName = fileName;
    m_duplicateCount = count;
    updatePreview();
}

void PDFAppToolOutputFilesSection::updatePreview()
{
    // 383:9088: equal names stay in the box (text/danger "Handbook.pdf × 4", "The names must
    // differ"); another error of the template has only its callout
    const bool isDuplicate = !m_duplicateName.isEmpty();
    if (m_preview)
    {
        m_preview->setText(isDuplicate ? tr("%1 × %2").arg(m_duplicateName, PDFAppLocale::formatNumber(m_duplicateCount)) : PDFDocumentToolFileOutput::formatFileNames(m_fileNames));
        m_preview->setTextColor(isDuplicate ? PDFThemeColors::TextDanger : PDFThemeColors::TextPrimary);
    }
    const QString hint = isDuplicate ? tr("The names must differ") : m_hint;
    if (m_previewHint)
    {
        m_previewHint->setText(hint);
        m_previewHint->setVisible(!hint.isEmpty());
    }
    if (m_namesBox)
    {
        m_namesBox->setVisible(isDuplicate || (m_error.isEmpty() && !m_fileNames.isEmpty()));
    }
    if (m_errorCallout)
    {
        m_errorCallout->setTexts(m_error, m_errorText);
        m_errorCallout->setVisible(m_isVisible && !m_error.isEmpty());
    }
    updateFolderPlaceholder();
}

void PDFAppToolOutputFilesSection::setVisible(bool isVisible)
{
    m_isVisible = isVisible;
    if (m_container)
    {
        m_container->setVisible(isVisible);
    }
    if (m_errorCallout)
    {
        m_errorCallout->setVisible(isVisible && !m_error.isEmpty());
    }
}

QString PDFAppToolOutputFilesSection::getFolder() const
{
    const PDFAppToolParameterModel* model = m_gui ? m_gui->getModel() : nullptr;
    const QString folder = model ? model->getValue(QString::fromLatin1(PDFDocumentToolFileOutput::PARAMETER_FOLDER)).toString().trimmed() : QString();
    if (!folder.isEmpty())
    {
        return folder;
    }
    const PDFAppDocument* document = m_gui ? m_gui->getDocument() : nullptr;
    return document && !document->getFilePath().isEmpty() ? QFileInfo(document->getFilePath()).absolutePath() : QString();
}

void PDFAppToolOutputFilesSection::onBrowseClicked()
{
    // The native dialog runs its own event loop: the section may be gone after it
    QPointer<PDFAppToolOutputFilesSection> guard(this);
    const QString folder = PDFAppFileDialogs::getExistingDirectory(m_browseButton ? m_browseButton->window() : nullptr, getFolder());
    if (!guard || folder.isEmpty() || !m_gui || !m_gui->getModel())
    {
        return;
    }

    // One undo step; the model announces it last
    m_gui->getModel()->setValue(QString::fromLatin1(PDFDocumentToolFileOutput::PARAMETER_FOLDER), QDir::toNativeSeparators(folder));
}

PDFAppToolFileConflictDialog::PDFAppToolFileConflictDialog(const PDFAppToolFileSink::Question& question, QWidget* parent) :
    BaseClass(PDFThemeIcons::Badge::Warning, QString(), QString(), QDialogButtonBox::NoButton, parent),
    m_question(question)
{
    setObjectName(QStringLiteral("toolFileConflictDialog"));

    // 356:11997: Stop Export at the leading edge, Skip, Rename and Overwrite (Primary) at the trailing one
    m_stopButton = addActionButton(QString(), ActionPosition::Leading, false);
    m_stopButton->setObjectName(QStringLiteral("conflictStopButton"));
    m_stopButton->setIcon(PDFThemeIcons::getIcon(QStringLiteral("stop")));
    connect(m_stopButton, &QPushButton::clicked, this, &PDFAppToolFileConflictDialog::onStopClicked);

    m_skipButton = addActionButton(QString(), ActionPosition::Trailing, false);
    m_skipButton->setObjectName(QStringLiteral("conflictSkipButton"));
    connect(m_skipButton, &QPushButton::clicked, this, &PDFAppToolFileConflictDialog::onSkipClicked);

    m_renameButton = addActionButton(QString(), ActionPosition::Trailing, false);
    m_renameButton->setObjectName(QStringLiteral("conflictRenameButton"));
    connect(m_renameButton, &QPushButton::clicked, this, &PDFAppToolFileConflictDialog::onRenameClicked);

    // R14: a source or a file open in a tab is never overwritten - Rename is the Primary button then
    m_overwriteButton = addActionButton(QString(), ActionPosition::Trailing, !m_question.isProtected);
    m_overwriteButton->setObjectName(QStringLiteral("conflictOverwriteButton"));
    m_overwriteButton->setEnabled(!m_question.isProtected);
    connect(m_overwriteButton, &QPushButton::clicked, this, &PDFAppToolFileConflictDialog::onOverwriteClicked);

    updateTexts();
}

PDFAppToolFileConflictDialog::~PDFAppToolFileConflictDialog() = default;

void PDFAppToolFileConflictDialog::updateTexts()
{
    setTitle(tr("%1 already exists").arg(m_question.fileName));
    const QString text = m_question.conflictCount > 1 ? tr("The folder %1 already contains %Ln files with the names of this export.", nullptr, m_question.conflictCount).arg(m_question.folderName)
                                                      : tr("The folder %1 already contains a file with this name.").arg(m_question.folderName);
    setText(m_question.isProtected ? tr("%1 The file is open in PDF4QT or is the source of the export, so it cannot be overwritten.").arg(text) : text);
    setCheckBoxText(m_question.otherCount > 0 ? tr("Do the same for the other %Ln files", nullptr, m_question.otherCount) : QString());

    m_stopButton->setText(tr("Stop Export"));
    m_skipButton->setText(tr("Skip"));
    m_renameButton->setText(tr("Rename"));
    m_overwriteButton->setText(tr("Overwrite"));
}

void PDFAppToolFileConflictDialog::answer(PDFAppToolFileSink::Answer answer)
{
    if (m_isAnswered)
    {
        return;
    }
    m_isAnswered = true;

    const bool isForAll = m_question.otherCount > 0 && isCheckBoxChecked();
    QPointer<PDFAppToolFileConflictDialog> guard(this);
    QDialog::done(answer == PDFAppToolFileSink::Answer::StopExport ? QDialog::Rejected : QDialog::Accepted);
    if (guard)
    {
        Q_EMIT answered(answer, isForAll);
    }
}

void PDFAppToolFileConflictDialog::reject()
{
    answer(PDFAppToolFileSink::Answer::StopExport);
}

void PDFAppToolFileConflictDialog::changeEvent(QEvent* event)
{
    BaseClass::changeEvent(event);
    if (event->type() == QEvent::LanguageChange)
    {
        updateTexts();
    }
}

void PDFAppToolFileConflictDialog::onStopClicked()
{
    answer(PDFAppToolFileSink::Answer::StopExport);
}

void PDFAppToolFileConflictDialog::onSkipClicked()
{
    answer(PDFAppToolFileSink::Answer::Skip);
}

void PDFAppToolFileConflictDialog::onRenameClicked()
{
    answer(PDFAppToolFileSink::Answer::Rename);
}

void PDFAppToolFileConflictDialog::onOverwriteClicked()
{
    answer(PDFAppToolFileSink::Answer::Overwrite);
}

}   // namespace pdf

#include "moc_pdfapptooloutputfiles.cpp"
