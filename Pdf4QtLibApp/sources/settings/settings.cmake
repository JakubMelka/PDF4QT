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

# The page Settings of the new application (step 5, plan step5_app_plan.md 2):
# the page, the sections and their registry, the generic rows and their control
# binder, the dialog Restore defaults and the sections of the packages A - D
# (wave 0 stubs, which the packages replace). Paths are relative to the directory
# of Pdf4QtLibApp.

set(PDF4QT_LIBAPP_SETTINGS_SOURCES
    sources/settings/pdfapprestoredefaultsdialog.cpp
    sources/settings/pdfapprestoredefaultsdialog.h
    sources/settings/pdfappsettingcontrol.cpp
    sources/settings/pdfappsettingcontrol.h
    sources/settings/pdfappsettingrow.cpp
    sources/settings/pdfappsettingrow.h
    sources/settings/pdfappsettingspage.cpp
    sources/settings/pdfappsettingspage.h
    sources/settings/pdfappsettingssection.cpp
    sources/settings/pdfappsettingssection.h
)

# Sections of the packages of wave 1: each package adds its own files below its line
list(APPEND PDF4QT_LIBAPP_SETTINGS_SOURCES
    # Package A - General, language, tool history
    sources/settings/pdfappsettingssection_general.cpp
    sources/settings/pdfappsettingssection_general.h
    # Package B - Viewing, Color modes, Color management, Advanced
    sources/settings/pdfappcolormodepresets.cpp
    sources/settings/pdfappcolormodepresets.h
    sources/settings/pdfappcolormodepreview.cpp
    sources/settings/pdfappcolormodepreview.h
    sources/settings/pdfappcolorprofiles.cpp
    sources/settings/pdfappcolorprofiles.h
    sources/settings/pdfappsettingssection_colormanagement.cpp
    sources/settings/pdfappsettingssection_colormanagement.h
    sources/settings/pdfappsettingssection_colormodes.cpp
    sources/settings/pdfappsettingssection_colormodes.h
    # Package C - Editing, Security, Speech
    sources/settings/pdfappcertificatetable.cpp
    sources/settings/pdfappcertificatetable.h
    sources/settings/pdfappsettingssection_security.cpp
    sources/settings/pdfappsettingssection_security.h
    sources/settings/pdfappsettingssection_speech.cpp
    sources/settings/pdfappsettingssection_speech.h
    # Package D - Shortcuts, Storage
    sources/settings/pdfappsettingssection_shortcuts.cpp
    sources/settings/pdfappsettingssection_shortcuts.h
    sources/settings/pdfappsettingssection_storage.cpp
    sources/settings/pdfappsettingssection_storage.h
    sources/settings/pdfappshortcutsmodel.cpp
    sources/settings/pdfappshortcutsmodel.h
)
