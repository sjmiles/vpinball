// license:GPLv3+

#pragma once

#include "PropertyPane.h"

class PinTable;

namespace VPX::EditorUI
{

// 2D top-down "CAD view" of the table: true-scale rendering of the same blueprint
// geometry stream that feeds the DXF export (see ui/win/dxfsur.h), with pan/zoom,
// a unit grid, cursor readout, and a physical-board overlay. View only for now.
//
// World coordinates are board-anchored inches, y-up, origin at the canvas
// bottom-left corner - matching the DXF export and CAM conventions, not the
// VPX-internal y-down unit space.
class View2D final
{
public:
   void Render(PinTable *table, float dpi, PropertyPane::Unit lengthUnit);

   bool m_show = false;

private:
   float m_zoom = 0.f; // pixels per world inch; <= 0 requests fit-to-window
   Vertex2D m_center { 0.f, 0.f }; // world point kept at the viewport center

   // physical board overlay; the canvas is usually taller than the real board
   // (e.g. 45.94" canvas over a 45" blank), so the board rect is what matters
   // for fabrication
   float m_boardWidth = 20.25f;
   float m_boardHeight = 45.0f;
};

}
