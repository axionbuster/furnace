/**
 * Furnace Tracker - multi-system chiptune tracker
 * Copyright (C) 2021-2026 tildearrow and contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#define _USE_MATH_DEFINES
#include "gui.h"
#include "imgui.h"
#include "imgui_internal.h"

// fills per accidental class, indexed by DivEDO31Accidental
static const ImU32 terpstraFill[5]={
  IM_COL32(0xf6,0xf6,0xf6,0xff), // natural
  IM_COL32(0xdc,0xdc,0xdc,0xff), // sharp
  IM_COL32(0xcb,0xb7,0x9a,0xff), // flat
  IM_COL32(0x6f,0x6f,0x6f,0xff), // double sharp
  IM_COL32(0xe8,0x96,0x3c,0xff)  // double flat
};

#define TERPSTRA_TEXT_DARK IM_COL32(0x18,0x18,0x18,0xff)
#define TERPSTRA_TEXT_LIGHT IM_COL32(0xf0,0xf0,0xf0,0xff)

static const ImU32 terpstraTextColor[5]={
  TERPSTRA_TEXT_DARK,
  TERPSTRA_TEXT_DARK,
  TERPSTRA_TEXT_DARK,
  TERPSTRA_TEXT_LIGHT,
  TERPSTRA_TEXT_DARK
};

#define TERPSTRA_PRESSED IM_COL32(0x3f,0xd9,0x42,0xff)
#define TERPSTRA_OUT_OF_RANGE IM_COL32(0x40,0x40,0x40,0x40)
#define TERPSTRA_OUTLINE IM_COL32(0x10,0x10,0x10,0xff)
#define TERPSTRA_BADGE IM_COL32(0x1c,0x1c,0x1c,0xd0)

#define TERPSTRA_SQRT3 1.7320508f
// length of the octave vector (7,-2) in hex radii: sqrt(117)
#define TERPSTRA_OCTAVE_LEN 10.816654f
// the octave vector leans atan(1/(2*sqrt(3)))=16.102 degrees off horizontal in
// the raw pointy-top layout; the whole lattice is rotated to lay it flat
#define TERPSTRA_ROT_COS 0.96076892f
#define TERPSTRA_ROT_SIN 0.27735010f
#define TERPSTRA_OCTAVES_VISIBLE 2.5f
#define TERPSTRA_MIN_ZOOM 0.3f
#define TERPSTRA_MAX_ZOOM 5.0f

// nearest hex center of a fractional axial coordinate (cube rounding)
static void terpstraRound(float q, float r, int& outQ, int& outR) {
  float x=q;
  float z=r;
  float y=-x-z;
  float rx=round(x);
  float ry=round(y);
  float rz=round(z);
  float dx=fabs(rx-x);
  float dy=fabs(ry-y);
  float dz=fabs(rz-z);
  if (dx>dy && dx>dz) {
    rx=-ry-rz;
  } else if (dy<=dz) {
    rz=-rx-ry;
  }
  outQ=(int)rx;
  outR=(int)rz;
}

void FurnaceGUI::drawTerpstra() {
  if (nextWindow==GUI_WINDOW_TERPSTRA) {
    terpstraOpen=true;
    ImGui::SetNextWindowFocus();
    nextWindow=GUI_WINDOW_NOTHING;
  }
  if (!terpstraOpen) return;
  if (ImGui::Begin("Terpstra Keyboard",&terpstraOpen,globalWinFlags|ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse,_("Terpstra Keyboard"))) {
    bool oldTerpstraKeyPressed[180];
    memcpy(oldTerpstraKeyPressed,terpstraKeyPressed,180*sizeof(bool));
    memset(terpstraKeyPressed,0,180*sizeof(bool));

    // reverse noteKeys lookup for the QWERTY badges
    char keyBadge[97];
    memset(keyBadge,0,97);
    for (std::map<int,int>::value_type& i: noteKeys) {
      if (i.second<0 || i.second>96) continue;
      const char* keyName=SDL_GetScancodeName((SDL_Scancode)i.first);
      if (keyName==NULL) continue;
      if (keyName[0]==0) continue;
      keyBadge[i.second]=keyName[0];
    }

    ImDrawList* dl=ImGui::GetWindowDrawList();
    ImGuiWindow* window=ImGui::GetCurrentWindow();
    ImVec2 size=ImGui::GetContentRegionAvail();
    if (size.x<1.0f) size.x=1.0f;
    if (size.y<1.0f) size.y=1.0f;

    ImVec2 minArea=window->DC.CursorPos;
    ImVec2 maxArea=ImVec2(
      minArea.x+size.x,
      minArea.y+size.y
    );
    ImRect rect=ImRect(minArea,maxArea);
    ImGui::ItemSize(size,ImGui::GetStyle().FramePadding.y);
    if (ImGui::ItemAdd(rect,ImGui::GetID("terpstraDisplay"))) {
      bool canInput=false;
      if (ImGui::ItemHoverable(rect,ImGui::GetID("terpstraDisplay"),0)) {
        canInput=true;
        ImGui::InhibitInertialScroll();
      }

      ImGuiIO& io=ImGui::GetIO();
      if (canInput) {
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Right)) {
          terpstraPanX+=io.MouseDelta.x;
          terpstraPanY+=io.MouseDelta.y;
        }
        if (io.KeyCtrl && io.MouseWheel!=0.0f) {
          terpstraZoom*=pow(2.0f,io.MouseWheel*0.25f);
          if (terpstraZoom<TERPSTRA_MIN_ZOOM) terpstraZoom=TERPSTRA_MIN_ZOOM;
          if (terpstraZoom>TERPSTRA_MAX_ZOOM) terpstraZoom=TERPSTRA_MAX_ZOOM;
        }
      }

      float hexSize=(size.x/(TERPSTRA_OCTAVES_VISIBLE*TERPSTRA_OCTAVE_LEN))*terpstraZoom;
      if (hexSize<2.0f) hexSize=2.0f;
      ImVec2 center=ImVec2(
        (rect.Min.x+rect.Max.x)*0.5f+terpstraPanX,
        (rect.Min.y+rect.Max.y)*0.5f+terpstraPanY
      );

      auto hexCenter=[&](int q, int r) -> ImVec2 {
        float rawX=hexSize*TERPSTRA_SQRT3*((float)q+(float)r*0.5f);
        float rawY=hexSize*1.5f*(float)r;
        return ImVec2(
          center.x+rawX*TERPSTRA_ROT_COS-rawY*TERPSTRA_ROT_SIN,
          center.y+rawX*TERPSTRA_ROT_SIN+rawY*TERPSTRA_ROT_COS
        );
      };

      auto hexCoord=[&](float x, float y, float& q, float& r) {
        float dx=x-center.x;
        float dy=y-center.y;
        float rawX=dx*TERPSTRA_ROT_COS+dy*TERPSTRA_ROT_SIN;
        float rawY=dy*TERPSTRA_ROT_COS-dx*TERPSTRA_ROT_SIN;
        r=rawY/(1.5f*hexSize);
        q=rawX/(TERPSTRA_SQRT3*hexSize)-r*0.5f;
      };

      // evaluate input
      if (canInput) for (TouchPoint& i: activePoints) {
        if (!rect.Contains(ImVec2(i.x,i.y))) continue;
        float fq=0.0f;
        float fr=0.0f;
        int q=0;
        int r=0;
        hexCoord(i.x,i.y,fq,fr);
        terpstraRound(fq,fr,q,r);
        int note=DIV_EDO31_MIDDLE_C+5*q+2*r;
        if (note<0) continue;
        if (note>=180) continue;
        terpstraKeyPressed[note]=true;
      }

      // hexagon outline, rotated with the lattice
      ImVec2 vertex[6];
      for (int i=0; i<6; i++) {
        float angle=(float)i*(M_PI/3.0)+(M_PI/2.0);
        float vx=hexSize*cos(angle)*0.95f;
        float vy=hexSize*sin(angle)*0.95f;
        vertex[i]=ImVec2(
          vx*TERPSTRA_ROT_COS-vy*TERPSTRA_ROT_SIN,
          vx*TERPSTRA_ROT_SIN+vy*TERPSTRA_ROT_COS
        );
      }

      // visible lattice range
      float minQ=0.0f, maxQ=0.0f, minR=0.0f, maxR=0.0f;
      for (int i=0; i<4; i++) {
        float cq=0.0f;
        float cr=0.0f;
        hexCoord((i&1)?rect.Max.x:rect.Min.x,(i&2)?rect.Max.y:rect.Min.y,cq,cr);
        if (i==0 || cq<minQ) minQ=cq;
        if (i==0 || cq>maxQ) maxQ=cq;
        if (i==0 || cr<minR) minR=cr;
        if (i==0 || cr>maxR) maxR=cr;
      }

      const float nameSize=hexSize*0.5f;
      const float smallSize=hexSize*0.32f;
      const bool labels=(hexSize>=14.0f*dpiScale);

      dl->PushClipRect(rect.Min,rect.Max,true);
      for (int r=(int)floor(minR)-1; r<=(int)ceil(maxR)+1; r++) {
        for (int q=(int)floor(minQ)-1; q<=(int)ceil(maxQ)+1; q++) {
          ImVec2 pos=hexCenter(q,r);
          if (pos.x<rect.Min.x-hexSize || pos.x>rect.Max.x+hexSize) continue;
          if (pos.y<rect.Min.y-hexSize || pos.y>rect.Max.y+hexSize) continue;
          int note=DIV_EDO31_MIDDLE_C+5*q+2*r;
          bool inRange=(note>=0 && note<180);

          ImVec2 points[6];
          for (int i=0; i<6; i++) {
            points[i]=ImVec2(pos.x+vertex[i].x,pos.y+vertex[i].y);
          }

          ImU32 fill=TERPSTRA_OUT_OF_RANGE;
          if (inRange) {
            fill=terpstraKeyPressed[note]?TERPSTRA_PRESSED:terpstraFill[edo31Class[note%31]];
          }
          dl->AddConvexPolyFilled(points,6,fill);
          dl->AddPolyline(points,6,TERPSTRA_OUTLINE,ImDrawFlags_Closed,dpiScale);

          if (!inRange) continue;
          if (!labels) continue;

          ImU32 textColor=terpstraKeyPressed[note]?TERPSTRA_TEXT_DARK:terpstraTextColor[edo31Class[note%31]];

          const char* stepName=edo31Names[note%31];
          ImVec2 stepSize=mainFont->CalcTextSizeA(nameSize,FLT_MAX,0.0f,stepName);
          dl->AddText(mainFont,nameSize,ImVec2(pos.x-stepSize.x*0.5f,pos.y-stepSize.y*0.5f),textColor,stepName);

          char octave[2];
          octave[0]='0'+edo31Octave(note);
          octave[1]=0;
          ImVec2 octaveSize=mainFont->CalcTextSizeA(smallSize,FLT_MAX,0.0f,octave);
          dl->AddText(mainFont,smallSize,ImVec2(pos.x+hexSize*0.44f-octaveSize.x,pos.y-hexSize*0.62f),textColor,octave);

          int key=note-DIV_EDO31_STEPS*(curOctave-2);
          if (key>=0 && key<=96 && keyBadge[key]) {
            char badge[2];
            badge[0]=keyBadge[key];
            badge[1]=0;
            ImVec2 badgeSize=mainFont->CalcTextSizeA(smallSize,FLT_MAX,0.0f,badge);
            ImVec2 badgePos=ImVec2(pos.x-hexSize*0.44f,pos.y+hexSize*0.16f);
            dl->AddRectFilled(
              ImVec2(badgePos.x-hexSize*0.06f,badgePos.y-hexSize*0.04f),
              ImVec2(badgePos.x+badgeSize.x+hexSize*0.06f,badgePos.y+badgeSize.y+hexSize*0.04f),
              TERPSTRA_BADGE,hexSize*0.08f
            );
            dl->AddText(mainFont,smallSize,badgePos,TERPSTRA_TEXT_LIGHT,badge);
          }
        }
      }
      dl->PopClipRect();
    }

    // first check released keys
    for (int i=0; i<180; i++) {
      int note=i;
      if (!terpstraKeyPressed[i]) {
        if (terpstraKeyPressed[i]!=oldTerpstraKeyPressed[i]) {
          e->synchronized([this,note]() {
            e->autoNoteOff(-1,note);
            failedNoteOn=false;
          });
        }
      }
    }
    // then pressed ones
    for (int i=0; i<180; i++) {
      int note=i;
      if (terpstraKeyPressed[i]) {
        if (terpstraKeyPressed[i]!=oldTerpstraKeyPressed[i]) {
          e->synchronized([this,note]() {
            if (!e->autoNoteOn(-1,curIns,note)) failedNoteOn=true;
          });
          if (edit && curWindow!=GUI_WINDOW_INS_LIST && curWindow!=GUI_WINDOW_INS_EDIT) noteInput(note,0);
        }
      }
    }
  }
  if (ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows)) curWindow=GUI_WINDOW_TERPSTRA;
  ImGui::End();
}
