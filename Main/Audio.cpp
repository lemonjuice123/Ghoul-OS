#include <Preferences.h>
#include "Audio.h"
#include "Config.h"

// =====================================================================
// Ghoul OS - Audio.cpp
// =====================================================================

static Preferences prefs;
static bool audioEnabled = true;

// A C4-D5 major-scale mapping for keys 1-9 -- a simple, recognizably
// "in tune" mini piano without needing 12-tone/sharp handling.
static const uint16_t pianoFreqs[9] = {262, 294, 330, 349, 392, 440, 494, 523, 587};
static const char* const pianoNames[9] = {"C4", "D4", "E4", "F4", "G4", "A4", "B4", "C5", "D5"};

void audioInit()
{
    // NVS namespace "ghoulos", read/write mode. Defaults to enabled on
    // a completely fresh flash (first-ever boot).
    prefs.begin("ghoulos", false);
    audioEnabled = prefs.getBool("audioOn", true);
}

void audioBeepNav()
{
    if (!audioEnabled)
    {
        return;
    }
    // Short, high click -- tone()'s duration argument is non-blocking
    // and auto-stops itself, so this never stalls key handling.
    tone(PIEZO_PIN, 1800, 25);
}

void audioPlayNote(uint8_t noteIndex)
{
    if (!audioEnabled || noteIndex >= 9)
    {
        return;
    }
    tone(PIEZO_PIN, pianoFreqs[noteIndex], 300);
}

void audioStop()
{
    noTone(PIEZO_PIN);
}

const char* audioNoteName(uint8_t noteIndex)
{
    return (noteIndex < 9) ? pianoNames[noteIndex] : "";
}

bool audioIsEnabled()
{
    return audioEnabled;
}

void audioSetEnabled(bool enabled)
{
    audioEnabled = enabled;
    prefs.putBool("audioOn", audioEnabled);
    if (!enabled)
    {
        audioStop();
    }
}
