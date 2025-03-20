cmake_minimum_required (VERSION 3.12)
project (win32 LANGUAGES C)

include_directories(${CMAKE_BINARY_DIR} ../lib5250 )

if (${CMAKE_HOST_SYSTEM_NAME} MATCHES "Windows")
    add_executable(tn5250 WIN32 tn5250-win.c tn5250-res.rc winterm.c)
    target_link_libraries(tn5250 5250 ${PLATFORM_LIBRARIES})

    # We have a special version of lp5250d for Windows
    add_executable(lp5250d_win WIN32 lp5250d-win.c)
    target_link_libraries(lp5250d_win 5250 ${PLATFORM_LIBRARIES})
    set_target_properties(lp5250d_win PROPERTIES OUTPUT_NAME lp5250d)

    add_executable(dftmap WIN32 dftmap.c)
    target_link_libraries(dftmap 5250 ${PLATFORM_LIBRARIES})
endif()
