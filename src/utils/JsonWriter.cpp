// license:GPLv3+

#include "core/stdafx.h"
#include "JsonWriter.h"

#include <iomanip>
#include <sstream>

JsonWriter::JsonWriter(std::ostream &stream)
   : m_stream(stream)
{
   m_stream << "[\n";
   m_firstEntry.push_back(true);
   m_depth = 1;
}

string JsonWriter::TagToString(const int fieldId)
{
   const char tag[5] = { (char)(fieldId & 0xFF), (char)((fieldId >> 8) & 0xFF), (char)((fieldId >> 16) & 0xFF), (char)((fieldId >> 24) & 0xFF), '\0' };
   string result;
   for (const char c : string(tag))
      result += (c >= 32 && c < 127) ? c : '?';
   return result;
}

string JsonWriter::EscapeJson(const string &value)
{
   string out;
   out.reserve(value.length() + 2);
   for (const unsigned char c : value)
   {
      switch (c)
      {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
         if (c < 0x20)
         {
            char buf[8];
            snprintf(buf, sizeof(buf), "\\u%04x", c);
            out += buf;
         }
         else
            out += (char)c;
      }
   }
   return out;
}

string JsonWriter::Base64(const uint8_t *data, const size_t size)
{
   static constexpr char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
   string out;
   out.reserve(((size + 2) / 3) * 4);
   for (size_t i = 0; i < size; i += 3)
   {
      const uint32_t b0 = data[i];
      const uint32_t b1 = (i + 1 < size) ? data[i + 1] : 0;
      const uint32_t b2 = (i + 2 < size) ? data[i + 2] : 0;
      const uint32_t triple = (b0 << 16) | (b1 << 8) | b2;
      out += alphabet[(triple >> 18) & 0x3F];
      out += alphabet[(triple >> 12) & 0x3F];
      out += (i + 1 < size) ? alphabet[(triple >> 6) & 0x3F] : '=';
      out += (i + 2 < size) ? alphabet[triple & 0x3F] : '=';
   }
   return out;
}

void JsonWriter::Indent()
{
   for (int i = 0; i < m_depth; i++)
      m_stream << "  ";
}

void JsonWriter::BeginEntry(const int fieldId)
{
   if (!m_firstEntry.back())
      m_stream << ",\n";
   m_firstEntry.back() = false;
   Indent();
   m_stream << "{ \"f\": \"" << TagToString(fieldId) << "\", \"v\": ";
}

void JsonWriter::EndEntry()
{
   m_stream << " }";
}

void JsonWriter::BeginObject(const int objectId, bool isArray, bool isSkippable)
{
   if (!m_firstEntry.back())
      m_stream << ",\n";
   m_firstEntry.back() = false;
   Indent();
   m_stream << "{ \"f\": \"" << TagToString(objectId) << "\", \"o\": [\n";
   m_depth++;
   m_firstEntry.push_back(true);
}

void JsonWriter::EndObject()
{
   // the outermost EndObject closes the object being serialized, inner ones close sub objects
   m_stream << '\n';
   m_firstEntry.pop_back();
   m_depth--;
   Indent();
   if (m_depth > 0)
      m_stream << "] }";
   else
      m_stream << "]\n";
}

void JsonWriter::WriteBool(const int fieldId, const bool value)
{
   BeginEntry(fieldId);
   m_stream << (value ? "true" : "false");
   EndEntry();
}

void JsonWriter::WriteInt(const int fieldId, const int value)
{
   BeginEntry(fieldId);
   m_stream << value;
   EndEntry();
}

void JsonWriter::WriteUInt(const int fieldId, const unsigned int value)
{
   BeginEntry(fieldId);
   m_stream << value;
   EndEntry();
}

void JsonWriter::WriteFloat(const int fieldId, const float value)
{
   BeginEntry(fieldId);
   // round trip precision: 9 significant digits recovers any float exactly
   m_stream << std::setprecision(9) << value;
   EndEntry();
}

void JsonWriter::WriteString(const int fieldId, const string &value)
{
   BeginEntry(fieldId);
   m_stream << '"' << EscapeJson(value) << '"';
   EndEntry();
}

void JsonWriter::WriteWideString(const int fieldId, const wstring &value)
{
   BeginEntry(fieldId);
   m_stream << '"' << EscapeJson(MakeString(value)) << '"';
   EndEntry();
}

void JsonWriter::WriteVector2(const int fieldId, const Vertex2D &value)
{
   BeginEntry(fieldId);
   m_stream << std::setprecision(9) << '[' << value.x << ", " << value.y << ']';
   EndEntry();
}

void JsonWriter::WriteVector3(const int fieldId, const vec3 &value)
{
   BeginEntry(fieldId);
   m_stream << std::setprecision(9) << '[' << value.x << ", " << value.y << ", " << value.z << ']';
   EndEntry();
}

void JsonWriter::WriteVector4(const int fieldId, const vec4 &value)
{
   BeginEntry(fieldId);
   m_stream << std::setprecision(9) << '[' << value.x << ", " << value.y << ", " << value.z << ", " << value.w << ']';
   EndEntry();
}

void JsonWriter::WriteScript(const int fieldId, const string &value)
{
   BeginEntry(fieldId);
   m_stream << '"' << EscapeJson(value) << '"';
   EndEntry();
}

void JsonWriter::WriteFontDescriptor(const int fieldId, const FontDesc &value)
{
   BeginEntry(fieldId);
   m_stream << std::setprecision(9) << "{ \"version\": " << (int)value.version << ", \"charset\": " << (int)value.charset << ", \"attributes\": " << (int)value.attributes
            << ", \"weight\": " << (int)value.weight << ", \"size\": " << value.size << ", \"name\": \"" << EscapeJson(value.name) << "\" }";
   EndEntry();
}

void JsonWriter::WriteRaw(const int fieldId, const void *pvalue, const int size)
{
   BeginEntry(fieldId);
   m_stream << "{ \"raw\": \"" << Base64(static_cast<const uint8_t *>(pvalue), (size_t)max(size, 0)) << "\", \"size\": " << size << " }";
   EndEntry();
}
