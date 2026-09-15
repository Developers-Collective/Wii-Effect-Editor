#include "preview_view.h"
#include <dolphin/gx.h>
#include <dolphin/gx/GXAurora.h>
#include <dolphin/mtx.h>

void drawPreviewGrid(const PreviewRect& r,const OrbitCamera& camera,bool grid,bool axes) {
    if (r.width<1 || r.height<1) return;
    AuroraSetViewportPolicy(AURORA_VIEWPORT_NATIVE);
    GXSetViewportRender(r.x,r.y,r.width,r.height,0,1);
    GXSetScissorRender(unsigned(r.x),unsigned(r.y),unsigned(r.width),unsigned(r.height));
    Mtx44 projection; C_MTXPerspective(projection,45.f,r.width/r.height,.001f,1000000.f);
    GXSetProjection(projection,GX_PERSPECTIVE);
    Mtx view; camera.view(view); GXLoadPosMtxImm(view,GX_PNMTX0); GXSetCurrentMtx(GX_PNMTX0);
    GXClearVtxDesc(); GXSetVtxDesc(GX_VA_POS,GX_DIRECT); GXSetVtxDesc(GX_VA_CLR0,GX_DIRECT);
    GXSetVtxAttrFmt(GX_VTXFMT0,GX_VA_POS,GX_POS_XYZ,GX_F32,0);
    GXSetVtxAttrFmt(GX_VTXFMT0,GX_VA_CLR0,GX_CLR_RGBA,GX_RGBA8,0);
    GXSetNumChans(1); GXSetChanCtrl(GX_COLOR0A0,GX_FALSE,GX_SRC_REG,GX_SRC_VTX,GX_LIGHT_NULL,GX_DF_NONE,GX_AF_NONE);
    GXSetNumTexGens(0); GXSetNumTevStages(1); GXSetNumIndStages(0);
    GXSetTevDirect(GX_TEVSTAGE0); GXSetTevOrder(GX_TEVSTAGE0,GX_TEXCOORD_NULL,GX_TEXMAP_NULL,GX_COLOR0A0);
    GXSetTevOp(GX_TEVSTAGE0,GX_PASSCLR);
    GXSetBlendMode(GX_BM_NONE,GX_BL_ONE,GX_BL_ZERO,GX_LO_COPY);
    GXSetAlphaCompare(GX_ALWAYS,0,GX_AOP_AND,GX_ALWAYS,0);
    GXSetZMode(GX_TRUE,GX_LEQUAL,GX_TRUE); GXSetCullMode(GX_CULL_NONE);
    GXSetColorUpdate(GX_TRUE); GXSetAlphaUpdate(GX_TRUE);
    GXSetFog(GX_FOG_NONE,0,1,.001f,1000000.f,GXColor{0,0,0,0});
    GXSetLineWidth(6,GX_TO_ZERO);
    auto vertex=[](float x,float y,float z,GXColor c) { GXPosition3f32(x,y,z); GXColor4u8(c.r,c.g,c.b,c.a); };
    if (grid) {
    GXBegin(GX_LINES,GX_VTXFMT0,84);
    for (int i=-10;i<=10;++i) {
        const GXColor color=i==0 ? GXColor{72,85,101,255} : GXColor{37,46,58,255};
        vertex(float(i*10),-100,0,color); vertex(float(i*10),100,0,color);
        vertex(-100,float(i*10),0,color); vertex(100,float(i*10),0,color);
    }
    GXEnd();
    }
    if (axes) {
    GXBegin(GX_LINES,GX_VTXFMT0,6);
    vertex(0,0,0,{210,85,85,255}); vertex(25,0,0,{210,85,85,255});
    vertex(0,0,0,{85,190,130,255}); vertex(0,25,0,{85,190,130,255});
    vertex(0,0,0,{95,145,230,255}); vertex(0,0,25,{95,145,230,255});
    GXEnd();
    }
}
