// license:GPLv3+

#pragma once

#include <functional>

#include "PropertyPane.h"

class PinTable;
class IEditable;

namespace VPX::EditorUI
{

// 2D top-down "CAD view" of the table: true-scale rendering of the same blueprint
// geometry stream that feeds the DXF export (see ui/win/dxfsur.h), with pan/zoom,
// a unit grid, cursor readout, and a physical-board overlay.
//
// World coordinates are board-anchored inches, y-up, origin at the canvas
// bottom-left corner - matching the DXF export and CAM conventions, not the
// VPX-internal y-down unit space.
//
// Left-click picks the part whose drawn outline is nearest the cursor (within a
// few pixels) and reports it through onSelect, so the host can route it to the
// property panes; left/middle-drag pans; wheel zooms at the cursor.
//
// When the selected part has drag points (walls, ramps, rubbers, lights,
// triggers, flashers), they are drawn as handles - circles for smooth points,
// squares for corners - and can be dragged, snapping to the inch grid. Each
// drag is registered with the host's undo through pushUndo.
class View2D final
{
public:
   void Render(PinTable *table, float dpi, PropertyPane::Unit lengthUnit, IEditable *selected, const std::function<void(IEditable *)> &onSelect,
      const std::function<void(IEditable *, unsigned int)> &pushUndo);

   bool m_show = false;

private:
   float m_zoom = 0.f; // pixels per world inch; <= 0 requests fit-to-window
   Vertex2D m_center { 0.f, 0.f }; // world point kept at the viewport center

   // drag point editing
   int m_dragPointIndex = -1; // index into the selected part's drag points while dragging
   bool m_snap = true;
   float m_snapStep = 0.0625f; // inches (1/16")

   // physical board overlay; the canvas is usually taller than the real board
   // (e.g. 45.94" canvas over a 45" blank), so the board rect is what matters
   // for fabrication
   float m_boardWidth = 20.25f;
   float m_boardHeight = 45.0f;
};

}
