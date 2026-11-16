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

#include "pdfapptooltimestampauthoritypart.h"
#include "pdfappcontext.h"
#include "pdfapppagelayout.h"
#include "pdfapptoolpanelparts.h"
#include "pdfappworkspace.h"
#include "pdftoolparameterrepository.h"

#include "pdfdocumenttool_signature.h"
#include "pdffluentwidgets.h"
#include "pdfthemefonts.h"
#include "pdftimestampclient.h"

#include <QLineEdit>
#include <QVBoxLayout>

namespace pdf
{

PDFAppToolTimestampAuthorityPart::PDFAppToolTimestampAuthorityPart(PDFAppContext* context, QObject* parent) :
    BaseClass(parent),
    m_context(context)
{

}

PDFAppToolTimestampAuthorityPart::~PDFAppToolTimestampAuthorityPart()
{
    if (m_combo)
    {
        disconnect(m_combo.data(), nullptr, this, nullptr);
        if (QLineEdit* lineEdit = m_combo->lineEdit())
        {
            disconnect(lineEdit, nullptr, this, nullptr);
        }
    }
}

QWidget* PDFAppToolTimestampAuthorityPart::createContent(QWidget* parent)
{
    // A second call replaces the parts (the old content belongs to its parent)
    if (m_combo)
    {
        disconnect(m_combo.data(), nullptr, this, nullptr);
        if (QLineEdit* lineEdit = m_combo->lineEdit())
        {
            disconnect(lineEdit, nullptr, this, nullptr);
        }
    }

    PDFAppToolCard* card = new PDFAppToolCard(parent);
    card->setObjectName(QStringLiteral("timestampAuthorityCard"));
    card->getBodyLayout()->setContentsMargins(CARD_PADDING, CARD_PADDING, CARD_PADDING, CARD_PADDING);
    card->getBodyLayout()->setSpacing(CARD_GAP);

    // 434:6123 / 435:7178: the editable list of the addresses
    m_combo = new PDFComboBox(card);
    m_combo->setObjectName(QStringLiteral("timestampAuthorityCombo"));
    m_combo->setEditable(true);
    m_combo->setInsertPolicy(QComboBox::NoInsert);
    m_combo->setAccessibleName(tr("Address of the timestamp authority"));
    m_isUpdating = true;
    m_combo->addItems(getUrls());
    m_combo->setEditText(m_url);
    m_isUpdating = false;
    if (QLineEdit* lineEdit = m_combo->lineEdit())
    {
        lineEdit->setMaxLength(PDFTimestampClient::MAXIMUM_URL_LENGTH);
        connect(lineEdit, &QLineEdit::textEdited, this, &PDFAppToolTimestampAuthorityPart::onTextEdited);
    }
    connect(m_combo.data(), &QComboBox::textActivated, this, &PDFAppToolTimestampAuthorityPart::onTextEdited);
    card->getBodyLayout()->addWidget(m_combo);

    // The hint, or the error of the address in accent/red/text (435:7693)
    m_text = PDFAppPageWidgets::createText(QString(), PDFThemeFonts::getFont(QFont::Normal, 11), 13, PDFThemeColors::TextTertiary, card);
    m_text->setObjectName(QStringLiteral("timestampAuthorityText"));
    card->getBodyLayout()->addWidget(m_text);
    updateText();
    return card;
}

void PDFAppToolTimestampAuthorityPart::setHintText(const QString& text)
{
    m_hintText = text;
    updateText();
}

QString PDFAppToolTimestampAuthorityPart::getUrl() const
{
    return m_url;
}

void PDFAppToolTimestampAuthorityPart::setUrl(const QString& url)
{
    const QString trimmed = url.trimmed().left(PDFTimestampClient::MAXIMUM_URL_LENGTH);
    if (trimmed == m_url && (!m_combo || m_combo->currentText().trimmed() == trimmed))
    {
        return;
    }
    m_url = trimmed;
    if (m_combo && m_combo->currentText().trimmed() != trimmed)
    {
        m_isUpdating = true;
        m_combo->setEditText(trimmed);
        m_isUpdating = false;
    }
    updateText();
    Q_EMIT urlChanged(m_url);
}

bool PDFAppToolTimestampAuthorityPart::isUrlValid() const
{
    return PDFTimestampClient::validateUrl(m_url, nullptr);
}

QString PDFAppToolTimestampAuthorityPart::getErrorText() const
{
    QString errorText;
    if (!PDFTimestampClient::validateUrl(m_url, &errorText) && errorText.isEmpty())
    {
        errorText = tr("Address of the timestamp authority is not valid.");
    }
    return errorText;
}

QStringList PDFAppToolTimestampAuthorityPart::getUrls() const
{
    QStringList urls = getUserUrls(m_context.data());
    if (!m_url.isEmpty() && !urls.contains(m_url) && isUrlValid())
    {
        urls.append(m_url);
    }
    return urls;
}

QStringList PDFAppToolTimestampAuthorityPart::getUserUrls(PDFAppContext* context)
{
    QStringList urls = PDFTimestampClient::getPresetUrls();
    PDFDatabase* database = context ? context->getDatabase() : nullptr;
    if (!database)
    {
        return urls;
    }

    // The last values of tsaUrl of both tools; a stored value is untrusted input (S6.6): only a valid
    // address is offered (the repository gives the default for a value, which is not valid)
    PDFToolParameterRepository repository(database);
    for (const PDFDocumentToolSignature::Mode mode : { PDFDocumentToolSignature::Mode::Sign, PDFDocumentToolSignature::Mode::Timestamp })
    {
        const PDFDocumentToolSignature tool(mode);
        const QVariantMap values = repository.getValues(tool.getId(), tool.getParameters());
        const QString url = values.value(QString::fromLatin1(PDFDocumentToolSignature::PARAMETER_TSA_URL)).toString().trimmed();
        if (!url.isEmpty() && !urls.contains(url) && PDFTimestampClient::validateUrl(url, nullptr))
        {
            urls.append(url);
        }
    }
    return urls;
}

void PDFAppToolTimestampAuthorityPart::updateText()
{
    if (!m_text)
    {
        return;
    }

    const bool isValid = isUrlValid();
    m_text->setText(isValid ? m_hintText : getErrorText());
    m_text->setTextColor(isValid ? PDFThemeColors::TextTertiary : PDFThemeColors::TextDanger);
    if (m_combo)
    {
        m_combo->setProperty("error", !isValid);
        m_combo->setAccessibleDescription(isValid ? m_hintText : getErrorText());
        m_combo->update();
    }
}

void PDFAppToolTimestampAuthorityPart::onTextEdited(const QString& text)
{
    if (m_isUpdating)
    {
        return;
    }
    const QString trimmed = text.trimmed().left(PDFTimestampClient::MAXIMUM_URL_LENGTH);
    if (trimmed == m_url)
    {
        return;
    }
    m_url = trimmed;
    updateText();
    Q_EMIT urlChanged(m_url);
}

}   // namespace pdf

#include "moc_pdfapptooltimestampauthoritypart.cpp"
