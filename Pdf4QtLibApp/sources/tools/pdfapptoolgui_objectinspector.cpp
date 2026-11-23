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

#include "pdfapptoolgui_objectinspector.h"
#include "pdfappdocument.h"
#include "pdfappdocumentpanels_common.h"
#include "pdfappdocumentpermissions.h"
#include "pdfappdocumentsearchindex.h"
#include "pdfappdocumentsoftproof.h"
#include "pdfappdocumentview.h"
#include "pdfapplocale.h"
#include "pdfappobjecttreemodel.h"
#include "pdfappstatisticstable.h"
#include "pdfapptoolhost.h"
#include "pdfapptoolpanelparts.h"
#include "pdfapptoolpanelpopup.h"
#include "pdfapptoolparametermodel.h"
#include "pdfapptoolvalueformatter.h"
#include "pdfappworkspace.h"
#include "pdfdocumenttoolguiregistry.h"
#include "pdfdocumenttoolregistry.h"
#include "pdfdocumenttoolshell.h"

#include "pdfcomponentpanels.h"
#include "pdfcomponentrows.h"
#include "pdfdocumenttool_objectinspector.h"
#include "pdfpainterutils.h"
#include "pdfstylepainter.h"
#include "pdfthemefonts.h"
#include "pdfthemeiconengine.h"
#include "pdfwidgetstyleproperties.h"

#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QMenu>
#include <QMimeData>
#include <QPainter>
#include <QStackedWidget>
#include <QTextDocumentFragment>
#include <QToolButton>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

#include <limits>

namespace pdf
{

/// The names of the parameters and the texts of Object Inspector
class PDFAppToolGUIObjectInspectorHelper
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFAppToolGUIObjectInspector)

public:
    PDFAppToolGUIObjectInspectorHelper() = delete;

    using Tool = PDFDocumentToolObjectInspector;

    /// Property of a row of the menu of the views and of a button of a tab
    static constexpr const char* VIEW_PROPERTY = "pdf4qtInspectorView";
    static constexpr const char* TAB_PROPERTY = "pdf4qtInspectorTab";

    static QString name(const char* parameter) { return QString::fromLatin1(parameter); }

    /// The text of a value of the dictionary of an image (a name without its slash, an array by its first name)
    static QString getValueText(const PDFDocument* document, const PDFObject& value)
    {
        const PDFObject& object = document ? document->getObject(value) : value;
        if (object.isName())
        {
            return QString::fromLatin1(object.getString());
        }
        if (object.isInt())
        {
            return PDFAppLocale::formatNumber(object.getInteger());
        }
        if (object.isArray() && object.getArray()->getCount() > 0)
        {
            const PDFObject& first = document ? document->getObject(object.getArray()->getItem(0)) : object.getArray()->getItem(0);
            if (first.isName())
            {
                return QString::fromLatin1(first.getString());
            }
        }
        return QStringLiteral("–");
    }
};

// ---------------------------------------------------------- PDFAppObjectContentsView

PDFAppObjectContentsView::PDFAppObjectContentsView(QWidget* parent) :
    BaseClass(parent)
{
    setReadOnly(true);
    setFrameShape(QFrame::NoFrame);
    setLineWrapMode(QPlainTextEdit::NoWrap);
    setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    setFont(PDFThemeFonts::getFont(PDFThemeFonts::Role::Small));

    // The contents of 482:14847 (RS7FIGMA): padding 16 on every side, lines of 19
    setViewportMargins(0, 0, 0, 0);
    document()->setDocumentMargin(PADDING);
    setLineHeight(LINE_HEIGHT);
}

PDFAppObjectContentsView::~PDFAppObjectContentsView() = default;

void PDFAppObjectContentsView::copySelection()
{
    const QString text = textCursor().hasSelection() ? textCursor().selection().toPlainText() : toPlainText();
    if (QClipboard* clipboard = QApplication::clipboard())
    {
        clipboard->setText(text);
    }
}

void PDFAppObjectContentsView::requestCopy()
{
    if (!m_isCopyGuarded)
    {
        copySelection();
        return;
    }

    // The GUI checks the permission; its dialog runs foreign code: the last statement
    Q_EMIT copyRequested();
}

void PDFAppObjectContentsView::keyPressEvent(QKeyEvent* event)
{
    if (event->matches(QKeySequence::Copy))
    {
        event->accept();
        requestCopy();
        return;
    }
    BaseClass::keyPressEvent(event);
}

void PDFAppObjectContentsView::contextMenuEvent(QContextMenuEvent* event)
{
    // A menu of its own: the copy goes through requestCopy (the standard menu would copy by itself)
    event->accept();
    QMenu* menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    QAction* copyAction = menu->addAction(tr("Copy"));
    copyAction->setShortcut(QKeySequence::Copy);
    connect(copyAction, &QAction::triggered, this, &PDFAppObjectContentsView::requestCopy);
    QAction* selectAllAction = menu->addAction(tr("Select All"));
    selectAllAction->setShortcut(QKeySequence::SelectAll);
    connect(selectAllAction, &QAction::triggered, this, &PDFAppObjectContentsView::onSelectAll);
    menu->popup(event->globalPos());
}

void PDFAppObjectContentsView::onSelectAll()
{
    selectAll();
}

QMimeData* PDFAppObjectContentsView::createMimeDataFromSelection() const
{
    // Every way out of the view, which is not the guarded copy (a drag, the selection of X11), gives no data of a
    // stream (R7-22 (6), Q7.2)
    if (m_isCopyGuarded)
    {
        return new QMimeData();
    }
    return BaseClass::createMimeDataFromSelection();
}

// ---------------------------------------------------------- PDFAppObjectImageView

PDFAppObjectImageView::PDFAppObjectImageView(QWidget* parent) :
    BaseClass(parent)
{
    setMinimumSize(64, 64);
}

PDFAppObjectImageView::~PDFAppObjectImageView() = default;

void PDFAppObjectImageView::setImage(QImage image)
{
    m_image = std::move(image);
    update();
}

void PDFAppObjectImageView::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    // 482:15559: the picture in the middle of the contents, fitted, never enlarged
    QPainter painter(this);
    PDFPainterStateGuard guard(&painter);
    PDFStylePainter::fillRect(&painter, QRectF(rect()), PDFThemeColors::BgSurface);
    if (m_image.isNull())
    {
        return;
    }

    const QRectF area = QRectF(rect()).adjusted(16, 16, -16, -16);
    QSizeF size = QSizeF(m_image.size()) / m_image.devicePixelRatio();
    if (size.width() > area.width() || size.height() > area.height())
    {
        size.scale(area.size(), Qt::KeepAspectRatio);
    }
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.drawImage(PDFStylePainter::centered(area, size), m_image);
}

// ---------------------------------------------------------- PDFAppToolGUIObjectInspector

PDFAppToolGUIObjectInspector::PDFAppToolGUIObjectInspector(QObject* parent) :
    BaseClass(parent)
{
    m_model = new PDFAppObjectTreeModel(this);

    Tab current;
    current.id = m_nextTabId++;
    m_tabs.push_back(std::move(current));
}

PDFAppToolGUIObjectInspector::~PDFAppToolGUIObjectInspector()
{
    // The jobs go on with their own copies; their cancellation stops them early
    cancelPreparation();
    cancelContentJob();
}

void PDFAppToolGUIObjectInspector::registerObjectInspectorTools()
{
    const QString toolId = QString::fromLatin1(PDFDocumentToolObjectInspector::TOOL_ID);
    if (!PDFDocumentToolRegistry::isImplemented(toolId))
    {
        PDFDocumentToolRegistry::registerTool(std::make_shared<PDFDocumentToolObjectInspector>());
    }
    if (!PDFDocumentToolGUIRegistry::getFactory(toolId))
    {
        PDFDocumentToolGUIRegistry::registerFactory(toolId, std::make_shared<PDFDocumentToolGUIFactoryOf<PDFAppToolGUIObjectInspector>>());
    }
}

QString PDFAppToolGUIObjectInspector::getCancelText() const
{
    return tr("Close");
}

QString PDFAppToolGUIObjectInspector::getViewTitle(PDFObjectInspectorView view)
{
    using Helper = PDFAppToolGUIObjectInspectorHelper;
    switch (view)
    {
        case PDFObjectInspectorView::Document:
            return Helper::tr("Document");
        case PDFObjectInspectorView::Pages:
            return Helper::tr("Pages");
        case PDFObjectInspectorView::ContentStreams:
            return Helper::tr("Content Streams");
        case PDFObjectInspectorView::GraphicStates:
            return Helper::tr("Graphic States");
        case PDFObjectInspectorView::ColorSpaces:
            return Helper::tr("Color Spaces");
        case PDFObjectInspectorView::Patterns:
            return Helper::tr("Patterns");
        case PDFObjectInspectorView::Shadings:
            return Helper::tr("Shadings");
        case PDFObjectInspectorView::Images:
            return Helper::tr("Images");
        case PDFObjectInspectorView::Forms:
            return Helper::tr("Forms");
        case PDFObjectInspectorView::Fonts:
            return Helper::tr("Fonts");
        case PDFObjectInspectorView::Actions:
            return Helper::tr("Actions");
        case PDFObjectInspectorView::Annotations:
            return Helper::tr("Annotations");
        case PDFObjectInspectorView::Objects:
            return Helper::tr("Object List");
    }
    return QString();
}

QString PDFAppToolGUIObjectInspector::getViewIconName(PDFObjectInspectorView view)
{
    // The icons of the rows of 482:15260
    switch (view)
    {
        case PDFObjectInspectorView::Document:
            return QStringLiteral("file");
        case PDFObjectInspectorView::Pages:
            return QStringLiteral("pages");
        case PDFObjectInspectorView::ContentStreams:
            return QStringLiteral("stream");
        case PDFObjectInspectorView::GraphicStates:
            return QStringLiteral("gstate");
        case PDFObjectInspectorView::ColorSpaces:
            return QStringLiteral("colorspace");
        case PDFObjectInspectorView::Patterns:
            return QStringLiteral("pattern");
        case PDFObjectInspectorView::Shadings:
            return QStringLiteral("gradient");
        case PDFObjectInspectorView::Images:
            return QStringLiteral("image");
        case PDFObjectInspectorView::Forms:
            return QStringLiteral("xobject-form");
        case PDFObjectInspectorView::Fonts:
            return QStringLiteral("font");
        case PDFObjectInspectorView::Actions:
            return QStringLiteral("zap");
        case PDFObjectInspectorView::Annotations:
            return QStringLiteral("message-square");
        case PDFObjectInspectorView::Objects:
            return QStringLiteral("list-ordered");
    }
    return QString();
}

QString PDFAppToolGUIObjectInspector::getViewSubtitle(PDFObjectInspectorView view, qint64 count)
{
    using Helper = PDFAppToolGUIObjectInspectorHelper;

    // 482:15183 "From the trailer of the document", 482:15358 "24 page objects", 482:15559 "38 image objects",
    // 482:16200 "10,275 objects by number"
    const int n = int(qBound(qint64(0), count, qint64((std::numeric_limits<int>::max)())));
    switch (view)
    {
        case PDFObjectInspectorView::Document:
            return Helper::tr("From the trailer of the document");
        case PDFObjectInspectorView::Pages:
            return Helper::tr("%Ln page objects", nullptr, n);
        case PDFObjectInspectorView::ContentStreams:
            return Helper::tr("%Ln content stream objects", nullptr, n);
        case PDFObjectInspectorView::GraphicStates:
            return Helper::tr("%Ln graphic state objects", nullptr, n);
        case PDFObjectInspectorView::ColorSpaces:
            return Helper::tr("%Ln color space objects", nullptr, n);
        case PDFObjectInspectorView::Patterns:
            return Helper::tr("%Ln pattern objects", nullptr, n);
        case PDFObjectInspectorView::Shadings:
            return Helper::tr("%Ln shading objects", nullptr, n);
        case PDFObjectInspectorView::Images:
            return Helper::tr("%Ln image objects", nullptr, n);
        case PDFObjectInspectorView::Forms:
            return Helper::tr("%Ln form objects", nullptr, n);
        case PDFObjectInspectorView::Fonts:
            return Helper::tr("%Ln font objects", nullptr, n);
        case PDFObjectInspectorView::Actions:
            return Helper::tr("%Ln action objects", nullptr, n);
        case PDFObjectInspectorView::Annotations:
            return Helper::tr("%Ln annotation objects", nullptr, n);
        case PDFObjectInspectorView::Objects:
            return Helper::tr("%Ln objects by number", nullptr, n);
    }
    return QString();
}

QString PDFAppToolGUIObjectInspector::getTypeTitle(const PDFObject& object)
{
    using Helper = PDFAppToolGUIObjectInspectorHelper;
    switch (object.getType())
    {
        case PDFObject::Type::Null:
            return Helper::tr("Null");
        case PDFObject::Type::Bool:
            return Helper::tr("Boolean");
        case PDFObject::Type::Int:
            return Helper::tr("Integer");
        case PDFObject::Type::Real:
            return Helper::tr("Real");
        case PDFObject::Type::String:
            return Helper::tr("String");
        case PDFObject::Type::Name:
            return Helper::tr("Name");
        case PDFObject::Type::Array:
            return Helper::tr("Array");
        case PDFObject::Type::Dictionary:
            return Helper::tr("Dictionary");
        case PDFObject::Type::Stream:
            return Helper::tr("Stream");
        case PDFObject::Type::Reference:
            return Helper::tr("Reference");
        default:
            break;
    }
    return QString();
}

QString PDFAppToolGUIObjectInspector::getReferenceTitle(const PDFObjectInspectorNode& node)
{
    using Helper = PDFAppToolGUIObjectInspectorHelper;

    // 482:14847 "1 0 R", 482:15989 "Part of 12 0 R"
    if (node.isTrailer)
    {
        return Helper::tr("Trailer");
    }
    if (node.reference.isValid())
    {
        return PDFObjectInspector::getReferenceText(node.reference);
    }
    if (node.container.isValid())
    {
        return Helper::tr("Part of %1").arg(PDFObjectInspector::getReferenceText(node.container));
    }
    return Helper::tr("Trailer");
}

QString PDFAppToolGUIObjectInspector::getTabTitle(const PDFObjectInspectorNode& node)
{
    using Helper = PDFAppToolGUIObjectInspectorHelper;

    // 482:15778 "1 0 R", "12 0 R (part)"
    if (node.isTrailer)
    {
        return Helper::tr("Trailer");
    }
    if (node.reference.isValid())
    {
        return PDFObjectInspector::getReferenceText(node.reference);
    }
    if (node.container.isValid())
    {
        return Helper::tr("%1 (part)").arg(PDFObjectInspector::getReferenceText(node.container));
    }
    return Helper::tr("Trailer (part)");
}

bool PDFAppToolGUIObjectInspector::isImage(const PDFObjectInspectorNode& node)
{
    if (!node.object.isStream() || !node.reference.isValid())
    {
        return false;
    }
    const PDFDictionary* dictionary = node.object.getStream()->getDictionary();
    const PDFObject& subtype = dictionary ? dictionary->get("Subtype") : PDFObject();
    return subtype.isName() && subtype.getString() == "Image";
}

PDFObjectInspectorView PDFAppToolGUIObjectInspector::getView() const
{
    using Helper = PDFAppToolGUIObjectInspectorHelper;
    const PDFAppToolParameterModel* model = getModel();
    const std::optional<PDFObjectInspectorView> view = model ? PDFObjectInspector::findView(model->getValue(Helper::name(Helper::Tool::PARAMETER_VIEW)).toString()) : std::nullopt;
    return view.value_or(PDFObjectInspectorView::Document);
}

void PDFAppToolGUIObjectInspector::setView(PDFObjectInspectorView view)
{
    using Helper = PDFAppToolGUIObjectInspectorHelper;
    if (PDFAppToolParameterModel* model = getModel())
    {
        // The slots of the model run: the last statement (onParametersChanged rebuilds the tree)
        model->setValue(Helper::name(Helper::Tool::PARAMETER_VIEW), PDFObjectInspector::getViewId(view));
    }
}

QToolButton* PDFAppToolGUIObjectInspector::getViewButton() const
{
    return m_viewMenu ? m_viewMenu->getButton() : nullptr;
}

PDFTileRow* PDFAppToolGUIObjectInspector::getViewRow(PDFObjectInspectorView view) const
{
    auto it = m_viewRows.find(view);
    return it != m_viewRows.cend() ? it->second.data() : nullptr;
}

std::vector<QToolButton*> PDFAppToolGUIObjectInspector::getTabButtons() const
{
    std::vector<QToolButton*> buttons;
    for (const QPointer<QToolButton>& button : m_tabButtons)
    {
        if (button)
        {
            buttons.push_back(button.data());
        }
    }
    return buttons;
}

// ------------------------------------------------------------------ the parts

void PDFAppToolGUIObjectInspector::describeToolbar(PDFAppToolPanelBuilder& builder)
{
    using Helper = PDFAppToolGUIObjectInspectorHelper;

    // tool-actions 482:15074: the view (482:15157, the panel 482:15260), a divider, Pin, Unpin
    m_viewRows.clear();
    const PDFObjectInspectorView view = getView();
    m_viewMenu = builder.addMenu(QStringLiteral("inspectorView"), getViewIconName(view), getViewTitle(view), VIEW_MENU_WIDTH);
    m_viewMenu->getButton()->setObjectName(QStringLiteral("inspectorView"));
    m_viewMenu->getButton()->setToolTip(tr("What the tree of objects shows"));
    for (const PDFObjectInspectorView item : PDFObjectInspector::getViews())
    {
        PDFTileRow* row = m_viewMenu->addRow(getViewTitle(item), QString(), getViewIconName(item), PDFTileRow::Kind::MenuItem);
        row->setObjectName(QStringLiteral("inspectorView-") + PDFObjectInspector::getViewId(item));
        row->setProperty(Helper::VIEW_PROPERTY, int(item));
        row->setSelectionBehavior(PDFComponentButton::SelectionBehavior::Exclusive);
        connect(row, &PDFTileRow::clicked, this, &PDFAppToolGUIObjectInspector::onViewRowClicked);
        m_viewRows[item] = row;
    }
    m_viewMenu->addGroupDivider();
    m_viewMenu->addGroupCaption(tr("A kind that the document does not contain is not listed."));

    builder.addActionDivider();
    m_pinButton = builder.addAction(tr("Pin"), QStringLiteral("pin"), this, &PDFAppToolGUIObjectInspector::onPinClicked);
    m_pinButton->setObjectName(QStringLiteral("inspectorPin"));
    m_pinButton->setToolTip(tr("Keep this object in its own tab"));
    m_unpinButton = builder.addAction(tr("Unpin"), QStringLiteral("pin-off"), this, &PDFAppToolGUIObjectInspector::onUnpinClicked);
    m_unpinButton->setObjectName(QStringLiteral("inspectorUnpin"));
    m_unpinButton->setToolTip(tr("Close the pinned tab"));

    updateViewMenu();
    updateButtons();
}

void PDFAppToolGUIObjectInspector::describeLeftPanel(PDFAppToolPanelBuilder& builder)
{
    // sidebar-left 482:15081: Objects / the subtitle of the view, the tree, the note
    m_objectsHeader = builder.addPanelHeader(tr("Objects"), QString());
    m_objectsHeader->setObjectName(QStringLiteral("inspectorObjectsHeader"));

    m_treeView = new PDFTreeView(builder.getWidget());
    m_treeView->setObjectName(QStringLiteral("inspectorTree"));
    m_treeView->setRowStyle(PDFTreeView::RowStyle::Outline);
    m_treeView->setHeaderHidden(true);
    m_treeView->setWordWrap(true);
    m_treeView->setUniformRowHeights(false);
    m_treeView->setModel(m_model.data());
    m_treeView->setAccessibleName(tr("Objects"));
    // object-tree 482:14976: the rows lie on the background of the sidebar, no frame (as the panels Outline and
    // Layers); the long rows wrap
    PDFAppPanelWidgets::prepareView(m_treeView);
    connect(m_treeView->selectionModel(), &QItemSelectionModel::currentChanged, this, &PDFAppToolGUIObjectInspector::onCurrentIndexChanged);
    builder.addWidget(m_treeView);
    builder.setFillsPanel(m_treeView);

    m_treeNote = builder.addHint(QString());
    m_treeNote->setObjectName(QStringLiteral("inspectorTreeNote"));

    // The tree of the version of now (prepared by the job, which the first description starts)
    if (!m_inspector && !m_prepareWatcher)
    {
        startPreparation();
    }
    updateViewMenu();
}

void PDFAppToolGUIObjectInspector::describeRightPanel(PDFAppToolPanelBuilder& builder)
{
    // sidebar-right 482:15088: Information (Reference, Type), Description, Image (482:15559), the note
    builder.addSection(tr("Information"));
    m_informationRows = builder.addKeyValues();
    m_informationRows->setObjectName(QStringLiteral("inspectorInformation"));
    builder.endSection();

    builder.addSection(tr("Description"));
    m_description = builder.addTextCard(QString());
    m_description->setObjectName(QStringLiteral("inspectorDescription"));
    builder.endSection();

    m_imageHeader = builder.addSection(tr("Image"));
    m_imageRows = builder.addKeyValues();
    m_imageRows->setObjectName(QStringLiteral("inspectorImage"));
    builder.endSection();

    m_tabNote = builder.addHint(QString());
    m_tabNote->setObjectName(QStringLiteral("inspectorTabNote"));
    updateRightPanel();
}

QWidget* PDFAppToolGUIObjectInspector::createCanvasPage(QWidget* parent)
{
    // document-canvas 482:15087: bg/track, padding 20; the tabs (482:15246), Contents with its caption
    // (482:15255), the white contents (482:15258)
    PDFAppToolCanvasMessage* page = new PDFAppToolCanvasMessage(parent);
    page->setObjectName(QStringLiteral("inspectorCanvas"));
    QVBoxLayout* layout = new QVBoxLayout(page);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->setSpacing(12);

    m_tabBar = new QWidget(page);
    m_tabBar->setObjectName(QStringLiteral("inspectorTabs"));
    QHBoxLayout* tabLayout = new QHBoxLayout(m_tabBar);
    tabLayout->setContentsMargins(0, 0, 0, 0);
    tabLayout->setSpacing(6);
    tabLayout->addStretch(1);
    layout->addWidget(m_tabBar);

    QWidget* head = new QWidget(page);
    QHBoxLayout* headLayout = new QHBoxLayout(head);
    headLayout->setContentsMargins(0, 0, 0, 0);
    PDFLabel* contents = new PDFLabel(tr("Contents"), PDFThemeFonts::Role::BodySemiBold, PDFThemeColors::TextPrimary, head);
    headLayout->addWidget(contents, 1);
    m_contentsCaption = new PDFLabel(QString(), PDFThemeFonts::Role::CaptionRegular, PDFThemeColors::TextTertiary, head);
    m_contentsCaption->setObjectName(QStringLiteral("inspectorContentsCaption"));
    headLayout->addWidget(m_contentsCaption);
    layout->addWidget(head);

    QWidget* card = new QWidget(page);
    PDFWidgetStyleProperties::set(card, PDFWidgetStyleProperties::CARD);
    QVBoxLayout* cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(12, 12, 12, 12);
    m_contentsStack = new QStackedWidget(card);
    m_contentsView = new PDFAppObjectContentsView(m_contentsStack);
    m_contentsView->setObjectName(QStringLiteral("inspectorContents"));
    connect(m_contentsView.data(), &PDFAppObjectContentsView::copyRequested, this, &PDFAppToolGUIObjectInspector::onCopyRequested);
    m_contentsStack->addWidget(m_contentsView);
    m_imageView = new PDFAppObjectImageView(m_contentsStack);
    m_imageView->setObjectName(QStringLiteral("inspectorImagePreview"));
    m_contentsStack->addWidget(m_imageView);
    m_errorPage = new QWidget(m_contentsStack);
    QVBoxLayout* errorLayout = new QVBoxLayout(m_errorPage);
    errorLayout->setContentsMargins(0, 0, 0, 0);
    m_errorCallout = new PDFAppToolCallout(PDFAccentHue::Red, QStringLiteral("alert-circle"), tr("The stream cannot be decoded"), QString(), m_errorPage);
    m_errorCallout->setObjectName(QStringLiteral("inspectorDecodeError"));
    errorLayout->addWidget(m_errorCallout);
    errorLayout->addStretch(1);
    m_contentsStack->addWidget(m_errorPage);
    cardLayout->addWidget(m_contentsStack);
    layout->addWidget(card, 1);

    updateTabBar();
    updateContents();
    return page;
}

// ------------------------------------------------------------------ the jobs

PDFAppToolGUIObjectInspector::PrepareResult PDFAppToolGUIObjectInspector::prepareJob(quint64 generation, PDFDocumentPointer document, PDFCMSPointer cms, std::shared_ptr<PDFAppCancelFlag> cancel)
{
    // A thread of the pool: the shared immutable document, the shared immutable color management of the tab and
    // the flag; the inspector is made here and given to the GUI only when it is complete (its object streams and
    // its classification, FD-6, B158)
    PrepareResult result;
    result.generation = generation;
    if (!document)
    {
        return result;
    }

    std::shared_ptr<PDFObjectInspector> inspector = std::make_shared<PDFObjectInspector>(document);
    inspector->setCMS(std::move(cms));
    inspector->setObjectStreams(PDFObjectInspector::findObjectStreams(document.data(), cancel.get()));
    if (!PDFOperationControl::isOperationCanceled(cancel.get()) && inspector->prepare(cancel.get()))
    {
        result.inspector = std::move(inspector);
    }
    return result;
}

PDFAppToolGUIObjectInspector::ContentResult PDFAppToolGUIObjectInspector::contentJob(quint64 generation, quint64 tabId, std::shared_ptr<const PDFObjectInspector> inspector,
                                                                                     PDFObjectReference reference, bool isImageNode, std::shared_ptr<PDFAppCancelFlag> cancel)
{
    // A thread of the pool (B165: never the GUI thread): the decoded data at most INSPECTOR_MAXIMUM_DECODED_SIZE
    // (Q7.18), the first MAXIMUM_SHOWN_BYTES shown as a text or a hex dump (Q7.12)
    ContentResult result;
    result.generation = generation;
    result.tabId = tabId;
    if (!inspector)
    {
        return result;
    }

    if (isImageNode)
    {
        QString errorMessage;
        result.image = inspector->getImage(reference, cancel.get(), &errorMessage);
        if (!result.image.isNull())
        {
            result.kind = ContentKind::Image;
            return result;
        }
        result.errorText = errorMessage;
        result.kind = ContentKind::Error;
        return result;
    }

    const PDFObjectInspector::DecodedStream decoded = inspector->decodeStream(reference, PDFObjectInspector::INSPECTOR_MAXIMUM_DECODED_SIZE, cancel.get());
    result.decodedSize = decoded.decodedSize;
    result.isTruncated = decoded.isTruncated;
    switch (decoded.status)
    {
        case PDFLimitedStreamDecoder::Status::Decoded:
            result.kind = decoded.isBinary ? ContentKind::Hex : ContentKind::Text;
            result.text = decoded.isBinary ? PDFObjectInspector::createHexDump(decoded.data) : PDFObjectInspector::toText(decoded.data);
            break;

        default:
            result.kind = ContentKind::Error;
            result.errorText = decoded.errorMessage;
            break;
    }
    return result;
}

void PDFAppToolGUIObjectInspector::cancelPreparation()
{
    if (m_prepareCancel)
    {
        m_prepareCancel->cancel();
        m_prepareCancel.reset();
    }
    if (m_prepareWatcher)
    {
        // The late result is dropped: the watcher goes, the job ends by itself
        m_prepareWatcher->disconnect(this);
        m_prepareWatcher->deleteLater();
        m_prepareWatcher = nullptr;
    }
}

void PDFAppToolGUIObjectInspector::startPreparation()
{
    cancelPreparation();
    const PDFAppDocument* document = getDocument();
    if (!document || !document->getDocument())
    {
        return;
    }

    // The pictures of images in the colors of the tab (its color management, its output intents), never with the
    // soft proof of the tab (Q7.10: the proof is only the view of the tab); a shared immutable copy for the thread
    // (F12), null = the generic CMS
    const PDFAppToolHost* host = getHost();
    const PDFAppDocumentView* view = host ? host->getView() : nullptr;
    const PDFCMSManager* cmsManager = view ? view->getCMSManager() : nullptr;
    PDFCMSPointer cms = cmsManager ? PDFAppDocumentSoftProof::createCMSWithoutProofing(cmsManager, document->getDocument().data()) : PDFCMSPointer();

    // One asynchronous job (CLAUDE.md: QtConcurrent for one job); a new version replaces the old job
    m_prepareCancel = std::make_shared<PDFAppCancelFlag>();
    m_prepareWatcher = new QFutureWatcher<PrepareResult>(this);
    connect(m_prepareWatcher.data(), &QFutureWatcherBase::finished, this, &PDFAppToolGUIObjectInspector::onPreparationFinished);
    m_prepareWatcher->setFuture(QtConcurrent::run(&PDFAppToolGUIObjectInspector::prepareJob, ++m_prepareGeneration, document->getDocument(), std::move(cms), m_prepareCancel));
}

void PDFAppToolGUIObjectInspector::onPreparationFinished()
{
    QFutureWatcher<PrepareResult>* watcher = m_prepareWatcher.data();
    if (!watcher || sender() != watcher)
    {
        return;
    }

    const PrepareResult result = watcher->result();
    m_prepareWatcher = nullptr;
    watcher->deleteLater();
    m_prepareCancel.reset();
    if (result.generation != m_prepareGeneration || !result.inspector)
    {
        return;
    }

    // The new tree chooses its row (onCurrentIndexChanged says the status, whose slots may retire this GUI)
    m_inspector = result.inspector;
    const PDFObjectReference selection = m_pendingSelection;
    m_pendingSelection = PDFObjectReference();
    rebuildTree(selection);
    if (!PDFDeferredDeletion::isRetired(this))
    {
        updateAll();
    }
}

void PDFAppToolGUIObjectInspector::cancelContentJob()
{
    if (m_contentCancel)
    {
        m_contentCancel->cancel();
        m_contentCancel.reset();
    }
    if (m_contentWatcher)
    {
        m_contentWatcher->disconnect(this);
        m_contentWatcher->deleteLater();
        m_contentWatcher = nullptr;
    }
    m_contentTabId = 0;
}

void PDFAppToolGUIObjectInspector::startContentJob()
{
    cancelContentJob();
    if (m_activeTab < 0 || m_activeTab >= int(m_tabs.size()))
    {
        return;
    }

    // A pinned tab decodes from its own version (also while the inspector of a new version is prepared), the tab
    // Current Object from the version of now (RS7APP L6)
    Tab& tab = m_tabs[m_activeTab];
    const std::shared_ptr<const PDFObjectInspector> inspector = getTabInspector(tab);
    if (tab.kind != ContentKind::Loading || !inspector)
    {
        return;
    }

    m_contentCancel = std::make_shared<PDFAppCancelFlag>();
    m_contentTabId = tab.id;
    m_contentWatcher = new QFutureWatcher<ContentResult>(this);
    connect(m_contentWatcher.data(), &QFutureWatcherBase::finished, this, &PDFAppToolGUIObjectInspector::onContentJobFinished);
    m_contentWatcher->setFuture(QtConcurrent::run(&PDFAppToolGUIObjectInspector::contentJob, ++m_contentGeneration, tab.id, inspector, tab.node.reference, isImage(tab.node), m_contentCancel));
}

std::shared_ptr<const PDFObjectInspector> PDFAppToolGUIObjectInspector::getTabInspector(const Tab& tab) const
{
    return tab.isPinned ? tab.inspector : m_inspector;
}

void PDFAppToolGUIObjectInspector::onContentJobFinished()
{
    QFutureWatcher<ContentResult>* watcher = m_contentWatcher.data();
    if (!watcher || sender() != watcher)
    {
        return;
    }

    const ContentResult result = watcher->result();
    m_contentWatcher = nullptr;
    watcher->deleteLater();
    m_contentCancel.reset();
    m_contentTabId = 0;
    if (result.generation != m_contentGeneration)
    {
        return;
    }

    if (Tab* tab = findTab(result.tabId))
    {
        tab->kind = result.kind;
        tab->text = result.text;
        tab->image = result.image;
        tab->errorText = result.errorText;
        tab->isTruncated = result.isTruncated;
        tab->decodedSize = result.decodedSize;
    }
    updateAll();
}

// ------------------------------------------------------------------ the tree and the tabs

PDFAppToolGUIObjectInspector::Tab* PDFAppToolGUIObjectInspector::findTab(quint64 id)
{
    for (Tab& tab : m_tabs)
    {
        if (tab.id == id)
        {
            return &tab;
        }
    }
    return nullptr;
}

void PDFAppToolGUIObjectInspector::rebuildTree(PDFObjectReference selection)
{
    if (!m_model)
    {
        return;
    }

    // The signals of the model may retire this GUI (the callers check it too)
    m_model->setInspector(m_inspector, getView());
    if (PDFDeferredDeletion::isRetired(this) || !m_model)
    {
        return;
    }

    // The selection by its reference (a root of the view, or a loaded row), else the first root
    QModelIndex index = m_model->findReference(selection);
    if (!index.isValid())
    {
        index = m_model->index(0, 0, QModelIndex());
    }
    if (m_treeView && index.isValid())
    {
        // The restored selection keeps the active tab (a shown pinned tab stays shown, its job goes on - RS7APP L6)
        m_isRestoringSelection = true;
        m_treeView->setCurrentIndex(index);
        if (PDFDeferredDeletion::isRetired(this))
        {
            return;
        }
        m_isRestoringSelection = false;
    }
    else if (!index.isValid())
    {
        Tab& current = m_tabs.front();
        current.hasNode = false;
        current.node = PDFObjectInspectorNode();
        loadTab(current);
    }
}

void PDFAppToolGUIObjectInspector::loadTab(Tab& tab)
{
    // The syntax of an object at once (bounded by MAXIMUM_SHOWN_BYTES), the data of a stream and the picture of
    // an image in the job
    tab.text.clear();
    tab.image = QImage();
    tab.errorText.clear();
    tab.isTruncated = false;
    tab.decodedSize = 0;
    tab.inspector = tab.hasNode ? m_inspector : nullptr;
    if (!tab.hasNode || !m_inspector)
    {
        tab.kind = ContentKind::Syntax;
        return;
    }

    // A repeated reference shows its object too ("Selecting it again elsewhere shows the object on the right")
    if (tab.node.object.isStream() && tab.node.reference.isValid())
    {
        tab.kind = ContentKind::Loading;
        return;
    }

    tab.kind = ContentKind::Syntax;
    tab.text = m_inspector->getSyntax(tab.node, &tab.isTruncated);
}

void PDFAppToolGUIObjectInspector::selectIndex(const QModelIndex& index)
{
    if (m_treeView && index.isValid())
    {
        // The slot of the selection updates the tabs
        m_treeView->setCurrentIndex(index);
    }
}

void PDFAppToolGUIObjectInspector::onCurrentIndexChanged(const QModelIndex& current, const QModelIndex& previous)
{
    Q_UNUSED(previous);

    // The tab Current Object follows the selection of the tree (482:14847); a pinned tab stays
    const PDFObjectInspectorNode* node = m_model ? m_model->getNode(current) : nullptr;
    Tab& tab = m_tabs.front();
    tab.hasNode = node != nullptr;
    tab.node = node ? *node : PDFObjectInspectorNode();
    loadTab(tab);
    if (!m_isRestoringSelection)
    {
        // A choice of the user shows Current Object; the selection restored after a change keeps the shown tab
        m_activeTab = 0;

        // A selected object is a use of the tool (owner 2026-11-20, isInteractive), also the first one (RESCCODE
        // L9). The selection model selects before it announces the current index; the current index, which the
        // tree sets on its focus without a selection (QAbstractItemView::focusInEvent, NoUpdate), is no choice
        const QItemSelectionModel* selectionModel = m_treeView ? m_treeView->selectionModel() : nullptr;
        if (current.isValid() && selectionModel && selectionModel->isSelected(current))
        {
            markUsed();
        }
    }
    if (m_activeTab == 0 && tab.kind == ContentKind::Loading && m_contentTabId != tab.id)
    {
        startContentJob();
    }

    // The signal comes from inside of QItemSelectionModel, which goes on after it: the parts (whose signals may
    // delete the tree with the model of the selection) are updated from the event loop
    if (!m_isUpdateQueued)
    {
        m_isUpdateQueued = true;
        QMetaObject::invokeMethod(this, &PDFAppToolGUIObjectInspector::onQueuedUpdate, Qt::QueuedConnection);
    }
}

void PDFAppToolGUIObjectInspector::onQueuedUpdate()
{
    m_isUpdateQueued = false;
    updateAll();
}

void PDFAppToolGUIObjectInspector::setActiveTab(int index)
{
    if (index < 0 || index >= int(m_tabs.size()) || index == m_activeTab)
    {
        return;
    }
    m_activeTab = index;
    updateAll();
}

bool PDFAppToolGUIObjectInspector::canPin() const
{
    // 482:15989: Pin only on the tab Current Object with an object, Disabled on a pinned tab
    return m_activeTab == 0 && !m_tabs.empty() && m_tabs.front().hasNode && int(m_tabs.size()) <= MAXIMUM_PINNED;
}

bool PDFAppToolGUIObjectInspector::canUnpin() const
{
    return m_activeTab > 0 && m_activeTab < int(m_tabs.size());
}

bool PDFAppToolGUIObjectInspector::pin()
{
    if (!canPin())
    {
        return false;
    }

    // A snapshot of the node (and of its loaded contents); the tab Current Object stays active (482:15778)
    Tab pinned = m_tabs.front();
    pinned.id = m_nextTabId++;
    pinned.isPinned = true;
    // A pinned copy of a tab, which is still decoded, loads its own contents when it is shown
    m_tabs.push_back(std::move(pinned));
    updateAll();
    return true;
}

bool PDFAppToolGUIObjectInspector::unpin()
{
    if (!canUnpin())
    {
        return false;
    }

    if (m_contentTabId == m_tabs[m_activeTab].id)
    {
        cancelContentJob();
    }
    m_tabs.erase(m_tabs.begin() + m_activeTab);
    m_activeTab = qMin(m_activeTab, int(m_tabs.size()) - 1);
    updateAll();
    return true;
}

void PDFAppToolGUIObjectInspector::copyContents()
{
    if (m_contentsView)
    {
        m_contentsView->requestCopy();
    }
}

// ------------------------------------------------------------------ the updates

void PDFAppToolGUIObjectInspector::updateAll()
{
    // The job of the contents of the active tab, when it needs one
    if (m_activeTab >= 0 && m_activeTab < int(m_tabs.size()))
    {
        const Tab& tab = m_tabs[m_activeTab];
        if (tab.kind == ContentKind::Loading && m_contentTabId != tab.id)
        {
            startContentJob();
        }
    }

    updateViewMenu();
    updateButtons();
    updateTabBar();
    if (!updateContents())
    {
        return;
    }
    updateRightPanel();

    // The slots of the status may delete this GUI: the status is the last thing
    updateStatusText();
}

void PDFAppToolGUIObjectInspector::updateViewMenu()
{
    // 482:15260: the rows with their counts; a view the document does not contain is not listed (the views of
    // the classes); the chosen view checked, the button with its icon and title
    const PDFObjectInspectorView view = getView();
    for (const auto& item : m_viewRows)
    {
        PDFTileRow* row = item.second.data();
        if (!row)
        {
            continue;
        }

        const qint64 count = m_inspector ? m_inspector->getRootCount(item.first) : -1;
        QString description;
        if (item.first == PDFObjectInspectorView::Document)
        {
            description = tr("Everything, from the trailer");
        }
        else if (item.first == PDFObjectInspectorView::Objects)
        {
            description = count >= 0 ? tr("All %1 objects by number").arg(PDFAppLocale::formatNumber(count)) : tr("All objects by number");
        }
        else if (count >= 0)
        {
            description = tr("%Ln objects", nullptr, int(qMin<qint64>(count, (std::numeric_limits<int>::max)())));
        }
        row->setDescription(description);
        row->setVisible(item.first == PDFObjectInspectorView::Document || item.first == PDFObjectInspectorView::Objects || count != 0);
        const QSignalBlocker blocker(row);
        row->setChecked(item.first == view);
    }

    if (QToolButton* button = getViewButton())
    {
        button->setText(getViewTitle(view));
        button->setIcon(PDFThemeIcons::getIcon(getViewIconName(view)));
    }

    if (m_objectsHeader)
    {
        const QString subtitle = !m_inspector ? tr("Reading the objects…") : getViewSubtitle(view, m_model ? m_model->getRootCount() : 0);
        m_objectsHeader->setTexts(tr("Objects"), subtitle);
    }
    if (m_treeNote)
    {
        // 482:15229, 482:16200
        m_treeNote->setText(view == PDFObjectInspectorView::Objects ? tr("References are not followed in the object list – every object is listed once by its number.")
                                                                    : tr("A reference is opened only once in the tree. Selecting it again elsewhere shows the object on the right."));
    }
}

void PDFAppToolGUIObjectInspector::updateButtons()
{
    if (m_pinButton)
    {
        m_pinButton->setEnabled(canPin());
    }
    if (m_unpinButton)
    {
        m_unpinButton->setEnabled(canUnpin());
    }
}

void PDFAppToolGUIObjectInspector::updateTabBar()
{
    using Helper = PDFAppToolGUIObjectInspectorHelper;

    if (!m_tabBar)
    {
        return;
    }

    // 482:15778: Current Object and the pinned tabs as chips, the active one checked. The old buttons go later:
    // the update may run in the slot of a click of one of them (never delete the sender synchronously)
    QHBoxLayout* layout = qobject_cast<QHBoxLayout*>(m_tabBar->layout());
    for (const QPointer<QToolButton>& button : m_tabButtons)
    {
        if (button)
        {
            button->disconnect(this);
            button->hide();
            if (layout)
            {
                layout->removeWidget(button);
            }
            button->deleteLater();
        }
    }
    m_tabButtons.clear();

    for (int i = 0; i < int(m_tabs.size()); ++i)
    {
        QToolButton* button = new QToolButton(m_tabBar);
        PDFWidgetStyleProperties::set(button, PDFWidgetStyleProperties::CHIP);
        button->setObjectName(QStringLiteral("inspectorTab"));
        button->setCheckable(true);
        {
            const QSignalBlocker blocker(button);
            button->setChecked(i == m_activeTab);
        }
        button->setText(i == 0 ? tr("Current Object") : getTabTitle(m_tabs[i].node));
        button->setProperty(Helper::TAB_PROPERTY, i);
        connect(button, &QToolButton::clicked, this, &PDFAppToolGUIObjectInspector::onTabButtonClicked);
        if (layout)
        {
            layout->insertWidget(i, button);
        }
        m_tabButtons.push_back(button);
    }
}

bool PDFAppToolGUIObjectInspector::updateContents()
{
    if (!m_contentsStack || m_activeTab < 0 || m_activeTab >= int(m_tabs.size()))
    {
        return true;
    }

    // 482:14847 "PDF syntax of the object", 482:15358 "Decoded stream data", 482:15559 "Image preview",
    // 482:16403 the error of a filter. The parts without a signal first; the text and the page of the stack emit
    // (a slot of any signal may retire this GUI): copies of the values, then each followed by the check of the retired state
    const Tab tab = m_tabs[m_activeTab];
    QString caption = tr("PDF syntax of the object");
    QString text;
    QWidget* page = m_contentsView.data();
    switch (tab.kind)
    {
        case ContentKind::Syntax:
            text = tab.isTruncated ? tab.text + QStringLiteral("\n…") : tab.text;
            break;

        case ContentKind::Loading:
            caption = tr("Decoded stream data");
            text = tr("Decoding…");
            break;

        case ContentKind::Text:
        case ContentKind::Hex:
            caption = tab.isTruncated ? tr("Decoded stream data · first %1 shown").arg(PDFAppLocale::formatDataSize(PDFObjectInspector::MAXIMUM_SHOWN_BYTES, 0, QLocale::DataSizeTraditionalFormat))
                                      : tr("Decoded stream data");
            text = tab.text;
            break;

        case ContentKind::Image:
            caption = tr("Image preview");
            m_imageView->setImage(tab.image);
            page = m_imageView.data();
            break;

        case ContentKind::Error:
            caption = isImage(tab.node) ? tr("Image preview") : tr("Decoded stream data");
            m_errorCallout->setTexts(isImage(tab.node) ? tr("The image cannot be shown") : tr("The stream cannot be decoded"), tab.errorText);
            page = m_errorPage.data();
            break;
    }
    m_contentsCaption->setText(caption);
    m_contentsView->setCopyGuarded(tab.kind != ContentKind::Syntax);
    m_contentsView->setFont(tab.kind == ContentKind::Hex ? PDFThemeFonts::getMonospaceFont(QFont::Normal, 12) : PDFThemeFonts::getFont(PDFThemeFonts::Role::Small));

    if (page == m_contentsView.data() && m_contentsView->toPlainText() != text)
    {
        m_contentsView->setPlainText(text);
        if (PDFDeferredDeletion::isRetired(this) || !m_contentsStack)
        {
            return false;
        }
    }
    m_contentsStack->setCurrentWidget(page);
    return !PDFDeferredDeletion::isRetired(this);
}

void PDFAppToolGUIObjectInspector::updateRightPanel()
{
    using Helper = PDFAppToolGUIObjectInspectorHelper;

    const Tab* tab = (m_activeTab >= 0 && m_activeTab < int(m_tabs.size())) ? &m_tabs[m_activeTab] : nullptr;
    const bool hasNode = tab && tab->hasNode;
    if (m_informationRows)
    {
        // 482:15230: Reference, Type
        m_informationRows->clear();
        if (hasNode)
        {
            m_informationRows->addRow(tr("Reference"), getReferenceTitle(tab->node));
            m_informationRows->addRow(tr("Type"), getTypeTitle(tab->node.object));
            if (!tab->node.objectStreamNote.isEmpty())
            {
                m_informationRows->addRow(tr("Stored"), tab->node.objectStreamNote);
            }
        }
    }
    // A pinned tab is described by its own version (RS7APP L6)
    const PDFObjectInspector* inspector = tab ? getTabInspector(*tab).get() : nullptr;
    if (m_description)
    {
        const PDFAppToolValueFormatter formatter;
        m_description->setText(hasNode && inspector ? inspector->describe(tab->node, &formatter) : tr("Choose an object in the tree."));
    }

    // 482:15559: Image (Size, Bits per component, Color space, Filter) of an image
    const bool isImageNode = hasNode && isImage(tab->node);
    if (m_imageRows)
    {
        m_imageRows->clear();
        if (isImageNode)
        {
            const PDFDocument* document = inspector ? inspector->getDocument().data() : nullptr;
            const PDFDictionary* dictionary = tab->node.object.getStream()->getDictionary();
            const PDFObject& width = document ? document->getObject(dictionary->get("Width")) : dictionary->get("Width");
            const PDFObject& height = document ? document->getObject(dictionary->get("Height")) : dictionary->get("Height");
            if (width.isInt() && height.isInt())
            {
                m_imageRows->addRow(tr("Size"), tr("%1 × %2 pixels").arg(PDFAppLocale::formatNumber(width.getInteger()), PDFAppLocale::formatNumber(height.getInteger())));
            }
            m_imageRows->addRow(tr("Bits per component"), Helper::getValueText(document, dictionary->get("BitsPerComponent")));
            m_imageRows->addRow(tr("Color space"), Helper::getValueText(document, dictionary->get("ColorSpace")));
            m_imageRows->addRow(tr("Filter"), Helper::getValueText(document, dictionary->get("Filter")));
        }
        // The card of the rows goes with them: an empty card is no part of 482:14847
        m_imageRows->setVisible(isImageNode);
        if (QWidget* card = m_imageRows->parentWidget())
        {
            card->setVisible(isImageNode);
        }
    }
    if (m_imageHeader)
    {
        m_imageHeader->setVisible(isImageNode);
    }

    if (m_tabNote)
    {
        // 482:15245, 482:15989
        m_tabNote->setText(tab && tab->isPinned ? tr("A pinned tab is a snapshot and does not follow the tree. Unpin closes it.")
                                                : tr("Pin keeps this object in its own tab, so you can compare it with another one. The Current Object tab follows the selection in the tree."));
    }
}

QString PDFAppToolGUIObjectInspector::getStatusText() const
{
    // 482:14847 "1 0 R · Dictionary [6 items]", 482:15778 "14 0 R · two objects are pinned.", 482:15989
    // "Pinned: part of object 12 0 R.", 482:16403 "The stream of 14 0 R cannot be decoded."
    const PDFDocumentToolShell* shell = getShell();
    if (shell && shell->isPermissionRefused())
    {
        return BaseClass::getStatusText();
    }
    if (!m_inspector)
    {
        return tr("Reading the objects of the document…");
    }

    const Tab* tab = (m_activeTab >= 0 && m_activeTab < int(m_tabs.size())) ? &m_tabs[m_activeTab] : nullptr;
    if (!tab || !tab->hasNode)
    {
        return tr("Choose an object in the tree.");
    }

    const QString reference = getReferenceTitle(tab->node);
    if (tab->kind == ContentKind::Error)
    {
        return isImage(tab->node) ? tr("The image of %1 cannot be shown.").arg(reference) : tr("The stream of %1 cannot be decoded.").arg(reference);
    }
    if (tab->isPinned)
    {
        return tab->node.reference.isValid() ? tr("Pinned: object %1.").arg(reference) : tr("Pinned: part of object %1.").arg(PDFObjectInspector::getReferenceText(tab->node.container));
    }

    const int pinnedCount = int(m_tabs.size()) - 1;
    if (pinnedCount > 0)
    {
        return tr("%1 · %Ln pinned", nullptr, pinnedCount).arg(reference);
    }

    const PDFAppToolValueFormatter formatter;
    return tr("%1 · %2").arg(reference, m_inspector->describe(tab->node, &formatter));
}

// ------------------------------------------------------------------ the events

void PDFAppToolGUIObjectInspector::onParametersChanged(const QString& name)
{
    using Helper = PDFAppToolGUIObjectInspectorHelper;
    if (name == Helper::name(Helper::Tool::PARAMETER_VIEW))
    {
        // Another view: a new tree with its own set of the opened references (its new row says the status)
        if (m_viewMenu)
        {
            m_viewMenu->close();
        }
        rebuildTree(PDFObjectReference());
    }
    if (!PDFDeferredDeletion::isRetired(this))
    {
        updateAll();
    }
}

void PDFAppToolGUIObjectInspector::onDocumentChanged()
{
    // A new version: the inspector is prepared again; the selection is restored by its reference, the pinned
    // tabs stay (snapshots)
    const Tab& current = m_tabs.front();
    m_pendingSelection = current.hasNode ? (current.node.reference.isValid() ? current.node.reference : current.node.container) : PDFObjectReference();
    if (m_contentTabId == current.id)
    {
        // The job of Current Object (a job of a pinned tab goes on with its own version, RS7APP L6)
        cancelContentJob();
    }
    m_inspector.reset();
    if (m_model)
    {
        // The reset of the model may change the current row of the tree (its slot says the status)
        m_model->setInspector(nullptr, getView());
    }
    if (PDFDeferredDeletion::isRetired(this))
    {
        return;
    }
    startPreparation();
    updateAll();
}

void PDFAppToolGUIObjectInspector::onViewRowClicked()
{
    QObject* row = sender();
    if (!row)
    {
        return;
    }

    // The slots of the model run: the last statement
    setView(static_cast<PDFObjectInspectorView>(row->property(PDFAppToolGUIObjectInspectorHelper::VIEW_PROPERTY).toInt()));
}

void PDFAppToolGUIObjectInspector::onTabButtonClicked()
{
    QObject* button = sender();
    if (button)
    {
        setActiveTab(button->property(PDFAppToolGUIObjectInspectorHelper::TAB_PROPERTY).toInt());
    }
}

void PDFAppToolGUIObjectInspector::onPinClicked()
{
    markUsed();
    pin();
}

void PDFAppToolGUIObjectInspector::onUnpinClicked()
{
    markUsed();
    unpin();
}

void PDFAppToolGUIObjectInspector::onCopyRequested()
{
    // R7-22 (6), Q7.2: the data of a stream are content of the document - bit 5 (CopyContent) of Table 22; not
    // allowed: the owner password dialog (or the refusal), the user copies again after the unlock
    PDFAppDocument* document = getDocument();
    QPointer<PDFAppObjectContentsView> view = m_contentsView;
    if (!document || !view)
    {
        return;
    }

    if (PDFAppDocumentPermissions::request(document, PDFAppPermissionRequest::create(PDFAppPermissionAction::CopyContent), view->window()) && !PDFDeferredDeletion::isRetired(view))
    {
        view->copySelection();
    }
}

}   // namespace pdf

#include "moc_pdfapptoolgui_objectinspector.cpp"
