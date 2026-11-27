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
    sources/tools/pdfapptooldragsource.h
    sources/tools/pdfapptoolowntabquestion.h
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
    # Step 4, package P1 (PDF to Images)
    sources/tools/pdfapptoolgui_pdftoimages.cpp
    sources/tools/pdfapptoolgui_pdftoimages.h
    sources/tools/pdfapptoolimageformatmenu.cpp
    sources/tools/pdfapptoolimageformatmenu.h
    # Step 4, package P2 (the list of the images, the comparison, the profile editor, Optimize
    # Images with Grayscale, Extract Images)
    sources/tools/pdfapptoolimagelist.cpp
    sources/tools/pdfapptoolimagelist.h
    sources/tools/pdfapptoolimagecomparison.cpp
    sources/tools/pdfapptoolimagecomparison.h
    sources/tools/pdfapptoolimageprofileeditor.cpp
    sources/tools/pdfapptoolimageprofileeditor.h
    sources/tools/pdfapptoolgui_optimizeimages.cpp
    sources/tools/pdfapptoolgui_optimizeimages.h
    sources/tools/pdfapptoolgui_extractimages.cpp
    sources/tools/pdfapptoolgui_extractimages.h
    # Step 4, package P3 (Convert to Black & White)
    sources/tools/pdfapptoolgui_bitonal.cpp
    sources/tools/pdfapptoolgui_bitonal.h
    # Step 4, package P4 (Create Audio Book and its overlay of the page)
    sources/tools/pdfapptoolgui_audiobook.cpp
    sources/tools/pdfapptoolgui_audiobook.h
    sources/tools/pdfappaudiobookoverlay.cpp
    sources/tools/pdfappaudiobookoverlay.h
    # Step 5, package P2 (the GUI of the four tools of the encryption, the cards of the passwords,
    # the list of the permissions, What changes)
    sources/tools/pdfapptoolgui_encryption.cpp
    sources/tools/pdfapptoolgui_encryption.h
    sources/tools/pdfapptoolpasswordcard.cpp
    sources/tools/pdfapptoolpasswordcard.h
    sources/tools/pdfapptoolpermissionlist.cpp
    sources/tools/pdfapptoolpermissionlist.h
    sources/tools/pdfapptoolchangelist.cpp
    sources/tools/pdfapptoolchangelist.h
    # Step 5, package P3 (the list of the certificates and the dialogs Create Certificate, Import)
    sources/tools/pdfapptoolcertificatelist.cpp
    sources/tools/pdfapptoolcertificatelist.h
    sources/tools/pdfappcertificatedialogs.cpp
    sources/tools/pdfappcertificatedialogs.h
    # Step 6 (plan 10.1, 10.2 with R6-23), package P4 (Sign with Certificate, Add Timestamp, Sign by
    # Hand, the drawing, the timestamp authority)
    sources/tools/pdfapptoolgui_signature.cpp
    sources/tools/pdfapptoolgui_signature.h
    sources/tools/pdfapptoolgui_signbyhand.cpp
    sources/tools/pdfapptoolgui_signbyhand.h
    sources/tools/pdfapptooldrawingpart.cpp
    sources/tools/pdfapptooldrawingpart.h
    sources/tools/pdfapptooltimestampauthoritypart.cpp
    sources/tools/pdfapptooltimestampauthoritypart.h
    # Step 6, package P5 (Redact Content and its search, Manage Certificates and the detail of a
    # certificate)
    sources/tools/pdfapptoolgui_redact.cpp
    sources/tools/pdfapptoolgui_redact.h
    sources/tools/pdfapptoolredactsearchpart.cpp
    sources/tools/pdfapptoolredactsearchpart.h
    sources/tools/pdfapptoolgui_certificates.cpp
    sources/tools/pdfapptoolgui_certificates.h
    sources/tools/pdfappcertificatedetails.cpp
    sources/tools/pdfappcertificatedetails.h
    # Step 7a + 7b (plan 9.1, 9.2 with R7-17): the skeleton (the formatter of the exports), package P2
    # (Document Statistics, Object Inspector, Document Report), package P4 (Ink Coverage, Output Preview,
    # Soft Proofing; the state of the proof of a tab is in document/view.cmake)
    sources/tools/pdfapptoolvalueformatter.cpp
    sources/tools/pdfapptoolvalueformatter.h
    sources/tools/pdfapptoolgui_statistics.cpp
    sources/tools/pdfapptoolgui_statistics.h
    sources/tools/pdfappstatisticstable.cpp
    sources/tools/pdfappstatisticstable.h
    sources/tools/pdfapptoolgui_objectinspector.cpp
    sources/tools/pdfapptoolgui_objectinspector.h
    sources/tools/pdfappobjecttreemodel.cpp
    sources/tools/pdfappobjecttreemodel.h
    sources/tools/pdfapptoolgui_report.cpp
    sources/tools/pdfapptoolgui_report.h
    sources/tools/pdfappreportpreview.cpp
    sources/tools/pdfappreportpreview.h
    sources/tools/pdfapptoolgui_inkcoverage.cpp
    sources/tools/pdfapptoolgui_inkcoverage.h
    sources/tools/pdfapptoolgui_outputpreview.cpp
    sources/tools/pdfapptoolgui_outputpreview.h
    sources/tools/pdfappoutputpreviewcanvas.cpp
    sources/tools/pdfappoutputpreviewcanvas.h
    sources/tools/pdfapptoolgui_softproof.cpp
    sources/tools/pdfapptoolgui_softproof.h
    # Step 7c (plan 9.1, 9.2): package P6 (Compare in its own tab), package P7 (Measure)
    sources/tools/pdfapptoolgui_compare.cpp
    sources/tools/pdfapptoolgui_compare.h
    sources/tools/pdfappcompareview.cpp
    sources/tools/pdfappcompareview.h
    sources/tools/pdfappcomparedifferencemodel.cpp
    sources/tools/pdfappcomparedifferencemodel.h
    sources/tools/pdfapptoolgui_measure.cpp
    sources/tools/pdfapptoolgui_measure.h
    sources/tools/pdfappmeasureoverlay.cpp
    sources/tools/pdfappmeasureoverlay.h
    sources/tools/pdfappmeasuredialogs.cpp
    sources/tools/pdfappmeasuredialogs.h
    # Edit Content, stage E1 (GUI_overhaul/edit_tool/zadani.md 11.3): the files of the skeleton (the coordinator
    # changes them), then the files of the packages P11 (canvas), P12 (tabs Objects and Pages), P13 (cards) and
    # P14 (GUI of the tool, shell, keys)
    sources/tools/pdfappeditcontentregistry.cpp
    sources/tools/pdfappeditcontentregistry.h
    sources/tools/pdfappeditcontentcontext.cpp
    sources/tools/pdfappeditcontentcontext.h
    sources/tools/pdfappeditcontentview.cpp
    sources/tools/pdfappeditcontentview.h
    sources/tools/pdfappeditcontentscene.cpp
    sources/tools/pdfappeditcontentscene.h
    sources/tools/pdfappeditcontentitems.cpp
    sources/tools/pdfappeditcontentitems.h
    sources/tools/pdfappeditcontentselection.cpp
    sources/tools/pdfappeditcontentselection.h
    sources/tools/pdfappeditcontentpreview.cpp
    sources/tools/pdfappeditcontentpreview.h
    sources/tools/pdfappeditcontenttools.cpp
    sources/tools/pdfappeditcontenttools.h
    sources/tools/pdfappeditcontentpanels_objects.cpp
    sources/tools/pdfappeditcontentpanels_objects.h
    sources/tools/pdfappeditcontentpanels_pages.cpp
    sources/tools/pdfappeditcontentpanels_pages.h
    sources/tools/pdfappeditcontentmenus.cpp
    sources/tools/pdfappeditcontentmenus.h
    sources/tools/pdfappeditcontentcards.cpp
    sources/tools/pdfappeditcontentcards.h
    sources/tools/pdfappeditcontentcards_appearance.cpp
    sources/tools/pdfappeditcontentcards_appearance.h
    sources/tools/pdfappeditcontentcards_geometry.cpp
    sources/tools/pdfappeditcontentcards_geometry.h
    sources/tools/pdfapptoolgui_content.cpp
    sources/tools/pdfapptoolgui_content.h
    sources/tools/pdfappeditcontentnotices.cpp
    sources/tools/pdfappeditcontentnotices.h
    sources/tools/pdfappeditcontentundo.cpp
    sources/tools/pdfappeditcontentundo.h
    sources/tools/pdfappeditcontentkeyboard.cpp
    sources/tools/pdfappeditcontentkeyboard.h
)
