//    Copyright (C) 2020-2021 Jakub Melka
//
//    This file is part of PDF4QT.
//
//    PDF4QT is free software: you can redistribute it and/or modify
//    it under the terms of the GNU Lesser General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    with the written consent of the copyright owner, any later version.
//
//    PDF4QT is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU Lesser General Public License for more details.
//
//    You should have received a copy of the GNU Lesser General Public License
//    along with PDF4QT.  If not, see <https://www.gnu.org/licenses/>.

#include "pdftoolcertstore.h"
#include "pdfsignaturehandler.h"

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

    pdf::PDFCertificateStore::CertificateEntries certificates = certificateStore.getCertificates();
    if (options.certStoreEnumerateSystemCertificates)
    {
        pdf::PDFCertificateStore::CertificateEntries systemCertificates = pdf::PDFCertificateStore::getSystemCertificates();
        certificates.insert(certificates.end(), std::make_move_iterator(systemCertificates.begin()), std::make_move_iterator(systemCertificates.end()));
    }

    PDFOutputFormatter formatter(options.outputStyle);
    formatter.beginDocument("cert-store", PDFToolTranslationContext::tr("Certificates used in signature verification"));
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

    for (const pdf::PDFCertificateStore::CertificateEntry& entry : certificates)
    {
        QString type;
        switch (entry.type)
        {
            case pdf::PDFCertificateStore::EntryType::User:
                type = PDFToolTranslationContext::tr("User");
                break;

            case pdf::PDFCertificateStore::EntryType::EUTL:
                type = PDFToolTranslationContext::tr("EUTL");
                break;

            case pdf::PDFCertificateStore::EntryType::System:
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

    if (certificateStore.add(pdf::PDFCertificateStore::EntryType::User, certificateData))
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
