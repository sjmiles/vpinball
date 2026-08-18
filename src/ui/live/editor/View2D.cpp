// license:GPLv3+

#include "core/stdafx.h"
#include "View2D.h"

#include "parts/pintable.h"
#include "ui/win/sur.h"

#include "imgui/imgui.h"

namespace VPX::EditorUI
{

namespace
{

// Draws the blueprint geometry stream (see Sur) into an ImGui draw list.
// Input coordinates are VPX units (y down); output is screen pixels through a
// board-anchored inch world space (y up), so the on-screen picture matches the
// DXF export and the physical board.
class ImGuiSur final : public Sur
{
public:
   ImGuiSur(ImDrawList *drawList, const PinTable *table, const ImVec2 &viewCenter, const Vertex2D &worldCenter, float zoom)
      : Sur(nullptr, 1.f, 0.f, 0.f, 0, 0)
      , m_drawList(drawList)
      , m_viewCenter(viewCenter)
      , m_worldCenter(worldCenter)
      , m_pxPerInch(zoom)
   {
      m_tableLeft = table->m_left;
      m_tableBottom = table->m_bottom;
   }

   ImVec2 P(const float x, const float y) const // VPX units -> screen
   {
      const float wx = (x - m_tableLeft) * (float)VPUTOINCHES(1.);
      const float wy = (m_tableBottom - y) * (float)VPUTOINCHES(1.);
      return { m_viewCenter.x + (wx - m_worldCenter.x) * m_pxPerInch, m_viewCenter.y - (wy - m_worldCenter.y) * m_pxPerInch };
   }

   void Line(const float x, const float y, const float x2, const float y2) override { m_drawList->AddLine(P(x, y), P(x2, y2), StrokeColor()); }

   void Rectangle(const float x, const float y, const float x2, float y2) override
   {
      const ImVec2 pts[4] = { P(x, y), P(x2, y), P(x2, y2), P(x, y2) };
      m_drawList->AddPolyline(pts, 4, StrokeColor(), ImDrawFlags_Closed, 1.f);
   }

   void Rectangle2(const int x, const int y, const int x2, const int y2) override { } // screen-space UI decoration

   void Ellipse(const float centerx, const float centery, const float radius) override
   {
      m_drawList->AddCircle(P(centerx, centery), radius * (float)VPUTOINCHES(1.) * m_pxPerInch, StrokeColor());
   }

   void Ellipse2(const float centerx, const float centery, const int radius) override
   {
      m_drawList->AddCircle(P(centerx, centery), (float)radius, StrokeColor()); // pixel radius (drag point style handles)
   }

   void Polygon(const Vertex2D *const rgv, const int count) override { Poly(rgv, count, true); }
   void Polygon(const vector<RenderVertex> &rgv) override { Poly(rgv.data(), (int)rgv.size(), true); }

   void PolygonImage(const vector<RenderVertex> &rgv, HBITMAP hbm, const float left, const float top, const float right, const float bottom, const int bitmapwidth,
      const int bitmapheight) override
   {
      Poly(rgv.data(), (int)rgv.size(), true); // outline only, no raster fill
   }

   void Polyline(const Vertex2D *const rgv, const int count) override { Poly(rgv, count, false); }

   void Lines(const Vertex2D *const rgv, const int count) override
   {
      for (int i = 0; i + 1 < count; i += 2)
         m_drawList->AddLine(P(rgv[i].x, rgv[i].y), P(rgv[i + 1].x, rgv[i + 1].y), StrokeColor());
   }

   void Arc(const float x, const float y, const float radius, const float pt1x, const float pt1y, const float pt2x, const float pt2y) override
   {
      // GDI Arc() semantics: counterclockwise (as displayed) from pt1 to pt2. ImGui's
      // PathArcTo sweeps a_min -> a_max clockwise on screen (y down), which traces the
      // same point set going pt2 -> pt1.
      const ImVec2 c = P(x, y);
      const ImVec2 p1 = P(pt1x, pt1y);
      const ImVec2 p2 = P(pt2x, pt2y);
      const float r = radius * (float)VPUTOINCHES(1.) * m_pxPerInch;
      const float a1 = atan2f(p1.y - c.y, p1.x - c.x);
      float a2 = atan2f(p2.y - c.y, p2.x - c.x);
      if (a1 < a2)
         a2 -= 2.f * (float)M_PI;
      m_drawList->PathArcTo(c, r, a2, a1);
      m_drawList->PathStroke(StrokeColor(), 0, 1.f);
   }

   void Image(const float x, const float y, const float x2, const float y2, HDC hdcSrc, const int width, const int height) override { } // raster content

   void SetObject(ISelect *const psel) override { } // selection comes in a later stage

   void SetFillColor(const int rgb) override { }
   void SetBorderColor(const int rgb, const bool dashed, const int width) override { m_dashed = dashed; }
   void SetLineColor(const int rgb, const bool dashed, const int width) override { m_dashed = dashed; }

private:
   template <class T> void Poly(const T *rgv, const int count, const bool closed)
   {
      if (count < 2)
         return;
      m_points.resize(count);
      for (int i = 0; i < count; ++i)
         m_points[i] = P(rgv[i].x, rgv[i].y);
      m_drawList->AddPolyline(m_points.data(), count, StrokeColor(), closed ? ImDrawFlags_Closed : ImDrawFlags_None, 1.f);
   }

   ImU32 StrokeColor() const { return m_dashed ? IM_COL32(210, 215, 220, 80) : IM_COL32(210, 215, 220, 220); }

   ImDrawList *const m_drawList;
   const ImVec2 m_viewCenter;
   const Vertex2D m_worldCenter;
   const float m_pxPerInch;
   float m_tableLeft, m_tableBottom;
   bool m_dashed = false;
   vector<ImVec2> m_points;
};

} // anonymous namespace

void View2D::Render(PinTable *table, float dpi, PropertyPane::Unit lengthUnit)
{
   if (table == nullptr)
      return;

   const float S = (float)VPUTOINCHES(1.);
   const float canvasW = (table->m_right - table->m_left) * S;
   const float canvasH = (table->m_bottom - table->m_top) * S;

   ImGui::SetNextWindowSize(ImVec2(420.f * dpi, 700.f * dpi), ImGuiCond_FirstUseEver);
   if (!ImGui::Begin("2D CAD View", &m_show, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
   {
      ImGui::End();
      return;
   }

   // options strip
   if (ImGui::Button("Fit"))
      m_zoom = 0.f;
   ImGui::SameLine();
   ImGui::SetNextItemWidth(70.f * dpi);
   ImGui::InputFloat("W\"", &m_boardWidth, 0.f, 0.f, "%.3f");
   ImGui::SameLine();
   ImGui::SetNextItemWidth(70.f * dpi);
   ImGui::InputFloat("H\"", &m_boardHeight, 0.f, 0.f, "%.3f");
   ImGui::SameLine();
   ImGui::TextDisabled("board");

   ImDrawList *const dl = ImGui::GetWindowDrawList();
   const ImVec2 canvasPos = ImGui::GetCursorScreenPos();
   ImVec2 avail = ImGui::GetContentRegionAvail();
   avail.x = max(avail.x, 50.f);
   avail.y = max(avail.y, 50.f);
   const ImVec2 viewCenter { canvasPos.x + avail.x * 0.5f, canvasPos.y + avail.y * 0.5f };

   if (m_zoom <= 0.f) // fit to window
   {
      m_zoom = min((avail.x - 20.f) / canvasW, (avail.y - 20.f) / canvasH);
      m_center = { canvasW * 0.5f, canvasH * 0.5f };
   }

   // input: pan with left/middle drag, zoom on wheel centered at the cursor
   ImGui::InvisibleButton("view2d_canvas", avail, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle);
   const bool hovered = ImGui::IsItemHovered();
   ImGuiIO &io = ImGui::GetIO();
   if (ImGui::IsItemActive() && (ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.f) || ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.f)))
   {
      m_center.x -= io.MouseDelta.x / m_zoom;
      m_center.y += io.MouseDelta.y / m_zoom;
   }
   const Vertex2D mouseWorld { m_center.x + (io.MousePos.x - viewCenter.x) / m_zoom, m_center.y - (io.MousePos.y - viewCenter.y) / m_zoom };
   if (hovered && io.MouseWheel != 0.f)
   {
      const float factor = powf(1.15f, io.MouseWheel);
      // keep the world point under the cursor fixed while zooming
      m_center.x = mouseWorld.x - (mouseWorld.x - m_center.x) / factor;
      m_center.y = mouseWorld.y - (mouseWorld.y - m_center.y) / factor;
      m_zoom *= factor;
   }

   dl->PushClipRect(canvasPos, ImVec2(canvasPos.x + avail.x, canvasPos.y + avail.y), true);
   dl->AddRectFilled(canvasPos, ImVec2(canvasPos.x + avail.x, canvasPos.y + avail.y), IM_COL32(24, 26, 30, 255));

   const auto toScreen = [&](float wx, float wy) -> ImVec2 { return { viewCenter.x + (wx - m_center.x) * m_zoom, viewCenter.y - (wy - m_center.y) * m_zoom }; };

   // unit grid (inch-based; the metric skin only changes the readout)
   const float wxMin = m_center.x - avail.x * 0.5f / m_zoom, wxMax = m_center.x + avail.x * 0.5f / m_zoom;
   const float wyMin = m_center.y - avail.y * 0.5f / m_zoom, wyMax = m_center.y + avail.y * 0.5f / m_zoom;
   if (m_zoom > 24.f) // minor grid: quarter inch
      for (float g = floorf(wxMin * 4.f) * 0.25f; g <= wxMax; g += 0.25f)
         dl->AddLine(toScreen(g, wyMin), toScreen(g, wyMax), IM_COL32(255, 255, 255, 10));
   if (m_zoom > 24.f)
      for (float g = floorf(wyMin * 4.f) * 0.25f; g <= wyMax; g += 0.25f)
         dl->AddLine(toScreen(wxMin, g), toScreen(wxMax, g), IM_COL32(255, 255, 255, 10));
   if (m_zoom > 4.f) // major grid: one inch
   {
      for (float g = floorf(wxMin); g <= wxMax; g += 1.f)
         dl->AddLine(toScreen(g, wyMin), toScreen(g, wyMax), IM_COL32(255, 255, 255, 26));
      for (float g = floorf(wyMin); g <= wyMax; g += 1.f)
         dl->AddLine(toScreen(wxMin, g), toScreen(wxMax, g), IM_COL32(255, 255, 255, 26));
   }

   // canvas edge (VPX table rect) and physical board overlay
   dl->AddRect(toScreen(0.f, 0.f), toScreen(canvasW, canvasH), IM_COL32(110, 150, 255, 110));
   dl->AddRect(toScreen(0.f, 0.f), toScreen(m_boardWidth, m_boardHeight), IM_COL32(255, 180, 60, 200));

   // table geometry, same stream as the DXF export
   {
      ImGuiSur sur(dl, table, viewCenter, m_center, m_zoom);
      for (const auto &pedit : table->GetParts())
         if (pedit->m_uiVisible && pedit->GetISelect() && !pedit->m_desktopBackdrop)
            pedit->GetISelect()->RenderBlueprint(&sur, false);
   }

   // cursor crosshair + readout
   if (hovered)
   {
      dl->AddLine(toScreen(mouseWorld.x, wyMin), toScreen(mouseWorld.x, wyMax), IM_COL32(255, 255, 255, 30));
      dl->AddLine(toScreen(wxMin, mouseWorld.y), toScreen(wxMax, mouseWorld.y), IM_COL32(255, 255, 255, 30));
      char buf[96];
      switch (lengthUnit)
      {
      case PropertyPane::Unit::Millimeters: snprintf(buf, sizeof(buf), "x %.2f  y %.2f mm", mouseWorld.x * 25.4f, mouseWorld.y * 25.4f); break;
      case PropertyPane::Unit::VPLength: snprintf(buf, sizeof(buf), "x %.1f  y %.1f vpu", mouseWorld.x / S + table->m_left, table->m_bottom - mouseWorld.y / S); break;
      default: snprintf(buf, sizeof(buf), "x %.3f  y %.3f in", mouseWorld.x, mouseWorld.y); break;
      }
      dl->AddRectFilled(ImVec2(canvasPos.x + 4.f, canvasPos.y + avail.y - 22.f * dpi), ImVec2(canvasPos.x + 190.f * dpi, canvasPos.y + avail.y - 4.f), IM_COL32(0, 0, 0, 160));
      dl->AddText(ImVec2(canvasPos.x + 8.f, canvasPos.y + avail.y - 20.f * dpi), IM_COL32(240, 240, 240, 255), buf);
   }

   dl->PopClipRect();
   ImGui::End();
}

}
