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

#ifndef PDFAPPMCPCONFIRMATION_H
#define PDFAPPMCPCONFIRMATION_H

#include "pdfappglobal.h"
#include "pdfappmcppermissions.h"

#include "pdfmcpcall.h"

#include <QJsonObject>
#include <QPointer>
#include <QString>
#include <QStringList>

#include <memory>

namespace pdf
{

class PDFAppDocument;

/// What a confirmation asks for (the variants of plan 5.4)
enum class PDFAppMcpConfirmationKind
{
    ReadDocuments,  ///< The read approval 966:150610
    Change,         ///< A change of the document (annotations, forms, outline, a tool) 940:133693
    Save,           ///< Save over the own file 940:134248
    SaveCopy,       ///< Save a copy 958:27539
    Open,           ///< Open a file 940:136305
    Close           ///< Close a tab (no frame in Figma)
};

/// How a confirmation ended (plan 2.5; the activity status follows it)
enum class PDFAppMcpDecision
{
    Allowed,            ///< The user allowed it (activity "allowed")
    DeniedUser,         ///< The user denied it (activity "denied_user")
    DeniedTimeout,      ///< No answer in 2 minutes (activity "denied_timeout")
    DeniedQueueTimeout, ///< Its dialog did not appear within 10 minutes of waiting - other requests, a hidden tab, a modal dialog (activity "denied_timeout" with the detail reason "queue", item 59)
    DeniedClosed,       ///< The tab was closed (activity "denied_closed")
    DeniedDisconnected, ///< The assistant was disconnected (activity "denied_disconnected")
    DeniedStopped,      ///< The server stopped (activity "denied_stopped")
    Canceled,           ///< The assistant canceled the call (activity "canceled")
    Unavailable         ///< No confirmation interface is installed (wave 0, a headless instance)
};

/// Receiver of the end of a confirmation (the operation of the call, held by
/// std::shared_ptr; the host keeps a weak pointer)
class PDF4QTLIBAPPSHARED_EXPORT PDFAppMcpConfirmationReceiver
{
public:
    virtual ~PDFAppMcpConfirmationReceiver();

    /// The confirmation ended (GUI thread, never inside of the call of the host,
    /// which started it - always from the event loop)
    virtual void confirmationFinished(quint64 confirmationId, PDFAppMcpDecision decision) = 0;
};

/// One question to the user (plan 5.4). The model of package C: the host
/// queues it, the provider (the dialogs of package C) shows it, the host
/// resolves it and tells the receiver. Texts written by the model (titles,
/// contents, paths) are plain text shown as written (sanitized, elided).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppMcpConfirmation
{
public:
    quint64 id = 0;                         ///< Unique in the process (set by the host)
    PDFAppMcpConfirmationKind kind = PDFAppMcpConfirmationKind::Change;
    PDFAppMcpPermissionKinds permissions;   ///< The permissions set to Ask, which the request needs (the reason row)
    std::shared_ptr<PDFMcpCall> call;
    QPointer<PDFAppDocument> document;      ///< The document of the request (nullptr for Open / ReadDocuments without a tab)
    QString assistantKey;
    QString clientName;                     ///< Display name of the assistant
    QString clientNameRaw;                  ///< clientInfo.name (monogram, hue - PDFAppMcpClientNames)
    QString remoteAddress;
    bool isRemote = false;
    qint64 askedAt = 0;                     ///< ms since the epoch
    bool isAfterDialog = false;             ///< Asked because a dialog was open when the request came (958:28030)
    bool isAlwaysConfirmed = false;         ///< Overwriting the original file is always confirmed (958:28030)
    QString actionId;                       ///< "annotations.add"
    QJsonObject details;                    ///< Rows of the body (package C defines the keys per kind)
    qint64 activityId = 0;                  ///< Local id of the waiting activity row
    std::weak_ptr<PDFAppMcpConfirmationReceiver> receiver;
    std::shared_ptr<PDFAppMcpConfirmationReceiver> receiverHolder;  ///< Keeps a receiver alive, which nobody else holds (released at the end)
};

/// The user interface of the confirmations (package C installs it into the
/// host: the dialogs per tab, the queue, the status items, the Home banner).
/// Without a provider every confirmation ends as Unavailable.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppMcpConfirmationProvider
{
public:
    virtual ~PDFAppMcpConfirmationProvider();

    /// Shows (queues) the confirmation; the answer comes by PDFAppMcpHost::resolveConfirmation
    virtual void showConfirmation(std::shared_ptr<PDFAppMcpConfirmation> confirmation) = 0;

    /// The confirmation ended without the user (canceled, disconnected, server
    /// stopped, tab closed): its dialog is withdrawn
    virtual void withdrawConfirmation(quint64 confirmationId, PDFAppMcpDecision decision) = 0;
};

}   // namespace pdf

#endif // PDFAPPMCPCONFIRMATION_H
