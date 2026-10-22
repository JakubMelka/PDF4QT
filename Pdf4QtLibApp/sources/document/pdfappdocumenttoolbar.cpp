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

#include "pdfappdocumenttoolbar.h"
#include "pdfappcommand.h"
#include "pdfappcontext.h"
#include "pdfappdocument.h"
#include "pdfappdocumentmenus.h"
#include "pdfappdocumentview.h"
#include "pdfappmainwindow.h"

#include "pdfwidgettool.h"

#include "pdfcomponentpanels.h"
#include "pdffluentwidgets.h"
#include "pdfthemeiconengine.h"
#include "pdfthememanager.h"

#include <QHBoxLayout>
#include <QLocale>
#include <QPainter>
#include <QToolButton>

#include <cmath>

namespace pdf
{

/// Helpers of the toolbar
class PDFAppDocumentToolbarHelper
{
public:
    PDFAppDocumentToolbarHelper() = delete;

    /// Property of a button: the id of its command
    static constexpr const char* COMMAND_PROPERTY = "pdf4qtCommandId";

    /// Levels of the narrowing of the toolbar
    enum CollapseLevel
    {
        Full = 0,
        MenuIconsOnly,
        SearchNarrow,
        ZoomHidden,
        SearchMinimum,
        AnnotateInsertHidden,
        PageNavigationHidden,
        LastLevel = PageNavigationHidden
    };

    /// Creates a horizontal group without margins
    static QHBoxLayout* createGroup(QWidget* group, int spacing)
    {
        QHBoxLayout* layout = new QHBoxLayout(group);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(spacing);
        return layout;
    }

    /// Icon of the tool of the canvas shown by the Select button
    static QString getSelectIconName(const PDFAppDocumentView* view)
    {
        if (view && view->isCommandChecked(PDFAppCommands::selectTable()))
        {
            return QStringLiteral("table");
        }
        if (view && view->isCommandChecked(PDFAppCommands::screenshot()))
        {
            return QStringLiteral("camera");
        }
        if (view && view->isCommandChecked(PDFAppCommands::extractImage()))
        {
            return QStringLiteral("image");
        }
        return QStringLiteral("text-cursor");
    }
};

// ------------------------------------------------------------ PDFAppToolbarDivider

PDFAppToolbarDivider::PDFAppToolbarDivider(Qt::Orientation orientation, int length, int padding, PDFThemeColors::Color color, QWidget* parent) :
    BaseClass(parent),
    m_orientation(orientation),
    m_length(qMax(0, length)),
    m_padding(qMax(0, padding)),
    m_color(color)
{
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    setFocusPolicy(Qt::NoFocus);
    if (orientation == Qt::Horizontal)
    {
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }
    else
    {
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    }
    connect(PDFThemeManager::getInstance(), &PDFThemeManager::themeChanged, this, qOverload<>(&PDFAppToolbarDivider::update));
}

QSize PDFAppToolbarDivider::sizeHint() const
{
    if (m_orientation == Qt::Horizontal)
    {
        return QSize(m_length + 2 * m_padding, 1);
    }
    return QSize(1 + 2 * m_padding, m_length);
}

QSize PDFAppToolbarDivider::minimumSizeHint() const
{
    return sizeHint();
}

void PDFAppToolbarDivider::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    const QColor color = PDFThemeManager::getInstance()->getColor(m_color);
    if (m_orientation == Qt::Horizontal)
    {
        painter.fillRect(QRect(m_padding, (height() - 1) / 2, qMax(0, width() - 2 * m_padding), 1), color);
    }
    else
    {
        const int length = m_length > 0 ? qMin(m_length, height()) : height();
        painter.fillRect(QRect(m_padding, (height() - length) / 2, 1, length), color);
    }
}

// ----------------------------------------------------------- PDFAppDocumentToolbar

PDFAppDocumentToolbar::PDFAppDocumentToolbar(PDFAppDocumentView* view, QWidget* parent) :
    BaseClass(parent),
    m_view(view)
{
    using C = PDFAppCommands;
    using H = PDFAppDocumentToolbarHelper;

    setObjectName(QStringLiteral("documentToolbar"));
    setFixedHeight(HEIGHT);
    setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);

    // toolbar 193:785: paddings 12, gap 4 between the groups
    m_layout = new QHBoxLayout(this);
    m_layout->setContentsMargins(12, 0, 12, 0);
    m_layout->setSpacing(4);

    // toolbar-left 193:786: buttons 2 px apart, dividers 13 x 20
    m_leftGroup = new QWidget(this);
    m_leftGroup->setObjectName(QStringLiteral("toolbarLeft"));
    QHBoxLayout* leftLayout = H::createGroup(m_leftGroup, 2);
    leftLayout->addWidget(createCommandButton(C::toggleSidebar(), QStringLiteral("panel-left")));
    leftLayout->addWidget(new PDFAppToolbarDivider(Qt::Vertical, 20, 6, PDFThemeColors::BorderSeparator, m_leftGroup));
    leftLayout->addWidget(createCommandButton(C::save(), QStringLiteral("save")));
    leftLayout->addWidget(createCommandButton(C::undo(), QStringLiteral("undo")));
    leftLayout->addWidget(createCommandButton(C::redo(), QStringLiteral("redo")));
    leftLayout->addWidget(new PDFAppToolbarDivider(Qt::Vertical, 20, 6, PDFThemeColors::BorderSeparator, m_leftGroup));
    m_fileButton = createMenuButton(QStringLiteral("menuFile"), QStringLiteral("file"));
    m_viewButton = createMenuButton(QStringLiteral("menuView"), QStringLiteral("eye"));
    m_annotateButton = createMenuButton(QStringLiteral("menuAnnotate"), QStringLiteral("highlighter"));
    m_insertButton = createMenuButton(QStringLiteral("menuInsert"), QStringLiteral("plus-circle"));
    m_goToButton = createMenuButton(QStringLiteral("menuGoTo"), QStringLiteral("compass"));
    m_toolsButton = createMenuButton(QStringLiteral("menuTools"), QStringLiteral("tools"));
    for (QToolButton* button : { m_fileButton.data(), m_viewButton.data(), m_annotateButton.data(), m_insertButton.data(), m_goToButton.data(), m_toolsButton.data() })
    {
        leftLayout->addWidget(button);
    }

    // Annotate and Insert are the annotations of step 4; Tools opens the catalog of the tools
    m_annotateButton->setCheckable(false);
    m_annotateButton->setEnabled(false);
    m_insertButton->setCheckable(false);
    m_insertButton->setEnabled(false);
    m_toolsButton->setCheckable(false);
    disconnect(m_toolsButton.data(), &QToolButton::clicked, this, &PDFAppDocumentToolbar::onMenuButtonClicked);
    connect(m_toolsButton.data(), &QToolButton::clicked, this, &PDFAppDocumentToolbar::onToolsClicked);
    m_layout->addWidget(m_leftGroup);
    m_layout->addStretch(1);

    // toolbar-center 193:840: the page navigation and the zoom, 12 px apart
    QWidget* centerGroup = new QWidget(this);
    centerGroup->setObjectName(QStringLiteral("toolbarCenter"));
    QHBoxLayout* centerLayout = H::createGroup(centerGroup, 12);

    m_pageGroup = new QWidget(centerGroup);
    m_pageGroup->setObjectName(QStringLiteral("pageNavigation"));
    QHBoxLayout* pageLayout = H::createGroup(m_pageGroup, 2);
    pageLayout->addWidget(createCommandButton(C::previousPage(), QStringLiteral("chevron-left")));
    m_pageField = new PDFToolBarField(m_pageGroup);
    m_pageField->setObjectName(QStringLiteral("pageField"));
    m_pageField->setMaxLength(256);
    connect(m_pageField.data(), &QLineEdit::returnPressed, this, &PDFAppDocumentToolbar::onPageFieldEdited);
    connect(m_pageField.data(), &QLineEdit::editingFinished, this, &PDFAppDocumentToolbar::updatePageField);
    pageLayout->addWidget(m_pageField);
    pageLayout->addWidget(createCommandButton(C::nextPage(), QStringLiteral("chevron-right")));
    centerLayout->addWidget(m_pageGroup);

    m_zoomGroup = new QWidget(centerGroup);
    m_zoomGroup->setObjectName(QStringLiteral("zoom"));
    QHBoxLayout* zoomLayout = H::createGroup(m_zoomGroup, 2);
    zoomLayout->addWidget(createCommandButton(C::zoomOut(), QStringLiteral("minus")));
    m_zoomField = new PDFToolBarField(m_zoomGroup);
    m_zoomField->setObjectName(QStringLiteral("zoomField"));
    m_zoomField->setSuffixVisible(false);
    m_zoomField->setMaxLength(16);
    connect(m_zoomField.data(), &QLineEdit::returnPressed, this, &PDFAppDocumentToolbar::onZoomFieldEdited);
    connect(m_zoomField.data(), &QLineEdit::editingFinished, this, &PDFAppDocumentToolbar::updateZoomField);
    zoomLayout->addWidget(m_zoomField);
    zoomLayout->addWidget(createCommandButton(C::zoomIn(), QStringLiteral("plus")));
    centerLayout->addWidget(m_zoomGroup);
    m_layout->addWidget(centerGroup);
    m_layout->addStretch(1);

    // toolbar-right 193:870: the Select button with its menu and the search field, 8 px apart
    m_rightGroup = new QWidget(this);
    m_rightGroup->setObjectName(QStringLiteral("toolbarRight"));
    QHBoxLayout* rightLayout = H::createGroup(m_rightGroup, 8);
    m_selectButton = createButton(QStringLiteral("selectButton"), QStringLiteral("text-cursor"));
    m_selectMenu = new PDFAppDocumentSelectMenu(view, m_selectButton);
    m_selectButton->setMenu(m_selectMenu);
    m_selectButton->setPopupMode(QToolButton::InstantPopup);
    rightLayout->addWidget(m_selectButton);

    // The search of package D: the field only, D connects its behavior
    m_searchField = new PDFLineEdit(m_rightGroup);
    m_searchField->setObjectName(QStringLiteral("documentSearchField"));
    m_searchField->setSearchField();
    m_searchField->setFixedWidth(SEARCH_FIELD_WIDTH);
    m_searchField->setMaxLength(1024);
    rightLayout->addWidget(m_searchField);
    m_layout->addWidget(m_rightGroup);

    connect(PDFThemeManager::getInstance(), &PDFThemeManager::themeChanged, this, qOverload<>(&PDFAppDocumentToolbar::update));
    if (view)
    {
        connect(view, &PDFAppDocumentView::commandStatesChanged, this, &PDFAppDocumentToolbar::updateState);
        connect(view, &PDFAppDocumentView::viewChanged, this, &PDFAppDocumentToolbar::onViewChanged);
        connect(view, &PDFAppDocumentView::currentPageChanged, this, &PDFAppDocumentToolbar::onViewChanged);
    }

    retranslate();
    updateState();
}

PDFAppDocumentToolbar::~PDFAppDocumentToolbar()
{
    // The panels close while the toolbar is whole (they are popup windows)
    for (const auto& item : m_panels)
    {
        delete item.second.data();
    }
    m_panels.clear();
}

QToolButton* PDFAppDocumentToolbar::createButton(const QString& objectName, const QString& iconName)
{
    // Toolbar button 190:538: 36 x 36, icon 20, no background at rest
    QToolButton* button = new QToolButton(this);
    button->setObjectName(objectName);
    button->setAutoRaise(true);
    button->setIcon(PDFThemeIcons::getIcon(iconName));
    button->setIconSize(QSize(20, 20));
    button->setFocusPolicy(Qt::TabFocus);
    return button;
}

QToolButton* PDFAppDocumentToolbar::createCommandButton(const QString& commandId, const QString& iconName)
{
    QToolButton* button = createButton(commandId, iconName);
    button->setProperty(PDFAppDocumentToolbarHelper::COMMAND_PROPERTY, commandId);
    connect(button, &QToolButton::clicked, this, &PDFAppDocumentToolbar::onCommandButtonClicked);
    m_commandButtons[commandId] = button;
    return button;
}

QToolButton* PDFAppDocumentToolbar::createMenuButton(const QString& objectName, const QString& iconName)
{
    // Toolbar menu button 190:577: icon 18 and the label Medium 13; Open = checked
    QToolButton* button = createButton(objectName, iconName);
    button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    button->setIconSize(QSize(18, 18));
    button->setCheckable(true);
    connect(button, &QToolButton::clicked, this, &PDFAppDocumentToolbar::onMenuButtonClicked);
    m_menuButtons.emplace_back(button);
    return button;
}

void PDFAppDocumentToolbar::retranslate()
{
    const std::pair<QToolButton*, QString> menuTexts[] =
    {
        { m_fileButton.data(), tr("File") },
        { m_viewButton.data(), tr("View") },
        { m_annotateButton.data(), tr("Annotate") },
        { m_insertButton.data(), tr("Insert") },
        { m_goToButton.data(), tr("Go to") },
        { m_toolsButton.data(), tr("Tools") }
    };
    for (const std::pair<QToolButton*, QString>& item : menuTexts)
    {
        if (item.first)
        {
            item.first->setText(item.second);
            item.first->setToolTip(item.second);
            item.first->setAccessibleName(item.second);
        }
    }
    if (m_annotateButton)
    {
        m_annotateButton->setToolTip(tr("Annotations are available in a later version"));
    }
    if (m_insertButton)
    {
        m_insertButton->setToolTip(tr("Inserting is available in a later version"));
    }

    // Icon buttons: the title of the command and its shortcut
    const PDFAppContext* context = m_view ? m_view->getContext() : nullptr;
    const PDFAppCommandRegistry* registry = context ? context->getCommandRegistry() : nullptr;
    for (const auto& item : m_commandButtons)
    {
        QToolButton* button = item.second.data();
        const PDFAppCommand* command = registry ? registry->findCommand(item.first) : nullptr;
        if (!button || !command)
        {
            continue;
        }

        const QString title = command->getTitle();
        const QKeySequence shortcut = command->defaultShortcut;
        button->setAccessibleName(title);
        button->setToolTip(shortcut.isEmpty() ? title : tr("%1 (%2)").arg(title, shortcut.toString(QKeySequence::NativeText)));
    }

    if (m_selectButton)
    {
        m_selectButton->setToolTip(tr("Select"));
        m_selectButton->setAccessibleName(tr("Select"));
    }
    if (m_searchField)
    {
        m_searchField->setPlaceholderText(tr("Search in document…"));
        m_searchField->setAccessibleName(tr("Search in document"));
    }
    if (m_pageField)
    {
        m_pageField->setAccessibleName(tr("Page"));
    }
    if (m_zoomField)
    {
        m_zoomField->setAccessibleName(tr("Zoom"));
    }

    updatePageField();
    updateZoomField();
}

PDFAppDocumentView* PDFAppDocumentToolbar::getView() const
{
    return m_view.data();
}

PDFLineEdit* PDFAppDocumentToolbar::getSearchField() const
{
    return m_searchField.data();
}

PDFToolBarField* PDFAppDocumentToolbar::getPageField() const
{
    return m_pageField.data();
}

PDFToolBarField* PDFAppDocumentToolbar::getZoomField() const
{
    return m_zoomField.data();
}

QToolButton* PDFAppDocumentToolbar::getCommandButton(const QString& commandId) const
{
    auto it = m_commandButtons.find(commandId);
    return it != m_commandButtons.end() ? it->second.data() : nullptr;
}

QToolButton* PDFAppDocumentToolbar::getMenuButton(Panel panel) const
{
    switch (panel)
    {
        case Panel::File:
            return m_fileButton.data();
        case Panel::View:
            return m_viewButton.data();
        case Panel::GoTo:
            return m_goToButton.data();
    }
    return nullptr;
}

QToolButton* PDFAppDocumentToolbar::getAnnotateButton() const
{
    return m_annotateButton.data();
}

QToolButton* PDFAppDocumentToolbar::getInsertButton() const
{
    return m_insertButton.data();
}

QToolButton* PDFAppDocumentToolbar::getToolsButton() const
{
    return m_toolsButton.data();
}

QToolButton* PDFAppDocumentToolbar::getSelectButton() const
{
    return m_selectButton.data();
}

PDFAppDocumentSelectMenu* PDFAppDocumentToolbar::getSelectMenu() const
{
    return m_selectMenu.data();
}

PDFAppDocumentPanelPopup* PDFAppDocumentToolbar::findPanel(Panel panel) const
{
    auto it = m_panels.find(panel);
    return it != m_panels.end() ? it->second.data() : nullptr;
}

PDFAppDocumentPanelPopup* PDFAppDocumentToolbar::getOpenPanel() const
{
    for (const auto& item : m_panels)
    {
        if (item.second && item.second->isVisible())
        {
            return item.second.data();
        }
    }
    return nullptr;
}

PDFAppDocumentPanelPopup* PDFAppDocumentToolbar::openPanel(Panel panel)
{
    QToolButton* button = getMenuButton(panel);
    if (!button || !button->isVisible() || !button->isEnabled() || !isVisible())
    {
        return nullptr;
    }

    // One panel at a time
    if (PDFAppDocumentPanelPopup* openPanel = getOpenPanel())
    {
        openPanel->close();
    }

    QPointer<PDFAppDocumentPanelPopup>& popup = m_panels[panel];
    if (!popup)
    {
        switch (panel)
        {
            case Panel::File:
                popup = new PDFAppDocumentFilePanel(m_view, this);
                break;
            case Panel::View:
                popup = new PDFAppDocumentViewPanel(m_view, this);
                break;
            case Panel::GoTo:
                popup = new PDFAppDocumentGoToPanel(m_view, this);
                break;
        }
        connect(popup.data(), &PDFAppDocumentPanelPopup::closed, this, &PDFAppDocumentToolbar::onPanelClosed);
    }

    // The panel opens 4 px below the toolbar (y 94 of the frames)
    QPointer<PDFAppDocumentPanelPopup> guard(popup.data());
    QPointer<QToolButton> buttonGuard(button);
    button->setChecked(true);
    popup->popup(button, mapToGlobal(QPoint(0, height())).y() + PANEL_GAP);
    if ((!guard || !guard->isVisible()) && buttonGuard)
    {
        buttonGuard->setChecked(false);
    }
    return guard.data();
}

void PDFAppDocumentToolbar::onMenuButtonClicked()
{
    // The menu buttons do not identify themselves in the signal
    QToolButton* button = qobject_cast<QToolButton*>(sender());
    if (!button)
    {
        return;
    }

    for (Panel panel : { Panel::File, Panel::View, Panel::GoTo })
    {
        if (getMenuButton(panel) != button)
        {
            continue;
        }

        PDFAppDocumentPanelPopup* popup = findPanel(panel);
        if (popup && popup->isVisible())
        {
            popup->close();
            button->setChecked(false);
        }
        else
        {
            openPanel(panel);
        }
        return;
    }
}

void PDFAppDocumentToolbar::onPanelClosed()
{
    // The buttons of the closed panels are not checked
    for (Panel panel : { Panel::File, Panel::View, Panel::GoTo })
    {
        QToolButton* button = getMenuButton(panel);
        PDFAppDocumentPanelPopup* popup = findPanel(panel);
        if (button)
        {
            button->setChecked(popup && popup->isVisible());
        }
    }
}

void PDFAppDocumentToolbar::onCommandButtonClicked()
{
    // The buttons do not identify themselves in the signal; the command runs from
    // the event loop (it may delete the tab together with the toolbar)
    QToolButton* button = qobject_cast<QToolButton*>(sender());
    if (button && m_view)
    {
        m_view->requestCommand(button->property(PDFAppDocumentToolbarHelper::COMMAND_PROPERTY).toString());
    }
}

void PDFAppDocumentToolbar::onToolsClicked()
{
    // The catalog of the tools (phase 2 adds the tools of the document)
    PDFAppMainWindow* mainWindow = qobject_cast<PDFAppMainWindow*>(window());
    if (mainWindow && mainWindow->getNavigator())
    {
        mainWindow->getNavigator()->navigate(PDFAppRoutes::tools());
    }
}

void PDFAppDocumentToolbar::updateState()
{
    PDFAppDocumentView* view = m_view.data();
    for (const auto& item : m_commandButtons)
    {
        if (QToolButton* button = item.second.data())
        {
            button->setEnabled(view && view->isCommandEnabled(item.first));
        }
    }

    // The panels and the tools need a shown document
    const PDFAppDocument* document = view ? view->getDocument() : nullptr;
    const bool isReady = document && document->isReady();
    for (QToolButton* button : { m_fileButton.data(), m_viewButton.data(), m_goToButton.data(), m_selectButton.data() })
    {
        if (button)
        {
            button->setEnabled(isReady);
        }
    }
    if (m_pageField)
    {
        m_pageField->setEnabled(isReady && view->getPageCount() > 0);
    }
    if (m_zoomField)
    {
        m_zoomField->setEnabled(isReady && view->getPageCount() > 0);
    }

    updateSelectButton();
    updatePageField();
    updateZoomField();
}

void PDFAppDocumentToolbar::onViewChanged()
{
    updatePageField();
    updateZoomField();
    updateSelectButton();
}

void PDFAppDocumentToolbar::updateSelectButton()
{
    if (m_selectButton)
    {
        m_selectButton->setIcon(PDFThemeIcons::getIcon(PDFAppDocumentToolbarHelper::getSelectIconName(m_view)));
    }
    if (m_selectMenu && m_selectMenu->isVisible())
    {
        m_selectMenu->updateState();
    }
}

void PDFAppDocumentToolbar::updatePageField()
{
    if (!m_pageField || m_pageField->hasFocus())
    {
        // The text of the user is not replaced while it is typed
        return;
    }

    const PDFAppDocumentView* view = m_view.data();
    const PDFInteger pageCount = view ? view->getPageCount() : 0;
    const PDFInteger pageIndex = view ? view->getCurrentPageIndex() : -1;
    if (pageCount <= 0 || pageIndex < 0)
    {
        m_pageField->setText(QString());
        m_pageField->setSuffix(QString());
        return;
    }

    // page-field 193:847: "308" with the suffix "/ 1592"; a document with page
    // labels shows the label and the number of the page in the suffix
    const QLocale locale;
    m_pageField->setText(view->getPageLabel(pageIndex));
    if (view->hasPageLabels())
    {
        m_pageField->setSuffix(tr("(%1 / %2)").arg(locale.toString(pageIndex + 1), locale.toString(pageCount)));
    }
    else
    {
        m_pageField->setSuffix(tr("/ %1").arg(locale.toString(pageCount)));
    }
}

void PDFAppDocumentToolbar::updateZoomField()
{
    if (!m_zoomField || m_zoomField->hasFocus())
    {
        return;
    }

    const PDFAppDocumentView* view = m_view.data();
    if (!view || view->getPageCount() <= 0)
    {
        m_zoomField->setText(QString());
        return;
    }

    // zoom-field 193:861: "151 %"
    m_zoomField->setText(tr("%1 %").arg(QLocale().toString(qRound(view->getZoom() * 100.0))));
}

void PDFAppDocumentToolbar::onPageFieldEdited()
{
    if (!m_pageField || !m_view)
    {
        return;
    }

    const std::optional<PDFInteger> pageIndex = m_view->findPageIndex(m_pageField->text());
    if (!pageIndex)
    {
        // An unknown page keeps the field with the text of the user marked invalid
        m_pageField->setInvalid(true, tr("The document has no page “%1”.").arg(m_pageField->text().trimmed().left(64)));
        m_pageField->selectAll();
        return;
    }

    m_pageField->setInvalid(false);
    m_view->goToPage(*pageIndex);
    m_pageField->clearFocus();
    updatePageField();
}

void PDFAppDocumentToolbar::onZoomFieldEdited()
{
    if (!m_zoomField || !m_view)
    {
        return;
    }

    // "151 %", "151%", "151", "75,5" (the decimal separator of the locale too)
    QString text = m_zoomField->text().trimmed();
    text.remove(QLatin1Char('%'));
    text = text.trimmed();

    bool ok = false;
    double percent = QLocale().toDouble(text, &ok);
    if (!ok)
    {
        percent = QLocale::c().toDouble(text, &ok);
    }

    if (!ok || !std::isfinite(percent) || percent <= 0.0)
    {
        m_zoomField->setInvalid(true, tr("Type the zoom in percent, for example 125."));
        m_zoomField->selectAll();
        return;
    }

    m_zoomField->setInvalid(false);
    m_view->setZoom(percent / 100.0);
    m_zoomField->clearFocus();
    updateZoomField();
}

void PDFAppDocumentToolbar::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    // bg/subtle with the border border/default at the bottom (toolbar 193:785)
    QPainter painter(this);
    const PDFThemeManager* manager = PDFThemeManager::getInstance();
    painter.fillRect(rect(), manager->getColor(PDFThemeColors::BgSubtle));
    painter.fillRect(QRect(0, height() - 1, width(), 1), manager->getColor(PDFThemeColors::BorderDefault));
}

void PDFAppDocumentToolbar::resizeEvent(QResizeEvent* event)
{
    BaseClass::resizeEvent(event);
    updateCollapse();
}

void PDFAppDocumentToolbar::changeEvent(QEvent* event)
{
    BaseClass::changeEvent(event);

    switch (event->type())
    {
        case QEvent::LanguageChange:
            retranslate();
            updateCollapse();
            break;

        case QEvent::FontChange:
        case QEvent::StyleChange:
            updateCollapse();
            break;

        default:
            break;
    }
}

QSize PDFAppDocumentToolbar::minimumSizeHint() const
{
    // The toolbar narrows itself, it never makes the window wider
    return QSize(0, HEIGHT);
}

void PDFAppDocumentToolbar::updateCollapse()
{
    if (m_isUpdatingCollapse || !m_layout)
    {
        return;
    }

    m_isUpdatingCollapse = true;

    // The first level, at which the content fits into the width of the toolbar
    int level = PDFAppDocumentToolbarHelper::Full;
    for (; level < PDFAppDocumentToolbarHelper::LastLevel; ++level)
    {
        applyCollapse(level);
        m_layout->invalidate();
        if (m_layout->sizeHint().width() <= width())
        {
            break;
        }
    }
    applyCollapse(level);
    m_collapseLevel = level;
    m_isUpdatingCollapse = false;
}

void PDFAppDocumentToolbar::applyCollapse(int level)
{
    using H = PDFAppDocumentToolbarHelper;

    for (const QPointer<QToolButton>& button : m_menuButtons)
    {
        if (button)
        {
            button->setToolButtonStyle(level >= H::MenuIconsOnly ? Qt::ToolButtonIconOnly : Qt::ToolButtonTextBesideIcon);
            button->setIconSize(level >= H::MenuIconsOnly ? QSize(20, 20) : QSize(18, 18));
        }
    }

    if (m_searchField)
    {
        int searchWidth = SEARCH_FIELD_WIDTH;
        if (level >= H::SearchMinimum)
        {
            searchWidth = SEARCH_FIELD_MINIMUM_WIDTH;
        }
        else if (level >= H::SearchNarrow)
        {
            searchWidth = SEARCH_FIELD_NARROW_WIDTH;
        }
        m_searchField->setFixedWidth(searchWidth);
    }

    if (m_zoomGroup)
    {
        m_zoomGroup->setVisible(level < H::ZoomHidden);
    }
    if (m_annotateButton)
    {
        m_annotateButton->setVisible(level < H::AnnotateInsertHidden);
    }
    if (m_insertButton)
    {
        m_insertButton->setVisible(level < H::AnnotateInsertHidden);
    }
    if (m_pageGroup)
    {
        m_pageGroup->setVisible(level < H::PageNavigationHidden);
    }
}

}   // namespace pdf
