/* SPDX-FileCopyrightText: 2020 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup edsculpt
 */

#include "MEM_guardedalloc.h"

#include "BLI_math_matrix.h"
#include "BLI_math_rotation.h"
#include "BLI_math_vector.h"
#include "BLI_math_vector_types.hh"
#include "BLI_span.hh"

#include "BKE_brush.hh"
#include "BKE_context.hh"
#include "BKE_kelvinlet.h"
#include "BKE_layer.hh"
#include "BKE_paint.hh"
#include "BKE_pbvh_api.hh"

#include "WM_api.hh"
#include "WM_types.hh"

#include "ED_screen.hh"
#include "ED_sculpt.hh"
#include "ED_view3d.hh"
#include "paint_intern.hh"
#include "sculpt_intern.hh"

#include "RNA_access.hh"
#include "RNA_define.hh"

#include "bmesh.hh"

#include <cmath>
#include <cstdlib>

namespace blender::ed::sculpt_paint {

void init_transform(bContext *C, Object &ob, const float mval_fl[2], const char *undo_name)
{
  const Sculpt &sd = *CTX_data_tool_settings(C)->sculpt;
  SculptSession &ss = *ob.sculpt;
  Depsgraph *depsgraph = CTX_data_depsgraph_pointer(C);

  ss.init_pivot_pos = ss.pivot_pos;
  ss.init_pivot_rot = ss.pivot_rot;
  ss.init_pivot_scale = ss.pivot_scale;

  ss.prev_pivot_pos = ss.pivot_pos;
  ss.prev_pivot_rot = ss.pivot_rot;
  ss.prev_pivot_scale = ss.pivot_scale;

  undo::push_begin_ex(ob, undo_name);
  BKE_sculpt_update_object_for_edit(depsgraph, &ob, false);

  ss.pivot_rot[3] = 1.0f;

  SCULPT_vertex_random_access_ensure(ss);

  filter::cache_init(C, ob, sd, undo::Type::Position, mval_fl, 5.0, 1.0f);

  if (sd.transform_mode == SCULPT_TRANSFORM_MODE_RADIUS_ELASTIC) {
    ss.filter_cache->transform_displacement_mode = SCULPT_TRANSFORM_DISPLACEMENT_INCREMENTAL;
  }
  else {
    ss.filter_cache->transform_displacement_mode = SCULPT_TRANSFORM_DISPLACEMENT_ORIGINAL;
  }
}

static std::array<float4x4, 8> transform_matrices_init(
    const SculptSession &ss,
    const ePaintSymmetryFlags symm,
    const SculptTransformDisplacementMode t_mode)
{
  std::array<float4x4, 8> mats;

  float final_pivot_pos[3], d_t[3], d_r[4], d_s[3];
  float t_mat[4][4], r_mat[4][4], s_mat[4][4], pivot_mat[4][4], pivot_imat[4][4],
      transform_mat[4][4];

  float start_pivot_pos[3], start_pivot_rot[4], start_pivot_scale[3];
  switch (t_mode) {
    case SCULPT_TRANSFORM_DISPLACEMENT_ORIGINAL:
      copy_v3_v3(start_pivot_pos, ss.init_pivot_pos);
      copy_v4_v4(start_pivot_rot, ss.init_pivot_rot);
      copy_v3_v3(start_pivot_scale, ss.init_pivot_scale);
      break;
    case SCULPT_TRANSFORM_DISPLACEMENT_INCREMENTAL:
      copy_v3_v3(start_pivot_pos, ss.prev_pivot_pos);
      copy_v4_v4(start_pivot_rot, ss.prev_pivot_rot);
      copy_v3_v3(start_pivot_scale, ss.prev_pivot_scale);
      break;
  }

  for (int i = 0; i < PAINT_SYMM_AREAS; i++) {
    ePaintSymmetryAreas v_symm = ePaintSymmetryAreas(i);

    copy_v3_v3(final_pivot_pos, ss.pivot_pos);

    unit_m4(pivot_mat);

    unit_m4(t_mat);
    unit_m4(r_mat);
    unit_m4(s_mat);

    /* Translation matrix. */
    sub_v3_v3v3(d_t, ss.pivot_pos, start_pivot_pos);
    SCULPT_flip_v3_by_symm_area(d_t, symm, v_symm, ss.init_pivot_pos);
    translate_m4(t_mat, d_t[0], d_t[1], d_t[2]);

    /* Rotation matrix. */
    sub_qt_qtqt(d_r, ss.pivot_rot, start_pivot_rot);
    normalize_qt(d_r);
    SCULPT_flip_quat_by_symm_area(d_r, symm, v_symm, ss.init_pivot_pos);
    quat_to_mat4(r_mat, d_r);

    /* Scale matrix. */
    sub_v3_v3v3(d_s, ss.pivot_scale, start_pivot_scale);
    add_v3_fl(d_s, 1.0f);
    size_to_mat4(s_mat, d_s);

    /* Pivot matrix. */
    SCULPT_flip_v3_by_symm_area(final_pivot_pos, symm, v_symm, start_pivot_pos);
    translate_m4(pivot_mat, final_pivot_pos[0], final_pivot_pos[1], final_pivot_pos[2]);
    invert_m4_m4(pivot_imat, pivot_mat);

    /* Final transform matrix. */
    mul_m4_m4m4(transform_mat, r_mat, t_mat);
    mul_m4_m4m4(transform_mat, transform_mat, s_mat);
    mul_m4_m4m4(mats[i].ptr(), transform_mat, pivot_imat);
    mul_m4_m4m4(mats[i].ptr(), pivot_mat, mats[i].ptr());
  }

  return mats;
}

static constexpr float transform_mirror_max_distance_eps = 0.00002f;

static void transform_node(Object &ob,
                           const std::array<float4x4, 8> &transform_mats,
                           PBVHNode *node)
{
  SculptSession &ss = *ob.sculpt;

  SculptOrigVertData orig_data;
  SCULPT_orig_vert_data_init(orig_data, ob, *node, undo::Type::Position);

  PBVHVertexIter vd;

  const ePaintSymmetryFlags symm = SCULPT_mesh_symmetry_xyz_get(ob);

  undo::push_node(ob, node, undo::Type::Position);
  BKE_pbvh_vertex_iter_begin (*ss.pbvh, node, vd, PBVH_ITER_UNIQUE) {
    SCULPT_orig_vert_data_update(orig_data, vd);
    float *start_co;
    float transformed_co[3], orig_co[3], disp[3];
    float fade = vd.mask;
    copy_v3_v3(orig_co, orig_data.co);
    char symm_area = SCULPT_get_vertex_symm_area(orig_co);

    switch (ss.filter_cache->transform_displacement_mode) {
      case SCULPT_TRANSFORM_DISPLACEMENT_ORIGINAL:
        start_co = orig_co;
        break;
      case SCULPT_TRANSFORM_DISPLACEMENT_INCREMENTAL:
        start_co = vd.co;
        break;
    }

    copy_v3_v3(transformed_co, start_co);
    mul_m4_v3(transform_mats[int(symm_area)].ptr(), transformed_co);
    sub_v3_v3v3(disp, transformed_co, start_co);
    mul_v3_fl(disp, 1.0f - fade);
    add_v3_v3v3(vd.co, start_co, disp);

    /* Keep vertices on the mirror axis. */
    if ((symm & PAINT_SYMM_X) && (fabs(start_co[0]) < transform_mirror_max_distance_eps)) {
      vd.co[0] = 0.0f;
    }
    if ((symm & PAINT_SYMM_Y) && (fabs(start_co[1]) < transform_mirror_max_distance_eps)) {
      vd.co[1] = 0.0f;
    }
    if ((symm & PAINT_SYMM_Z) && (fabs(start_co[2]) < transform_mirror_max_distance_eps)) {
      vd.co[2] = 0.0f;
    }
  }
  BKE_pbvh_vertex_iter_end;

  BKE_pbvh_node_mark_update(node);
}

static void sculpt_transform_all_vertices(Object &ob)
{
  SculptSession &ss = *ob.sculpt;
  const ePaintSymmetryFlags symm = SCULPT_mesh_symmetry_xyz_get(ob);

  std::array<float4x4, 8> transform_mats = transform_matrices_init(
      ss, symm, ss.filter_cache->transform_displacement_mode);

  /* Regular transform applies all symmetry passes at once as it is split by symmetry areas
   * (each vertex can only be transformed once by the transform matrix of its area). */
  threading::parallel_for(ss.filter_cache->nodes.index_range(), 1, [&](const IndexRange range) {
    for (const int i : range) {
      transform_node(ob, transform_mats, ss.filter_cache->nodes[i]);
    }
  });
}

static void elastic_transform_node(Object &ob,
                                   const float transform_radius,
                                   const float elastic_transform_mat[4][4],
                                   const float elastic_transform_pivot[3],
                                   PBVHNode *node)
{
  SculptSession &ss = *ob.sculpt;

  const MutableSpan<float3> proxy = BKE_pbvh_node_add_proxy(*ss.pbvh, *node).co;

  SculptOrigVertData orig_data;
  SCULPT_orig_vert_data_init(orig_data, ob, *node, undo::Type::Position);

  KelvinletParams params;
  /* TODO(pablodp606): These parameters can be exposed if needed as transform strength and volume
   * preservation like in the elastic deform brushes. Setting them to the same default as elastic
   * deform triscale grab because they work well in most cases. */
  const float force = 1.0f;
  const float shear_modulus = 1.0f;
  const float poisson_ratio = 0.4f;
  BKE_kelvinlet_init_params(&params, transform_radius, force, shear_modulus, poisson_ratio);

  undo::push_node(ob, node, undo::Type::Position);

  PBVHVertexIter vd;
  BKE_pbvh_vertex_iter_begin (*ss.pbvh, node, vd, PBVH_ITER_UNIQUE) {
    SCULPT_orig_vert_data_update(orig_data, vd);
    float transformed_co[3], orig_co[3], disp[3];
    const float fade = vd.mask;
    copy_v3_v3(orig_co, orig_data.co);

    copy_v3_v3(transformed_co, vd.co);
    mul_m4_v3(elastic_transform_mat, transformed_co);
    sub_v3_v3v3(disp, transformed_co, vd.co);

    float final_disp[3];
    BKE_kelvinlet_grab_triscale(final_disp, &params, vd.co, elastic_transform_pivot, disp);
    mul_v3_fl(final_disp, 20.0f * (1.0f - fade));

    copy_v3_v3(proxy[vd.i], final_disp);
  }
  BKE_pbvh_vertex_iter_end;

  BKE_pbvh_node_mark_update(node);
}

static void transform_radius_elastic(const Sculpt &sd, Object &ob, const float transform_radius)
{
  SculptSession &ss = *ob.sculpt;
  BLI_assert(ss.filter_cache->transform_displacement_mode ==
             SCULPT_TRANSFORM_DISPLACEMENT_INCREMENTAL);

  const ePaintSymmetryFlags symm = SCULPT_mesh_symmetry_xyz_get(ob);

  std::array<float4x4, 8> transform_mats = transform_matrices_init(
      ss, symm, ss.filter_cache->transform_displacement_mode);

  /* Elastic transform needs to apply all transform matrices to all vertices and then combine the
   * displacement proxies as all vertices are modified by all symmetry passes. */
  for (ePaintSymmetryFlags symmpass = PAINT_SYMM_NONE; symmpass <= symm; symmpass++) {
    if (SCULPT_is_symmetry_iteration_valid(symmpass, symm)) {
      float elastic_transform_pivot[3];
      flip_v3_v3(elastic_transform_pivot, ss.pivot_pos, symmpass);
      float elastic_transform_pivot_init[3];
      flip_v3_v3(elastic_transform_pivot_init, ss.init_pivot_pos, symmpass);

      const int symm_area = SCULPT_get_vertex_symm_area(elastic_transform_pivot);
      float elastic_transform_mat[4][4];
      copy_m4_m4(elastic_transform_mat, transform_mats[symm_area].ptr());
      threading::parallel_for(
          ss.filter_cache->nodes.index_range(), 1, [&](const IndexRange range) {
            for (const int i : range) {
              elastic_transform_node(ob,
                                     transform_radius,
                                     elastic_transform_mat,
                                     elastic_transform_pivot,
                                     ss.filter_cache->nodes[i]);
            }
          });
    }
  }
  SCULPT_combine_transform_proxies(sd, ob);
}

void update_modal_transform(bContext *C, Object &ob)
{
  const Sculpt &sd = *CTX_data_tool_settings(C)->sculpt;
  SculptSession &ss = *ob.sculpt;
  Depsgraph *depsgraph = CTX_data_depsgraph_pointer(C);

  SCULPT_vertex_random_access_ensure(ss);
  BKE_sculpt_update_object_for_edit(depsgraph, &ob, false);

  switch (sd.transform_mode) {
    case SCULPT_TRANSFORM_MODE_ALL_VERTICES: {
      sculpt_transform_all_vertices(ob);
      break;
    }
    case SCULPT_TRANSFORM_MODE_RADIUS_ELASTIC: {
      const Brush &brush = *BKE_paint_brush_for_read(&sd.paint);
      Scene *scene = CTX_data_scene(C);
      float transform_radius;

      if (BKE_brush_use_locked_size(scene, &brush)) {
        transform_radius = BKE_brush_unprojected_radius_get(scene, &brush);
      }
      else {
        ViewContext vc = ED_view3d_viewcontext_init(C, depsgraph);

        transform_radius = paint_calc_object_space_radius(
            vc, ss.init_pivot_pos, BKE_brush_size_get(scene, &brush));
      }

      transform_radius_elastic(sd, ob, transform_radius);
      break;
    }
  }

  copy_v3_v3(ss.prev_pivot_pos, ss.pivot_pos);
  copy_v4_v4(ss.prev_pivot_rot, ss.pivot_rot);
  copy_v3_v3(ss.prev_pivot_scale, ss.pivot_scale);

  if (ss.deform_modifiers_active || ss.shapekey_active) {
    SCULPT_flush_stroke_deform(sd, ob, true);
  }

  flush_update_step(C, UpdateType::Position);
}

void end_transform(bContext *C, Object &ob)
{
  SculptSession &ss = *ob.sculpt;
  if (ss.filter_cache) {
    filter::cache_free(ss);
  }
  flush_update_done(C, ob, UpdateType::Position);
}

enum class PivotPositionMode {
  Origin = 0,
  Unmasked = 1,
  MaskBorder = 2,
  ActiveVert = 3,
  CursorSurface = 4,
};

static EnumPropertyItem prop_sculpt_pivot_position_types[] = {
    {int(PivotPositionMode::Origin),
     "ORIGIN",
     0,
     "Origin",
     "Sets the pivot to the origin of the sculpt"},
    {int(PivotPositionMode::Unmasked),
     "UNMASKED",
     0,
     "Unmasked",
     "Sets the pivot position to the average position of the unmasked vertices"},
    {int(PivotPositionMode::MaskBorder),
     "BORDER",
     0,
     "Mask Border",
     "Sets the pivot position to the center of the border of the mask"},
    {int(PivotPositionMode::ActiveVert),
     "ACTIVE",
     0,
     "Active Vertex",
     "Sets the pivot position to the active vertex position"},
    {int(PivotPositionMode::CursorSurface),
     "SURFACE",
     0,
     "Surface",
     "Sets the pivot position to the surface under the cursor"},
    {0, nullptr, 0, nullptr, nullptr},
};

static bool set_pivot_depends_on_cursor(bContext & /*C*/, wmOperatorType & /*ot*/, PointerRNA *ptr)
{
  if (!ptr) {
    return true;
  }
  const PivotPositionMode mode = PivotPositionMode(RNA_enum_get(ptr, "mode"));
  return mode == PivotPositionMode::CursorSurface;
}

static int set_pivot_position_exec(bContext *C, wmOperator *op)
{
  Object &ob = *CTX_data_active_object(C);
  SculptSession &ss = *ob.sculpt;
  ARegion *region = CTX_wm_region(C);
  Depsgraph *depsgraph = CTX_data_ensure_evaluated_depsgraph(C);
  const char symm = SCULPT_mesh_symmetry_xyz_get(ob);

  const PivotPositionMode mode = PivotPositionMode(RNA_enum_get(op->ptr, "mode"));

  const View3D *v3d = CTX_wm_view3d(C);
  const Base *base = CTX_data_active_base(C);
  if (!BKE_base_is_visible(v3d, base)) {
    return OPERATOR_CANCELLED;
  }

  BKE_sculpt_update_object_for_edit(depsgraph, &ob, false);

  /* Pivot to center. */
  if (mode == PivotPositionMode::Origin) {
    zero_v3(ss.pivot_pos);
  }
  /* Pivot to active vertex. */
  else if (mode == PivotPositionMode::ActiveVert) {
    copy_v3_v3(ss.pivot_pos, SCULPT_active_vertex_co_get(ss));
  }
  /* Pivot to ray-cast surface. */
  else if (mode == PivotPositionMode::CursorSurface) {
    float stroke_location[3];
    const float mval[2] = {
        RNA_float_get(op->ptr, "mouse_x"),
        RNA_float_get(op->ptr, "mouse_y"),
    };
    if (SCULPT_stroke_get_location(C, stroke_location, mval, false)) {
      copy_v3_v3(ss.pivot_pos, stroke_location);
    }
  }
  else {
    Vector<PBVHNode *> nodes = bke::pbvh::search_gather(*ss.pbvh, {});

    float avg[3];
    int total = 0;
    zero_v3(avg);

    /* Pivot to unmasked. */
    if (mode == PivotPositionMode::Unmasked) {
      for (PBVHNode *node : nodes) {
        PBVHVertexIter vd;
        BKE_pbvh_vertex_iter_begin (*ss.pbvh, node, vd, PBVH_ITER_UNIQUE) {
          const float mask = vd.mask;
          if (mask < 1.0f) {
            if (SCULPT_check_vertex_pivot_symmetry(vd.co, ss.pivot_pos, symm)) {
              add_v3_v3(avg, vd.co);
              total++;
            }
          }
        }
        BKE_pbvh_vertex_iter_end;
      }
    }
    /* Pivot to mask border. */
    else if (mode == PivotPositionMode::MaskBorder) {
      const float threshold = 0.2f;

      for (PBVHNode *node : nodes) {
        PBVHVertexIter vd;
        BKE_pbvh_vertex_iter_begin (*ss.pbvh, node, vd, PBVH_ITER_UNIQUE) {
          const float mask = vd.mask;
          if (mask < (0.5f + threshold) && mask > (0.5f - threshold)) {
            if (SCULPT_check_vertex_pivot_symmetry(vd.co, ss.pivot_pos, symm)) {
              add_v3_v3(avg, vd.co);
              total++;
            }
          }
        }
        BKE_pbvh_vertex_iter_end;
      }
    }

    if (total > 0) {
      mul_v3_fl(avg, 1.0f / total);
      copy_v3_v3(ss.pivot_pos, avg);
    }
  }

  /* Update the viewport navigation rotation origin. */
  UnifiedPaintSettings *ups = &CTX_data_tool_settings(C)->unified_paint_settings;
  copy_v3_v3(ups->average_stroke_accum, ss.pivot_pos);
  ups->average_stroke_counter = 1;
  ups->last_stroke_valid = true;

  ED_region_tag_redraw(region);
  WM_event_add_notifier(C, NC_GEOM | ND_SELECT, ob.data);

  return OPERATOR_FINISHED;
}

static int set_pivot_position_invoke(bContext *C, wmOperator *op, const wmEvent *event)
{
  RNA_float_set(op->ptr, "mouse_x", event->mval[0]);
  RNA_float_set(op->ptr, "mouse_y", event->mval[1]);
  return set_pivot_position_exec(C, op);
}

static bool set_pivot_position_poll_property(const bContext * /*C*/,
                                             wmOperator *op,
                                             const PropertyRNA *prop)
{
  if (STRPREFIX(RNA_property_identifier(prop), "mouse_")) {
    const PivotPositionMode mode = PivotPositionMode(RNA_enum_get(op->ptr, "mode"));
    return mode == PivotPositionMode::CursorSurface;
  }
  return true;
}

void SCULPT_OT_set_pivot_position(wmOperatorType *ot)
{
  ot->name = "Set Pivot Position";
  ot->idname = "SCULPT_OT_set_pivot_position";
  ot->description = "Sets the sculpt transform pivot position";

  ot->invoke = set_pivot_position_invoke;
  ot->exec = set_pivot_position_exec;
  ot->poll = SCULPT_mode_poll;
  ot->depends_on_cursor = set_pivot_depends_on_cursor;
  ot->poll_property = set_pivot_position_poll_property;

  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
  RNA_def_enum(ot->srna,
               "mode",
               prop_sculpt_pivot_position_types,
               int(PivotPositionMode::Unmasked),
               "Mode",
               "");

  RNA_def_float(ot->srna,
                "mouse_x",
                0.0f,
                0.0f,
                FLT_MAX,
                "Mouse Position X",
                "Position of the mouse used for \"Surface\" mode",
                0.0f,
                10000.0f);
  RNA_def_float(ot->srna,
                "mouse_y",
                0.0f,
                0.0f,
                FLT_MAX,
                "Mouse Position Y",
                "Position of the mouse used for \"Surface\" mode",
                0.0f,
                10000.0f);
}

}  // namespace blender::ed::sculpt_paint
