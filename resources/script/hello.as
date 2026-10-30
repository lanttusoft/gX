// hello.as — gX scripting shell example (project GPLv3, author-owned).
//
//   The shell scans resources/script/*.as at boot (GX_SCRIPT_SCAN=0 opts out,
//   GX_SCRIPT_DIR overrides the directory) and runs each script's main().
//   Explicit run:  ./build/scripting/gX_build --script resources/script/hello.as
//
//   Contract highlights: Sleep(ms) suspends across frames; Hook(name, func)
//   registers a void() callback; ScriptSet*/ScriptGet* keep state across
//   hot-reloads (in-process; file persistence is a later slice).

void main()
{
    Hook("scene_enter", OnSceneEnter);

    int runs = ScriptGetInt("hello_runs", 0) + 1;
    ScriptSetInt("hello_runs", runs);
    if (runs == 1)
    {
        Print("hello from the gX scripting shell (first run this boot)");
    }
}

void OnSceneEnter()
{
    Print("hello: scene entered");
}
