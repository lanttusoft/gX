//opencode: gX shell synthesizer — on-screen keyboard + parameter panel over
//   the Synth_* tour API, drawn with the overlay paint list (topmost) on the
//   synthvis scene so the scope visuals stay live. Sound starts muted
//   (gX default): click SOUND, or launch with GX_SYNTH_MUTE=0.
//   Mouse: click/drag the keys and sliders. Physical keys: Z S X D V C G B
//   H N J M (lower octave) and Q 2 W 3 E R 5 T 6 Y 7 U (upper), COMMA/PERIOD
//   shift the octave.
//
//   WAVETABLE strip: cycles slot 0 through four generated shapes
//   (sine/square/saw/triangle) and three shipped file tables
//   (resources/audio/wavetable/*.f32 — raw float32, regenerate with
//   scripts/gen_wavetables.py), previews whatever loaded and switches the
//   oscillator to WAVETABLE; PRE/PT buttons save/load the full engine preset
//   (default save/synth_demo_preset.json — the gate seeds
//   ScriptSetString("synth_preset_path", <scratch>) for its round trip) and
//   the in-memory patch bank (SAVE captures the current defaults, ARM routes
//   new notes through Synth_NoteOnPatch).
//
//   Run:  ./build/scripting/gX_build --module build/scripting/gx_shell.so \
//             --script resources/script/synth/synth.as
//   (the boot scan only reads resources/script/*.as, so the demo stays inert
//   unless started explicitly).
//
//   The synth gate runs this file with state key "synth_selftest" = 1: it
//   exercises notes, mute, hit-testing, the wavetable readback, the patch bank
//   and the preset file round trip, prints markers and exits.

// ---- state -----------------------------------------------------------------

float gW;
float gH;

// slider/button table — one entry per control, values normalized 0..1
int gCtrlCount = 26;
float[] gVal;
float[] gCtrlX;
float[] gCtrlY;
float[] gCtrlW;
int[] gCtrlKind; // 0 slider, 1 button

// control ids
int C_ATK = 0;
int C_DEC = 1;
int C_SUS = 2;
int C_REL = 3;
int C_CUT = 4;
int C_RES = 5;
int C_FMODE = 6;
int C_LRATE = 7;
int C_LDEP = 8;
int C_LTGT = 9;
int C_FMAMT = 10;
int C_FMALG = 11;
int C_DTIME = 12;
int C_DFB = 13;
int C_DMIX = 14;
int C_ARATE = 15;
int C_AGATE = 16;
int C_AMODE = 17;
int C_VOL = 18;
int C_SOUND = 19;
int C_WAVE = 20;
int C_WT = 21;
int C_PSAVE = 22;
int C_PLOAD = 23;
int C_PTSAVE = 24;
int C_PTLOAD = 25;

int gWaveIdx = 0;
int gFilterIdx = 0; // 0 none, 1 lp, 2 hp, 3 bp
int gLfoIdx = 0;    // 0 pitch, 1 amp, 2 filter
int gFmIdx = 0;     // 0 off, 1 simple, 2 serial, 3 parallel, 4 feedback
int gArpIdx = 0;    // 0 off, 1 up, 2 down, 3 updown, 4 random
int gOctave = 3;

// wavetable strip: slot 0 table + preset/patch state
int gWtIdx = 2;       // 0..3 generated shapes, 4..6 shipped file tables
bool gWtFileOk = false;
string[] gWtFiles;
int gPatchArm = -1;   // >= 0 routes new notes through Synth_NoteOnPatch
bool gPresetSaved = false;
bool gPresetLoaded = false;
string gStatus = "";

// keyboard geometry (white keys = 14, two octaves)
float gKeyX;
float gKeyY;
float gKeyW;
float gKeyH;

// wavetable/preset strip geometry
float gStripY;
float gWtX;
float gWtW;
float gSx;
float gSw;

// scratch rect (filled by WhiteRect/BlackRect)
float gRcX;
float gRcY;
float gRcW;
float gRcH;

int[] gSlotSemi;
int[] gVoices;   // voice slot per key (>=0 sounding, -1 free)
int gMouseKey = -1;
int gDragCtrl = -1;

float[] gBaseFreq; // C4..B4

// ---- helpers ---------------------------------------------------------------

void InitTables()
{
    gBaseFreq.resize(12);
    gSlotSemi.resize(24);
    gVoices.resize(24);
    gVal.resize(32);
    gCtrlX.resize(32);
    gCtrlY.resize(32);
    gCtrlW.resize(32);
    gCtrlKind.resize(32);

    gBaseFreq[0] = 261.63f;
    gBaseFreq[1] = 277.18f;
    gBaseFreq[2] = 293.66f;
    gBaseFreq[3] = 311.13f;
    gBaseFreq[4] = 329.63f;
    gBaseFreq[5] = 349.23f;
    gBaseFreq[6] = 369.99f;
    gBaseFreq[7] = 392.00f;
    gBaseFreq[8] = 415.30f;
    gBaseFreq[9] = 440.00f;
    gBaseFreq[10] = 466.16f;
    gBaseFreq[11] = 493.88f;

    int[] semi = {0, 2, 4, 5, 7, 9, 11, 12, 14, 16, 17, 19, 21, 23,
                  1, 3, 6, 8, 10, 13, 15, 18, 20, 22};
    for (int i = 0; i < 24; i++) {
        gSlotSemi[i] = semi[i];
        gVoices[i] = -1;
    }

    // shipped wavetable files (raw float32, 1024 samples; scripts/gen_wavetables.py)
    gWtFiles.resize(3);
    gWtFiles[0] = "resources/audio/wavetable/warm_saw.f32";
    gWtFiles[1] = "resources/audio/wavetable/organ.f32";
    gWtFiles[2] = "resources/audio/wavetable/bell.f32";
}

int WaveCount() { return 6; }
int WaveAt(int i)
{
    if (i == 1) return SYNTH_WAVE_SQUARE;
    if (i == 2) return SYNTH_WAVE_SAWTOOTH;
    if (i == 3) return SYNTH_WAVE_TRIANGLE;
    if (i == 4) return SYNTH_WAVE_NOISE;
    if (i == 5) return SYNTH_WAVE_WAVETABLE;
    return SYNTH_WAVE_SINE;
}

string WaveName(int i)
{
    if (i == 1) return "SQUARE";
    if (i == 2) return "SAW";
    if (i == 3) return "TRI";
    if (i == 4) return "NOISE";
    if (i == 5) return "WAVETABLE";
    return "SINE";
}

string FilterName(int i)
{
    if (i == 1) return "LOWPASS";
    if (i == 2) return "HIGHPASS";
    if (i == 3) return "BANDPASS";
    return "FILTER OFF";
}

int FilterModeAt(int i)
{
    if (i == 1) return SYNTH_FILTER_LOWPASS;
    if (i == 2) return SYNTH_FILTER_HIGHPASS;
    if (i == 3) return SYNTH_FILTER_BANDPASS;
    return SYNTH_FILTER_NONE;
}

string LfoName(int i)
{
    if (i == 1) return "AMP";
    if (i == 2) return "FILTER";
    return "PITCH";
}

int LfoTargetAt(int i)
{
    if (i == 1) return SYNTH_LFO_AMP;
    if (i == 2) return SYNTH_LFO_FILTER;
    return SYNTH_LFO_PITCH;
}

string FmName(int i)
{
    if (i == 1) return "SIMPLE";
    if (i == 2) return "SERIAL";
    if (i == 3) return "PARALLEL";
    if (i == 4) return "FEEDBACK";
    return "FM OFF";
}

int FmAlgoAt(int i)
{
    if (i == 1) return SYNTH_FM_SIMPLE;
    if (i == 2) return SYNTH_FM_SERIAL;
    if (i == 3) return SYNTH_FM_PARALLEL;
    if (i == 4) return SYNTH_FM_FEEDBACK;
    return SYNTH_FM_OFF;
}

string ArpName(int i)
{
    if (i == 1) return "UP";
    if (i == 2) return "DOWN";
    if (i == 3) return "UP-DOWN";
    if (i == 4) return "RANDOM";
    return "ARP OFF";
}

int ArpModeAt(int i)
{
    if (i == 1) return SYNTH_ARP_UP;
    if (i == 2) return SYNTH_ARP_DOWN;
    if (i == 3) return SYNTH_ARP_UPDOWN;
    if (i == 4) return SYNTH_ARP_RANDOM;
    return SYNTH_ARP_OFF;
}

float Clamp01(float v)
{
    if (v < 0.0f) return 0.0f;
    if (v > 1.0f) return 1.0f;
    return v;
}

int ClampIdx(int v, int n)
{
    if (v < 0) return 0;
    if (v >= n) return n - 1;
    return v;
}

// demo filter index <-> engine mode (mode -1 = none is index 0)
int FilterIdxFromMode(int mode)
{
    if (mode < 0) return 0;
    if (mode > 2) return 3;
    return mode + 1;
}

float SlotFreq(int slot)
{
    int semi = gSlotSemi[slot];
    int oct = gOctave + semi / 12 - 4;
    float f = gBaseFreq[semi % 12];
    while (oct > 0) { f *= 2.0f; oct--; }
    while (oct < 0) { f *= 0.5f; oct++; }
    return f;
}

string NoteName(int slot)
{
    int[] names = {0, 0, 1, 1, 2, 3, 3, 4, 4, 5, 5, 6};
    string[] white = {"C", "D", "E", "F", "G", "A", "B"};
    int semi = gSlotSemi[slot];
    int oct = gOctave + semi / 12;
    return "" + white[names[semi % 12]] + oct;
}

// ---- sound -----------------------------------------------------------------

void ApplySound()
{
    Synth_SetDefaultWaveform(WaveAt(gWaveIdx));
    Synth_SetDefaultADSR(0.002f + gVal[C_ATK] * 0.5f,
                         0.01f + gVal[C_DEC] * 0.99f,
                         gVal[C_SUS],
                         0.01f + gVal[C_REL] * 0.99f);
    Synth_SetDefaultFilter(FilterModeAt(gFilterIdx),
                           0.02f + gVal[C_CUT] * 0.96f,
                           gVal[C_RES] * 0.9f);
    Synth_SetDefaultLFO(gVal[C_LRATE] * 12.0f, gVal[C_LDEP],
                        SYNTH_WAVE_SINE, LfoTargetAt(gLfoIdx));
    Synth_SetDefaultFM(FmAlgoAt(gFmIdx), 2.0f, gVal[C_FMAMT], 1.0f, 0.0f, 0.0f);
    Synth_SetDelay(0.02f + gVal[C_DTIME] * 0.98f, gVal[C_DFB] * 0.9f,
                   gVal[C_DMIX], 0.3f);
    Synth_SetDelayEnable(gVal[C_DMIX] > 0.02f);
    Synth_Arp_SetMode(ArpModeAt(gArpIdx));
    Synth_Arp_SetRate(0.05f + gVal[C_ARATE] * 0.45f);
    Synth_Arp_SetGate(0.1f + gVal[C_AGATE] * 0.8f);
    Synth_SetMasterVolume(gVal[C_VOL]);
    Synth_ApplyDefaultsToActive();
}

void PanicNotes()
{
    Synth_AllNotesOff();
    Synth_Arp_AllNotesOff();
    for (int i = 0; i < 24; i++) gVoices[i] = -1;
    gMouseKey = -1;
}

void NotePress(int slot)
{
    if (gVoices[slot] >= 0) return;
    float f = SlotFreq(slot);
    if (gArpIdx > 0) {
        Synth_Arp_NoteOn(f, 0.7f);
        gVoices[slot] = 100 + slot;
    } else if (gPatchArm >= 0) {
        gVoices[slot] = Synth_NoteOnPatch(f, 0.7f, gPatchArm);
    } else {
        gVoices[slot] = Synth_NoteOn(f, 0.7f);
    }
}

void NoteRelease(int slot)
{
    if (gVoices[slot] < 0) return;
    if (gArpIdx > 0) Synth_Arp_NoteOff(SlotFreq(slot));
    else if (gVoices[slot] < 8) Synth_NoteOff(gVoices[slot]);
    gVoices[slot] = -1;
}

// ---- wavetable / preset / patch --------------------------------------------

string PresetPath()
{
    string p = ScriptGetString("synth_preset_path");
    if (p == "") return "save/synth_demo_preset.json";
    return p;
}

// generate slot 0 from shape index 0..3, or load a shipped file for 4..6
void WtApply()
{
    gWtFileOk = true;
    if (gWtIdx < 4) {
        Synth_WavetableGenerate(0, WaveAt(gWtIdx));
    } else {
        gWtFileOk = (Synth_WavetableLoad(0, WtPath(gWtIdx)) == 0);
    }
    Synth_SetWavetableIndex(0);
}

int WtCount() { return 7; }

string WtName(int i)
{
    if (i == 0) return "SINE";
    if (i == 1) return "SQUARE";
    if (i == 2) return "SAW";
    if (i == 3) return "TRIANGLE";
    if (i == 4) return "WARM SAW";
    if (i == 5) return "ORGAN";
    return "BELL";
}

string WtPath(int i)
{
    if (i < 4 || i >= WtCount()) return "";
    return gWtFiles[i - 4];
}

void WtSelect(int index)
{
    gWtIdx = index;
    WtApply();
    gWaveIdx = 5; // audible immediately
    ApplySound();
    if (gWtIdx < 4) gStatus = "WAVETABLE " + WtName(gWtIdx) + " GENERATED (slot 0)";
    else if (gWtFileOk) gStatus = "WAVETABLE FILE " + WtName(gWtIdx) + " LOADED";
    else gStatus = "WAVETABLE FILE " + WtPath(gWtIdx) + " LOAD FAILED";
}

void WtCycle()
{
    WtSelect((gWtIdx + 1) % WtCount());
}

// engine defaults -> demo controls (after a preset load; mirrors ApplySound)
void SyncUiFromEngine()
{
    gVal[C_ATK] = Clamp01((Synth_GetDefaultAttack() - 0.002f) / 0.5f);
    gVal[C_DEC] = Clamp01((Synth_GetDefaultDecay() - 0.01f) / 0.99f);
    gVal[C_SUS] = Clamp01(Synth_GetDefaultSustain());
    gVal[C_REL] = Clamp01((Synth_GetDefaultRelease() - 0.01f) / 0.99f);
    gVal[C_CUT] = Clamp01((Synth_GetDefaultFilterCutoff() - 0.02f) / 0.96f);
    gVal[C_RES] = Clamp01(Synth_GetDefaultFilterRes() / 0.9f);
    gFilterIdx = FilterIdxFromMode(Synth_GetDefaultFilterMode());
    gVal[C_LRATE] = Clamp01(Synth_GetDefaultLfoFreq() / 12.0f);
    gVal[C_LDEP] = Clamp01(Synth_GetDefaultLfoDepth());
    gLfoIdx = ClampIdx(Synth_GetDefaultLfoTarget(), 3);
    gFmIdx = ClampIdx(Synth_GetDefaultFmAlgorithm(), 5);
    gVal[C_FMAMT] = Clamp01(Synth_GetDefaultFmLevel());
    gVal[C_DTIME] = Clamp01((Synth_GetDefaultDelayTime() - 0.02f) / 0.98f);
    gVal[C_DFB] = Clamp01(Synth_GetDefaultDelayFeedback() / 0.9f);
    gVal[C_DMIX] = Clamp01(Synth_GetDefaultDelayMix());
    gVal[C_VOL] = Clamp01(Synth_GetMasterVolume());
    gWaveIdx = ClampIdx(Synth_GetDefaultWaveform(), WaveCount());
    gArpIdx = ClampIdx(Synth_GetDefaultArpMode(), 5);
    gVal[C_ARATE] = Clamp01((Synth_GetDefaultArpRate() - 0.05f) / 0.45f);
    gVal[C_AGATE] = Clamp01((Synth_GetDefaultArpGate() - 0.1f) / 0.8f);
    if (gWaveIdx == 5) Synth_SetWavetableIndex(0);
}

void PresetSaveAction()
{
    ApplySound();
    gPresetSaved = (Synth_Preset_Save(PresetPath()) == 0);
    if (gPresetSaved) gStatus = "PRESET SAVED -> " + PresetPath();
    else gStatus = "PRESET SAVE FAILED -> " + PresetPath();
}

void PresetLoadAction()
{
    gPresetLoaded = (Synth_Preset_Load(PresetPath()) == 0);
    if (gPresetLoaded) {
        SyncUiFromEngine();
        if (gPatchArm >= Synth_PatchCount()) gPatchArm = -1; // bank may be smaller now
        PanicNotes();
        gStatus = "PRESET LOADED <- " + PresetPath();
    } else {
        gStatus = "PRESET LOAD FAILED <- " + PresetPath();
    }
}

void PatchSaveAction()
{
    ApplySound();
    int slot = Synth_PatchCount();
    if (slot >= 8) slot = 7; // full bank refills the last slot
    if (Synth_Patch_SaveToBank(slot, "demo " + slot) == 0) gStatus = "PATCH SAVED slot " + slot;
    else gStatus = "PATCH SAVE FAILED";
}

void PatchArmAction()
{
    int count = Synth_PatchCount();
    if (count <= 0) {
        gPatchArm = -1;
        gStatus = "NO PATCHES YET - SAVE PATCH FIRST";
        return;
    }
    gPatchArm++;
    if (gPatchArm >= count) gPatchArm = -1;
    if (gPatchArm < 0) gStatus = "PATCH ARM OFF";
    else gStatus = "PATCH #" + gPatchArm + " ARMED - NEW NOTES";
}

string PatchArmLabel()
{
    if (gPatchArm < 0) return "ARM PATCH: OFF";
    return "ARM PATCH: #" + gPatchArm;
}

// ---- layout ----------------------------------------------------------------

float ColX(int col)
{
    float colW = (gW - 160.0f) / 4.0f;
    if (colW > 240.0f) colW = 240.0f;
    return 32.0f + col * (colW + 16.0f);
}

float ColW()
{
    float colW = (gW - 160.0f) / 4.0f;
    if (colW > 240.0f) colW = 240.0f;
    return colW;
}

float RowY(int row) { return 96.0f + row * 42.0f; }

void SetCtrlRect(int id, float x, float y, float w, int kind)
{
    gCtrlX[id] = x;
    gCtrlY[id] = y;
    gCtrlW[id] = w;
    gCtrlKind[id] = kind;
}

void BuildLayout()
{
    BuildKeyboardLayout();

    // column 1 — envelope
    SetCtrlRect(C_ATK, ColX(0), RowY(0), ColW(), 0);
    SetCtrlRect(C_DEC, ColX(0), RowY(1), ColW(), 0);
    SetCtrlRect(C_SUS, ColX(0), RowY(2), ColW(), 0);
    SetCtrlRect(C_REL, ColX(0), RowY(3), ColW(), 0);
    // column 2 — filter + lfo + fm
    SetCtrlRect(C_CUT, ColX(1), RowY(0), ColW(), 0);
    SetCtrlRect(C_RES, ColX(1), RowY(1), ColW(), 0);
    SetCtrlRect(C_FMODE, ColX(1), RowY(2), ColW(), 1);
    SetCtrlRect(C_LRATE, ColX(1), RowY(3), ColW(), 0);
    SetCtrlRect(C_LDEP, ColX(1), RowY(4), ColW(), 0);
    SetCtrlRect(C_LTGT, ColX(1), RowY(5), ColW(), 1);
    // column 3 — fm + arp
    SetCtrlRect(C_FMAMT, ColX(2), RowY(0), ColW(), 0);
    SetCtrlRect(C_FMALG, ColX(2), RowY(1), ColW(), 1);
    SetCtrlRect(C_AMODE, ColX(2), RowY(2), ColW(), 1);
    SetCtrlRect(C_ARATE, ColX(2), RowY(3), ColW(), 0);
    SetCtrlRect(C_AGATE, ColX(2), RowY(4), ColW(), 0);
    // column 4 — delay + output
    SetCtrlRect(C_DTIME, ColX(3), RowY(0), ColW(), 0);
    SetCtrlRect(C_DFB, ColX(3), RowY(1), ColW(), 0);
    SetCtrlRect(C_DMIX, ColX(3), RowY(2), ColW(), 0);
    SetCtrlRect(C_VOL, ColX(3), RowY(3), ColW(), 0);
    SetCtrlRect(C_SOUND, ColX(3), RowY(4), ColW(), 1);
    SetCtrlRect(C_WAVE, ColX(3), RowY(5), ColW(), 1);

    BuildStripLayout();
}

// wavetable preview + preset/patch buttons between the panel and the keys
void BuildStripLayout()
{
    gStripY = gKeyY - 180.0f;
    if (gStripY < 336.0f) gStripY = 336.0f;

    float margin = 32.0f;
    float totalW = gW - margin * 2.0f;
    gWtW = totalW * 0.34f;
    if (gWtW > 360.0f) gWtW = 360.0f;
    if (gWtW < 220.0f) gWtW = 220.0f;
    gWtX = margin;
    gSx = gWtX + gWtW + 24.0f;
    gSw = gW - margin - gSx;
    if (gSw < 260.0f) gSw = 260.0f;

    float half = (gSw - 12.0f) * 0.5f;
    SetCtrlRect(C_WT, gWtX, gStripY + 92.0f, gWtW, 1);
    SetCtrlRect(C_PSAVE, gSx, gStripY + 26.0f, half, 1);
    SetCtrlRect(C_PLOAD, gSx + half + 12.0f, gStripY + 26.0f, half, 1);
    SetCtrlRect(C_PTSAVE, gSx, gStripY + 68.0f, half, 1);
    SetCtrlRect(C_PTLOAD, gSx + half + 12.0f, gStripY + 68.0f, half, 1);
}

void BuildKeyboardLayout()
{
    gKeyX = 40.0f;
    gKeyW = (gW - 80.0f) / 14.0f;
    gKeyH = gH * 0.20f;
    if (gKeyH > 180.0f) gKeyH = 180.0f;
    if (gKeyH < 90.0f) gKeyH = 90.0f;
    gKeyY = gH - gKeyH - 36.0f;
}

// white key rect (into the scratch rect)
void WhiteRect(int i)
{
    gRcX = gKeyX + i * gKeyW + 1.0f;
    gRcY = gKeyY;
    gRcW = gKeyW - 2.0f;
    gRcH = gKeyH;
}

int WhiteSlotAt(int white)
{
    return white; // slots 0..13 are the white keys
}

// black key: exists after white i when the interval is a semitone step
int BlackAfter(int white)
{
    int step = white % 7;
    if (step == 0 || step == 1 || step == 3 || step == 4 || step == 5) return 1;
    return 0;
}

int BlackCountBefore(int white)
{
    int n = 0;
    for (int i = 0; i < white; i++) n += BlackAfter(i);
    return n;
}

void BlackRect(int blackIdx)
{
    int white = 0;
    int seen = 0;
    for (int i = 0; i < 14; i++) {
        if (BlackAfter(i) != 0) {
            if (seen == blackIdx) { white = i; break; }
            seen++;
        }
    }
    gRcW = gKeyW * 0.55f;
    gRcX = gKeyX + (white + 1) * gKeyW - gRcW * 0.5f;
    gRcY = gKeyY;
    gRcH = gKeyH * 0.62f;
}

int HitWhite(float mx, float my)
{
    if (my < gKeyY || my > gKeyY + gKeyH) return -1;
    if (mx < gKeyX || mx > gKeyX + 14.0f * gKeyW) return -1;
    int i = int((mx - gKeyX) / gKeyW);
    if (i < 0 || i > 13) return -1;
    return i;
}

int HitBlack(float mx, float my)
{
    for (int b = 0; b < 10; b++) {
        BlackRect(b);
        if (mx >= gRcX && mx <= gRcX + gRcW && my >= gRcY && my <= gRcY + gRcH) return 14 + b;
    }
    return -1;
}

int HitCtrl(float mx, float my)
{
    for (int id = 0; id < gCtrlCount; id++) {
        float h = (gCtrlKind[id] == 1) ? 26.0f : 20.0f;
        if (mx >= gCtrlX[id] && mx <= gCtrlX[id] + gCtrlW[id]
            && my >= gCtrlY[id] && my <= gCtrlY[id] + h) return id;
    }
    return -1;
}

// ---- input -----------------------------------------------------------------

void DragCtrl(int id, float mx)
{
    float t = (mx - gCtrlX[id]) / gCtrlW[id];
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    gVal[id] = t;
    ApplySound();
}

void ButtonPress(int id)
{
    if (id == C_FMODE) gFilterIdx = (gFilterIdx + 1) % 4;
    if (id == C_LTGT) gLfoIdx = (gLfoIdx + 1) % 3;
    if (id == C_FMALG) gFmIdx = (gFmIdx + 1) % 5;
    if (id == C_AMODE) {
        gArpIdx = (gArpIdx + 1) % 5;
        PanicNotes();
    }
    if (id == C_WAVE) gWaveIdx = (gWaveIdx + 1) % WaveCount();
    if (id == C_SOUND) Synth_SetMuted(!Synth_IsMuted());
    if (id == C_WT) WtCycle();
    if (id == C_PSAVE) PresetSaveAction();
    if (id == C_PLOAD) PresetLoadAction();
    if (id == C_PTSAVE) PatchSaveAction();
    if (id == C_PTLOAD) PatchArmAction();
    ApplySound();
}

void ProcessMouse()
{
    float mx = Mouse_GetX();
    float my = Mouse_GetY();
    bool down = Mouse_IsDown(0);
    bool pressed = Mouse_IsPressed(0);

    if (pressed) {
        int slot = HitBlack(mx, my);
        if (slot < 0) slot = HitWhite(mx, my);
        if (slot >= 0) {
            gMouseKey = slot;
            NotePress(slot);
        } else {
            int cid = HitCtrl(mx, my);
            if (cid >= 0) {
                if (gCtrlKind[cid] == 1) ButtonPress(cid);
                else { gDragCtrl = cid; DragCtrl(cid, mx); }
            }
        }
    }

    if (down && gDragCtrl >= 0) DragCtrl(gDragCtrl, mx);

    if (down && gMouseKey >= 0) {
        int slot = HitBlack(mx, my);
        if (slot < 0) slot = HitWhite(mx, my);
        if (slot >= 0 && slot != gMouseKey) {
            NoteRelease(gMouseKey);
            gMouseKey = slot;
            NotePress(slot);
        }
    }

    if (!down) {
        if (gMouseKey >= 0) { NoteRelease(gMouseKey); gMouseKey = -1; }
        gDragCtrl = -1;
    }
}

int SlotFromSemi(int semi)
{
    for (int i = 0; i < 24; i++) {
        if (gSlotSemi[i] == semi) return i;
    }
    return -1;
}

void KeyNote(int key, int semi)
{
    int slot = SlotFromSemi(semi);
    if (slot < 0) return;
    if (IsKeyPressed(key)) NotePress(slot);
    if (IsKeyReleased(key)) NoteRelease(slot);
}

void ProcessKeys()
{
    KeyNote(KEY_Z, 0);
    KeyNote(KEY_S, 1);
    KeyNote(KEY_X, 2);
    KeyNote(KEY_D, 3);
    KeyNote(KEY_C, 4);
    KeyNote(KEY_V, 5);
    KeyNote(KEY_G, 6);
    KeyNote(KEY_B, 7);
    KeyNote(KEY_H, 8);
    KeyNote(KEY_N, 9);
    KeyNote(KEY_J, 10);
    KeyNote(KEY_M, 11);
    KeyNote(KEY_Q, 12);
    KeyNote(KEY_TWO, 13);
    KeyNote(KEY_W, 14);
    KeyNote(KEY_THREE, 15);
    KeyNote(KEY_E, 16);
    KeyNote(KEY_R, 17);
    KeyNote(KEY_FIVE, 18);
    KeyNote(KEY_T, 19);
    KeyNote(KEY_SIX, 20);
    KeyNote(KEY_Y, 21);
    KeyNote(KEY_SEVEN, 22);
    KeyNote(KEY_U, 23);

    if (IsKeyPressed(KEY_COMMA) && gOctave > 1) { PanicNotes(); gOctave--; }
    if (IsKeyPressed(KEY_PERIOD) && gOctave < 6) { PanicNotes(); gOctave++; }
}

// ---- drawing ---------------------------------------------------------------

void Label(float x, float y, string text)
{
    Overlay_SetColor(40, 50, 65, 240);
    Overlay_Text(x, y, 16.0f, text);
}

void DrawSlider(int id, string label)
{
    float x = gCtrlX[id];
    float y = gCtrlY[id];
    float w = gCtrlW[id];

    Overlay_SetColor(120, 150, 180, 220);
    Overlay_Text(x, y - 1.0f, 16.0f, label);
    Overlay_SetColor(40, 60, 80, 220);
    Overlay_Bar(x, y + 16.0f, w, 14.0f, gVal[id]);
}

void DrawButton(int id, string label, int on)
{
    float x = gCtrlX[id];
    float y = gCtrlY[id];
    float w = gCtrlW[id];

    if (on != 0) Overlay_SetColor(90, 160, 110, 235);
    else Overlay_SetColor(70, 80, 100, 220);
    Overlay_Bar(x, y + 14.0f, w, 22.0f, (on != 0) ? 1.0f : 0.0f);
    Overlay_SetColor(230, 235, 245, 255);
    Overlay_Text(x + 6.0f, y + 17.0f, 16.0f, label);
}

// wavetable preview + preset/patch strip (see BuildStripLayout for the rects)
void DrawStrip()
{
    Overlay_SetColor(200, 210, 225, 255);
    if (gWaveIdx == 5) Overlay_Text(gWtX, gStripY, 16.0f, "WAVETABLE  " + WtName(gWtIdx) + "  (ACTIVE)");
    else Overlay_Text(gWtX, gStripY, 16.0f, "WAVETABLE  " + WtName(gWtIdx));

    Overlay_SetColor(20, 24, 34, 230);
    Overlay_Rect(gWtX, gStripY + 22.0f, gWtW, 62.0f);
    Overlay_SetColor(70, 90, 120, 200);
    Overlay_RectBorder(gWtX, gStripY + 22.0f, gWtW, 62.0f);

    // slot-0 waveform polyline (size-aware: file tables may be < engine max)
    Overlay_SetColor(120, 220, 180, 255);
    int segs = 48;
    int sz = Synth_WavetableSize(0);
    if (sz <= 1) sz = 2048;
    float px = gWtX;
    float py = gStripY + 53.0f;
    for (int i = 0; i <= segs; i++) {
        float v = Synth_WavetableSample(0, i * (sz - 1) / segs);
        float sx = gWtX + gWtW * float(i) / float(segs);
        float sy = gStripY + 22.0f + 62.0f * (1.0f - v);
        if (i > 0) Overlay_Line(px, py, sx, sy);
        px = sx;
        py = sy;
    }
    DrawButton(C_WT, "WT TABLE: " + WtName(gWtIdx), gWtFileOk ? 1 : 0);

    Overlay_SetColor(200, 210, 225, 255);
    Overlay_Text(gSx, gStripY, 16.0f, "PRESET / PATCH");
    DrawButton(C_PSAVE, "SAVE PRESET", 1);
    DrawButton(C_PLOAD, "LOAD PRESET", 1);
    DrawButton(C_PTSAVE, "SAVE PATCH", 1);
    DrawButton(C_PTLOAD, PatchArmLabel(), (gPatchArm >= 0) ? 1 : 0);
    Overlay_SetColor(170, 185, 205, 255);
    Overlay_Text(gSx, gStripY + 112.0f, 15.0f, gStatus);
}

string StateLine()
{
    string s = "WAVE " + WaveName(gWaveIdx);
    s = s + "   " + FilterName(gFilterIdx);
    s = s + "   LFO " + LfoName(gLfoIdx);
    s = s + "   " + FmName(gFmIdx);
    s = s + "   " + ArpName(gArpIdx);
    s = s + "   OCT " + gOctave;
    if (Synth_IsMuted()) s = s + "   ** MUTED (click SOUND) **";
    else s = s + "   SOUND ON";
    return s;
}

void DrawUI()
{
    Overlay_Clear();

    //opencode: full-height backdrop — the synthvis scene also draws its own
    //   (physical-key) keyboard canvas; this panel hides it behind the demo's
    //   mouse-playable key bed. Near-opaque so the scene scope visuals don't
    //   ghost through the controls.
    Overlay_SetColor(8, 10, 16, 238);
    Overlay_Rect(16.0f, 52.0f, gW - 32.0f, gH - 52.0f);
    //opencode: opaque band behind the demo key bed — the scene keyboard would
    //   otherwise ghost through the translucent panel at the edges.
    Overlay_SetColor(8, 10, 16, 255);
    Overlay_Rect(0.0f, gKeyY - 8.0f, gW, gH - gKeyY + 8.0f);

    Overlay_SetColor(255, 210, 120, 255);
    Overlay_Text(32.0f, 18.0f, 30.0f, "gX SYNTH");
    Overlay_SetColor(200, 210, 225, 255);
    Overlay_Text(32.0f, 58.0f, 16.0f, StateLine());

    DrawSlider(C_ATK, "ATTACK  " + int(gVal[C_ATK] * 100) + "%");
    DrawSlider(C_DEC, "DECAY  " + int(gVal[C_DEC] * 100) + "%");
    DrawSlider(C_SUS, "SUSTAIN  " + int(gVal[C_SUS] * 100) + "%");
    DrawSlider(C_REL, "RELEASE  " + int(gVal[C_REL] * 100) + "%");

    DrawSlider(C_CUT, "CUTOFF  " + int(gVal[C_CUT] * 100) + "%");
    DrawSlider(C_RES, "RESONANCE  " + int(gVal[C_RES] * 100) + "%");
    DrawButton(C_FMODE, FilterName(gFilterIdx), (gFilterIdx > 0) ? 1 : 0);
    DrawSlider(C_LRATE, "LFO RATE  " + int(gVal[C_LRATE] * 100) + "%");
    DrawSlider(C_LDEP, "LFO DEPTH  " + int(gVal[C_LDEP] * 100) + "%");
    DrawButton(C_LTGT, "LFO TARGET  " + LfoName(gLfoIdx), 1);

    DrawSlider(C_FMAMT, "FM AMOUNT  " + int(gVal[C_FMAMT] * 100) + "%");
    DrawButton(C_FMALG, FmName(gFmIdx), (gFmIdx > 0) ? 1 : 0);
    DrawButton(C_AMODE, "ARP  " + ArpName(gArpIdx), (gArpIdx > 0) ? 1 : 0);
    DrawSlider(C_ARATE, "ARP RATE  " + int(gVal[C_ARATE] * 100) + "%");
    DrawSlider(C_AGATE, "ARP GATE  " + int(gVal[C_AGATE] * 100) + "%");

    DrawSlider(C_DTIME, "DELAY TIME  " + int(gVal[C_DTIME] * 100) + "%");
    DrawSlider(C_DFB, "DELAY FEEDBACK  " + int(gVal[C_DFB] * 100) + "%");
    DrawSlider(C_DMIX, "DELAY MIX  " + int(gVal[C_DMIX] * 100) + "%");
    DrawSlider(C_VOL, "VOLUME  " + int(gVal[C_VOL] * 100) + "%");
    DrawButton(C_SOUND, Synth_IsMuted() ? "SOUND: MUTED" : "SOUND: ON", Synth_IsMuted() ? 0 : 1);
    DrawButton(C_WAVE, "WAVEFORM: " + WaveName(gWaveIdx), 1);

    DrawStrip();

    // keyboard
    for (int i = 0; i < 14; i++) {
        if (gVoices[i] >= 0) Overlay_SetColor(150, 210, 255, 255);
        else Overlay_SetColor(235, 235, 230, 255);
        WhiteRect(i);
        Overlay_Rect(gRcX, gRcY, gRcW, gRcH);
        Overlay_SetColor(120, 120, 115, 255);
        Overlay_RectBorder(gRcX, gRcY, gRcW, gRcH);
        if (i % 7 == 0) {
            Overlay_SetColor(70, 70, 70, 255);
            Overlay_Text(gRcX + 6.0f, gRcY + gRcH - 22.0f, 15.0f, NoteName(i));
        }
    }
    for (int b = 0; b < 10; b++) {
        if (gVoices[14 + b] >= 0) Overlay_SetColor(90, 140, 190, 255);
        else Overlay_SetColor(30, 32, 38, 255);
        BlackRect(b);
        Overlay_Rect(gRcX, gRcY, gRcW, gRcH);
        Overlay_SetColor(10, 10, 12, 255);
        Overlay_RectBorder(gRcX, gRcY, gRcW, gRcH);
    }

    // octave buttons + help
    Overlay_SetColor(90, 110, 140, 220);
    Overlay_Bar(gKeyX, gKeyY - 34.0f, 46.0f, 22.0f, 1.0f);
    Overlay_Bar(gKeyX + 54.0f, gKeyY - 34.0f, 46.0f, 22.0f, 1.0f);
    Overlay_SetColor(235, 240, 250, 255);
    Overlay_Text(gKeyX + 8.0f, gKeyY - 31.0f, 16.0f, "< OCT");
    Overlay_Text(gKeyX + 62.0f, gKeyY - 31.0f, 16.0f, "OCT >");
    Label(gW - 470.0f, gH - 22.0f, "keys: Z S X D C V G B H N J M / Q 2 W 3 E R 5 T 6 Y 7 U   [ , . ] octave");
}

// ---- self test -------------------------------------------------------------

void SelfTest()
{
    BuildLayout();
    ApplySound();

    AssertTrue(Synth_GetVoiceCount() == 8, "synth demo: voice count");
    int v = Synth_NoteOn(440.0f, 0.6f);
    AssertTrue(v >= 0, "synth demo: note on");
    AssertTrue(Synth_VoiceActive(v), "synth demo: voice active");
    Synth_NoteOff(v);

    float x, y, w, h;
    WhiteRect(0);
    x = gRcX; y = gRcY; w = gRcW; h = gRcH;
    AssertEqual(HitWhite(x + w * 0.5f, y + h * 0.5f), 0, "synth demo: white hit test");
    BlackRect(0);
    x = gRcX; y = gRcY; w = gRcW; h = gRcH;
    AssertEqual(HitBlack(x + w * 0.5f, y + h * 0.5f), 14, "synth demo: black hit test");
    AssertEqual(HitBlack(4.0f, 4.0f), -1, "synth demo: miss test");

    int cid = HitCtrl(gCtrlX[C_CUT] + 4.0f, gCtrlY[C_CUT] + 20.0f);
    AssertEqual(cid, C_CUT, "synth demo: slider hit test");
    DragCtrl(C_CUT, gCtrlX[C_CUT] + gCtrlW[C_CUT] * 0.5f);
    AssertFloat(gVal[C_CUT], 0.5f, 0.05f, "synth demo: slider drag");
    ButtonPress(C_FMODE);
    AssertTrue(gFilterIdx == 1, "synth demo: filter cycle");
    ButtonPress(C_AMODE);
    AssertTrue(gArpIdx == 1, "synth demo: arp cycle");
    PanicNotes();

    Synth_Arp_NoteOn(220.0f, 0.5f);
    Synth_Arp_NoteOn(330.0f, 0.5f);
    Sleep(150.0f);
    Synth_Arp_AllNotesOff();
    AssertTrue(true, "synth demo: arp run");

    bool before = Synth_IsMuted();
    Synth_SetMuted(!before);
    AssertTrue(Synth_IsMuted() != before, "synth demo: mute toggle");
    Synth_SetMuted(before);

    // wavetable generation + preview readback (slot 0)
    WtSelect(2); // SAW
    AssertFloat(Synth_WavetableSample(0, 1024), 0.5f, 0.02f, "synth demo: wavetable saw mid");
    AssertTrue(Synth_WavetableSample(0, 16) < 0.05f, "synth demo: wavetable saw start");
    float genMid = Synth_WavetableSample(0, 128); // raw saw ~0.06
    WtSelect(3); // TRIANGLE, mid sample 1.0
    AssertFloat(Synth_WavetableSample(0, 1024), 1.0f, 0.02f, "synth demo: wavetable tri peak");
    AssertTrue(Synth_WavetableCount() >= 1, "synth demo: wavetable loaded");
    AssertTrue(Synth_GetDefaultWaveform() == SYNTH_WAVE_WAVETABLE, "synth demo: wavetable waveform active");

    // shipped file tables (raw f32, 1024 samples) + failure reporting
    WtSelect(4); // WARM SAW
    AssertTrue(gWtFileOk, "synth demo: wavetable file loaded");
    AssertEqual(Synth_WavetableSize(0), 1024, "synth demo: wavetable file size");
    float warmMid = Synth_WavetableSample(0, 128);
    AssertTrue(genMid + 0.2f < warmMid, "synth demo: file table replaces generated data");
    WtSelect(5); // ORGAN
    AssertTrue(gWtFileOk, "synth demo: wavetable organ loaded");
    float organMid = Synth_WavetableSample(0, 128);
    AssertTrue(warmMid + 0.05f < organMid || organMid + 0.05f < warmMid, "synth demo: wavetable file tables differ");
    AssertTrue(Synth_WavetableLoad(0, "resources/audio/wavetable/no_such_table.f32") == 1, "synth demo: wavetable bad path reported");

    // patch bank round trip (SAVE captures the current defaults; a note played
    // through Synth_NoteOnPatch carries the stored patch)
    gVal[C_CUT] = 0.8f;
    ApplySound();
    PatchSaveAction();
    AssertTrue(Synth_PatchCount() >= 1, "synth demo: patch saved to bank");
    gVal[C_CUT] = 0.05f;
    ApplySound();
    int pv = Synth_NoteOnPatch(440.0f, 0.6f, 0);
    AssertTrue(pv >= 0, "synth demo: patch note on");
    AssertFloat(Synth_VoiceFilterCutoff(pv), 0.02f + 0.8f * 0.96f, 0.03f, "synth demo: patch bank round-trip");
    Synth_NoteOff(pv);

    // preset file round trip (path from the gate's ScriptGetString seed;
    // default save/synth_demo_preset.json) — save, mutate, load, UI + engine
    // both reflect the saved state.
    gVal[C_CUT] = 0.9f;
    gVal[C_VOL] = 0.42f;
    gFilterIdx = 2;
    ApplySound();
    PresetSaveAction();
    AssertTrue(gPresetSaved, "synth demo: preset file saved");
    gVal[C_CUT] = 0.1f;
    gVal[C_VOL] = 0.8f;
    gFilterIdx = 1;
    ApplySound();
    PresetLoadAction();
    AssertTrue(gPresetLoaded, "synth demo: preset file loaded");
    AssertFloat(gVal[C_CUT], 0.9f, 0.03f, "synth demo: preset cutoff restored");
    AssertFloat(gVal[C_VOL], 0.42f, 0.03f, "synth demo: preset volume restored");
    AssertTrue(gFilterIdx == 2, "synth demo: preset filter restored");
    AssertFloat(Synth_GetDefaultFilterCutoff(), 0.02f + 0.9f * 0.96f, 0.03f, "synth demo: preset engine state restored");

    AssertTrue(Overlay_Width() > 0.0f && Overlay_Height() > 0.0f, "synth demo: overlay size");
    Print("synth demo: selftest ok");
    Quit(0);
}

// ---- main ------------------------------------------------------------------

void main()
{
    gW = Overlay_Width();
    gH = Overlay_Height();

    InitTables();
    BuildLayout();

    gVal[C_ATK] = 0.02f;
    gVal[C_DEC] = 0.08f;
    gVal[C_SUS] = 0.7f;
    gVal[C_REL] = 0.25f;
    gVal[C_CUT] = 0.6f;
    gVal[C_RES] = 0.2f;
    gVal[C_LRATE] = 5.0f / 12.0f;
    gVal[C_LDEP] = 0.0f;
    gVal[C_FMAMT] = 0.0f;
    gVal[C_DTIME] = 0.2f;
    gVal[C_DFB] = 0.3f;
    gVal[C_DMIX] = 0.0f;
    gVal[C_ARATE] = 0.2f;
    gVal[C_AGATE] = 0.5f;
    gVal[C_VOL] = 0.5f;

    if (ScriptGetInt("synth_selftest", 0) == 1) {
        SelfTest();
        return;
    }

    WtApply();
    gStatus = "preset: " + PresetPath() + "   patches: " + Synth_PatchCount();
    SwitchScene(SCENE_SYNTHVIS);
    ApplySound();

    while (true) {
        ProcessKeys();
        ProcessMouse();
        DrawUI();
        Yield();
    }
}
