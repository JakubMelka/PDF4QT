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

#include "pdfapprestoredefaultsdialog.h"
#include "pdfappcontext.h"
#include "pdfappsettingssection.h"
#include "pdfdatabasenotifier.h"

#include "pdfcomponentpanels.h"
#include "pdfwidgetstyleproperties.h"

#include <QFrame>
#include <QPushButton>
#include <QVBoxLayout>

namespace pdf
{

PDFAppRestoreDefaultsDialog::PDFAppRestoreDefaultsDialog(PDFAppSettingsSection* section, QWidget* parent) :
    BaseClass(PDFThemeIcons::Badge::Warning, true, tr("Restore the defaults of %1?").arg(section ? section->getTitle() : QString()), QString(), CARD_WIDTH, parent),
    m_section(section)
{
    setObjectName(QStringLiteral("restoreDefaultsDialog"));

    // facts 941:136116: bg/subtle, radius 10, paddings 4 / 12, Key value rows
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

    // actions 941:136129: Restore Defaults (Secondary) at the leading edge, Cancel (Primary)
    m_restoreButton = addActionButton(tr("Restore Defaults"), false, true);
    m_restoreButton->setObjectName(QStringLiteral("restoreButton"));
    m_cancelButton = addActionButton(tr("Cancel"), true, false);
    m_cancelButton->setObjectName(QStringLiteral("cancelButton"));
    connect(m_restoreButton.data(), &QPushButton::clicked, this, &PDFAppRestoreDefaultsDialog::accept);
    connect(m_cancelButton.data(), &QPushButton::clicked, this, &PDFAppRestoreDefaultsDialog::reject);

    if (section)
    {
        connect(section, &QObject::destroyed, this, &PDFAppRestoreDefaultsDialog::onSectionDestroyed);
        if (PDFAppContext* context = section->getContext(); context && context->getNotifier())
        {
            connect(context->getNotifier(), &PDFDatabaseNotifier::changed, this, &PDFAppRestoreDefaultsDialog::onDatabaseChanged);
            connect(context->getNotifier(), &PDFDatabaseNotifier::everythingChanged, this, &PDFAppRestoreDefaultsDialog::updateRows);
        }
    }

    updateRows();
}

PDFAppRestoreDefaultsDialog::~PDFAppRestoreDefaultsDialog() = default;

void PDFAppRestoreDefaultsDialog::updateRows()
{
    std::vector<PDFAppSettingChange> changes;
    QString text;
    if (m_section)
    {
        changes = m_section->getChangedValues();
        text = m_section->getRestoreDefaultsText(int(changes.size()));
    }

    m_changeCount = int(changes.size());
    setText(text);

    if (m_facts)
    {
        m_facts->clear();
        for (size_t i = 0; i < changes.size(); ++i)
        {
            if (i + 1 == MAXIMUM_ROWS && changes.size() > size_t(MAXIMUM_ROWS))
            {
                m_facts->addRow(tr("Other settings"), tr("%Ln more", nullptr, int(changes.size() - i)));
                break;
            }
            m_facts->addRow(changes[i].title, tr("%1 → %2").arg(changes[i].currentValue, changes[i].defaultValue));
        }
    }

    if (m_factsFrame)
    {
        m_factsFrame->setHidden(changes.empty());
    }
    if (m_restoreButton)
    {
        m_restoreButton->setEnabled(!changes.empty());
    }
}

void PDFAppRestoreDefaultsDialog::onDatabaseChanged(const QString& topic, const QString& key)
{
    Q_UNUSED(key);

    // A change of another instance (or of a binder): the rows follow it (only the
    // topics of the section can change them - Recent, history and the thumbnails
    // write often)
    if (m_section && !m_section->isRestoreTopic(topic))
    {
        return;
    }
    updateRows();
}

void PDFAppRestoreDefaultsDialog::onSectionDestroyed()
{
    // The page of the section is gone: there is nothing to restore (the sender is
    // not called, it is being destroyed). The dialog is closed from the event loop:
    // the section can be deleted while QEvent::LanguageChange is sent to a copy of
    // the list of the children of the window (the page is built again), of which
    // the dialog - deleted on close - is one.
    m_section = nullptr;
    QMetaObject::invokeMethod(this, &PDFAppRestoreDefaultsDialog::reject, Qt::QueuedConnection);
}

}   // namespace pdf
