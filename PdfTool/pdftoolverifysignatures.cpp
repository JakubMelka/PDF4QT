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

#include "pdftoolverifysignatures.h"

#include "pdfdocumentreader.h"
#include "pdfsignaturehandler.h"
#include "pdfform.h"

namespace pdftool
{

static PDFToolVerifySignaturesApplication s_verifySignaturesApplication;

QString PDFToolVerifySignaturesApplication::getStandardString(StandardString standardString) const
{
    switch (standardString)
    {
        case Command:
            return "verify-signatures";

        case Name:
            return PDFToolTranslationContext::tr("Signature verification");

        case Description:
            return PDFToolTranslationContext::tr("Verify signatures and timestamps in pdf document.");

        default:
            Q_ASSERT(false);
            break;
    }

    return QString();
}

int PDFToolVerifySignaturesApplication::execute(const PDFToolOptions& options)
{
    // No document specified?
    if (options.document.isEmpty())
    {
        PDFConsole::writeError(PDFToolTranslationContext::tr("No document specified."));
        return ErrorNoDocumentSpecified;
    }

    bool isFirstPasswordAttempt = true;
    auto passwordCallback = [&options, &isFirstPasswordAttempt](bool* ok) -> QString
    {
        *ok = isFirstPasswordAttempt;
        isFirstPasswordAttempt = false;
        return options.password;
    };
    pdf::PDFDocumentReader reader(nullptr, passwordCallback, options.permissiveReading);
    pdf::PDFDocument document = reader.readFromFile(options.document);

    switch (reader.getReadingResult())
    {
        case pdf::PDFDocumentReader::Result::OK:
            break;

        case pdf::PDFDocumentReader::Result::Cancelled:
            return ExitSuccess;

        case pdf::PDFDocumentReader::Result::Failed:
        {
            PDFConsole::writeError(PDFToolTranslationContext::tr("Error occured during document reading. %1").arg(reader.getErrorMessage()));
            return ErrorDocumentReading;
        }

        default:
            Q_ASSERT(false);
            return ErrorDocumentReading;
    }

    for (const QString& warning : reader.getWarnings())
    {
        PDFConsole::writeError(PDFToolTranslationContext::tr("Warning: %1").arg(warning));
    }

    // Verify signatures
    pdf::PDFCertificateStore certificateStore;
    if (options.verificationUseUserCertificates)
    {
        certificateStore.loadDefaultUserCertificates();
    }

    pdf::PDFSignatureHandler::Parameters parameters;
    parameters.store = &certificateStore;
    parameters.dss = &document.getCatalog()->getDocumentSecurityStore();
    parameters.enableVerification = true;
    parameters.ignoreExpirationDate = options.verificationIgnoreExpirationDate;
    parameters.useSystemCertificateStore = options.verificationUseSystemCertificates;

    pdf::PDFForm form = pdf::PDFForm::parse(&document, document.getCatalog()->getFormObject());
    std::vector<pdf::PDFSignatureVerificationResult> signatures = pdf::PDFSignatureHandler::verifySignatures(form, reader.getSource(), parameters);

    PDFOutputFormatter formatter(options.outputStyle);
    formatter.beginDocument("signatures", PDFToolTranslationContext::tr("Digital signatures/timestamps verification of %1").arg(options.document));
    formatter.endl();

    auto getTypeName = [](const pdf::PDFSignatureVerificationResult& signature)
    {
        switch (signature.getType())
        {
            case pdf::PDFSignature::Type::Invalid:
                return PDFToolTranslationContext::tr("Invalid");

            case pdf::PDFSignature::Type::Sig:
                return PDFToolTranslationContext::tr("Signature");

            case pdf::PDFSignature::Type::DocTimeStamp:
                return PDFToolTranslationContext::tr("Timestamp");
                break;

            default:
                Q_ASSERT(false);
                break;
        }

        return QString();
    };

    if (!signatures.empty())
    {
        formatter.beginTable("overview", PDFToolTranslationContext::tr("Overview"));

        formatter.beginTableHeaderRow("header");
        formatter.writeTableHeaderColumn("no", PDFToolTranslationContext::tr("No."), Qt::AlignLeft);
        formatter.writeTableHeaderColumn("type", PDFToolTranslationContext::tr("Type"), Qt::AlignLeft);
        formatter.writeTableHeaderColumn("common-name", PDFToolTranslationContext::tr("Signed by"), Qt::AlignLeft);
        formatter.writeTableHeaderColumn("cert-status", PDFToolTranslationContext::tr("Certificate"), Qt::AlignLeft);
        formatter.writeTableHeaderColumn("signature-status", PDFToolTranslationContext::tr("Signature"), Qt::AlignLeft);
        formatter.writeTableHeaderColumn("signing-date", PDFToolTranslationContext::tr("Signing date"), Qt::AlignLeft);
        formatter.writeTableHeaderColumn("timestamp-date", PDFToolTranslationContext::tr("Timestamp date"), Qt::AlignLeft);
        formatter.writeTableHeaderColumn("hash-algorithm", PDFToolTranslationContext::tr("Hash alg."), Qt::AlignLeft);
        formatter.writeTableHeaderColumn("handler", PDFToolTranslationContext::tr("Handler"), Qt::AlignLeft);
        formatter.writeTableHeaderColumn("whole-signed", PDFToolTranslationContext::tr("Signed whole"), Qt::AlignLeft);
        formatter.endTableHeaderRow();

        int i = 1;
        for (const pdf::PDFSignatureVerificationResult& signature : signatures)
        {
            const pdf::PDFCertificateInfos& certificateInfos = signature.getCertificateInfos();
            const pdf::PDFCertificateInfo* certificateInfo = !certificateInfos.empty() ? &certificateInfos.front() : nullptr;

            formatter.beginTableRow("signature", i);

            formatter.writeTableColumn("no", QString::number(i), Qt::AlignRight);
            formatter.writeTableColumn("type", getTypeName(signature));

            QString commonName = certificateInfo ? certificateInfo->getName(pdf::PDFCertificateInfo::CommonName) : PDFToolTranslationContext::tr("Unknown");
            formatter.writeTableColumn("common-name", commonName);
            formatter.writeTableColumn("cert-status", options.verificationOmitCertificateCheck ? PDFToolTranslationContext::tr("Skipped") : signature.getCertificateStatusText());
            formatter.writeTableColumn("signature-status", signature.getSignatureStatusText());
            formatter.writeTableColumn("signing-date", signature.getSignatureDate().isValid() ? signature.getSignatureDate().toLocalTime().toString(options.verificationDateFormat) : QString());
            formatter.writeTableColumn("timestamp-date", signature.getTimestampDate().isValid() ? signature.getTimestampDate().toLocalTime().toString(options.verificationDateFormat) : QString());
            formatter.writeTableColumn("hash-algorithm", signature.getHashAlgorithms().join(", ").toUpper());
            formatter.writeTableColumn("handler", QString::fromLatin1(signature.getSignatureHandler()));
            formatter.writeTableColumn("whole-signed", signature.hasFlag(pdf::PDFSignatureVerificationResult::Warning_Signature_NotCoveredBytes) ? PDFToolTranslationContext::tr("No") : PDFToolTranslationContext::tr("Yes"));

            formatter.endTableRow();
            ++i;
        }

        formatter.endTable();


        if (options.verificationPrintCertificateDetails)
        {
            formatter.endl();
            formatter.beginHeader("details", PDFToolTranslationContext::tr("Details"));

            int i = 1;
            for (const pdf::PDFSignatureVerificationResult& signature : signatures)
            {
                formatter.endl();
                formatter.beginHeader("signature", PDFToolTranslationContext::tr("%1 #%2").arg(getTypeName(signature)).arg(i), i);

                const pdf::PDFCertificateInfos& certificateInfos = signature.getCertificateInfos();
                const pdf::PDFCertificateInfo* certificateInfo = !certificateInfos.empty() ? &certificateInfos.front() : nullptr;
                QString commonName = certificateInfo ? certificateInfo->getName(pdf::PDFCertificateInfo::CommonName) : PDFToolTranslationContext::tr("Unknown");
                formatter.writeText("common-name", PDFToolTranslationContext::tr("Signed by: %1").arg(commonName));
                formatter.writeText("certificate-status", PDFToolTranslationContext::tr("Certificate status: %1").arg(options.verificationOmitCertificateCheck ? PDFToolTranslationContext::tr("Skipped") : signature.getCertificateStatusText()));
                formatter.writeText("signature-status", PDFToolTranslationContext::tr("Signature status: %1").arg(signature.getSignatureStatusText()));
                formatter.writeText("signing-date", PDFToolTranslationContext::tr("Signing date: %1").arg(signature.getSignatureDate().isValid() ? signature.getSignatureDate().toLocalTime().toString(options.verificationDateFormat) : QString()));
                formatter.writeText("timestamp-date", PDFToolTranslationContext::tr("Timestamp date: %1").arg(signature.getTimestampDate().isValid() ? signature.getTimestampDate().toLocalTime().toString(options.verificationDateFormat) : QString()));
                formatter.writeText("hash-algorithm", PDFToolTranslationContext::tr("Hash algorithm: %1").arg(signature.getHashAlgorithms().join(", ").toUpper()));
                formatter.writeText("handler", PDFToolTranslationContext::tr("Handler: %1").arg(QString::fromLatin1(signature.getSignatureHandler())));
                formatter.writeText("whole-signed", PDFToolTranslationContext::tr("Is whole document signed: %1").arg(signature.hasFlag(pdf::PDFSignatureVerificationResult::Warning_Signature_NotCoveredBytes) ? PDFToolTranslationContext::tr("No") : PDFToolTranslationContext::tr("Yes")));

                formatter.endHeader();
                ++i;
            }

            formatter.endHeader();
        }
    }
    else
    {
        formatter.writeText("no-signatures", PDFToolTranslationContext::tr("No digital signatures or timestamps found in the document."));
    }

    formatter.endDocument();

    PDFConsole::writeText(formatter.getString());
    return ExitSuccess;
}

PDFToolAbstractApplication::Options PDFToolVerifySignaturesApplication::getOptionsFlags() const
{
    return PDFToolAbstractApplication::ConsoleFormat | PDFToolAbstractApplication::OpenDocument | PDFToolAbstractApplication::SignatureVerification;
}

}   // namespace pdftool
