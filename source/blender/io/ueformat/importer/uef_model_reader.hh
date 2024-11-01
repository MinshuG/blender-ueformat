#pragma once
#include "BLI_math_vector_types.hh"

#include <string>
#include <vector>

/*
template<typename T, int Size> struct vec_struct_base {
  std::array<T, Size> values;
};

template<typename T> struct vec_struct_base<T, 2> {
  T x, y;
};

template<typename T> struct vec_struct_base<T, 3> {
  T x, y, z;
};

template<typename T> struct vec_struct_base<T, 4> {
  T x, y, z, w;
};

template<typename T, int Size> struct VecBase : public vec_struct_base<T, Size> {};

using char2 = VecBase<int8_t, 2>;
using char3 = VecBase<int8_t, 3>;
using char4 = VecBase<int8_t, 4>;

using uchar3 = VecBase<uint8_t, 3>;
using uchar4 = VecBase<uint8_t, 4>;

using int2 = VecBase<int32_t, 2>;
using int3 = VecBase<int32_t, 3>;
using int4 = VecBase<int32_t, 4>;

using uint2 = VecBase<uint32_t, 2>;
using uint3 = VecBase<uint32_t, 3>;
using uint4 = VecBase<uint32_t, 4>;

using short2 = VecBase<int16_t, 2>;
using short3 = VecBase<int16_t, 3>;
using short4 = VecBase<int16_t, 4>;

using ushort2 = VecBase<uint16_t, 2>;
using ushort3 = VecBase<uint16_t, 3>;
using ushort4 = VecBase<uint16_t, 4>;

using float1 = VecBase<float, 1>;
using float2 = VecBase<float, 2>;
using float3 = VecBase<float, 3>;
using float4 = VecBase<float, 4>;

using double2 = VecBase<double, 2>;
using double3 = VecBase<double, 3>;
using double4 = VecBase<double, 4>;*/

using namespace blender;

struct FVertexColorChunk {
  std::string Name;
  //int Count;
  std::vector<char4> Data;
};
struct FWeightChunk {
  short WeightBoneIndex;
  int WeightVertexIndex;
  float WeightAmount;
};
struct FBoneChunk {
  std::string BoneName;
  int BoneParentIndex;
  blender::float3 BonePos;
  float4 BoneRot;  // FQuat
};
struct FSocketChunk {
  std::string SocketName;
  std::string SocketParentName;
  float4 SocketPos;
  float4 SocketRot;
  float3 SocketScale;
};
struct FMaterialChunk {
  // int MatIndex;
  std::string Name;
  int FirstIndex;
  int NumFaces;
};
struct FMorphTargetDataChunk {
  float3 MorphPosition;
  float3 MorphNormals;
  int MorphVertexIndex;
};
struct FMorphTargetChunk {
  std::string MorphName;
  std::vector<FMorphTargetDataChunk> MorphDeltas;
};
struct FUEFormatHeader {
  std::string Identifier;
  char FileVersionBytes;
  std::string ObjectName;
  bool IsCompressed;
  std::string CompressionType;
  int CompressedSize;
  int UncompressedSize;
};

struct FLODData {
  std::string LODName;
  std::vector<float3> Vertices;
  std::vector<int> Indices;
  std::vector<float4> Normals;  // W XYZ
  std::vector<float3> Tangents;
  std::vector<FVertexColorChunk> VertexColors;
  std::vector<std::vector<float2>> TextureCoordinates;
  std::vector<FMaterialChunk> Materials;
  std::vector<FWeightChunk> Weights;
  std::vector<FMorphTargetChunk> Morphs;
};

struct FSkeletonData {
  std::vector<FBoneChunk> Bones;
  std::vector<FSocketChunk> Sockets;
};

const std::string UEF_MAGIC = "UEFORMAT";

struct FUEModelData {
  FUEFormatHeader Header;
  std::vector<FLODData> LODs;
  FSkeletonData Skeleton;
};

FUEModelData* ReadUEFModelData(const std::string &FilePath);

FUEModelData* ReadUEFModelData(FILE *File);
