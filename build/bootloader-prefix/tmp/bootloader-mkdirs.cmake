# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

file(MAKE_DIRECTORY
  "D:/Espressif/frameworks/esp-idf-v5.3.1/components/bootloader/subproject"
  "D:/NCKH2024/Summary/Huka/SIM7600x-Esp32-idf-main/build/bootloader"
  "D:/NCKH2024/Summary/Huka/SIM7600x-Esp32-idf-main/build/bootloader-prefix"
  "D:/NCKH2024/Summary/Huka/SIM7600x-Esp32-idf-main/build/bootloader-prefix/tmp"
  "D:/NCKH2024/Summary/Huka/SIM7600x-Esp32-idf-main/build/bootloader-prefix/src/bootloader-stamp"
  "D:/NCKH2024/Summary/Huka/SIM7600x-Esp32-idf-main/build/bootloader-prefix/src"
  "D:/NCKH2024/Summary/Huka/SIM7600x-Esp32-idf-main/build/bootloader-prefix/src/bootloader-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "D:/NCKH2024/Summary/Huka/SIM7600x-Esp32-idf-main/build/bootloader-prefix/src/bootloader-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "D:/NCKH2024/Summary/Huka/SIM7600x-Esp32-idf-main/build/bootloader-prefix/src/bootloader-stamp${cfgdir}") # cfgdir has leading slash
endif()
