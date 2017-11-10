# build service
set(SOURCE_FILES
    MapBasedNoLockImpl.cpp
    MapBasedGlobalLockImpl.cpp
    MapBasedStripedLockImpl.cpp
    MapBasedRWLockImpl.cpp
)

add_library(Storage ${SOURCE_FILES})
target_link_libraries(Storage ${CMAKE_THREAD_LIBS_INIT})
