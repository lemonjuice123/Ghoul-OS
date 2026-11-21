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

// ---------------------------------------------------------------------
// Boot jingle -- a placeholder "power-on" motif: a rising arpeggio, a
// short breath, then a falling answer that lands back on the root. Each
// entry is one note; dur is how long it sounds and the sequencer waits
// BOOT_TUNE_GAP_MS after it before the next step. Change the notes here
// to make it your own.
// ---------------------------------------------------------------------
struct BootNote
{
    uint16_t freq; // Hz (0 = rest)
    uint16_t dur;  // ms
};

static const BootNote bootTune[] = {
    { 440,  90 },  // A4
    { 554,  90 },  // C#5
    { 659,  90 },  // E5
    { 880, 150 },  // A5
    { 784,  90 },  // G5
    { 659,  90 },  // E5
    { 880, 190 },  // A5
    { 0,   110 },  // breath
    { 1047, 120 }, // C6
    { 880,  90 },  // A5
    { 659,  90 },  // E5
    { 440, 280 }   // A4 -- root, lets it ring out
};

#define BOOT_TUNE_COUNT  (sizeof(bootTune) / sizeof(bootTune[0]))
#define BOOT_TUNE_GAP_MS 16

static bool          bootTunePlaying = false;
static uint8_t       bootTuneStep = 0;
static unsigned long bootTuneNext = 0;

void audioBootTuneStart()
{
    bootTuneStep = 0;
    bootTuneNext = millis();
    bootTunePlaying = audioEnabled;
}

void audioBootTuneService()
{
    if (!bootTunePlaying)
    {
        return;
    }

    unsigned long now = millis();
    if ((long)(now - bootTuneNext) < 0)
    {
        return;
    }

    if (bootTuneStep >= BOOT_TUNE_COUNT)
    {
        bootTunePlaying = false;
        return;
    }

    const BootNote &note = bootTune[bootTuneStep++];
    if (note.freq > 0)
    {
        // tone() with a duration is non-blocking and stops itself, so the
        // splash animation never stalls waiting for a note to finish.
        tone(PIEZO_PIN, note.freq, note.dur);
    }
    else
    {
        audioStop();
    }

    bootTuneNext = now + note.dur + BOOT_TUNE_GAP_MS;
}

void audioBootTuneStop()
{
    bootTunePlaying = false;
    audioStop();
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
