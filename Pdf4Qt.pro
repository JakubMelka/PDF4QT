#    Copyright (C) 2018-2021 Jakub Melka
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
    Pdf4QtViewerProfi \
    Pdf4QtViewerLite \
    Pdf4QtDocPageOrganizer \
    Pdf4QtDocDiff

CodeGenerator.depends = Pdf4QtLib
JBIG2_Viewer.depends = Pdf4QtLib
PdfExampleGenerator.depends = Pdf4QtLib
PdfTool.depends = Pdf4QtLib
UnitTests.depends = Pdf4QtLib
Pdf4QtViewer.depends = Pdf4QtLib
Pdf4QtViewerPlugins.depends = Pdf4QtLib
Pdf4QtViewerProfi.depends = Pdf4QtViewer
Pdf4QtViewerLite.depends = Pdf4QtViewer
Pdf4QtDocPageOrganizer.depends = Pdf4QtLib
Pdf4QtDocDiff.depends = Pdf4QtLib
