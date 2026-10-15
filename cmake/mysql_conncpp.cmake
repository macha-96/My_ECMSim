if(DEFINED ENV{MYSQL_CONNCXX_HOME} AND NOT "$ENV{MYSQL_CONNCXX_HOME}" STREQUAL "")
    set(MYSQL_CONNCXX_HOME "$ENV{MYSQL_CONNCXX_HOME}")
    message(STATUS "MySQL Connector/C++ from ENV: ${MYSQL_CONNCXX_HOME}")
else()
    set(MYSQL_CONNCXX_HOME "/home/ky/soft/mysql-conncpp")
    message(STATUS "MySQL Connector/C++ default: ${MYSQL_CONNCXX_HOME}")
endif()

set(MYSQL_CONNCXX_INCLUDE ${MYSQL_CONNCXX_HOME}/include/mysql-cppconn)
set(MYSQL_CONNCXX_LIB_DIR ${MYSQL_CONNCXX_HOME}/lib/x86_64-linux-gnu)

find_library(MYSQL_CONCPP_LIB
    NAMES mysqlcppconn libmysqlcppconn
    PATHS ${MYSQL_CONNCXX_LIB_DIR}
    NO_DEFAULT_PATH REQUIRED
)
find_path(MYSQL_CONCPP_INCLUDE_DIR
    NAMES mysql_driver.h
    PATHS ${MYSQL_CONNCXX_INCLUDE}/jdbc
    NO_DEFAULT_PATH REQUIRED
)

target_include_directories(sim_core PUBLIC ${MYSQL_CONNCXX_INCLUDE})
target_link_libraries(ecmsim_http_beast PRIVATE ${MYSQL_CONCPP_LIB})

add_custom_command(TARGET ecmsim_http_beast POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${MYSQL_CONNCXX_LIB_DIR}/libmysqlcppconn.so.10.9.7.0"
        "${PROJECT_SOURCE_DIR}/lib/"
    COMMAND ${CMAKE_COMMAND} -E create_symlink
        "libmysqlcppconn.so.10.9.7.0"
        "${PROJECT_SOURCE_DIR}/lib/libmysqlcppconn.so.10"
    COMMAND ${CMAKE_COMMAND} -E create_symlink
        "libmysqlcppconn.so.10"
        "${PROJECT_SOURCE_DIR}/lib/libmysqlcppconn.so"
    COMMENT "Deploying libmysqlcppconn.so to bin/../lib/"
)

target_include_directories(ecmsim_http_beast PRIVATE
    ${MYSQL_CONNCXX_INCLUDE}
    ${MYSQL_CONCPP_INCLUDE_DIR}
)
