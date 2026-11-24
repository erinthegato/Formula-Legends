// CPU clipping for geometry close to the camera.
//
// The PSP's graphics chip clips triangles against the near plane, but drops any triangle whose
// corners land far outside its 4096 x 4096 drawing area -- which happens to big ground pieces and
// cockpit parts right next to the camera (they would flicker or vanish). Triangles of objects close
// to the camera are cut to a slightly enlarged view volume here first, in the object's own space,
// so the same matrices still draw them.
#pragma once
#include "platform.h"

namespace clip {
void setMatrices(const M4& proj, const M4& view);   // call when the camera changes
// draw world vertices (triangle list) clipped
void drawWorld(const WorldVertex* v, int count, const M4& model, bool strip = false);
// draw a 16-bit model clipped (converted to float vertices)
void drawModel(const ModelVertex* v, int count, const M4& model, bool strip = false);
}
