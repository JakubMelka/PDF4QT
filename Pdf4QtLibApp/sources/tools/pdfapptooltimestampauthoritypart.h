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

#ifndef PDFAPPTOOLTIMESTAMPAUTHORITYPART_H
#define PDFAPPTOOLTIMESTAMPAUTHORITYPART_H

#include "pdfappglobal.h"

#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>

class QWidget;

namespace pdf
{

class PDFAppContext;
class PDFAppParagraph;
class PDFComboBox;

/// The timestamp authority of Sign with Certificate (the left panel, 434:5884, 434:6241) and of Add
/// Timestamp (the right panel, 435:7037 ... 435:7693) - one part (plan of step 6 of phase 2, 4.5, 6.6,
/// K6.20, Q6.6; package P4 implements it): an editable list of the presets (PDFTimestampClient::
/// getPresetUrls, the default first) and the addresses the user has used, the validation of the typed
/// address (PDFTimestampClient::validateUrl - the error of the field 435:7693, the confirmation
/// Disabled). Nothing contacts the authority here (no request at the opening of the page, the choice or
/// the validation, S6.6). The address is no secret: it is the parameter tsaUrl of the run.
///
/// The address the user has used is the last value of the parameter tsaUrl of protect.sign and of
/// protect.timestamp (PDFToolParameterRepository, written when a result is accepted; not secret, not
/// sensitive): no table or setting of its own. A stored address is untrusted input and is validated
/// again when it is read. getUserUrls is the list, from which an assistant may choose (MCP accepts only
/// an address of this list, 6.5 - package P6).
///
/// Ownership: a child object of the GUI; its widgets belong to their parents, the part holds them by
/// QPointer. Q_EMIT is the last call of a function.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolTimestampAuthorityPart : public QObject
{
    Q_OBJECT

private:
    using BaseClass = QObject;

public:
    /// card 434:6116 / 435:7176: padding 12 + the border, the field and its text 10 apart
    static constexpr int CARD_PADDING = 13;
    static constexpr int CARD_GAP = 10;

    explicit PDFAppToolTimestampAuthorityPart(PDFAppContext* context, QObject* parent);
    virtual ~PDFAppToolTimestampAuthorityPart() override;

    /// The section Timestamp authority (its caption, the editable list, the error of the address), a
    /// new widget of the parent. The caption is the GUI's (Sign 434:5884 "Timestamp authority", Add
    /// Timestamp 435:7176 "Address"); the part gives the card.
    QWidget* createContent(QWidget* parent);

    /// The text below the field, while the address is valid (Sign 434:6123, Add Timestamp 435:7180)
    void setHintText(const QString& text);

    /// The address of the field (trimmed)
    QString getUrl() const;

    /// Puts an address into the field (the last used, the value of the parameter); emits urlChanged
    void setUrl(const QString& url);

    /// The address of the field is valid (PDFTimestampClient::validateUrl)
    bool isUrlValid() const;

    /// The error of the field, empty = none
    QString getErrorText() const;

    /// The presets and the addresses the user has used, in the order of the list, without duplicates
    QStringList getUrls() const;

    /// The addresses an assistant may choose (MCP, 6.5): the presets and the valid last values of
    /// tsaUrl of Sign with Certificate and Add Timestamp
    /// \param context The context of the application (null = the presets only)
    static QStringList getUserUrls(PDFAppContext* context);

    /// Parts (tests; nullptr = none)
    PDFComboBox* getCombo() const { return m_combo.data(); }
    PDFAppParagraph* getTextLabel() const { return m_text.data(); }

Q_SIGNALS:
    /// The address of the field has changed (also its validity)
    void urlChanged(const QString& url);

private:
    void updateText();
    void onTextEdited(const QString& text);

    QPointer<PDFAppContext> m_context;
    QPointer<PDFComboBox> m_combo;
    QPointer<PDFAppParagraph> m_text;
    QString m_hintText;
    QString m_url;
    bool m_isUpdating = false;
};

}   // namespace pdf

#endif // PDFAPPTOOLTIMESTAMPAUTHORITYPART_H
