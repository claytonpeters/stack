cmake_minimum_required(VERSION 3.12)
project(runstack)
list(APPEND CMAKE_CXX_FLAGS "-std=c++0x -O3 -Werror=return-type")

# Add in program for compiling resources
find_program(GLIB_COMPILE_RESOURCES NAMES glib-compile-resources REQUIRED)

# Compile resource target: Stack
add_custom_command(
	OUTPUT src/resources.c
	WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
	COMMAND ${GLIB_COMPILE_RESOURCES}
	ARGS --generate-source --target=src/resources.c src/resources.xml
	VERBATIM
	MAIN_DEPENDENCY src/resources.xml
	DEPENDS ui/SelectTriggerType.ui ui/SelectCue.ui ui/StackGroupCue.ui ui/StackLoading.ui ui/StackRenumberCue.ui ui/StackShowSettings.ui ui/window.ui ui/stack.css
)
add_custom_target(resources-target DEPENDS src/resources.c)
set_source_files_properties(src/resources.c PROPERTIES GENERATED TRUE)

# Compile resource target: StackActionCue
add_custom_command(
	OUTPUT src/stackactioncue-resources.c
	WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
	COMMAND ${GLIB_COMPILE_RESOURCES}
	ARGS --generate-source --target=src/stackactioncue-resources.c src/stackactioncue-resources.xml
	VERBATIM
	MAIN_DEPENDENCY src/stackactioncue-resources.xml
	DEPENDS icons/stackactioncue-pause.png icons/stackactioncue-play.png icons/stackactioncue-stop.png ui/StackActionCue.ui
)
add_custom_target(stackactioncue-resources-target DEPENDS src/stackactioncue-resources.c)
set_source_files_properties(src/stackactioncue-resources.c PROPERTIES GENERATED TRUE)

# Compile resource target: StackAudioCue
add_custom_command(
	OUTPUT src/stackaudiocue-resources.c
	WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
	COMMAND ${GLIB_COMPILE_RESOURCES}
	ARGS --generate-source --target=src/stackaudiocue-resources.c src/stackaudiocue-resources.xml
	VERBATIM
	MAIN_DEPENDENCY src/stackaudiocue-resources.xml
	DEPENDS icons/stackaudiocue.png ui/StackAudioCue.ui
)
add_custom_target(stackaudiocue-resources-target DEPENDS src/stackaudiocue-resources.c)
set_source_files_properties(src/stackaudiocue-resources.c PROPERTIES GENERATED TRUE)

# Compile resource target: StackExecCue
add_custom_command(
	OUTPUT src/stackexeccue-resources.c
	WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
	COMMAND ${GLIB_COMPILE_RESOURCES}
	ARGS --generate-source --target=src/stackexeccue-resources.c src/stackexeccue-resources.xml
	VERBATIM
	MAIN_DEPENDENCY src/stackexeccue-resources.xml
	DEPENDS icons/stackexeccue.png ui/StackExecCue.ui
)
add_custom_target(stackexeccue-resources-target DEPENDS src/stackexeccue-resources.c)
set_source_files_properties(src/stackexecue-resources.c PROPERTIES GENERATED TRUE)

# Compile resource target: StackFadeCue
add_custom_command(
	OUTPUT src/stackfadecue-resources.c
	WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
	COMMAND ${GLIB_COMPILE_RESOURCES}
	ARGS --generate-source --target=src/stackfadecue-resources.c src/stackfadecue-resources.xml
	VERBATIM
	MAIN_DEPENDENCY src/stackfadecue-resources.xml
	DEPENDS icons/stackfadecue.png ui/StackFadeCue.ui
)
add_custom_target(stackfadecue-resources-target DEPENDS src/stackfadecue-resources.c)
set_source_files_properties(src/stackfadecue-resources.c PROPERTIES GENERATED TRUE)

# Compile resource target: StackKeyTrigger
add_custom_command(
	OUTPUT src/stackkeytrigger-resources.c
	WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
	COMMAND ${GLIB_COMPILE_RESOURCES}
	ARGS --generate-source --target=src/stackkeytrigger-resources.c src/stackkeytrigger-resources.xml
	VERBATIM
	MAIN_DEPENDENCY src/stackkeytrigger-resources.xml
	DEPENDS ui/StackKeyTrigger.ui
)
add_custom_target(stackkeytrigger-resources-target DEPENDS src/stackkeytrigger-resources.c)
set_source_files_properties(src/stackkeytrigger-resources.c PROPERTIES GENERATED TRUE)

# Compile resource target: StackTimeTrigger
add_custom_command(
	OUTPUT src/stacktimetrigger-resources.c
	WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
	COMMAND ${GLIB_COMPILE_RESOURCES}
	ARGS --generate-source --target=src/stacktimetrigger-resources.c src/stacktimetrigger-resources.xml
	VERBATIM
	MAIN_DEPENDENCY src/stacktimetrigger-resources.xml
	DEPENDS ui/StackTimeTrigger.ui
)
add_custom_target(stacktimetrigger-resources-target DEPENDS src/stacktimetrigger-resources.c)
set_source_files_properties(src/stacktimetrigger-resources.c PROPERTIES GENERATED TRUE)

set(STACK_SOURCES src/StackLog.cpp src/StackProperty.cpp src/StackRingBuffer.cpp src/StackGtkEntryHelper.cpp src/StackCue.cpp src/StackCueBase.cpp src/StackCueHelper.cpp src/StackCueList.cpp src/StackTrigger.cpp src/StackGroupCue.cpp src/StackApp.cpp src/StackWindow.cpp src/StackCueListWidget.cpp src/StackShowSettings.cpp src/main.cpp src/StackAudioDevice.cpp src/StackRenumberCue.cpp src/StackResampler.cpp src/StackLevelMeter.cpp src/StackAudioPreview.cpp src/StackAudioFile.cpp src/StackAudioFileWave.cpp src/StackAudioFileMP3.cpp src/MPEGAudioFile.cpp src/resources.c)
#set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/build)
#set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/build)
add_library(StackPulseAudioDevice SHARED src/StackPulseAudioDevice.cpp)
add_library(StackActionCue SHARED src/StackActionCue.cpp src/stackactioncue-resources.c)
add_dependencies(StackActionCue stackactioncue-resources-target)
add_library(StackAudioCue SHARED src/StackAudioCue.cpp src/stackaudiocue-resources.c)
add_dependencies(StackAudioCue stackaudiocue-resources-target)
add_library(StackFadeCue SHARED src/StackFadeCue.cpp src/stackfadecue-resources.c)
add_dependencies(StackFadeCue stackfadecue-resources-target)
add_library(StackExecCue SHARED src/StackExecCue.cpp src/stackexeccue-resources.c)
add_dependencies(StackExecCue stackexeccue-resources-target)
add_library(StackKeyTrigger SHARED src/StackKeyTrigger.cpp src/stackkeytrigger-resources.c)
add_library(StackTimeTrigger SHARED src/StackTimeTrigger.cpp src/stacktimetrigger-resources.c)
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

## Protobuf-c doesn't ship with a CMakeFile
find_path(PROTOBUF_C_INCLUDE_DIRS protobuf-c.h)
find_library(PROTOBUF_C_LIBRARIES protobuf-c)
find_package_handle_standard_args(PROTOBUF_C PROTOBUF_C_INCLUDE_DIRS PROTOBUF_C_LIBRARIES)
mark_as_advanced(PROTOBUF_C_INCLUDE_DIRS PROTOBUF_C_LIBRARIES PROTOBUF_DEFINITIONS)

## Required libraries
include_directories("${PROJECT_SOURCE_DIR}")
include_directories(${GTK3_INCLUDE_DIRS})
include_directories(${PULSEAUDIO_INCLUDE_DIRS})
include_directories(${JSONCPP_INCLUDE_DIRS})
link_directories(${GTK3_LIBRARY_DIRS})
link_directories(${PULSEAUDIO_LIBRARY_DIR})
link_directories(${JSONCPP_LIBRARY_DIRS})
add_definitions(${GTK3_CFLAGS_OTHER})

# Optional: Add in StackRPC code if we have protobuf-c
if (PROTOBUF_C_FOUND)
	list(APPEND STACK_SOURCES src/StackRPC.pb-c.c src/StackRPCSocket.cpp)
endif()

add_executable(runstack ${STACK_SOURCES})
set_target_properties(runstack PROPERTIES ENABLE_EXPORTS TRUE)
add_dependencies(runstack resources-target)

target_link_libraries(StackPulseAudioDevice ${PULSEAUDIO_LIBRARY})
target_link_libraries(StackPulseAudioDevice ${PULSEAUDIO_MAINLOOP_LIBRARY})
target_link_libraries(runstack ${GTK3_LIBRARIES})
target_link_libraries(runstack ${CMAKE_THREAD_LIBS_INIT})
target_link_libraries(runstack ${JSONCPP_LIBRARIES})
target_link_libraries(runstack ${DL_LIBRARIES})

# Optional: Only build StackAlsaAudioDevice if ALSA was found
if (ALSA_FOUND)
	add_library(StackAlsaAudioDevice SHARED src/StackAlsaAudioDevice.cpp)
	if (ALSA_INCLUDE_DIRS)
		include_directories(${ALSA_INCLUDE_DIRS})
	endif()
	link_directories(${ALSA_LIBRARY_DIRS})
	target_link_libraries(StackAlsaAudioDevice ${ALSA_LIBRARY})
endif()

# Optional: Add in MAD if it was found
if (MAD_FOUND)
	list(APPEND MAD_DEFINITIONS -DHAVE_LIBMAD=1)
	if (MAD_INCLUDE_DIRS)
		include_directories(${MAD_INCLUDE_DIRS})
	endif()
	add_definitions(${MAD_DEFINITIONS})
	target_link_libraries(runstack ${MAD_LIBRARIES})
endif()

# Optional: Add in SOXR if it was found
if (SOXR_FOUND)
	list(APPEND SOXR_DEFINITIONS -DHAVE_LIBSOXR=1)
	if (SOXR_INCLUDE_DIRS)
		include_directories(${SOXR_INCLUDE_DIRS})
	endif()
	add_definitions(${SOXR_DEFINITIONS})
	target_link_libraries(runstack ${SOXR_LIBRARIES})
endif()

# Optional: Add in protobuf-C if it was found
if (PROTOBUF_C_FOUND)
	list(APPEND PROTOBUF_C_DEFINITIONS -DHAVE_LIBPROTOBUF_C=1)
	if (PROTOBUF_C_INCLUDE_DIRS)
		include_directories(${PROTOBUF_C_INCLUDE_DIRS})
	endif()
	add_definitions(${PROTOBUF_C_DEFINITIONS})
	target_link_libraries(runstack ${PROTOBUF_C_LIBRARIES})
endif()
