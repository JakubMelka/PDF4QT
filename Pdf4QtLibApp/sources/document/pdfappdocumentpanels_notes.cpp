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
#include "pdfappdocument.h"
#include "pdfappdocumentpanels_common.h"
#include "pdfappdocumentview.h"

#include "pdfcatalog.h"
#include "pdfexception.h"
#include "pdffluentwidgets.h"
#include "pdfthemefonts.h"

#include <QEvent>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLocale>
#include <QPainter>
#include <QVBoxLayout>

#include <atomic>
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
                note.isReply = markup->getInReplyTo().isValid();
                note.author = PDFAppNotesHelper::sanitizeText(markup->getWindowTitle()).left(256);
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

void PDFAppNotesModel::rebuildRows()
{
    m_rows.clear();
    PDFInteger lastPage = -1;
    for (int i = 0; i < int(m_notes.size()); ++i)
    {
        const PDFAppNote& note = m_notes[size_t(i)];
        if (!m_filterText.isEmpty() && !note.author.contains(m_filterText, Qt::CaseInsensitive) && !note.text.contains(m_filterText, Qt::CaseInsensitive))
        {
            continue;
        }

        if (note.page != lastPage)
        {
            m_rows.push_back({ note.page, -1 });
            lastPage = note.page;
        }
        m_rows.push_back({ note.page, i });
    }
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
    switch (role)
    {
        case Qt::DisplayRole:
            return note.author.isEmpty() ? PDFAppNotesPanel::tr("Unknown author") : note.author;

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

        case Qt::ToolTipRole:
        case Qt::AccessibleTextRole:
            return note.text.isEmpty() ? (note.author.isEmpty() ? PDFAppNotesPanel::tr("Unknown author") : note.author)
                                       : QStringLiteral("%1: %2").arg(note.author.isEmpty() ? PDFAppNotesPanel::tr("Unknown author") : note.author, note.text);

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

void PDFAppNotesDelegate::paintItem(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index, const PDFComponentPainter::State& state) const
{
    if (!index.data(PDFAppNotesModel::CaptionRole).toBool())
    {
        BaseClass::paintItem(painter, option, index, state);
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
    m_view->setSelectionMode(QAbstractItemView::SingleSelection);
    m_view->setResizeMode(QListView::Adjust);
    m_view->setAccessibleName(tr("Notes"));
    PDFAppPanelWidgets::prepareList(m_view);

    m_model = new PDFAppNotesModel(m_view);
    m_delegate = new PDFAppNotesDelegate(m_view);
    m_view->setModel(m_model);
    m_view->setItemDelegate(m_delegate);
    m_view->installEventFilter(this);
    layout->addWidget(m_view, 1);

    connect(m_searchField, &QLineEdit::textChanged, this, &PDFAppNotesPanel::onSearchTextChanged);
    connect(m_view, &QAbstractItemView::clicked, this, &PDFAppNotesPanel::onClicked);
}

PDFAppNotesPanel::~PDFAppNotesPanel()
{
    if (m_connectedDocument)
    {
        m_connectedDocument->disconnect(this);
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
    // 195:3910 "5 notes on 3 pages"
    const int notes = m_model ? m_model->getNoteCount() : 0;
    if (notes == 0)
    {
        return QString();
    }
    const int pages = m_model->getPageCount();
    const QString noteText = notes == 1 ? tr("%Ln note", nullptr, notes) : tr("%Ln notes", nullptr, notes);
    const QString pageText = pages == 1 ? tr("%Ln page", nullptr, pages) : tr("%Ln pages", nullptr, pages);
    //: "5 notes on 3 pages"
    return tr("%1 on %2").arg(noteText, pageText);
}

QString PDFAppNotesPanel::getIconName() const
{
    return QStringLiteral("message-square");
}

void PDFAppNotesPanel::setDocument(PDFAppDocument* document, PDFAppDocumentView* view)
{
    if (m_connectedDocument && m_connectedDocument != document)
    {
        m_connectedDocument->disconnect(this);
    }

    BaseClass::setDocument(document, view);

    if (document && m_connectedDocument != document)
    {
        connect(document, &PDFAppDocument::documentChanged, this, &PDFAppNotesPanel::onDocumentChanged);
        connect(document, &QObject::destroyed, this, &PDFAppNotesPanel::onDocumentDestroyed);
    }
    m_connectedDocument = document;
    rebuild();
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
        m_model->setNotes({}, PDFDocumentPointer());
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

    m_document = pointer;
    if (m_model)
    {
        // The list of the version is shared with the tab of the sidebar
        m_model->setNotes(pointer ? *PDFAppNotes::getNotes(document) : std::vector<PDFAppNote>(), pointer);
    }
    Q_EMIT titleChanged();
}

void PDFAppNotesPanel::onSearchTextChanged(const QString& text)
{
    if (m_model)
    {
        m_model->setFilterText(text);
    }
}

void PDFAppNotesPanel::onClicked(const QModelIndex& index)
{
    if (index.isValid())
    {
        activateRow(index.row());
    }
}

void PDFAppNotesPanel::activateRow(int row)
{
    PDFAppDocumentView* view = getView();
    const PDFAppNote* note = m_model ? m_model->getNote(row) : nullptr;
    if (view && note)
    {
        view->goToPage(note->page);
    }
}

bool PDFAppNotesPanel::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_view && m_view && event->type() == QEvent::KeyPress)
    {
        const QKeyEvent* keyEvent = static_cast<const QKeyEvent*>(event);
        if ((keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter) && keyEvent->modifiers() == Qt::NoModifier && m_view->currentIndex().isValid())
        {
            activateRow(m_view->currentIndex().row());
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
