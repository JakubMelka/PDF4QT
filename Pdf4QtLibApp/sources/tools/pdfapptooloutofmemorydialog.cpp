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

#include "pdfapptooloutofmemorydialog.h"

#include "pdfappcontext.h"
#include "pdfapppagelayout.h"
#include "pdfappvaluestore.h"
#include "pdfappworkspace.h"
#include "pdfdatabasenotifier.h"
#include "pdfsettingdefinitions.h"
#include "pdfsettingsrepository.h"
#include "pdfthemefonts.h"

#include <QEvent>
#include <QPushButton>

namespace pdf
{

/// Helpers of the dialog
class PDFAppToolOutOfMemoryDialogHelper
{
public:
    PDFAppToolOutOfMemoryDialogHelper() = delete;

    /// Value of the setting of one thread (the option of Settings › Advanced)
    static QString getSingleThreadValue() { return QStringLiteral("singleThread"); }

    /// Width of the card: as the other questions of the tools (the text of 955:145813)
    static constexpr int CARD_WIDTH = 440;
};

PDFAppToolOutOfMemoryDialog::PDFAppToolOutOfMemoryDialog(PDFAppContext* context, QWidget* parent) :
    BaseClass(PDFThemeIcons::Badge::Warning, true, QString(), QString(), PDFAppToolOutOfMemoryDialogHelper::CARD_WIDTH, parent),
    m_context(context)
{
    setObjectName(QStringLiteral("toolOutOfMemoryDialog"));

    m_errorLabel = PDFAppPageWidgets::createText(QString(), PDFThemeFonts::getFont(QFont::Normal, 12), 15, PDFThemeColors::TextDanger, this);
    m_errorLabel->setObjectName(QStringLiteral("toolOutOfMemoryError"));
    m_errorLabel->hide();
    addContent(m_errorLabel);

    // The order of the question Stop and Leave (955:145813): the Secondary button, then the
    // Primary one at the trailing edge
    m_closeButton = addActionButton(QString(), false, false);
    m_closeButton->setObjectName(QStringLiteral("toolOutOfMemoryClose"));
    m_oneThreadButton = addActionButton(QString(), true, false);
    m_oneThreadButton->setObjectName(QStringLiteral("toolOutOfMemoryOneThread"));
    connect(m_closeButton.data(), &QPushButton::clicked, this, &PDFAppToolOutOfMemoryDialog::reject);
    connect(m_oneThreadButton.data(), &QPushButton::clicked, this, &PDFAppToolOutOfMemoryDialog::onOneThreadClicked);

    if (const PDFDatabaseNotifier* notifier = m_context ? m_context->getNotifier() : nullptr)
    {
        connect(notifier, &PDFDatabaseNotifier::changed, this, &PDFAppToolOutOfMemoryDialog::onSettingChanged);
        connect(notifier, &PDFDatabaseNotifier::everythingChanged, this, &PDFAppToolOutOfMemoryDialog::updateTexts);
    }

    updateTexts();
}

PDFAppToolOutOfMemoryDialog::~PDFAppToolOutOfMemoryDialog() = default;

bool PDFAppToolOutOfMemoryDialog::isOneThreadOffered(const PDFAppContext* context)
{
    if (!context || !context->getDatabase())
    {
        return false;
    }
    return context->getSettings().getString(PDFSettingKeys::renderingMultithreading()) != PDFAppToolOutOfMemoryDialogHelper::getSingleThreadValue();
}

void PDFAppToolOutOfMemoryDialog::updateTexts()
{
    const bool isOffered = isOneThreadOffered(m_context);

    setTitle(tr("Not enough memory"));
    setText(isOffered ? tr("Nothing was changed. The tool works on several images at once and the computer ran out of memory. With one thread it is slower, but it holds one image at a time. The setting also slows down the drawing of pages until you change it back in Settings › Advanced.")
                      : tr("There is not enough memory for this tool. Nothing was changed."));

    if (m_closeButton)
    {
        m_closeButton->setText(tr("Close"));
    }
    if (m_oneThreadButton)
    {
        m_oneThreadButton->setText(tr("Use One Thread"));
        m_oneThreadButton->setVisible(isOffered);
    }

    // Enter works in both forms: the default button of the dialog is the one it shows
    QPushButton* defaultButton = isOffered ? m_oneThreadButton.data() : m_closeButton.data();
    if (defaultButton)
    {
        defaultButton->setDefault(true);
    }

    if (m_errorLabel)
    {
        // A refused write is of no interest, when the setting has been switched meanwhile
        const bool hasError = isOffered && !m_writeError.isEmpty();
        m_errorLabel->setText(hasError ? tr("The setting was not changed: %1").arg(m_writeError) : QString());
        m_errorLabel->setVisible(hasError);
    }
}

void PDFAppToolOutOfMemoryDialog::onOneThreadClicked()
{
    // The same path as the row of Settings: the notifier tells the documents and the Settings page
    // from the event loop, other instances read the change log. The database announces a new
    // state of its own inside of a write, a slot of it may delete this dialog.
    QPointer<PDFAppToolOutOfMemoryDialog> guard(this);
    PDFAppSettingsValueStore store(m_context);
    if (!store.isAvailable())
    {
        return;
    }

    const QString error = store.writeValue(PDFSettingKeys::renderingMultithreading(), PDFAppToolOutOfMemoryDialogHelper::getSingleThreadValue());
    if (!guard)
    {
        return;
    }

    if (!error.isEmpty())
    {
        m_writeError = error;
        updateTexts();
        return;
    }

    // Last: the slots of the end of the dialog may delete anything
    m_writeError.clear();
    accept();
}

void PDFAppToolOutOfMemoryDialog::onSettingChanged(const QString& topic, const QString& key)
{
    if (topic == PDFSettingsRepository::getTopic() && (key.isEmpty() || key == PDFSettingKeys::renderingMultithreading()))
    {
        updateTexts();
    }
}

void PDFAppToolOutOfMemoryDialog::changeEvent(QEvent* event)
{
    if (event->type() == QEvent::LanguageChange)
    {
        updateTexts();
    }
    BaseClass::changeEvent(event);
}

}   // namespace pdf

#include "moc_pdfapptooloutofmemorydialog.cpp"
