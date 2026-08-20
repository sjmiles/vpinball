// license:GPLv3+

#pragma once

#include "fileio.h"

#include <ostream>

// Serializes any VPX object (table, part, collection, ...) as JSON through the same
// IObjectWriter interface the binary BIFF format uses, so every part type is covered
// by the part's existing Save() implementation without per-part code.
//
// Fields keep their file format tag (a four character id) and are emitted as an ordered
// list of entries rather than a map: BIFF is order sensitive and repeats tags for
// sequences (drag points, materials, ...), so preserving order and duplicates is what
// makes the format faithful. One entry per line keeps diffs readable.
class JsonWriter final : public IObjectWriter
{
public:
   explicit JsonWriter(std::ostream &stream);

   bool HasError() const override { return m_hasError; }

   void BeginObject(int objectId, bool isArray, bool isSkippable) override;
   void WriteBool(int fieldId, bool value) override;
   void WriteInt(int fieldId, int value) override;
   void WriteUInt(int fieldId, unsigned int value) override;
   void WriteFloat(int fieldId, float value) override;
   void WriteString(int fieldId, const string &value) override;
   void WriteWideString(int fieldId, const wstring &value) override;
   void WriteVector2(int fieldId, const Vertex2D &value) override;
   void WriteVector3(int fieldId, const vec3 &value) override;
   void WriteVector4(int fieldId, const vec4 &value) override;
   void WriteScript(int fieldId, const string &value) override;
   void WriteFontDescriptor(int fieldId, const FontDesc &value) override;
   void WriteRaw(int fieldId, const void *pvalue, const int size) override;
   void EndObject() override;

   static string TagToString(int fieldId); // four character file format tag, e.g. "LEFT"

private:
   void BeginEntry(int fieldId);
   void EndEntry();
   void Indent();
   static string EscapeJson(const string &value);
   static string Base64(const uint8_t *data, size_t size);

   std::ostream &m_stream;
   int m_depth = 0;
   vector<bool> m_firstEntry; // per nesting level, whether the next entry is the first
   bool m_hasError = false;
};
