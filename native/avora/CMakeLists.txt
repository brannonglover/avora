# Avora browser example for cef-project.

set(EXAMPLE_SRCS
  app_browser_impl.cc
  avora_browser_manager.cc
  client_avora.cc
  main_avora.cc
  avora_browser_manager.h
  avora_layout.h
  client_avora.h
  )
set(EXAMPLE_SRCS_WINDOWS
  resource.h
  resource_util_win_impl.cc
  )
APPEND_PLATFORM_SOURCES(EXAMPLE_SRCS)

if(OS_LINUX OR OS_WINDOWS)
  set(EXAMPLE_SRCS
    ${EXAMPLE_SRCS}
    ../minimal/app_other_minimal.cc
    app_renderer_impl.cc
    )
elseif(OS_MAC)
  set(EXAMPLE_HELPER_SRCS
    ../minimal/app_other_minimal.cc
    ../minimal/process_helper_mac_minimal.cc
    app_renderer_impl.cc
    )
endif()

set(EXAMPLE_RESOURCES_SRCS
  resources/index.html
  resources/shell.js
  resources/styles.css
  )
set(EXAMPLE_RESOURCES_SRCS_WINDOWS
  win/resource.rc
  )
APPEND_PLATFORM_SOURCES(EXAMPLE_RESOURCES_SRCS)

if(OS_MACOSX OR OS_WINDOWS)
  set(EXAMPLE_RESOURCES_SRCS
    ${EXAMPLE_RESOURCES_SRCS}
    ${SHARED_RESOURCES_SRCS}
    )
endif()

set(EXAMPLE_TARGET "Avora")
if(OS_MAC)
  set(EXAMPLE_HELPER_TARGET "Avora_Helper")
  set(EXAMPLE_HELPER_OUTPUT_NAME "Avora Helper")
endif()

if(OS_LINUX)
  add_executable(${EXAMPLE_TARGET} ${EXAMPLE_SRCS})
  SET_EXAMPLE_EXECUTABLE_TARGET_PROPERTIES(${EXAMPLE_TARGET})
  add_dependencies(${EXAMPLE_TARGET} shared)
  target_link_libraries(${EXAMPLE_TARGET} shared)
  COPY_FILES("${EXAMPLE_TARGET}" "${EXAMPLE_RESOURCES_SRCS}" "${CMAKE_CURRENT_SOURCE_DIR}" "${EXAMPLE_TARGET_OUT_DIR}/${EXAMPLE_TARGET}_files")
endif()

if(OS_MAC)
  SET_EXAMPLE_SOURCE_GROUPS("${EXAMPLE_SRCS}")
  SET_EXAMPLE_SOURCE_GROUPS("${EXAMPLE_HELPER_SRCS}")

  set(EXAMPLE_APP "${EXAMPLE_TARGET_OUT_DIR}/${EXAMPLE_TARGET}.app")
  set(EXECUTABLE_NAME "${EXAMPLE_TARGET}")
  set(PRODUCT_NAME "${EXAMPLE_TARGET}")

  add_executable(${EXAMPLE_TARGET} MACOSX_BUNDLE ${EXAMPLE_SRCS} ${EXAMPLE_RESOURCES_SRCS})
  SET_EXAMPLE_EXECUTABLE_TARGET_PROPERTIES(${EXAMPLE_TARGET})
  add_dependencies(${EXAMPLE_TARGET} shared)
  target_link_libraries(${EXAMPLE_TARGET} shared)
  set_target_properties(${EXAMPLE_TARGET} PROPERTIES
    RESOURCE "${EXAMPLE_RESOURCES_SRCS}"
    MACOSX_BUNDLE_INFO_PLIST "${SHARED_INFO_PLIST}"
    )

  COPY_MAC_FRAMEWORK("${EXAMPLE_TARGET}" "${CEF_BINARY_DIR}" "${EXAMPLE_APP}")

  foreach(_suffix_list ${CEF_HELPER_APP_SUFFIXES})
    string(REPLACE ":" ";" _suffix_list ${_suffix_list})
    list(GET _suffix_list 0 _name_suffix)
    list(GET _suffix_list 1 _target_suffix)
    list(GET _suffix_list 2 _plist_suffix)

    set(_helper_target "${EXAMPLE_HELPER_TARGET}${_target_suffix}")
    set(_helper_output_name "${EXAMPLE_HELPER_OUTPUT_NAME}${_name_suffix}")

    set(_helper_info_plist "${CMAKE_CURRENT_BINARY_DIR}/helper-Info${_target_suffix}.plist.in")
    file(READ "${SHARED_HELPER_INFO_PLIST}" _plist_contents)
    string(REPLACE "\${EXECUTABLE_NAME}" "${_helper_output_name}" _plist_contents ${_plist_contents})
    string(REPLACE "\${PRODUCT_NAME}" "${_helper_output_name}" _plist_contents ${_plist_contents})
    string(REPLACE "\${BUNDLE_ID_SUFFIX}" "${_plist_suffix}" _plist_contents ${_plist_contents})
    file(WRITE ${_helper_info_plist} ${_plist_contents})

    add_executable(${_helper_target} MACOSX_BUNDLE ${EXAMPLE_HELPER_SRCS})
    SET_EXAMPLE_EXECUTABLE_TARGET_PROPERTIES(${_helper_target})
    add_dependencies(${_helper_target} shared_helper)
    target_link_libraries(${_helper_target} shared_helper)
    set_target_properties(${_helper_target} PROPERTIES
      MACOSX_BUNDLE_INFO_PLIST ${_helper_info_plist}
      OUTPUT_NAME ${_helper_output_name}
      )

    add_dependencies(${EXAMPLE_TARGET} "${_helper_target}")

    add_custom_command(
      TARGET ${EXAMPLE_TARGET}
      POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_directory
              "${EXAMPLE_TARGET_OUT_DIR}/${_helper_output_name}.app"
              "${EXAMPLE_APP}/Contents/Frameworks/${_helper_output_name}.app"
      VERBATIM
      )
  endforeach()

  set(PREFIXES "../shared/mac/")
  COPY_MAC_RESOURCES("${EXAMPLE_RESOURCES_SRCS}" "${PREFIXES}" "${EXAMPLE_TARGET}" "${CMAKE_CURRENT_SOURCE_DIR}" "${EXAMPLE_APP}")
endif()

if(OS_WINDOWS)
  set(EXAMPLE_SRCS
    ${EXAMPLE_SRCS}
    ${EXAMPLE_RESOURCES_SRCS}
    )
  SET_EXAMPLE_SOURCE_GROUPS("${EXAMPLE_SRCS}")

  if(USE_SANDBOX)
    add_library(${EXAMPLE_TARGET} SHARED ${EXAMPLE_SRCS})
    SET_EXAMPLE_LIBRARY_TARGET_PROPERTIES(${EXAMPLE_TARGET})
    COPY_SINGLE_FILE(${EXAMPLE_TARGET} ${CEF_BINARY_DIR}/bootstrap.exe ${EXAMPLE_TARGET_OUT_DIR}/${EXAMPLE_TARGET}.exe)
  else()
    add_executable(${EXAMPLE_TARGET} WIN32 ${EXAMPLE_SRCS})
    SET_EXAMPLE_EXECUTABLE_TARGET_PROPERTIES(${EXAMPLE_TARGET})
  endif()

  add_dependencies(${EXAMPLE_TARGET} shared)
  target_link_libraries(${EXAMPLE_TARGET} shared)
endif()
