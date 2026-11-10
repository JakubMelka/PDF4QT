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

#include "pdfapptoolfilesink.h"
#include "pdfapptoolpathrules.h"
#include "pdfdocumenttool_split.h"
#include "pdfoperationcontrol.h"
#include "pdfrepositoryhelper.h"
#include "pdfthreadannotations.h"

#include <QDeadlineTimer>
#include <QDir>
#include <QFileInfo>

namespace pdf
{

PDFAppToolFileSink::PDFAppToolFileSink(QString folder, ExistingFiles policy, QStringList protectedPaths, bool isNetworkRefused) :
    BaseClass(std::move(folder), policy),
    m_protectedPaths(std::move(protectedPaths)),
    m_isNetworkRefused(isNetworkRefused)
{

}

PDFAppToolFileSink::~PDFAppToolFileSink() = default;

std::unique_ptr<PDFAppToolFileSink> PDFAppToolFileSink::create(const PDFDocumentTool* tool, const QVariantMap& parameters, const QString& sourceFilePath,
                                                               QStringList protectedPaths, bool isNetworkRefused)
{
    if (!tool || tool->getResultKind(parameters) != PDFDocumentToolResultKind::Files)
    {
        return nullptr;
    }

    const QString folderKey = QString::fromLatin1(PDFDocumentToolFileOutput::PARAMETER_FOLDER);
    const QString fileNameKey = QString::fromLatin1(PDFDocumentToolFileOutput::PARAMETER_FILE_NAME);
    bool hasFolder = false;
    bool hasFileName = false;
    for (const PDFDocumentToolParameterDefinition& definition : tool->getParameters())
    {
        hasFolder = hasFolder || definition.getName() == folderKey;
        hasFileName = hasFileName || definition.getName() == fileNameKey;
    }
    if (!hasFolder || !hasFileName)
    {
        return nullptr;
    }

    QString folder = parameters.value(folderKey).toString().trimmed();
    if (folder.isEmpty() && !sourceFilePath.isEmpty())
    {
        folder = QFileInfo(sourceFilePath).absolutePath();
    }
    if (!sourceFilePath.isEmpty())
    {
        const QString canonicalSource = QFileInfo(sourceFilePath).canonicalFilePath();
        protectedPaths << (canonicalSource.isEmpty() ? QDir::cleanPath(sourceFilePath) : canonicalSource);
    }
    return std::make_unique<PDFAppToolFileSink>(folder, PDFDocumentToolFileOutput::getPolicy(parameters), std::move(protectedPaths), isNetworkRefused);
}

bool PDFAppToolFileSink::begin(const QStringList& fileNames, QString* errorText)
{
    QString dummy;
    QString& error = errorText ? *errorText : dummy;

    // T1 of the first batch: never a device; a folder of another computer only by the user
    const QString folder = getFolder();
    if (PDFRepositoryHelper::isDevicePath(folder) || (m_isNetworkRefused && PDFAppToolPathRules::isDeviceOrNetworkPath(folder)))
    {
        error = tr("The files cannot be written into this folder.");
        return false;
    }

    const QString canonicalFolder = QFileInfo(folder).canonicalFilePath();
    if (!canonicalFolder.isEmpty() && PDFAppToolPathRules::resolveJunctions(canonicalFolder).isEmpty())
    {
        error = tr("The files cannot be written into this folder.");
        return false;
    }

    if (!BaseClass::begin(fileNames, &error))
    {
        return false;
    }
    m_remainingConflicts = int(getConflictFiles().size());
    return true;
}

bool PDFAppToolFileSink::isStopped() const
{
    return m_isStopped.load(std::memory_order_acquire) || PDFOperationControl::isOperationCancelled(m_control);
}

std::optional<PDFAppToolFileSink::Question> PDFAppToolFileSink::getQuestion() const
{
    QMutexLocker locker(&m_mutex);
    return m_question;
}

void PDFAppToolFileSink::answer(Answer answer, bool isForAll)
{
    QMutexLocker locker(&m_mutex);
    if (!m_question)
    {
        return;
    }
    m_answer = answer;
    if (isForAll)
    {
        // Read by the thread of the run after its wake, under the mutex
        m_answerForAll = answer;
    }
    PDFThreadAnnotations::happensBefore(&m_condition);
    m_condition.wakeAll();
}

void PDFAppToolFileSink::stop()
{
    QMutexLocker locker(&m_mutex);
    m_isStopped.store(true, std::memory_order_release);
    PDFThreadAnnotations::happensBefore(&m_condition);
    m_condition.wakeAll();
}

PDFDocumentToolFileSink::ExistingFiles PDFAppToolFileSink::resolveConflict(const QString& fileName, bool isProtected)
{
    m_remainingConflicts = qMax(0, m_remainingConflicts - 1);
    if (getPolicy() != ExistingFiles::Ask)
    {
        return BaseClass::resolveConflict(fileName, isProtected);
    }

    QMutexLocker locker(&m_mutex);
    if (!m_answerForAll)
    {
        // 356:11997: the run waits for the decision, the GUI polls the question
        Question question;
        question.fileName = fileName;
        question.folderName = QDir(getFolder()).dirName();
        question.conflictCount = int(getConflictFiles().size());
        question.otherCount = m_remainingConflicts;
        question.isProtected = isProtected;
        m_question = question;
        m_answer.reset();

        while (!m_answer && !isStopped())
        {
            PDFThreadAnnotations::wait(m_condition, &m_mutex, QDeadlineTimer(WAIT_INTERVAL));
        }
        PDFThreadAnnotations::happensAfter(&m_condition);
        m_question.reset();
    }

    const std::optional<Answer> decision = m_answerForAll ? m_answerForAll : m_answer;
    m_answer.reset();
    if (!decision || *decision == Answer::StopExport)
    {
        m_isStopped.store(true, std::memory_order_release);
        return ExistingFiles::Skip;
    }

    switch (*decision)
    {
        case Answer::Overwrite:
            return ExistingFiles::Overwrite;
        case Answer::Rename:
            return ExistingFiles::Rename;
        case Answer::Skip:
        case Answer::StopExport:
            break;
    }
    return ExistingFiles::Skip;
}

bool PDFAppToolFileSink::isProtectedPath(const QString& filePath) const
{
    const QString canonicalPath = QFileInfo(filePath).canonicalFilePath();
    const QString path = canonicalPath.isEmpty() ? QDir::cleanPath(filePath) : canonicalPath;
    for (const QString& protectedPath : m_protectedPaths)
    {
        if (PDFRepositoryHelper::isSamePath(path, protectedPath))
        {
            return true;
        }
    }
    return false;
}

bool PDFAppToolFileSink::checkFileName(const QString& fileName, QString* errorText) const
{
    const PDFAppToolPathRules::NameProblem problem = PDFAppToolPathRules::checkFileName(fileName);
    if (problem != PDFAppToolPathRules::NameProblem::None)
    {
        *errorText = PDFAppToolPathRules::getNameProblemText(problem);
        return false;
    }
    return BaseClass::checkFileName(fileName, errorText);
}

PDFAppToolMemoryFileSink::PDFAppToolMemoryFileSink() :
    PDFDocumentToolFileSink(ExistingFiles::Rename)
{

}

PDFAppToolMemoryFileSink::~PDFAppToolMemoryFileSink() = default;

bool PDFAppToolMemoryFileSink::write(const QString& fileName, const QByteArray& data, QString* errorText)
{
    if (errorText)
    {
        errorText->clear();
    }
    m_files.emplace_back(fileName, data);
    addWrittenFile(fileName);
    return true;
}

}   // namespace pdf
