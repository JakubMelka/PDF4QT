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

#include "pdfappeditcontentview.h"

#include "pdfappcommand.h"
#include "pdfappcontext.h"
#include "pdfappeditcontentcontext.h"
#include "pdfappeditcontentregistry.h"
#include "pdfappeditcontentscene.h"
#include "pdfappeditcontentselection.h"
#include "pdfappeditcontenttools.h"
#include "pdfapplocale.h"
#include "pdfcatalog.h"
#include "pdfdocumenttoolgui.h"
#include "pdfshortcutrepository.h"
#include "pdfstylepainter.h"
#include "pdfthemecolors.h"
#include "pdfthemefonts.h"

#include <QContextMenuEvent>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFocusEvent>
#include <QGridLayout>
#include <QKeyEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QScrollBar>
#include <QStyle>
#include <QTimer>
#include <QWheelEvent>
#include <QtGlobal>

#include <cmath>

namespace pdf
{

// Stage E1, package P11: the view of the canvas (zadani_gui.md 4.15, 4.16, 4.20, 4.23) and the canvas page around it.
// The view forwards its events to the tool controller of the context; it keeps the zoom and the center of every page
// shown until the tool closes. The scene coordinates are the page space (y up): the view flips them and turns them by
// /Rotate.

/// Helpers of the view (static functions only)
class PDFAppEditContentViewHelper
{
public:
    PDFAppEditContentViewHelper() = delete;

    /// The registry of the commands of the application with its default shortcuts (a view without an application
    /// context: a test); built once in the GUI thread
    static const PDFAppCommandRegistry* getDefaultCommandRegistry()
    {
        static std::unique_ptr<PDFAppCommandRegistry> registry;
        if (!registry)
        {
            registry = std::make_unique<PDFAppCommandRegistry>();
            PDFAppCommands::registerBuiltinCommands(registry.get());
        }
        return registry.get();
    }

    /// The key sequence of a key event (no modifier of the keypad)
    static QKeySequence getSequence(const QKeyEvent* event)
    {
        return QKeySequence(QKeyCombination(event->modifiers() & ~Qt::KeypadModifier, Qt::Key(event->key())));
    }

    /// A modifier key alone
    static bool isModifierKey(int key)
    {
        return key == Qt::Key_Shift || key == Qt::Key_Control || key == Qt::Key_Alt || key == Qt::Key_Meta;
    }

    /// The angle of /Rotate in degrees (clockwise on the screen)
    static qreal getAngle(PageRotation rotation)
    {
        switch (rotation)
        {
            case PageRotation::Rotate90:  return 90.0;
            case PageRotation::Rotate180: return 180.0;
            case PageRotation::Rotate270: return 270.0;
            case PageRotation::None:
                break;
        }
        return 0.0;
    }
};

/// The label of the mode at the top left of the canvas (zadani_gui.md 4.15): a small pill with the text
class PDFAppEditContentModeLabel : public QWidget
{
public:
    explicit PDFAppEditContentModeLabel(QWidget* parent) :
        QWidget(parent)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents, true);
        setFocusPolicy(Qt::NoFocus);
        hide();
    }

    void setText(const QString& text)
    {
        if (m_text == text)
        {
            return;
        }
        m_text = text;
        setAccessibleName(text);
        setVisible(!text.isEmpty());
        updateGeometry();
        adjustSize();
        update();
    }

    const QString& getText() const { return m_text; }

    virtual QSize sizeHint() const override
    {
        const QFontMetrics metrics(PDFThemeFonts::getFont(PDFThemeFonts::Role::SmallMedium));
        return QSize(metrics.horizontalAdvance(m_text) + 20, metrics.height() + 8);
    }

protected:
    virtual void paintEvent(QPaintEvent* event) override
    {
        Q_UNUSED(event);
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        const QRectF area = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        PDFStylePainter::fill(&painter, area, PDFStylePainter::Radii(area.height() * 0.5), PDFThemeColors::BgSurface);
        PDFStylePainter::strokeInside(&painter, QRectF(rect()), PDFStylePainter::Radii(height() * 0.5), PDFThemeColors::BorderDefault, 1.0);
        PDFStylePainter::text(&painter, QRectF(rect()), Qt::AlignCenter, m_text, PDFThemeFonts::getFont(PDFThemeFonts::Role::SmallMedium), PDFStylePainter::color(PDFThemeColors::TextPrimary));
    }

private:
    QString m_text;
};

// ---------------------------------------------------------------- PDFAppEditContentView

PDFAppEditContentView::PDFAppEditContentView(PDFAppEditContentContext* context, QWidget* parent) :
    BaseClass(parent),
    PDFRetirable(this),
    m_context(context)
{
    setObjectName(QStringLiteral("editContentView"));
    setFocusPolicy(Qt::StrongFocus);
    setAcceptDrops(true);
    setDragMode(QGraphicsView::NoDrag);
    setTransformationAnchor(QGraphicsView::NoAnchor);
    setResizeAnchor(QGraphicsView::AnchorViewCenter);
    setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
    setViewportUpdateMode(QGraphicsView::SmartViewportUpdate);
    setFrameShape(QFrame::NoFrame);
    viewport()->setMouseTracking(true);
    setAccessibleName(tr("Page canvas"));
    setAccessibleDescription(tr("The objects of the page; Tab selects the next object"));

    m_scene = new PDFAppEditContentScene(context, this);
    setScene(m_scene);

    // Preview renders again 150 ms after the last change of its input (zadani_gui.md 4.15)
    m_previewTimer = new QTimer(this);
    m_previewTimer->setSingleShot(true);
    m_previewTimer->setInterval(PDFAppEditContentPreview::RENDER_DELAY);
    connect(m_previewTimer, &QTimer::timeout, this, &PDFAppEditContentView::requestPreview);

    if (context)
    {
        PDFDeferredDeletion::connect(context, &PDFAppEditContentContext::pageChanged, this, &PDFAppEditContentView::onPageChanged);
        PDFDeferredDeletion::connect(context, &PDFAppEditContentContext::containerChanged, this, &PDFAppEditContentView::onContainerChanged);
        PDFDeferredDeletion::connect(context, &PDFAppEditContentContext::previewActiveChanged, this, &PDFAppEditContentView::onPreviewActiveChanged);
        PDFDeferredDeletion::connect(context->getPreview(), &PDFAppEditContentPreview::imageChanged, this, &PDFAppEditContentView::onPreviewImageChanged);
        PDFDeferredDeletion::connect(context->getOptions(), &PDFAppEditContentOptions::changed, this, &PDFAppEditContentView::onOptionChanged);
        PDFDeferredDeletion::connect(context->getToolController(), &PDFAppEditContentToolController::stateChanged, this, &PDFAppEditContentView::onToolStateChanged);
        m_shownPage = context->getPageIndex();
    }

    applyTransform();
}

PDFAppEditContentView::~PDFAppEditContentView() = default;

PDFAppEditContentScene* PDFAppEditContentView::getContentScene() const
{
    return m_scene;
}

qreal PDFAppEditContentView::getDeviceScale() const
{
    // 1.0 = 100 %: one point of the page is 1/72 inch of the screen
    return qMax(1, logicalDpiX()) / 72.0;
}

PageRotation PDFAppEditContentView::getRotation() const
{
    const PDFDocument* document = m_context ? m_context->getSession()->getDocument() : nullptr;
    const PDFInteger pageIndex = m_context ? m_context->getPageIndex() : -1;
    if (document && pageIndex >= 0 && pageIndex < PDFInteger(document->getCatalog()->getPageCount()))
    {
        if (const PDFPage* page = document->getCatalog()->getPage(pageIndex))
        {
            return page->getPageRotation();
        }
    }
    return PageRotation::None;
}

void PDFAppEditContentView::applyTransform()
{
    // The page space (y up) -> the screen: the scale of the zoom with y flipped, then /Rotate clockwise
    const qreal scale = m_zoom * getDeviceScale();
    QTransform rotation;
    rotation.rotate(PDFAppEditContentViewHelper::getAngle(getRotation()));
    setTransform(QTransform::fromScale(scale, -scale) * rotation);
    if (m_scene)
    {
        m_scene->getSelectionItem()->updateGeometry();
    }
}

qreal PDFAppEditContentView::getZoom() const
{
    return m_zoom;
}

void PDFAppEditContentView::setZoom(qreal zoom)
{
    const qreal bounded = qBound(MINIMUM_ZOOM, zoom, MAXIMUM_ZOOM);
    if (!std::isfinite(zoom))
    {
        return;
    }

    const QPointF center = getVisibleCenter();
    m_zoom = bounded;
    applyTransform();
    centerOn(center);
    Q_EMIT viewChanged();
}

void PDFAppEditContentView::zoomIn()
{
    setZoom(m_zoom * ZOOM_STEP);
}

void PDFAppEditContentView::zoomOut()
{
    setZoom(m_zoom / ZOOM_STEP);
}

void PDFAppEditContentView::zoomActualSize()
{
    setZoom(1.0);
}

void PDFAppEditContentView::fit(bool isWidth, bool isHeight)
{
    const QRectF paper = m_scene ? m_scene->getPaperRectangle() : QRectF();
    if (paper.isEmpty())
    {
        return;
    }

    // The paper as it is shown (/Rotate by 90 or 270 swaps its sides)
    const PageRotation rotation = getRotation();
    const bool isSwapped = rotation == PageRotation::Rotate90 || rotation == PageRotation::Rotate270;
    const qreal width = (isSwapped ? paper.height() : paper.width()) * getDeviceScale();
    const qreal height = (isSwapped ? paper.width() : paper.height()) * getDeviceScale();
    const qreal availableWidth = qMax(1, viewport()->width() - 2 * FIT_MARGIN);
    const qreal availableHeight = qMax(1, viewport()->height() - 2 * FIT_MARGIN);
    qreal zoom = m_zoom;
    if (isWidth && isHeight)
    {
        zoom = qMin(availableWidth / width, availableHeight / height);
    }
    else if (isWidth)
    {
        zoom = availableWidth / width;
    }
    else if (isHeight)
    {
        zoom = availableHeight / height;
    }

    m_zoom = qBound(MINIMUM_ZOOM, zoom, MAXIMUM_ZOOM);
    applyTransform();
    centerOn(paper.center());
    Q_EMIT viewChanged();
}

void PDFAppEditContentView::fitPage()
{
    fit(true, true);
}

void PDFAppEditContentView::fitWidth()
{
    fit(true, false);
}

void PDFAppEditContentView::fitHeight()
{
    fit(false, true);
}

qreal PDFAppEditContentView::getPixelSize() const
{
    return 1.0 / (m_zoom * getDeviceScale());
}

QPointF PDFAppEditContentView::getVisibleCenter() const
{
    return mapToScene(viewport()->rect().center());
}

void PDFAppEditContentView::ensureVisible(const QRectF& sceneRectangle)
{
    BaseClass::ensureVisible(sceneRectangle, FIT_MARGIN, FIT_MARGIN);
}

QString PDFAppEditContentView::getZoomText() const
{
    return tr("%1 %").arg(PDFAppLocale::formatNumber(qRound(m_zoom * 100.0)));
}

void PDFAppEditContentView::retire()
{
    // The grab of the mouse and the cursor go, the timer stops; nothing is emitted
    m_isPanning = false;
    m_previewTimer->stop();
    if (QWidget::mouseGrabber() == viewport() || QWidget::mouseGrabber() == this)
    {
        QWidget::mouseGrabber()->releaseMouse();
    }
    viewport()->unsetCursor();
}

void PDFAppEditContentView::loadCommandKeys()
{
    // The shortcuts of the commands of the scope Canvas, as the user has them (a database of the application), else the
    // defaults; read when the view gets the focus (the user may rebind them meanwhile in Settings)
    m_commandKeys.clear();
    m_isCommandKeysLoaded = true;

    const PDFDocumentToolGUI* gui = m_context ? m_context->getGUI() : nullptr;
    const PDFAppContext* appContext = gui ? gui->getContext() : nullptr;
    const PDFAppCommandRegistry* registry = appContext ? appContext->getCommandRegistry() : PDFAppEditContentViewHelper::getDefaultCommandRegistry();
    PDFAppCommandRegistry::UserShortcuts userShortcuts;
    if (appContext && appContext->getDatabase())
    {
        PDFDatabaseError error;
        userShortcuts = appContext->getShortcuts().getShortcuts(&error);
        if (error.isError())
        {
            userShortcuts.clear();
        }
    }

    for (const PDFAppCommand* command : registry->getCommands())
    {
        if (command->scope != PDFAppCommandScope::Canvas)
        {
            continue;
        }
        for (const QKeySequence& shortcut : registry->getShortcuts(command->id, userShortcuts))
        {
            if (!shortcut.isEmpty())
            {
                m_commandKeys.emplace_back(shortcut, command->id);
            }
        }
    }
}

QString PDFAppEditContentView::getCanvasCommand(const QKeyEvent* event) const
{
    if (!m_isCommandKeysLoaded)
    {
        const_cast<PDFAppEditContentView*>(this)->loadCommandKeys();
    }

    const QKeySequence sequence = PDFAppEditContentViewHelper::getSequence(event);
    for (const auto& item : m_commandKeys)
    {
        if (item.first == sequence)
        {
            return item.second;
        }
    }
    return QString();
}

bool PDFAppEditContentView::isCanvasKey(const QKeyEvent* event) const
{
    if (!m_context || PDFDeferredDeletion::isRetired(this))
    {
        return false;
    }

    // A command of the application the tool answers now (Ctrl+A, Delete, ...): its action lives on the hidden canvas
    // of the document and never fires here (pe1_skeleton_notes.md 8.2)
    const QString command = getCanvasCommand(event);
    if (!command.isEmpty() && m_context->getApplicationCommandState(command) == PDFAppToolCommandState::Enabled)
    {
        return true;
    }

    // A key of a tool, of a canvas action, of the tools (arrows, Enter, Delete, Backspace): no shortcut of the
    // application, which the user rebound to a bare key, takes it
    const Qt::KeyboardModifiers modifiers = event->modifiers() & ~Qt::KeypadModifier;
    if (modifiers & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))
    {
        return false;
    }

    const PDFAppEditContentRegistry* registry = m_context->getRegistry();
    const QKeySequence sequence = PDFAppEditContentViewHelper::getSequence(event);
    if ((modifiers == Qt::NoModifier && registry->findToolByKey(QKeySequence(event->key()))) || registry->findActionByKey(sequence, PDFAppEditContentKeyScope::Canvas))
    {
        return true;
    }

    switch (event->key())
    {
        case Qt::Key_Left:
        case Qt::Key_Right:
        case Qt::Key_Up:
        case Qt::Key_Down:
        case Qt::Key_Return:
        case Qt::Key_Enter:
        case Qt::Key_Delete:
        case Qt::Key_Backspace:
            return m_context->isEditingAllowed();

        default:
            break;
    }
    return false;
}

bool PDFAppEditContentView::event(QEvent* event)
{
    if (event->type() == QEvent::ShortcutOverride && isCanvasKey(static_cast<QKeyEvent*>(event)))
    {
        event->accept();
        return true;
    }
    return BaseClass::event(event);
}

void PDFAppEditContentView::keyPressEvent(QKeyEvent* event)
{
    PDFAppEditContentContext* context = m_context.data();
    if (!context || PDFDeferredDeletion::isRetired(this))
    {
        BaseClass::keyPressEvent(event);
        return;
    }

    if (PDFAppEditContentViewHelper::isModifierKey(event->key()))
    {
        context->getToolController()->modifiersChanged(event->modifiers());
        event->accept();
        return;
    }

    // A command of the application of the scope Canvas: handled from the event loop by the context
    const QString command = getCanvasCommand(event);
    if (!command.isEmpty() && context->handleApplicationCommand(command))
    {
        event->accept();
        return;
    }

    // The keys of the tools and of the canvas actions (the controller may close the tool: nothing after it)
    if (context->getToolController()->keyPress(event))
    {
        event->accept();
        return;
    }

    BaseClass::keyPressEvent(event);
}

void PDFAppEditContentView::keyReleaseEvent(QKeyEvent* event)
{
    if (m_context && PDFAppEditContentViewHelper::isModifierKey(event->key()))
    {
        m_context->getToolController()->modifiersChanged(event->modifiers());
        event->accept();
        return;
    }
    BaseClass::keyReleaseEvent(event);
}

void PDFAppEditContentView::mousePressEvent(QMouseEvent* event)
{
    setFocus(Qt::MouseFocusReason);

    // The middle button pans the view
    if (event->button() == Qt::MiddleButton)
    {
        m_isPanning = true;
        m_panPosition = event->position().toPoint();
        viewport()->setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }

    if (m_context)
    {
        const QPointF scenePoint = mapToScene(event->position().toPoint());
        QPointer<PDFAppEditContentView> guard(this);
        m_context->getToolController()->mousePress(event, scenePoint);
        if (guard && !PDFDeferredDeletion::isRetired(this))
        {
            updateCursor(scenePoint);
        }
    }
    event->accept();
}

void PDFAppEditContentView::mouseMoveEvent(QMouseEvent* event)
{
    if (m_isPanning)
    {
        const QPoint position = event->position().toPoint();
        const QPoint delta = position - m_panPosition;
        m_panPosition = position;
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - delta.x());
        verticalScrollBar()->setValue(verticalScrollBar()->value() - delta.y());
        event->accept();
        return;
    }

    if (m_context)
    {
        const QPointF scenePoint = mapToScene(event->position().toPoint());
        QPointer<PDFAppEditContentView> guard(this);
        m_context->getToolController()->mouseMove(event, scenePoint);
        if (guard && !PDFDeferredDeletion::isRetired(this))
        {
            updateCursor(scenePoint);
        }
    }
    event->accept();
}

void PDFAppEditContentView::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::MiddleButton && m_isPanning)
    {
        m_isPanning = false;
        viewport()->unsetCursor();
        event->accept();
        return;
    }

    if (m_context)
    {
        const QPointF scenePoint = mapToScene(event->position().toPoint());
        QPointer<PDFAppEditContentView> guard(this);
        m_context->getToolController()->mouseRelease(event, scenePoint);
        if (guard && !PDFDeferredDeletion::isRetired(this))
        {
            updateCursor(scenePoint);
        }
    }
    event->accept();
}

void PDFAppEditContentView::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (m_context && event->button() == Qt::LeftButton)
    {
        m_context->getToolController()->mouseDoubleClick(event, mapToScene(event->position().toPoint()));
    }
    event->accept();
}

void PDFAppEditContentView::wheelEvent(QWheelEvent* event)
{
    // Ctrl + wheel zooms around the pointer (the point of the page under it stays), the wheel alone scrolls
    if (event->modifiers().testFlag(Qt::ControlModifier))
    {
        const int delta = event->angleDelta().y();
        if (delta != 0)
        {
            const QPoint position = event->position().toPoint();
            const QPointF anchor = mapToScene(position);
            const qreal zoom = m_zoom * std::pow(ZOOM_STEP, qreal(delta) / qreal(QWheelEvent::DefaultDeltasPerStep));
            m_zoom = qBound(MINIMUM_ZOOM, zoom, MAXIMUM_ZOOM);
            applyTransform();

            // The anchor back under the pointer
            const QPointF moved = mapFromScene(anchor);
            horizontalScrollBar()->setValue(horizontalScrollBar()->value() + qRound(moved.x() - position.x()));
            verticalScrollBar()->setValue(verticalScrollBar()->value() + qRound(moved.y() - position.y()));
            Q_EMIT viewChanged();
        }
        event->accept();
        return;
    }
    BaseClass::wheelEvent(event);
}

void PDFAppEditContentView::focusInEvent(QFocusEvent* event)
{
    BaseClass::focusInEvent(event);
    loadCommandKeys();
    if (m_scene)
    {
        m_scene->getSelectionItem()->updateGeometry();
    }
}

void PDFAppEditContentView::focusOutEvent(QFocusEvent* event)
{
    BaseClass::focusOutEvent(event);
    if (m_scene)
    {
        m_scene->getSelectionItem()->updateGeometry();
    }
}

void PDFAppEditContentView::contextMenuEvent(QContextMenuEvent* event)
{
    // The menu of the selection (P12 opens it by popup)
    if (m_context && m_context->isEditingAllowed())
    {
        const QPoint position = event->reason() == QContextMenuEvent::Mouse ? event->globalPos() : viewport()->mapToGlobal(viewport()->rect().center());
        m_context->requestContextMenu(QString::fromLatin1(PDFAppEditContentMenuIds::CONTEXT), position);
    }
    event->accept();
}

void PDFAppEditContentView::resizeEvent(QResizeEvent* event)
{
    BaseClass::resizeEvent(event);
    if (m_isFitPending)
    {
        fitPage();
        m_isFitPending = m_scene ? m_scene->getPaperRectangle().isEmpty() : true;
    }
    Q_EMIT viewChanged();
    schedulePreview();
}

void PDFAppEditContentView::scrollContentsBy(int dx, int dy)
{
    BaseClass::scrollContentsBy(dx, dy);
    Q_EMIT viewChanged();
    schedulePreview();
}

bool PDFAppEditContentView::focusNextPrevChild(bool next)
{
    // Tab / Shift+Tab walk through the objects of the page (zadani_gui.md 4.20); past the last one the focus leaves
    // the canvas as usual (no trap of the keyboard)
    PDFAppEditContentContext* context = m_context.data();
    if (!context || !hasFocus() || !m_scene || !context->isEditingAllowed())
    {
        return BaseClass::focusNextPrevChild(next);
    }

    const std::vector<PDFContentNodeReference> nodes = m_scene->getSelectableNodes();
    PDFAppEditContentSelection* selection = context->getSelection();
    const auto current = std::find(nodes.cbegin(), nodes.cend(), selection->getKeyNode());
    PDFInteger index = current != nodes.cend() ? PDFInteger(current - nodes.cbegin()) + (next ? 1 : -1) : (next ? 0 : PDFInteger(nodes.size()) - 1);
    if (index < 0 || index >= PDFInteger(nodes.size()))
    {
        selection->clear();
        return BaseClass::focusNextPrevChild(next);
    }

    const PDFContentNodeReference node = nodes[size_t(index)];
    QPointer<PDFAppEditContentView> guard(this);
    selection->setNode(node);
    if (guard && !PDFDeferredDeletion::isRetired(this))
    {
        const QRectF box = PDFAppEditContentGeometryHelper::getPageBoundingBox(m_context.data(), node);
        if (!box.isNull())
        {
            ensureVisible(box);
        }
    }
    return true;
}

void PDFAppEditContentView::dragEnterEvent(QDragEnterEvent* event)
{
    const PDFAppEditContentContext* context = m_context.data();
    if (context && context->isEditingAllowed())
    {
        for (const PDFAppEditContentDropEntry* entry : context->getRegistry()->getDrops())
        {
            if (entry->canDrop(event->mimeData()))
            {
                event->acceptProposedAction();
                return;
            }
        }
    }
    event->ignore();
}

void PDFAppEditContentView::dragMoveEvent(QDragMoveEvent* event)
{
    const PDFAppEditContentContext* context = m_context.data();
    if (context && context->isEditingAllowed())
    {
        for (const PDFAppEditContentDropEntry* entry : context->getRegistry()->getDrops())
        {
            if (entry->canDrop(event->mimeData()))
            {
                event->acceptProposedAction();
                return;
            }
        }
    }
    event->ignore();
}

void PDFAppEditContentView::dropEvent(QDropEvent* event)
{
    // The first drop entry, which can take the data, at the point of the page (it changes the model from the event loop)
    PDFAppEditContentContext* context = m_context.data();
    if (context && context->isEditingAllowed())
    {
        const QPointF pagePoint = mapToScene(event->position().toPoint());
        for (const PDFAppEditContentDropEntry* entry : context->getRegistry()->getDrops())
        {
            if (entry->canDrop(event->mimeData()))
            {
                if (entry->drop(context, event->mimeData(), pagePoint))
                {
                    event->acceptProposedAction();
                    return;
                }
                break;
            }
        }
    }
    event->ignore();
}

void PDFAppEditContentView::updateCursor(const QPointF& scenePoint)
{
    if (m_isPanning || !m_context)
    {
        return;
    }
    viewport()->setCursor(m_context->getToolController()->getCursor(scenePoint));
}

void PDFAppEditContentView::onPageChanged()
{
    // The view of the old page is kept until the tool closes; the new page gets its own one (or a fit)
    if (m_shownPage >= 0)
    {
        m_pageViews[m_shownPage] = PageView{ m_zoom, getVisibleCenter() };
    }
    m_shownPage = m_context ? m_context->getPageIndex() : -1;

    const auto it = m_pageViews.find(m_shownPage);
    if (it != m_pageViews.end())
    {
        m_zoom = it->second.zoom;
        applyTransform();
        centerOn(it->second.center);
        m_isFitPending = false;
        Q_EMIT viewChanged();
    }
    else
    {
        m_isFitPending = true;
        applyTransform();
        if (m_scene && !m_scene->getPaperRectangle().isEmpty())
        {
            fitPage();
            m_isFitPending = false;
        }
    }
    schedulePreview();
}

void PDFAppEditContentView::onContainerChanged()
{
    if (m_isFitPending && m_scene && !m_scene->getPaperRectangle().isEmpty())
    {
        fitPage();
        m_isFitPending = false;
    }
}

void PDFAppEditContentView::onPreviewActiveChanged(bool isActive)
{
    if (isActive)
    {
        requestPreview();
    }
    else
    {
        m_previewTimer->stop();
    }
    viewport()->update();
}

void PDFAppEditContentView::onPreviewImageChanged()
{
    if (m_scene)
    {
        m_scene->update();
    }
}

void PDFAppEditContentView::onOptionChanged(const QString& name)
{
    if (name == QLatin1String(PDFAppEditContentOptions::PREVIEW_OVERPRINT) || name == QLatin1String(PDFAppEditContentOptions::PREVIEW_ANNOTATIONS) ||
        name == QLatin1String(PDFAppEditContentOptions::PREVIEW_PAPER_COLOR))
    {
        schedulePreview();
    }
}

void PDFAppEditContentView::onToolStateChanged()
{
    if (m_context && viewport()->underMouse())
    {
        updateCursor(mapToScene(viewport()->mapFromGlobal(QCursor::pos())));
    }
}

void PDFAppEditContentView::schedulePreview()
{
    if (m_context && m_context->isPreviewActive() && !PDFDeferredDeletion::isRetired(this))
    {
        m_previewTimer->start();
    }
}

PDFAppEditContentPreviewRequest PDFAppEditContentView::getPreviewRequest() const
{
    PDFAppEditContentPreviewRequest request;
    const QRectF paper = m_scene ? m_scene->getPaperRectangle() : QRectF();
    if (!m_context || paper.isEmpty())
    {
        return request;
    }

    // The visible part of the paper at the resolution of the screen
    const QRectF visible = mapToScene(viewport()->rect()).boundingRect().intersected(paper);
    if (visible.isEmpty())
    {
        return request;
    }

    const qreal scale = m_zoom * getDeviceScale() * devicePixelRatioF();
    const PageRotation rotation = getRotation();
    const bool isSwapped = rotation == PageRotation::Rotate90 || rotation == PageRotation::Rotate270;
    qreal width = (isSwapped ? visible.height() : visible.width()) * scale;
    qreal height = (isSwapped ? visible.width() : visible.height()) * scale;
    const qreal pixels = width * height;
    if (pixels > qreal(PDFAppEditContentPreview::MAXIMUM_PIXELS))
    {
        const qreal factor = std::sqrt(qreal(PDFAppEditContentPreview::MAXIMUM_PIXELS) / pixels);
        width *= factor;
        height *= factor;
    }

    const PDFAppEditContentOptions* options = m_context->getOptions();
    request.pageIndex = m_context->getPageIndex();
    request.pageRectangle = visible;
    request.imageSize = QSize(qMax(1, qRound(width)), qMax(1, qRound(height)));
    request.isOverprintSimulated = options->getBool(QString::fromLatin1(PDFAppEditContentOptions::PREVIEW_OVERPRINT));
    request.isAnnotationsShown = options->getBool(QString::fromLatin1(PDFAppEditContentOptions::PREVIEW_ANNOTATIONS));
    const QColor paperColor(options->getValue(QString::fromLatin1(PDFAppEditContentOptions::PREVIEW_PAPER_COLOR)).toString());
    request.paperColor = paperColor.isValid() ? paperColor : QColor(Qt::white);
    return request;
}

void PDFAppEditContentView::requestPreview()
{
    if (!m_context || !m_context->isPreviewActive() || PDFDeferredDeletion::isRetired(this))
    {
        return;
    }

    const PDFAppEditContentPreviewRequest request = getPreviewRequest();
    if (request.pageIndex >= 0)
    {
        m_context->getPreview()->render(request);
    }
}

// ---------------------------------------------------------------- PDFAppEditContentCanvas

PDFAppEditContentCanvas::PDFAppEditContentCanvas(PDFAppEditContentContext* context, QWidget* parent) :
    BaseClass(parent),
    m_context(context)
{
    setObjectName(QStringLiteral("editContentCanvas"));

    // The rulers around the view (canvas widget entries), the view in the middle
    QGridLayout* layout = new QGridLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_view = new PDFAppEditContentView(context, this);
    layout->addWidget(m_view, 1, 1);
    layout->setRowStretch(1, 1);
    layout->setColumnStretch(1, 1);
    m_view->installEventFilter(this);

    m_modeLabel = new PDFAppEditContentModeLabel(this);

    if (context)
    {
        for (const PDFAppEditContentCanvasWidgetEntry* entry : context->getRegistry()->getCanvasWidgets())
        {
            QWidget* widget = entry->createWidget(context, this);
            if (!widget)
            {
                continue;
            }

            widget->setParent(this);
            m_entryWidgets.emplace_back(entry->getId(), widget);
            switch (entry->getPlace())
            {
                case PDFAppEditContentCanvasWidgetEntry::Place::TopRuler:
                    layout->addWidget(widget, 0, 1);
                    break;
                case PDFAppEditContentCanvasWidgetEntry::Place::LeftRuler:
                    layout->addWidget(widget, 1, 0);
                    break;
                case PDFAppEditContentCanvasWidgetEntry::Place::RulerCorner:
                    layout->addWidget(widget, 0, 0);
                    break;
                case PDFAppEditContentCanvasWidgetEntry::Place::TopBar:
                    m_topBars.push_back(widget);
                    widget->installEventFilter(this);
                    break;
                case PDFAppEditContentCanvasWidgetEntry::Place::FloatingPanel:
                    m_floatingPanels.push_back(widget);
                    widget->installEventFilter(this);
                    break;
            }
        }

        PDFDeferredDeletion::connect(context->getToolController(), &PDFAppEditContentToolController::stateChanged, this, &PDFAppEditContentCanvas::onToolStateChanged);
        PDFDeferredDeletion::connect(context->getToolController(), &PDFAppEditContentToolController::activeToolChanged, this, &PDFAppEditContentCanvas::onToolStateChanged);
        context->setCanvas(this);
    }

    onToolStateChanged();
}

PDFAppEditContentCanvas::~PDFAppEditContentCanvas()
{
    // The children are deleted after this body: no event of a child being deleted reaches the filter
    if (m_view)
    {
        m_view->removeEventFilter(this);
    }
    if (QWidget* notice = m_noticeWidget.data())
    {
        notice->removeEventFilter(this);
    }
    for (const QPointer<QWidget>& widget : m_topBars)
    {
        if (widget)
        {
            widget->removeEventFilter(this);
        }
    }
    for (const QPointer<QWidget>& widget : m_floatingPanels)
    {
        if (widget)
        {
            widget->removeEventFilter(this);
        }
    }
    m_view = nullptr;
}

PDFAppEditContentView* PDFAppEditContentCanvas::getView() const
{
    return m_view;
}

PDFAppEditContentScene* PDFAppEditContentCanvas::getScene() const
{
    return m_view ? m_view->getContentScene() : nullptr;
}

QWidget* PDFAppEditContentCanvas::getEntryWidget(const QString& entryId) const
{
    for (const auto& item : m_entryWidgets)
    {
        if (item.first == entryId)
        {
            return item.second.data();
        }
    }
    return nullptr;
}

void PDFAppEditContentCanvas::setNoticeWidget(QWidget* widget)
{
    if (m_noticeWidget == widget)
    {
        return;
    }

    if (QWidget* previous = m_noticeWidget.data())
    {
        m_noticeWidget = nullptr;
        previous->removeEventFilter(this);
        PDFDeferredDeletion::retire(previous);
    }

    m_noticeWidget = widget;
    if (widget)
    {
        widget->setParent(this);
        widget->installEventFilter(this);
        widget->show();
        widget->raise();
    }
    layoutOverlays();
}

QWidget* PDFAppEditContentCanvas::getNoticeWidget() const
{
    return m_noticeWidget.data();
}

QString PDFAppEditContentCanvas::getModeLabelText() const
{
    return static_cast<const PDFAppEditContentModeLabel*>(m_modeLabel)->getText();
}

void PDFAppEditContentCanvas::onToolStateChanged()
{
    const PDFAppEditContentToolController* controller = m_context ? m_context->getToolController() : nullptr;
    static_cast<PDFAppEditContentModeLabel*>(m_modeLabel)->setText(controller ? controller->getModeLabel() : QString());
    layoutOverlays();
}

bool PDFAppEditContentCanvas::eventFilter(QObject* watched, QEvent* event)
{
    // The view changed its geometry, an overlay its size hint or its visibility: the overlays are placed again
    switch (event->type())
    {
        case QEvent::Resize:
        case QEvent::Move:
        case QEvent::Show:
        case QEvent::Hide:
        case QEvent::LayoutRequest:
            if (watched == m_view || watched == m_noticeWidget.data() || event->type() != QEvent::Move)
            {
                layoutOverlays();
            }
            break;

        default:
            break;
    }
    return BaseClass::eventFilter(watched, event);
}

void PDFAppEditContentCanvas::resizeEvent(QResizeEvent* event)
{
    BaseClass::resizeEvent(event);
    layoutOverlays();
}

void PDFAppEditContentCanvas::layoutOverlays()
{
    if (!m_view || !m_modeLabel)
    {
        return;
    }

    // From the top: the bar of the group over the top edge, the mode label below it at the left, the floating panels
    // at the top trailing corner; the notice at the bottom edge (zadani_gui.md 4.15)
    const QRect area = m_view->geometry();
    const int margin = OVERLAY_MARGIN;
    int top = area.top();
    for (const QPointer<QWidget>& bar : m_topBars)
    {
        if (bar && !bar->sizeHint().isEmpty())
        {
            const int height = bar->sizeHint().height();
            bar->setGeometry(area.left(), top, area.width(), height);
            bar->show();
            bar->raise();
            top += height;
        }
        else if (bar)
        {
            bar->hide();
        }
    }

    if (m_modeLabel->isVisibleTo(this) || !getModeLabelText().isEmpty())
    {
        const QSize size = m_modeLabel->sizeHint();
        const QRect rectangle(area.left() + margin, top + margin, qMin(size.width(), qMax(0, area.width() - 2 * margin)), size.height());
        m_modeLabel->setGeometry(QStyle::visualRect(layoutDirection(), area, rectangle));
        m_modeLabel->raise();
    }

    for (const QPointer<QWidget>& panel : m_floatingPanels)
    {
        if (panel)
        {
            const QSize size = panel->sizeHint().boundedTo(QSize(qMax(0, area.width() - 2 * margin), qMax(0, area.height() - 2 * margin)));
            const QRect rectangle(area.right() - margin - size.width() + 1, top + margin, size.width(), size.height());
            panel->setGeometry(QStyle::visualRect(layoutDirection(), area, rectangle));
            panel->raise();
        }
    }

    if (QWidget* notice = m_noticeWidget.data())
    {
        const int width = qMax(0, area.width() - 2 * margin);
        const int height = notice->hasHeightForWidth() ? notice->heightForWidth(width) : notice->sizeHint().height();
        notice->setGeometry(area.left() + margin, area.bottom() - margin - height + 1, width, qMax(0, height));
        notice->raise();
    }
}

}   // namespace pdf

#include "moc_pdfappeditcontentview.cpp"
