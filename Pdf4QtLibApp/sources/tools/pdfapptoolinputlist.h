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

#ifndef PDFAPPTOOLINPUTLIST_H
#define PDFAPPTOOLINPUTLIST_H

#include "pdfappglobal.h"
#include "pdfapptoolinputloader.h"

#include "pdfaccenthue.h"
#include "pdfdocument.h"
#include "pdfdocumenttool.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QImage>
#include <QObject>
#include <QPointer>
#include <QSize>
#include <QSizeF>
#include <QString>
#include <QStringList>

#include <map>
#include <memory>
#include <vector>

namespace pdf
{

class PDFAppDocument;
class PDFAppThumbnailSource;

/// One input of the list: plain values and shared immutable data. A copy of it may go to a
/// worker (the document is an immutable shared pointer, the bytes are implicitly shared and
/// only read); it never holds a pointer to a QObject (R11: no pointer to PDFAppDocument).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolInput
{
public:
    QString id;                         ///< Stable id within its list ("i1", "i2", ...; never reused by the list); the parameter "inputs" of a tool holds these ids
    PDFAppToolInputKind kind = PDFAppToolInputKind::File;
    PDFAppToolInputState state = PDFAppToolInputState::Loading;
    QString name;                       ///< Shown name (name of the file, title of the tab, "Pasted image 1")
    QString filePath;                   ///< Canonical path of the file (empty: clipboard, an unsaved tab)
    QString documentId;                 ///< OpenTab: id of the document of the tab when it was added (Tag "open tab" while that tab is open)
    QString reason;                     ///< Translated reason of a state other than Ready / Loading (empty otherwise)
    bool isEncrypted = false;           ///< The file is encrypted (a password was or is needed)
    bool isPasswordWrong = false;       ///< The last password given was wrong (PasswordRequired, Restricted)
    bool isRepaired = false;            ///< Ready, but read by the permissive reader from a damaged file (Tag "Repaired")
    bool isOwnerPasswordVerified = false;   ///< OpenTab: the owner password of the restricted snapshot was verified (Q15), the core permits it
    bool isModified = false;            ///< OpenTab: the tab had unsaved changes - its working copy is used, not the file
    PDFInteger pageCount = 0;           ///< Pages of the input (PDF pages, image frames); 0 until Ready / NoPages
    QSizeF firstPageSize;               ///< PDF: the rotated crop box of the first page in points (empty = unknown)
    qint64 fileSize = 0;                ///< Bytes of the input (the file, the PNG of the clipboard; 0 = unknown)
    quint64 contentGeneration = 0;      ///< Generation of the list, when the content of the input changed last (the keys of its pictures)

    // PDF inputs (Ready, Restricted, NoPages)
    PDFDocumentPointer document;        ///< The document (also the snapshot of an open tab)
    QByteArray originalData;            ///< Bytes of the file (a second reading with a password, the signatures of the core)

    // Image inputs (Image, Clipboard)
    QByteArray imageData;               ///< Bytes of the image file (the core and the previews decode them)
    QByteArray imageFormat;             ///< Format of QImageReader ("png", "jpeg", "tiff")
    QSize imageSize;                    ///< Size of the first frame in pixels (after the EXIF orientation)
    double imageDpiX = 0.0;             ///< Resolution stored in the file (0 = none; the core applies its limits, R15)
    double imageDpiY = 0.0;

    /// The input counts for a run: Ready with at least one page
    bool isUsable() const { return state == PDFAppToolInputState::Ready && pageCount > 0; }

    /// The input is a PDF (an open tab or a PDF file)
    bool isPdf() const { return kind == PDFAppToolInputKind::OpenTab || (kind == PDFAppToolInputKind::File); }

    /// The input is an image (an image file or the clipboard)
    bool isImage() const { return kind == PDFAppToolInputKind::Image || kind == PDFAppToolInputKind::Clipboard; }

    /// The state needs the user (a password, a removal): 378:2613 "2 need attention"
    bool needsAttention() const;

    /// Input document of the core for a usable PDF input (PDFDocumentToolContext::documents,
    /// PDFAppToolRunEnvironment::additionalDocuments); the file name is the name of the input,
    /// the input id is the id of the input
    PDFDocumentToolInputDocument createToolInputDocument() const;

    /// Input image of the core for a usable image input (PDFDocumentToolContext::images): the
    /// bytes of the file, its format, the name and the id; the resolution of the file (the core
    /// applies its limits, R15)
    PDFDocumentToolInputImage createToolInputImage() const;
};

/// The inputs of one tool with more inputs (Merge; plan step 2, 4.4, R11, R12, R13, R15, K15,
/// K22, K29, K33, K39, K40): the model of the panel Files, of the multi-input start page
/// (531:3208) and of Add Files. The list owns the documents and bytes of its inputs, reads
/// the files by its PDFAppToolInputLoader (a child) in the threads of PDFExecutionPolicy and
/// keeps removed inputs parked for Undo / Redo.
///
/// Thread: GUI thread only (the loader hands its results back by a queued call).
///
/// Ownership (checklist point 1): the list is a QObject without a widget. The start page
/// creates it as its child, PDFAppMainWindow::createToolTab takes it over (setParent, one
/// synchronous call, R13), then the own tool tab (PDFAppToolTab) owns it, so the list outlives
/// the shell (Discard result keeps the inputs); the GUI of a tool holds it by a QPointer. The inputs are values owned by the list: an open tab is a snapshot taken
/// when it is added (PDFAppDocument::getSnapshot) - the list keeps no pointer to the
/// PDFAppDocument, the tab may close at any time and the input stays (R11). A running reading
/// is dropped by the destructor (nobody waits for the worker, its late result is lost).
///
/// Order and identity: the order of the list is the order of the result. The letter (A, B,
/// ..., Z, AA, AB, ...) and the color of an input come from its position (getLetter, getHue),
/// the id is stable for the whole life of the list.
///
/// Undo / Redo (R12): removeInput parks the input (its document, bytes, password state and
/// reason stay); setInputIds with a parked id takes it back without a second reading and
/// without a second question for the password. The owner of the Undo history drops the
/// parked inputs, which the history does not reference any more (dropParkedInputs).
///
/// Generation (K33): every change of the content of an input (a reading finished, a password
/// accepted, an input added, removed or moved) gives a new generation; the runner adds it to
/// the key of its preview cache.
///
/// Signals: emitted last (the state is changed before), a slot may delete the list.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolInputList : public QObject
{
    Q_OBJECT

private:
    using BaseClass = QObject;

public:
    /// Most inputs of one list (plan 6: GUI 1 000, MCP 100 by its schema)
    static constexpr int MAXIMUM_INPUTS = 1000;

    /// Most pixels of an image of the clipboard (the same limit as the allocation limit of
    /// the image files, R15)
    static constexpr qint64 MAXIMUM_CLIPBOARD_PIXELS = PDFAppToolInputLoader::MAXIMUM_IMAGE_PIXELS;

    /// Why an input was not added
    enum class Refusal
    {
        None,           ///< Added
        Duplicate,      ///< The same file (canonical path) or the same open document is in the list already (K29)
        TooMany,        ///< The list has MAXIMUM_INPUTS inputs
        NotReady,       ///< The open document is not read yet (it is loading, it needs its password)
        Invalid,        ///< No file, an empty image, no document
        ImageTooLarge   ///< An image of the clipboard larger than MAXIMUM_CLIPBOARD_PIXELS
    };

    /// Result of an addition
    struct AddResult
    {
        QString id;                         ///< Id of the new input (empty: refused)
        Refusal refusal = Refusal::None;
        QString name;                       ///< Name of the refused item (the text of the status bar)
    };

    explicit PDFAppToolInputList(QObject* parent);
    virtual ~PDFAppToolInputList() override;

    // ---------------------------------------------------------------- reading

    /// Number of the inputs (parked ones are not counted)
    int getCount() const { return int(m_inputs.size()); }

    /// Input at \p index (the order of the result), nullptr for an invalid index
    const PDFAppToolInput* getInput(int index) const;

    /// Input of the id (also a parked one), nullptr if there is none
    const PDFAppToolInput* findInput(const QString& id) const;

    /// Index of the input of the id, -1 for none (or a parked one)
    int indexOf(const QString& id) const;

    /// Ids of the inputs in their order (the value of the parameter "inputs")
    QStringList getInputIds() const;

    /// The inputs are parked: removed from the list, kept for Undo / Redo
    bool isParked(const QString& id) const;

    /// Id of the parked input of the file (canonical path), empty if there is none (the
    /// chooser checks a file again without a second reading)
    QString findParkedFile(const QString& filePath) const;
    int getParkedCount() const { return int(m_parked.size()); }
    QStringList getParkedIds() const;

    /// Number of the usable inputs (K22: Create PDF needs at least two)
    int getUsableCount() const;

    /// Number of the inputs being read
    int getLoadingCount() const;

    /// Number of the inputs, which need the user (378:2613 "4 files · 2 need attention")
    int getAttentionCount() const;

    /// Every input is usable and there is at least \p minimumCount of them: the tool can run
    /// (378:2613 "Create PDF is available when every file in the list can be read")
    bool isRunnable(int minimumCount = 2) const;

    /// Sum of the pages of the usable inputs and of their sizes (the header of Files,
    /// "3 files · 16 pages · 18.6 MB")
    PDFInteger getTotalPageCount() const;
    qint64 getTotalSize() const;

    /// Generation of the content of the inputs (K33): a new number for every change
    quint64 getGeneration() const { return m_generation; }

    /// The file (canonical path) or the open document is in the list (parked inputs do not
    /// count: Undo sets the inputs exactly, two inputs of one file are never active together)
    bool containsFile(const QString& filePath) const;
    bool containsDocument(const QString& documentId) const;

    /// Percentage of the reading of a loading input (0-100), -1 when it is not known yet
    int getLoadingProgress(const QString& id) const;

    /// Letter of the input at the index: A ... Z, AA, AB, ... (R20)
    static QString getLetter(int index);

    /// Color of the input at the index (K15 as changed by K40): the cycle blue, green, violet,
    /// teal - red and amber are left out, they mean an error and a partial result; an input
    /// which cannot be used is neutral
    static PDFAccentHue getHue(int index, const PDFAppToolInput* input = nullptr);

    /// Translated text of a refusal ("Contract.pdf is already in the list")
    static QString getRefusalText(Refusal refusal, const QString& name);

    /// Translated short text of a state ("Protected by a password", "Damaged – the file
    /// cannot be read", "Restricted – enter the owner password", "Loading…", "No pages",
    /// "Not supported", empty for Ready)
    static QString getStateText(PDFAppToolInputState state);

    /// Source of the picture of a page of an input for the scheduler of the thumbnails:
    /// a PDF page (the document, the page) or a frame of an image (its bytes); an invalid
    /// source for a page out of range or an input, which is not usable
    PDFAppThumbnailSource getPictureSource(const QString& id, int pageIndex) const;

    /// Key of the picture of the page of the input in QPixmapCache (plan 4.4: the id of the
    /// list, the id of the input, its generation, the page and the size); the rotation of a
    /// page is drawn by the delegate, it is not a part of the key
    QString getPictureKey(const QString& id, int pageIndex, const QSize& size, qreal devicePixelRatio) const;

    // ---------------------------------------------------------------- changes

    /// Adds a file at the end (a PDF or an image by its content; it is read in a worker,
    /// the input is Loading until then)
    AddResult addFile(const QString& filePath);

    /// Adds files at the end in their order (the drop order, 531:3208)
    std::vector<AddResult> addFiles(const QStringList& filePaths);

    /// Adds a snapshot of an open document at the end (R11: the snapshot of now, the tab is
    /// never followed); a document which is not ready is refused (NotReady)
    AddResult addOpenDocument(const PDFAppDocument* document);

    /// Adds an image of the clipboard at the end (Paste image, stored as PNG bytes)
    AddResult addImage(const QImage& image, const QString& name);

    /// Moves the input to the index (clamped). \returns false, when nothing moved.
    bool moveInput(const QString& id, int index);

    /// Removes the input: it is parked (Undo takes it back). \returns false for an unknown id.
    bool removeInput(const QString& id);

    /// Sets the inputs and their order by the ids (Undo / Redo of the parameter "inputs"):
    /// a parked id comes back without a second reading, an input which is not in \p ids is
    /// parked, an unknown id is skipped. \returns false, when nothing changed.
    bool setInputIds(const QStringList& ids);

    /// Forgets the parked inputs, which are not in \p keptIds (the history does not refer to
    /// them any more): their documents and bytes are freed
    void dropParkedInputs(const QStringList& keptIds);

    /// Reads the input again with the password: the password of the user (PasswordRequired)
    /// or the owner password (Restricted). A restricted snapshot of an open tab is not read
    /// again: the owner password is only verified (Q15, its unsaved changes stay the input).
    /// The password is held only by the request of the reading (never logged, never stored).
    /// \returns false, when the input does not wait for a password or is being read.
    bool setPassword(const QString& id, const QString& password);

    /// Loader of the list (tests)
    PDFAppToolInputLoader* getLoader() const { return m_loader.data(); }

    /// Number of the readings started since the list was created (tests: Undo / Redo reads nothing)
    int getStartedReadingCount() const { return m_startedReadingCount; }

Q_SIGNALS:
    /// Inputs were added, removed or moved (the rows are built again)
    void inputsChanged();

    /// The state or the content of an input has changed (a reading finished, a password)
    void inputChanged(const QString& id);

private:
    void onReadingFinished(const QString& id, const pdf::PDFAppToolInputLoader::Result& result);
    QString createId();
    AddResult addInput(PDFAppToolInput input, bool isReadingNeeded);
    void startReading(const PDFAppToolInput& input, bool hasPassword, const QString& password, bool isOwnerPassword);
    void advanceGeneration();
    static void applyResult(PDFAppToolInput* input, const PDFAppToolInputLoader::Result& result);
    PDFAppToolInput* findMutableInput(const QString& id);

    QPointer<PDFAppToolInputLoader> m_loader;
    std::vector<PDFAppToolInput> m_inputs;
    std::map<QString, PDFAppToolInput> m_parked;
    QString m_listId;               ///< Unique in the process (the keys of the pictures)
    quint64 m_generation = 1;
    int m_nextId = 1;
    int m_clipboardCount = 0;
    int m_startedReadingCount = 0;
};

}   // namespace pdf

#endif // PDFAPPTOOLINPUTLIST_H
