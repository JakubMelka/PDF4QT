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

#include "pdfappcertificatedetails.h"
#include "pdfapplocale.h"
#include "pdfapppagelayout.h"
#include "pdfapptoolpanelparts.h"

#include "pdfcertificatestore.h"
#include "pdfcomponentpanels.h"
#include "pdfcomponentrows.h"
#include "pdfdeferreddeletion.h"
#include "pdffluentwidgets.h"
#include "pdfthemefonts.h"

#include <QFileInfo>
#include <QVBoxLayout>

namespace pdf
{

/// File-local constants and helpers of the detail of a certificate
class PDFAppCertificateDetailsHelper
{
public:
    PDFAppCertificateDetailsHelper() = delete;

    /// sidebar-right 436:9432: the sections 14 apart; a section: the header and its card 8 apart
    static constexpr int SECTION_SPACING = 14;
    static constexpr int SECTION_GAP = 8;

    /// card of section-Subject 436:9590: the Key value rows of 28 are 8 apart
    static constexpr int ROW_GAP = 8;

    /// card 436:9631: padding 6 + the border, the rows 2 apart
    static constexpr int USAGE_PADDING = 7;
    static constexpr int USAGE_SPACING = 2;

    /// The largest DER of a certificate read by the walker (as the trusted certificates)
    static constexpr qsizetype MAXIMUM_DER_SIZE = PDFTrustedCertificateRepository::MAXIMUM_CERTIFICATE_SIZE;

    /// One element of DER (X.690 8.1): the tag, its content [contentStart, end)
    struct Element
    {
        quint8 tag = 0;
        qsizetype contentStart = 0;
        qsizetype end = 0;
    };

    /// Reads the element at \p position, which must end by \p limit (definite lengths of at most
    /// four bytes; anything else is no certificate); false for damaged data
    static bool readElement(const QByteArray& data, qsizetype position, qsizetype limit, Element* element)
    {
        if (position < 0 || limit > data.size() || position + 2 > limit)
        {
            return false;
        }

        qsizetype offset = position;
        element->tag = quint8(data[offset++]);
        const quint8 first = quint8(data[offset++]);
        qsizetype length = 0;
        if (first < 0x80)
        {
            length = first;
        }
        else
        {
            const int count = first & 0x7F;
            if (count == 0 || count > 4 || offset + count > limit)
            {
                return false;
            }
            for (int i = 0; i < count; ++i)
            {
                length = (length << 8) | qsizetype(quint8(data[offset++]));
            }
        }

        if (length < 0 || length > limit - offset)
        {
            return false;
        }
        element->contentStart = offset;
        element->end = offset + length;
        return true;
    }

    /// The fields of TBSCertificate (RFC 5280, 4.1): the serial number, the issuer and the subject
    struct TbsFields
    {
        QByteArray serialNumber;    ///< The content octets of the INTEGER
        QByteArray issuer;          ///< The whole encoded Name
        QByteArray subject;         ///< The whole encoded Name
    };

    /// The fields of a DER certificate; false for data, which are no certificate
    static bool readTbsFields(const QByteArray& data, TbsFields* fields)
    {
        constexpr quint8 SEQUENCE = 0x30;
        constexpr quint8 INTEGER = 0x02;
        constexpr quint8 VERSION = 0xA0;    // [0] EXPLICIT Version

        Element certificate;
        Element tbs;
        if (data.size() > MAXIMUM_DER_SIZE || !readElement(data, 0, data.size(), &certificate) || certificate.tag != SEQUENCE ||
            !readElement(data, certificate.contentStart, certificate.end, &tbs) || tbs.tag != SEQUENCE)
        {
            return false;
        }

        Element element;
        qsizetype position = tbs.contentStart;
        if (!readElement(data, position, tbs.end, &element))
        {
            return false;
        }
        if (element.tag == VERSION)
        {
            position = element.end;
            if (!readElement(data, position, tbs.end, &element))
            {
                return false;
            }
        }
        if (element.tag != INTEGER || element.end == element.contentStart)
        {
            return false;
        }
        fields->serialNumber = data.mid(element.contentStart, element.end - element.contentStart);

        // signature AlgorithmIdentifier, issuer Name, validity, subject Name
        QByteArray* names[] = { nullptr, &fields->issuer, nullptr, &fields->subject };
        position = element.end;
        for (QByteArray* name : names)
        {
            if (!readElement(data, position, tbs.end, &element) || element.tag != SEQUENCE)
            {
                return false;
            }
            if (name)
            {
                *name = data.mid(position, element.end - position);
            }
            position = element.end;
        }
        return true;
    }

    /// A date of the certificate in the language of the user interface (O17), "–" for none
    static QString formatDate(const QDateTime& dateTime)
    {
        return dateTime.isValid() ? PDFAppLocale::formatDate(dateTime.toLocalTime().date()) : PDFAppCertificateDetails::tr("–");
    }
};

PDFAppCertificateDetails::PDFAppCertificateDetails(QWidget* parent) :
    BaseClass(parent)
{
    using Helper = PDFAppCertificateDetailsHelper;

    setObjectName(QStringLiteral("certificateDetails"));
    m_layout = PDFAppPageWidgets::createColumn(this, Helper::SECTION_SPACING);

    // header 436:9586: the name Semi Bold 14 and the file Regular 11 text/tertiary
    m_header = new PDFAppToolPanelHeader(QString(), QString(), this);
    m_header->setObjectName(QStringLiteral("certificateDetailsHeader"));
    m_layout->addWidget(m_header);

    setCertificate(std::nullopt);
}

PDFAppCertificateDetails::~PDFAppCertificateDetails() = default;

void PDFAppCertificateDetails::setCertificate(const std::optional<PDFAppToolCertificateItem>& certificate)
{
    m_certificate = certificate;
    clearParts();

    if (!certificate)
    {
        // 436:9646: no selection
        setHeader(tr("Details"), tr("Select a certificate"));
        addNote(tr("The details of the selected certificate show here."));
        return;
    }

    const PDFAppToolCertificateItem& item = *certificate;
    const bool isSystem = item.source == PDFAppToolCertificateItem::Source::SystemStore;
    const QString fileName = isSystem ? QString() : QFileInfo(item.filePath).fileName();
    const QString title = PDFAppCertificateEntry::sanitizeText(item.getTitle());
    setHeader(title, isSystem ? tr("System store") : PDFAppCertificateEntry::sanitizeText(fileName));

    std::optional<PDFCertificateInfo> info = item.certificateData.isEmpty() ? std::nullopt : PDFCertificateInfo::getCertificateInfo(item.certificateData);
    if (!info)
    {
        // Q5.11: a .pfx file with a password never opened - its certificate is not known yet
        addNote(tr("The details are shown after the certificate is used with its password once (signing or encrypting a document)."));
        return;
    }

    // 436:11809: an expired certificate (or one, which is not valid yet)
    showValidityCallout(info->getNotValidBefore(), info->getNotValidAfter());

    // section-Subject 436:9590
    showSubject(*info);

    // section-Certificate 436:9609
    m_certificateValues = addValues(tr("Certificate"));
    m_certificateValues->setObjectName(QStringLiteral("certificateDetailsCertificate"));
    const QString issuer = isSelfIssued(item.certificateData) ? tr("Self-signed") : PDFAppCertificateEntry::sanitizeText(PDFCertificateInfo::getIssuerName(item.certificateData));
    m_certificateValues->addRow(tr("Issued by"), issuer, PDFComponentPainter::TextKind::Data);
    m_certificateValues->addRow(tr("Valid from"), PDFAppCertificateDetailsHelper::formatDate(info->getNotValidBefore()));
    m_certificateValues->addRow(tr("Valid to"), PDFAppCertificateDetailsHelper::formatDate(info->getNotValidAfter()));
    m_certificateValues->addRow(tr("Key"), getKeyText(*info));
    QString privateKey;
    if (isSystem)
    {
        // 436:9830: the key stays in the system (Q6.2)
        privateKey = tr("In the system");
    }
    else
    {
        privateKey = item.isPasswordProtected ? tr("Password") : tr("No password");
    }
    m_certificateValues->addRow(tr("Private key"), privateKey);

    // section-Usable for 436:9628
    QWidget* card = addSection(tr("Usable for"), nullptr);
    if (item.isUsableForSigning)
    {
        addUsageRow(card, tr("Digital signatures"), QStringLiteral("sign-certificate"));
    }
    if (item.isUsableForEncryption)
    {
        addUsageRow(card, tr("Certificate encryption"), QStringLiteral("certificate-lock"));
    }
    if (!item.isUsableForSigning && !item.isUsableForEncryption)
    {
        // No frame: the card of the section with the reason
        PDFAppParagraph* text = PDFAppPageWidgets::createText(tr("Neither signing nor encryption – the key usage of the certificate allows other purposes only."),
                                                             PDFThemeFonts::getFont(QFont::Normal, 12), 17, PDFThemeColors::TextSecondary, card);
        text->setObjectName(QStringLiteral("certificateDetailsNoUsage"));
        text->setMinimumWidth(1);
        text->setContentsMargins(6, 6, 6, 6);
        card->layout()->addWidget(text);
    }
}

void PDFAppCertificateDetails::setTrustedCertificate(const PDFAppCertificateEntry& entry)
{
    m_certificate.reset();
    clearParts();

    const bool isUser = entry.type == PDFAppCertificateEntry::Type::User;
    const QString subtitle = isUser ? tr("Trusted · added by you") : tr("Trusted · from the system store");
    setHeader(entry.name.isEmpty() ? tr("Certificate without a name") : entry.name, subtitle);

    std::optional<PDFCertificateInfo> info = entry.isDamaged ? std::nullopt : PDFCertificateInfo::getCertificateInfo(entry.data);
    if (!info)
    {
        // A damaged row of the table (Settings › Security lists it, so it can be removed)
        addNote(tr("The data of this row are not a certificate. Remove it."));
        return;
    }

    showValidityCallout(info->getNotValidBefore(), info->getNotValidAfter());
    showSubject(*info);

    // section-Certificate 436:12290: Valid from, Valid to, Key, Serial number
    m_certificateValues = addValues(tr("Certificate"));
    m_certificateValues->setObjectName(QStringLiteral("certificateDetailsCertificate"));
    if (!isSelfIssued(entry.data))
    {
        m_certificateValues->addRow(tr("Issued by"), PDFAppCertificateEntry::sanitizeText(PDFCertificateInfo::getIssuerName(entry.data)), PDFComponentPainter::TextKind::Data);
    }
    m_certificateValues->addRow(tr("Valid from"), PDFAppCertificateDetailsHelper::formatDate(info->getNotValidBefore()));
    m_certificateValues->addRow(tr("Valid to"), PDFAppCertificateDetailsHelper::formatDate(info->getNotValidAfter()));
    m_certificateValues->addRow(tr("Key"), getKeyText(*info));
    m_certificateValues->addRow(tr("Serial number"), getSerialNumberText(entry.data), PDFComponentPainter::TextKind::Data);

    // section-Key usage 436:12306 (RFC 5280, 4.2.1.3 and 4.2.1.12): no extension - any use
    QWidget* card = addSection(tr("Key usage"), nullptr);
    const quint32 flags = quint32(info->getKeyUsage().toInt());
    struct Usage
    {
        quint32 flag;
        const char* text;
        const char* iconName;
    };
    static constexpr Usage usages[] =
    {
        { PDFCertificateInfo::KeyUsageCertSign, QT_TR_NOOP("Verify signatures on certificates"), "chain" },
        { PDFCertificateInfo::KeyUsageCrlSign, QT_TR_NOOP("Verify revocation information"), "list" },
        { PDFCertificateInfo::KeyUsageDigitalSignature, QT_TR_NOOP("Digital signatures"), "sign-certificate" },
        { PDFCertificateInfo::KeyUsageNonRepudiation, QT_TR_NOOP("Non-repudiation of signed content"), "sign-certificate" },
        { PDFCertificateInfo::KeyUsageKeyEncipherment, QT_TR_NOOP("Certificate encryption"), "certificate-lock" },
        { PDFCertificateInfo::KeyUsageDataEncipherment, QT_TR_NOOP("Data encryption"), "certificate-lock" },
        { PDFCertificateInfo::KeyUsageAgreement, QT_TR_NOOP("Key agreement"), "key" },
        { PDFCertificateInfo::KeyUsageExtended_SMIME, QT_TR_NOOP("E-mail protection"), "mail" },
        { PDFCertificateInfo::KeyUsageExtended_TIMESTAMP, QT_TR_NOOP("Timestamps"), "clock" },
        { PDFCertificateInfo::KeyUsageExtended_CODE_SIGN, QT_TR_NOOP("Code signing"), "code" },
        { PDFCertificateInfo::KeyUsageExtended_SSL_SERVER, QT_TR_NOOP("Server authentication"), "globe" },
        { PDFCertificateInfo::KeyUsageExtended_SSL_CLIENT, QT_TR_NOOP("Client authentication"), "user" },
        { PDFCertificateInfo::KeyUsageExtended_OCSP_SIGN, QT_TR_NOOP("Status responses (OCSP)"), "list" }
    };
    bool hasUsage = false;
    for (const Usage& usage : usages)
    {
        if (flags & usage.flag)
        {
            addUsageRow(card, tr(usage.text), QString::fromLatin1(usage.iconName));
            hasUsage = true;
        }
    }
    if (!hasUsage)
    {
        addUsageRow(card, tr("Any use (no key usage is given)"), QStringLiteral("check"));
    }
}

std::vector<PDFTileRow*> PDFAppCertificateDetails::getUsageRows() const
{
    std::vector<PDFTileRow*> rows;
    if (m_usageCard)
    {
        for (PDFTileRow* row : m_usageCard->findChildren<PDFTileRow*>(QString(), Qt::FindDirectChildrenOnly))
        {
            rows.push_back(row);
        }
    }
    return rows;
}

QString PDFAppCertificateDetails::getValue(const PDFKeyValueList* list, const QString& key)
{
    for (int i = 0; list && i < list->getRowCount(); ++i)
    {
        if (list->getKey(i) == key)
        {
            return list->getValue(i);
        }
    }
    return QString();
}

QString PDFAppCertificateDetails::getSerialNumberText(const QByteArray& certificateData)
{
    PDFAppCertificateDetailsHelper::TbsFields fields;
    if (!PDFAppCertificateDetailsHelper::readTbsFields(certificateData, &fields))
    {
        return QString();
    }

    // X.690 8.3.2: a leading zero byte only keeps a positive number positive
    QByteArray serial = fields.serialNumber;
    if (serial.size() > 1 && serial.front() == '\0')
    {
        serial.remove(0, 1);
    }
    return QString::fromLatin1(serial.toHex(':').toUpper());
}

QString PDFAppCertificateDetails::getKeyText(const PDFCertificateInfo& info)
{
    QString algorithm;
    switch (info.getPublicKey())
    {
        case PDFCertificateInfo::KeyRSA:
            algorithm = QStringLiteral("RSA");
            break;
        case PDFCertificateInfo::KeyDSA:
            algorithm = QStringLiteral("DSA");
            break;
        case PDFCertificateInfo::KeyEC:
            algorithm = QStringLiteral("EC");
            break;
        case PDFCertificateInfo::KeyDH:
            algorithm = QStringLiteral("DH");
            break;
        case PDFCertificateInfo::KeyUnknown:
            return tr("Unknown");
    }
    // A number of bits has no group separator in "RSA 2048-bit" (436:9398)
    return tr("%1 %2-bit", "The key of a certificate: RSA 2048-bit").arg(algorithm, QString::number(info.getKeySize()));
}

bool PDFAppCertificateDetails::isSelfIssued(const QByteArray& certificateData)
{
    PDFAppCertificateDetailsHelper::TbsFields fields;
    return PDFAppCertificateDetailsHelper::readTbsFields(certificateData, &fields) && fields.issuer == fields.subject;
}

void PDFAppCertificateDetails::clearParts()
{
    // The parts are retired from the event loop (step A: a rebuilt row; one may be the target of the event, which
    // leads here)
    for (const QPointer<QWidget>& part : m_parts)
    {
        if (part)
        {
            PDFDeferredDeletion::retireLater(part.data());
        }
    }
    m_parts.clear();
}

void PDFAppCertificateDetails::setHeader(const QString& title, const QString& subtitle)
{
    if (m_header)
    {
        m_header->setTexts(title, subtitle);
    }
}

QWidget* PDFAppCertificateDetails::addSection(const QString& title, PDFAppToolSectionHeader** header)
{
    using Helper = PDFAppCertificateDetailsHelper;

    // section 436:9628: the header Semi Bold 13 and its card 8 apart
    QWidget* section = new QWidget(this);
    QVBoxLayout* layout = PDFAppPageWidgets::createColumn(section, Helper::SECTION_GAP);
    PDFAppToolSectionHeader* sectionHeader = new PDFAppToolSectionHeader(title, QString(), section);
    layout->addWidget(sectionHeader);
    PDFAppToolCard* card = new PDFAppToolCard(section);
    card->getBodyLayout()->setContentsMargins(Helper::USAGE_PADDING, Helper::USAGE_PADDING, Helper::USAGE_PADDING, Helper::USAGE_PADDING);
    card->getBodyLayout()->setSpacing(Helper::USAGE_SPACING);
    layout->addWidget(card);
    m_layout->addWidget(section);
    m_parts.emplace_back(section);

    m_usageHeader = sectionHeader;
    m_usageCard = card;
    if (header)
    {
        *header = sectionHeader;
    }
    return card;
}

PDFKeyValueList* PDFAppCertificateDetails::addValues(const QString& title)
{
    using Helper = PDFAppCertificateDetailsHelper;

    // section-Subject 436:9590: the header and the card of the key value rows (padding 6 / 12)
    QWidget* section = new QWidget(this);
    QVBoxLayout* layout = PDFAppPageWidgets::createColumn(section, Helper::SECTION_GAP);
    layout->addWidget(new PDFAppToolSectionHeader(title, QString(), section));
    PDFAppToolCard* card = new PDFAppToolCard(section);
    PDFKeyValueList* list = new PDFKeyValueList(card);
    list->setRowGap(Helper::ROW_GAP);
    card->getBodyLayout()->addWidget(list);
    layout->addWidget(card);
    m_layout->addWidget(section);
    m_parts.emplace_back(section);
    return list;
}

void PDFAppCertificateDetails::addUsageRow(QWidget* card, const QString& title, const QString& iconName)
{
    // item-Digital signatures 436:9635: the tile 26 with the icon 14, the title; not clickable
    PDFTileRow* row = new PDFTileRow(title, QString(), iconName, PDFTileRow::Kind::Result, card);
    row->setTone(PDFTileRow::Tone::Muted);
    row->setClickable(false);
    card->layout()->addWidget(row);
}

void PDFAppCertificateDetails::addNote(const QString& text)
{
    m_noteText = PDFAppPageWidgets::createText(text, PDFThemeFonts::getFont(QFont::Normal, 12), 17, PDFThemeColors::TextSecondary, this);
    m_noteText->setObjectName(QStringLiteral("certificateDetailsNote"));
    m_noteText->setMinimumWidth(1);
    m_layout->addWidget(m_noteText);
    m_parts.emplace_back(m_noteText.data());
}

void PDFAppCertificateDetails::showSubject(const PDFCertificateInfo& info)
{
    // 436:9590: the rows, which have a value (436:12277 has three of them)
    m_subjectValues = addValues(tr("Subject"));
    m_subjectValues->setObjectName(QStringLiteral("certificateDetailsSubject"));
    struct Entry
    {
        PDFCertificateInfo::NameEntry entry;
        const char* key;
    };
    static constexpr Entry entries[] =
    {
        { PDFCertificateInfo::CommonName, QT_TR_NOOP("Name") },
        { PDFCertificateInfo::OrganizationName, QT_TR_NOOP("Organization") },
        { PDFCertificateInfo::OrganizationalUnitName, QT_TR_NOOP("Unit") },
        { PDFCertificateInfo::Email, QT_TR_NOOP("E-mail") },
        { PDFCertificateInfo::CountryName, QT_TR_NOOP("Country") }
    };
    for (const Entry& entry : entries)
    {
        const QString value = PDFAppCertificateEntry::sanitizeText(info.getName(entry.entry));
        if (!value.isEmpty())
        {
            m_subjectValues->addRow(tr(entry.key), value, PDFComponentPainter::TextKind::Data);
        }
    }
}

void PDFAppCertificateDetails::showValidityCallout(const QDateTime& validFrom, const QDateTime& validTo)
{
    const QDateTime now = QDateTime::fromMSecsSinceEpoch(PDFAppLocale::getCurrentTime());
    QString title;
    QString text;
    if (validTo.isValid() && now > validTo)
    {
        // 436:11809
        title = tr("This certificate has expired");
        text = tr("It can no longer sign or encrypt. Keep it if you still need to open documents encrypted for it.");
    }
    else if (validFrom.isValid() && now < validFrom)
    {
        // No frame: the callout of 436:11809
        title = tr("This certificate is not valid yet");
        text = tr("It can be used from %1.").arg(PDFAppCertificateDetailsHelper::formatDate(validFrom));
    }
    if (title.isEmpty())
    {
        return;
    }

    m_validityCallout = new PDFAppToolCallout(PDFAccentHue::Red, QStringLiteral("alert-circle"), title, text, this);
    m_validityCallout->setObjectName(QStringLiteral("certificateDetailsValidity"));
    m_validityCallout->setCompactStyle();
    m_layout->addWidget(m_validityCallout);
    m_parts.emplace_back(m_validityCallout.data());
}

}   // namespace pdf

#include "moc_pdfappcertificatedetails.cpp"
