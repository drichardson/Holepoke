cmake_minimum_required(VERSION 2.8)
project(holepoke)

file(GLOB LIB_SOURCES
  [^.]*.cpp
  [^.]*.cc
  [^.]*.h
  )

file(GLOB PROTO_SOURCES
  [^.]*.proto
  )

find_program(PROTOC protoc
  PATHS
  support/windows
  )

foreach(PB ${PROTO_SOURCES})
    GET_FILENAME_COMPONENT(PB_WE ${PB} NAME_WE)

    set(PB_H
      ${CMAKE_CURRENT_SOURCE_DIR}/${PB_WE}.pb.h
      )

    set(PB_CC
      ${CMAKE_CURRENT_SOURCE_DIR}/${PB_WE}.pb.cc
      )

    add_custom_command(OUTPUT ${PB_H} ${PB_CC}
      COMMAND ${PROTOC}
      ARGS -I=${CMAKE_CURRENT_SOURCE_DIR}
           --cpp_out=${CMAKE_CURRENT_SOURCE_DIR}
           ${PB}
      )


    set(PB_SOURCES
      ${PB_SOURCES}
      ${PB_H} ${PB_CC}
      )
endforeach(PB)

set_source_files_properties(${PB_SOURCES} GENERATED)

add_library(holepoke STATIC
  ${PB_SOURCES}
  ${LIB_SOURCES}
  )
