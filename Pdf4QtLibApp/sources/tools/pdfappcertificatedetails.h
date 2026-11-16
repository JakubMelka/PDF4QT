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

#ifndef PDFAPPCERTIFICATEDETAILS_H
#define PDFAPPCERTIFICATEDETAILS_H

#include "pdfappglobal.h"
#include "pdfappcertificatetable.h"
#include "pdfapptoolcertificatelist.h"
#include "pdfapptoolpanelparts.h"

#include "pdfcomponentpanels.h"
#include "pdfcomponentrows.h"

#include <QPointer>
#include <QWidget>

#include <optional>
#include <vector>

class QVBoxLayout;

namespace pdf
{

class PDFAppParagraph;
class PDFAppToolCallout;
class PDFAppToolPanelHeader;
class PDFAppToolSectionHeader;
class PDFKeyValueList;
class PDFTileRow;

/// The detail of a certificate in the right panel of Manage Certificates (plan of step 6 of phase 2,
/// 2.4, 4.5, K6.14, K6.17; Figma 436:9398 a .pfx file, 436:9830 a certificate of the system store,
/// 436:11809 an expired one, 436:11280 a new one, 436:12086 a trusted certificate, 436:9646 no
/// selection): the header (the name and the file, "System store" or "Trusted · added by you"), the
/// red callout of an expired certificate, the sections Subject (Name, Organization, Unit, E-mail,
/// Country - the rows, which have a value), Certificate (Issued by - "Self-signed" when the issuer is
/// the subject, RFC 5280 6.1; Valid from, Valid to by PDFAppLocale, O17; Key "RSA 2048-bit"; Private
/// key "Password" / "No password" / "In the system" of my certificates; Serial number of a trusted
/// one) and Usable for (Digital signatures, Certificate encryption - B116, B99) of my certificates or
/// Key usage (RFC 5280, 4.2.1.3, 4.2.1.12) of a trusted one. The values come from the certificate
/// itself (K6.14), never from the frame; its texts are untrusted data shown on one line each
/// (PDFAppCertificateEntry::sanitizeText). Nothing of a private key, a password or a folder is shown.
///
/// The widget owns its parts (children, held by QPointer); each setter makes the parts of the sections
/// again (the old ones are deleted later). GUI thread.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppCertificateDetails : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    explicit PDFAppCertificateDetails(QWidget* parent);
    virtual ~PDFAppCertificateDetails() override;

    /// Shows the certificate of a row of My certificates (std::nullopt = the empty state of no
    /// selection, 436:9646 "Details / Select a certificate")
    void setCertificate(const std::optional<PDFAppToolCertificateItem>& certificate);

    /// Shows a trusted certificate (436:12086)
    void setTrustedCertificate(const PDFAppCertificateEntry& entry);

    /// The shown certificate of My certificates, std::nullopt = none (also while a trusted one is shown)
    const std::optional<PDFAppToolCertificateItem>& getCertificate() const { return m_certificate; }

    /// Parts (tests; nullptr = not shown)
    PDFAppToolPanelHeader* getHeader() const { return m_header.data(); }
    PDFAppToolCallout* getValidityCallout() const { return m_validityCallout.data(); }
    PDFKeyValueList* getSubjectValues() const { return m_subjectValues.data(); }
    PDFKeyValueList* getCertificateValues() const { return m_certificateValues.data(); }
    PDFAppToolSectionHeader* getUsageHeader() const { return m_usageHeader.data(); }
    std::vector<PDFTileRow*> getUsageRows() const;
    PDFAppParagraph* getNoteText() const { return m_noteText.data(); }

    /// The value of the row of the key of a list (tests; empty = no such row)
    static QString getValue(const PDFKeyValueList* list, const QString& key);

    /// The text of the serial number of a DER certificate: its bytes in hexadecimal, two digits each,
    /// separated by colons ("4F:2A:91:0C"), without the leading zero byte of a positive INTEGER (RFC
    /// 5280, 4.1.2.2; X.690 8.3.2); empty for data, which are no certificate
    static QString getSerialNumberText(const QByteArray& certificateData);

    /// The key of a certificate: "RSA 2048-bit", "EC 256-bit" (436:9398)
    static QString getKeyText(const PDFCertificateInfo& info);

    /// The issuer of the DER certificate is its subject - the same encoded Name (a self-issued
    /// certificate, RFC 5280 6.1; the certificates of Create Certificate… are self-signed); false for
    /// data, which are no certificate
    static bool isSelfIssued(const QByteArray& certificateData);

private:
    void clearParts();
    void setHeader(const QString& title, const QString& subtitle);
    QWidget* addSection(const QString& title, PDFAppToolSectionHeader** header);
    PDFKeyValueList* addValues(const QString& title);
    void addUsageRow(QWidget* card, const QString& title, const QString& iconName);
    void addNote(const QString& text);
    void showSubject(const PDFCertificateInfo& info);
    void showValidityCallout(const QDateTime& validFrom, const QDateTime& validTo);

    std::optional<PDFAppToolCertificateItem> m_certificate;
    QPointer<QVBoxLayout> m_layout;
    QPointer<PDFAppToolPanelHeader> m_header;
    QPointer<PDFAppToolCallout> m_validityCallout;
    QPointer<PDFKeyValueList> m_subjectValues;
    QPointer<PDFKeyValueList> m_certificateValues;
    QPointer<PDFAppToolSectionHeader> m_usageHeader;
    QPointer<QWidget> m_usageCard;
    QPointer<PDFAppParagraph> m_noteText;
    std::vector<QPointer<QWidget>> m_parts;     ///< Every part below the header (made again by each setter)
};

}   // namespace pdf

#endif // PDFAPPCERTIFICATEDETAILS_H
