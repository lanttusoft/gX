/**********************************************************************************************
* 
*   [common/types_audio.h]
*   Uses borrowed code from: raylib v5.x
*   All instances are marked with respective labels
*
*   Copyright (c) 2025 lanttu
* 
*   [raylib]
*   LICENSE: zlib/libpng
*
*   raylib is licensed under an unmodified zlib/libpng license, which is an OSI-certified,
*   BSD-like license that allows static linking with closed source software:
*
*   Copyright (c) 2013-2023 Ramon Santamaria (@raysan5)
*
*   This software is provided "as-is", without any express or implied warranty. In no event
*   will the authors be held liable for any damages arising from the use of this software.
*
*   Permission is granted to anyone to use this software for any purpose, including commercial
*   applications, and to alter it and redistribute it freely, subject to the following restrictions:
*
*     1. The origin of this software must not be misrepresented; you must not claim that you
*     wrote the original software. If you use this software in a product, an acknowledgment
*     in the product documentation would be appreciated but is not required.
*
*     2. Altered source versions must be plainly marked as follows: as being the original software.
*
*     3. This notice may not be removed or altered from any source distribution.
*
**********************************************************************************************/

#ifndef TYPES_AUDIO_H
#define TYPES_AUDIO_H

#ifdef __cplusplus
extern "C" {
#endif

#include "primary.h"
    
// [raylib] borrowed
// Wave, audio wave data
typedef struct gX_Wave {
    gX_U32 frameCount;    // Total number of frames (considering channels)
    gX_U32 sampleRate;    // Frequency (samples per second)
    gX_U32 sampleSize;    // Bit depth (bits per sample): 8, 16, 32 (24 not supported)
    gX_U32 channels;      // Number of channels (1-mono, 2-stereo, ...)
    gX_I0 *data;          // Buffer data pointer
} gX_Wave;

// [raylib] borrowed
// Opaque structs declaration
// NOTE: Actual structs are defined internally in raudio module
typedef struct rAudioBuffer gX_AudioBuffer;
typedef struct rAudioProcessor gX_AudioProcessor;

// [raylib] borrowed
// AudioStream, custom audio stream
typedef struct gX_AudioStream {
    gX_AudioBuffer *buffer;       // Pointer to internal data used by the audio system
    gX_AudioProcessor *processor; // Pointer to internal data processor, useful for audio effects

    gX_U32 sampleRate;    // Frequency (samples per second)
    gX_U32 sampleSize;    // Bit depth (bits per sample): 8, 16, 32 (24 not supported)
    gX_U32 channels;      // Number of channels (1-mono, 2-stereo, ...)
} gX_AudioStream;

// [raylib] borrowed
// Sound
typedef struct gX_Sound {
    gX_AudioStream stream;  // Audio stream
    gX_U32 frameCount;      // Total number of frames (considering channels)
} gX_Sound;

typedef struct gX_Sound_Parameters {
    gX_I1 play;
    gX_I1 pause;
    gX_I1 stop;
    gX_I1 resume;
    gX_I1 playing;
    gX_F32 length;
    gX_F32 played;
    gX_F32 position;
    gX_F32 volume;
    gX_F32 pitch;
    gX_F32 pan;
} gX_Sound_Parameters;

// [raylib] borrowed
// Music, audio stream, anything longer than ~10 seconds should be streamed
typedef struct gX_Music {
    gX_AudioStream stream;  // Audio stream
    gX_U32 frameCount;      // Total number of frames (considering channels)
    gX_I1 looping;          // Music looping enable

    gX_I32 ctxType;         // Type of music context (audio filetype)
    gX_I0 *ctxData;         // Audio context data, depends on type
} gX_Music;
    
#ifdef __cplusplus
}
#endif

#endif //TYPES_AUDIO_H
