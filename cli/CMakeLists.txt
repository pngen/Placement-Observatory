if(NOT PO_BUILD_CLI)
  return()
endif()

add_executable(po po.cpp)
target_link_libraries(po PRIVATE PlacementObservatory)
target_include_directories(po PRIVATE ${CMAKE_SOURCE_DIR}/include)
set_target_properties(po PROPERTIES CXX_STANDARD 20)
