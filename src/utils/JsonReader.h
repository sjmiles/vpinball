// license:GPLv3+

#pragma once

#include "fileio.h"

#include <filesystem>

namespace nlohmann { }

// Reads back what JsonWriter produced, through the same IObjectReader interface the binary
// BIFF format uses, so every part type is deserialized by its existing Load() with no
// per-part code.
//
// The writer emits an ordered list of { "f": tag, "v": value } entries (or "o" for sub
// objects); this reader walks that list and hands each field to the part, exactly like
// BiffReader walks records in a stream.
class JsonReader final : public IObjectReader
{
public:
   // Parses a project JSON file. Returns nullptr (and logs) if the file cannot be read.
   static std::unique_ptr<JsonReader> FromFile(const std::filesystem::path &path, int fileFormatVersion);
   ~JsonReader() override;

   int GetVersion() const override { return m_version; }
   bool HasError() const override { return m_hasError; }

   bool AsBool() override;
   int AsInt() override;
   unsigned int AsUInt() override;
   float AsFloat() override;
   string AsString() override;
   wstring AsWideString() override;
   Vertex2D AsVector2() override;
   vec3 AsVector3() override;
   vec4 AsVector4() override;
   string AsScript(bool isScriptProtected) override;
   FontDesc AsFontDescriptor() override;
   void AsRaw(void *pvalue, const int size) override;
   void AsObject(const std::function<bool(const int fieldTag, IObjectReader &fieldReader)> &processField, bool isSkippable = false) override;

private:
   JsonReader() = default;

   struct Impl;
   std::unique_ptr<Impl> m_impl;
   int m_version = 0;
   bool m_hasError = false;
};
