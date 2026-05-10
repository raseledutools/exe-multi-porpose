cmake_minimum_required(VERSION 3.15)
project(RasFocusProMax)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(OpenCV CONFIG REQUIRED)
find_package(unofficial-libharu CONFIG REQUIRED)
find_package(Tesseract CONFIG REQUIRED)
find_package(qpdf CONFIG REQUIRED)
find_package(unofficial-nayuki-qr-code-generator CONFIG REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(POPPLER_CPP REQUIRED IMPORTED_TARGET poppler-cpp)

add_executable(RasFocusProMax WIN32 main.cpp)

target_link_libraries(RasFocusProMax PRIVATE
    gdiplus ole32 winmm shlwapi
    ${OpenCV_LIBS}
    unofficial::libharu::hpdf
    Tesseract::libtesseract
    qpdf::libqpdf
    unofficial::nayuki-qr-code-generator::nayuki-qr-code-generator
    PkgConfig::POPPLER_CPP
)
