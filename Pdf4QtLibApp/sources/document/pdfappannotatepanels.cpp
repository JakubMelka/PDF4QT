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

#include "pdfappannotatepanels.h"
#include "pdfappannotatecommands.h"
#include "pdfappcommand.h"
#include "pdfappdocument.h"
#include "pdfappdocumentpanel.h"
#include "pdfappdocumentpanels_common.h"
#include "pdfappdocumentview.h"
#include "pdfapplocale.h"
#include "pdfappoutlineeditor.h"
#include "pdfapppagelayout.h"
#include "pdfapproute.h"

#include "pdfdocument.h"

#include "pdfcomponentbuttons.h"
#include "pdfcomponentpanels.h"
#include "pdffluentwidgets.h"
#include "pdfthemeiconengine.h"

#include <QCoreApplication>
#include <QHBoxLayout>
#include <QToolButton>
#include <QVBoxLayout>

namespace pdf
{

/// Values of the panels of package A
class PDFAppAnnotatePanelsHelper
{
public:
    PDFAppAnnotatePanelsHelper() = delete;

    static constexpr const char* VARIANT_PROPERTY = "pdf4qtVariant";

    /// Gap between a caption (13 high in the design) and its tiles: the tiles lie
    /// 21 px below the top of the section (877:10160, 196:3191)
    static constexpr int CAPTION_GAP = 7;

    /// Tool id of Insert - Page numbers (the tool route, Page Not Found in phase 1)
    static QString getPageNumbersToolId() { return QStringLiteral("edit.page-numbers"); }

    /// Annotation is a comment of the Notes panel (a markup annotation, which is
    /// not a reply and not a popup)
    static bool isComment(const PDFObjectStorage& storage, PDFObjectReference reference)
    {
        const PDFObject& object = storage.getObjectByReference(reference);
        const PDFDictionary* dictionary = object.isDictionary() ? object.getDictionary() : nullptr;
        if (!dictionary || dictionary->hasKey("IRT"))
        {
            return false;
        }

        const PDFObject& subtype = storage.getObject(dictionary->get("Subtype"));
        if (!subtype.isName())
        {
            return false;
        }

        // Annotations, which are not markups (PDF 2.0, 12.5.6.2 Table 171)
        static const QByteArray notMarkups[] = { "Popup", "Link", "Widget", "Screen", "PrinterMark", "TrapNet", "Watermark", "3D", "RichMedia", "Movie" };
        const QByteArray name = subtype.getString();
        for (const QByteArray& notMarkup : notMarkups)
        {
            if (name == notMarkup)
            {
                return false;
            }
        }
        return true;
    }
};

// --------------------------------------------------------- PDFAppAnnotatePanel

PDFAppAnnotatePanel::PDFAppAnnotatePanel(PDFAppDocumentView* view, QWidget* parent) :
    BaseClass(view, PANEL_WIDTH, parent),
    m_creator(PDFAppAnnotateCommandHandler::findCreator(view))
{
    setObjectName(QStringLiteral("annotatePanel"));
    setTitleText(QT_TRANSLATE_NOOP("pdf::PDFAppDocumentPanelPopup", "Annotate"));

    QVBoxLayout* layout = getContentLayout();

    // section-text-markup 877:10158: caption, tiles 877:10160
    QWidget* markupSection = new QWidget(this);
    QVBoxLayout* markupLayout = new QVBoxLayout(markupSection);
    markupLayout->setContentsMargins(0, 0, 0, 0);
    markupLayout->setSpacing(PDFAppAnnotatePanelsHelper::CAPTION_GAP);
    markupLayout->addWidget(createCaption(QT_TRANSLATE_NOOP("pdf::PDFAppDocumentPanelPopup", "Text markup")));
    QWidget* tiles = createTileRow({ createTile(PDFAppCommands::annotateHighlight(), QStringLiteral("highlighter"), QT_TRANSLATE_NOOP("pdf::PDFAppDocumentPanelPopup", "Highlight")),
                                     createTile(PDFAppCommands::annotateUnderline(), QStringLiteral("underline"), QT_TRANSLATE_NOOP("pdf::PDFAppDocumentPanelPopup", "Underline")),
                                     createTile(PDFAppCommands::annotateStrikeout(), QStringLiteral("strikethrough"), QT_TRANSLATE_NOOP("pdf::PDFAppDocumentPanelPopup", "Strikeout")),
                                     createTile(PDFAppCommands::annotateSquiggly(), QStringLiteral("squiggly"), QT_TRANSLATE_NOOP("pdf::PDFAppDocumentPanelPopup", "Squiggly")),
                                     createTile(PDFAppCommands::annotateHighlightComment(), QStringLiteral("highlight-comment"), QT_TRANSLATE_NOOP("pdf::PDFAppDocumentPanelPopup", "Highlight and comment")) });
    if (QHBoxLayout* tileLayout = qobject_cast<QHBoxLayout*>(tiles->layout()))
    {
        // Four tiles 88 and Highlight and comment 140 (the stretch shares the width in that ratio)
        for (int i = 0; i < tileLayout->count(); ++i)
        {
            tileLayout->setStretch(i, i < 4 ? MARKUP_TILE_WIDTH : COMMENT_TILE_WIDTH);
        }
    }
    markupLayout->addWidget(tiles);
    layout->addWidget(markupSection);

    // section-color 877:10186: caption, swatches 877:10188 (272 x 32)
    QWidget* colorSection = new QWidget(this);
    QVBoxLayout* colorLayout = new QVBoxLayout(colorSection);
    colorLayout->setContentsMargins(0, 0, 0, 0);
    colorLayout->setSpacing(PDFAppAnnotatePanelsHelper::CAPTION_GAP);
    colorLayout->addWidget(createCaption(QT_TRANSLATE_NOOP("pdf::PDFAppDocumentPanelPopup", "Color")));
    m_colorRow = new PDFColorSwatchRow(colorSection);
    m_colorRow->setObjectName(QStringLiteral("annotateColors"));
    colorLayout->addWidget(m_colorRow, 0, Qt::AlignLeading);
    layout->addWidget(colorSection);
    connect(m_colorRow.data(), &PDFColorSwatchRow::colorSelected, this, &PDFAppAnnotatePanel::onColorSelected);

    // divider 877:10210, item-show-all-comments 877:10212
    layout->addWidget(createDivider());
    m_showComments = createActionItem(QStringLiteral("message-square"), nullptr);
    m_showComments->setObjectName(QStringLiteral("item:showAllComments"));
    connect(m_showComments.data(), &QAbstractButton::clicked, this, &PDFAppAnnotatePanel::onShowCommentsClicked);
    layout->addWidget(m_showComments);

    if (m_creator)
    {
        connect(m_creator.data(), &PDFAppAnnotationCreator::toolStateChanged, this, &PDFAppAnnotatePanel::onCreatorStateChanged);
    }
    retranslate();
}

PDFAppAnnotatePanel::~PDFAppAnnotatePanel() = default;

int PDFAppAnnotatePanel::countComments(const PDFDocument* document)
{
    const PDFCatalog* catalog = document ? document->getCatalog() : nullptr;
    if (!catalog)
    {
        return 0;
    }

    int count = 0;
    const PDFObjectStorage& storage = document->getStorage();
    for (size_t i = 0; i < catalog->getPageCount() && count < MAXIMUM_COUNTED_COMMENTS; ++i)
    {
        for (const PDFObjectReference& reference : catalog->getPage(i)->getAnnotations())
        {
            if (PDFAppAnnotatePanelsHelper::isComment(storage, reference) && ++count >= MAXIMUM_COUNTED_COMMENTS)
            {
                break;
            }
        }
    }
    return count;
}

AnnotationType PDFAppAnnotatePanel::getColorMarkupType() const
{
    const PDFAppAnnotationCreator* creator = m_creator.data();
    if (!creator)
    {
        return AnnotationType::Highlight;
    }

    const AnnotationType activeType = PDFAppAnnotationTools::getMarkupType(creator->getActiveKind());
    return activeType != AnnotationType::Invalid ? activeType : creator->getLastMarkupType();
}

void PDFAppAnnotatePanel::updateState()
{
    BaseClass::updateState();

    // The active tile is the active markup tool, else the last used markup type
    // (Action tile Active=True, 877:10161); the commands give the checked state of
    // an active tool, the last used type is shown active without one
    const PDFAppAnnotationToolKind activeKind = m_creator ? m_creator->getActiveKind() : PDFAppAnnotationToolKind::None;
    if (m_creator && !PDFAppAnnotationTools::isMarkup(activeKind))
    {
        const QString commandId = PDFAppAnnotationTools::getCommandId(PDFAppAnnotationTools::getMarkupKind(m_creator->getLastMarkupType()));
        if (QAbstractButton* tile = getCommandButton(commandId))
        {
            const QSignalBlocker blocker(tile);
            tile->setChecked(tile->isEnabled());
        }
    }
    if (m_colorRow)
    {
        m_colorRow->setColor(PDFAppAnnotationToolSettings::getMarkupColor(getColorMarkupType()));
        m_colorRow->setEnabled(m_creator && m_creator->isCreationAllowed());
    }

    // The count of the comments is read, when the panel opens
    const PDFAppDocumentView* view = getView();
    const PDFAppDocument* document = view ? view->getDocument() : nullptr;
    const int count = document && document->isReady() ? countComments(document->getDocument().data()) : 0;
    if (count != m_commentCount || m_showComments->text().isEmpty())
    {
        m_commentCount = count;
        retranslate();
    }
}

void PDFAppAnnotatePanel::retranslate()
{
    BaseClass::retranslate();
    if (m_showComments)
    {
        //: Row of the Annotate panel, which opens the Notes tab (877:10212)
        const QString text = QCoreApplication::translate("pdf::PDFAppDocumentPanelPopup", "Show all comments (%1)").arg(PDFAppLocale::formatNumber(m_commentCount));
        m_showComments->setText(text);
        m_showComments->setAccessibleName(text);
    }
    if (m_colorRow)
    {
        m_colorRow->setAccessibleName(QCoreApplication::translate("pdf::PDFAppDocumentPanelPopup", "Color of the markup"));
    }
}

PDFColorSwatchRow* PDFAppAnnotatePanel::getColorRow() const
{
    return m_colorRow.data();
}

PDFAppPanelMenuItem* PDFAppAnnotatePanel::getShowCommentsItem() const
{
    return m_showComments.data();
}

void PDFAppAnnotatePanel::onColorSelected(const QColor& color)
{
    // The color of the active markup is remembered for the next markups; the
    // panel stays open (tools.md 9.8)
    if (PDFAppAnnotationCreator* creator = m_creator.data())
    {
        creator->setColor(PDFAppAnnotationTools::getMarkupKind(getColorMarkupType()), PDFAppAnnotationCreator::ColorTarget::Stroke, color);
    }
}

void PDFAppAnnotatePanel::onShowCommentsClicked()
{
    // Navigation in the document: the Notes tab of the sidebar
    QPointer<PDFAppDocumentView> view = getView();
    close();
    if (view)
    {
        view->showPanel(PDFAppDocumentPanelIds::notes());
    }
}

void PDFAppAnnotatePanel::onCreatorStateChanged()
{
    if (isVisible())
    {
        updateState();
    }
}

// ----------------------------------------------------------- PDFAppInsertPanel

PDFAppInsertPanel::PDFAppInsertPanel(PDFAppDocumentView* view, QWidget* parent) :
    BaseClass(view, PANEL_WIDTH, parent),
    m_creator(PDFAppAnnotateCommandHandler::findCreator(view))
{
    setObjectName(QStringLiteral("insertPanel"));
    setTitleText(QT_TRANSLATE_NOOP("pdf::PDFAppDocumentPanelPopup", "Insert"));

    QVBoxLayout* layout = getContentLayout();
    // Text and notes 196:3189: Text box, Comment, Note, Help, Key, Insert, Paragraph, New paragraph
    std::vector<QToolButton*> textTiles = { createTile(PDFAppCommands::insertTextBox(), QStringLiteral("text-box"), QT_TRANSLATE_NOOP("pdf::PDFAppDocumentPanelPopup", "Text box")) };
    static const char* stickyNames[] = { QT_TRANSLATE_NOOP("pdf::PDFAppDocumentPanelPopup", "Comment"),
                                         QT_TRANSLATE_NOOP("pdf::PDFAppDocumentPanelPopup", "Note"),
                                         QT_TRANSLATE_NOOP("pdf::PDFAppDocumentPanelPopup", "Help"),
                                         QT_TRANSLATE_NOOP("pdf::PDFAppDocumentPanelPopup", "Key"),
                                         QT_TRANSLATE_NOOP("pdf::PDFAppDocumentPanelPopup", "Insert"),
                                         QT_TRANSLATE_NOOP("pdf::PDFAppDocumentPanelPopup", "Paragraph"),
                                         QT_TRANSLATE_NOOP("pdf::PDFAppDocumentPanelPopup", "New paragraph") };
    const std::vector<TextAnnotationIcon> icons = PDFAppAnnotationTools::getStickyNoteIcons();
    for (size_t i = 0; i < icons.size() && i < std::size(stickyNames); ++i)
    {
        const TextAnnotationIcon icon = icons[i];
        QToolButton* tile = createVariantTile(QStringLiteral("tile:sticky:") + PDFAppAnnotationTools::getStickyNoteIconName(icon), PDFAppAnnotationTools::getStickyNoteIconName(icon));
        tile->setProperty(PDFAppAnnotatePanelsHelper::VARIANT_PROPERTY, int(icon));
        tile->setCheckable(true);
        addText(tile, stickyNames[i]);
        connect(tile, &QToolButton::clicked, this, &PDFAppInsertPanel::onStickyNoteClicked);
        m_stickyNoteTiles[icon] = tile;
        textTiles.push_back(tile);
    }
    addSection(QT_TRANSLATE_NOOP("pdf::PDFAppDocumentPanelPopup", "Text and notes"), createTileRow(textTiles, TILE_COLUMNS));

    // Shapes 196:3227
    addSection(QT_TRANSLATE_NOOP("pdf::PDFAppDocumentPanelPopup", "Shapes"),
               createTileRow({ createTile(PDFAppCommands::insertLine(), QStringLiteral("line"), QT_TRANSLATE_NOOP("pdf::PDFAppDocumentPanelPopup", "Line")),
                               createTile(PDFAppCommands::insertPolyline(), QStringLiteral("polyline"), QT_TRANSLATE_NOOP("pdf::PDFAppDocumentPanelPopup", "Polyline")),
                               createTile(PDFAppCommands::insertRectangle(), QStringLiteral("square"), QT_TRANSLATE_NOOP("pdf::PDFAppDocumentPanelPopup", "Rectangle")),
                               createTile(PDFAppCommands::insertPolygon(), QStringLiteral("polygon"), QT_TRANSLATE_NOOP("pdf::PDFAppDocumentPanelPopup", "Polygon")),
                               createTile(PDFAppCommands::insertEllipse(), QStringLiteral("circle"), QT_TRANSLATE_NOOP("pdf::PDFAppDocumentPanelPopup", "Ellipse")),
                               createTile(PDFAppCommands::insertFreehand(), QStringLiteral("scribble"), QT_TRANSLATE_NOOP("pdf::PDFAppDocumentPanelPopup", "Freehand")) },
                             TILE_COLUMNS));

    // Links and pages 196:3260: Web link, Link to page, Page numbers, Outline item
    m_pageNumbersTile = createVariantTile(QStringLiteral("tile:pageNumbers"), QStringLiteral("hash"));
    addText(m_pageNumbersTile, QT_TRANSLATE_NOOP("pdf::PDFAppDocumentPanelPopup", "Page numbers"));
    connect(m_pageNumbersTile.data(), &QToolButton::clicked, this, &PDFAppInsertPanel::onPageNumbersClicked);
    m_outlineItemTile = createVariantTile(QStringLiteral("tile:outlineItem"), QStringLiteral("list"));
    addText(m_outlineItemTile, QT_TRANSLATE_NOOP("pdf::PDFAppDocumentPanelPopup", "Outline item"));
    connect(m_outlineItemTile.data(), &QToolButton::clicked, this, &PDFAppInsertPanel::onOutlineItemClicked);
    addSection(QT_TRANSLATE_NOOP("pdf::PDFAppDocumentPanelPopup", "Links and pages"),
               createTileRow({ createTile(PDFAppCommands::insertWebLink(), QStringLiteral("link"), QT_TRANSLATE_NOOP("pdf::PDFAppDocumentPanelPopup", "Web link")),
                               createTile(PDFAppCommands::insertPageLink(), QStringLiteral("link-page"), QT_TRANSLATE_NOOP("pdf::PDFAppDocumentPanelPopup", "Link to page")),
                               m_pageNumbersTile.data(), m_outlineItemTile.data() },
                             TILE_COLUMNS));

    // Stamp 196:3283: 14 Stamp chips 190:779 in two rows, gaps 6
    QWidget* stamps = new QWidget(this);
    stamps->setObjectName(QStringLiteral("stamps"));
    PDFAppFlowLayout* stampLayout = new PDFAppFlowLayout(stamps, STAMP_GAP, STAMP_GAP);
    for (Stamp stamp : PDFAppAnnotationTools::getStamps())
    {
        PDFStampChip* chip = new PDFStampChip(QString(), PDFAppAnnotationTools::getStampHue(stamp), stamps);
        chip->setObjectName(QStringLiteral("stamp:%1").arg(int(stamp)));
        chip->setProperty(PDFAppAnnotatePanelsHelper::VARIANT_PROPERTY, int(stamp));
        connect(chip, &QAbstractButton::clicked, this, &PDFAppInsertPanel::onStampClicked);
        stampLayout->addWidget(chip);
        m_stampChips[stamp] = chip;
    }
    addSection(QT_TRANSLATE_NOOP("pdf::PDFAppDocumentPanelPopup", "Stamp"), stamps);

    // divider 196:3314, item-delete-annotations 196:3316
    layout->addWidget(createDivider());
    layout->addWidget(createMenuItem(PDFAppCommands::annotateDeleteTool(), QStringLiteral("eraser"), QT_TRANSLATE_NOOP("pdf::PDFAppDocumentPanelPopup", "Delete annotations")));

    if (m_creator)
    {
        connect(m_creator.data(), &PDFAppAnnotationCreator::toolStateChanged, this, &PDFAppInsertPanel::onCreatorStateChanged);
    }
    retranslate();
}

PDFAppInsertPanel::~PDFAppInsertPanel() = default;

void PDFAppInsertPanel::addSection(const char* caption, QWidget* content)
{
    // Sections 196:3189 …: caption, the content 21 px below the top
    QWidget* section = new QWidget(this);
    QVBoxLayout* sectionLayout = new QVBoxLayout(section);
    sectionLayout->setContentsMargins(0, 0, 0, 0);
    sectionLayout->setSpacing(PDFAppAnnotatePanelsHelper::CAPTION_GAP);
    sectionLayout->addWidget(createCaption(caption));
    content->setParent(section);
    sectionLayout->addWidget(content);
    getContentLayout()->addWidget(section);
}

QToolButton* PDFAppInsertPanel::createVariantTile(const QString& objectName, const QString& iconName)
{
    // The look of the action tile of a command (createTile) without a command
    QToolButton* tile = new QToolButton(this);
    tile->setObjectName(objectName);
    tile->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    tile->setIcon(PDFThemeIcons::getIcon(iconName));
    tile->setIconSize(QSize(18, 18));
    tile->setFocusPolicy(Qt::TabFocus);
    tile->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    tile->setFixedHeight(TILE_HEIGHT);
    return tile;
}

void PDFAppInsertPanel::retranslate()
{
    BaseClass::retranslate();
    for (const auto& item : m_stampChips)
    {
        if (PDFStampChip* chip = item.second.data())
        {
            chip->setText(PDFAppAnnotationTools::getStampName(item.first));
            chip->setAccessibleName(PDFAppAnnotationTools::tr("Stamp “%1”").arg(chip->text()));
        }
    }
}

void PDFAppInsertPanel::updateState()
{
    BaseClass::updateState();

    const PDFAppAnnotationCreator* creator = m_creator.data();
    const bool isAllowed = creator && creator->isCreationAllowed();
    const bool isStickyActive = creator && creator->getActiveKind() == PDFAppAnnotationToolKind::StickyNote;
    const TextAnnotationIcon activeIcon = creator ? creator->getStickyNoteIcon() : TextAnnotationIcon::Comment;
    for (const auto& item : m_stickyNoteTiles)
    {
        if (QToolButton* tile = item.second.data())
        {
            const QSignalBlocker blocker(tile);
            tile->setEnabled(isAllowed);
            tile->setChecked(isStickyActive && item.first == activeIcon);
        }
    }
    for (const auto& item : m_stampChips)
    {
        if (PDFStampChip* chip = item.second.data())
        {
            chip->setEnabled(isAllowed);
        }
    }

    const PDFAppDocumentView* view = getView();
    const PDFAppOutlineEditor* outlineEditor = view ? view->getOutlineEditor() : nullptr;
    if (m_outlineItemTile)
    {
        m_outlineItemTile->setEnabled(outlineEditor && outlineEditor->isEditingAllowed());
    }
    if (m_pageNumbersTile)
    {
        m_pageNumbersTile->setEnabled(isAllowed);
    }
}

QToolButton* PDFAppInsertPanel::getStickyNoteTile(TextAnnotationIcon icon) const
{
    auto it = m_stickyNoteTiles.find(icon);
    return it != m_stickyNoteTiles.end() ? it->second.data() : nullptr;
}

QToolButton* PDFAppInsertPanel::getPageNumbersTile() const
{
    return m_pageNumbersTile.data();
}

QToolButton* PDFAppInsertPanel::getOutlineItemTile() const
{
    return m_outlineItemTile.data();
}

PDFStampChip* PDFAppInsertPanel::getStampChip(Stamp stamp) const
{
    auto it = m_stampChips.find(stamp);
    return it != m_stampChips.end() ? it->second.data() : nullptr;
}

void PDFAppInsertPanel::onStickyNoteClicked()
{
    // The tiles do not identify themselves in the signal
    const QObject* tile = sender();
    QPointer<PDFAppAnnotationCreator> creator = m_creator;
    if (!tile || !creator)
    {
        return;
    }

    creator->setStickyNoteIcon(TextAnnotationIcon(tile->property(PDFAppAnnotatePanelsHelper::VARIANT_PROPERTY).toInt()));
    close();
    if (creator)
    {
        creator->requestTool(PDFAppAnnotationToolKind::StickyNote);
    }
}

void PDFAppInsertPanel::onStampClicked()
{
    const QObject* chip = sender();
    QPointer<PDFAppAnnotationCreator> creator = m_creator;
    if (!chip || !creator)
    {
        return;
    }

    creator->setStamp(Stamp(chip->property(PDFAppAnnotatePanelsHelper::VARIANT_PROPERTY).toInt()));
    close();
    if (creator)
    {
        creator->requestTool(PDFAppAnnotationToolKind::Stamp);
    }
}

void PDFAppInsertPanel::onPageNumbersClicked()
{
    // The tool of the catalog (Page Not Found in phase 1); the route is opened
    // after the panel has closed
    QPointer<PDFAppDocumentView> view = getView();
    const PDFAppDocument* document = view ? view->getDocument() : nullptr;
    const QString documentId = document ? document->getId() : QString();
    close();
    if (view && !documentId.isEmpty())
    {
        PDFAppPanelWidgets::navigate(view, PDFAppRoutes::documentTool(documentId, PDFAppAnnotatePanelsHelper::getPageNumbersToolId()));
    }
}

void PDFAppInsertPanel::onOutlineItemClicked()
{
    // The outline editor shows the Outline panel and starts the rename - after
    // the panel has closed (from the event loop)
    close();
    QMetaObject::invokeMethod(this, &PDFAppInsertPanel::onOutlineItemQueued, Qt::QueuedConnection);
}

void PDFAppInsertPanel::onOutlineItemQueued()
{
    PDFAppDocumentView* view = getView();
    if (PDFAppOutlineEditor* outlineEditor = view ? view->getOutlineEditor() : nullptr)
    {
        outlineEditor->createItemAtCurrentView();
    }
}

void PDFAppInsertPanel::onCreatorStateChanged()
{
    if (isVisible())
    {
        updateState();
    }
}

}   // namespace pdf
