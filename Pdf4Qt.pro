#    Copyright (C) 2018-2020 Jakub Melka
#
#    This file is part of Pdf4Qt.
#
#    Pdf4Qt is free software: you can redistribute it and/or modify
#    it under the terms of the GNU Lesser General Public License as published by
#    the Free Software Foundation, either version 3 of the License, or
#    (at your option) any later version.
#
#    Pdf4Qt is distributed in the hope that it will be useful,
#    but WITHOUT ANY WARRANTY; without even the implied warranty of
#    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#    GNU Lesser General Public License for more details.
#
#    You should have received a copy of the GNU Lesser General Public License
#    along with Pdf4Qt.  If not, see <https://www.gnu.org/licenses/>.

TEMPLATE = subdirs

SUBDIRS += \
    Pdf4QtLib \
    CodeGenerator \
    JBIG2_Viewer \
    PdfExampleGenerator \
    PdfTool \
    UnitTests \
    Pdf4QtViewer \
    Pdf4QtViewerPlugins \
    Pdf4QtViewerProfi

CodeGenerator.depends = Pdf4QtLib
JBIG2_Viewer.depends = Pdf4QtLib
PdfExampleGenerator.depends = Pdf4QtLib
PdfTool.depends = Pdf4QtLib
UnitTests.depends = Pdf4QtLib
Pdf4QtViewer.depends = Pdf4QtLib
Pdf4QtViewerPlugins.depends = Pdf4QtLib
Pdf4QtViewerProfi.depends = Pdf4QtViewer
