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

#ifndef PDFDOCUMENTTOOL_SIGNATURE_H
#define PDFDOCUMENTTOOL_SIGNATURE_H

#include "pdfdocumenttool.h"
#include "pdfcertificatemanager.h"

#include <QRectF>
#include <QStringList>

#include <optional>

namespace pdf
{

/// Sign with Certificate (protect.sign) and Add Timestamp (protect.timestamp) in one class of the core
/// (plan of step 6 of phase 2, 4.1, as changed by chapters 12 and 13; package P2 implements the run). One
/// instance per mode is registered (the pattern of PDFDocumentToolEncryption). The skeleton fixed the
/// ids, the modes, the parameters and the names of the facts and the data.
///
/// Parameters (the names are PARAMETER_*):
///   - certificate (String, Sign): the SHA-256 fingerprint of the certificate of the user (64 lower case
///     hexadecimal digits, PDFDocumentToolEncryption::getCertificateFingerprint); the GUI and the
///     confirmation of MCP give the entry with its PKCS#12 in PDFDocumentToolContext::userCertificates
///     (an assistant gives only a preselection, the user chooses in PDF4QT - Q6.7);
///   - certificatePassword (Secret, Sign): the password of the .pfx file; never stored, never in MCP,
///     never in the parameter model; it stays in the values of the run only until the run ends or the
///     error dialog of the authority closes (6.1, R6-11);
///   - format (Enum pades / pkcs7, default pades; Sign; owner answer Q6.1): ETSI.CAdES.detached or
///     adbe.pkcs7.detached (PDFSignatureFormat); a field with /SV /SubFilter restricts it (R6-25);
///   - type (Enum signature / signature-timestamp, default signature; Sign, Figma 434:4951);
///   - appearance (Enum visible / invisible, default visible; Sign): MCP gives only invisible (Q6.7);
///   - drawing (Value PDFPageDrawing, Sign, sensitive - R6-19): the visible appearance, the elements of
///     its first page with graphics (434:7784); never in the schema of MCP;
///   - field (String, Sign, at most 256 characters): the full name of an existing empty signature field
///     (Q6.3); empty = a new field;
///   - reason, contactInfo (String, Sign, at most MAXIMUM_TEXT_LENGTH characters): /Reason and
///     /ContactInfo (Table 255); texts of the user, never a secret;
///   - tsaUrl (String, Sign with signature-timestamp and Timestamp, at most
///     PDFTimestampClient::MAXIMUM_URL_LENGTH): the address of the authority, validated by
///     PDFTimestampClient::validateUrl; MCP only an address of the list of the user (6.5).
/// Add Timestamp has only tsaUrl.
///
/// The run (P2, 4.1 with R6-1, R6-4, R6-8, R6-12, B121, B122, B134, B135): an incremental update of the
/// exact bytes of the input (PDFDocumentToolInputDocument::originalData; signatures counted from them,
/// never from the document in the memory - R6-12), a whole write only of a document without signatures;
/// a whole rewrite in the tab or a restored file with signatures fails ("The document was not signed …
/// signatures which cannot be preserved", 434:8913); one unlock of the key (PDFSignatureFactory::unlock,
/// RAII); the appearance by PDFPageDrawing::createFormXObject; the timestamp by the client of the context
/// (PDFDocumentToolContext::timestampClient); the new signature and every earlier one verified again over
/// the written bytes (the fact Earlier signatures). Certified P = 1: Sign refused, Timestamp only in a
/// revision of nothing else (R6-4). The result is a New document with writtenData = the verified bytes
/// (PDFDocumentToolOutputDocument::writtenData, isFileOnlyResult) and the suggested name
/// "<name>_SIGNED.pdf" / "<name>_TIMESTAMPED.pdf".
///
/// Result: the facts FACT_* (434:9276, 435:8356) and the data of MCP DATA_*. Never a password, a key or a
/// path of a certificate.
class PDF4QTLIBCORESHARED_EXPORT PDFDocumentToolSignature : public PDFDocumentTool
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFDocumentToolSignature)

public:
    /// The two tools of the class
    enum class Mode
    {
        Sign,       ///< protect.sign (Figma 434:4338)
        Timestamp   ///< protect.timestamp (435:7037)
    };

    static constexpr const char* TOOL_ID_SIGN = "protect.sign";
    static constexpr const char* TOOL_ID_TIMESTAMP = "protect.timestamp";

    /// Names of the parameters
    static constexpr const char* PARAMETER_CERTIFICATE = "certificate";
    static constexpr const char* PARAMETER_CERTIFICATE_PASSWORD = "certificatePassword";
    static constexpr const char* PARAMETER_FORMAT = "format";
    static constexpr const char* PARAMETER_TYPE = "type";
    static constexpr const char* PARAMETER_APPEARANCE = "appearance";
    static constexpr const char* PARAMETER_DRAWING = "drawing";
    static constexpr const char* PARAMETER_FIELD = "field";
    static constexpr const char* PARAMETER_REASON = "reason";
    static constexpr const char* PARAMETER_CONTACT_INFO = "contactInfo";
    static constexpr const char* PARAMETER_TSA_URL = "tsaUrl";

    /// Ids of the options of format (owner answer Q6.1)
    static constexpr const char* FORMAT_PADES = "pades";
    static constexpr const char* FORMAT_PKCS7 = "pkcs7";

    /// Ids of the options of type (the menu Signature of 434:4951)
    static constexpr const char* TYPE_SIGNATURE = "signature";
    static constexpr const char* TYPE_SIGNATURE_TIMESTAMP = "signature-timestamp";

    /// Ids of the options of appearance (the menu Visible of 434:4951)
    static constexpr const char* APPEARANCE_VISIBLE = "visible";
    static constexpr const char* APPEARANCE_INVISIBLE = "invisible";

    /// Longest reason and contact info (4.1)
    static constexpr int MAXIMUM_TEXT_LENGTH = 512;

    /// Longest full name of a field
    static constexpr int MAXIMUM_FIELD_NAME_LENGTH = 256;

    /// Names of the data of the result (MCP)
    static constexpr const char* DATA_SUB_FILTER = "subFilter";                    ///< "ETSI.CAdES.detached", "adbe.pkcs7.detached", "ETSI.RFC3161"
    static constexpr const char* DATA_SIGNING_TIME = "signingTime";                ///< /M of the new signature (ISO 8601 UTC), Sign only
    static constexpr const char* DATA_TIMESTAMP_TIME = "timestampTime";            ///< genTime of the token (ISO 8601 UTC), when a timestamp was added
    static constexpr const char* DATA_TSA_NAME = "tsaName";                        ///< The name of the authority, when a timestamp was added
    static constexpr const char* DATA_TSA_TRUSTED = "tsaTrusted";                  ///< The authority is trusted (the trusted certificates of the context and AATL), when a timestamp was added
    static constexpr const char* DATA_EARLIER_SIGNATURES = "earlierSignatures";    ///< The states of the earlier signatures after the update ("valid", "invalid", ...), only states
    static constexpr const char* DATA_UNSAVED_CHANGES = "unsavedChanges";          ///< The result includes the unsaved changes of the tab (Q6.17)
    static constexpr const char* DATA_TIMESTAMP_ERROR = "timestampError";          ///< A failed run: the authority did not give the timestamp ("no-network", "timeout", "http-error", "invalid-response", "rejected", "invalid-url", "no-transport"; 434:8518, 435:8117 - P4 offers Try Again / Sign Without Timestamp)
    static constexpr const char* DATA_HTTP_STATUS = "httpStatus";                  ///< The HTTP status of the answer of a failed authority ("HTTP 503", 435:8117)

    /// Space reserved for the token of a document timestamp (B134: no trial request of the
    /// authority; the token of an authority with its chain is a few KiB, a larger one gets one more
    /// request - R6-28)
    static constexpr qint64 TIMESTAMP_TOKEN_RESERVE = 16 * 1024 + 2048;

    /// An empty signature field of a document (owner answer Q6.3, R6-25): the items "In field …" of
    /// the menu Visible (P4) and the check of the parameter field
    struct EmptySignatureField
    {
        QString name;                   ///< Fully qualified name of the field (the value of the parameter field)
        PDFObjectReference field;       ///< The field
        PDFObjectReference widget;      ///< Its first widget (null = none)
        PDFInteger pageIndex = -1;      ///< The page of the widget (-1 = none)
        QRectF rectangle;               ///< /Rect of the widget in the user space of its page (empty = an invisible widget)
        QString unusableReason;         ///< Translated reason, why the tool cannot sign it whatever the parameters are (/Lock of FieldMDP, a seed value the tool cannot meet - /SV /Ff, ISO 32000-2 Table 234), empty = usable
        bool isTimestampRequired = false;   ///< /SV /TimeStamp with /Ff 1: only a signature with a timestamp (Table 234)
        QStringList subFilters;         ///< /SV /SubFilter of a field, which requires it (/Ff bit 2), empty = any
        QStringList reasons;            ///< /SV /Reasons of a field, which requires them (/Ff bit 4), empty = any
    };

    /// The empty signature fields of the document (/FT /Sig without /V; one walk of the field tree with
    /// a visited set and a depth limit - PDFForm::parse), in the order of the fields
    static std::vector<EmptySignatureField> getEmptySignatureFields(const PDFDocument* document);

    /// Names of the facts of the result dialog (434:9276, 435:8356)
    static constexpr const char* FACT_SIGNER = "signer";                       ///< The common name of the signer
    static constexpr const char* FACT_TYPE = "type";                           ///< "Signature · visible on page 308"
    static constexpr const char* FACT_TIME = "time";                           ///< "Sep 30, 2026 · 14:32:08 UTC"
    static constexpr const char* FACT_AUTHORITY = "authority";                 ///< The name of the authority ("Authority not trusted" besides it, when so)
    static constexpr const char* FACT_EARLIER_SIGNATURES = "earlierSignatures"; ///< "Stay valid", "1 stays valid", "2 were invalid already"
    static constexpr const char* FACT_UNSAVED_CHANGES = "unsavedChanges";      ///< "Includes the unsaved changes" (Q6.17)

    explicit PDFDocumentToolSignature(Mode mode);

    Mode getMode() const { return m_mode; }

    /// The id of the tool of a mode (TOOL_ID_*)
    static QString getToolId(Mode mode);

    /// The mode of a tool id, std::nullopt for another id
    static std::optional<Mode> findMode(QStringView toolId);

    /// The format of an id of the parameter format (FORMAT_*); PAdES for another id
    static PDFSignatureFormat getFormat(const QString& formatId);

    virtual QString getId() const override;
    virtual PDFDocumentToolParameterDefinitions getParameters() const override;
    virtual std::vector<PDFSecurityHandler::Permission> getRequiredPermissions(const QVariantMap& parameters) const override;
    virtual std::vector<PDFSecurityHandler::Permission> getAnyOfPermissions(const QVariantMap& parameters) const override;
    virtual std::vector<PDFDocumentToolParameterError> validate(const PDFDocumentToolContext& context, const QVariantMap& parameters) const override;
    virtual PDFDocumentToolResult execute(const PDFDocumentToolContext& context, const QVariantMap& parameters, const PDFOperationControl* control, PDFProgressSink* progress) const override;

private:
    Mode m_mode;
};

}   // namespace pdf

#endif // PDFDOCUMENTTOOL_SIGNATURE_H
