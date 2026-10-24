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

#include "pdfwidgetstyleimpl.h"
#include "pdfthemeiconengine.h"
#include "pdfthememanager.h"

#include <QAbstractItemView>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QEvent>
#include <QFocusFrame>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QHeaderView>
#include <QPen>
#include <QStyleOptionSlider>
#include <QTabBar>
#include <QTabWidget>
#include <QTextCharFormat>
#include <QToolBar>
#include <QWizard>

namespace pdf
{

bool PDFWidgetStyleMetrics::pixelMetric(const PDFWidgetStyleImpl* impl, QStyle::PixelMetric metric, const QStyleOption* option, const QWidget* widget, int& value)
{
    Q_UNUSED(impl);

    switch (metric)
    {
        case QStyle::PM_DefaultFrameWidth:
            // A popup of a combo box has its frame in the margins of its shadow
            value = PDFWidgetStyleImpl::inherits(widget, "QComboBoxPrivateContainer") ? 0 : 1;

            if (PDFWidgetStyleImpl::inherits(widget, "QComboBox"))
            {
                // The delegate of a combo box makes its separators higher by 4 px than this value
                value = 3;
            }
            return true;

        case QStyle::PM_SpinBoxFrameWidth:
        case QStyle::PM_ComboBoxFrameWidth:
        case QStyle::PM_MenuPanelWidth:
        case QStyle::PM_TextCursorWidth:
            value = 1;
            return true;

        case QStyle::PM_ToolTipLabelFrameWidth:
            // The tooltip of Qt has the same margin at all sides (this width
            // and 1 px): border 1 and the vertical padding 6 of the design
            value = (widget && widget->inherits("QTipLabel")) ? 6 : 1;
            return true;

        case QStyle::PM_ButtonMargin:
            value = 16;
            return true;

        case QStyle::PM_ButtonDefaultIndicator:
        case QStyle::PM_ButtonShiftHorizontal:
        case QStyle::PM_ButtonShiftVertical:
        case QStyle::PM_MenuTearoffHeight:
        case QStyle::PM_MenuDesktopFrameWidth:
        case QStyle::PM_MenuBarPanelWidth:
        case QStyle::PM_ToolBarFrameWidth:
        case QStyle::PM_ToolBarItemMargin:
        case QStyle::PM_TabBarTabShiftHorizontal:
        case QStyle::PM_TabBarTabShiftVertical:
        case QStyle::PM_TabBar_ScrollButtonOverlap:
        case QStyle::PM_TabBarBaseHeight:
        case QStyle::PM_TabBarBaseOverlap:
        case QStyle::PM_TabBarTabOverlap:
        case QStyle::PM_TabBarTabVSpace:
        case QStyle::PM_ScrollView_ScrollBarSpacing:
        case QStyle::PM_DockWidgetHandleExtent:
        case QStyle::PM_MdiSubWindowFrameWidth:
        case QStyle::PM_MdiSubWindowMinimizedWidth:
        case QStyle::PM_FocusFrameVMargin:
        case QStyle::PM_FocusFrameHMargin:
        {
            value = 0;

            // The margin of the focus frame also sets margins of texts in views,
            // so it is returned only for the frame itself.
            if ((metric == QStyle::PM_FocusFrameVMargin || metric == QStyle::PM_FocusFrameHMargin) && qobject_cast<const QFocusFrame*>(widget))
            {
                value = PDFWidgetStyleImpl::getFocusFrameMargin(widget);
            }
            return true;
        }

        case QStyle::PM_MenuButtonIndicator:
            value = 14;
            return true;

        case QStyle::PM_ScrollView_ScrollBarOverlap:
            // The scroll bars of an area with the property overlayScrollBars lie over
            // the edge of its viewport (QAbstractScrollArea does not make the viewport
            // smaller by an overlapping scroll bar); a scroll bar draws only its groove
            // and its slider, so the content below its gutters stays visible
            value = PDFWidgetStyleImpl::isOverlayScrollBar(widget) ? SCROLL_BAR_EXTENT : 0;
            return true;

        case QStyle::PM_TreeViewIndentation:
            // A level of the outline is indented by 18 px (Outline row), of the contents of the manual by 24 px
        {
            const QString rowStyle = widget ? widget->property(PDFWidgetStyleProperties::ROW_STYLE).toString() : QString();
            value = 16;
            if (rowStyle == QLatin1String("outline"))
            {
                value = 18;
            }
            else if (rowStyle == QLatin1String("contents"))
            {
                value = 24;
            }
            return true;
        }

        case QStyle::PM_ButtonIconSize:
        case QStyle::PM_SmallIconSize:
        case QStyle::PM_ListViewIconSize:
        case QStyle::PM_LineEditIconSize:
        case QStyle::PM_SizeGripSize:
            value = 16;
            return true;

        case QStyle::PM_LineEditIconMargin:
            // Distance of the icon from the edge of the field: border 1 + padding 12
            // (16 in a pill), the button is 3 px wider than its icon at each side
            value = PDFWidgetStyleImpl::hasProperty(widget, PDFWidgetStyleProperties::PILL) ? 13 : 10;
            return true;

        case QStyle::PM_TabBarIconSize:
        {
            // Chips and document tabs 14, rows of a vertical tab bar 20
            value = 14;
            if (const QTabBar* tabBar = qobject_cast<const QTabBar*>(widget))
            {
                const QTabBar::Shape shape = tabBar->shape();
                const bool isVertical = shape == QTabBar::RoundedWest || shape == QTabBar::RoundedEast || shape == QTabBar::TriangularWest || shape == QTabBar::TriangularEast;
                if (isVertical)
                {
                    value = 20;
                }
            }
            return true;
        }

        case QStyle::PM_ToolBarIconSize:
            value = PDFWidgetStyleImpl::hasProperty(widget, PDFWidgetStyleProperties::SIDEBAR_TABS) ? 18 : 20;
            return true;

        case QStyle::PM_LargeIconSize:
        case QStyle::PM_IconViewIconSize:
        case QStyle::PM_ScrollBarSliderMin:
            value = 32;
            return true;

        case QStyle::PM_TitleBarHeight:
            // The design does not use title bars drawn by the style, but the
            // search tool of the document view draws one (CC_TitleBar)
            value = 32;
            return true;

        case QStyle::PM_MessageBoxIconSize:
        case QStyle::PM_ToolBarExtensionExtent:
            value = 36;
            return true;

        case QStyle::PM_IndicatorWidth:
        case QStyle::PM_IndicatorHeight:
        case QStyle::PM_ExclusiveIndicatorWidth:
        case QStyle::PM_ExclusiveIndicatorHeight:
            value = 18;
            return true;

        case QStyle::PM_MenuBarHMargin:
            // The first item of the menu bar starts at 8 px: the menu bar adds
            // the spacing of the items (2) also before the first item
            value = 6;
            return true;

        case QStyle::PM_CheckBoxLabelSpacing:
        case QStyle::PM_RadioButtonLabelSpacing:
        case QStyle::PM_LayoutHorizontalSpacing:
        case QStyle::PM_LayoutVerticalSpacing:
            value = 8;
            return true;

        case QStyle::PM_MenuHMargin:
        case QStyle::PM_MenuVMargin:
        case QStyle::PM_SplitterWidth:
        case QStyle::PM_DockWidgetSeparatorExtent:
            value = 6;
            return true;

        case QStyle::PM_MenuScrollerHeight:
        case QStyle::PM_SliderThickness:
        case QStyle::PM_TabCloseIndicatorWidth:
        case QStyle::PM_TabCloseIndicatorHeight:
            value = 20;
            return true;

        case QStyle::PM_SubMenuOverlap:
        {
            // The window of a submenu has transparent margins with its shadow. Its
            // panel overlaps the panel of the parent menu by its padding and border.
            // The submenu is placed at the right edge of the last item (which ends
            // 7 px before the edge of the panel) and its panel starts after its margin.
            const QMargins margins = PDFWidgetStyleImpl::getPopupMargins(widget);
            value = 7 - margins.left();
            return true;
        }

        case QStyle::PM_MenuBarItemSpacing:
        case QStyle::PM_ToolBarItemSpacing:
            value = 2;
            return true;

        case QStyle::PM_MenuBarVMargin:
        case QStyle::PM_DockWidgetTitleBarButtonMargin:
        case QStyle::PM_SliderTickmarkOffset:
            value = 4;
            return true;

        case QStyle::PM_ToolBarHandleExtent:
        {
            value = 0;
            if (const QToolBar* toolBar = qobject_cast<const QToolBar*>(widget))
            {
                value = toolBar->isMovable() && !PDFWidgetStyleImpl::hasProperty(widget, PDFWidgetStyleProperties::SIDEBAR_TABS) ? 10 : 0;
            }
            return true;
        }

        case QStyle::PM_ToolBarSeparatorExtent:
            value = 13;
            return true;

        case QStyle::PM_TabBarTabHSpace:
        case QStyle::PM_TabBarScrollButtonWidth:
            value = 28;
            return true;

        case QStyle::PM_HeaderMargin:
        {
            // The header view derives the minimal size of a section from this
            // margin (height of the font and two margins). Rows of a vertical
            // header are 30 px high, which the margin of 12 px would not allow.
            const QHeaderView* headerView = qobject_cast<const QHeaderView*>(widget);
            value = headerView && headerView->orientation() == Qt::Vertical ? 7 : 12;
            return true;
        }

        case QStyle::PM_ScrollBarExtent:
            value = SCROLL_BAR_EXTENT;
            return true;

        case QStyle::PM_DockWidgetTitleMargin:
        case QStyle::PM_HeaderMarkSize:
            value = 12;
            return true;

        case QStyle::PM_SliderControlThickness:
        case QStyle::PM_SliderLength:
            value = 14;
            return true;

        case QStyle::PM_SliderSpaceAvailable:
        {
            if (const QStyleOptionSlider* slider = qstyleoption_cast<const QStyleOptionSlider*>(option))
            {
                value = (slider->orientation == Qt::Horizontal ? slider->rect.width() : slider->rect.height()) - 14;
                return true;
            }
            return false;
        }

        case QStyle::PM_ProgressBarChunkWidth:
            value = 1;
            return true;

        case QStyle::PM_DockWidgetFrameWidth:
        {
            const QDockWidget* dockWidget = qobject_cast<const QDockWidget*>(widget);
            value = dockWidget && dockWidget->isFloating() ? 1 : 0;
            return true;
        }

        case QStyle::PM_HeaderGripMargin:
            value = 3;
            return true;

        case QStyle::PM_HeaderDefaultSectionSizeHorizontal:
            value = 120;
            return true;

        case QStyle::PM_HeaderDefaultSectionSizeVertical:
            value = 30;
            return true;

        case QStyle::PM_LayoutLeftMargin:
        case QStyle::PM_LayoutTopMargin:
        case QStyle::PM_LayoutRightMargin:
        case QStyle::PM_LayoutBottomMargin:
        {
            // 16 for windows and dialogs, 12 for layouts of child widgets
            value = (!widget || widget->isWindow()) ? 16 : 12;

            if (const QGroupBox* groupBox = qobject_cast<const QGroupBox*>(widget))
            {
                if (PDFWidgetStyleImpl::hasProperty(widget, PDFWidgetStyleProperties::CARD))
                {
                    // Rows of a card have their own paddings
                    value = 0;
                }
                else if (groupBox->isFlat())
                {
                    // The content of a flat group box starts 12 px below its line
                    value = metric == QStyle::PM_LayoutTopMargin ? 12 : 0;
                }
            }
            return true;
        }

        default:
            break;
    }

    return false;
}

bool PDFWidgetStyleMetrics::styleHint(const PDFWidgetStyleImpl* impl, QStyle::StyleHint hint, const QStyleOption* option, const QWidget* widget, QStyleHintReturn* returnData, int& value)
{
    Q_UNUSED(option);

    switch (hint)
    {
        case QStyle::SH_UnderlineShortcut:
            value = impl->isMnemonicVisible(widget) ? 1 : 0;
            return true;

        case QStyle::SH_DialogButtonLayout:
            value = QDialogButtonBox::MacLayout;
            return true;

        // Hints with the value 0
        case QStyle::SH_DialogButtonBox_ButtonsHaveIcons:
        case QStyle::SH_FocusFrame_Mask:
        case QStyle::SH_ItemView_ActivateItemOnSingleClick:
        case QStyle::SH_ItemView_DrawDelegateFrame:
        case QStyle::SH_ComboBox_Popup:
        case QStyle::SH_ComboBox_AllowWheelScrolling:
        case QStyle::SH_Menu_FillScreenWithScroll:
        case QStyle::SH_Menu_FadeOutOnHide:
        case QStyle::SH_MainWindow_SpaceBelowMenuBar:
        case QStyle::SH_ScrollBar_Transient:
        case QStyle::SH_ScrollBar_ContextMenu:
        case QStyle::SH_ScrollBar_RollBetweenButtons:
        case QStyle::SH_TabBar_AllowWheelScrolling:
        case QStyle::SH_ToolTip_Mask:
        case QStyle::SH_ToolBox_SelectedPageTitleBold:
        case QStyle::SH_MessageBox_CenterButtons:
        case QStyle::SH_MessageBox_UseBorderForButtonSpacing:
        case QStyle::SH_ProgressDialog_CenterCancelButton:
        case QStyle::SH_LineEdit_PasswordMaskDelay:
        case QStyle::SH_Widget_ShareActivation:
        case QStyle::SH_Workspace_FillSpaceOnMaximize:
        case QStyle::SH_DockWidget_ButtonsHaveFrame:
        case QStyle::SH_EtchDisabledText:
        case QStyle::SH_DitherDisabledText:
        case QStyle::SH_WindowFrame_Mask:
        case QStyle::SH_Menu_Mask:
        case QStyle::SH_RubberBand_Mask:
            value = 0;
            return true;

        // Hints with the value 1
        case QStyle::SH_FocusFrame_AboveWidget:
        case QStyle::SH_ItemView_ShowDecorationSelected:
        case QStyle::SH_ItemView_ChangeHighlightOnFocus:
        case QStyle::SH_ItemView_ArrowKeysNavigateIntoChildren:
        case QStyle::SH_ItemView_PaintAlternatingRowColorsForEmptyArea:
        case QStyle::SH_ComboBox_ListMouseTracking:
        case QStyle::SH_SpinBox_AnimateButton:
        case QStyle::SH_SpinBox_ButtonsInsideFrame:
        case QStyle::SH_Menu_MouseTracking:
        case QStyle::SH_Menu_Scrollable:
        case QStyle::SH_Menu_SubMenuUniDirection:
        case QStyle::SH_Menu_SelectionWrap:
        case QStyle::SH_Menu_KeyboardSearch:
        case QStyle::SH_Menu_SpaceActivatesItem:
        case QStyle::SH_Menu_SupportsSections:
        case QStyle::SH_MenuBar_MouseTracking:
        case QStyle::SH_MenuBar_AltKeyNavigation:
        case QStyle::SH_ScrollBar_LeftClickAbsolutePosition:
        case QStyle::SH_ScrollBar_MiddleClickAbsolutePosition:
        case QStyle::SH_ScrollBar_ScrollWhenPointerLeavesControl:
        case QStyle::SH_Slider_StopMouseOverSlider:
        case QStyle::SH_TabBar_PreferNoArrows:
        case QStyle::SH_Widget_Animate:
        case QStyle::SH_SpinControls_DisableOnBounds:
        case QStyle::SH_TitleBar_NoBorder:
        case QStyle::SH_TitleBar_AutoRaise:
        case QStyle::SH_PrintDialog_RightAlignButtons:
        case QStyle::SH_FontDialog_SelectAssociatedText:
        case QStyle::SH_Slider_SnapToValue:
            value = 1;
            return true;

        case QStyle::SH_ItemView_EllipsisLocation:
            value = Qt::AlignRight;
            return true;

        case QStyle::SH_ItemView_ScrollMode:
            value = QAbstractItemView::ScrollPerPixel;
            return true;

        case QStyle::SH_ComboBox_PopupFrameStyle:
            // The styled frame draws the panel of the popup with its shadow
            value = QFrame::StyledPanel | QFrame::Plain;
            return true;

        case QStyle::SH_SpinBox_KeyPressAutoRepeatRate:
            value = 75;
            return true;

        case QStyle::SH_SpinBox_ClickAutoRepeatThreshold:
        case QStyle::SH_ToolTip_WakeUpDelay:
            value = 500;
            return true;

        case QStyle::SH_Menu_SubMenuPopupDelay:
            value = 200;
            return true;

        case QStyle::SH_Menu_SubMenuSloppyCloseTimeout:
            value = 1000;
            return true;

        case QStyle::SH_Slider_AbsoluteSetButtons:
            value = Qt::LeftButton;
            return true;

        case QStyle::SH_Slider_PageSetButtons:
            value = Qt::MiddleButton;
            return true;

        case QStyle::SH_TabBar_Alignment:
            value = Qt::AlignLeft;
            return true;

        case QStyle::SH_TabBar_CloseButtonPosition:
            value = QTabBar::RightSide;
            return true;

        case QStyle::SH_TabBar_ElideMode:
            value = Qt::ElideRight;
            return true;

        case QStyle::SH_TabBar_SelectMouseType:
            value = QEvent::MouseButtonPress;
            return true;

        case QStyle::SH_TabWidget_DefaultTabPosition:
            value = QTabWidget::North;
            return true;

        case QStyle::SH_GroupBox_TextLabelVerticalAlignment:
            value = Qt::AlignTop;
            return true;

        case QStyle::SH_GroupBox_TextLabelColor:
            value = static_cast<int>(PDFWidgetStyleImpl::color(PDFThemeColors::TextPrimary).rgba());
            return true;

        case QStyle::SH_Table_GridLineColor:
            value = static_cast<int>(PDFWidgetStyleImpl::color(PDFThemeColors::BorderDivider).rgba());
            return true;

        case QStyle::SH_Header_ArrowAlignment:
            value = Qt::AlignRight | Qt::AlignVCenter;
            return true;

        case QStyle::SH_ToolTip_FallAsleepDelay:
            value = 2000;
            return true;

        case QStyle::SH_ToolTipLabel_Opacity:
            value = 255;
            return true;

        case QStyle::SH_ToolButton_PopupDelay:
            value = 300;
            return true;

        case QStyle::SH_ToolButtonStyle:
            value = Qt::ToolButtonIconOnly;
            return true;

        case QStyle::SH_MessageBox_TextInteractionFlags:
            value = Qt::TextSelectableByMouse;
            return true;

        case QStyle::SH_ProgressDialog_TextLabelAlignment:
            value = Qt::AlignLeft;
            return true;

        case QStyle::SH_LineEdit_PasswordCharacter:
            value = 0x2022;
            return true;

        case QStyle::SH_Widget_Animation_Duration:
            value = PDFThemeManager::getInstance()->isReducedMotion() ? 0 : 120;
            return true;

        case QStyle::SH_RequestSoftwareInputPanel:
            value = QStyle::RSIP_OnMouseClick;
            return true;

        case QStyle::SH_Button_FocusPolicy:
            value = Qt::StrongFocus;
            return true;

        case QStyle::SH_WizardStyle:
            value = QWizard::ModernStyle;
            return true;

        case QStyle::SH_TextControl_FocusIndicatorTextCharFormat:
        {
            // Focused link of a rich text is outlined by the color of the focus
            if (QStyleHintReturnVariant* variantReturn = qstyleoption_cast<QStyleHintReturnVariant*>(returnData))
            {
                QTextCharFormat format;
                format.setProperty(QTextFormat::OutlinePen, QPen(PDFWidgetStyleImpl::color(PDFThemeColors::BorderFocus), 2.0));
                variantReturn->variant = format;
            }
            value = 1;
            return true;
        }

        default:
            break;
    }

    return false;
}

QString PDFWidgetStyleMetrics::getStandardIconName(QStyle::StandardPixmap standardPixmap)
{
    switch (standardPixmap)
    {
        case QStyle::SP_MessageBoxInformation:
            return QStringLiteral("info");
        case QStyle::SP_MessageBoxQuestion:
            return QStringLiteral("help-circle");
        case QStyle::SP_MessageBoxWarning:
            return QStringLiteral("alert-triangle");
        case QStyle::SP_MessageBoxCritical:
            return QStringLiteral("alert-circle");

        case QStyle::SP_DialogOkButton:
        case QStyle::SP_DialogApplyButton:
        case QStyle::SP_DialogYesButton:
            return QStringLiteral("check");
        case QStyle::SP_DialogSaveButton:
        case QStyle::SP_DialogSaveAllButton:
            return QStringLiteral("save");

        case QStyle::SP_DialogCancelButton:
        case QStyle::SP_DialogCloseButton:
        case QStyle::SP_DialogNoButton:
        case QStyle::SP_TabCloseButton:
        case QStyle::SP_DockWidgetCloseButton:
        case QStyle::SP_TitleBarCloseButton:
            return QStringLiteral("x");
        case QStyle::SP_DialogAbortButton:
            return QStringLiteral("ban");

        case QStyle::SP_DialogHelpButton:
        case QStyle::SP_TitleBarContextHelpButton:
            return QStringLiteral("help-circle");
        case QStyle::SP_DialogResetButton:
        case QStyle::SP_RestoreDefaultsButton:
            return QStringLiteral("rotate-ccw");
        case QStyle::SP_DialogDiscardButton:
        case QStyle::SP_TrashIcon:
            return QStringLiteral("trash");
        case QStyle::SP_DialogRetryButton:
        case QStyle::SP_BrowserReload:
            return QStringLiteral("refresh-cw");
        case QStyle::SP_DialogIgnoreButton:
        case QStyle::SP_CommandLink:
        case QStyle::SP_ArrowRight:
        case QStyle::SP_ArrowForward:
        case QStyle::SP_MediaSeekForward:
            return QStringLiteral("chevron-right");

        case QStyle::SP_DialogOpenButton:
        case QStyle::SP_DirOpenIcon:
            return QStringLiteral("folder-open");
        case QStyle::SP_DialogYesToAllButton:
            return QStringLiteral("check-circle");
        case QStyle::SP_DialogNoToAllButton:
        case QStyle::SP_BrowserStop:
        case QStyle::SP_LineEditClearButton:
            return QStringLiteral("x-circle");

        case QStyle::SP_ArrowUp:
        case QStyle::SP_TitleBarShadeButton:
            return QStringLiteral("chevron-up");
        case QStyle::SP_ArrowDown:
        case QStyle::SP_TitleBarUnshadeButton:
            return QStringLiteral("chevron-down");
        case QStyle::SP_ArrowLeft:
        case QStyle::SP_ArrowBack:
        case QStyle::SP_FileDialogBack:
        case QStyle::SP_MediaSeekBackward:
            return QStringLiteral("chevron-left");

        case QStyle::SP_DirIcon:
        case QStyle::SP_DirClosedIcon:
            return QStringLiteral("folder");
        case QStyle::SP_DirHomeIcon:
        case QStyle::SP_FileDialogStart:
            return QStringLiteral("home");
        case QStyle::SP_DirLinkIcon:
        case QStyle::SP_DirLinkOpenIcon:
        case QStyle::SP_FileLinkIcon:
            return QStringLiteral("link");

        case QStyle::SP_FileIcon:
            return QStringLiteral("file");
        case QStyle::SP_FileDialogNewFolder:
            return QStringLiteral("folder-plus");
        case QStyle::SP_FileDialogToParent:
            return QStringLiteral("arrow-up");

        case QStyle::SP_FileDialogDetailedView:
            return QStringLiteral("list");
        case QStyle::SP_FileDialogListView:
            return QStringLiteral("grid");
        case QStyle::SP_FileDialogContentsView:
            return QStringLiteral("file-text");
        case QStyle::SP_FileDialogInfoView:
            return QStringLiteral("info");
        case QStyle::SP_FileDialogEnd:
        case QStyle::SP_MediaSkipForward:
            return QStringLiteral("chevron-last");
        case QStyle::SP_MediaSkipBackward:
            return QStringLiteral("chevron-first");

        case QStyle::SP_DriveHDIcon:
        case QStyle::SP_DriveFDIcon:
        case QStyle::SP_DriveCDIcon:
        case QStyle::SP_DriveDVDIcon:
            return QStringLiteral("drive");
        case QStyle::SP_DriveNetIcon:
            return QStringLiteral("server");
        case QStyle::SP_ComputerIcon:
        case QStyle::SP_DesktopIcon:
            return QStringLiteral("monitor");

        case QStyle::SP_MediaPlay:
            return QStringLiteral("play");
        case QStyle::SP_MediaPause:
            return QStringLiteral("pause");
        case QStyle::SP_MediaStop:
            return QStringLiteral("stop");
        case QStyle::SP_MediaVolume:
            return QStringLiteral("volume-2");
        case QStyle::SP_MediaVolumeMuted:
            return QStringLiteral("volume-off");

        case QStyle::SP_TitleBarNormalButton:
            return QStringLiteral("expand");
        case QStyle::SP_TitleBarMaxButton:
            return QStringLiteral("window-maximize");
        case QStyle::SP_TitleBarMinButton:
            return QStringLiteral("minus");
        case QStyle::SP_TitleBarMenuButton:
        case QStyle::SP_ToolBarHorizontalExtensionButton:
            return QStringLiteral("more-horizontal");
        case QStyle::SP_ToolBarVerticalExtensionButton:
            return QStringLiteral("more-vertical");

        case QStyle::SP_VistaShield:
            return QStringLiteral("shield");

        default:
            break;
    }

    return QString();
}

QIcon PDFWidgetStyleMetrics::standardIcon(QStyle::StandardPixmap standardPixmap)
{
    switch (standardPixmap)
    {
        case QStyle::SP_MessageBoxInformation:
            return PDFThemeIcons::getBadgeIcon(PDFThemeIcons::Badge::Information);
        case QStyle::SP_MessageBoxQuestion:
            return PDFThemeIcons::getBadgeIcon(PDFThemeIcons::Badge::Question);
        case QStyle::SP_MessageBoxWarning:
            return PDFThemeIcons::getBadgeIcon(PDFThemeIcons::Badge::Warning);
        case QStyle::SP_MessageBoxCritical:
            return PDFThemeIcons::getBadgeIcon(PDFThemeIcons::Badge::Critical);
        default:
            break;
    }

    const QString name = getStandardIconName(standardPixmap);
    return name.isEmpty() ? QIcon() : PDFThemeIcons::getIcon(name);
}

}   // namespace pdf
