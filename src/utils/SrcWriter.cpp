// license:GPLv3+

#include "core/stdafx.h"
#include "SrcWriter.h"
#include "JsonWriter.h"

#include <algorithm>
#include <charconv>

using ordered_json = nlohmann::ordered_json;

SrcWriter::SrcWriter(ordered_json &part, const SrcTypeFields &type, ordered_json *const index, const bool isNewPart)
   : m_index(index)
   , m_indexType(isNewPart ? &c_srcIndexTypeNew : &c_srcIndexType)
{
   m_levels.push_back({ &part, &type });
   if (part.contains("drag_points"s) && part["drag_points"].is_array())
      m_points = &part["drag_points"];
}

const SrcTypeFields *SrcWriter::FindType(const string &typeName)
{
   for (const SrcTypeFields &type : c_srcTypeFields)
      if (typeName == type.typeName)
         return &type;
   return nullptr;
}

ordered_json SrcWriter::Number(const float value)
{
   // The tree writes floats the way Rust does, shortest first: 79.3 stays 79.3 instead of
   // becoming 79.3000031, so a value that did not change produces no textual diff.
   char buf[32];
   const auto res = std::to_chars(buf, buf + sizeof(buf) - 3, value);
   string s(buf, res.ptr);
   if (s.find_first_of(".en") == string::npos) // an integral value is still a float there
      s += ".0";
   return ordered_json::parse(s);
}

static const SrcField *FindIn(const SrcTypeFields &type, const int fieldId)
{
   for (uint32_t i = 0; i < type.count; i++)
      if (type.fields[i].tag == (uint32_t)fieldId)
         return &type.fields[i];
   return nullptr;
}

SrcWriter::Resolved SrcWriter::Resolve(const int fieldId) const
{
   if (m_levels.empty())
      return { nullptr, nullptr };
   const Level &level = m_levels.back();
   if (level.target == nullptr || level.type == nullptr)
      return { nullptr, nullptr };
   // the shared attributes belong to the index entry, and only the part itself has them:
   // a drag point spells the same tags in its own object
   if (m_index != nullptr && m_levels.size() == 1)
      if (const SrcField *const shared = FindIn(*m_indexType, fieldId); shared != nullptr)
         return { shared, m_index };
   return { FindIn(*level.type, fieldId), level.target };
}

void SrcWriter::Skipped(const int fieldId)
{
   // inside an object the tree does not hold (materials and render probes, which it keeps
   // in files of their own) the object was named once already; naming each of its fields
   // as well would say nothing more
   if (!m_levels.empty() && m_levels.back().target == nullptr)
      return;
   const string tag = JsonWriter::TagToString(fieldId);
   if (std::ranges::find(m_unwritten, tag) == m_unwritten.end())
      m_unwritten.push_back(tag);
}

void SrcWriter::Put(const int fieldId, const ordered_json &value)
{
   const auto [field, targetPtr] = Resolve(fieldId);
   if (field == nullptr)
   {
      Skipped(fieldId);
      return;
   }
   ordered_json &target = *targetPtr;
   const auto found = target.find(field->key);
   if (found == target.end() || found->is_null())
   {
      Skipped(fieldId);
      return;
   }
   if (field->index >= 0)
   {
      if (found->is_array() && field->index < (int)found->size())
         (*found)[field->index] = value;
      else
         Skipped(fieldId);
      return;
   }
   *found = value;
}

void SrcWriter::PutXY(const int fieldId, const float x, const float y)
{
   const auto [field, targetPtr] = Resolve(fieldId);
   if (field == nullptr || field->key2 == nullptr)
   {
      Skipped(fieldId);
      return;
   }
   ordered_json &target = *targetPtr;
   if (target.contains(field->key) && target.contains(field->key2))
   {
      target[field->key] = Number(x);
      target[field->key2] = Number(y);
   }
   else
      Skipped(fieldId);
}

void SrcWriter::WriteBool(const int fieldId, const bool value) { Put(fieldId, value); }

void SrcWriter::WriteFloat(const int fieldId, const float value)
{
   Put(fieldId, Number(value));
}

void SrcWriter::WriteInt(const int fieldId, const int value)
{
   const SrcField *const field = Resolve(fieldId).field;
   if (field == nullptr)
   {
      Skipped(fieldId);
      return;
   }
   switch (field->kind)
   {
   case SrcValueKind::Color:
   {
      // the stored value is a Windows COLORREF, 0x00bbggrr; the tree writes "#rrggbb",
      // keeping the unused top byte in front of it when a table happens to carry one
      const unsigned int v = (unsigned int)value;
      char buf[16];
      if ((v >> 24) == 0)
         snprintf(buf, sizeof(buf), "#%02x%02x%02x", v & 0xFF, (v >> 8) & 0xFF, (v >> 16) & 0xFF);
      else
         snprintf(buf, sizeof(buf), "%02x#%02x%02x%02x", v >> 24, v & 0xFF, (v >> 8) & 0xFF, (v >> 16) & 0xFF);
      Put(fieldId, string(buf));
      break;
   }
   case SrcValueKind::UInt:
      // the tree holds it unsigned, and a tag like the physics loop limit uses the whole range
      Put(fieldId, (unsigned int)value);
      break;
   case SrcValueKind::Quantized8:
      // the file holds the byte the value was quantized to, the tree the value it means
      Put(fieldId, Number(dequantizeUnsigned<8>(value)));
      break;
   case SrcValueKind::Enum:
   {
      for (uint8_t i = 0; i < field->valueCount; i++)
         if (field->values[i].value == value)
         {
            Put(fieldId, string(field->values[i].name));
            return;
         }
      Put(fieldId, value); // a value outside the known range stays a number, as it is read
      break;
   }
   default: Put(fieldId, value); break;
   }
}

void SrcWriter::WriteUInt(const int fieldId, const unsigned int value) { WriteInt(fieldId, (int)value); }

void SrcWriter::WriteString(const int fieldId, const string &value) { Put(fieldId, value); }

void SrcWriter::WriteWideString(const int fieldId, const wstring &value) { Put(fieldId, MakeString(value)); }

void SrcWriter::WriteScript(const int fieldId, const string &value) { Put(fieldId, value); }

void SrcWriter::WriteVector2(const int fieldId, const Vertex2D &value)
{
   const SrcField *const field = Resolve(fieldId).field;
   if (field != nullptr && field->kind == SrcValueKind::Vertex2DXY)
   {
      PutXY(fieldId, value.x, value.y); // a drag point spells its position as plain x and y
      return;
   }
   ordered_json point = ordered_json::object();
   point["x"] = Number(value.x);
   point["y"] = Number(value.y);
   Put(fieldId, point);
}

void SrcWriter::WriteVector3(const int fieldId, const vec3 &value)
{
   ordered_json point = ordered_json::object();
   point["x"] = Number(value.x);
   point["y"] = Number(value.y);
   point["z"] = Number(value.z);
   Put(fieldId, point);
}

void SrcWriter::WriteVector4(const int fieldId, const vec4 &value)
{
   // positions and sizes are held as a vec4 with an unused w, and written as x, y, z
   WriteVector3(fieldId, vec3(value.x, value.y, value.z));
}

void SrcWriter::WriteFontDescriptor(const int fieldId, const FontDesc &value)
{
   const auto [field, targetPtr] = Resolve(fieldId);
   if (field == nullptr || !targetPtr->contains(field->key) || !(*targetPtr)[field->key].is_object())
   {
      Skipped(fieldId);
      return;
   }
   ordered_json &font = (*targetPtr)[field->key];

   // the style bits are written as an unordered set of names, so only replace the array
   // when the set actually differs: rewriting it would otherwise reorder it at random
   static constexpr const char *styleNames[] = { "Normal", "Bold", "Italic", "Underline", "Strikethrough" };
   vector<string> styles;
   for (int bit = 0; bit < 5; bit++)
      if ((value.attributes & (1 << bit)) != 0)
         styles.push_back(styleNames[bit]);
   if (font.contains("style"s) && font["style"].is_array())
   {
      vector<string> current;
      for (const auto &entry : font["style"])
         if (entry.is_string())
            current.push_back(entry.get<string>());
      vector<string> sortedCurrent = current, sortedNew = styles;
      std::ranges::sort(sortedCurrent);
      std::ranges::sort(sortedNew);
      if (sortedCurrent != sortedNew)
         font["style"] = styles;
   }
   if (font.contains("charset"s))
      font["charset"] = value.charset;
   if (font.contains("weight"s))
      font["weight"] = value.weight;
   if (font.contains("size"s))
      font["size"] = value.size;
   if (font.contains("name"s))
      font["name"] = value.name;
}

void SrcWriter::WriteRaw(const int fieldId, const void *pvalue, const int size)
{
   // mesh blobs and the like: the tree keeps those beside the JSON, never in it
   Skipped(fieldId);
}

void SrcWriter::BeginObject(const int objectId, bool isArray, bool isSkippable)
{
   static const SrcTypeFields *const dragPointType = FindType("DragPoint"s);
   if (objectId == FID(DPNT) && m_points != nullptr && dragPointType != nullptr)
   {
      m_pointIndex++;
      if ((size_t)m_pointIndex >= m_points->size())
      {
         // a point the editor added: start from the one before it so the bookkeeping the
         // tree keeps per point (lock and layer state) carries over instead of being made up
         m_points->push_back(m_points->empty() ? ordered_json::object() : m_points->back());
      }
      m_levels.push_back({ &(*m_points)[m_pointIndex], dragPointType });
      return;
   }
   Skipped(objectId);
   m_levels.push_back({ nullptr, nullptr });
}

void SrcWriter::EndObject()
{
   if (m_levels.size() > 1)
   {
      m_levels.pop_back();
      return;
   }

   // the part's own closing tag: everything has been written, so points the editor
   // removed can go. The level itself stays, so a stray write afterwards still lands
   // somewhere valid.
   if (m_points != nullptr && m_pointIndex >= 0 && m_points->size() > (size_t)(m_pointIndex + 1))
      m_points->erase(m_points->begin() + (m_pointIndex + 1), m_points->end());
}
