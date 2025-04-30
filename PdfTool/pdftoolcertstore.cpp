// MIT License
//
// Copyright (c) 2018-2025 Jakub Melka and Contributors
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

#include "pdftoolcertstore.h"
#include "pdfsignaturehandler.h"

#include <QFile>

namespace pdftool
{

static PDFToolCertStore s_certStoreApplication;
static PDFToolCertStoreInstallCertificate s_certStoreAddApplication;

QString PDFToolCertStore::getStandardString(PDFToolAbstractApplication::StandardString standardString) const
{
    switch (standardString)
    {
        case Command:
            return "cert-store";

        case Name:
            return PDFToolTranslationContext::tr("Certificate Store");

        case Description:
            return PDFToolTranslationContext::tr("Certificate store operations (list, add, remove certificates).");

        default:
            Q_ASSERT(false);
            break;
    }

    return QString();
}

int PDFToolCertStore::execute(const PDFToolOptions& options)
{
    // Certificate store
    pdf::PDFCertificateStore certificateStore;


    if (options.certStoreEnumerateUserCertificates)
    {
        certificateStore.loadDefaultUserCertificates();
    }

    pdf::PDFCertificateEntries certificates = certificateStore.getCertificates();
    if (options.certStoreEnumerateSystemCertificates)
    {
        pdf::PDFCertificateEntries systemCertificates = pdf::PDFCertificateStore::getSystemCertificates();
        certificates.insert(certificates.end(), std::make_move_iterator(systemCertificates.begin()), std::make_move_iterator(systemCertificates.end()));

        pdf::PDFCertificateEntries aatlCertificates = pdf::PDFCertificateStore::getAATLCertificates();
        certificates.insert(certificates.end(), std::make_move_iterator(aatlCertificates.begin()), std::make_move_iterator(aatlCertificates.end()));
    }

    PDFOutputFormatter formatter(options.outputStyle);
    formatter.beginDocument("cert-store", PDFToolTranslationContext::tr("Certificates used in the signature verification"));
    formatter.endl();

    formatter.beginTable("certificate-list", PDFToolTranslationContext::tr("Certificates"));

    formatter.beginTableHeaderRow("header");
    formatter.writeTableHeaderColumn("no", PDFToolTranslationContext::tr("No."));
    formatter.writeTableHeaderColumn("type", PDFToolTranslationContext::tr("Type"));
    formatter.writeTableHeaderColumn("certificate", PDFToolTranslationContext::tr("Certificate"));
    formatter.writeTableHeaderColumn("organization", PDFToolTranslationContext::tr("Organization"));
    formatter.writeTableHeaderColumn("valid-from", PDFToolTranslationContext::tr("Valid from"));
    formatter.writeTableHeaderColumn("valid-to", PDFToolTranslationContext::tr("Valid to"));
    formatter.endTableHeaderRow();

    int ref = 1;
    QLocale locale;

    for (const pdf::PDFCertificateEntry& entry : certificates)
    {
        QString type;
        switch (entry.type)
        {
            case pdf::PDFCertificateEntry::EntryType::User:
                type = PDFToolTranslationContext::tr("User");
                break;

            case pdf::PDFCertificateEntry::EntryType::System:
                type = PDFToolTranslationContext::tr("System");
                break;

            default:
                Q_ASSERT(false);
                break;
        }

        QDateTime notValidBefore = entry.info.getNotValidBefore().toLocalTime();
        QDateTime notValidAfter = entry.info.getNotValidAfter().toLocalTime();

        QString notValidBeforeText;
        QString notValidAfterText;

        if (notValidBefore.isValid())
        {
            notValidBeforeText = convertDateTimeToString(notValidBefore, options.outputDateFormat);
        }

        if (notValidAfter.isValid())
        {
            notValidAfterText = convertDateTimeToString(notValidAfter, options.outputDateFormat);
        }

        formatter.beginTableRow("certificate", ref);

        formatter.writeTableColumn("no", locale.toString(ref++), Qt::AlignRight);
        formatter.writeTableColumn("type", type);
        formatter.writeTableColumn("certificate", entry.info.getName(pdf::PDFCertificateInfo::CommonName));
        formatter.writeTableColumn("organization", entry.info.getName(pdf::PDFCertificateInfo::OrganizationName));
        formatter.writeTableColumn("valid-from", notValidBeforeText);
        formatter.writeTableColumn("valid-to", notValidAfterText);

        formatter.endTableRow();
    }

    formatter.endTable();

    formatter.endDocument();
    PDFConsole::writeText(formatter.getString(), options.outputCodec);

    return ExitSuccess;
}

PDFToolAbstractApplication::Options PDFToolCertStore::getOptionsFlags() const
{
    return ConsoleFormat | DateFormat | CertStore;
}

QString PDFToolCertStoreInstallCertificate::getStandardString(PDFToolAbstractApplication::StandardString standardString) const
{
    switch (standardString)
    {
        case Command:
            return "cert-store-install";

        case Name:
            return PDFToolTranslationContext::tr("Install Certificate");

        case Description:
            return PDFToolTranslationContext::tr("Install a new user certificate to certificate store.");

        default:
            Q_ASSERT(false);
            break;
    }

    return QString();
}

int PDFToolCertStoreInstallCertificate::execute(const PDFToolOptions& options)
{
    QByteArray certificateData;
    QFile file(options.certificateStoreInstallCertificateFile);
    if (file.open(QFile::ReadOnly))
    {
        certificateData = file.readAll();
        file.close();
    }
    else
    {
        PDFConsole::writeError(PDFToolTranslationContext::tr("Cannot open file '%1'. %2").arg(options.certificateStoreInstallCertificateFile, file.errorString()), options.outputCodec);
        return ErrorCertificateReading;
    }

    pdf::PDFCertificateStore certificateStore;
    certificateStore.loadDefaultUserCertificates();

    if (certificateStore.add(pdf::PDFCertificateEntry::EntryType::User, certificateData))
    {
        certificateStore.saveDefaultUserCertificates();
    }
    else
    {
        PDFConsole::writeError(PDFToolTranslationContext::tr("Cannot read certificate from file '%1'.").arg(options.certificateStoreInstallCertificateFile), options.outputCodec);
        return ErrorCertificateReading;
    }

    return ExitSuccess;
}

PDFToolAbstractApplication::Options PDFToolCertStoreInstallCertificate::getOptionsFlags() const
{
    return ConsoleFormat | CertStoreInstall;
}

}   // namespace pdftool
