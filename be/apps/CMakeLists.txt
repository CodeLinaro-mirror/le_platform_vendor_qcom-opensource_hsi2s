project(hsi2s_pvm)

cmake_minimum_required(VERSION 3.10)
SET(CMAKE_INSTALL_PREFIX "/usr/")

INCLUDE_DIRECTORIES(include)
link_directories(${SYSROOT_LIBDIR})


set(SOURCE_FILES
    pvm.c
    shm_habmm.c
)

add_executable(hsi2s_pvm ${SOURCE_FILES})
target_link_libraries (hsi2s_pvm hsi2sinterface)
target_link_libraries (hsi2s_pvm pthread)
target_link_libraries (hsi2s_pvm uhab)

install(TARGETS "hsi2s_pvm"
    RUNTIME DESTINATION bin
    COMPONENT runtime
)
