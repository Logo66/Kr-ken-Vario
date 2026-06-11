#pragma once
// sound_settings.h — GEMEINSAMER Ton-Settings-Ort in NVS (Ticket §4: eine Quelle, zwei Bedienwege).
//  • Flug  = Touch  -> schreibt nur Lautstaerke (0 = STUMM .. 5).
//  • Boden = Web/App -> fuellt spaeter Schwellen/Tonkurve/Totzone (hier nur reserviert mit Defaults).
// KEIN NVS-Key-Wirrwarr: alles im Namespace "sound", ein Struct.
#include <Preferences.h>

static const uint8_t SND_VOL_MAX = 5;          // Stufen 1..5, 0 = STUMM

struct SoundSettings {
    // --- Flug-Bedienung (Touch) ---
    uint8_t volume = 3;            // 0 = STUMM, 1..SND_VOL_MAX
    // --- Boden-Konfig (Web/App, spaeteres Ticket — hier nur Platzhalter mit sinnvollen Defaults) ---
    float   climb_threshold = 0.2f;   // m/s, ab hier Steigton
    float   sink_alarm      = -3.0f;  // m/s, Sinkalarm-Schwelle
    float   deadband        = 0.1f;   // m/s, Totzone um 0
    uint8_t tone_curve      = 0;      // 0 = Standard-Tonkurve
};

static SoundSettings g_sound;
static Preferences   soundPrefs;

static void soundSettingsLoad() {
    soundPrefs.begin("sound", true);                 // read-only
    g_sound.volume          = soundPrefs.getUChar("vol",   3);
    g_sound.climb_threshold = soundPrefs.getFloat("climb", 0.2f);
    g_sound.sink_alarm      = soundPrefs.getFloat("sink", -3.0f);
    g_sound.deadband        = soundPrefs.getFloat("dead",  0.1f);
    g_sound.tone_curve      = soundPrefs.getUChar("curve", 0);
    soundPrefs.end();
    if (g_sound.volume > SND_VOL_MAX) g_sound.volume = 3;   // Schutz gegen Murks
}

static void soundSettingsSave() {
    soundPrefs.begin("sound", false);
    soundPrefs.putUChar("vol",   g_sound.volume);
    soundPrefs.putFloat("climb", g_sound.climb_threshold);
    soundPrefs.putFloat("sink",  g_sound.sink_alarm);
    soundPrefs.putFloat("dead",  g_sound.deadband);
    soundPrefs.putUChar("curve", g_sound.tone_curve);
    soundPrefs.end();
}

static bool soundMuted() { return g_sound.volume == 0; }
