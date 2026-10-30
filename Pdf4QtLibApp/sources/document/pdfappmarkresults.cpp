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

#include "pdfappmarkresults.h"
#include "pdfappcolorpicker.h"
#include "pdfappannotatecommands.h"
#include "pdfappannotationcontroller.h"
#include "pdfappannotationcreator.h"
#include "pdfappcommand.h"
#include "pdfappdocument.h"
#include "pdfappdocumentmenus.h"
#include "pdfappdocumentsearch.h"
#include "pdfappdocumentsearchcontroller.h"
#include "pdfappdocumentstatusbar.h"
#include "pdfappdocumenttab.h"
#include "pdfappdocumenttoolbar.h"
#include "pdfappdocumentview.h"
#include "pdfapplocale.h"

#include "pdfcomponentpanels.h"
#include "pdffluentwidgets.h"
#include "pdfstylepainter.h"
#include "pdfthemeiconengine.h"
#include "pdfthememanager.h"
#include "pdfutils.h"
#include "pdfwidgetstyle.h"
#include "pdfwidgetstyleproperties.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QPainter>
#include <QPushButton>
#include <QScreen>
#include <QTimer>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

namespace pdf
{

/// Values of Mark as annotations
class PDFAppMarkResultsHelper
{
public:
    PDFAppMarkResultsHelper() = delete;

    static constexpr const char* CONTEXT = "pdf::PDFAppMarkResults";
    static constexpr const char* VALUE_PROPERTY = "pdf4qtMarkValue";

    /// Menu 737:9221: padding 6 inside of the border, gap 1
    static constexpr int MENU_PADDING = 7;
    static constexpr int MENU_GAP = 1;

    /// Footer 737:9129 (59 high): border-top 1 border/default, padding 10 / 12 / 12,
    /// the button 12, 11 (275 x 36)
    static constexpr int FOOTER_PADDING_X = 12;
    static constexpr int FOOTER_PADDING_TOP = 11;
    static constexpr int FOOTER_PADDING_BOTTOM = 12;
    static constexpr int BUTTON_HEIGHT = 36;

    /// Gap between the menu and its anchor (737:9061: the menu ends 6 px above the button)
    static constexpr int ANCHOR_GAP = 6;

    static QString translate(const char* text)
    {
        return QCoreApplication::translate("pdf::PDFAppMarkResults", text);
    }

    /// Text of the Apply button ("Highlight 24 results"). The plural texts follow
    /// the rule of the application: the count is %Ln (the number of the locale of
    /// the user interface, O17) with n, so a translation has the plural forms of
    /// its language; English chooses its singular source text by the count.
    static QString getApplyText(AnnotationType type, int count)
    {
        const bool isOne = count == 1;
        switch (type)
        {
            case AnnotationType::Underline:
                return isOne ? QCoreApplication::translate("pdf::PDFAppMarkResults", "Underline %Ln result", nullptr, count)
                             : QCoreApplication::translate("pdf::PDFAppMarkResults", "Underline %Ln results", nullptr, count);
            case AnnotationType::Squiggly:
                return isOne ? QCoreApplication::translate("pdf::PDFAppMarkResults", "Underline %Ln result with a squiggly line", nullptr, count)
                             : QCoreApplication::translate("pdf::PDFAppMarkResults", "Underline %Ln results with a squiggly line", nullptr, count);
            case AnnotationType::StrikeOut:
                return isOne ? QCoreApplication::translate("pdf::PDFAppMarkResults", "Strike through %Ln result", nullptr, count)
                             : QCoreApplication::translate("pdf::PDFAppMarkResults", "Strike through %Ln results", nullptr, count);
            default:
                break;
        }
        return isOne ? QCoreApplication::translate("pdf::PDFAppMarkResults", "Highlight %Ln result", nullptr, count)
                     : QCoreApplication::translate("pdf::PDFAppMarkResults", "Highlight %Ln results", nullptr, count);
    }

    /// Message of the status bar of the result (737:9219), the plural texts as
    /// getApplyText
    static QString getResultText(AnnotationType type, int count)
    {
        const bool isOne = count == 1;
        switch (type)
        {
            case AnnotationType::Underline:
                return isOne ? QCoreApplication::translate("pdf::PDFAppMarkResults", "%Ln result underlined as an annotation. Ctrl+Z undoes it.", nullptr, count)
                             : QCoreApplication::translate("pdf::PDFAppMarkResults", "%Ln results underlined as annotations. Ctrl+Z undoes it.", nullptr, count);
            case AnnotationType::Squiggly:
                return isOne ? QCoreApplication::translate("pdf::PDFAppMarkResults", "%Ln result underlined with a squiggly line as an annotation. Ctrl+Z undoes it.", nullptr, count)
                             : QCoreApplication::translate("pdf::PDFAppMarkResults", "%Ln results underlined with a squiggly line as annotations. Ctrl+Z undoes it.", nullptr, count);
            case AnnotationType::StrikeOut:
                return isOne ? QCoreApplication::translate("pdf::PDFAppMarkResults", "%Ln result struck through as an annotation. Ctrl+Z undoes it.", nullptr, count)
                             : QCoreApplication::translate("pdf::PDFAppMarkResults", "%Ln results struck through as annotations. Ctrl+Z undoes it.", nullptr, count);
            default:
                break;
        }
        return isOne ? QCoreApplication::translate("pdf::PDFAppMarkResults", "%Ln result highlighted as an annotation. Ctrl+Z undoes it.", nullptr, count)
                     : QCoreApplication::translate("pdf::PDFAppMarkResults", "%Ln results highlighted as annotations. Ctrl+Z undoes it.", nullptr, count);
    }
};

// ------------------------------------------------------- PDFAppMarkResultsMenu

PDFAppMarkResultsMenu::PDFAppMarkResultsMenu(QWidget* parent) :
    BaseClass(parent, Qt::Popup)
{
    using H = PDFAppMarkResultsHelper;

    setObjectName(QStringLiteral("markResultsMenu"));
    setProperty(PDFWidgetStyleProperties::POPOVER, true);
    setProperty(PDFWidgetStyleProperties::POPUP_PLACED, true);
    setAttribute(Qt::WA_WindowPropagation, true);

    m_layout = new QVBoxLayout(this);
    m_layout->setSpacing(H::MENU_GAP);
    m_layout->setSizeConstraint(QLayout::SetNoConstraint);

    // section 737:9255 "Mark as": Highlight, Underline, Squiggly underline, Strikethrough
    m_layout->addWidget(createSection(QT_TRANSLATE_NOOP("pdf::PDFAppMarkResults", "Mark as")));
    const std::pair<AnnotationType, QString> types[] = { { AnnotationType::Highlight, QStringLiteral("highlighter") },
                                                         { AnnotationType::Underline, QStringLiteral("underline") },
                                                         { AnnotationType::Squiggly, QStringLiteral("squiggly") },
                                                         { AnnotationType::StrikeOut, QStringLiteral("strikethrough") } };
    for (const auto& type : types)
    {
        PDFAppPanelMenuItem* item = new PDFAppPanelMenuItem(type.second, this);
        item->setObjectName(QStringLiteral("markType:") + type.second);
        item->setCheckable(true);
        item->setProperty(H::VALUE_PROPERTY, int(type.first));
        connect(item, &QAbstractButton::clicked, this, &PDFAppMarkResultsMenu::onTypeClicked);
        m_layout->addWidget(item);
        m_typeItems[type.first] = item;
    }

    // section 737:9304 "Color", colors 737:9306 (padding 2 / 10 / 6)
    m_layout->addWidget(createSeparator());
    m_layout->addWidget(createSection(QT_TRANSLATE_NOOP("pdf::PDFAppMarkResults", "Color")));
    QWidget* colors = new QWidget(this);
    QHBoxLayout* colorsLayout = new QHBoxLayout(colors);
    colorsLayout->setContentsMargins(10, 2, 10, 6);
    m_colorRow = new PDFColorSwatchRow(colors);
    m_colorRow->setObjectName(QStringLiteral("markColors"));
    PDFAppColorPicker::attach(m_colorRow.data())->setPopupRestored(true);
    colorsLayout->addWidget(m_colorRow);
    colorsLayout->addStretch(1);
    m_layout->addWidget(colors);

    // section 737:9333 "Results": All results, Results on this page, Selected result only
    m_layout->addWidget(createSeparator());
    m_layout->addWidget(createSection(QT_TRANSLATE_NOOP("pdf::PDFAppMarkResults", "Results")));
    for (Scope scope : { Scope::All, Scope::CurrentPage, Scope::Selected })
    {
        PDFAppPanelMenuItem* item = new PDFAppPanelMenuItem(QString(), this);
        item->setObjectName(QStringLiteral("markScope:%1").arg(int(scope)));
        item->setCheckable(true);
        item->setProperty(H::VALUE_PROPERTY, int(scope));
        connect(item, &QAbstractButton::clicked, this, &PDFAppMarkResultsMenu::onScopeClicked);
        m_layout->addWidget(item);
        m_scopeItems[scope] = item;
    }

    // actions 737:9367 (padding 4 / 4 / 2): Primary button 282 x 36
    m_layout->addWidget(createSeparator());
    QWidget* actions = new QWidget(this);
    QHBoxLayout* actionsLayout = new QHBoxLayout(actions);
    actionsLayout->setContentsMargins(4, 4, 4, 2);
    m_applyButton = new QPushButton(actions);
    m_applyButton->setObjectName(QStringLiteral("markApply"));
    m_applyButton->setFixedHeight(H::BUTTON_HEIGHT);
    PDFWidgetStyleProperties::set(m_applyButton, PDFWidgetStyleProperties::PRIMARY);
    actionsLayout->addWidget(m_applyButton);
    m_layout->addWidget(actions);
    connect(m_applyButton.data(), &QPushButton::clicked, this, &PDFAppMarkResultsMenu::onApplyClicked);

    connect(PDFThemeManager::getInstance(), &PDFThemeManager::themeChanged, this, qOverload<>(&PDFAppMarkResultsMenu::update));
    retranslate();
}

PDFAppMarkResultsMenu::~PDFAppMarkResultsMenu()
{
    if (isVisible())
    {
        hide();
    }
}

QWidget* PDFAppMarkResultsMenu::createSection(const char* caption)
{
    // Menu section 706:150119: 24 high, Medium 11 text/tertiary
    QWidget* section = new QWidget(this);
    QHBoxLayout* layout = new QHBoxLayout(section);
    layout->setContentsMargins(10, 6, 10, 4);
    PDFLabel* label = new PDFLabel(QString(), PDFThemeFonts::Role::Caption, PDFThemeColors::TextTertiary, section);
    layout->addWidget(label);
    m_captions.emplace_back(label, caption);
    return section;
}

QWidget* PDFAppMarkResultsMenu::createSeparator()
{
    // Menu separator 706:150117
    QWidget* separator = new QWidget(this);
    QHBoxLayout* layout = new QHBoxLayout(separator);
    layout->setContentsMargins(4, 3, 4, 3);
    layout->addWidget(new PDFAppToolbarDivider(Qt::Horizontal, 0, 0, PDFThemeColors::BorderDivider, separator));
    return separator;
}

void PDFAppMarkResultsMenu::retranslate()
{
    using H = PDFAppMarkResultsHelper;

    for (const auto& caption : m_captions)
    {
        if (caption.first)
        {
            caption.first->setText(H::translate(caption.second));
        }
    }

    const std::pair<AnnotationType, const char*> typeTexts[] = { { AnnotationType::Highlight, QT_TRANSLATE_NOOP("pdf::PDFAppMarkResults", "Highlight") },
                                                                 { AnnotationType::Underline, QT_TRANSLATE_NOOP("pdf::PDFAppMarkResults", "Underline") },
                                                                 { AnnotationType::Squiggly, QT_TRANSLATE_NOOP("pdf::PDFAppMarkResults", "Squiggly underline") },
                                                                 { AnnotationType::StrikeOut, QT_TRANSLATE_NOOP("pdf::PDFAppMarkResults", "Strikethrough") } };
    for (const auto& typeText : typeTexts)
    {
        if (PDFAppPanelMenuItem* item = getTypeItem(typeText.first))
        {
            item->setText(H::translate(typeText.second));
            item->setAccessibleName(item->text());
        }
    }
    m_colorRow->setAccessibleName(QCoreApplication::translate("pdf::PDFAppMarkResults", "Color"));
    setAccessibleName(QCoreApplication::translate("pdf::PDFAppMarkResults", "Mark as annotations"));
    setWindowTitle(accessibleName());
    updateState();
}

void PDFAppMarkResultsMenu::updateState()
{
    using H = PDFAppMarkResultsHelper;

    for (const auto& item : m_typeItems)
    {
        if (item.second)
        {
            const QSignalBlocker blocker(item.second);
            item.second->setChecked(item.first == m_type);
        }
    }

    const QString scopeTexts[] = { QCoreApplication::translate("pdf::PDFAppMarkResults", "All results (%1)").arg(PDFAppLocale::formatNumber(m_counts[0])),
                                   QCoreApplication::translate("pdf::PDFAppMarkResults", "Results on this page (%1)").arg(PDFAppLocale::formatNumber(m_counts[1])),
                                   QCoreApplication::translate("pdf::PDFAppMarkResults", "Selected result only") };
    for (const auto& item : m_scopeItems)
    {
        if (item.second)
        {
            const int index = int(item.first);
            const QSignalBlocker blocker(item.second);
            item.second->setText(scopeTexts[index]);
            item.second->setAccessibleName(scopeTexts[index]);
            item.second->setChecked(item.first == m_scope);
            item.second->setEnabled(m_counts[index] > 0);
        }
    }

    const int count = m_counts[int(m_scope)];
    m_applyButton->setText(H::getApplyText(m_type, count));
    m_applyButton->setEnabled(count > 0);
}

void PDFAppMarkResultsMenu::popup(QWidget* anchor, AnnotationType type, const QColor& color, const std::array<int, 3>& counts, Scope scope)
{
    using H = PDFAppMarkResultsHelper;

    if (!anchor || !anchor->isVisible())
    {
        return;
    }

    m_type = PDFAppAnnotationOperations::isTextMarkupType(type) ? type : AnnotationType::Highlight;
    m_counts = counts;
    m_scope = scope;
    if (m_counts[int(m_scope)] <= 0)
    {
        m_scope = Scope::All;
    }
    m_colorRow->setColor(color);
    setLayoutDirection(anchor->layoutDirection());
    ensurePolished();
    updateState();

    const QMargins margins = PDFWidgetStyle::getPopoverMargins(this);
    m_layout->setContentsMargins(margins + QMargins(H::MENU_PADDING, H::MENU_PADDING, H::MENU_PADDING, H::MENU_PADDING));
    m_layout->activate();
    const int windowWidth = PANEL_WIDTH + margins.left() + margins.right();
    const int windowHeight = m_layout->hasHeightForWidth() ? m_layout->totalHeightForWidth(windowWidth) : m_layout->totalSizeHint().height();
    const QSize panelSize(PANEL_WIDTH, qMax(0, windowHeight - margins.top() - margins.bottom()));

    // Above the anchor at its leading edge (the footer lies at the bottom of the
    // sidebar), below it, when there is no room; kept inside of the screen
    const QRect anchorRect(anchor->mapToGlobal(QPoint(0, 0)), anchor->size());
    const bool isRightToLeft = layoutDirection() == Qt::RightToLeft;
    QRect panel(QPoint(isRightToLeft ? anchorRect.right() + 1 - panelSize.width() : anchorRect.left(), anchorRect.top() - H::ANCHOR_GAP - panelSize.height()), panelSize);
    if (const QScreen* screen = anchor->screen())
    {
        const QRect available = screen->availableGeometry();
        if (panel.top() < available.top())
        {
            panel.moveTop(anchorRect.bottom() + 1 + H::ANCHOR_GAP);
        }
        panel.moveLeft(qBound(available.left(), panel.left(), qMax(available.left(), available.right() + 1 - panel.width())));
        panel.moveTop(qBound(available.top(), panel.top(), qMax(available.top(), available.bottom() + 1 - panel.height())));
    }

    setFixedSize(panel.size().grownBy(margins));
    move(panel.topLeft() - QPoint(margins.left(), margins.top()));

    QPointer<PDFAppMarkResultsMenu> guard(this);
    show();
    if (!guard)
    {
        return;
    }
    activateWindow();
    if (PDFAppPanelMenuItem* item = getTypeItem(m_type))
    {
        item->setFocus(Qt::PopupFocusReason);
    }
}

QColor PDFAppMarkResultsMenu::getColor() const
{
    return m_colorRow ? m_colorRow->getColor() : QColor();
}

PDFAppPanelMenuItem* PDFAppMarkResultsMenu::getTypeItem(AnnotationType type) const
{
    auto it = m_typeItems.find(type);
    return it != m_typeItems.end() ? it->second.data() : nullptr;
}

PDFAppPanelMenuItem* PDFAppMarkResultsMenu::getScopeItem(Scope scope) const
{
    auto it = m_scopeItems.find(scope);
    return it != m_scopeItems.end() ? it->second.data() : nullptr;
}

PDFColorSwatchRow* PDFAppMarkResultsMenu::getColorRow() const
{
    return m_colorRow.data();
}

QPushButton* PDFAppMarkResultsMenu::getApplyButton() const
{
    return m_applyButton.data();
}

QRect PDFAppMarkResultsMenu::getPanelRect() const
{
    return rect().marginsRemoved(PDFWidgetStyle::getPopoverMargins(this));
}

void PDFAppMarkResultsMenu::onTypeClicked()
{
    // The items do not identify themselves in the signal; a change of the type
    // shows the remembered color of the type
    if (const QObject* item = sender())
    {
        m_type = AnnotationType(item->property(PDFAppMarkResultsHelper::VALUE_PROPERTY).toInt());
        m_colorRow->setColor(PDFAppAnnotationToolSettings::getMarkupColor(m_type));
        updateState();
    }
}

void PDFAppMarkResultsMenu::onScopeClicked()
{
    if (const QObject* item = sender())
    {
        m_scope = Scope(item->property(PDFAppMarkResultsHelper::VALUE_PROPERTY).toInt());
        updateState();
    }
}

void PDFAppMarkResultsMenu::onApplyClicked()
{
    const AnnotationType type = m_type;
    const QColor color = getColor();
    const Scope scope = m_scope;
    QPointer<PDFAppMarkResultsMenu> guard(this);
    hide();
    if (guard)
    {
        Q_EMIT applyRequested(type, color, scope);
    }
}

void PDFAppMarkResultsMenu::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    PDFWidgetStyle::drawPopoverPanel(&painter, this, RADIUS);
}

void PDFAppMarkResultsMenu::keyPressEvent(QKeyEvent* event)
{
    switch (event->key())
    {
        case Qt::Key_Escape:
            event->accept();
            hide();
            return;

        case Qt::Key_Down:
        case Qt::Key_Up:
            event->accept();
            focusNextPrevChild(event->key() == Qt::Key_Down);
            return;

        default:
            break;
    }
    BaseClass::keyPressEvent(event);
}

void PDFAppMarkResultsMenu::changeEvent(QEvent* event)
{
    BaseClass::changeEvent(event);
    if (event->type() == QEvent::LanguageChange)
    {
        retranslate();
    }
}

// ----------------------------------------------------------- PDFAppMarkResults

PDFAppMarkResults::PDFAppMarkResults(PDFAppDocumentView* view, QObject* parent) :
    BaseClass(parent),
    m_view(view)
{
    setObjectName(QStringLiteral("markResults"));
}

PDFAppMarkResults::~PDFAppMarkResults()
{
    // A running worker is stopped (its result is dropped with the watcher)
    cancel();
    delete m_menu.data();
}

PDFAppDocumentView* PDFAppMarkResults::getView() const
{
    return m_view.data();
}

PDFAppDocumentSearchController* PDFAppMarkResults::getSearchController() const
{
    const PDFAppDocumentTab* tab = m_view ? m_view->getTab() : nullptr;
    const PDFAppDocumentSearchCommandHandler* handler = tab ? qobject_cast<const PDFAppDocumentSearchCommandHandler*>(tab->findHandler(PDFAppCommands::find())) : nullptr;
    return handler ? handler->getSearchController() : nullptr;
}

bool PDFAppMarkResults::canMark() const
{
    const PDFAppAnnotationController* controller = m_view ? m_view->getAnnotationController() : nullptr;
    return controller && controller->isAnnotationEditingAllowed() && !isRunning() && getMatchCount(Scope::All) > 0;
}

PDFDocumentTextSearchMatches PDFAppMarkResults::getMatches(Scope scope) const
{
    const PDFAppDocumentSearchController* searchController = getSearchController();
    const PDFAppDocumentSearchResultsPointer results = searchController ? searchController->getResults() : PDFAppDocumentSearchResultsPointer();
    if (!results)
    {
        return PDFDocumentTextSearchMatches();
    }

    const PDFDocumentTextSearchMatches& matches = results->result.results;
    switch (scope)
    {
        case Scope::All:
            return matches;

        case Scope::CurrentPage:
        {
            const PDFInteger pageIndex = m_view ? m_view->getCurrentPageIndex() : -1;
            PDFDocumentTextSearchMatches pageMatches;
            for (const PDFDocumentTextSearchMatch& match : matches)
            {
                if (match.pageIndex == pageIndex)
                {
                    pageMatches.push_back(match);
                }
            }
            return pageMatches;
        }

        case Scope::Selected:
        {
            const int index = searchController->getCurrentIndex();
            if (index >= 0 && size_t(index) < matches.size())
            {
                return { matches[size_t(index)] };
            }
            break;
        }
    }
    return PDFDocumentTextSearchMatches();
}

int PDFAppMarkResults::getMatchCount(Scope scope) const
{
    if (scope == Scope::All)
    {
        const PDFAppDocumentSearchController* searchController = getSearchController();
        const PDFAppDocumentSearchResultsPointer results = searchController ? searchController->getResults() : PDFAppDocumentSearchResultsPointer();
        return results ? int(qMin<size_t>(results->result.results.size(), size_t(std::numeric_limits<int>::max()))) : 0;
    }
    return int(getMatches(scope).size());
}

bool PDFAppMarkResults::isRunning() const
{
    return m_watcher != nullptr;
}

void PDFAppMarkResults::cancel()
{
    if (m_control)
    {
        m_control->cancel();
    }

    // The watcher is dropped (the worker ends by itself, nothing is committed)
    if (m_watcher)
    {
        m_watcher->disconnect(this);
        m_watcher->deleteLater();
        m_watcher = nullptr;
    }
    if (m_progressTimer)
    {
        m_progressTimer->stop();
    }

    // The members are cleared first: finish() announces the end synchronously and a
    // slot may delete the tab with this object (review RPKT 95a)
    QPointer<PDFAppDocumentOperation> operation = m_operation;
    m_operation = nullptr;
    m_control.reset();
    if (operation)
    {
        operation->disconnect(this);
        operation->finish();
    }
}

bool PDFAppMarkResults::mark(AnnotationType type, const QColor& color, Scope scope)
{
    PDFAppDocument* document = m_view ? m_view->getDocument() : nullptr;
    if (!document || !document->isReady() || !canMark() || !PDFAppAnnotationOperations::isTextMarkupType(type))
    {
        return false;
    }

    PDFDocumentTextSearchMatches matches = getMatches(scope);
    if (matches.empty())
    {
        return false;
    }

    // The color is remembered for the type (as the color of the markup tools)
    PDFAppAnnotationToolSettings::setMarkupColor(type, color);
    PDFAppNewAnnotation data;
    data.author = PDFAuthorSettings::getAuthorName();
    data.color = color.isValid() ? color : PDFAppAnnotationToolSettings::getMarkupColor(type);
    if (type == AnnotationType::Highlight)
    {
        data.opacity = PDFAppAnnotationOperations::HIGHLIGHT_OPACITY;
    }

    if (matches.size() <= SYNCHRONOUS_LIMIT)
    {
        size_t count = 0;
        const PDFAppAnnotationResult change = PDFAppAnnotationOperations::markSearchResults(document->getDocument(), matches, type, data, nullptr, nullptr, &count);
        finishMarking(change, count, type, document->getVersion());
        return count > 0;
    }

    // A large scope: a worker builds the document of a snapshot, the document
    // waits (a blocking operation with the progress and Cancel)
    const PDFAppDocumentSnapshot snapshot = document->getSnapshot();
    m_control = std::make_shared<PDFAppMarkResultsControl>();
    m_runningType = type;
    m_runningVersion = snapshot.version;
    m_runningTotal = matches.size();
    // The document announces the operation synchronously: a slot may delete the tab
    // (this object, the document and the operation) - review RPKT 95a
    QPointer<PDFAppMarkResults> beginGuard(this);
    PDFAppDocumentOperation* operation = document->beginOperation(PDFAppDocumentOperationKind::Edit, QCoreApplication::translate("pdf::PDFAppMarkResults", "Marking the search results"), true);
    if (!beginGuard)
    {
        return false;
    }
    m_operation = operation;
    if (m_operation)
    {
        connect(m_operation.data(), &PDFAppDocumentOperation::cancelRequested, this, &PDFAppMarkResults::onOperationCancelRequested);
    }

    m_watcher = new Watcher(this);
    connect(m_watcher.data(), &Watcher::finished, this, &PDFAppMarkResults::onWorkerFinished);
    if (!m_progressTimer)
    {
        m_progressTimer = new QTimer(this);
        m_progressTimer->setInterval(PROGRESS_INTERVAL);
        connect(m_progressTimer.data(), &QTimer::timeout, this, &PDFAppMarkResults::onProgressTimer);
    }
    m_progressTimer->start();
    m_watcher->setFuture(QtConcurrent::run(&PDFAppMarkResults::run, snapshot.document, std::move(matches), type, data, m_control));

    QPointer<PDFAppMarkResults> guard(this);
    Q_EMIT stateChanged();
    return guard != nullptr;
}

PDFAppMarkResultsOutcome PDFAppMarkResults::run(PDFDocumentPointer document,
                                                PDFDocumentTextSearchMatches matches,
                                                AnnotationType type,
                                                PDFAppNewAnnotation data,
                                                std::shared_ptr<PDFAppMarkResultsControl> control)
{
    PDFAppMarkResultsOutcome outcome;
    outcome.change = PDFAppAnnotationOperations::markSearchResults(document, matches, type, data, control.get(), control ? control->getProcessed() : nullptr, &outcome.count);
    return outcome;
}

void PDFAppMarkResults::onProgressTimer()
{
    if (m_operation && m_control && m_runningTotal > 0)
    {
        m_operation->setProgress(int(qMin<size_t>(100, m_control->getProcessedCount() * 100 / m_runningTotal)));
    }
}

void PDFAppMarkResults::onOperationCancelRequested()
{
    // Stop and Close of the close-running dialog, Cancel of the operation
    QPointer<PDFAppMarkResults> guard(this);
    cancel();
    if (guard)
    {
        showStatusMessage(QCoreApplication::translate("pdf::PDFAppMarkResults", "Marking of the search results was canceled."));
    }
    if (guard)
    {
        Q_EMIT stateChanged();
    }
    if (guard)
    {
        Q_EMIT finished(0);
    }
}

void PDFAppMarkResults::onWorkerFinished()
{
    Watcher* watcher = m_watcher.data();
    if (!watcher || sender() != watcher)
    {
        return;
    }

    const PDFAppMarkResultsOutcome outcome = watcher->isCanceled() ? PDFAppMarkResultsOutcome() : watcher->result();
    m_watcher = nullptr;
    watcher->deleteLater();
    if (m_progressTimer)
    {
        m_progressTimer->stop();
    }

    // The operation ends first (it blocks the commit); its end is announced
    // synchronously - a slot may delete the tab with this object (review RPKT 95a)
    const bool isCanceled = m_control && m_control->isOperationCancelled();
    m_control.reset();
    const AnnotationType type = m_runningType;
    const quint64 version = m_runningVersion;
    QPointer<PDFAppDocumentOperation> operation = m_operation;
    m_operation = nullptr;
    if (operation)
    {
        QPointer<PDFAppMarkResults> guard(this);
        operation->disconnect(this);
        operation->finish();
        if (!guard)
        {
            return;
        }
    }

    finishMarking(isCanceled ? PDFAppAnnotationResult() : outcome.change, isCanceled ? 0 : outcome.count, type, version);
}

void PDFAppMarkResults::finishMarking(const PDFAppAnnotationResult& change, size_t count, AnnotationType type, quint64 expectedVersion)
{
    using H = PDFAppMarkResultsHelper;

    PDFAppDocument* document = m_view ? m_view->getDocument() : nullptr;
    QPointer<PDFAppMarkResults> guard(this);
    bool isCommitted = false;
    if (change && document && count > 0)
    {
        // Refused, when the document has changed since the snapshot (or it is locked)
        isCommitted = document->commit(*change, QCoreApplication::translate("pdf::PDFAppMarkResults", "Mark search results as annotations"), expectedVersion, PDFAppChangeSource::User);
        if (!guard)
        {
            return;
        }
    }

    const int committed = isCommitted ? int(qMin<size_t>(count, size_t(std::numeric_limits<int>::max()))) : 0;
    if (isCommitted)
    {
        showStatusMessage(H::getResultText(type, committed));
    }
    else if (count > 0)
    {
        showStatusMessage(QCoreApplication::translate("pdf::PDFAppMarkResults", "The search results cannot be marked: the document has changed. Search again and mark them."));
    }

    if (guard)
    {
        Q_EMIT stateChanged();
    }
    if (guard)
    {
        Q_EMIT finished(committed);
    }
}

void PDFAppMarkResults::showStatusMessage(const QString& text)
{
    if (m_view && m_view->getStatusBar() && !text.isEmpty())
    {
        m_view->getStatusBar()->showMessage(text, PDFAppAnnotationController::STATUS_MESSAGE_TIMEOUT * 2);
    }
}

void PDFAppMarkResults::openMenu()
{
    // The footer of the shown search panel, else the search field of the toolbar
    QWidget* anchor = nullptr;
    if (m_view)
    {
        for (PDFAppMarkResultsFooter* footer : m_view->findChildren<PDFAppMarkResultsFooter*>())
        {
            if (footer->isVisible() && footer->getButton())
            {
                anchor = footer->getButton();
                break;
            }
        }
        if (!anchor && m_view->getToolbar())
        {
            anchor = m_view->getToolbar()->getSearchField();
        }
    }
    openMenu(anchor);
}

void PDFAppMarkResults::openMenu(QWidget* anchor)
{
    if (!anchor || !canMark())
    {
        return;
    }

    if (!m_menu)
    {
        m_menu = new PDFAppMarkResultsMenu(anchor->window());
        connect(m_menu.data(), &PDFAppMarkResultsMenu::applyRequested, this, &PDFAppMarkResults::onApplyRequested);
    }

    const AnnotationType type = PDFAppAnnotationToolSettings::getLastMarkupType(m_view ? m_view->getContext() : nullptr);
    const std::array<int, 3> counts = { getMatchCount(Scope::All), getMatchCount(Scope::CurrentPage), getMatchCount(Scope::Selected) };
    m_menu->popup(anchor, type, PDFAppAnnotationToolSettings::getMarkupColor(type), counts, Scope::All);
}

PDFAppMarkResultsMenu* PDFAppMarkResults::getMenu() const
{
    return m_menu.data();
}

void PDFAppMarkResults::onApplyRequested(AnnotationType type, const QColor& color, PDFAppMarkResultsMenu::Scope scope)
{
    PDFAppAnnotationToolSettings::setLastMarkupType(m_view ? m_view->getContext() : nullptr, type);
    mark(type, color, scope);
}

// ----------------------------------------------------- PDFAppMarkResultsFooter

PDFAppMarkResultsFooter::PDFAppMarkResultsFooter(QWidget* parent) :
    BaseClass(parent)
{
    using H = PDFAppMarkResultsHelper;

    setObjectName(QStringLiteral("markResultsFooter"));
    QHBoxLayout* layout = new QHBoxLayout(this);
    layout->setContentsMargins(H::FOOTER_PADDING_X, H::FOOTER_PADDING_TOP, H::FOOTER_PADDING_X, H::FOOTER_PADDING_BOTTOM);
    m_button = new QPushButton(this);
    m_button->setObjectName(QStringLiteral("markResultsButton"));
    m_button->setIcon(PDFThemeIcons::getIcon(QStringLiteral("to-annotation")));
    m_button->setFixedHeight(H::BUTTON_HEIGHT);
    layout->addWidget(m_button);
    connect(m_button.data(), &QPushButton::clicked, this, &PDFAppMarkResultsFooter::onButtonClicked);
    connect(PDFThemeManager::getInstance(), &PDFThemeManager::themeChanged, this, qOverload<>(&PDFAppMarkResultsFooter::update));

    retranslate();
    hide();
}

PDFAppMarkResultsFooter::~PDFAppMarkResultsFooter() = default;

void PDFAppMarkResultsFooter::setView(PDFAppDocumentView* view)
{
    for (const QMetaObject::Connection& connection : m_connections)
    {
        disconnect(connection);
    }
    m_connections.clear();
    m_view = view;

    if (PDFAppMarkResults* markResults = getMarkResults())
    {
        m_connections.push_back(connect(markResults, &PDFAppMarkResults::stateChanged, this, &PDFAppMarkResultsFooter::updateState));
        if (PDFAppDocumentSearchController* searchController = markResults->getSearchController())
        {
            m_connections.push_back(connect(searchController, &PDFAppDocumentSearchController::stateChanged, this, &PDFAppMarkResultsFooter::updateState));
        }
    }
    if (PDFAppAnnotationController* controller = view ? view->getAnnotationController() : nullptr)
    {
        m_connections.push_back(connect(controller, &PDFAppAnnotationController::editingAllowedChanged, this, &PDFAppMarkResultsFooter::updateState));
    }

    // The panel may get its view before the tab has created the handlers of
    // step 4: the connections are made again from the event loop
    if (view && !getMarkResults() && !m_isRetryPending)
    {
        m_isRetryPending = true;
        QMetaObject::invokeMethod(this, &PDFAppMarkResultsFooter::onRetryQueued, Qt::QueuedConnection);
    }
    updateState();
}

void PDFAppMarkResultsFooter::onRetryQueued()
{
    m_isRetryPending = false;
    if (m_view && getMarkResults())
    {
        setView(m_view.data());
    }
}

PDFAppMarkResults* PDFAppMarkResultsFooter::getMarkResults() const
{
    const PDFAppAnnotateCommandHandler* handler = PDFAppAnnotateCommandHandler::findHandler(m_view.data());
    return handler ? handler->getMarkResults() : nullptr;
}

QPushButton* PDFAppMarkResultsFooter::getButton() const
{
    return m_button.data();
}

void PDFAppMarkResultsFooter::updateState()
{
    // Shown with the results of a search; disabled with the reason for a locked
    // document or one without the permission
    const PDFAppMarkResults* markResults = getMarkResults();
    const bool hasResults = markResults && markResults->getMatchCount(PDFAppMarkResults::Scope::All) > 0;
    setVisible(hasResults);
    if (!m_button)
    {
        return;
    }

    const bool canMark = markResults && markResults->canMark();
    m_button->setEnabled(canMark);
    const PDFAppAnnotationController* controller = m_view ? m_view->getAnnotationController() : nullptr;
    const QString reason = controller && !controller->isAnnotationEditingAllowed() ? controller->getEditingRefusalReason() : QString();
    m_button->setToolTip(reason);
}

void PDFAppMarkResultsFooter::retranslate()
{
    m_button->setText(QCoreApplication::translate("pdf::PDFAppMarkResults", "Mark as annotations…"));
    m_button->setAccessibleName(m_button->text());
}

void PDFAppMarkResultsFooter::onButtonClicked()
{
    if (PDFAppMarkResults* markResults = getMarkResults())
    {
        markResults->openMenu(m_button);
    }
}

void PDFAppMarkResultsFooter::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    // The footer separates itself from the results by its top border border/default (737:9129)
    QPainter painter(this);
    PDFStylePainter::fillRect(&painter, QRectF(0, 0, width(), 1), PDFThemeColors::BorderDefault);
}

void PDFAppMarkResultsFooter::changeEvent(QEvent* event)
{
    BaseClass::changeEvent(event);
    if (event->type() == QEvent::LanguageChange)
    {
        retranslate();
    }
}

}   // namespace pdf
