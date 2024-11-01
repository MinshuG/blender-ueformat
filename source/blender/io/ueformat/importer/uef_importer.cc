/* SPDX-FileCopyrightText: 2024 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup ueformat
 */
#include "DNA_customdata_types.h"
#include "DNA_material_types.h"
#include "DNA_meshdata_types.h"
#include "DNA_object_types.h"

#include "BKE_context.hh"
#include "BKE_mesh.hh"
#include "BKE_object.hh"

#include "IO_ueformat.hh"
#include "uef_importer.hh"

#include "BKE_attribute.hh"
#include "BLI_math_matrix.h"
#include "uef_model_reader.hh"

namespace blender::io::ueformat {


Object* BuildUEModel(bContext *C, const FUEModelData model, const UEFORMATImportParams &import_params) {
  Main *bmain = CTX_data_main(C);
  Scene *scene = CTX_data_scene(C);
  ViewLayer *view_layer = CTX_data_view_layer(C);

  if (model.LODs.empty()) {
    return nullptr;
  }

  // if more than one lod parent lods to the first one
  Object *parent = nullptr;
  for (const auto &lod : model.LODs) {
    Mesh* mesh = BKE_mesh_new_nomain(lod.Vertices.size(), 0, lod.Indices.size()/3, lod.Indices.size());
    if (mesh == nullptr) {
      return nullptr;
    }

    Object* ob;
    if (parent == nullptr) {
      ob = BKE_object_add(bmain, scene, view_layer, OB_MESH, (model.Header.ObjectName + lod.LODName).c_str());
      ob->data = mesh;
      parent = ob;
    } else {
      Object *ob = BKE_object_add_from(bmain, scene, view_layer, OB_MESH, (model.Header.ObjectName + lod.LODName).c_str(), parent);
      ob->data = mesh;
    }

    // vertices
    mesh->vert_positions_for_write().copy_from(lod.Vertices);

    // faces
    MutableSpan<int> face_offsets = mesh->face_offsets_for_write(); // basically index where a face starts and goes till next entry(index)?
    MutableSpan<int> corner_verts = mesh->corner_verts_for_write();
    bke::MutableAttributeAccessor attributes = mesh->attributes_for_write();
    bke::SpanAttributeWriter<int> material_indices =
        attributes.lookup_or_add_for_write_only_span<int>("material_index", bke::AttrDomain::Face);

    corner_verts.copy_from(lod.Indices);
    // TODO optimize this
    int corner_index = 0;
    for (int face_idx = 0; face_idx < mesh->faces_num; ++face_idx) {
      face_offsets[face_idx] = corner_index;
      int mat_index = 0;
      for (int i = 0; i < lod.Materials.size(); i++) {
        if (lod.Materials[i].FirstIndex > face_idx) {
          mat_index = i - 1;
          break;
        }
      }

      material_indices.span[face_idx] = mat_index;
      corner_index += 3;
    }
    material_indices.finish();
    bke::mesh_calc_edges(*mesh, true, false);

    // normals
    if (!lod.Normals.empty()) {
      Array<float3> normals = Array<float3>(lod.Normals.size());
      for (int i = 0; i < lod.Normals.size(); i += 1) {
        normals[i] = lod.Normals[i].yzw(); // serialized as float4(blender: XYZW) WXYZ and we need only XYZ
      }

      BKE_mesh_set_custom_normals_from_verts(mesh, reinterpret_cast<float(*)[3]>(normals.data()));
    }


    float scale_vec[3] = {import_params.scale, import_params.scale, import_params.scale};
    float obmat4x4[4][4];
    unit_m4(obmat4x4);
    rescale_m4(obmat4x4, scale_vec);
    BKE_object_apply_mat4(ob, obmat4x4, true, false);
  }

  return parent;
}

void importer_main(bContext *C, const UEFORMATImportParams &import_params) {
  auto &filepath = import_params.filepath;
  auto model = ReadUEFModelData(filepath);
  if (model == nullptr) {
      return;
  }
  Object* obj = BuildUEModel(C, *model, import_params);
  if (obj == nullptr) {
      return;
  }
}
}  // namespace blender::io::ueformat
