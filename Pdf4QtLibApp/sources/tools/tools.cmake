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

# The Tools catalog of the new application (step 6, plan step6_app_plan.md 4):
# the shared part of wave 0 (presentation, state, built-in collections and their
# merge, the registry of the tool pages) and the files of the packages of wave 1
# (wave 0 stubs, which the packages replace). Paths are relative to the directory
# of Pdf4QtLibApp.

set(PDF4QT_LIBAPP_TOOLS_SOURCES
    sources/tools/pdfappbuiltintoolcollections.cpp
    sources/tools/pdfappbuiltintoolcollections.h
    sources/tools/pdfapptoolcollections.cpp
    sources/tools/pdfapptoolcollections.h
    sources/tools/pdfapptoolpageregistry.cpp
    sources/tools/pdfapptoolpageregistry.h
    sources/tools/pdfapptoolpresentation.cpp
    sources/tools/pdfapptoolpresentation.h
    sources/tools/pdfapptoolstate.cpp
    sources/tools/pdfapptoolstate.h
)

# Files of the packages of wave 1: each package adds its own files below its line
list(APPEND PDF4QT_LIBAPP_TOOLS_SOURCES
    # Package A - the Tools page
    sources/tools/pdfapptoolspage.cpp
    sources/tools/pdfapptoolspage.h
    sources/tools/pdfapptoolcategorycard.cpp
    sources/tools/pdfapptoolcategorycard.h
    sources/tools/pdfapptoolmenu.cpp
    sources/tools/pdfapptoolmenu.h
    # Package B - the collections page
    sources/tools/pdfapptoolcollectionspage.cpp
    sources/tools/pdfapptoolcollectionspage.h
    sources/tools/pdfapptoolcollectioneditor.cpp
    sources/tools/pdfapptoolcollectioneditor.h
    sources/tools/pdfappcollectiontoollist.cpp
    sources/tools/pdfappcollectiontoollist.h
    sources/tools/pdfappcollectioniconchooser.cpp
    sources/tools/pdfappcollectioniconchooser.h
    # Package C - the Tools panel of the document
    sources/tools/pdfappdocumenttoolspanel.cpp
    sources/tools/pdfappdocumenttoolspanel.h
)

# Phase 2, the shell of the tools: each part adds its own files below its line
list(APPEND PDF4QT_LIBAPP_TOOLS_SOURCES
    # Shared interface of the view and of the parameter model
    sources/tools/pdfapptoolundotarget.h
    # Runner, parameter model and result dialog
    sources/tools/pdfapptoolparametermodel.cpp
    sources/tools/pdfapptoolparametermodel.h
    sources/tools/pdfapptoolpathrules.cpp
    sources/tools/pdfapptoolpathrules.h
    sources/tools/pdfapptoolresultdialog.cpp
    sources/tools/pdfapptoolresultdialog.h
    sources/tools/pdfapptoolresultwriter.cpp
    sources/tools/pdfapptoolresultwriter.h
    sources/tools/pdfdocumenttoolrunner.cpp
    sources/tools/pdfdocumenttoolrunner.h
    # Tool mode of the view
    # The shell, its GUI, the builder of the thin tools and the page of a tool
    sources/tools/pdfappbuiltintools.cpp
    sources/tools/pdfappbuiltintools.h
    sources/tools/pdfappdeclarativetoolgui.cpp
    sources/tools/pdfappdeclarativetoolgui.h
    sources/tools/pdfapptooldocumentpage.cpp
    sources/tools/pdfapptooldocumentpage.h
    sources/tools/pdfapptoolhost.cpp
    sources/tools/pdfapptoolhost.h
    sources/tools/pdfapptoolpanelbuilder.cpp
    sources/tools/pdfapptoolpanelbuilder.h
    sources/tools/pdfapptoolpanelparts.cpp
    sources/tools/pdfapptoolpanelparts.h
    sources/tools/pdfapptoolpanelpopup.cpp
    sources/tools/pdfapptoolpanelpopup.h
    sources/tools/pdfapptooltoolbar.cpp
    sources/tools/pdfapptooltoolbar.h
    sources/tools/pdfdocumenttoolgui.cpp
    sources/tools/pdfdocumenttoolgui.h
    sources/tools/pdfdocumenttoolguiregistry.cpp
    sources/tools/pdfdocumenttoolguiregistry.h
    sources/tools/pdfdocumenttoolshell.cpp
    sources/tools/pdfdocumenttoolshell.h
    # Wave D: Compress and the start page of a tool
    sources/tools/pdfapppagerendercontext.cpp
    sources/tools/pdfapppagerendercontext.h
    sources/tools/pdfapptoolbeforeafteroverlay.cpp
    sources/tools/pdfapptoolbeforeafteroverlay.h
    sources/tools/pdfapptooldocumentchooser.cpp
    sources/tools/pdfapptooldocumentchooser.h
    sources/tools/pdfapptoolgui_compress.cpp
    sources/tools/pdfapptoolgui_compress.h
    sources/tools/pdftoolstartpage.cpp
    sources/tools/pdftoolstartpage.h
    # Package 2A (pages.rotate, pages.remove)
    sources/tools/pdfapptoolgui_pages.cpp
    sources/tools/pdfapptoolgui_pages.h
    sources/tools/pdfapptoolpagegrid.cpp
    sources/tools/pdfapptoolpagegrid.h
    # Package 2B (protect.sanitize, protect.remove-links)
    sources/tools/pdfapptoolgui_protect.cpp
    sources/tools/pdfapptoolgui_protect.h
    # Package 2C (optimize.structure)
    sources/tools/pdfapptoolgui_structure.cpp
    sources/tools/pdfapptoolgui_structure.h
    # Package 2D (convert.extract-text)
    sources/tools/pdfapptoolgui_extracttext.cpp
    sources/tools/pdfapptoolgui_extracttext.h
    # Step 2 of phase 2, package B1 (own tab of a tool, its host and the shared layout)
    sources/tools/pdfappowntabtoolhost.cpp
    sources/tools/pdfappowntabtoolhost.h
    sources/tools/pdfapptoolmodelayout.cpp
    sources/tools/pdfapptoolmodelayout.h
    sources/tools/pdfapptooltab.cpp
    sources/tools/pdfapptooltab.h
    # Step 2 of phase 2, package B2 (inputs of a tool with more inputs)
    sources/tools/pdfapppassworddialog.cpp
    sources/tools/pdfapppassworddialog.h
    sources/tools/pdfapptoolinputlist.cpp
    sources/tools/pdfapptoolinputlist.h
    sources/tools/pdfapptoolinputloader.cpp
    sources/tools/pdfapptoolinputloader.h
    # Step 2 of phase 2, package C (Merge)
    sources/tools/pdfapptoolgui_merge.cpp
    sources/tools/pdfapptoolgui_merge.h
    sources/tools/pdfapptoolinputrow.cpp
    sources/tools/pdfapptoolinputrow.h
    sources/tools/pdfapptoolmergemodel.cpp
    sources/tools/pdfapptoolmergemodel.h
    sources/tools/pdfapptoolmergeview.cpp
    sources/tools/pdfapptoolmergeview.h
    # Step 2 of phase 2, package D (Blank PDF)
    sources/tools/pdfapppagesizepreview.cpp
    sources/tools/pdfapppagesizepreview.h
    sources/tools/pdfapptoolgui_blank.cpp
    sources/tools/pdfapptoolgui_blank.h
    # Step 3a of phase 2, the skeleton (fixed interfaces, only the coordinator changes them)
    sources/tools/pdfapptoolgroupedpagemodel.cpp
    sources/tools/pdfapptoolgroupedpagemodel.h
    # Step 3a, package A (Split, Extract Pages, more files)
    sources/tools/pdfapptoolfilesink.cpp
    sources/tools/pdfapptoolfilesink.h
    sources/tools/pdfapptoolgui_split.cpp
    sources/tools/pdfapptoolgui_split.h
    sources/tools/pdfapptooloutputfiles.cpp
    sources/tools/pdfapptooloutputfiles.h
    # Step 3a, package B (Crop, Resize, Set Page Boxes)
    sources/tools/pdfapptoolboxesoverlay.cpp
    sources/tools/pdfapptoolboxesoverlay.h
    sources/tools/pdfapptoolcropoverlay.cpp
    sources/tools/pdfapptoolcropoverlay.h
    sources/tools/pdfapptoolgui_geometry.cpp
    sources/tools/pdfapptoolgui_geometry.h
    sources/tools/pdfapptoolpagestage.cpp
    sources/tools/pdfapptoolpagestage.h
    # Step 3a, package C (Insert Pages, Add Image as Page)
    sources/tools/pdfapptoolgui_insert.cpp
    sources/tools/pdfapptoolgui_insert.h
    sources/tools/pdfapptoolinsertstrip.cpp
    sources/tools/pdfapptoolinsertstrip.h
    # Step 3a, package D (Images to PDF, Interleave Pages)
    sources/tools/pdfappimagepagepreview.cpp
    sources/tools/pdfappimagepagepreview.h
    sources/tools/pdfapptoolgui_images.cpp
    sources/tools/pdfapptoolgui_images.h
    sources/tools/pdfapptoolgui_interleave.cpp
    sources/tools/pdfapptoolgui_interleave.h
    # Step 3b, package M3 (Manage Pages: the GUI, the canvas, the panels, the dialogs)
    sources/tools/pdfappmanagecanvas.cpp
    sources/tools/pdfappmanagecanvas.h
    sources/tools/pdfappmanagedialogs.cpp
    sources/tools/pdfappmanagedialogs.h
    sources/tools/pdfappmanagepanels.cpp
    sources/tools/pdfappmanagepanels.h
    sources/tools/pdfapptoolgui_manage.cpp
    sources/tools/pdfapptoolgui_manage.h
    # Step 3b, package M4 (Manage Pages: the opening of an assembly file)
    sources/tools/pdfappassemblyloader.cpp
    sources/tools/pdfappassemblyloader.h
)
