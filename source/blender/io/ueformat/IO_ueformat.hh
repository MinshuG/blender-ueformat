/* SPDX-FileCopyrightText: 2024 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup ueformat
 */

#pragma once

#include "BLI_path_util.h"

#include "DEG_depsgraph.hh"

#include "IO_orientation.hh"
#include "IO_path_util_types.hh"

struct bContext;
struct ReportList;

struct UEFORMATImportParams {
  /** Full path to the source .ueformat file to import. */
  char filepath[FILE_MAX];
  /** Value 0 disables clamping. */
  float scale = 0.01f; // cm to m
  bool link = true;

  ReportList *reports = nullptr;
};

void UEFORMAT_import(bContext *C, const UEFORMATImportParams *import_params);
