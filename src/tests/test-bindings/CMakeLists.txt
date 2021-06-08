
add_library(hello SHARED hello.c)

set(bugnb)

foreach(bugnb RANGE 11 22)
    add_library(bug${bugnb} SHARED bug.c)
    target_compile_definitions(bug${bugnb} PRIVATE -DBUG${bugnb})
    target_include_directories(bug${bugnb} PRIVATE ${INCLUDE_DIRS} ${check_INCLUDE_DIRS} ${CMAKE_CURRENT_SOURCE_DIR}/../libafb)
	target_link_libraries(bug${bugnb} libafbsta ${ldflags}  ${check_LDFLAGS})
endforeach()
