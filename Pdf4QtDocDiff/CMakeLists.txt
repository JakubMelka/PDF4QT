#    Copyright (C) 2022 Jakub Melka
#
#    This file is part of PDF4QT.
#
#    PDF4QT is free software: you can redistribute it and/or modify
#    it under the terms of the GNU Lesser General Public License as published by
#    the Free Software Foundation, either version 3 of the License, or
#    with the written consent of the copyright owner, any later version.
#
#    PDF4QT is distributed in the hope that it will be useful,
#    but WITHOUT ANY WARRANTY; without even the implied warranty of
#    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#    GNU Lesser General Public License for more details.
#
#    You should have received a copy of the GNU Lesser General Public License
#    along with PDF4QT.  If not, see <https://www.gnu.org/licenses/>.

add_executable(Pdf4QtDocDiff
    aboutdialog.cpp
    differencesdockwidget.cpp
    main.cpp
    mainwindow.cpp
    settingsdockwidget.cpp
    utils.cpp
    aboutdialog.ui
    differencesdockwidget.ui
    mainwindow.ui
    settingsdockwidget.ui
    resources.qrc
    icon.rc
)

if(PDF4QT_ENABLE_OPENGL)
    target_link_libraries(Pdf4QtDocDiff PRIVATE Qt6::OpenGLWidgets)
endif()

target_link_libraries(Pdf4QtDocDiff PRIVATE Pdf4QtLibCore Pdf4QtLibWidgets Qt6::Core Qt6::Gui Qt6::Widgets)

set_target_properties(Pdf4QtDocDiff PROPERTIES
    WIN32_EXECUTABLE ON
    MACOSX_BUNDLE ON
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_LIB_DIR}
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_BIN_DIR}
)

install(TARGETS Pdf4QtDocDiff RUNTIME DESTINATION ${PDF4QT_INSTALL_BIN_DIR} LIBRARY DESTINATION ${PDF4QT_INSTALL_LIB_DIR})
