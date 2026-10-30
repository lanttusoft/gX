//ai: gX shell audio demo — the OPX 8-operator FM engine and the PAD layered
//   ensemble engine playing an ambient piece together over the tour APIs.
//   Both engines start muted (the gX default): press M to open the outputs,
//   or launch with GX_OPX_MUTE=0 GX_PAD_MUTE=0. The PAD holds a slow chord
//   progression (three detuned layers per note, drifting, through the
//   ensemble chorus); OPX answers with a bell-flavoured melody and a low
//   bass voice on the same roots.
//
//   Keys: A S D B V X Z R select the OPX algorithm (ALL-CARRIER/PAIRS/STACKS/
//         CASCADE/EP/BASS/BELL/COMPLEX), C toggles the chorus, W toggles the
//         stereo width, M toggles both engines' mute. Presets are exercised
//         by the self-test (they round-trip save/opx_pad_demo_*.json).
//   Mouse: none (keyboard showcase; the synth demo is the full instrument).
//
//   Run:  ./build/scripting/gX_build --module build/scripting/gx_shell.so \
//             --script resources/script/audio/opx_pad_demo.as
//   (the boot scan only reads resources/script/*.as, so the demo stays inert
//   unless started explicitly).
//
//   The scripting gate runs this file with state key "opx_pad_selftest" = 1:
//   the self-test exercises notes, mute, per-voice edits, the algorithm
//   switch, the chorus/width readbacks and the preset round trips, prints
//   markers and exits.

// ---- state ------------------------------------------------------------------

int gStep;
int gChord = -1;
int gAlg = OPX_ALG_BELL;
bool gSound = false;
bool gChorus = true;
float gWidth = 1.0f;
int[] gPadVoices;
int gPadCount = 0;
string gStatus = "";

// chord roots: A2, D3, G2, C3 (a slow i-VI-III-VII-ish loop)
float[] gRoots = { 110.0f, 146.83f, 98.0f, 130.81f };
// chord shape: root, fifth, octave (just ratios keep the layers beating)
float[] gShape = { 1.0f, 1.5f, 2.0f };
// pentatonic melody steps over the root (2 = one octave up)
float[] gMelody = { 2.0f, 2.25f, 2.5f, 3.0f, 2.5f, 2.25f, 1.5f, 2.0f };

// ---- patch ------------------------------------------------------------------

void SetupOPX()
{
    Opx_SetDefaultAlgorithm(gAlg);
    // two operators is the classic starting point; the algorithm table
    // decides which of the eight slots actually sound
    int i = 0;
    while (i < OPX_OPERATORS) {
        Opx_SetDefaultOperatorEnabled(i, false);
        Opx_SetDefaultOperator(i, 1.0f, 0.0f);
        Opx_SetDefaultOperatorDetune(i, 0.0f);
        Opx_SetDefaultOperatorFeedback(i, 0.0f);
        Opx_SetDefaultOperatorADSR(i, 0.005f, 0.45f, 0.0f, 0.5f);
        i++;
    }
    // bell-ish carriers + a bright modulator (the algorithm routes the rest)
    Opx_SetDefaultOperatorEnabled(0, true);
    Opx_SetDefaultOperator(0, 3.5f, 0.55f);   // modulator
    Opx_SetDefaultOperatorEnabled(1, true);
    Opx_SetDefaultOperator(1, 1.0f, 0.75f);   // carrier
    Opx_SetDefaultOperatorADSR(1, 0.005f, 0.6f, 0.0f, 0.7f);
    Opx_SetDefaultOperatorEnabled(7, true);
    Opx_SetDefaultOperator(7, 0.5f, 0.6f);    // sub carrier on the low end
    Opx_SetMasterVolume(0.5f);
}

void SetupPAD()
{
    // warm three-layer ensemble: saw / tri / saw, drifting apart
    Pad_SetDefaultLayer(0, PAD_WAVE_SAW, -9.0f, 0.45f, -0.55f, 0.11f, 6.0f);
    Pad_SetDefaultLayer(1, PAD_WAVE_TRI, 7.0f, 0.5f, 0.05f, 0.17f, 5.0f);
    Pad_SetDefaultLayer(2, PAD_WAVE_SAW, 14.0f, 0.4f, 0.6f, 0.07f, 7.0f);
    Pad_SetDefaultADSR(1.2f, 1.5f, 0.85f, 1.6f);
    Pad_SetDefaultFilter(PAD_FILTER_LP, 0.06f, 0.15f);
    Pad_SetDefaultFilterLFO(0.09f, 0.35f);
    Pad_SetChorus(gChorus, 0.6f, 0.6f, 0.5f, 0.25f);
    Pad_SetWidth(gWidth);
    Pad_SetMasterVolume(0.5f);
}

void ApplySound()
{
    // both engines are muted by default — open them while the demo sounds
    Pad_SetMuted(!gSound);
    Opx_SetMuted(!gSound);
    Pad_SetChorus(gChorus, 0.6f, 0.6f, 0.5f, 0.25f);
    Pad_SetWidth(gWidth);
    Opx_SetDefaultAlgorithm(gAlg);
}

// ---- sequencer --------------------------------------------------------------

void PadChordRelease()
{
    int i = 0;
    while (i < gPadCount) {
        Pad_NoteOff(gPadVoices[i]);
        i++;
    }
    gPadCount = 0;
}

void PadChord(int index)
{
    PadChordRelease();
    float root = gRoots[index];
    int i = 0;
    while (i < gShape.length() && gPadCount < 4) {
        int v = Pad_NoteOn(root * gShape[i], 0.7f);
        if (v >= 0) {
            gPadVoices[gPadCount] = v;
            gPadCount++;
        }
        i++;
    }
}

void MelodyNote(int index, float root)
{
    float f = root * gMelody[index % gMelody.length()];
    int v = Opx_NoteOn(f, 0.65f, gAlg);
    if (v >= 0) Opx_SetPan(v, (index % 2 == 0) ? -0.35f : 0.35f);
}

void BassNote(float root)
{
    int v = Opx_NoteOn(root * 0.5f, 0.8f, OPX_ALG_BASS);
    if (v >= 0) {
        Opx_SetOperator(v, 0, 1.0f, 0.45f);
        Opx_SetOperator(v, 1, 1.0f, 0.8f);
        Opx_SetOperatorADSR(v, 1, 0.01f, 0.8f, 0.4f, 0.8f);
    }
}

// ---- ui ---------------------------------------------------------------------

void DrawUI()
{
    Overlay_SetColor(255, 255, 255, 255);
    Overlay_Text(32.0f, 22.0f, 30.0f, "gX OPX + PAD");
    Overlay_SetColor(180, 190, 220, 255);
    Overlay_Text(32.0f, 62.0f, 17.0f, "sound: " + (gSound ? "ON" : "muted (M)"));
    Overlay_Text(32.0f, 86.0f, 17.0f, "opx algorithm: " + Opx_AlgorithmName(gAlg));
    Overlay_Text(32.0f, 110.0f, 17.0f, "pad chords: three drifting layers, chorus "
        + (gChorus ? "on" : "off") + ", width " + gWidth);
    Overlay_Text(32.0f, 134.0f, 17.0f, gStatus);
    Overlay_SetColor(140, 150, 180, 255);
    Overlay_Text(32.0f, 640.0f, 15.0f, "A S D B V X Z R algorithm   C chorus   W width   M sound");
}

// ---- self-test (gate) -------------------------------------------------------

void SelfTest()
{
    SetupOPX();
    SetupPAD();

    string presetPath = ScriptGetString("opx_pad_preset_path");
    if (presetPath == "") presetPath = "save/opx_pad_demo";
    string opxPath = presetPath + "_opx.json";
    string padPath = presetPath + "_pad.json";

    AssertEqual(Opx_AlgorithmCount(), 8, "opx/pad demo: algorithm count");
    AssertTrue(Opx_IsMuted(), "opx/pad demo: opx muted by default");
    AssertTrue(Pad_IsMuted(), "opx/pad demo: pad muted by default");
    bool before = Opx_IsMuted();
    Opx_SetMuted(!before);
    AssertTrue(Opx_IsMuted() != before, "opx/pad demo: mute toggle");
    Opx_SetMuted(before);
    bool padBefore = Pad_IsMuted();
    Pad_SetMuted(!padBefore);
    AssertTrue(Pad_IsMuted() != padBefore, "opx/pad demo: pad mute toggle");
    Pad_SetMuted(padBefore);

    int v = Opx_NoteOn(440.0f, 0.6f, gAlg);
    AssertTrue(v >= 0, "opx/pad demo: opx note on");
    AssertTrue(Opx_VoiceActive(v), "opx/pad demo: opx voice active");
    Opx_SetOperator(v, 1, 2.0f, 0.5f);
    Opx_SetOperatorEnabled(v, 1, true);
    Opx_NoteOff(v);

    int pv = Pad_NoteOn(220.0f, 0.6f);
    AssertTrue(pv >= 0, "opx/pad demo: pad note on");
    AssertTrue(Pad_VoiceActive(pv), "opx/pad demo: pad voice active");
    Pad_SetLayer(pv, 0, PAD_WAVE_SAW, -9.0f, 0.5f, -0.5f, 0.1f, 6.0f);
    Pad_NoteOff(pv);

    gChorus = false;
    gWidth = 0.4f;
    ApplySound();
    AssertTrue(!Pad_GetChorusEnabled(), "opx/pad demo: chorus toggle");
    AssertFloat(Pad_GetWidth(), 0.4f, 0.01f, "opx/pad demo: width toggle");
    gChorus = true;
    gWidth = 1.0f;
    ApplySound();

    gAlg = OPX_ALG_EP;
    Opx_SetDefaultAlgorithm(gAlg);
    AssertEqual(Opx_GetDefaultAlgorithm(), OPX_ALG_EP, "opx/pad demo: algorithm switch");
    AssertTrue(Opx_AlgorithmName(OPX_ALG_CASCADE) == "CASCADE", "opx/pad demo: algorithm name");

    // preset round trips: distinctive state, save, mutate, load
    Opx_SetDefaultOperator(0, 2.25f, 0.55f);
    Opx_SetDefaultAlgorithm(OPX_ALG_CASCADE);
    AssertTrue(Opx_Preset_Save(opxPath) == 0, "opx/pad demo: opx preset save");
    Opx_SetDefaultOperator(0, 1.0f, 0.1f);
    Opx_SetDefaultAlgorithm(OPX_ALG_ALL_CARRIER);
    AssertTrue(Opx_Preset_Load(opxPath) == 0, "opx/pad demo: opx preset load");
    AssertFloat(Opx_GetOperatorRatio(0), 2.25f, 0.01f, "opx/pad demo: opx preset ratio restored");
    AssertEqual(Opx_GetDefaultAlgorithm(), OPX_ALG_CASCADE, "opx/pad demo: opx preset algorithm restored");

    Pad_SetDefaultLayer(2, PAD_WAVE_TRI, 30.0f, 0.35f, 0.7f, 0.3f, 12.0f);
    Pad_SetWidth(0.65f);
    AssertTrue(Pad_Preset_Save(padPath) == 0, "opx/pad demo: pad preset save");
    Pad_SetDefaultLayer(2, PAD_WAVE_SAW, 0.0f, 0.1f, 0.0f, 0.0f, 0.0f);
    Pad_SetWidth(0.2f);
    AssertTrue(Pad_Preset_Load(padPath) == 0, "opx/pad demo: pad preset load");
    AssertFloat(Pad_GetLayerLevel(2), 0.35f, 0.01f, "opx/pad demo: pad preset layer restored");
    AssertEqual(Pad_GetLayerWaveform(2), PAD_WAVE_TRI, "opx/pad demo: pad preset waveform restored");
    AssertFloat(Pad_GetWidth(), 0.65f, 0.01f, "opx/pad demo: pad preset width restored");

    Print("opx/pad demo: selftest ok");
}

// ---- main -------------------------------------------------------------------

void main()
{
    gPadVoices.resize(4);
    if (ScriptGetInt("opx_pad_selftest", 0) == 1) {
        SelfTest();
        return;
    }

    SetupOPX();
    SetupPAD();
    gStatus = "presets (self-test): " + ScriptGetString("opx_pad_preset_path") + "_*.json";
    ApplySound();

    float chordTimer = 0.0f;
    float noteTimer = 0.0f;
    float bassTimer = 0.0f;

    while (true) {
        float dt = Time_GetDeltaTime();

        if (IsKeyPressed(KEY_M)) {
            gSound = !gSound;
            ApplySound();
        }
        if (IsKeyPressed(KEY_C)) {
            gChorus = !gChorus;
            ApplySound();
        }
        if (IsKeyPressed(KEY_W)) {
            gWidth = (gWidth > 0.5f) ? 0.25f : 1.0f;
            ApplySound();
        }
        int slot = 0;
        if (IsKeyPressed(KEY_A)) slot = 1;
        if (IsKeyPressed(KEY_S)) slot = 2;
        if (IsKeyPressed(KEY_D)) slot = 3;
        if (IsKeyPressed(KEY_B)) slot = 4;
        if (IsKeyPressed(KEY_V)) slot = 5;
        if (IsKeyPressed(KEY_X)) slot = 6;
        if (IsKeyPressed(KEY_Z)) slot = 7;
        if (IsKeyPressed(KEY_R)) slot = 8;
        if (slot > 0) {
            gAlg = slot - 1;
            Opx_SetDefaultAlgorithm(gAlg);
            Opx_AllNotesOff();
        }
        chordTimer += dt;
        noteTimer += dt;
        bassTimer += dt;
        if (gChord < 0 || chordTimer >= 7.0f) {
            chordTimer = 0.0f;
            gChord = (gChord + 1) % gRoots.length();
            PadChord(gChord);
        }
        if (noteTimer >= 0.55f) {
            noteTimer = 0.0f;
            MelodyNote(gStep, gRoots[gChord]);
            gStep++;
        }
        if (bassTimer >= 7.0f) {
            bassTimer = 0.0f;
            BassNote(gRoots[gChord]);
        }

        DrawUI();
        Yield();
    }
}
