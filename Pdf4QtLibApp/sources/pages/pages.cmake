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

# Pages of the routes of the application (newapp_navigation.md 3); every page
# registers its factory in PDFAppPageRegistry::registerFactories

set(PDF4QT_LIBAPP_PAGES_SOURCES
    sources/pages/pdfapppageregistry.cpp
    sources/pages/pdfapppageregistry.h
)

# The layout helpers and the base of the pages (columns, grid, flow layout,
# responsive row, section header, card, PDFAppContentPage)
list(APPEND PDF4QT_LIBAPP_PAGES_SOURCES
    sources/pages/pdfapppagelayout.cpp
    sources/pages/pdfapppagelayout.h
)

# About, Support and Sponsor (step 2): the pages, their widgets, the build
# information and the catalog of the video tutorials
list(APPEND PDF4QT_LIBAPP_PAGES_SOURCES
    sources/pages/pdfappaboutinfo.cpp
    sources/pages/pdfappaboutinfo.h
    sources/pages/pdfappaboutpage.cpp
    sources/pages/pdfappaboutpage.h
    sources/pages/pdfappinfowidgets.cpp
    sources/pages/pdfappinfowidgets.h
    sources/pages/pdfappsponsorpage.cpp
    sources/pages/pdfappsponsorpage.h
    sources/pages/pdfappsupportpage.cpp
    sources/pages/pdfappsupportpage.h
    sources/pages/pdfappthirdparty.cpp
    sources/pages/pdfsupportvideocatalog.cpp
    sources/pages/pdfsupportvideocatalog.h
)

# What's New, the release notes, the manual /help and the Markdown view (step 2),
# their resources (resources/whatsnew.qrc, resources/help.qrc)
list(APPEND PDF4QT_LIBAPP_PAGES_SOURCES
    sources/pages/pdfapphelppage.cpp
    sources/pages/pdfapphelppage.h
    sources/pages/pdfappmarkdownview.cpp
    sources/pages/pdfappmarkdownview.h
    sources/pages/pdfappwhatsnewpage.cpp
    sources/pages/pdfappwhatsnewpage.h
    resources/help.qrc
    resources/whatsnew.qrc
)

# The global search (step 2): the engine and its providers, the popup of the
# field of the header, the page of all results /search
list(APPEND PDF4QT_LIBAPP_PAGES_SOURCES
    sources/pages/pdfappsearchengine.cpp
    sources/pages/pdfappsearchengine.h
    sources/pages/pdfappsearchpage.cpp
    sources/pages/pdfappsearchpage.h
    sources/pages/pdfappsearchwidgets.cpp
    sources/pages/pdfappsearchwidgets.h
)

# Home, Open and Recent (step 2): the pages, their widgets, menus and dialogs,
# the drop of files and the native file dialogs
list(APPEND PDF4QT_LIBAPP_PAGES_SOURCES
    sources/pages/pdfappfiledrop.cpp
    sources/pages/pdfappfiledrop.h
    sources/pages/pdfapphomebasepage.cpp
    sources/pages/pdfapphomebasepage.h
    sources/pages/pdfapphomedialogs.cpp
    sources/pages/pdfapphomedialogs.h
    sources/pages/pdfapphomepage.cpp
    sources/pages/pdfapphomepage.h
    sources/pages/pdfapphomewidgets.cpp
    sources/pages/pdfapphomewidgets.h
    sources/pages/pdfappopenpage.cpp
    sources/pages/pdfappopenpage.h
    sources/pages/pdfapprecentpage.cpp
    sources/pages/pdfapprecentpage.h
)

# The table of the third-party libraries of About reads the versions from the
# headers of the libraries, which Pdf4QtLibCore links: only this file gets their
# include directories (Pdf4QtLibApp does not link them). The system folders are left
# out: GCC finds them by itself, an explicit /usr/include breaks #include_next.
set(PDF4QT_LIBAPP_THIRD_PARTY_INCLUDES)
foreach(PDF4QT_LIBAPP_THIRD_PARTY_TARGET OpenSSL::Crypto ZLIB::ZLIB Freetype::Freetype openjp2 JPEG::JPEG PNG::PNG lcms2::lcms2 blend2d::blend2d Clipper2::Clipper2 TBB::tbb)
    list(APPEND PDF4QT_LIBAPP_THIRD_PARTY_INCLUDES
         "$<$<TARGET_EXISTS:${PDF4QT_LIBAPP_THIRD_PARTY_TARGET}>:$<FILTER:$<TARGET_PROPERTY:${PDF4QT_LIBAPP_THIRD_PARTY_TARGET},INTERFACE_INCLUDE_DIRECTORIES>,EXCLUDE,^/usr(/local)?/include/?$>>")
endforeach()
set_source_files_properties(sources/pages/pdfappthirdparty.cpp PROPERTIES INCLUDE_DIRECTORIES "${PDF4QT_LIBAPP_THIRD_PARTY_INCLUDES}")
if(LINUX_GCC)
    # Only the GCC builds link oneTBB (Pdf4QtLibCore)
    set_source_files_properties(sources/pages/pdfappthirdparty.cpp PROPERTIES COMPILE_DEFINITIONS PDF4QT_APP_LINKS_TBB)
endif()
