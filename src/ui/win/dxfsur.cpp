// license:GPLv3+

#include "core/stdafx.h"
#include "dxfsur.h"

#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <fstream>

// All output coordinates are inches: 1 inch = 50/1.0625 VPX units (see VPUTOINCHES in core/def.h —
// note the 17/16 factor, it is NOT a plain 50 units per inch)
#define DXF_SCALE VPUTOINCHES(1.0f)

static string FormatDxfFloat(const float v)
{
   char buf[32];
   snprintf(buf, sizeof(buf), "%.6f", v);
   return buf;
}

static const char *ItemTypeToLayerPrefix(const ItemTypeEnum type)
{
   switch (type)
   {
   case eItemSurface: return "WALL";
   case eItemFlipper: return "FLIPPER";
   case eItemPlunger: return "PLUNGER";
   case eItemTextbox: return "TEXTBOX";
   case eItemBumper: return "BUMPER";
   case eItemTrigger: return "TRIGGER";
   case eItemLight: return "LIGHT";
   case eItemKicker: return "KICKER";
   case eItemDecal: return "DECAL";
   case eItemGate: return "GATE";
   case eItemSpinner: return "SPINNER";
   case eItemRamp: return "RAMP";
   case eItemPrimitive: return "PRIMITIVE";
   case eItemFlasher: return "FLASHER";
   case eItemRubber: return "RUBBER";
   case eItemHitTarget: return "TARGET";
   default: return "MISC";
   }
}

static int ItemTypeToLayerColor(const ItemTypeEnum type)
{
   switch (type) // AutoCAD Color Index
   {
   case eItemSurface: return 1;    // red
   case eItemLight: return 2;      // yellow
   case eItemRubber: return 3;     // green
   case eItemTrigger: return 4;    // cyan
   case eItemBumper: return 5;     // blue
   case eItemKicker: return 6;     // magenta
   case eItemFlipper: return 30;   // orange
   case eItemGate: return 8;       // dark gray
   case eItemSpinner: return 9;    // light gray
   case eItemRamp: return 140;     // steel blue
   case eItemPrimitive: return 252;// dark gray
   case eItemFlasher: return 41;   // pale yellow
   case eItemHitTarget: return 11; // pale red
   default: return 7;              // white/black
   }
}

DxfSur::DxfSur(const float left, const float top, const float right, const float bottom)
   : Sur(nullptr, 1.0f, (left + right) * 0.5f, (top + bottom) * 0.5f, (int)(right - left), (int)(bottom - top))
{
   m_left = left;
   m_top = top;
   m_bottom = bottom;
   m_curLayer = "0";
   m_layers.push_back({ "0"s, 7 });
   m_minx = m_miny = FLT_MAX;
   m_maxx = m_maxy = -FLT_MAX;
}

DxfSur::~DxfSur()
{
}

float DxfSur::TX(const float x) const
{
   return (x - m_left) * DXF_SCALE;
}

float DxfSur::TY(const float y) const
{
   return (m_bottom - y) * DXF_SCALE; // flip: VPX Y grows downward, DXF Y grows upward
}

void DxfSur::Extend(const float tx, const float ty)
{
   m_minx = min(m_minx, tx);
   m_miny = min(m_miny, ty);
   m_maxx = max(m_maxx, tx);
   m_maxy = max(m_maxy, ty);
}

void DxfSur::EmitGroup(const int code, const string &value)
{
   m_entities << code << '\n' << value << '\n';
}

void DxfSur::EmitGroup(const int code, const float value)
{
   m_entities << code << '\n' << FormatDxfFloat(value) << '\n';
}

void DxfSur::EmitLine(const float x, const float y, const float x2, const float y2)
{
   const float tx1 = TX(x), ty1 = TY(y), tx2 = TX(x2), ty2 = TY(y2);
   EmitGroup(0, "LINE"s);
   EmitGroup(8, m_curLayer);
   EmitGroup(10, tx1);
   EmitGroup(20, ty1);
   EmitGroup(11, tx2);
   EmitGroup(21, ty2);
   Extend(tx1, ty1);
   Extend(tx2, ty2);
}

void DxfSur::EmitCircle(const float centerx, const float centery, const float radius)
{
   const float tx = TX(centerx), ty = TY(centery), tr = radius * DXF_SCALE;
   EmitGroup(0, "CIRCLE"s);
   EmitGroup(8, m_curLayer);
   EmitGroup(10, tx);
   EmitGroup(20, ty);
   EmitGroup(40, tr);
   Extend(tx - tr, ty - tr);
   Extend(tx + tr, ty + tr);
}

// R12 has no LWPOLYLINE; emit the classic POLYLINE/VERTEX/SEQEND sequence
template <class T> void DxfSur::EmitPoly(const T *rgv, const int count, const bool closed)
{
   if (count < 2)
      return;
   EmitGroup(0, "POLYLINE"s);
   EmitGroup(8, m_curLayer);
   EmitGroup(66, "1"s);
   EmitGroup(70, closed ? "1"s : "0"s);
   EmitGroup(10, 0.f);
   EmitGroup(20, 0.f);
   EmitGroup(30, 0.f);
   for (int i = 0; i < count; ++i)
   {
      const float tx = TX(rgv[i].x), ty = TY(rgv[i].y);
      EmitGroup(0, "VERTEX"s);
      EmitGroup(8, m_curLayer);
      EmitGroup(10, tx);
      EmitGroup(20, ty);
      EmitGroup(30, 0.f);
      Extend(tx, ty);
   }
   EmitGroup(0, "SEQEND"s);
   EmitGroup(8, m_curLayer);
}

void DxfSur::Line(const float x, const float y, const float x2, const float y2)
{
   EmitLine(x, y, x2, y2);
}

void DxfSur::Rectangle(const float x, const float y, const float x2, float y2)
{
   const Vertex2D rgv[4] = { { x, y }, { x2, y }, { x2, y2 }, { x, y2 } };
   EmitPoly(rgv, 4, true);
}

void DxfSur::Rectangle2(const int x, const int y, const int x2, const int y2)
{
   // screen-space UI decoration (window background fill) - not table geometry
}

void DxfSur::Ellipse(const float centerx, const float centery, const float radius)
{
   EmitCircle(centerx, centery, radius);
}

void DxfSur::Ellipse2(const float centerx, const float centery, const int radius)
{
   // screen-space UI decoration (drag point handles, pixel radius) - not table geometry
}

void DxfSur::Polygon(const Vertex2D * const rgv, const int count)
{
   EmitPoly(rgv, count, true);
}

void DxfSur::Polygon(const vector<RenderVertex> &rgv)
{
   EmitPoly(rgv.data(), (int)rgv.size(), true);
}

void DxfSur::PolygonImage(const vector<RenderVertex> &rgv, HBITMAP hbm, const float left, const float top, const float right, const float bottom, const int bitmapwidth, const int bitmapheight)
{
   // keep the polygon outline, drop the raster fill
   EmitPoly(rgv.data(), (int)rgv.size(), true);
}

void DxfSur::Polyline(const Vertex2D * const rgv, const int count)
{
   EmitPoly(rgv, count, false);
}

void DxfSur::Lines(const Vertex2D * const rgv, const int count)
{
   for (int i = 0; i + 1 < count; i += 2)
      EmitLine(rgv[i].x, rgv[i].y, rgv[i + 1].x, rgv[i + 1].y);
}

void DxfSur::Arc(const float x, const float y, const float radius, const float pt1x, const float pt1y, const float pt2x, const float pt2y)
{
   const float tx = TX(x), ty = TY(y), tr = radius * DXF_SCALE;
   // GDI Arc() draws counterclockwise (as displayed) from pt1 to pt2; after the Y flip that
   // matches DXF's counterclockwise sweep from start (50) to end (51) angle
   const float a1 = RADTOANG(atan2f(TY(pt1y) - ty, TX(pt1x) - tx));
   const float a2 = RADTOANG(atan2f(TY(pt2y) - ty, TX(pt2x) - tx));
   EmitGroup(0, "ARC"s);
   EmitGroup(8, m_curLayer);
   EmitGroup(10, tx);
   EmitGroup(20, ty);
   EmitGroup(40, tr);
   EmitGroup(50, a1 < 0.f ? a1 + 360.f : a1);
   EmitGroup(51, a2 < 0.f ? a2 + 360.f : a2);
   Extend(tx - tr, ty - tr);
   Extend(tx + tr, ty + tr);
}

void DxfSur::Image(const float x, const float y, const float x2, const float y2, HDC hdcSrc, const int width, const int height)
{
   // raster content, meaningless for CAD/CAM
}

void DxfSur::SetObject(ISelect * const psel)
{
   // parts announce themselves via SetObject(this) and often follow up with SetObject(nullptr)
   // before drawing, so a null selection keeps the current layer rather than resetting it
   if (psel == nullptr)
      return;

   const ItemTypeEnum type = psel->GetItemType();
   string name = ItemTypeToLayerPrefix(type);
   if (const IEditable *const pedit = psel->GetIEditable(); pedit != nullptr)
   {
      const string itemName = pedit->GetName();
      if (!itemName.empty())
      {
         name += '_';
         for (const char c : itemName)
            name += isalnum((unsigned char)c) ? (char)toupper((unsigned char)c) : '_';
      }
   }
   if (name.length() > 31) // R12 layer name limit
      name = name.substr(0, 31);

   m_curLayer = name;
   for (const Layer &layer : m_layers)
      if (layer.name == name)
         return;
   m_layers.push_back({ name, ItemTypeToLayerColor(type) });
}

void DxfSur::SetFillColor(const int rgb)
{
}

void DxfSur::SetBorderColor(const int rgb, const bool dashed, const int width)
{
}

void DxfSur::SetLineColor(const int rgb, const bool dashed, const int width)
{
}

bool DxfSur::Save(const string &filename) const
{
   std::ofstream f(filename); // text mode so line endings are native
   if (!f.is_open())
      return false;

   const bool empty = (m_minx > m_maxx);
   const string minx = FormatDxfFloat(empty ? 0.f : m_minx), miny = FormatDxfFloat(empty ? 0.f : m_miny);
   const string maxx = FormatDxfFloat(empty ? 0.f : m_maxx), maxy = FormatDxfFloat(empty ? 0.f : m_maxy);

   f << "0\nSECTION\n2\nHEADER\n";
   f << "9\n$ACADVER\n1\nAC1009\n";
   f << "9\n$INSUNITS\n70\n1\n";     // inches (ignored by strict R12 readers, helps modern ones)
   f << "9\n$MEASUREMENT\n70\n0\n";  // english
   f << "9\n$EXTMIN\n10\n" << minx << "\n20\n" << miny << "\n30\n0.0\n";
   f << "9\n$EXTMAX\n10\n" << maxx << "\n20\n" << maxy << "\n30\n0.0\n";
   f << "0\nENDSEC\n";

   f << "0\nSECTION\n2\nTABLES\n";
   f << "0\nTABLE\n2\nLTYPE\n70\n1\n";
   f << "0\nLTYPE\n2\nCONTINUOUS\n70\n0\n3\nSolid line\n72\n65\n73\n0\n40\n0.0\n";
   f << "0\nENDTAB\n";
   f << "0\nTABLE\n2\nLAYER\n70\n" << m_layers.size() << '\n';
   for (const Layer &layer : m_layers)
      f << "0\nLAYER\n2\n" << layer.name << "\n70\n0\n62\n" << layer.color << "\n6\nCONTINUOUS\n";
   f << "0\nENDTAB\n";
   f << "0\nENDSEC\n";

   f << "0\nSECTION\n2\nENTITIES\n";
   f << m_entities.str();
   f << "0\nENDSEC\n";
   f << "0\nEOF\n";

   return f.good();
}
