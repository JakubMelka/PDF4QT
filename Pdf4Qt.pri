#    Copyright (C) 2018-2022 Jakub Melka
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

win32-msvc*: {
    QMAKE_CXXFLAGS += /std:c++latest /utf-8 /bigobj
}

win32-*g++|unix: {
    CONFIG += link_pkgconfig
    QMAKE_CXXFLAGS += -std=c++20
    win32: {
        QMAKE_CXXFLAGS += -Wa,-mbig-obj
    }
}

win32 {
    CONFIG += skip_target_version_ext
} else {
    CONFIG += unversioned_libname
}

VERSION = 1.2.1
DEFINES += PDF4QT_PROJECT_VERSION=$$VERSION
