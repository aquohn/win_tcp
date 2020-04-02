cmake_minimum_required (VERSION 2.8)
cmake_minimum_required (VERSION 2.8 FATAL_ERROR)

project (win_tcp C)

# Setup folders
set( WIN_TCP_SRC src )
set( EXECUTABLE_OUTPUT_PATH bin )

if ( "${CMAKE_C_COMPILER_ID}" STREQUAL "GNU" )
  message( STATUS "Setting flags for ${CMAKE_C_COMPILER_ID} compiler" )
  add_definitions( -Wall )
endif()

add_executable( server ${WIN_TCP_SRC}/server.c )
add_executable( client_p ${WIN_TCP_SRC}/client_p.c )
add_executable( client_np ${WIN_TCP_SRC}/client_np.c )

target_link_libraries( server ws2_32 ) 
target_link_libraries( client_p ws2_32 ) 
target_link_libraries( client_np ws2_32 ) 

