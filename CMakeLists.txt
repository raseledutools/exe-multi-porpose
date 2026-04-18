cmake_minimum_required(VERSION 3.15)
project(RasFocusProMax)

# C++ Standard Setup
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find Packages from vcpkg
find_package(OpenCV REQUIRED)
find_package(haru REQUIRED)
find_package(Poppler REQUIRED)
find_package(qpdf REQUIRED)
find_package(tesseract REQUIRED)
find_package(qrcodegen CONFIG REQUIRED)

# Executable Setup
# WIN32 flag is required to hide the black console/terminal window
add_executable(RasFocusProMax WIN32 main.cpp)

# Link Windows Native Libraries
target_link_libraries(RasFocusProMax PRIVATE
    gdiplus
    ole32
    winmm
    shlwapi
)

# Link vcpkg External Libraries
target_link_libraries(RasFocusProMax PRIVATE
    ${OpenCV_LIBS}
    libharu::hpdf
    Poppler::poppler
    qpdf::qpdf
    libtesseract
    qrcodegen::qrcodegen
)
