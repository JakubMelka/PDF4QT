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

# Smoke test of the command 'PdfTool ocr' with the Tesseract engine and the built-in
# language models (phase 7 of OCR_PLAN.md). A text document is rasterized into a scan
# by 'PdfTool bitonal', the scan is recognized, the text layer, the exports, the project
# and a batch are checked.
#
#   cmake -DPDFTOOL=<PdfTool executable> -DWORK_DIR=<directory> [-DOCR_REQUIRED=ON] -P pdftoolocrsmoke.cmake
#
# Missing engine or models skip the test (the output contains PDF4QT_OCR_SKIP), unless
# OCR_REQUIRED is set (release gate).

if(NOT PDFTOOL OR NOT WORK_DIR)
    message(FATAL_ERROR "PDFTOOL and WORK_DIR must be set.")
endif()

file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}")

# Document with a text of two pages (the offsets of the cross reference table are computed)
set(objects
    "<< /Type /Catalog /Pages 2 0 R >>"
    "<< /Type /Pages /Kids [3 0 R 5 0 R] /Count 2 >>"
    "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 595 842] /Resources << /Font << /F1 7 0 R >> >> /Contents 4 0 R >>"
    "STREAM:BT /F1 24 Tf 60 760 Td (The quick brown fox) Tj ET BT /F1 24 Tf 60 720 Td (jumps over the lazy dog) Tj ET"
    "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 595 842] /Resources << /Font << /F1 7 0 R >> >> /Contents 6 0 R >>"
    "STREAM:BT /F1 24 Tf 60 760 Td (Second page of the scan) Tj ET"
    "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>"
)

set(pdf "%PDF-1.7\n")
set(offsets "")
set(number 0)
foreach(object IN LISTS objects)
    math(EXPR number "${number} + 1")
    string(LENGTH "${pdf}" offset)
    list(APPEND offsets ${offset})
    if(object MATCHES "^STREAM:(.*)$")
        set(content "${CMAKE_MATCH_1}")
        string(LENGTH "${content}" length)
        string(APPEND pdf "${number} 0 obj\n<< /Length ${length} >>\nstream\n${content}\nendstream\nendobj\n")
    else()
        string(APPEND pdf "${number} 0 obj\n${object}\nendobj\n")
    endif()
endforeach()
string(LENGTH "${pdf}" xrefOffset)
math(EXPR size "${number} + 1")
string(APPEND pdf "xref\n0 ${size}\n0000000000 65535 f \n")
foreach(offset IN LISTS offsets)
    string(LENGTH "${offset}" digits)
    math(EXPR padding "10 - ${digits}")
    string(REPEAT "0" ${padding} zeros)
    string(APPEND pdf "${zeros}${offset} 00000 n \n")
endforeach()
string(APPEND pdf "trailer\n<< /Size ${size} /Root 1 0 R >>\nstartxref\n${xrefOffset}\n%%EOF\n")
file(WRITE "${WORK_DIR}/text.pdf" "${pdf}")

set(models "${WORK_DIR}/models")

function(run_pdftool expectedExitCode)
    execute_process(COMMAND "${PDFTOOL}" ${ARGN}
                    WORKING_DIRECTORY "${WORK_DIR}"
                    RESULT_VARIABLE exitCode
                    OUTPUT_VARIABLE output
                    ERROR_VARIABLE errorOutput)
    set(LAST_OUTPUT "${output}" PARENT_SCOPE)
    set(LAST_ERROR "${errorOutput}" PARENT_SCOPE)
    if(NOT "${exitCode}" STREQUAL "${expectedExitCode}")
        message(FATAL_ERROR "PdfTool ${ARGN}\nexit code ${exitCode}, expected ${expectedExitCode}\n${output}\n${errorOutput}")
    endif()
endfunction()

function(require_text file text)
    file(READ "${WORK_DIR}/${file}" content)
    string(FIND "${content}" "${text}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "The file ${file} does not contain '${text}':\n${content}")
    endif()
endfunction()

# Scan of the document
run_pdftool(0 bitonal text.pdf scan.pdf --bitonal-dpi 300)

# The engine and the models: a missing one skips the test (never downloaded)
execute_process(COMMAND "${PDFTOOL}" ocr scan.pdf -o out.pdf --languages eng --quiet --ocr-data-dir "${models}"
                        --export-txt out.txt --export-alto out.xml --export-hocr out.hocr --export-tsv out.tsv --save-project out.pdf4qt-ocr
                WORKING_DIRECTORY "${WORK_DIR}"
                RESULT_VARIABLE exitCode
                OUTPUT_VARIABLE output
                ERROR_VARIABLE errorOutput)
if(NOT exitCode EQUAL 0)
    if(errorOutput MATCHES "is not available|is not installed|not found" AND NOT OCR_REQUIRED)
        message("PDF4QT_OCR_SKIP: ${errorOutput}")
        return()
    endif()
    message(FATAL_ERROR "Recognition failed (${exitCode}):\n${output}\n${errorOutput}")
endif()

# Report, text layer and exports
string(FIND "${output}" "Recognized" position)
if(position EQUAL -1)
    message(FATAL_ERROR "The report does not list the recognized pages:\n${output}")
endif()
require_text(out.txt "quick brown fox")
require_text(out.txt "Second page")
require_text(out.xml "<alto")
require_text(out.hocr "ocrx_word")
require_text(out.tsv "brown")
require_text(out.pdf4qt-ocr "pdf4qt-ocr-project")

run_pdftool(0 fetch-text out.pdf)
string(FIND "${LAST_OUTPUT}" "brown" position)
if(position EQUAL -1)
    message(FATAL_ERROR "The text layer of the output is not extracted:\n${LAST_OUTPUT}")
endif()

# The own layer is never recognized again by the default policy (ErrorNoText = 10),
# the replacement is explicit
run_pdftool(10 ocr out.pdf -o again.pdf --languages eng --quiet --ocr-data-dir "${models}")
run_pdftool(0 ocr out.pdf -o again.pdf --languages eng --quiet --existing-text replace-own --ocr-data-dir "${models}")

# The project is used for the pages with the same content, its configuration is used
run_pdftool(0 ocr scan.pdf --export-only --export-txt project.txt --project out.pdf4qt-ocr --quiet --ocr-data-dir "${models}")
require_text(project.txt "quick brown fox")
string(FIND "${LAST_OUTPUT}" "project" position)
if(position EQUAL -1)
    message(FATAL_ERROR "The pages were not taken from the project:\n${LAST_OUTPUT}")
endif()

# The source is never overwritten, invalid values are refused (ErrorInvalidArguments = 7)
run_pdftool(7 ocr scan.pdf -o scan.pdf --languages eng)
run_pdftool(7 ocr scan.pdf -o x.pdf --languages eng --compression zip)
run_pdftool(7 ocr scan.pdf -o x.pdf)

# Lossless compression of the scan
run_pdftool(0 ocr scan.pdf -o compressed.pdf --languages eng --compression lossless --quiet --ocr-data-dir "${models}")

# Batch: a file without a text is recognized, a broken file fails the batch
file(MAKE_DIRECTORY "${WORK_DIR}/batch")
file(COPY_FILE "${WORK_DIR}/scan.pdf" "${WORK_DIR}/batch/a.pdf")
file(COPY_FILE "${WORK_DIR}/scan.pdf" "${WORK_DIR}/batch/b.pdf")
run_pdftool(0 ocr --batch batch --output-dir batchout --languages eng --batch-export txt --quiet --ocr-data-dir "${models}")
foreach(file a_ocr.pdf a_ocr.txt b_ocr.pdf b_ocr.txt)
    if(NOT EXISTS "${WORK_DIR}/batchout/${file}")
        message(FATAL_ERROR "The batch did not create ${file}:\n${LAST_OUTPUT}\n${LAST_ERROR}")
    endif()
endforeach()
require_text(batchout/b_ocr.txt "quick brown fox")

run_pdftool(0 ocr --batch batch --output-dir batchout --languages eng --batch-export txt --skip-existing --quiet --ocr-data-dir "${models}")
string(FIND "${LAST_OUTPUT}" "skipped: 2" position)
if(position EQUAL -1)
    message(FATAL_ERROR "The existing outputs were not skipped:\n${LAST_OUTPUT}")
endif()

file(WRITE "${WORK_DIR}/batch/broken.pdf" "not a PDF document")
run_pdftool(1 ocr --batch batch --output-dir batchout2 --languages eng --continue-on-error --quiet --ocr-data-dir "${models}")
foreach(file a_ocr.pdf b_ocr.pdf)
    if(NOT EXISTS "${WORK_DIR}/batchout2/${file}")
        message(FATAL_ERROR "The batch did not continue after the error:\n${LAST_OUTPUT}")
    endif()
endforeach()

# Language models: the list works offline, the download needs the consent
run_pdftool(0 ocr-models list --profile fast eng --ocr-data-dir "${models}")
string(FIND "${LAST_OUTPUT}" "eng" position)
if(position EQUAL -1)
    message(FATAL_ERROR "The built-in model is not listed:\n${LAST_OUTPUT}")
endif()
run_pdftool(7 ocr-models install deu --profile best --ocr-data-dir "${models}")

message("PdfTool OCR smoke test passed.")
