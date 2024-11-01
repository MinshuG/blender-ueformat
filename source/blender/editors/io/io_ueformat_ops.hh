/* SPDX-FileCopyrightText: 2023 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup editor/io
 */

#pragma once

struct wmOperatorType;

// void WM_OT_ueformat_export(wmOperatorType *ot);
void WM_OT_ueformat_import(wmOperatorType *ot);

namespace blender::ed::io {
void ueformat_file_handler_add();
}
