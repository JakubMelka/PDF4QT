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

# The command 'PdfTool optimize' is one implementation with the tool Compress of the new
# application (owner decision 8). The old options work; the output keeps the version of the
# input (the header of the optimizers said PDF 2.0) and an unused object is gone. A document
# with a declaration of PDF/A loses it and the command says so.
#
#   cmake -DPDFTOOL=<PdfTool executable> -DWORK_DIR=<directory> -P pdftooloptimizesmoke.cmake

if(NOT PDFTOOL OR NOT WORK_DIR)
    message(FATAL_ERROR "PDFTOOL and WORK_DIR must be set.")
endif()

file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}")

# Writes a document of the objects (the offsets of the cross reference table are computed)
function(write_document fileName)
    set(pdf "%PDF-1.4\n")
    set(offsets "")
    set(number 0)
    foreach(object IN LISTS ARGN)
        math(EXPR number "${number} + 1")
        string(LENGTH "${pdf}" offset)
        list(APPEND offsets ${offset})
        if(object MATCHES "^IMAGE:([0-9]+):(.*)$")
            string(APPEND pdf "${number} 0 obj
<< /Type /XObject /Subtype /Image /Width ${CMAKE_MATCH_1} /Height ${CMAKE_MATCH_1} /BitsPerComponent 8 /ColorSpace /DeviceGray /Filter /ASCIIHexDecode /Length ${imageLength} >>
stream
${CMAKE_MATCH_2}
endstream
endobj
")
        elseif(object MATCHES "^STREAM:(.*)$")
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
    file(WRITE "${WORK_DIR}/${fileName}" "${pdf}")
endfunction()

set(pageObjects
    "<< /Type /Pages /Kids [3 0 R] /Count 1 >>"
    "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 595 842] /Contents 4 0 R >>"
    "STREAM:0 0 m 100 100 l S"
    "<< /Unused (the object the clean-up removes) >>"
)

write_document("plain.pdf" "<< /Type /Catalog /Pages 2 0 R >>" ${pageObjects})

execute_process(COMMAND "${PDFTOOL}" optimize plain.pdf --opt-remove-unused
                WORKING_DIRECTORY "${WORK_DIR}"
                RESULT_VARIABLE exitCode
                OUTPUT_VARIABLE output
                ERROR_VARIABLE output)
if(NOT exitCode EQUAL 0)
    message(FATAL_ERROR "PdfTool optimize failed (${exitCode}): ${output}")
endif()
if(NOT output MATCHES "removed")
    message(FATAL_ERROR "The texts of the stages of the clean-up are written: ${output}")
endif()

file(READ "${WORK_DIR}/plain.pdf" written)
string(SUBSTRING "${written}" 0 8 header)
if(NOT header STREQUAL "%PDF-1.4")
    message(FATAL_ERROR "The output keeps the version of the input, it has the header ${header}.")
endif()
if(written MATCHES "the object the clean-up removes")
    message(FATAL_ERROR "The unused object is still in the output.")
endif()

# Without an option nothing is run and nothing is written
file(READ "${WORK_DIR}/plain.pdf" before)
execute_process(COMMAND "${PDFTOOL}" optimize plain.pdf
                WORKING_DIRECTORY "${WORK_DIR}"
                RESULT_VARIABLE exitCode
                OUTPUT_VARIABLE output
                ERROR_VARIABLE output)
if(exitCode EQUAL 0)
    message(FATAL_ERROR "PdfTool optimize without an option has to fail: ${output}")
endif()
if(NOT output MATCHES "No optimization option")
    message(FATAL_ERROR "PdfTool optimize without an option has to say why: ${output}")
endif()
file(READ "${WORK_DIR}/plain.pdf" after)
if(NOT before STREQUAL after)
    message(FATAL_ERROR "PdfTool optimize without an option has changed the file.")
endif()

# A missing file: the error of the reading
execute_process(COMMAND "${PDFTOOL}" optimize missing.pdf --opt-remove-unused
                WORKING_DIRECTORY "${WORK_DIR}"
                RESULT_VARIABLE exitCode
                OUTPUT_QUIET
                ERROR_QUIET)
if(exitCode EQUAL 0)
    message(FATAL_ERROR "PdfTool optimize of a missing file has to fail.")
endif()

# The options of the images: 100 x 100 samples on one inch of the page, to 50 dpi by Flate
string(REPEAT "10F0" 5000 samples)
string(LENGTH "${samples}" imageLength)
write_document("image.pdf"
    "<< /Type /Catalog /Pages 2 0 R >>"
    "<< /Type /Pages /Kids [3 0 R] /Count 1 >>"
    "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 595 842] /Contents 4 0 R /Resources << /XObject << /Im 5 0 R >> >> >>"
    "STREAM:q 72 0 0 72 0 0 cm /Im Do Q"
    "IMAGE:100:${samples}"
)

execute_process(COMMAND "${PDFTOOL}" optimize image.pdf --opt-images --opt-images-mode custom --opt-images-color-mode gray --opt-images-gray-alg flate --opt-images-gray-dpi 50 --opt-images-keep-original 0
                WORKING_DIRECTORY "${WORK_DIR}"
                RESULT_VARIABLE exitCode
                OUTPUT_VARIABLE output
                ERROR_VARIABLE output)
if(NOT exitCode EQUAL 0)
    message(FATAL_ERROR "PdfTool optimize of the images failed (${exitCode}): ${output}")
endif()
file(READ "${WORK_DIR}/image.pdf" written)
if(written MATCHES "ASCIIHexDecode")
    message(FATAL_ERROR "The image is not encoded by the options.")
endif()
if(NOT written MATCHES "/FlateDecode")
    message(FATAL_ERROR "--opt-images-gray-alg flate is not used.")
endif()
if(NOT written MATCHES "/Width 50[^0-9]")
    message(FATAL_ERROR "--opt-images-gray-dpi 50 is not used.")
endif()

# A declaration of PDF/A, the document is rewritten and not checked against the standard
string(CONCAT xmp "STREAM:<x:xmpmeta xmlns:x=\"adobe:ns:meta/\"><rdf:RDF xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\">"
                  "<rdf:Description rdf:about=\"\" xmlns:pdfaid=\"http://www.aiim.org/pdfa/ns/id/\"><pdfaid:part>1</pdfaid:part></rdf:Description>"
                  "</rdf:RDF></x:xmpmeta>")
write_document("pdfa.pdf" "<< /Type /Catalog /Pages 2 0 R /Metadata 6 0 R >>" ${pageObjects} "${xmp}")

execute_process(COMMAND "${PDFTOOL}" optimize pdfa.pdf --opt-remove-unused
                WORKING_DIRECTORY "${WORK_DIR}"
                RESULT_VARIABLE exitCode
                OUTPUT_VARIABLE output
                ERROR_VARIABLE output)
if(NOT exitCode EQUAL 0)
    message(FATAL_ERROR "PdfTool optimize of PDF/A failed (${exitCode}): ${output}")
endif()
if(NOT output MATCHES "PDF/A")
    message(FATAL_ERROR "The command says, that the declaration was removed: ${output}")
endif()
file(READ "${WORK_DIR}/pdfa.pdf" written)
if(written MATCHES "pdfaid:part")
    message(FATAL_ERROR "The declaration of PDF/A is still in the output.")
endif()
