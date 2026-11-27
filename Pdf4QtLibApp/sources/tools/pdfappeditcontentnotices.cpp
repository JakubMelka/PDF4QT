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

#include "pdfappeditcontentnotices.h"

#include "pdfappeditcontentregistry.h"
#include "pdfappeditcontentselection.h"
#include "pdfapptoolpanelparts.h"

#include "pdfaccenthue.h"
#include "pdfcomponentbuttons.h"

#include <QGridLayout>
#include <QHBoxLayout>

#include <iterator>

namespace pdf
{

/// The look of a notice by its severity and its kind (the frames edit-content-v2-preview, -scanned,
/// -complex-page, -color-converted, -text-missing-glyph): blue information, amber warning, red error
class PDFAppEditContentNoticesHelper
{
public:
    PDFAppEditContentNoticesHelper() = delete;

    /// The space kept free at the trailing end of the title for the close button
    static constexpr int CLOSE_SPACE = 24;

    /// The close button lies on the head of the callout: the padding of the callout
    static constexpr int CLOSE_MARGIN_TOP = 9;
    static constexpr int CLOSE_MARGIN_SIDE = 10;

    static PDFAccentHue getHue(PDFAppEditContentNoticeRequest::Severity severity)
    {
        switch (severity)
        {
            case PDFAppEditContentNoticeRequest::Severity::Information:
                return PDFAccentHue::Blue;
            case PDFAppEditContentNoticeRequest::Severity::Warning:
                return PDFAccentHue::Amber;
            case PDFAppEditContentNoticeRequest::Severity::Error:
                return PDFAccentHue::Red;
        }
        return PDFAccentHue::Blue;
    }

    static QString getIconName(const PDFAppEditContentNoticeRequest& request)
    {
        // The notice of Preview has the icon of the toggle (edit-content-v2-preview)
        if (request.id == QLatin1String(PDFAppEditContentNoticeIds::PREVIEW))
        {
            return QStringLiteral("preview-exact");
        }

        switch (request.severity)
        {
            case PDFAppEditContentNoticeRequest::Severity::Information:
                return QStringLiteral("info");
            case PDFAppEditContentNoticeRequest::Severity::Warning:
                return QStringLiteral("alert-triangle");
            case PDFAppEditContentNoticeRequest::Severity::Error:
                return QStringLiteral("alert-circle");
        }
        return QStringLiteral("info");
    }
};

// ---------------------------------------------------------------- PDFAppEditContentNotices

PDFAppEditContentNotices::PDFAppEditContentNotices(PDFAppEditContentContext* context, QObject* parent) :
    BaseClass(parent),
    m_context(context)
{
    // The context and the selection are siblings of this object under the GUI: they live as long as it does
    if (context)
    {
        connect(context, &PDFAppEditContentContext::noticeRequested, this, &PDFAppEditContentNotices::onNoticeRequested);
        connect(context, &PDFAppEditContentContext::noticeHideRequested, this, &PDFAppEditContentNotices::onNoticeHideRequested);
        if (PDFAppEditContentSelection* selection = context->getSelection())
        {
            connect(selection, &PDFAppEditContentSelection::changed, this, &PDFAppEditContentNotices::onSelectionChanged);
        }
    }
}

PDFAppEditContentNotices::~PDFAppEditContentNotices() = default;

std::optional<PDFAppEditContentNoticeRequest> PDFAppEditContentNotices::getCurrent() const
{
    // An event covers the state below it; of the states the newest one, which the user has not closed
    if (m_eventNotice)
    {
        return m_eventNotice;
    }

    for (auto it = m_stateNotices.crbegin(); it != m_stateNotices.crend(); ++it)
    {
        if (!m_closedNotices.contains(it->id))
        {
            return *it;
        }
    }

    return std::nullopt;
}

void PDFAppEditContentNotices::closeCurrent()
{
    const std::optional<PDFAppEditContentNoticeRequest> before = getCurrent();
    if (!before)
    {
        return;
    }

    if (before->kind == PDFAppEditContentNoticeRequest::Kind::Event)
    {
        m_eventNotice.reset();
    }
    else
    {
        m_closedNotices.insert(before->id);
        if (before->isOncePerSession)
        {
            m_sessionClosedNotices.insert(before->id);
        }
    }

    emitIfChanged(before);
}

void PDFAppEditContentNotices::triggerCurrentAction()
{
    const std::optional<PDFAppEditContentNoticeRequest> current = getCurrent();
    if (current && !current->actionId.isEmpty() && m_context)
    {
        // Queued by the context (the action may close the tool)
        m_context->triggerAction(current->actionId);
    }
}

void PDFAppEditContentNotices::onNoticeRequested(const PDFAppEditContentNoticeRequest& request)
{
    if (!request.isValid())
    {
        return;
    }

    const std::optional<PDFAppEditContentNoticeRequest> before = getCurrent();
    if (request.kind == PDFAppEditContentNoticeRequest::Kind::Event)
    {
        // The newer event replaces the older one (one place, one notice)
        m_eventNotice = request;
    }
    else
    {
        // A notice closed for the session is not shown again (QE.2); a state closed by the user stays hidden until it
        // is left (hideNotice) - a request again (a new text after a change of the language) only replaces its text
        if (request.isOncePerSession && m_sessionClosedNotices.contains(request.id))
        {
            return;
        }

        bool isKnown = false;
        for (PDFAppEditContentNoticeRequest& notice : m_stateNotices)
        {
            if (notice.id == request.id)
            {
                if (notice == request)
                {
                    return;
                }
                // The same state again with a new text keeps its place in the order of the states
                notice = request;
                isKnown = true;
                break;
            }
        }
        if (!isKnown)
        {
            m_stateNotices.push_back(request);
        }
    }

    emitIfChanged(before);
}

void PDFAppEditContentNotices::onNoticeHideRequested(const QString& noticeId)
{
    const std::optional<PDFAppEditContentNoticeRequest> before = getCurrent();

    // The state is left: closed by the user, it comes back when it is entered again
    for (auto it = m_stateNotices.begin(); it != m_stateNotices.end();)
    {
        it = it->id == noticeId ? m_stateNotices.erase(it) : std::next(it);
    }
    m_closedNotices.remove(noticeId);
    if (m_eventNotice && m_eventNotice->id == noticeId)
    {
        m_eventNotice.reset();
    }

    emitIfChanged(before);
}

void PDFAppEditContentNotices::onSelectionChanged()
{
    // An event leaves with the next change of the selection (zadani_gui.md 4.15)
    if (!m_eventNotice)
    {
        return;
    }

    const std::optional<PDFAppEditContentNoticeRequest> before = getCurrent();
    m_eventNotice.reset();
    emitIfChanged(before);
}

void PDFAppEditContentNotices::emitIfChanged(const std::optional<PDFAppEditContentNoticeRequest>& before)
{
    if (getCurrent() != before)
    {
        Q_EMIT currentChanged();
    }
}

// ---------------------------------------------------------------- PDFAppEditContentNotice

PDFAppEditContentNotice::PDFAppEditContentNotice(PDFAppEditContentNotices* notices, QWidget* parent) :
    BaseClass(parent),
    m_notices(notices)
{
    setObjectName(QStringLiteral("editContentNotice"));

    // The callout and the close button share one cell: the button lies over the trailing end of the head
    QGridLayout* layout = new QGridLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    QWidget* closeHolder = new QWidget(this);
    QHBoxLayout* closeLayout = new QHBoxLayout(closeHolder);
    closeLayout->setContentsMargins(0, PDFAppEditContentNoticesHelper::CLOSE_MARGIN_TOP, PDFAppEditContentNoticesHelper::CLOSE_MARGIN_SIDE, 0);
    closeLayout->setSpacing(0);
    PDFIconButton* closeButton = new PDFIconButton(QStringLiteral("x"), PDFIconButton::Size::TabClose, closeHolder);
    closeButton->setObjectName(QStringLiteral("editContentNoticeClose"));
    closeButton->setToolTip(tr("Close"));
    closeButton->setAccessibleName(tr("Close"));
    closeLayout->addWidget(closeButton);
    layout->addWidget(closeHolder, 0, 0, Qt::AlignTop | Qt::AlignTrailing);
    connect(closeButton, &QAbstractButton::clicked, this, &PDFAppEditContentNotice::onCloseClicked);
    m_closeHolder = closeHolder;
    m_closeButton = closeButton;

    // The notices live as long as the tool, this widget is a part of the canvas: a foreign sender
    if (notices)
    {
        PDFDeferredDeletion::connect(notices, &PDFAppEditContentNotices::currentChanged, this, &PDFAppEditContentNotice::onCurrentChanged);
    }

    onCurrentChanged();
}

PDFAppEditContentNotice::~PDFAppEditContentNotice() = default;

QString PDFAppEditContentNotice::getTitle() const
{
    return m_title;
}

QString PDFAppEditContentNotice::getText() const
{
    return m_text;
}

void PDFAppEditContentNotice::onCurrentChanged()
{
    const std::optional<PDFAppEditContentNoticeRequest> current = m_notices ? m_notices->getCurrent() : std::nullopt;

    // The hue and the icon of a callout are given at its creation: a new notice gets a new callout. The old one is
    // never the sender here (its action is queued by the context, the close button is not inside of it).
    if (PDFAppToolCallout* callout = m_callout.data())
    {
        m_callout = nullptr;
        PDFDeferredDeletion::retire(callout);
    }

    m_title = current ? current->title : QString();
    m_text = current ? current->text : QString();
    if (!current)
    {
        setVisible(false);
        return;
    }

    PDFAppToolCallout* callout = new PDFAppToolCallout(PDFAppEditContentNoticesHelper::getHue(current->severity), PDFAppEditContentNoticesHelper::getIconName(*current), m_title, m_text, this);
    callout->setObjectName(QStringLiteral("editContentNoticeCallout"));
    if (PDFLabel* title = callout->getTitleLabel())
    {
        title->setContentsMargins(0, 0, PDFAppEditContentNoticesHelper::CLOSE_SPACE, 0);
    }

    // The button of the action: the title of the action of the registry (no notice of stage E1 has one)
    const PDFAppEditContentContext* context = m_notices ? m_notices->getContext() : nullptr;
    const PDFAppEditContentActionEntry* action = context && !current->actionId.isEmpty() ? context->getRegistry()->findAction(current->actionId) : nullptr;
    if (action)
    {
        PDFTextLink* link = new PDFTextLink(action->getTitle(context), callout);
        link->setObjectName(QStringLiteral("editContentNoticeAction"));
        callout->getBodyLayout()->addWidget(link, 0, Qt::AlignLeading);
        connect(link, &QAbstractButton::clicked, this, &PDFAppEditContentNotice::onActionClicked);
    }

    QGridLayout* layout = static_cast<QGridLayout*>(this->layout());
    layout->addWidget(callout, 0, 0);
    m_callout = callout;
    if (m_closeHolder)
    {
        m_closeHolder->raise();
    }
    setVisible(true);
}

void PDFAppEditContentNotice::onCloseClicked()
{
    // The last call: the notices emit currentChanged, the slot of this widget rebuilds the callout (the close button
    // stays)
    if (m_notices)
    {
        m_notices->closeCurrent();
    }
}

void PDFAppEditContentNotice::onActionClicked()
{
    if (m_notices)
    {
        m_notices->triggerCurrentAction();
    }
}

}   // namespace pdf

#include "moc_pdfappeditcontentnotices.cpp"
