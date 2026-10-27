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

#include "pdfappmcppermissions.h"
#include "pdfsettingdefinitions.h"
#include "pdfsettingsrepository.h"

namespace pdf
{

PDFAppMcpPermissions PDFAppMcpPermissions::fromSettings(const PDFSettingsRepository& settings)
{
    PDFAppMcpPermissions permissions;
    permissions.read = fromName(settings.getString(PDFSettingKeys::mcpPermissionRead())).value_or(PDFAppMcpPermission::Allow);
    permissions.edit = fromName(settings.getString(PDFSettingKeys::mcpPermissionEdit())).value_or(PDFAppMcpPermission::Ask);
    permissions.files = fromName(settings.getString(PDFSettingKeys::mcpPermissionFiles())).value_or(PDFAppMcpPermission::Ask);
    return permissions;
}

PDFAppMcpPermissions PDFAppMcpPermissions::createAll(PDFAppMcpPermission permission)
{
    PDFAppMcpPermissions permissions;
    permissions.read = permission;
    permissions.edit = permission;
    permissions.files = permission;
    return permissions;
}

PDFAppMcpPermission PDFAppMcpPermissions::get(PDFAppMcpPermissionKind kind) const
{
    switch (kind)
    {
        case PDFAppMcpPermissionKind::Read:
            return read;
        case PDFAppMcpPermissionKind::Edit:
            return edit;
        case PDFAppMcpPermissionKind::Files:
            return files;
        case PDFAppMcpPermissionKind::None:
            break;
    }
    return PDFAppMcpPermission::Allow;
}

void PDFAppMcpPermissions::set(PDFAppMcpPermissionKind kind, PDFAppMcpPermission permission)
{
    switch (kind)
    {
        case PDFAppMcpPermissionKind::Read:
            read = permission;
            break;
        case PDFAppMcpPermissionKind::Edit:
            edit = permission;
            break;
        case PDFAppMcpPermissionKind::Files:
            files = permission;
            break;
        case PDFAppMcpPermissionKind::None:
            break;
    }
}

QString PDFAppMcpPermissions::getName(PDFAppMcpPermission permission)
{
    switch (permission)
    {
        case PDFAppMcpPermission::Allow:
            return PDFSettingValues::permissionAllow();
        case PDFAppMcpPermission::Ask:
            return PDFSettingValues::permissionAsk();
        case PDFAppMcpPermission::Deny:
            return PDFSettingValues::permissionDeny();
    }
    return PDFSettingValues::permissionDeny();
}

std::optional<PDFAppMcpPermission> PDFAppMcpPermissions::fromName(const QString& name)
{
    for (PDFAppMcpPermission permission : { PDFAppMcpPermission::Allow, PDFAppMcpPermission::Ask, PDFAppMcpPermission::Deny })
    {
        if (getName(permission) == name)
        {
            return permission;
        }
    }
    return std::nullopt;
}

PDFAppMcpPermissionKinds PDFAppMcpPermissionPolicy::getKindsToConfirm(PDFAppMcpPermissionKinds kinds, const QString& assistantKey, bool hasOpenDocuments) const
{
    PDFAppMcpPermissionKinds result;
    for (PDFAppMcpPermissionKind kind : { PDFAppMcpPermissionKind::Read, PDFAppMcpPermissionKind::Edit, PDFAppMcpPermissionKind::Files })
    {
        if (!kinds.testFlag(kind) || m_permissions.get(kind) != PDFAppMcpPermission::Ask)
        {
            continue;
        }

        // The read approval: once per assistant, and only while a document is open
        if (kind == PDFAppMcpPermissionKind::Read && (!hasOpenDocuments || isReadApproved(assistantKey)))
        {
            continue;
        }
        result |= kind;
    }
    return result;
}

PDFAppMcpPolicyResult PDFAppMcpPermissionPolicy::evaluate(PDFAppMcpPermissionKinds kinds, const QString& assistantKey, bool hasOpenDocuments) const
{
    for (PDFAppMcpPermissionKind kind : { PDFAppMcpPermissionKind::Read, PDFAppMcpPermissionKind::Edit, PDFAppMcpPermissionKind::Files })
    {
        if (kinds.testFlag(kind) && m_permissions.get(kind) == PDFAppMcpPermission::Deny)
        {
            return PDFAppMcpPolicyResult::Denied;
        }
    }

    return getKindsToConfirm(kinds, assistantKey, hasOpenDocuments) ? PDFAppMcpPolicyResult::Ask : PDFAppMcpPolicyResult::Allowed;
}

void PDFAppMcpPermissionPolicy::approveRead(const QString& assistantKey)
{
    if (!assistantKey.isEmpty())
    {
        m_readApprovals.insert(assistantKey);
    }
}

void PDFAppMcpPermissionPolicy::forgetRead(const QString& assistantKey)
{
    m_readApprovals.remove(assistantKey);
}

}   // namespace pdf
