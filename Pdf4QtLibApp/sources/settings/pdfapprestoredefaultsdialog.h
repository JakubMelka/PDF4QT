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

#ifndef PDFAPPRESTOREDEFAULTSDIALOG_H
#define PDFAPPRESTOREDEFAULTSDIALOG_H

#include "pdfappglobal.h"
#include "pdfapphomedialogs.h"
#include "pdfappsettingssection.h"

#include <QFrame>
#include <QPointer>
#include <QPushButton>

#include "pdfcomponentpanels.h"

namespace pdf
{

class PDFKeyValueList;

/// Restore the defaults of a section? (941:135918): the warning badge, the text of
/// the section with the count, one key value row per changed setting ("Magnifier
/// zoom: 3.0 × → 2.0 ×"), Restore Defaults (Secondary, leading) and Cancel (Primary,
/// the default). The rows follow the changes of the database (another instance)
/// while the dialog is open; without a changed setting Restore Defaults is disabled.
/// The dialog only answers: the page restores the defaults, when it is accepted.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppRestoreDefaultsDialog : public PDFAppCardDialog
{
    Q_OBJECT

private:
    using BaseClass = PDFAppCardDialog;

public:
    /// Width of the card of 941:136108
    static constexpr int CARD_WIDTH = 520;

    /// Most rows shown (the other ones are summarized in the last row)
    static constexpr int MAXIMUM_ROWS = 12;

    explicit PDFAppRestoreDefaultsDialog(PDFAppSettingsSection* section, QWidget* parent);
    virtual ~PDFAppRestoreDefaultsDialog() override;

    PDFAppSettingsSection* getSection() const { return m_section; }
    PDFKeyValueList* getFacts() const { return m_facts; }
    QPushButton* getRestoreButton() const { return m_restoreButton; }
    QPushButton* getCancelButton() const { return m_cancelButton; }

    /// Number of the changed settings shown
    int getChangeCount() const { return m_changeCount; }

    /// Reads the changed values of the section again
    void updateRows();

private:
    void onDatabaseChanged(const QString& topic, const QString& key);
    void onSectionDestroyed();

    QPointer<PDFAppSettingsSection> m_section;
    QPointer<QFrame> m_factsFrame;
    QPointer<PDFKeyValueList> m_facts;
    QPointer<QPushButton> m_restoreButton;
    QPointer<QPushButton> m_cancelButton;
    int m_changeCount = 0;
};

}   // namespace pdf

#endif // PDFAPPRESTOREDEFAULTSDIALOG_H
