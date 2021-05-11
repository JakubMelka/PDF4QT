//    Copyright (C) 2020-2021 Jakub Melka
//
//    This file is part of Pdf4Qt.
//
//    Pdf4Qt is free software: you can redistribute it and/or modify
//    it under the terms of the GNU Lesser General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    with the written consent of the copyright owner, any later version.
//
//    Pdf4Qt is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU Lesser General Public License for more details.
//
//    You should have received a copy of the GNU Lesser General Public License
//    along with Pdf4Qt.  If not, see <https://www.gnu.org/licenses/>.

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
        PDFConsole::writeError(PDFToolTranslationContext::tr("No document specified."), options.outputCodec);
        return ErrorNoDocumentSpecified;
    }

    bool isFirstPasswordAttempt = true;
    auto passwordCallback = [&options, &isFirstPasswordAttempt](bool* ok) -> QString
    {
        *ok = isFirstPasswordAttempt;
        isFirstPasswordAttempt = false;
        return options.password;
    };
    pdf::PDFDocumentReader reader(nullptr, passwordCallback, options.permissiveReading, false);
    pdf::PDFDocument document = reader.readFromFile(options.document);

    switch (reader.getReadingResult())
    {
        case pdf::PDFDocumentReader::Result::OK:
            break;

        case pdf::PDFDocumentReader::Result::Cancelled:
            return ExitSuccess;

        case pdf::PDFDocumentReader::Result::Failed:
        {
            PDFConsole::writeError(PDFToolTranslationContext::tr("Error occured during document reading. %1").arg(reader.getErrorMessage()), options.outputCodec);
            return ErrorDocumentReading;
        }

        default:
            Q_ASSERT(false);
            return ErrorDocumentReading;
    }

    for (const QString& warning : reader.getWarnings())
    {
        PDFConsole::writeError(PDFToolTranslationContext::tr("Warning: %1").arg(warning), options.outputCodec);
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

    PDFOutputFormatter formatter(options.outputStyle, options.outputCodec);
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
            formatter.writeTableColumn("signing-date", signature.getSignatureDate().isValid() ? signature.getSignatureDate().toLocalTime().toString(options.outputDateFormat) : QString());
            formatter.writeTableColumn("timestamp-date", signature.getTimestampDate().isValid() ? signature.getTimestampDate().toLocalTime().toString(options.outputDateFormat) : QString());
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
                formatter.writeText("signing-date", PDFToolTranslationContext::tr("Signing date: %1").arg(signature.getSignatureDate().isValid() ? signature.getSignatureDate().toLocalTime().toString(options.outputDateFormat) : QString()));
                formatter.writeText("timestamp-date", PDFToolTranslationContext::tr("Timestamp date: %1").arg(signature.getTimestampDate().isValid() ? signature.getTimestampDate().toLocalTime().toString(options.outputDateFormat) : QString()));
                formatter.writeText("hash-algorithm", PDFToolTranslationContext::tr("Hash algorithm: %1").arg(signature.getHashAlgorithms().join(", ").toUpper()));
                formatter.writeText("handler", PDFToolTranslationContext::tr("Handler: %1").arg(QString::fromLatin1(signature.getSignatureHandler())));
                formatter.writeText("whole-signed", PDFToolTranslationContext::tr("Is whole document signed: %1").arg(signature.hasFlag(pdf::PDFSignatureVerificationResult::Warning_Signature_NotCoveredBytes) ? PDFToolTranslationContext::tr("No") : PDFToolTranslationContext::tr("Yes")));

                // Signature range
                const pdf::PDFClosedIntervalSet& bytesCoveredBySignature = signature.getBytesCoveredBySignature();
                formatter.writeText("byte-range", PDFToolTranslationContext::tr("Byte range covered by signature: %1").arg(bytesCoveredBySignature.toText(false)));

                if (signature.hasError())
                {
                    formatter.endl();
                    formatter.beginHeader("errors", PDFToolTranslationContext::tr("Errors:"));
                    for (const QString& error : signature.getErrors())
                    {
                        formatter.writeText("error", error);
                    }
                    formatter.endHeader();
                }

                if (signature.hasWarning())
                {
                    formatter.endl();
                    formatter.beginHeader("warnings", PDFToolTranslationContext::tr("Warnings:"));
                    for (const QString& warning : signature.getWarnings())
                    {
                        formatter.writeText("warning", warning);
                    }
                    formatter.endHeader();
                }

                formatter.endl();

                if (!options.verificationOmitCertificateCheck)
                {
                    int certificateIndex = 1;
                    for (const pdf::PDFCertificateInfo& currentInfo : certificateInfos)
                    {
                        formatter.beginTable("certificate", PDFToolTranslationContext::tr("Certificate No. #%1").arg(certificateIndex++));

                        formatter.beginTableHeaderRow("header");
                        formatter.writeTableHeaderColumn("description", PDFToolTranslationContext::tr("Description"));
                        formatter.writeTableHeaderColumn("value", PDFToolTranslationContext::tr("Value"));
                        formatter.endTableHeaderRow();

                        auto addName = [&formatter, &currentInfo](pdf::PDFCertificateInfo::NameEntry nameEntry, QString caption)
                        {
                            QString text = currentInfo.getName(nameEntry);
                            if (!text.isEmpty())
                            {
                                formatter.beginTableRow("info", int(nameEntry));
                                formatter.writeTableColumn("description", caption);
                                formatter.writeTableColumn("value", text);
                                formatter.endTableRow();
                            }
                        };

                        addName(pdf::PDFCertificateInfo::CountryName, PDFToolTranslationContext::tr("Country"));
                        addName(pdf::PDFCertificateInfo::OrganizationName, PDFToolTranslationContext::tr("Organization"));
                        addName(pdf::PDFCertificateInfo::OrganizationalUnitName, PDFToolTranslationContext::tr("Org. unit"));
                        addName(pdf::PDFCertificateInfo::DistinguishedName, PDFToolTranslationContext::tr("Name"));
                        addName(pdf::PDFCertificateInfo::StateOrProvinceName, PDFToolTranslationContext::tr("State"));
                        addName(pdf::PDFCertificateInfo::SerialNumber, PDFToolTranslationContext::tr("Serial number"));
                        addName(pdf::PDFCertificateInfo::LocalityName, PDFToolTranslationContext::tr("Locality"));
                        addName(pdf::PDFCertificateInfo::Title, PDFToolTranslationContext::tr("Title"));
                        addName(pdf::PDFCertificateInfo::Surname, PDFToolTranslationContext::tr("Surname"));
                        addName(pdf::PDFCertificateInfo::GivenName, PDFToolTranslationContext::tr("Forename"));
                        addName(pdf::PDFCertificateInfo::Initials, PDFToolTranslationContext::tr("Initials"));
                        addName(pdf::PDFCertificateInfo::Pseudonym, PDFToolTranslationContext::tr("Pseudonym"));
                        addName(pdf::PDFCertificateInfo::GenerationalQualifier, PDFToolTranslationContext::tr("Qualifier"));
                        addName(pdf::PDFCertificateInfo::Email, PDFToolTranslationContext::tr("Email"));

                        QString publicKeyMethod;
                        switch (currentInfo.getPublicKey())
                        {
                            case pdf::PDFCertificateInfo::KeyRSA:
                                publicKeyMethod = PDFToolTranslationContext::tr("RSA method, %1-bit key").arg(currentInfo.getKeySize());
                                break;

                            case pdf::PDFCertificateInfo::KeyDSA:
                                publicKeyMethod = PDFToolTranslationContext::tr("DSA method, %1-bit key").arg(currentInfo.getKeySize());
                                break;

                            case pdf::PDFCertificateInfo::KeyEC:
                                publicKeyMethod = PDFToolTranslationContext::tr("EC method, %1-bit key").arg(currentInfo.getKeySize());
                                break;

                            case pdf::PDFCertificateInfo::KeyDH:
                                publicKeyMethod = PDFToolTranslationContext::tr("DH method, %1-bit key").arg(currentInfo.getKeySize());
                                break;

                            case pdf::PDFCertificateInfo::KeyUnknown:
                                publicKeyMethod = PDFToolTranslationContext::tr("Unknown method, %1-bit key").arg(currentInfo.getKeySize());
                                break;

                            default:
                                Q_ASSERT(false);
                                break;
                        }

                        formatter.beginTableRow("protection-method");
                        formatter.writeTableColumn("description", PDFToolTranslationContext::tr("Encryption method"));
                        formatter.writeTableColumn("value", publicKeyMethod);
                        formatter.endTableRow();

                        QDateTime notValidBefore = currentInfo.getNotValidBefore().toLocalTime();
                        QDateTime notValidAfter = currentInfo.getNotValidAfter().toLocalTime();

                        if (notValidBefore.isValid())
                        {
                            formatter.beginTableRow("valid-from");
                            formatter.writeTableColumn("description", PDFToolTranslationContext::tr("Valid from"));
                            formatter.writeTableColumn("value", notValidBefore.toString(options.outputDateFormat));
                            formatter.endTableRow();
                        }

                        if (notValidAfter.isValid())
                        {
                            formatter.beginTableRow("valid-to");
                            formatter.writeTableColumn("description", PDFToolTranslationContext::tr("Valid to"));
                            formatter.writeTableColumn("value", notValidAfter.toString(options.outputDateFormat));
                            formatter.endTableRow();
                        }

                        pdf::PDFCertificateInfo::KeyUsageFlags keyUsageFlags = currentInfo.getKeyUsage();
                        QStringList keyUsages;
                        if (keyUsageFlags.testFlag(pdf::PDFCertificateInfo::KeyUsageDigitalSignature))
                        {
                            keyUsages << PDFToolTranslationContext::tr("Digital signatures");
                        }
                        if (keyUsageFlags.testFlag(pdf::PDFCertificateInfo::KeyUsageNonRepudiation))
                        {
                            keyUsages << PDFToolTranslationContext::tr("Non-repudiation");
                        }
                        if (keyUsageFlags.testFlag(pdf::PDFCertificateInfo::KeyUsageKeyEncipherment))
                        {
                            keyUsages << PDFToolTranslationContext::tr("Key encipherement");
                        }
                        if (keyUsageFlags.testFlag(pdf::PDFCertificateInfo::KeyUsageDataEncipherment))
                        {
                            keyUsages << PDFToolTranslationContext::tr("Application data encipherement");
                        }
                        if (keyUsageFlags.testFlag(pdf::PDFCertificateInfo::KeyUsageAgreement))
                        {
                            keyUsages << PDFToolTranslationContext::tr("Key agreement");
                        }
                        if (keyUsageFlags.testFlag(pdf::PDFCertificateInfo::KeyUsageCertSign))
                        {
                            keyUsages << PDFToolTranslationContext::tr("Verify signatures on certificates");
                        }
                        if (keyUsageFlags.testFlag(pdf::PDFCertificateInfo::KeyUsageCrlSign))
                        {
                            keyUsages << PDFToolTranslationContext::tr("Verify signatures on revocation information");
                        }
                        if (keyUsageFlags.testFlag(pdf::PDFCertificateInfo::KeyUsageEncipherOnly))
                        {
                            keyUsages << PDFToolTranslationContext::tr("Encipher data during key agreement");
                        }
                        if (keyUsageFlags.testFlag(pdf::PDFCertificateInfo::KeyUsageDecipherOnly))
                        {
                            keyUsages << PDFToolTranslationContext::tr("Decipher data during key agreement");
                        }
                        if (keyUsageFlags.testFlag(pdf::PDFCertificateInfo::KeyUsageExtended_TIMESTAMP))
                        {
                            keyUsages << PDFToolTranslationContext::tr("Trusted timestamping");
                        }

                        formatter.beginTableRow("key-usages");
                        formatter.writeTableColumn("description", PDFToolTranslationContext::tr("Key usage"));
                        formatter.writeTableColumn("value", keyUsages.join(", "));
                        formatter.endTableRow();

                        formatter.endTable();
                        formatter.endl();
                    }
                }

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

    PDFConsole::writeText(formatter.getString(), options.outputCodec);
    return ExitSuccess;
}

PDFToolAbstractApplication::Options PDFToolVerifySignaturesApplication::getOptionsFlags() const
{
    return PDFToolAbstractApplication::ConsoleFormat | PDFToolAbstractApplication::OpenDocument | PDFToolAbstractApplication::SignatureVerification | PDFToolAbstractApplication::DateFormat;
}

}   // namespace pdftool
