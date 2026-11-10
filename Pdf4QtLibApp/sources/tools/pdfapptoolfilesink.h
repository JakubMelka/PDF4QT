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

#ifndef PDFAPPTOOLFILESINK_H
#define PDFAPPTOOLFILESINK_H

#include "pdfappglobal.h"
#include "pdfdocumenttool.h"

#include <QCoreApplication>
#include <QMutex>
#include <QWaitCondition>

#include <atomic>
#include <memory>
#include <optional>

namespace pdf
{

class PDFOperationControl;

/// The file sink of the application (plan of step 3 of phase 2, 4.2, 9.3, R3, R14, R17): the
/// folder sink of the core with the rules of the paths of the application and the question of
/// a conflict (356:11997) - for every tool, whose result is more files (Split, Extract one file
/// per page, the exports of Convert).
///
///  - The names: PDFAppToolPathRules::checkFileName; the folder: never a device, never a path of
///    another computer when \p isNetworkRefused (MCP, the rule of save_file); junctions resolved.
///  - Protected paths (R14): the source and the files open in the tabs are never overwritten
///    (Overwrite gives a not written file, Ask asks with the reason).
///  - Ask (only the GUI; MCP never asks, R17): the conflicts are counted at begin ("37 files"),
///    at the first one the thread of the run waits (PDFThreadAnnotations::wait, by intervals,
///    checking the control and stop) until the GUI thread, which polls getQuestion, answers by
///    answer; "Do the same for the other N files" keeps the answer. Stop Export = stop.
///
/// Ownership and threads: the job of the runner owns it (std::unique_ptr) for the whole run, the
/// runner and the GUI reach it only through the job (a shared_ptr); begin, write, isStopped and
/// resolveConflict run in the thread of the run, getQuestion, answer and stop in any thread
/// (a mutex); the lists of the base are read after the run. No QObject, no signal.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolFileSink : public PDFDocumentToolFolderSink
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFAppToolFileSink)

private:
    using BaseClass = PDFDocumentToolFolderSink;

public:
    /// Interval of a wait for the answer (ms): the control is checked after each
    static constexpr int WAIT_INTERVAL = 100;

    /// The answer of the question of a conflict (356:11997)
    enum class Answer
    {
        Overwrite,
        Skip,
        Rename,
        StopExport
    };

    /// The question of a conflict, which waits for an answer
    struct Question
    {
        QString fileName;           ///< Name of the file, which exists ("Report_17.png")
        QString folderName;         ///< Name of the folder ("Exports")
        int conflictCount = 0;      ///< Files of the run, which existed at its start ("37 files")
        int otherCount = 0;         ///< Further conflicts after this one ("the other 36 files")
        bool isProtected = false;   ///< The file is a source or open in a tab: Overwrite is not possible

        bool operator==(const Question&) const = default;
    };

    /// \param folder The folder of the files
    /// \param policy Existing files
    /// \param protectedPaths Paths, which are never overwritten (the source, the open documents)
    /// \param isNetworkRefused A folder of another computer is refused (MCP)
    PDFAppToolFileSink(QString folder, ExistingFiles policy, QStringList protectedPaths, bool isNetworkRefused);
    virtual ~PDFAppToolFileSink() override;

    /// The sink of a run of \p tool with \p parameters, nullptr when the run writes no files
    /// itself (its result kind is not Files, or it has no parameters of the panel Output files,
    /// PDFDocumentToolFileOutput). The folder is the parameter folder, empty = the folder of the
    /// source; the source is protected too.
    /// \param tool The tool
    /// \param parameters The parameters of the run
    /// \param sourceFilePath Path of the file of the first input, may be empty
    /// \param protectedPaths Paths of the open documents
    /// \param isNetworkRefused A folder of another computer is refused (MCP)
    static std::unique_ptr<PDFAppToolFileSink> create(const PDFDocumentTool* tool, const QVariantMap& parameters, const QString& sourceFilePath,
                                                      QStringList protectedPaths, bool isNetworkRefused);

    /// The control of the run: a canceled run stops the wait (the job sets it before the run)
    void setOperationControl(const PDFOperationControl* control) { m_control = control; }

    virtual bool begin(const QStringList& fileNames, QString* errorText) override;
    virtual bool isStopped() const override;

    /// The question, which waits for an answer, std::nullopt = none (any thread)
    std::optional<Question> getQuestion() const;

    /// Answers the question (any thread); \p isForAll: the other conflicts get the same answer
    void answer(Answer answer, bool isForAll);

    /// Stops the sink (Stop, the close of the tab): a waiting question ends, no further file
    void stop();

protected:
    virtual ExistingFiles resolveConflict(const QString& fileName, bool isProtected) override;
    virtual bool isProtectedPath(const QString& filePath) const override;
    virtual bool checkFileName(const QString& fileName, QString* errorText) const override;

private:
    QStringList m_protectedPaths;
    bool m_isNetworkRefused;
    const PDFOperationControl* m_control = nullptr;
    std::atomic<bool> m_isStopped = false;

    mutable QMutex m_mutex;
    QWaitCondition m_condition;
    std::optional<Question> m_question;     ///< Guarded by m_mutex
    std::optional<Answer> m_answer;         ///< Guarded by m_mutex
    std::optional<Answer> m_answerForAll;   ///< Only the thread of the run
    int m_remainingConflicts = 0;           ///< Only the thread of the run
};

/// A file sink, which keeps the files in the memory (the result save_files of MCP, R17): the
/// run makes the files, the permission of the files is asked after the run as for every
/// result of an assistant (save_file), then the files are written by a PDFAppToolFileSink.
/// The bytes of a split are about the bytes of the document. Threads: as its base.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolMemoryFileSink : public PDFDocumentToolFileSink
{
public:
    PDFAppToolMemoryFileSink();
    virtual ~PDFAppToolMemoryFileSink() override;

    virtual bool write(const QString& fileName, const QByteArray& data, QString* errorText) override;

    /// The files in the order of their writes (name, bytes)
    const std::vector<std::pair<QString, QByteArray>>& getFiles() const { return m_files; }

private:
    std::vector<std::pair<QString, QByteArray>> m_files;
};

}   // namespace pdf

#endif // PDFAPPTOOLFILESINK_H
