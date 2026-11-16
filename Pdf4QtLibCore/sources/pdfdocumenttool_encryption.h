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

#ifndef PDFDOCUMENTTOOL_ENCRYPTION_H
#define PDFDOCUMENTTOOL_ENCRYPTION_H

#include "pdfdocumenttool.h"
#include "pdfsecurityhandler.h"

#include <optional>
#include <utility>
#include <vector>

namespace pdf
{

/// The four tools of the subgroup Encryption in one class of the core (plan of step 5 of phase 2,
/// 4.1, as changed by chapters 12 and 13; package P1b): Protect with Password (protect.password),
/// Restrict Permissions (protect.permissions), Encrypt with Certificate (protect.certificate-encrypt)
/// and Remove Password (protect.unlock). One instance per mode is registered (the pattern of one class
/// for more ids). The skeleton fixed the ids, the modes, the parameters and the data of the result;
/// package P1b implemented the run (changeEncryption, the one implementation of PdfTool too).
///
/// Parameters (the names are PARAMETER_*):
///   - algorithm (Enum aes-256 / aes-128 / rc4, default aes-256; Password, Permissions): AES 256-bit
///     writes revision 6, AES 128-bit and RC4 128-bit revision 4 (ISO 32000-2, 7.6.4; revisions 2 - 5
///     are deprecated in PDF 2.0, K5.16 - kept for older readers, tools.md 5.4);
///   - scope (Enum everything / except-metadata / attachments, default everything; Password,
///     Permissions, Certificate): ISO 32000-2, 7.6.6 (PDFSecurityHandlerFactory::EncryptContents);
///   - openPassword (Secret; Password - needed -, Permissions with requireOpenPassword);
///   - ownerSameAsOpen (Bool, default true; Password): the open password gives the owner access too;
///     owner answer Q5.1: a document, which keeps restrictions (keepRestrictions), needs its own owner
///     password - the validation refuses the same password then;
///   - ownerPassword (Secret; Permissions - needed, different from the open password (432:3721) -,
///     Password without ownerSameAsOpen);
///   - requireOpenPassword (Bool, default false; Permissions);
///   - keepRestrictions (Bool, default true; Password): the permission bits (/P) of an encrypted input
///     stay (only the bits: the old owner password is never kept, K5.19);
///   - printLow, printHigh, modify, assemble, annotate, fillForms, copy (Bool; Permissions with the
///     defaults of the preset View and print, Certificate with every one allowed): the bits 3, 12, 4,
///     11, 6, 9, 5 of Table 22 (bit 10, the accessibility, is always set: K5.7, owner answer Q5.2);
///   - certificate (String, the SHA-256 fingerprint of the certificate as 64 lower case hexadecimal
///     digits; Certificate; MCP-excluded, the confirmation of PDF4QT gives it, owner answer Q5.6);
///   - certificatePassword (Secret; Certificate): the password of the PKCS#12 file; a certificate of the
///     system store (PDFSecurityHandlerFactory::isSystemStoreCertificate, owner 2026-11-15) needs none:
///     only its public certificate is the recipient, the run never uses its key;
///   - Remove Password has no parameters.
/// Every Secret is MCP-excluded (PDFDocumentToolParameterDefinition::isMcpExcluded): it is never in
/// the schema of an action nor in a result, never stored, never in the parameter model (S5.2); the
/// GUI and the confirmation of MCP give it only to the run. No Secret is setRequired (the model never
/// holds a password): the validation reports an empty needed password.
///
/// The run (P1b, plan 4.1 execute with R5-5): needs the owner authorization of the document or no
/// encryption (ISO 32000-2, 7.6.4.1; a certificate only with bit 2 of its recipient, Table 24, owner
/// answer Q5.3 b; the catalog entry isSecurityChange); /ID first (an existing /ID[0] stays), then the
/// handler of PDFSecurityHandlerFactory (SecuritySettings::id = /ID[0]); the old /Encrypt is removed
/// (B86), the declaration of PDF/A is removed before the encryption, PDF/UA stays (R5-6, Q5.7); a check
/// written into the memory is read back by the new password; the whole file is written
/// (LostCopyOnly, B80). Progress by PDFProgressSink, Stop by the objects.
///
/// Result (Success): the document, the facts of the -done frames (FACT_*) and the data of MCP
/// (DATA_*: algorithm, revision, scope, permissions - the ids of getPermissionIds -,
/// openPasswordRequired, ownerPasswordSeparate). Never a password, a key or a path of a certificate.
class PDF4QTLIBCORESHARED_EXPORT PDFDocumentToolEncryption : public PDFDocumentTool
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFDocumentToolEncryption)

public:
    /// The four tools of the class
    enum class Mode
    {
        Password,       ///< protect.password (Figma 430:1068)
        Permissions,    ///< protect.permissions (432:2105)
        Certificate,    ///< protect.certificate-encrypt (433:3049)
        Unlock          ///< protect.unlock (433:5022)
    };

    static constexpr const char* TOOL_ID_PASSWORD = "protect.password";
    static constexpr const char* TOOL_ID_PERMISSIONS = "protect.permissions";
    static constexpr const char* TOOL_ID_CERTIFICATE = "protect.certificate-encrypt";
    static constexpr const char* TOOL_ID_UNLOCK = "protect.unlock";

    /// Names of the parameters
    static constexpr const char* PARAMETER_ALGORITHM = "algorithm";
    static constexpr const char* PARAMETER_SCOPE = "scope";
    static constexpr const char* PARAMETER_OPEN_PASSWORD = "openPassword";
    static constexpr const char* PARAMETER_OWNER_SAME_AS_OPEN = "ownerSameAsOpen";
    static constexpr const char* PARAMETER_OWNER_PASSWORD = "ownerPassword";
    static constexpr const char* PARAMETER_REQUIRE_OPEN_PASSWORD = "requireOpenPassword";
    static constexpr const char* PARAMETER_KEEP_RESTRICTIONS = "keepRestrictions";
    static constexpr const char* PARAMETER_PRINT_LOW = "printLow";
    static constexpr const char* PARAMETER_PRINT_HIGH = "printHigh";
    static constexpr const char* PARAMETER_MODIFY = "modify";
    static constexpr const char* PARAMETER_ASSEMBLE = "assemble";
    static constexpr const char* PARAMETER_ANNOTATE = "annotate";
    static constexpr const char* PARAMETER_FILL_FORMS = "fillForms";
    static constexpr const char* PARAMETER_COPY = "copy";
    static constexpr const char* PARAMETER_CERTIFICATE = "certificate";
    static constexpr const char* PARAMETER_CERTIFICATE_PASSWORD = "certificatePassword";

    /// Ids of the options of algorithm (the menu Algorithm of 430:1497)
    static constexpr const char* ALGORITHM_AES_256 = "aes-256";
    static constexpr const char* ALGORITHM_AES_128 = "aes-128";
    static constexpr const char* ALGORITHM_RC4 = "rc4";

    /// Ids of the options of scope (the menu Encrypt of 430:1497)
    static constexpr const char* SCOPE_EVERYTHING = "everything";
    static constexpr const char* SCOPE_EXCEPT_METADATA = "except-metadata";
    static constexpr const char* SCOPE_ATTACHMENTS = "attachments";

    /// Names of the data of the result (MCP, the result dialog)
    static constexpr const char* DATA_ALGORITHM = "algorithm";                          ///< aes-256 / aes-128 / rc4 / certificate / none (Remove Password)
    static constexpr const char* DATA_REVISION = "revision";                            ///< /R of the standard handler (4, 6), 0 for a certificate and for none
    static constexpr const char* DATA_SCOPE = "scope";                                  ///< An id of scope (empty for Remove Password)
    static constexpr const char* DATA_PERMISSIONS = "permissions";                      ///< The allowed permissions, ids of getPermissionIds in the order of Table 22
    static constexpr const char* DATA_OPEN_PASSWORD_REQUIRED = "openPasswordRequired";  ///< The result asks for a password (or the private key) to open; false for File attachments only (the document opens freely, RS5SEC L5)
    static constexpr const char* DATA_OWNER_PASSWORD_SEPARATE = "ownerPasswordSeparate"; ///< The owner password differs from the open password

    /// Names of the facts of the result dialog (430:3226, 432:4388, 433:4721, 433:5999)
    static constexpr const char* FACT_ENCRYPTION = "encryption";        ///< "AES 256-bit · everything"
    static constexpr const char* FACT_OPEN_PASSWORD = "openPassword";   ///< "Required" / "Not required"
    static constexpr const char* FACT_ALLOWED = "allowed";              ///< "Printing, filling in forms"
    static constexpr const char* FACT_NOT_ALLOWED = "notAllowed";       ///< "Copying, changes"
    static constexpr const char* FACT_OPENS_FOR = "opensFor";           ///< The holder of the certificate (its name, never a path)
    static constexpr const char* FACT_ENCRYPTED = "encrypted";          ///< "Everything, including metadata" (the scope, 430:3226, 433:4721)
    static constexpr const char* FACT_PERMISSIONS = "permissions";      ///< "All allowed" (Remove Password, 433:5999)
    static constexpr const char* FACT_NO_ATTACHMENTS = "noAttachments"; ///< File attachments only of a document without attachments: nothing is protected (RS5SEC L5)

    /// Presets of Restrict Permissions (the menu Preset of 432:2698): a preset only checks the list,
    /// it is no parameter (the pattern of Remove Hidden Data); Custom = the values match no preset
    enum class Preset
    {
        ViewOnly,               ///< Nothing allowed but the accessibility (1 of 8)
        ViewAndPrint,           ///< Printing in low and high resolution (3 of 8, 432:2105, the default)
        PrintAndFillForms,      ///< Printing and filling in forms (4 of 8, 432:3072)
        AllowEverything,        ///< Every permission (8 of 8, 432:4069)
        Custom                  ///< The values match no preset
    };

    explicit PDFDocumentToolEncryption(Mode mode);

    Mode getMode() const { return m_mode; }

    /// The id of the tool of a mode (TOOL_ID_*)
    static QString getToolId(Mode mode);

    /// The mode of a tool id, std::nullopt for another id
    static std::optional<Mode> findMode(QStringView toolId);

    /// The parameters of the permissions and their bits (Table 22) in the order of the list of
    /// 432:2105 - print low, print high, modify, assemble, annotate, fill forms, copy
    static std::vector<std::pair<const char*, PDFSecurityHandler::Permission>> getPermissionParameters();

    /// The ids of the permissions in the data of MCP (DATA_PERMISSIONS) and their bits: print-low,
    /// print-high, modify, assemble, annotate, fill-forms, copy, accessibility
    static std::vector<std::pair<const char*, PDFSecurityHandler::Permission>> getPermissionIds();

    /// The fingerprint of a certificate - the value of the parameter certificate and the key of the
    /// certificate cache of the application (PDFCertificateCacheRepository): SHA-256 of its DER,
    /// 64 lower case hexadecimal digits (empty data give the hash of nothing, never a match of a
    /// certificate)
    static QString getCertificateFingerprint(const QByteArray& certificateData);

    /// The values of the permission parameters of a preset (Custom: every one off)
    static QVariantMap getPresetValues(Preset preset);

    /// The preset of the values of the permission parameters (the first match wins)
    static Preset findPreset(const QVariantMap& values);

    /// The result of changeEncryption
    struct Change
    {
        PDFDocument document;                       ///< The document with the new security handler (valid when \p error is empty and not \p isStopped)
        QByteArray writtenData;                     ///< The file of the check: written whole and read back by the new password
        QString error;                              ///< Translated reason of a failure; never a password
        bool isStopped = false;                     ///< The run was canceled
        QStringList conformanceRemoved;             ///< The standards no longer declared (PDF/A, R5-6)
        int removedCryptFilters = 0;                ///< Streams, whose /Crypt filter was decoded (R5-10)
        int typedEmbeddedFiles = 0;                 ///< Embedded file streams, which got /Type /EmbeddedFile (R5-3)
        int embeddedFiles = 0;                      ///< Embedded file streams of the document (an encryption; R5-3)
        int removedCheckSums = 0;                   ///< Parameters of embedded files, whose /CheckSum was removed (File attachments only, owner 2026-11-15)
    };

    /// The one implementation of a change of the encryption (the tools and PdfTool encrypt /
    /// decrypt, plan of step 5 of phase 2, 4.1, 4.4, 4.7; R5-3, R5-5, R5-6, R5-10, B85, B86, B101):
    ///  1. the security of the input may be changed (isSecurityChangeAllowed), its attachments are
    ///     not locked (Open without attachments, Q5.9 c);
    ///  2. an encryption: /ID first (an existing /ID[0] stays) - \p settings.id is set to /ID[0];
    ///  3. no stream keeps a /Crypt step (the new /Encrypt does not know the old crypt filters,
    ///     7.4.10): the reader has decrypted the streams of their own crypt filters, a step Identity
    ///     is dropped, a step the reader could not decrypt fails the run with its reason;
    ///  4. an encryption: every embedded file stream of a file specification (/EF and /RF, Table
    ///     43, 7.6.6: the same crypt filter) gets /Type /EmbeddedFile, so File attachments only
    ///     encrypts it (Table 44 makes /Type optional); the declaration of PDF/A goes, PDF/UA stays;
    ///  5. the containers of the file structure go (the whole file is written), the version stays;
    ///  6. the handler of PDFSecurityHandlerFactory (validated, with the reason of a failure) or none;
    ///     the old /Encrypt goes (B86);
    ///  7. the check: the file is written into the memory and read back - by the owner password and
    ///     the open password (standard handler), by the certificate (public-key handler), without a
    ///     password (removal); a result, which cannot be opened so, is never given.
    /// Reentrant: only its arguments, no state; the passwords of \p settings are cleared.
    /// \param input The document (owner authorized)
    /// \param settings The new encryption; algorithm None = remove the encryption
    /// \param control Cancellation (may be null)
    /// \param progress Progress of the run (may be null)
    static Change changeEncryption(const PDFDocument* input, PDFSecurityHandlerFactory::SecuritySettings settings, const PDFOperationControl* control, PDFProgressSink* progress);

    /// The security of the document may be changed (ISO 32000-2, 7.6.4.1: only the owner; Table 24:
    /// a recipient of a certificate only with bit 2; plan R5-4, owner answer Q5.3 b): a document
    /// without encryption, a standard handler authorized as the owner, a public-key handler, whose
    /// recipient has bit 2. setNoDRMMode never counts.
    static bool isSecurityChangeAllowed(const PDFDocument* document);

    /// The permissions (mask of PDFSecurityHandler::Permission) the document gives to a user, who is
    /// not its owner (its /P, Table 22 - revision 2 by its bits, Table 24 of a recipient); every
    /// permission for a document without encryption
    static uint32_t getDocumentPermissions(const PDFDocument* document);

    /// The mask of every permission of Table 22, which a tool writes (bits 3, 4, 5, 6, 9, 10, 11, 12)
    static uint32_t getAllPermissions();

    /// The name of the encryption of a document: "None", "AES 256-bit", "AES 128-bit",
    /// "RC4 128-bit", "RC4 40-bit", "Certificate · AES 256-bit" (the rows Encryption of the panels)
    static QString getEncryptionName(const PDFDocument* document);

    /// The texts of the facts (FACT_*), one for the core and the panels of the application:

    /// The value of the fact Open password: "Not required" without a password, "Required" with
    /// it; File attachments only (7.6.6: /StmF and /StrF Identity) opens without the password, only
    /// its attachments need it - "Not required · attachments need the password" (review RS5SEC L5)
    /// \param isPasswordSet The document (or the result) is protected by a password or a certificate
    /// \param isAttachmentsOnly Only the embedded files are encrypted
    static QString getOpenPasswordText(bool isPasswordSet, bool isAttachmentsOnly);

    /// "All allowed" - every permission of Table 22 (the fact Permissions of Remove Password, 433:5999)
    static QString getAllAllowedText();

    /// "None" - no encryption (getEncryptionName of a document without encryption)
    static QString getNoEncryptionText();

    virtual QString getId() const override;
    virtual PDFDocumentToolParameterDefinitions getParameters() const override;
    virtual std::vector<PDFDocumentToolParameterError> validate(const PDFDocumentToolContext& context, const QVariantMap& parameters) const override;
    virtual PDFDocumentToolResult execute(const PDFDocumentToolContext& context, const QVariantMap& parameters, const PDFOperationControl* control, PDFProgressSink* progress) const override;

private:
    /// The settings of the factory of a run (the passwords are copies of their own, the caller
    /// clears them); the errors of the parameters, when they cannot be made
    PDFSecurityHandlerFactory::SecuritySettings createSettings(const PDFDocumentToolContext& context, const QVariantMap& parameters, std::vector<PDFDocumentToolParameterError>* errors) const;

    Mode m_mode;
};

}   // namespace pdf

#endif // PDFDOCUMENTTOOL_ENCRYPTION_H
