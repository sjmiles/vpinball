// license:GPLv3+

#include "core/stdafx.h"
#include "JsonReader.h"
#include "JsonWriter.h"

#include <fstream>

#include "nlohmann/json.hpp"

using json = nlohmann::json;

struct JsonReader::Impl
{
   json doc;
   // The entry list currently being iterated, and the entry the part is reading right now.
   // Nested sub objects push a new level, mirroring how BiffReader nests inside a record.
   vector<const json *> levelStack;
   const json *current = nullptr;
};

static int TagFromString(const string &tag)
{
   char t[4] = { ' ', ' ', ' ', ' ' };
   for (size_t i = 0; i < 4 && i < tag.length(); i++)
      t[i] = tag[i];
   return (int)((unsigned int)(unsigned char)t[0] | ((unsigned int)(unsigned char)t[1] << 8) | ((unsigned int)(unsigned char)t[2] << 16) | ((unsigned int)(unsigned char)t[3] << 24));
}

static vector<uint8_t> Base64Decode(const string &in)
{
   static constexpr auto index = [](const char c) -> int
   {
      if (c >= 'A' && c <= 'Z') return c - 'A';
      if (c >= 'a' && c <= 'z') return c - 'a' + 26;
      if (c >= '0' && c <= '9') return c - '0' + 52;
      if (c == '+') return 62;
      if (c == '/') return 63;
      return -1;
   };
   vector<uint8_t> out;
   out.reserve((in.length() / 4) * 3);
   uint32_t buffer = 0;
   int bits = 0;
   for (const char c : in)
   {
      const int value = index(c);
      if (value < 0)
         continue; // padding and whitespace
      buffer = (buffer << 6) | (uint32_t)value;
      bits += 6;
      if (bits >= 8)
      {
         bits -= 8;
         out.push_back((uint8_t)((buffer >> bits) & 0xFF));
      }
   }
   return out;
}

std::unique_ptr<JsonReader> JsonReader::FromFile(const std::filesystem::path &path, const int fileFormatVersion)
{
   std::ifstream file(path);
   if (!file.is_open())
   {
      PLOGE << "Could not open " << path.string();
      return nullptr;
   }

   auto reader = std::unique_ptr<JsonReader>(new JsonReader());
   reader->m_impl = std::make_unique<Impl>();
   reader->m_version = fileFormatVersion;
   try
   {
      reader->m_impl->doc = json::parse(file);
   }
   catch (const std::exception &e)
   {
      PLOGE << "Malformed project file " << path.string() << ": " << e.what();
      return nullptr;
   }
   if (!reader->m_impl->doc.is_array())
   {
      PLOGE << "Project file " << path.string() << " is not an entry list";
      return nullptr;
   }
   reader->m_impl->levelStack.push_back(&reader->m_impl->doc);
   return reader;
}

JsonReader::~JsonReader() = default;

void JsonReader::AsObject(const std::function<bool(const int, IObjectReader &)> &processField, bool isSkippable)
{
   if (m_impl->levelStack.empty())
   {
      m_hasError = true;
      return;
   }

   // a field callback that descends into a sub object reads the entries stored under "o"
   const json *level = m_impl->levelStack.back();
   if (m_impl->current != nullptr && m_impl->current->contains("o"))
      level = &(*m_impl->current)["o"];

   m_impl->levelStack.push_back(level);
   const json *const savedCurrent = m_impl->current;

   for (const auto &entry : *level)
   {
      if (!entry.contains("f"))
         continue;
      m_impl->current = &entry;
      const int tag = TagFromString(entry["f"].get<string>());
      if (!processField(tag, *this))
      {
         m_hasError = true;
         break;
      }
   }

   m_impl->current = savedCurrent;
   m_impl->levelStack.pop_back();
}

// Value accessors. A field whose type does not match what the part asks for is a corrupt
// or hand edited project: report the error rather than silently returning garbage.
#define CURRENT_VALUE(check, extract, fallback)                                                                                                                                    \
   if (m_impl->current == nullptr || !m_impl->current->contains("v"))                                                                                                              \
   {                                                                                                                                                                              \
      m_hasError = true;                                                                                                                                                          \
      return fallback;                                                                                                                                                            \
   }                                                                                                                                                                              \
   const json &v = (*m_impl->current)["v"];                                                                                                                                        \
   if (!(check))                                                                                                                                                                   \
   {                                                                                                                                                                              \
      m_hasError = true;                                                                                                                                                          \
      return fallback;                                                                                                                                                            \
   }                                                                                                                                                                              \
   return extract;

bool JsonReader::AsBool() { CURRENT_VALUE(v.is_boolean() || v.is_number(), v.is_boolean() ? v.get<bool>() : (v.get<int>() != 0), false) }
int JsonReader::AsInt() { CURRENT_VALUE(v.is_number(), v.get<int>(), 0) }
unsigned int JsonReader::AsUInt() { CURRENT_VALUE(v.is_number(), v.get<unsigned int>(), 0u) }
float JsonReader::AsFloat() { CURRENT_VALUE(v.is_number(), v.get<float>(), 0.f) }
string JsonReader::AsString() { CURRENT_VALUE(v.is_string(), v.get<string>(), string()) }
string JsonReader::AsScript(bool isScriptProtected) { return AsString(); }
wstring JsonReader::AsWideString() { return MakeWString(AsString()); }

Vertex2D JsonReader::AsVector2() { CURRENT_VALUE(v.is_array() && v.size() >= 2, (Vertex2D { v[0].get<float>(), v[1].get<float>() }), (Vertex2D { 0.f, 0.f })) }
vec3 JsonReader::AsVector3() { CURRENT_VALUE(v.is_array() && v.size() >= 3, (vec3(v[0].get<float>(), v[1].get<float>(), v[2].get<float>())), vec3()) }
vec4 JsonReader::AsVector4() { CURRENT_VALUE(v.is_array() && v.size() >= 4, (vec4(v[0].get<float>(), v[1].get<float>(), v[2].get<float>(), v[3].get<float>())), vec4()) }

FontDesc JsonReader::AsFontDescriptor()
{
   FontDesc desc;
   if (m_impl->current == nullptr || !m_impl->current->contains("v") || !(*m_impl->current)["v"].is_object())
   {
      m_hasError = true;
      return desc;
   }
   const json &v = (*m_impl->current)["v"];
   desc.version = (uint8_t)v.value("version", 1);
   desc.charset = (uint16_t)v.value("charset", 0);
   desc.attributes = (uint8_t)v.value("attributes", 0);
   desc.weight = (uint16_t)v.value("weight", 0);
   desc.size = (uint32_t)v.value("size", 0);
   desc.name = v.value("name", string());
   return desc;
}

void JsonReader::AsRaw(void *pvalue, const int size)
{
   if (m_impl->current == nullptr || !m_impl->current->contains("v") || !(*m_impl->current)["v"].is_object())
   {
      m_hasError = true;
      return;
   }
   const vector<uint8_t> bytes = Base64Decode((*m_impl->current)["v"].value("raw", string()));
   if ((int)bytes.size() < size)
   {
      m_hasError = true;
      return;
   }
   memcpy(pvalue, bytes.data(), (size_t)size);
}
