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

#ifndef PDFDOCUMENTSANITIZER_H
#define PDFDOCUMENTSANITIZER_H

#include "pdfdocument.h"
#include "pdfoperationcontrol.h"

#include <functional>
#include <map>
#include <vector>

namespace pdf
{
class PDFAnnotation;
class PDFProgressSink;

/// Class for sanitizing documents. Can remove sensitive content from the document
/// and can also perform selected low-level content stream cleanups.
/// Sanitization is configurable, user can specify which content should be removed.
class PDF4QTLIBCORESHARED_EXPORT PDFDocumentSanitizer : public QObject
{
    Q_OBJECT

public:

    enum SanitizationFlag
    {
        None                = 0x0000, ///< No sanitization is performed
        DocumentInfo        = 0x0001, ///< Remove document information
        Metadata            = 0x0002, ///< Remove all metadata streams in all objects
        Outline             = 0x0004, ///< Remove outline
        FileAttachments     = 0x0008, ///< Remove file attachments
        EmbeddedSearchIndex = 0x0010, ///< Remove embedded search index
        MarkupAnnotations   = 0x0020, ///< Remove markup annotations from all pages
        PageThumbnails      = 0x0040, ///< Remove page thumbnails
        PageLabels          = 0x0080, ///< Remove page labels
        InvisibleText       = 0x0100, ///< Remove text-showing operators executed with rendering mode Tr = 3 from page and form content streams, and the private data of an own OCR layer of a page
        SignatureFields     = 0x0200, ///< Remove the signature fields, their widgets and signature values (names, reasons, certificates), /Perms and /DSS (owner answer Q10: only when it is chosen)
        All                 = 0x01FF, ///< The nine kinds of the old applications; SignatureFields is never a part of it (owner answer Q10)
    };
    Q_DECLARE_FLAGS(SanitizationFlags, SanitizationFlag)

    /// State of the text of the content streams of one page, processed in their order: the
    /// streams of /Contents are one content stream (ISO 32000-2, 7.8.2), so the rendering
    /// mode and the text object go on from one stream to the next one
    struct InvisibleTextState
    {
        int renderingMode = 0;              ///< Tr of the current graphic state
        std::vector<int> stack;             ///< Tr of the saved graphic states (q)
        bool isInTextObject = false;
    };

    explicit PDFDocumentSanitizer(SanitizationFlag flags, QObject* parent);

    /// Removes the text showing operators of the invisible rendering mode (Tr 3) from a
    /// decoded content stream; the other operators stay byte by byte
    /// \param content Decoded content stream
    /// \param state State of the text before the stream, after it on return
    /// \param changed Something was removed
    /// \returns the content without the invisible text (\p content when nothing was removed)
    static QByteArray removeInvisibleText(const QByteArray& content, InvisibleTextState* state, bool* changed);

    /// The annotation is removed by MarkupAnnotations: a markup annotation (comments,
    /// highlights, ... with their replies) and a popup annotation (the window of a text)
    static bool isCommentAnnotation(const PDFAnnotation* annotation);

    /// The field (or the widget of a field) is a signature field: /FT /Sig of itself or of
    /// the nearest parent, which has /FT (the depth bounded by PDFFormField::MAX_FIELD_TREE_DEPTH)
    static bool isSignatureField(const PDFObjectStorage* storage, const PDFDictionary* dictionary);

    /// Steps performed by sanitize in their order (the flags, which are set)
    static std::vector<SanitizationFlag> getSteps(SanitizationFlags flags);

    /// The properties of the XMP metadata of the document, which are the entries of the
    /// document information dictionary, in the order of ISO 32000-2, 14.3.3, Table 349 (and
    /// ISO 19005-1, Table 1): Title dc:title, Author dc:creator, Subject dc:description,
    /// Keywords pdf:Keywords, Creator xmp:CreatorTool, Producer pdf:Producer, CreationDate
    /// xmp:CreateDate, ModDate xmp:ModifyDate, Trapped pdf:Trapped
    static QStringList getDocumentInfoPropertyNames();

    /// What the XMP packet has of the properties of the document information
    struct DocumentInfoProperties
    {
        QStringList names;          ///< The properties found ("dc:title", the prefixes of getDocumentInfoPropertyNames), in their order
        QByteArray metadata;        ///< The packet without them: the other bytes unchanged (the same bytes, when there are none)
        bool isReadable = true;     ///< false: the packet cannot be parsed and its text names a namespace of them, so they may be in it
    };

    /// Removes the properties of the document information from an XMP packet (DocumentInfo
    /// without Metadata: the same data must not stay in the metadata of the document). A
    /// property is recognized by its namespace and its name, not by the prefix, in the element
    /// form and in the attribute form of a child of rdf:Description; everything else - the
    /// declaration of PDF/A (pdfaid), the other properties, the xpacket wrapper and its
    /// padding - stays byte by byte.
    /// \param metadata The XMP packet
    static DocumentInfoProperties removeDocumentInfoProperties(const QByteArray& metadata);

    /// The XMP packet of the catalog (/Metadata), empty for none
    /// \param storage Objects of the document
    /// \param reference Reference of the stream (optional, invalid for none)
    static QByteArray getCatalogMetadata(const PDFObjectStorage* storage, PDFObjectReference* reference);

    /// The properties of the document information, which the last sanitize removed from the
    /// XMP metadata of the document (DocumentInfo without Metadata)
    const QStringList& getRemovedDocumentInfoProperties() const { return m_removedDocumentInfoProperties; }

    /// Sets the cancellation of sanitize, checked between the steps and per page or object,
    /// also in the final optimization (optional, it must outlive sanitize). The storage of
    /// a canceled sanitization is valid, but hidden data may stay in it: it is not to be used.
    void setOperationControl(const PDFOperationControl* operationControl) { m_operationControl = operationControl; }

    /// The operation control has canceled the sanitization
    bool isCanceled() const { return PDFOperationControl::isOperationCancelled(m_operationControl); }

    /// Sets the progress of sanitize: a step begins the phase, which \p phaseOfStep gives to
    /// its flag (PDFProgressSink::beginPhase, in the thread of sanitize); a step without a
    /// phase begins none (optional, the progress must outlive sanitize)
    /// \param progress Progress of the run (may be null)
    /// \param phaseOfStep Index of the phase of a step (a SanitizationFlag)
    void setProgress(PDFProgressSink* progress, std::map<int, int> phaseOfStep) { m_progress = progress; m_phaseOfStep = std::move(phaseOfStep); }

    /// Set document, which should be sanitized
    /// \param document Document to be sanitized
    void setDocument(const PDFDocument* document) { setStorage(document->getStorage()); }

    /// Set storage directly (storage must be valid and filled with objects)
    /// \param storage Storage
    void setStorage(const PDFObjectStorage& storage) { m_storage = storage; }

    /// Perform document sanitization. During optimization process, various
    /// signals are emitted to view progress.
    void sanitize();

    /// Returns object storage used for optimization
    const PDFObjectStorage& getStorage() const { return m_storage; }

    /// Returns object storage by move semantics, old object storage is destroyed
    PDFObjectStorage takeStorage() { return qMove(m_storage); }

    /// Returns sanitized document. Object storage is cleared after
    /// this function call.
    PDFDocument takeSanitizedDocument() { return PDFDocument(qMove(m_storage), PDFVersion(2, 0), QByteArray()); }

    SanitizationFlags getFlags() const;
    void setFlags(SanitizationFlags flags);

signals:
    void sanitizationStarted();
    void sanitizationProgress(QString progressText);
    void sanitizationFinished();

private:
    void performSanitizeDocumentInfo();
    void performSanitizeMetadata();
    void performSanitizeOutline();
    void performSanitizeFileAttachments();
    void performSanitizeEmbeddedSearchIndex();
    void performSanitizeMarkupAnnotations();
    void performSanitizePageThumbnails();
    void performSanitizePageLabels();
    /// Removes invisible OCR-like text from page and form content streams while
    /// keeping the remaining content stream structure intact as much as possible.
    void performSanitizeInvisibleText();
    void performSanitizeSignatureFields();

    /// Removes the trailer entry /Info (after every builder of the steps: PDFDocumentBuilder::build
    /// writes /Producer and /ModDate into the information dictionary)
    void removeDocumentInfoEntry();

    void removeAnnotations(const std::function<bool(const PDFAnnotation*, const PDFDictionary*)>& filter, QString message);

    SanitizationFlags m_flags;
    PDFObjectStorage m_storage;
    QStringList m_removedDocumentInfoProperties;
    const PDFOperationControl* m_operationControl = nullptr;
    PDFProgressSink* m_progress = nullptr;
    std::map<int, int> m_phaseOfStep;
};

}   // namespace pdf

#endif // PDFDOCUMENTSANITIZER_H
