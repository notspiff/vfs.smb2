project(smb)

cmake_minimum_required(VERSION 2.8)

include(ExternalProject)

externalproject_add(krb5
                    SOURCE_DIR ${CMAKE_SOURCE_DIR}
                    WORKING_DIRECTORY <SOURCE_DIR>
                    CONFIGURE_COMMAND <SOURCE_DIR>/configure
                      --prefix=${OUTPUT_DIR}
                      --enable-static
                      --disable-shared
                    INSTALL_COMMAND ""
                    BUILD_IN_SOURCE 1)

install(CODE "execute_process(COMMAND make install WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})")

externalproject_add(gss-ntlmssp
                    SOURCE_DIR ${CMAKE_SOURCE_DIR}
                    UPDATE_COMMAND autoreconf -f -i
                    WORKING_DIRECTORY <SOURCE_DIR>
                    CONFIGURE_COMMAND <SOURCE_DIR>/configure
                      --prefix=${OUTPUT_DIR}
                      --enable-static
                      --disable-shared
                    INSTALL_COMMAND ""
                    BUILD_IN_SOURCE 1)

install(CODE "execute_process(COMMAND make install WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})")

externalproject_add(smb
                    SOURCE_DIR ${CMAKE_SOURCE_DIR}
                    UPDATE_COMMAND ./bootstrap WORKING_DIRECTORY <SOURCE_DIR>
                    CONFIGURE_COMMAND <SOURCE_DIR>/configure
                      --prefix=${OUTPUT_DIR}
                      --enable-static
                      --disable-shared
                    INSTALL_COMMAND ""
                    BUILD_IN_SOURCE 1)

install(CODE "execute_process(COMMAND make install WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})")
