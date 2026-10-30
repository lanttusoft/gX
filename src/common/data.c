/**********************************************************************************************
*
*   [gX: src/common/data.c]
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

#include "data.h"

const gX_I8 *gX_Data_String_gX =                        "gX";
const gX_I8 *gX_Data_String_gX_name =                   "gX: graphics eXtension or game eXperiment?";
const gX_I8 *gX_Data_String_gX_description =            "extends a graphics or game library";
const gX_I8 *gX_Data_String_gX_version =                "0.19.0 beta";
const gX_I8 *gX_Data_String_gX_license =                "GPL-3.0-or-later";
const gX_I8 *gX_Data_String_gX_copyright =              "Copyright © 2023-2026 lanttu";
const gX_I8 *gX_Data_String_gX_copyright_nosymbol =     "Copyright (c) 2023-2026 lanttu";
const gX_I8 *gX_Data_String_gX_credit =                 "by lanttu";

const gX_I8 *gX_Data_String_AI =                        "This program contains AI-generated code provided by opencode";

const gX_I8 *gX_Data_String_angelscript =               "angelscript";
const gX_I8 *gX_Data_String_angelscript_name =          "AngelScript";
const gX_I8 *gX_Data_String_angelscript_description =   "cross-platform scripting library";
const gX_I8 *gX_Data_String_angelscript_version =       "2.38.0";
const gX_I8 *gX_Data_String_angelscript_license =       "zlib/libpng license";
const gX_I8 *gX_Data_String_angelscript_copyright =     "Copyright © 2003-2024 Andreas Jönsson";
const gX_I8 *gX_Data_String_angelscript_copyright_nosymbol = "Copyright (c) 2003-2024 Andreas Jönsson";
const gX_I8 *gX_Data_String_angelscript_credit =        "by Andreas Jönsson";

const gX_I8 *gX_Data_String_jsonc =                     "json-c";
const gX_I8 *gX_Data_String_jsonc_name =                "json-c";
const gX_I8 *gX_Data_String_jsonc_description =         "JSON library for C";
const gX_I8 *gX_Data_String_jsonc_license =             "MIT license";

const gX_I8 *gX_Data_String_imgui =                     "ImGui";
const gX_I8 *gX_Data_String_imgui_name =                "Dear ImGui";
const gX_I8 *gX_Data_String_imgui_description =         "bloat-free graphical user interface for c++ with minimal dependencies";
const gX_I8 *gX_Data_String_imgui_license =             "MIT license";
const gX_I8 *gX_Data_String_imgui_copyright =           "Copyright © 2014-2025 Omar Cornut";
const gX_I8 *gX_Data_String_imgui_copyright_nosymbol =  "Copyright (c) 2014-2025 Omar Cornut";
const gX_I8 *gX_Data_String_imgui_github =              "https://github.com/ocornut/imgui";
const gX_I8 *gX_Data_String_imgui_credit =              "by Omar Cornut";

const gX_I8 *gX_Data_String_raylib =                    "raylib";
const gX_I8 *gX_Data_String_raylib_name =               "raylib 6.0";
const gX_I8 *gX_Data_String_raylib_description =        "simple and easy-to-use library to enjoy videogames programming.";
const gX_I8 *gX_Data_String_raylib_version =            "6.0";
const gX_I8 *gX_Data_String_raylib_license =            "unmodified zlib/libpng license";
const gX_I8 *gX_Data_String_raylib_copyright =          "Copyright © 2013-2026 Ramon Santamaria (@raysan5)";
const gX_I8 *gX_Data_String_raylib_copyright_nosymbol = "Copyright (c) 2013-2026 Ramon Santamaria (@raysan5)";
const gX_I8 *gX_Data_String_raylib_website =            "https://www.raylib.com";
const gX_I8 *gX_Data_String_raylib_github =             "https://github.com/raysan5/raylib";
const gX_I8 *gX_Data_String_raylib_credit =             "by Ramon Santamaria (@raysan5)";


const gX_I8 *gX_Data_String_rlimgui =                   "rlimgui";
const gX_I8 *gX_Data_String_rlimgui_name =              "rlimgui";
const gX_I8 *gX_Data_String_rlimgui_description =       "Dear ImGui backend for raylib";
const gX_I8 *gX_Data_String_rlimgui_license =           "permissive";
const gX_I8 *gX_Data_String_rlimgui_copyright =         "Copyright © 2020-2021 Jeffery Myers";
const gX_I8 *gX_Data_String_rlimgui_copyright_nosymbol = "Copyright (c) 2020-2021 Jeffery Myers";

//for loading fonts
const gX_I8 *gX_Data_String_uppercase =          "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
const gX_I8 *gX_Data_String_uppercase_nordic =   "ÅÄÖÆŒØ";
const gX_I8 *gX_Data_String_lowercase =          "abcdefghijklmnopqrstuvwxyz";
const gX_I8 *gX_Data_String_lowercase_nordic =   "åäöæœø";
const gX_I8 *gX_Data_String_numbers =            "0123456789";

const gX_I8 *gX_Data_String_hiragana = "あいうえおぁぃぅぇぉかきくけこさしすせそたちつてとなにぬねのはひふへほまみむめもやゆよゃゅょらりるれろわゐゑをがぎぐげござじずぜぞだぢづでどばびぶべぼぱぴぷぺぽゔっんゝゞ";
const gX_I8 *gX_Data_String_katakana = "アイウエオァィゥェォカキクケコサシスセソタチツテトナニヌネノハヒフヘホマミムメモヤユヨャュョラリルレロワヰヱヲガギグゲゴザジズゼゾダヂヅデドバビブベボパピプペポヴッンヽヾー・";
const gX_I8 *gX_Data_String_kanji = "一二三四五六七八九十百千万億年日月火水木金土山川天気学校先生言語日本東京大小高低長短新旧白黒赤青花鳥名前東西南北内外上下左右男女父母子友人生世界時間毎週今明日昨後前行来出入見聞食飲読書話買物電車駅道店会社員手足目口耳力音色香味楽愛感謝幸笑顔春夏秋冬風雪雲星空海光夜色朝昼晩夜半分数回線画面画面設定開始終了保存読込書出印刷変換実行停止追加削除変更検索結果確認応答質問回答理由目的手段方法効果影響関係差別自由平等権利義務責任安全安心必要十分簡単複雑可能不可能当然自然必然偶然突然徐々段階的連続的断続的規則的不規則的積極的消極的主観的客観的普遍的個別的具体的抽象的直接的間接的絶対的相对的基本的応用的理論的実践的歴史的文化的経済的政治的社会科学技術情報通信環境資源材料構造機能性能品質量度範囲境界条件状態様子模様形種類性質特徴特性個性共通類似同一異同対比対照対立矛盾統一調和安定不安定流動変化発展進歩退歩向上低下増加減少拡大縮小集中分散結合分離融合混合純粋混在共存競争協力支援援助保護育成指導教育学習研究開発設計製造管理運営運用保守修復改善改革革新創造表現描写叙述説明論証証明批判評価判断決断選択採択採用応用適応適用調整整備準備用意計画立案実行実践経験練習訓練鍛錬修行努力忍耐辛抱我慢勤勉怠惰習慣伝統文化芸術美術音楽文学哲学宗教科学数学物理学化学生物学地理歴史考古学言語学心理学社会学経済学政治学医学工学農学";
