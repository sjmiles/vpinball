// license:GPLv3+

#pragma once

#include "fileio.h"
#include "SrcFieldMap.h"

#include "nlohmann/json.hpp"

// Writes a part into its file in a vpxtool source tree, through the same IObjectWriter
// interface the binary format uses, so every part type is covered by the part's existing
// Save() with no per-part code (the trick DxfSur and JsonWriter already use).
//
// The tree is a checked in source of truth, so this patches values into the JSON that is
// already there rather than regenerating it: keys keep their order, keys the editor knows
// nothing about keep their value, and a save that changed nothing rewrites nothing. Which
// tag lands in which key comes from SrcFieldMap.h, generated from both sides of the
// contract (see tools/srcmap).
class SrcWriter final : public IObjectWriter
{
public:
   // `part` is the object under the type name in a gameitems file, e.g. doc["Wall"], and
   // `index` the part's entry in gameitems.json, where the tree keeps the attributes every
   // part shares (lock and layer state) instead of in the part file.
   // `isNewPart` says the entry is being created rather than patched, which is the one
   // case where the legacy layer index and name are taken from the editor.
   SrcWriter(nlohmann::ordered_json &part, const SrcTypeFields &type, nlohmann::ordered_json *index, bool isNewPart = false);

   static const SrcTypeFields *FindType(const string &typeName);
   // A float as the tree spells it: shortest representation that reads back exactly.
   static nlohmann::ordered_json Number(float value);

   bool HasError() const override { return false; }
   // Tags the part wrote that did not reach the tree, for reporting: an edit to one of
   // these is not persisted, so they are named rather than silently dropped.
   const vector<string> &Unwritten() const { return m_unwritten; }

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
   void WriteRaw(int fieldId, const void *pvalue, int size) override;
   void EndObject() override;

private:
   struct Level
   {
      nlohmann::ordered_json *target; // null while inside an object the tree does not hold
      const SrcTypeFields *type;
   };

   struct Resolved
   {
      const SrcField *field;
      nlohmann::ordered_json *target;
   };
   Resolved Resolve(int fieldId) const;
   // Puts `value` in the field's key, unless the key is absent or holds null: the tree
   // only has a null where the table never had the field at all, and inventing one would
   // both change a file that did not change and lose that fact.
   void Put(int fieldId, const nlohmann::ordered_json &value);
   void PutXY(int fieldId, float x, float y);
   void Skipped(int fieldId);

   vector<Level> m_levels;
   nlohmann::ordered_json *m_index = nullptr;
   const SrcTypeFields *m_indexType = nullptr;
   nlohmann::ordered_json *m_points = nullptr; // the drag_points array, while writing one
   int m_pointIndex = -1;
   vector<string> m_unwritten;
};
