//ai: gX shell audio demo — a large FM bell built on the OPX 8-operator
//   engine. The preset uses the BELL algorithm (four 2-op pairs): a low hum,
//   a prime, an inharmonic nominal/tierce and short high-index strike
//   modulators with a long tail. The output starts muted (the engine default):
//   press M to open it, or launch with GX_OPX_MUTE=0.
//
//   Keys: A S D V strike the four bell notes, Space strikes the current note,
//         X stops all tails, M toggles sound.
//
//   Run:  ./build/scripting/gX_build --module build/scripting/gx_shell.so \
//             --script resources/script/audio/opx_bell_demo.as
//   The scripting gate runs this file with state key "opx_bell_selftest" = 1.

// ---- state ------------------------------------------------------------------

int gRoot = 0;
int gVoice = -1;
float gHold = 0.0f;
float gStrikeTimer = 0.0f;
bool gSound = false;
bool gLoaded = false;
string gStatus = "";

// large-bell fundamentals: D2, A2, F2, C3
float[] gRoots = { 73.42f, 110.0f, 87.31f, 130.81f };

// ---- patch ------------------------------------------------------------------

void SetupBell()
{
    gLoaded = Opx_Preset_Load("resources/audio/presets/opx_large_bell.json") == 0;
    if (!gLoaded) {
        // fallback: the same shape programmed directly if the preset is absent
        Opx_SetDefaultAlgorithm(OPX_ALG_BELL);
        int i = 0;
        while (i < OPX_OPERATORS) {
            Opx_SetDefaultOperatorEnabled(i, false);
            i++;
        }
        Opx_SetDefaultOperatorEnabled(0, true);
        Opx_SetDefaultOperator(0, 3.5f, 0.10f);
        Opx_SetDefaultOperatorDetune(0, -2.0f);
        Opx_SetDefaultOperatorADSR(0, 0.002f, 1.6f, 0.0f, 1.2f);
        Opx_SetDefaultOperatorEnabled(1, true);
        Opx_SetDefaultOperator(1, 1.0f, 0.50f);
        Opx_SetDefaultOperatorDetune(1, 1.0f);
        Opx_SetDefaultOperatorADSR(1, 0.003f, 8.0f, 0.0f, 7.0f);
        Opx_SetDefaultOperatorEnabled(2, true);
        Opx_SetDefaultOperator(2, 4.7f, 0.07f);
        Opx_SetDefaultOperatorDetune(2, 2.0f);
        Opx_SetDefaultOperatorADSR(2, 0.001f, 1.0f, 0.0f, 0.9f);
        Opx_SetDefaultOperatorEnabled(3, true);
        Opx_SetDefaultOperator(3, 1.2f, 0.16f);
        Opx_SetDefaultOperatorDetune(3, -3.0f);
        Opx_SetDefaultOperatorADSR(3, 0.004f, 6.5f, 0.0f, 5.5f);
        Opx_SetDefaultOperatorEnabled(4, true);
        Opx_SetDefaultOperator(4, 2.9f, 0.09f);
        Opx_SetDefaultOperatorDetune(4, 1.0f);
        Opx_SetDefaultOperatorADSR(4, 0.002f, 1.2f, 0.0f, 1.0f);
        Opx_SetDefaultOperatorEnabled(5, true);
        Opx_SetDefaultOperator(5, 2.0f, 0.18f);
        Opx_SetDefaultOperatorDetune(5, 2.0f);
        Opx_SetDefaultOperatorADSR(5, 0.003f, 7.0f, 0.0f, 6.0f);
        Opx_SetDefaultOperatorEnabled(6, true);
        Opx_SetDefaultOperator(6, 5.8f, 0.05f);
        Opx_SetDefaultOperatorDetune(6, -1.0f);
        Opx_SetDefaultOperatorFeedback(6, 0.28f);
        Opx_SetDefaultOperatorADSR(6, 0.001f, 0.6f, 0.0f, 0.5f);
        Opx_SetDefaultOperatorEnabled(7, true);
        Opx_SetDefaultOperator(7, 0.5f, 0.22f);
        Opx_SetDefaultOperatorDetune(7, 4.0f);
        Opx_SetDefaultOperatorADSR(7, 0.005f, 9.0f, 0.0f, 8.0f);
        Opx_SetMasterVolume(0.55f);
    }
    Opx_SetMuted(true);
}

void Strike(int root)
{
    if (gVoice >= 0) Opx_NoteOff(gVoice);
    gRoot = root % gRoots.length();
    gVoice = Opx_NoteOn(gRoots[gRoot], 0.85f, OPX_ALG_BELL);
    if (gVoice >= 0) Opx_SetPan(gVoice, (gRoot % 2 == 0) ? -0.25f : 0.25f);
    gHold = 0.0f;
    gStatus = "struck " + gRoots[gRoot] + " Hz";
}

void Silence()
{
    if (gVoice >= 0) Opx_NoteOff(gVoice);
    gVoice = -1;
    Opx_AllNotesOff();
    gStatus = "tails stopped";
}

// ---- ui ---------------------------------------------------------------------

void DrawUI()
{
    Overlay_SetColor(255, 255, 255, 255);
    Overlay_Text(32.0f, 22.0f, 30.0f, "gX OPX large bell");
    Overlay_SetColor(180, 190, 220, 255);
    Overlay_Text(32.0f, 62.0f, 17.0f,
        "sound: " + (gSound ? "ON" : "muted (M)"));
    Overlay_Text(32.0f, 86.0f, 17.0f,
        "patch: " + (gLoaded ? "opx_large_bell.json" : "fallback") + " / BELL");
    Overlay_Text(32.0f, 110.0f, 17.0f, gStatus);
    Overlay_SetColor(140, 150, 180, 255);
    Overlay_Text(32.0f, 640.0f, 15.0f,
        "A S D V strike   Space strike   X silence   M sound");
}

// ---- self-test (gate) -------------------------------------------------------

void SelfTest()
{
    SetupBell();
    string presetPath = ScriptGetString("opx_bell_preset_path");
    if (presetPath == "") presetPath = "save/opx_bell_demo";
    string saved = presetPath + "_roundtrip.json";

    AssertTrue(Opx_IsMuted(), "opx/bell demo: muted by default");
    bool before = Opx_IsMuted();
    Opx_SetMuted(!before);
    AssertTrue(Opx_IsMuted() != before, "opx/bell demo: mute toggle");
    Opx_SetMuted(before);

    AssertEqual(Opx_GetDefaultAlgorithm(), OPX_ALG_BELL, "opx/bell demo: bell algorithm");
    AssertTrue(Opx_GetOperatorEnabled(1), "opx/bell demo: prime carrier enabled");
    AssertTrue(Opx_GetOperatorEnabled(7), "opx/bell demo: hum carrier enabled");
    AssertTrue(Opx_GetOperatorRatio(7) > 0.49f && Opx_GetOperatorRatio(7) < 0.51f,
        "opx/bell demo: hum ratio 0.5");

    int v = Opx_NoteOn(73.42f, 0.85f, OPX_ALG_BELL);
    AssertTrue(v >= 0, "opx/bell demo: note on");
    AssertTrue(Opx_VoiceActive(v), "opx/bell demo: voice active");
    Opx_NoteOff(v);

    Opx_SetDefaultOperator(0, 2.25f, 0.4f);
    AssertTrue(Opx_Preset_Save(saved) == 0, "opx/bell demo: preset save");
    Opx_SetDefaultOperator(0, 1.0f, 0.1f);
    AssertTrue(Opx_Preset_Load(saved) == 0, "opx/bell demo: preset load");
    AssertFloat(Opx_GetOperatorRatio(0), 2.25f, 0.01f,
        "opx/bell demo: preset ratio restored");

    Print("opx/bell demo: selftest ok");
}

// ---- main -------------------------------------------------------------------

void main()
{
    if (ScriptGetInt("opx_bell_selftest", 0) == 1) {
        SelfTest();
        return;
    }

    SetupBell();
    gStatus = "strike a bell";
    Opx_SetMuted(!gSound);
    Strike(0);

    while (true) {
        float dt = Time_GetDeltaTime();
        if (IsKeyPressed(KEY_M)) {
            gSound = !gSound;
            Opx_SetMuted(!gSound);
        }
        if (IsKeyPressed(KEY_A)) Strike(0);
        if (IsKeyPressed(KEY_S)) Strike(1);
        if (IsKeyPressed(KEY_D)) Strike(2);
        if (IsKeyPressed(KEY_V)) Strike(3);
        if (IsKeyPressed(KEY_SPACE)) Strike(gRoot);
        if (IsKeyPressed(KEY_X)) Silence();

        gHold += dt;
        gStrikeTimer += dt;
        if (gVoice >= 0 && gHold >= 2.5f) {
            Opx_NoteOff(gVoice);
            gVoice = -1;
        }
        if (gStrikeTimer >= 6.0f) {
            gStrikeTimer = 0.0f;
            Strike((gRoot + 1) % gRoots.length());
        }

        DrawUI();
        Yield();
    }
}
