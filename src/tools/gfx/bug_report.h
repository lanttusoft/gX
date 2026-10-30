/**********************************************************************************************
*
*   [gX: src/tools/gfx/bug_report.h]
*
*   Copyright (c) 2025-2026 lanttu
*
*
*   SPDX-License-Identifier: GPL-3.0-or-later
*
*   This file is part of gX.
*   gX is free software: you can redistribute it and/or modify it under the
*   terms of the GNU General Public License as published by the Free Software
*   Foundation, either version 3 of the License, or (at your option) any later
*   version.
*   gX is distributed in the hope that it will be useful, but WITHOUT ANY
*   WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
*   FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
*   details.
*   You should have received a copy of the GNU General Public License along
*   with gX. If not, see <https://www.gnu.org/licenses/>.
*
*
**********************************************************************************************/
//ai: offline editor reports; disk writes happen only on explicit submission.
#ifndef GX_TOOLS_GFX_BUG_REPORT_H
#define GX_TOOLS_GFX_BUG_REPORT_H

#if gX_ENABLE_RENDERER == 1

//ai: stable JSON lifecycle codes stay independent from report severity.
enum
{
    GX_GFX_BUG_STATUS_OPEN = 0,
    GX_GFX_BUG_STATUS_FIXED = 1
};

static struct
{
    gX_I8 name[128];
    gX_I8 description[256];
    gX_I8 steps[1024];
    gX_I8 when[256];
    gX_I8 affects[256];
    gX_I8 status[384];
    gX_I32 type;
    gX_I32 severity;
    gX_I1 open;
    gX_I1 screenshot;
    gX_I1 diagnostics;
} gX_BugReport;

static const gX_I8 *gX_BugTypes[] = {"Rendering", "Editor", "Assets", "Crash", "Other"};
static const gX_I8 *gX_BugSeverities[] = {"Low", "Medium", "High", "Critical"};

//ai: whitespace-only mandatory fields must not produce empty reports.
static gX_I1 gX_Tools_Gfx_BugReport_HasText(const gX_I8 *Text)
{
    while (*Text)
    {
        if (!isspace((unsigned char)*Text++)) return 1;
    }
    return 0;
}

//ai: exclusive creation prevents overwrite; new reports begin with an OPEN status code.
static gX_I1 gX_Tools_Gfx_BugReport_Save(const gX_I8 *Directory)
{
    if (!gX_Tools_Gfx_BugReport_HasText(gX_BugReport.name)
        || !gX_Tools_Gfx_BugReport_HasText(gX_BugReport.description)
        || gX_BugReport.type < 0 || gX_BugReport.type >= 5
        || gX_BugReport.severity < 0 || gX_BugReport.severity >= 4)
    {
        snprintf(gX_BugReport.status, sizeof(gX_BugReport.status), "Title and description required.");
        return 0;
    }
    if (!DirectoryExists(Directory) && MakeDirectory(Directory) != 0)
    {
        snprintf(gX_BugReport.status, sizeof(gX_BugReport.status), "Cannot create report directory.");
        return 0;
    }
    time_t Now = time(NULL);
    gX_I8 Date[32];
    struct tm *Utc = gmtime(&Now);
    if (!Utc || !strftime(Date, sizeof(Date), "%Y-%m-%dT%H:%M:%SZ", Utc))
    {
        snprintf(gX_BugReport.status, sizeof(gX_BugReport.status), "Cannot read UTC time.");
        return 0;
    }
    gX_I8 Path[512], Shot[512];
    FILE *File = NULL;
    for (gX_U32 Index = 0; Index < 100 && !File; Index++)
    {
        if (snprintf(Path, sizeof(Path), "%s/report-%lld-%u.json", Directory, (long long)Now, Index) >= (gX_I32)sizeof(Path)) break;
        if (snprintf(Shot, sizeof(Shot), "%s/report-%lld-%u.png", Directory, (long long)Now, Index) >= (gX_I32)sizeof(Shot)) break;
        if (FileExists(Shot)) continue;
        File = fopen(Path, "wx");
        if (!File && errno != EEXIST) break;
    }
    if (!File)
    {
        snprintf(gX_BugReport.status, sizeof(gX_BugReport.status), "Cannot create report file.");
        return 0;
    }
    gX_I1 ShotSaved = 0;
    if (gX_BugReport.screenshot)
    {
        Image Screen = LoadImageFromScreen();
        if (Screen.data) ShotSaved = ExportImage(Screen, Shot);
        UnloadImage(Screen);
    }
    const gX_I8 *Key = "title\0description\0type\0severity\0when\0affects\0steps\0utc\0engine_version\0preset\0screenshot\0screenshot_status";
    const gX_I8 *Values[] = {gX_BugReport.name, gX_BugReport.description,
        gX_BugTypes[gX_BugReport.type], gX_BugSeverities[gX_BugReport.severity],
        gX_BugReport.when, gX_BugReport.affects, gX_BugReport.steps, Date,
        gX_Data_String_gX_version, s_presetPath, ShotSaved ? Shot : "",
        !gX_BugReport.screenshot ? "not_requested" : ShotSaved ? "saved" : "failed"};
    struct json_object *Root = json_object_new_object();
    gX_I1 Ok = Root != NULL;
    if (Ok)
    {
        struct json_object *StatusCode = json_object_new_int(GX_GFX_BUG_STATUS_OPEN);
        if (!StatusCode || json_object_object_add(Root, "status_code", StatusCode) != 0)
        {
            if (StatusCode) json_object_put(StatusCode);
            Ok = 0;
        }
    }
    for (gX_U32 Index = 0; Ok && Index < sizeof(Values)/sizeof(Values[0]); Index++)
    {
        struct json_object *Value = json_object_new_string(Values[Index]);
        if (!Value) Ok = 0;
        else if (json_object_object_add(Root, Key, Value) != 0)
        {
            json_object_put(Value);
            Ok = 0;
        }
        Key += strlen(Key) + 1;
    }
    if (Ok && gX_BugReport.diagnostics)
    {
        struct json_object *State = gX_RendererGFX_Preset_ToObj(gfx_state(), 1);
        const gX_I8 *Names[] = {"viewport_x", "viewport_y", "viewport_width", "viewport_height", "selected_model"};
        gX_Rectangle View = gX_GUI_ViewportBox_Get();
        const gX_F64 Numbers[] = {View.x, View.y, View.width, View.height, (gX_F64)s_selModel};
        for (gX_U32 Index = 0; State && Index < 5; Index++)
            json_object_object_add(State, Names[Index], json_object_new_double(Numbers[Index]));
        Ok = State && json_object_object_add(Root, "diagnostics", State) == 0;
        if (!Ok) json_object_put(State);
    }
    const gX_I8 *Json = Ok ? json_object_to_json_string_ext(Root, JSON_C_TO_STRING_PRETTY) : NULL;
    Ok = Json && fputs(Json, File) >= 0;
    if (fclose(File) != 0) Ok = 0;
    json_object_put(Root);
    snprintf(gX_BugReport.status, sizeof(gX_BugReport.status), "%s: %.280s%s",
        Ok ? "Saved" : "Write failed", Path,
        Ok && gX_BugReport.screenshot && !ShotSaved ? " (screenshot failed)" : "");
    return Ok;
}

//ai: Debug-only opt-in fixtures exercise the real writer without authoring clicks.
#ifndef NDEBUG
static gX_I0 gX_Tools_Gfx_BugReport_Test(gX_I0)
{
    static gX_I1 Done = 0;
    const gX_I8 *Path = getenv("GX_GFX_BUG_REPORT_TEST");
    if (Done || !Path || ImGui::GetFrameCount() < 3) return;
    Done = 1;
    struct json_object *Root = json_object_from_file(Path);
    struct json_object *Items = json_object_object_get(Root, "items");
    if (!Root || !Items)
    {
        printf("BUG_REPORT_TEST: invalid fixture\n");
        json_object_put(Root);
        return;
    }
    for (gX_U32 Index = 0; Index < json_object_array_length(Items); Index++)
    {
        struct json_object *Item = json_object_array_get_idx(Items, Index);
        memset(&gX_BugReport, 0, sizeof(gX_BugReport));
        const gX_I8 *Keys[] = {"title", "description", "steps", "when", "affects"};
        gX_I8 *Buffers[] = {gX_BugReport.name, gX_BugReport.description, gX_BugReport.steps, gX_BugReport.when, gX_BugReport.affects};
        const size_t Sizes[] = {sizeof(gX_BugReport.name), sizeof(gX_BugReport.description), sizeof(gX_BugReport.steps), sizeof(gX_BugReport.when), sizeof(gX_BugReport.affects)};
        for (gX_U32 Field = 0; Field < 5; Field++)
        {
            struct json_object *Value = json_object_object_get(Item, Keys[Field]);
            const gX_I8 *Text = Value ? json_object_get_string(Value) : "";
            snprintf(Buffers[Field], Sizes[Field], "%s", Text ? Text : "");
        }
        gX_BugReport.type = json_object_get_int(json_object_object_get(Item, "type"));
        gX_BugReport.severity = json_object_get_int(json_object_object_get(Item, "severity"));
        gX_BugReport.screenshot = json_object_get_boolean(json_object_object_get(Item, "screenshot"));
        gX_BugReport.diagnostics = json_object_get_boolean(json_object_object_get(Item, "diagnostics"));
        gX_BugReport.open = json_object_get_boolean(json_object_object_get(Item, "open_only"));
        if (gX_BugReport.open)
        {
            printf("BUG_REPORT_TEST: open only\n");
            continue;
        }
        const gX_I8 *Directory = json_object_get_string(json_object_object_get(Item, "directory"));
        gX_I1 Saved = Directory ? gX_Tools_Gfx_BugReport_Save(Directory) : 0;
        printf("BUG_REPORT_TEST: saved=%d %s\n", Saved, gX_BugReport.status);
    }
    json_object_put(Root);
}
#endif

//ai: menu/hotkey form retains drafts in memory; no background saves or uploads.
static gX_I0 gX_Tools_Gfx_BugReport_Draw(gX_I0)
{
    if (!ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_F10, false))
        gX_BugReport.open = !gX_BugReport.open;
    if (!gX_BugReport.open) return;
    bool Open = true;
    ImGui::SetNextWindowSize(ImVec2(520, 0), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Report bug (offline)", &Open))
    {
        ImGui::TextWrapped("Save to bugs/. No uploads. Draft lost on exit.");
        ImGui::InputText("Title", gX_BugReport.name, sizeof(gX_BugReport.name));
        ImGui::Combo("Type", &gX_BugReport.type, gX_BugTypes, 5);
        ImGui::Combo("Severity", &gX_BugReport.severity, gX_BugSeverities, 4);
        ImGui::TextWrapped("Low: cosmetic. Medium: workaround. High: blocked task/repeated crash. Critical: data loss/corruption/security.");
        ImGui::TextWrapped("One issue: steps, expected vs actual, frequency, preset/revision. Unsure? Medium; explain impact.");
        ImGui::InputTextMultiline("Description", gX_BugReport.description, sizeof(gX_BugReport.description));
        ImGui::InputTextMultiline("Reproduction steps", gX_BugReport.steps, sizeof(gX_BugReport.steps));
        ImGui::InputText("When / how", gX_BugReport.when, sizeof(gX_BugReport.when));
        ImGui::InputText("Affected feature", gX_BugReport.affects, sizeof(gX_BugReport.affects));
        bool Shot = gX_BugReport.screenshot != 0;
        ImGui::Checkbox("Attach screen (may include UI)", &Shot);
        gX_BugReport.screenshot = Shot;
        bool Diagnostics = gX_BugReport.diagnostics != 0;
        ImGui::Checkbox("Attach diagnostic state (private context)", &Diagnostics);
        gX_BugReport.diagnostics = Diagnostics;
        if (ImGui::Button("Save local report")) gX_Tools_Gfx_BugReport_Save("bugs");
        ImGui::TextWrapped("%s", gX_BugReport.status);
    }
    ImGui::End();
    gX_BugReport.open = Open;
}

#endif
#endif
