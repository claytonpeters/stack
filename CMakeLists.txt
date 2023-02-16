cmake_minimum_required(VERSION 3.12)
project(stack)
list(APPEND CMAKE_CXX_FLAGS "-std=c++0x -O3 -Werror=return-type")
#set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/build)
add_executable(runstack StackRingBuffer.cpp StackCue.cpp StackCueBase.cpp StackCueHelper.cpp StackCueList.cpp StackApp.cpp StackWindow.cpp StackShowSettings.cpp main.cpp StackAudioDevice.cpp StackRenumberCue.cpp StackResampler.cpp)
set_target_properties(runstack PROPERTIES ENABLE_EXPORTS TRUE)
#set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/build)
add_library(StackPulseAudioDevice SHARED StackPulseAudioDevice.cpp)
add_library(StackActionCue SHARED StackActionCue.cpp)
add_library(StackFadeCue SHARED StackFadeCue.cpp)
add_library(StackAudioCue SHARED StackAudioCue.cpp StackAudioFile.cpp StackAudioFileWave.cpp StackAudioFileMP3.cpp MPEGAudioFile.cpp)
include(FindPkgConfig)
include(FindPackageHandleStandardArgs)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK3 REQUIRED gtk+-3.0)
find_package(PulseAudio REQUIRED)
find_package(Threads REQUIRED)
find_package(ALSA)
pkg_check_modules(JSONCPP REQUIRED jsoncpp)

## We need libdl for our plugin loading support
find_library(DL_LIBRARIES dl REQUIRED)

## MAD doesn't ship with a CMake file
find_path(MAD_INCLUDE_DIRS mad.h)
find_library(MAD_LIBRARIES mad)
find_package_handle_standard_args(MAD DEFAULT_MSG MAD_INCLUDE_DIRS MAD_LIBRARIES)
mark_as_advanced(MAD_INCLUDE_DIRS MAD_LIBRARIES MAD_DEFINITIONS)

## SOXR doesn't ship with a CMake file
find_path(SOXR_INCLUDE_DIRS soxr.h)
find_library(SOXR_LIBRARIES soxr)
find_package_handle_standard_args(SOXR DEFAULT_MSG SOXR_INCLUDE_DIRS SOXR_LIBRARIES)
mark_as_advanced(SOXR_INCLUDE_DIRS SOXR_LIBRARIES SOXR_DEFINITIONS)

## Required libraries
include_directories("${PROJECT_SOURCE_DIR}")
include_directories(${GTK3_INCLUDE_DIRS})
include_directories(${PULSEAUDIO_INCLUDE_DIRS})
include_directories(${JSONCPP_INCLUDE_DIRS})
link_directories(${GTK3_LIBRARY_DIRS})
link_directories(${PULSEAUDIO_LIBRARY_DIR})
link_directories(${JSONCPP_LIBRARY_DIRS})
add_definitions(${GTK3_CFLAGS_OTHER})
target_link_libraries(StackPulseAudioDevice ${PULSEAUDIO_LIBRARY})
target_link_libraries(StackPulseAudioDevice ${PULSEAUDIO_MAINLOOP_LIBRARY})
target_link_libraries(runstack ${GTK3_LIBRARIES})
target_link_libraries(runstack ${CMAKE_THREAD_LIBS_INIT})
target_link_libraries(runstack ${JSONCPP_LIBRARIES})
target_link_libraries(runstack ${DL_LIBRARIES})

# Optional: Only build StackAlsaAudioDevice if ALSA was found
if (ALSA_FOUND)
	add_library(StackAlsaAudioDevice SHARED StackAlsaAudioDevice.cpp)
	include_directories(${ALSA_INCLUDE_DIRS})
	link_directories(${ALSA_LIBRARY_DIRS})
	target_link_libraries(StackAlsaAudioDevice ${ALSA_LIBRARY})
endif()

# Optional: Add in MAD if it was found
if (MAD_FOUND)
	list(APPEND MAD_DEFINITIONS -DHAVE_LIBMAD=1)
	include_directories(${MAD_INCLUDE_DIRS})
	add_definitions(${MAD_DEFINITIONS})
	target_link_libraries(StackAudioCue ${MAD_LIBRARIES})
endif()

# Optional: Add in SOXR if it was found
if (SOXR_FOUND)
	include_directories(${SOXR_INCLUDE_DIRS})
	list(APPEND SOXR_DEFINITIONS -DHAVE_LIBSOXR=1)
	add_definitions(${SOXR_DEFINITIONS})
	target_link_libraries(runstack ${SOXR_LIBRARIES})
endif()
