# MIT License
#
# Copyright (c) 2018-2026 Jakub Melka and Contributors
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.

# Precompiled headers of the libraries, of the larger applications and of the tests.
#
# The headers were chosen by a measurement, not by a guess (2026-10-31): every C++
# object of a Release MSVC build (822 TUs) was compiled again with /Bt+ /d1reportTime.
# The front end took 4272 s, the back end 787 s; 2481 s of the front end was the
# parsing of the headers of Qt, of the C++ standard library and of the C runtime.
# A header is in a list below, when our code includes it directly and at least 30 %
# of the TUs of some group of targets (Core, Widgets, Gui, Fluent, Mcp, App, the
# applications, the plugins, the groups of the tests) parse it; a header used by one
# group only is left out. Only stable third-party headers are listed: Qt and the
# standard library, never a header of PDF4QT (a change of it would compile everything
# again), never <windows.h> or another header of the system (macros as min / max),
# never OpenSSL / vcpkg headers. Most of the standard library comes through the Qt
# headers; the list of it holds the headers our code includes before any Qt header.
# A list of a module is used only by the targets, which link the module.
#
# Every source must compile without the precompiled headers as well (no hidden missing
# include): PDF4QT_ENABLE_PCH=OFF (or CMAKE_DISABLE_PRECOMPILE_HEADERS=ON) turns them off;
# the valgrind tree of the CI builds without them as the guard of the missing includes.

option(PDF4QT_ENABLE_PCH "Precompile the most used headers of Qt and of the C++ standard library" ON)

set(PDF4QT_USE_PCH ${PDF4QT_ENABLE_PCH})
if(CMAKE_DISABLE_PRECOMPILE_HEADERS)
    # CMake ignores every precompiled header; no donor target of the tests is created
    set(PDF4QT_USE_PCH OFF)
endif()
if(PDF4QT_USE_PCH AND PDF4QT_ENABLE_VALGRIND_ANNOTATIONS AND NOT MSVC)
    # The prelude of the annotations (pdf4qtvalgrindprelude.h, -include) must be the first
    # header of every C++ file; GCC uses a precompiled header only before the first token,
    # so the prelude and a precompiled header exclude each other
    message(STATUS "Precompiled headers are turned off: PDF4QT_ENABLE_VALGRIND_ANNOTATIONS includes its prelude before every header")
    set(PDF4QT_USE_PCH OFF)
endif()

set(PDF4QT_PCH_STD
    <array>
    <cstddef>
    <deque>
    <map>
    <mutex>
    <set>
    <stack>
    <vector>
)

set(PDF4QT_PCH_CORE
    <QtCore/QAbstractItemModel>
    <QtCore/QByteArray>
    <QtCore/QByteArrayView>
    <QtCore/QCoreApplication>
    <QtCore/QDataStream>
    <QtCore/QDateTime>
    <QtCore/QDeadlineTimer>
    <QtCore/QElapsedTimer>
    <QtCore/QFlags>
    <QtCore/QHash>
    <QtCore/QIODevice>
    <QtCore/QJsonArray>
    <QtCore/QJsonObject>
    <QtCore/QJsonValue>
    <QtCore/QLineF>
    <QtCore/QList>
    <QtCore/QMap>
    <QtCore/QMargins>
    <QtCore/QMarginsF>
    <QtCore/QMetaType>
    <QtCore/QMutex>
    <QtCore/QMutexLocker>
    <QtCore/QObject>
    <QtCore/QPointF>
    <QtCore/QPointer>
    <QtCore/QRect>
    <QtCore/QRectF>
    <QtCore/QRecursiveMutex>
    <QtCore/QSemaphore>
    <QtCore/QSet>
    <QtCore/QSharedPointer>
    <QtCore/QSize>
    <QtCore/QSizeF>
    <QtCore/QString>
    <QtCore/QStringList>
    <QtCore/QStringView>
    <QtCore/QThread>
    <QtCore/QThreadPool>
    <QtCore/QTimer>
    <QtCore/QUrl>
    <QtCore/QVariant>
    <QtCore/QVariantMap>
    <QtCore/QtGlobal>
)

set(PDF4QT_PCH_GUI
    <QtGui/QBrush>
    <QtGui/QColor>
    <QtGui/QCursor>
    <QtGui/QFont>
    <QtGui/QIcon>
    <QtGui/QIconEngine>
    <QtGui/QImage>
    <QtGui/QImageWriter>
    <QtGui/QKeySequence>
    <QtGui/QMatrix4x4>
    <QtGui/QPainter>
    <QtGui/QPainterPath>
    <QtGui/QPalette>
    <QtGui/QPen>
    <QtGui/QPixmap>
    <QtGui/QPolygonF>
    <QtGui/QTextLayout>
    <QtGui/QTransform>
)

set(PDF4QT_PCH_WIDGETS
    <QtWidgets/QAbstractButton>
    <QtWidgets/QApplication>
    <QtWidgets/QBoxLayout>
    <QtWidgets/QButtonGroup>
    <QtWidgets/QCalendarWidget>
    <QtWidgets/QComboBox>
    <QtWidgets/QDateTimeEdit>
    <QtWidgets/QDial>
    <QtWidgets/QDialog>
    <QtWidgets/QDialogButtonBox>
    <QtWidgets/QDoubleSpinBox>
    <QtWidgets/QFrame>
    <QtWidgets/QGroupBox>
    <QtWidgets/QHeaderView>
    <QtWidgets/QItemEditorFactory>
    <QtWidgets/QLabel>
    <QtWidgets/QLineEdit>
    <QtWidgets/QListView>
    <QtWidgets/QMenu>
    <QtWidgets/QPlainTextEdit>
    <QtWidgets/QPushButton>
    <QtWidgets/QScrollArea>
    <QtWidgets/QScrollBar>
    <QtWidgets/QSpacerItem>
    <QtWidgets/QSpinBox>
    <QtWidgets/QSplitter>
    <QtWidgets/QStackedWidget>
    <QtWidgets/QStatusBar>
    <QtWidgets/QStyle>
    <QtWidgets/QStyleOption>
    <QtWidgets/QStyledItemDelegate>
    <QtWidgets/QTabBar>
    <QtWidgets/QTextEdit>
    <QtWidgets/QToolButton>
    <QtWidgets/QTreeView>
    <QtWidgets/QVBoxLayout>
    <QtWidgets/QWidget>
)

set(PDF4QT_PCH_NETWORK
    <QtNetwork/QHostAddress>
    <QtNetwork/QTcpSocket>
)

# Every test includes the module header <QtTest> (it includes all of QtCore); what
# it declares depends on QT_GUI_LIB / QT_WIDGETS_LIB / QT_NETWORK_LIB, so a test
# shares a precompiled header only with a donor of the same Qt modules (see below)
set(PDF4QT_PCH_TEST
    <QtTest/QtTest>
)

# pdf4qt_target_precompile_headers(<target> MODULES <CORE|GUI|WIDGETS|NETWORK|TEST>...)
#     The target precompiles the standard library list and the lists of the modules.
# pdf4qt_target_precompile_headers(<target> REUSE_FROM <donor>)
#     The target uses the precompiled header of the donor (CMake REUSE_FROM). The
#     compile definitions of the target, which the headers test (QT_GUI_LIB,
#     QT_WIDGETS_LIB, QT_NETWORK_LIB, QT_TESTCASE_BUILDDIR, ...), must equal the ones
#     of the donor: MSVC warns (C4651) of a definition of the donor missing in the
#     target; an additional definition of the target (PDF4QT_SOURCE_DIRECTORY,
#     QT_PRINTSUPPORT_LIB, ...) is allowed, no header of the lists tests it. A
#     difference in QT_GUI_LIB / QT_WIDGETS_LIB / QT_NETWORK_LIB is silent on MSVC:
#     the macros of the precompiled header win, so QTEST_MAIN of a test with Widgets
#     reusing a donor without them would create a QGuiApplication. Every pair is
#     therefore checked at the end of the configuration (pdf4qt_check_pch_donors).
function(pdf4qt_target_precompile_headers target)
    cmake_parse_arguments(ARG "" "REUSE_FROM" "MODULES" ${ARGN})
    if(NOT PDF4QT_USE_PCH)
        return()
    endif()
    if(ARG_REUSE_FROM)
        target_precompile_headers(${target} REUSE_FROM ${ARG_REUSE_FROM})
        set_property(GLOBAL APPEND PROPERTY PDF4QT_PCH_REUSE_PAIRS "${target}>${ARG_REUSE_FROM}")
        return()
    endif()
    set(headers ${PDF4QT_PCH_STD})
    foreach(module IN LISTS ARG_MODULES)
        if(NOT DEFINED PDF4QT_PCH_${module})
            message(FATAL_ERROR "pdf4qt_target_precompile_headers: unknown module ${module}")
        endif()
        list(APPEND headers ${PDF4QT_PCH_${module}})
    endforeach()
    target_precompile_headers(${target} PRIVATE ${headers})
endfunction()

# The Qt modules, whose definitions QT_<MODULE>_LIB a target compiles with: the
# definitions of the target and the usage requirements of its link closure (a
# $<LINK_ONLY:...> item passes none; other generator expressions are searched for
# the names of targets, which may over-approximate - a reported difference is then
# checked in build.ninja). Only the definitions tested by the listed headers count.
function(pdf4qt_pch_get_qt_definitions target out_var)
    set(pending ${target})
    set(visited "")
    set(definitions "")
    set(start TRUE)
    while(pending)
        list(POP_FRONT pending item)
        if(NOT TARGET "${item}" OR item IN_LIST visited)
            continue()
        endif()
        list(APPEND visited ${item})
        get_target_property(aliased ${item} ALIASED_TARGET)
        if(aliased)
            set(item ${aliased})
        endif()
        if(start)
            set(start FALSE)
            get_target_property(item_definitions ${item} COMPILE_DEFINITIONS)
            get_target_property(item_links ${item} LINK_LIBRARIES)
        else()
            get_target_property(item_definitions ${item} INTERFACE_COMPILE_DEFINITIONS)
            get_target_property(item_links ${item} INTERFACE_LINK_LIBRARIES)
        endif()
        if(item_definitions)
            string(REGEX MATCHALL "QT_(GUI|WIDGETS|NETWORK|TESTLIB)_LIB" found "${item_definitions}")
            list(APPEND definitions ${found})
        endif()
        if(item_links)
            foreach(link IN LISTS item_links)
                if(link MATCHES "^\\$<LINK_ONLY:")
                    continue()
                endif()
                if(link MATCHES "\\$<")
                    string(REGEX MATCHALL "[A-Za-z0-9_]+::[A-Za-z0-9_]+|[A-Za-z0-9_]+" names "${link}")
                    list(APPEND pending ${names})
                else()
                    list(APPEND pending ${link})
                endif()
            endforeach()
        endif()
    endwhile()
    list(REMOVE_DUPLICATES definitions)
    list(SORT definitions)
    set(${out_var} "${definitions}" PARENT_SCOPE)
endfunction()

# Fails the configuration, when a target reuses the precompiled header of a donor
# with other Qt modules (see pdf4qt_target_precompile_headers) or of another
# directory (QT_TESTCASE_BUILDDIR / QT_TESTCASE_SOURCEDIR of QtTest are the
# directories of the target).
function(pdf4qt_check_pch_donors)
    get_property(pairs GLOBAL PROPERTY PDF4QT_PCH_REUSE_PAIRS)
    set(errors "")
    foreach(pair IN LISTS pairs)
        string(REPLACE ">" ";" pair_list "${pair}")
        list(GET pair_list 0 user)
        list(GET pair_list 1 donor)
        pdf4qt_pch_get_qt_definitions(${user} user_definitions)
        pdf4qt_pch_get_qt_definitions(${donor} donor_definitions)
        if(NOT "${user_definitions}" STREQUAL "${donor_definitions}")
            string(APPEND errors "\n  ${user} [${user_definitions}] reuses ${donor} [${donor_definitions}]")
        endif()
        get_target_property(user_directory ${user} BINARY_DIR)
        get_target_property(donor_directory ${donor} BINARY_DIR)
        if("QT_TESTLIB_LIB" IN_LIST donor_definitions AND NOT user_directory STREQUAL donor_directory)
            string(APPEND errors "\n  ${user} (${user_directory}) reuses ${donor} of another directory (${donor_directory})")
        endif()
    endforeach()
    if(errors)
        message(FATAL_ERROR "A precompiled header is reused by a target with other Qt modules than its donor "
                            "(what <QtTest> declares and the application of QTEST_MAIN depend on them); "
                            "choose a donor with the same modules in UnitTests/CMakeLists.txt:${errors}")
    endif()
endfunction()

if(PDF4QT_USE_PCH AND CMAKE_VERSION VERSION_GREATER_EQUAL 3.19)
    # Every target exists at the end of the top-level directory (DEFER needs CMake 3.19)
    cmake_language(DEFER CALL pdf4qt_check_pch_donors)
endif()
