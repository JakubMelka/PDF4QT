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

#include "pdfappleaveflow.h"

#include <QDialog>

#include <algorithm>

namespace pdf
{

// ---------------------------------------------------------------- answer of a dialog

PDFAppDialogAnswer::PDFAppDialogAnswer(QDialog* dialog, QObject* receiver) :
    BaseClass(receiver),
    m_dialog(dialog)
{
    if (!dialog)
    {
        // Nothing to wait for: the answer is Rejected, never inside the call (the receiver connects after it)
        m_isAnswered = true;
        QMetaObject::invokeMethod(this, &PDFAppDialogAnswer::onDialogGone, Qt::QueuedConnection);
        return;
    }

    connect(dialog, &QDialog::finished, this, &PDFAppDialogAnswer::onDialogFinished);
    connect(dialog, &QObject::destroyed, this, &PDFAppDialogAnswer::onDialogDestroyed);
}

PDFAppDialogAnswer::~PDFAppDialogAnswer() = default;

void PDFAppDialogAnswer::showModal(QDialog* dialog)
{
    if (!dialog)
    {
        return;
    }

    // QDialog::exec sets WA_ShowModal: a dialog without its own window modality becomes application modal
    // (QWidget::setAttribute); then it shows it and starts its event loop - here only the show
    dialog->setAttribute(Qt::WA_ShowModal, true);
    dialog->setResult(0);
    dialog->show();
}

QDialog* PDFAppDialogAnswer::getDialog() const
{
    return m_dialog.data();
}

void PDFAppDialogAnswer::emitAnswer(const QDialog* dialog, int code)
{
    Q_UNUSED(dialog);
    Q_EMIT answered(code);
}

void PDFAppDialogAnswer::onDialogFinished(int code)
{
    if (m_isAnswered)
    {
        return;
    }
    m_isAnswered = true;

    // The dialog emits finished from QDialog::done (alive, hidden); it is never watched again
    QDialog* dialog = m_dialog.data();
    if (dialog)
    {
        disconnect(dialog, nullptr, this, nullptr);
        if (m_isDialogDeleted)
        {
            dialog->deleteLater();
        }
    }
    deleteLater();
    emitAnswer(dialog, code);
}

void PDFAppDialogAnswer::onDialogDestroyed()
{
    if (m_isAnswered)
    {
        return;
    }

    // The dialog has gone without finished (deleted with its parent): the answer comes from the event loop, the
    // receiver never runs inside the destructor, which has deleted the dialog
    m_isAnswered = true;
    QMetaObject::invokeMethod(this, &PDFAppDialogAnswer::onDialogGone, Qt::QueuedConnection);
}

void PDFAppDialogAnswer::onDialogGone()
{
    deleteLater();
    emitAnswer(nullptr, QDialog::Rejected);
}

// ---------------------------------------------------------------- step

PDFAppLeaveFlowStep::PDFAppLeaveFlowStep(QObject* reporter, PDFAppLeaveFlow* flow) :
    BaseClass(flow),
    m_reporterKey(reporter)
{
    if (reporter)
    {
        connect(reporter, &QObject::destroyed, this, &PDFAppLeaveFlowStep::onReporterDestroyed);
    }
}

PDFAppLeaveFlowStep::~PDFAppLeaveFlowStep() = default;

void PDFAppLeaveFlowStep::deliver(PDFAppLeaveResult result)
{
    if (m_isDelivered)
    {
        return;
    }
    m_isDelivered = true;
    deleteLater();
    Q_EMIT finished(result);
}

void PDFAppLeaveFlowStep::drop()
{
    m_isDelivered = true;
    disconnect(this, &PDFAppLeaveFlowStep::finished, nullptr, nullptr);
    deleteLater();
}

void PDFAppLeaveFlowStep::onReporterDestroyed()
{
    if (m_isDelivered)
    {
        return;
    }

    // The reporter has gone without its result (its tab, its page): the leave is refused, from the event loop (the
    // waiter never runs inside the destructor of the reporter)
    m_isDelivered = true;
    QMetaObject::invokeMethod(this, &PDFAppLeaveFlowStep::onReporterGone, Qt::QueuedConnection);
}

void PDFAppLeaveFlowStep::onReporterGone()
{
    deleteLater();
    Q_EMIT finished(PDFAppLeaveResult::Refused);
}

// ---------------------------------------------------------------- flow

PDFAppLeaveFlow::PDFAppLeaveFlow(QObject* owner) :
    BaseClass(owner)
{
    setObjectName(QStringLiteral("leaveFlow"));
}

PDFAppLeaveFlow::~PDFAppLeaveFlow() = default;

PDFAppLeaveFlow* PDFAppLeaveFlow::find(const QObject* owner)
{
    return owner ? owner->findChild<PDFAppLeaveFlow*>(QString(), Qt::FindDirectChildrenOnly) : nullptr;
}

PDFAppLeaveFlow* PDFAppLeaveFlow::findRunning(const QObject* root)
{
    if (!root)
    {
        return nullptr;
    }

    if (const PDFAppLeaveFlow* flow = qobject_cast<const PDFAppLeaveFlow*>(root); flow && flow->isRunning())
    {
        return const_cast<PDFAppLeaveFlow*>(flow);
    }

    const QList<PDFAppLeaveFlow*> flows = root->findChildren<PDFAppLeaveFlow*>();
    for (PDFAppLeaveFlow* flow : flows)
    {
        if (flow->isRunning())
        {
            return flow;
        }
    }
    return nullptr;
}

bool PDFAppLeaveFlow::start(Goal goal)
{
    if (isRunning() || goal == Goal::None)
    {
        return false;
    }

    m_goal = goal;
    m_step = 0;
    return true;
}

void PDFAppLeaveFlow::finish(bool isDone)
{
    if (!isRunning())
    {
        return;
    }

    // The state is cleared first: the pages and the slots of finished may start a new flow
    std::vector<QPointer<PDFAppPage>> pages = std::move(m_acceptedPages);
    std::vector<QPointer<PDFAppLeaveFlowStep>> steps = std::move(m_steps);
    pages.insert(pages.end(), m_finishingPages.begin(), m_finishingPages.end());
    m_acceptedPages.clear();
    m_finishingPages.clear();
    m_isFinishProceeding = false;
    m_isFinishRefused = false;
    m_steps.clear();
    m_goal = Goal::None;
    m_step = 0;
    m_targetRoute = PDFAppRoute();
    m_targetMode = PDFAppNavigationMode::Push;
    m_subject = nullptr;
    m_value = 0;
    m_queue.clear();
    m_question = nullptr;

    // A step, which still waits, belongs to the ended flow: its waiter gets nothing
    for (const QPointer<PDFAppLeaveFlowStep>& step : steps)
    {
        if (step)
        {
            step->drop();
        }
    }

    // A page, which has accepted and was not finished by the owner, forgets its choice (decision 8); the pages run
    // code of the application
    QPointer<PDFAppLeaveFlow> guard(this);
    PDFAppPage::finishLeaves(pages, false);
    if (guard)
    {
        Q_EMIT finished(isDone);
    }
}

void PDFAppLeaveFlow::setTarget(const PDFAppRoute& route, PDFAppNavigationMode mode)
{
    m_targetRoute = route;
    m_targetMode = mode;
}

void PDFAppLeaveFlow::setQueue(const std::vector<QPointer<QObject>>& objects)
{
    m_queue = objects;
}

QObject* PDFAppLeaveFlow::takeNext()
{
    while (!m_queue.empty())
    {
        QPointer<QObject> object = m_queue.front();
        m_queue.erase(m_queue.begin());
        if (object)
        {
            return object.data();
        }
    }
    return nullptr;
}

int PDFAppLeaveFlow::getQueueSize() const
{
    int size = 0;
    for (const QPointer<QObject>& object : m_queue)
    {
        if (object)
        {
            ++size;
        }
    }
    return size;
}

void PDFAppLeaveFlow::addAcceptedPage(PDFAppPage* page)
{
    if (!page)
    {
        return;
    }

    for (const QPointer<PDFAppPage>& acceptedPage : m_acceptedPages)
    {
        if (acceptedPage.data() == page)
        {
            return;
        }
    }
    m_acceptedPages.emplace_back(page);
}

std::vector<QPointer<PDFAppPage>> PDFAppLeaveFlow::getAcceptedPages() const
{
    std::vector<QPointer<PDFAppPage>> pages;
    for (const QPointer<PDFAppPage>& page : m_acceptedPages)
    {
        if (page)
        {
            pages.push_back(page);
        }
    }
    return pages;
}

PDFAppLeaveResult PDFAppLeaveFlow::requestFinishPages(bool isProceeding)
{
    // The list is taken first: a page may start the next step or end the flow
    m_finishingPages.insert(m_finishingPages.end(), m_acceptedPages.begin(), m_acceptedPages.end());
    m_acceptedPages.clear();
    m_isFinishProceeding = isProceeding;
    m_isFinishRefused = false;
    return continueFinishPages();
}

PDFAppLeaveResult PDFAppLeaveFlow::continueFinishPages()
{
    QPointer<PDFAppLeaveFlow> guard(this);
    while (!m_finishingPages.empty())
    {
        QPointer<PDFAppPage> page = m_finishingPages.front();
        m_finishingPages.erase(m_finishingPages.begin());
        if (!page)
        {
            continue;
        }

        // A refusal cancels the leave: the rest of the pages forget their choices (PDFAppPage::finishLeaves). The page
        // runs code of the application (a commit, its question).
        PDFAppLeaveResult result = page->requestFinishLeave(m_isFinishProceeding && !m_isFinishRefused, this);
        if (!guard)
        {
            return PDFAppLeaveResult::Refused;
        }
        if (result == PDFAppLeaveResult::Pending)
        {
            if (page && waitForStep(page.data(), this, &PDFAppLeaveFlow::onPageFinished))
            {
                return PDFAppLeaveResult::Pending;
            }

            // Nobody can report the step (the flow has ended, the page has gone): the leave is refused
            result = PDFAppLeaveResult::Refused;
        }
        if (result == PDFAppLeaveResult::Refused)
        {
            m_isFinishRefused = true;
        }
    }

    const bool isRefused = m_isFinishRefused;
    m_isFinishProceeding = false;
    m_isFinishRefused = false;
    return isRefused ? PDFAppLeaveResult::Refused : PDFAppLeaveResult::Done;
}

void PDFAppLeaveFlow::onPageFinished(PDFAppLeaveResult result)
{
    if (!isRunning())
    {
        return;
    }
    if (result != PDFAppLeaveResult::Done)
    {
        m_isFinishRefused = true;
    }

    // The waiter of this flow gets the result of all pages (the last call: it may delete the flow)
    QPointer<PDFAppLeaveFlow> guard(this);
    const PDFAppLeaveResult finishResult = continueFinishPages();
    if (guard && finishResult != PDFAppLeaveResult::Pending)
    {
        finishStep(this, finishResult);
    }
}

void PDFAppLeaveFlow::setQuestion(QDialog* question)
{
    if (isRunning())
    {
        m_question = question;
    }
}

QDialog* PDFAppLeaveFlow::getQuestion() const
{
    // A question answered already is hidden until it is deleted
    return m_question && m_question->isVisible() ? m_question.data() : nullptr;
}

void PDFAppLeaveFlow::raiseQuestion()
{
    if (QDialog* question = getQuestion())
    {
        question->raise();
        question->activateWindow();
    }
}

bool PDFAppLeaveFlow::finishStep(QObject* reporter, PDFAppLeaveResult result)
{
    if (result == PDFAppLeaveResult::Pending)
    {
        return false;
    }

    PDFAppLeaveFlowStep* step = findStep(reporter);
    if (!step)
    {
        return false;
    }

    // The waiter is the last call: it may end the flow, delete the owner and the flow
    m_steps.erase(std::remove(m_steps.begin(), m_steps.end(), QPointer<PDFAppLeaveFlowStep>(step)), m_steps.end());
    step->deliver(result);
    return true;
}

bool PDFAppLeaveFlow::isWaitingFor(const QObject* reporter) const
{
    return findStep(reporter) != nullptr;
}

bool PDFAppLeaveFlow::hasPendingSteps() const
{
    for (const QPointer<PDFAppLeaveFlowStep>& step : m_steps)
    {
        if (step && !step->isDelivered())
        {
            return true;
        }
    }
    return false;
}

PDFAppLeaveFlowStep* PDFAppLeaveFlow::createStep(QObject* reporter)
{
    if (!isRunning() || !reporter || findStep(reporter))
    {
        return nullptr;
    }

    // The delivered steps are forgotten (deleted later)
    std::vector<QPointer<PDFAppLeaveFlowStep>> steps;
    for (const QPointer<PDFAppLeaveFlowStep>& waitingStep : m_steps)
    {
        if (waitingStep && !waitingStep->isDelivered())
        {
            steps.push_back(waitingStep);
        }
    }
    m_steps = std::move(steps);

    PDFAppLeaveFlowStep* step = new PDFAppLeaveFlowStep(reporter, this);
    m_steps.emplace_back(step);
    return step;
}

PDFAppLeaveFlowStep* PDFAppLeaveFlow::findStep(const QObject* reporter) const
{
    if (!reporter)
    {
        return nullptr;
    }

    for (const QPointer<PDFAppLeaveFlowStep>& step : m_steps)
    {
        if (step && !step->isDelivered() && step->getReporterKey() == reporter)
        {
            return step.data();
        }
    }
    return nullptr;
}

}   // namespace pdf

#include "moc_pdfappleaveflow.cpp"
