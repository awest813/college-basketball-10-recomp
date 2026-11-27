// ncaabb10 - the graphics cards the renderer can use, and which one to prefer.
#pragma once

#include <string>
#include <vector>

namespace ncaabb10 {

struct GraphicsCard {
  std::string name;  // As the driver reports it, "Intel(R) Arc(TM) A750 Graphics".
  int index = -1;    // The renderer's own number for it (gpu.d3d12.adapter, gpu.vulkan.device).
};

struct GraphicsCards {
  // Hardware cards, in the renderer's order. No software renderers (WARP, llvmpipe).
  std::vector<GraphicsCard> cards;
  // Position in cards of the one to use when the player has not chosen: the high-performance card
  // (on a laptop, the dedicated one rather than the one built into the processor). -1: none found,
  // the renderer chooses itself.
  int preferred = -1;
  const char* api = "";      // "Direct3D 12" or "Vulkan".
  const char* setting = "";  // The renderer's setting for the card's index.
};

// Lists the cards of the renderer this platform uses by default: Direct3D 12 on Windows, Vulkan
// elsewhere. Loads the preferred card's driver, which the renderer loads next anyway; the result
// does not change while the game runs, so later calls return the first one's.
const GraphicsCards& ListGraphicsCards();

// Whether the renderer can run on the card (Direct3D 12 at feature level 11_0). Loads the card's
// driver, hundreds of milliseconds: ask only of the card about to be used.
bool CanRender(const GraphicsCard& card);

}  // namespace ncaabb10
