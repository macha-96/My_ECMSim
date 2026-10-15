set(BCRYPT_SRCS
    ${PROJECT_SOURCE_DIR}/src/bcrypt/bcrypt.c
    ${PROJECT_SOURCE_DIR}/src/bcrypt/crypt_gensalt.c
    ${PROJECT_SOURCE_DIR}/src/bcrypt/crypt_blowfish.c
    ${PROJECT_SOURCE_DIR}/src/bcrypt/wrapper.c
    ${PROJECT_SOURCE_DIR}/src/bcrypt/x86.S
)

add_library(bcrypt STATIC ${BCRYPT_SRCS})
target_include_directories(bcrypt PUBLIC
    ${PROJECT_SOURCE_DIR}/include/bcrypt
)
set_target_properties(bcrypt PROPERTIES POSITION_INDEPENDENT_CODE ON)
