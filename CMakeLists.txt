cmake_minimum_required(VERSION 3.15)
project(RasFocusProMax)

# C++ Standard Setup
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Strict vcpkg package configuration
find_package(OpenCV CONFIG REQUIRED)
find_package(libharu CONFIG REQUIRED)
find_package(Poppler CONFIG REQUIRED)
find_package(qpdf CONFIG REQUIRED)
find_package(tesseract CONFIG REQUIRED)
find_package(nayuki-qr-code-generator CONFIG REQUIRED)

# Executable Setup (WIN32 hides the CMD console)
add_executable(RasFocusProMax WIN32 main.cpp)

# Link Windows Native Libraries
target_link_libraries(RasFocusProMax PRIVATE
    gdiplus
    ole32
    winmm
    shlwapi
)

# Link vcpkg External Libraries perfectly
target_link_libraries(RasFocusProMax PRIVATE
    ${OpenCV_LIBS}
    libharu::hpdf
    Poppler::poppler
    qpdf::qpdf
    libtesseract
    nayuki-qr-code-generator::nayuki-qr-code-generator
)
