//    Copyright (C) 2020 Jakub Melka
//
//    This file is part of PdfForQt.
//
//    PdfForQt is free software: you can redistribute it and/or modify
//    it under the terms of the GNU Lesser General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    (at your option) any later version.
//
//    PdfForQt is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU Lesser General Public License for more details.
//
//    You should have received a copy of the GNU Lesser General Public License
//    along with PDFForQt.  If not, see <https://www.gnu.org/licenses/>.

#include "pdftoolcertstore.h"
#include "pdfsignaturehandler.h"

namespace pdftool
{

static PDFToolCertStore s_certStoreApplication;

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

    PDFOutputFormatter formatter(options.outputStyle, options.outputCodec);
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
            notValidBeforeText = notValidBefore.toString(options.outputDateFormat);
        }

        if (notValidAfter.isValid())
        {
            notValidAfterText = notValidAfter.toString(options.outputDateFormat);
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

}   // namespace pdftool
