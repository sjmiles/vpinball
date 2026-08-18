// license:GPLv3+

#include "core/stdafx.h"
#include "View2D.h"

#include "parts/pintable.h"
#include "parts/dragpoint.h"
#include "parts/flasher.h"
#include "parts/light.h"
#include "parts/ramp.h"
#include "parts/rubber.h"
#include "parts/surface.h"
#include "parts/trigger.h"
#include "ui/win/sur.h"

#include "imgui/imgui.h"

namespace VPX::EditorUI
{

namespace
{

// the parts whose outline is a drag point chain; IEditable and IHaveDragPoints are
// unrelated bases, so the cross-cast goes through the concrete part type
IHaveDragPoints *GetDragPoints(IEditable *edit)
{
   if (edit == nullptr)
      return nullptr;
   switch (edit->GetItemType())
   {
   case eItemSurface: return static_cast<Surface *>(edit);
   case eItemRamp: return static_cast<Ramp *>(edit);
   case eItemRubber: return static_cast<Rubber *>(edit);
   case eItemLight: return static_cast<Light *>(edit);
   case eItemTrigger: return static_cast<Trigger *>(edit);
   case eItemFlasher: return static_cast<Flasher *>(edit);
   default: return nullptr;
   }
}

// Draws the blueprint geometry stream (see Sur) into an ImGui draw list, and
// doubles as the picker: every stroke it draws is also distance-tested against
// the mouse, so picking always agrees with the picture.
// Input coordinates are VPX units (y down); output is screen pixels through a
// board-anchored inch world space (y up), so the on-screen picture matches the
// DXF export and the physical board.
class ImGuiSur final : public Sur
{
public:
   ImGuiSur(ImDrawList *drawList, const PinTable *table, const ImVec2 &viewCenter, const Vertex2D &worldCenter, float zoom, const ImVec2 &mouse, const IEditable *selected)
      : Sur(nullptr, 1.f, 0.f, 0.f, 0, 0)
      , m_drawList(drawList)
      , m_viewCenter(viewCenter)
      , m_worldCenter(worldCenter)
      , m_pxPerInch(zoom)
      , m_mouse(mouse)
      , m_selected(selected)
   {
      m_tableLeft = table->m_left;
      m_tableBottom = table->m_bottom;
   }

   static constexpr float PICK_RADIUS_PX = 8.f;

   IEditable *GetHit() const { return m_hit; }

   ImVec2 P(const float x, const float y) const // VPX units -> screen
   {
      const float wx = (x - m_tableLeft) * (float)VPUTOINCHES(1.);
      const float wy = (m_tableBottom - y) * (float)VPUTOINCHES(1.);
      return { m_viewCenter.x + (wx - m_worldCenter.x) * m_pxPerInch, m_viewCenter.y - (wy - m_worldCenter.y) * m_pxPerInch };
   }

   void Line(const float x, const float y, const float x2, const float y2) override
   {
      const ImVec2 a = P(x, y), b = P(x2, y2);
      m_drawList->AddLine(a, b, StrokeColor(), StrokeWidth());
      Consider(DistSqToSegment(m_mouse, a, b));
   }

   void Rectangle(const float x, const float y, const float x2, float y2) override
   {
      const ImVec2 pts[4] = { P(x, y), P(x2, y), P(x2, y2), P(x, y2) };
      m_drawList->AddPolyline(pts, 4, StrokeColor(), ImDrawFlags_Closed, StrokeWidth());
      for (int i = 0; i < 4; ++i)
         Consider(DistSqToSegment(m_mouse, pts[i], pts[(i + 1) & 3]));
   }

   void Rectangle2(const int x, const int y, const int x2, const int y2) override { } // screen-space UI decoration

   void Ellipse(const float centerx, const float centery, const float radius) override
   {
      const ImVec2 c = P(centerx, centery);
      const float r = radius * (float)VPUTOINCHES(1.) * m_pxPerInch;
      m_drawList->AddCircle(c, r, StrokeColor(), 0, StrokeWidth());
      const float d = fabsf(sqrtf((m_mouse.x - c.x) * (m_mouse.x - c.x) + (m_mouse.y - c.y) * (m_mouse.y - c.y)) - r);
      Consider(d * d);
   }

   void Ellipse2(const float centerx, const float centery, const int radius) override
   {
      m_drawList->AddCircle(P(centerx, centery), (float)radius, StrokeColor(), 0, StrokeWidth()); // pixel radius (drag point style handles)
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
         Line(rgv[i].x, rgv[i].y, rgv[i + 1].x, rgv[i + 1].y);
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
      m_drawList->PathStroke(StrokeColor(), 0, StrokeWidth());

      float am = atan2f(m_mouse.y - c.y, m_mouse.x - c.x);
      while (am < a2)
         am += 2.f * (float)M_PI;
      if (am <= a1) // mouse angle inside the swept range: distance to the ring
      {
         const float d = fabsf(sqrtf((m_mouse.x - c.x) * (m_mouse.x - c.x) + (m_mouse.y - c.y) * (m_mouse.y - c.y)) - r);
         Consider(d * d);
      }
      else // outside: distance to the arc end points
      {
         Consider((m_mouse.x - p1.x) * (m_mouse.x - p1.x) + (m_mouse.y - p1.y) * (m_mouse.y - p1.y));
         Consider((m_mouse.x - p2.x) * (m_mouse.x - p2.x) + (m_mouse.y - p2.y) * (m_mouse.y - p2.y));
      }
   }

   void Image(const float x, const float y, const float x2, const float y2, HDC hdcSrc, const int width, const int height) override { } // raster content

   void SetObject(ISelect *const psel) override
   {
      // parts announce themselves then often call SetObject(nullptr) before drawing,
      // so keep the last non-null object current (same convention as DxfSur)
      if (psel != nullptr)
         m_curEditable = psel->GetIEditable();
   }

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
      m_drawList->AddPolyline(m_points.data(), count, StrokeColor(), closed ? ImDrawFlags_Closed : ImDrawFlags_None, StrokeWidth());
      for (int i = 0; i + 1 < count; ++i)
         Consider(DistSqToSegment(m_mouse, m_points[i], m_points[i + 1]));
      if (closed)
         Consider(DistSqToSegment(m_mouse, m_points[count - 1], m_points[0]));
   }

   static float DistSqToSegment(const ImVec2 &p, const ImVec2 &a, const ImVec2 &b)
   {
      const float abx = b.x - a.x, aby = b.y - a.y;
      const float lenSq = abx * abx + aby * aby;
      float t = lenSq > 0.f ? ((p.x - a.x) * abx + (p.y - a.y) * aby) / lenSq : 0.f;
      t = clamp(t, 0.f, 1.f);
      const float dx = p.x - (a.x + t * abx), dy = p.y - (a.y + t * aby);
      return dx * dx + dy * dy;
   }

   void Consider(const float distSq)
   {
      if (m_curEditable != nullptr && distSq < m_bestDistSq)
      {
         m_bestDistSq = distSq;
         m_hit = m_curEditable;
      }
   }

   bool IsSelected() const { return m_selected != nullptr && m_curEditable == m_selected; }
   ImU32 StrokeColor() const
   {
      if (IsSelected())
         return m_dashed ? IM_COL32(255, 180, 60, 130) : IM_COL32(255, 180, 60, 255);
      return m_dashed ? IM_COL32(210, 215, 220, 80) : IM_COL32(210, 215, 220, 220);
   }
   float StrokeWidth() const { return IsSelected() ? 2.f : 1.f; }

   ImDrawList *const m_drawList;
   const ImVec2 m_viewCenter;
   const Vertex2D m_worldCenter;
   const float m_pxPerInch;
   const ImVec2 m_mouse;
   const IEditable *const m_selected;
   float m_tableLeft, m_tableBottom;
   bool m_dashed = false;
   IEditable *m_curEditable = nullptr;
   IEditable *m_hit = nullptr;
   float m_bestDistSq = PICK_RADIUS_PX * PICK_RADIUS_PX;
   vector<ImVec2> m_points;
};

} // anonymous namespace

void View2D::Render(PinTable *table, float dpi, PropertyPane::Unit lengthUnit, IEditable *selected, const std::function<void(IEditable *)> &onSelect,
   const std::function<void(IEditable *, unsigned int)> &pushUndo)
{
   if (table == nullptr)
      return;

   const float S = (float)VPUTOINCHES(1.);
   const float canvasW = (table->m_right - table->m_left) * S;
   const float canvasH = (table->m_bottom - table->m_top) * S;

   ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
   if (m_docked)
   {
      ImGui::SetNextWindowPos(m_dockPos);
      ImGui::SetNextWindowSize(m_dockSize);
      windowFlags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus;
   }
   else
      ImGui::SetNextWindowSize(ImVec2(420.f * dpi, 700.f * dpi), ImGuiCond_FirstUseEver);
   if (!ImGui::Begin("2D CAD View", m_docked ? nullptr : &m_show, windowFlags))
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
   ImGui::SameLine();
   ImGui::Checkbox("Snap", &m_snap);
   ImGui::SameLine();
   ImGui::SetNextItemWidth(60.f * dpi);
   ImGui::InputFloat("##snapstep", &m_snapStep, 0.f, 0.f, "%.4f");
   if (ImGui::IsItemHovered())
      ImGui::SetTooltip("Snap step in inches (0.0625 = 1/16\")");

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

   // input: pan with left/middle drag, zoom on wheel centered at the cursor,
   // plain left-click (no drag) selects, left-drag on a handle moves that drag point
   ImGui::InvisibleButton("view2d_canvas", avail, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle);
   const bool hovered = ImGui::IsItemHovered();
   ImGuiIO &io = ImGui::GetIO();

   const auto toScreen = [&](float wx, float wy) -> ImVec2 { return { viewCenter.x + (wx - m_center.x) * m_zoom, viewCenter.y - (wy - m_center.y) * m_zoom }; };
   const auto pointToWorld = [&](const DragPoint *dp) -> Vertex2D { return { (dp->m_v.x - table->m_left) * S, (table->m_bottom - dp->m_v.y) * S }; };

   // drag point handles of the selected part
   IHaveDragPoints *const dragPts = GetDragPoints(selected);
   int hoverHandle = -1;
   if (dragPts != nullptr && hovered)
   {
      const float handleRadius = 7.f * dpi;
      float best = handleRadius * handleRadius;
      for (int i = 0; i < (int)dragPts->m_vdpoint.size(); ++i)
      {
         const DragPoint *const dp = dragPts->m_vdpoint[i];
         if (dp->m_uiLocked)
            continue;
         const Vertex2D w = pointToWorld(dp);
         const ImVec2 hp = toScreen(w.x, w.y);
         const float distSq = (io.MousePos.x - hp.x) * (io.MousePos.x - hp.x) + (io.MousePos.y - hp.y) * (io.MousePos.y - hp.y);
         if (distSq < best)
         {
            best = distSq;
            hoverHandle = i;
         }
      }
   }
   if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && hoverHandle >= 0)
      m_dragPointIndex = hoverHandle;

   bool draggedPointThisClick = false;
   if (m_dragPointIndex >= 0)
   {
      if (dragPts == nullptr || m_dragPointIndex >= (int)dragPts->m_vdpoint.size())
         m_dragPointIndex = -1;
      else if (!ImGui::IsMouseDown(ImGuiMouseButton_Left) || !ImGui::IsItemActive())
      {
         // also drop the drag if the canvas lost active state (focus loss, lost release event),
         // otherwise the point stays glued to the cursor
         m_dragPointIndex = -1;
         draggedPointThisClick = true; // release frame: don't treat it as a select click
      }
      else if (pushUndo)
      {
         pushUndo(selected, 0x2000u + (unsigned int)m_dragPointIndex); // deduped by the host for the duration of the drag
         float wx = m_center.x + (io.MousePos.x - viewCenter.x) / m_zoom;
         float wy = m_center.y - (io.MousePos.y - viewCenter.y) / m_zoom;
         if (m_snap && m_snapStep > 1e-4f)
         {
            wx = roundf(wx / m_snapStep) * m_snapStep;
            wy = roundf(wy / m_snapStep) * m_snapStep;
         }
         DragPoint *const dp = dragPts->m_vdpoint[m_dragPointIndex];
         dp->m_v.x = wx / S + table->m_left;
         dp->m_v.y = table->m_bottom - wy / S;
      }
   }

   if (m_dragPointIndex < 0 && ImGui::IsItemActive() && (ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.f) || ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.f)))
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

   // table geometry, same stream as the DXF export; the drawing pass doubles as the picker
   IEditable *hit = nullptr;
   {
      ImGuiSur sur(dl, table, viewCenter, m_center, m_zoom, io.MousePos, selected);
      for (const auto &pedit : table->GetParts())
         if (pedit->m_uiVisible && pedit->GetISelect() && !pedit->m_desktopBackdrop)
            pedit->GetISelect()->RenderBlueprint(&sur, false);
      hit = sur.GetHit();
   }

   // drag point handles on top of the geometry: circles for smooth points, squares for corners
   if (dragPts != nullptr)
   {
      const float hs = 4.f * dpi;
      for (int i = 0; i < (int)dragPts->m_vdpoint.size(); ++i)
      {
         const DragPoint *const dp = dragPts->m_vdpoint[i];
         const Vertex2D w = pointToWorld(dp);
         const ImVec2 hp = toScreen(w.x, w.y);
         const bool active = (i == m_dragPointIndex);
         const ImU32 col = active ? IM_COL32(255, 180, 60, 255) : (i == hoverHandle) ? IM_COL32(255, 255, 255, 255) : dp->m_uiLocked ? IM_COL32(140, 140, 140, 130) : IM_COL32(120, 200, 255, 230);
         if (dp->m_smooth)
         {
            if (active)
               dl->AddCircleFilled(hp, hs, col);
            else
               dl->AddCircle(hp, hs, col, 0, 1.5f);
         }
         else
         {
            const ImVec2 a { hp.x - hs, hp.y - hs }, b { hp.x + hs, hp.y + hs };
            if (active)
               dl->AddRectFilled(a, b, col);
            else
               dl->AddRect(a, b, col, 0.f, 0, 1.5f);
         }
      }
   }

   if (hovered && hit != nullptr && hit != selected && hoverHandle < 0)
      ImGui::SetTooltip("%s", hit->GetName().c_str());
   if (hovered && ImGui::IsMouseReleased(ImGuiMouseButton_Left) && io.MouseDragMaxDistanceSqr[ImGuiMouseButton_Left] < 9.f && !draggedPointThisClick && hoverHandle < 0 && onSelect)
      onSelect(hit); // nullptr clears the selection

   // double-click on a handle toggles the smooth flag
   if (hovered && hoverHandle >= 0 && dragPts != nullptr && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
   {
      if (pushUndo)
         pushUndo(selected, 0x3000u + (unsigned int)hoverHandle);
      DragPoint *const dp = dragPts->m_vdpoint[hoverHandle];
      dp->m_smooth = !dp->m_smooth;
      m_dragPointIndex = -1; // the double-click's second press also started a drag; cancel it
   }

   // right-click: context menu on a handle, or Add Point on the selected outline
   if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
   {
      if (dragPts != nullptr && hoverHandle >= 0)
      {
         m_ctxPointIndex = hoverHandle;
         ImGui::OpenPopup("view2d_point_ctx");
      }
      else if (dragPts != nullptr && hit == selected)
      {
         m_ctxWorld = mouseWorld;
         ImGui::OpenPopup("view2d_outline_ctx");
      }
   }
   if (ImGui::BeginPopup("view2d_point_ctx"))
   {
      if (dragPts == nullptr || m_ctxPointIndex < 0 || m_ctxPointIndex >= (int)dragPts->m_vdpoint.size())
         ImGui::CloseCurrentPopup();
      else
      {
         DragPoint *const dp = dragPts->m_vdpoint[m_ctxPointIndex];
         bool smooth = dp->m_smooth;
         if (ImGui::Checkbox("Smooth", &smooth))
         {
            if (pushUndo)
               pushUndo(selected, 0x3000u + (unsigned int)m_ctxPointIndex);
            dp->m_smooth = smooth;
         }
         // numeric entry: inches/mm are board-anchored y-up (like the cursor readout),
         // VP Units show the raw stored coordinates (y down)
         const bool nativeUnits = (lengthUnit == PropertyPane::Unit::VPLength);
         const Vertex2D w = pointToWorld(dp);
         const float unitScale = (lengthUnit == PropertyPane::Unit::Millimeters) ? 25.4f : 1.f;
         const char *const fmt = nativeUnits ? "%.1f vpu" : (lengthUnit == PropertyPane::Unit::Millimeters) ? "%.2f mm" : "%.3f\"";
         float ex = nativeUnits ? dp->m_v.x : w.x * unitScale;
         float ey = nativeUnits ? dp->m_v.y : w.y * unitScale;
         bool changed = false;
         ImGui::SetNextItemWidth(90.f * dpi);
         changed |= ImGui::InputFloat("X", &ex, 0.f, 0.f, fmt, ImGuiInputTextFlags_EnterReturnsTrue);
         ImGui::SetNextItemWidth(90.f * dpi);
         changed |= ImGui::InputFloat("Y", &ey, 0.f, 0.f, fmt, ImGuiInputTextFlags_EnterReturnsTrue);
         if (changed)
         {
            if (pushUndo)
               pushUndo(selected, 0x4000u + (unsigned int)m_ctxPointIndex);
            dp->m_v.x = nativeUnits ? ex : (ex / unitScale) / S + table->m_left;
            dp->m_v.y = nativeUnits ? ey : table->m_bottom - (ey / unitScale) / S;
         }
         ImGui::Separator();
         ImGui::BeginDisabled((int)dragPts->m_vdpoint.size() <= dragPts->GetMinimumPoints());
         if (ImGui::MenuItem("Delete Point"))
         {
            dp->Delete(); // guards the minimum point count and registers its own undo
            m_dragPointIndex = -1;
            m_ctxPointIndex = -1;
            ImGui::CloseCurrentPopup();
         }
         ImGui::EndDisabled();
      }
      ImGui::EndPopup();
   }
   if (ImGui::BeginPopup("view2d_outline_ctx"))
   {
      if (dragPts == nullptr)
         ImGui::CloseCurrentPopup();
      else if (ImGui::MenuItem("Add Point"))
      {
         // AddPointAt registers its own undo; new points are corners, matching the Win32 editor default
         selected->GetISelect()->AddPointAt(Vertex2D { m_ctxWorld.x / S + table->m_left, table->m_bottom - m_ctxWorld.y / S }, false);
         m_dragPointIndex = -1;
         ImGui::CloseCurrentPopup();
      }
      ImGui::EndPopup();
   }

   // cursor crosshair + readout
   if (hovered)
   {
      dl->AddLine(toScreen(mouseWorld.x, wyMin), toScreen(mouseWorld.x, wyMax), IM_COL32(255, 255, 255, 30));
      dl->AddLine(toScreen(wxMin, mouseWorld.y), toScreen(wxMax, mouseWorld.y), IM_COL32(255, 255, 255, 30));
      // while dragging a point, read out the (snapped) point position instead of the cursor
      Vertex2D readout = mouseWorld;
      const char *label = "";
      if (m_dragPointIndex >= 0 && dragPts != nullptr && m_dragPointIndex < (int)dragPts->m_vdpoint.size())
      {
         readout = pointToWorld(dragPts->m_vdpoint[m_dragPointIndex]);
         label = "  [pt]";
      }
      char buf[96];
      switch (lengthUnit)
      {
      case PropertyPane::Unit::Millimeters: snprintf(buf, sizeof(buf), "x %.2f  y %.2f mm%s", readout.x * 25.4f, readout.y * 25.4f, label); break;
      case PropertyPane::Unit::VPLength: snprintf(buf, sizeof(buf), "x %.1f  y %.1f vpu%s", readout.x / S + table->m_left, table->m_bottom - readout.y / S, label); break;
      default: snprintf(buf, sizeof(buf), "x %.3f  y %.3f in%s", readout.x, readout.y, label); break;
      }
      dl->AddRectFilled(ImVec2(canvasPos.x + 4.f, canvasPos.y + avail.y - 22.f * dpi), ImVec2(canvasPos.x + 190.f * dpi, canvasPos.y + avail.y - 4.f), IM_COL32(0, 0, 0, 160));
      dl->AddText(ImVec2(canvasPos.x + 8.f, canvasPos.y + avail.y - 20.f * dpi), IM_COL32(240, 240, 240, 255), buf);
   }

   dl->PopClipRect();
   ImGui::End();
}

}
