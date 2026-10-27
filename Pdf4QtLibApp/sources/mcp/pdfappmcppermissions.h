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

#ifndef PDFAPPMCPPERMISSIONS_H
#define PDFAPPMCPPERMISSIONS_H

#include "pdfappglobal.h"

#include <QFlags>
#include <QSet>
#include <QString>

#include <optional>

namespace pdf
{

class PDFSettingsRepository;

/// Value of a permission of the AI page (settings mcp.permission*)
enum class PDFAppMcpPermission
{
    Allow,
    Ask,
    Deny
};

/// Which permission an action needs (plan 3.0: R / E / F)
enum class PDFAppMcpPermissionKind
{
    None = 0x00,    ///< No permission (application.get_state - its document list needs R)
    Read = 0x01,    ///< Read documents
    Edit = 0x02,    ///< Edit documents
    Files = 0x04    ///< Open and save files
};
Q_DECLARE_FLAGS(PDFAppMcpPermissionKinds, PDFAppMcpPermissionKind)

/// The three permissions of the AI page (one global set for every assistant:
/// the catalog "MAY vary by the authorization", never per connection)
class PDF4QTLIBAPPSHARED_EXPORT PDFAppMcpPermissions
{
public:
    PDFAppMcpPermission read = PDFAppMcpPermission::Allow;
    PDFAppMcpPermission edit = PDFAppMcpPermission::Ask;
    PDFAppMcpPermission files = PDFAppMcpPermission::Ask;

    /// Permissions of the settings (hostile stored values are the defaults)
    static PDFAppMcpPermissions fromSettings(const PDFSettingsRepository& settings);

    /// Every permission set to \p permission (tests)
    static PDFAppMcpPermissions createAll(PDFAppMcpPermission permission);

    /// Permission of the kind (Allow for None)
    PDFAppMcpPermission get(PDFAppMcpPermissionKind kind) const;
    void set(PDFAppMcpPermissionKind kind, PDFAppMcpPermission permission);

    /// The kind is not denied
    bool isAvailable(PDFAppMcpPermissionKind kind) const { return get(kind) != PDFAppMcpPermission::Deny; }

    /// Name of a permission ("allow", "ask", "deny" - the values of the settings and of get_state)
    static QString getName(PDFAppMcpPermission permission);

    /// Permission of a name, std::nullopt for an unknown one
    static std::optional<PDFAppMcpPermission> fromName(const QString& name);

    bool operator==(const PDFAppMcpPermissions&) const = default;
};

/// Result of the permission policy for one request
enum class PDFAppMcpPolicyResult
{
    Allowed,    ///< Every needed permission is Allow (or approved)
    Ask,        ///< At least one needs the confirmation of the user, none is denied
    Denied      ///< At least one needed permission is Deny
};

/// Permission policy and the read approvals [D2] (plan 2.7): Read = Ask is
/// approved once per assistant key until the assistant goes idle, is
/// disconnected or its session ends; Edit / Files = Ask are confirmed for every
/// request. Deny of the read approval answers only that request (O21).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppMcpPermissionPolicy
{
public:
    void setPermissions(const PDFAppMcpPermissions& permissions) { m_permissions = permissions; }
    const PDFAppMcpPermissions& getPermissions() const { return m_permissions; }

    /// Policy for a request of the assistant, which needs the \p kinds.
    /// \p hasOpenDocuments: a read approval is needed only while a document is open.
    PDFAppMcpPolicyResult evaluate(PDFAppMcpPermissionKinds kinds, const QString& assistantKey, bool hasOpenDocuments) const;

    /// Kinds of \p kinds, which need the confirmation of the user
    PDFAppMcpPermissionKinds getKindsToConfirm(PDFAppMcpPermissionKinds kinds, const QString& assistantKey, bool hasOpenDocuments) const;

    /// The read approval of the assistant
    bool isReadApproved(const QString& assistantKey) const { return m_readApprovals.contains(assistantKey); }
    void approveRead(const QString& assistantKey);
    void forgetRead(const QString& assistantKey);
    void forgetAll() { m_readApprovals.clear(); }

private:
    PDFAppMcpPermissions m_permissions;
    QSet<QString> m_readApprovals;
};

Q_DECLARE_OPERATORS_FOR_FLAGS(PDFAppMcpPermissionKinds)

}   // namespace pdf

#endif // PDFAPPMCPPERMISSIONS_H
