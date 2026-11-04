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

#ifndef PDFAPPTOOLOUTOFMEMORYDIALOG_H
#define PDFAPPTOOLOUTOFMEMORYDIALOG_H

#include "pdfappglobal.h"
#include "pdfapphomedialogs.h"

#include <QPointer>
#include <QString>

class QEvent;
class QPushButton;

namespace pdf
{

class PDFAppContext;
class PDFAppParagraph;

/// A tool has run out of memory (owner decision of 2026-11-04; the design has no frame, the
/// shape is the one of the dialogs of the tools): the user chooses the strategy of the threads,
/// the tool does not report what it left out. Close (Secondary) and Use One Thread (Primary),
/// which writes the setting of the row of Settings › Advanced and closes the dialog by accept -
/// the tool is not run again. A setting, which is single thread already, offers nothing: the
/// dialog says only that there is not enough memory. It follows the setting while it is open
/// (another instance, the other way around too). A refused write is said in the dialog, which
/// stays open. The shell of the tools owns the pointer, the window owns the dialog.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolOutOfMemoryDialog : public PDFAppCardDialog
{
    Q_OBJECT

private:
    using BaseClass = PDFAppCardDialog;

public:
    /// \param context Context of the application (the settings)
    /// \param parent The window
    explicit PDFAppToolOutOfMemoryDialog(PDFAppContext* context, QWidget* parent);
    virtual ~PDFAppToolOutOfMemoryDialog() override;

    /// The setting can be switched to one thread: it is not single thread now. The setting
    /// and not the strategy of the process decides - it is what the button writes, and the
    /// documents apply it to the process from their own slots of the same notification.
    static bool isOneThreadOffered(const PDFAppContext* context);

    QPushButton* getCloseButton() const { return m_closeButton.data(); }
    QPushButton* getOneThreadButton() const { return m_oneThreadButton.data(); }

    /// The line of a refused write of the setting (hidden without an error)
    PDFAppParagraph* getErrorLabel() const { return m_errorLabel.data(); }

protected:
    virtual void changeEvent(QEvent* event) override;

private:
    void updateTexts();
    void onOneThreadClicked();
    void onSettingChanged(const QString& topic, const QString& key);

    QPointer<PDFAppContext> m_context;
    QPointer<QPushButton> m_closeButton;
    QPointer<QPushButton> m_oneThreadButton;
    QPointer<PDFAppParagraph> m_errorLabel;
    QString m_writeError;
};

}   // namespace pdf

#endif // PDFAPPTOOLOUTOFMEMORYDIALOG_H
