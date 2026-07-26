#ifndef AUDIO_H
#define AUDIO_H

#include <Arduino.h>

// =====================================================================
// Ghoul OS - Audio.h
//
// Drives a passive piezo buzzer on PIEZO_PIN (see Config.h) using the
// standard Arduino tone()/noTone() API, which the ESP32 core implements
// on top of its PWM hardware -- so this file never has to manage PWM
// channels/resolution itself, keeping it simple and portable across
// ESP32 core versions.
//
// Two things use it:
//   - A short "click" on every navigation key press (Up/Down/Select/Back),
//     the way many small handheld devices give audio feedback.
//   - The Music app's 9-key mini piano (keys 1-9).
//
// Whether audio feedback is enabled is stored in NVS flash (via the
// Preferences library) under namespace "ghoulos", key "audioOn", so the
// choice made in Settings survives a power cycle.
// =====================================================================

// Initializes persisted state. Call once from setup().
void audioInit();

// Short click/beep played on every Up/Down/Select/Back press. Does
// nothing if audio feedback is currently disabled.
void audioBeepNav();

// Plays the mini-piano tone for noteIndex (0-8, mapped to a C4-D5
// major scale). Does nothing if noteIndex is out of range or audio
// feedback is currently disabled.
void audioPlayNote(uint8_t noteIndex);

// Immediately silences the buzzer.
void audioStop();

// Returns the note name for noteIndex (0-8), e.g. "C4", "G4", "D5".
// Returns an empty string if noteIndex is out of range.
const char* audioNoteName(uint8_t noteIndex);

// Current audio-feedback on/off state.
bool audioIsEnabled();

// Changes the audio-feedback on/off state and persists it to flash so
// it's remembered on the next boot.
void audioSetEnabled(bool enabled);

#endif // AUDIO_H
