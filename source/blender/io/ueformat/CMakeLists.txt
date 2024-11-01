# SPDX-FileCopyrightText: 2023 Blender Authors
#
# SPDX-License-Identifier: GPL-2.0-or-later

set(INC
  .
  ../common
  ../../blenkernel
  ../../bmesh
  ../../bmesh/intern
  ../../editors/include
  ../../makesrna
  ../../nodes
  ../../windowmanager
)

set(INC_SYS
  ../../../../extern/fast_float

  ${ZLIB_INCLUDE_DIRS}
  ${ZSTD_INCLUDE_DIRS}
)

set(SRC
  IO_ueformat.cc
  importer/uef_importer.cc
  importer/uef_model_reader.cc

  IO_ueformat.hh
  importer/uef_importer.hh
  importer/uef_model_reader.hh
)

set(LIB
  bf_blenkernel
  PRIVATE bf::blenlib
  PRIVATE bf::depsgraph
  PRIVATE bf::dna
  PRIVATE bf::intern::guardedalloc
  bf_io_common
  PRIVATE bf::extern::fmtlib
  ${ZLIB_LIBRARIES}
  ${ZSTD_LIBRARIES}
)

if(WITH_TBB)
  add_definitions(-DWITH_TBB)
  list(APPEND INC_SYS ${TBB_INCLUDE_DIRS})
  list(APPEND LIB ${TBB_LIBRARIES})
endif()

blender_add_lib(bf_io_ueformat "${SRC}" "${INC}" "${INC_SYS}" "${LIB}")

if(WITH_GTESTS)

endif()
