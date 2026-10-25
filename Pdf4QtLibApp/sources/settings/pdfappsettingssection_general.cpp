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

#include "pdfappsettingssection_general.h"
#include "pdfapppagelayout.h"
#include "pdfappsettingrow.h"
#include "pdfapptranslator.h"
#include "pdfdatabasenotifier.h"
#include "pdfsettingdefinitions.h"
#include "pdfsettingsrepository.h"
#include "pdftoolrepository.h"

#include "pdfcomponentpanels.h"
#include "pdfwidgetstyleproperties.h"

#include <QEvent>
#include <QFrame>
#include <QPushButton>
#include <QVBoxLayout>

#include <limits>

namespace pdf
{

/// Texts shared by the row and the dialog
class PDFAppGeneralSettingsSectionHelper
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFAppGeneralSettingsSection)

public:
    PDFAppGeneralSettingsSectionHelper() = delete;

    /// Number for "%Ln" (a hostile count of another version is bounded)
    static int toPluralCount(qint64 runs)
    {
        return int(qBound(qint64(0), runs, qint64(std::numeric_limits<int>::max())));
    }

    static QString getDialogText(qint64 runs)
    {
        if (runs == 1)
        {
            return tr("PDF4QT forgets which tools you ran after each other (1 run). “Then continue with” offers only the default suggestions until new runs are recorded.");
        }
        return tr("PDF4QT forgets which tools you ran after each other (%Ln runs). “Then continue with” offers only the default suggestions until new runs are recorded.",
                  nullptr, toPluralCount(runs));
    }

    static QString getDeletedText(qint64 runs)
    {
        if (runs == 1)
        {
            return tr("1 recorded run");
        }
        return tr("%Ln recorded runs", nullptr, toPluralCount(runs));
    }
};

PDFAppClearToolHistoryDialog::PDFAppClearToolHistoryDialog(PDFAppContext* context, QWidget* parent) :
    BaseClass(PDFThemeIcons::Badge::Warning, true, tr("Clear the tool history?"), QString(), CARD_WIDTH, parent),
    m_context(context)
{
    setObjectName(QStringLiteral("clearToolHistoryDialog"));

    // facts 941:136888 …: bg/subtle, radius 10, paddings 4 / 12, Key value rows
    m_factsFrame = new QFrame(this);
    m_factsFrame->setObjectName(QStringLiteral("facts"));
    PDFWidgetStyleProperties::set(m_factsFrame, PDFWidgetStyleProperties::NOTE);
    QVBoxLayout* factsLayout = new QVBoxLayout(m_factsFrame);
    factsLayout->setContentsMargins(12, 4, 12, 4);
    factsLayout->setSpacing(0);
    m_facts = new PDFKeyValueList(m_factsFrame);
    m_facts->setObjectName(QStringLiteral("factsList"));
    factsLayout->addWidget(m_facts);
    addContent(m_factsFrame);

    // Clear Tool History (Secondary) at the leading edge, Cancel (Primary)
    m_clearButton = addActionButton(tr("Clear Tool History"), false, true);
    m_clearButton->setObjectName(QStringLiteral("clearButton"));
    m_cancelButton = addActionButton(tr("Cancel"), true, false);
    m_cancelButton->setObjectName(QStringLiteral("cancelButton"));
    connect(m_clearButton.data(), &QPushButton::clicked, this, &PDFAppClearToolHistoryDialog::accept);
    connect(m_cancelButton.data(), &QPushButton::clicked, this, &PDFAppClearToolHistoryDialog::reject);

    if (context && context->getNotifier())
    {
        connect(context->getNotifier(), &PDFDatabaseNotifier::changed, this, &PDFAppClearToolHistoryDialog::onDatabaseChanged);
        connect(context->getNotifier(), &PDFDatabaseNotifier::everythingChanged, this, &PDFAppClearToolHistoryDialog::updateTexts);
    }

    updateTexts();
}

PDFAppClearToolHistoryDialog::~PDFAppClearToolHistoryDialog() = default;

void PDFAppClearToolHistoryDialog::updateTexts()
{
    m_runCount = (m_context && m_context->getDatabase()) ? m_context->getTools().getHistoryRuns() : 0;

    setTitle(tr("Clear the tool history?"));
    setText(PDFAppGeneralSettingsSectionHelper::getDialogText(m_runCount));

    if (m_facts)
    {
        m_facts->clear();
        m_facts->addRow(tr("Deleted"), PDFAppGeneralSettingsSectionHelper::getDeletedText(m_runCount));
        m_facts->addRow(tr("Kept"), tr("Pinned tools, collections, list of recently used tools"));
    }

    if (m_clearButton)
    {
        m_clearButton->setText(tr("Clear Tool History"));
        m_clearButton->setEnabled(m_runCount > 0);
    }
    if (m_cancelButton)
    {
        m_cancelButton->setText(tr("Cancel"));
    }
}

void PDFAppClearToolHistoryDialog::changeEvent(QEvent* event)
{
    BaseClass::changeEvent(event);
    if (event->type() == QEvent::LanguageChange)
    {
        updateTexts();
    }
}

void PDFAppClearToolHistoryDialog::onDatabaseChanged(const QString& topic, const QString& key)
{
    // Runs recorded or cleared by another instance
    if (topic == PDFToolRepository::getPairTopic() || (topic == PDFSettingsRepository::getTopic() && key == PDFSettingKeys::toolsHistoryRuns()))
    {
        updateTexts();
    }
}

PDFAppGeneralSettingsSection::PDFAppGeneralSettingsSection(PDFAppContext* context, const QString& sectionId, QWidget* parent) :
    BaseClass(context, sectionId, parent)
{

}

PDFAppGeneralSettingsSection::~PDFAppGeneralSettingsSection()
{
    // The dialog belongs to the window: it goes with the section (a page replaced
    // or built again), its answer is not delivered to a dying section. It is hidden
    // at once and deleted later: the section can be deleted while
    // QEvent::LanguageChange is sent to a copy of the list of the children of the
    // window, of which the dialog is one (its scrim is deleted later by the box).
    if (m_clearHistoryDialog)
    {
        PDFAppClearToolHistoryDialog* dialog = m_clearHistoryDialog;
        m_clearHistoryDialog = nullptr;
        dialog->disconnect(this);
        dialog->hide();
        dialog->deleteLater();
    }
}

QString PDFAppGeneralSettingsSection::getToolHistoryDescription()
{
    return tr("Counts of tools run one after another, used for the suggestions");
}

QString PDFAppGeneralSettingsSection::getRunCountText(qint64 runs)
{
    if (runs <= 0)
    {
        return tr("no runs recorded");
    }
    if (runs == 1)
    {
        return tr("1 run recorded");
    }
    return tr("%Ln runs recorded", nullptr, PDFAppGeneralSettingsSectionHelper::toPluralCount(runs));
}

bool PDFAppGeneralSettingsSection::openClearHistoryDialog()
{
    if (m_clearHistoryDialog)
    {
        m_clearHistoryDialog->raise();
        return true;
    }

    PDFAppContext* context = getContext();
    if (!context || !context->getDatabase() || context->getTools().getHistoryRuns() <= 0)
    {
        return false;
    }

    PDFAppClearToolHistoryDialog* dialog = new PDFAppClearToolHistoryDialog(context, window());
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    connect(dialog, &QDialog::finished, this, &PDFAppGeneralSettingsSection::onClearHistoryDialogFinished);
    m_clearHistoryDialog = dialog;
    dialog->open();
    return true;
}

void PDFAppGeneralSettingsSection::refresh(const QString& topic, const QString& key)
{
    BaseClass::refresh(topic, key);

    const bool isEverything = topic.isEmpty();
    const bool isSetting = topic == PDFSettingsRepository::getTopic();
    if (isEverything || topic == PDFToolRepository::getPairTopic() || (isSetting && key == PDFSettingKeys::toolsHistoryRuns()))
    {
        updateToolHistory();
    }
    if (isEverything || (isSetting && key == PDFSettingKeys::appearanceLanguage()))
    {
        updateLanguageRow();
    }
}

void PDFAppGeneralSettingsSection::finishContent()
{
    // Tool history 505:5219: the number of the recorded runs and Clear history…
    // (a Secondary button with the icon trash) below Suggest tools I often use next
    PDFAppSettingGroup* group = findGroup(QStringLiteral("Tools"));
    if (!group)
    {
        group = addGroup(QStringLiteral("Tools"), tr("Tools"), QString());
    }

    m_toolHistoryRow = new PDFAppSettingRow(tr("Tool history"), getToolHistoryDescription(), getToolHistoryAnchor());
    m_toolHistoryRow->setSearchKeywords({ QStringLiteral("tool history"), QStringLiteral("clear history"), QStringLiteral("suggestions"),
                                          QStringLiteral("then continue with"), QStringLiteral("runs") });
    m_clearHistoryButton = PDFAppPageWidgets::createButton(tr("Clear history…"), QStringLiteral("trash"), false, nullptr);
    m_clearHistoryButton->setObjectName(QStringLiteral("clearToolHistoryButton"));
    m_clearHistoryButton->setAccessibleName(tr("Clear the tool history"));
    connect(m_clearHistoryButton.data(), &QPushButton::clicked, this, &PDFAppGeneralSettingsSection::onClearHistoryClicked);
    m_toolHistoryRow->setControlWidget(m_clearHistoryButton);
    group->addRow(m_toolHistoryRow);

    // Tab goes from Suggest tools to Clear history… (the row is created after the
    // generic rows, so its button is last in the focus chain otherwise)
    PDFAppSettingRow* suggestRow = findRow(QStringLiteral("suggest-next-tools"));
    if (suggestRow && suggestRow->getControl() && suggestRow->getControl()->getEditor())
    {
        QWidget::setTabOrder(suggestRow->getControl()->getEditor(), m_clearHistoryButton);
    }

    if (PDFAppContext* context = getContext(); context && context->getTranslator())
    {
        connect(context->getTranslator(), &PDFAppTranslator::languageChanged, this, &PDFAppGeneralSettingsSection::updateLanguageRow);
    }

    updateToolHistory();
    updateLanguageRow();
}

void PDFAppGeneralSettingsSection::updateToolHistory()
{
    PDFAppContext* context = getContext();
    m_shownRunCount = (context && context->getDatabase()) ? context->getTools().getHistoryRuns() : 0;

    if (m_toolHistoryRow)
    {
        // 505:5219 "… · 37 runs recorded", 955:143792 "… · no runs recorded"
        m_toolHistoryRow->setDescription(tr("%1 · %2").arg(getToolHistoryDescription(), getRunCountText(m_shownRunCount)));
    }
    if (m_clearHistoryButton)
    {
        m_clearHistoryButton->setEnabled(m_shownRunCount > 0);
    }
}

void PDFAppGeneralSettingsSection::updateLanguageRow()
{
    PDFAppSettingRow* row = findRow(QStringLiteral("language"));
    if (!row || !row->getDefinition())
    {
        return;
    }

    // A translation, which could not be loaded, is said in the row (English is shown)
    const QString description = row->getDefinition()->getDescription();
    PDFAppContext* context = getContext();
    PDFAppTranslator* translator = context ? context->getTranslator() : nullptr;
    const QString text = (translator && translator->hasError()) ? tr("%1 · %2").arg(description, translator->getErrorMessage()) : description;
    row->setDescription(text);
    if (PDFAppSettingControl* control = row->getControl(); control && control->getEditor())
    {
        control->getEditor()->setAccessibleDescription(text);
    }
}

void PDFAppGeneralSettingsSection::onClearHistoryClicked()
{
    openClearHistoryDialog();
}

void PDFAppGeneralSettingsSection::onClearHistoryDialogFinished(int result)
{
    m_clearHistoryDialog = nullptr;

    PDFAppContext* context = getContext();
    if (result != QDialog::Accepted || !context || !context->getDatabase())
    {
        return;
    }

    // The row follows the notification of the change (also of this connection)
    const PDFDatabaseError error = context->getTools().clearHistory();
    if (error.isError())
    {
        Q_EMIT statusMessageRequested(tr("The tool history could not be cleared: %1").arg(error.toString()));
    }
}

PDFAppSettingsSection* PDFAppGeneralSettingsSectionFactory::createSection(PDFAppContext* context, const QString& sectionId, QWidget* parent) const
{
    return new PDFAppGeneralSettingsSection(context, sectionId, parent);
}

}   // namespace pdf
