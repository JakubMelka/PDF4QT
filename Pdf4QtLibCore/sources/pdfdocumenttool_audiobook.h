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

#ifndef PDFDOCUMENTTOOL_AUDIOBOOK_H
#define PDFDOCUMENTTOOL_AUDIOBOOK_H

#include "pdfdocumenttextflow.h"
#include "pdfdocumenttool.h"
#include "pdfspeechsynthesizer.h"

#include <QMetaType>
#include <QRegularExpression>
#include <QStringList>

#include <memory>

namespace pdf
{

/// The edited text stream of the panel of Audio Book (a panel-only Value parameter, the pattern of
/// the assembly of Manage Pages, RB7): implicitly shared and immutable, compared by the identity of
/// its data, never stored, never in the schema of MCP. The run gets a copy of the flow (plan 6:
/// "the run gets a copy of the edited stream").
class PDF4QTLIBCORESHARED_EXPORT PDFDocumentToolAudioBookStream
{
public:
    PDFDocumentToolAudioBookStream() = default;
    explicit PDFDocumentToolAudioBookStream(PDFDocumentTextFlow flow);

    /// \param flow The items, which are read (PDFDocumentTextFlowEditor::createEditedTextFlow)
    /// \param itemCount All items of the stream of the panel, also the skipped ones (the fact
    ///        Items read "1,712 of 1,840", 341:11159)
    PDFDocumentToolAudioBookStream(PDFDocumentTextFlow flow, qint64 itemCount);

    /// The state of the editor of the panel (its items with their Read / Skip, texts and order;
    /// no selection), the item count its items. Undo and Redo of the panel restore the editor from
    /// the value. Only the editor is kept (its original flow is shared by all copies of the editor):
    /// a step of Undo holds one copy of the items, the run creates the edited flow (createFlow).
    explicit PDFDocumentToolAudioBookStream(const PDFDocumentTextFlowEditor& editor);

    /// The stream given as a flow (nullptr = a stream of an editor, or none)
    const PDFDocumentTextFlow* getFlow() const { return m_flow.get(); }

    /// No stream: the tool builds the stream by its parameters
    bool isEmpty() const { return !m_flow && !m_editor; }

    /// The items, which are read: the flow, or the edited flow of the editor
    /// (PDFDocumentTextFlowEditor::createEditedTextFlow); empty for no stream
    PDFDocumentTextFlow createFlow() const;

    /// The editor of the panel, nullptr = the stream was given as a flow
    const PDFDocumentTextFlowEditor* getEditor() const { return m_editor.get(); }

    /// The identity of the data of the value (nullptr = no stream)
    const void* getIdentity() const { return m_editor ? static_cast<const void*>(m_editor.get()) : static_cast<const void*>(m_flow.get()); }

    /// All items of the stream of the panel (-1 = not known: the read items are all)
    qint64 getItemCount() const { return m_itemCount; }

    bool operator==(const PDFDocumentToolAudioBookStream& other) const { return getIdentity() == other.getIdentity(); }

private:
    std::shared_ptr<const PDFDocumentTextFlow> m_flow;
    std::shared_ptr<const PDFDocumentTextFlowEditor> m_editor;
    qint64 m_itemCount = -1;
};

/// The tool Create Audio Book (convert.audiobook, Figma 341:9229 ... 341:11665; plan of step 4 of
/// phase 2, 4.7, K4.7, owner answers Q4.1 - Q4.3, Q4.5, R4-13 - R4-15; package P4). The reading
/// stream of PDFDocumentTextFlowFactory (setReadingStream; or the edited stream of the panel) is
/// spoken by the synthesizer of the context (PDFDocumentToolContext::speechSynthesizerFactory; none
/// = Failed "No text-to-speech engine") into a temporary file of the result
/// (PDFDocumentToolTemporaryFile, R4-1) through PDFAudioFileWriter as WAV (owner 14. 11.: WAV only,
/// no MP3 encoder for now). Silence 300 ms between the items, 600 ms between the pages. An item of
/// a language (/Lang, ISO 32000-2 14.9.2), which a voice of the engine speaks, is read by that
/// voice; else by the chosen voice. Stop deletes the temporary file ("nothing is kept",
/// 341:10706); an error of the engine is Failed without a file.
///
/// Parameters: pages, order (auto | layout | content | structure), voice (the name of a voice of
/// the engine; empty = the default; an unknown name is an error with the names of the voices),
/// rate (-1 to 1), volume (0 to 1), format (wav), skip (a regular expression of the items, which
/// are not read; at most 1000 characters), readSpecial (R4-15: the special items, which are read,
/// separated by commas: page-numbers, titles, alt, expanded, actual-text, artifacts; the default is
/// "actual-text,expanded" - the replacements of ISO 32000-2 14.9.4 and 14.9.5 are read instead of
/// the text they replace; without them the replaced text is read), editedStream (panel-only Value,
/// PDFDocumentToolAudioBookStream: every item of it with a text is read, in its order).
///
/// Result: Files - one output, a temporary file ("<name>.wav", "audio/wav"); facts "items",
/// "length", "size", "voice".
class PDF4QTLIBCORESHARED_EXPORT PDFDocumentToolAudioBook : public PDFDocumentTool
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFDocumentToolAudioBook)

public:
    static constexpr const char* TOOL_ID = "convert.audiobook";

    /// Names of the parameters
    static constexpr const char* PARAMETER_PAGES = "pages";
    static constexpr const char* PARAMETER_ORDER = "order";
    static constexpr const char* PARAMETER_VOICE = "voice";
    static constexpr const char* PARAMETER_RATE = "rate";
    static constexpr const char* PARAMETER_VOLUME = "volume";
    static constexpr const char* PARAMETER_FORMAT = "format";
    static constexpr const char* PARAMETER_SKIP = "skip";
    static constexpr const char* PARAMETER_READ_SPECIAL = "readSpecial";
    static constexpr const char* PARAMETER_EDITED_STREAM = "editedStream";

    /// Identifiers of Order (341:9657)
    static constexpr const char* ORDER_AUTO = "auto";
    static constexpr const char* ORDER_LAYOUT = "layout";
    static constexpr const char* ORDER_CONTENT = "content";
    static constexpr const char* ORDER_STRUCTURE = "structure";

    /// Identifier of the format (341:9229 File format; WAV only, owner 14. 11.)
    static constexpr const char* FORMAT_WAV = "wav";

    /// Kinds of readSpecial (R4-15; PdfTool --say-page-numbers, --say-struct-*)
    static constexpr const char* SPECIAL_PAGE_NUMBERS = "page-numbers";
    static constexpr const char* SPECIAL_TITLES = "titles";
    static constexpr const char* SPECIAL_ALT = "alt";
    static constexpr const char* SPECIAL_EXPANDED = "expanded";
    static constexpr const char* SPECIAL_ACTUAL_TEXT = "actual-text";
    static constexpr const char* SPECIAL_ARTIFACTS = "artifacts";

    /// Longest regular expression of skip (6: catastrophic backtracking is bounded by the length,
    /// the match limit of PCRE2 and the cancellation after every item)
    static constexpr int MAXIMUM_SKIP_LENGTH = 1000;

    /// Largest number of the steps of the match of one item (the match limit of PCRE2)
    static constexpr int MAXIMUM_MATCH_STEPS = 100000;

    /// The regular expression of skip and of Select by regular expression of the panel (plan 6):
    /// the pattern of the user with the lower match limit of PCRE2 ((*LIMIT_MATCH)), so the
    /// catastrophic backtracking of a pattern ends every item quickly - an item, which reaches the
    /// limit, does not match. The check of the popover runs in the GUI thread at every key.
    static QRegularExpression createExpression(const QString& pattern);

    /// Characters of a text spoken in one second at the normal speed (about 150 words a minute):
    /// the estimate of the length (341:9229 Summary)
    static constexpr double CHARACTERS_PER_SECOND = 15.0;

    /// Bytes of one second of the audio of the estimate: 16 bit mono at 16 kHz (the voices of
    /// Windows); the file has the rate of the voice
    static constexpr qint64 ESTIMATED_BYTES_PER_SECOND = 32000;

    virtual QString getId() const override;
    virtual PDFDocumentToolParameterDefinitions getParameters() const override;
    virtual std::vector<PDFDocumentToolParameterError> validate(const PDFDocumentToolContext& context, const QVariantMap& parameters) const override;

    /// The WAV file is no text (MCP: save_file only)
    virtual bool isTextOutput() const override { return false; }
    virtual PDFDocumentToolResult execute(const PDFDocumentToolContext& context, const QVariantMap& parameters, const PDFOperationControl* control, PDFProgressSink* progress) const override;

    /// The algorithm of the text stream of the parameter order
    static PDFDocumentTextFlowFactory::Algorithm getAlgorithm(const QVariantMap& parameters);

    /// The identifier of an algorithm ("structure")
    static QString getOrderId(PDFDocumentTextFlowFactory::Algorithm algorithm);

    /// The kinds of readSpecial (trimmed, lower case); \p errorMessage gets an unknown kind
    static QStringList getReadSpecial(const QVariantMap& parameters, QString* errorMessage);

    /// The kind of readSpecial of a special item ("page-numbers"), empty for a text item and for an
    /// item, which is never read (the ends of the pages and of the structure, the languages)
    static QString getSpecialKind(const PDFDocumentTextFlow::Item& item);

    /// The item is read, when the user does not change it (the editor of the panel starts with it,
    /// the run without an edited stream reads by it): a text with a text, which no read replacement
    /// replaces (ReplacedText), and a special item of a kind of \p readSpecial
    static bool isReadByDefault(const PDFDocumentTextFlow::Item& item, const QStringList& readSpecial);

    /// The reading stream of the pages and the order of the parameters (no edited stream): the
    /// stream of the panel (Rebuild) and of a run. Reentrant; the cancellation and the progress
    /// per page. Canceled = an empty stream. The items have their rectangles, never the rectangles
    /// of their characters (the panel keeps a copy of the stream in every step of Undo).
    /// \param document The document
    /// \param parameters The normalized parameters
    /// \param control Cancellation, may be null
    /// \param progress Progress, may be null
    /// \param errorMessage Reason of an empty stream (no page)
    static PDFDocumentTextFlow createStream(const PDFDocument* document, const QVariantMap& parameters, const PDFOperationControl* control, PDFProgressSink* progress, QString* errorMessage);

    /// Estimated length of the speech of \p characters characters at the speed \p rate (-1 to 1:
    /// half to double the speed), milliseconds
    static qint64 estimateDuration(qint64 characters, double rate);

    /// The length of an audio book ("4 h 18 min", "12 min", "40 s"; 341:11159)
    static QString formatLength(qint64 milliseconds);

    /// The voice of the language \p language of the voices of the engine: the voice of the same
    /// language and region ("en-US"), else of the same language ("en"); \p fallback when the language
    /// is empty or no voice speaks it, or when \p fallback speaks it already. An empty \p fallback
    /// (no voice chosen) is the voice \p defaultVoice of the engine: it stays (an empty name), when
    /// it speaks the language (PDFSpeechSynthesizerFactory::getDefaultVoice; empty = not known).
    static QString getVoiceForLanguage(const std::vector<PDFSpeechVoice>& voices, const QString& language, const QString& fallback, const QString& defaultVoice);
};

}   // namespace pdf

Q_DECLARE_METATYPE(pdf::PDFDocumentToolAudioBookStream)

#endif // PDFDOCUMENTTOOL_AUDIOBOOK_H
