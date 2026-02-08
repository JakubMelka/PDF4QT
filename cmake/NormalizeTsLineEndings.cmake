if(NOT DEFINED TS_OUTPUT_DIRECTORY)
    message(FATAL_ERROR "TS_OUTPUT_DIRECTORY is not defined.")
endif()

file(GLOB TS_FILES "${TS_OUTPUT_DIRECTORY}/*.ts")

foreach(TS_FILE IN LISTS TS_FILES)
    file(READ "${TS_FILE}" TS_CONTENT)
    # Normalize all known line ending variants to LF first.
    string(REPLACE "\r\r\n" "\n" TS_CONTENT "${TS_CONTENT}")
    string(REPLACE "\r\n" "\n" TS_CONTENT "${TS_CONTENT}")
    string(REPLACE "\r" "\n" TS_CONTENT "${TS_CONTENT}")

    # file(WRITE) on Windows writes LF as CRLF; keep LF here to avoid CRCRLF.
    file(WRITE "${TS_FILE}" "${TS_CONTENT}")
endforeach()
