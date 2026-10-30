/**********************************************************************************************
*
*   [gX: src/gui/gui.h]
*   gX — a game engine
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


#ifndef GUI_H
#define GUI_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../common/pool.h"   // for GX_GUI_USE_POOLS pool types
#include "../common/heap.h"

//opencode: pool-based element/module system (experimental). When enabled,
//   gX_GUI_Pools_MigrateFromHeap copies old heap elements to pools after build;
//   the pool draw path renders from pools instead of the old heap.
#ifndef GX_GUI_USE_POOLS
#define GX_GUI_USE_POOLS 0
#endif
#include "../common/types.h"
#include "../common/enum.h"

#include "config.h"
#include "enum.h"
    
#include "canvas.h"

typedef struct gX_GUI_State {
    gX_I1   SHOW_MENU           ;
    gX_I1   SHOW_DEBUGDISPLAY   ;
    gX_I1   SHOW_MSG            ;
    gX_I8   toastMessage[256];  //opencode: dynamic text for MESSAGE toast
    gX_I8   lastSavedText[64];  //opencode: "Last saved: just now" for pause menu
    gX_I1   SKIP_TRANSITION     ;  //opencode: bypass all transitions (for testing)
    gX_I1   INITIAL_LOAD        ;  //opencode: set during startup so the initial scene switch skips transition animation
    gX_I1   SUBTEMPLATE_FADE    ;  //opencode: deferred fade for sub-template switches (set by callback, consumed in update)
    gX_I1   DIAGNOSTIC_TEMPLATE ;  //opencode: dump element rects after template build
    
    gX_U32  PADDING;
} gX_GUI_State;

//opencode: transition callback typedef (fired at mid-phase start)
typedef gX_I0 (*gX_GUI_TransitionCallback)(gX_I0 *userData);

//opencode: easing enum moved to config.h (shared with element animations)

//opencode: v0.3.8 — transition styles ("menu personality" pack). FADE is the
//   default and the only style that touches canvasHue; SLIDE/ZOOM animate
//   canvasOffset/canvasScale; SKEW_SLIDE adds a swing rotation on top of the
//   slide (Xbox-Blade style); the wipe styles draw a screen-space overlay
//   (gui_wipe.frag) that sweeps over the composited canvases.
typedef enum gX_GUI_TransitionStyle {
    GX_GUI_TRANSITION_STYLE_FADE = 0,
    GX_GUI_TRANSITION_STYLE_SLIDE,
    GX_GUI_TRANSITION_STYLE_ZOOM,
    GX_GUI_TRANSITION_STYLE_SKEW_SLIDE,
    GX_GUI_TRANSITION_STYLE_DIAGONAL_WIPE,
    GX_GUI_TRANSITION_STYLE_SCANLINE_CURTAIN,
    GX_GUI_TRANSITION_STYLE_IRIS,
} gX_GUI_TransitionStyle;

typedef struct gX_GUI_Canvas_Transition_Sub {
    
    gX_I1   execute;
    gX_I1   done;
    
    gX_F32  Tactual;
    gX_F32  T;
    gX_F32  Tseconds;
    
} gX_GUI_Canvas_Transition_Sub;
    
typedef struct gX_GUI_Canvas_Transition {
    gX_I1   enabled;
    
    gX_I1   overrideRefresh;
    
    gX_I1   doRefreshStartMiddle;
    gX_I1   doRefreshEndMiddle;
    
    gX_I1   refreshNow;
    
    gX_I1   execute;
    gX_I1   executing;
    gX_I1   setup;
    gX_I1   done;
    
    gX_I1   doOut;
    gX_I1   doMiddle;
    gX_I1   doIn;
    
    gX_I1   targetRect;
    gX_I1   targetColor;
    gX_I1   targetPP;
    //opencode: position and scale transition targets (animates canvasOffset and canvasScale)
    gX_I1   targetPosition;
    gX_I1   targetScale;
    gX_F32v2 startPosOffset;
    gX_F32v2 endPosOffset;
    gX_F32v2 startScaleVal;
    gX_F32v2 endScaleVal;
    
    gX_F32v4 result;
    gX_F32v4 start;
    gX_F32v4 end;
    
    gX_GUI_Canvas_Transition_Sub out;
    gX_GUI_Canvas_Transition_Sub middle;
    gX_GUI_Canvas_Transition_Sub in;
    
    //opencode: unified callback-based transition fields (replaces static g_fade)
    gX_GUI_TransitionCallback callback;
    gX_I0 *userData;
    gX_I1  callbackRan;
    
    //opencode: per-phase easing
    gX_GUI_Easing easeOut;
    gX_GUI_Easing easeMiddle;
    gX_GUI_Easing easeIn;

    //opencode: global clock for per-canvas stagger — seconds since setup,
    //   never reset mid-transition so transitionDelay offsets the full
    //   out/middle/in sequence per canvas.
    gX_F32 elapsed;

    //opencode: v0.3.8 — transition style (see enum above)
    gX_GUI_TransitionStyle style;
    
} gX_GUI_Canvas_Transition;

//opencode: forward declaration — occlusion stores a direct element pointer
//   (full type lives in element/struct.h)
typedef struct gX_GUI_Element gX_GUI_Element;

typedef struct gX_GUI_OcclusionCheck {
    gX_I32 canvasID;
    gX_I32 elementID;
    //opencode: direct element pointer for hit-matching — the draw-phase
    //   layer sort permutes the live element array, so element->id (heap
    //   slot) no longer equals the sorted array index stored in elementID.
    //   Matching by pointer is permutation-independent.
    gX_GUI_Element *elementPtr;
    //opencode: v0.4.0 — B1 audit: canvas pointer for hit-matching.
    //   canvasID = heap index at creation, but heap holes (canvas removal)
    //   drift the mapping — the element-side match compared canvas->canvasID
    //   (a creation-order id) against the heap INDEX, silently missing every
    //   click once a canvas was removed. Pointer match is drift-proof.
    gX_GUI_Canvas *canvasPtr;
    gX_Rectangle rect;
} gX_GUI_OcclusionCheck;

typedef struct gX_GUI_DirectDraw {
    gX_I1       drawFPS;
    gX_I32      coordFPS_X;
    gX_I32      coordFPS_Y;
    gX_I1       drawLoadingScreen;
    //opencode: fade transition state
    gX_I1       fadeActive;
    gX_F32      fadeTimer;
    gX_F32      fadeDuration;   // half-duration (fade-out + fade-in each)
    gX_I32      fadeTargetScene; // scene to switch to at midpoint
    //opencode: debug overlays
    gX_I1       showElementOverlay;  // D2: colored rects over all elements
    gX_I1       showCanvasHeatmap;   // D4: colored rects per canvas
} gX_GUI_DirectDraw;

//opencode: template hints — metadata for resource preloading and debug introspection
typedef struct gX_GUI_TemplateHint {
    gX_I8 requiredTexture[64];
    gX_I8 requiredShader[64];
    gX_I8 description[128];
} gX_GUI_TemplateHint;

//opencode: centralized style/theme — single source for colors, font sizes, spacing,
//   widget dimensions, and animation constants used across all GUI templates.
//   Every template should reference gui.style.* instead of hardcoded literals.
typedef struct gX_GUI_Style {
    // --- General ---
    gX_Color  panelBg;            // default panel/header background (muted purple-blue)
    gX_Color  textColor;          // default text color
    gX_Color  focusRing;          // keyboard focus outline
    gX_F32    focusRingOffset;    // pixels between element edge and focus ring
    gX_F32    fontSize;           // default text size (pixels)
    gX_I32    fontSpacing;        // default letter spacing
    
    // --- Button ---
    gX_Color  buttonBase;         // default button background
    gX_Color  buttonHover;        // button hover tint (bright color)
    gX_Color  buttonText;         // button label text
    gX_F32    buttonW;            // grid units
    gX_F32    buttonH;            // grid units
    
    // --- Back / Cancel ---
    gX_Color  backBase;           // back/cancel button background
    gX_Color  backHover;          // back button hover tint
    gX_Color  backText;           // back button label text
    gX_F32    backW;              // grid units
    gX_F32    backH;              // grid units
    
    // --- Special button variants ---
    gX_Color  playBase;           // "play" / primary action button
    gX_Color  playHover;
    gX_Color  extraBase;          // secondary/extra action buttons
    gX_Color  extraHover;
    gX_Color  quitBase;           // quit/destructive action
    gX_Color  quitHover;
    
    // --- Section / Panel ---
    gX_Color  sectionHeader;      // section divider/header color
    gX_F32    sectionDividerH;    // grid units (thin horizontal rule)
    gX_Color  panelOverlay;       // semi-transparent overlay (e.g. credits panel)
    
    // --- Text size variants ---
    gX_F32    titleSize;          // large headers (48px)
    gX_F32    bodySize;           // body text (18-24px)
    gX_F32    smallSize;          // captions/labels (14-16px)
    gX_F32    creditsSize;        // credits / fine print (28px)
    gX_I32    creditsSpacing;     // negative letter spacing for credits (-3)
    
    // --- Hover animation ---
    gX_F32    hoverSeconds;       // hover color transition duration
    gX_F32    hoverMultiplier;    // hover brightness multiplier
    gX_F32    hoverScale;         // hover scale factor (1.1 = 110%)
    
    // --- v0.2.0 SDF widget surface (guiwidget.frag) ---
    gX_I1    widgetSDF;          // route rect elements through the SDF shader
    gX_F32   cornerRadius;       // px, per-widget override when element >= 0
    gX_F32   shadowOpacity;      // 0..1 drop-shadow strength
    gX_F32   gradientStrength;   // 0..1 vertical surface gradient
    gX_F32   borderWidth;        // hairline rim width in px
    gX_Font  uiFont;             // Kenney Mini Square Mono — loaded at GUI init
    gX_I1    uiFontLoaded;
    gX_Font  uiFontSDF;          //opencode: v0.6.0 — SDF twin of uiFont (crisp at scale)
    gX_I1    uiFontSDFLoaded;
    //opencode: title wordmark font — European Teletext keeps gX distinct.
    gX_Font  titleFont;
    gX_I1    titleFontLoaded;

    // --- Widget dimensions (grid units, mirrors config.h) ---
    gX_F32    scrollbarW;
    gX_F32    scrolllistItemH;
    gX_F32    stepperBtnW;
    gX_F32    tabBarH;
    gX_F32    tabGutter;
    gX_F32    sliderH;            // slider track height
    gX_F32    knobRadius;         // slider knob circle radius (pixels)
    
    // --- Scrollbar ---
    gX_Color  scrollbarTrack;
    gX_Color  scrollbarThumb;
    gX_Color  scrollbarArrow;
    
    // --- Selection ---
    gX_Color  selectedBg;
    gX_Color  unselectedBg;
    gX_Color  selectedText;
    gX_Color  unselectedText;
    
    // --- Battle HUD (used by scene/battle.c gX_SceneBattle_Draw) ---
    struct {
        gX_Color  panelBg;           // player panel background
        gX_Color  nameBg;            // player name background
        gX_Color  hpBg;              // HP bar background
        gX_Color  mpBg;              // MP bar background
        gX_Color  enBg;              // EN bar background
        gX_Color  enemyPanelBg;      // enemy panel background
        gX_Color  enemyNameBg;       // enemy name background
        gX_Color  enemyHpBg;         // enemy HP bar background
        gX_Color  menuBg;            // main action menu background
        gX_Color  menuItemBg;        // menu item background
        gX_Color  magicBg;           // magic submenu background
        gX_Color  magicItemBg;       // magic item background
        gX_Color  skillsBg;          // skills submenu background
        gX_Color  skillsItemBg;      // skills item background
        gX_Color  itemsBg;           // items submenu background
        gX_Color  itemsItemBg;       // items item background
        gX_Color  targetBg;          // target selector background
        gX_Color  overlayBg;         // victory/defeat/wave overlay
    } battle;
    
    // --- Title2 (title screen) ---
    struct {
        gX_Color  bgGradientTop;      // {40,0,60,255}   — gradient top
        gX_Color  bgGradientBottom;   // {100,20,120,255}— gradient bottom
        gX_Color  titleCardBg;        // {255,255,255,255}— title card background
        gX_Color  moduleAvailable;    // {63,127,63,255}  — play module interactive
        gX_Color  moduleUnavailable;  // {63,63,63,255}   — play module disabled
        gX_Color  moduleHover;        // gX_GREEN          — play module hover
        gX_Color  subButtonBg;        // {127,127,63,255} — extra/play button base
        gX_Color  actionBg;           // {63,63,127,255}  — apply/save button bg
        gX_Color  actionHover;        // gX_BLUE           — apply/save hover
        gX_Color  modelBg;            // {30,30,60,255}   — rotatecube background
        //opencode: v0.3.8 — titlegx.c token migration (aesthetic presets):
        //   every literal in the title template is now a token so a theme
        //   file can re-skin the whole screen. Values = the historical
        //   literals (zero visual change on the default theme).
        gX_Color  heroBand;           // {205,192,245,120}— hero motif triangle bands
        gX_Color  wordmarkText;       // {245,242,255,255}— "gX" wordmark
        gX_Color  subtitleText;       // {205,185,235,230}— "alpha" subtitle
        gX_Color  menuBg1;            // {50,40,80,180}   — RESUME button bg (ramp start)
        gX_Color  menuBg2;            // {45,35,75,180}   — RUN DEMO button bg
        gX_Color  menuBg3;            // {40,30,70,180}   — LOAD SAVE button bg
        gX_Color  menuBg4;            // {35,28,65,180}   — SETTINGS button bg
        gX_Color  menuBg5;            // {30,25,55,170}   — DEMOS button bg
        gX_Color  menuBg6;            // {28,22,50,160}   — EXTRAS button bg
        gX_Color  menuText1;          // {244,240,255,245}— RESUME text
        gX_Color  menuText2;          // {240,234,254,240}— RUN DEMO text
        gX_Color  menuText3;          // {236,228,252,235}— LOAD SAVE text
        gX_Color  menuText4;          // {232,224,250,235}— SETTINGS text
        gX_Color  menuText5;          // {225,216,246,225}— DEMOS text
        gX_Color  menuText6;          // {220,210,242,220}— EXTRAS text
        gX_Color  quitBg;             // {60,20,40,200}   — QUIT button bg
        gX_Color  quitText;           // {248,190,208,230}— QUIT text
        gX_Color  footerText;         // {190,172,220,200}— footer links (INFO/DEMO/QUIT)
        gX_Color  versionText;        // {175,155,205,190}— version line
        gX_Color  savePanelBg;        // {25,18,45,240}   — save-slot view panel
        gX_Color  saveHeaderText;     // {245,240,255,240}— "LOAD SAVE" header
        gX_Color  saveSlotBg;         // {50,40,80,220}   — save slot rect
        gX_Color  saveSlotText;       // {235,228,252,235}— save slot label
        gX_Color  backBg;             // {60,30,55,220}   — BACK button bg
        gX_Color  backText;           // {250,236,248,240}— BACK button text
    } title2;

    //opencode: v0.6.2 — blueprint design-language group (frontend_design.md
    //   v0.2 flat line-art spec): technical-drawing ink/accent palette + the
    //   lattice-canvas motif. The themlab preview consumes these; ink/accent
    //   also drive the title hero when a blueprint theme is loaded.
    struct {
        gX_Color  inkColor;        // {40,45,60,255}    — blueprint line ink (borders/outlines)
        gX_Color  accentColor;     // {120,180,240,255} — blueprint accent (hi-lites/motifs)
        gX_Color  latticeColor;    // {90,120,180,255}  — lattice grid line color
        gX_F32    latticeAlpha;    // 0.06 — lattice grid opacity
        gX_F32    latticeCell;     // 48 px — lattice cell size
    } blueprint;
    
    // --- Escape/pause menu ---
    struct {
        gX_Color  dimOverlay;        // {40,30,60,80}    — full-screen dim
        gX_Color  panelBg;           // {20,15,35,200}   — menu panel
        gX_Color  titleBarBg;        // {40,30,60,220}   — "Pause" title bar
        gX_Color  buttonBg;          // {40,35,60,200}   — menu buttons
        gX_Color  closeBg;           // {60,50,80,200}   — X close button
        gX_Color  titleText;         // {200,200,255}    — title text
        gX_Color  hintText;          // {150,150,180,180}— "ESC to close" hint
        gX_Color  savedText;         // {120,120,150,160}— last-saved line
    } escape;
    
    //opencode: shared accent tokens — migration targets for the hardcoded
    //   colors that appear across >=2 screens (see scripts/color_inventory.py).
    //   Values match the historical usage so adopting a token changes nothing
    //   visually; they exist so templates can stop hardcoding.
    //opencode: v0.6.0 — a11y token group: surface stack + On- pairs (WCAG 2.2
    //   AA contrast asserted >= 4.5:1 at load), motion durations + reduced
    //   motion, type metrics. Dark themes rebalance the SURFACES only — the
    //   On- pairs remap with them; text colors never invert independently.
    struct {
        gX_Color  surfaceCanvas;      // base canvas surface
        gX_Color  surfaceRaised;      // panels/cards on the canvas
        gX_Color  surfaceOverlay;     // menus/dialogs above the raised tier
        gX_Color  surfaceSunken;      // wells/insets within a surface
        gX_Color  surfaceInverse;     // inverse tier (dark-on-light themes)
        gX_Color  textOnCanvas;       // text on surfaceCanvas
        gX_Color  textOnRaised;       // text on surfaceRaised
        gX_Color  textOnOverlay;      // text on surfaceOverlay
        gX_Color  textOnSunken;       // text on surfaceSunken
        gX_Color  textOnInverse;      // text on surfaceInverse
        gX_F32    motionShort;        // ms — micro-interactions (hover pulse)
        gX_F32    motionBase;         // ms — standard transitions
        gX_F32    motionLong;         // ms — emphasis transitions
        gX_I1     motionReduced;      // user prefers reduced motion — snap, don't animate
        gX_F32    typeLineHeight;     // multi-line text line-height multiplier (>= 1.5, WCAG 1.4.12)
    } a11y;
    //opencode: text shadow tokens (per-element TEXT_SHADOW flag). The offset
    //   is in base-1024 pixels and scales with the font like the text size;
    //   alpha below 255 softens the shadow without a blur pass, and alpha 0
    //   disables it while keeping the element flag.
    struct {
        gX_F32    offsetX;
        gX_F32    offsetY;
        //opencode: small-text pair (below core.bodySize) so compact labels do
        //   not carry the body offset; defaults mirror the base pair.
        gX_F32    offsetXSmall;
        gX_F32    offsetYSmall;
        gX_F32    softness; // 0 = single offset (default); >0 = 5-tap soft shadow
        gX_Color  color;
    } textShadow;
    //opencode: text outline — 8-way 1px copies under the glyphs; width 0
    //   (default) disables it, so contrast themes can opt in.
    struct {
        gX_F32    width;   // px at the 1024x768 reference, scaled with the font
        gX_Color  color;
    } textOutline;
    //opencode: surface depth tokens — gradient profiles applied to SDF
    //   surfaces (buttons + textureless panels). mode 0 keeps the authored
    //   gradient (or the derived button ramp); 1 = horizon (soft reflected
    //   sky band), 2 = metallic (anisotropic highlight + edge rim). Values
    //   are fractions of the surface height unless noted.
    struct {
        gX_I32    mode;
        gX_F32    horizonPos;
        gX_F32    horizonSoft;
        gX_F32    horizonGlow;
        gX_F32    metalBand;
        gX_F32    metalWidth;
        gX_F32    metalStrength;
        gX_F32    metalRim;
    } surfaceDepth;
    //opencode: Aero-glass tokens — a glass surface samples the accumulated
    //   backdrop (gui.ping: scene + earlier canvases), blurs it, tints toward
    //   the element color, and adds a silhouette rim plus a top gloss. Zeroed
    //   tokens (or GX_GUI_GLASS=0) leave the plain translucent fill as the
    //   honest fallback. Lengths are base-1024 pixels, scaled like type.
    struct {
        gX_F32    blur;
        gX_F32    tint;
        gX_F32    rim;
        gX_F32    refraction;
        gX_F32    gloss;
    } glass;
    //opencode: cursor light — a soft radial highlight on SDF surfaces that
    //   follows the pointer (game coords mapped per draw path). strength 0
    //   (default) keeps every surface pixel-identical, so the effect is
    //   opt-in for the perf/a11y review window.
    struct {
        gX_F32    strength;
        gX_F32    radius;    // px at the 1024x768 reference
        gX_F32    glint;     // tighter hover/pressed highlight (0 disables)
        gX_Color  color;
    } cursorLight;
    struct {
        gX_Color  accentGold;         // {255,220,100,255}— names/highlights (dialogue, world, questlog, trade, bestiary)
        gX_Color  accentCyan;         // {100,200,255,255}— interaction prompts (world, battle)
        gX_Color  panelDark;          // {20,20,40,200}   — dark panels (bestiary, trade)
        gX_Color  textDim;            // {150,150,150,200}— dim secondary text (bestiary, dialogue, questlog)
        gX_Color  textLightBlue;      // {200,200,255,255}— light blue text (menu, questlog, settings, escape)
        gX_Color  translucentWhite;   // {255,255,255,127}— translucent white overlay (menu, editor, title)
        gX_Color  hpGreen;            // {100,255,100,200}— positive/hp accents (battle, bestiary, questlog)
        gX_Color  alertRed;           // {255,100,100,200}— negative/alert accents (bestiary, questlog)
        gX_Color  goldDim;            // {200,200,100,200}— muted gold accents (bestiary, questlog)
        gX_Color  borderLight;        // {200,200,200,200}— light borders (interior, world, battle)
        gX_Color  textOnCard;         // {255,255,255,255}— text ON card/panel backgrounds (NOT titleCardBg — that is a bg token)
    } shared;
} gX_GUI_Style;

typedef struct gX_GUI {
    gX_I8   name[gX_GUI_NAMEBFR_SIZE];
    gX_I8   description[gX_GUI_DESCBFR_SIZE];
    
    gX_I1   loaded;
    gX_I1   debug;
    gX_I1   masterIndexSwitchResponse;
    gX_GUI_State state;
    gX_GUI_Style style;         //opencode: centralized theme — set in gX_GUI_Load
    
    gX_I1   make;
    gX_I32  auditTemplateID; //opencode: v0.1.40 — forced template re-made on every respond
gX_U32  makeAmount;
    gX_I32  makeToken[gX_GUI_MAKE_QUEUE_MAX];
    gX_I32  canvasToken[gX_GUI_MAKE_QUEUE_MAX];
    
    gX_I1   refresh;
    gX_I1   clearAll;
    
    gX_GUI_OcclusionCheck occlusion;
    
    gX_Heap canvases;
    
    //special canvases
    gX_I1   doNotClearMenu;
    gX_I1   doNotClearDebugDisplay;
    gX_I1   doNotClearMessages;
    gX_GUI_Canvas *menuCanvas;
    gX_GUI_Canvas *debugDisplayCanvas;
    gX_GUI_Canvas *messagesCanvas;
    gX_I32  menuCanvasIndex;
    gX_I32  debugDisplayCanvasIndex;
    gX_I32  messagesCanvasIndex;
    //gX_Heap canvasesSpecial;
    
    gX_GUI_Canvas_Transition transition;
    
    //compositing
    gX_RenderTexture_Wrapper buffer;
    gX_RenderTexture_Wrapper ping;
    gX_RenderTexture_Wrapper pong; // used by shader element for multi-pass ping-pong effects
    
    //opencode: compositing system — four-phase pipeline
    //  Phase 0: capture scene background
    //  Phase 1: draw canvases to buffers + composite to screen + accumulate to gui.ping
    //  Phase 2: composite background to screen (with optional inter-layer effects like blur)
    //  Phase 3: overlays (ImGui, FPS, messages)
    struct {
        gX_RenderTexture_Wrapper background;  // scene content at screen resolution
        gX_I1                  backgroundValid;
        //opencode: inter-layer blur effect (Phase 2b)
        gX_Shader              blurShader;
        gX_I32                 blurInputLoc;
        gX_I32                 blurResLoc;
        gX_I32                 blurColLoc;
        gX_I32                 blurUniformsLoc;
        gX_I32                 blurPasses;
        gX_I32                 blurTexSlot;   // dedicated texture unit for blur input
    } composite;
    
    gX_GUI_DirectDraw directdraw;
    
    //opencode: runtime blur radius for escape menu background
    gX_F32 blurRadius;
    
    //opencode: buffer pool for render texture recycling on resize
    //   Avoids GPU alloc/free cycles when window resolution changes.
//opencode: increased from 8 to 24 to accommodate 4 main GUI buffers + up to
//   16 canvas buffers during a resize, avoiding unnecessary GPU unload/reload.
#define GX_GUI_BUFFER_POOL_MAX 24
    struct {
        gX_RenderTexture_Wrapper rt;
        gX_U32 width, height;
        gX_I1  used;
        gX_I1  hdr;
    } bufferPool[GX_GUI_BUFFER_POOL_MAX];
    
    //opencode: keyboard focus / accessibility
    gX_I1  focusEnabled;
    gX_I1  toolsInputCaptured;
    gX_I32 focusCanvasID;   // -1 if no focus
    gX_I32 focusElementID;  // -1 if no focus
    //opencode: interaction scope — one modal canvas owns pointer and keyboard
    //   input while open; the previous focus is restored when it closes.
    //   A shallow stack (menu + dialog) lets a confirm dialog open over the
    //   pause menu; close pops back to the superseded modal.
    gX_I32 modalCanvasID;
    gX_I32 modalPreviousFocusCanvasID;
    gX_I32 modalPreviousFocusElementID;
    gX_I1  modalDismissOnOutside;
    gX_I1  focusFirstPending;
#define GX_GUI_MODAL_STACK 3
    struct {
        gX_I32 canvasID;
        gX_I1  dismissOnOutside;
        gX_I32 returnFocusCanvasID;
        gX_I32 returnFocusElementID;
    } modalStack[GX_GUI_MODAL_STACK];
    gX_U32 modalDepth;
    //opencode: modal dialog state — one confirm/alert at a time on its own
    //   canvas (blocked background via the modal scope above).
    gX_I1  dialogOpen;
    gX_I8  dialogTitle[128];
    gX_I8  dialogMessage[256];
    gX_I8  dialogOkLabel[32];
    gX_I8  dialogCancelLabel[32];
    gX_I1  dialogHasCancel;
    gX_I0 (*dialogOnOk)(gX_I0 *userData);
    gX_I0 *dialogOnOkData;
    gX_I0 (*dialogOnCancel)(gX_I0 *userData);
    gX_I0 *dialogOnCancelData;
    gX_I1  doNotClearDialog;
    gX_GUI_Canvas *dialogCanvas;
    gX_I32 dialogCanvasIndex;
    //opencode: left-pointer capture for sliders and drag widgets. IDs refer to
    //   stable heap slots and are invalidated when the owning canvas rebuilds.
    gX_I1  pointerCaptureActive;
    gX_I32 pointerCaptureCanvasID;
    gX_I32 pointerCaptureElementID;
    
    //opencode: event ring buffer — records GUI interactions for AI/script feedback
#define GX_GUI_EVENT_LOG_MAX 64
    gX_U32  eventHead;                        // write index
    gX_U32  eventCount;                       // total events written (for seq number)
    struct {
        gX_F64  time;                         // maindata.timeElapsed
        gX_I32  canvasID;
        gX_I32  elementID;
        gX_I8   type;                         // 'H' hover, 'C' click, 'F' focus
        gX_I8   detail[64];                   // element text or state info
    } eventLog[GX_GUI_EVENT_LOG_MAX];
    
    //opencode: element inspector — F7 toggle, shows element tree with rect/color/state
    gX_I1  inspectorEnabled;
    gX_I32 inspectorCanvasID;   // -1 if none
    gX_I32 inspectorElementID;
    //opencode: v0.6.9 — viewport (dock mode). Screen-space game rect: fullscreen
    //   normally; centered 4:3 fit when docked (16:9 window, tools in margins).
    //   canvasBox stays game-logical; viewport maps game<->screen (with scale).
    gX_Rectangle viewportBox;
    gX_I1  viewportDocked;
    //opencode: right-edge inset (px) the docked viewport must leave free for
    //   host panels (the GFX editor's inspector); 0 for every other template.
    gX_F32 viewportRightInset;
    //opencode: additional left-edge inset; the GFX editor widens/shrinks the
    //   dock with its scene panel and uses -320 for focus mode.
    gX_F32 viewportLeftInset;
    
#if GX_GUI_USE_POOLS
    //opencode: pool-based element/module system — experimental, replaces per-element
    //   sub-module heaps. gX_GUI_Pools_MigrateFromHeap copies old heap to pools.
    gX_Pool  poolCanvases;       // gX_PoolCanvas slots
    gX_Pool  poolElements;       // gX_PoolElement slots (all canvases share this)
    gX_Pool  poolTexts;          // gX_PoolText
    gX_Pool  poolShaders;        // gX_PoolShader
    gX_Pool  poolTextures;       // gX_PoolTexture
    gX_Pool  poolModels;         // gX_PoolModel
    gX_Pool  poolShapes;         // gX_PoolShape
    gX_Pool  poolAnims;          // gX_PoolAnim
    gX_Pool  poolDatas;          // gX_PoolData
    gX_Pool  poolSounds;         // gX_PoolSound
#endif
    
} gX_GUI;

extern gX_GUI gui;

gX_I32 gX_GUI_Load(gX_I0);
gX_I32 gX_GUI_Unload(gX_I0);

gX_I32 gX_GUI_Add_Canvas(gX_I0);
gX_I32 gX_GUI_Remove_Canvas(gX_U32 index);

gX_I32 gX_GUI_Transition_Clear(gX_I0);
gX_I32 gX_GUI_Transition_Setup(gX_I0);

//opencode: unified fade transition — sets up gui.transition fields, fires callback at midpoint
gX_I0 gX_GUI_Transition_Fade(gX_F32 outSec, gX_F32 midSec, gX_F32 inSec,
    gX_GUI_TransitionCallback onMidpoint, gX_I0 *userData);
gX_I1 gX_GUI_Transition_Fade_IsActive(gX_I0);
//opencode: v0.4.0 — fade-from-black entrance (single out phase, black->white)
gX_I0 gX_GUI_Transition_FadeFromBlack(gX_F32 secs);
//opencode: reset canvas offset/scale for all canvases (cleanup after position/scale transition)
gX_I0 gX_GUI_Canvas_ResetTransforms(gX_I0);

#if GX_GUI_USE_POOLS
gX_Pool_Handle gX_GUI_Pool_AddElement(gX_U32 canvasIndex);
gX_I0          gX_GUI_Canvas_Draw_Pools(gX_U32 canvasIndex);
gX_I0          gX_GUI_Pools_MigrateFromHeap(gX_I0);
#endif
//opencode: composite a canvas buffer to screen with ortho/viewport setup and forced batch flush
gX_I0 gX_GUI_CompositeToScreen(gX_Texture texture, gX_Rectangle dst, gX_F32v2 scale, gX_F32 angle, const gX_I8 *debugLabel);
//opencode: position/scale transition setup (animates canvasOffset/canvasScale)
gX_I0 gX_GUI_Transition_Slide(gX_F32 outSec, gX_F32 midSec, gX_F32 inSec,
    gX_F32v2 startOffset, gX_F32v2 endOffset,
    gX_GUI_TransitionCallback onMidpoint, gX_I0 *userData);
gX_I0 gX_GUI_Transition_Zoom(gX_F32 outSec, gX_F32 midSec, gX_F32 inSec,
    gX_F32v2 startScale, gX_F32v2 endScale,
    gX_GUI_TransitionCallback onMidpoint, gX_I0 *userData);
//opencode: v0.3.8 — style transition — same phase machine as Fade, plus a
//   chosen style (SKEW_SLIDE swings canvasAngle; wipe styles drive the
//   screen-space overlay drawn by gX_GUI_Transition_DrawOverlay)
gX_I0 gX_GUI_Transition_Style(gX_F32 outSec, gX_F32 midSec, gX_F32 inSec,
    gX_GUI_TransitionStyle style,
    gX_GUI_TransitionCallback onMidpoint, gX_I0 *userData);
gX_I0 gX_GUI_Transition_DrawOverlay(gX_I0);
//opencode: per-canvas stagger — pure evaluation of the out/middle/in curve
//   at a delay-offset local clock. Used by the update loop and unit tests.
gX_F32 gX_GUI_Transition_TotalSeconds(const gX_GUI_Canvas_Transition *tra);
gX_I1 gX_GUI_Transition_EvalLocal(const gX_GUI_Canvas_Transition *tra,
    gX_F32 localElapsed, gX_F32v4 *outColor, gX_F32v2 *outPos,
    gX_F32v2 *outScale, gX_F32 *outAngle);

//opencode: GUI event logging — records interactions for AI/script feedback
gX_I0 gX_GUI_EventLog_Add(gX_I32 canvasID, gX_I32 elementID, gX_I8 type, const gX_I8 *detail);
gX_I0 gX_GUI_EventLog_Dump(gX_I32 count);

//opencode: opt-in structured GUI audit trace (GX_GUI_TRACE=1). One
//   `GUITRACE:` line per interaction/lifecycle event — clicks, focus,
//   modal/dialog scope, title views, canvas builds and scene responses — so
//   scripted gates can assert behavior without pixel-only checks.
gX_I1 gX_GUI_Trace_Enabled(gX_I0);
gX_I0 gX_GUI_Trace_Event(const gX_I8 *type, gX_I32 canvasID, gX_I32 elementID,
    const gX_I8 *detail);
gX_I0 gX_GUI_Trace_Canvas(gX_I32 canvasIndex);
gX_I0 gX_GUI_DrawDebugOverlays(gX_I0); //opencode: debug element/canvas overlay  // dump last N events as [GUI_EVENT] log lines

gX_I0 gX_GUI_AddMake(gX_I32 makeindex, gX_I32 canvasIndex);
//opencode: v0.6.9 — viewport (dock): recompute box + game<->screen mapping.
//   No-op pixel-wise until docked (box = fullscreen).
gX_I0 gX_GUI_Viewport_Refresh(gX_I0);
//opencode: WM resize sync — returns true when the live client size was
//   applied (viewport re-fit + window-sized RT re-target). Cramped/duplicate
//   sizes are no-ops; the headless suite drives this directly.
gX_I1 gX_GUI_WindowResize_Sync(gX_I32 liveW, gX_I32 liveH);
//opencode: cramped dock -> overlay fallback (true when it flipped).
gX_I1 gX_GUI_Dock_Cramped_Check(gX_I0);
//opencode: reserve `inset` px at the window's right edge when computing the
//   docked viewport, so host panels never cover the scene. Refreshes only when
//   the value changes.
gX_I0 gX_GUI_Viewport_SetRightInset(gX_F32 inset);
gX_I0 gX_GUI_Viewport_SetLeftInset(gX_F32 inset);
gX_F32v2 gX_GUI_ScreenToGame(gX_F32 sx, gX_F32 sy);
gX_Rectangle gX_GUI_GameToScreen(gX_Rectangle r);
gX_Rectangle gX_GUI_ViewportBox_Get(gX_I0);
gX_I32 gX_GUI_Make(gX_I32 makeindex, gX_I32 canvasIndex);
gX_I0 gX_GUI_Diagnostic_Canvas(gX_I32 canvasIndex); //opencode: dump element rects/text to stdout
gX_I32 gX_GUI_Make_LoadFile(const gX_I8 *path, gX_I32 canvasIndex);
gX_I32 gX_GUI_Make_SaveFile(const gX_I8 *path, gX_I32 canvasIndex);

gX_I0 gX_GUI_Update(gX_I0);
gX_I0 gX_GUI_Draw(gX_I0);

//opencode: compositing pipeline phases
gX_I0 gX_GUI_Composite_CaptureBackground(gX_I0);
gX_I0 gX_GUI_Composite_DrawBackground(gX_I0);
gX_I1 gX_GUI_Composite_BackgroundValid(gX_I0);
gX_I0 gX_GUI_Composite_ApplyBlur(gX_I0);

//opencode: interaction foundation — modal input scope, exclusive focus, and
//   drag capture. Canvas IDs are current heap indices, element IDs are stable
//   element heap slots.
gX_I1 gX_GUI_Canvas_InputAllowed(gX_I32 canvasID);
gX_I0 gX_GUI_Focus_Clear(gX_I0);
gX_I1 gX_GUI_Focus_Set(gX_I32 canvasID, gX_I32 elementID);
gX_I1 gX_GUI_Focus_First(gX_I0);
gX_I32 gX_GUI_Modal_Open(gX_I32 canvasID, gX_I1 dismissOnOutside);
gX_I0 gX_GUI_Modal_Close(gX_I0);
gX_I1 gX_GUI_Modal_IsOpen(gX_I0);
//opencode: modal dialog — confirm/alert on the dialog canvas. Labels and
//   callbacks are copied/held, not owned. Returns 0 on open, -1 when a
//   dialog is already open or the modal stack is full.
gX_I32 gX_GUI_Dialog_Open(const gX_I8 *title, const gX_I8 *message,
    const gX_I8 *okLabel, const gX_I8 *cancelLabel,
    gX_I0 (*onOk)(gX_I0 *userData), gX_I0 *onOkData,
    gX_I0 (*onCancel)(gX_I0 *userData), gX_I0 *onCancelData);
gX_I0 gX_GUI_Dialog_Close(gX_I1 confirmed);
gX_I1 gX_GUI_Dialog_IsOpen(gX_I0);
gX_I0 gX_GUI_PointerCapture_Clear(gX_I0);
gX_I1 gX_GUI_PointerCapture_Begin(gX_I32 canvasID, gX_I32 elementID);
gX_I1 gX_GUI_PointerCapture_IsOwner(const gX_GUI_Canvas *canvas,
    const gX_GUI_Element *element);

gX_I0 gX_GUI_Load_ImGui(gX_I0);
gX_I0 gX_GUI_Unload_ImGui(gX_I0);
gX_I0 gX_GUI_Draw_ImGui(gX_I0);

//opencode: ephemeris instance binding — allows world scene to provide its own
struct gX_Ephemeris;
gX_I0 gX_GUI_Ephemeris_SetInstance(struct gX_Ephemeris *eph);
struct gX_Ephemeris* gX_GUI_Ephemeris_GetInstance(gX_I0);

//opencode: show a temporary on-screen toast message (auto-dismiss after 3s)
gX_I0 gX_GUI_ShowToast(const gX_I8 *msg);
//opencode: reset the toast auto-dismiss timer (called when a new toast replaces
//   an old one so it isn't dismissed almost immediately)
gX_I0 gX_GUI_Toast_ResetTimer(gX_I0);

//opencode: buffer pool — recycle render textures on resize instead of destroy+create
gX_RenderTexture_Wrapper gX_GUI_BufferPool_Fetch(gX_U32 width, gX_U32 height, gX_I1 hdr);
gX_I0                    gX_GUI_BufferPool_Stash(gX_RenderTexture_Wrapper rt, gX_U32 width, gX_U32 height, gX_I1 hdr);
gX_I0                    gX_GUI_BufferPool_Flush(gX_I0);

#ifdef __cplusplus
}
#endif

#endif //GUI_H
