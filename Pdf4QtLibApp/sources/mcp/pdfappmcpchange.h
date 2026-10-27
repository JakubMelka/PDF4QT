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

#ifndef PDFAPPMCPCHANGE_H
#define PDFAPPMCPCHANGE_H

#include "pdfappglobal.h"
#include "pdfappannotationoperations.h"
#include "pdfappmcpconfirmation.h"
#include "pdfappmcpjob.h"
#include "pdfappmcptoolhandler.h"
#include "pdfmcprepository.h"

#include <QColor>
#include <QJsonObject>
#include <QJsonValue>
#include <QPointer>
#include <QPolygonF>
#include <QString>

#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace pdf
{

class PDFAppDocument;
class PDFAppMcpHost;
class PDFDocumentTextIndex;
class PDFMcpInputRequests;

/// One line of the body of a change confirmation (940:133693): the icon, the
/// change ("Add 3 highlights"), its pages ("Page 308") and a quote of the
/// text ("“Enterprise”, “Education”") - texts written by the model are plain
/// texts, sanitized and bounded
class PDF4QTLIBAPPSHARED_EXPORT PDFAppMcpChangeRow
{
public:
    QString iconName;
    QString text;
    QString page;
    QString detail;

    QJsonObject toJson() const;
    static PDFAppMcpChangeRow fromJson(const QJsonObject& object);
};

/// Ghost of a proposed annotation, drawn by the preview of a confirmation
/// (955:151877): the geometry in the page space and the color of the annotation
class PDF4QTLIBAPPSHARED_EXPORT PDFAppMcpGhost
{
public:
    enum class Shape
    {
        Quads,      ///< Text markup: 4 points per piece (QuadPoints order)
        Rectangle,  ///< Two corners
        Ellipse,    ///< Two corners
        Note,       ///< The top-left point of the icon
        Polyline,   ///< Points of a line, an arrow or a polyline
        Polygon,    ///< Points of a polygon
        Ink         ///< Strokes
    };

    /// Most ghosts of one confirmation (a preview, not the change)
    static constexpr int MAXIMUM_GHOSTS = 500;

    /// Most points of the geometry of one ghost
    static constexpr int MAXIMUM_POINTS = 4000;

    PDFInteger pageIndex = -1;
    Shape shape = Shape::Rectangle;
    QPolygonF points;
    std::vector<QPolygonF> strokes;
    QColor color;

    QJsonObject toJson() const;

    /// Ghost of an object written by toJson (hostile values bounded); std::nullopt for an invalid one
    static std::optional<PDFAppMcpGhost> fromJson(const QJsonObject& object);

    /// Bounding rectangle of the geometry in the page space
    QRectF getBoundingRect() const;
};

/// Elicitation of a choice of a change (plan 2.8: ambiguous form field names,
/// outline items given by their titles, the path of save_copy): the modern MRTR
/// input request or the legacy elicitation/create (the dispatcher converts it),
/// the fallback text without the capability of the client
class PDF4QTLIBAPPSHARED_EXPORT PDFAppMcpChoice
{
public:
    PDFAppMcpChoice() = delete;

    /// Key of the input request
    static QString getKey() { return QStringLiteral("pdf4qt.choice"); }

    /// One property of the requested schema: a string, with titled options (a
    /// choice) or a free text with a default value
    class Property
    {
    public:
        QString name;
        QString title;
        std::vector<std::pair<QString, QString>> options;     ///< value, title
        QString defaultValue;
    };

    /// Answer of a retried request: the content of an accepted answer; \p isDeclined
    /// for decline / cancel; std::nullopt without an answer
    static std::optional<QJsonObject> getAnswer(const PDFMcpCallRequest& request, bool* isDeclined);

    /// Asks the client (finishes the call by InputRequired) or, without the
    /// capability of elicitation, finishes it by \p fallbackText
    static void ask(PDFAppMcpHost* host, const std::shared_ptr<PDFMcpCall>& call, const QString& actionId, const PDFAppDocument* document,
                    const QString& message, const std::vector<Property>& properties, const QString& fallbackText);

    /// The input requests of the properties (tests)
    static PDFMcpInputRequests createRequests(const QString& message, const std::vector<Property>& properties);
};

/// Ordered members of an output object of an action (written in this order)
using PDFAppMcpOutputMembers = std::vector<std::pair<QString, QJsonValue>>;

/// A change of a document built by a worker from a snapshot (plan 2.5)
class PDF4QTLIBAPPSHARED_EXPORT PDFAppMcpChange
{
public:
    PDFAppAnnotationResult modified;        ///< The changed document; nullopt = nothing changed (the output is returned)
    QString errorText;                      ///< Execution error: nothing is changed
    PDFMcpActivityStatus errorStatus = PDFMcpActivityStatus::Failed;
    bool isTargetMissing = false;           ///< The error is a target (annotation, item), which does not exist
    QString undoDescription;                ///< "Claude Code added 3 highlights and 1 note"
    QString summary;                        ///< "3 highlights, 1 note" (the preview bar)
    std::vector<PDFAppMcpChangeRow> rows;   ///< Rows of the confirmation
    int changeCount = 0;                    ///< Number of the changes (the undo note)
    std::vector<PDFObjectReference> annotations;    ///< New or changed annotations (Show Changes)
    QString panelId;                        ///< Panel of Show Changes (notes, outline; empty = none)
    std::vector<PDFAppMcpGhost> ghosts;     ///< Ghosts of the preview
    QJsonObject activityDetail;             ///< {count, kind} of the activity row
    PDFAppMcpOutputMembers output;          ///< Output of the action (without documentId and title)
    std::vector<PDFAppMcpChoice::Property> choices;    ///< A choice of the client is needed first (elicitation, plan 2.8)
    QString choiceMessage;                  ///< Message of the elicitation
    QString choiceFallback;                 ///< Execution error without the capability of elicitation
};

/// What the notification bar of a committed change shows (940:33875)
class PDF4QTLIBAPPSHARED_EXPORT PDFAppMcpCommitInfo
{
public:
    QPointer<PDFAppDocument> document;
    QString clientName;                     ///< Display name
    QString clientNameRaw;                  ///< clientInfo.name (the monogram)
    QString text;                           ///< "Claude Code added 3 highlights and 1 note"
    quint64 version = 0;                    ///< Version of the document after the commit (Undo only while it is on top)
    std::vector<PDFObjectReference> annotations;
    QString panelId;
};

/// Request of a change: who asks and what (the rerun on a new version keeps it)
class PDF4QTLIBAPPSHARED_EXPORT PDFAppMcpChangeContext
{
public:
    std::shared_ptr<PDFMcpCall> call;
    QString toolName;
    QString actionName;
    QPointer<PDFAppDocument> document;
    PDFAppDocumentSnapshot snapshot;
    std::shared_ptr<const PDFDocumentTextIndex> textIndex;     ///< Index of the document (quotes of the markups), may be null
    QString authorName;                                         ///< Display name of the assistant (author of the annotations)
    PDFAppMcpPermissionKind permission = PDFAppMcpPermissionKind::Edit;
    int attempt = 0;                                            ///< Rebuilds after the document changed (plan 2.5: at most 3 tries)
    int itemCount = 0;                                          ///< Items of the request (the largest list of the action; many = a heavy job)
    bool isConfirmed = false;                                   ///< The user has allowed the change (a rebuild does not ask again)
    qint64 activityId = 0;                                      ///< The waiting activity row of the confirmation (updated, not repeated)

    QString getActionId() const { return toolName + QLatin1Char('.') + actionName; }
};

/// A change of a document requested by an assistant (plan 2.5): validated
/// arguments -> the document (lock, busy, modal dialog) -> a worker builds the
/// changed document from a snapshot -> the confirmation (Ask) -> the rebuild on
/// the current version, when it changed (up to 3 tries, no second question) ->
/// one undo step (source Mcp, author = the assistant) -> the notification bar.
/// A subclass builds the change (build, worker thread) and creates its rerun.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppMcpChangeJob : public PDFAppMcpJob, public PDFAppMcpConfirmationReceiver
{
public:
    /// Most tries of a change after the document changed meanwhile (plan 2.5)
    static constexpr int MAXIMUM_ATTEMPTS = 3;

    explicit PDFAppMcpChangeJob(PDFAppMcpChangeContext context);
    virtual ~PDFAppMcpChangeJob() override;

    const PDFAppMcpChangeContext& getContext() const { return m_context; }
    const PDFAppMcpChange& getChange() const { return m_change; }

    virtual bool isHeavy() const override;
    virtual void execute() override;
    virtual void finish(PDFAppMcpHost* host) override;
    virtual void confirmationFinished(quint64 confirmationId, PDFAppMcpDecision decision) override;

    /// The document of a change: resolves documentId of the invocation and
    /// refuses a document, which is locked (O20, unless \p allowLocked), busy (a
    /// save, a print, a tool) or a request while a modal dialog of the
    /// application is open (denied_dialog). Returns nullptr after the call has
    /// been finished by the refusal.
    static PDFAppDocument* prepareDocument(const PDFAppMcpToolInvocation& invocation, bool allowLocked = false);

    /// Context of a change of the document by the invocation
    static PDFAppMcpChangeContext createContext(const PDFAppMcpToolInvocation& invocation, PDFAppDocument* document);

    /// The complete tool result { action, <action>: { documentId, title, members } }
    /// with the compact JSON in the text block
    static PDFMcpCallResult createResult(const QString& actionName, const PDFAppDocument* document, const PDFAppMcpOutputMembers& members, bool usesDocument = true);

    /// Text of a denial of the user for the model
    static QString getDecisionText(PDFAppMcpDecision decision);

    /// Text of a locked document
    static QString getLockedText();

    /// Joins the parts "3 highlights", "1 note" into "3 highlights and 1 note"
    static QString joinParts(const QStringList& parts);

protected:
    /// Builds the change from the snapshot of the context (worker thread)
    virtual PDFAppMcpChange build() = 0;

    /// The same request on a newer snapshot (a rebuild on the current version)
    virtual std::shared_ptr<PDFAppMcpChangeJob> createRerun(PDFAppMcpChangeContext context) const = 0;

    /// Confirmation of the change (the kind Change by default); the subclass may
    /// add its facts (a save)
    virtual std::shared_ptr<PDFAppMcpConfirmation> createConfirmation() const;

    /// Commits the change (GUI thread). The default commits the changed document
    /// as one undo step; a subclass may do another action (a save).
    virtual void commitChange();

    /// Ends the call by an execution error (the waiting activity row is updated)
    void fail(const QString& text, PDFMcpActivityStatus status);

    /// Ends the call successfully (the activity row of the confirmation is
    /// updated, else a row is recorded)
    void succeed(const PDFMcpCallResult& result);

    PDFAppMcpHost* getHost() const;

private:
    void proceed();

    PDFAppMcpChangeContext m_context;
    PDFAppMcpChange m_change;
    QPointer<PDFAppMcpHost> m_host;
    std::shared_ptr<PDFAppMcpJob> m_holder;     ///< Alive while the confirmation waits
};

}   // namespace pdf

#endif // PDFAPPMCPCHANGE_H
