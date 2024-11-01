#include "uef_model_reader.hh"
#undef ZSTD_LEGACY_SUPPORT
#include "zstd.h"

#define UEF_PERF 1

#if UEF_PERF
#include <chrono>
#include <iostream>
#include <ostream>
// #include <stdexcept>
#endif

#define UEFINLINE inline

UEFINLINE void ReadString(std::string &str, FILE *File)
{
  int len;
  fread(&len, sizeof(int), 1, File);
  str.resize(len);
  fread(&str[0], len, 1, File);
}

UEFINLINE void ReadString(std::string &str, FILE *File, size_t len)
{
  str.resize(len);
  fread(&str[0], len, 1, File);
}

UEFINLINE void ReadString(std::string &str, char *Data, long &Offset)
{
  int len = *(int *)&Data[Offset];
  Offset += sizeof(int);
  str.resize(len);
  memcpy((void*)str.data(), &Data[Offset], len);
  Offset += len;
}

UEFINLINE void ReadStruct(FILE *File, void *Struct, int Size)
{
  fread(Struct, Size, 1, File);
}

UEFINLINE void ReadStruct(char *Data, long &Offset, void *Struct, long Size)
{
  memcpy(Struct, &Data[Offset], Size);
  Offset += Size;
}

void ReadLods(std::vector<FLODData> &lods, const int numLods, char *DecompressedData, long &Offset)
{
  lods.resize(numLods);
  for (int i = 0; i < numLods; i++) {
    FLODData &lod = lods[i];
    
    ReadString(lod.LODName, DecompressedData, Offset);

    int LodsSize;
    ReadStruct(DecompressedData, Offset, &LodsSize, sizeof(int));
    long LodsOffset = Offset;

    while (LodsOffset < Offset + LodsSize) {
      std::string HeaderType;
      ReadString(HeaderType, DecompressedData, LodsOffset);  // VERTICES, INDICES, NORMALS, etc.
      int Num;
      ReadStruct(DecompressedData, LodsOffset, &Num, sizeof(int));
      int DataSize;
      ReadStruct(DecompressedData, LodsOffset, &DataSize, sizeof(int));

      if (HeaderType == "VERTICES") {
        lod.Vertices.resize(Num);
        ReadStruct(DecompressedData, LodsOffset, lod.Vertices.data(), Num * sizeof(float3));
      }
      else if (HeaderType == "INDICES") {
        lod.Indices.resize(Num);
        ReadStruct(DecompressedData, LodsOffset, lod.Indices.data(), Num * sizeof(int));
      }
      else if (HeaderType == "NORMALS") {
        lod.Normals.resize(Num);
        ReadStruct(DecompressedData, LodsOffset, lod.Normals.data(), Num * sizeof(float4));
      }
      else if (HeaderType == "TANGENTS") {
        // LodsOffset += Num * sizeof(float3);
        LodsOffset += DataSize;
      }
      else if (HeaderType == "VERTEXCOLORS") {
        lod.VertexColors.resize(Num);
        for (int j = 0; j < Num; j++) {
          FVertexColorChunk vtxColor;

          ReadString(vtxColor.Name, DecompressedData, LodsOffset);
          int vtxArraySize;
          ReadStruct(DecompressedData, LodsOffset, &vtxArraySize, sizeof(int));
          vtxColor.Data.resize(vtxArraySize);
          ReadStruct(
              DecompressedData, LodsOffset, vtxColor.Data.data(), vtxArraySize * sizeof(char4));

          lod.VertexColors[j] = vtxColor;
        }
      }
      else if (HeaderType == "TEXCOORDS") {
        lod.TextureCoordinates.resize(Num);
        for (size_t j = 0; j < Num; j++) {
          int vtxArraySize;
          ReadStruct(DecompressedData, LodsOffset, &vtxArraySize, sizeof(int));
          lod.TextureCoordinates[j].resize(vtxArraySize);
          ReadStruct(DecompressedData,
                     LodsOffset,
                     lod.TextureCoordinates[j].data(),
                     vtxArraySize * sizeof(float2));
        }
      }
      else if (HeaderType == "MATERIALS") {
        lod.Materials.resize(Num);
        for (size_t j = 0; j < Num; j++) {
          FMaterialChunk mat;
          ReadString(mat.Name, DecompressedData, LodsOffset);
          ReadStruct(DecompressedData, LodsOffset, &mat.FirstIndex, sizeof(int));
          ReadStruct(DecompressedData, LodsOffset, &mat.NumFaces, sizeof(int));
          lod.Materials[j] = mat;
        }
      }
      else if (HeaderType == "WEIGHTS") {
        lod.Weights.resize(Num);
        ReadStruct(DecompressedData, LodsOffset, lod.Weights.data(), Num * sizeof(FWeightChunk));
      }
      else if (HeaderType == "MORPHTARGETS") {
        lod.Morphs.resize(Num);
        for (size_t j = 0; j < Num; j++) {
          FMorphTargetChunk morph;
          ReadString(morph.MorphName, DecompressedData, LodsOffset);

          int NumDeltas;
          ReadStruct(DecompressedData, LodsOffset, &NumDeltas, sizeof(int));
          morph.MorphDeltas.resize(NumDeltas);
          ReadStruct(DecompressedData,
                     LodsOffset,
                     morph.MorphDeltas.data(),
                     NumDeltas * sizeof(FMorphTargetDataChunk));

          lod.Morphs[j] = morph;
        }
      }
      else {
        LodsOffset += DataSize;
      }
    }

    Offset += LodsSize;
  }
}

void ReadSkeleton(FSkeletonData &Skeleton, const int Num)
{
    Skeleton.Bones.resize(Num);
}

void ReadModel(FUEModelData *Data,
               char *DecompressedData,
               long &Offset,
               const long DecompressedSize)
{
  // uintptr_t Offset = 0;
  while (Offset < DecompressedSize) {
    std::string SectionType;
    ReadString(SectionType, DecompressedData, Offset);  // LODS, SKELETON, COLLISION
    int Num;
    ReadStruct(DecompressedData, Offset, &Num, sizeof(int));
    int DataSize;
    ReadStruct(DecompressedData, Offset, &DataSize, sizeof(int));

    if (SectionType == "LODS") {
      ReadLods(Data->LODs, Num, DecompressedData, Offset);
    }
    // else if (SectionType == "SKELETON")
    // {
    //     ReadSkeleton(Data.Skeleton, Num);
    // }
    // else if (SectionType == "COLLISION")
    // {
    //     ReadCollision(Data.Collision, Num);
    // }
    else
    {
        Offset += DataSize;
        // throw std::runtime_error("Invalid section type");
    }
  }
}

FUEModelData* ReadUEFModelData(const std::string &FilePath)
{
  FILE *File;
  auto err = fopen_s(&File, FilePath.c_str(), "rb");
  if (err != 0 || File == nullptr) {
    // throw std::runtime_error("Failed to open file");
    printf("failed to open file\n");
    return nullptr;
  }

  FUEModelData* Data = ReadUEFModelData(File);
  fclose(File);

  return Data;
}

FUEModelData* ReadUEFModelData(FILE *File)
{
  FUEModelData* Data;
  Data = new FUEModelData();
  // UEF_HEADER_IDENTIFIER;

  std::string Magic;
  ReadString(Magic, File, UEF_MAGIC.length());
  if (Magic != UEF_MAGIC) {
    // throw std::runtime_error("Invalid file format");
    return Data;
  }

  FUEFormatHeader Header;
  ReadString(Header.Identifier, File);
  fread(&Header.FileVersionBytes, sizeof(char), 1, File);

  // SerializeBinormalSign = 1
  // AddMultipleVertexColors = 2
  // AddConvexCollisionGeom = 3
  // LevelOfDetailFormatRestructure = 4
  // SerializeVirtualBones = 5
  if (!(Header.FileVersionBytes <= 5 || Header.FileVersionBytes > 3)) {
    // throw std::runtime_error("Invalid file version");
    printf("too old or too new file version\n");
    return nullptr;
  }

  ReadString(Header.ObjectName, File);
  fread(&Header.IsCompressed, sizeof(bool), 1, File);
  if (Header.IsCompressed) {
    ReadString(Header.CompressionType, File);
    ReadStruct(File, &Header.UncompressedSize, sizeof(int));
    ReadStruct(File, &Header.CompressedSize, sizeof(int));
  }

  Data->Header = Header;

  char *DecompressedData = nullptr;
  long DecompressedSize = 0;
  if (Header.IsCompressed) {
    DecompressedData = new char[Header.UncompressedSize];
    DecompressedSize = Header.UncompressedSize;
#if UEF_PERF
    auto start = std::chrono::high_resolution_clock::now();
#endif
    char *CompressedData = new char[Header.CompressedSize];
    ReadStruct(File, CompressedData, Header.CompressedSize);
#if UEF_PERF
    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = end - start;
    std::cout << "[IO] Time taken: " << elapsed.count() << " seconds" << "\n";
#endif
#if UEF_PERF
    auto decompress_start = std::chrono::high_resolution_clock::now();
#endif
    
    if (Header.CompressionType == "ZSTD") {
      size_t DecompressedSize = ZSTD_decompress(
          DecompressedData, Header.UncompressedSize, CompressedData, Header.CompressedSize);
      if (DecompressedSize != Header.UncompressedSize) {
        // throw std::runtime_error("Failed to decompress data");
        printf("Failed to decompress data\n");
        return nullptr;
      }
    }
    else {
        printf("Unsupported compression type\n");
        return nullptr;
    //   throw std::runtime_error("Unsupported compression type");
    }
    delete[] CompressedData;

#if UEF_PERF
    auto decompress_end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> decompress_elapsed = decompress_end - decompress_start;
    std::cout << "    [Decompress] " << decompress_elapsed.count() << " seconds" << "\n";
#endif

  }
  else {
    const long CurrentPos = ftell(File);
    fseek(File, 0, SEEK_END);
    const long EndPos = ftell(File);
    fseek(File, CurrentPos, SEEK_SET);

    DecompressedSize = EndPos - CurrentPos;
    DecompressedData = new char[DecompressedSize];
    ReadStruct(File, DecompressedData, DecompressedSize);
  }

#if UEF_PERF
  auto start = std::chrono::high_resolution_clock::now();
#endif

  long Offset = 0;
  if (Header.Identifier == "UEMODEL") {
    ReadModel(Data, DecompressedData, Offset, DecompressedSize);
  }

#if UEF_PERF
  auto end = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double> elapsed = end - start;
  std::cout << "      [LOD] " << elapsed.count() << " seconds" << "\n";
#endif
  return Data;
}
