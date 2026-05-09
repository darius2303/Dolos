cmake_minimum_required(VERSION 3.15)
project(Dolos LANGUAGES C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(gsoap REQUIRED)
find_package(argtable2 REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(ZLIB REQUIRED)
find_package(libconfig REQUIRED)

find_program(GSOAP_CPP2 soapcpp2 REQUIRED)

find_library(GSOAP_C_LIB
    NAMES gsoapssl gsoap
    HINTS ${gsoap_LIB_DIRS}
    REQUIRED
)

find_path(GSOAP_IMPORT_DIR
    NAMES xsd.h
    HINTS
        ${gsoap_INCLUDE_DIRS}/../bin/import
    REQUIRED
)

set(SERVICE ${CMAKE_CURRENT_SOURCE_DIR}/services/service.h)
set(GEN_DIR ${CMAKE_CURRENT_SOURCE_DIR}/generated)
set(STEMMER_O ${CMAKE_CURRENT_SOURCE_DIR}/libs/libstemmer.o)

file(MAKE_DIRECTORY ${GEN_DIR})

add_custom_command(
    OUTPUT
    ${GEN_DIR}/soapC.c
    ${GEN_DIR}/soapServer.c
    ${GEN_DIR}/soapClient.c
    ${GEN_DIR}/soapStub.h
    ${GEN_DIR}/soapH.h
    ${GEN_DIR}/ns.nsmap
    COMMAND ${GSOAP_CPP2} -c -d ${GEN_DIR} -I${GSOAP_IMPORT_DIR} ${SERVICE}
    DEPENDS ${SERVICE}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    VERBATIM
)

add_custom_target(gsoap_gen_target
    DEPENDS
    ${GEN_DIR}/soapC.c
    ${GEN_DIR}/soapServer.c
    ${GEN_DIR}/soapClient.c
)

add_executable(server
    server/main.c
    server/server.c
    ${GEN_DIR}/soapC.c
    ${GEN_DIR}/soapServer.c
)
target_link_libraries(server PRIVATE
    ${GSOAP_C_LIB}
    argtable2::argtable2
    OpenSSL::SSL
    OpenSSL::Crypto
    ZLIB::ZLIB
    libconfig::libconfig
    ${STEMMER_O}
    pthread
)
target_include_directories(server PRIVATE
    ${GEN_DIR}
    server
    ${gsoap_INCLUDE_DIRS}
    libs
)
add_dependencies(server gsoap_gen_target)

add_executable(client
    client/main.c
    client/client.c
    ${GEN_DIR}/soapC.c
    ${GEN_DIR}/soapClient.c
)
target_link_libraries(client PRIVATE
    ${GSOAP_C_LIB}
    argtable2::argtable2
    OpenSSL::SSL
    OpenSSL::Crypto
    ZLIB::ZLIB
    libconfig::libconfig
)
target_include_directories(client PRIVATE
    ${GEN_DIR}
    client
    ${gsoap_INCLUDE_DIRS}
)
add_dependencies(client gsoap_gen_target)
