cmake_minimum_required(VERSION 3.15)
project(RasFocusProMax)

# C++ Standard Setup
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 1. Find OpenCV
find_package(OpenCV CONFIG REQUIRED)

# 2. Find libharu (vcpkg uses 'unofficial-libharu')
find_package(unofficial-libharu CONFIG REQUIRED)

# 3. Find Tesseract (Requires Capital 'T')
find_package(Tesseract CONFIG REQUIRED)

# 4. Find QPDF
find_package(qpdf CONFIG REQUIRED)

# 5. Find QR Code Generator
find_package(unofficial-nayuki-qr-code-generator CONFIG REQUIRED)

# 6. Find Poppler (Requires PkgConfig as per your log)
find_package(PkgConfig REQUIRED)
pkg_check_modules(POPPLER_CPP REQUIRED IMPORTED_TARGET poppler-cpp)

# Executable Setup (WIN32 hides the CMD console)
add_executable(RasFocusProMax WIN32 main.cpp)

# Link Windows Native Libraries
target_link_libraries(RasFocusProMax PRIVATE
    gdiplus
    ole32
    winmm
    shlwapi
)

# Link vcpkg External Libraries exactly as vcpkg instructed in the log
target_link_libraries(RasFocusProMax PRIVATE
    ${OpenCV_LIBS}
    unofficial::libharu::hpdf
    Tesseract::libtesseract
    qpdf::libqpdf
    unofficial::nayuki-qr-code-generator::nayuki-qr-code-generator
    PkgConfig::POPPLER_CPP
)
