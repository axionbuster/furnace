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
#include "IconsFontAwesome4.h"
#include "furIcons.h"

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
#define TERPSTRA_OCTAVES_VISIBLE 2.65f
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
  // the body below is what normally issues note-offs. if it doesn't run (window
  // closed or collapsed while a hex is held) the held notes would drone forever,
  // so sweep them here instead.
  auto releaseHeld=[this]() {
    for (int i=0; i<180; i++) {
      if (!terpstraKeyPressed[i]) continue;
      terpstraKeyPressed[i]=false;
      int note=i;
      e->synchronized([this,note]() {
        e->autoNoteOff(-1,note);
        failedNoteOn=false;
      });
    }
  };

  if (nextWindow==GUI_WINDOW_TERPSTRA) {
    terpstraOpen=true;
    ImGui::SetNextWindowFocus();
    nextWindow=GUI_WINDOW_NOTHING;
  }
  if (!terpstraOpen) {
    releaseHeld();
    return;
  }
  ImGui::SetNextWindowSize(ImVec2(860.0f*dpiScale,480.0f*dpiScale),ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSizeConstraints(
    ImVec2(MIN(480.0f*dpiScale,canvasW),MIN(280.0f*dpiScale,canvasH)),
    ImVec2(canvasW,canvasH)
  );
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

    ImGuiIO& io=ImGui::GetIO();
    auto tooltip=[](const char* description) {
      if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s",description);
    };
    auto moveGuide=[this](int q, int r) {
      terpstraAnchorQ=CLAMP(terpstraAnchorQ+q,-128,128);
      terpstraAnchorR=CLAMP(terpstraAnchorR+r,-128,128);
    };
    auto zoomCentered=[this](float factor) {
      float oldZoom=terpstraZoom;
      terpstraZoom=CLAMP(terpstraZoom*factor,TERPSTRA_MIN_ZOOM,TERPSTRA_MAX_ZOOM);
      float ratio=terpstraZoom/oldZoom;
      terpstraPanX*=ratio;
      terpstraPanY*=ratio;
    };

    // keep the pattern and keyboard-guide controls in one compact row so the
    // lattice layout itself remains unchanged.
    ImGui::Text("%s %d",_("Step"),editStep);
    ImGui::SameLine();
    ImGui::BeginDisabled(!edit);
    if (ImGui::SmallButton(ICON_FA_ARROW_UP "##TerpstraStepUp")) moveCursor(0,-editStep,false);
    tooltip(_("Move the pattern cursor up by Edit Step"));
    ImGui::SameLine();
    if (ImGui::SmallButton(ICON_FA_ARROW_DOWN "##TerpstraStepDown")) moveCursor(0,editStep,false);
    tooltip(_("Move the pattern cursor down by Edit Step"));
    ImGui::SameLine();
    if (ImGui::SmallButton(ICON_FA_ANGLE_DOUBLE_UP "##TerpstraPatternTop")) moveCursor(0,-cursor.y,false);
    tooltip(_("Move to the top of the pattern"));
    ImGui::SameLine();
    if (ImGui::SmallButton(ICON_FA_CROSSHAIRS "##TerpstraPatternMiddle")) moveCursor(0,((e->curSubSong->patLen-1)/2)-cursor.y,false);
    tooltip(_("Move to the middle of the pattern"));
    ImGui::SameLine();
    if (ImGui::SmallButton(ICON_FA_ANGLE_DOUBLE_DOWN "##TerpstraPatternBottom")) moveCursor(0,(e->curSubSong->patLen-1)-cursor.y,false);
    tooltip(_("Move to the bottom of the pattern"));
    ImGui::EndDisabled();

    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    ImGui::TextUnformatted(_("Guide"));
    ImGui::SameLine();
    if (ImGui::SmallButton(ICON_FA_ARROW_LEFT "##TerpstraGuideLeft")) moveGuide(-1,0);
    tooltip(_("Move the physical keyboard guide left (5 EDO steps)"));
    ImGui::SameLine();
    if (ImGui::SmallButton(ICON_FA_ARROW_RIGHT "##TerpstraGuideRight")) moveGuide(1,0);
    tooltip(_("Move the physical keyboard guide right (5 EDO steps)"));
    ImGui::SameLine();
    if (ImGui::SmallButton(ICON_FA_ARROW_UP "##TerpstraGuideUp")) moveGuide(0,1);
    tooltip(_("Move the physical keyboard guide up (2 EDO steps)"));
    ImGui::SameLine();
    if (ImGui::SmallButton(ICON_FA_ARROW_DOWN "##TerpstraGuideDown")) moveGuide(0,-1);
    tooltip(_("Move the physical keyboard guide down (2 EDO steps)"));
    ImGui::SameLine();
    if (ImGui::SmallButton(_("Oct-##TerpstraGuideOctaveDown"))) moveGuide(-7,2);
    tooltip(_("Move the guide down one octave"));
    ImGui::SameLine();
    if (ImGui::SmallButton(_("Oct+##TerpstraGuideOctaveUp"))) moveGuide(7,-2);
    tooltip(_("Move the guide up one octave"));
    ImGui::SameLine();
    if (ImGui::SmallButton(_("5th-##TerpstraGuideFifthDown"))) moveGuide(-4,1);
    tooltip(_("Move the guide down one fifth"));
    ImGui::SameLine();
    if (ImGui::SmallButton(_("5th+##TerpstraGuideFifthUp"))) moveGuide(4,-1);
    tooltip(_("Move the guide up one fifth"));

    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    if (ImGui::SmallButton(ICON_FA_MINUS "##TerpstraZoomOut")) zoomCentered(1.0f/1.15f);
    tooltip(_("Zoom out"));
    ImGui::SameLine();
    if (ImGui::SmallButton(ICON_FA_PLUS "##TerpstraZoomIn")) zoomCentered(1.15f);
    tooltip(_("Zoom in"));
    ImGui::SameLine();
    if (ImGui::SmallButton(ICON_FA_HOME "##TerpstraViewHome")) {
      terpstraPanX=0.0f;
      terpstraPanY=0.0f;
      terpstraZoom=1.0f;
    }
    tooltip(_("Reset zoom and pan"));
    ImGui::SameLine();
    if (ImGui::SmallButton(ICON_FA_COG "##TerpstraOptions")) {
      ImGui::OpenPopup("TerpstraOptionsPopup");
    }
    tooltip(_("Options"));
    if (ImGui::BeginPopup("TerpstraOptionsPopup")) {
      bool channelColorMode=terpstraColorMode==1;
      ImGui::BeginDisabled(channelColorMode);
      ImGui::ColorEdit4(_("Color"),(float*)&terpstraColor);
      ImGui::EndDisabled();
      if (ImGui::Checkbox(_("Set to channel color"),&channelColorMode)) {
        terpstraColorMode=channelColorMode?1:0;
      }
      ImGui::EndPopup();
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

      bool windowFocused=ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows);
      if (windowFocused) {
        const float keyboardPan=36.0f*dpiScale;
        if (io.KeyCtrl) {
          if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) terpstraPanX+=keyboardPan;
          if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) terpstraPanX-=keyboardPan;
          if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) terpstraPanY+=keyboardPan;
          if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) terpstraPanY-=keyboardPan;
          if (ImGui::IsKeyPressed(ImGuiKey_Minus) || ImGui::IsKeyPressed(ImGuiKey_KeypadSubtract)) zoomCentered(1.0f/1.15f);
          if (ImGui::IsKeyPressed(ImGuiKey_Equal) || ImGui::IsKeyPressed(ImGuiKey_KeypadAdd)) zoomCentered(1.15f);
          if (ImGui::IsKeyPressed(ImGuiKey_0) || ImGui::IsKeyPressed(ImGuiKey_Keypad0)) {
            terpstraPanX=0.0f;
            terpstraPanY=0.0f;
            terpstraZoom=1.0f;
          }
        } else if (io.KeyShift) {
          if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) moveGuide(-7,2);
          if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) moveGuide(7,-2);
        } else if (io.KeyAlt) {
          if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) moveGuide(-4,1);
          if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) moveGuide(4,-1);
        } else {
          if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) moveGuide(-1,0);
          if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) moveGuide(1,0);
          if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) moveGuide(0,1);
          if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) moveGuide(0,-1);
        }
      }

      auto zoomAt=[this,&rect](const ImVec2& anchor, float factor) {
        float oldZoom=terpstraZoom;
        terpstraZoom=CLAMP(terpstraZoom*factor,TERPSTRA_MIN_ZOOM,TERPSTRA_MAX_ZOOM);
        float ratio=terpstraZoom/oldZoom;
        ImVec2 baseCenter=ImVec2((rect.Min.x+rect.Max.x)*0.5f,(rect.Min.y+rect.Max.y)*0.5f);
        terpstraPanX=anchor.x-baseCenter.x-(anchor.x-baseCenter.x-terpstraPanX)*ratio;
        terpstraPanY=anchor.y-baseCenter.y-(anchor.y-baseCenter.y-terpstraPanY)*ratio;
      };

      if (canInput) {
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Right) || ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
          terpstraPanX+=io.MouseDelta.x;
          terpstraPanY+=io.MouseDelta.y;
        }
        if ((io.KeyCtrl || io.KeyAlt) && (io.MouseWheel!=0.0f || io.MouseWheelH!=0.0f)) {
          float wheel=(fabs(io.MouseWheel)>=fabs(io.MouseWheelH))?io.MouseWheel:io.MouseWheelH;
          zoomAt(io.MousePos,pow(2.0f,wheel*0.18f));
        } else {
          terpstraPanX-=io.MouseWheelH*36.0f*dpiScale;
          terpstraPanY+=io.MouseWheel*36.0f*dpiScale;
        }
      }

      // direct two-touch input pans and pinches. desktop trackpads arrive as
      // precise wheel events above; touch screens use the same viewport math.
      TouchPoint* gesturePoints[2]={NULL,NULL};
      int gesturePointCount=0;
      for (TouchPoint& point: activePoints) {
        if (point.id<0 || !rect.Contains(ImVec2(point.x,point.y))) continue;
        if (gesturePointCount<2) gesturePoints[gesturePointCount]=&point;
        gesturePointCount++;
      }
      if (gesturePointCount>=2) {
        ImVec2 touchCenter=ImVec2(
          (gesturePoints[0]->x+gesturePoints[1]->x)*0.5f,
          (gesturePoints[0]->y+gesturePoints[1]->y)*0.5f
        );
        float dx=gesturePoints[1]->x-gesturePoints[0]->x;
        float dy=gesturePoints[1]->y-gesturePoints[0]->y;
        float distance=sqrt(dx*dx+dy*dy);
        if (terpstraTouchDistance>1.0f && distance>1.0f) {
          terpstraPanX+=touchCenter.x-terpstraTouchX;
          terpstraPanY+=touchCenter.y-terpstraTouchY;
          zoomAt(touchCenter,CLAMP(distance/terpstraTouchDistance,0.5f,2.0f));
        }
        terpstraTouchGesture=true;
        terpstraTouchX=touchCenter.x;
        terpstraTouchY=touchCenter.y;
        terpstraTouchDistance=distance;
      } else {
        terpstraTouchDistance=0.0f;
        if (gesturePointCount==0) terpstraTouchGesture=false;
      }

      if (!isfinite(terpstraZoom)) terpstraZoom=1.0f;
      if (!isfinite(terpstraPanX)) terpstraPanX=0.0f;
      if (!isfinite(terpstraPanY)) terpstraPanY=0.0f;
      terpstraZoom=CLAMP(terpstraZoom,TERPSTRA_MIN_ZOOM,TERPSTRA_MAX_ZOOM);
      float maxPanX=size.x*(1.5f+terpstraZoom*0.65f);
      float maxPanY=size.y*(1.5f+terpstraZoom*0.65f);
      terpstraPanX=CLAMP(terpstraPanX,-maxPanX,maxPanX);
      terpstraPanY=CLAMP(terpstraPanY,-maxPanY,maxPanY);

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
          center.y-rawX*TERPSTRA_ROT_SIN-rawY*TERPSTRA_ROT_COS
        );
      };

      auto hexCoord=[&](float x, float y, float& q, float& r) {
        float dx=x-center.x;
        float dy=y-center.y;
        float rawX=dx*TERPSTRA_ROT_COS-dy*TERPSTRA_ROT_SIN;
        float rawY=-dy*TERPSTRA_ROT_COS-dx*TERPSTRA_ROT_SIN;
        r=rawY/(1.5f*hexSize);
        q=rawX/(TERPSTRA_SQRT3*hexSize)-r*0.5f;
      };

      // evaluate input
      if (canInput) for (TouchPoint& i: activePoints) {
        if (!rect.Contains(ImVec2(i.x,i.y))) continue;
        if (i.id>=0 && terpstraTouchGesture) continue;
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

      bool physicalKeyPressed[180];
      bool pitchClassPressed[31];
      int inputChannel[180];
      int pitchClassChannel[31];
      size_t inputAge[180];
      memset(physicalKeyPressed,0,sizeof(physicalKeyPressed));
      memset(pitchClassPressed,0,sizeof(pitchClassPressed));
      memset(inputChannel,-1,sizeof(inputChannel));
      memset(pitchClassChannel,-1,sizeof(pitchClassChannel));
      memset(inputAge,0,sizeof(inputAge));
      for (int i=0; i<SDL_NUM_SCANCODES; i++) {
        int note=terpstraPreviewNote[i];
        if (note>=0 && note<180) physicalKeyPressed[note]=true;
      }

      // resolve a held preview note back to the actual channel selected by
      // autoNoteOn(). this keeps the visual correct when polyphonic preview
      // routes away from the cursor channel as well as in mono mode.
      for (int i=0; i<e->getTotalChannelCount(); i++) {
        DivChannelState* chanState=e->getChanState(i);
        if (chanState==NULL || chanState->midiNote<0 || chanState->midiNote>=180) continue;
        int note=chanState->midiNote;
        if (inputChannel[note]<0 || chanState->midiAge>=inputAge[note]) {
          inputChannel[note]=i;
          inputAge[note]=chanState->midiAge;
        }
      }
      for (int note=0; note<180; note++) {
        if (terpstraKeyPressed[note] || physicalKeyPressed[note]) {
          int inputChan=inputChannel[note];
          if (inputChan<0 && cursor.xCoarse>=0 && cursor.xCoarse<e->getTotalChannelCount()) {
            inputChan=cursor.xCoarse;
          }
          pitchClassPressed[note%31]=true;
          pitchClassChannel[note%31]=inputChan;
        }
      }

      // unlike the piano's configurable trigger/volume feedback, the
      // Terpstra shows the notes which are currently held by playback. a
      // channel keeps its cell lit until note-off (keyOn becomes false), and
      // muted channels do not contribute a light. recompute this every frame
      // so stopping playback and changing a channel's pitch clear the old
      // cells without requiring a separate latch to maintain.
      int playbackChannel[180];
      memset(playbackChannel,-1,sizeof(playbackChannel));
      if (e->isRunning()) {
        for (int i=0; i<e->getTotalChannelCount(); i++) {
          DivChannelState* chanState=e->getChanState(i);
          if (e->isChannelMuted(i) || chanState==NULL || !chanState->keyOn) continue;
          if (chanState->note>=0 && chanState->note<180) {
            playbackChannel[chanState->note]=i;
          }
        }
      }

      auto highlightColor=[this](int channel) {
        if (terpstraColorMode==1 && channel>=0 && channel<e->getTotalChannelCount()) {
          return channelColor(channel);
        }
        return terpstraColor;
      };

      // hexagon outline, rotated with the lattice
      ImVec2 vertex[6];
      for (int i=0; i<6; i++) {
        float angle=(float)i*(M_PI/3.0)+(M_PI/2.0);
        float vx=hexSize*cos(angle)*0.95f;
        float vy=hexSize*sin(angle)*0.95f;
        vertex[i]=ImVec2(
          vx*TERPSTRA_ROT_COS-vy*TERPSTRA_ROT_SIN,
          -vx*TERPSTRA_ROT_SIN-vy*TERPSTRA_ROT_COS
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
          ImU32 highlightTextColor=TERPSTRA_TEXT_DARK;
          bool pressed=false;
          bool octaveEcho=false;
          bool highlighted=false;
          if (inRange) {
            pressed=terpstraKeyPressed[note] || physicalKeyPressed[note];
            octaveEcho=!pressed && pitchClassPressed[note%31];
            fill=terpstraFill[edo31Class[note%31]];
            int hitChan=pressed?inputChannel[note]:playbackChannel[note];
            if (pressed && hitChan<0 && cursor.xCoarse>=0 && cursor.xCoarse<e->getTotalChannelCount()) {
              hitChan=cursor.xCoarse;
            }
            if (pressed || hitChan>=0) {
              ImVec4 baseColor=ImGui::ColorConvertU32ToFloat4(fill);
              ImVec4 hitColor=highlightColor(hitChan);
              float mix=hitColor.w;
              baseColor.x+=(hitColor.x-baseColor.x)*mix;
              baseColor.y+=(hitColor.y-baseColor.y)*mix;
              baseColor.z+=(hitColor.z-baseColor.z)*mix;
              fill=ImGui::ColorConvertFloat4ToU32(baseColor);
              float luminance=baseColor.x*0.299f+baseColor.y*0.587f+baseColor.z*0.114f;
              highlightTextColor=(luminance>0.55f)?TERPSTRA_TEXT_DARK:TERPSTRA_TEXT_LIGHT;
              highlighted=true;
            }
          }
          dl->AddConvexPolyFilled(points,6,fill);
          if (octaveEcho) {
            ImVec4 echoColor=highlightColor(pitchClassChannel[note%31]);
            echoColor.w*=0.3f;
            dl->AddConvexPolyFilled(points,6,ImGui::ColorConvertFloat4ToU32(echoColor));
          }
          dl->AddPolyline(points,6,TERPSTRA_OUTLINE,ImDrawFlags_Closed,dpiScale);

          if (!inRange) continue;
          if (!labels) continue;

          ImU32 textColor=highlighted?highlightTextColor:terpstraTextColor[edo31Class[note%31]];

          const char* stepName=edo31Names[note%31];
          if (edo31Class[note%31]==DIV_EDO31_DFLAT) {
            // the custom accidental is merged into the pattern font at a
            // fixed cell width, so compose it with the UI-font letter here.
            char noteLetter[2]={stepName[0],0};
            ImVec2 letterSize=mainFont->CalcTextSizeA(nameSize,FLT_MAX,0.0f,noteLetter);
            ImVec2 accidentalSize=patFont->CalcTextSizeA(nameSize,FLT_MAX,0.0f,ICON_FUR_DOUBLE_FLAT);
            float labelX=pos.x-(letterSize.x+accidentalSize.x)*0.5f;
            dl->AddText(mainFont,nameSize,ImVec2(labelX,pos.y-letterSize.y*0.5f),textColor,noteLetter);
            dl->AddText(patFont,nameSize,ImVec2(labelX+letterSize.x,pos.y-accidentalSize.y*0.5f),textColor,ICON_FUR_DOUBLE_FLAT);
          } else {
            ImVec2 stepSize=mainFont->CalcTextSizeA(nameSize,FLT_MAX,0.0f,stepName);
            dl->AddText(mainFont,nameSize,ImVec2(pos.x-stepSize.x*0.5f,pos.y-stepSize.y*0.5f),textColor,stepName);
          }

          char octave[2];
          octave[0]='0'+edo31Octave(note);
          octave[1]=0;
          ImVec2 octaveSize=mainFont->CalcTextSizeA(smallSize,FLT_MAX,0.0f,octave);
          dl->AddText(mainFont,smallSize,ImVec2(pos.x+hexSize*0.44f-octaveSize.x,pos.y-hexSize*0.62f),textColor,octave);

          int key=note-DIV_EDO31_STEPS*(curOctave-2)-(5*terpstraAnchorQ+2*terpstraAnchorR);
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
          e->setMidiBaseChan(cursor.xCoarse);
          e->synchronized([this,note]() {
            if (!e->autoNoteOn(-1,curIns,note)) failedNoteOn=true;
          });
          if (edit && curWindow!=GUI_WINDOW_INS_LIST && curWindow!=GUI_WINDOW_INS_EDIT) noteInput(note,0);
        }
      }
    }
  } else {
    releaseHeld();
  }
  if (ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows)) curWindow=GUI_WINDOW_TERPSTRA;
  ImGui::End();
}
