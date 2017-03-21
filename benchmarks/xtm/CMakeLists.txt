if(USE_XTM_INLINE)
  add_definitions(-DXTM_USE_INLINE_LOCKABLE)
endif()

file(GLOB MM_SRC mm/*.cpp)
file(GLOB LL_SRC ll/*.cpp)
include_directories(include .)
add_library(xtm xtm.cpp ${MM_SRC} ${LL_SRC})
target_link_libraries(xtm ${CMAKE_THREAD_LIBS_INIT})
