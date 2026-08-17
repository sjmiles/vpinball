// license:GPLv3+

#pragma once

#include "sur.h"

#include <sstream>

// Writes blueprint geometry as an ASCII DXF (R12/AC1009) file for CAD/CAM use.
// Coordinates are converted from VPX units to inches (VPUTOINCHES) and the Y axis
// is flipped (VPX Y grows downward, DXF Y grows upward), origin at the bottom-left
// corner of the playfield. Each part gets its own layer, named <TYPE>_<PARTNAME>,
// colored per part type, so toolpaths can be assigned per layer/part in CAM.
class DxfSur final : public Sur
{
public:
   DxfSur(const float left, const float top, const float right, const float bottom);
   ~DxfSur() override;

   void Line(const float x, const float y, const float x2, const float y2) override;
   void Rectangle(const float x, const float y, const float x2, float y2) override;
   void Rectangle2(const int x, const int y, const int x2, const int y2) override;
   void Ellipse(const float centerx, const float centery, const float radius) override;
   void Ellipse2(const float centerx, const float centery, const int radius) override;
   void Polygon(const Vertex2D * const rgv, const int count) override;
   void Polygon(const vector<RenderVertex> &rgv) override;
   void PolygonImage(const vector<RenderVertex> &rgv, HBITMAP hbm, const float left, const float top, const float right, const float bottom, const int bitmapwidth, const int bitmapheight) override;
   void Polyline(const Vertex2D * const rgv, const int count) override;
   void Lines(const Vertex2D * const rgv, const int count) override;
   void Arc(const float x, const float y, const float radius, const float pt1x, const float pt1y, const float pt2x, const float pt2y) override;
   void Image(const float x, const float y, const float x2, const float y2, HDC hdcSrc, const int width, const int height) override;

   void SetObject(ISelect * const psel) override;

   void SetFillColor(const int rgb) override;
   void SetBorderColor(const int rgb, const bool dashed, const int width) override;
   void SetLineColor(const int rgb, const bool dashed, const int width) override;

   bool Save(const string &filename) const;

private:
   float TX(const float x) const;
   float TY(const float y) const;
   void Extend(const float tx, const float ty);
   void EmitGroup(const int code, const string &value);
   void EmitGroup(const int code, const float value);
   void EmitLine(const float x, const float y, const float x2, const float y2);
   void EmitCircle(const float centerx, const float centery, const float radius);
   template <class T> void EmitPoly(const T *rgv, const int count, const bool closed);

   struct Layer
   {
      string name;
      int color; // AutoCAD Color Index
   };
   vector<Layer> m_layers;
   string m_curLayer;

   std::ostringstream m_entities;

   float m_left, m_top, m_bottom; // table extent in VPX units; m_bottom anchors the Y flip

   // drawing extents in DXF units (inches), for the header's $EXTMIN/$EXTMAX
   float m_minx, m_miny, m_maxx, m_maxy;
};
