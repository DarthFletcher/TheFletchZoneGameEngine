# Install script for directory: C:/Users/fchil/source/repos/TheFletchZoneGameEngine/vcpkg_installed/x64-windows/vcpkg/blds/directxtk12/src/oct2024-b19bcd6174.clean

# Set the install prefix
if(NOT DEFINED CMAKE_INSTALL_PREFIX)
  set(CMAKE_INSTALL_PREFIX "C:/Users/fchil/source/repos/TheFletchZoneGameEngine/vcpkg_installed/x64-windows/vcpkg/pkgs/directxtk12_x64-windows/debug")
endif()
string(REGEX REPLACE "/$" "" CMAKE_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}")

# Set the install configuration name.
if(NOT DEFINED CMAKE_INSTALL_CONFIG_NAME)
  if(BUILD_TYPE)
    string(REGEX REPLACE "^[^A-Za-z0-9_]+" ""
           CMAKE_INSTALL_CONFIG_NAME "${BUILD_TYPE}")
  else()
    set(CMAKE_INSTALL_CONFIG_NAME "Debug")
  endif()
  message(STATUS "Install configuration: \"${CMAKE_INSTALL_CONFIG_NAME}\"")
endif()

# Set the component getting installed.
if(NOT CMAKE_INSTALL_COMPONENT)
  if(COMPONENT)
    message(STATUS "Install component: \"${COMPONENT}\"")
    set(CMAKE_INSTALL_COMPONENT "${COMPONENT}")
  else()
    set(CMAKE_INSTALL_COMPONENT)
  endif()
endif()

# Is this installation the result of a crosscompile?
if(NOT DEFINED CMAKE_CROSSCOMPILING)
  set(CMAKE_CROSSCOMPILING "OFF")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "C:/Users/fchil/source/repos/TheFletchZoneGameEngine/vcpkg_installed/x64-windows/vcpkg/blds/directxtk12/x64-windows-dbg/lib/DirectXTK12.lib")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/share/directxtk12/DirectXTK12-targets.cmake")
    file(DIFFERENT _cmake_export_file_changed FILES
         "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/share/directxtk12/DirectXTK12-targets.cmake"
         "C:/Users/fchil/source/repos/TheFletchZoneGameEngine/vcpkg_installed/x64-windows/vcpkg/blds/directxtk12/x64-windows-dbg/CMakeFiles/Export/dc79b1416ca4e830525952dfb83a96c7/DirectXTK12-targets.cmake")
    if(_cmake_export_file_changed)
      file(GLOB _cmake_old_config_files "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/share/directxtk12/DirectXTK12-targets-*.cmake")
      if(_cmake_old_config_files)
        string(REPLACE ";" ", " _cmake_old_config_files_text "${_cmake_old_config_files}")
        message(STATUS "Old export file \"$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/share/directxtk12/DirectXTK12-targets.cmake\" will be replaced.  Removing files [${_cmake_old_config_files_text}].")
        unset(_cmake_old_config_files_text)
        file(REMOVE ${_cmake_old_config_files})
      endif()
      unset(_cmake_old_config_files)
    endif()
    unset(_cmake_export_file_changed)
  endif()
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/directxtk12" TYPE FILE FILES "C:/Users/fchil/source/repos/TheFletchZoneGameEngine/vcpkg_installed/x64-windows/vcpkg/blds/directxtk12/x64-windows-dbg/CMakeFiles/Export/dc79b1416ca4e830525952dfb83a96c7/DirectXTK12-targets.cmake")
  if(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Dd][Ee][Bb][Uu][Gg])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/directxtk12" TYPE FILE FILES "C:/Users/fchil/source/repos/TheFletchZoneGameEngine/vcpkg_installed/x64-windows/vcpkg/blds/directxtk12/x64-windows-dbg/CMakeFiles/Export/dc79b1416ca4e830525952dfb83a96c7/DirectXTK12-targets-debug.cmake")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/directxtk12" TYPE FILE FILES
    "C:/Users/fchil/source/repos/TheFletchZoneGameEngine/vcpkg_installed/x64-windows/vcpkg/blds/directxtk12/src/oct2024-b19bcd6174.clean/Inc/BufferHelpers.h"
    "C:/Users/fchil/source/repos/TheFletchZoneGameEngine/vcpkg_installed/x64-windows/vcpkg/blds/directxtk12/src/oct2024-b19bcd6174.clean/Inc/CommonStates.h"
    "C:/Users/fchil/source/repos/TheFletchZoneGameEngine/vcpkg_installed/x64-windows/vcpkg/blds/directxtk12/src/oct2024-b19bcd6174.clean/Inc/DDSTextureLoader.h"
    "C:/Users/fchil/source/repos/TheFletchZoneGameEngine/vcpkg_installed/x64-windows/vcpkg/blds/directxtk12/src/oct2024-b19bcd6174.clean/Inc/DescriptorHeap.h"
    "C:/Users/fchil/source/repos/TheFletchZoneGameEngine/vcpkg_installed/x64-windows/vcpkg/blds/directxtk12/src/oct2024-b19bcd6174.clean/Inc/DirectXHelpers.h"
    "C:/Users/fchil/source/repos/TheFletchZoneGameEngine/vcpkg_installed/x64-windows/vcpkg/blds/directxtk12/src/oct2024-b19bcd6174.clean/Inc/Effects.h"
    "C:/Users/fchil/source/repos/TheFletchZoneGameEngine/vcpkg_installed/x64-windows/vcpkg/blds/directxtk12/src/oct2024-b19bcd6174.clean/Inc/EffectPipelineStateDescription.h"
    "C:/Users/fchil/source/repos/TheFletchZoneGameEngine/vcpkg_installed/x64-windows/vcpkg/blds/directxtk12/src/oct2024-b19bcd6174.clean/Inc/GeometricPrimitive.h"
    "C:/Users/fchil/source/repos/TheFletchZoneGameEngine/vcpkg_installed/x64-windows/vcpkg/blds/directxtk12/src/oct2024-b19bcd6174.clean/Inc/GraphicsMemory.h"
    "C:/Users/fchil/source/repos/TheFletchZoneGameEngine/vcpkg_installed/x64-windows/vcpkg/blds/directxtk12/src/oct2024-b19bcd6174.clean/Inc/Model.h"
    "C:/Users/fchil/source/repos/TheFletchZoneGameEngine/vcpkg_installed/x64-windows/vcpkg/blds/directxtk12/src/oct2024-b19bcd6174.clean/Inc/PostProcess.h"
    "C:/Users/fchil/source/repos/TheFletchZoneGameEngine/vcpkg_installed/x64-windows/vcpkg/blds/directxtk12/src/oct2024-b19bcd6174.clean/Inc/PrimitiveBatch.h"
    "C:/Users/fchil/source/repos/TheFletchZoneGameEngine/vcpkg_installed/x64-windows/vcpkg/blds/directxtk12/src/oct2024-b19bcd6174.clean/Inc/RenderTargetState.h"
    "C:/Users/fchil/source/repos/TheFletchZoneGameEngine/vcpkg_installed/x64-windows/vcpkg/blds/directxtk12/src/oct2024-b19bcd6174.clean/Inc/ResourceUploadBatch.h"
    "C:/Users/fchil/source/repos/TheFletchZoneGameEngine/vcpkg_installed/x64-windows/vcpkg/blds/directxtk12/src/oct2024-b19bcd6174.clean/Inc/ScreenGrab.h"
    "C:/Users/fchil/source/repos/TheFletchZoneGameEngine/vcpkg_installed/x64-windows/vcpkg/blds/directxtk12/src/oct2024-b19bcd6174.clean/Inc/SpriteBatch.h"
    "C:/Users/fchil/source/repos/TheFletchZoneGameEngine/vcpkg_installed/x64-windows/vcpkg/blds/directxtk12/src/oct2024-b19bcd6174.clean/Inc/SpriteFont.h"
    "C:/Users/fchil/source/repos/TheFletchZoneGameEngine/vcpkg_installed/x64-windows/vcpkg/blds/directxtk12/src/oct2024-b19bcd6174.clean/Inc/VertexTypes.h"
    "C:/Users/fchil/source/repos/TheFletchZoneGameEngine/vcpkg_installed/x64-windows/vcpkg/blds/directxtk12/src/oct2024-b19bcd6174.clean/Inc/WICTextureLoader.h"
    "C:/Users/fchil/source/repos/TheFletchZoneGameEngine/vcpkg_installed/x64-windows/vcpkg/blds/directxtk12/src/oct2024-b19bcd6174.clean/Inc/GamePad.h"
    "C:/Users/fchil/source/repos/TheFletchZoneGameEngine/vcpkg_installed/x64-windows/vcpkg/blds/directxtk12/src/oct2024-b19bcd6174.clean/Inc/Keyboard.h"
    "C:/Users/fchil/source/repos/TheFletchZoneGameEngine/vcpkg_installed/x64-windows/vcpkg/blds/directxtk12/src/oct2024-b19bcd6174.clean/Inc/Mouse.h"
    "C:/Users/fchil/source/repos/TheFletchZoneGameEngine/vcpkg_installed/x64-windows/vcpkg/blds/directxtk12/src/oct2024-b19bcd6174.clean/Inc/SimpleMath.h"
    "C:/Users/fchil/source/repos/TheFletchZoneGameEngine/vcpkg_installed/x64-windows/vcpkg/blds/directxtk12/src/oct2024-b19bcd6174.clean/Inc/SimpleMath.inl"
    "C:/Users/fchil/source/repos/TheFletchZoneGameEngine/vcpkg_installed/x64-windows/vcpkg/blds/directxtk12/src/oct2024-b19bcd6174.clean/Inc/Audio.h"
    )
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/directxtk12" TYPE FILE FILES
    "C:/Users/fchil/source/repos/TheFletchZoneGameEngine/vcpkg_installed/x64-windows/vcpkg/blds/directxtk12/x64-windows-dbg/directxtk12-config.cmake"
    "C:/Users/fchil/source/repos/TheFletchZoneGameEngine/vcpkg_installed/x64-windows/vcpkg/blds/directxtk12/x64-windows-dbg/directxtk12-config-version.cmake"
    )
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/pkgconfig" TYPE FILE FILES "C:/Users/fchil/source/repos/TheFletchZoneGameEngine/vcpkg_installed/x64-windows/vcpkg/blds/directxtk12/x64-windows-dbg/DirectXTK12.pc")
endif()

if(CMAKE_INSTALL_COMPONENT)
  set(CMAKE_INSTALL_MANIFEST "install_manifest_${CMAKE_INSTALL_COMPONENT}.txt")
else()
  set(CMAKE_INSTALL_MANIFEST "install_manifest.txt")
endif()

string(REPLACE ";" "\n" CMAKE_INSTALL_MANIFEST_CONTENT
       "${CMAKE_INSTALL_MANIFEST_FILES}")
file(WRITE "C:/Users/fchil/source/repos/TheFletchZoneGameEngine/vcpkg_installed/x64-windows/vcpkg/blds/directxtk12/x64-windows-dbg/${CMAKE_INSTALL_MANIFEST}"
     "${CMAKE_INSTALL_MANIFEST_CONTENT}")
