# Downloads a whisper.cpp model for the viewer's voice input.
#
#     cmake -P fetch_whisper_model.cmake                  # base.en (148 MB, English)
#     cmake -DMODEL=base -P fetch_whisper_model.cmake     # multilingual
#     cmake -DMODEL=small.en -P fetch_whisper_model.cmake # more accurate, slower (488 MB)
#
# Plain CMake, so it runs the same on macOS, Linux and Windows. The file lands
# in models/ next to this script, where the viewer looks for ggml-base.en.bin;
# for any other model, point ROSETTA_WHISPER_MODEL at the file it prints.
if(NOT MODEL)
    set(MODEL "base.en")
endif()

set(_dir  "${CMAKE_CURRENT_LIST_DIR}/models")
set(_file "${_dir}/ggml-${MODEL}.bin")
set(_url  "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-${MODEL}.bin")

if(EXISTS "${_file}")
    message(STATUS "Already there: ${_file}")
    return()
endif()

file(MAKE_DIRECTORY "${_dir}")
message(STATUS "Downloading ${_url}")
file(DOWNLOAD "${_url}" "${_file}.part" SHOW_PROGRESS STATUS _status TLS_VERIFY ON)
list(GET _status 0 _code)
if(NOT _code EQUAL 0)
    list(GET _status 1 _why)
    file(REMOVE "${_file}.part")
    message(FATAL_ERROR "Download failed: ${_why}")
endif()
file(RENAME "${_file}.part" "${_file}")
message(STATUS "Saved ${_file}")
if(NOT MODEL STREQUAL "base.en")
    message(STATUS "Use it with:  ROSETTA_WHISPER_MODEL=${_file}")
endif()
