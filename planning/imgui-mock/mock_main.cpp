// Headless Dear ImGui -> PNG mockup harness.
// No window, no GL, no SDL: builds the vendored (patched) ImGui, renders scenes
// with a small CPU rasterizer, and writes out/<scene>.png.
//
// Add a scene: write a `static void sceneFoo()` that builds UI with plain ImGui
// calls, then append {"foo", sceneFoo} to kScenes[] below.

#include "imgui.h"

#include <cstdio>
#include <cstring>
#include <cstdint>
#include <cmath>
#include <string>
#include <vector>
#include <sys/stat.h>
#include <zlib.h>

// Furnace's patched imgui.cpp calls these helpers (src/fileutils.h) from its
// ini-saving path. We never save an ini (io.IniFilename=nullptr), so stubs do.
bool moveFiles(const char*, const char*) { return false; }
bool deleteFile(const char*) { return false; }
int  fileExists(const char*) { return 0; }

// ---------------------------------------------------------------------------
// output config
// ---------------------------------------------------------------------------
static const int   FB_W   = 1920;
static const int   FB_H   = 1280;
static const float UI_SCALE = 2.0f;   // everything rendered at 2x -> Retina-ish PNG

// ---------------------------------------------------------------------------
// font atlas texture (CPU copy, RGBA32)
// ---------------------------------------------------------------------------
struct CPUTexture {
  const unsigned char* rgba=nullptr;
  int w=0, h=0;
};
static CPUTexture g_fontTex;
static const ImTextureID FONT_TEX_ID=(ImTextureID)1;

// ---------------------------------------------------------------------------
// software rasterizer
// ---------------------------------------------------------------------------
struct Framebuffer {
  std::vector<unsigned char> px; // RGBA8
  int w, h;
  Framebuffer(int w_, int h_): px((size_t)w_*h_*4), w(w_), h(h_) {}
  void clear(unsigned char r, unsigned char g, unsigned char b) {
    for (size_t i=0; i<px.size(); i+=4) { px[i]=r; px[i+1]=g; px[i+2]=b; px[i+3]=255; }
  }
};

static inline void sampleTex(const CPUTexture& t, float u, float v, int out[4]) {
  int x=(int)(u*(float)t.w); int y=(int)(v*(float)t.h);
  if (x<0) x=0; if (x>=t.w) x=t.w-1;
  if (y<0) y=0; if (y>=t.h) y=t.h-1;
  const unsigned char* p=&t.rgba[((size_t)y*t.w+x)*4];
  out[0]=p[0]; out[1]=p[1]; out[2]=p[2]; out[3]=p[3];
}

static inline float edgeF(const ImVec2& p, const ImVec2& q, float x, float y) {
  return (q.x-p.x)*(y-p.y)-(q.y-p.y)*(x-p.x);
}
// top-left fill rule (y-down, clockwise-on-screen winding): a pixel exactly on
// an edge belongs to the triangle only if that edge is a top or left edge, so
// shared edges (e.g. the diagonal of a quad) rasterize exactly once. This
// matters because ImGui fills like FrameBg are semi-transparent: double-blend
// would leave a visible seam.
static inline bool topLeft(const ImVec2& p, const ImVec2& q) {
  return (p.y==q.y && q.x>p.x) || (q.y<p.y);
}

static void rasterTriangle(Framebuffer& fb, const ImDrawVert& av, const ImDrawVert& bv,
                           const ImDrawVert& cv, const ImVec4& clip, const CPUTexture* tex) {
  const ImDrawVert* A=&av; const ImDrawVert* B=&bv; const ImDrawVert* C=&cv;
  float area=(B->pos.x-A->pos.x)*(C->pos.y-A->pos.y)-(B->pos.y-A->pos.y)*(C->pos.x-A->pos.x);
  if (area==0.0f) return;
  if (area<0.0f) { const ImDrawVert* t=B; B=C; C=t; area=-area; }
  const ImDrawVert& a=*A; const ImDrawVert& b=*B; const ImDrawVert& c=*C;

  // clip-rect-clamped bounding box
  float minxf=std::fmin(a.pos.x, std::fmin(b.pos.x, c.pos.x));
  float minyf=std::fmin(a.pos.y, std::fmin(b.pos.y, c.pos.y));
  float maxxf=std::fmax(a.pos.x, std::fmax(b.pos.x, c.pos.x));
  float maxyf=std::fmax(a.pos.y, std::fmax(b.pos.y, c.pos.y));
  int x0=(int)std::floor(std::fmax(minxf, clip.x));
  int y0=(int)std::floor(std::fmax(minyf, clip.y));
  int x1=(int)std::ceil (std::fmin(maxxf, clip.z));
  int y1=(int)std::ceil (std::fmin(maxyf, clip.w));
  if (x0<0) x0=0; if (y0<0) y0=0;
  if (x1>fb.w) x1=fb.w; if (y1>fb.h) y1=fb.h;
  if (x0>=x1 || y0>=y1) return;

  float inv=1.0f/area;
  bool tlAB=topLeft(a.pos, b.pos), tlBC=topLeft(b.pos, c.pos), tlCA=topLeft(c.pos, a.pos);

  int ar= a.col      &0xFF, ag=(a.col>>8) &0xFF, ab=(a.col>>16)&0xFF, aa=(a.col>>24)&0xFF;
  int br= b.col      &0xFF, bg=(b.col>>8) &0xFF, bb=(b.col>>16)&0xFF, ba=(b.col>>24)&0xFF;
  int cr= c.col      &0xFF, cg=(c.col>>8) &0xFF, cb=(c.col>>16)&0xFF, ca=(c.col>>24)&0xFF;

  for (int y=y0; y<y1; y++) {
    float py=(float)y+0.5f;
    for (int x=x0; x<x1; x++) {
      float pxc=(float)x+0.5f;
      // sub-pixel clip (ImGui clip rects are in float pixels)
      if (pxc<clip.x || pxc>=clip.z || py<clip.y || py>=clip.w) continue;
      float e0=edgeF(a.pos, b.pos, pxc, py);   // opposite c
      float e1=edgeF(b.pos, c.pos, pxc, py);   // opposite a
      float e2=edgeF(c.pos, a.pos, pxc, py);   // opposite b
      if (e0<0.0f || (e0==0.0f && !tlAB)) continue;
      if (e1<0.0f || (e1==0.0f && !tlBC)) continue;
      if (e2<0.0f || (e2==0.0f && !tlCA)) continue;
      float w1=e1*inv, w2=e2*inv, w0=e0*inv;   // w1 -> a, w2 -> b, w0 -> c
      float u=w1*a.uv.x+w2*b.uv.x+w0*c.uv.x;
      float v=w1*a.uv.y+w2*b.uv.y+w0*c.uv.y;
      float rf=w1*(float)ar+w2*(float)br+w0*(float)cr;
      float gf=w1*(float)ag+w2*(float)bg+w0*(float)cg;
      float bf=w1*(float)ab+w2*(float)bb+w0*(float)cb;
      float af=w1*(float)aa+w2*(float)ba+w0*(float)ca;
      int sr=(int)(rf+0.5f), sg=(int)(gf+0.5f), sb=(int)(bf+0.5f), sa=(int)(af+0.5f);
      if (tex) {
        int t[4]; sampleTex(*tex, u, v, t);
        sr=sr*t[0]/255; sg=sg*t[1]/255; sb=sb*t[2]/255; sa=sa*t[3]/255;
      }
      if (sa<=0) continue;
      unsigned char* d=&fb.px[((size_t)y*fb.w+x)*4];
      if (sa>=255) { d[0]=(unsigned char)sr; d[1]=(unsigned char)sg; d[2]=(unsigned char)sb; d[3]=255; }
      else {
        int ia=255-sa;
        d[0]=(unsigned char)((sr*sa+d[0]*ia)/255);
        d[1]=(unsigned char)((sg*sa+d[1]*ia)/255);
        d[2]=(unsigned char)((sb*sa+d[2]*ia)/255);
        d[3]=255;
      }
    }
  }
}

static void rasterDrawData(Framebuffer& fb, ImDrawData* dd) {
  for (int li=0; li<dd->CmdListsCount; li++) {
    const ImDrawList* dl=dd->CmdLists[li];
    const ImDrawVert* vtx=dl->VtxBuffer.Data;
    const ImDrawIdx*  idx=dl->IdxBuffer.Data;
    for (int ci=0; ci<dl->CmdBuffer.Size; ci++) {
      const ImDrawCmd& cmd=dl->CmdBuffer[ci];
      if (cmd.UserCallback) continue;
      const CPUTexture* tex=(cmd.GetTexID()==FONT_TEX_ID)?&g_fontTex:nullptr;
      const ImDrawVert* base=vtx+cmd.VtxOffset;
      for (unsigned int e=0; e+2<cmd.ElemCount; e+=3) {
        const ImDrawVert& a=base[idx[cmd.IdxOffset+e  ]];
        const ImDrawVert& b=base[idx[cmd.IdxOffset+e+1]];
        const ImDrawVert& c=base[idx[cmd.IdxOffset+e+2]];
        rasterTriangle(fb, a, b, c, cmd.ClipRect, tex);
      }
    }
  }
}

// ---------------------------------------------------------------------------
// PNG writer (zlib)
// ---------------------------------------------------------------------------
static void put32(std::vector<unsigned char>& v, uint32_t x) {
  v.push_back((unsigned char)(x>>24)); v.push_back((unsigned char)(x>>16));
  v.push_back((unsigned char)(x>>8));  v.push_back((unsigned char)x);
}
static void chunk(FILE* f, const char* type, const unsigned char* data, uint32_t len) {
  std::vector<unsigned char> hdr; put32(hdr, len);
  fwrite(hdr.data(), 1, 4, f);
  fwrite(type, 1, 4, f);
  if (len) fwrite(data, 1, len, f);
  uint32_t crc=(uint32_t)crc32(0L, Z_NULL, 0);
  crc=(uint32_t)crc32(crc, (const Bytef*)type, 4);
  if (len) crc=(uint32_t)crc32(crc, data, len);
  std::vector<unsigned char> tail; put32(tail, crc);
  fwrite(tail.data(), 1, 4, f);
}
static bool writePNG(const char* path, const unsigned char* rgba, int w, int h) {
  // raw stream: per scanline, filter byte 0 + RGBA pixels
  std::vector<unsigned char> raw; raw.reserve((size_t)h*((size_t)w*4+1));
  for (int y=0; y<h; y++) {
    raw.push_back(0);
    raw.insert(raw.end(), rgba+(size_t)y*w*4, rgba+(size_t)(y+1)*w*4);
  }
  uLongf zlen=compressBound((uLong)raw.size());
  std::vector<unsigned char> z(zlen);
  if (compress2(z.data(), &zlen, raw.data(), (uLong)raw.size(), 6)!=Z_OK) return false;
  FILE* f=fopen(path, "wb");
  if (!f) return false;
  static const unsigned char sig[8]={0x89,'P','N','G',0x0D,0x0A,0x1A,0x0A};
  fwrite(sig, 1, 8, f);
  std::vector<unsigned char> ihdr; put32(ihdr, (uint32_t)w); put32(ihdr, (uint32_t)h);
  ihdr.push_back(8); ihdr.push_back(6); ihdr.push_back(0); ihdr.push_back(0); ihdr.push_back(0);
  chunk(f, "IHDR", ihdr.data(), (uint32_t)ihdr.size());
  chunk(f, "IDAT", z.data(), (uint32_t)zlen);
  chunk(f, "IEND", nullptr, 0);
  fclose(f);
  return true;
}

// ---------------------------------------------------------------------------
// scenes
// ---------------------------------------------------------------------------
typedef void (*SceneFn)();
struct Scene { const char* name; SceneFn fn; };

// ---- Justify Intervals mockups --------------------------------------------

static const ImVec4 COL_WARN(1.00f, 0.80f, 0.25f, 1.00f);
static const ImVec4 COL_OK  (0.55f, 0.90f, 0.55f, 1.00f);
static const ImVec4 COL_BAD (1.00f, 0.45f, 0.40f, 1.00f);
static const ImVec4 COL_DIM (0.55f, 0.55f, 0.55f, 1.00f);

// shared scope block (mirrors Find/Replace's QueryLimits table)
static void drawScopeBlock() {
  if (ImGui::BeginTable("Scope", 3, ImGuiTableFlags_BordersOuter)) {
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::TextUnformatted("Scope:");
    static int range=1;
    ImGui::RadioButton("Song", &range, 0);
    ImGui::RadioButton("Selection", &range, 1);
    ImGui::RadioButton("Pattern", &range, 2);

    ImGui::TableSetColumnIndex(1);
    ImGui::TextUnformatted("Channels:");
    static int chFrom=1, chTo=3;
    const char* chans[]={"1: FM 1", "2: FM 2", "3: FM 3", "4: FM 4", "5: FM 5", "6: FM 6"};
    ImGui::SetNextItemWidth(240.0f);
    ImGui::Combo("From", &chFrom, chans, 6);
    ImGui::SetNextItemWidth(240.0f);
    ImGui::Combo("To", &chTo, chans, 6);

    ImGui::TableSetColumnIndex(2);
    ImGui::TextUnformatted("Effect column:");
    static int fxPolicy=1;
    ImGui::RadioButton("use free slot only", &fxPolicy, 0);
    ImGui::RadioButton("widen if full", &fxPolicy, 1);
    ImGui::RadioButton("skip note and report", &fxPolicy, 2);
    ImGui::EndTable();
  }
}

// Tab 1 (setup view): key, tuning profile, targets table, options
static void drawJustifyTabSetup() {
  static int rootNote=0;   // C
  static int rootOct=4;
  const char* names31[]={"C","Dbb","C#","Db","Cx","D","Ebb","D#","Eb","Dx","E","Fb","E#","F","Gbb","F#","Gb","Fx","G","Abb","G#","Ab","Gx","A","Bbb","A#","Bb","Ax","B","Cb","B#"};
  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted("Key / root:");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(160.0f);
  ImGui::Combo("##Root", &rootNote, names31, 31);
  ImGui::SameLine();
  static bool allOct=true;
  ImGui::Checkbox("all octaves", &allOct);
  ImGui::SameLine(0.0f, 40.0f);
  ImGui::TextUnformatted("Profile:");
  ImGui::SameLine();
  static int preset=1;
  const char* presets[]={"Fifths only", "5-limit", "7-limit", "3-limit (Pythagorean)", "Custom"};
  ImGui::SetNextItemWidth(360.0f);
  ImGui::Combo("##Preset", &preset, presets, 5);
  ImGui::SameLine();
  if (ImGui::Button("Reset profile")) {}

  ImGui::TextColored(COL_WARN, "(!) linear pitch is OFF in this song - E5xx will not be cents-accurate. Enable it in Compatibility Flags.");

  ImGui::Separator();
  ImGui::TextUnformatted("Interval targets (relative to key):");

  struct TRow {
    bool on;
    const char* steps; const char* sp; const char* name;
    const char* tc; const char* target; const char* jc; const char* corr; const char* fx;
    bool ambig; bool anchor;
  };
  static TRow rows[]={
    {true,  " 0", "C",  "unison (anchor)",   "   0.00", "1:1",  "   0.00", " 0.0", "E580", false, true},
    {false, " 5", "D",  "whole tone",        " 193.55", "9:8",  " 203.91", "+10.4", "E5A2", true,  false},
    {true,  " 8", "Eb", "minor third",       " 309.68", "6:5",  " 315.64", " +6.0", "E594", false, false},
    {true,  "10", "E",  "major third",       " 387.10", "5:4",  " 386.31", " -0.8", "E57D", false, false},
    {true,  "13", "F",  "fourth",            " 503.23", "4:3",  " 498.04", " -5.2", "E56F", false, false},
    {true,  "18", "G",  "fifth",             " 696.77", "3:2",  " 701.96", " +5.2", "E591", false, false},
    {true,  "21", "Ab", "minor sixth",       " 812.90", "8:5",  " 813.69", " +0.8", "E583", false, false},
    {true,  "23", "A",  "major sixth",       " 890.32", "5:3",  " 884.36", " -6.0", "E56C", false, false},
    {false, "25", "A#", "harmonic seventh",  " 967.74", "7:4",  " 968.83", " +1.1", "E584", false, false},
    {false, "26", "Bb", "minor seventh",     "1006.45", "16:9", " 996.09", "-10.4", "E55E", true,  false},
    {false, "28", "B",  "major seventh",     "1083.87", "15:8", "1088.27", " +4.4", "E58F", false, false},
  };
  if (ImGui::BeginTable("Targets", 9, ImGuiTableFlags_Borders|ImGuiTableFlags_RowBg)) {
    ImGui::TableSetupColumn("on", ImGuiTableColumnFlags_WidthFixed, 60.0f);
    ImGui::TableSetupColumn("steps", ImGuiTableColumnFlags_WidthFixed, 120.0f);
    ImGui::TableSetupColumn("degree", ImGuiTableColumnFlags_WidthFixed, 130.0f);
    ImGui::TableSetupColumn("interval", ImGuiTableColumnFlags_WidthFixed, 300.0f);
    ImGui::TableSetupColumn("31-EDO c");
    ImGui::TableSetupColumn("target", ImGuiTableColumnFlags_WidthFixed, 190.0f);
    ImGui::TableSetupColumn("just c");
    ImGui::TableSetupColumn("corr c");
    ImGui::TableSetupColumn("E5xx");
    ImGui::TableHeadersRow();
    int id=0;
    for (TRow& r : rows) {
      ImGui::TableNextRow();
      ImGui::PushID(id++);
      ImGui::TableSetColumnIndex(0);
      if (r.anchor) ImGui::TextDisabled("--"); else ImGui::Checkbox("##on", &r.on);
      ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(r.steps);
      ImGui::TableSetColumnIndex(2); ImGui::TextUnformatted(r.sp);
      ImGui::TableSetColumnIndex(3);
      if (r.anchor) ImGui::TextDisabled("%s", r.name); else ImGui::TextUnformatted(r.name);
      ImGui::TableSetColumnIndex(4); ImGui::TextUnformatted(r.tc);
      ImGui::TableSetColumnIndex(5);
      if (r.ambig) {
        static int amb0=0, amb1=0;
        const char* wt[]={"9:8", "10:9"};
        const char* m7[]={"16:9", "9:5"};
        ImGui::SetNextItemWidth(170.0f);
        if (r.steps[1]=='5' && r.steps[0]==' ') ImGui::Combo("##amb", &amb0, wt, 2);
        else ImGui::Combo("##amb", &amb1, m7, 2);
      } else {
        ImGui::TextUnformatted(r.target);
      }
      ImGui::TableSetColumnIndex(6); ImGui::TextUnformatted(r.jc);
      ImGui::TableSetColumnIndex(7);
      float cv=(float)atof(r.corr);
      if (r.anchor) ImGui::TextDisabled("%s", r.corr);
      else if (cv>5.0f||cv<-5.0f) ImGui::TextColored(COL_BAD, "%s", r.corr);
      else ImGui::TextColored(COL_OK, "%s", r.corr);
      ImGui::TableSetColumnIndex(8);
      if (r.anchor) ImGui::TextDisabled("%s", r.fx); else ImGui::TextUnformatted(r.fx);
      ImGui::PopID();
    }
    ImGui::EndTable();
  }
  ImGui::TextDisabled("unchecked degrees stay tempered; other degrees (2, 3, 15...) hidden - expand with \"show all 31\"");

  ImGui::Separator();
  drawScopeBlock();

  static bool resetUntouched=true, resetAfter=true;
  ImGui::Checkbox("reset pitch (E580) on untouched notes in scope", &resetUntouched);
  ImGui::Checkbox("reset pitch on first note after scope (per channel)", &resetAfter);
  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted("existing E5xx in scope:");
  ImGui::SameLine();
  static int existing=0;
  const char* exModes[]={"overwrite", "compound (add correction)", "skip and report"};
  ImGui::SetNextItemWidth(420.0f);
  ImGui::Combo("##Existing", &existing, exModes, 3);

  ImGui::Separator();
  if (ImGui::Button("Preview")) {}
  ImGui::SameLine();
  if (ImGui::Button("Apply")) {}
  ImGui::SameLine();
  ImGui::TextDisabled("apply writes E5xx effects - one undo step");
}

// Tab 1 (preview view): scan results, like Find's results list
static void drawJustifyTabPreview() {
  ImGui::TextUnformatted("Preview: key C (all octaves), profile 5-limit, scope Selection (order 02, rows 00-0F, ch 2-4)");
  ImGui::Separator();
  struct PRow {
    bool on;
    const char* order; const char* row; const char* chan; const char* note;
    const char* deg; const char* interval; const char* corr; const char* fx; const char* status; int st;
  };
  static PRow rows[]={
    {true,  "02", "00", "2: FM 2", "G-4",  "18", "fifth",            "+5.2", "E591", "retune",              0},
    {true,  "02", "00", "3: FM 3", "E-5",  "10", "major third",      "-0.8", "E57D", "retune",              0},
    {true,  "02", "00", "4: FM 4", "C-5",  " 0", "unison",           " 0.0", "E580", "anchor: reset",       2},
    {true,  "02", "02", "2: FM 2", "F-4",  "13", "fourth",           "-5.2", "E56F", "retune",              0},
    {true,  "02", "04", "2: FM 2", "G-4",  "18", "fifth",            "+5.2", "E591", "retune",              0},
    {true,  "02", "04", "4: FM 4", "D-5",  " 5", "whole tone",       " -- ", "----", "no target: reset",    2},
    {true,  "02", "06", "3: FM 3", "A-4",  "23", "major sixth",      "-6.0", "E56C", "retune",              0},
    {false, "02", "08", "2: FM 2", "G-4",  "18", "fifth",            "+5.2", "E591", "skip: no free FX col", 1},
    {true,  "02", "0A", "3: FM 3", "Bb4",  "26", "minor seventh",    " -- ", "----", "no target: reset",    2},
    {true,  "02", "0C", "2: FM 2", "E-4",  "10", "major third",      "-0.8", "E57D", "retune",              0},
    {true,  "02", "0E", "4: FM 4", "A#4",  "25", "harmonic seventh", "+1.1", "E584", "retune",              0},
    {true,  "02", "0F", "2: FM 2", "G-4",  "18", "fifth",            "+5.2", "E591", "retune",              0},
  };
  if (ImGui::BeginTable("Preview", 10, ImGuiTableFlags_Borders|ImGuiTableFlags_RowBg)) {
    ImGui::TableSetupColumn("on", ImGuiTableColumnFlags_WidthFixed, 60.0f);
    ImGui::TableSetupColumn("order");
    ImGui::TableSetupColumn("row");
    ImGui::TableSetupColumn("channel");
    ImGui::TableSetupColumn("note");
    ImGui::TableSetupColumn("deg", ImGuiTableColumnFlags_WidthFixed, 80.0f);
    ImGui::TableSetupColumn("interval", ImGuiTableColumnFlags_WidthFixed, 300.0f);
    ImGui::TableSetupColumn("corr c", ImGuiTableColumnFlags_WidthFixed, 120.0f);
    ImGui::TableSetupColumn("write", ImGuiTableColumnFlags_WidthFixed, 110.0f);
    ImGui::TableSetupColumn("status", ImGuiTableColumnFlags_WidthFixed, 430.0f);
    ImGui::TableHeadersRow();
    int id=0;
    for (PRow& r : rows) {
      ImGui::TableNextRow();
      ImGui::PushID(id++);
      ImGui::TableSetColumnIndex(0); ImGui::Checkbox("##on", &r.on);
      ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(r.order);
      ImGui::TableSetColumnIndex(2); ImGui::TextUnformatted(r.row);
      ImGui::TableSetColumnIndex(3); ImGui::TextUnformatted(r.chan);
      ImGui::TableSetColumnIndex(4); ImGui::TextUnformatted(r.note);
      ImGui::TableSetColumnIndex(5); ImGui::TextUnformatted(r.deg);
      ImGui::TableSetColumnIndex(6); ImGui::TextUnformatted(r.interval);
      ImGui::TableSetColumnIndex(7); ImGui::TextUnformatted(r.corr);
      ImGui::TableSetColumnIndex(8); ImGui::TextUnformatted(r.fx);
      ImGui::TableSetColumnIndex(9);
      if (r.st==1) ImGui::TextColored(COL_BAD, "%s", r.status);
      else if (r.st==2) ImGui::TextColored(COL_DIM, "%s", r.status);
      else ImGui::TextUnformatted(r.status);
      ImGui::PopID();
    }
    ImGui::EndTable();
  }
  ImGui::TextUnformatted("12 notes in scope - 8 retuned, 3 reset, 1 skipped (no free effect column)");
  ImGui::TextColored(COL_WARN, "1 skipped note: widen effect columns on channel 2, or change the effect column policy.");
  ImGui::Separator();
  if (ImGui::Button("Apply")) {}
  ImGui::SameLine();
  if (ImGui::Button("Back")) {}
  ImGui::SameLine();
  if (ImGui::Button("Re-scan")) {}
}

// Tab 2: key-aware find & replace of note spellings
static void drawRespellTab() {
  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted("Key:");
  ImGui::SameLine();
  static int rootNote=0;
  const char* names31[]={"C","Dbb","C#","Db","Cx","D","Ebb","D#","Eb","Dx","E","Fb","E#","F","Gbb","F#","Gb","Fx","G","Abb","G#","Ab","Gx","A","Bbb","A#","Bb","Ax","B","Cb","B#"};
  ImGui::SetNextItemWidth(160.0f);
  ImGui::Combo("##Root", &rootNote, names31, 31);
  ImGui::SameLine(0.0f, 40.0f);
  ImGui::TextUnformatted("Preset:");
  ImGui::SameLine();
  static int preset=0;
  const char* presets[]={"dominant 7th -> harmonic 7th", "respell sharps as flats", "custom"};
  ImGui::SetNextItemWidth(520.0f);
  ImGui::Combo("##Preset", &preset, presets, 3);

  ImGui::Separator();
  ImGui::TextUnformatted("Rules (applied top to bottom; a note matches at most one rule):");
  struct RRow { bool on; const char* find; const char* mode; const char* repl; const char* note; };
  static RRow rows[]={
    {true,  "deg 26 (Bb)", "relative to key", "deg 25 (A#)", "min 7th -> harm 7th (-38.7c)"},
    {true,  "C#",          "absolute",        "Db",          "respell up one diesis (+38.7c)"},
    {false, "F#",          "absolute",        "Gb",          "respell up one diesis (+38.7c)"},
  };
  if (ImGui::BeginTable("Rules", 6, ImGuiTableFlags_Borders|ImGuiTableFlags_RowBg)) {
    ImGui::TableSetupColumn("on", ImGuiTableColumnFlags_WidthFixed, 60.0f);
    ImGui::TableSetupColumn("find", ImGuiTableColumnFlags_WidthFixed, 330.0f);
    ImGui::TableSetupColumn("match mode", ImGuiTableColumnFlags_WidthFixed, 330.0f);
    ImGui::TableSetupColumn("replace with", ImGuiTableColumnFlags_WidthFixed, 330.0f);
    ImGui::TableSetupColumn("effect", ImGuiTableColumnFlags_WidthFixed, 640.0f);
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 70.0f);
    ImGui::TableHeadersRow();
    int id=0;
    for (RRow& r : rows) {
      ImGui::TableNextRow();
      ImGui::PushID(id++);
      ImGui::TableSetColumnIndex(0); ImGui::Checkbox("##on", &r.on);
      ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(r.find);
      ImGui::TableSetColumnIndex(2); ImGui::TextUnformatted(r.mode);
      ImGui::TableSetColumnIndex(3); ImGui::TextUnformatted(r.repl);
      ImGui::TableSetColumnIndex(4); ImGui::TextDisabled("%s", r.note);
      ImGui::TableSetColumnIndex(5);
      if (ImGui::Button("-")) {}
      ImGui::PopID();
    }
    ImGui::EndTable();
  }
  if (ImGui::Button("+ Add rule")) {}
  ImGui::SameLine();
  ImGui::TextDisabled("octave is preserved; substitution moves the slot, it does not write E5xx");

  ImGui::Separator();
  drawScopeBlock();

  ImGui::Separator();
  if (ImGui::Button("Preview")) {}
  ImGui::SameLine();
  if (ImGui::Button("Apply")) {}
}

// Tab 3: report-only analysis
static void drawIdentifyTab() {
  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted("Analyze against key:");
  ImGui::SameLine();
  static int rootNote=0;
  const char* names31[]={"C","Dbb","C#","Db","Cx","D","Ebb","D#","Eb","Dx","E","Fb","E#","F","Gbb","F#","Gb","Fx","G","Abb","G#","Ab","Gx","A","Bbb","A#","Bb","Ax","B","Cb","B#"};
  ImGui::SetNextItemWidth(160.0f);
  ImGui::Combo("##Root", &rootNote, names31, 31);
  ImGui::SameLine();
  if (ImGui::Button("Scan")) {}
  ImGui::SameLine();
  ImGui::TextDisabled("read-only: no pattern data is modified");

  ImGui::Separator();
  ImGui::TextUnformatted("Interval classes found in scope (162 notes, order 00-05, ch 1-4):");
  struct IRow { const char* deg; const char* name; const char* count; const char* err; const char* verdict; int v; };
  static IRow rows[]={
    {"18", "fifth",            "42", "-5.2", "audibly flat",           1},
    {"10", "major third",      "38", "+0.8", "near-just",              0},
    {"13", "fourth",           "17", "+5.2", "audibly sharp",          1},
    {" 5", "whole tone",       "14", "-10.4 / +11.1", "ambiguous (9:8 vs 10:9)", 2},
    {" 8", "minor third",      "12", "-6.0", "audibly flat",           1},
    {"23", "major sixth",      " 9", "+6.0", "audibly sharp",          1},
    {"25", "harmonic seventh", " 7", "-1.1", "near-just",              0},
    {"26", "minor seventh",    " 6", "+10.4 / -11.1", "ambiguous (16:9 vs 9:5)", 2},
    {"28", "major seventh",    " 4", "-4.4", "slightly flat",          1},
    {" 9", "neutral third",    " 2", "+1.0", "near-just",              0},
  };
  if (ImGui::BeginTable("Classes", 5, ImGuiTableFlags_Borders|ImGuiTableFlags_RowBg)) {
    ImGui::TableSetupColumn("deg");
    ImGui::TableSetupColumn("interval vs key");
    ImGui::TableSetupColumn("count");
    ImGui::TableSetupColumn("31-EDO error c");
    ImGui::TableSetupColumn("assessment");
    ImGui::TableHeadersRow();
    for (IRow& r : rows) {
      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(r.deg);
      ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(r.name);
      ImGui::TableSetColumnIndex(2); ImGui::TextUnformatted(r.count);
      ImGui::TableSetColumnIndex(3); ImGui::TextUnformatted(r.err);
      ImGui::TableSetColumnIndex(4);
      if (r.v==1) ImGui::TextColored(COL_BAD, "%s", r.verdict);
      else if (r.v==2) ImGui::TextColored(COL_WARN, "%s", r.verdict);
      else ImGui::TextColored(COL_OK, "%s", r.verdict);
    }
    ImGui::EndTable();
  }
  ImGui::Separator();
  ImGui::TextUnformatted("Summary:");
  ImGui::Bullet(); ImGui::TextUnformatted("59 of 162 notes (36%) form a tempered fifth or fourth against the key - the main source of beating.");
  ImGui::Bullet(); ImGui::TextUnformatted("Thirds and harmonic sevenths are within 1.1c: leave them tempered.");
  ImGui::Bullet(); ImGui::TextColored(COL_WARN, "20 notes are ambiguous (whole tone / minor seventh): pick a target ratio in the Justify tab.");
  ImGui::Bullet(); ImGui::TextUnformatted("Suggestion: profile \"Fifths only\", then audition; add 6:5 / 5:3 if the minor thirds still beat.");
}

static void drawJustifyWindow(int selTab, bool previewView) {
  ImGui::SetNextWindowPos(ImVec2(40.0f, 30.0f), ImGuiCond_Always);
  ImGui::SetNextWindowSize(ImVec2(1840.0f, 1210.0f), ImGuiCond_Always);
  static bool open=true;
  ImGui::Begin("Justify Intervals", &open, ImGuiWindowFlags_NoCollapse);
  if (ImGui::BeginTabBar("JustifyTabs")) {
    if (ImGui::BeginTabItem("Justify", nullptr, selTab==0?ImGuiTabItemFlags_SetSelected:0)) {
      if (previewView) drawJustifyTabPreview(); else drawJustifyTabSetup();
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Respell", nullptr, selTab==1?ImGuiTabItemFlags_SetSelected:0)) {
      drawRespellTab();
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Identify", nullptr, selTab==2?ImGuiTabItemFlags_SetSelected:0)) {
      drawIdentifyTab();
      ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
  }
  ImGui::End();
}

static void sceneJustifySetup()   { drawJustifyWindow(0, false); }
static void sceneJustifyPreview() { drawJustifyWindow(0, true);  }
static void sceneRespell()        { drawJustifyWindow(1, false); }
static void sceneIdentify()       { drawJustifyWindow(2, false); }

static const Scene kScenes[]={
  {"justify_setup",   sceneJustifySetup},
  {"justify_preview", sceneJustifyPreview},
  {"respell",         sceneRespell},
  {"identify",        sceneIdentify},
};

// ---------------------------------------------------------------------------
// harness
// ---------------------------------------------------------------------------
static bool renderScene(const Scene& sc, const std::string& outDir) {
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io=ImGui::GetIO();
  io.DisplaySize=ImVec2((float)FB_W, (float)FB_H);
  io.DeltaTime=1.0f/60.0f;
  io.IniFilename=nullptr;   // no imgui.ini
  io.LogFilename=nullptr;

  ImFontConfig fcfg;
  fcfg.SizePixels=13.0f*UI_SCALE;
  io.Fonts->AddFontDefault(&fcfg);
  unsigned char* texPixels=nullptr; int texW=0, texH=0;
  io.Fonts->GetTexDataAsRGBA32(&texPixels, &texW, &texH);
  io.Fonts->SetTexID(FONT_TEX_ID);
  g_fontTex.rgba=texPixels; g_fontTex.w=texW; g_fontTex.h=texH;

  ImGui::StyleColorsDark();
  ImGui::GetStyle().ScaleAllSizes(UI_SCALE);

  // two frames: layout/sizing settles on frame 2
  for (int frame=0; frame<2; frame++) {
    ImGui::NewFrame();
    sc.fn();
    ImGui::Render();
  }

  Framebuffer fb(FB_W, FB_H);
  fb.clear(15, 15, 15);   // backdrop outside windows
  rasterDrawData(fb, ImGui::GetDrawData());

  std::string path=outDir+"/"+sc.name+".png";
  bool ok=writePNG(path.c_str(), fb.px.data(), fb.w, fb.h);
  fprintf(ok?stdout:stderr, "%s %s (%dx%d, font atlas %dx%d)\n",
          ok?"wrote":"FAILED to write", path.c_str(), fb.w, fb.h, texW, texH);

  ImGui::DestroyContext();
  g_fontTex=CPUTexture();
  return ok;
}

int main(int argc, char** argv) {
  std::string outDir=(argc>1)?argv[1]:"out";
  mkdir(outDir.c_str(), 0755);
  bool allOk=true;
  for (const Scene& sc : kScenes) allOk&=renderScene(sc, outDir);
  return allOk?0:1;
}
