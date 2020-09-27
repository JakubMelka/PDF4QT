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

    return ExitSuccess;
}

PDFToolAbstractApplication::Options PDFToolVerifySignaturesApplication::getOptionsFlags() const
{
    return PDFToolAbstractApplication::ConsoleFormat | PDFToolAbstractApplication::OpenDocument | PDFToolAbstractApplication::SignatureVerification;
}

}   // namespace pdftool
