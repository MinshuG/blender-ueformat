/* SPDX-FileCopyrightText: 2023 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup editor/io
 */

#ifdef WITH_IO_UEFORMAT
#  include "DNA_space_types.h"

#  include "BKE_context.hh"
#  include "BKE_file_handler.hh"
#  include "BKE_main.hh"
#  include "BKE_report.hh"

#  include "BLI_string.h"
#  include "BLI_utildefines.h"

#  include "BLT_translation.hh"

#  include "ED_outliner.hh"

#  include "RNA_access.hh"
#  include "RNA_define.hh"

#  include "UI_interface.hh"
#  include "UI_resources.hh"

#  include "WM_api.hh"
#  include "WM_types.hh"

#  include "IO_ueformat.hh"

#  include "io_ueformat_ops.hh"
#  include "io_utils.hh"


static int wm_ueformat_import_exec(bContext *C, wmOperator *op)
{
  UEFORMATImportParams import_params{};
  import_params.scale = RNA_float_get(op->ptr, "scale");

  import_params.reports = op->reports;

  const auto paths = blender::ed::io::paths_from_operator_properties(op->ptr);

  if (paths.is_empty()) {
    BKE_report(op->reports, RPT_ERROR, "No filepath given");
    return OPERATOR_CANCELLED;
  }
  for (const auto &path : paths) {
    STRNCPY(import_params.filepath, path.c_str());
    UEFORMAT_import(C, &import_params);
    /* Only first import clears selection. */
    //import_params.clear_selection = false;
  }

  Scene *scene = CTX_data_scene(C);
  WM_event_add_notifier(C, NC_SCENE | ND_OB_SELECT, scene);
  WM_event_add_notifier(C, NC_SCENE | ND_OB_ACTIVE, scene);
  WM_event_add_notifier(C, NC_SCENE | ND_LAYER_CONTENT, scene);
  ED_outliner_select_sync_from_object_tag(C);

  return OPERATOR_FINISHED;
}


static void ui_ueformat_import_settings(const bContext *C, uiLayout *layout, PointerRNA *ptr)
{
  uiLayoutSetPropSep(layout, true);
  uiLayoutSetPropDecorate(layout, false);

   if (uiLayout *panel = uiLayoutPanel(C, layout, "UEFORMAT_import_general", false, IFACE_("General"))) {
     uiLayout *col = uiLayoutColumn(panel, false);
     uiItemR(col, ptr, "scale", UI_ITEM_NONE, nullptr, ICON_NONE);
   }
}

static void wm_ueformat_import_draw(bContext *C, wmOperator *op)
{
  ui_ueformat_import_settings(C, op->layout, op->ptr);
}


void WM_OT_ueformat_import(wmOperatorType *ot)
{
  PropertyRNA *prop;

  ot->name = "Import UEFORMAT";
  ot->description = "Load a ueformat file";
  ot->idname = "WM_OT_ueformat_import";
  ot->flag = OPTYPE_UNDO | OPTYPE_PRESET;

  ot->invoke = blender::ed::io::filesel_drop_import_invoke;
  ot->exec = wm_ueformat_import_exec;
  ot->poll = WM_operator_winactive;
  ot->ui = wm_ueformat_import_draw;

  WM_operator_properties_filesel(ot,
                                 FILE_TYPE_FOLDER,
                                 FILE_BLENDER,
                                 FILE_OPENFILE,
                                 WM_FILESEL_FILEPATH | WM_FILESEL_SHOW_PROPS |
                                     WM_FILESEL_DIRECTORY | WM_FILESEL_FILES,
                                 FILE_DEFAULTDISPLAY,
                                 FILE_SORT_DEFAULT);

  // TODO: Add import settings
  RNA_def_float(
        ot->srna,
        "scale",
        0.1f, // ue cm to m
        0.0001f,
        10000.0f,
        "Scale",
        "Value by which to enlarge or shrink the objects with respect to the world's origin",
        0.0001f,
        10000.0f);

  prop = RNA_def_string(ot->srna, "filter_glob", "*.uemodel;*.ueanim;*.ueworld", 0, "Extension Filter", "");
  RNA_def_property_flag(prop, PROP_HIDDEN);
}

namespace blender::ed::io {
void ueformat_file_handler_add()
{
  auto fh = std::make_unique<blender::bke::FileHandlerType>();
  STRNCPY(fh->idname, "IO_FH_ueformat");
  STRNCPY(fh->import_operator, "WM_OT_ueformat_import");
//   STRNCPY(fh->export_operator, "WM_OT_obj_export");
  STRNCPY(fh->label, "uemodel");
  STRNCPY(fh->file_extensions_str, ".uemodel");
  fh->poll_drop = poll_file_object_drop;
  bke::file_handler_add(std::move(fh));
}
}  // namespace blender::ed::io

#endif /* WITH_IO_UEFORMAT */
