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

#include "pdfappmcpchange.h"
#include "pdfappmcpconfirmationui.h"
#include "pdfappmcphost.h"
#include "pdfappmcptoolset.h"
#include "pdfappdocument.h"
#include "pdfappdocumentsave.h"

#include "pdfmcpcatalog.h"
#include "pdfmcpjson.h"
#include "pdfmcpschema.h"

#include <QApplication>
#include <QJsonArray>

#include <cmath>
#include <limits>

namespace pdf
{

/// Helpers of the changes of the assistants
class PDFAppMcpChangeHelper
{
public:
    PDFAppMcpChangeHelper() = delete;

    /// Number of the items, from which a change is a heavy job
    static constexpr int HEAVY_CHANGE_COUNT = 50;

    static QJsonArray pointsToJson(const QPolygonF& points)
    {
        QJsonArray array;
        for (const QPointF& point : points)
        {
            array.append(point.x());
            array.append(point.y());
        }
        return array;
    }

    static QPolygonF pointsFromJson(const QJsonArray& array)
    {
        QPolygonF points;
        const qsizetype count = qMin<qsizetype>(array.size() / 2, PDFAppMcpGhost::MAXIMUM_POINTS);
        points.reserve(count);
        for (qsizetype i = 0; i < count; ++i)
        {
            const double x = array.at(2 * i).toDouble(qQNaN());
            const double y = array.at(2 * i + 1).toDouble(qQNaN());
            if (!std::isfinite(x) || !std::isfinite(y) || std::abs(x) > 1.0e7 || std::abs(y) > 1.0e7)
            {
                return QPolygonF();
            }
            points.push_back(QPointF(x, y));
        }
        return points;
    }

    static QString getShapeName(PDFAppMcpGhost::Shape shape)
    {
        switch (shape)
        {
            case PDFAppMcpGhost::Shape::Quads:
                return QStringLiteral("quads");
            case PDFAppMcpGhost::Shape::Rectangle:
                return QStringLiteral("rectangle");
            case PDFAppMcpGhost::Shape::Ellipse:
                return QStringLiteral("ellipse");
            case PDFAppMcpGhost::Shape::Note:
                return QStringLiteral("note");
            case PDFAppMcpGhost::Shape::Polyline:
                return QStringLiteral("polyline");
            case PDFAppMcpGhost::Shape::Polygon:
                return QStringLiteral("polygon");
            case PDFAppMcpGhost::Shape::Ink:
                return QStringLiteral("ink");
        }
        return QString();
    }

    static std::optional<PDFAppMcpGhost::Shape> getShape(const QString& name)
    {
        for (PDFAppMcpGhost::Shape shape : { PDFAppMcpGhost::Shape::Quads, PDFAppMcpGhost::Shape::Rectangle, PDFAppMcpGhost::Shape::Ellipse,
                                             PDFAppMcpGhost::Shape::Note, PDFAppMcpGhost::Shape::Polyline, PDFAppMcpGhost::Shape::Polygon,
                                             PDFAppMcpGhost::Shape::Ink })
        {
            if (getShapeName(shape) == name)
            {
                return shape;
            }
        }
        return std::nullopt;
    }

    /// Writes the members of an output object
    static void writeMembers(PDFMcpJsonWriter& writer, const PDFAppMcpOutputMembers& members)
    {
        for (const auto& member : members)
        {
            writer.key(QStringView(member.first));
            writer.writeQJson(member.second);
        }
    }
};

QJsonObject PDFAppMcpChangeRow::toJson() const
{
    QJsonObject object;
    object.insert(QStringLiteral("icon"), iconName);
    object.insert(QStringLiteral("text"), text);
    object.insert(QStringLiteral("page"), page);
    object.insert(QStringLiteral("detail"), detail);
    return object;
}

PDFAppMcpChangeRow PDFAppMcpChangeRow::fromJson(const QJsonObject& object)
{
    // Texts of the model are bounded again (the dialog elides them as well)
    PDFAppMcpChangeRow row;
    row.iconName = object.value(QStringLiteral("icon")).toString().left(64);
    row.text = PDFMcpJson::sanitizeString(object.value(QStringLiteral("text")).toString(), PDFMcpJson::SanitizeFlag::RemoveBidi, 200);
    row.page = PDFMcpJson::sanitizeString(object.value(QStringLiteral("page")).toString(), PDFMcpJson::SanitizeFlag::RemoveBidi, 100);
    row.detail = PDFMcpJson::sanitizeString(object.value(QStringLiteral("detail")).toString(), PDFMcpJson::SanitizeFlag::RemoveBidi, 400);
    return row;
}

QJsonObject PDFAppMcpGhost::toJson() const
{
    QJsonObject object;
    object.insert(QStringLiteral("page"), QJsonValue(qint64(pageIndex)));
    object.insert(QStringLiteral("shape"), PDFAppMcpChangeHelper::getShapeName(shape));
    object.insert(QStringLiteral("points"), PDFAppMcpChangeHelper::pointsToJson(points));
    if (!strokes.empty())
    {
        QJsonArray array;
        for (const QPolygonF& stroke : strokes)
        {
            array.append(PDFAppMcpChangeHelper::pointsToJson(stroke));
        }
        object.insert(QStringLiteral("strokes"), array);
    }
    object.insert(QStringLiteral("color"), color.isValid() ? color.name(QColor::HexRgb) : QString());
    return object;
}

std::optional<PDFAppMcpGhost> PDFAppMcpGhost::fromJson(const QJsonObject& object)
{
    PDFAppMcpGhost ghost;
    const std::optional<Shape> shape = PDFAppMcpChangeHelper::getShape(object.value(QStringLiteral("shape")).toString());
    const double page = object.value(QStringLiteral("page")).toDouble(-1.0);
    if (!shape || !(page >= 0.0) || page > 1.0e9)
    {
        return std::nullopt;
    }

    ghost.shape = *shape;
    ghost.pageIndex = PDFInteger(page);
    ghost.points = PDFAppMcpChangeHelper::pointsFromJson(object.value(QStringLiteral("points")).toArray());

    const QJsonArray strokes = object.value(QStringLiteral("strokes")).toArray();
    int pointCount = int(ghost.points.size());
    for (const QJsonValue& value : strokes)
    {
        QPolygonF stroke = PDFAppMcpChangeHelper::pointsFromJson(value.toArray());
        pointCount += int(stroke.size());
        if (pointCount > MAXIMUM_POINTS)
        {
            break;
        }
        if (!stroke.isEmpty())
        {
            ghost.strokes.push_back(std::move(stroke));
        }
    }

    ghost.color = QColor(object.value(QStringLiteral("color")).toString().left(16));
    if (ghost.points.isEmpty() && ghost.strokes.empty())
    {
        return std::nullopt;
    }
    return ghost;
}

QRectF PDFAppMcpGhost::getBoundingRect() const
{
    QRectF rect = points.boundingRect();
    for (const QPolygonF& stroke : strokes)
    {
        rect = rect.united(stroke.boundingRect());
    }
    return rect;
}

PDFAppMcpChangeJob::PDFAppMcpChangeJob(PDFAppMcpChangeContext context) :
    PDFAppMcpJob(context.call, context.snapshot),
    m_context(std::move(context))
{

}

PDFAppMcpChangeJob::~PDFAppMcpChangeJob() = default;

PDFAppMcpHost* PDFAppMcpChangeJob::getHost() const
{
    return m_host.data();
}

bool PDFAppMcpChangeJob::isHeavy() const
{
    // A copy of a big document is a heavy job, and a change of many items (500
    // annotations with their appearance streams) too; the context does not
    // change during the job, so the host gets the same answer at its start and end
    if (m_context.itemCount >= PDFAppMcpChangeHelper::HEAVY_CHANGE_COUNT)
    {
        return true;
    }
    const PDFDocument* document = getSnapshot().document.data();
    return document && document->getCatalog() && document->getCatalog()->getPageCount() > 500;
}

void PDFAppMcpChangeJob::execute()
{
    m_change = build();
}

PDFAppDocument* PDFAppMcpChangeJob::prepareDocument(const PDFAppMcpToolInvocation& invocation, bool allowLocked)
{
    PDFAppMcpHost* host = invocation.host;
    const std::shared_ptr<PDFMcpCall>& call = invocation.call;
    const QString actionId = invocation.getActionId();

    // A request for a change while a modal dialog of the application is open is
    // refused (plan 2.5: denied_dialog) - the user decides about the dialog first
    if (QApplication::activeModalWidget())
    {
        host->finishWithError(call, actionId, nullptr, QStringLiteral("PDF4QT shows a dialog; try again after the user closes it."), PDFMcpActivityStatus::DeniedDialog);
        return nullptr;
    }

    const PDFAppMcpResolvedDocument resolved = host->getResolver().resolve(invocation.documentId, true);
    if (!resolved.isResolved())
    {
        host->finishWithError(call, actionId, resolved.document, resolved.errorText, PDFMcpActivityStatus::Failed);
        return nullptr;
    }

    PDFAppDocument* document = resolved.document;
    if (!allowLocked && document->isLocked())
    {
        host->finishWithError(call, actionId, document, getLockedText(), PDFMcpActivityStatus::DeniedLocked);
        return nullptr;
    }

    if (document->isBlockedByOperation())
    {
        host->finishWithError(call, actionId, document, QStringLiteral("PDF4QT is busy with another operation on the document (saving, printing or a tool). Try again when it ends."),
                              PDFMcpActivityStatus::DeniedBusy);
        return nullptr;
    }

    return document;
}

PDFAppMcpChangeContext PDFAppMcpChangeJob::createContext(const PDFAppMcpToolInvocation& invocation, PDFAppDocument* document)
{
    PDFAppMcpChangeContext context;
    context.call = invocation.call;
    context.toolName = invocation.toolName;
    context.actionName = invocation.actionName;
    context.document = document;
    context.permission = invocation.action ? invocation.action->permission : PDFAppMcpPermissionKind::Edit;
    if (document)
    {
        context.snapshot = document->getSnapshot();
        context.textIndex = document->getTextIndex();
    }

    // The items of the request (items, ids, values, names - the largest list of
    // the action, validated by the schema of the tool)
    for (auto it = invocation.actionArguments.constBegin(); it != invocation.actionArguments.constEnd(); ++it)
    {
        if (it.value().isArray())
        {
            context.itemCount = qMax(context.itemCount, int(qMin<qsizetype>(it.value().toArray().size(), std::numeric_limits<int>::max())));
        }
    }
    if (invocation.call)
    {
        context.authorName = PDFAppMcpHost::getClientInfo(invocation.call->getRequest().client).displayName;
    }
    return context;
}

PDFMcpCallResult PDFAppMcpChangeJob::createResult(const QString& actionName, const PDFAppDocument* document, const PDFAppMcpOutputMembers& members, bool usesDocument)
{
    PDFMcpJsonWriter writer;
    writer.beginObject();
    writer.writeMember(QLatin1StringView("action"), QStringView(actionName));
    writer.key(QStringView(actionName));
    writer.beginObject();
    if (usesDocument && document)
    {
        writer.writeMember(QLatin1StringView("documentId"), QStringView(document->getId()));
        writer.writeMember(QLatin1StringView("title"), QStringView(document->getTitle()));
    }
    PDFAppMcpChangeHelper::writeMembers(writer, members);
    writer.endObject();
    writer.endObject();
    const QByteArray structured = writer.take();

    PDFMcpContentBuilder builder;
    builder.addText(QString::fromUtf8(structured));
    builder.setStructuredContent(structured);
    return builder.createCallResult(false);
}

QString PDFAppMcpChangeJob::getDecisionText(PDFAppMcpDecision decision)
{
    switch (decision)
    {
        case PDFAppMcpDecision::Allowed:
            return QString();
        case PDFAppMcpDecision::DeniedTimeout:
            return QStringLiteral("The user did not answer the confirmation in PDF4QT within 2 minutes. Ask the user before trying again.");
        case PDFAppMcpDecision::Unavailable:
            return QStringLiteral("PDF4QT cannot ask the user for the confirmation now. Ask the user to allow the change on the AI page of PDF4QT.");
        case PDFAppMcpDecision::DeniedClosed:
            return QStringLiteral("The tab of the document was closed in PDF4QT.");
        case PDFAppMcpDecision::DeniedDisconnected:
            return QStringLiteral("The assistant was disconnected in PDF4QT.");
        case PDFAppMcpDecision::DeniedStopped:
            return QStringLiteral("The MCP server of PDF4QT has stopped.");
        case PDFAppMcpDecision::Canceled:
            return QStringLiteral("The request was canceled.");
        case PDFAppMcpDecision::DeniedUser:
            break;
    }
    return QStringLiteral("The user denied the change in PDF4QT. Do not repeat it without asking the user.");
}

QString PDFAppMcpChangeJob::getLockedText()
{
    return QStringLiteral("The document is locked in PDF4QT. Ask the user to unlock it or to use Edit a Copy.");
}

QString PDFAppMcpChangeJob::joinParts(const QStringList& parts)
{
    if (parts.size() <= 1)
    {
        return parts.value(0);
    }
    return QCoreApplication::translate("pdf::PDFAppMcpChange", "%1 and %2").arg(parts.mid(0, parts.size() - 1).join(QStringLiteral(", ")), parts.back());
}

void PDFAppMcpChangeJob::fail(const QString& text, PDFMcpActivityStatus status)
{
    PDFAppMcpHost* host = m_host;
    const std::shared_ptr<PDFMcpCall>& call = getCall();
    if (!call || call->isFinished())
    {
        return;
    }

    if (host && m_context.activityId > 0 && host->getActivityLog())
    {
        QJsonObject detail = m_change.activityDetail;
        if (status == PDFMcpActivityStatus::Failed)
        {
            detail.insert(QStringLiteral("reason"), PDFMcpJson::sanitizeString(text, PDFMcpJson::SanitizeFlag::RemoveBidi, 200));
        }
        host->getActivityLog()->update(m_context.activityId, status, detail);
        call->finish(PDFMcpCallResult::createToolError(text));
        return;
    }

    if (host)
    {
        host->finishWithError(call, m_context.getActionId(), m_context.document, text, status);
    }
    else
    {
        call->finish(PDFMcpCallResult::createToolError(text));
    }
}

void PDFAppMcpChangeJob::succeed(const PDFMcpCallResult& result)
{
    PDFAppMcpHost* host = m_host;
    const std::shared_ptr<PDFMcpCall>& call = getCall();
    if (!call || call->isFinished())
    {
        return;
    }

    if (host)
    {
        if (m_context.activityId > 0 && host->getActivityLog())
        {
            host->getActivityLog()->update(m_context.activityId, PDFMcpActivityStatus::Allowed, m_change.activityDetail);
        }
        else
        {
            host->recordActivity(call, m_context.getActionId(), m_context.document, m_change.activityDetail,
                                 m_context.isConfirmed ? PDFMcpActivityStatus::Allowed : PDFMcpActivityStatus::Ok);
        }
    }
    call->finish(result);
}

void PDFAppMcpChangeJob::finish(PDFAppMcpHost* host)
{
    m_host = host;
    const std::shared_ptr<PDFMcpCall>& call = getCall();
    if (!host || !call || call->isFinished())
    {
        return;
    }

    if (call->isCanceled())
    {
        fail(QStringLiteral("The request was canceled."), PDFMcpActivityStatus::Canceled);
        return;
    }

    PDFAppDocument* document = m_context.document;
    if (!document)
    {
        fail(QStringLiteral("The tab of the document was closed in PDF4QT."), PDFMcpActivityStatus::DeniedClosed);
        return;
    }

    if (!m_change.choices.empty())
    {
        // An ambiguous name or title: the client chooses (or gets the candidates)
        PDFAppMcpChoice::ask(host, call, m_context.getActionId(), document, m_change.choiceMessage, m_change.choices, m_change.choiceFallback);
        return;
    }

    if (!m_change.errorText.isEmpty())
    {
        // A target, which has disappeared after the document changed meanwhile
        if (m_context.attempt > 0 && m_change.isTargetMissing)
        {
            fail(QStringLiteral("The document changed in the meantime: %1").arg(m_change.errorText), PDFMcpActivityStatus::Failed);
            return;
        }
        fail(m_change.errorText, m_change.errorStatus);
        return;
    }

    if (!m_change.modified)
    {
        // Nothing to change (a reset of a form at its defaults, the same values)
        succeed(createResult(m_context.actionName, document, m_change.output));
        return;
    }

    if (document->isLocked() && m_context.permission == PDFAppMcpPermissionKind::Edit)
    {
        fail(getLockedText(), PDFMcpActivityStatus::DeniedLocked);
        return;
    }

    // The permissions are checked again (a change of the settings meanwhile); a
    // request, which needs the read approval too, is one dialog with both reasons
    const QString assistantKey = call->getRequest().client.assistantKey;
    const PDFAppMcpPermissionKinds kinds = PDFAppMcpPermissionKinds(PDFAppMcpPermissionKind::Read) | m_context.permission;
    const PDFAppMcpPermissionPolicy& policy = host->getPolicy();
    PDFAppMcpPolicyResult policyResult = policy.evaluate(kinds, assistantKey, true);
    if (m_context.isConfirmed && policyResult == PDFAppMcpPolicyResult::Ask)
    {
        policyResult = PDFAppMcpPolicyResult::Allowed;
    }

    switch (policyResult)
    {
        case PDFAppMcpPolicyResult::Denied:
            fail(m_context.permission == PDFAppMcpPermissionKind::Files ? QStringLiteral("The user does not allow assistants to open and save files (Open and save files is set to Deny in PDF4QT).")
                                                                       : QStringLiteral("The user does not allow assistants to edit documents (Edit documents is set to Deny in PDF4QT)."),
                 PDFMcpActivityStatus::DeniedUser);
            return;

        case PDFAppMcpPolicyResult::Ask:
        {
            // At most 16 requests wait in one tab (plan 2.5)
            if (host->isConfirmationQueueFull(document))
            {
                fail(PDFAppMcpHost::getConfirmationQueueFullText(), PDFMcpActivityStatus::Failed);
                return;
            }

            std::shared_ptr<PDFAppMcpConfirmation> confirmation = createConfirmation();
            confirmation->permissions = policy.getKindsToConfirm(kinds, assistantKey, true);
            confirmation->receiver = std::shared_ptr<PDFAppMcpConfirmationReceiver>(shared_from_this(), static_cast<PDFAppMcpConfirmationReceiver*>(this));
            m_holder = shared_from_this();

            // A slot of the host may delete it (with its controller): the request is
            // denied at once then and this job is released by the decision
            const auto keepAlive = shared_from_this();
            host->requestConfirmation(confirmation);
            m_context.activityId = confirmation->activityId;

            // A host shut down meanwhile has denied the request at once: its row
            // keeps the decision (never Waiting again)
            if (!m_host || !getCall() || getCall()->isFinished())
            {
                return;
            }
            if (host->getActivityLog() && m_context.activityId > 0)
            {
                // The waiting row names the change ("Add 3 highlights")
                host->getActivityLog()->update(m_context.activityId, PDFMcpActivityStatus::Waiting, m_change.activityDetail);
            }
            return;
        }

        case PDFAppMcpPolicyResult::Allowed:
            break;
    }

    proceed();
}

std::shared_ptr<PDFAppMcpConfirmation> PDFAppMcpChangeJob::createConfirmation() const
{
    std::shared_ptr<PDFAppMcpConfirmation> confirmation = std::make_shared<PDFAppMcpConfirmation>();
    confirmation->kind = PDFAppMcpConfirmationKind::Change;
    confirmation->call = getCall();
    confirmation->document = m_context.document;
    confirmation->actionId = m_context.getActionId();

    QJsonArray rows;
    for (const PDFAppMcpChangeRow& row : m_change.rows)
    {
        rows.append(row.toJson());
    }

    QJsonArray ghosts;
    for (const PDFAppMcpGhost& ghost : m_change.ghosts)
    {
        if (ghosts.size() >= PDFAppMcpGhost::MAXIMUM_GHOSTS)
        {
            break;
        }
        ghosts.append(ghost.toJson());
    }

    const PDFDocument* document = getSnapshot().document.data();
    confirmation->details.insert(QStringLiteral("rows"), rows);
    confirmation->details.insert(QStringLiteral("ghosts"), ghosts);
    confirmation->details.insert(QStringLiteral("summary"), m_change.summary);
    confirmation->details.insert(QStringLiteral("changeCount"), m_change.changeCount);
    confirmation->details.insert(QStringLiteral("signatureCount"), document ? PDFAppDocumentFileWriter::getSignatureCount(document) : 0);
    confirmation->details.insert(QStringLiteral("isAnnotationChange"), m_context.toolName == QLatin1String("annotations"));
    return confirmation;
}

void PDFAppMcpChangeJob::confirmationFinished(quint64 confirmationId, PDFAppMcpDecision decision)
{
    Q_UNUSED(confirmationId);

    std::shared_ptr<PDFAppMcpJob> holder = std::move(m_holder);
    const std::shared_ptr<PDFMcpCall>& call = getCall();
    if (!m_host || !call || call->isFinished())
    {
        return;
    }

    if (decision != PDFAppMcpDecision::Allowed)
    {
        // The waiting row shows the decision already
        call->finish(PDFMcpCallResult::createToolError(getDecisionText(decision)));
        return;
    }

    m_context.isConfirmed = true;
    proceed();
}

void PDFAppMcpChangeJob::proceed()
{
    PDFAppMcpHost* host = m_host;
    const std::shared_ptr<PDFMcpCall>& call = getCall();
    if (!host || !call || call->isFinished())
    {
        return;
    }

    if (call->isCanceled())
    {
        fail(QStringLiteral("The request was canceled."), PDFMcpActivityStatus::Canceled);
        return;
    }

    PDFAppDocument* document = m_context.document;
    if (!document)
    {
        fail(QStringLiteral("The tab of the document was closed in PDF4QT."), PDFMcpActivityStatus::DeniedClosed);
        return;
    }

    if (document->isLocked() && m_context.permission == PDFAppMcpPermissionKind::Edit)
    {
        fail(getLockedText(), PDFMcpActivityStatus::DeniedLocked);
        return;
    }

    if (document->getVersion() != getSnapshot().version && m_context.permission == PDFAppMcpPermissionKind::Edit)
    {
        // The document changed meanwhile (the user, another request): the change
        // is built again on the current version without a second question
        if (m_context.attempt + 1 >= MAXIMUM_ATTEMPTS)
        {
            fail(QStringLiteral("The document changed in the meantime. Call the tool again."), PDFMcpActivityStatus::Failed);
            return;
        }

        if (document->isBlockedByOperation())
        {
            fail(QStringLiteral("PDF4QT is busy with another operation on the document (saving, printing or a tool). Try again when it ends."), PDFMcpActivityStatus::DeniedBusy);
            return;
        }

        PDFAppMcpChangeContext context = m_context;
        context.snapshot = document->getSnapshot();
        context.textIndex = document->getTextIndex();
        context.attempt = m_context.attempt + 1;
        host->startJob(createRerun(std::move(context)));
        return;
    }

    commitChange();
}

void PDFAppMcpChangeJob::commitChange()
{
    PDFAppMcpHost* host = m_host;
    PDFAppDocument* document = m_context.document;
    if (!host || !document || !m_change.modified)
    {
        fail(QStringLiteral("The tab of the document was closed in PDF4QT."), PDFMcpActivityStatus::DeniedClosed);
        return;
    }

    // The optional content activity of the document goes with the changed document
    const PDFModifiedDocument modified(PDFDocumentPointer(*m_change.modified), document->getOptionalContentActivity(), m_change.modified->getFlags());
    if (!document->commit(modified, m_change.undoDescription, getSnapshot().version, PDFAppChangeSource::Mcp))
    {
        fail(QStringLiteral("PDF4QT is busy with another operation on the document. Try again when it ends."), PDFMcpActivityStatus::DeniedBusy);
        return;
    }

    // The commit runs the code of the application (the views, the panels, the
    // listeners of the document): a slot may close the tab or the window, or delete
    // the host with its controller (UnitTestsAppMcpLifetime)
    document = m_context.document;
    host = m_host;
    if (!host || !document)
    {
        fail(QStringLiteral("The tab of the document was closed in PDF4QT."), PDFMcpActivityStatus::DeniedClosed);
        return;
    }

    // The notification bar (every change of an assistant is shown, also with
    // Edit documents = Allow - owner Q3)
    PDFAppMcpCommitInfo info;
    info.document = document;
    info.clientName = m_context.authorName;
    info.clientNameRaw = getCall()->getRequest().client.clientName;
    info.text = m_change.undoDescription;
    info.version = document->getVersion();
    info.annotations = m_change.annotations;
    info.panelId = m_change.panelId;
    PDFAppMcpConfirmationUi::notifyCommitted(host, info);

    PDFAppMcpOutputMembers output = m_change.output;
    output.emplace_back(QStringLiteral("undoDescription"), m_change.undoDescription);
    succeed(createResult(m_context.actionName, document, output));
}

std::optional<QJsonObject> PDFAppMcpChoice::getAnswer(const PDFMcpCallRequest& request, bool* isDeclined)
{
    if (isDeclined)
    {
        *isDeclined = false;
    }

    const QJsonValue value = request.inputResponses.value(getKey());
    if (!value.isObject())
    {
        return std::nullopt;
    }

    const QJsonObject response = value.toObject();
    const QString action = response.value(QStringLiteral("action")).toString();
    if (action != QLatin1String("accept"))
    {
        if (isDeclined)
        {
            *isDeclined = action == QLatin1String("decline") || action == QLatin1String("cancel");
        }
        return std::nullopt;
    }
    return response.value(QStringLiteral("content")).toObject();
}

PDFMcpInputRequests PDFAppMcpChoice::createRequests(const QString& message, const std::vector<Property>& properties)
{
    // A flat object schema of strings: titled choices (oneOf of const + title,
    // the titled enum of the elicitation schema) or a text with a default
    PDFMcpJsonWriter writer;
    writer.beginObject();
    writer.writeMember(QLatin1StringView("type"), QLatin1StringView("object"));
    writer.key(QLatin1StringView("properties"));
    writer.beginObject();

    PDFMcpInputRequests requests;
    QJsonObject allowedProperties;
    QStringList required;
    for (const Property& property : properties)
    {
        writer.key(QStringView(property.name));
        writer.beginObject();
        writer.writeMember(QLatin1StringView("type"), QLatin1StringView("string"));
        writer.writeMember(QLatin1StringView("title"), QStringView(property.title));
        if (!property.options.empty())
        {
            writer.key(QLatin1StringView("oneOf"));
            writer.beginArray();
            QJsonArray allowed;
            for (const auto& option : property.options)
            {
                writer.beginObject();
                writer.writeMember(QLatin1StringView("const"), QStringView(option.first));
                writer.writeMember(QLatin1StringView("title"), QStringView(option.second));
                writer.endObject();
                allowed.append(option.first);
            }
            writer.endArray();
            allowedProperties.insert(property.name, allowed);
        }
        else
        {
            // A free text (a path), checked by the tool on the retry
            allowedProperties.insert(property.name, QStringLiteral("any"));
        }
        if (!property.defaultValue.isEmpty())
        {
            writer.writeMember(QLatin1StringView("default"), QStringView(property.defaultValue));
        }
        writer.endObject();
        required << property.name;
    }
    writer.endObject();
    writer.key(QLatin1StringView("required"));
    writer.writeStringArray(required);
    writer.endObject();

    PDFMcpInputRequest request;
    request.key = getKey();
    request.message = message;
    request.requestedSchema = writer.take();
    requests.requests.push_back(std::move(request));

    // {key: {property: [values] | "any"}} - the dispatcher seals it and drops
    // the answers, which are not allowed (contract of package A)
    requests.allowedValues.insert(getKey(), allowedProperties);
    return requests;
}

void PDFAppMcpChoice::ask(PDFAppMcpHost* host, const std::shared_ptr<PDFMcpCall>& call, const QString& actionId, const PDFAppDocument* document,
                          const QString& message, const std::vector<Property>& properties, const QString& fallbackText)
{
    if (!call)
    {
        return;
    }

    if (!call->getRequest().client.supportsElicitation())
    {
        // Elicitation is never required (Gemini CLI): the parameter to pass
        if (host)
        {
            host->finishWithError(call, actionId, document, fallbackText, PDFMcpActivityStatus::Failed);
        }
        else
        {
            call->finish(PDFMcpCallResult::createToolError(fallbackText));
        }
        return;
    }

    // No activity row: the retried request records the change
    call->requestInput(createRequests(message, properties));
}

}   // namespace pdf
