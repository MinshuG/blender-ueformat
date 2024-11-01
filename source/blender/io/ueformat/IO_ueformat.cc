/* SPDX-FileCopyrightText: 2024 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup ueformat
 */

#include <iostream>

#include "BLI_path_util.h"
#include "BLI_timeit.hh"

#include "IO_ueformat.hh"

#include "importer/uef_importer.hh"

using namespace blender::timeit;

void UEFORMAT_import(bContext *C, const UEFORMATImportParams *import_params)
{
    //TimePoint start_time = Clock::now();
    blender::io::ueformat::importer_main(C, *import_params);
    //report_duration("import", start_time, import_params->filepath);
}
