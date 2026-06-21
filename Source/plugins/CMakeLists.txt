# importers
add_subdirectory(importers/armory)
add_subdirectory(importers/wowhead)

#exporters
add_subdirectory(exporters/obj)
# The FBX exporter needs the Autodesk FBX SDK, which is only available here as a 32-bit prebuilt.
# Skip it for the x64 build until an x64 FBX SDK is wired in; the OBJ exporter still ships.
if(NOT WMV_X64)
  add_subdirectory(exporters/fbx)
endif()
