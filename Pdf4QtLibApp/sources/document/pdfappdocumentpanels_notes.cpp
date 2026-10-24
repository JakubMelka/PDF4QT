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

#include "pdfappdocumentpanels_notes.h"
#include "pdfapplocale.h"
#include "pdfappannotationcontroller.h"
#include "pdfappannotationinfo.h"
#include "pdfappdocument.h"
#include "pdfappdocumentpanels_common.h"
#include "pdfappdocumentstatusbar.h"
#include "pdfappdocumentview.h"
#include "pdfapppage.h"

#include "pdfcatalog.h"
#include "pdfdrawspacecontroller.h"
#include "pdfexception.h"
#include "pdffluentwidgets.h"
#include "pdfthemefonts.h"
#include "pdfthemeiconengine.h"
#include "pdfwidgetannotation.h"
#include "pdfwidgetstyleproperties.h"

#include <QAction>
#include <QEvent>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QLocale>
#include <QMenu>
#include <QPainter>
#include <QVBoxLayout>

#include <algorithm>
#include <atomic>
#include <map>
#include <set>

namespace pdf
{

/// Helpers of the notes
class PDFAppNotesHelper
{
public:
    PDFAppNotesHelper() = delete;

    /// Subtypes of the listed annotations (/Subtype of the dictionary)
    static bool isListedSubtype(const QByteArray& subtype)
    {
        static const std::set<QByteArray> subtypes =
        {
            "Text", "FreeText", "Line", "Square", "Circle", "Polygon", "PolyLine", "Highlight", "Underline", "Squiggly",
            "StrikeOut", "Stamp", "Caret", "Ink", "FileAttachment", "Sound", "Redact", "Projection"
        };
        return subtypes.count(subtype) > 0;
    }

    /// Text of the document shown in a row: one paragraph without control characters
    static QString sanitizeText(const QString& text)
    {
        QString result = text.left(PDFAppNotes::MAXIMUM_TEXT_LENGTH);
        for (QChar& character : result)
        {
            if (character.category() == QChar::Other_Control || character == QChar::LineSeparator || character == QChar::ParagraphSeparator)
            {
                character = QChar(u' ');
            }
        }
        return result.simplified();
    }

    /// Paddings of the caption of a page (group-caption 195:4315)
    static constexpr int CAPTION_PADDING_TOP = 12;
    static constexpr int CAPTION_PADDING_SIDE = 10;
    static constexpr int CAPTION_PADDING_BOTTOM = 4;

    /// Longest chain of replies followed to the note of a thread
    static constexpr int MAXIMUM_REPLY_DEPTH = 256;
};

bool PDFAppNotes::isListedType(AnnotationType type)
{
    switch (type)
    {
        case AnnotationType::Text:
        case AnnotationType::FreeText:
        case AnnotationType::Line:
        case AnnotationType::Square:
        case AnnotationType::Circle:
        case AnnotationType::Polygon:
        case AnnotationType::Polyline:
        case AnnotationType::Highlight:
        case AnnotationType::Underline:
        case AnnotationType::Squiggly:
        case AnnotationType::StrikeOut:
        case AnnotationType::Stamp:
        case AnnotationType::Caret:
        case AnnotationType::Ink:
        case AnnotationType::FileAttachment:
        case AnnotationType::Sound:
        case AnnotationType::Redact:
        case AnnotationType::Projection:
            return true;

        default:
            break;
    }
    return false;
}

void PDFAppNotes::getBadge(AnnotationType type, bool isReply, QString* iconName, PDFAccentHue* hue)
{
    // Annotation types in Notes 222:8664
    if (isReply)
    {
        *iconName = QStringLiteral("corner-down-right");
        *hue = PDFAccentHue::Neutral;
        return;
    }

    switch (type)
    {
        case AnnotationType::Text:          *iconName = QStringLiteral("message-square"); *hue = PDFAccentHue::Blue; break;
        case AnnotationType::FreeText:      *iconName = QStringLiteral("text-box"); *hue = PDFAccentHue::Violet; break;
        case AnnotationType::Caret:         *iconName = QStringLiteral("caret"); *hue = PDFAccentHue::Blue; break;
        case AnnotationType::Stamp:         *iconName = QStringLiteral("stamp"); *hue = PDFAccentHue::Green; break;
        case AnnotationType::Highlight:     *iconName = QStringLiteral("highlighter"); *hue = PDFAccentHue::Amber; break;
        case AnnotationType::Underline:     *iconName = QStringLiteral("underline"); *hue = PDFAccentHue::Amber; break;
        case AnnotationType::Squiggly:      *iconName = QStringLiteral("squiggly"); *hue = PDFAccentHue::Amber; break;
        case AnnotationType::StrikeOut:     *iconName = QStringLiteral("strikethrough"); *hue = PDFAccentHue::Red; break;
        case AnnotationType::Line:          *iconName = QStringLiteral("line"); *hue = PDFAccentHue::Teal; break;
        case AnnotationType::Polyline:      *iconName = QStringLiteral("polyline"); *hue = PDFAccentHue::Teal; break;
        case AnnotationType::Square:        *iconName = QStringLiteral("square"); *hue = PDFAccentHue::Teal; break;
        case AnnotationType::Circle:        *iconName = QStringLiteral("circle"); *hue = PDFAccentHue::Teal; break;
        case AnnotationType::Polygon:       *iconName = QStringLiteral("polygon"); *hue = PDFAccentHue::Teal; break;
        case AnnotationType::Ink:           *iconName = QStringLiteral("scribble"); *hue = PDFAccentHue::Teal; break;
        case AnnotationType::FileAttachment:*iconName = QStringLiteral("paperclip"); *hue = PDFAccentHue::Neutral; break;
        case AnnotationType::Sound:         *iconName = QStringLiteral("volume-2"); *hue = PDFAccentHue::Neutral; break;
        case AnnotationType::Redact:        *iconName = QStringLiteral("redact"); *hue = PDFAccentHue::Red; break;
        case AnnotationType::Projection:    *iconName = QStringLiteral("projection"); *hue = PDFAccentHue::Neutral; break;

        default:
            *iconName = QStringLiteral("message-square");
            *hue = PDFAccentHue::Neutral;
            break;
    }
}

/// Notes of one version of a document (PDFAppDocument::getDerivedData)
class PDFAppNoteList : public PDFAppDocumentDerivedData
{
public:
    static constexpr const char* KEY = "notes";

    std::shared_ptr<const std::vector<PDFAppNote>> notes;
};

/// Calls of PDFAppNotes::collect (tests)
class PDFAppNotesCounter
{
public:
    PDFAppNotesCounter() = delete;

    static std::atomic<int>& get()
    {
        static std::atomic<int> counter = 0;
        return counter;
    }
};

int PDFAppNotes::getCollectCount()
{
    return PDFAppNotesCounter::get().load();
}

std::shared_ptr<const std::vector<PDFAppNote>> PDFAppNotes::getNotes(const PDFAppDocument* document)
{
    if (!document || !document->isReady() || !document->getDocument())
    {
        return std::make_shared<const std::vector<PDFAppNote>>();
    }

    const QString key = QLatin1String(PDFAppNoteList::KEY);
    if (const PDFAppNoteList* list = dynamic_cast<const PDFAppNoteList*>(document->getDerivedData(key).get()))
    {
        return list->notes;
    }

    std::shared_ptr<PDFAppNoteList> list = std::make_shared<PDFAppNoteList>();
    list->notes = std::make_shared<const std::vector<PDFAppNote>>(collect(document->getDocument().data()));
    document->setDerivedData(key, list);
    return list->notes;
}

std::vector<PDFAppNote> PDFAppNotes::collect(const PDFDocument* document, size_t maximumCount)
{
    // Only the lists are counted
    if (maximumCount > 1)
    {
        ++PDFAppNotesCounter::get();
    }
    std::vector<PDFAppNote> notes;
    if (!document)
    {
        return notes;
    }

    const PDFCatalog* catalog = document->getCatalog();
    const PDFObjectStorage* storage = &document->getStorage();
    const PDFInteger pageCount = PDFInteger(catalog->getPageCount());
    for (PDFInteger pageIndex = 0; pageIndex < pageCount; ++pageIndex)
    {
        for (const PDFObjectReference& reference : catalog->getPage(pageIndex)->getAnnotations())
        {
            if (notes.size() >= maximumCount)
            {
                return notes;
            }

            // Only the dictionary is read for the other annotations (links, widgets)
            const PDFDictionary* dictionary = storage->getDictionaryFromObject(storage->getObjectByReference(reference));
            const PDFObject& subtype = dictionary ? storage->getObject(dictionary->get("Subtype")) : PDFObject();
            if (!subtype.isName() || !PDFAppNotesHelper::isListedSubtype(subtype.getString()))
            {
                continue;
            }

            try
            {
                PDFAnnotationPtr annotation = PDFAnnotation::parse(storage, reference);
                const PDFMarkupAnnotation* markup = annotation ? annotation->asMarkupAnnotation() : nullptr;
                if (!markup || !isListedType(annotation->getType()))
                {
                    continue;
                }

                PDFAppNote note;
                note.page = pageIndex;
                note.reference = reference;
                note.type = annotation->getType();
                note.isReply = markup->isReplyTo();
                note.isHidden = annotation->getFlags().testFlag(PDFAnnotation::Hidden);
                note.inReplyTo = markup->getInReplyTo();
                note.rectangle = annotation->getRectangle();
                // Bidirectional controls of the author are removed (owner, security review of step 4)
                note.author = PDFAppAnnotationInfo::sanitizeName(markup->getWindowTitle());
                note.text = PDFAppNotesHelper::sanitizeText(annotation->getContents());
                note.date = annotation->getLastModifiedDateTime().isValid() ? annotation->getLastModifiedDateTime() : markup->getCreationDate();
                notes.push_back(std::move(note));
            }
            catch (const PDFException&)
            {
                // A damaged annotation is left out
            }
        }
    }
    return notes;
}

bool PDFAppNotes::hasNotes(const PDFDocument* document)
{
    if (!document)
    {
        return false;
    }

    // Only the subtypes are read (the tab is updated at every change)
    const PDFCatalog* catalog = document->getCatalog();
    const PDFObjectStorage* storage = &document->getStorage();
    const PDFInteger pageCount = PDFInteger(catalog->getPageCount());
    for (PDFInteger pageIndex = 0; pageIndex < pageCount; ++pageIndex)
    {
        for (const PDFObjectReference& reference : catalog->getPage(pageIndex)->getAnnotations())
        {
            const PDFDictionary* dictionary = storage->getDictionaryFromObject(storage->getObjectByReference(reference));
            const PDFObject& subtype = dictionary ? storage->getObject(dictionary->get("Subtype")) : PDFObject();
            if (subtype.isName() && PDFAppNotesHelper::isListedSubtype(subtype.getString()))
            {
                return true;
            }
        }
    }
    return false;
}

QString PDFAppNotes::formatDate(const QDateTime& dateTime, const QDate& today)
{
    if (!dateTime.isValid())
    {
        return QString();
    }

    // 195:3910 "Sep 28"
    const QDate date = dateTime.toLocalTime().date();
    return date.year() == today.year() ? PDFAppLocale::formatShortDate(date) : PDFAppLocale::formatDate(date);
}

PDFAppNotesModel::PDFAppNotesModel(QObject* parent) :
    BaseClass(parent),
    m_today(QDate::currentDate())
{

}

PDFAppNotesModel::~PDFAppNotesModel() = default;

void PDFAppNotesModel::setNotes(std::vector<PDFAppNote> notes, PDFDocumentPointer document)
{
    beginResetModel();
    m_notes = std::move(notes);
    m_document = std::move(document);
    m_today = QDate::currentDate();

    // The threads: each reply belongs to the note at the start of its chain of
    // replies (only listed notes; a cycle or a too long chain makes a note its own
    // thread)
    m_noteIndices.clear();
    for (int i = 0; i < int(m_notes.size()); ++i)
    {
        m_noteIndices.emplace(m_notes[size_t(i)].reference, i);
    }

    m_roots.assign(m_notes.size(), -1);
    m_replies.assign(m_notes.size(), std::vector<int>());
    for (int i = 0; i < int(m_notes.size()); ++i)
    {
        int root = i;
        std::set<int> visited = { i };
        while (m_notes[size_t(root)].isReply)
        {
            auto it = m_noteIndices.find(m_notes[size_t(root)].inReplyTo);
            if (it == m_noteIndices.end())
            {
                break;
            }
            if (!visited.insert(it->second).second || int(visited.size()) > PDFAppNotesHelper::MAXIMUM_REPLY_DEPTH)
            {
                // A cycle of replies or a too long chain
                root = i;
                break;
            }
            root = it->second;
        }
        m_roots[size_t(i)] = root;
        if (root != i)
        {
            m_replies[size_t(root)].push_back(i);
        }
    }

    rebuildRows();
    endResetModel();
}

void PDFAppNotesModel::setFilterText(const QString& text)
{
    const QString filter = text.trimmed();
    if (filter == m_filterText)
    {
        return;
    }

    beginResetModel();
    m_filterText = filter;
    rebuildRows();
    endResetModel();
}

bool PDFAppNotesModel::isMatching(const PDFAppNote& note) const
{
    return m_filterText.isEmpty() || note.author.contains(m_filterText, Qt::CaseInsensitive) || note.text.contains(m_filterText, Qt::CaseInsensitive);
}

void PDFAppNotesModel::rebuildRows()
{
    m_rows.clear();
    m_rowIndices.clear();
    PDFInteger lastPage = -1;
    for (int i = 0; i < int(m_notes.size()); ++i)
    {
        // A reply is shown under the note of its thread
        if (m_roots[size_t(i)] != i)
        {
            continue;
        }

        const PDFAppNote& note = m_notes[size_t(i)];
        const std::vector<int>& replies = m_replies[size_t(i)];
        const bool isNoteMatching = isMatching(note);
        std::vector<int> shownReplies;
        for (int reply : replies)
        {
            if (isNoteMatching || isMatching(m_notes[size_t(reply)]))
            {
                shownReplies.push_back(reply);
            }
        }
        if (!isNoteMatching && shownReplies.empty())
        {
            continue;
        }

        if (note.page != lastPage)
        {
            m_rows.push_back({ note.page, -1, 0 });
            lastPage = note.page;
        }
        m_rowIndices[note.reference] = int(m_rows.size());
        m_rows.push_back({ note.page, i, 0 });
        for (int reply : shownReplies)
        {
            m_rowIndices[m_notes[size_t(reply)].reference] = int(m_rows.size());
            m_rows.push_back({ m_notes[size_t(reply)].page, reply, PDFAppNotes::REPLY_INDENT });
        }
    }
}

int PDFAppNotesModel::getReplyCount() const
{
    int count = 0;
    for (const PDFAppNote& note : m_notes)
    {
        count += note.isReply ? 1 : 0;
    }
    return count;
}

int PDFAppNotesModel::getPageCount() const
{
    std::set<PDFInteger> pages;
    for (const PDFAppNote& note : m_notes)
    {
        pages.insert(note.page);
    }
    return int(pages.size());
}

const PDFAppNote* PDFAppNotesModel::getNote(int row) const
{
    if (row < 0 || row >= int(m_rows.size()) || m_rows[size_t(row)].note < 0)
    {
        return nullptr;
    }
    return &m_notes[size_t(m_rows[size_t(row)].note)];
}

int PDFAppNotesModel::findRow(PDFObjectReference reference) const
{
    auto it = m_rowIndices.find(reference);
    return it != m_rowIndices.end() ? it->second : -1;
}

std::vector<PDFObjectReference> PDFAppNotesModel::getThreads(const std::vector<PDFObjectReference>& references) const
{
    // The replies of the annotations, also replies of replies. Only a reply (/IRT
    // with /RT /R) belongs to its note: a member of a group (/RT /Group) is listed
    // as a note of its own, so it is not deleted with another note.
    std::multimap<PDFObjectReference, PDFObjectReference> replies;
    for (const PDFAppNote& note : m_notes)
    {
        if (note.isReply && note.inReplyTo.isValid())
        {
            replies.emplace(note.inReplyTo, note.reference);
        }
    }

    std::set<PDFObjectReference> found(references.begin(), references.end());
    std::vector<PDFObjectReference> stack(references.begin(), references.end());
    while (!stack.empty())
    {
        const PDFObjectReference reference = stack.back();
        stack.pop_back();
        const auto range = replies.equal_range(reference);
        for (auto it = range.first; it != range.second; ++it)
        {
            if (found.insert(it->second).second)
            {
                stack.push_back(it->second);
            }
        }
    }

    std::vector<PDFObjectReference> result;
    for (const PDFObjectReference& reference : references)
    {
        if (std::find(result.cbegin(), result.cend(), reference) == result.cend())
        {
            result.push_back(reference);
        }
    }
    for (const PDFAppNote& note : m_notes)
    {
        if (found.count(note.reference) && std::find(result.cbegin(), result.cend(), note.reference) == result.cend())
        {
            result.push_back(note.reference);
        }
    }
    return result;
}

int PDFAppNotesModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : int(m_rows.size());
}

Qt::ItemFlags PDFAppNotesModel::flags(const QModelIndex& index) const
{
    if (!index.isValid() || index.row() >= int(m_rows.size()))
    {
        return Qt::NoItemFlags;
    }

    // A caption is not selectable
    return m_rows[size_t(index.row())].note < 0 ? Qt::ItemFlags(Qt::ItemIsEnabled | Qt::ItemNeverHasChildren)
                                                : Qt::ItemFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemNeverHasChildren);
}

QVariant PDFAppNotesModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= int(m_rows.size()) || !m_document)
    {
        return QVariant();
    }

    const Row& row = m_rows[size_t(index.row())];
    if (role == CaptionRole)
    {
        return row.note < 0;
    }
    if (role == PageRole)
    {
        return qlonglong(row.page);
    }

    if (row.note < 0)
    {
        switch (role)
        {
            case Qt::DisplayRole:
            case Qt::AccessibleTextRole:
                return PDFAppPanelWidgets::getPageTitle(m_document->getCatalog(), row.page);

            default:
                break;
        }
        return QVariant();
    }

    const PDFAppNote& note = m_notes[size_t(row.note)];
    const QString author = note.author.isEmpty() ? PDFAppNotesPanel::tr("Unknown author") : note.author;
    switch (role)
    {
        case Qt::DisplayRole:
            return author;

        case PDFComponentRoles::DateRole:
            return PDFAppNotes::formatDate(note.date, m_today);

        case PDFComponentRoles::TextRole:
            return note.text;

        case PDFComponentRoles::IconNameRole:
        case PDFComponentRoles::HueRole:
        {
            QString iconName;
            PDFAccentHue hue = PDFAccentHue::Neutral;
            PDFAppNotes::getBadge(note.type, note.isReply, &iconName, &hue);
            return role == PDFComponentRoles::IconNameRole ? QVariant(iconName) : QVariant(int(hue));
        }

        case PDFComponentRoles::IndentRole:
            return row.indent;

        case PDFComponentRoles::StateIconRole:
            // 1026:21995: a hidden annotation has the eye-off mark
            return note.isHidden && !note.isReply ? QStringLiteral("eye-off") : QString();

        case Qt::ToolTipRole:
        case Qt::AccessibleTextRole:
        {
            QString text = note.text.isEmpty() ? author : QStringLiteral("%1: %2").arg(author, note.text);
            if (note.isReply)
            {
                //: Accessible text of a reply row: "Reply - Jane: Agreed"
                text = PDFAppNotesPanel::tr("Reply - %1").arg(text);
            }
            if (note.isHidden && !note.isReply)
            {
                //: Accessible text of a hidden annotation: "Jane: Text (hidden on the page)"
                text = PDFAppNotesPanel::tr("%1 (hidden on the page)").arg(text);
            }
            return text;
        }

        default:
            break;
    }

    return QVariant();
}

PDFAppNotesDelegate::PDFAppNotesDelegate(QObject* parent) :
    BaseClass(parent)
{

}

PDFAppNotesDelegate::~PDFAppNotesDelegate() = default;

PDFComponentPainter::NoteRow PDFAppNotesDelegate::getPaintedContent(const QModelIndex& index, Qt::LayoutDirection direction) const
{
    PDFComponentPainter::NoteRow content = getContent(index);
    content.iconName = PDFAppPage::getDirectionalIconName(content.iconName, direction);
    return content;
}

void PDFAppNotesDelegate::paintItem(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index, const PDFComponentPainter::State& state) const
{
    if (!index.data(PDFAppNotesModel::CaptionRole).toBool())
    {
        // A reply is indented with its selection (1025:21920); the arrow of a reply
        // points to its text also in a right-to-left layout
        const QRect rowRect = getIndentedRect(option.rect, getIndent(index), option.direction);
        PDFComponentPainter::paintNoteRow(painter, QRectF(rowRect), getPaintedContent(index, option.direction), state);
        return;
    }

    // group-caption 195:4315: Medium 11 text/muted, paddings 12 / 10 / 4
    const QRectF rect = QRectF(option.rect).adjusted(PDFAppNotesHelper::CAPTION_PADDING_SIDE, PDFAppNotesHelper::CAPTION_PADDING_TOP,
                                                     -PDFAppNotesHelper::CAPTION_PADDING_SIDE, -PDFAppNotesHelper::CAPTION_PADDING_BOTTOM);
    PDFComponentPainter::drawElidedText(painter, state, rect, index.data(Qt::DisplayRole).toString(), PDFThemeFonts::getFont(PDFThemeFonts::Role::Caption),
                                        PDFThemeColors::TextMuted, PDFComponentPainter::TextKind::Interface);
}

QSize PDFAppNotesDelegate::getItemSize(const QStyleOptionViewItem& option, const QModelIndex& index, int width) const
{
    if (!index.data(PDFAppNotesModel::CaptionRole).toBool())
    {
        return BaseClass::getItemSize(option, index, width);
    }

    const QFontMetrics metrics(PDFThemeFonts::getFont(PDFThemeFonts::Role::Caption));
    const int height = PDFAppNotesHelper::CAPTION_PADDING_TOP + metrics.height() + PDFAppNotesHelper::CAPTION_PADDING_BOTTOM;
    return QSize(width > 0 ? width : 276, height);
}

PDFAppNotesPanel::PDFAppNotesPanel(QWidget* parent) :
    BaseClass(parent)
{
    setObjectName(QStringLiteral("notesPanel"));

    // sidebar-content 195:4310: the search field at 2 / 12, the rows with the gap 2
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, PDFAppPanelWidgets::CONTENT_PADDING_TOP, 0, 0);
    layout->setSpacing(0);

    QHBoxLayout* searchLayout = new QHBoxLayout();
    searchLayout->setContentsMargins(PDFAppPanelWidgets::CONTENT_PADDING_SIDE, 0, PDFAppPanelWidgets::CONTENT_PADDING_SIDE, 0);
    m_searchField = PDFAppPanelWidgets::createSearchField(this, tr("Search notes…"), QStringLiteral("notesSearch"));
    searchLayout->addWidget(m_searchField);
    layout->addLayout(searchLayout);

    m_view = new PDFAppPanelListView(this);
    m_view->setObjectName(QStringLiteral("notesView"));

    // One selection with the page (also of more annotations, step 4)
    m_view->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_view->setResizeMode(QListView::Adjust);
    m_view->setAccessibleName(tr("Notes"));
    m_view->setContextMenuPolicy(Qt::CustomContextMenu);
    PDFAppPanelWidgets::prepareList(m_view);

    m_model = new PDFAppNotesModel(m_view);
    m_delegate = new PDFAppNotesDelegate(m_view);
    m_view->setModel(m_model);
    m_view->setItemDelegate(m_delegate);
    m_view->installEventFilter(this);
    m_view->viewport()->installEventFilter(this);
    layout->addWidget(m_view, 1);

    connect(m_searchField, &QLineEdit::textChanged, this, &PDFAppNotesPanel::onSearchTextChanged);
    connect(m_view, &QAbstractItemView::clicked, this, &PDFAppNotesPanel::onClicked);
    connect(m_view, &QWidget::customContextMenuRequested, this, &PDFAppNotesPanel::onContextMenuRequested);
    connect(m_view->selectionModel(), &QItemSelectionModel::selectionChanged, this, &PDFAppNotesPanel::onListSelectionChanged);
}

PDFAppNotesPanel::~PDFAppNotesPanel()
{
    // The children are deleted by the base class, when the members of the panel are
    // gone: their signals do not reach the panel any more
    if (m_view)
    {
        m_view->disconnect(this);
        if (m_view->selectionModel())
        {
            m_view->selectionModel()->disconnect(this);
        }
        m_view->removeEventFilter(this);
        m_view->viewport()->removeEventFilter(this);
    }
    if (m_searchField)
    {
        m_searchField->disconnect(this);
    }
    if (m_connectedDocument)
    {
        m_connectedDocument->disconnect(this);
    }
    if (m_connectedController)
    {
        m_connectedController->disconnect(this);
    }
}

QString PDFAppNotesPanel::getId() const
{
    return PDFAppDocumentPanelIds::notes();
}

QString PDFAppNotesPanel::getTitle() const
{
    return tr("Notes");
}

QString PDFAppNotesPanel::getSubtitle() const
{
    // 195:3910 "5 notes on 3 pages", 1025:21533 "6 notes and 2 replies on 3 pages"
    const int count = m_model ? m_model->getNoteCount() : 0;
    if (count == 0)
    {
        return QString();
    }
    const int replies = m_model->getReplyCount();
    const int notes = count - replies;
    const int pages = m_model->getPageCount();
    const QString noteText = tr("%Ln notes", nullptr, notes);
    const QString pageText = tr("%Ln pages", nullptr, pages);
    if (replies > 0)
    {
        //: "6 notes and 2 replies on 3 pages"
        return tr("%1 and %2 on %3").arg(noteText, tr("%Ln replies", nullptr, replies), pageText);
    }
    //: "5 notes on 3 pages"
    return tr("%1 on %2").arg(noteText, pageText);
}

QString PDFAppNotesPanel::getIconName() const
{
    return QStringLiteral("message-square");
}

PDFAppAnnotationController* PDFAppNotesPanel::getController() const
{
    PDFAppDocumentView* view = getView();
    return view ? view->getAnnotationController() : nullptr;
}

void PDFAppNotesPanel::setDocument(PDFAppDocument* document, PDFAppDocumentView* view)
{
    if (m_connectedDocument && m_connectedDocument != document)
    {
        m_connectedDocument->disconnect(this);
    }

    PDFAppAnnotationController* controller = view ? view->getAnnotationController() : nullptr;
    if (m_connectedController && m_connectedController != controller)
    {
        m_connectedController->disconnect(this);
    }

    BaseClass::setDocument(document, view);

    if (document && m_connectedDocument != document)
    {
        connect(document, &PDFAppDocument::documentChanged, this, &PDFAppNotesPanel::onDocumentChanged);
        connect(document, &QObject::destroyed, this, &PDFAppNotesPanel::onDocumentDestroyed);
    }
    if (controller && m_connectedController != controller)
    {
        connect(controller, &PDFAppAnnotationController::selectionChanged, this, &PDFAppNotesPanel::onPageSelectionChanged);
    }
    m_connectedDocument = document;
    m_connectedController = controller;

    // A slot of titleChanged (emitted by rebuild) may delete the tab with the panel
    QPointer<PDFAppNotesPanel> guard(this);
    rebuild();
    if (!guard)
    {
        return;
    }

    // The rows follow the selection on the page
    onPageSelectionChanged();
}

void PDFAppNotesPanel::onDocumentChanged(const PDFModifiedDocument& modified)
{
    Q_UNUSED(modified);
    rebuild();
}

void PDFAppNotesPanel::onDocumentDestroyed()
{
    m_connectedDocument = nullptr;
    m_document.reset();
    if (m_model)
    {
        const bool wasSyncing = m_isSyncing;
        m_isSyncing = true;
        m_model->setNotes({}, PDFDocumentPointer());
        m_isSyncing = wasSyncing;
    }
}

void PDFAppNotesPanel::rebuild()
{
    PDFAppDocument* document = getDocument();
    PDFDocumentPointer pointer = document && document->isReady() ? document->getDocument() : PDFDocumentPointer();
    if (pointer == m_document && m_model && m_model->getNoteCount() > 0)
    {
        return;
    }

    // The selected notes, which still exist, stay selected (the page keeps its
    // selection itself - the panel does not change it here)
    const std::vector<PDFObjectReference> selection = getSelectedNotes();
    m_document = pointer;
    if (m_model)
    {
        // The list of the version is shared with the tab of the sidebar
        const bool wasSyncing = m_isSyncing;
        m_isSyncing = true;
        m_model->setNotes(pointer ? *PDFAppNotes::getNotes(document) : std::vector<PDFAppNote>(), pointer);
        m_isSyncing = wasSyncing;
    }
    restoreSelection(selection);
    Q_EMIT titleChanged();
}

void PDFAppNotesPanel::restoreSelection(const std::vector<PDFObjectReference>& references)
{
    const bool wasSyncing = m_isSyncing;
    m_isSyncing = true;
    selectNotes(references);
    m_isSyncing = wasSyncing;
}

void PDFAppNotesPanel::onSearchTextChanged(const QString& text)
{
    if (m_model)
    {
        // The filter keeps the selection (also of the rows it hides)
        const std::vector<PDFObjectReference> selection = getSelectedNotes();
        PDFAppAnnotationController* controller = getController();
        std::vector<PDFObjectReference> kept = selection;
        if (controller)
        {
            for (const PDFObjectReference& reference : controller->getSelectedAnnotations())
            {
                if (std::find(kept.cbegin(), kept.cend(), reference) == kept.cend())
                {
                    kept.push_back(reference);
                }
            }
        }

        const bool wasSyncing = m_isSyncing;
        m_isSyncing = true;
        m_model->setFilterText(text);
        m_isSyncing = wasSyncing;
        restoreSelection(kept);
    }
}

std::vector<PDFObjectReference> PDFAppNotesPanel::getSelectedNotes() const
{
    std::vector<PDFObjectReference> references;
    if (!m_view || !m_model || !m_view->selectionModel())
    {
        return references;
    }

    QModelIndexList indexes = m_view->selectionModel()->selectedRows();
    std::sort(indexes.begin(), indexes.end());
    for (const QModelIndex& index : indexes)
    {
        if (const PDFAppNote* note = m_model->getNote(index.row()))
        {
            references.push_back(note->reference);
        }
    }
    return references;
}

void PDFAppNotesPanel::selectNotes(const std::vector<PDFObjectReference>& references)
{
    if (!m_view || !m_model || !m_view->selectionModel())
    {
        return;
    }

    QItemSelection selection;
    int firstRow = -1;
    for (const PDFObjectReference& reference : references)
    {
        const int row = m_model->findRow(reference);
        if (row >= 0)
        {
            const QModelIndex index = m_model->index(row);
            selection.select(index, index);
            firstRow = firstRow < 0 ? row : qMin(firstRow, row);
        }
    }

    QItemSelectionModel* selectionModel = m_view->selectionModel();
    if (selection == selectionModel->selection())
    {
        return;
    }

    selectionModel->select(selection, QItemSelectionModel::ClearAndSelect);
    if (firstRow >= 0)
    {
        const QModelIndex first = m_model->index(firstRow);
        selectionModel->setCurrentIndex(first, QItemSelectionModel::NoUpdate);
        m_view->scrollTo(first, QAbstractItemView::EnsureVisible);
    }
}

void PDFAppNotesPanel::onListSelectionChanged(const QItemSelection& selected, const QItemSelection& deselected)
{
    Q_UNUSED(selected);
    Q_UNUSED(deselected);
    if (m_isSyncing)
    {
        return;
    }

    // The annotations of the rows are selected on the page (a hidden annotation
    // and a reply are not selectable there: they stay selected in the panel only)
    PDFAppAnnotationController* controller = getController();
    if (controller)
    {
        const bool wasSyncing = m_isSyncing;
        m_isSyncing = true;
        QPointer<PDFAppNotesPanel> guard(this);
        QPointer<PDFAppAnnotationController> controllerGuard(controller);
        controller->selectAnnotations(getSelectedNotes());
        if (!guard)
        {
            return;
        }
        m_isSyncing = wasSyncing;

        // The controller announces the new page selection from the event loop: the
        // rows keep their selection (also of the replies and the hidden notes,
        // which are not selected on the page), unless the page selection changes
        // again meanwhile
        m_hasPushedSelection = controllerGuard != nullptr;
        m_pushedSelection = controllerGuard ? controllerGuard->getSelectedAnnotations() : std::vector<PDFObjectReference>();
    }
}

void PDFAppNotesPanel::onPageSelectionChanged()
{
    PDFAppAnnotationController* controller = getController();
    if (m_isSyncing || !controller)
    {
        return;
    }

    // The page selection made by the rows themselves is not given back to them
    std::vector<PDFObjectReference> selection = controller->getSelectedAnnotations();
    const bool isPushed = m_hasPushedSelection && selection == m_pushedSelection;
    m_hasPushedSelection = false;
    m_pushedSelection.clear();
    if (isPushed)
    {
        return;
    }

    // A selection made on the page (or restored by undo / redo)
    restoreSelection(selection);
}

void PDFAppNotesPanel::onClicked(const QModelIndex& index)
{
    // A click with a modifier changes the selection only
    if (index.isValid() && !(m_pressModifiers & (Qt::ControlModifier | Qt::ShiftModifier)))
    {
        activateRow(index.row());
    }
}

void PDFAppNotesPanel::activateRow(int row)
{
    PDFAppDocumentView* view = getView();
    const PDFAppNote* note = m_model ? m_model->getNote(row) : nullptr;
    if (!view || !note)
    {
        return;
    }

    // The annotation is shown on its page (the page itself without a rectangle)
    PDFDrawWidgetProxy* proxy = view->getProxy();
    const QRectF rectangle = note->rectangle.normalized();
    if (proxy && note->page >= 0 && note->page < view->getPageCount() && rectangle.isValid() && !rectangle.isEmpty() &&
        qIsFinite(rectangle.left()) && qIsFinite(rectangle.top()) && qIsFinite(rectangle.width()) && qIsFinite(rectangle.height()))
    {
        proxy->goToPageAndEnsureVisible(note->page, rectangle);
        if (view->getCurrentPageIndex() != note->page)
        {
            view->goToPage(note->page);
        }
    }
    else
    {
        view->goToPage(note->page);
    }
}

bool PDFAppNotesPanel::deleteSelectedNotes()
{
    PDFAppAnnotationController* controller = getController();
    PDFWidgetAnnotationManager* manager = controller ? controller->getAnnotationManager() : nullptr;
    const std::vector<PDFObjectReference> selection = getSelectedNotes();
    if (!controller || !manager || !m_model || selection.empty())
    {
        return false;
    }
    if (!controller->isAnnotationEditingAllowed())
    {
        if (PDFAppDocumentView* view = getView(); view && view->getStatusBar())
        {
            view->getStatusBar()->showMessage(controller->getEditingRefusalReason(), PDFAppAnnotationController::STATUS_MESSAGE_TIMEOUT);
        }
        return false;
    }

    // The replies go with their notes (one undo step)
    const std::vector<PDFObjectReference> references = m_model->getThreads(selection);
    controller->setPendingDescription(selection.size() == 1 ? tr("Delete note") : tr("Delete notes"));
    QPointer<PDFAppAnnotationController> guard(controller);
    const bool isDeleted = manager->deleteAnnotations(references);
    if (!isDeleted && guard)
    {
        // Nothing has been committed: the description does not stay for another change
        guard->setPendingDescription(QString());
    }
    return isDeleted;
}

bool PDFAppNotesPanel::setSelectedNotesHidden(bool hidden)
{
    PDFAppAnnotationController* controller = getController();
    PDFWidgetAnnotationManager* manager = controller ? controller->getAnnotationManager() : nullptr;
    std::vector<PDFObjectReference> references;
    for (const PDFObjectReference& reference : getSelectedNotes())
    {
        const int row = m_model ? m_model->findRow(reference) : -1;
        const PDFAppNote* note = m_model ? m_model->getNote(row) : nullptr;
        if (note && !note->isReply && note->isHidden != hidden)
        {
            references.push_back(reference);
        }
    }
    if (!controller || !manager || references.empty())
    {
        return false;
    }
    if (!controller->isAnnotationEditingAllowed())
    {
        if (PDFAppDocumentView* view = getView(); view && view->getStatusBar())
        {
            view->getStatusBar()->showMessage(controller->getEditingRefusalReason(), PDFAppAnnotationController::STATUS_MESSAGE_TIMEOUT);
        }
        return false;
    }

    controller->setPendingDescription(hidden ? tr("Hide on page") : tr("Show on page"));
    QPointer<PDFAppAnnotationController> guard(controller);
    const bool isChanged = manager->setAnnotationsHidden(references, hidden);
    if (!isChanged && guard)
    {
        guard->setPendingDescription(QString());
    }
    return isChanged;
}

QMenu* PDFAppNotesPanel::createContextMenu()
{
    const std::vector<PDFObjectReference> selection = getSelectedNotes();
    PDFAppAnnotationController* controller = getController();
    if (selection.empty() || !m_model || !controller)
    {
        return nullptr;
    }

    // The menu does not block: its notes are remembered (the rows may be built
    // again before an action runs)
    m_menuNotes = selection;
    const bool isEditable = controller->isAnnotationEditingAllowed();
    bool hasVisible = false;
    bool hasHidden = false;
    for (const PDFObjectReference& reference : selection)
    {
        const PDFAppNote* note = m_model->getNote(m_model->findRow(reference));
        if (note && !note->isReply)
        {
            (note->isHidden ? hasHidden : hasVisible) = true;
        }
    }

    QMenu* menu = new QMenu(this);
    menu->setObjectName(QStringLiteral("notesContextMenu"));
    menu->setAttribute(Qt::WA_DeleteOnClose);

    const PDFAppNote* single = selection.size() == 1 ? m_model->getNote(m_model->findRow(selection.front())) : nullptr;
    const bool isOnPage = single && !single->isReply && !single->isHidden;
    if (isOnPage)
    {
        // 1019:18817: a note on the page has its comment and its replies
        QAction* openComment = menu->addAction(PDFThemeIcons::getIcon(QStringLiteral("message-square")), tr("Open Comment"));
        openComment->setObjectName(QStringLiteral("notesOpenComment"));
        connect(openComment, &QAction::triggered, this, &PDFAppNotesPanel::onMenuOpenComment);
        if (isEditable)
        {
            QAction* reply = menu->addAction(PDFThemeIcons::getIcon(PDFAppPage::getDirectionalIconName(QStringLiteral("corner-down-right"), layoutDirection())), tr("Reply"));
            reply->setObjectName(QStringLiteral("notesReply"));
            connect(reply, &QAction::triggered, this, &PDFAppNotesPanel::onMenuReply);
        }
        menu->addSeparator();
    }

    // 1026:21995: the row of a hidden annotation has Show on Page, Properties… and
    // Delete, without separators
    const bool isHiddenSingle = single && !single->isReply && single->isHidden;
    if (isHiddenSingle && isEditable)
    {
        QAction* show = menu->addAction(PDFThemeIcons::getIcon(QStringLiteral("eye")), tr("Show on Page"));
        show->setObjectName(QStringLiteral("notesShow"));
        connect(show, &QAction::triggered, this, &PDFAppNotesPanel::onMenuShow);
        hasHidden = false;
    }

    if (single)
    {
        QAction* properties = menu->addAction(PDFThemeIcons::getIcon(QStringLiteral("info")), tr("Properties…"));
        properties->setObjectName(QStringLiteral("notesProperties"));
        connect(properties, &QAction::triggered, this, &PDFAppNotesPanel::onMenuProperties);
    }

    if (isEditable)
    {
        // Show on Page of hidden annotations, Hide on Page of the others
        if (hasHidden || hasVisible)
        {
            menu->addSeparator();
        }
        if (hasHidden)
        {
            QAction* show = menu->addAction(PDFThemeIcons::getIcon(QStringLiteral("eye")), tr("Show on Page"));
            show->setObjectName(QStringLiteral("notesShow"));
            connect(show, &QAction::triggered, this, &PDFAppNotesPanel::onMenuShow);
        }
        if (hasVisible)
        {
            QAction* hide = menu->addAction(PDFThemeIcons::getIcon(QStringLiteral("eye-off")), tr("Hide on Page"));
            hide->setObjectName(QStringLiteral("notesHide"));
            connect(hide, &QAction::triggered, this, &PDFAppNotesPanel::onMenuHide);
        }

        if (!isHiddenSingle)
        {
            menu->addSeparator();
        }
        QAction* remove = menu->addAction(PDFThemeIcons::getIcon(QStringLiteral("trash")), selection.size() == 1 ? tr("Delete") : tr("Delete %Ln Notes", nullptr, int(selection.size())));
        remove->setObjectName(QStringLiteral("notesDelete"));
        remove->setShortcut(QKeySequence(Qt::Key_Delete));
        remove->setShortcutContext(Qt::WidgetShortcut);
        remove->setShortcutVisibleInContextMenu(true);
        remove->setProperty(PDFWidgetStyleProperties::DESTRUCTIVE, true);
        connect(remove, &QAction::triggered, this, &PDFAppNotesPanel::onMenuDelete);
    }

    if (menu->actions().isEmpty())
    {
        delete menu;
        return nullptr;
    }
    return menu;
}

PDFObjectReference PDFAppNotesPanel::getMenuNote() const
{
    return m_menuNotes.size() == 1 ? m_menuNotes.front() : PDFObjectReference();
}

void PDFAppNotesPanel::onMenuOpenComment()
{
    // The comment popup of package B lies at the annotation on the page
    const PDFObjectReference note = getMenuNote();
    PDFAppAnnotationController* controller = getController();
    if (controller && note.isValid())
    {
        activateRow(m_model ? m_model->findRow(note) : -1);
        controller->requestComment(note, false);
    }
}

void PDFAppNotesPanel::onMenuReply()
{
    const PDFObjectReference note = getMenuNote();
    PDFAppAnnotationController* controller = getController();
    if (controller && note.isValid())
    {
        activateRow(m_model ? m_model->findRow(note) : -1);
        controller->requestReply(note);
    }
}

void PDFAppNotesPanel::onMenuProperties()
{
    const PDFObjectReference note = getMenuNote();
    PDFAppAnnotationController* controller = getController();
    if (controller && note.isValid())
    {
        controller->requestProperties(note);
    }
}

void PDFAppNotesPanel::onMenuShow()
{
    restoreSelection(m_menuNotes);
    setSelectedNotesHidden(false);
}

void PDFAppNotesPanel::onMenuHide()
{
    restoreSelection(m_menuNotes);
    setSelectedNotesHidden(true);
}

void PDFAppNotesPanel::onMenuDelete()
{
    restoreSelection(m_menuNotes);
    deleteSelectedNotes();
}

void PDFAppNotesPanel::onContextMenuRequested(const QPoint& position)
{
    if (!m_view || !m_model)
    {
        return;
    }

    // A right click on an unselected note selects it (as on the page)
    QModelIndex index = m_view->indexAt(position);
    QPoint globalPosition = m_view->viewport()->mapToGlobal(position);
    if (index.isValid())
    {
        if (!m_model->getNote(index.row()))
        {
            return;
        }
        if (!m_view->selectionModel()->isSelected(index))
        {
            m_view->selectionModel()->setCurrentIndex(index, QItemSelectionModel::ClearAndSelect);
        }
    }
    else if (m_view->viewport()->rect().contains(position))
    {
        // A click into the empty part of the list (below the last row) is not the
        // menu of the selected notes
        return;
    }
    else
    {
        // The keyboard: the menu of the current row
        index = m_view->currentIndex();
        const QRect rect = index.isValid() ? m_view->visualRect(index) : QRect();
        if (!rect.isValid())
        {
            return;
        }
        globalPosition = m_view->viewport()->mapToGlobal(rect.center());
    }

    if (QMenu* menu = createContextMenu())
    {
        menu->popup(globalPosition);
    }
}

bool PDFAppNotesPanel::eventFilter(QObject* watched, QEvent* event)
{
    if (m_view && watched == m_view->viewport() && event->type() == QEvent::MouseButtonPress)
    {
        // The modifiers of the click (clicked has none)
        m_pressModifiers = static_cast<const QMouseEvent*>(event)->modifiers();
    }
    if (watched == m_view && m_view && event->type() == QEvent::KeyPress)
    {
        const QKeyEvent* keyEvent = static_cast<const QKeyEvent*>(event);
        if ((keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter) && keyEvent->modifiers() == Qt::NoModifier && m_view->currentIndex().isValid())
        {
            activateRow(m_view->currentIndex().row());
            return true;
        }
        if (keyEvent->key() == Qt::Key_Delete && keyEvent->modifiers() == Qt::NoModifier && !getSelectedNotes().empty())
        {
            deleteSelectedNotes();
            return true;
        }
    }
    return BaseClass::eventFilter(watched, event);
}

void PDFAppNotesPanel::changeEvent(QEvent* event)
{
    BaseClass::changeEvent(event);
    if (event->type() == QEvent::LanguageChange)
    {
        if (m_searchField)
        {
            m_searchField->setPlaceholderText(tr("Search notes…"));
            m_searchField->setAccessibleName(tr("Search notes…"));
        }
        if (m_view)
        {
            m_view->setAccessibleName(tr("Notes"));
        }
        Q_EMIT titleChanged();
    }
}

QString PDFAppNotesPanelFactory::getId() const
{
    return PDFAppDocumentPanelIds::notes();
}

int PDFAppNotesPanelFactory::getOrder() const
{
    return 80;
}

QString PDFAppNotesPanelFactory::getTitle() const
{
    return PDFAppNotesPanel::tr("Notes");
}

QString PDFAppNotesPanelFactory::getIconName() const
{
    return QStringLiteral("message-square");
}

bool PDFAppNotesPanelFactory::isAvailable(const PDFAppDocument* document) const
{
    if (!document)
    {
        return false;
    }

    // The tab is asked at every change of the document: the list of the version, when
    // a panel has collected it, otherwise the quick test of the subtypes (the whole
    // list of a large document is collected only for a shown panel)
    if (const PDFAppNoteList* list = dynamic_cast<const PDFAppNoteList*>(document->getDerivedData(PDFAppNoteList::KEY).get()))
    {
        return !list->notes->empty();
    }
    return PDFAppNotes::hasNotes(document->getDocument().data());
}

PDFAppDocumentPanel* PDFAppNotesPanelFactory::createPanel(QWidget* parent) const
{
    return new PDFAppNotesPanel(parent);
}

}   // namespace pdf
