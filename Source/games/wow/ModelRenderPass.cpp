/*
 * ModelRenderPass.cpp
 *
 *  Created on: 21 oct. 2013
 *
 */

#include "ModelRenderPass.h"

#include "ModelColor.h"
#include "ModelTransparency.h"
#include "TextureAnim.h"
#include "video.h"
#include "wow_enums.h"
#include "WoWModel.h"

#include "logger/Logger.h"

#include "GL/glew.h"
#include "glm/gtc/type_ptr.hpp"

// ---------------------------------------------------------------------------
// M2 multi-texture combiner (GLSL).
//
// War Within+ "cosmic/void" materials combine up to four textures per material via
// a Blizzard combiner shader (selected by setLOD into ModelRenderPass::pixelShader).
// WMV's fixed-function path can only show one texture per material, so those capes
// and orb halos rendered solid white. Here we provide a small *fragment-only* GLSL
// program implementing the M2 multi-texture pixel-combiner that reproduces the
// combine. Being fragment-only keeps WMV's entire fixed-function vertex pipeline
// intact: transform, environment/sphere-map texgen, texture-matrix UV animation and
// per-vertex lighting all still run and feed gl_TexCoord[0..3] / gl_Color into the
// combiner. The program is only bound for op_count>1 passes, so ordinary single-
// texture rendering is completely unchanged.
//
// GLSL 1.20 is used (guaranteed on any GL2.1 context) - it has no switch statement,
// so the combiner is expressed as an if/else chain.
// ---------------------------------------------------------------------------
namespace
{
  GLuint g_combinerProg = 0;
  bool   g_combinerTried = false;
  GLint  g_uTex0 = -1, g_uTex1 = -1, g_uTex2 = -1, g_uTex3 = -1;
  GLint  g_uPixelShader = -1, g_uBlendMode = -1, g_uAlphaTest = -1, g_uTexSampleAlpha = -1;

  // raw M2 blend mode (0..7) -> the "EGX" blend mode used by the combiner's
  // final-opacity logic (matches M2BLEND_TO_EGX in M2RendererGL.js).
  int egxBlendMode(int raw)
  {
    static const int t[8] = { 0, 1, 2, 10, 3, 4, 5, 13 };
    return (raw >= 0 && raw < 8) ? t[raw] : 0;
  }

  static const char * COMBINER_FRAGMENT_SRC =
    "#version 120\n"
    "uniform sampler2D u_tex0;\n"
    "uniform sampler2D u_tex1;\n"
    "uniform sampler2D u_tex2;\n"
    "uniform sampler2D u_tex3;\n"
    "uniform int   u_pixel_shader;\n"
    "uniform int   u_blend_mode;\n"      // EGX blend mode
    "uniform float u_alpha_test;\n"
    "uniform vec3  u_tex_sample_alpha;\n"
    "void main() {\n"
    "  vec2 uv1 = gl_TexCoord[0].xy;\n"
    "  vec2 uv2 = gl_TexCoord[1].xy;\n"
    "  vec2 uv3 = gl_TexCoord[2].xy;\n"
    "  if (u_pixel_shader == 26 || u_pixel_shader == 27 || u_pixel_shader == 28) { uv2 = uv1; uv3 = uv1; }\n"
    "  vec4 tex1 = texture2D(u_tex0, uv1);\n"
    "  vec4 tex2 = texture2D(u_tex1, uv2);\n"
    "  vec4 tex3 = texture2D(u_tex2, uv3);\n"
    "  vec4 tex4 = texture2D(u_tex3, gl_TexCoord[1].xy);\n"
    "  vec3 mesh_color = gl_Color.rgb;\n"          // already lit by the fixed pipeline
    "  float mesh_opacity = gl_Color.a;\n"
    "  vec3 mat_diffuse = vec3(0.0);\n"
    "  vec3 specular = vec3(0.0);\n"
    "  float discard_alpha = 1.0;\n"
    "  bool can_discard = false;\n"
    "  int ps = u_pixel_shader;\n"
    "  vec3 g0 = vec3(1.0);\n"
    "  if (ps == 0) { mat_diffuse = mesh_color * tex1.rgb; }\n"
    "  else if (ps == 1) { mat_diffuse = mesh_color * tex1.rgb; discard_alpha = tex1.a; can_discard = true; }\n"
    "  else if (ps == 2) { mat_diffuse = mesh_color * tex1.rgb * tex2.rgb; discard_alpha = tex2.a; can_discard = true; }\n"
    "  else if (ps == 3) { mat_diffuse = mesh_color * tex1.rgb * tex2.rgb * 2.0; discard_alpha = tex2.a * 2.0; can_discard = true; }\n"
    "  else if (ps == 4) { mat_diffuse = mesh_color * tex1.rgb * tex2.rgb * 2.0; }\n"
    "  else if (ps == 5) { mat_diffuse = mesh_color * tex1.rgb * tex2.rgb; }\n"
    "  else if (ps == 6) { mat_diffuse = mesh_color * tex1.rgb * tex2.rgb; discard_alpha = tex1.a * tex2.a; can_discard = true; }\n"
    "  else if (ps == 7) { mat_diffuse = mesh_color * tex1.rgb * tex2.rgb * 2.0; discard_alpha = tex1.a * tex2.a * 2.0; can_discard = true; }\n"
    "  else if (ps == 8) { mat_diffuse = mesh_color * tex1.rgb; discard_alpha = tex1.a + tex2.a; can_discard = true; specular = tex2.rgb; }\n"
    "  else if (ps == 9) { mat_diffuse = mesh_color * tex1.rgb * tex2.rgb * 2.0; discard_alpha = tex1.a; can_discard = true; }\n"
    "  else if (ps == 10) { mat_diffuse = mesh_color * tex1.rgb; discard_alpha = tex1.a; can_discard = true; specular = tex2.rgb; }\n"
    "  else if (ps == 11) { mat_diffuse = mesh_color * tex1.rgb * tex2.rgb; discard_alpha = tex1.a; can_discard = true; }\n"
    "  else if (ps == 12) { mat_diffuse = mesh_color * mix(tex1.rgb * tex2.rgb * 2.0, tex1.rgb, vec3(tex1.a)); }\n"
    "  else if (ps == 13) { mat_diffuse = mesh_color * tex1.rgb; specular = tex2.rgb * tex2.a; }\n"
    "  else if (ps == 14) { mat_diffuse = mesh_color * tex1.rgb; specular = tex2.rgb * tex2.a * (1.0 - tex1.a); }\n"
    "  else if (ps == 15) { mat_diffuse = mesh_color * mix(tex1.rgb * tex2.rgb * 2.0, tex1.rgb, vec3(tex1.a)); specular = tex3.rgb * tex3.a * u_tex_sample_alpha.b; }\n"
    "  else if (ps == 16) { mat_diffuse = mesh_color * tex1.rgb; discard_alpha = tex1.a; can_discard = true; specular = tex2.rgb * tex2.a; }\n"
    "  else if (ps == 17) { mat_diffuse = mesh_color * tex1.rgb; discard_alpha = tex1.a + tex2.a * (0.3 * tex2.r + 0.59 * tex2.g + 0.11 * tex2.b); can_discard = true; specular = tex2.rgb * tex2.a * (1.0 - tex1.a); }\n"
    "  else if (ps == 18) { mat_diffuse = mesh_color * mix(mix(tex1.rgb, tex2.rgb, vec3(tex2.a)), tex1.rgb, vec3(tex1.a)); }\n"
    "  else if (ps == 19) { mat_diffuse = mesh_color * mix(tex1.rgb * tex2.rgb * 2.0, tex3.rgb, vec3(tex3.a)); }\n"
    "  else if (ps == 20) { mat_diffuse = mesh_color * tex1.rgb; specular = tex2.rgb * tex2.a * u_tex_sample_alpha.g; }\n"
    "  else if (ps == 21) { mat_diffuse = mesh_color * tex1.rgb; discard_alpha = tex1.a + tex2.a; can_discard = true; specular = tex2.rgb * (1.0 - tex1.a); }\n"
    "  else if (ps == 22) { mat_diffuse = mesh_color * mix(tex1.rgb * tex2.rgb, tex1.rgb, vec3(tex1.a)); }\n"
    "  else if (ps == 23) { mat_diffuse = mesh_color * tex1.rgb; discard_alpha = tex1.a; can_discard = true; specular = tex2.rgb * tex2.a * u_tex_sample_alpha.g; }\n"
    "  else if (ps == 24) { mat_diffuse = mesh_color * mix(tex1.rgb, tex2.rgb, vec3(tex2.a)); specular = tex1.rgb * tex1.a * u_tex_sample_alpha.r; }\n"
    "  else if (ps == 25) { float go = clamp(tex3.a * u_tex_sample_alpha.b, 0.0, 1.0); mat_diffuse = mesh_color * mix(tex1.rgb * tex2.rgb * 2.0, tex1.rgb, vec3(tex1.a)) * (1.0 - go); specular = tex3.rgb * go; }\n"
    "  else if (ps == 26) { vec4 mx = mix(mix(tex1, tex2, vec4(clamp(u_tex_sample_alpha.g, 0.0, 1.0))), tex3, vec4(clamp(u_tex_sample_alpha.b, 0.0, 1.0))); mat_diffuse = mesh_color * mx.rgb; discard_alpha = mx.a; can_discard = true; }\n"
    "  else if (ps == 27) { mat_diffuse = mesh_color * mix(mix(tex1.rgb * tex2.rgb * 2.0, tex3.rgb, vec3(tex3.a)), tex1.rgb, vec3(tex1.a)); }\n"
    "  else if (ps == 28) { vec4 mx = mix(mix(tex1, tex2, vec4(clamp(u_tex_sample_alpha.g, 0.0, 1.0))), tex3, vec4(clamp(u_tex_sample_alpha.b, 0.0, 1.0))); mat_diffuse = mesh_color * mx.rgb; discard_alpha = mx.a * tex4.a; can_discard = true; }\n"
    "  else if (ps == 29) { mat_diffuse = mesh_color * mix(tex1.rgb, tex2.rgb, vec3(tex2.a)); }\n"
    "  else if (ps == 30) { mat_diffuse = mesh_color * mix(tex1.rgb * mix(g0, tex2.rgb, vec3(tex2.a)), tex3.rgb, vec3(tex3.a)); discard_alpha = tex1.a; can_discard = true; }\n"
    "  else if (ps == 31) { mat_diffuse = mesh_color * tex1.rgb * mix(g0, tex2.rgb, vec3(tex2.a)); discard_alpha = tex1.a; can_discard = true; }\n"
    "  else if (ps == 32) { mat_diffuse = mesh_color * mix(tex1.rgb * mix(g0, tex2.rgb, vec3(tex2.a)), tex3.rgb, vec3(tex3.a)); }\n"
    "  else if (ps == 33) { mat_diffuse = mesh_color * tex1.rgb; discard_alpha = tex1.a; can_discard = true; }\n"
    "  else if (ps == 34) { discard_alpha = tex1.a; can_discard = true; }\n"
    "  else if (ps == 35) { vec4 combined = tex1 * tex2 * tex3; mat_diffuse = mesh_color * combined.rgb; discard_alpha = combined.a; can_discard = true; }\n"
    "  else if (ps == 36) { mat_diffuse = mesh_color * tex1.rgb * tex2.rgb; discard_alpha = tex1.a * tex2.a; can_discard = true; }\n"
    "  else { mat_diffuse = mesh_color * tex1.rgb; }\n"
    "  float final_opacity;\n"
    "  bool do_discard = false;\n"
    "  if (u_blend_mode == 13) { final_opacity = discard_alpha * mesh_opacity; }\n"
    "  else if (u_blend_mode == 1) { final_opacity = mesh_opacity; if (can_discard && discard_alpha < u_alpha_test) do_discard = true; }\n"
    "  else if (u_blend_mode == 0) { final_opacity = mesh_opacity; }\n"
    "  else if (u_blend_mode == 4 || u_blend_mode == 5) { final_opacity = discard_alpha * mesh_opacity; if (can_discard && discard_alpha < u_alpha_test) do_discard = true; }\n"
    "  else { final_opacity = discard_alpha * mesh_opacity; }\n"
    "  if (do_discard) discard;\n"
    "  gl_FragColor = vec4(mat_diffuse, final_opacity);\n"
    "}\n";

  // Lazily compile + link the fragment-only combiner program. Returns 0 on failure
  // (caller then falls back to the legacy fixed-function path).
  GLuint ensureCombinerProgram()
  {
    if (g_combinerTried)
      return g_combinerProg;
    g_combinerTried = true;

    if (!video.supportGLSL || !video.supportOGL20)
    {
      LOG_INFO << "M2 combiner shader disabled (no GLSL / OpenGL 2.0 support).";
      return 0;
    }

    GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fs, 1, &COMBINER_FRAGMENT_SRC, NULL);
    glCompileShader(fs);

    GLint ok = GL_FALSE;
    glGetShaderiv(fs, GL_COMPILE_STATUS, &ok);
    if (ok != GL_TRUE)
    {
      char buf[2048]; GLsizei len = 0;
      glGetShaderInfoLog(fs, sizeof(buf), &len, buf);
      LOG_ERROR << "M2 combiner fragment shader failed to compile:" << buf;
      glDeleteShader(fs);
      return 0;
    }

    GLuint prog = glCreateProgram();
    glAttachShader(prog, fs);
    glLinkProgram(prog);
    glDeleteShader(fs); // flagged for deletion once detached at program delete

    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (ok != GL_TRUE)
    {
      char buf[2048]; GLsizei len = 0;
      glGetProgramInfoLog(prog, sizeof(buf), &len, buf);
      LOG_ERROR << "M2 combiner program failed to link:" << buf;
      glDeleteProgram(prog);
      return 0;
    }

    g_uTex0 = glGetUniformLocation(prog, "u_tex0");
    g_uTex1 = glGetUniformLocation(prog, "u_tex1");
    g_uTex2 = glGetUniformLocation(prog, "u_tex2");
    g_uTex3 = glGetUniformLocation(prog, "u_tex3");
    g_uPixelShader = glGetUniformLocation(prog, "u_pixel_shader");
    g_uBlendMode = glGetUniformLocation(prog, "u_blend_mode");
    g_uAlphaTest = glGetUniformLocation(prog, "u_alpha_test");
    g_uTexSampleAlpha = glGetUniformLocation(prog, "u_tex_sample_alpha");

    g_combinerProg = prog;
    LOG_INFO << "M2 combiner shader compiled (program" << prog << ").";
    return prog;
  }
}

ModelRenderPass::ModelRenderPass(WoWModel * m, int geo):
  useTex2(false), useEnvMap(false), cull(false), trans(false),
  unlit(false), noZWrite(false), billboard(false),
  texanim(-1), color(-1), opacity(-1), blendmode(-1), tex(INVALID_TEX),
  swrap(false), twrap(false), ocol(0.0f, 0.0f, 0.0f, 0.0f), ecol(0.0f, 0.0f, 0.0f, 0.0f),
  model(m), geoIndex(geo), specialTex(-1),
  textureCount(1), pixelShader(-1), vertexShader(-1),
  tex2(INVALID_TEX), tex3(INVALID_TEX), tex4(INVALID_TEX), texanim2(-1), combinerActive(false)
{
  uvSource[0] = 0; uvSource[1] = 1; uvSource[2] = 3; uvSource[3] = 3;
}

void ModelRenderPass::deinit()
{
  // Tear down the multi-texture combiner state first, so the rest of this function
  // (and the next pass) sees a clean single-texture-unit, fixed-function pipeline.
  if (combinerActive)
  {
    glUseProgram(0);
    for (int u = 1; u < 4; u++)
    {
      glActiveTexture(GL_TEXTURE0 + u);
      // pop this unit's animated texture matrix if init() pushed one
      if (u == 1 && texanim2 >= 0 && texanim2 < (int16)model->texAnims.size() && uvSource[u] != 2)
      {
        glMatrixMode(GL_TEXTURE);
        glPopMatrix();
        glMatrixMode(GL_MODELVIEW);
      }
      glDisable(GL_TEXTURE_GEN_S);
      glDisable(GL_TEXTURE_GEN_T);
      glDisable(GL_TEXTURE_2D);
    }
    glActiveTexture(GL_TEXTURE0);
    if (uvSource[0] == 2)
    {
      glDisable(GL_TEXTURE_GEN_S);
      glDisable(GL_TEXTURE_GEN_T);
    }
    combinerActive = false;
  }

  glDisable(GL_BLEND);
  glDisable(GL_ALPHA_TEST);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  if (noZWrite)
    glDepthMask(GL_TRUE);

  // pop unit 0's animated texture matrix (pushed in init()). Be explicit about the
  // active unit + matrix mode: the combiner teardown above may have left either in a
  // different state, and popping the wrong stack would corrupt the modelview matrix.
  if (texanim!=-1)
  {
    glActiveTexture(GL_TEXTURE0);
    glMatrixMode(GL_TEXTURE);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
  }

  if (unlit)
    glEnable(GL_LIGHTING);

  //if (billboard)
  //  glPopMatrix();

  if (cull)
    glDisable(GL_CULL_FACE);

  if (useEnvMap)
  {
    glDisable(GL_TEXTURE_GEN_S);
    glDisable(GL_TEXTURE_GEN_T);
  }

  if (swrap)
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);

  if (twrap)
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

  /*
    if (useTex2)
    {
      glDisable(GL_TEXTURE_2D);
      glActiveTextureARB(GL_TEXTURE0);
    }
   */

  if (opacity!=-1 || color!=-1)
  {
    GLfloat czero[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    glMaterialfv(GL_FRONT, GL_EMISSION, czero);

    //glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    //glMaterialfv(GL_FRONT, GL_AMBIENT, ocol);
    //ocol = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
    //glMaterialfv(GL_FRONT, GL_DIFFUSE, ocol);
  }
}


bool ModelRenderPass::init()
{
  // May as well check that we're going to render the geoset before doing all this crap.
  if (!model || geoIndex == -1 || !model->geosets[geoIndex]->display)
    return false;

  // COLOUR
  // Get the colour and transparency and check that we should even render
  ocol = glm::vec4(1.0f, 1.0f, 1.0f, model->trans);
  ecol = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);

  // emissive colors
  if (color != -1 && color < (int16)model->colors.size() && model->colors[color].color.uses(0))
  {
    glm::vec3 c;
    /* Alfred 2008.10.02 buggy opacity make model invisible, TODO */
    c = model->colors[color].color.getValue(0, model->animtime);
    if (model->colors[color].opacity.uses(model->anim))
      ocol.w = model->colors[color].opacity.getValue(model->anim, model->animtime);

    if (unlit)
    {
      ocol.x = c.x; ocol.y = c.y; ocol.z = c.z;
    }
    else
      ocol.x = ocol.y = ocol.z = 0;

    ecol = glm::vec4(c, ocol.w);
    glMaterialfv(GL_FRONT, GL_EMISSION, glm::value_ptr(ecol));
  }

  // opacity
  if (opacity != -1 && 
      opacity < (int16)model->transparency.size() && 
      model->transparency[opacity].trans.uses(0))
  {
    // Alfred 2008.10.02 buggy opacity make model invisible, TODO
    ocol.w *= model->transparency[opacity].trans.getValue(0, model->animtime);
  }

  // exit and return false before affecting the opengl render state
  if (!((ocol.w > 0) && (color == -1 || ecol.w > 0)))
    return false;


  // TEXTURE
  // bind to our texture
  GLuint texId = model->getGLTexture(tex);
  if (texId != INVALID_TEX)
    glBindTexture(GL_TEXTURE_2D, texId);

  // ALPHA BLENDING
  // blend mode
  
  switch (blendmode)
  {
  case BM_OPAQUE:           // 0
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    break;
  case BM_TRANSPARENT:      // 1
    glEnable(GL_ALPHA_TEST);
    glBlendFunc(GL_ONE, GL_ZERO);
    break;
  case BM_ALPHA_BLEND:      // 2
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    break;
  case BM_ADDITIVE:         // 3
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_COLOR, GL_ONE);
    break;
  case BM_ADDITIVE_ALPHA:   // 4
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    break;
  case BM_MODULATE:           // 5
    glEnable(GL_BLEND);
    glBlendFunc(GL_DST_COLOR, GL_ZERO);
    break;
  case BM_MODULATEX2:      // 6
    glEnable(GL_BLEND);
    glBlendFunc(GL_DST_COLOR, GL_SRC_COLOR);
    break;
  case BM_7:                 // 7, new in WoD
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    break;
  default:
    LOG_ERROR << "Unknown blendmode:" << blendmode;
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  }

  if (cull)
    glEnable(GL_CULL_FACE);
  else
    glDisable(GL_CULL_FACE);
  // no writing to the depth buffer.
  if (noZWrite)
    glDepthMask(GL_FALSE);
  else
    glDepthMask(GL_TRUE);

  // Texture wrapping around the geometry
  if (swrap)
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
  if (twrap)
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

  // Environmental mapping, material, and effects
  if (useEnvMap)
  {
    // Turn on the 'reflection' shine, using 18.0f as that is what WoW uses based on the reverse engineering
    // This is now set in InitGL(); - no need to call it every render.
    glMaterialf(GL_FRONT_AND_BACK, GL_SHININESS, 18.0f);

    // env mapping
    glEnable(GL_TEXTURE_GEN_S);
    glEnable(GL_TEXTURE_GEN_T);

    const GLint maptype = GL_SPHERE_MAP;
    //const GLint maptype = GL_REFLECTION_MAP_ARB;

    glTexGeni(GL_S, GL_TEXTURE_GEN_MODE, maptype);
    glTexGeni(GL_T, GL_TEXTURE_GEN_MODE, maptype);
  }

  if (texanim != -1 && 
      texanim < (int16)model->texAnims.size())
  {
    glMatrixMode(GL_TEXTURE);
    glPushMatrix();

    model->texAnims[texanim].setup(texanim);
  }

  // color
  glColor4fv(glm::value_ptr(ocol));
  //glMaterialfv(GL_FRONT, GL_SPECULAR, ocol);

  // don't use lighting on the surface
  if (unlit)
    glDisable(GL_LIGHTING);

  if (blendmode<=1 && ocol.w<1.0f)
    glEnable(GL_BLEND);

  // --- multi-texture combiner (War Within+ cosmic/void materials) ---------------
  // For op_count>1 materials, bind the extra textures and the GLSL combiner so the
  // textures are merged the way the game does (instead of dropping all but one, which
  // left capes/orbs white). Single-texture passes never enter here and are unchanged.
  combinerActive = false;
  static const bool combinerDisabled = (getenv("WMV_NO_COMBINER") != NULL);
  if (pixelShader >= 0 && textureCount > 1 && video.supportMultiTex && !combinerDisabled)
  {
    GLuint prog = ensureCombinerProgram();
    if (prog)
    {
      combinerActive = true;

      // Bind units 1..3. Unused units are filled with unit-0's texture so every
      // sampler the fragment shader might read is well-defined.
      for (int u = 1; u < 4; u++)
      {
        uint16 tIndex = tex;
        if (u < textureCount)
          tIndex = (u == 1) ? tex2 : ((u == 2) ? tex3 : tex4);
        GLuint glTex = model->getGLTexture(tIndex);

        glActiveTexture(GL_TEXTURE0 + u);
        glEnable(GL_TEXTURE_2D);
        if (glTex != ModelRenderPass::INVALID_TEX)
          glBindTexture(GL_TEXTURE_2D, glTex);

        if (u < textureCount && uvSource[u] == 2)
        {
          // environment / sphere map for this unit
          glEnable(GL_TEXTURE_GEN_S);
          glEnable(GL_TEXTURE_GEN_T);
          glTexGeni(GL_S, GL_TEXTURE_GEN_MODE, GL_SPHERE_MAP);
          glTexGeni(GL_T, GL_TEXTURE_GEN_MODE, GL_SPHERE_MAP);
        }
        else
        {
          glDisable(GL_TEXTURE_GEN_S);
          glDisable(GL_TEXTURE_GEN_T);
        }

        // Animate this unit's UVs. War Within cosmic/void materials scroll their glow
        // layer via a per-unit texture transform (e.g. lichlord's green energy). The
        // active texture unit is GL_TEXTURE0+u, so the GL_TEXTURE matrix we load here is
        // unit u's. Env units use generated sphere-map coords and are left unanimated,
        // (the transform is skipped for env channels).
        if (u == 1 && texanim2 >= 0 && texanim2 < (int16)model->texAnims.size() && uvSource[u] != 2)
        {
          glMatrixMode(GL_TEXTURE);
          glPushMatrix();
          model->texAnims[texanim2].setup(texanim2);
          glMatrixMode(GL_MODELVIEW);
        }
      }

      glActiveTexture(GL_TEXTURE0);
      if (uvSource[0] == 2)
      {
        glEnable(GL_TEXTURE_GEN_S);
        glEnable(GL_TEXTURE_GEN_T);
        glTexGeni(GL_S, GL_TEXTURE_GEN_MODE, GL_SPHERE_MAP);
        glTexGeni(GL_T, GL_TEXTURE_GEN_MODE, GL_SPHERE_MAP);
      }

      glUseProgram(prog);
      if (g_uTex0 >= 0) glUniform1i(g_uTex0, 0);
      if (g_uTex1 >= 0) glUniform1i(g_uTex1, 1);
      if (g_uTex2 >= 0) glUniform1i(g_uTex2, 2);
      if (g_uTex3 >= 0) glUniform1i(g_uTex3, 3);
      if (g_uPixelShader >= 0) glUniform1i(g_uPixelShader, pixelShader);
      if (g_uBlendMode >= 0) glUniform1i(g_uBlendMode, egxBlendMode(blendmode));
      if (g_uAlphaTest >= 0) glUniform1f(g_uAlphaTest, 0.501960814f);
      if (g_uTexSampleAlpha >= 0) glUniform3f(g_uTexSampleAlpha, 1.0f, 1.0f, 1.0f);
    }
  }

  return true;
}

void ModelRenderPass::render(bool animated)
{
  ModelGeosetHD * geoset = model->geosets[geoIndex];

  // Multi-texture combiner pass: draw in immediate mode so we can hand every texture
  // unit its own UV (uv set 0, uv set 1, or - for env units - texgen-generated coords).
  // WIP_DH_SUPPORT forces non-VBO mode, so model->vertices/normals are valid CPU arrays.
  if (combinerActive)
  {
    glBegin(GL_TRIANGLES);
    for (size_t k = 0, b = geoset->istart; k < geoset->icount; k++, b++)
    {
      uint32 a = model->indices[b];
      const ModelVertex & ov = model->origVertices[a];
      const float u0 = ov.texcoords.x, v0 = ov.texcoords.y;
      // uv set 1 lives in the two trailing ints of the modern 48-byte M2 vertex.
      const float u1 = *reinterpret_cast<const float *>(&ov.unk1);
      const float v1 = *reinterpret_cast<const float *>(&ov.unk2);
      for (int unit = 0; unit < textureCount && unit < 4; unit++)
      {
        if (uvSource[unit] == 2)
          continue; // env: coordinates come from sphere-map texgen
        if (uvSource[unit] == 1)
          glMultiTexCoord2f(GL_TEXTURE0 + unit, u1, v1);
        else
          glMultiTexCoord2f(GL_TEXTURE0 + unit, u0, v0);
      }
      glNormal3fv(glm::value_ptr(model->normals[a]));
      glVertex3fv(glm::value_ptr(model->vertices[a]));
    }
    glEnd();
    return;
  }

  // we don't want to render completely transparent parts
  // render
  if (animated)
  {
    
    //glDrawElements(GL_TRIANGLES, p.indexCount, GL_UNSIGNED_SHORT, indices + p.indexStart);
    // a GDC OpenGL Performace Tuning paper recommended glDrawRangeElements over glDrawElements
    // I can't notice a difference but I guess it can't hurt
    if (video.supportVBO && video.supportDrawRangeElements)
    {
      glDrawRangeElements(GL_TRIANGLES, geoset->vstart, geoset->vstart + geoset->vcount, geoset->icount, GL_UNSIGNED_SHORT, &model->indices[geoset->istart]);
    }
    else
    {
      glBegin(GL_TRIANGLES);
      for (size_t k = 0, b = geoset->istart; k < geoset->icount; k++, b++)
      {
        uint32 a = model->indices[b];
        glNormal3fv(glm::value_ptr(model->normals[a]));
        glTexCoord2fv(glm::value_ptr(model->origVertices[a].texcoords));
        glVertex3fv(glm::value_ptr(model->vertices[a]));
        /*
        if (geoset->id == 2401 && k < 10)
        {
          LOG_INFO << "b" << b;
          LOG_INFO << "a" << model->indices[b] << a;
          LOG_INFO << "model->normals[a]" << model->normals[a].x << model->normals[a].y << model->normals[a].z;
          LOG_INFO << "model->vertices[a]" << model->vertices[a].x << model->vertices[a].y << model->vertices[a].z;
        }
        */

      }
      glEnd();
    }
  }
  else
  {
    glBegin(GL_TRIANGLES);
    for (size_t k = 0, b = geoset->istart; k < geoset->icount; k++, b++)
    {
      uint16 a = model->indices[b];
      glNormal3fv(glm::value_ptr(model->normals[a]));
      glTexCoord2fv(glm::value_ptr(model->origVertices[a].texcoords));
      glVertex3fv(glm::value_ptr(model->vertices[a]));
    }
    glEnd();
  }
}