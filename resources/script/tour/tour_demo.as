//opencode: rail tour demo — a scripted camera flythrough over the shipped
//   camera rail sample. The rail is shell-owned (the Camera_Rail_* bindings
//   plus the RAIL EDITOR tool panel); this example loads the sample, captions
//   it, plays it, and exits when the rail finishes so the scripting gate can
//   run it headless.
//   Run:  ./build/scripting/gX_build --module build/scripting/gx_shell.so \
//             --script resources/script/tour/tour_demo.as
//   The boot scan only reads resources/script/*.as (flat), so the demo stays
//   inert unless started explicitly.
//
//   Gate: the scripting check runs this file with a raised frame cap and
//   asserts the markers below.

void main()
{
    SwitchScene(SCENE_RENDERER_GFX);
    Sleep(250.0f);

    AssertTrue(Camera_Rail_Load("resources/tools/camera/orbit_sample.json") == 0,
        "tour demo: rail loaded");
    AssertEqual(Camera_Rail_Count(), 4, "tour demo: rail keyframes");
    AssertTrue(!Camera_Keyframe_IsRunning(), "tour demo: rail idle");

    TextWriter_SetColor(255, 255, 255, 255);
    TextWriter_DrawTagged("<yellow>gX</> rail tour — <b>orbit sample</b> (4 keyframes)",
        40.0f, 40.0f, 28.0f);
    TextWriter_DrawTagged("Camera_Rail_Load + Keyframe_Start; author rails in EXTRAS > RAIL EDITOR",
        40.0f, 82.0f, 18.0f);

    Camera_Keyframe_Start();
    float waited = 0.0f;
    while (Camera_Keyframe_IsRunning() && waited < 15.0f) {
        Sleep(100.0f);
        waited += 0.1f;
    }
    AssertTrue(!Camera_Keyframe_IsRunning(), "tour demo: rail finished");
    Print("tour demo: rail finished OK");
    Quit(0);
}
