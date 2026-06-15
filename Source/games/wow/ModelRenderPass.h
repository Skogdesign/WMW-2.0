/*
 * ModelRenderPass.h
 *
 *  Created on: 21 oct. 2013
 *
 */

#ifndef _MODELRENDERPASS_H_
#define _MODELRENDERPASS_H_

#include "types.h"

#include "glm/glm.hpp"

class WoWModel;

#ifdef _WIN32
#    ifdef BUILDING_WOW_DLL
#        define _MODELRENDERPASS_API_ __declspec(dllexport)
#    else
#        define _MODELRENDERPASS_API_ __declspec(dllimport)
#    endif
#else
#    define _MODELRENDERPASS_API_
#endif


class _MODELRENDERPASS_API_ ModelRenderPass
{
public:

  ModelRenderPass(WoWModel *, int geo);

  bool useTex2, useEnvMap, cull, trans, unlit, noZWrite, billboard;

  int16 texanim, color, opacity, blendmode, specialTex;
  uint16 tex;

  // Multi-texture combiner support (War Within+ cosmic/void materials use up to 4
  // textures combined by a Blizzard "M2 combiner shader"). For op_count==1 these stay
  // at their defaults and the legacy fixed-function path is used unchanged. For
  // op_count>1 we resolve the combiner + per-unit UV routing and render through a small
  // GLSL fragment shader implementing the M2 pixel-combiner so the extra textures are combined
  // instead of dropped (which used to leave capes/orbs white).
  int textureCount;            // op_count: number of textures in this unit (1..4)
  int pixelShader;             // combiner id (0..36), -1 if unresolved / single texture
  int vertexShader;            // vertex-combiner id (drives per-unit UV routing), -1 if none
  uint16 tex2, tex3, tex4;     // extra texture indices (model texture-array indices)
  int16 texanim2;              // UV-animation index for texture unit 1 (-1 if none).
                               // unit 0's animation stays in 'texanim' (legacy path).
  // Per texture-unit UV source: 0 = uv set 0 (T1), 1 = uv set 1 (T2),
  // 2 = environment / sphere map (Env), 3 = uv set 2 (T3).
  int8 uvSource[4];

  // runtime flag: set by init() when the GLSL combiner path is active for this pass,
  // read by render()/deinit() so they emit multi-texture coords and tear down state.
  bool combinerActive;

  // texture wrapping
  bool swrap, twrap;

  // colours
  glm::vec4 ocol, ecol;

  WoWModel * model;

  int geoIndex;

  bool init();
  int BlendValueForMode(int mode);

  void render(bool animated);

  void deinit();

  static const uint16 INVALID_TEX = 50000;
  
/*
  bool operator< (const ModelRenderPass &m) const
  {
    // Probably not 100% right, but seems to work better than just geoset sorting.
    // Blend mode mostly takes into account transparency and material - Wain
    if (trans == m.trans)
    {
      if (blendmode == m.blendmode)
        return (geoIndex < m.geoIndex);
      return blendmode < m.blendmode;
    }
    return (trans < m.trans);
  }
*/

};


#endif /* _MODELRENDERPASS_H_ */
