
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <dwmapi.h>
#include <uxtheme.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include "bass.h"
#include "bassloud.h"
#include "bassmix.h"
#include "basswasapi.h"
#include "bassasio.h"

#include "resource.h"

#pragma comment(lib, "bass.lib")
#pragma comment(lib, "bassloud.lib")
#pragma comment(lib, "bassmix.lib")
#pragma comment(lib, "basswasapi.lib")
#pragma comment(lib, "bassasio.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

namespace {

enum : int {
    ID_BTN_ADD = 1001,
    ID_BTN_REMOVE,
    ID_BTN_CLEAR,
    ID_BTN_PLAY,
    ID_BTN_STOP,
    ID_BTN_PREV,
    ID_BTN_NEXT,
    ID_CHK_NEXT,
    ID_CHK_DELAY,
    ID_SEEK,
    ID_VOL,
    ID_PLAYLIST,
    ID_LBL_TITLE,
    ID_LBL_TIME,
    ID_BTN_SAMPLER,
    ID_BTN_METER,
    ID_BTN_PLAYLIST1,
    ID_BTN_PLAYLIST2,
    ID_BTN_PLAYLIST3,
    ID_BTN_SETTINGS,

    ID_MENU_PLAY_NEXT = 1300,
    ID_MENU_REMOVE,
    ID_MENU_EXPORT,
    ID_MENU_MOVE_TO_PLAYLIST0 = 1310,
    ID_MENU_COPY_TO_PLAYLIST0 = 1320,
    ID_TIMER_UPDATE = 1
};

const int kUiPresetCount = 3;
const float kUiPresets[kUiPresetCount] = {0.70f, 0.85f, 1.00f};
const wchar_t* const kUiPresetNames[kUiPresetCount] = {
    L"Compact (70%)", L"Normal (85%)", L"Large (100%)"};
int g_uiPreset = 2;
float g_uiScale = 1.0f;

int S(int px) {
    return static_cast<int>(px * g_uiScale + (px >= 0 ? 0.5f : -0.5f));
}

const int kPadCount = 10;
const int ID_SMP_PAD0  = 1100;
const int ID_SMP_LOAD0 = 1110;
const int ID_SMP_VOL0  = 1120;
const int ID_SMP_CLEAR0 = 1130;
const int ID_SMP_RANGE0 = 1140;
const int ID_WAVE_PLAY = 1200;
const int ID_WAVE_RANGE = 1201;
const int ID_WAVE_SAVE = 1202;
const int ID_WAVE_SET_A = 1203;
const int ID_WAVE_SET_B = 1204;
const UINT WM_APP_PAD_STOP = WM_APP + 1;
const UINT WM_APP_EXPORT_DONE = WM_APP + 2;
const UINT_PTR ID_TIMER_PAD_STOP0 = 10;
const UINT_PTR ID_TIMER_SAMPLER = 2;
const UINT_PTR ID_TIMER_PAD_EDGE = 4;
const UINT_PTR ID_TIMER_WAVE = 5;
const UINT_PTR ID_TIMER_PAD_FILL = 6;
const UINT_PTR ID_TIMER_METER = 3;
const int kPadDefaultVolume = 50;

const int SLIDER_MAX = 1000;
const UINT_PTR TIMER_PERIOD_MS = 100;
const int kDefaultDelaySeconds = 2;
const int kMinDelaySeconds = 1;
const int kMaxDelaySeconds = 5;

const wchar_t* PauseDurationOptionText(int seconds) {
    switch (seconds) {
    case 1: return L"1 second";
    case 2: return L"2 seconds";
    case 3: return L"3 seconds";
    case 4: return L"4 seconds";
    case 5: return L"5 seconds";
    default: return L"";
    }
}

struct Track {
    std::wstring path;
    std::wstring name;
    double length = -1.0;
    bool missing = false;
    double lufs = 0.0;
    bool loudnessDone = false;
};

HWND g_hwnd = nullptr;
std::atomic<HWND> g_callbackHwnd{nullptr};
HWND g_btnAdd = nullptr;
HWND g_btnRemove = nullptr;
HWND g_btnClear = nullptr;
HWND g_btnPlay = nullptr;
HWND g_btnStop = nullptr;
HWND g_btnPrev = nullptr;
HWND g_btnNext = nullptr;
HWND g_chkNext = nullptr;
HWND g_seek = nullptr;
HWND g_vol = nullptr;
HWND g_list = nullptr;
HWND g_lblTitle = nullptr;
HWND g_lblTime = nullptr;
HWND g_btnSettings = nullptr;
HWND g_settingsWnd = nullptr;
HWND g_chkDelay = nullptr;
HFONT g_font = nullptr;
HFONT g_listFont = nullptr;
HFONT g_labelFont = nullptr;

// ---------------------------------------------------------------- sampler

struct SamplerPad {
    std::wstring path;
    DWORD stream = 0;
    BOOL isMusic = FALSE;
    bool glow = false;
    bool fading = false;
    double start = 0.0;
    double stop = 1.0;
    HSYNC stopSync = 0;
};

SamplerPad g_pads[kPadCount];

int g_padVolume[kPadCount] = {kPadDefaultVolume, kPadDefaultVolume, kPadDefaultVolume,
                              kPadDefaultVolume, kPadDefaultVolume, kPadDefaultVolume,
                              kPadDefaultVolume, kPadDefaultVolume, kPadDefaultVolume,
                              kPadDefaultVolume};

HWND g_smpWnd = nullptr;
HWND g_smpPad[kPadCount] = {};
HWND g_smpLoad[kPadCount] = {};
HWND g_smpClear[kPadCount] = {};
HWND g_smpRange[kPadCount] = {};
HWND g_smpVol[kPadCount] = {};
HWND g_btnSampler = nullptr;
BOOL g_smpGlow = FALSE;

HWND g_waveWnd = nullptr;
int g_wavePad = -1;
DWORD g_waveAudition = 0;

bool WaveWindowOpen();
void OpenWaveWindow(int pad);
void OpenWaveExport(const std::wstring& path);
void WaveFreeAudition();

// ---------------------------------------------------------------- playlists

const int kPlaylistCount = 3;
struct PlaylistState {
    std::vector<Track> tracks;
    int currentIndex = -1;
    size_t probeIndex = 0;
};
PlaylistState g_playlists[kPlaylistCount];
int g_activePlaylist = 0;
int g_playPlaylist = 0;
BOOL g_playingTrackDetached = FALSE;
Track g_detachedPlayingTrack;
HWND g_btnPl[kPlaylistCount] = {};

std::vector<Track>& VisibleTracks() { return g_playlists[g_activePlaylist].tracks; }
std::vector<Track>& PlayingTracks() { return g_playlists[g_playPlaylist].tracks; }
int& PlayingIndex() { return g_playlists[g_playPlaylist].currentIndex; }

const Track* CurrentPlayingTrack() {
    std::vector<Track>& tracks = PlayingTracks();
    const int index = PlayingIndex();
    if (index >= 0 && index < static_cast<int>(tracks.size()))
        return &tracks[index];
    return g_playingTrackDetached ? &g_detachedPlayingTrack : nullptr;
}

int g_dragFrom = -1;
int g_dragTarget = -1;
BOOL g_dragActive = FALSE;
POINT g_dragStart = {0, 0};

DWORD g_channel = 0;
BOOL g_isMusic = FALSE;

BOOL g_mixerMode = FALSE;
BOOL g_wasapiExclusive = FALSE;
BOOL g_asioMode = FALSE;
int g_asioMixerChans = 2;
HSTREAM g_outputMixer = 0;
BOOL g_audioInitialized = FALSE;
BOOL g_deckPaused = FALSE;
std::wstring g_outputDeviceId;
std::wstring g_outputDeviceName;
std::wstring g_asioDeviceId;
std::wstring g_asioDeviceName;

enum AudioOutputMode {
    kAudioOutputAuto = 0,
    kAudioOutputShared,
    kAudioOutputExclusive
};

enum AudioOutputBackend {
    kAudioBackendWasapi = 0,
    kAudioBackendAsio
};

int g_outputMode = kAudioOutputAuto;
int g_outputBackend = kAudioBackendWasapi;
std::wstring g_importDirectory;

BOOL g_awakeHeld = FALSE;

BOOL g_dragging = FALSE;
BOOL g_idle = FALSE;

BOOL  g_seekPending = FALSE;
QWORD g_seekTarget = 0;
ULONGLONG g_seekTick = 0;

const int kDefaultVolume = 5;
int g_volume = kDefaultVolume;
BOOL g_autoNext = FALSE;
BOOL g_delayOn = FALSE;
int g_delaySeconds = kDefaultDelaySeconds;
int g_movedPlayingNext = -1;
BOOL g_playlistMenuOpen = FALSE;

BOOL g_pendingStart = FALSE;
BOOL g_pendingResume = FALSE;
int g_pendingIndex = -1;
int g_pendingPlaylist = 0;
int g_pendingTicks = 0;

int DelayTicks() {
    return g_delaySeconds * 1000 / static_cast<int>(TIMER_PERIOD_MS);
}

BOOL g_uiPlaying = FALSE;
BOOL g_uiPaused = FALSE;
BOOL g_blinkOn = FALSE;
int g_blinkCount = 0;

const wchar_t* kAudioFilter =
    L"Audio files (*.mp3;*.ogg;*.wav;*.aiff)\0*.mp3;*.mp2;*.mp1;*.ogg;*.wav;*.aif;*.aiff\0"
    L"MOD music (*.mod;*.xm;*.s3m;*.it;*.mo3)\0*.mod;*.xm;*.s3m;*.it;*.mtm;*.umx;*.mo3\0"
    L"All files (*.*)\0*.*\0\0";

// ---------------------------------------------------------------- utilities

std::wstring FileNameOf(const std::wstring& path) {
    const size_t slash = path.find_last_of(L"\\/");
    return (slash == std::wstring::npos) ? path : path.substr(slash + 1);
}

std::wstring DirectoryOf(const std::wstring& path) {
    const size_t slash = path.find_last_of(L"\\/");
    if (slash == std::wstring::npos) return L".";
    if (slash == 0) return path.substr(0, 1);
    return path.substr(0, slash);
}

std::wstring FileStemOf(const std::wstring& path) {
    const std::wstring name = FileNameOf(path);
    const size_t dot = name.find_last_of(L'.');
    return (dot == std::wstring::npos || dot == 0) ? name : name.substr(0, dot);
}

std::wstring JoinPath(const std::wstring& directory, const std::wstring& name) {
    if (directory.empty()) return name;
    const wchar_t last = directory.back();
    if (last == L'\\' || last == L'/') return directory + name;
    return directory + L"\\" + name;
}

bool HasExtension(const std::wstring& path, const wchar_t* extension) {
    const size_t dot = path.find_last_of(L'.');
    return dot != std::wstring::npos && _wcsicmp(path.c_str() + dot, extension) == 0;
}

void FormatTime(double seconds, wchar_t* out, size_t cch) {
    if (!(seconds > 0.0)) seconds = 0.0;
    constexpr double maxSeconds =
        static_cast<double>((std::numeric_limits<int>::max)() - 1);
    if (seconds > maxSeconds) seconds = maxSeconds;
    const int total = static_cast<int>(seconds + 0.5);
    const int h = total / 3600;
    const int m = (total % 3600) / 60;
    const int s = total % 60;
    if (h > 0)
        swprintf_s(out, cch, L"%d h %02d", h, m);
    else
        swprintf_s(out, cch, L"%d : %02d", m, s);
}

bool AutoNextEnabled() {
    return g_autoNext != FALSE;
}

int AutoNextIndex() {
    const std::vector<Track>& tracks = PlayingTracks();
    const int count = static_cast<int>(tracks.size());
    const int current = PlayingIndex();
    if (count == 0) return -1;

    int start = -1;
    if (g_movedPlayingNext >= 0 && g_movedPlayingNext < count) {
        start = g_movedPlayingNext;
    } else {
        if (count < 2 || current < 0 || current >= count) return -1;
        start = (current + 1) % count;
    }

    for (int step = 0; step < count; ++step) {
        const int index = (start + step) % count;
        if (!tracks[index].missing) return index;
    }
    return -1;
}

void UpdateRemoveButtonState();
HFONT UiFont();
void RefreshPlayButton(BOOL advanceBlink);
void CancelPendingStart();
void ApplyAutoVolume();
void SaveSettings();
std::wstring Utf8ToWStr(const std::string& s);
std::string WStrToUtf8(const std::wstring& s);
void DrawFocusRing(HDC dc, const RECT& rc, COLORREF color);
void ShowPlaylistMenu(HWND list, int index, POINT screenPt);

// ---------------------------------------------------------------- audio output

DWORD CALLBACK WasapiProc(void* buffer, DWORD length, void*) {
    const DWORD got = g_outputMixer ? BASS_ChannelGetData(g_outputMixer, buffer, length) : 0;
    return got;
}

DWORD CALLBACK WasapiLatencyProc(HSTREAM, void*) {
    const DWORD buffered = BASS_WASAPI_GetData(nullptr, BASS_DATA_AVAILABLE);
    return (buffered == static_cast<DWORD>(-1)) ? 0 : buffered;
}

BOOL FindWasapiOutputDevice(int* device, DWORD* mixfreq, std::wstring* name) {
    BASS_WASAPI_DEVICEINFO info = {};
    int fallbackDevice = -1;
    int firstDevice = -1;
    DWORD fallbackFreq = 48000;
    DWORD firstFreq = 48000;
    std::wstring fallbackName;
    std::wstring firstName;
    for (int i = 0; BASS_WASAPI_GetDeviceInfo(i, &info); ++i) {
        if (!(info.flags & BASS_DEVICE_ENABLED)) continue;
        if (info.flags & (BASS_DEVICE_INPUT | BASS_DEVICE_LOOPBACK)) continue;
        const std::wstring deviceName = info.name ? Utf8ToWStr(info.name) : std::wstring();
        const std::wstring deviceId = info.id ? Utf8ToWStr(info.id) : std::wstring();
        if (!g_outputDeviceId.empty() && _wcsicmp(deviceId.c_str(), g_outputDeviceId.c_str()) == 0) {
            if (device) *device = i;
            if (mixfreq) *mixfreq = info.mixfreq ? info.mixfreq : 48000;
            if (name) *name = deviceName;
            return TRUE;
        }
        if (firstDevice < 0) {
            firstDevice = i;
            firstFreq = info.mixfreq ? info.mixfreq : 48000;
            firstName = deviceName;
        }
        if ((info.flags & BASS_DEVICE_DEFAULT) && fallbackDevice < 0) {
            fallbackDevice = i;
            fallbackFreq = info.mixfreq ? info.mixfreq : 48000;
            fallbackName = deviceName;
        }
    }
    if (fallbackDevice < 0) {
        fallbackDevice = firstDevice;
        fallbackFreq = firstFreq;
        fallbackName = firstName;
    }
    if (fallbackDevice < 0) return FALSE;
    if (device) *device = fallbackDevice;
    if (mixfreq) *mixfreq = fallbackFreq;
    if (name) *name = fallbackName;
    return TRUE;
}

BOOL TryInitWasapiOutput(int device, BOOL exclusive) {
    const DWORD wasapiFlags = BASS_WASAPI_AUTOFORMAT | BASS_WASAPI_BUFFER;
    const float buffers[2] = {0.2f, 0.3f};
    const float periods[2] = {0.03f, 0.05f};
    for (int attempt = 0; attempt < 2; ++attempt) {
        const DWORD flags = wasapiFlags | (exclusive ? BASS_WASAPI_EXCLUSIVE : 0);
        if (!BASS_WASAPI_Init(device, 0, 0, flags, buffers[attempt], periods[attempt],
                              WasapiProc, nullptr)) {
            continue;
        }

        BASS_WASAPI_INFO wi = {};
        if (!BASS_WASAPI_GetInfo(&wi) || !wi.freq || !wi.chans) {
            BASS_WASAPI_Free();
            continue;
        }
        g_outputMixer = BASS_Mixer_StreamCreate(wi.freq, wi.chans,
                                                BASS_SAMPLE_FLOAT | BASS_STREAM_DECODE |
                                                BASS_MIXER_POSEX | BASS_MIXER_NONSTOP);
        if (g_outputMixer)
            BASS_Mixer_StreamSetLatency(g_outputMixer, WasapiLatencyProc, nullptr);
        if (g_outputMixer && BASS_WASAPI_Start()) {
            g_mixerMode = TRUE;
            g_wasapiExclusive = exclusive;
            return TRUE;
        }
        BASS_WASAPI_Free();
        if (g_outputMixer) {
            BASS_StreamFree(g_outputMixer);
            g_outputMixer = 0;
        }
    }
    return FALSE;
}

// ------- ASIO audio output

DWORD CALLBACK AsioLatencyProc(HSTREAM, void*) {
    const DWORD samples = BASS_ASIO_GetLatency(FALSE);
    if (samples == static_cast<DWORD>(-1) || g_asioMixerChans <= 0) return 0;
    return samples * static_cast<DWORD>(g_asioMixerChans) * 4u;
}

BOOL FindAsioOutputDevice(int* device, std::wstring* deviceId, std::wstring* deviceName) {
    BASS_ASIO_DEVICEINFO info = {};
    int firstDevice = -1;
    std::wstring firstId;
    std::wstring firstName;
    for (int i = 0; BASS_ASIO_GetDeviceInfo(i, &info); ++i) {
        const std::wstring driver = info.driver ? Utf8ToWStr(info.driver) : std::wstring();
        const std::wstring name = info.name ? Utf8ToWStr(info.name) : driver;
        const std::wstring id = driver.empty() ? name : driver;
        if (firstDevice < 0) {
            firstDevice = i;
            firstId = id;
            firstName = name;
        }
        if (!g_asioDeviceId.empty() && _wcsicmp(id.c_str(), g_asioDeviceId.c_str()) == 0) {
            if (device) *device = i;
            if (deviceId) *deviceId = id;
            if (deviceName) *deviceName = name;
            return TRUE;
        }
    }
    if (firstDevice < 0) return FALSE;
    if (device) *device = firstDevice;
    if (deviceId) *deviceId = firstId;
    if (deviceName) *deviceName = firstName;
    return TRUE;
}

BOOL TryInitAsioOutput() {
    int device = -1;
    std::wstring deviceId;
    std::wstring deviceName;
    if (!FindAsioOutputDevice(&device, &deviceId, &deviceName)) return FALSE;
    if (!BASS_ASIO_Init(device, BASS_ASIO_THREAD)) return FALSE;

    BASS_ASIO_INFO ai = {};
    if (!BASS_ASIO_GetInfo(&ai) || !ai.outputs) {
        BASS_ASIO_Free();
        return FALSE;
    }

    double rate = 48000.0;
    if (!BASS_ASIO_CheckRate(rate)) {
        const double current = BASS_ASIO_GetRate();
        if (current > 0.0) rate = current;
    }
    BASS_ASIO_SetRate(rate);
    const DWORD mixfreq = static_cast<DWORD>(rate + 0.5);
    const int mixchans = ai.outputs >= 2 ? 2 : 1;

    if (!BASS_Init(0, mixfreq, 0, nullptr, nullptr)) {
        BASS_ASIO_Free();
        return FALSE;
    }

    g_outputMixer = BASS_Mixer_StreamCreate(mixfreq, mixchans,
                                            BASS_SAMPLE_FLOAT | BASS_STREAM_DECODE |
                                            BASS_MIXER_POSEX | BASS_MIXER_NONSTOP);
    if (!g_outputMixer) {
        BASS_ASIO_Free();
        BASS_Free();
        return FALSE;
    }
    BASS_Mixer_StreamSetLatency(g_outputMixer, AsioLatencyProc, nullptr);

    if (!BASS_ASIO_ChannelEnableBASS(FALSE, 0, g_outputMixer, mixchans == 2) ||
        !BASS_ASIO_Start(0, 0)) {
        BASS_ASIO_Stop();
        BASS_ASIO_Free();
        BASS_StreamFree(g_outputMixer);
        g_outputMixer = 0;
        BASS_Free();
        return FALSE;
    }

    g_asioMixerChans = mixchans;
    g_mixerMode = TRUE;
    g_wasapiExclusive = FALSE;
    g_asioMode = TRUE;
    g_asioDeviceId = deviceId;
    g_asioDeviceName = deviceName;
    g_outputDeviceName = deviceName;
    return TRUE;
}

BOOL InitAudioOutput(bool allowBackendFallback = true) {
    g_asioMode = FALSE;
    if (g_outputBackend == kAudioBackendAsio) {
        if (TryInitAsioOutput()) {
            g_audioInitialized = TRUE;
            return TRUE;
        }
        if (!allowBackendFallback) return FALSE;
    }

    int device = -1;
    DWORD mixfreq = 48000;
    std::wstring deviceName;
    FindWasapiOutputDevice(&device, &mixfreq, &deviceName);
    g_outputDeviceName = deviceName.empty() ? L"Default device" : deviceName;

    if (BASS_Init(0, mixfreq, 0, nullptr, nullptr)) {
        if (g_outputMode != kAudioOutputShared && TryInitWasapiOutput(device, TRUE)) {
            g_audioInitialized = TRUE;
            return TRUE;
        }
        if (g_outputMode != kAudioOutputExclusive && TryInitWasapiOutput(device, FALSE)) {
            g_audioInitialized = TRUE;
            return TRUE;
        }
        if (g_outputMode == kAudioOutputExclusive && TryInitWasapiOutput(device, FALSE)) {
            g_audioInitialized = TRUE;
            return TRUE;
        }
        BASS_Free();
    }

    if (!BASS_Init(-1, 44100, 0, nullptr, nullptr)) {
        g_audioInitialized = FALSE;
        g_mixerMode = FALSE;
        g_wasapiExclusive = FALSE;
        g_asioMode = FALSE;
        g_outputDeviceName = L"No audio device available";
        return FALSE;
    }
    g_mixerMode = FALSE;
    g_wasapiExclusive = FALSE;
    g_asioMode = FALSE;
    g_audioInitialized = TRUE;
    g_outputDeviceName = L"System BASS output";
    return TRUE;
}

void FreeAudioOutput() {
    WaveFreeAudition();
    if (g_audioInitialized) {
        if (g_asioMode) {
            BASS_ASIO_Stop();
            BASS_ASIO_Free();
        }
        BASS_WASAPI_Free();
        if (g_outputMixer) {
            BASS_StreamFree(g_outputMixer);
            g_outputMixer = 0;
        }
        BASS_Free();
    }
    g_audioInitialized = FALSE;
    g_mixerMode = FALSE;
    g_wasapiExclusive = FALSE;
    g_asioMode = FALSE;
}

// ------- channels: direct BASS playback or mixer output (WASAPI/ASIO)

DWORD ChannelState(DWORD chan) {
    if (!chan) return BASS_ACTIVE_STOPPED;
    if (!g_mixerMode) return BASS_ChannelIsActive(chan);
    if (chan == g_channel && g_deckPaused) return BASS_ACTIVE_PAUSED;
    return BASS_Mixer_ChannelIsActive(chan);
}

QWORD ChannelPos(DWORD chan) {
    if (!chan) return static_cast<QWORD>(-1);
    if (g_mixerMode && BASS_Mixer_ChannelGetMixer(chan))
        return BASS_Mixer_ChannelGetPosition(chan, BASS_POS_BYTE);
    return BASS_ChannelGetPosition(chan, BASS_POS_BYTE);
}

// ------- keep the system and display awake while audio is playing

BOOL AnySoundPlaying() {
    if (g_channel && ChannelState(g_channel) == BASS_ACTIVE_PLAYING) return TRUE;
    if (g_waveAudition && ChannelState(g_waveAudition) == BASS_ACTIVE_PLAYING)
        return TRUE;
    for (int i = 0; i < kPadCount; ++i) {
        if (g_pads[i].stream && ChannelState(g_pads[i].stream) == BASS_ACTIVE_PLAYING)
            return TRUE;
    }
    return FALSE;
}

void ApplyKeepAwake() {
    const BOOL want = AnySoundPlaying() ? TRUE : FALSE;
    if (want == g_awakeHeld) return;
    g_awakeHeld = want;
    SetThreadExecutionState(want
        ? (ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED)
        : ES_CONTINUOUS);
}

void ReleaseKeepAwake() {
    if (!g_awakeHeld) return;
    g_awakeHeld = FALSE;
    SetThreadExecutionState(ES_CONTINUOUS);
}

void ChannelSeek(DWORD chan, QWORD pos) {
    if (!chan) return;
    if (g_mixerMode && BASS_Mixer_ChannelGetMixer(chan))
        BASS_Mixer_ChannelSetPosition(chan, pos, BASS_POS_BYTE);
    else
        BASS_ChannelSetPosition(chan, pos, BASS_POS_BYTE);
}

QWORD ChannelPosRaw(DWORD chan) {
    if (!chan) return static_cast<QWORD>(-1);
    if (g_mixerMode && BASS_Mixer_ChannelGetMixer(chan))
        return BASS_Mixer_ChannelGetPosition(chan, BASS_POS_BYTE);
    return BASS_ChannelGetPosition(chan, BASS_POS_BYTE);
}

QWORD ChannelPosSafe(DWORD chan, QWORD len) {
    const QWORD pos = ChannelPos(chan);
    if (pos == static_cast<QWORD>(-1)) return pos;
    if (len && len != static_cast<QWORD>(-1) && pos > len) return ChannelPosRaw(chan);
    return pos;
}

const int kSeekSettleMs = 150;
const int kSeekSettleMaxMs = 700;

QWORD ShownPosition(QWORD pos) {
    if (!g_seekPending || !g_channel || pos == static_cast<QWORD>(-1)) return pos;

    const ULONGLONG age = GetTickCount64() - g_seekTick;
    const QWORD grown = BASS_ChannelSeconds2Bytes(g_channel, age / 1000.0);
    const QWORD tol = BASS_ChannelSeconds2Bytes(g_channel, kSeekSettleMs / 1000.0);
    const bool playing = ChannelState(g_channel) == BASS_ACTIVE_PLAYING;

    if (grown == static_cast<QWORD>(-1) || tol == static_cast<QWORD>(-1) ||
        (playing && age > kSeekSettleMaxMs)) {
        g_seekPending = FALSE;
        return pos;
    }

    if (pos + tol < g_seekTarget || pos > g_seekTarget + grown + tol)
        return g_seekTarget;

    g_seekPending = FALSE;
    return pos;
}

const DWORD kMixerChanFlags = BASS_MIXER_CHAN_DOWNMIX | BASS_MIXER_CHAN_BUFFER;

BOOL ChannelStart(DWORD chan, BOOL restart) {
    if (!chan) return FALSE;
    g_deckPaused = FALSE;
    if (!g_mixerMode) {
        return BASS_ChannelPlay(chan, restart);
    }
    if (!g_outputMixer) return FALSE;
    if (BASS_Mixer_ChannelGetMixer(chan)) BASS_Mixer_ChannelRemove(chan);
    if (restart && !BASS_ChannelSetPosition(chan, 0, BASS_POS_BYTE))
        return FALSE;
    return BASS_Mixer_StreamAddChannel(g_outputMixer, chan, kMixerChanFlags);
}

BOOL ChannelResume(DWORD chan) {
    if (!chan) return FALSE;
    g_deckPaused = FALSE;
    if (!g_mixerMode) return BASS_ChannelPlay(chan, FALSE);
    if (!g_outputMixer) return FALSE;
    const DWORD state = BASS_Mixer_ChannelIsActive(chan);
    if (state == BASS_ACTIVE_PLAYING || state == BASS_ACTIVE_PAUSED)
        return BASS_Mixer_ChannelFlags(chan, 0, BASS_MIXER_CHAN_PAUSE);
    return ChannelStart(chan, FALSE);
}

void ChannelPause(DWORD chan) {
    if (!chan) return;
    if (chan == g_channel) g_deckPaused = TRUE;
    if (g_mixerMode) BASS_Mixer_ChannelFlags(chan, BASS_MIXER_CHAN_PAUSE, BASS_MIXER_CHAN_PAUSE);
    else              BASS_ChannelPause(chan);
}

void ChannelStop(DWORD chan) {
    if (!chan) return;
    g_deckPaused = FALSE;
    if (g_mixerMode) BASS_Mixer_ChannelRemove(chan);
    else              BASS_ChannelStop(chan);
}

void CloseChannel() {
    if (g_channel) {
        if (g_isMusic) BASS_MusicFree(g_channel);
        else           BASS_StreamFree(g_channel);
    }
    g_channel = 0;
    g_isMusic = FALSE;
    g_deckPaused = FALSE;
    g_dragging = FALSE;
    g_seekPending = FALSE;
    g_idle = FALSE;
    g_playingTrackDetached = FALSE;
    g_detachedPlayingTrack = Track();
}

void UpdateTimeLabel() {

    static std::wstring lastText;
    wchar_t text[80] = L"- 0 : 00";

    if (g_pendingStart) {

        const int secondsLeft =
            (g_pendingTicks * static_cast<int>(TIMER_PERIOD_MS) + 999) / 1000;
        swprintf_s(text, L"Pause %d s", secondsLeft > 0 ? secondsLeft : 0);
    } else if (g_channel) {
        const QWORD len = BASS_ChannelGetLength(g_channel, BASS_POS_BYTE);
        if (len != static_cast<QWORD>(-1)) {
            const double totalSec = BASS_ChannelBytes2Seconds(g_channel, len);
            QWORD pos = ChannelPos(g_channel);
            if (g_dragging) {
                const int p = static_cast<int>(SendMessageW(g_seek, TBM_GETPOS, 0, 0));
                pos = static_cast<QWORD>(static_cast<double>(len) * p / SLIDER_MAX);
            } else {
                pos = ShownPosition(pos);
            }
            if (pos != static_cast<QWORD>(-1)) {
                double elapsed = BASS_ChannelBytes2Seconds(g_channel, pos);
                if (!(elapsed >= 0.0)) elapsed = 0.0;
                if (elapsed > totalSec) elapsed = totalSec;
                double left = totalSec - elapsed;
                if (!(left >= 0.0)) left = 0.0;

                wchar_t remains[32] = L"0 : 00";
                FormatTime(left, remains, 32);
                swprintf_s(text, L"- %s", remains);
            }
        }
    }
    if (lastText == text) return;
    lastText = text;
    SetWindowTextW(g_lblTime, text);
}

void UpdateSeekBar() {
    if (!g_channel || g_dragging) return;
    const QWORD len = BASS_ChannelGetLength(g_channel, BASS_POS_BYTE);
    const QWORD pos = ShownPosition(ChannelPos(g_channel));
    if (len == static_cast<QWORD>(-1) || len == 0 || pos == static_cast<QWORD>(-1)) return;
    const int p = static_cast<int>(static_cast<double>(pos) * SLIDER_MAX / static_cast<double>(len));
    SendMessageW(g_seek, TBM_SETPOS, TRUE, p);
}

void SeekToSlider() {
    if (!g_channel) return;
    const QWORD len = BASS_ChannelGetLength(g_channel, BASS_POS_BYTE);
    if (len == static_cast<QWORD>(-1) || len == 0) return;
    const int p = static_cast<int>(SendMessageW(g_seek, TBM_GETPOS, 0, 0));
    const QWORD pos = static_cast<QWORD>(static_cast<double>(len) * p / SLIDER_MAX);
    ChannelSeek(g_channel, pos);

    g_seekTarget = pos;
    g_seekTick = GetTickCount64();
    g_seekPending = TRUE;
}

void ApplyVolume() {
    if (g_channel)
        BASS_ChannelSetAttribute(g_channel, BASS_ATTRIB_VOL, static_cast<float>(g_volume) / 100.0f);
    if (g_waveAudition) {
        const int volume =
            (g_wavePad >= 0 && g_wavePad < kPadCount) ? g_padVolume[g_wavePad] : g_volume;
        BASS_ChannelSetAttribute(g_waveAudition, BASS_ATTRIB_VOL,
                                 static_cast<float>(volume) / 100.0f);
    }
}

std::wstring WindowTitle() {
    return L"DSplayer";
}

void SetIdleTitle() {
    const std::wstring title = WindowTitle();
    SetWindowTextW(g_hwnd, title.c_str());
}

void SetTitleText(const std::wstring& path) {
    const std::wstring name = FileNameOf(path);
    SetWindowTextW(g_lblTitle, name.c_str());
    SetIdleTitle();
}

// ---------------------------------------------------------------- playlist

const COLORREF kMainBack     = RGB(26, 26, 28);
const COLORREF kMainListBack = RGB(22, 22, 24);
const COLORREF kMainText     = RGB(206, 206, 212);
const COLORREF kMainDimText  = RGB(148, 148, 154);
const COLORREF kMainMissText = RGB(110, 110, 116);
const COLORREF kMainLine     = RGB(58, 58, 64);
const COLORREF kMainBtnFill  = RGB(52, 52, 58);
const COLORREF kMainBtnEdge  = RGB(96, 96, 104);
const COLORREF kMainBtnDown  = RGB(38, 38, 44);
const COLORREF kMainBtnOff   = RGB(30, 30, 33);
const COLORREF kMainBtnOffEdge = RGB(58, 58, 62);
const COLORREF kMainIcon     = RGB(198, 198, 204);
const COLORREF kMainCaption  = RGB(33, 33, 37);
const COLORREF kMainGlowBack = RGB(38, 66, 44);
const COLORREF kMainGlowEdge = RGB(64, 200, 84);
const COLORREF kMainGlowText = RGB(206, 242, 212);

const COLORREF kMainBlueBack = RGB(28, 54, 92);
const COLORREF kMainBlueEdge = RGB(86, 152, 232);
const COLORREF kMainBlueText = RGB(208, 228, 250);
const COLORREF kMainSelBack  = RGB(28, 54, 92);
const COLORREF kMainDropBack = RGB(44, 86, 132);
const COLORREF kMainGroove   = RGB(42, 42, 48);
const COLORREF kMainGrooveEdge = RGB(70, 70, 78);
const COLORREF kMainFilled   = RGB(108, 112, 120);
const COLORREF kMainHandle   = RGB(198, 198, 204);
const COLORREF kMainHandleEdge = RGB(92, 92, 100);
const COLORREF kMainFocusRing = RGB(132, 132, 140);
const COLORREF kMainGrip     = RGB(126, 126, 134);
const COLORREF kMainPressed  = RGB(52, 92, 58);
const COLORREF kMainBlueDown = RGB(38, 74, 124);
const COLORREF kPlaylistMenuBack = RGB(31, 31, 34);
const COLORREF kPlaylistMenuSelected = RGB(74, 74, 80);
const COLORREF kPlaylistMenuSeparator = RGB(184, 184, 190);

HBRUSH MainBackBrush() {
    static HBRUSH brush = nullptr;
    if (!brush) brush = CreateSolidBrush(kMainBack);
    return brush;
}

HBRUSH MainListBrush() {
    static HBRUSH brush = nullptr;
    if (!brush) brush = CreateSolidBrush(kMainListBack);
    return brush;
}

HBRUSH SolidBrush(COLORREF color) {
    static std::unordered_map<COLORREF, HBRUSH> cache;
    const auto it = cache.find(color);
    if (it != cache.end()) return it->second;
    HBRUSH brush = CreateSolidBrush(color);
    if (brush) cache.emplace(color, brush);
    return brush;
}

HPEN SolidPen(COLORREF color) {
    static std::unordered_map<COLORREF, HPEN> cache;
    const auto it = cache.find(color);
    if (it != cache.end()) return it->second;
    HPEN pen = CreatePen(PS_SOLID, 1, color);
    if (pen) cache.emplace(color, pen);
    return pen;
}

void DisableSystemWindowFrame(HWND hwnd) {

    const DWORD renderPolicy = 2;
    DwmSetWindowAttribute(hwnd, 2, &renderPolicy, sizeof(renderPolicy));

    const DWORD corners = 1;
    DwmSetWindowAttribute(hwnd, 33, &corners, sizeof(corners));

    const DWORD noBorder = 0xFFFFFFFE;
    DwmSetWindowAttribute(hwnd, 34, &noBorder, sizeof(noBorder));
}

bool SkipSystemFramePaint(UINT msg, LRESULT& result) {
    switch (msg) {
    case WM_NCPAINT:
        result = 0;
        return true;
    case WM_NCACTIVATE:
        result = TRUE;
        return true;
    }
    return false;
}

HICON PlayerIcon(int size) {
    HICON icon = static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr),
        MAKEINTRESOURCEW(IDI_PLAYER_APP), IMAGE_ICON, size, size, LR_SHARED));
    return icon ? icon : LoadIconW(nullptr, IDI_APPLICATION);
}

const int kBaseGutterWidth = 24;
const int kBaseTimeColWidth = 68;
const int kBaseLoudColWidth = 64;
const double kLoudNoValue = -1000.0;
const int kBaseMargin = 14;
const int kBaseGripSize = 18;
const int kBaseVolWidth = 140;
const int kBaseTransportBtnW = 64;
const int kBaseCompactBtnW = 42;
const int kBaseListBtnW = kBaseCompactBtnW;
const int kBaseNextBtnW = 88;
const int kBaseDelayBtnW = 96;
const int kBaseClearBtnW = 96;
const int kBaseSamplerBtnW = 110;
const int kBaseMeterBtnW = 120;
const int kBasePlaylistBtnW = 34;
const int kBaseBottomGap = 8;
const int kBaseBtnTextPad = 12;
const int kBaseTimeLabelW = 170;
const int kBaseSettingsBtnW = kBaseCompactBtnW;
const int kBaseCaptionH = 30;
const int kBaseCaptionBtnW = 34;
const int kBaseCaptionBtnH = 24;
const int kBaseCaptionGap = 4;
const int kBaseCaptionSide = 6;

const int kBaseSettingsWinW = 560;
const int kBaseSettingsWinH = 300;
const int kBaseSettingsNavW = 150;
const int kBaseSettingsNavH = 44;
const int kBaseSettingsRowH = 44;

int kGutterWidth = kBaseGutterWidth;
int kTimeColWidth = kBaseTimeColWidth;
int kLoudColWidth = kBaseLoudColWidth;
int kMargin = kBaseMargin;
int kGripSize = kBaseGripSize;
int kVolWidth = kBaseVolWidth;
int kTransportBtnW = kBaseTransportBtnW;
int kListBtnW = kBaseListBtnW;
int kNextBtnW = kBaseNextBtnW;
int kDelayBtnW = kBaseDelayBtnW;
int kPlaylistBtnW = kBasePlaylistBtnW;
int kBottomGap = kBaseBottomGap;
int kBtnTextPad = kBaseBtnTextPad;
int kTimeLabelW = kBaseTimeLabelW;
int kSettingsBtnW = kBaseSettingsBtnW;
int kCaptionH = kBaseCaptionH;
int kCaptionBtnW = kBaseCaptionBtnW;
int kCaptionBtnH = kBaseCaptionBtnH;
int kCaptionGap = kBaseCaptionGap;
int kCaptionSide = kBaseCaptionSide;

int kSettingsWinW = kBaseSettingsWinW;
int kSettingsWinH = kBaseSettingsWinH;
int kSettingsNavW = kBaseSettingsNavW;
int kSettingsNavH = kBaseSettingsNavH;
int kSettingsRowH = kBaseSettingsRowH;

int g_clearBtnW = kBaseClearBtnW;
int g_samplerBtnW = kBaseSamplerBtnW;
int g_meterBtnW = kBaseMeterBtnW;

int ButtonWidthForText(const wchar_t* text, int* cache) {

    HDC dc = GetDC(g_hwnd);
    if (!dc) return *cache;
    HFONT oldFont = static_cast<HFONT>(SelectObject(dc, UiFont()));
    SIZE size = {};
    GetTextExtentPoint32W(dc, text, lstrlenW(text), &size);
    SelectObject(dc, oldFont);
    ReleaseDC(g_hwnd, dc);
    *cache = size.cx + kBtnTextPad * 2;
    return *cache;
}

int ScaledFontHeight(int height) {
    const int scaled = S(height < 0 ? -height : height);
    if (scaled <= 0) return height;
    return height < 0 ? -scaled : scaled;
}

HFONT ListFont() {
    if (!g_listFont) {
        NONCLIENTMETRICSW ncm = {};
        ncm.cbSize = sizeof(ncm);
        if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0)) {
            LOGFONTW lf = ncm.lfMessageFont;
            const int h = ScaledFontHeight(lf.lfHeight);
            lf.lfHeight = (h < 0) ? h - S(4) : h + S(4);
            g_listFont = CreateFontIndirectW(&lf);
        }
    }
    return g_listFont ? g_listFont : UiFont();
}

int ListItemHeight() {
    LOGFONTW lf = {};
    if (!GetObjectW(ListFont(), sizeof(lf), &lf)) return S(24);
    const int h = (lf.lfHeight < 0) ? -lf.lfHeight : lf.lfHeight;
    return h + S(8);
}

HFONT LabelFont() {
    if (!g_labelFont) {
        NONCLIENTMETRICSW ncm = {};
        ncm.cbSize = sizeof(ncm);
        if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0)) {
            LOGFONTW lf = ncm.lfMessageFont;
            const int h = ScaledFontHeight(lf.lfHeight);
            lf.lfHeight = (h < 0) ? h - S(4) : h + S(4);
            g_labelFont = CreateFontIndirectW(&lf);
        }
    }
    return g_labelFont ? g_labelFont : UiFont();
}

void FillTriangle(HDC dc, POINT pts[3], COLORREF color) {
    HBRUSH brush = SolidBrush(color);
    HGDIOBJ oldBrush = SelectObject(dc, brush);
    HGDIOBJ oldPen = SelectObject(dc, GetStockObject(NULL_PEN));
    Polygon(dc, pts, 3);
    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
}

void DrawPlayTriangle(HDC dc, int centerX, int centerY, int half, int wide, COLORREF color) {
    POINT pts[3] = {
        {centerX - wide / 2, centerY - half},
        {centerX - wide / 2, centerY + half},
        {centerX + wide / 2, centerY}
    };
    FillTriangle(dc, pts, color);
}

std::wstring TimeText(double seconds) {
    wchar_t buf[32];
    if (!(seconds > 0.0)) return L"-- : --";
    FormatTime(seconds, buf, 32);
    return buf;
}

std::wstring LoudnessText(const Track& track) {
    if (!track.loudnessDone) return track.missing ? L"-" : L"...";
    if (track.lufs <= kLoudNoValue + 1.0) return L"-";

    const int tenths = static_cast<int>(track.lufs * 10.0 + (track.lufs < 0.0 ? -0.5 : 0.5));
    const int magnitude = (tenths < 0) ? -tenths : tenths;
    wchar_t buf[32];
    if (tenths < 0)
        swprintf_s(buf, L"- %d . %d", magnitude / 10, magnitude % 10);
    else
        swprintf_s(buf, L"%d . %d", magnitude / 10, magnitude % 10);
    return buf;
}

BOOL g_plBtnSound = FALSE;
int g_plBtnOwner = -1;

BOOL PlaylistSoundActive() {
    if (g_pendingStart) return TRUE;
    if (!g_channel) return FALSE;

    if (g_mixerMode && !BASS_Mixer_ChannelGetMixer(g_channel)) return FALSE;
    return (ChannelState(g_channel) != BASS_ACTIVE_STOPPED) ? TRUE : FALSE;
}

void RefreshPlaylistButtons() {
    for (int i = 0; i < kPlaylistCount; ++i)
        if (g_btnPl[i]) InvalidateRect(g_btnPl[i], nullptr, FALSE);
    g_plBtnSound = PlaylistSoundActive();
    g_plBtnOwner = g_playPlaylist;
}

void SyncPlaylistButtons() {
    if (g_plBtnSound == PlaylistSoundActive() && g_plBtnOwner == g_playPlaylist) return;
    RefreshPlaylistButtons();
}

void DrawPlaylistItem(const DRAWITEMSTRUCT* dis) {
    const std::vector<Track>& tracks = VisibleTracks();
    const int index = static_cast<int>(dis->itemID);
    if (index < 0 || index >= static_cast<int>(tracks.size())) {

        FillRect(dis->hDC, &dis->rcItem, MainListBrush());
        return;
    }

    const bool selected = (dis->itemState & ODS_SELECTED) != 0;
    const bool missing  = tracks[index].missing;

    const bool current  = (g_activePlaylist == g_playPlaylist) &&
                          (index == PlayingIndex()) && !missing;
    const bool dropHere = g_dragActive && index == g_dragTarget;

    COLORREF back = kMainListBack;
    if (dropHere)      back = kMainDropBack;
    else if (current)  back = kMainGlowBack;
    else if (selected) back = kMainSelBack;

    FillRect(dis->hDC, &dis->rcItem, SolidBrush(back));

    HFONT oldFont = static_cast<HFONT>(SelectObject(dis->hDC, ListFont()));
    SetBkMode(dis->hDC, TRANSPARENT);
    SetTextColor(dis->hDC, missing ? kMainMissText
                                   : (current ? kMainGlowText
                                              : (selected ? kMainBlueText : kMainText)));

    const int gutterRight = dis->rcItem.left + kGutterWidth;
    if (current)
        DrawPlayTriangle(dis->hDC, dis->rcItem.left + kGutterWidth / 2,
                         (dis->rcItem.top + dis->rcItem.bottom) / 2, S(6), S(9), kMainGlowEdge);

    HPEN sep = SolidPen(kMainLine);
    HGDIOBJ oldPen = SelectObject(dis->hDC, sep);
    MoveToEx(dis->hDC, gutterRight, dis->rcItem.top, nullptr);
    LineTo(dis->hDC, gutterRight, dis->rcItem.bottom);

    const int timeLeft = dis->rcItem.right - kTimeColWidth;
    const int loudLeft = timeLeft - kLoudColWidth;
    MoveToEx(dis->hDC, timeLeft, dis->rcItem.top, nullptr);
    LineTo(dis->hDC, timeLeft, dis->rcItem.bottom);
    MoveToEx(dis->hDC, loudLeft, dis->rcItem.top, nullptr);
    LineTo(dis->hDC, loudLeft, dis->rcItem.bottom);
    SelectObject(dis->hDC, oldPen);

    RECT rc = dis->rcItem;
    rc.left = gutterRight + S(6);
    rc.right = loudLeft - S(8);
    DrawTextW(dis->hDC, tracks[index].name.c_str(), -1, &rc,
              DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_NOPREFIX | DT_END_ELLIPSIS);

    SetTextColor(dis->hDC, missing ? kMainMissText : (current ? kMainGlowText : kMainDimText));
    RECT loudRc = dis->rcItem;
    loudRc.left = loudLeft + S(4);
    loudRc.right = timeLeft - S(8);
    const std::wstring loudness = LoudnessText(tracks[index]);
    DrawTextW(dis->hDC, loudness.c_str(), -1, &loudRc,
              DT_SINGLELINE | DT_VCENTER | DT_RIGHT | DT_NOPREFIX);

    RECT timeRc = dis->rcItem;
    timeRc.left = timeLeft + S(4);
    timeRc.right -= S(8);
    const std::wstring length = TimeText(tracks[index].length);
    DrawTextW(dis->hDC, length.c_str(), -1, &timeRc,
              DT_SINGLELINE | DT_VCENTER | DT_RIGHT | DT_NOPREFIX);

    if (dis->itemState & ODS_FOCUS)
        DrawFocusRing(dis->hDC, dis->rcItem, kMainFocusRing);

    SelectObject(dis->hDC, oldFont);
}

void FillPlaylist(int selectIndex) {
    const std::vector<Track>& tracks = VisibleTracks();
    const int top = static_cast<int>(SendMessageW(g_list, LB_GETTOPINDEX, 0, 0));
    SendMessageW(g_list, LB_RESETCONTENT, 0, 0);
    for (size_t i = 0; i < tracks.size(); ++i) {

        SendMessageW(g_list, LB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(tracks[i].name.c_str()));
    }
    if (selectIndex >= 0 && selectIndex < static_cast<int>(tracks.size()))
        SendMessageW(g_list, LB_SETCURSEL, selectIndex, 0);
    if (top > 0)
        SendMessageW(g_list, LB_SETTOPINDEX, top, 0);
    InvalidateRect(g_list, nullptr, FALSE);
    UpdateRemoveButtonState();
}

void SwitchPlaylist(int playlist) {
    if (playlist < 0 || playlist >= kPlaylistCount) return;
    if (playlist == g_activePlaylist) return;

    g_activePlaylist = playlist;
    const int current = g_playlists[playlist].currentIndex;
    FillPlaylist((current >= 0 &&
                  current < static_cast<int>(g_playlists[playlist].tracks.size()))
                     ? current : -1);
    RefreshPlaylistButtons();
}

void UpdateRemoveButtonState() {
    if (!g_btnRemove) return;

    const int sel = static_cast<int>(SendMessageW(g_list, LB_GETCURSEL, 0, 0));
    BOOL enabled = (sel != LB_ERR) ? TRUE : FALSE;

    if (enabled && g_activePlaylist == g_playPlaylist && sel == PlayingIndex() &&
        g_channel && ChannelState(g_channel) != BASS_ACTIVE_STOPPED) {
        enabled = FALSE;
    }
    EnableWindow(g_btnRemove, enabled);
}

void InvalidatePlaylistRow(int index) {
    if (!g_list) return;
    RECT rc = {};
    if (index >= 0 &&
        SendMessageW(g_list, LB_GETITEMRECT, static_cast<WPARAM>(index),
                     reinterpret_cast<LPARAM>(&rc)) != LB_ERR) {
        InvalidateRect(g_list, &rc, FALSE);
        return;
    }
    InvalidateRect(g_list, nullptr, FALSE);
}

// ------------------------------------------------- playlist persistence

std::wstring Utf8ToWStr(const std::string& s) {
    if (s.empty()) return std::wstring();
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring out(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), out.data(), n);
    return out;
}

std::string WStrToUtf8(const std::wstring& s) {
    if (s.empty()) return std::string();
    const int n = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()),
                                      nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()),
                        out.data(), n, nullptr, nullptr);
    return out;
}

// ------------------------- settings file I/O -------------------------

const size_t kMaxTextFileBytes = 64u * 1024u * 1024u;

bool ReadTextFileUtf8(const std::wstring& path, std::string& content) {
    content.clear();
    std::ifstream in(path.c_str(), std::ios::binary);
    if (!in) return false;
    std::vector<char> buffer(64 * 1024);
    while (in) {
        in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const std::streamsize got = in.gcount();
        if (got <= 0) continue;
        const size_t count = static_cast<size_t>(got);
        if (content.size() > kMaxTextFileBytes - count) return false;
        content.append(buffer.data(), count);
    }
    if (in.bad() || (!in.eof() && in.fail())) return false;

    const size_t size = content.size();
    const unsigned char first = (size > 0) ? static_cast<unsigned char>(content[0]) : 0;
    const unsigned char second = (size > 1) ? static_cast<unsigned char>(content[1]) : 0;
    if (size >= 3 && first == 0xEF && second == 0xBB &&
        static_cast<unsigned char>(content[2]) == 0xBF) {
        content.erase(0, 3);
        return true;
    }
    if (size >= 2 && ((first == 0xFF && second == 0xFE) ||
                      (first == 0xFE && second == 0xFF))) {

        const bool bigEndian = (first == 0xFE);
        std::wstring text;
        text.reserve((size - 2) / 2);
        for (size_t i = 2; i + 1 < size; i += 2) {
            const unsigned char low = static_cast<unsigned char>(content[bigEndian ? i + 1 : i]);
            const unsigned char high = static_cast<unsigned char>(content[bigEndian ? i : i + 1]);
            text.push_back(static_cast<wchar_t>(low | (high << 8)));
        }
        content = WStrToUtf8(text);
    }
    return true;
}

static LONG g_atomicWriteSerial = 0;

bool WriteTextFileAtomic(const std::wstring& path, const std::string& content) {
    std::wstring temp;
    HANDLE file = INVALID_HANDLE_VALUE;
    for (int attempt = 0; attempt < 8; ++attempt) {
        const LONG serial = InterlockedIncrement(&g_atomicWriteSerial);
        temp = path + L".tmp." + std::to_wstring(GetCurrentProcessId()) + L"." +
               std::to_wstring(serial);
        file = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                           FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr);
        if (file != INVALID_HANDLE_VALUE) break;
        const DWORD error = GetLastError();
        if (error != ERROR_FILE_EXISTS && error != ERROR_ALREADY_EXISTS)
            return false;
    }
    if (file == INVALID_HANDLE_VALUE) return false;

    bool ok = true;
    size_t offset = 0;
    while (ok && offset < content.size()) {
        const DWORD chunk = static_cast<DWORD>((std::min)(
            content.size() - offset, static_cast<size_t>(MAXDWORD)));
        DWORD written = 0;
        ok = WriteFile(file, content.data() + offset, chunk, &written, nullptr) &&
             written == chunk;
        offset += written;
    }
    if (ok) ok = FlushFileBuffers(file) != FALSE;
    CloseHandle(file);

    if (ok) {
        ok = MoveFileExW(temp.c_str(), path.c_str(),
                         MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
    }
    if (!ok) DeleteFileW(temp.c_str());
    return ok;
}

bool FileExists(const std::wstring& path) {
    const DWORD attr = GetFileAttributesW(path.c_str());
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

// -------------------------------------------- stored loudness database (LUFS)

const wchar_t* kLoudDbFileName = L"loudness.txt";
const size_t kLoudDbMaxEntries = 1000;

std::wstring ExecutablePath() {
    std::vector<wchar_t> buffer(MAX_PATH);
    while (buffer.size() <= 32768) {
        SetLastError(ERROR_SUCCESS);
        const DWORD length = GetModuleFileNameW(
            nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length > 0 && length < buffer.size())
            return std::wstring(buffer.data(), length);
        if (!length || GetLastError() != ERROR_INSUFFICIENT_BUFFER)
            return std::wstring();
        if (buffer.size() >= 32768) return std::wstring();
        buffer.resize((std::min)(buffer.size() * 2, static_cast<size_t>(32768)));
    }
    return std::wstring();
}

std::wstring DataFilePath(const std::wstring& name) {
    std::wstring path = ExecutablePath();
    if (path.empty()) return name;
    const size_t slash = path.find_last_of(L"\\/");
    if (slash != std::wstring::npos) path.erase(slash + 1);
    return path + name;
}

std::wstring LoudnessDbFilePath() {
    return DataFilePath(kLoudDbFileName);
}

struct LoudnessDbEntry {
    std::wstring path;
    ULONGLONG size = 0;
    ULONGLONG mtime = 0;
    double lufs = kLoudNoValue;
    bool legacy = false;
};

std::vector<LoudnessDbEntry> g_loudnessDb;

bool LoudnessFileStamp(const std::wstring& path, ULONGLONG* size, ULONGLONG* mtime) {
    WIN32_FILE_ATTRIBUTE_DATA data = {};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) return false;
    if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) return false;
    if (size) {
        *size = (static_cast<ULONGLONG>(data.nFileSizeHigh) << 32) |
                static_cast<ULONGLONG>(data.nFileSizeLow);
    }
    if (mtime) {
        *mtime = (static_cast<ULONGLONG>(data.ftLastWriteTime.dwHighDateTime) << 32) |
                 static_cast<ULONGLONG>(data.ftLastWriteTime.dwLowDateTime);
    }
    return true;
}

void LoudnessDbUpsert(const LoudnessDbEntry& entry) {
    for (auto it = g_loudnessDb.begin(); it != g_loudnessDb.end();) {
        if (it->legacy == entry.legacy &&
            _wcsicmp(it->path.c_str(), entry.path.c_str()) == 0) {
            it = g_loudnessDb.erase(it);
        } else {
            ++it;
        }
    }
    g_loudnessDb.push_back(entry);
    if (g_loudnessDb.size() > kLoudDbMaxEntries) {
        const size_t removeCount = g_loudnessDb.size() - kLoudDbMaxEntries;
        g_loudnessDb.erase(g_loudnessDb.begin(), g_loudnessDb.begin() + removeCount);
    }
}

bool SaveLoudnessDb() {
    if (g_loudnessDb.empty() && !FileExists(LoudnessDbFilePath())) return true;

    std::string content;
    const unsigned char bom[3] = {0xEF, 0xBB, 0xBF};
    content.append(reinterpret_cast<const char*>(bom), 3);
    content += "# DSplayer loudness cache v2\n";
    content += "# LUFS\tsize\tmtime\tpath\n";

    for (const LoudnessDbEntry& entry : g_loudnessDb) {
        char number[32];
        sprintf_s(number, "%.2f", entry.lufs);
        content += number;
        if (!entry.legacy) {
            char size[32];
            char mtime[32];
            sprintf_s(size, "%llu", entry.size);
            sprintf_s(mtime, "%llu", entry.mtime);
            content += '\t';
            content += size;
            content += '\t';
            content += mtime;
        }
        content += '\t';
        content += WStrToUtf8(entry.path);
        content += '\n';
    }
    return WriteTextFileAtomic(LoudnessDbFilePath(), content);
}

bool ParseLoudnessUll(const std::string& text, ULONGLONG* value) {
    if (text.empty() || !value) return false;
    char* stop = nullptr;
    const unsigned long long parsed = _strtoui64(text.c_str(), &stop, 10);
    if (!stop || *stop != '\0') return false;
    *value = static_cast<ULONGLONG>(parsed);
    return true;
}

void LoudnessDbStore(const std::wstring& path, double lufs) {
    if (path.empty() || lufs <= kLoudNoValue + 1.0) return;
    LoudnessDbEntry entry;
    entry.path = path;
    entry.lufs = lufs;
    if (!LoudnessFileStamp(path, &entry.size, &entry.mtime)) return;
    LoudnessDbUpsert(entry);
    SaveLoudnessDb();
}

void ApplyStoredLoudness(Track& track) {
    ULONGLONG size = 0;
    ULONGLONG mtime = 0;
    if (!LoudnessFileStamp(track.path, &size, &mtime)) return;

    bool versionedPathSeen = false;
    for (LoudnessDbEntry& entry : g_loudnessDb) {
        if (entry.legacy || _wcsicmp(entry.path.c_str(), track.path.c_str()) != 0) continue;
        versionedPathSeen = true;
        if (entry.size == size && entry.mtime == mtime) {
            track.lufs = entry.lufs;
            track.loudnessDone = true;
            return;
        }
    }
    if (versionedPathSeen || track.name.empty()) return;

    for (LoudnessDbEntry& entry : g_loudnessDb) {
        if (!entry.legacy || _wcsicmp(entry.path.c_str(), track.name.c_str()) != 0) continue;
        entry.path = track.path;
        entry.size = size;
        entry.mtime = mtime;
        entry.legacy = false;
        track.lufs = entry.lufs;
        track.loudnessDone = true;
        return;
    }
}

void LoadLoudnessDb() {
    g_loudnessDb.clear();
    std::string content;
    if (!ReadTextFileUtf8(LoudnessDbFilePath(), content)) return;

    size_t dataRows = 0;
    size_t start = 0;
    while (start <= content.size()) {
        const size_t end = content.find('\n', start);
        std::string line = (end == std::string::npos) ? content.substr(start)
                                                      : content.substr(start, end - start);
        if (!line.empty() && line.back() == '\r') line.pop_back();

        if (line.empty() || line[0] == '#') {
            if (end == std::string::npos) break;
            start = end + 1;
            continue;
        }
        ++dataRows;

        const size_t tab1 = line.find('\t');
        if (tab1 == std::string::npos || tab1 == 0) {
            if (end == std::string::npos) break;
            start = end + 1;
            continue;
        }
        const std::string number = line.substr(0, tab1);
        char* numberStop = nullptr;
        const double lufs = std::strtod(number.c_str(), &numberStop);
        if (!numberStop || *numberStop != '\0' || !(lufs > -100.0 && lufs < 20.0)) {
            if (end == std::string::npos) break;
            start = end + 1;
            continue;
        }

        LoudnessDbEntry entry;
        entry.lufs = lufs;
        const size_t tab2 = line.find('\t', tab1 + 1);
        const size_t tab3 =
            tab2 == std::string::npos ? std::string::npos : line.find('\t', tab2 + 1);
        if (tab2 != std::string::npos && tab3 != std::string::npos) {
            ULONGLONG size = 0;
            ULONGLONG mtime = 0;
            if (ParseLoudnessUll(line.substr(tab1 + 1, tab2 - tab1 - 1), &size) &&
                ParseLoudnessUll(line.substr(tab2 + 1, tab3 - tab2 - 1), &mtime)) {
                entry.path = Utf8ToWStr(line.substr(tab3 + 1));
                entry.size = size;
                entry.mtime = mtime;
                if (!entry.path.empty()) LoudnessDbUpsert(entry);
            }
        } else {
            entry.path = Utf8ToWStr(line.substr(tab1 + 1));
            entry.legacy = true;
            if (!entry.path.empty()) LoudnessDbUpsert(entry);
        }

        if (end == std::string::npos) break;
        start = end + 1;
    }
    if (dataRows != g_loudnessDb.size()) SaveLoudnessDb();
}

std::wstring PlaylistFilePath(int playlist) {
    wchar_t name[32];
    swprintf_s(name, L"playlist_%d.txt", playlist + 1);
    return DataFilePath(name);
}

std::wstring LegacyPlaylistFilePath() {
    return DataFilePath(L"playlist.txt");
}

bool SavePlaylist(int playlist) {
    if (playlist < 0 || playlist >= kPlaylistCount) return false;
    std::string content;
    const unsigned char bom[3] = {0xEF, 0xBB, 0xBF};
    content.append(reinterpret_cast<const char*>(bom), 3);
    for (const Track& track : g_playlists[playlist].tracks) {
        content += WStrToUtf8(track.path);
        content += '\n';
    }
    return WriteTextFileAtomic(PlaylistFilePath(playlist), content);
}

void LoadPlaylist(int playlist) {
    if (playlist < 0 || playlist >= kPlaylistCount) return;

    bool fromLegacy = false;
    std::string content;
    bool loaded = ReadTextFileUtf8(PlaylistFilePath(playlist), content);
    if (!loaded && playlist == 0 && !FileExists(PlaylistFilePath(playlist))) {
        loaded = ReadTextFileUtf8(LegacyPlaylistFilePath(), content);
        fromLegacy = loaded;
    }
    if (!loaded) return;

    std::vector<Track>& tracks = g_playlists[playlist].tracks;
    size_t start = 0;
    while (start <= content.size()) {
        const size_t end = content.find('\n', start);
        std::string line = (end == std::string::npos) ? content.substr(start)
                                                      : content.substr(start, end - start);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.empty()) {
            Track track;
            track.path = Utf8ToWStr(line);
            track.name = FileNameOf(track.path);
            track.missing = !FileExists(track.path);
            ApplyStoredLoudness(track);
            tracks.push_back(track);
        }
        if (end == std::string::npos) break;
        start = end + 1;
    }

    if (fromLegacy) {

        if (SavePlaylist(playlist)) DeleteFileW(LegacyPlaylistFilePath().c_str());
    }
}

double ProbeLength(const std::wstring& path) {
    DWORD stream = BASS_StreamCreateFile(FALSE, path.c_str(), 0, 0,
                                         BASS_UNICODE | BASS_STREAM_DECODE);
    if (stream) {
        const QWORD len = BASS_ChannelGetLength(stream, BASS_POS_BYTE);
        const double seconds =
            (len == static_cast<QWORD>(-1)) ? -1.0 : BASS_ChannelBytes2Seconds(stream, len);
        BASS_StreamFree(stream);
        return seconds;
    }

    HMUSIC music = BASS_MusicLoad(0, path.c_str(), 0, 0,
                                  BASS_MUSIC_DECODE | BASS_MUSIC_PRESCAN | BASS_UNICODE, 0);
    if (!music) return -1.0;
    const QWORD len = BASS_ChannelGetLength(music, BASS_POS_BYTE);
    const double seconds =
        (len == static_cast<QWORD>(-1)) ? -1.0 : BASS_ChannelBytes2Seconds(music, len);
    BASS_MusicFree(music);
    return seconds;
}

// ------------------------------------------------- loudness measurement (LUFS)

const double kLoudMaxSeconds = 150.0;

const DWORD kLoudSliceMs = 40;
const DWORD kLoudSliceBusyMs = 16;

size_t g_loudHint = 0;
std::wstring g_loudPath;
DWORD g_loudDecoder = 0;
HLOUDNESS g_loudHandle = 0;
QWORD g_loudDecoded = 0;
QWORD g_loudLimit = 0;

BOOL AudioIsPlaying() {
    if (g_channel && ChannelState(g_channel) == BASS_ACTIVE_PLAYING) return TRUE;
    for (int i = 0; i < kPadCount; ++i)
        if (g_pads[i].stream && ChannelState(g_pads[i].stream) == BASS_ACTIVE_PLAYING)
            return TRUE;
    return FALSE;
}

bool LoudnessBegin(int index) {
    std::vector<Track>& tracks = VisibleTracks();
    const std::wstring path = tracks[index].path;
    DWORD decoder = BASS_StreamCreateFile(FALSE, path.c_str(), 0, 0,
                                          BASS_UNICODE | BASS_STREAM_DECODE | BASS_SAMPLE_FLOAT);
    if (!decoder) {
        decoder = BASS_MusicLoad(0, path.c_str(), 0, 0,
                                 BASS_UNICODE | BASS_MUSIC_DECODE | BASS_SAMPLE_FLOAT, 0);
    }
    if (!decoder) return false;

    const QWORD len = BASS_ChannelGetLength(decoder, BASS_POS_BYTE);
    if (!(tracks[index].length > 0.0) && len != static_cast<QWORD>(-1))
        tracks[index].length = BASS_ChannelBytes2Seconds(decoder, len);

    g_loudDecoded = 0;
    g_loudLimit = BASS_ChannelSeconds2Bytes(decoder, kLoudMaxSeconds);
    if (g_loudLimit == static_cast<QWORD>(-1)) g_loudLimit = 0;

    const HLOUDNESS handle = BASS_Loudness_Start(decoder, BASS_LOUDNESS_INTEGRATED, 0);
    if (!handle) {
        BASS_ChannelFree(decoder);
        return false;
    }

    g_loudPath = path;
    g_loudDecoder = decoder;
    g_loudHandle = handle;
    return true;
}

bool LoudnessStep() {
    if (!g_loudHandle) return true;
    const ULONGLONG deadline =
        GetTickCount64() + (AudioIsPlaying() ? kLoudSliceBusyMs : kLoudSliceMs);
    static float buf[8192];
    for (;;) {

        if (g_loudLimit && g_loudDecoded >= g_loudLimit) return true;
        const DWORD got = BASS_ChannelGetData(g_loudDecoder, buf, sizeof(buf));
        if (got == 0 || got == static_cast<DWORD>(-1)) return true;
        g_loudDecoded += got;
        if (GetTickCount64() >= deadline) return false;
    }
}

void LoudnessFinish() {
    if (!g_loudHandle) return;
    float level = 0.0f;
    const BOOL ok = BASS_Loudness_GetLevel(g_loudHandle, BASS_LOUDNESS_INTEGRATED, &level);
    BASS_Loudness_Stop(g_loudHandle);
    if (g_loudDecoder) BASS_ChannelFree(g_loudDecoder);
    g_loudHandle = 0;
    g_loudDecoder = 0;
    g_loudDecoded = 0;
    g_loudLimit = 0;

    const std::wstring path = g_loudPath;
    g_loudPath.clear();

    for (int pl = 0; pl < kPlaylistCount; ++pl) {
        std::vector<Track>& tracks = g_playlists[pl].tracks;
        for (size_t i = 0; i < tracks.size(); ++i) {
            Track& track = tracks[i];
            if (track.path != path) continue;
            track.loudnessDone = true;
            track.lufs = (ok && level > kLoudNoValue) ? level : kLoudNoValue;
            if (pl == g_activePlaylist) InvalidatePlaylistRow(static_cast<int>(i));
        }
    }
    if (ok && level > kLoudNoValue) LoudnessDbStore(path, level);

    ApplyAutoVolume();
}

void CancelLoudnessProbe() {
    if (g_loudHandle) {
        BASS_Loudness_Stop(g_loudHandle);
        g_loudHandle = 0;
    }
    if (g_loudDecoder) {
        BASS_ChannelFree(g_loudDecoder);
        g_loudDecoder = 0;
    }
    g_loudDecoded = 0;
    g_loudLimit = 0;
    g_loudPath.clear();
}

void StepLoudnessProbe() {
    if (g_loudHandle) {
        if (!LoudnessStep()) return;
        LoudnessFinish();
        return;
    }

    std::vector<Track>& tracks = VisibleTracks();
    const size_t count = tracks.size();
    if (g_loudHint >= count) g_loudHint = 0;
    for (size_t step = 0; step < count; ++step) {
        const size_t i = (g_loudHint + step) % count;
        if (tracks[i].missing || tracks[i].loudnessDone) continue;
        g_loudHint = (i + 1) % count;
        if (!LoudnessBegin(static_cast<int>(i))) {
            tracks[i].loudnessDone = true;
            tracks[i].lufs = kLoudNoValue;
            InvalidatePlaylistRow(static_cast<int>(i));
        }
        return;
    }
}

void AddToPlaylist(const std::wstring& path) {
    Track track;
    track.path = path;
    track.name = FileNameOf(path);
    track.length = ProbeLength(path);
    ApplyStoredLoudness(track);
    VisibleTracks().push_back(track);
}

void AddFilesDialog() {
    std::vector<wchar_t> buffer(64 * 1024, L'\0');
    const DWORD importAttributes = g_importDirectory.empty()
        ? INVALID_FILE_ATTRIBUTES
        : GetFileAttributesW(g_importDirectory.c_str());
    const bool importDirectoryValid =
        importAttributes != INVALID_FILE_ATTRIBUTES &&
        (importAttributes & FILE_ATTRIBUTE_DIRECTORY);

    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_hwnd;
    ofn.lpstrFilter = kAudioFilter;
    ofn.lpstrFile = buffer.data();
    ofn.nMaxFile = static_cast<DWORD>(buffer.size());
    ofn.lpstrDefExt = L"mp3";
    if (importDirectoryValid) ofn.lpstrInitialDir = g_importDirectory.c_str();
    ofn.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST |
                OFN_HIDEREADONLY | OFN_ALLOWMULTISELECT | OFN_NOCHANGEDIR;

    if (!GetOpenFileNameW(&ofn)) return;

    const wchar_t* p = buffer.data();
    const std::wstring first = p;
    p += first.size() + 1;

    std::vector<std::wstring> paths;
    if (*p == L'\0') {
        paths.push_back(first);
    } else {
        while (*p) {
            const std::wstring name = p;
            p += name.size() + 1;
            paths.push_back(first + L"\\" + name);
        }
    }
    const std::wstring selectedDirectory =
        paths.size() > 1 ? first : DirectoryOf(paths.front());
    if (!selectedDirectory.empty() &&
        _wcsicmp(selectedDirectory.c_str(), g_importDirectory.c_str()) != 0) {
        g_importDirectory = selectedDirectory;
        SaveSettings();
    }

    const int firstAdded = static_cast<int>(VisibleTracks().size());
    for (const std::wstring& path : paths)
        AddToPlaylist(path);
    SavePlaylist(g_activePlaylist);
    FillPlaylist(firstAdded);
}

void MoveTrackInPlaylist(int from, int to) {
    std::vector<Track>& tracks = VisibleTracks();
    const int count = static_cast<int>(tracks.size());
    if (from < 0 || from >= count || to < 0 || to >= count || from == to) return;

    const Track moved = tracks[from];
    tracks.erase(tracks.begin() + from);
    tracks.insert(tracks.begin() + to, moved);

    if (g_activePlaylist != g_playPlaylist) return;
    if (g_movedPlayingNext == from) {
        g_movedPlayingNext = to;
    } else if (from < g_movedPlayingNext && to >= g_movedPlayingNext) {
        --g_movedPlayingNext;
    } else if (from > g_movedPlayingNext && to <= g_movedPlayingNext) {
        ++g_movedPlayingNext;
    }
    int& current = PlayingIndex();
    if (current == from) {
        current = to;
    } else if (from < current && to >= current) {
        --current;
    } else if (from > current && to <= current) {
        ++current;
    }
}

int ItemIndexFromPoint(HWND list, POINT pt) {
    if (SendMessageW(list, LB_GETCOUNT, 0, 0) <= 0) return -1;
    const LRESULT hit = SendMessageW(list, LB_ITEMFROMPOINT, 0, MAKELPARAM(pt.x, pt.y));
    return static_cast<int>(LOWORD(hit));
}

int ItemIndexUnderPoint(HWND list, POINT pt) {
    if (SendMessageW(list, LB_GETCOUNT, 0, 0) <= 0) return -1;
    const LRESULT hit = SendMessageW(list, LB_ITEMFROMPOINT, 0, MAKELPARAM(pt.x, pt.y));
    if (HIWORD(hit) != 0) return -1;
    const int index = static_cast<int>(LOWORD(hit));
    RECT rc = {};
    if (SendMessageW(list, LB_GETITEMRECT, static_cast<WPARAM>(index),
                     reinterpret_cast<LPARAM>(&rc)) == LB_ERR) {
        return -1;
    }
    return PtInRect(&rc, pt) ? index : -1;
}

LRESULT CALLBACK PlaylistProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
                              UINT_PTR, DWORD_PTR) {
    switch (msg) {
    case WM_LBUTTONDOWN: {
        const POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        const int index = ItemIndexUnderPoint(hwnd, pt);
        if (index < 0) {
            SetFocus(hwnd);
            return 0;
        }

        SetFocus(hwnd);
        SendMessageW(hwnd, LB_SETCURSEL, index, 0);
        SendMessageW(GetParent(hwnd), WM_COMMAND,
                     MAKEWPARAM(ID_PLAYLIST, LBN_SELCHANGE),
                     reinterpret_cast<LPARAM>(hwnd));

        g_dragFrom = index;
        g_dragTarget = index;
        g_dragActive = FALSE;
        g_dragStart = pt;
        SetCapture(hwnd);
        return 0;
    }

    case WM_LBUTTONDBLCLK: {
        const POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        if (ItemIndexUnderPoint(hwnd, pt) < 0) {
            SetFocus(hwnd);
            return 0;
        }
        break;
    }

    case WM_MOUSEMOVE: {
        if (g_dragFrom < 0) break;
        const POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        if (!g_dragActive) {
            if (abs(pt.x - g_dragStart.x) < GetSystemMetrics(SM_CXDRAG) &&
                abs(pt.y - g_dragStart.y) < GetSystemMetrics(SM_CYDRAG)) {
                break;
            }
            g_dragActive = TRUE;
            SetCursor(LoadCursorW(nullptr, IDC_SIZENS));
        }

        RECT client = {};
        GetClientRect(hwnd, &client);
        const int itemHeight = ListItemHeight();
        const int top = static_cast<int>(SendMessageW(hwnd, LB_GETTOPINDEX, 0, 0));
        if (pt.y < itemHeight) {
            if (top > 0) SendMessageW(hwnd, LB_SETTOPINDEX, top - 1, 0);
        } else if (pt.y > client.bottom - itemHeight) {
            const int visible = (client.bottom - client.top) / (itemHeight > 0 ? itemHeight : 1);
            if (top + visible < static_cast<int>(SendMessageW(hwnd, LB_GETCOUNT, 0, 0)))
                SendMessageW(hwnd, LB_SETTOPINDEX, top + 1, 0);
        }

        const int target = ItemIndexFromPoint(hwnd, pt);
        if (target >= 0 && target != g_dragTarget) {
            g_dragTarget = target;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }

    case WM_LBUTTONUP: {
        if (g_dragFrom < 0) break;
        const int from = g_dragFrom;
        const int to = g_dragTarget;
        const bool moved = g_dragActive;
        g_dragFrom = -1;
        g_dragTarget = -1;
        g_dragActive = FALSE;
        ReleaseCapture();
        if (moved && to >= 0 && to != from) {
            MoveTrackInPlaylist(from, to);
            SavePlaylist(g_activePlaylist);
            FillPlaylist(to);
        } else {
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }

    case WM_CAPTURECHANGED:
        if (g_dragFrom >= 0) {
            g_dragFrom = -1;
            g_dragTarget = -1;
            g_dragActive = FALSE;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        break;

    case WM_RBUTTONUP: {
        if (g_dragFrom >= 0) {

            g_dragFrom = -1;
            g_dragTarget = -1;
            g_dragActive = FALSE;
            ReleaseCapture();
        }
        const POINT client = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        const int index = ItemIndexUnderPoint(hwnd, client);
        if (index < 0) break;
        POINT screen = client;
        ClientToScreen(hwnd, &screen);
        ShowPlaylistMenu(hwnd, index, screen);
        return 0;
    }

    case WM_CONTEXTMENU: {
        const POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        if (pt.x != -1 || pt.y != -1) return 0;
        const int index = static_cast<int>(SendMessageW(hwnd, LB_GETCURSEL, 0, 0));
        if (index == LB_ERR) return 0;
        RECT rc = {};
        if (SendMessageW(hwnd, LB_GETITEMRECT, static_cast<WPARAM>(index),
                         reinterpret_cast<LPARAM>(&rc)) == LB_ERR) {
            return 0;
        }
        POINT at = {rc.left + 8, rc.top + 8};
        ClientToScreen(hwnd, &at);
        ShowPlaylistMenu(hwnd, index, at);
        return 0;
    }

    case WM_SETCURSOR:
        if (LOWORD(lParam) == HTCLIENT && g_dragFrom >= 0) {
            SetCursor(LoadCursorW(nullptr, g_dragActive ? IDC_SIZENS : IDC_ARROW));
            return TRUE;
        }
        break;
    }
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

void RemoveTrackAt(int index) {
    const int pl = g_activePlaylist;
    std::vector<Track>& tracks = g_playlists[pl].tracks;
    if (index < 0 || index >= static_cast<int>(tracks.size())) return;

    if (g_pendingStart && g_pendingPlaylist == pl) {
        if (index == g_pendingIndex) {
            CancelPendingStart();
        } else if (index < g_pendingIndex) {
            --g_pendingIndex;
        }
    }

    if (pl == g_playPlaylist && index == PlayingIndex() && g_channel &&
        ChannelState(g_channel) != BASS_ACTIVE_STOPPED) {
        return;
    }

    const std::wstring text = L"Remove from playlist:\n" + tracks[index].name + L"?";
    if (MessageBoxW(g_hwnd, text.c_str(), L"Remove track",
                    MB_ICONQUESTION | MB_YESNO | MB_DEFBUTTON2) != IDYES) {
        return;
    }

    if (pl == g_playPlaylist && g_movedPlayingNext >= 0 &&
        index < g_movedPlayingNext) {
        --g_movedPlayingNext;
    }

    if (pl == g_playPlaylist && index == PlayingIndex()) {

        CloseChannel();
        PlayingIndex() = -1;
        g_idle = TRUE;
        SendMessageW(g_seek, TBM_SETPOS, TRUE, 0);
        RefreshPlayButton(FALSE);
        SetWindowTextW(g_lblTitle, L"");
        SetIdleTitle();
        UpdateTimeLabel();
    } else if (pl == g_playPlaylist && index < PlayingIndex()) {
        --PlayingIndex();
    }

    tracks.erase(tracks.begin() + index);
    if (g_playlists[pl].probeIndex > static_cast<size_t>(index))
        --g_playlists[pl].probeIndex;
    if (pl == g_playPlaylist && g_movedPlayingNext >= static_cast<int>(tracks.size()))
        g_movedPlayingNext = -1;
    SavePlaylist(pl);
    const int nextSel = std::min(index, static_cast<int>(tracks.size()) - 1);
    FillPlaylist(nextSel);
}

void RemoveSelected() {
    const int sel = static_cast<int>(SendMessageW(g_list, LB_GETCURSEL, 0, 0));
    if (sel == LB_ERR) return;
    RemoveTrackAt(sel);
}

void TransferTrackToPlaylist(int from, int target, bool move) {
    const int source = g_activePlaylist;
    if (source < 0 || source >= kPlaylistCount ||
        target < 0 || target >= kPlaylistCount || target == source) return;

    PlaylistState& sourceState = g_playlists[source];
    PlaylistState& targetState = g_playlists[target];
    if (from < 0 || from >= static_cast<int>(sourceState.tracks.size())) return;

    if (move) {
        const std::wstring question =
            L"Move the track to playlist " + std::to_wstring(target + 1) +
            L"?\n\n" + sourceState.tracks[from].name;
        if (MessageBoxW(g_hwnd, question.c_str(), L"Move track",
                        MB_ICONQUESTION | MB_YESNO | MB_DEFBUTTON2) != IDYES) {
            return;
        }
    }

    const bool pendingTrack = move && g_pendingStart && !g_pendingResume &&
                              g_pendingPlaylist == source && g_pendingIndex == from;
    const bool activeTrack = !pendingTrack && source == g_playPlaylist &&
                             sourceState.currentIndex == from && PlaylistSoundActive();

    if (move && source == g_playPlaylist && g_movedPlayingNext >= 0 &&
        from < g_movedPlayingNext) {
        --g_movedPlayingNext;
    }

    const Track transferred = sourceState.tracks[from];
    if (move) {
        sourceState.tracks.erase(sourceState.tracks.begin() + from);
        if (from < sourceState.currentIndex) {
            --sourceState.currentIndex;
        } else if (from == sourceState.currentIndex) {
            sourceState.currentIndex = -1;
        }
        if (sourceState.probeIndex > static_cast<size_t>(from))
            --sourceState.probeIndex;

        if (g_pendingStart && g_pendingPlaylist == source && from < g_pendingIndex)
            --g_pendingIndex;

        if (source == g_playPlaylist) {
            if (g_movedPlayingNext >= static_cast<int>(sourceState.tracks.size()))
                g_movedPlayingNext = -1;
            if (activeTrack) {
                g_movedPlayingNext = (from < static_cast<int>(sourceState.tracks.size()))
                                         ? from : -1;
            }
        }
    }

    const int targetIndex = static_cast<int>(targetState.tracks.size());
    targetState.tracks.push_back(transferred);

    if (pendingTrack) {
        g_playPlaylist = target;
        g_pendingPlaylist = target;
        g_pendingIndex = targetIndex;
        targetState.currentIndex = targetIndex;
    }

    if (move) SavePlaylist(source);
    SavePlaylist(target);

    if (move) {
        const int nextSel = std::min(from, static_cast<int>(sourceState.tracks.size()) - 1);
        FillPlaylist(nextSel);
    } else {
        FillPlaylist(from);
    }
    RefreshPlaylistButtons();
}

int ActivePlaylistIndex() {
    const int pl = g_activePlaylist;
    const int count = static_cast<int>(g_playlists[pl].tracks.size());
    if (g_pendingStart && g_pendingPlaylist == pl &&
        g_pendingIndex >= 0 && g_pendingIndex < count) {
        return g_pendingIndex;
    }
    if (pl != g_playPlaylist) return -1;
    const int current = g_playlists[pl].currentIndex;
    if (current >= 0 && current < count) return current;
    return -1;
}

void MoveTrackAfterActive(int from, int anchor) {
    const int count = static_cast<int>(VisibleTracks().size());
    if (from < 0 || from >= count || anchor < 0 || anchor >= count) return;
    if (from == anchor) return;

    const int to = (from < anchor) ? anchor : anchor + 1;
    if (to == from) return;
    MoveTrackInPlaylist(from, to);
    SavePlaylist(g_activePlaylist);

    if (g_pendingStart && g_pendingPlaylist == g_activePlaylist &&
        g_pendingIndex == anchor) {
        g_pendingIndex = to - 1;
    }

    FillPlaylist(to);
}

void AppendPlaylistMenuItem(HMENU menu, UINT id, const std::wstring& text, bool enabled) {
    AppendMenuW(menu, MF_OWNERDRAW | (enabled ? MF_ENABLED : MF_GRAYED), id,
                reinterpret_cast<LPCWSTR>(&text));
}

void AppendPlaylistMenuSeparator(HMENU menu) {
    AppendMenuW(menu, MF_OWNERDRAW | MF_GRAYED, 0, nullptr);
}

void MeasurePlaylistMenuItem(MEASUREITEMSTRUCT* item) {
    const std::wstring* text = reinterpret_cast<const std::wstring*>(item->itemData);
    if (!text) {
        item->itemWidth = 1;
        item->itemHeight = static_cast<UINT>(S(9));
        return;
    }

    HDC dc = GetDC(g_hwnd);
    HFONT oldFont = static_cast<HFONT>(SelectObject(dc, UiFont()));
    SIZE size = {};
    GetTextExtentPoint32W(dc, text->c_str(), static_cast<int>(text->size()), &size);
    SelectObject(dc, oldFont);
    ReleaseDC(g_hwnd, dc);

    item->itemWidth = static_cast<UINT>(size.cx + S(32));
    item->itemHeight = static_cast<UINT>(S(30));
}

void DrawPlaylistMenuItem(const DRAWITEMSTRUCT* item) {
    const std::wstring* text = reinterpret_cast<const std::wstring*>(item->itemData);
    if (!text) {
        RECT line = item->rcItem;
        line.left += S(8);
        line.right -= S(8);
        line.top = (item->rcItem.top + item->rcItem.bottom) / 2;
        line.bottom = line.top + 1;
        FillRect(item->hDC, &line, SolidBrush(kPlaylistMenuSeparator));
        return;
    }

    const bool selected = (item->itemState & ODS_SELECTED) != 0;
    const bool disabled = (item->itemState & ODS_DISABLED) != 0;
    FillRect(item->hDC, &item->rcItem,
             SolidBrush(selected ? kPlaylistMenuSelected : kPlaylistMenuBack));

    HFONT oldFont = static_cast<HFONT>(SelectObject(item->hDC, UiFont()));
    SetBkMode(item->hDC, TRANSPARENT);
    SetTextColor(item->hDC, disabled ? kMainMissText : kMainText);

    RECT rc = item->rcItem;
    rc.left += S(14);
    rc.right -= S(10);
    DrawTextW(item->hDC, text->c_str(), -1, &rc,
              DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_NOPREFIX);
    SelectObject(item->hDC, oldFont);
}

void ShowPlaylistMenu(HWND list, int index, POINT screenPt) {
    if (index < 0 || index >= static_cast<int>(VisibleTracks().size())) return;

    const int anchor = ActivePlaylistIndex();

    SetFocus(list);
    SendMessageW(list, LB_SETCURSEL, static_cast<WPARAM>(index), 0);
    SendMessageW(GetParent(list), WM_COMMAND,
                 MAKEWPARAM(ID_PLAYLIST, LBN_SELCHANGE),
                 reinterpret_cast<LPARAM>(list));

    const bool canRemove = !(g_activePlaylist == g_playPlaylist &&
                             index == PlayingIndex() && g_channel &&
                             ChannelState(g_channel) != BASS_ACTIVE_STOPPED);

    const bool canBeNext = (anchor >= 0) && (index != anchor) && (index != anchor + 1);

    std::wstring itemText[2 * kPlaylistCount + 4];
    int itemTextIndex = 0;

    HMENU menu = CreatePopupMenu();
    if (!menu) return;
    MENUINFO menuInfo = {};
    menuInfo.cbSize = sizeof(menuInfo);
    menuInfo.fMask = MIM_BACKGROUND;
    menuInfo.hbrBack = SolidBrush(kPlaylistMenuBack);
    SetMenuInfo(menu, &menuInfo);

    itemText[itemTextIndex] = L"Play next";
    AppendPlaylistMenuItem(menu, ID_MENU_PLAY_NEXT, itemText[itemTextIndex++], canBeNext);
    const Track& contextTrack = VisibleTracks()[index];
    AppendPlaylistMenuSeparator(menu);
    for (int target = 0; target < kPlaylistCount; ++target) {
        if (target == g_activePlaylist) continue;
        itemText[itemTextIndex] =
            L"Move to playlist " + std::to_wstring(target + 1);
        AppendPlaylistMenuItem(menu, ID_MENU_MOVE_TO_PLAYLIST0 + target,
                               itemText[itemTextIndex++], true);
    }
    for (int target = 0; target < kPlaylistCount; ++target) {
        if (target == g_activePlaylist) continue;
        itemText[itemTextIndex] =
            L"Copy to playlist " + std::to_wstring(target + 1);
        AppendPlaylistMenuItem(menu, ID_MENU_COPY_TO_PLAYLIST0 + target,
                               itemText[itemTextIndex++], true);
    }
    AppendPlaylistMenuSeparator(menu);
    itemText[itemTextIndex] = L"Export MP3 without re-encoding";
    AppendPlaylistMenuItem(menu, ID_MENU_EXPORT, itemText[itemTextIndex++],
                           !contextTrack.missing && HasExtension(contextTrack.path, L".mp3"));
    AppendPlaylistMenuSeparator(menu);
    itemText[itemTextIndex] = L"Remove";
    AppendPlaylistMenuItem(menu, ID_MENU_REMOVE, itemText[itemTextIndex++], canRemove);

    HWND owner = GetParent(list);
    if (!owner) owner = g_hwnd;
    SetForegroundWindow(owner);
    g_playlistMenuOpen = TRUE;
    const int cmd = static_cast<int>(TrackPopupMenu(
        menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY,
        screenPt.x, screenPt.y, 0, owner, nullptr));
    g_playlistMenuOpen = FALSE;
    DestroyMenu(menu);

    if (cmd == ID_MENU_PLAY_NEXT)
        MoveTrackAfterActive(index, anchor);
    else if (cmd == ID_MENU_EXPORT)
        OpenWaveExport(VisibleTracks()[index].path);
    else if (cmd == ID_MENU_REMOVE)
        RemoveTrackAt(index);
    else if (cmd >= ID_MENU_MOVE_TO_PLAYLIST0 &&
             cmd < ID_MENU_MOVE_TO_PLAYLIST0 + kPlaylistCount)
        TransferTrackToPlaylist(index, cmd - ID_MENU_MOVE_TO_PLAYLIST0, true);
    else if (cmd >= ID_MENU_COPY_TO_PLAYLIST0 &&
             cmd < ID_MENU_COPY_TO_PLAYLIST0 + kPlaylistCount)
        TransferTrackToPlaylist(index, cmd - ID_MENU_COPY_TO_PLAYLIST0, false);
}

void ClearPlaylist() {
    const int pl = g_activePlaylist;
    std::vector<Track>& tracks = g_playlists[pl].tracks;
    if (tracks.empty()) return;

    wchar_t question[192];
    swprintf_s(question, L"Clear playlist %d?\n\nTracks in playlist: %d",
               pl + 1, static_cast<int>(tracks.size()));
    if (MessageBoxW(g_hwnd, question, L"Clear playlist",
                    MB_ICONQUESTION | MB_YESNO | MB_DEFBUTTON2) != IDYES) {
        return;
    }

    if (g_pendingStart && g_pendingPlaylist == pl) CancelPendingStart();

    const bool keepSound = pl == g_playPlaylist && PlaylistSoundActive();
    if (keepSound) {
        const Track* playing = CurrentPlayingTrack();
        if (playing) {
            g_detachedPlayingTrack = *playing;
            g_playingTrackDetached = TRUE;
        }
    } else if (pl == g_playPlaylist) {
        CloseChannel();
        g_idle = TRUE;
        g_seekPending = FALSE;
        g_dragging = FALSE;
        SendMessageW(g_seek, TBM_SETPOS, TRUE, 0);
        SetWindowTextW(g_lblTitle, L"");
        SetIdleTitle();
        UpdateTimeLabel();
        RefreshPlayButton(FALSE);
    }

    tracks.clear();
    if (pl == g_playPlaylist) g_movedPlayingNext = -1;
    g_playlists[pl].currentIndex = -1;
    g_playlists[pl].probeIndex = 0;
    SavePlaylist(pl);
    FillPlaylist(-1);
    UpdateRemoveButtonState();
}

// ---------------------------------------------------------------- playback

BOOL StartPlayback(const std::wstring& path) {
    CloseChannel();

    const DWORD modeFlags = g_mixerMode ? (BASS_SAMPLE_FLOAT | BASS_STREAM_DECODE) : 0;

    g_channel = BASS_StreamCreateFile(FALSE, path.c_str(), 0, 0, BASS_UNICODE | modeFlags);
    if (!g_channel) {
        g_isMusic = TRUE;
        g_channel = BASS_MusicLoad(0, path.c_str(), 0, 0,
                                   BASS_MUSIC_RAMP | BASS_UNICODE | modeFlags, 0);
    }
    if (!g_channel) {
        const DWORD bassError = BASS_ErrorGetCode();
        wchar_t msg[400];
        if (!FileExists(path)) {
            swprintf_s(msg, L"File not found:\n%s\n\n"
                            L"The file may have been deleted or moved.\n"
                            L"Restore it to its original location or remove the track from the playlist.",
                       FileNameOf(path).c_str());
            MessageBoxW(g_hwnd, msg, L"File not found", MB_ICONWARNING | MB_OK);
        } else {
            swprintf_s(msg, L"Could not open file:\n%s\n\nBASS error code: %lu",
                       FileNameOf(path).c_str(), bassError);
            MessageBoxW(g_hwnd, msg, L"Error", MB_ICONERROR | MB_OK);
        }
        return FALSE;
    }

    ApplyVolume();
    if (!ChannelStart(g_channel, FALSE)) {
        const DWORD bassError = BASS_ErrorGetCode();
        CloseChannel();
        wchar_t msg[400];
        swprintf_s(msg, L"Could not start playback:\n%s\n\n"
                        L"BASS error code: %lu",
                   FileNameOf(path).c_str(), bassError);
        MessageBoxW(g_hwnd, msg, L"Error", MB_ICONERROR | MB_OK);
        return FALSE;
    }
    SetTitleText(path);
    RefreshPlayButton(FALSE);
    SendMessageW(g_seek, TBM_SETPOS, TRUE, 0);
    UpdateTimeLabel();
    return TRUE;
}

BOOL PlayIndex(int playlist, int index) {
    if (playlist < 0 || playlist >= kPlaylistCount) return FALSE;
    std::vector<Track>& tracks = g_playlists[playlist].tracks;
    if (index < 0 || index >= static_cast<int>(tracks.size())) return FALSE;
    if (tracks[index].missing) {

        SetWindowTextW(g_lblTitle, (tracks[index].name + L" - file not found").c_str());
        return FALSE;
    }

    const int previousIndex = g_playlists[playlist].currentIndex;
    const int previousPlaylist = g_playPlaylist;
    g_playPlaylist = playlist;
    g_playlists[playlist].currentIndex = index;
    if (!StartPlayback(tracks[index].path)) {
        g_playlists[playlist].currentIndex = previousIndex;
        g_playPlaylist = previousPlaylist;
        return FALSE;
    }
    ApplyAutoVolume();

    if (!(tracks[index].length > 0.0) && g_channel) {
        const QWORD len = BASS_ChannelGetLength(g_channel, BASS_POS_BYTE);
        if (len != static_cast<QWORD>(-1))
            tracks[index].length = BASS_ChannelBytes2Seconds(g_channel, len);
    }
    if (g_activePlaylist == playlist) FillPlaylist(index);

    RefreshPlaylistButtons();
    return TRUE;
}

// ------------------------------------------------- delay before playback

void CancelPendingStart() {
    const BOOL wasResume = g_pendingResume != FALSE;
    const int readyIndex = g_pendingIndex;
    const int readyPlaylist = g_pendingPlaylist;
    g_pendingStart = FALSE;
    g_pendingResume = FALSE;
    g_pendingIndex = -1;
    g_pendingTicks = 0;

    if (wasResume) {

        RefreshPlayButton(FALSE);
        UpdateTimeLabel();
        RefreshPlaylistButtons();
        return;
    }

    CloseChannel();
    g_playPlaylist = readyPlaylist;
    std::vector<Track>& tracks = g_playlists[readyPlaylist].tracks;
    if (readyIndex >= 0 && readyIndex < static_cast<int>(tracks.size()))
        g_playlists[readyPlaylist].currentIndex = readyIndex;
    g_idle = TRUE;
    SendMessageW(g_seek, TBM_SETPOS, TRUE, 0);
    FillPlaylist(g_activePlaylist == readyPlaylist
                     ? g_playlists[readyPlaylist].currentIndex : -1);
    UpdateTimeLabel();
    RefreshPlayButton(FALSE);
    RefreshPlaylistButtons();
}

void CompletePendingStart() {
    if (!g_pendingStart) return;
    const BOOL resume = g_pendingResume;
    const int playlist = g_pendingPlaylist;
    const int index = g_pendingIndex;
    g_pendingStart = FALSE;
    g_pendingResume = FALSE;
    g_pendingIndex = -1;
    g_pendingTicks = 0;

    if (resume) {
        if (g_channel) {
            g_idle = FALSE;

            BOOL resumed = FALSE;
            if (ChannelState(g_channel) == BASS_ACTIVE_PAUSED)
                resumed = ChannelResume(g_channel);
            else
                resumed = ChannelStart(g_channel, TRUE);
            if (!resumed) g_idle = TRUE;
        }
    } else if (index >= 0 &&
               index < static_cast<int>(g_playlists[playlist].tracks.size())) {
        g_playlists[playlist].currentIndex = -1;
        PlayIndex(playlist, index);
    }
    UpdateTimeLabel();
    RefreshPlayButton(FALSE);
}

void RequestPlayIndex(int playlist, int index) {
    if (playlist < 0 || playlist >= kPlaylistCount) return;
    std::vector<Track>& tracks = g_playlists[playlist].tracks;
    if (index < 0 || index >= static_cast<int>(tracks.size())) return;
    if (tracks[index].missing) {
        SetWindowTextW(g_lblTitle, (tracks[index].name + L" - file not found").c_str());
        return;
    }
    g_movedPlayingNext = -1;
    if (!g_delayOn) {
        PlayIndex(playlist, index);
        return;
    }

    CloseChannel();
    g_playPlaylist = playlist;
    g_playlists[playlist].currentIndex = index;
    g_pendingPlaylist = playlist;
    g_pendingIndex = index;
    g_pendingResume = FALSE;
    g_pendingTicks = DelayTicks();
    g_pendingStart = TRUE;
    g_idle = TRUE;
    SendMessageW(g_seek, TBM_SETPOS, TRUE, 0);
    SetTitleText(tracks[index].path);
    if (g_activePlaylist == playlist) FillPlaylist(index);
    UpdateTimeLabel();
    RefreshPlayButton(FALSE);
    RefreshPlaylistButtons();
}

void SetReadyTrack(int playlist, int index) {
    if (playlist < 0 || playlist >= kPlaylistCount) return;
    std::vector<Track>& tracks = g_playlists[playlist].tracks;
    if (index < 0 || index >= static_cast<int>(tracks.size())) return;
    g_playPlaylist = playlist;
    g_playlists[playlist].currentIndex = index;
    if (g_activePlaylist == playlist) FillPlaylist(index);
    SetTitleText(tracks[index].path);
}

void StepTrack(int delta) {
    const int playlist = (g_channel || g_pendingStart) ? g_playPlaylist : g_activePlaylist;
    if (g_activePlaylist != playlist) SwitchPlaylist(playlist);

    std::vector<Track>& tracks = g_playlists[playlist].tracks;
    const int count = static_cast<int>(tracks.size());
    if (count == 0) return;

    const int marked = g_playlists[playlist].currentIndex;
    const int current = (marked >= 0 && marked < count) ? marked : 0;
    int target = current + delta;
    if (g_movedPlayingNext >= 0 && playlist == g_playPlaylist) {
        target = (delta > 0) ? g_movedPlayingNext : g_movedPlayingNext - 1;
    }

    while (target >= 0 && target < count && tracks[target].missing) target += delta;
    if (target < 0 || target >= count) return;

    const bool playing = ChannelState(g_channel) == BASS_ACTIVE_PLAYING;
    if (playing || g_pendingStart) {
        RequestPlayIndex(playlist, target);
        return;
    }

    if (g_channel) {
        CloseChannel();
        SendMessageW(g_seek, TBM_SETPOS, TRUE, 0);
        g_idle = TRUE;
        UpdateTimeLabel();
        RefreshPlayButton(FALSE);
    }
    SetReadyTrack(playlist, target);
    RefreshPlaylistButtons();
}

void TogglePlayPause() {
    if (g_pendingStart) {

        CompletePendingStart();
        return;
    }
    if (!g_channel) {
        std::vector<Track>& tracks = VisibleTracks();
        if (tracks.empty()) return;

        const int marked = g_playlists[g_activePlaylist].currentIndex;
        const int ready = (marked >= 0 && marked < static_cast<int>(tracks.size()))
                          ? marked : 0;
        RequestPlayIndex(g_activePlaylist, ready);
        return;
    }

    const DWORD state = ChannelState(g_channel);
    if (state == BASS_ACTIVE_PLAYING) {
        ChannelPause(g_channel);
    } else if (g_delayOn) {

        g_pendingStart = TRUE;
        g_pendingResume = TRUE;
        g_pendingPlaylist = g_playPlaylist;
        g_pendingIndex = -1;
        g_pendingTicks = DelayTicks();
    } else {
        BOOL resumed = FALSE;
        if (state == BASS_ACTIVE_PAUSED)
            resumed = ChannelResume(g_channel);
        else
            resumed = ChannelStart(g_channel, TRUE);
        g_idle = resumed ? FALSE : TRUE;
    }
    RefreshPlayButton(FALSE);
    UpdateTimeLabel();
    RefreshPlaylistButtons();
}

void StopPlayback() {
    if (g_pendingStart) CancelPendingStart();
    if (!g_channel) return;
    ChannelStop(g_channel);
    ChannelSeek(g_channel, 0);
    SendMessageW(g_seek, TBM_SETPOS, TRUE, 0);
    RefreshPlayButton(FALSE);
    g_idle = TRUE;
    g_dragging = FALSE;
    g_seekPending = FALSE;
    UpdateTimeLabel();
    RefreshPlaylistButtons();
}

// ---------------------------------------------------------------- window

HFONT UiFont() {
    if (!g_font) {
        NONCLIENTMETRICSW ncm = {};
        ncm.cbSize = sizeof(ncm);
        if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0)) {
            LOGFONTW lf = ncm.lfMessageFont;
            lf.lfHeight = ScaledFontHeight(lf.lfHeight);
            g_font = CreateFontIndirectW(&lf);
        }
    }
    return g_font;
}

void RebuildFonts() {
    if (g_font) { DeleteObject(g_font); g_font = nullptr; }
    if (g_listFont) { DeleteObject(g_listFont); g_listFont = nullptr; }
    if (g_labelFont) { DeleteObject(g_labelFont); g_labelFont = nullptr; }
    UiFont();
    ListFont();
    LabelFont();
}

HWND MakeButton(HWND parent, HINSTANCE inst, const wchar_t* text, int id,
                int x, int y, int w, int h, DWORD extraStyle = 0) {
    HWND btn = CreateWindowExW(0, L"BUTTON", text,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | extraStyle,
        x, y, w, h, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), inst, nullptr);
    SendMessageW(btn, WM_SETFONT, reinterpret_cast<WPARAM>(UiFont()), TRUE);
    return btn;
}

void FillSolidRect(HDC dc, const RECT& rc, COLORREF color) {
    FillRect(dc, &rc, SolidBrush(color));
}

void FrameSolidRect(HDC dc, const RECT& rc, COLORREF color) {
    FrameRect(dc, &rc, SolidBrush(color));
}

// ---- custom caption for auxiliary windows

bool CaptionPointIn(const RECT& rc, int x, int y) {
    return x >= rc.left && x < rc.right && y >= rc.top && y < rc.bottom;
}

bool CaptionHasMinimize(HWND hwnd) {
    return g_hwnd != nullptr && hwnd == g_hwnd;
}

RECT CaptionMinimizeRect(const RECT& client) {
    const int right = client.right - kCaptionSide;
    const int left = right - kCaptionBtnW;
    const int top = (kCaptionH - kCaptionBtnH) / 2;
    return {left - kCaptionGap - kCaptionBtnW, top,
            left - kCaptionGap, top + kCaptionBtnH};
}

RECT CaptionCloseRect(const RECT& client) {
    const int right = client.right - kCaptionSide;
    const int top = (kCaptionH - kCaptionBtnH) / 2;
    return {right - kCaptionBtnW, top, right, top + kCaptionBtnH};
}

void DrawCaptionButton(HDC dc, const RECT& rc, bool closeButton) {
    FillSolidRect(dc, rc, kMainBtnFill);
    FrameSolidRect(dc, rc, kMainBtnEdge);

    const int cx = (rc.left + rc.right) / 2;
    const int cy = (rc.top + rc.bottom) / 2;
    const COLORREF color = closeButton ? RGB(196, 132, 126) : kMainDimText;
    HPEN pen = SolidPen(color);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    const int half = (std::max)(2, S(3));
    if (closeButton) {
        MoveToEx(dc, cx - half, cy - half, nullptr);
        LineTo(dc, cx + half + 1, cy + half + 1);
        MoveToEx(dc, cx + half, cy - half, nullptr);
        LineTo(dc, cx - half - 1, cy + half + 1);
    } else {
        const int width = (std::max)(3, S(7));
        MoveToEx(dc, cx - width, cy, nullptr);
        LineTo(dc, cx + width + 1, cy);
    }
    SelectObject(dc, oldPen);
}

void DrawCustomCaption(HWND hwnd, HDC dc, const RECT& client) {
    const RECT bar = {client.left, client.top, client.right, client.top + kCaptionH};
    FillSolidRect(dc, bar, kMainCaption);

    const bool hasMinimize = CaptionHasMinimize(hwnd);
    wchar_t title[512] = L"";
    GetWindowTextW(hwnd, title, static_cast<int>(std::size(title)));
    HFONT oldFont = static_cast<HFONT>(SelectObject(dc, UiFont()));
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, kMainText);
    RECT titleRc = bar;
    titleRc.left += S(12);

    titleRc.right = (hasMinimize ? CaptionMinimizeRect(client).left
                                : CaptionCloseRect(client).left) - S(8);
    DrawTextW(dc, title, -1, &titleRc,
              DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_NOPREFIX | DT_END_ELLIPSIS);
    SelectObject(dc, oldFont);

    if (hasMinimize) DrawCaptionButton(dc, CaptionMinimizeRect(client), false);
    DrawCaptionButton(dc, CaptionCloseRect(client), true);

    const RECT divider = {client.left, client.top + kCaptionH - 1,
                          client.right, client.top + kCaptionH};
    FillSolidRect(dc, divider, kMainLine);
    FrameSolidRect(dc, client, kMainBtnEdge);
}

bool HandleCaptionMouseDown(HWND hwnd, LPARAM lParam) {
    RECT client = {};
    GetClientRect(hwnd, &client);
    const int x = GET_X_LPARAM(lParam);
    const int y = GET_Y_LPARAM(lParam);
    if (CaptionPointIn(CaptionCloseRect(client), x, y)) {
        SendMessageW(hwnd, WM_CLOSE, 0, 0);
        return true;
    }
    if (CaptionHasMinimize(hwnd) &&
        CaptionPointIn(CaptionMinimizeRect(client), x, y)) {
        ShowWindow(hwnd, SW_MINIMIZE);
        return true;
    }
    return false;
}

void ReleaseWindowTreeCapture(HWND hwnd) {
    if (!hwnd) return;
    const HWND capture = GetCapture();
    if (capture && GetAncestor(capture, GA_ROOT) == GetAncestor(hwnd, GA_ROOT))
        ReleaseCapture();
}

LRESULT CustomCaptionHitTest(HWND hwnd, LPARAM lParam, bool resizable = false) {
    POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
    ScreenToClient(hwnd, &pt);

    RECT client = {};
    GetClientRect(hwnd, &client);

    const bool onMinimize = CaptionHasMinimize(hwnd) &&
                            CaptionPointIn(CaptionMinimizeRect(client), pt.x, pt.y);
    if (onMinimize || CaptionPointIn(CaptionCloseRect(client), pt.x, pt.y))
        return HTCLIENT;

    if (resizable) {
        const int edge = (std::max)(3, S(6));
        const bool left = pt.x < edge;
        const bool right = pt.x >= client.right - edge;
        const bool top = pt.y < edge;
        const bool bottom = pt.y >= client.bottom - edge;
        if (top && left) return HTTOPLEFT;
        if (top && right) return HTTOPRIGHT;
        if (bottom && left) return HTBOTTOMLEFT;
        if (bottom && right) return HTBOTTOMRIGHT;
        if (left) return HTLEFT;
        if (right) return HTRIGHT;
        if (top) return HTTOP;
        if (bottom) return HTBOTTOM;
    }

    if (pt.y < 0 || pt.y >= kCaptionH) return HTCLIENT;
    return HTCAPTION;
}

void FillSolidCircle(HDC dc, const RECT& rc, COLORREF fill, COLORREF edge) {
    HBRUSH brush = SolidBrush(fill);
    HPEN pen = SolidPen(edge);
    HGDIOBJ oldBrush = SelectObject(dc, brush);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    Ellipse(dc, rc.left, rc.top, rc.right, rc.bottom);
    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
}

void DrawStepIcon(HDC dc, int cx, int cy, BOOL forward, COLORREF color) {
    const int half = S(8), wide = S(11), barW = S(3), gap = S(2);
    const int total = wide + gap + barW;
    const int left = cx - total / 2;

    if (forward) {
        POINT tri[3] = {
            {left, cy - half},
            {left, cy + half},
            {left + wide, cy}
        };
        FillTriangle(dc, tri, color);
        const RECT bar = {left + wide + gap, cy - half, left + wide + gap + barW, cy + half};
        FillSolidRect(dc, bar, color);
    } else {
        const int right = left + total;
        POINT tri[3] = {
            {right, cy - half},
            {right, cy + half},
            {right - wide, cy}
        };
        FillTriangle(dc, tri, color);
        const RECT bar = {left, cy - half, left + barW, cy + half};
        FillSolidRect(dc, bar, color);
    }
}

void DrawMenuIcon(HDC dc, int cx, int cy, int halfWidth, int lineHeight,
                  int lineGap, COLORREF color) {
    for (int i = -1; i <= 1; ++i) {
        const int y = cy + i * lineGap - lineHeight / 2;
        const RECT line = {cx - halfWidth, y, cx + halfWidth, y + lineHeight};
        FillSolidRect(dc, line, color);
    }
}

void DrawIconButton(const DRAWITEMSTRUCT* dis) {
    const int id = static_cast<int>(dis->CtlID);
    const bool isPlay = (id == ID_BTN_PLAY);
    const bool pressed = (dis->itemState & ODS_SELECTED) != 0;
    const bool disabled = (dis->itemState & ODS_DISABLED) != 0;
    RECT rc = dis->rcItem;

    const bool glow = isPlay && !disabled && (g_uiPlaying || (g_uiPaused && g_blinkOn));
    COLORREF back = glow ? kMainGlowBack : kMainBtnFill;
    if (disabled)      back = kMainBtnOff;
    else if (pressed)  back = glow ? kMainPressed : kMainBtnDown;

    FillSolidRect(dis->hDC, rc, back);
    FrameSolidRect(dis->hDC, rc,
                   disabled ? kMainBtnOffEdge : (glow ? kMainGlowEdge : kMainBtnEdge));

    const COLORREF icon = disabled ? kMainDimText : (glow ? kMainGlowText : kMainIcon);
    const int cx = (rc.left + rc.right) / 2;
    const int cy = (rc.top + rc.bottom) / 2;

    switch (id) {
    case ID_BTN_PLAY:
        if (g_uiPlaying) {

            const int barW = S(5), barH = S(16), gap = S(3);
            const RECT bars[2] = {
                {cx - gap - barW, cy - barH / 2, cx - gap, cy + barH / 2},
                {cx + gap, cy - barH / 2, cx + gap + barW, cy + barH / 2}
            };
            for (const RECT& bar : bars) FillSolidRect(dis->hDC, bar, icon);
        } else {
            DrawPlayTriangle(dis->hDC, cx + S(1), cy, S(9), S(13), icon);
        }
        break;

    case ID_BTN_STOP: {
        const RECT square = {cx - S(8), cy - S(8), cx + S(8), cy + S(8)};
        FillSolidRect(dis->hDC, square, icon);
        break;
    }

    case ID_BTN_PREV:
        DrawStepIcon(dis->hDC, cx, cy, FALSE, icon);
        break;

    case ID_BTN_NEXT:
        DrawStepIcon(dis->hDC, cx, cy, TRUE, icon);
        break;

    case ID_BTN_ADD:
    case ID_BTN_REMOVE: {

        const int len = S(6);
        const int thick = S(4);
        const int half = thick / 2;
        const RECT horizontal = {cx - len, cy - half, cx + len, cy + half};
        FillSolidRect(dis->hDC, horizontal, icon);
        if (id == ID_BTN_ADD) {
            const RECT vertical = {cx - half, cy - len, cx + half, cy + len};
            FillSolidRect(dis->hDC, vertical, icon);
        }
        break;
    }

    case ID_BTN_SETTINGS:
        DrawMenuIcon(dis->hDC, cx, cy, S(9), (std::max)(1, S(2)),
                     S(5), kMainDimText);
        break;
    }

}

void DrawTextButton(const DRAWITEMSTRUCT* dis, BOOL on, const wchar_t* text,
                    BOOL blue = FALSE) {
    RECT rc = dis->rcItem;
    const bool pressed = (dis->itemState & ODS_SELECTED) != 0;

    COLORREF back = on ? (blue ? kMainBlueBack : kMainGlowBack) : kMainBtnFill;
    if (pressed) {
        back = on ? (blue ? kMainBlueDown : kMainPressed)
                  : kMainBtnDown;
    }

    FillSolidRect(dis->hDC, rc, back);
    FrameSolidRect(dis->hDC, rc,
                   on ? (blue ? kMainBlueEdge : kMainGlowEdge) : kMainBtnEdge);

    HFONT oldFont = static_cast<HFONT>(SelectObject(dis->hDC, UiFont()));
    SetBkMode(dis->hDC, TRANSPARENT);
    SetTextColor(dis->hDC, on ? (blue ? kMainBlueText : kMainGlowText) : kMainText);
    DrawTextW(dis->hDC, text, -1, &rc,
              DT_SINGLELINE | DT_VCENTER | DT_CENTER | DT_NOPREFIX);
    SelectObject(dis->hDC, oldFont);

}

void DrawToggleButton(const DRAWITEMSTRUCT* dis) {
    const bool isNext = (dis->CtlID == ID_CHK_NEXT);
    const bool on = isNext ? (g_autoNext != FALSE) : (g_delayOn != FALSE);
    wchar_t pauseText[32];
    const wchar_t* text = nullptr;
    if (isNext) {
        text = on ? L"Next: On" : L"Next: Off";
    } else if (on) {
        swprintf_s(pauseText, L"Pause: %d s", g_delaySeconds);
        text = pauseText;
    } else {
        text = L"Pause: Off";
    }
    DrawTextButton(dis, on, text);
}

void RefreshPlayButton(BOOL advanceBlink) {
    const DWORD state = ChannelState(g_channel);
    const BOOL playing = (state == BASS_ACTIVE_PLAYING);
    const BOOL paused = (state == BASS_ACTIVE_PAUSED) || (g_pendingStart != FALSE);

    BOOL repaint = (playing != g_uiPlaying) || (paused != g_uiPaused);
    g_uiPlaying = playing;
    g_uiPaused = paused;

    if (!g_uiPaused) {
        if (g_blinkOn || g_blinkCount != 0) {
            g_blinkOn = FALSE;
            g_blinkCount = 0;
            repaint = TRUE;
        }
    } else if (advanceBlink) {
        if (++g_blinkCount >= 5) {
            g_blinkCount = 0;
            g_blinkOn = !g_blinkOn;
            repaint = TRUE;
        }
    }

    if (repaint && g_btnPlay) InvalidateRect(g_btnPlay, nullptr, FALSE);
}

BOOL SliderThumbHit(HWND hwnd, LPARAM lParam) {
    RECT thumb = {0, 0, 0, 0};
    SendMessageW(hwnd, TBM_GETTHUMBRECT, 0, reinterpret_cast<LPARAM>(&thumb));
    const POINT pt = {static_cast<short>(LOWORD(lParam)),
                      static_cast<short>(HIWORD(lParam))};
    return PtInRect(&thumb, pt);
}

BOOL SliderNavigationKey(WPARAM key) {
    switch (key) {
    case VK_LEFT: case VK_RIGHT: case VK_UP: case VK_DOWN:
    case VK_PRIOR: case VK_NEXT: case VK_HOME: case VK_END:
        return TRUE;
    default:
        return FALSE;
    }
}

LRESULT CALLBACK SeekGuardProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
                               UINT_PTR, DWORD_PTR) {
    switch (msg) {
    case WM_MOUSEWHEEL:
        return 0;

    case WM_LBUTTONDOWN:
        if (!SliderThumbHit(hwnd, lParam))
            return 0;
        break;

    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        if (SliderNavigationKey(wParam))
            return 0;
        break;
    }
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

LRESULT CALLBACK VolumeWheelProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
                                 UINT_PTR, DWORD_PTR) {
    switch (msg) {
    case WM_MOUSEWHEEL: {
        const int notches = GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA;
        if (notches != 0) {
            int pos = static_cast<int>(SendMessageW(hwnd, TBM_GETPOS, 0, 0));
            pos -= notches * 5;
            pos = (std::max)(0, (std::min)(pos, 100));
            SendMessageW(hwnd, TBM_SETPOS, TRUE, pos);
            SendMessageW(GetParent(hwnd), WM_HSCROLL,
                         MAKEWPARAM(TB_THUMBPOSITION, 0), reinterpret_cast<LPARAM>(hwnd));
        }
        return 0;
    }

    case WM_LBUTTONDOWN:
        if (!SliderThumbHit(hwnd, lParam))
            return 0;
        break;

    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        if (SliderNavigationKey(wParam))
            return 0;
        break;
    }
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

// ---------------------------------------------------------------- sampler

std::wstring PadVolumeFilePath() {
    return DataFilePath(L"padvolumes.txt");
}

bool LoadLegacyPadVolumes(std::unordered_map<std::wstring, int>& volumes) {
    std::string content;
    if (!ReadTextFileUtf8(PadVolumeFilePath(), content)) return false;

    size_t start = 0;
    while (start <= content.size()) {
        const size_t end = content.find('\n', start);
        std::string line = (end == std::string::npos) ? content.substr(start)
                                                      : content.substr(start, end - start);
        if (!line.empty() && line.back() == '\r') line.pop_back();

        const size_t tab = line.find('\t');
        if (tab != std::string::npos) {
            const std::string volText = line.substr(0, tab);
            const bool digits = !volText.empty() && volText.size() <= 3 &&
                                volText.find_first_not_of("0123456789") == std::string::npos;
            const std::wstring name = Utf8ToWStr(line.substr(tab + 1));
            if (digits && !name.empty()) {
                const int volume = (std::max)(0, (std::min)(std::stoi(volText), 100));
                volumes.emplace(name, volume);
            }
        }

        if (end == std::string::npos) break;
        start = end + 1;
    }
    return true;
}

ULONGLONG g_padStarted[kPadCount] = {};

QWORD g_padPosSeen[kPadCount] = {};

const int kPadStopIndexBits = 8;
ULONG_PTR g_padStopSerial = 0;
ULONG_PTR g_padStopToken[kPadCount] = {};

void PadClearStopSync(int i) {
    if (i < 0 || i >= kPadCount) return;
    g_padStopToken[i] = 0;
    if (g_pads[i].stopSync && g_pads[i].stream) {
        BASS_Mixer_ChannelRemoveSync(g_pads[i].stream, g_pads[i].stopSync);
    }
    g_pads[i].stopSync = 0;
}

void PadFree(int i) {

    if (g_smpWnd) KillTimer(g_smpWnd, ID_TIMER_PAD_STOP0 + i);
    PadClearStopSync(i);
    g_pads[i].fading = false;
    if (g_pads[i].stream) {
        if (g_pads[i].isMusic) BASS_MusicFree(g_pads[i].stream);
        else                   BASS_StreamFree(g_pads[i].stream);
    }
    g_pads[i].stream = 0;
    g_pads[i].isMusic = FALSE;
    g_pads[i].glow = false;
    g_padStarted[i] = 0;
    g_padPosSeen[i] = 0;

    if (g_wavePad == i && WaveWindowOpen()) SendMessageW(g_waveWnd, WM_CLOSE, 0, 0);
}

void PadApplyVolume(int i) {
    if (g_pads[i].stream)
        BASS_ChannelSetAttribute(g_pads[i].stream, BASS_ATTRIB_VOL,
                                 static_cast<float>(g_padVolume[i]) / 100.0f);
}

// -------- smooth pad stop --------

const int kPadStopFadeMs = 50;
const int kPadStopTailMs = 30;

const int kPadEdgeTimerMs = 15;

const int kPadFillTimerMs = 50;

bool AnyPadNeedsEdgeWatch() {
    for (int i = 0; i < kPadCount; ++i) {
        if (!g_pads[i].stream || g_pads[i].fading) continue;
        if (g_pads[i].stop >= 1.0) continue;
        if (ChannelState(g_pads[i].stream) == BASS_ACTIVE_PLAYING) return true;
    }
    return false;
}

void SyncSamplerTimers() {
    if (!g_smpWnd) return;
    if (IsWindowVisible(g_smpWnd))
        SetTimer(g_smpWnd, ID_TIMER_SAMPLER, TIMER_PERIOD_MS, nullptr);
    else
        KillTimer(g_smpWnd, ID_TIMER_SAMPLER);
    if (AnyPadNeedsEdgeWatch())
        SetTimer(g_smpWnd, ID_TIMER_PAD_EDGE, kPadEdgeTimerMs, nullptr);
    else
        KillTimer(g_smpWnd, ID_TIMER_PAD_EDGE);
}

void PadFadeOut(int i) {
    if (i < 0 || i >= kPadCount || !g_pads[i].stream) return;
    g_pads[i].fading = true;
    BASS_ChannelSlideAttribute(g_pads[i].stream, BASS_ATTRIB_VOL, 0.0f, kPadStopFadeMs);
    if (g_smpWnd)
        SetTimer(g_smpWnd, ID_TIMER_PAD_STOP0 + i,
                 static_cast<UINT>(kPadStopFadeMs + kPadStopTailMs), nullptr);
}

void PadCancelFade(int i) {
    if (i < 0 || i >= kPadCount) return;
    if (g_smpWnd) KillTimer(g_smpWnd, ID_TIMER_PAD_STOP0 + i);
    g_pads[i].fading = false;
    if (g_pads[i].stream)
        BASS_ChannelSetAttribute(g_pads[i].stream, BASS_ATTRIB_VOL,
                                 static_cast<float>(g_padVolume[i]) / 100.0f);
}

void PadFinishStop(int i) {
    if (i < 0 || i >= kPadCount) return;
    if (g_smpWnd) KillTimer(g_smpWnd, ID_TIMER_PAD_STOP0 + i);
    PadClearStopSync(i);
    g_pads[i].fading = false;
    if (!g_pads[i].stream) return;
    ChannelStop(g_pads[i].stream);
    ChannelSeek(g_pads[i].stream, 0);

    BASS_ChannelSetAttribute(g_pads[i].stream, BASS_ATTRIB_VOL,
                             static_cast<float>(g_padVolume[i]) / 100.0f);
    g_pads[i].glow = false;
    if (g_smpPad[i]) InvalidateRect(g_smpPad[i], nullptr, FALSE);
    SyncSamplerTimers();
}

bool PadLoadFile(int i, const std::wstring& path) {
    if (i < 0 || i >= kPadCount) return false;

    const DWORD modeFlags = g_mixerMode ? (BASS_SAMPLE_FLOAT | BASS_STREAM_DECODE) : 0;
    DWORD stream = BASS_StreamCreateFile(FALSE, path.c_str(), 0, 0, BASS_UNICODE | modeFlags);
    BOOL isMusic = FALSE;
    if (!stream) {
        stream = BASS_MusicLoad(0, path.c_str(), 0, 0,
                                BASS_MUSIC_RAMP | BASS_UNICODE | modeFlags, 0);
        isMusic = TRUE;
    }
    if (!stream) return false;

    PadFree(i);
    g_pads[i].path = path;
    g_pads[i].stream = stream;
    g_pads[i].isMusic = isMusic;

    PadApplyVolume(i);
    return true;
}

QWORD PadMarkerBytes(int i, double frac) {
    if (i < 0 || i >= kPadCount || !g_pads[i].stream) return 0;
    const QWORD len = BASS_ChannelGetLength(g_pads[i].stream, BASS_POS_BYTE);

    if (!len || len == static_cast<QWORD>(-1)) return 0;
    return static_cast<QWORD>(frac * static_cast<double>(len));
}

QWORD PadStopLeadBytes(int i) {
    if (!g_pads[i].stream) return 0;
    const QWORD bytes = BASS_ChannelSeconds2Bytes(g_pads[i].stream,
                                                  kPadStopFadeMs / 1000.0);
    if (bytes == static_cast<QWORD>(-1)) return 0;
    return bytes;
}

QWORD PadPositionBytes(int i) {
    if (i < 0 || i >= kPadCount || !g_pads[i].stream) return static_cast<QWORD>(-1);

    const QWORD len = BASS_ChannelGetLength(g_pads[i].stream, BASS_POS_BYTE);
    if (!len || len == static_cast<QWORD>(-1)) return static_cast<QWORD>(-1);

    QWORD pos = ChannelPosSafe(g_pads[i].stream, len);

    if (pos == static_cast<QWORD>(-1)) return g_padPosSeen[i];

    const QWORD from = static_cast<QWORD>(g_pads[i].start * static_cast<double>(len));

    const ULONGLONG playedMs = GetTickCount64() - g_padStarted[i];
    const QWORD played = BASS_ChannelSeconds2Bytes(g_pads[i].stream, playedMs / 1000.0);
    const QWORD slack = BASS_ChannelSeconds2Bytes(g_pads[i].stream, 0.04);
    if (played != static_cast<QWORD>(-1) && slack != static_cast<QWORD>(-1)) {
        const QWORD upper = from + played + slack;
        if (pos > upper) pos = (upper < len) ? upper : len;
        if (pos < from) pos = from;
    }

    if (pos < g_padPosSeen[i]) pos = g_padPosSeen[i];
    g_padPosSeen[i] = pos;
    return pos;
}

bool PadEndReached(int i) {
    if (i < 0 || i >= kPadCount || !g_pads[i].stream) return false;
    if (g_pads[i].stop >= 1.0) return false;
    const QWORD stop = PadMarkerBytes(i, g_pads[i].stop);
    if (!stop) return false;
    const QWORD lead = PadStopLeadBytes(i);
    const QWORD edge = (stop > lead) ? (stop - lead) : 0;
    const QWORD pos = PadPositionBytes(i);
    if (pos == static_cast<QWORD>(-1)) return false;
    return pos >= edge;
}

void CALLBACK PadStopSyncProc(HSYNC, DWORD, DWORD, void* user) {
    const ULONG_PTR token = reinterpret_cast<ULONG_PTR>(user);
    const int i = static_cast<int>(token & ((1u << kPadStopIndexBits) - 1u));
    const ULONG_PTR generation = token >> kPadStopIndexBits;
    const HWND target = g_callbackHwnd.load(std::memory_order_acquire);
    if (target)
        PostMessageW(target, WM_APP_PAD_STOP, static_cast<WPARAM>(i),
                     static_cast<LPARAM>(generation));
}

void HandlePadStopMessage(int i, ULONG_PTR generation) {
    if (i < 0 || i >= kPadCount) return;
    const ULONG_PTR token =
        (generation << kPadStopIndexBits) | static_cast<ULONG_PTR>(i);
    if (!g_padStopToken[i] || g_padStopToken[i] != token) return;

    g_padStopToken[i] = 0;
    g_pads[i].stopSync = 0;
    if (!g_pads[i].stream) return;
    g_pads[i].glow = false;
    if (g_smpPad[i]) InvalidateRect(g_smpPad[i], nullptr, FALSE);
    PadFadeOut(i);
}

void PadArmStopSync(int i, QWORD from) {
    if (i < 0 || i >= kPadCount || !g_pads[i].stream) return;
    if (g_pads[i].stop >= 1.0) return;
    if (!(g_mixerMode && BASS_Mixer_ChannelGetMixer(g_pads[i].stream))) return;
    const QWORD stop = PadMarkerBytes(i, g_pads[i].stop);
    const QWORD lead = PadStopLeadBytes(i);
    if (!stop) return;
    QWORD target = (stop > lead) ? (stop - lead) : 0;
    if (target < from) target = from;
    ++g_padStopSerial;
    if (!g_padStopSerial) ++g_padStopSerial;
    const ULONG_PTR token =
        (g_padStopSerial << kPadStopIndexBits) | static_cast<ULONG_PTR>(i);
    const HSYNC sync = BASS_Mixer_ChannelSetSync(
        g_pads[i].stream, BASS_SYNC_POS | BASS_SYNC_MIXTIME | BASS_SYNC_ONETIME,
        target, PadStopSyncProc, reinterpret_cast<void*>(token));
    if (sync) {
        g_pads[i].stopSync = sync;
        g_padStopToken[i] = token;
    }
}

double PadProgress(int i) {
    if (i < 0 || i >= kPadCount || !g_pads[i].stream) return -1.0;
    if (ChannelState(g_pads[i].stream) != BASS_ACTIVE_PLAYING) return -1.0;

    const QWORD len = BASS_ChannelGetLength(g_pads[i].stream, BASS_POS_BYTE);
    if (!len || len == static_cast<QWORD>(-1)) return -1.0;
    const QWORD from = static_cast<QWORD>(g_pads[i].start * static_cast<double>(len));
    const QWORD to = static_cast<QWORD>(g_pads[i].stop * static_cast<double>(len));
    if (to <= from) return -1.0;

    const QWORD pos = PadPositionBytes(i);
    if (pos == static_cast<QWORD>(-1)) return -1.0;
    if (pos <= from) return 0.0;
    double frac = static_cast<double>(pos - from) / static_cast<double>(to - from);
    if (frac < 0.0) frac = 0.0;
    if (frac > 1.0) frac = 1.0;
    return frac;
}

const int kPadStartFadeMs = 8;
void PadStartFromMarker(int i) {
    if (i < 0 || i >= kPadCount || !g_pads[i].stream) return;

    g_padStarted[i] = GetTickCount64();
    PadClearStopSync(i);

    g_pads[i].glow = true;
    if (g_smpPad[i]) InvalidateRect(g_smpPad[i], nullptr, FALSE);

    const QWORD from = PadMarkerBytes(i, g_pads[i].start);

    g_padPosSeen[i] = from;
    if (!from) {
        if (!ChannelStart(g_pads[i].stream, TRUE)) {
            g_pads[i].glow = false;
            if (g_smpPad[i]) InvalidateRect(g_smpPad[i], nullptr, FALSE);
            return;
        }
        PadArmStopSync(i, 0);
        return;
    }

    const float vol = static_cast<float>(g_padVolume[i]) / 100.0f;
    BASS_ChannelSetAttribute(g_pads[i].stream, BASS_ATTRIB_VOL, 0.0f);
    ChannelSeek(g_pads[i].stream, from);
    if (!ChannelStart(g_pads[i].stream, FALSE)) {
        BASS_ChannelSetAttribute(g_pads[i].stream, BASS_ATTRIB_VOL, vol);
        g_pads[i].glow = false;
        if (g_smpPad[i]) InvalidateRect(g_smpPad[i], nullptr, FALSE);
        return;
    }

    PadArmStopSync(i, from);
    if (!BASS_ChannelSlideAttribute(g_pads[i].stream, BASS_ATTRIB_VOL, vol, kPadStartFadeMs))
        BASS_ChannelSetAttribute(g_pads[i].stream, BASS_ATTRIB_VOL, vol);
}

void PadTrigger(int i) {
    if (i < 0 || i >= kPadCount || !g_pads[i].stream) return;

    if (g_pads[i].fading) {
        PadCancelFade(i);
        PadStartFromMarker(i);
        SyncSamplerTimers();
        return;
    }

    if (ChannelState(g_pads[i].stream) == BASS_ACTIVE_PLAYING) {

        g_pads[i].glow = false;
        if (g_smpPad[i]) InvalidateRect(g_smpPad[i], nullptr, FALSE);
        PadClearStopSync(i);
        PadFadeOut(i);
        SyncSamplerTimers();
        return;
    }

    PadStartFromMarker(i);

    SyncSamplerTimers();
}

std::wstring SamplerFilePath() {
    return DataFilePath(L"sampler.txt");
}

const int kMarkUnits = 1000000;
int MarkUnits(double frac) {
    int value = static_cast<int>(frac * kMarkUnits + 0.5);
    if (value < 0) value = 0;
    if (value > kMarkUnits) value = kMarkUnits;
    return value;
}

bool SaveSampler() {
    std::string content;
    const unsigned char bom[3] = {0xEF, 0xBB, 0xBF};
    content.append(reinterpret_cast<const char*>(bom), 3);
    content += "# pads: path, marker A, marker B, volume; positions are millionths"
               " of the length (1000000 = end of file)\n";
    for (int i = 0; i < kPadCount; ++i) {

        content += WStrToUtf8(g_pads[i].path);
        content += '\t';
        content += std::to_string(MarkUnits(g_pads[i].start));
        content += '\t';
        content += std::to_string(MarkUnits(g_pads[i].stop));
        content += '\t';
        content += std::to_string((std::max)(0, (std::min)(g_padVolume[i], 100)));
        content += '\n';
    }

    return WriteTextFileAtomic(SamplerFilePath(), content);
}

void LoadSampler() {
    std::string content;
    if (!ReadTextFileUtf8(SamplerFilePath(), content)) return;

    std::string rows[kPadCount];
    int rowCount = 0;
    bool hintMillionths = false;
    size_t start = 0;
    while (start <= content.size() && rowCount < kPadCount) {
        const size_t end = content.find('\n', start);
        std::string line = (end == std::string::npos) ? content.substr(start)
                                                      : content.substr(start, end - start);
        if (!line.empty() && line.back() == '\r') line.pop_back();

        const bool blank = line.find_first_not_of(" \t") == std::string::npos;
        if (!blank && line[0] == '#') {

            if (line.find("1000000") != std::string::npos) hintMillionths = true;
        } else if (!blank) {
            rows[rowCount++] = line;
        }
        if (end == std::string::npos) break;
        start = end + 1;
    }
    if (!rowCount) return;

    bool millionths = hintMillionths;
    for (int i = 0; i < rowCount && !millionths; ++i) {
        const std::string& line = rows[i];
        for (size_t tab = line.find('\t'); tab != std::string::npos;
             tab = line.find('\t', tab + 1)) {
            if (strtol(line.c_str() + tab + 1, nullptr, 10) > 10000) {
                millionths = true;
                break;
            }
        }
    }
    const double markScale = millionths ? static_cast<double>(kMarkUnits) : 10000.0;

    std::unordered_map<std::wstring, int> legacyVolumes;
    bool legacyLoaded = false;
    bool needRewrite = false;

    for (int pad = 0; pad < rowCount; ++pad) {

        std::string parts[4];
        int partCount = 0;
        size_t from = 0;
        while (partCount < 4) {
            const size_t tab = rows[pad].find('\t', from);
            parts[partCount++] = (tab == std::string::npos)
                                     ? rows[pad].substr(from)
                                     : rows[pad].substr(from, tab - from);
            if (tab == std::string::npos) break;
            from = tab + 1;
        }

        double regionStart = 0.0, regionStop = 1.0;
        if (partCount >= 3) {
            regionStart = strtol(parts[1].c_str(), nullptr, 10) / markScale;
            regionStop = strtol(parts[2].c_str(), nullptr, 10) / markScale;
        }

        if (!(regionStart >= 0.0 && regionStart < regionStop && regionStop <= 1.0)) {
            regionStart = 0.0;
            regionStop = 1.0;
            needRewrite = true;
        }
        g_pads[pad].start = regionStart;
        g_pads[pad].stop = regionStop;

        bool hasVolume = false;
        int volume = kPadDefaultVolume;
        if (partCount >= 4) {
            const std::string& text = parts[3];
            const bool digits = !text.empty() && text.size() <= 3 &&
                                text.find_first_not_of("0123456789") == std::string::npos;
            if (digits) {
                volume = std::stoi(text);
                hasVolume = true;
            }
        }

        const std::wstring path = Utf8ToWStr(parts[0]);
        if (!hasVolume) {
            if (!legacyLoaded) {
                LoadLegacyPadVolumes(legacyVolumes);
                legacyLoaded = true;
            }
            const std::wstring name = FileNameOf(path);
            if (!name.empty()) {
                const auto found = legacyVolumes.find(name);
                if (found != legacyVolumes.end()) volume = found->second;
            }
            needRewrite = true;
        }
        g_padVolume[pad] = (std::max)(0, (std::min)(volume, 100));

        if (path.empty()) continue;
        if (!PadLoadFile(pad, path)) g_pads[pad].path = path;
    }

    if (needRewrite && SaveSampler() && legacyLoaded)
        DeleteFileW(PadVolumeFilePath().c_str());
}

const COLORREF kSmpBack      = RGB(26, 26, 28);
const COLORREF kSmpText      = RGB(206, 206, 212);
const COLORREF kSmpDimText   = RGB(120, 120, 126);
const COLORREF kSmpBtnFill   = RGB(52, 52, 58);
const COLORREF kSmpBtnEdge   = RGB(96, 96, 104);
const COLORREF kSmpPressed   = RGB(38, 38, 44);
const COLORREF kSmpPadFill   = RGB(38, 38, 42);
const COLORREF kSmpPadEdge   = RGB(88, 88, 94);
const COLORREF kSmpGlowFill  = RGB(38, 66, 44);
const COLORREF kSmpGlowEdge  = RGB(64, 200, 84);
const COLORREF kSmpGlowText  = RGB(206, 242, 212);
const COLORREF kSmpPlayedFill = RGB(52, 124, 68);
const COLORREF kSmpMinusSign = RGB(214, 96, 88);
const COLORREF kSmpGroove    = RGB(12, 12, 14);
const COLORREF kSmpGrooveEdge = RGB(70, 70, 78);
const COLORREF kSmpHandle    = RGB(198, 198, 204);
const COLORREF kSmpHandleEdge = RGB(92, 92, 100);
const COLORREF kSmpVolFill   = RGB(108, 112, 120);
const COLORREF kSmpFocusRing = RGB(132, 132, 140);

HBRUSH SamplerBackBrush() {
    static HBRUSH brush = nullptr;
    if (!brush) brush = CreateSolidBrush(kSmpBack);
    return brush;
}

void DrawFocusRing(HDC dc, const RECT& rc, COLORREF color) {
    const int inset = (std::max)(1, S(2));
    const RECT ring = {rc.left + inset, rc.top + inset, rc.right - inset, rc.bottom - inset};
    FrameSolidRect(dc, ring, color);
}

// ------------------------------------------------ pad volume slider

const int kBaseVolGrooveInset = 6;
const int kBaseVolGrooveH     = 6;
const int kBaseVolHandleW     = 13;
const int kBaseVolHandleH     = 18;

int kVolGrooveInset = kBaseVolGrooveInset;
int kVolGrooveH = kBaseVolGrooveH;
int kVolHandleW = kBaseVolHandleW;
int kVolHandleH = kBaseVolHandleH;

int g_padVolDrag = -1;
int g_padVolGrabOffset = 0;

int PadSliderIndex(HWND slider) {
    return GetDlgCtrlID(slider) - ID_SMP_VOL0;
}

RECT PadVolGrooveRect(HWND slider) {
    RECT rc = {};
    GetClientRect(slider, &rc);
    const int midY = (rc.top + rc.bottom) / 2;
    const RECT groove = {rc.left + kVolGrooveInset, midY - kVolGrooveH / 2,
                         rc.right - kVolGrooveInset, midY - kVolGrooveH / 2 + kVolGrooveH};
    return groove;
}

int PadVolTravelLeft(HWND slider) {
    const RECT groove = PadVolGrooveRect(slider);
    return groove.left + kVolHandleW / 2;
}

int PadVolTravelRight(HWND slider) {
    const RECT groove = PadVolGrooveRect(slider);
    return groove.right - kVolHandleW / 2;
}

int PadVolHandleCenter(HWND slider, int volume) {
    const int left = PadVolTravelLeft(slider);
    const int right = PadVolTravelRight(slider);
    volume = (std::max)(0, (std::min)(volume, 100));
    return left + (right - left) * volume / 100;
}

int PadVolValueAt(HWND slider, int x) {
    const int left = PadVolTravelLeft(slider);
    const int right = PadVolTravelRight(slider);
    if (right <= left) return 0;
    const int value = (x - left) * 100 / (right - left);
    return (std::max)(0, (std::min)(value, 100));
}

RECT PadVolHandleRect(HWND slider) {
    const RECT groove = PadVolGrooveRect(slider);
    const int midY = (groove.top + groove.bottom) / 2;
    const int i = (std::max)(0, (std::min)(PadSliderIndex(slider), kPadCount - 1));
    const int cx = PadVolHandleCenter(slider, g_padVolume[i]);
    const RECT handle = {cx - kVolHandleW / 2, midY - kVolHandleH / 2,
                         cx - kVolHandleW / 2 + kVolHandleW, midY - kVolHandleH / 2 + kVolHandleH};
    return handle;
}

void SetPadVolume(int i, int volume) {
    if (i < 0 || i >= kPadCount) return;
    g_padVolume[i] = (std::max)(0, (std::min)(volume, 100));
    PadApplyVolume(i);
    if (g_wavePad == i && g_waveAudition) {
        BASS_ChannelSetAttribute(g_waveAudition, BASS_ATTRIB_VOL,
                                 static_cast<float>(g_padVolume[i]) / 100.0f);
    }
    if (g_smpVol[i]) InvalidateRect(g_smpVol[i], nullptr, FALSE);
}

void DrawPadSlider(HWND slider, HDC dc) {
    RECT rc = {};
    GetClientRect(slider, &rc);
    FillSolidRect(dc, rc, kSmpBack);

    const RECT groove = PadVolGrooveRect(slider);
    const RECT handle = PadVolHandleRect(slider);
    FillSolidRect(dc, groove, kSmpGroove);

    const RECT filled = {groove.left, groove.top,
                         (std::max)(groove.left, handle.left + kVolHandleW / 2), groove.bottom};
    if (filled.right > filled.left) FillSolidRect(dc, filled, kSmpVolFill);
    FrameSolidRect(dc, groove, kSmpGrooveEdge);

    FillSolidRect(dc, handle, kSmpHandle);
    FrameSolidRect(dc, handle, kSmpHandleEdge);

}

LRESULT CALLBACK PadSliderProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    const int i = PadSliderIndex(hwnd);
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps = {};
        HDC dc = BeginPaint(hwnd, &ps);
        DrawPadSlider(hwnd, dc);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_LBUTTONDOWN: {
        const int x = GET_X_LPARAM(lParam);
        SetFocus(hwnd);
        RECT handle = PadVolHandleRect(hwnd);
        handle.left -= 4;
        handle.right += 4;
        const POINT pt = {x, GET_Y_LPARAM(lParam)};
        if (!PtInRect(&handle, pt)) break;
        g_padVolDrag = i;
        g_padVolGrabOffset = x - PadVolHandleCenter(hwnd, g_padVolume[i]);
        SetCapture(hwnd);
        SetPadVolume(i, PadVolValueAt(hwnd, x - g_padVolGrabOffset));
        return 0;
    }

    case WM_MOUSEMOVE:
        if (g_padVolDrag == i) {
            SetPadVolume(i, PadVolValueAt(hwnd, GET_X_LPARAM(lParam) - g_padVolGrabOffset));
            return 0;
        }
        break;

    case WM_LBUTTONUP:
        if (g_padVolDrag == i) {
            g_padVolDrag = -1;
            ReleaseCapture();
            SaveSampler();
        }
        return 0;

    case WM_CAPTURECHANGED:
        if (g_padVolDrag == i) {
            g_padVolDrag = -1;
            SaveSampler();
        }
        return 0;

    case WM_GETDLGCODE:
        return DLGC_WANTARROWS;

    case WM_KEYDOWN: {
        const int step = (GetKeyState(VK_SHIFT) & 0x8000) ? 10 : 2;
        if (wParam == VK_LEFT) {
            SetPadVolume(i, g_padVolume[i] - step);
            return 0;
        }
        if (wParam == VK_RIGHT) {
            SetPadVolume(i, g_padVolume[i] + step);
            return 0;
        }
        break;
    }

    case WM_SETCURSOR:
        if (LOWORD(lParam) == HTCLIENT) {
            POINT pt = {};
            GetCursorPos(&pt);
            ScreenToClient(hwnd, &pt);
            RECT handle = PadVolHandleRect(hwnd);
            handle.left -= 4;
            handle.right += 4;
            SetCursor(LoadCursorW(nullptr, PtInRect(&handle, pt) ? IDC_SIZEWE : IDC_ARROW));
            return TRUE;
        }
        break;

    case WM_DESTROY:
        if (i >= 0 && i < kPadCount && g_smpVol[i] == hwnd) g_smpVol[i] = nullptr;
        if (g_padVolDrag == i) g_padVolDrag = -1;
        break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void DrawSamplerPad(const DRAWITEMSTRUCT* dis) {
    const int i = static_cast<int>(dis->CtlID) - ID_SMP_PAD0;
    if (i < 0 || i >= kPadCount) return;

    RECT rc = dis->rcItem;
    const bool pressed  = (dis->itemState & ODS_SELECTED) != 0;
    const bool disabled = (dis->itemState & ODS_DISABLED) != 0;
    const bool glow     = !disabled && g_pads[i].glow;

    COLORREF back = glow ? kSmpGlowFill : kSmpPadFill;
    COLORREF edge = glow ? kSmpGlowEdge : kSmpPadEdge;
    COLORREF textColor = glow ? kSmpGlowText : kSmpText;
    if (disabled) {
        back = RGB(30, 30, 33);
        edge = RGB(58, 58, 62);
        textColor = kSmpDimText;
    } else if (pressed) {
        back = glow ? RGB(52, 92, 58) : kSmpPressed;
    }

    FillSolidRect(dis->hDC, rc, back);

    if (glow) {
        const double frac = PadProgress(i);
        const int innerW = (rc.right - rc.left) - 2;
        if (frac > 0.0 && innerW > 0) {
            int fillW = static_cast<int>(innerW * frac + 0.5);
            if (fillW > innerW) fillW = innerW;
            if (fillW > 0) {
                const RECT played = {rc.left + 1, rc.top + 1, rc.left + 1 + fillW, rc.bottom - 1};
                FillSolidRect(dis->hDC, played, kSmpPlayedFill);

                const RECT front = {played.right - 2, played.top, played.right, played.bottom};
                FillSolidRect(dis->hDC, front, kSmpGlowEdge);
            }
        }
    }

    FrameSolidRect(dis->hDC, rc, edge);

    std::wstring text;
    if (!g_pads[i].path.empty())             text = FileNameOf(g_pads[i].path);
    if (disabled && !g_pads[i].path.empty()) text += L" (file missing)";

    HFONT oldFont = static_cast<HFONT>(SelectObject(dis->hDC, UiFont()));
    SetBkMode(dis->hDC, TRANSPARENT);
    SetTextColor(dis->hDC, textColor);
    RECT textRc = rc;
    textRc.left += S(10);
    textRc.right -= S(8);
    DrawTextW(dis->hDC, text.c_str(), -1, &textRc,
              DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_NOPREFIX | DT_END_ELLIPSIS);
    SelectObject(dis->hDC, oldFont);

    if ((dis->itemState & ODS_FOCUS) && !disabled)
        DrawFocusRing(dis->hDC, rc, kSmpFocusRing);
}

void DrawPadSignButton(const DRAWITEMSTRUCT* dis, bool minus) {
    RECT rc = dis->rcItem;
    const bool pressed  = (dis->itemState & ODS_SELECTED) != 0;
    const bool disabled = (dis->itemState & ODS_DISABLED) != 0;

    FillSolidRect(dis->hDC, rc, (pressed && !disabled) ? kSmpPressed : kSmpBtnFill);
    FrameSolidRect(dis->hDC, rc, disabled ? RGB(58, 58, 62) : kSmpBtnEdge);

    const int cx = (rc.left + rc.right) / 2;
    const int cy = (rc.top + rc.bottom) / 2;
    const int len = S(6);
    const int thick = S(3);
    const int half = thick / 2;

    const COLORREF sign = disabled ? kSmpDimText : (minus ? kSmpMinusSign : kSmpText);
    const RECT horizontal = {cx - len, cy - half, cx + len + 1, cy - half + thick};
    FillSolidRect(dis->hDC, horizontal, sign);
    if (!minus) {
        const RECT vertical = {cx - half, cy - len, cx - half + thick, cy + len + 1};
        FillSolidRect(dis->hDC, vertical, sign);
    }

    if ((dis->itemState & ODS_FOCUS) && !disabled)
        DrawFocusRing(dis->hDC, rc, kSmpFocusRing);
}

void DrawPadLetterButton(const DRAWITEMSTRUCT* dis) {
    RECT rc = dis->rcItem;
    const bool pressed  = (dis->itemState & ODS_SELECTED) != 0;
    const bool disabled = (dis->itemState & ODS_DISABLED) != 0;

    FillSolidRect(dis->hDC, rc, (pressed && !disabled) ? kSmpPressed : kSmpBtnFill);
    FrameSolidRect(dis->hDC, rc, disabled ? RGB(58, 58, 62) : kSmpBtnEdge);

    HFONT oldFont = static_cast<HFONT>(SelectObject(dis->hDC, UiFont()));
    SetBkMode(dis->hDC, TRANSPARENT);
    SetTextColor(dis->hDC, disabled ? kSmpDimText : kSmpText);
    DrawTextW(dis->hDC, L"R", -1, &rc,
              DT_SINGLELINE | DT_VCENTER | DT_CENTER | DT_NOPREFIX);
    SelectObject(dis->hDC, oldFont);

    if ((dis->itemState & ODS_FOCUS) && !disabled)
        DrawFocusRing(dis->hDC, rc, kSmpFocusRing);
}

BOOL PadRangeEnabled(int i) {
    return g_pads[i].stream ? TRUE : FALSE;
}

void RefreshRangeButtons() {
    if (!g_smpWnd) return;
    for (int i = 0; i < kPadCount; ++i)
        if (g_smpRange[i]) EnableWindow(g_smpRange[i], PadRangeEnabled(i));
}

void UpdatePadUi(int i) {
    if (!g_smpWnd || i < 0 || i >= kPadCount) return;
    if (g_smpPad[i]) {
        EnableWindow(g_smpPad[i], g_pads[i].stream ? TRUE : FALSE);
        InvalidateRect(g_smpPad[i], nullptr, FALSE);
    }

    if (g_smpClear[i])
        EnableWindow(g_smpClear[i], g_pads[i].path.empty() ? FALSE : TRUE);

    if (g_smpRange[i])
        EnableWindow(g_smpRange[i], PadRangeEnabled(i));

    if (g_smpVol[i])
        InvalidateRect(g_smpVol[i], nullptr, FALSE);
}

void LoadPadDialog(int i) {
    if (i < 0 || i >= kPadCount) return;

    std::vector<wchar_t> buffer(32 * 1024, L'\0');
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_smpWnd ? g_smpWnd : g_hwnd;
    ofn.lpstrFilter = kAudioFilter;
    ofn.lpstrFile = buffer.data();
    ofn.nMaxFile = static_cast<DWORD>(buffer.size());
    ofn.lpstrDefExt = L"mp3";
    ofn.lpstrTitle = L"Pad file";
    ofn.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;

    if (!GetOpenFileNameW(&ofn)) return;

    const std::wstring path = buffer.data();
    if (!PadLoadFile(i, path)) {
        wchar_t msg[200];
        swprintf_s(msg, L"Could not open file:\n%s\n\nBASS error code: %lu",
                   FileNameOf(path).c_str(), BASS_ErrorGetCode());
        MessageBoxW(g_smpWnd ? g_smpWnd : g_hwnd, msg, L"Error", MB_ICONERROR | MB_OK);
        return;
    }

    g_pads[i].start = 0.0;
    g_pads[i].stop = 1.0;
    UpdatePadUi(i);
    SaveSampler();
}

void ClearPadDialog(int i) {
    if (i < 0 || i >= kPadCount) return;
    if (g_pads[i].path.empty()) return;

    const std::wstring name = FileNameOf(g_pads[i].path);
    wchar_t msg[512];
    swprintf_s(msg, L"Clear pad %s?", name.c_str());
    if (MessageBoxW(g_smpWnd ? g_smpWnd : g_hwnd, msg, L"Clear pad",
                    MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES)
        return;

    PadFree(i);
    g_pads[i].path.clear();
    if (g_smpPad[i]) InvalidateRect(g_smpPad[i], nullptr, FALSE);
    UpdatePadUi(i);
    SaveSampler();
}

const int kBasePadRowH   = 46;
const int kBasePadBtnH   = 34;
const int kBasePadTop    = 14;
const int kBasePadLoadX  = 14;
const int kBasePadClearGap = 6;
const int kBasePadRangeGap = 6;
const int kBasePadBtnW   = 176;
const int kBasePadVolW   = 122;

int kPadRowH = kBasePadRowH;
int kPadBtnH = kBasePadBtnH;
int kPadTop = kBasePadTop;
int kPadLoadX = kBasePadLoadX;
int kPadLoadW = kBasePadBtnH;
int kPadClearGap = kBasePadClearGap;
int kPadClearX = kBasePadLoadX + kBasePadBtnH + kBasePadClearGap;
int kPadClearW = kBasePadBtnH;
int kPadRangeGap = kBasePadRangeGap;
int kPadRangeX = kBasePadLoadX + kBasePadBtnH + kBasePadClearGap + kBasePadBtnH + kBasePadRangeGap;
int kPadRangeW = kBasePadBtnH;
int kPadBtnX = kBasePadLoadX + kBasePadBtnH + kBasePadClearGap + kBasePadBtnH +
               kBasePadRangeGap + kBasePadBtnH + 8;
int kPadBtnW = kBasePadBtnW;
int kPadVolX = kBasePadLoadX + kBasePadBtnH + kBasePadClearGap + kBasePadBtnH +
               kBasePadRangeGap + kBasePadBtnH + 8 + kBasePadBtnW + 10;
int kPadVolW = kBasePadVolW;
int kPadsWinW = kBasePadLoadX + kBasePadBtnH + kBasePadClearGap + kBasePadBtnH +
                kBasePadRangeGap + kBasePadBtnH + 8 + kBasePadBtnW + 10 + kBasePadVolW + 14;
int kPadsWinH = kBasePadTop + kPadCount * kBasePadRowH - (kBasePadRowH - kBasePadBtnH) + 14;

void LayoutSampler(HWND hwnd) {
    if (!hwnd) return;
    for (int i = 0; i < kPadCount; ++i) {
        const int y = kPadTop + i * kPadRowH;
        if (g_smpLoad[i]) MoveWindow(g_smpLoad[i], kPadLoadX, y, kPadLoadW, kPadBtnH, TRUE);
        if (g_smpClear[i]) MoveWindow(g_smpClear[i], kPadClearX, y, kPadClearW, kPadBtnH, TRUE);
        if (g_smpRange[i]) MoveWindow(g_smpRange[i], kPadRangeX, y, kPadRangeW, kPadBtnH, TRUE);
        if (g_smpPad[i]) MoveWindow(g_smpPad[i], kPadBtnX, y, kPadBtnW, kPadBtnH, TRUE);
        if (g_smpVol[i]) MoveWindow(g_smpVol[i], kPadVolX, y + S(2), kPadVolW, S(30), TRUE);
    }
}

void UpdatePadFill() {
    static int shown[kPadCount] = {};
    if (!g_smpWnd) return;

    for (int i = 0; i < kPadCount; ++i) {

        const double frac = g_pads[i].glow ? PadProgress(i) : -1.0;
        const int px = (frac > 0.0) ? static_cast<int>(kPadBtnW * frac + 0.5) : 0;
        if (px == shown[i]) continue;
        shown[i] = px;
        if (g_smpPad[i]) InvalidateRect(g_smpPad[i], nullptr, FALSE);
    }
}

LRESULT CALLBACK SamplerProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    LRESULT frameResult = 0;
    if (SkipSystemFramePaint(msg, frameResult)) return frameResult;
    switch (msg) {
    case WM_CREATE: {
        DisableSystemWindowFrame(hwnd);
        HINSTANCE inst = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(hwnd, GWLP_HINSTANCE));

        WNDCLASSEXW sliderClass = {};
        sliderClass.cbSize = sizeof(sliderClass);
        sliderClass.lpfnWndProc = PadSliderProc;
        sliderClass.hInstance = inst;
        sliderClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        sliderClass.hbrBackground = nullptr;
        sliderClass.lpszClassName = L"BassPadSlider";
        RegisterClassExW(&sliderClass);

        for (int i = 0; i < kPadCount; ++i) {
            const int y = kPadTop + i * kPadRowH;

            g_smpLoad[i] = MakeButton(hwnd, inst, L"+", ID_SMP_LOAD0 + i,
                                      kPadLoadX, y, kPadLoadW, kPadBtnH, BS_OWNERDRAW);
            g_smpClear[i] = MakeButton(hwnd, inst, L"-", ID_SMP_CLEAR0 + i,
                                       kPadClearX, y, kPadClearW, kPadBtnH, BS_OWNERDRAW);

            EnableWindow(g_smpClear[i], g_pads[i].path.empty() ? FALSE : TRUE);
            g_smpRange[i] = MakeButton(hwnd, inst, L"", ID_SMP_RANGE0 + i,
                                       kPadRangeX, y, kPadRangeW, kPadBtnH, BS_OWNERDRAW);
            g_smpPad[i] = MakeButton(hwnd, inst, L"", ID_SMP_PAD0 + i,
                                     kPadBtnX, y, kPadBtnW, kPadBtnH, BS_OWNERDRAW);
            g_smpVol[i] = CreateWindowExW(0, sliderClass.lpszClassName, L"",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                kPadVolX, y + S(2), kPadVolW, S(30), hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_SMP_VOL0 + i)), inst, nullptr);
        }
        LayoutSampler(hwnd);

        for (int i = 0; i < kPadCount; ++i) UpdatePadUi(i);
        SetTimer(hwnd, ID_TIMER_SAMPLER, 100, nullptr);

        SetTimer(hwnd, ID_TIMER_PAD_EDGE, kPadEdgeTimerMs, nullptr);
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT: {
        PAINTSTRUCT ps = {};
        HDC dc = BeginPaint(hwnd, &ps);
        RECT rc = {};
        GetClientRect(hwnd, &rc);
        FillSolidRect(dc, rc, kSmpBack);
        DrawCustomCaption(hwnd, dc, rc);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_NCHITTEST:
        return CustomCaptionHitTest(hwnd, lParam);

    case WM_LBUTTONDOWN:
        if (HandleCaptionMouseDown(hwnd, lParam)) return 0;
        break;

    case WM_COMMAND: {
        const int id = LOWORD(wParam);
        if (id >= ID_SMP_PAD0 && id < ID_SMP_PAD0 + kPadCount) {
            PadTrigger(id - ID_SMP_PAD0);
            return 0;
        }
        if (id >= ID_SMP_LOAD0 && id < ID_SMP_LOAD0 + kPadCount) {
            LoadPadDialog(id - ID_SMP_LOAD0);
            return 0;
        }
        if (id >= ID_SMP_CLEAR0 && id < ID_SMP_CLEAR0 + kPadCount) {
            ClearPadDialog(id - ID_SMP_CLEAR0);
            return 0;
        }
        if (id >= ID_SMP_RANGE0 && id < ID_SMP_RANGE0 + kPadCount) {
            OpenWaveWindow(id - ID_SMP_RANGE0);
            return 0;
        }
        break;
    }

    case WM_DRAWITEM: {
        LPDRAWITEMSTRUCT dis = reinterpret_cast<LPDRAWITEMSTRUCT>(lParam);
        if (dis->CtlType == ODT_BUTTON &&
            dis->CtlID >= static_cast<UINT>(ID_SMP_PAD0) &&
            dis->CtlID < static_cast<UINT>(ID_SMP_PAD0 + kPadCount)) {
            DrawSamplerPad(dis);
            return TRUE;
        }
        if (dis->CtlType == ODT_BUTTON &&
            dis->CtlID >= static_cast<UINT>(ID_SMP_LOAD0) &&
            dis->CtlID < static_cast<UINT>(ID_SMP_LOAD0 + kPadCount)) {
            DrawPadSignButton(dis, false);
            return TRUE;
        }
        if (dis->CtlType == ODT_BUTTON &&
            dis->CtlID >= static_cast<UINT>(ID_SMP_CLEAR0) &&
            dis->CtlID < static_cast<UINT>(ID_SMP_CLEAR0 + kPadCount)) {
            DrawPadSignButton(dis, true);
            return TRUE;
        }
        if (dis->CtlType == ODT_BUTTON &&
            dis->CtlID >= static_cast<UINT>(ID_SMP_RANGE0) &&
            dis->CtlID < static_cast<UINT>(ID_SMP_RANGE0 + kPadCount)) {
            DrawPadLetterButton(dis);
            return TRUE;
        }
        break;
    }

    case WM_TIMER:

        if (wParam >= ID_TIMER_PAD_STOP0 && wParam < ID_TIMER_PAD_STOP0 + kPadCount) {
            PadFinishStop(static_cast<int>(wParam - ID_TIMER_PAD_STOP0));
            return 0;
        }
        if (wParam == ID_TIMER_SAMPLER) {
            for (int i = 0; i < kPadCount; ++i) {

                const bool playing = !g_pads[i].fading && g_pads[i].stream &&
                    ChannelState(g_pads[i].stream) == BASS_ACTIVE_PLAYING;
                if (playing != g_pads[i].glow) {
                    g_pads[i].glow = playing;
                    if (g_smpPad[i]) InvalidateRect(g_smpPad[i], nullptr, FALSE);
                }
            }
            return 0;
        }
        if (wParam == ID_TIMER_PAD_EDGE) {

            for (int i = 0; i < kPadCount; ++i) {
                if (g_pads[i].fading || !g_pads[i].stream) continue;
                if (g_pads[i].stopSync) continue;
                if (ChannelState(g_pads[i].stream) != BASS_ACTIVE_PLAYING) continue;
                if (!PadEndReached(i)) continue;
                g_pads[i].glow = false;
                if (g_smpPad[i]) InvalidateRect(g_smpPad[i], nullptr, FALSE);
                PadFadeOut(i);
            }

            if (!AnyPadNeedsEdgeWatch()) KillTimer(hwnd, ID_TIMER_PAD_EDGE);
            return 0;
        }
        if (wParam == ID_TIMER_PAD_FILL) {
            UpdatePadFill();
            return 0;
        }
        return 0;

    case WM_SYSCOMMAND:
        if ((wParam & 0xFFF0) == SC_MINIMIZE) {
            SendMessageW(hwnd, WM_CLOSE, 0, 0);
            return 0;
        }
        break;

    case WM_CLOSE:
        SaveSampler();
        KillTimer(hwnd, ID_TIMER_PAD_FILL);
        ShowWindow(hwnd, SW_HIDE);
        SyncSamplerTimers();
        return 0;

    case WM_DESTROY:
        KillTimer(hwnd, ID_TIMER_SAMPLER);
        KillTimer(hwnd, ID_TIMER_PAD_EDGE);
        KillTimer(hwnd, ID_TIMER_PAD_FILL);
        g_smpWnd = nullptr;
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void OpenSamplerWindow() {
    if (!g_smpWnd) {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = SamplerProc;
        wc.hInstance = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(g_hwnd, GWLP_HINSTANCE));
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = SamplerBackBrush();
        wc.lpszClassName = L"BassSamplerWindow";
        wc.hIcon = PlayerIcon(GetSystemMetrics(SM_CXICON));
        wc.hIconSm = PlayerIcon(GetSystemMetrics(SM_CXSMICON));
        RegisterClassExW(&wc);

        RECT rc = {0, 0, kPadsWinW, kPadsWinH};

        const DWORD style = WS_POPUP | WS_CLIPCHILDREN;
        AdjustWindowRect(&rc, style, FALSE);

        g_smpWnd = CreateWindowExW(WS_EX_CONTROLPARENT, wc.lpszClassName, L"Sampler",
            style, CW_USEDEFAULT, CW_USEDEFAULT,
            rc.right - rc.left, rc.bottom - rc.top,
            g_hwnd, nullptr, wc.hInstance, nullptr);
        if (!g_smpWnd) return;
    }

    ShowWindow(g_smpWnd, IsIconic(g_smpWnd) ? SW_RESTORE : SW_SHOW);
    SetForegroundWindow(g_smpWnd);

    SetTimer(g_smpWnd, ID_TIMER_PAD_FILL, kPadFillTimerMs, nullptr);
    SyncSamplerTimers();
}

void ToggleSamplerWindow() {
    if (g_smpWnd && IsWindowVisible(g_smpWnd)) {
        if (IsIconic(g_smpWnd)) {
            ShowWindow(g_smpWnd, SW_RESTORE);
            SetForegroundWindow(g_smpWnd);
            return;
        }
        SendMessageW(g_smpWnd, WM_CLOSE, 0, 0);
        return;
    }
    OpenSamplerWindow();
}

// ------------------------------------------------------ sample waveform

const int kBaseWaveWinW     = 1200;
const int kBaseWaveWinH     = 460;
const int kBaseWaveWinMinW  = 420;
const int kBaseWaveWinMinH  = 250;
const DWORD kWaveWinStyle = WS_POPUP | WS_THICKFRAME | WS_CLIPCHILDREN;
const int kBaseWaveMargin   = 12;
const int kBaseWaveScrollH  = 12;
const int kBaseWaveGap      = 5;
const int kBaseWaveBarH     = 30;
const int kBaseWaveButtonW  = 105;
const int kBaseWaveExportGap = 35;

const int kWavePeaks    = 65536;
const double kWaveMaxZoom = 100.0;
const int kWaveTimerMs  = 50;

int kWaveWinW = kBaseWaveWinW;
int kWaveWinH = kBaseWaveWinH;
int kWaveWinMinW = kBaseWaveWinMinW;
int kWaveWinMinH = kBaseWaveWinMinH;
int kWaveMargin = kBaseWaveMargin;
int kWaveScrollH = kBaseWaveScrollH;
int kWaveGap = kBaseWaveGap;
int kWaveBarH = kBaseWaveBarH;
int kWaveButtonW = kBaseWaveButtonW;
int kWaveExportGap = kBaseWaveExportGap;

const int kWaveDragFrameMs = 16;
const UINT_PTR ID_TIMER_WAVE_FRAME = 7;

const COLORREF kWaveArea  = RGB(12, 12, 14);
const COLORREF kWaveLine  = RGB(64, 200, 84);
const COLORREF kWaveZero  = RGB(58, 58, 64);
const COLORREF kWaveMarkA = RGB(86, 152, 232);
const COLORREF kWaveMarkB = RGB(214, 96, 88);
const COLORREF kWavePlayhead = RGB(238, 238, 244);

struct WaveView {
    std::vector<float> lo;
    std::vector<float> hi;
    float peak = 0.0f;
    size_t total = 0;
    size_t done = 0;
    int sampleRate = 0;
    int bitrateKbps = 0;
};

WaveView g_wave;
std::wstring g_wavePath;
enum class WaveMode {
    Sampler,
    Export
};
enum class WaveAuditionKind {
    None,
    Cursor,
    MarkerRange
};
bool WaveStartAudition(HWND hwnd, WaveAuditionKind kind,
                       double start, double stop, bool showError,
                       bool rememberStart);
WaveMode g_waveMode = WaveMode::Sampler;
int g_waveDrag = 0;
double g_waveZoom = 1.0;
double g_waveViewStart = 0.0;
BOOL g_waveScrollDrag = FALSE;
double g_waveScrollGrab = 0.0;
BOOL g_wavePanDrag = FALSE;
double g_wavePanGrab = 0.0;
BOOL g_wavePanPending = FALSE;
POINT g_wavePanStart = {};
double g_wavePlayhead = -1.0;
double g_waveCursor = -1.0;
double g_waveAuditionStart = -1.0;
double g_waveAuditionEnd = -1.0;
WaveAuditionKind g_waveAuditionKind = WaveAuditionKind::None;
ULONGLONG g_waveFrameTick = 0;
HWND g_wavePlayBtn = nullptr;
HWND g_waveRangeBtn = nullptr;
BOOL g_waveBtnPlaying = FALSE;
HWND g_waveSaveBtn = nullptr;
BOOL g_waveMarkerMenuOpen = FALSE;
BOOL g_waveAuditionIsMusic = FALSE;
std::wstring g_waveExportSource;
std::wstring g_exportDirectory;
double g_waveExportStart = 0.0;
double g_waveExportStop = 1.0;
BOOL g_waveExportBusy = FALSE;
std::thread g_waveExportThread;
std::atomic<bool> g_waveExportCancel{false};

HBITMAP g_waveBackBmp = nullptr;
int g_waveBackW = 0;
int g_waveBackH = 0;

void DrawGrip(HDC dc, const RECT& client);

bool WaveInExportMode() {
    return g_waveMode == WaveMode::Export;
}

double WaveMarkerStart() {
    if (WaveInExportMode()) return g_waveExportStart;
    if (g_wavePad < 0 || g_wavePad >= kPadCount) return 0.0;
    return g_pads[g_wavePad].start;
}

double WaveMarkerStop() {
    if (WaveInExportMode()) return g_waveExportStop;
    if (g_wavePad < 0 || g_wavePad >= kPadCount) return 1.0;
    return g_pads[g_wavePad].stop;
}

void SetWaveMarkerStart(double value) {
    if (WaveInExportMode()) g_waveExportStart = value;
    else if (g_wavePad >= 0 && g_wavePad < kPadCount) g_pads[g_wavePad].start = value;
}

void SetWaveMarkerStop(double value) {
    if (WaveInExportMode()) g_waveExportStop = value;
    else if (g_wavePad >= 0 && g_wavePad < kPadCount) g_pads[g_wavePad].stop = value;
}

bool WaveAuditionPlaying() {
    return g_waveAudition &&
           ChannelState(g_waveAudition) == BASS_ACTIVE_PLAYING;
}

std::wstring WaveAuditionSource() {
    if (WaveInExportMode()) return g_waveExportSource;
    if (g_wavePad < 0 || g_wavePad >= kPadCount) return {};
    return g_pads[g_wavePad].path;
}

float WaveAuditionVolume() {
    const int volume = WaveInExportMode()
        ? g_volume
        : (g_wavePad >= 0 && g_wavePad < kPadCount ? g_padVolume[g_wavePad] : 0);
    return static_cast<float>(volume) / 100.0f;
}

const int kWaveAuditionTailGuardMs = 100;
const int kWaveAuditionStopFadeMs = 5;

bool WaveSetAuditionStopEnvelope(QWORD stopBytes) {
    if (!g_waveAudition || !g_mixerMode) return false;

    BASS_MIXER_NODE nodes[3] = {};
    DWORD count = 0;
    if (!stopBytes) {
        nodes[0] = {0, 1.0f};
        count = 1;
    } else {
        const QWORD fadeBytes =
            BASS_ChannelSeconds2Bytes(g_waveAudition,
                                      kWaveAuditionStopFadeMs / 1000.0);
        const QWORD fadeStart =
            (fadeBytes != static_cast<QWORD>(-1) && stopBytes > fadeBytes)
                ? stopBytes - fadeBytes : 0;
        nodes[0] = {0, 1.0f};
        if (fadeStart) nodes[count++] = {fadeStart, 1.0f};
        nodes[count++] = {stopBytes, 0.0f};
    }
    return BASS_Mixer_ChannelSetEnvelope(
        g_waveAudition, BASS_MIXER_ENV_VOL, nodes, count);
}

void WaveFreeAudition() {
    if (g_waveAudition) {
        ChannelStop(g_waveAudition);
        if (g_waveAuditionIsMusic) BASS_MusicFree(g_waveAudition);
        else                         BASS_StreamFree(g_waveAudition);
    }
    g_waveAudition = 0;
    g_waveAuditionIsMusic = FALSE;
    g_waveAuditionEnd = -1.0;
    g_waveAuditionKind = WaveAuditionKind::None;
    g_waveBtnPlaying = FALSE;
}

void InvalidateWaveAuditionButtons() {
    if (g_wavePlayBtn) InvalidateRect(g_wavePlayBtn, nullptr, FALSE);
    if (g_waveRangeBtn) InvalidateRect(g_waveRangeBtn, nullptr, FALSE);
}

double WaveViewSpan() {
    return 1.0 / g_waveZoom;
}

void WaveClampView() {
    if (g_waveZoom < 1.0) g_waveZoom = 1.0;
    if (g_waveZoom > kWaveMaxZoom) g_waveZoom = kWaveMaxZoom;
    const double span = WaveViewSpan();
    if (span >= 1.0) g_waveViewStart = 0.0;
    if (g_waveViewStart < 0.0) g_waveViewStart = 0.0;
    if (g_waveViewStart > 1.0 - span) g_waveViewStart = 1.0 - span;
}

bool WaveWindowOpen() {
    return g_waveWnd != nullptr && IsWindowVisible(g_waveWnd) != FALSE &&
           IsIconic(g_waveWnd) == FALSE;
}

void WaveChunkToMono(const float* src, size_t frames, int chans, std::vector<float>& mono) {
    mono.resize(frames);
    for (size_t i = 0; i < frames; ++i) {
        float sum = 0.0f;
        for (int c = 0; c < chans; ++c) sum += src[i * static_cast<size_t>(chans) + c];
        mono[i] = sum / static_cast<float>(chans);
    }
}

void WaveViewAdd(WaveView& view, const std::vector<float>& mono) {
    const size_t cols = view.lo.size();
    if (!cols || !view.total) return;
    for (float value : mono) {
        size_t col = (view.done * cols) / view.total;
        if (col >= cols) col = cols - 1;
        if (value < view.lo[col]) view.lo[col] = value;
        if (value > view.hi[col]) view.hi[col] = value;
        const float magnitude = (value < 0.0f) ? -value : value;
        if (magnitude > view.peak) view.peak = magnitude;
        ++view.done;
    }
}

void WaveAllocColumns(size_t frames) {
    size_t cols = frames ? frames : 1;
    if (cols > static_cast<size_t>(kWavePeaks)) cols = static_cast<size_t>(kWavePeaks);
    if (cols < 2) cols = 2;
    g_wave.lo.assign(cols, 0.0f);
    g_wave.hi.assign(cols, 0.0f);
}

bool LoadWaveView(const std::wstring& path) {
    g_wave = WaveView();
    g_wavePath = path;

    DWORD dec = BASS_StreamCreateFile(FALSE, path.c_str(), 0, 0,
        BASS_UNICODE | BASS_SAMPLE_FLOAT | BASS_STREAM_DECODE | BASS_STREAM_PRESCAN);
    BOOL music = FALSE;
    if (!dec) {
        dec = BASS_MusicLoad(0, path.c_str(), 0, 0,
            BASS_UNICODE | BASS_SAMPLE_FLOAT | BASS_MUSIC_DECODE | BASS_MUSIC_PRESCAN, 0);
        music = TRUE;
    }
    if (!dec) return false;

    BASS_CHANNELINFO ci = {};
    if (!BASS_ChannelGetInfo(dec, &ci) || ci.chans <= 0) {
        if (music) BASS_MusicFree(dec);
        else       BASS_StreamFree(dec);
        return false;
    }
    const int chans = ci.chans;
    g_wave.sampleRate = static_cast<int>(ci.freq);
    float bitrate = 0.0f;
    if (BASS_ChannelGetAttribute(dec, BASS_ATTRIB_BITRATE, &bitrate) &&
        bitrate > 0.0f) {
        g_wave.bitrateKbps = static_cast<int>(bitrate + 0.5f);
    }
    const size_t frameBytes = sizeof(float) * static_cast<size_t>(chans);
    const QWORD lenBytes = BASS_ChannelGetLength(dec, BASS_POS_BYTE);
    const bool lengthKnown = (lenBytes && lenBytes != static_cast<QWORD>(-1)) ? true : false;
    if (lenBytes && lenBytes != static_cast<QWORD>(-1)) {
        g_wave.total = static_cast<size_t>(lenBytes / frameBytes);
        WaveAllocColumns(g_wave.total);
    }

    std::vector<float> raw(32 * 1024);
    std::vector<float> mono;
    size_t decodedFrames = 0;
    for (;;) {
        const DWORD got = BASS_ChannelGetData(dec, raw.data(),
                                              static_cast<DWORD>(raw.size() * sizeof(float)));
        if (got == 0 || got == static_cast<DWORD>(-1)) break;
        const size_t frames = got / frameBytes;
        if (!frames) break;
        WaveChunkToMono(raw.data(), frames, chans, mono);
        if (lengthKnown) WaveViewAdd(g_wave, mono);
        else             decodedFrames += frames;
    }

    if (!lengthKnown) {
        bool rewind = BASS_ChannelSetPosition(dec, 0, BASS_POS_BYTE) != FALSE;
        if (rewind) {
            g_wave.total = decodedFrames;
            WaveAllocColumns(decodedFrames);
            for (;;) {
                const DWORD got = BASS_ChannelGetData(
                    dec, raw.data(), static_cast<DWORD>(raw.size() * sizeof(float)));
                if (got == 0 || got == static_cast<DWORD>(-1)) break;
                const size_t frames = got / frameBytes;
                if (!frames) break;
                WaveChunkToMono(raw.data(), frames, chans, mono);
                WaveViewAdd(g_wave, mono);
            }
        }
    }
    if (music) BASS_MusicFree(dec);
    else       BASS_StreamFree(dec);
    return g_wave.done > 0;
}

struct Mp3FrameHeader {
    uint32_t sampleRate = 0;
    uint32_t frameBytes = 0;
    uint32_t samples = 0;
    uint16_t bitrateKbps = 0;
    uint16_t sideOffset = 0;
    uint16_t sideBytes = 0;
    int channels = 0;
};

struct Mp3Frame {
    uint64_t offset = 0;
    uint64_t sampleStart = 0;
    uint32_t bytes = 0;
    uint32_t samples = 0;
    uint32_t mainDataBytes = 0;
    uint16_t mainDataBegin = 0;
    bool metadataFrame = false;
};

struct Mp3Index {
    std::vector<Mp3Frame> frames;
    uint32_t sampleRate = 0;
    int channels = 0;
    uint64_t audioStart = 0;
    uint64_t audioEnd = 0;
    uint64_t totalSamples = 0;
};

struct Mp3ExportResult {
    bool ok = false;
    bool canceled = false;
    std::wstring error;
    std::wstring outputPath;
    double startFraction = 0.0;
    double stopFraction = 1.0;
    double duration = 0.0;
};

class ReadOnlyFileMapping {
public:
    ReadOnlyFileMapping() = default;
    ReadOnlyFileMapping(const ReadOnlyFileMapping&) = delete;
    ReadOnlyFileMapping& operator=(const ReadOnlyFileMapping&) = delete;

    ~ReadOnlyFileMapping() {
        Reset();
    }

    bool Open(const std::wstring& path) {
        Reset();
        file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) return false;

        LARGE_INTEGER fileSize = {};
        if (!GetFileSizeEx(file, &fileSize) || fileSize.QuadPart <= 0) {
            Reset();
            return false;
        }
        size = static_cast<uint64_t>(fileSize.QuadPart);
        mapping = CreateFileMappingW(file, nullptr, PAGE_READONLY,
                                     static_cast<DWORD>(size >> 32),
                                     static_cast<DWORD>(size), nullptr);
        if (!mapping) {
            Reset();
            return false;
        }
        data = static_cast<const unsigned char*>(MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0));
        if (!data) {
            Reset();
            return false;
        }
        return true;
    }

    const unsigned char* Data() const { return data; }
    uint64_t Size() const { return size; }

private:
    void Reset() {
        if (data) {
            UnmapViewOfFile(data);
            data = nullptr;
        }
        if (mapping) {
            CloseHandle(mapping);
            mapping = nullptr;
        }
        if (file != INVALID_HANDLE_VALUE) {
            CloseHandle(file);
            file = INVALID_HANDLE_VALUE;
        }
        size = 0;
    }

    HANDLE file = INVALID_HANDLE_VALUE;
    HANDLE mapping = nullptr;
    const unsigned char* data = nullptr;
    uint64_t size = 0;
};

bool ParseMp3FrameHeader(const unsigned char* bytes, uint64_t available,
                         Mp3FrameHeader* header) {
    if (!bytes || !header || available < 4) return false;
    if (bytes[0] != 0xFF || (bytes[1] & 0xE0) != 0xE0) return false;

    const int versionBits = (bytes[1] >> 3) & 0x03;
    const int layerBits = (bytes[1] >> 1) & 0x03;
    const int bitrateIndex = (bytes[2] >> 4) & 0x0F;
    const int sampleRateIndex = (bytes[2] >> 2) & 0x03;
    const int padding = (bytes[2] >> 1) & 0x01;
    const int emphasis = bytes[3] & 0x03;
    if (versionBits == 1 || layerBits != 1 || bitrateIndex == 0 || bitrateIndex == 15 ||
        sampleRateIndex == 3 || emphasis == 2) {
        return false;
    }

    static const uint16_t mpeg1Bitrates[] = {
        0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 0};
    static const uint16_t mpeg2Bitrates[] = {
        0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0};
    static const uint32_t mpeg1Rates[] = {44100, 48000, 32000};
    static const uint32_t mpeg2Rates[] = {22050, 24000, 16000};
    static const uint32_t mpeg25Rates[] = {11025, 12000, 8000};

    const bool mpeg1 = versionBits == 3;
    const uint16_t bitrateKbps =
        (mpeg1 ? mpeg1Bitrates[bitrateIndex] : mpeg2Bitrates[bitrateIndex]);
    const uint32_t sampleRate =
        (versionBits == 3) ? mpeg1Rates[sampleRateIndex]
                           : (versionBits == 2) ? mpeg2Rates[sampleRateIndex]
                                                : mpeg25Rates[sampleRateIndex];
    if (!bitrateKbps || !sampleRate) return false;

    const uint32_t samples = mpeg1 ? 1152 : 576;
    const uint32_t frameBytes = static_cast<uint32_t>(
        ((mpeg1 ? 144u : 72u) * static_cast<uint64_t>(bitrateKbps) * 1000u) /
            sampleRate +
        padding);
    const bool hasCrc = (bytes[1] & 0x01) == 0;
    const int channels = ((bytes[3] >> 6) == 3) ? 1 : 2;
    const uint16_t sideOffset = static_cast<uint16_t>(4 + (hasCrc ? 2 : 0));
    const uint16_t sideBytes = static_cast<uint16_t>(mpeg1 ? (channels == 1 ? 17 : 32)
                                                           : (channels == 1 ? 9 : 17));
    if (frameBytes <= static_cast<uint32_t>(sideOffset + sideBytes) ||
        available < frameBytes) {
        return false;
    }

    header->sampleRate = sampleRate;
    header->frameBytes = frameBytes;
    header->samples = samples;
    header->bitrateKbps = bitrateKbps;
    header->sideOffset = sideOffset;
    header->sideBytes = sideBytes;
    header->channels = channels;
    return true;
}

bool ParseMp3Frame(const unsigned char* data, uint64_t size, uint64_t offset,
                   Mp3Frame* frame, Mp3FrameHeader* header) {
    Mp3FrameHeader parsed = {};
    if (!ParseMp3FrameHeader(data + offset, size - offset, &parsed)) return false;
    const unsigned char* side = data + offset + parsed.sideOffset;
    frame->offset = offset;
    frame->bytes = parsed.frameBytes;
    frame->samples = parsed.samples;
    frame->mainDataBytes =
        parsed.frameBytes - parsed.sideOffset - parsed.sideBytes;
    frame->mainDataBegin = static_cast<uint16_t>((side[0] << 1) | (side[1] >> 7));
    const uint64_t sideEnd = offset + parsed.sideOffset + parsed.sideBytes;
    const auto hasAscii = [data, size](uint64_t at, const char* text) {
        if (at + 4 > size) return false;
        for (int i = 0; i < 4; ++i) {
            if (data[at + static_cast<uint64_t>(i)] !=
                static_cast<unsigned char>(text[i])) {
                return false;
            }
        }
        return true;
    };
    frame->metadataFrame =
        hasAscii(sideEnd, "Xing") || hasAscii(sideEnd, "Info") ||
        hasAscii(offset + 36, "VBRI");
    if (header) *header = parsed;
    return true;
}

bool IndexMp3File(const std::wstring& path, Mp3Index* index, std::wstring* error) {
    if (!index) return false;
    *index = Mp3Index();

    ReadOnlyFileMapping file;
    if (!file.Open(path)) {
        if (error) *error = L"Could not open the MP3 file.";
        return false;
    }

    HSTREAM probe = BASS_StreamCreateFile(
        FALSE, path.c_str(), 0, 0,
        BASS_UNICODE | BASS_STREAM_DECODE | BASS_STREAM_PRESCAN);
    if (!probe) {
        if (error) {
            wchar_t text[128];
            swprintf_s(text, L"BASS could not recognize the MP3 (error %lu).", BASS_ErrorGetCode());
            *error = text;
        }
        return false;
    }
    const QWORD start = BASS_StreamGetFilePosition(probe, BASS_FILEPOS_START);
    const QWORD end = BASS_StreamGetFilePosition(probe, BASS_FILEPOS_END);
    BASS_StreamFree(probe);
    if (start == static_cast<QWORD>(-1) || end == static_cast<QWORD>(-1) ||
        start >= file.Size() || end == 0) {
        if (error) *error = L"Could not determine the MP3 audio data boundaries.";
        return false;
    }

    const uint64_t audioStart = static_cast<uint64_t>(start);
    const uint64_t audioEnd =
        (std::min)(file.Size(), audioStart + static_cast<uint64_t>(end));
    uint64_t firstFrameOffset = 0;
    Mp3FrameHeader firstHeader = {};
    Mp3Frame firstFrame = {};
    bool foundFirst = false;
    for (uint64_t at = audioStart; at + 4 <= audioEnd; ++at) {
        Mp3FrameHeader header = {};
        Mp3Frame frame = {};
        if (!ParseMp3Frame(file.Data(), audioEnd, at, &frame, &header)) continue;
        if (at + frame.bytes < audioEnd) {
            Mp3Frame next = {};
            Mp3FrameHeader nextHeader = {};
            if (!ParseMp3Frame(file.Data(), audioEnd, at + frame.bytes,
                               &next, &nextHeader) ||
                nextHeader.sampleRate != header.sampleRate ||
                nextHeader.channels != header.channels) {
                continue;
            }
        }
        firstFrameOffset = at;
        firstHeader = header;
        firstFrame = frame;
        foundFirst = true;
        break;
    }
    if (!foundFirst) {
        if (error) *error = L"No valid MP3 Layer III frames were found in the file.";
        return false;
    }

    index->sampleRate = firstHeader.sampleRate;
    index->channels = firstHeader.channels;
    index->audioStart = firstFrameOffset;
    index->audioEnd = audioEnd;

    uint64_t at = firstFrameOffset;
    while (at + 4 <= audioEnd) {
        if ((index->frames.size() & 0x3FFu) == 0 &&
            g_waveExportCancel.load(std::memory_order_relaxed)) {
            if (error) error->clear();
            return false;
        }

        Mp3Frame frame = {};
        Mp3FrameHeader header = {};
        if (!ParseMp3Frame(file.Data(), audioEnd, at, &frame, &header) ||
            at + frame.bytes > audioEnd) {
            if (error) *error = L"The MP3 contains a damaged or incomplete frame.";
            return false;
        }
        if (header.sampleRate != index->sampleRate || header.channels != index->channels) {
            if (error) *error = L"Changing format within an MP3 file is not supported.";
            return false;
        }
        frame.sampleStart = index->totalSamples;
        index->totalSamples += frame.samples;
        index->frames.push_back(frame);
        at += frame.bytes;
    }
    if (index->frames.empty() || index->totalSamples == 0) {
        if (error) *error = L"No audio frames were found in the MP3 file.";
        return false;
    }
    return true;
}

float EvaluateMp3Boundary(HSTREAM decoder, int channels, double seconds, bool atEnd) {
    if (!decoder || channels <= 0) return 1.0f;
    BASS_CHANNELINFO info = {};
    if (!BASS_ChannelGetInfo(decoder, &info) || !info.freq) return 1.0f;
    const int frames = 64;
    double readAt = seconds;
    if (atEnd) readAt = (std::max)(0.0, seconds - static_cast<double>(frames) / info.freq);
    const QWORD bytePos = BASS_ChannelSeconds2Bytes(decoder, readAt);
    if (bytePos == static_cast<QWORD>(-1) ||
        !BASS_ChannelSetPosition(decoder, bytePos, BASS_POS_BYTE)) {
        return 1.0f;
    }

    std::vector<float> samples(static_cast<size_t>(frames) * channels);
    const DWORD got = BASS_ChannelGetData(
        decoder, samples.data(),
        static_cast<DWORD>(samples.size() * sizeof(float)));
    if (got == 0 || got == static_cast<DWORD>(-1)) return 1.0f;
    const size_t gotFrames = got / (sizeof(float) * static_cast<size_t>(channels));
    if (!gotFrames) return 1.0f;

    const size_t edgeFrame = atEnd ? gotFrames - 1 : 0;
    const size_t first = atEnd ? (gotFrames > 8 ? gotFrames - 8 : 0) : 0;
    const size_t last = atEnd ? gotFrames : (std::min)(gotFrames, static_cast<size_t>(8));
    float edge = 0.0f;
    float average = 0.0f;
    for (size_t frame = first; frame < last; ++frame) {
        for (int channel = 0; channel < channels; ++channel) {
            const float value = std::fabs(
                samples[frame * static_cast<size_t>(channels) +
                        static_cast<size_t>(channel)]);
            average += value;
            if (frame == edgeFrame && value > edge) edge = value;
        }
    }
    average /= static_cast<float>((last - first) * static_cast<size_t>(channels));
    return edge + average * 0.25f;
}

size_t Mp3ReservoirStart(const Mp3Index& index, size_t frame) {
    uint32_t needed = index.frames[frame].mainDataBegin;
    size_t start = frame;
    while (needed > 0 && start > 0) {
        --start;
        const uint32_t available = index.frames[start].mainDataBytes;
        needed = (needed > available) ? needed - available : 0;
    }
    return start;
}

bool ChooseMp3Range(const Mp3Index& index, double startFraction, double stopFraction,
                    HSTREAM decoder, size_t* startFrame, size_t* endFrame) {
    if (!startFrame || !endFrame || index.frames.empty() ||
        !(startFraction >= 0.0 && stopFraction <= 1.0 &&
          startFraction < stopFraction)) {
        return false;
    }

    const uint64_t startTarget = static_cast<uint64_t>(
        startFraction * static_cast<double>(index.totalSamples));
    uint64_t stopTarget = static_cast<uint64_t>(
        std::ceil(stopFraction * static_cast<double>(index.totalSamples)));
    if (stopTarget <= startTarget) stopTarget = startTarget + 1;

    size_t targetStart = 0;
    while (targetStart + 1 < index.frames.size() &&
           index.frames[targetStart + 1].sampleStart <= startTarget) {
        ++targetStart;
    }

    size_t targetEnd = targetStart;
    while (targetEnd + 1 < index.frames.size() &&
           index.frames[targetEnd + 1].sampleStart < stopTarget) {
        ++targetEnd;
    }

    const size_t searchLimit = (std::min)(targetEnd, targetStart + 8);
    float bestScore = (std::numeric_limits<float>::max)();
    size_t bestStart = targetEnd;
    bool foundStart = false;
    for (size_t candidate = targetStart; candidate <= searchLimit; ++candidate) {
        if (index.frames[candidate].metadataFrame) continue;
        size_t candidateStart = Mp3ReservoirStart(index, candidate);
        if (candidateStart > targetEnd) continue;

        const uint64_t startSample = index.frames[candidateStart].sampleStart;
        const uint64_t distanceSamples =
            (startSample >= startTarget) ? startSample - startTarget
                                         : startTarget - startSample;
        const double distanceFrames =
            static_cast<double>(distanceSamples) /
            static_cast<double>(index.frames[candidateStart].samples);
        const double seconds =
            static_cast<double>(startSample) / index.sampleRate;
        const float score = EvaluateMp3Boundary(decoder, index.channels, seconds, false) +
                            static_cast<float>(distanceFrames) * 0.005f;
        if (score < bestScore) {
            bestScore = score;
            bestStart = candidateStart;
            foundStart = true;
        }
    }
    if (!foundStart) return false;

    size_t bestEnd = targetEnd;
    bestScore = (std::numeric_limits<float>::max)();
    const size_t endBegin =
        (targetEnd > bestStart) ? (targetEnd - 1) : bestStart;
    const size_t endLimit = (std::min)(index.frames.size() - 1, targetEnd + 1);
    for (size_t i = endBegin; i <= endLimit; ++i) {
        if (i < bestStart) continue;
        const Mp3Frame& frame = index.frames[i];
        const double seconds =
            static_cast<double>(frame.sampleStart + frame.samples) / index.sampleRate;
        const int64_t distance = static_cast<int64_t>(i) - static_cast<int64_t>(targetEnd);
        const float score = EvaluateMp3Boundary(decoder, index.channels, seconds, true) +
                            std::fabs(static_cast<float>(distance)) * 0.005f;
        if (score < bestScore) {
            bestScore = score;
            bestEnd = i;
        }
    }
    if (bestEnd < bestStart) return false;

    *startFrame = bestStart;
    *endFrame = bestEnd;
    return true;
}

std::wstring WindowsErrorText(DWORD code) {
    wchar_t* text = nullptr;
    const DWORD length = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, code, 0, reinterpret_cast<LPWSTR>(&text), 0, nullptr);
    std::wstring result = length && text ? std::wstring(text, length) : L"";
    if (text) LocalFree(text);
    while (!result.empty() &&
           (result.back() == L'\r' || result.back() == L'\n' || result.back() == L' ')) {
        result.pop_back();
    }
    return result;
}

bool WriteAll(HANDLE file, const void* data, DWORD bytes) {
    const unsigned char* cursor = static_cast<const unsigned char*>(data);
    while (bytes) {
        DWORD written = 0;
        if (!WriteFile(file, cursor, bytes, &written, nullptr) || written == 0) return false;
        cursor += written;
        bytes -= written;
    }
    return true;
}

std::wstring FullPathOf(const std::wstring& path) {
    const DWORD needed = GetFullPathNameW(path.c_str(), 0, nullptr, nullptr);
    if (!needed) return path;
    std::vector<wchar_t> buffer(static_cast<size_t>(needed) + 1, L'\0');
    if (!GetFullPathNameW(path.c_str(), static_cast<DWORD>(buffer.size()),
                          buffer.data(), nullptr)) {
        return path;
    }
    return buffer.data();
}

Mp3ExportResult ExportMp3Range(const std::wstring& source,
                               const std::wstring& target,
                               double startFraction,
                               double stopFraction) {
    Mp3ExportResult result;
    result.outputPath = target;
    result.startFraction = startFraction;
    result.stopFraction = stopFraction;

    if (!HasExtension(source, L".mp3")) {
        result.error = L"MP3 export without re-encoding supports MP3 files only.";
        return result;
    }
    if (_wcsicmp(FullPathOf(source).c_str(), FullPathOf(target).c_str()) == 0) {
        result.error = L"The source MP3 cannot be overwritten.";
        return result;
    }

    Mp3Index index;
    std::wstring indexError;
    if (!IndexMp3File(source, &index, &indexError)) {
        result.canceled = g_waveExportCancel.load(std::memory_order_relaxed);
        result.error = result.canceled ? L"" : indexError;
        return result;
    }

    HSTREAM decoder = BASS_StreamCreateFile(
        FALSE, source.c_str(), 0, 0,
        BASS_UNICODE | BASS_SAMPLE_FLOAT | BASS_STREAM_DECODE | BASS_STREAM_PRESCAN);
    size_t startFrame = 0;
    size_t endFrame = 0;
    const bool rangeOk =
        decoder && ChooseMp3Range(index, startFraction, stopFraction, decoder,
                                  &startFrame, &endFrame);
    if (decoder) BASS_StreamFree(decoder);
    if (!rangeOk) {
        result.error = L"Could not select safe MP3 frame boundaries.";
        return result;
    }

    const Mp3Frame& first = index.frames[startFrame];
    const Mp3Frame& last = index.frames[endFrame];
    const uint64_t copyStart = first.offset;
    const uint64_t copyStop = last.offset + last.bytes;
    if (copyStop <= copyStart || copyStop > index.audioEnd) {
        result.error = L"Invalid MP3 frame range.";
        return result;
    }

    result.startFraction =
        static_cast<double>(first.sampleStart) / static_cast<double>(index.totalSamples);
    result.stopFraction =
        static_cast<double>(last.sampleStart + last.samples) /
        static_cast<double>(index.totalSamples);
    result.duration =
        static_cast<double>(last.sampleStart + last.samples - first.sampleStart) /
        index.sampleRate;

    wchar_t suffix[96];
    swprintf_s(suffix, L".tmp.%lu.%llu", GetCurrentProcessId(),
               static_cast<unsigned long long>(GetTickCount64()));
    std::wstring temporary = target + suffix;
    HANDLE output = INVALID_HANDLE_VALUE;
    for (int attempt = 0; attempt < 8 && output == INVALID_HANDLE_VALUE; ++attempt) {
        if (attempt) {
            swprintf_s(suffix, L".tmp.%lu.%llu.%d", GetCurrentProcessId(),
                       static_cast<unsigned long long>(GetTickCount64()), attempt);
            temporary = target + suffix;
        }
        output = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                             FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    }
    if (output == INVALID_HANDLE_VALUE) {
        result.error = L"Could not create a temporary file next to the destination.";
        return result;
    }

    ReadOnlyFileMapping sourceFile;
    if (!sourceFile.Open(source)) {
        CloseHandle(output);
        DeleteFileW(temporary.c_str());
        result.error = L"Could not reopen the source MP3.";
        return result;
    }

    bool writeOk = true;
    uint64_t at = copyStart;
    while (at < copyStop) {
        if (g_waveExportCancel.load(std::memory_order_relaxed)) {
            result.canceled = true;
            writeOk = false;
            break;
        }
        const DWORD chunk = static_cast<DWORD>(
            (std::min)(copyStop - at, static_cast<uint64_t>(1024 * 1024)));
        if (!WriteAll(output, sourceFile.Data() + at, chunk)) {
            writeOk = false;
            result.error = L"MP3 write error: " + WindowsErrorText(GetLastError());
            break;
        }
        at += chunk;
    }

    if (writeOk && !FlushFileBuffers(output)) {
        writeOk = false;
        result.error = L"Could not flush MP3 data to disk: " +
                       WindowsErrorText(GetLastError());
    }
    CloseHandle(output);
    if (!writeOk) {
        DeleteFileW(temporary.c_str());
        if (result.canceled) result.error.clear();
        return result;
    }

    HSTREAM verify = BASS_StreamCreateFile(
        FALSE, temporary.c_str(), 0, 0,
        BASS_UNICODE | BASS_STREAM_DECODE | BASS_STREAM_PRESCAN);
    if (!verify) {
        DeleteFileW(temporary.c_str());
        wchar_t text[160];
        swprintf_s(text, L"The exported MP3 cannot be opened by BASS (error %lu).",
                   BASS_ErrorGetCode());
        result.error = text;
        return result;
    }
    BASS_StreamFree(verify);

    if (!MoveFileExW(temporary.c_str(), target.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(temporary.c_str());
        result.error = L"Could not replace the destination MP3: " +
                       WindowsErrorText(GetLastError());
        return result;
    }
    result.ok = true;
    return result;
}

Mp3ExportResult g_waveExportResult;

RECT WaveAreaRect(HWND hwnd) {
    RECT rc = {};
    GetClientRect(hwnd, &rc);
    rc.left += kWaveMargin;
    rc.right -= kWaveMargin;
    rc.top += kCaptionH + kWaveMargin;
    rc.bottom -= kWaveMargin + kWaveBarH + kWaveGap + kWaveScrollH + kWaveGap;
    return rc;
}

RECT WaveScrollRect(HWND hwnd) {
    RECT rc = {};
    GetClientRect(hwnd, &rc);
    const int bottom = rc.bottom - kWaveMargin - kWaveBarH - kWaveGap;
    return {rc.left + kWaveMargin, bottom - kWaveScrollH, rc.right - kWaveMargin, bottom};
}

RECT WaveBarRect(HWND hwnd) {
    RECT rc = {};
    GetClientRect(hwnd, &rc);
    const int bottom = rc.bottom - kWaveMargin;
    return {rc.left + kWaveMargin, bottom - kWaveBarH, rc.right - kWaveMargin, bottom};
}

double WaveFracAt(const RECT& area, int x) {
    const double w = (area.right > area.left) ? (area.right - area.left) : 1.0;
    double frac = g_waveViewStart + (static_cast<double>(x - area.left) / w) * WaveViewSpan();
    if (frac < 0.0) frac = 0.0;
    if (frac > 1.0) frac = 1.0;
    return frac;
}

int WaveXOfFrac(const RECT& area, double frac) {
    const double w = (area.right > area.left) ? (area.right - area.left) : 1.0;
    return area.left + static_cast<int>((frac - g_waveViewStart) / WaveViewSpan() * w + 0.5);
}

bool WaveFracVisible(double frac) {
    const double span = WaveViewSpan();
    return frac >= g_waveViewStart && frac <= g_waveViewStart + span;
}

RECT WaveThumbRect(HWND hwnd) {
    const RECT track = WaveScrollRect(hwnd);
    const double w = (track.right > track.left) ? (track.right - track.left) : 1.0;
    RECT thumb = track;
    thumb.left = track.left + static_cast<int>(g_waveViewStart * w);
    thumb.right = track.left + static_cast<int>((g_waveViewStart + WaveViewSpan()) * w);

    const int minThumb = S(20);
    if (thumb.right - thumb.left < minThumb) thumb.right = thumb.left + minThumb;
    if (thumb.right > track.right) {
        thumb.right = track.right;
        thumb.left = track.right - minThumb;
    }
    return thumb;
}

void WaveScrollTo(HWND hwnd, int x) {
    if (WaveViewSpan() >= 1.0) return;
    const RECT track = WaveScrollRect(hwnd);
    const double w = (track.right > track.left) ? (track.right - track.left) : 1.0;
    g_waveViewStart = (static_cast<double>(x - track.left) / w) - g_waveScrollGrab;
    WaveClampView();
}

void InvalidateWaveStrip(HWND hwnd, const RECT& area, double frac) {
    const int x = WaveXOfFrac(area, frac);
    RECT strip = {x - 2, area.top, x + 3, area.bottom + 1};
    if (strip.left < area.left) strip.left = area.left;
    if (strip.right > area.right) strip.right = area.right;
    if (strip.left >= strip.right) return;
    InvalidateRect(hwnd, &strip, FALSE);
}

double WaveChannelFraction(DWORD chan) {
    if (!chan) return -1.0;
    const QWORD len = BASS_ChannelGetLength(chan, BASS_POS_BYTE);
    if (!len || len == static_cast<QWORD>(-1)) return -1.0;
    const QWORD pos = ChannelPosSafe(chan, len);
    if (pos == static_cast<QWORD>(-1)) return -1.0;
    double frac = static_cast<double>(pos) / static_cast<double>(len);
    if (frac < 0.0) frac = 0.0;
    if (frac > 1.0) frac = 1.0;
    return frac;
}

void WaveSetPlayhead(HWND hwnd, double frac) {
    if (frac < 0.0) frac = -1.0;
    if (frac > 1.0) frac = 1.0;
    if (frac == g_wavePlayhead) return;

    const RECT area = WaveAreaRect(hwnd);
    const double old = g_wavePlayhead;
    g_wavePlayhead = frac;
    if (old >= 0.0 && WaveFracVisible(old)) InvalidateWaveStrip(hwnd, area, old);
    if (frac >= 0.0 && WaveFracVisible(frac)) InvalidateWaveStrip(hwnd, area, frac);
}

bool WaveSeekAudition(HWND hwnd, double frac) {
    if (!g_waveAudition || !WaveAuditionPlaying()) return false;

    if (g_waveAuditionKind == WaveAuditionKind::MarkerRange) {
        const double start = g_waveAuditionStart >= 0.0
            ? g_waveAuditionStart : WaveMarkerStart();
        const double end = g_waveAuditionEnd >= 0.0
            ? g_waveAuditionEnd : WaveMarkerStop();
        if (frac <= start) frac = start;
        if (frac >= end) {
            ChannelStop(g_waveAudition);
            g_waveCursor = end;
            WaveSetPlayhead(hwnd, end);
            ApplyKeepAwake();
            return true;
        }
        return WaveStartAudition(
            hwnd, WaveAuditionKind::MarkerRange, frac, end, false, false);
    } else if (frac >= 1.0) {
        ChannelStop(g_waveAudition);
        g_waveCursor = 1.0;
        WaveSetPlayhead(hwnd, 1.0);
        ApplyKeepAwake();
        return true;
    }

    if (frac < 0.0) frac = 0.0;
    if (frac > 1.0) frac = 1.0;
    const QWORD len = BASS_ChannelGetLength(g_waveAudition, BASS_POS_BYTE);
    if (!len || len == static_cast<QWORD>(-1)) return false;
    QWORD pos = static_cast<QWORD>(frac * static_cast<double>(len));
    if (pos >= len) pos = len - 1;
    ChannelSeek(g_waveAudition, pos);
    g_waveCursor = frac;
    WaveSetPlayhead(hwnd, frac);
    return true;
}

void WaveStopAudition(HWND hwnd) {
    if (!g_waveAudition) return;
    double cursor = g_waveAuditionStart;
    if (cursor < 0.0) cursor = WaveChannelFraction(g_waveAudition);
    ChannelStop(g_waveAudition);
    if (cursor >= 0.0) {
        g_waveCursor = cursor;
        WaveSetPlayhead(hwnd, cursor);
    }
    g_waveAuditionEnd = -1.0;
    g_waveAuditionKind = WaveAuditionKind::None;
    g_waveBtnPlaying = FALSE;
    InvalidateWaveAuditionButtons();
    ApplyKeepAwake();
}

bool WaveStartAuditionChannel(QWORD lengthBytes) {
    if (lengthBytes && g_mixerMode && g_outputMixer) {
        return BASS_Mixer_StreamAddChannelEx(
            g_outputMixer, g_waveAudition, kMixerChanFlags, 0, lengthBytes);
    }
    return ChannelStart(g_waveAudition, FALSE);
}

bool WaveStartAudition(HWND hwnd, WaveAuditionKind kind,
                       double start, double stop, bool showError,
                       bool rememberStart = true) {
    if (kind == WaveAuditionKind::None) return false;

    if (!g_waveAudition) {
        const std::wstring source = WaveAuditionSource();
        if (source.empty()) return false;
        const DWORD modeFlags = g_mixerMode
            ? (BASS_SAMPLE_FLOAT | BASS_STREAM_DECODE) : 0;
        g_waveAudition = BASS_StreamCreateFile(
            FALSE, source.c_str(), 0, 0, BASS_UNICODE | modeFlags);
        g_waveAuditionIsMusic = FALSE;
        if (!g_waveAudition) {
            g_waveAudition = BASS_MusicLoad(
                0, source.c_str(), 0, 0,
                BASS_MUSIC_RAMP | BASS_UNICODE | modeFlags, 0);
            g_waveAuditionIsMusic = g_waveAudition ? TRUE : FALSE;
        }
        if (!g_waveAudition) {
            if (showError) {
                wchar_t text[400];
                swprintf_s(text, L"Could not open the file for preview.\n\n"
                                  L"BASS error code: %lu",
                           BASS_ErrorGetCode());
                MessageBoxW(hwnd, text, L"Preview", MB_OK | MB_ICONERROR);
            }
            return false;
        }
    }

    const QWORD len = BASS_ChannelGetLength(g_waveAudition, BASS_POS_BYTE);
    if (!len || len == static_cast<QWORD>(-1)) {
        WaveFreeAudition();
        if (showError)
            MessageBoxW(hwnd, L"Could not determine the preview file length.",
                        L"Preview", MB_OK | MB_ICONERROR);
        return false;
    }

    if (start < 0.0) start = 0.0;
    if (start > 1.0) start = 1.0;
    if (stop < 0.0) stop = 0.0;
    if (stop > 1.0) stop = 1.0;

    if (kind == WaveAuditionKind::Cursor) {
        stop = 1.0;
        if (start >= 1.0) start = 0.0;
    } else {
        if (start >= 1.0 || start >= stop) {
            if (!rememberStart && g_waveAuditionStart >= 0.0 &&
                g_waveAuditionStart < stop) {
                start = g_waveAuditionStart;
            } else {
                start = WaveMarkerStart();
            }
        }
        if (start < 0.0 || start >= stop) return false;
    }

    const QWORD startBytes = static_cast<QWORD>(start * static_cast<double>(len));
    const QWORD stopBytes = kind == WaveAuditionKind::MarkerRange
        ? static_cast<QWORD>(stop * static_cast<double>(len)) : 0;
    const QWORD audibleBytes = stopBytes > startBytes
        ? stopBytes - startBytes : 0;
    QWORD playBytes = audibleBytes;
    if (playBytes) {
        const QWORD guardBytes =
            BASS_ChannelSeconds2Bytes(g_waveAudition,
                                      kWaveAuditionTailGuardMs / 1000.0);
        if (guardBytes != static_cast<QWORD>(-1) &&
            playBytes <= static_cast<QWORD>(-1) - guardBytes)
            playBytes += guardBytes;
        if (playBytes > len - startBytes) playBytes = len - startBytes;
    }
    ChannelStop(g_waveAudition);
    ChannelSeek(g_waveAudition, startBytes);
    if (g_mixerMode &&
        !WaveSetAuditionStopEnvelope(audibleBytes ? stopBytes : 0)) {
        playBytes = audibleBytes;
    }
    BASS_ChannelSetAttribute(g_waveAudition, BASS_ATTRIB_VOL,
                             WaveAuditionVolume());
    BOOL started = WaveStartAuditionChannel(playBytes);
    if (!started) {
        const DWORD bassError = BASS_ErrorGetCode();
        WaveFreeAudition();
        if (showError) {
            wchar_t text[400];
            swprintf_s(text, L"Could not start preview playback.\n\n"
                              L"BASS error code: %lu",
                       bassError);
            MessageBoxW(hwnd, text, L"Preview", MB_OK | MB_ICONERROR);
        }
        return false;
    }

    if (rememberStart || g_waveAuditionStart < 0.0)
        g_waveAuditionStart = start;
    g_waveAuditionEnd = stop;
    g_waveAuditionKind = kind;
    g_waveCursor = start;
    g_waveBtnPlaying = TRUE;
    WaveSetPlayhead(hwnd, start);
    InvalidateWaveAuditionButtons();
    ApplyKeepAwake();
    return true;
}

void WaveToggleAudition(HWND hwnd, WaveAuditionKind kind) {
    if (g_waveAuditionKind == kind && WaveAuditionPlaying()) {
        WaveStopAudition(hwnd);
        return;
    }

    if (kind == WaveAuditionKind::MarkerRange) {
        const double start = WaveMarkerStart();
        const double stop = WaveMarkerStop();
        if (start < 0.0 || stop <= start) return;
        WaveStartAudition(hwnd, kind, start, stop, true);
        return;
    }

    double start = g_waveCursor;
    if (start < 0.0 || start >= 1.0) start = 0.0;
    WaveStartAudition(hwnd, WaveAuditionKind::Cursor, start, 1.0, true);
}

void RequestWaveFrame(HWND hwnd, const RECT* area) {
    const ULONGLONG now = GetTickCount64();
    if (now - g_waveFrameTick >= kWaveDragFrameMs) {
        g_waveFrameTick = now;
        KillTimer(hwnd, ID_TIMER_WAVE_FRAME);
        InvalidateRect(hwnd, area, FALSE);
        return;
    }
    SetTimer(hwnd, ID_TIMER_WAVE_FRAME, kWaveDragFrameMs, nullptr);
}

RECT WaveDirtyRect(HWND hwnd) {
    RECT r = WaveAreaRect(hwnd);
    const RECT track = WaveScrollRect(hwnd);
    if (track.left < r.left) r.left = track.left;
    if (track.right > r.right) r.right = track.right;
    if (track.top < r.top) r.top = track.top;
    if (track.bottom > r.bottom) r.bottom = track.bottom;
    return r;
}

void WaveUpdatePlayhead(HWND hwnd) {
    const BOOL wasPlaying = g_waveBtnPlaying;
    BOOL playing = WaveAuditionPlaying() ? TRUE : FALSE;
    double frac = g_waveCursor;
    const WaveAuditionKind kind = g_waveAuditionKind;
    const double stop =
        kind == WaveAuditionKind::MarkerRange && g_waveAuditionEnd >= 0.0
            ? g_waveAuditionEnd : 1.0;
    if (playing) {
        const double pos = WaveChannelFraction(g_waveAudition);
        if (pos >= 0.0) {
            if (kind == WaveAuditionKind::MarkerRange && pos >= stop) {
                ChannelStop(g_waveAudition);
                g_waveCursor = stop;
                frac = stop;
                playing = FALSE;
                ApplyKeepAwake();
            } else {
                g_waveCursor = pos;
                frac = pos;
            }
        }
    }
    if (wasPlaying && !playing) {
        frac = kind == WaveAuditionKind::MarkerRange ? stop : 1.0;
        g_waveCursor = frac;
        g_waveAuditionEnd = -1.0;
        g_waveAuditionKind = WaveAuditionKind::None;
        ApplyKeepAwake();
    }
    if (playing != g_waveBtnPlaying) {
        g_waveBtnPlaying = playing;
        InvalidateWaveAuditionButtons();
    }
    WaveSetPlayhead(hwnd, frac);
}

RECT WaveMarkerFlagRect(const RECT& area, int x) {
    if (x < area.left) x = area.left;
    if (x > area.right - 2) x = area.right - 2;
    return {x - S(8), area.top + S(2), x + S(10), area.top + S(20)};
}

int WaveMarkerAt(HWND hwnd, int x, int y) {
    if (!WaveInExportMode() && (g_wavePad < 0 || g_wavePad >= kPadCount)) return 0;
    const RECT area = WaveAreaRect(hwnd);
    const POINT pt = {x, y};
    int marker = 0;
    int bestDistance = 0;

    if (WaveFracVisible(WaveMarkerStart())) {
        const RECT flag = WaveMarkerFlagRect(area, WaveXOfFrac(area, WaveMarkerStart()));
        if (PtInRect(&flag, pt)) {
            marker = 1;
            bestDistance = abs(x - (flag.left + flag.right) / 2);
        }
    }
    if (WaveFracVisible(WaveMarkerStop())) {
        const RECT flag = WaveMarkerFlagRect(area, WaveXOfFrac(area, WaveMarkerStop()));
        if (PtInRect(&flag, pt)) {
            const int distance = abs(x - (flag.left + flag.right) / 2);
            if (!marker || distance < bestDistance) marker = 2;
        }
    }
    return marker;
}

void WaveDragMarker(HWND hwnd, int x) {
    if (!g_waveDrag ||
        (!WaveInExportMode() && (g_wavePad < 0 || g_wavePad >= kPadCount))) {
        return;
    }
    const RECT area = WaveAreaRect(hwnd);
    const double w = (area.right > area.left) ? (area.right - area.left) : 1.0;
    const double gap = (std::max)(2, S(4)) * WaveViewSpan() / w;

    const double frac = WaveFracAt(area, x);
    if (g_waveDrag == 1) SetWaveMarkerStart((std::min)(frac, WaveMarkerStop() - gap));
    else                 SetWaveMarkerStop((std::max)(frac, WaveMarkerStart() + gap));
    if (WaveMarkerStart() < 0.0) SetWaveMarkerStart(0.0);
    if (WaveMarkerStop() > 1.0) SetWaveMarkerStop(1.0);
}

void SetWaveMarkerFromContext(HWND hwnd, double frac, bool startMarker) {
    if (!WaveInExportMode() && (g_wavePad < 0 || g_wavePad >= kPadCount)) return;
    const RECT area = WaveAreaRect(hwnd);
    const double w = (area.right > area.left) ? (area.right - area.left) : 1.0;
    const double gap = (std::max)(2, S(4)) * WaveViewSpan() / w;

    if (startMarker) {
        if (frac >= WaveMarkerStop()) {
            SetWaveMarkerStop(1.0);
            if (frac > 1.0 - gap) frac = 1.0 - gap;
        }
        SetWaveMarkerStart((std::max)(0.0, frac));
    } else {
        if (frac <= WaveMarkerStart()) {
            SetWaveMarkerStart(0.0);
            if (frac < gap) frac = gap;
        }
        SetWaveMarkerStop((std::min)(frac, 1.0));
    }

    if (!WaveInExportMode()) SaveSampler();
    InvalidateRect(hwnd, nullptr, FALSE);
}

void ShowWaveMarkerMenu(HWND hwnd, int x, POINT screenPt) {
    if (!WaveInExportMode() && (g_wavePad < 0 || g_wavePad >= kPadCount)) return;
    const RECT area = WaveAreaRect(hwnd);
    const double frac = WaveFracAt(area, x);

    std::wstring itemA = L"Set A here";
    std::wstring itemB = L"Set B here";
    HMENU menu = CreatePopupMenu();
    if (!menu) return;

    MENUINFO menuInfo = {};
    menuInfo.cbSize = sizeof(menuInfo);
    menuInfo.fMask = MIM_BACKGROUND;
    menuInfo.hbrBack = SolidBrush(kPlaylistMenuBack);
    SetMenuInfo(menu, &menuInfo);
    AppendPlaylistMenuItem(menu, ID_WAVE_SET_A, itemA, true);
    AppendPlaylistMenuItem(menu, ID_WAVE_SET_B, itemB, true);

    SetForegroundWindow(hwnd);
    g_waveMarkerMenuOpen = TRUE;
    const int cmd = static_cast<int>(TrackPopupMenu(
        menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY,
        screenPt.x, screenPt.y, 0, hwnd, nullptr));
    g_waveMarkerMenuOpen = FALSE;
    DestroyMenu(menu);

    if (cmd == ID_WAVE_SET_A) SetWaveMarkerFromContext(hwnd, frac, true);
    else if (cmd == ID_WAVE_SET_B) SetWaveMarkerFromContext(hwnd, frac, false);
}

void DrawWaveMarker(HDC dc, const RECT& area, int x, COLORREF color, const wchar_t* letter) {
    if (x < area.left) x = area.left;
    if (x > area.right - 2) x = area.right - 2;

    const RECT line = {x, area.top, x + (std::max)(1, S(2)), area.bottom};
    FillSolidRect(dc, line, color);

    const RECT flag = WaveMarkerFlagRect(area, x);
    FillSolidRect(dc, flag, color);
    FrameSolidRect(dc, flag, kWaveArea);

    HFONT oldFont = static_cast<HFONT>(SelectObject(dc, UiFont()));
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, kWaveArea);
    RECT textRc = flag;
    DrawTextW(dc, letter, -1, &textRc, DT_SINGLELINE | DT_VCENTER | DT_CENTER | DT_NOPREFIX);
    SelectObject(dc, oldFont);
}

void DrawWaveScroll(HWND hwnd, HDC dc) {
    const RECT track = WaveScrollRect(hwnd);
    if (track.right <= track.left || track.bottom <= track.top) return;

    FillSolidRect(dc, track, kSmpGroove);
    FrameSolidRect(dc, track, kSmpGrooveEdge);

    if (WaveViewSpan() >= 1.0) return;
    const RECT thumb = WaveThumbRect(hwnd);
    FillSolidRect(dc, thumb, kSmpHandle);
    FrameSolidRect(dc, thumb, kSmpHandleEdge);
}

RECT WaveSaveButtonRect(HWND hwnd) {
    const RECT bar = WaveBarRect(hwnd);
    const int left =
        bar.left + kWaveButtonW * 2 + kWaveGap + kWaveExportGap;
    return {left, bar.top, left + kWaveButtonW, bar.bottom};
}

RECT WaveInfoRect(HWND hwnd) {
    const RECT save = WaveSaveButtonRect(hwnd);
    const RECT bar = WaveBarRect(hwnd);
    const int left = save.right + kWaveGap;
    const int right = bar.right > left ? bar.right : left;
    return {left, bar.top, right, bar.bottom};
}

std::wstring WaveExportInfoText() {
    const double sourceDuration =
        g_wave.sampleRate > 0
            ? static_cast<double>(g_wave.total) / g_wave.sampleRate : 0.0;
    double rangeDuration =
        (WaveMarkerStop() - WaveMarkerStart()) * sourceDuration;
    if (!(rangeDuration > 0.0)) rangeDuration = 0.0;
    const int totalSeconds = static_cast<int>(rangeDuration + 0.5);
    const int minutes = totalSeconds / 60;
    const int seconds = totalSeconds % 60;

    wchar_t bitrate[32] = L"-";
    if (g_wave.bitrateKbps > 0) {
        swprintf_s(bitrate, L"%d kbps", g_wave.bitrateKbps);
    }
    wchar_t frequency[32] = L"-";
    if (g_wave.sampleRate > 0) {
        swprintf_s(frequency, L"%d Hz", g_wave.sampleRate);
    }

    wchar_t text[192];
    swprintf_s(text, L"Bitrate: %s   Sample rate: %s   Length: %d:%02d",
               bitrate, frequency, minutes, seconds);
    return text;
}

void DrawWaveInfo(HDC dc, HWND hwnd) {
    if (!WaveInExportMode()) return;
    const RECT rc = WaveInfoRect(hwnd);
    if (rc.right - rc.left < S(40)) return;

    FillSolidRect(dc, rc, kSmpGroove);
    FrameSolidRect(dc, rc, kSmpGrooveEdge);

    RECT textRc = rc;
    textRc.left += S(10);
    textRc.right -= S(10);
    HFONT oldFont = static_cast<HFONT>(SelectObject(dc, UiFont()));
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, kSmpText);
    const std::wstring text = WaveExportInfoText();
    DrawTextW(dc, text.c_str(), -1, &textRc,
              DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_NOPREFIX |
              DT_END_ELLIPSIS);
    SelectObject(dc, oldFont);
}

void DrawWaveWindow(HWND hwnd, HDC dc) {
    RECT rc = {};
    GetClientRect(hwnd, &rc);

    const RECT area = WaveAreaRect(hwnd);
    if (area.right <= area.left || area.bottom <= area.top) {
        FillSolidRect(dc, rc, kSmpBack);
        return;
    }

    const RECT topBand    = {rc.left, rc.top, rc.right, area.top};
    const RECT bottomBand = {rc.left, area.bottom, rc.right, rc.bottom};
    const RECT leftBand   = {rc.left, area.top, area.left, area.bottom};
    const RECT rightBand  = {area.right, area.top, rc.right, area.bottom};
    FillSolidRect(dc, topBand, kSmpBack);
    FillSolidRect(dc, bottomBand, kSmpBack);
    FillSolidRect(dc, leftBand, kSmpBack);
    FillSolidRect(dc, rightBand, kSmpBack);

    FillSolidRect(dc, area, kWaveArea);
    FrameSolidRect(dc, area, kSmpGrooveEdge);

    const int mid = (area.top + area.bottom) / 2;

    const int half = (area.bottom - area.top) / 2 - S(3);

    const float scale = (g_wave.peak > 1.0f) ? (1.0f / g_wave.peak) : 1.0f;
    const RECT zero = {area.left, mid, area.right, mid + 1};
    FillSolidRect(dc, zero, kWaveZero);

    const int areaW = area.right - area.left;
    const int cols = static_cast<int>(g_wave.lo.size());
    const double span = WaveViewSpan();
    HBRUSH bar = SolidBrush(kWaveLine);

    int runLeft = area.left;
    int runTop = 0;
    int runBottom = 0;
    bool runOpen = false;
    int px = 0;
    for (; px < areaW && cols > 0; ++px) {
        const double left = g_waveViewStart + (static_cast<double>(px) / areaW) * span;
        const double right = g_waveViewStart + (static_cast<double>(px + 1) / areaW) * span;
        size_t from = static_cast<size_t>(left * cols);
        size_t to = static_cast<size_t>(right * cols);
        if (to <= from) to = from + 1;
        if (to > static_cast<size_t>(cols)) to = static_cast<size_t>(cols);
        if (from >= static_cast<size_t>(cols)) break;
        float lo = 0.0f, hi = 0.0f;
        for (size_t k = from; k < to; ++k) {
            if (g_wave.lo[k] < lo) lo = g_wave.lo[k];
            if (g_wave.hi[k] > hi) hi = g_wave.hi[k];
        }
        int top = mid - static_cast<int>(hi * half * scale);
        int bottom = mid - static_cast<int>(lo * half * scale) + 1;
        if (top < area.top) top = area.top;
        if (bottom > area.bottom) bottom = area.bottom;
        if (runOpen && top == runTop && bottom == runBottom) continue;
        if (runOpen) {
            const RECT column = {runLeft, runTop, area.left + px, runBottom};
            FillRect(dc, &column, bar);
        }
        runLeft = area.left + px;
        runTop = top;
        runBottom = bottom;
        runOpen = true;
    }
    if (runOpen) {
        const RECT column = {runLeft, runTop, area.left + px, runBottom};
        FillRect(dc, &column, bar);
    }

    if (g_wavePlayhead >= 0.0 && WaveFracVisible(g_wavePlayhead)) {
        const int x = WaveXOfFrac(area, g_wavePlayhead);
        const RECT line = {x, area.top, x + (std::max)(1, S(2)), area.bottom};
        FillSolidRect(dc, line, kWavePlayhead);
    }

    if (WaveInExportMode() || (g_wavePad >= 0 && g_wavePad < kPadCount)) {
        if (WaveFracVisible(WaveMarkerStart()))
            DrawWaveMarker(dc, area, WaveXOfFrac(area, WaveMarkerStart()), kWaveMarkA, L"A");
        if (WaveFracVisible(WaveMarkerStop()))
            DrawWaveMarker(dc, area, WaveXOfFrac(area, WaveMarkerStop()), kWaveMarkB, L"B");
    }
    DrawWaveScroll(hwnd, dc);
    DrawWaveInfo(dc, hwnd);

    DrawGrip(dc, rc);
    DrawCustomCaption(hwnd, dc, rc);
}

void LayoutWaveButton(HWND hwnd) {
    const RECT bar = WaveBarRect(hwnd);
    const int height = bar.bottom - bar.top;
    if (g_wavePlayBtn)
        MoveWindow(g_wavePlayBtn, bar.left, bar.top, kWaveButtonW, height, TRUE);
    if (g_waveRangeBtn)
        MoveWindow(g_waveRangeBtn, bar.left + kWaveButtonW + kWaveGap, bar.top,
                   kWaveButtonW, height, TRUE);
    if (g_waveSaveBtn) {
        const RECT save = WaveSaveButtonRect(hwnd);
        MoveWindow(g_waveSaveBtn, save.left, save.top,
                   save.right - save.left, height, TRUE);
    }
}

void ShowWaveModeControls() {
    if (g_wavePlayBtn) ShowWindow(g_wavePlayBtn, SW_SHOW);
    if (g_waveRangeBtn) ShowWindow(g_waveRangeBtn, SW_SHOW);
    if (g_waveSaveBtn)
        ShowWindow(g_waveSaveBtn, WaveInExportMode() ? SW_SHOW : SW_HIDE);
}

void RefreshWaveExportButton() {
    if (g_waveSaveBtn) {
        EnableWindow(g_waveSaveBtn, g_waveExportBusy ? FALSE : TRUE);
        InvalidateRect(g_waveSaveBtn, nullptr, FALSE);
    }
}

void FinishWaveExport(HWND hwnd) {
    if (g_waveExportThread.joinable()) g_waveExportThread.join();
    g_waveExportBusy = FALSE;
    RefreshWaveExportButton();

    const Mp3ExportResult result = g_waveExportResult;
    if (result.ok) {
        g_waveExportStart = result.startFraction;
        g_waveExportStop = result.stopFraction;
        InvalidateRect(hwnd, nullptr, FALSE);
        wchar_t text[700];
        swprintf_s(text, L"MP3 saved without re-encoding.\n\n%s\n\n"
                         L"Duration: %.3f s",
                   result.outputPath.c_str(), result.duration);
        MessageBoxW(hwnd, text, L"Export complete", MB_OK | MB_ICONINFORMATION);
    } else if (!result.canceled && !result.error.empty()) {
        MessageBoxW(hwnd, result.error.c_str(), L"Export error",
                    MB_OK | MB_ICONERROR);
    }
}

void StartWaveExport(HWND hwnd) {
    if (g_waveExportBusy) return;
    std::wstring exportFolder = g_exportDirectory;
    const DWORD folderAttributes = exportFolder.empty()
        ? INVALID_FILE_ATTRIBUTES
        : GetFileAttributesW(exportFolder.c_str());
    if (folderAttributes == INVALID_FILE_ATTRIBUTES ||
        !(folderAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
        exportFolder = DirectoryOf(g_waveExportSource);
    }
    const std::wstring initialName =
        FileStemOf(g_waveExportSource) + L" - excerpt.mp3";
    std::vector<wchar_t> buffer(32 * 1024, L'\0');
    wcsncpy_s(buffer.data(), buffer.size(), initialName.c_str(), _TRUNCATE);

    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = L"MP3 without re-encoding (*.mp3)\0*.mp3\0All files (*.*)\0*.*\0\0";
    ofn.lpstrFile = buffer.data();
    ofn.nMaxFile = static_cast<DWORD>(buffer.size());
    ofn.lpstrInitialDir = exportFolder.c_str();
    ofn.lpstrDefExt = L"mp3";
    ofn.lpstrTitle = L"Save MP3 as";
    ofn.Flags = OFN_EXPLORER | OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT |
                OFN_NOCHANGEDIR | OFN_DONTADDTORECENT;
    if (!GetSaveFileNameW(&ofn)) return;

    std::wstring target = buffer.data();
    if (!HasExtension(target, L".mp3")) target += L".mp3";
    g_exportDirectory = DirectoryOf(target);
    SaveSettings();

    if (g_waveExportThread.joinable()) g_waveExportThread.join();
    g_waveExportResult = Mp3ExportResult();
    g_waveExportCancel.store(false, std::memory_order_relaxed);
    g_waveExportBusy = TRUE;
    RefreshWaveExportButton();

    const std::wstring source = g_waveExportSource;
    const double start = g_waveExportStart;
    const double stop = g_waveExportStop;
    try {
        g_waveExportThread = std::thread([hwnd, source, target, start, stop]() {
            g_waveExportResult = ExportMp3Range(source, target, start, stop);
            PostMessageW(hwnd, WM_APP_EXPORT_DONE, 0, 0);
        });
    } catch (const std::exception&) {
        g_waveExportBusy = FALSE;
        RefreshWaveExportButton();
        MessageBoxW(hwnd, L"Could not start background export.",
                    L"Export error", MB_OK | MB_ICONERROR);
    }
}

LRESULT CALLBACK WaveProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    LRESULT frameResult = 0;
    if (SkipSystemFramePaint(msg, frameResult)) return frameResult;
    switch (msg) {
    case WM_CREATE: {
        DisableSystemWindowFrame(hwnd);
        HINSTANCE inst = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(hwnd, GWLP_HINSTANCE));
        g_wavePlayBtn = MakeButton(hwnd, inst, L"", ID_WAVE_PLAY,
                                   kWaveMargin, 0, kWaveButtonW, kWaveBarH, BS_OWNERDRAW);
        g_waveRangeBtn = MakeButton(hwnd, inst, L"", ID_WAVE_RANGE,
                                    0, 0, kWaveButtonW, kWaveBarH, BS_OWNERDRAW);
        g_waveSaveBtn = MakeButton(hwnd, inst, L"", ID_WAVE_SAVE,
                                   0, 0, kWaveButtonW, kWaveBarH, BS_OWNERDRAW);
        ShowWaveModeControls();
        RefreshWaveExportButton();
        LayoutWaveButton(hwnd);
        return 0;
    }

    case WM_COMMAND: {
        const int id = LOWORD(wParam);
        if (id == ID_WAVE_PLAY) {
            WaveToggleAudition(hwnd, WaveAuditionKind::Cursor);
            WaveUpdatePlayhead(hwnd);
            return 0;
        }
        if (id == ID_WAVE_RANGE) {
            WaveToggleAudition(hwnd, WaveAuditionKind::MarkerRange);
            WaveUpdatePlayhead(hwnd);
            return 0;
        }
        if (id == ID_WAVE_SAVE) {
            StartWaveExport(hwnd);
            return 0;
        }
        break;
    }

    case WM_MEASUREITEM: {
        LPMEASUREITEMSTRUCT mis = reinterpret_cast<LPMEASUREITEMSTRUCT>(lParam);
        if (mis->CtlType == ODT_MENU && g_waveMarkerMenuOpen) {
            MeasurePlaylistMenuItem(mis);
            return TRUE;
        }
        break;
    }

    case WM_DRAWITEM: {
        const LPDRAWITEMSTRUCT dis = reinterpret_cast<LPDRAWITEMSTRUCT>(lParam);
        if (dis->CtlType == ODT_MENU && g_waveMarkerMenuOpen) {
            DrawPlaylistMenuItem(dis);
            return TRUE;
        }
        if (dis->CtlType == ODT_BUTTON && dis->CtlID == ID_WAVE_PLAY) {
            const BOOL playing =
                g_waveAuditionKind == WaveAuditionKind::Cursor &&
                WaveAuditionPlaying();
            DrawTextButton(dis, playing, playing ? L"Stop" : L"Play");
            return TRUE;
        }
        if (dis->CtlType == ODT_BUTTON && dis->CtlID == ID_WAVE_RANGE) {
            const BOOL playing =
                g_waveAuditionKind == WaveAuditionKind::MarkerRange &&
                WaveAuditionPlaying();
            DrawTextButton(dis, playing, playing ? L"Stop" : L"Play A-B");
            return TRUE;
        }
        if (dis->CtlType == ODT_BUTTON && dis->CtlID == ID_WAVE_SAVE) {
            DrawTextButton(dis, FALSE,
                           g_waveExportBusy ? L"Exporting..." : L"Export A-B");
            return TRUE;
        }
        break;
    }

    case WM_TIMER:
        if (wParam == ID_TIMER_WAVE) {
            WaveUpdatePlayhead(hwnd);
            return 0;
        }
        if (wParam == ID_TIMER_WAVE_FRAME) {

            KillTimer(hwnd, ID_TIMER_WAVE_FRAME);
            g_waveFrameTick = GetTickCount64();
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_NCCALCSIZE:
        if (wParam) return 0;
        break;

    case WM_NCHITTEST: {
        const LRESULT hit = CustomCaptionHitTest(hwnd, lParam, true);
        if (hit == HTCLIENT) {
            POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            ScreenToClient(hwnd, &pt);
            RECT rc = {};
            GetClientRect(hwnd, &rc);
            if (pt.x >= rc.right - kGripSize && pt.y >= rc.bottom - kGripSize)
                return HTBOTTOMRIGHT;
        }
        return hit;
    }

    case WM_GETMINMAXINFO: {

        MINMAXINFO* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
        mmi->ptMinTrackSize.x = kWaveWinMinW;
        mmi->ptMinTrackSize.y = kWaveWinMinH;
        return 0;
    }

    case WM_SIZE:
        LayoutWaveButton(hwnd);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;

    case WM_PAINT: {
        PAINTSTRUCT ps = {};
        HDC dc = BeginPaint(hwnd, &ps);

        if (ps.rcPaint.right <= ps.rcPaint.left ||
            ps.rcPaint.bottom <= ps.rcPaint.top) {
            EndPaint(hwnd, &ps);
            return 0;
        }
        RECT rc = {};
        GetClientRect(hwnd, &rc);
        if (rc.right <= 0 || rc.bottom <= 0) {
            EndPaint(hwnd, &ps);
            return 0;
        }

        if (!g_waveBackBmp || g_waveBackW != rc.right || g_waveBackH != rc.bottom) {
            if (g_waveBackBmp) DeleteObject(g_waveBackBmp);
            g_waveBackBmp = CreateCompatibleBitmap(dc, rc.right, rc.bottom);
            g_waveBackW = g_waveBackBmp ? rc.right : 0;
            g_waveBackH = g_waveBackBmp ? rc.bottom : 0;
            if (g_waveBackBmp) ps.rcPaint = rc;
        }
        if (!g_waveBackBmp) {
            DrawWaveWindow(hwnd, dc);
            EndPaint(hwnd, &ps);
            return 0;
        }
        HDC mem = CreateCompatibleDC(dc);
        HGDIOBJ oldBmp = SelectObject(mem, g_waveBackBmp);

        IntersectClipRect(mem, ps.rcPaint.left, ps.rcPaint.top,
                          ps.rcPaint.right, ps.rcPaint.bottom);
        DrawWaveWindow(hwnd, mem);
        BitBlt(dc, ps.rcPaint.left, ps.rcPaint.top,
               ps.rcPaint.right - ps.rcPaint.left, ps.rcPaint.bottom - ps.rcPaint.top,
               mem, ps.rcPaint.left, ps.rcPaint.top, SRCCOPY);
        SelectObject(mem, oldBmp);
        DeleteDC(mem);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_MOUSEWHEEL: {
        const int delta = GET_WHEEL_DELTA_WPARAM(wParam);
        if (!delta) return 0;
        POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        ScreenToClient(hwnd, &pt);
        const RECT area = WaveAreaRect(hwnd);

        if (GET_KEYSTATE_WPARAM(wParam) & MK_SHIFT) {

            const double step = WaveViewSpan() * 0.15 * ((delta > 0) ? -1.0 : 1.0);
            g_waveViewStart += step;
            WaveClampView();
        } else {

            const double focus = WaveFracAt(area, pt.x);
            double k = (area.right > area.left)
                ? static_cast<double>(pt.x - area.left) / (area.right - area.left) : 0.5;
            if (k < 0.0) k = 0.0;
            if (k > 1.0) k = 1.0;
            g_waveZoom *= (delta > 0) ? 1.3 : (1.0 / 1.3);
            WaveClampView();
            g_waveViewStart = focus - k * WaveViewSpan();
            WaveClampView();
        }
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }

    case WM_LBUTTONDOWN: {
        if (HandleCaptionMouseDown(hwnd, lParam)) {
            RefreshRangeButtons();
            return 0;
        }
        const int x = GET_X_LPARAM(lParam);
        const int y = GET_Y_LPARAM(lParam);

        const RECT track = WaveScrollRect(hwnd);
        if (y >= track.top && y < track.bottom && x >= track.left && x < track.right) {
            const RECT thumb = WaveThumbRect(hwnd);
            const double w = (track.right > track.left) ? (track.right - track.left) : 1.0;
            if (x >= thumb.left && x < thumb.right)
                g_waveScrollGrab = (x - thumb.left) / w;
            else
                g_waveScrollGrab = (thumb.right - thumb.left) / w / 2.0;
            g_waveScrollDrag = TRUE;
            SetCapture(hwnd);
            WaveScrollTo(hwnd, x);
            RequestWaveFrame(hwnd, nullptr);
            return 0;
        }

        const RECT area = WaveAreaRect(hwnd);
        if (x >= area.left && x <= area.right &&
            y >= area.top && y <= area.bottom) {
            g_waveDrag = WaveMarkerAt(hwnd, x, y);
            if (!g_waveDrag) {
                const double w = (area.right > area.left)
                    ? (area.right - area.left) : 1.0;
                g_wavePanPending = TRUE;
                g_wavePanStart.x = x;
                g_wavePanStart.y = y;
                g_wavePanGrab = g_waveViewStart +
                                ((x - area.left) / w) * WaveViewSpan();
                SetCapture(hwnd);
                return 0;
            }
            SetCapture(hwnd);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        return 0;
    }

    case WM_RBUTTONUP: {
        const POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        const RECT area = WaveAreaRect(hwnd);
        if (!PtInRect(&area, pt)) return 0;
        POINT screen = pt;
        ClientToScreen(hwnd, &screen);
        ShowWaveMarkerMenu(hwnd, pt.x, screen);
        return 0;
    }

    case WM_MOUSEMOVE:
        if (g_wavePanPending) {
            const int x = GET_X_LPARAM(lParam);
            const int y = GET_Y_LPARAM(lParam);
            if (abs(x - g_wavePanStart.x) >= GetSystemMetrics(SM_CXDRAG) ||
                abs(y - g_wavePanStart.y) >= GetSystemMetrics(SM_CYDRAG)) {
                g_wavePanPending = FALSE;
                if (WaveViewSpan() < 1.0) {
                    g_wavePanDrag = TRUE;
                    SetCursor(LoadCursorW(nullptr, IDC_HAND));
                }
            }
        }
        if (g_waveScrollDrag) {
            const double before = g_waveViewStart;
            WaveScrollTo(hwnd, GET_X_LPARAM(lParam));

            if (g_waveViewStart != before) {
                const RECT dirty = WaveDirtyRect(hwnd);
                RequestWaveFrame(hwnd, &dirty);
            }
        } else if (g_waveDrag) {
            WaveDragMarker(hwnd, GET_X_LPARAM(lParam));
            const RECT area = WaveAreaRect(hwnd);
            RequestWaveFrame(hwnd, &area);
            const RECT info = WaveInfoRect(hwnd);
            InvalidateRect(hwnd, &info, FALSE);
        } else if (g_wavePanDrag) {

            const RECT area = WaveAreaRect(hwnd);
            const double w = (area.right > area.left) ? (area.right - area.left) : 1.0;
            const double before = g_waveViewStart;
            g_waveViewStart = g_wavePanGrab -
                              ((GET_X_LPARAM(lParam) - area.left) / w) * WaveViewSpan();
            WaveClampView();
            if (g_waveViewStart != before) {
                const RECT dirty = WaveDirtyRect(hwnd);
                RequestWaveFrame(hwnd, &dirty);
            }
        }
        return 0;

    case WM_LBUTTONUP:
        if (g_waveScrollDrag) {
            g_waveScrollDrag = FALSE;
            ReleaseCapture();
        }
        if (g_wavePanDrag) {
            g_wavePanDrag = FALSE;
            ReleaseCapture();
        }
        if (g_wavePanPending) {
            g_wavePanPending = FALSE;
            ReleaseCapture();
            const RECT area = WaveAreaRect(hwnd);
            const double frac = WaveFracAt(area, g_wavePanStart.x);
            if (!WaveSeekAudition(hwnd, frac)) {
                g_waveCursor = frac;
                WaveSetPlayhead(hwnd, frac);
            }
        }
        if (g_waveDrag) {
            g_waveDrag = 0;
            ReleaseCapture();
            if (!WaveInExportMode()) SaveSampler();
        }
        return 0;

    case WM_CAPTURECHANGED:
        if (g_waveDrag) {

            g_waveDrag = 0;
            if (!WaveInExportMode()) SaveSampler();
        }
        g_waveScrollDrag = FALSE;
        g_wavePanDrag = FALSE;
        g_wavePanPending = FALSE;
        SetCursor(LoadCursorW(nullptr, IDC_ARROW));
        return 0;

    case WM_KILLFOCUS:
        if (GetCapture() == hwnd) {
            ReleaseCapture();
            SetCursor(LoadCursorW(nullptr, IDC_ARROW));
        }
        return 0;

    case WM_SETCURSOR:
        if (LOWORD(lParam) == HTCLIENT) {
            POINT pt = {};
            GetCursorPos(&pt);
            ScreenToClient(hwnd, &pt);
            const RECT track = WaveScrollRect(hwnd);
            const RECT area = WaveAreaRect(hwnd);
            LPCWSTR cursor = IDC_ARROW;
            if (pt.y >= track.top && pt.y < track.bottom) {
                cursor = IDC_HAND;
            } else if (pt.x >= area.left && pt.x <= area.right &&
                       pt.y >= area.top && pt.y <= area.bottom) {
                if (g_waveDrag || WaveMarkerAt(hwnd, pt.x, pt.y))
                    cursor = IDC_SIZEWE;
                else
                    cursor = IDC_HAND;
            }
            SetCursor(LoadCursorW(nullptr, cursor));
            return TRUE;
        }
        break;

    case WM_SYSCOMMAND:
        if ((wParam & 0xFFF0) == SC_MINIMIZE) {
            SendMessageW(hwnd, WM_CLOSE, 0, 0);
            return 0;
        }
        break;

    case WM_CLOSE:
        ReleaseWindowTreeCapture(hwnd);
        WaveFreeAudition();
        g_waveDrag = 0;
        g_waveScrollDrag = FALSE;
        g_wavePanDrag = FALSE;
        g_wavePanPending = FALSE;
        SetCursor(LoadCursorW(nullptr, IDC_ARROW));
        if (g_waveExportBusy) {
            ShowWindow(hwnd, SW_HIDE);
            return 0;
        }
        if (!WaveInExportMode()) SaveSampler();
        KillTimer(hwnd, ID_TIMER_WAVE);
        KillTimer(hwnd, ID_TIMER_WAVE_FRAME);
        g_waveFrameTick = 0;
        g_wavePlayhead = -1.0;
        ShowWindow(hwnd, SW_HIDE);
        RefreshRangeButtons();
        return 0;

    case WM_DESTROY:
        ReleaseWindowTreeCapture(hwnd);
        g_waveExportCancel.store(true, std::memory_order_relaxed);
        if (g_waveExportThread.joinable()) g_waveExportThread.join();
        WaveFreeAudition();
        g_waveExportBusy = FALSE;
        KillTimer(hwnd, ID_TIMER_WAVE);
        KillTimer(hwnd, ID_TIMER_WAVE_FRAME);
        if (g_waveBackBmp) {
            DeleteObject(g_waveBackBmp);
            g_waveBackBmp = nullptr;
            g_waveBackW = 0;
            g_waveBackH = 0;
        }
        g_wavePlayBtn = nullptr;
        g_waveRangeBtn = nullptr;
        g_waveSaveBtn = nullptr;
        g_waveWnd = nullptr;
        return 0;

    case WM_APP_EXPORT_DONE:
        FinishWaveExport(hwnd);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void ClampWindowSizeToWorkArea(RECT* rc) {
    HMONITOR mon = MonitorFromWindow(g_hwnd ? g_hwnd : GetForegroundWindow(),
                                     MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = {};
    mi.cbSize = sizeof(mi);
    if (!mon || !GetMonitorInfoW(mon, &mi)) return;
    const int workW = mi.rcWork.right - mi.rcWork.left;
    const int workH = mi.rcWork.bottom - mi.rcWork.top;
    int w = rc->right - rc->left;
    int h = rc->bottom - rc->top;
    if (w > workW * 9 / 10) w = workW * 9 / 10;
    if (h > workH * 9 / 10) h = workH * 9 / 10;
    rc->right = rc->left + w;
    rc->bottom = rc->top + h;
}

void OpenWaveWindow(int pad) {
    if (pad < 0 || pad >= kPadCount || g_pads[pad].path.empty()) return;
    WaveFreeAudition();

    if (!LoadWaveView(g_pads[pad].path)) {
        MessageBoxW(g_smpWnd ? g_smpWnd : g_hwnd,
                    L"Could not read the pad file.", L"Error",
                    MB_OK | MB_ICONWARNING);
        return;
    }
    g_waveMode = WaveMode::Sampler;
    g_wavePad = pad;
    g_waveDrag = 0;
    g_waveZoom = 1.0;
    g_waveViewStart = 0.0;
    g_waveScrollDrag = FALSE;
    g_wavePanDrag = FALSE;
    g_wavePanPending = FALSE;
    g_wavePlayhead = -1.0;
    g_waveCursor = -1.0;
    g_waveAuditionStart = -1.0;
    g_waveAuditionEnd = -1.0;
    g_waveAuditionKind = WaveAuditionKind::None;
    g_waveFrameTick = 0;

    if (!g_waveWnd) {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = WaveProc;
        wc.hInstance = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(g_hwnd, GWLP_HINSTANCE));
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = SamplerBackBrush();
        wc.lpszClassName = L"BassWaveWindow";
        wc.hIcon = PlayerIcon(GetSystemMetrics(SM_CXICON));
        wc.hIconSm = PlayerIcon(GetSystemMetrics(SM_CXSMICON));
        RegisterClassExW(&wc);

        RECT rc = {0, 0, kWaveWinW, kWaveWinH};
        ClampWindowSizeToWorkArea(&rc);
        g_waveWnd = CreateWindowExW(0, wc.lpszClassName, L"",
            kWaveWinStyle, CW_USEDEFAULT, CW_USEDEFAULT,
            rc.right - rc.left, rc.bottom - rc.top,
            g_hwnd, nullptr, wc.hInstance, nullptr);
        if (!g_waveWnd) return;
    }

    const std::wstring title = FileNameOf(g_pads[pad].path);
    SetWindowTextW(g_waveWnd, title.c_str());
    ShowWaveModeControls();
    LayoutWaveButton(g_waveWnd);
    ShowWindow(g_waveWnd, IsIconic(g_waveWnd) ? SW_RESTORE : SW_SHOW);
    SetForegroundWindow(g_waveWnd);

    SetTimer(g_waveWnd, ID_TIMER_WAVE, kWaveTimerMs, nullptr);
    g_waveBtnPlaying = WaveAuditionPlaying() ? TRUE : FALSE;
    InvalidateWaveAuditionButtons();
    InvalidateRect(g_waveWnd, nullptr, TRUE);
    RefreshRangeButtons();
}

void OpenWaveExport(const std::wstring& path) {
    if (path.empty() || !HasExtension(path, L".mp3")) return;
    if (g_waveExportBusy) {
        MessageBoxW(g_hwnd, L"Wait for the current MP3 export to finish.",
                    L"Export MP3 without re-encoding", MB_OK | MB_ICONINFORMATION);
        return;
    }
    WaveFreeAudition();
    if (!LoadWaveView(path)) {
        MessageBoxW(g_hwnd, L"Could not build the MP3 waveform.",
                    L"Error", MB_OK | MB_ICONWARNING);
        return;
    }

    g_waveMode = WaveMode::Export;
    g_wavePad = -1;
    g_waveDrag = 0;
    g_waveZoom = 1.0;
    g_waveViewStart = 0.0;
    g_waveScrollDrag = FALSE;
    g_wavePanDrag = FALSE;
    g_wavePanPending = FALSE;
    g_wavePlayhead = -1.0;
    g_waveCursor = -1.0;
    g_waveAuditionStart = -1.0;
    g_waveAuditionEnd = -1.0;
    g_waveAuditionKind = WaveAuditionKind::None;
    g_waveFrameTick = 0;
    g_waveExportSource = path;
    g_waveExportStart = 0.0;
    g_waveExportStop = 1.0;

    if (!g_waveWnd) {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = WaveProc;
        wc.hInstance = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(g_hwnd, GWLP_HINSTANCE));
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = SamplerBackBrush();
        wc.lpszClassName = L"BassWaveWindow";
        wc.hIcon = PlayerIcon(GetSystemMetrics(SM_CXICON));
        wc.hIconSm = PlayerIcon(GetSystemMetrics(SM_CXSMICON));
        RegisterClassExW(&wc);

        RECT rc = {0, 0, kWaveWinW, kWaveWinH};
        ClampWindowSizeToWorkArea(&rc);
        g_waveWnd = CreateWindowExW(WS_EX_CONTROLPARENT, wc.lpszClassName, L"",
            kWaveWinStyle, CW_USEDEFAULT, CW_USEDEFAULT,
            rc.right - rc.left, rc.bottom - rc.top,
            g_hwnd, nullptr, wc.hInstance, nullptr);
        if (!g_waveWnd) return;
    }

    const std::wstring title = L"Export MP3 without re-encoding - " + FileNameOf(path);
    SetWindowTextW(g_waveWnd, title.c_str());
    ShowWaveModeControls();
    LayoutWaveButton(g_waveWnd);
    ShowWindow(g_waveWnd, IsIconic(g_waveWnd) ? SW_RESTORE : SW_SHOW);
    SetForegroundWindow(g_waveWnd);

    SetTimer(g_waveWnd, ID_TIMER_WAVE, kWaveTimerMs, nullptr);
    g_waveBtnPlaying = FALSE;
    InvalidateWaveAuditionButtons();
    InvalidateRect(g_waveWnd, nullptr, TRUE);
    RefreshRangeButtons();
}

// ---------------------------------------------------- track level meter

const int kMeterTimerMs = 50;

const int kBaseMeterLabelW = 42;
const int kBaseMeterBarW = 29;
const int kBaseMeterBarGap = 10;
const int kBaseMeterTop = 16;
const int kBaseMeterBarH = 355;
const int kBaseMeterPanelGap = 18;
const int kBaseMeterPanelW = 104;
const int kBaseBtnH = 26;

int kMeterLabelW = kBaseMeterLabelW;
int kMeterBarW = kBaseMeterBarW;
int kMeterBarGap = kBaseMeterBarGap;
int kMeterTop = kBaseMeterTop;
int kMeterBarH = kBaseMeterBarH;
int kMeterBottom = kBaseMeterTop + kBaseMeterBarH;

int kMeterBarX = kBaseMeterLabelW;
int kMeterScaleW = kBaseMeterLabelW + kBaseMeterBarW * 2 + kBaseMeterBarGap;
int kMeterPanelGap = kBaseMeterPanelGap;
int kMeterPanelW = kBaseMeterPanelW;
int kMeterPanelX = kBaseMeterLabelW + kBaseMeterBarW * 2 + kBaseMeterBarGap + kBaseMeterPanelGap;
int kBtnH = kBaseBtnH;
int kMeterWinW = kMeterPanelX + kBaseMeterPanelW;
int kMeterWinH = kMeterBottom + 16;
const float kMeterMinDb = -40.0f;

const float kMeterScaleCurve = 0.055f;
const float kMeterFallDbPerSec = 20.0f;
const int kMeterHoldMs = 1500;
const float kMeterYellowDb = -2.0f;
const float kMeterRedDb = -0.2f;

HWND g_btnMeter = nullptr;
HWND g_meterWnd = nullptr;
float g_meterPeak[2] = {kMeterMinDb, kMeterMinDb};
float g_meterHold[2] = {kMeterMinDb, kMeterMinDb};
int g_meterHoldTicks[2] = {0, 0};
HBITMAP g_meterBackBmp = nullptr;
HDC g_meterBackDc = nullptr;
HGDIOBJ g_meterBackOldBmp = nullptr;
int g_meterBackW = 0;
int g_meterBackH = 0;

const int kBaseFaderW = 24;
const int kBaseFaderHandleH = 12;

const int kBaseTargetDotD = 13;
const int kBaseTargetDotGap = 5;

const COLORREF kDimAccent = RGB(86, 152, 232);
const COLORREF kDimAccentDark = RGB(28, 66, 132);
const COLORREF kDimBtnFill = RGB(28, 54, 92);
const COLORREF kDimBtnText = RGB(208, 228, 250);

const int kBaseBtnRowGap = 10;

int kFaderW = kBaseFaderW;
int kFaderHandleH = kBaseFaderHandleH;
int kTargetDotD = kBaseTargetDotD;
int kTargetDotGap = kBaseTargetDotGap;
int kBtnRowGap = kBaseBtnRowGap;
int kFaderTopOffset = kBaseBtnH + kBaseBtnRowGap;
int kFaderTravelH = kBaseMeterBarH - (kBaseBtnH + kBaseBtnRowGap) - kBaseBtnH - kBaseBtnRowGap;
BOOL g_faderDrag = FALSE;
int g_faderGrabOffset = 0;

int FaderX() { return kMeterPanelX + (kMeterPanelW - kFaderW) / 2; }
int FaderTravelTop() { return kMeterTop + kFaderTopOffset; }
int FaderTravelBottom() { return FaderTravelTop() + kFaderTravelH; }
int FaderTop() { return FaderTravelTop() + kFaderHandleH / 2; }
int FaderBottom() { return FaderTravelBottom() - kFaderHandleH / 2; }

int FaderY(int value) {
    return FaderBottom() - (FaderBottom() - FaderTop()) * value / 100;
}

int FaderValueAt(int y) {
    const float t = static_cast<float>(FaderBottom() - y) /
                    static_cast<float>(FaderBottom() - FaderTop());
    const int value = static_cast<int>(t * 100.0f + 0.5f);
    return (std::max)(0, (std::min)(value, 100));
}

RECT FaderHandleRect() {
    const int hy = FaderY(g_volume);
    const RECT rc = {FaderX(), hy - kFaderHandleH / 2, FaderX() + kFaderW,
                     hy + (kFaderHandleH + 1) / 2};
    return rc;
}

bool FaderHandleAt(int x, int y) {
    RECT rc = FaderHandleRect();
    rc.left -= 4; rc.right += 4;
    rc.top -= 2;  rc.bottom += 2;
    return x >= rc.left && x < rc.right && y >= rc.top && y < rc.bottom;
}

// -------- Auto mode: adjust track volume toward the target LUFS level --------

BOOL g_autoVolume = FALSE;
const int kDefaultAutoTargetDb = -12;
int g_autoTargetDb = kDefaultAutoTargetDb;
int g_autoAnimTo = 100;
BOOL g_autoAnim = FALSE;

// -------- -dB button: attenuate volume by the selected amount --------

const int kDimTargets[] = {-3, -6, -9, -12, -15, -18, -21, -24, -30, -36};
const int kDimTargetCount =
    static_cast<int>(sizeof(kDimTargets) / sizeof(kDimTargets[0]));
const int kDimTargetCmd = 41100;

bool IsKnownDimTarget(int db) {
    for (int target : kDimTargets)
        if (target == db) return true;
    return false;
}

const int kDefaultDimDb = -21;
int g_dimDb = kDefaultDimDb;
BOOL g_dimOn = FALSE;
int g_dimBase = 100;

int DimApply(int value, int db) {
    const int scaled = static_cast<int>(value * std::pow(10.0, db / 20.0) + 0.5);
    return (std::max)(0, (std::min)(scaled, 100));
}

void RefreshLevelButton() {
    if (g_btnMeter) InvalidateRect(g_btnMeter, nullptr, FALSE);
}

void SetAutoMode(BOOL on) {
    if ((g_autoVolume != FALSE) == (on != FALSE)) return;
    const BOOL wasOn = g_autoVolume;
    g_autoVolume = on;

    if (wasOn && !on) g_dimOn = FALSE;
    RefreshLevelButton();
}

void SetVolume(int value, BOOL syncSlider, BOOL fromUser) {
    value = (std::max)(0, (std::min)(value, 100));
    if (fromUser) {
        SetAutoMode(FALSE);
        if (g_dimOn) {
            g_dimOn = FALSE;
            RefreshLevelButton();
        }
        g_autoAnim = FALSE;
    }
    g_volume = value;
    ApplyVolume();
    if (syncSlider && g_vol) SendMessageW(g_vol, TBM_SETPOS, TRUE, value);
    if (g_meterWnd) InvalidateRect(g_meterWnd, nullptr, FALSE);
}

const int kAutoTargets[] = {-10, -11, -12, -13, -14};
const int kAutoTargetCount =
    static_cast<int>(sizeof(kAutoTargets) / sizeof(kAutoTargets[0]));

bool IsKnownAutoTarget(int db) {
    for (int target : kAutoTargets)
        if (target == db) return true;
    return false;
}

const double kAutoCorrectionPart = 0.7;
const int kBaseAutoBtnW = 46;
const int kBaseTargetBtnW = 40;
const int kBaseBtnGap = 6;
const int kAutoTargetCmd = 41000;

int kAutoBtnW = kBaseAutoBtnW;
int kTargetBtnW = kBaseTargetBtnW;
int kBtnGap = kBaseBtnGap;

RECT AutoButtonRect() {
    const int rowW = kAutoBtnW + kBtnGap + kTargetBtnW;
    const int x = kMeterPanelX + (kMeterPanelW - rowW) / 2;
    const int y = kMeterBottom - kBtnH;
    const RECT rc = {x, y, x + kAutoBtnW, y + kBtnH};
    return rc;
}

RECT TargetButtonRect() {
    RECT rc = AutoButtonRect();
    rc.left = rc.right + kBtnGap;
    rc.right = rc.left + kTargetBtnW;
    return rc;
}

RECT DimButtonRect() {
    const int rowW = kAutoBtnW + kBtnGap + kTargetBtnW;
    const int x = kMeterPanelX + (kMeterPanelW - rowW) / 2;
    const RECT rc = {x, kMeterTop, x + kAutoBtnW, kMeterTop + kBtnH};
    return rc;
}

RECT DimTargetButtonRect() {
    RECT rc = DimButtonRect();
    rc.left = rc.right + kBtnGap;
    rc.right = rc.left + kTargetBtnW;
    return rc;
}

bool InRect(const RECT& rc, int x, int y) {
    return x >= rc.left && x < rc.right && y >= rc.top && y < rc.bottom;
}

void DrawFlatButton(HDC dc, const RECT& rc, const wchar_t* text, bool on) {
    FillSolidRect(dc, rc, on ? RGB(38, 66, 44) : RGB(52, 52, 58));
    FrameSolidRect(dc, rc, on ? RGB(64, 200, 84) : RGB(96, 96, 104));
    RECT textRc = rc;
    SetTextColor(dc, on ? RGB(206, 242, 212) : RGB(206, 206, 212));
    DrawTextW(dc, text, -1, &textRc,
              DT_SINGLELINE | DT_VCENTER | DT_CENTER | DT_NOPREFIX);
}

void DrawDimButton(HDC dc, const RECT& rc, bool on) {
    FillSolidRect(dc, rc, on ? kDimBtnFill : RGB(52, 52, 58));
    FrameSolidRect(dc, rc, on ? kDimAccent : RGB(96, 96, 104));
    RECT textRc = rc;
    SetTextColor(dc, on ? kDimBtnText : RGB(206, 206, 212));
    DrawTextW(dc, L"-dB", -1, &textRc,
              DT_SINGLELINE | DT_VCENTER | DT_CENTER | DT_NOPREFIX);
}

int AutoVolumeForTrack(const Track& track) {
    if (!track.loudnessDone || track.lufs <= kLoudNoValue + 1.0) return -1;

    const double gainDb = (g_autoTargetDb - track.lufs) * kAutoCorrectionPart;
    if (gainDb >= 0.0) return 100;
    const int value = static_cast<int>(100.0 * std::pow(10.0, gainDb / 20.0) + 0.5);
    return (std::max)(1, (std::min)(value, 100));
}

int AutoLevelNow() {
    const Track* track = CurrentPlayingTrack();
    return track ? AutoVolumeForTrack(*track) : -1;
}

int DimBaseLevel() {
    if (g_autoVolume) return AutoLevelNow();
    return g_dimOn ? g_dimBase : g_volume;
}

int DimMarkLevel() {
    const int base = DimBaseLevel();
    if (base < 0) return -1;
    return g_dimOn ? base : DimApply(base, g_dimDb);
}

RECT FaderTargetDotRect() {
    const RECT none = {0, 0, 0, 0};
    const int value = AutoLevelNow();
    if (value < 0) return none;
    const int cy = FaderY(value);
    const int right = FaderX() - kTargetDotGap;
    const RECT rc = {right - kTargetDotD, cy - kTargetDotD / 2,
                     right, cy + (kTargetDotD + 1) / 2};
    return rc;
}

RECT FaderDimDotRect() {
    const RECT none = {0, 0, 0, 0};
    const int value = DimMarkLevel();
    if (value < 0) return none;
    const int cy = FaderY(value);
    const int left = FaderX() + kFaderW + kTargetDotGap;
    const RECT rc = {left, cy - kTargetDotD / 2, left + kTargetDotD,
                     cy + (kTargetDotD + 1) / 2};
    return rc;
}

void AnimateVolumeTo(int value) {
    g_autoAnimTo = value;
    g_autoAnim = TRUE;
}

void ApplyAutoVolume() {
    if (!g_autoVolume) return;
    const int value = AutoLevelNow();
    if (value < 0) return;
    AnimateVolumeTo(g_dimOn ? DimApply(value, g_dimDb) : value);
}

void RefreshLevelSettingsUi() {
    if (g_meterWnd) InvalidateRect(g_meterWnd, nullptr, FALSE);
    if (g_settingsWnd) InvalidateRect(g_settingsWnd, nullptr, FALSE);
}

void SetAutoTargetDb(int db) {
    if (!IsKnownAutoTarget(db) || g_autoTargetDb == db) return;
    g_autoTargetDb = db;
    SaveSettings();
    if (g_autoVolume) ApplyAutoVolume();
    RefreshLevelSettingsUi();
}

void SetDimTargetDb(int db) {
    if (!IsKnownDimTarget(db) || g_dimDb == db) return;
    g_dimDb = db;
    SaveSettings();
    if (g_dimOn) {
        if (g_autoVolume) ApplyAutoVolume();
        else AnimateVolumeTo(DimApply(g_dimBase, g_dimDb));
    }
    RefreshLevelSettingsUi();
}

void ToggleDim() {
    if (g_autoVolume) {
        g_dimOn = !g_dimOn;
        ApplyAutoVolume();
        RefreshLevelButton();
        return;
    }
    if (!g_dimOn) {
        g_dimOn = TRUE;
        g_dimBase = g_volume;
        AnimateVolumeTo(DimApply(g_dimBase, g_dimDb));
    } else {
        g_dimOn = FALSE;
        AnimateVolumeTo(g_dimBase);
    }
}

void StepVolumeAnimation() {
    if (!g_autoAnim) return;
    const int diff = g_autoAnimTo - g_volume;
    const int adiff = (diff < 0) ? -diff : diff;
    if (adiff == 0) { g_autoAnim = FALSE; return; }
    int step = static_cast<int>(adiff * 0.15f);
    if (step < 1) step = 1;
    if (step > adiff) step = adiff;
    SetVolume(g_volume + (diff > 0 ? step : -step), TRUE, FALSE);
    if (g_volume == g_autoAnimTo) g_autoAnim = FALSE;
}

void ShowAutoTargetMenu(HWND hwnd) {
    HMENU menu = CreatePopupMenu();
    if (!menu) return;
    for (int i = 0; i < kAutoTargetCount; ++i) {
        wchar_t text[16];
        swprintf_s(text, L"%d", kAutoTargets[i]);
        AppendMenuW(menu, MF_STRING | (kAutoTargets[i] == g_autoTargetDb ? MF_CHECKED : 0),
                    static_cast<UINT_PTR>(kAutoTargetCmd + i), text);
    }
    POINT pt = {};
    GetCursorPos(&pt);
    SetForegroundWindow(hwnd);
    const int cmd = static_cast<int>(TrackPopupMenu(
        menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY, pt.x, pt.y, 0, hwnd, nullptr));
    DestroyMenu(menu);
    if (cmd < kAutoTargetCmd || cmd >= kAutoTargetCmd + kAutoTargetCount) return;
    SetAutoTargetDb(kAutoTargets[cmd - kAutoTargetCmd]);
    InvalidateRect(hwnd, nullptr, FALSE);
}

void ShowDimTargetMenu(HWND hwnd) {
    HMENU menu = CreatePopupMenu();
    if (!menu) return;
    for (int i = 0; i < kDimTargetCount; ++i) {
        wchar_t text[16];
        swprintf_s(text, L"%d", kDimTargets[i]);
        AppendMenuW(menu, MF_STRING | (kDimTargets[i] == g_dimDb ? MF_CHECKED : 0),
                    static_cast<UINT_PTR>(kDimTargetCmd + i), text);
    }
    POINT pt = {};
    GetCursorPos(&pt);
    SetForegroundWindow(hwnd);
    const int cmd = static_cast<int>(TrackPopupMenu(
        menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY, pt.x, pt.y, 0, hwnd, nullptr));
    DestroyMenu(menu);
    if (cmd < kDimTargetCmd || cmd >= kDimTargetCmd + kDimTargetCount) return;
    SetDimTargetDb(kDimTargets[cmd - kDimTargetCmd]);
    InvalidateRect(hwnd, nullptr, FALSE);
}

int MeterY(float db) {
    if (db < kMeterMinDb) db = kMeterMinDb;
    if (db > 0.0f) db = 0.0f;
    const float d = -db;

    const float span = -kMeterMinDb;
    const float k = (d / (1.0f + kMeterScaleCurve * d)) /
                    (span / (1.0f + kMeterScaleCurve * span));
    return kMeterTop + static_cast<int>(k * (kMeterBottom - kMeterTop) + 0.5f);
}

RECT MeterBarsRect() {
    return {kMeterBarX, kMeterTop,
            kMeterBarX + kMeterBarW * 2 + kMeterBarGap, kMeterBottom};
}

void MeterReadLevels(float* out) {
    out[0] = out[1] = kMeterMinDb;
    if (!g_channel) return;
    float levels[2] = {0.0f, 0.0f};
    const BOOL ok = g_mixerMode
        ? BASS_Mixer_ChannelGetLevelEx(g_channel, levels, 0.02f,
                                       BASS_LEVEL_STEREO | BASS_LEVEL_VOLPAN)
        : BASS_ChannelGetLevelEx(g_channel, levels, 0.02f,
                                 BASS_LEVEL_STEREO | BASS_LEVEL_VOLPAN);
    if (!ok) return;
    for (int ch = 0; ch < 2; ++ch)
        out[ch] = (levels[ch] > 0.0000001f)
                  ? static_cast<float>(20.0 * std::log10(levels[ch]))
                  : kMeterMinDb;
}

bool MeterUpdate() {
    float level[2];
    MeterReadLevels(level);
    const float fall = kMeterFallDbPerSec * kMeterTimerMs / 1000.0f;
    bool changed = false;
    for (int ch = 0; ch < 2; ++ch) {
        const float prevPeak = g_meterPeak[ch];
        const float prevHold = g_meterHold[ch];

        g_meterPeak[ch] = (std::max)(level[ch], g_meterPeak[ch] - fall);
        if (g_meterPeak[ch] < kMeterMinDb) g_meterPeak[ch] = kMeterMinDb;

        if (g_meterPeak[ch] >= g_meterHold[ch]) {
            g_meterHold[ch] = g_meterPeak[ch];
            g_meterHoldTicks[ch] = kMeterHoldMs / kMeterTimerMs;
        } else if (g_meterHoldTicks[ch] > 0) {
            --g_meterHoldTicks[ch];
        } else {
            g_meterHold[ch] = (std::max)(g_meterPeak[ch], g_meterHold[ch] - fall);
        }

        if (g_meterPeak[ch] != prevPeak || g_meterHold[ch] != prevHold)
            changed = true;
    }
    return changed;
}

void DrawMeterWindow(HWND hwnd, HDC dc, const RECT& client) {
    FillSolidRect(dc, client, RGB(26, 26, 28));

    const int barsTotal = kMeterBarW * 2 + kMeterBarGap;
    const int x0 = kMeterBarX;

    HFONT oldFont = static_cast<HFONT>(SelectObject(dc, UiFont()));
    SetBkMode(dc, TRANSPARENT);

    const float marks[7] = {-40.0f, -30.0f, -20.0f,
                            -12.0f, -6.0f, -3.0f, 0.0f};
    for (float db : marks) {
        const int y = MeterY(db);
        const RECT tick = {x0, y, x0 + barsTotal, y + 1};
        FillSolidRect(dc, tick, RGB(66, 66, 72));

        if (db <= kMeterMinDb || db >= 0.0f) continue;
        wchar_t text[8];
        swprintf_s(text, L"%d", static_cast<int>(db));
        RECT numRc = {S(4), y - S(9), x0 - S(7), y + S(9)};
        SetTextColor(dc, RGB(148, 148, 154));
        DrawTextW(dc, text, -1, &numRc,
                  DT_SINGLELINE | DT_VCENTER | DT_RIGHT | DT_NOPREFIX);
    }

    for (int ch = 0; ch < 2; ++ch) {
        const int bx = x0 + ch * (kMeterBarW + kMeterBarGap);
        const RECT bar = {bx, kMeterTop, bx + kMeterBarW, kMeterBottom};
        FillSolidRect(dc, bar, RGB(12, 12, 14));

        const int yLevel = MeterY(g_meterPeak[ch]);
        const int yGreen = MeterY(kMeterYellowDb);
        const int yYellow = MeterY(kMeterRedDb);
        const RECT green = {bx + S(2), (std::max)(yLevel, yGreen), bx + kMeterBarW - S(2),
                            kMeterBottom - 1};
        const RECT yellow = {bx + S(2), (std::max)(yLevel, yYellow), bx + kMeterBarW - S(2),
                             (std::min)(kMeterBottom, yGreen) - 1};
        const RECT red = {bx + S(2), yLevel, bx + kMeterBarW - S(2),
                          (std::min)(kMeterBottom, yYellow) - 1};
        if (green.bottom > green.top) FillSolidRect(dc, green, RGB(64, 200, 84));
        if (yellow.bottom > yellow.top) FillSolidRect(dc, yellow, RGB(232, 196, 42));
        if (red.bottom > red.top) FillSolidRect(dc, red, RGB(226, 62, 52));

        if (g_meterHold[ch] > kMeterMinDb) {
            const int yHold = MeterY(g_meterHold[ch]);
            const RECT hold = {bx + S(2), yHold - 1, bx + kMeterBarW - S(2), yHold + 1};
            FillSolidRect(dc, hold, RGB(240, 240, 240));
        }
        FrameSolidRect(dc, bar, RGB(88, 88, 94));
    }

    const int fx = FaderX();
    const int fcx = fx + kFaderW / 2;
    const RECT groove = {fcx - (std::max)(1, S(3)), FaderTravelTop(),
                         fcx + (std::max)(1, S(4)), FaderTravelBottom()};
    FillSolidRect(dc, groove, RGB(12, 12, 14));
    FrameSolidRect(dc, groove, RGB(70, 70, 78));

    const int hy = FaderY(g_volume);
    const RECT handle = FaderHandleRect();
    FillSolidRect(dc, handle, RGB(198, 198, 204));
    FrameSolidRect(dc, handle, RGB(92, 92, 100));
    const RECT grip = {fx + S(6), hy - 1, fx + kFaderW - S(6), hy + 1};
    FillSolidRect(dc, grip, RGB(126, 126, 134));

    const RECT targetDot = FaderTargetDotRect();
    if (targetDot.right > targetDot.left)
        FillSolidCircle(dc, targetDot, RGB(74, 206, 96), RGB(30, 96, 44));

    const RECT dimDot = FaderDimDotRect();
    if (dimDot.right > dimDot.left)
        FillSolidCircle(dc, dimDot, kDimAccent, kDimAccentDark);

    wchar_t dimValue[16];
    swprintf_s(dimValue, L"%d", g_dimDb);
    DrawDimButton(dc, DimButtonRect(), g_dimOn != FALSE);
    DrawFlatButton(dc, DimTargetButtonRect(), dimValue, FALSE);

    wchar_t target[16];
    swprintf_s(target, L"%d", g_autoTargetDb);
    DrawFlatButton(dc, AutoButtonRect(), L"Auto", g_autoVolume != FALSE);
    DrawFlatButton(dc, TargetButtonRect(), target, FALSE);

    SelectObject(dc, oldFont);
    DrawCustomCaption(hwnd, dc, client);
}

void ReleaseMeterBackBuffer() {
    if (g_meterBackDc) {
        if (g_meterBackBmp) {
            if (g_meterBackOldBmp)
                SelectObject(g_meterBackDc, g_meterBackOldBmp);
            DeleteObject(g_meterBackBmp);
        }
        DeleteDC(g_meterBackDc);
    }
    g_meterBackBmp = nullptr;
    g_meterBackDc = nullptr;
    g_meterBackOldBmp = nullptr;
    g_meterBackW = 0;
    g_meterBackH = 0;
}

LRESULT CALLBACK MeterProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    LRESULT frameResult = 0;
    if (SkipSystemFramePaint(msg, frameResult)) return frameResult;
    switch (msg) {
    case WM_CREATE:
        SetTimer(hwnd, ID_TIMER_METER, kMeterTimerMs, nullptr);
        return 0;

    case WM_TIMER:
        if (wParam == ID_TIMER_METER) {
            StepVolumeAnimation();
            if (MeterUpdate()) {
                RECT bars = MeterBarsRect();
                InvalidateRect(hwnd, &bars, FALSE);
            }
        }
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_NCHITTEST:
        return CustomCaptionHitTest(hwnd, lParam);

    case WM_LBUTTONDOWN: {
        if (HandleCaptionMouseDown(hwnd, lParam)) return 0;
        const int x = GET_X_LPARAM(lParam);
        const int y = GET_Y_LPARAM(lParam);
        if (InRect(DimButtonRect(), x, y)) {
            ToggleDim();
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        if (InRect(DimTargetButtonRect(), x, y)) {
            ShowDimTargetMenu(hwnd);
            return 0;
        }
        if (InRect(AutoButtonRect(), x, y)) {
            SetAutoMode(!g_autoVolume);
            g_autoAnim = FALSE;
            if (g_autoVolume) ApplyAutoVolume();
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        if (InRect(TargetButtonRect(), x, y)) {
            ShowAutoTargetMenu(hwnd);
            return 0;
        }
        if (!FaderHandleAt(x, y)) break;
        g_faderDrag = TRUE;
        g_faderGrabOffset = y - FaderY(g_volume);
        SetCapture(hwnd);
        SetVolume(FaderValueAt(y - g_faderGrabOffset), TRUE, TRUE);
        return 0;
    }

    case WM_SETCURSOR:
        if (LOWORD(lParam) == HTCLIENT) {
            POINT pt = {};
            GetCursorPos(&pt);
            ScreenToClient(hwnd, &pt);
            SetCursor(LoadCursorW(nullptr, FaderHandleAt(pt.x, pt.y) ? IDC_SIZENS : IDC_ARROW));
            return TRUE;
        }
        break;

    case WM_MOUSEMOVE:
        if (g_faderDrag) {
            SetVolume(FaderValueAt(GET_Y_LPARAM(lParam) - g_faderGrabOffset), TRUE, TRUE);
            return 0;
        }
        break;

    case WM_LBUTTONUP:
        if (g_faderDrag) {
            g_faderDrag = FALSE;
            ReleaseCapture();
        }
        return 0;

    case WM_CAPTURECHANGED:
        g_faderDrag = FALSE;
        return 0;

    case WM_PAINT: {
        PAINTSTRUCT ps = {};
        HDC dc = BeginPaint(hwnd, &ps);
        RECT rc = {};
        GetClientRect(hwnd, &rc);

        if (rc.right <= 0 || rc.bottom <= 0) {
            EndPaint(hwnd, &ps);
            return 0;
        }
        if (!g_meterBackDc)
            g_meterBackDc = CreateCompatibleDC(dc);
        if (!g_meterBackDc) {
            DrawMeterWindow(hwnd, dc, rc);
            EndPaint(hwnd, &ps);
            return 0;
        }

        if (!g_meterBackBmp || g_meterBackW != rc.right || g_meterBackH != rc.bottom) {
            if (g_meterBackBmp) {
                if (g_meterBackOldBmp)
                    SelectObject(g_meterBackDc, g_meterBackOldBmp);
                DeleteObject(g_meterBackBmp);
                g_meterBackBmp = nullptr;
                g_meterBackOldBmp = nullptr;
            }
            HBITMAP bitmap = CreateCompatibleBitmap(dc, rc.right, rc.bottom);
            if (bitmap) {
                HGDIOBJ oldBmp = SelectObject(g_meterBackDc, bitmap);
                if (oldBmp && oldBmp != HGDI_ERROR) {
                    g_meterBackBmp = bitmap;
                    g_meterBackOldBmp = oldBmp;
                    g_meterBackW = rc.right;
                    g_meterBackH = rc.bottom;
                    ps.rcPaint = rc;
                } else {
                    DeleteObject(bitmap);
                }
            }
        }
        if (!g_meterBackBmp) {
            DrawMeterWindow(hwnd, dc, rc);
            EndPaint(hwnd, &ps);
            return 0;
        }

        const int saved = SaveDC(g_meterBackDc);
        if (!saved) {
            DrawMeterWindow(hwnd, dc, rc);
            EndPaint(hwnd, &ps);
            return 0;
        }
        IntersectClipRect(g_meterBackDc, ps.rcPaint.left, ps.rcPaint.top,
                          ps.rcPaint.right, ps.rcPaint.bottom);
        DrawMeterWindow(hwnd, g_meterBackDc, rc);
        RestoreDC(g_meterBackDc, saved);
        BitBlt(dc, ps.rcPaint.left, ps.rcPaint.top,
               ps.rcPaint.right - ps.rcPaint.left,
               ps.rcPaint.bottom - ps.rcPaint.top,
               g_meterBackDc, ps.rcPaint.left, ps.rcPaint.top, SRCCOPY);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_SYSCOMMAND:
        if ((wParam & 0xFFF0) == SC_MINIMIZE) {
            SendMessageW(hwnd, WM_CLOSE, 0, 0);
            return 0;
        }
        break;

    case WM_CLOSE:
        KillTimer(hwnd, ID_TIMER_METER);
        ShowWindow(hwnd, SW_HIDE);
        return 0;

    case WM_DESTROY:
        KillTimer(hwnd, ID_TIMER_METER);
        ReleaseMeterBackBuffer();
        g_meterWnd = nullptr;
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void OpenMeterWindow() {
    if (!g_meterWnd) {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = MeterProc;
        wc.hInstance = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(g_hwnd, GWLP_HINSTANCE));
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = nullptr;
        wc.lpszClassName = L"BassMeterWindow";
        wc.hIcon = PlayerIcon(GetSystemMetrics(SM_CXICON));
        wc.hIconSm = PlayerIcon(GetSystemMetrics(SM_CXSMICON));
        RegisterClassExW(&wc);

        RECT rc = {0, 0, kMeterWinW, kMeterWinH};
        const DWORD style = WS_POPUP;
        AdjustWindowRect(&rc, style, FALSE);
        g_meterWnd = CreateWindowExW(0, wc.lpszClassName, L"Level",
            style, CW_USEDEFAULT, CW_USEDEFAULT,
            rc.right - rc.left, rc.bottom - rc.top,
            g_hwnd, nullptr, wc.hInstance, nullptr);
        if (!g_meterWnd) return;
    }

    ShowWindow(g_meterWnd, IsIconic(g_meterWnd) ? SW_RESTORE : SW_SHOW);
    SetForegroundWindow(g_meterWnd);

    InvalidateRect(g_meterWnd, nullptr, TRUE);

    SetTimer(g_meterWnd, ID_TIMER_METER, kMeterTimerMs, nullptr);
}

void ToggleMeterWindow() {
    if (g_meterWnd && IsWindowVisible(g_meterWnd)) {
        if (IsIconic(g_meterWnd)) {
            ShowWindow(g_meterWnd, SW_RESTORE);
            SetForegroundWindow(g_meterWnd);
            return;
        }
        SendMessageW(g_meterWnd, WM_CLOSE, 0, 0);
        return;
    }
    OpenMeterWindow();
}

// --------------------------------------------- dark sliders in the main window

const int kBaseMainHandleW = 18;
const int kBaseMainHandleH = 16;
const int kBaseMainGrooveH = 8;

int kMainHandleW = kBaseMainHandleW;
int kMainHandleH = kBaseMainHandleH;
int kMainGrooveH = kBaseMainGrooveH;

void RebuildUiMetrics() {

    kGutterWidth = S(kBaseGutterWidth);
    kTimeColWidth = S(kBaseTimeColWidth);
    kLoudColWidth = S(kBaseLoudColWidth);
    kMargin = S(kBaseMargin);
    kGripSize = S(kBaseGripSize);
    kVolWidth = S(kBaseVolWidth);
    kTransportBtnW = S(kBaseTransportBtnW);
    kListBtnW = S(kBaseListBtnW);
    kNextBtnW = S(kBaseNextBtnW);
    kDelayBtnW = S(kBaseDelayBtnW);
    kPlaylistBtnW = S(kBasePlaylistBtnW);
    kBottomGap = S(kBaseBottomGap);
    kBtnTextPad = S(kBaseBtnTextPad);
    kTimeLabelW = S(kBaseTimeLabelW);
    kSettingsBtnW = S(kBaseSettingsBtnW);
    kCaptionH = S(kBaseCaptionH);
    kCaptionBtnW = S(kBaseCaptionBtnW);
    kCaptionBtnH = S(kBaseCaptionBtnH);
    kCaptionGap = S(kBaseCaptionGap);
    kCaptionSide = S(kBaseCaptionSide);

    g_clearBtnW = S(kBaseClearBtnW);
    g_samplerBtnW = S(kBaseSamplerBtnW);
    g_meterBtnW = S(kBaseMeterBtnW);

    kSettingsWinW = S(kBaseSettingsWinW);
    kSettingsWinH = kCaptionH + S(kBaseSettingsWinH);
    kSettingsNavW = S(kBaseSettingsNavW);
    kSettingsNavH = S(kBaseSettingsNavH);
    kSettingsRowH = S(kBaseSettingsRowH);

    kMainHandleW = S(kBaseMainHandleW);
    kMainHandleH = S(kBaseMainHandleH);
    kMainGrooveH = S(kBaseMainGrooveH);

    kPadRowH = S(kBasePadRowH);
    kPadBtnH = S(kBasePadBtnH);
    kPadTop = kCaptionH + S(kBasePadTop);
    kPadLoadX = S(kBasePadLoadX);
    kPadLoadW = kPadBtnH;
    kPadClearGap = S(kBasePadClearGap);
    kPadClearX = kPadLoadX + kPadLoadW + kPadClearGap;
    kPadClearW = kPadBtnH;
    kPadRangeGap = S(kBasePadRangeGap);
    kPadRangeX = kPadClearX + kPadClearW + kPadRangeGap;
    kPadRangeW = kPadBtnH;
    kPadBtnX = kPadRangeX + kPadRangeW + S(8);
    kPadBtnW = S(kBasePadBtnW);
    kPadVolX = kPadBtnX + kPadBtnW + S(10);
    kPadVolW = S(kBasePadVolW);
    kPadsWinW = kPadVolX + kPadVolW + S(14);
    kPadsWinH = kPadTop + kPadCount * kPadRowH - (kPadRowH - kPadBtnH) + S(14);

    kVolGrooveInset = S(kBaseVolGrooveInset);
    kVolGrooveH = S(kBaseVolGrooveH);
    kVolHandleW = S(kBaseVolHandleW);
    kVolHandleH = S(kBaseVolHandleH);

    kWaveWinW = S(kBaseWaveWinW);
    kWaveWinH = kCaptionH + S(kBaseWaveWinH);
    kWaveWinMinW = S(kBaseWaveWinMinW);
    kWaveWinMinH = kCaptionH + S(kBaseWaveWinMinH);
    kWaveMargin = S(kBaseWaveMargin);
    kWaveScrollH = S(kBaseWaveScrollH);
    kWaveGap = S(kBaseWaveGap);
    kWaveBarH = S(kBaseWaveBarH);
    kWaveButtonW = S(kBaseWaveButtonW);
    kWaveExportGap = S(kBaseWaveExportGap);

    kMeterLabelW = S(kBaseMeterLabelW);
    kMeterBarW = S(kBaseMeterBarW);
    kMeterBarGap = S(kBaseMeterBarGap);
    kMeterTop = kCaptionH + S(kBaseMeterTop);
    kMeterBarH = S(kBaseMeterBarH);
    kMeterBottom = kMeterTop + kMeterBarH;
    kMeterBarX = kMeterLabelW;
    kMeterScaleW = kMeterLabelW + kMeterBarW * 2 + kMeterBarGap;
    kMeterPanelGap = S(kBaseMeterPanelGap);
    kMeterPanelW = S(kBaseMeterPanelW);
    kMeterPanelX = kMeterScaleW + kMeterPanelGap;
    kBtnH = S(kBaseBtnH);
    kMeterWinW = kMeterPanelX + kMeterPanelW;
    kMeterWinH = kMeterBottom + S(16);
    kFaderW = S(kBaseFaderW);
    kFaderHandleH = S(kBaseFaderHandleH);
    kTargetDotD = S(kBaseTargetDotD);
    kTargetDotGap = S(kBaseTargetDotGap);
    kBtnRowGap = S(kBaseBtnRowGap);
    kFaderTopOffset = kBtnH + kBtnRowGap;
    kFaderTravelH = kMeterBarH - kFaderTopOffset - kBtnH - kBtnRowGap;
    kAutoBtnW = S(kBaseAutoBtnW);
    kTargetBtnW = S(kBaseTargetBtnW);
    kBtnGap = S(kBaseBtnGap);
}

void DrawMainSlider(HWND slider, HDC dc, BOOL volume) {
    RECT rc = {};
    GetClientRect(slider, &rc);
    FillSolidRect(dc, rc, kMainBack);

    RECT channel = {};
    SendMessageW(slider, TBM_GETCHANNELRECT, 0, reinterpret_cast<LPARAM>(&channel));
    if (channel.right <= channel.left) return;

    const int midY = (channel.top + channel.bottom) / 2;
    const RECT groove = {channel.left, midY - kMainGrooveH / 2,
                         channel.right, midY - kMainGrooveH / 2 + kMainGrooveH};

    FillSolidRect(dc, groove, kMainGroove);

    RECT thumb = {};
    SendMessageW(slider, TBM_GETTHUMBRECT, 0, reinterpret_cast<LPARAM>(&thumb));
    const int center = (thumb.left + thumb.right) / 2;

    const RECT filled = {groove.left, groove.top, center, groove.bottom};
    if (filled.right > filled.left)
        FillSolidRect(dc, filled, volume ? kMainFilled : kMainGlowEdge);
    FrameSolidRect(dc, groove, kMainGrooveEdge);

    const RECT handle = {thumb.left, midY - kMainHandleH / 2,
                         thumb.right, midY - kMainHandleH / 2 + kMainHandleH};
    FillSolidRect(dc, handle, kMainHandle);
    FrameSolidRect(dc, handle, kMainHandleEdge);
}

LRESULT CALLBACK DarkSliderProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
                                UINT_PTR, DWORD_PTR) {
    switch (msg) {
    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT: {

        RECT client = {};
        RECT updateRc = {};
        GetClientRect(hwnd, &client);
        if (GetUpdateRect(hwnd, &updateRc, FALSE) &&
            (updateRc.left > client.left || updateRc.top > client.top ||
             updateRc.right < client.right || updateRc.bottom < client.bottom)) {
            InvalidateRect(hwnd, nullptr, FALSE);
        }

        PAINTSTRUCT ps = {};
        HDC dc = BeginPaint(hwnd, &ps);
        DrawMainSlider(hwnd, dc, GetDlgCtrlID(hwnd) == ID_VOL);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_SETCURSOR:
        if (LOWORD(lParam) == HTCLIENT && GetDlgCtrlID(hwnd) == ID_VOL) {

            POINT pt = {};
            GetCursorPos(&pt);
            ScreenToClient(hwnd, &pt);
            RECT thumb = {};
            SendMessageW(hwnd, TBM_GETTHUMBRECT, 0, reinterpret_cast<LPARAM>(&thumb));
            SetCursor(LoadCursorW(nullptr, PtInRect(&thumb, pt) ? IDC_SIZEWE : IDC_ARROW));
            return TRUE;
        }
        break;

    }
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

void LayoutControls(int cx, int cy) {
    if (!g_hwnd) return;

    const int rowY = (std::max)(kCaptionH + S(190), cy - S(42));
    const int titleY = kCaptionH + S(8);
    const int seekY = kCaptionH + S(36);
    const int btnY = kCaptionH + S(76);
    const int listTop = kCaptionH + S(116);
    const int btnH = S(32);
    const int gap = S(8);

    const int timeX = cx - kMargin - kTimeLabelW;
    MoveWindow(g_lblTitle, kMargin, titleY, (std::max)(S(120), timeX - S(10) - kMargin), S(24), TRUE);
    MoveWindow(g_lblTime, timeX, titleY, kTimeLabelW, S(24), TRUE);

    const int volX = cx - kMargin - kVolWidth;
    MoveWindow(g_seek, kMargin, seekY, (std::max)(S(120), volX - S(12) - kMargin), S(30), TRUE);
    MoveWindow(g_vol, volX, seekY, kVolWidth, S(30), TRUE);

    MoveWindow(g_btnPlay, kMargin, btnY, kTransportBtnW, btnH, TRUE);
    MoveWindow(g_btnStop, kMargin + kTransportBtnW + gap, btnY, kTransportBtnW, btnH, TRUE);
    MoveWindow(g_btnPrev, kMargin + (kTransportBtnW + gap) * 2, btnY, kTransportBtnW, btnH, TRUE);
    MoveWindow(g_btnNext, kMargin + (kTransportBtnW + gap) * 3, btnY, kTransportBtnW, btnH, TRUE);
    const int nextX = kMargin + (kTransportBtnW + gap) * 4;
    MoveWindow(g_chkNext, nextX, btnY, kNextBtnW, btnH, TRUE);
    MoveWindow(g_chkDelay, nextX + kNextBtnW + gap, btnY, kDelayBtnW, btnH, TRUE);

    int listHeight = rowY - listTop - gap;
    if (listHeight < S(100)) listHeight = S(100);
    MoveWindow(g_list, kMargin, listTop, (std::max)(S(560), cx - kMargin * 2), listHeight, TRUE);

    MoveWindow(g_btnAdd, kMargin, rowY, kListBtnW, btnH, TRUE);
    MoveWindow(g_btnRemove, kMargin + kListBtnW + gap, rowY, kListBtnW, btnH, TRUE);
    const int listBtnRight = kMargin + (kListBtnW + kBottomGap) * 2;
    MoveWindow(g_btnClear, listBtnRight, rowY, g_clearBtnW, btnH, TRUE);
    if (g_btnSampler)
        MoveWindow(g_btnSampler, listBtnRight + g_clearBtnW + kBottomGap,
                   rowY, g_samplerBtnW, btnH, TRUE);
    if (g_btnMeter)
        MoveWindow(g_btnMeter, listBtnRight + g_clearBtnW + kBottomGap +
                   g_samplerBtnW + kBottomGap, rowY, g_meterBtnW, btnH, TRUE);

    int playlistX = listBtnRight + g_clearBtnW + kBottomGap +
                    g_samplerBtnW + kBottomGap + g_meterBtnW + kBottomGap;
    for (int i = 0; i < kPlaylistCount; ++i) {
        if (!g_btnPl[i]) continue;
        MoveWindow(g_btnPl[i], playlistX, rowY, kPlaylistBtnW, btnH, TRUE);
        playlistX += kPlaylistBtnW + kBottomGap;
    }
    if (g_btnSettings)
        MoveWindow(g_btnSettings, playlistX, rowY, kSettingsBtnW, btnH, TRUE);
}

void CreateControls(HWND hwnd, HINSTANCE inst) {

    ButtonWidthForText(L"Clear", &g_clearBtnW);
    ButtonWidthForText(L"Sampler", &g_samplerBtnW);
    ButtonWidthForText(L"Level", &g_meterBtnW);

    g_lblTitle = CreateWindowExW(0, L"STATIC", L"",
        WS_CHILD | WS_VISIBLE | SS_LEFT | SS_ENDELLIPSIS,
        14, 12, 872, 20, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_LBL_TITLE)), inst, nullptr);

    g_seek = CreateWindowExW(0, TRACKBAR_CLASSW, L"",
        WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_NOTICKS,
        14, 38, 872, 30, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_SEEK)), inst, nullptr);
    SendMessageW(g_seek, TBM_SETRANGE, TRUE, MAKELONG(0, SLIDER_MAX));
    SendMessageW(g_seek, TBM_SETPAGESIZE, 0, SLIDER_MAX / 20);
    SendMessageW(g_seek, TBM_SETTHUMBLENGTH, kMainHandleW, 0);
    SendMessageW(g_seek, TBM_SETPOS, TRUE, 0);

    g_lblTime = CreateWindowExW(0, L"STATIC", L"- 0 : 00",
        WS_CHILD | WS_VISIBLE | SS_RIGHT,
        14, 72, 872, 18, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_LBL_TIME)), inst, nullptr);

    g_btnPlay = MakeButton(hwnd, inst, L"", ID_BTN_PLAY, 14, 100, kTransportBtnW, 32, BS_OWNERDRAW);
    g_btnStop = MakeButton(hwnd, inst, L"", ID_BTN_STOP, 86, 100, kTransportBtnW, 32, BS_OWNERDRAW);
    g_btnPrev = MakeButton(hwnd, inst, L"", ID_BTN_PREV, 158, 100, kTransportBtnW, 32, BS_OWNERDRAW);
    g_btnNext = MakeButton(hwnd, inst, L"", ID_BTN_NEXT, 230, 100, kTransportBtnW, 32, BS_OWNERDRAW);
    g_chkNext = MakeButton(hwnd, inst, L"", ID_CHK_NEXT, 302, 100, kNextBtnW, 32, BS_OWNERDRAW);
    g_chkDelay = MakeButton(hwnd, inst, L"", ID_CHK_DELAY,
                            302 + kNextBtnW + 8, 100, kDelayBtnW, 32, BS_OWNERDRAW);

    g_vol = CreateWindowExW(0, TRACKBAR_CLASSW, L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | TBS_HORZ | TBS_NOTICKS,
        630, 100, 256, 30, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_VOL)), inst, nullptr);
    SendMessageW(g_vol, TBM_SETRANGE, TRUE, MAKELONG(0, 100));
    SendMessageW(g_vol, TBM_SETPAGESIZE, 0, 5);
    SendMessageW(g_vol, TBM_SETTHUMBLENGTH, kMainHandleW, 0);
    SendMessageW(g_vol, TBM_SETPOS, TRUE, g_volume);

    SetWindowSubclass(g_seek, SeekGuardProc, 1, 0);
    SetWindowSubclass(g_vol, VolumeWheelProc, 1, 0);

    SetWindowSubclass(g_seek, DarkSliderProc, 2, 0);
    SetWindowSubclass(g_vol, DarkSliderProc, 2, 0);

    g_list = CreateWindowExW(0, L"LISTBOX", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL |
        LBS_NOTIFY | LBS_NOINTEGRALHEIGHT | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS,
        14, 142, 872, 250, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_PLAYLIST)), inst, nullptr);

    SetWindowTheme(g_list, L"DarkMode_Explorer", nullptr);
    SetWindowSubclass(g_list, PlaylistProc, 1, 0);

    g_btnAdd    = MakeButton(hwnd, inst, L"", ID_BTN_ADD,    kMargin, 400, kListBtnW, 32, BS_OWNERDRAW);
    g_btnRemove = MakeButton(hwnd, inst, L"", ID_BTN_REMOVE, kMargin + kListBtnW + kBottomGap, 400,
                             kListBtnW, 32, BS_OWNERDRAW);
    int rowX = kMargin + (kListBtnW + kBottomGap) * 2;
    g_btnClear  = MakeButton(hwnd, inst, L"", ID_BTN_CLEAR, rowX, 400, g_clearBtnW, 32, BS_OWNERDRAW);
    rowX += g_clearBtnW + kBottomGap;
    g_btnSampler = MakeButton(hwnd, inst, L"", ID_BTN_SAMPLER, rowX, 400, g_samplerBtnW, 32, BS_OWNERDRAW);
    rowX += g_samplerBtnW + kBottomGap;
    g_btnMeter = MakeButton(hwnd, inst, L"", ID_BTN_METER, rowX, 400, g_meterBtnW, 32, BS_OWNERDRAW);
    rowX += g_meterBtnW + kBottomGap;

    const wchar_t* playlistNumbers[kPlaylistCount] = {L"1", L"2", L"3"};
    int playlistX = rowX;
    for (int i = 0; i < kPlaylistCount; ++i) {
        g_btnPl[i] = MakeButton(hwnd, inst, L"", ID_BTN_PLAYLIST1 + i, playlistX, 400,
                                kPlaylistBtnW, 32, BS_OWNERDRAW);
        SetWindowTextW(g_btnPl[i], playlistNumbers[i]);
        playlistX += kPlaylistBtnW + kBottomGap;
    }

    g_btnSettings = MakeButton(hwnd, inst, L"", ID_BTN_SETTINGS, playlistX, 400,
                               kSettingsBtnW, 32, BS_OWNERDRAW);

    for (HWND child : {g_lblTitle, g_lblTime})
        SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(LabelFont()), TRUE);
    SendMessageW(g_list, WM_SETFONT, reinterpret_cast<WPARAM>(ListFont()), TRUE);

    RECT rc = {};
    GetClientRect(hwnd, &rc);
    LayoutControls(rc.right - rc.left, rc.bottom - rc.top);

    LoadLoudnessDb();
    for (int i = 0; i < kPlaylistCount; ++i)
        LoadPlaylist(i);
    FillPlaylist(-1);
    LoadSampler();

    UpdateRemoveButtonState();
}

int BottomRowWidth() {
    int width = kMargin * 2;
    width += kListBtnW * 2 + g_clearBtnW + g_samplerBtnW + g_meterBtnW;
    width += kPlaylistBtnW * kPlaylistCount + kSettingsBtnW;
    width += kBottomGap * 8;
    return width;
}

void RefreshTextButtonWidths() {
    ButtonWidthForText(L"Clear", &g_clearBtnW);
    ButtonWidthForText(L"Sampler", &g_samplerBtnW);
    ButtonWidthForText(L"Level", &g_meterBtnW);

    ButtonWidthForText(L"Next: Off", &kNextBtnW);
    ButtonWidthForText(L"Pause: 5 s", &kDelayBtnW);
}

void ApplyFontsToControls() {
    for (HWND child : {g_lblTitle, g_lblTime})
        SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(LabelFont()), TRUE);
    for (HWND btn : {g_btnPlay, g_btnStop, g_btnPrev, g_btnNext,
                     g_chkNext, g_chkDelay, g_btnAdd, g_btnRemove,
                     g_btnClear, g_btnSampler, g_btnMeter, g_btnSettings,
                     g_btnPl[0], g_btnPl[1], g_btnPl[2]})
        if (btn) SendMessageW(btn, WM_SETFONT, reinterpret_cast<WPARAM>(UiFont()), TRUE);
    if (g_seek) SendMessageW(g_seek, TBM_SETTHUMBLENGTH, kMainHandleW, 0);
    if (g_vol) SendMessageW(g_vol, TBM_SETTHUMBLENGTH, kMainHandleW, 0);
    if (g_list) {
        SendMessageW(g_list, WM_SETFONT, reinterpret_cast<WPARAM>(ListFont()), TRUE);
        SendMessageW(g_list, LB_SETITEMHEIGHT, 0, ListItemHeight());
    }
    if (g_smpWnd) {
        for (int i = 0; i < kPadCount; ++i) {
            for (HWND btn : {g_smpLoad[i], g_smpClear[i], g_smpRange[i], g_smpPad[i]})
                if (btn) SendMessageW(btn, WM_SETFONT, reinterpret_cast<WPARAM>(UiFont()), TRUE);
        }
    }
    if (g_wavePlayBtn)
        SendMessageW(g_wavePlayBtn, WM_SETFONT, reinterpret_cast<WPARAM>(UiFont()), TRUE);
}

void ScaleWindowClient(HWND wnd, float factor) {
    if (!wnd || !IsWindow(wnd)) return;
    RECT rc = {};
    GetWindowRect(wnd, &rc);
    RECT client = {};
    GetClientRect(wnd, &client);
    const int w = static_cast<int>(client.right * factor + 0.5f);
    const int h = static_cast<int>(client.bottom * factor + 0.5f);
    SetWindowPos(wnd, nullptr, rc.left, rc.top, w, h,
                 SWP_NOZORDER | SWP_NOACTIVATE);
}

void SetWindowClientSize(HWND wnd, int clientW, int clientH) {
    if (!wnd || !IsWindow(wnd)) return;
    RECT rc = {};
    GetWindowRect(wnd, &rc);
    SetWindowPos(wnd, nullptr, rc.left, rc.top, clientW, clientH,
                 SWP_NOZORDER | SWP_NOACTIVATE);
}

void FitWindowToWorkArea(HWND wnd) {
    if (!wnd || !IsWindow(wnd)) return;
    HMONITOR mon = MonitorFromWindow(wnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = {};
    mi.cbSize = sizeof(mi);
    if (!mon || !GetMonitorInfoW(mon, &mi)) return;
    RECT rc = {};
    GetWindowRect(wnd, &rc);
    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;
    const int workW = mi.rcWork.right - mi.rcWork.left;
    const int workH = mi.rcWork.bottom - mi.rcWork.top;
    if (w > workW) w = workW;
    if (h > workH) h = workH;
    int x = rc.left;
    int y = rc.top;
    if (x + w > mi.rcWork.right) x = mi.rcWork.right - w;
    if (y + h > mi.rcWork.bottom) y = mi.rcWork.bottom - h;
    if (x < mi.rcWork.left) x = mi.rcWork.left;
    if (y < mi.rcWork.top) y = mi.rcWork.top;
    if (x != rc.left || y != rc.top || w != rc.right - rc.left || h != rc.bottom - rc.top)
        SetWindowPos(wnd, nullptr, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
}

// ------------------------------------------------------------- settings file

const wchar_t* kSettingsFileName = L"settings.txt";

std::wstring SettingsFilePath() {
    return DataFilePath(kSettingsFileName);
}

std::wstring TrimSettingsText(const std::wstring& text) {
    const size_t first = text.find_first_not_of(L" \t\r\n");
    if (first == std::wstring::npos) return std::wstring();
    const size_t last = text.find_last_not_of(L" \t\r\n");
    return text.substr(first, last - first + 1);
}

bool ParseUiScale(const std::wstring& text, float* value) {
    std::wstring s = TrimSettingsText(text);
    if (s.empty()) return false;
    for (wchar_t& c : s)
        if (c == L',') c = L'.';

    wchar_t* end = nullptr;
    const double v = wcstod(s.c_str(), &end);
    if (!end || end == s.c_str() || *end != L'\0') return false;
    if (!(v >= 0.5 && v <= 2.0)) return false;
    *value = static_cast<float>(v);
    return true;
}

bool ParseSettingsInt(const std::wstring& text, int* value) {
    const std::wstring s = TrimSettingsText(text);
    if (s.empty()) return false;
    wchar_t* end = nullptr;
    const long parsed = wcstol(s.c_str(), &end, 10);
    if (!end || end == s.c_str() || *end != L'\0') return false;
    if (parsed < -100 || parsed > 100) return false;
    *value = static_cast<int>(parsed);
    return true;
}

const wchar_t* OutputModeSettingName(int mode) {
    if (mode == kAudioOutputShared) return L"shared";
    if (mode == kAudioOutputExclusive) return L"exclusive";
    return L"auto";
}

const wchar_t* OutputModeDisplayName(int mode) {
    if (mode == kAudioOutputShared) return L"Shared";
    if (mode == kAudioOutputExclusive) return L"Exclusive";
    return L"Auto";
}

const wchar_t* OutputBackendSettingName(int backend) {
    return backend == kAudioBackendAsio ? L"asio" : L"wasapi";
}

const wchar_t* OutputBackendDisplayName(int backend) {
    return backend == kAudioBackendAsio ? L"ASIO" : L"WASAPI";
}

bool ParseOutputBackend(const std::wstring& text, int* backend) {
    const std::wstring value = TrimSettingsText(text);
    if (_wcsicmp(value.c_str(), L"wasapi") == 0) {
        *backend = kAudioBackendWasapi;
        return true;
    }
    if (_wcsicmp(value.c_str(), L"asio") == 0) {
        *backend = kAudioBackendAsio;
        return true;
    }
    return false;
}

bool ParseOutputMode(const std::wstring& text, int* mode) {
    const std::wstring value = TrimSettingsText(text);
    if (_wcsicmp(value.c_str(), L"auto") == 0) {
        *mode = kAudioOutputAuto;
        return true;
    }
    if (_wcsicmp(value.c_str(), L"shared") == 0) {
        *mode = kAudioOutputShared;
        return true;
    }
    if (_wcsicmp(value.c_str(), L"exclusive") == 0) {
        *mode = kAudioOutputExclusive;
        return true;
    }
    return false;
}

int NearestUiPreset(float value) {
    int best = 2;
    float bestDiff = 1e9f;
    for (int i = 0; i < kUiPresetCount; ++i) {
        const float diff = std::fabs(kUiPresets[i] - value);
        if (diff < bestDiff) {
            bestDiff = diff;
            best = i;
        }
    }
    return best;
}

void SaveSettings() {
    const int preset = (g_uiPreset >= 0 && g_uiPreset < kUiPresetCount) ? g_uiPreset : 2;
    const int dimDb = IsKnownDimTarget(g_dimDb) ? g_dimDb : kDefaultDimDb;
    const int autoTargetDb =
        IsKnownAutoTarget(g_autoTargetDb) ? g_autoTargetDb : kDefaultAutoTargetDb;
    std::wstring text;
    text += L"# DSplayer settings. This file may be edited manually.\r\n";
    text += L"# ui_scale: 0.70 - compact, 0.85 - normal, 1.00 - large.\r\n";
    text += L"# dim_db: selected attenuation for the -dB button.\r\n";
    text += L"# auto_target_db: target LUFS level for Auto mode.\r\n";
    text += L"# pause_seconds: delay before playback starts or resumes, 1-5.\r\n";
    text += L"# output_mode: auto, shared, or exclusive.\r\n";
    text += L"# output_device: WASAPI device ID; default selects the system device.\r\n";
    text += L"# output_backend: wasapi or asio.\r\n";
    text += L"# asio_device: ASIO driver string; empty selects the first available driver.\r\n";
    text += L"# import_directory: last directory used to add files.\r\n";
    text += L"# export_directory: last MP3 export directory.\r\n";
    wchar_t line[96];
    swprintf_s(line, L"ui_scale\t%.2f\r\n", static_cast<double>(kUiPresets[preset]));
    text += line;
    swprintf_s(line, L"dim_db\t%d\r\n", dimDb);
    text += line;
    swprintf_s(line, L"auto_target_db\t%d\r\n", autoTargetDb);
    text += line;
    swprintf_s(line, L"pause_seconds\t%d\r\n", g_delaySeconds);
    text += line;
    swprintf_s(line, L"output_mode\t%s\r\n", OutputModeSettingName(g_outputMode));
    text += line;
    text += L"output_device\t";
    text += g_outputDeviceId.empty() ? L"default" : g_outputDeviceId;
    text += L"\r\n";
    text += L"output_backend\t";
    text += OutputBackendSettingName(g_outputBackend);
    text += L"\r\n";
    text += L"asio_device\t";
    text += g_asioDeviceId;
    text += L"\r\n";
    text += L"import_directory\t";
    text += g_importDirectory;
    text += L"\r\n";
    text += L"export_directory\t";
    text += g_exportDirectory;
    text += L"\r\n";

    std::string content;
    const unsigned char bom[3] = {0xEF, 0xBB, 0xBF};
    content.append(reinterpret_cast<const char*>(bom), 3);
    content += WStrToUtf8(text);
    WriteTextFileAtomic(SettingsFilePath(), content);
}

void LoadSettings() {
    bool foundUiScale = false;
    bool foundDimDb = false;
    bool foundAutoTarget = false;
    bool foundPauseSeconds = false;
    bool foundOutputMode = false;
    bool foundOutputDevice = false;
    bool foundOutputBackend = false;
    bool foundAsioDevice = false;
    bool foundImportDirectory = false;
    bool foundExportDirectory = false;
    bool foundObsoleteKeepAwake = false;
    bool foundInvalidValue = false;
    float value = 1.0f;
    std::string content;
    if (ReadTextFileUtf8(SettingsFilePath(), content)) {
        const std::wstring text = Utf8ToWStr(content);
        size_t pos = 0;
        while (pos <= text.size()) {
            size_t eol = text.find(L'\n', pos);
            if (eol == std::wstring::npos) eol = text.size();
            std::wstring line = text.substr(pos, eol - pos);
            if (!line.empty() && line.back() == L'\r') line.pop_back();
            const size_t tab = line.find(L'\t');
            if (tab != std::wstring::npos) {
                const std::wstring key = TrimSettingsText(line.substr(0, tab));
                if (key == L"ui_scale") {
                    if (ParseUiScale(line.substr(tab + 1), &value))
                        foundUiScale = true;
                    else
                        foundInvalidValue = true;
                } else if (key == L"dim_db") {
                    int db = kDefaultDimDb;
                    if (ParseSettingsInt(line.substr(tab + 1), &db) && IsKnownDimTarget(db)) {
                        g_dimDb = db;
                        foundDimDb = true;
                    } else {
                        foundInvalidValue = true;
                    }
                } else if (key == L"auto_target_db") {
                    int db = kDefaultAutoTargetDb;
                    if (ParseSettingsInt(line.substr(tab + 1), &db) && IsKnownAutoTarget(db)) {
                        g_autoTargetDb = db;
                        foundAutoTarget = true;
                    } else {
                        foundInvalidValue = true;
                    }
                } else if (key == L"pause_seconds") {
                    int seconds = kDefaultDelaySeconds;
                    if (ParseSettingsInt(line.substr(tab + 1), &seconds) &&
                        seconds >= kMinDelaySeconds && seconds <= kMaxDelaySeconds) {
                        g_delaySeconds = seconds;
                        foundPauseSeconds = true;
                    } else {
                        foundInvalidValue = true;
                    }
                } else if (key == L"output_mode") {
                    int mode = kAudioOutputAuto;
                    if (ParseOutputMode(line.substr(tab + 1), &mode)) {
                        g_outputMode = mode;
                        foundOutputMode = true;
                    } else {
                        foundInvalidValue = true;
                    }
                } else if (key == L"output_device") {
                    const std::wstring device = TrimSettingsText(line.substr(tab + 1));
                    g_outputDeviceId = (_wcsicmp(device.c_str(), L"default") == 0)
                                           ? std::wstring() : device;
                    foundOutputDevice = true;
                } else if (key == L"output_backend") {
                    int backend = kAudioBackendWasapi;
                    if (ParseOutputBackend(line.substr(tab + 1), &backend)) {
                        g_outputBackend = backend;
                        foundOutputBackend = true;
                    } else {
                        foundInvalidValue = true;
                    }
                } else if (key == L"asio_device") {
                    g_asioDeviceId = TrimSettingsText(line.substr(tab + 1));
                    foundAsioDevice = true;
                } else if (key == L"import_directory") {
                    const std::wstring directory = TrimSettingsText(line.substr(tab + 1));
                    const DWORD attributes = directory.empty()
                        ? INVALID_FILE_ATTRIBUTES
                        : GetFileAttributesW(directory.c_str());
                    if (directory.empty()) {
                        g_importDirectory.clear();
                        foundImportDirectory = true;
                    } else if (attributes != INVALID_FILE_ATTRIBUTES &&
                               (attributes & FILE_ATTRIBUTE_DIRECTORY)) {
                        g_importDirectory = directory;
                        foundImportDirectory = true;
                    } else {
                        foundInvalidValue = true;
                    }
                } else if (key == L"export_directory") {
                    const std::wstring directory = TrimSettingsText(line.substr(tab + 1));
                    const DWORD attributes = directory.empty()
                        ? INVALID_FILE_ATTRIBUTES
                        : GetFileAttributesW(directory.c_str());
                    if (directory.empty()) {
                        g_exportDirectory.clear();
                        foundExportDirectory = true;
                    } else if (attributes != INVALID_FILE_ATTRIBUTES &&
                               (attributes & FILE_ATTRIBUTE_DIRECTORY)) {
                        g_exportDirectory = directory;
                        foundExportDirectory = true;
                    } else {
                        foundInvalidValue = true;
                    }
                } else if (key == L"keep_awake") {
                    foundObsoleteKeepAwake = true;
                }
            }
            if (eol >= text.size()) break;
            pos = eol + 1;
        }
    }

    if (foundUiScale) {
        g_uiPreset = NearestUiPreset(value);
        g_uiScale = kUiPresets[g_uiPreset];
    } else {

        g_uiPreset = 2;
        g_uiScale = 1.0f;
    }

    if (!foundDimDb) g_dimDb = kDefaultDimDb;
    if (!foundAutoTarget) g_autoTargetDb = kDefaultAutoTargetDb;
    if (!foundPauseSeconds) g_delaySeconds = kDefaultDelaySeconds;
    if (!foundOutputMode) g_outputMode = kAudioOutputAuto;
    if (!foundOutputDevice) g_outputDeviceId.clear();
    if (!foundOutputBackend) g_outputBackend = kAudioBackendWasapi;
    if (!foundAsioDevice) g_asioDeviceId.clear();
    if (!foundImportDirectory) g_importDirectory.clear();
    if (!foundExportDirectory) g_exportDirectory.clear();

    if (!foundUiScale || !foundDimDb || !foundAutoTarget || !foundPauseSeconds ||
        !foundOutputMode || !foundOutputDevice || !foundOutputBackend ||
        !foundAsioDevice ||
        std::fabs(kUiPresets[g_uiPreset] - value) > 0.011f ||
        foundObsoleteKeepAwake || foundInvalidValue) {
        SaveSettings();
    }
}

void ApplyUiPreset(int preset) {
    if (preset < 0 || preset >= kUiPresetCount) return;
    if (preset == g_uiPreset) return;

    const float oldScale = g_uiScale;
    g_uiPreset = preset;
    g_uiScale = kUiPresets[preset];
    const float factor = g_uiScale / oldScale;
    SaveSettings();

    RebuildUiMetrics();
    RebuildFonts();
    RefreshTextButtonWidths();
    ApplyFontsToControls();

    ScaleWindowClient(g_hwnd, factor);
    if (g_smpWnd) {
        SetWindowClientSize(g_smpWnd, kPadsWinW, kPadsWinH);
        LayoutSampler(g_smpWnd);
    }
    if (g_meterWnd) SetWindowClientSize(g_meterWnd, kMeterWinW, kMeterWinH);
    if (g_waveWnd) ScaleWindowClient(g_waveWnd, factor);
    if (g_settingsWnd) SetWindowClientSize(g_settingsWnd, kSettingsWinW, kSettingsWinH);

    RECT rc = {};
    GetClientRect(g_hwnd, &rc);
    LayoutControls(rc.right, rc.bottom);

    FitWindowToWorkArea(g_hwnd);
    if (g_smpWnd) FitWindowToWorkArea(g_smpWnd);
    if (g_meterWnd) FitWindowToWorkArea(g_meterWnd);
    if (g_waveWnd) FitWindowToWorkArea(g_waveWnd);
    if (g_settingsWnd) FitWindowToWorkArea(g_settingsWnd);

    InvalidateRect(g_hwnd, nullptr, TRUE);
    if (g_smpWnd) InvalidateRect(g_smpWnd, nullptr, TRUE);
    if (g_meterWnd) InvalidateRect(g_meterWnd, nullptr, TRUE);
    if (g_waveWnd) InvalidateRect(g_waveWnd, nullptr, TRUE);
    if (g_settingsWnd) InvalidateRect(g_settingsWnd, nullptr, TRUE);
}

// ---- settings

const int kOutputDeviceCmd = 42000;
const int kOutputModeCmd = 42100;
const int kOutputBackendCmd = 42200;
const int kAsioDeviceCmd = 42300;
const int kPauseSecondsCmd = 42400;

std::wstring SelectedAsioDeviceName() {
    int device = -1;
    std::wstring id;
    std::wstring name;
    if (!FindAsioOutputDevice(&device, &id, &name)) return L"No ASIO drivers";
    if (!g_asioDeviceId.empty() && _wcsicmp(id.c_str(), g_asioDeviceId.c_str()) != 0)
        return L"Driver unavailable";
    return name;
}

std::wstring SelectedOutputDeviceName() {
    if (g_outputBackend == kAudioBackendAsio) return SelectedAsioDeviceName();
    if (g_outputDeviceId.empty()) return L"System device";
    BASS_WASAPI_DEVICEINFO info = {};
    for (int i = 0; BASS_WASAPI_GetDeviceInfo(i, &info); ++i) {
        if (!(info.flags & BASS_DEVICE_ENABLED)) continue;
        if (info.flags & (BASS_DEVICE_INPUT | BASS_DEVICE_LOOPBACK)) continue;
        const std::wstring id = info.id ? Utf8ToWStr(info.id) : std::wstring();
        if (_wcsicmp(id.c_str(), g_outputDeviceId.c_str()) == 0)
            return info.name ? Utf8ToWStr(info.name) : id;
    }
    return L"Device unavailable";
}

std::wstring CurrentOutputStatus() {
    if (g_asioMode) return g_asioDeviceName + L" - ASIO";
    std::wstring text = g_outputDeviceName.empty() ? L"Default device"
                                                   : g_outputDeviceName;
    if (!g_mixerMode) {
        text += L" - direct BASS";
        return text;
    }
    text += g_wasapiExclusive ? L" - WASAPI, exclusive"
                              : L" - WASAPI, shared";
    return text;
}

bool ReinitializeAudioOutput(int fallbackMode,
                             const std::wstring& fallbackDeviceId,
                             int fallbackBackend,
                             const std::wstring& fallbackAsioDeviceId) {
    struct PadRestore {
        std::wstring path;
        QWORD position = 0;
        bool playing = false;
        bool loaded = false;
    };
    PadRestore pads[kPadCount];
    CancelLoudnessProbe();
    for (int i = 0; i < kPadCount; ++i) {
        pads[i].path = g_pads[i].path;
        pads[i].loaded = g_pads[i].stream != 0;
        if (!pads[i].loaded) continue;
        pads[i].playing = ChannelState(g_pads[i].stream) == BASS_ACTIVE_PLAYING;
        const QWORD position = ChannelPos(g_pads[i].stream);
        pads[i].position = position == static_cast<QWORD>(-1) ? 0 : position;
    }

    std::wstring trackPath;
    QWORD trackPosition = 0;
    DWORD trackState = BASS_ACTIVE_STOPPED;
    Track detachedTrack;
    const bool trackLoaded = g_channel != 0;
    const bool trackDetached = trackLoaded && g_playingTrackDetached;
    if (trackLoaded) {
        trackState = ChannelState(g_channel);
        const QWORD position = ChannelPos(g_channel);
        trackPosition = position == static_cast<QWORD>(-1) ? 0 : position;
        const Track* track = CurrentPlayingTrack();
        if (track) trackPath = track->path;
        if (trackDetached) detachedTrack = *track;
    }

    const int wavePad = g_wavePad;
    const bool waveAuditionWasPlaying = WaveAuditionPlaying();
    const double waveCursor = g_waveCursor;
    const double waveAuditionEnd = g_waveAuditionEnd;
    const WaveAuditionKind waveAuditionKind = g_waveAuditionKind;
    g_wavePad = -1;
    if (g_channel) CloseChannel();
    for (int i = 0; i < kPadCount; ++i) {
        if (g_smpWnd) KillTimer(g_smpWnd, ID_TIMER_PAD_STOP0 + i);
        PadClearStopSync(i);
        if (g_pads[i].stream) {
            if (g_pads[i].isMusic) BASS_MusicFree(g_pads[i].stream);
            else                   BASS_StreamFree(g_pads[i].stream);
        }
        g_pads[i].stream = 0;
        g_pads[i].isMusic = FALSE;
        g_pads[i].glow = false;
        g_pads[i].fading = false;
        g_padStarted[i] = 0;
        g_padPosSeen[i] = 0;
    }
    if (g_smpWnd) KillTimer(g_smpWnd, ID_TIMER_PAD_EDGE);

    FreeAudioOutput();
    bool requestedOk = InitAudioOutput(false);
    const bool usedFallback = !requestedOk && fallbackMode >= 0;
    if (usedFallback) {
        g_outputBackend = fallbackBackend;
        g_outputMode = fallbackMode;
        g_outputDeviceId = fallbackDeviceId;
        g_asioDeviceId = fallbackAsioDeviceId;
        requestedOk = InitAudioOutput(false);
    }
    if (!requestedOk) return false;

    for (int i = 0; i < kPadCount; ++i) {
        if (!pads[i].loaded || pads[i].path.empty()) continue;
        if (!PadLoadFile(i, pads[i].path)) continue;
        QWORD position = pads[i].position;
        const QWORD length = BASS_ChannelGetLength(g_pads[i].stream, BASS_POS_BYTE);
        if (length && length != static_cast<QWORD>(-1) && position >= length)
            position = 0;
        g_padPosSeen[i] = position;
        g_padStarted[i] = GetTickCount64();
        if (pads[i].playing) {
            ChannelSeek(g_pads[i].stream, position);
            if (ChannelStart(g_pads[i].stream, FALSE)) {
                PadArmStopSync(i, position);
                g_pads[i].glow = true;
            } else {
                g_pads[i].glow = false;
            }
        }
        if (g_smpPad[i]) InvalidateRect(g_smpPad[i], nullptr, FALSE);
    }
    g_wavePad = wavePad;

    if (trackLoaded && !trackPath.empty() && StartPlayback(trackPath)) {
        if (trackDetached) {
            g_detachedPlayingTrack = detachedTrack;
            g_playingTrackDetached = TRUE;
        }
        ChannelSeek(g_channel, trackPosition);
        if (trackState == BASS_ACTIVE_PAUSED) {
            ChannelPause(g_channel);
        } else if (trackState == BASS_ACTIVE_STOPPED) {
            ChannelStop(g_channel);
            ChannelSeek(g_channel, 0);
            g_idle = TRUE;
        }
        g_seekPending = FALSE;
        UpdateSeekBar();
        UpdateTimeLabel();
        RefreshPlayButton(FALSE);
    }

    if (waveAuditionWasPlaying && !WaveAuditionSource().empty()) {
        WaveStartAudition(g_waveWnd, waveAuditionKind, waveCursor,
                          waveAuditionEnd, false, false);
    }

    SetIdleTitle();
    UpdateRemoveButtonState();
    RefreshPlaylistButtons();
    SyncSamplerTimers();
    ApplyKeepAwake();
    if (g_hwnd) InvalidateRect(g_hwnd, nullptr, FALSE);
    if (g_waveWnd) InvalidateRect(g_waveWnd, nullptr, TRUE);
    return !usedFallback;
}

void ApplyAudioOutputSetting(int previousMode,
                             const std::wstring& previousDeviceId,
                             int previousBackend,
                             const std::wstring& previousAsioDeviceId) {
    if (g_waveExportBusy) {
        g_outputBackend = previousBackend;
        g_outputMode = previousMode;
        g_outputDeviceId = previousDeviceId;
        g_asioDeviceId = previousAsioDeviceId;
        MessageBoxW(g_settingsWnd,
                    L"The audio output cannot be changed during MP3 export.\n"
                    L"Wait for the export to finish and select the device again.",
                    L"Audio", MB_OK | MB_ICONINFORMATION);
        if (g_settingsWnd) InvalidateRect(g_settingsWnd, nullptr, TRUE);
        return;
    }

    const bool switched = ReinitializeAudioOutput(
        previousMode, previousDeviceId, previousBackend, previousAsioDeviceId);
    SaveSettings();
    if (!switched) {
        MessageBoxW(g_settingsWnd, L"Could not switch the audio output.\n"
                                   L"Make sure the selected device is connected.\n"
                                   L"The previous available output has been restored.",
                    L"Audio", MB_ICONWARNING | MB_OK);
    }
    if (g_settingsWnd) InvalidateRect(g_settingsWnd, nullptr, TRUE);
    if (g_hwnd) InvalidateRect(g_hwnd, nullptr, FALSE);
}

void ShowOutputDeviceMenu(HWND hwnd) {
    const int previousMode = g_outputMode;
    const std::wstring previousDeviceId = g_outputDeviceId;
    const int previousBackend = g_outputBackend;
    const std::wstring previousAsioDeviceId = g_asioDeviceId;
    HMENU menu = CreatePopupMenu();
    if (!menu) return;

    std::vector<std::wstring> ids;
    if (g_outputBackend == kAudioBackendAsio) {
        BASS_ASIO_DEVICEINFO info = {};
        for (int i = 0; BASS_ASIO_GetDeviceInfo(i, &info); ++i) {
            const std::wstring driver = info.driver ? Utf8ToWStr(info.driver) : std::wstring();
            const std::wstring name = info.name ? Utf8ToWStr(info.name) : driver;
            const std::wstring id = driver.empty() ? name : driver;
            const bool checked = g_asioDeviceId.empty()
                ? ids.empty()
                : _wcsicmp(id.c_str(), g_asioDeviceId.c_str()) == 0;
            AppendMenuW(menu, MF_STRING | (checked ? MF_CHECKED : 0),
                        static_cast<UINT_PTR>(kAsioDeviceCmd + ids.size()), name.c_str());
            ids.push_back(id);
        }
        if (ids.empty()) {
            AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, L"No ASIO drivers");
        }
    } else {
        AppendMenuW(menu, MF_STRING | (g_outputDeviceId.empty() ? MF_CHECKED : 0),
                    static_cast<UINT_PTR>(kOutputDeviceCmd), L"System device");

        BASS_WASAPI_DEVICEINFO info = {};
        for (int i = 0; BASS_WASAPI_GetDeviceInfo(i, &info); ++i) {
            if (!(info.flags & BASS_DEVICE_ENABLED)) continue;
            if (info.flags & (BASS_DEVICE_INPUT | BASS_DEVICE_LOOPBACK)) continue;
            const std::wstring id = info.id ? Utf8ToWStr(info.id) : std::wstring();
            const std::wstring name = info.name ? Utf8ToWStr(info.name) : id;
            const bool checked = !g_outputDeviceId.empty() &&
                                 _wcsicmp(id.c_str(), g_outputDeviceId.c_str()) == 0;
            AppendMenuW(menu, MF_STRING | (checked ? MF_CHECKED : 0),
                        static_cast<UINT_PTR>(kOutputDeviceCmd + 1 + ids.size()), name.c_str());
            ids.push_back(id);
        }
    }

    POINT pt = {};
    GetCursorPos(&pt);
    SetForegroundWindow(hwnd);
    const int cmd = static_cast<int>(TrackPopupMenu(
        menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY, pt.x, pt.y, 0, hwnd, nullptr));
    DestroyMenu(menu);
    if (cmd <= 0) return;
    if (g_outputBackend == kAudioBackendAsio) {
        if (cmd < kAsioDeviceCmd || cmd >= kAsioDeviceCmd + static_cast<int>(ids.size()))
            return;
        g_asioDeviceId = ids[cmd - kAsioDeviceCmd];
    } else if (cmd == kOutputDeviceCmd) {
        g_outputDeviceId.clear();
    } else if (cmd > kOutputDeviceCmd &&
               cmd <= kOutputDeviceCmd + static_cast<int>(ids.size())) {
        g_outputDeviceId = ids[cmd - kOutputDeviceCmd - 1];
    } else {
        return;
    }
    ApplyAudioOutputSetting(previousMode, previousDeviceId, previousBackend, previousAsioDeviceId);
}

void ShowOutputModeMenu(HWND hwnd) {
    const int previousMode = g_outputMode;
    const std::wstring previousDeviceId = g_outputDeviceId;
    const int previousBackend = g_outputBackend;
    const std::wstring previousAsioDeviceId = g_asioDeviceId;
    HMENU menu = CreatePopupMenu();
    if (!menu) return;
    const wchar_t* names[3] = {L"Auto", L"Shared", L"Exclusive"};
    for (int i = 0; i < 3; ++i) {
        AppendMenuW(menu, MF_STRING | (g_outputMode == i ? MF_CHECKED : 0),
                    static_cast<UINT_PTR>(kOutputModeCmd + i), names[i]);
    }
    POINT pt = {};
    GetCursorPos(&pt);
    SetForegroundWindow(hwnd);
    const int cmd = static_cast<int>(TrackPopupMenu(
        menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY, pt.x, pt.y, 0, hwnd, nullptr));
    DestroyMenu(menu);
    if (cmd < kOutputModeCmd || cmd >= kOutputModeCmd + 3) return;
    g_outputMode = cmd - kOutputModeCmd;
    ApplyAudioOutputSetting(previousMode, previousDeviceId, previousBackend, previousAsioDeviceId);
}

void ShowOutputBackendMenu(HWND hwnd) {
    const int previousMode = g_outputMode;
    const std::wstring previousDeviceId = g_outputDeviceId;
    const int previousBackend = g_outputBackend;
    const std::wstring previousAsioDeviceId = g_asioDeviceId;
    HMENU menu = CreatePopupMenu();
    if (!menu) return;
    AppendMenuW(menu, MF_STRING | (g_outputBackend == kAudioBackendWasapi ? MF_CHECKED : 0),
                static_cast<UINT_PTR>(kOutputBackendCmd), L"WASAPI");
    AppendMenuW(menu, MF_STRING | (g_outputBackend == kAudioBackendAsio ? MF_CHECKED : 0),
                static_cast<UINT_PTR>(kOutputBackendCmd + 1), L"ASIO");
    POINT pt = {};
    GetCursorPos(&pt);
    SetForegroundWindow(hwnd);
    const int cmd = static_cast<int>(TrackPopupMenu(
        menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY, pt.x, pt.y, 0, hwnd, nullptr));
    DestroyMenu(menu);
    if (cmd == kOutputBackendCmd) g_outputBackend = kAudioBackendWasapi;
    else if (cmd == kOutputBackendCmd + 1) g_outputBackend = kAudioBackendAsio;
    else return;
    ApplyAudioOutputSetting(previousMode, previousDeviceId, previousBackend, previousAsioDeviceId);
}

void SetPauseSeconds(int seconds) {
    if (seconds < kMinDelaySeconds || seconds > kMaxDelaySeconds) return;
    g_delaySeconds = seconds;
    if (g_pendingStart) g_pendingTicks = DelayTicks();
    SaveSettings();
    if (g_chkDelay) InvalidateRect(g_chkDelay, nullptr, TRUE);
    if (g_settingsWnd) InvalidateRect(g_settingsWnd, nullptr, FALSE);
}

void ShowPauseDurationMenu(HWND hwnd) {
    HMENU menu = CreatePopupMenu();
    if (!menu) return;
    for (int seconds = kMinDelaySeconds; seconds <= kMaxDelaySeconds; ++seconds) {
        AppendMenuW(menu, MF_STRING | (g_delaySeconds == seconds ? MF_CHECKED : 0),
                    static_cast<UINT_PTR>(kPauseSecondsCmd + seconds),
                    PauseDurationOptionText(seconds));
    }
    POINT pt = {};
    GetCursorPos(&pt);
    SetForegroundWindow(hwnd);
    const int cmd = static_cast<int>(TrackPopupMenu(
        menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY, pt.x, pt.y, 0, hwnd, nullptr));
    DestroyMenu(menu);
    if (cmd < kPauseSecondsCmd + kMinDelaySeconds ||
        cmd > kPauseSecondsCmd + kMaxDelaySeconds) {
        return;
    }
    SetPauseSeconds(cmd - kPauseSecondsCmd);
}

const int kSettingsTabCount = 4;
int g_settingsTab = 0;
int g_settingsHot = -1;
bool g_settingsTrackingMouse = false;
const int kSettingsHotRowBase = 100;

RECT SettingsNavRect(const RECT&, int index) {
    const int left = S(12);
    const int top = kCaptionH + S(22) + index * (kSettingsNavH + S(6));
    return {left, top, kSettingsNavW - S(12), top + kSettingsNavH};
}

RECT SettingsContentRect(const RECT& client) {
    return {kSettingsNavW + S(20), kCaptionH + S(18),
            client.right - S(20), client.bottom - S(18)};
}

RECT SettingsGroupRect(const RECT& client) {
    const RECT content = SettingsContentRect(client);
    return {content.left, content.top, content.right, content.top + S(30)};
}

RECT SettingsRowRect(const RECT& client, int index) {
    const RECT content = SettingsContentRect(client);
    const RECT group = SettingsGroupRect(client);
    const int top = group.bottom + S(8) + index * (kSettingsRowH + S(8));
    return {content.left, top, content.right, top + kSettingsRowH};
}

int SettingsRowCount(int tab) {
    if (tab == 0) return kUiPresetCount;
    if (tab == 1) return 2;
    if (tab == 2) return 4;
    return 1;
}

bool SettingsRowEnabled(int tab, int row) {
    if (tab != 2 || row != 3) {
        return tab != 2 || row != 2 || g_outputBackend != kAudioBackendAsio;
    }
    return false;
}

int SettingsHitTest(HWND hwnd, const POINT& pt) {
    RECT client = {};
    GetClientRect(hwnd, &client);
    for (int i = 0; i < kSettingsTabCount; ++i) {
        const RECT nav = SettingsNavRect(client, i);
        if (PtInRect(&nav, pt)) return i;
    }

    for (int i = 0; i < SettingsRowCount(g_settingsTab); ++i) {
        if (!SettingsRowEnabled(g_settingsTab, i)) continue;
        const RECT row = SettingsRowRect(client, i);
        if (PtInRect(&row, pt)) return kSettingsHotRowBase + i;
    }
    return -1;
}

void DrawSettingsNavItem(HDC dc, const RECT& item, const wchar_t* text,
                         bool active, bool hot) {
    if (active)
        FillSolidRect(dc, item, kMainGlowBack);
    else if (hot)
        FillSolidRect(dc, item, kMainBtnFill);

    if (active) {
        FrameSolidRect(dc, item, kMainGlowEdge);
        RECT accent = {item.left, item.top + S(6),
                       item.left + S(3), item.bottom - S(6)};
        FillSolidRect(dc, accent, kMainGlowEdge);
    } else if (hot) {
        FrameSolidRect(dc, item, kMainLine);
    }

    RECT label = item;
    label.left += S(16);
    SetTextColor(dc, active ? kMainGlowText : kMainText);
    DrawTextW(dc, text, -1, &label,
              DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_NOPREFIX);
}

void DrawSettingsOptionRow(HDC dc, const RECT& row, const wchar_t* text,
                           bool selected, bool hot) {
    FillSolidRect(dc, row, selected ? kMainGlowBack
                                     : (hot ? kMainPressed : kMainBtnFill));
    FrameSolidRect(dc, row, selected ? kMainGlowEdge
                                      : (hot ? kMainBtnEdge : kMainLine));

    RECT label = row;
    label.left += S(16);
    label.right -= S(42);
    SetTextColor(dc, selected ? kMainGlowText : kMainText);
    DrawTextW(dc, text, -1, &label,
              DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_NOPREFIX |
              DT_END_ELLIPSIS);

    const int d = (std::max)(S(9), S(10));
    const int cy = (row.top + row.bottom) / 2;
    const RECT dot = {row.right - S(16) - d, cy - d / 2,
                      row.right - S(16), cy - d / 2 + d};
    FillSolidCircle(dc, dot, selected ? kMainGlowEdge : kMainBtnOff,
                    selected ? kMainGlowEdge : kMainBtnEdge);
}

void DrawSettingsValueRow(HDC dc, const RECT& row, const wchar_t* label,
                          const std::wstring& value, bool enabled, bool hot) {
    FillSolidRect(dc, row, enabled ? (hot ? kMainPressed : kMainBtnFill)
                                    : kMainBtnOff);
    FrameSolidRect(dc, row, enabled ? (hot ? kMainBtnEdge : kMainLine)
                                     : kMainBtnOffEdge);

    const int labelWidth = S(140);
    const int valueLeft = row.left + labelWidth;
    RECT valueRc = {valueLeft, row.top + S(5),
                    row.right - S(8), row.bottom - S(5)};
    RECT labelRc = {row.left + S(16), row.top, valueLeft - S(10), row.bottom};
    RECT columnLine = {valueLeft - S(7), row.top + S(8),
                       valueLeft - S(6), row.bottom - S(8)};

    FillSolidRect(dc, columnLine, kMainLine);

    SetTextColor(dc, enabled ? kMainText : kMainDimText);
    DrawTextW(dc, label, -1, &labelRc,
              DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_NOPREFIX |
              DT_END_ELLIPSIS);

    FillSolidRect(dc, valueRc, enabled ? kMainGlowBack : kMainBtnFill);
    FrameSolidRect(dc, valueRc, enabled ? kMainGlowEdge : kMainBtnEdge);
    RECT valueText = valueRc;
    valueText.left += S(9);
    valueText.right -= S(9);
    SetTextColor(dc, enabled ? kMainGlowText : kMainDimText);
    DrawTextW(dc, value.c_str(), -1, &valueText,
              DT_SINGLELINE | DT_VCENTER | DT_RIGHT | DT_NOPREFIX |
              DT_END_ELLIPSIS);
}

void DrawSettingsWindow(HWND hwnd, HDC dc) {
    RECT rc = {};
    GetClientRect(hwnd, &rc);
    FillSolidRect(dc, rc, kMainBack);

    RECT navBack = {rc.left, kCaptionH, kSettingsNavW, rc.bottom};
    FillSolidRect(dc, navBack, kMainListBack);
    RECT navLine = {kSettingsNavW - 1, kCaptionH, kSettingsNavW, rc.bottom};
    FillSolidRect(dc, navLine, kMainLine);

    HFONT oldFont = static_cast<HFONT>(SelectObject(dc, UiFont()));
    SetBkMode(dc, TRANSPARENT);

    const wchar_t* tabs[kSettingsTabCount] = {
        L"View", L"Levels", L"Audio", L"Playback"};
    for (int i = 0; i < kSettingsTabCount; ++i) {
        DrawSettingsNavItem(dc, SettingsNavRect(rc, i), tabs[i],
                            i == g_settingsTab, g_settingsHot == i);
    }

    RECT group = SettingsGroupRect(rc);
    const wchar_t* groupNames[kSettingsTabCount] = {
        L"Interface scale", L"Target levels", L"Output settings",
        L"Playback controls"};
    SetTextColor(dc, kMainDimText);
    DrawTextW(dc, groupNames[g_settingsTab], -1, &group,
              DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_NOPREFIX);

    if (g_settingsTab == 0) {
        for (int i = 0; i < kUiPresetCount; ++i) {
            DrawSettingsOptionRow(dc, SettingsRowRect(rc, i), kUiPresetNames[i],
                                  i == g_uiPreset,
                                  g_settingsHot == kSettingsHotRowBase + i);
        }
    } else if (g_settingsTab == 1) {
        const wchar_t* labels[2] = {L"Dim level", L"Auto LUFS level"};
        const int values[2] = {g_dimDb, g_autoTargetDb};
        for (int i = 0; i < 2; ++i) {
            wchar_t valueText[32];
            swprintf_s(valueText, i == 0 ? L"%d dB" : L"%d LUFS", values[i]);
            DrawSettingsValueRow(dc, SettingsRowRect(rc, i), labels[i],
                                 valueText, true,
                                 g_settingsHot == kSettingsHotRowBase + i);
        }
    } else if (g_settingsTab == 2) {
        const wchar_t* labels[4] = {
            L"Output", L"Device", L"WASAPI mode", L"Current output"};
        const bool wasapi = g_outputBackend != kAudioBackendAsio;
        std::wstring values[4] = {
            OutputBackendDisplayName(g_outputBackend),
            SelectedOutputDeviceName(),
            wasapi ? std::wstring(OutputModeDisplayName(g_outputMode)) : std::wstring(L"-"),
            CurrentOutputStatus()
        };
        const bool interactive[4] = {true, true, wasapi, false};
        for (int i = 0; i < 4; ++i) {
            DrawSettingsValueRow(dc, SettingsRowRect(rc, i), labels[i],
                                 values[i], interactive[i],
                                 g_settingsHot == kSettingsHotRowBase + i);
        }
    } else {
        DrawSettingsValueRow(dc, SettingsRowRect(rc, 0), L"Pause",
                             PauseDurationOptionText(g_delaySeconds), true,
                             g_settingsHot == kSettingsHotRowBase);
    }

    SelectObject(dc, oldFont);
    DrawCustomCaption(hwnd, dc, rc);
}

LRESULT CALLBACK SettingsProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    LRESULT frameResult = 0;
    if (SkipSystemFramePaint(msg, frameResult)) return frameResult;
    switch (msg) {
    case WM_CREATE:
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_NCHITTEST:
        return CustomCaptionHitTest(hwnd, lParam);

    case WM_PAINT: {
        PAINTSTRUCT ps = {};
        HDC dc = BeginPaint(hwnd, &ps);
        DrawSettingsWindow(hwnd, dc);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_LBUTTONDOWN: {
        if (HandleCaptionMouseDown(hwnd, lParam)) return 0;
        const POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        const int hit = SettingsHitTest(hwnd, pt);
        if (hit >= 0 && hit < kSettingsTabCount) {
            g_settingsTab = hit;
            g_settingsHot = -1;
            InvalidateRect(hwnd, nullptr, TRUE);
            return 0;
        }

        if (hit >= kSettingsHotRowBase) {
            const int row = hit - kSettingsHotRowBase;
            if (g_settingsTab == 0) {
                ApplyUiPreset(row);
            } else if (g_settingsTab == 1) {
                if (row == 0) ShowDimTargetMenu(hwnd);
                else ShowAutoTargetMenu(hwnd);
            } else if (g_settingsTab == 2) {
                if (row == 0) {
                    ShowOutputBackendMenu(hwnd);
                } else if (row == 1) {
                    ShowOutputDeviceMenu(hwnd);
                } else if (row == 2) {
                    ShowOutputModeMenu(hwnd);
                }
            } else {
                ShowPauseDurationMenu(hwnd);
            }
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        return 0;
    }

    case WM_MOUSEMOVE: {
        const POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        const int hot = SettingsHitTest(hwnd, pt);
        if (hot != g_settingsHot) {
            g_settingsHot = hot;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        if (!g_settingsTrackingMouse) {
            TRACKMOUSEEVENT track = {};
            track.cbSize = sizeof(track);
            track.dwFlags = TME_LEAVE;
            track.hwndTrack = hwnd;
            g_settingsTrackingMouse = TrackMouseEvent(&track) != FALSE;
        }
        return 0;
    }

    case WM_MOUSELEAVE:
        g_settingsTrackingMouse = false;
        if (g_settingsHot >= 0) {
            g_settingsHot = -1;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;

    case WM_SETCURSOR:
        if (LOWORD(lParam) == HTCLIENT) {
            POINT pt = {};
            GetCursorPos(&pt);
            ScreenToClient(hwnd, &pt);
            const bool interactive = SettingsHitTest(hwnd, pt) >= 0;
            SetCursor(LoadCursorW(nullptr, interactive ? IDC_HAND : IDC_ARROW));
            return TRUE;
        }
        break;

    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) {
            ShowWindow(hwnd, SW_HIDE);
            return 0;
        }
        if (wParam == VK_UP || wParam == VK_DOWN) {
            const int delta = wParam == VK_DOWN ? 1 : kSettingsTabCount - 1;
            g_settingsTab = (g_settingsTab + delta) % kSettingsTabCount;
            g_settingsHot = -1;
            InvalidateRect(hwnd, nullptr, TRUE);
            return 0;
        }
        break;

    case WM_SYSCOMMAND:
        if ((wParam & 0xFFF0) == SC_MINIMIZE) {
            SendMessageW(hwnd, WM_CLOSE, 0, 0);
            return 0;
        }
        break;

    case WM_CLOSE:
        ShowWindow(hwnd, SW_HIDE);
        return 0;

    case WM_DESTROY:
        g_settingsWnd = nullptr;
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void OpenSettingsWindow() {
    if (!g_settingsWnd) {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = SettingsProc;
        wc.hInstance = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(g_hwnd, GWLP_HINSTANCE));
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = MainBackBrush();
        wc.lpszClassName = L"BassSettingsWindow";
        wc.hIcon = PlayerIcon(GetSystemMetrics(SM_CXICON));
        wc.hIconSm = PlayerIcon(GetSystemMetrics(SM_CXSMICON));
        RegisterClassExW(&wc);

        RECT frame = {0, 0, kSettingsWinW, kSettingsWinH};
        const DWORD style = WS_POPUP;
        AdjustWindowRect(&frame, style, FALSE);
        RECT mainRc = {};
        GetWindowRect(g_hwnd, &mainRc);
        const int x = mainRc.left + ((mainRc.right - mainRc.left) - (frame.right - frame.left)) / 2;
        const int y = mainRc.top + ((mainRc.bottom - mainRc.top) - (frame.bottom - frame.top)) / 2;
        g_settingsWnd = CreateWindowExW(0, wc.lpszClassName, L"Settings",
            style, x, y, frame.right - frame.left, frame.bottom - frame.top,
            g_hwnd, nullptr, wc.hInstance, nullptr);
        if (!g_settingsWnd) return;
        FitWindowToWorkArea(g_settingsWnd);
    }

    ShowWindow(g_settingsWnd, IsIconic(g_settingsWnd) ? SW_RESTORE : SW_SHOW);
    SetForegroundWindow(g_settingsWnd);
    SetFocus(g_settingsWnd);
    InvalidateRect(g_settingsWnd, nullptr, TRUE);
}

void ToggleSettingsWindow() {
    if (g_settingsWnd && IsWindowVisible(g_settingsWnd)) {
        if (IsIconic(g_settingsWnd)) {
            ShowWindow(g_settingsWnd, SW_RESTORE);
            SetForegroundWindow(g_settingsWnd);
            return;
        }
        SendMessageW(g_settingsWnd, WM_CLOSE, 0, 0);
        return;
    }
    OpenSettingsWindow();
}

void DrawGrip(HDC dc, const RECT& client) {
    HPEN pen = SolidPen(kMainGrip);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    for (int i = 1; i <= 3; ++i) {
        const int d = S(i * 5);
        MoveToEx(dc, client.right - S(3) - d, client.bottom - S(3), nullptr);
        LineTo(dc, client.right - S(3), client.bottom - S(3) - d);
    }
    SelectObject(dc, oldPen);
}

void DrawListFrame(HDC dc) {
    if (!g_list) return;
    RECT rc = {};
    GetWindowRect(g_list, &rc);
    POINT pts[2] = {{rc.left, rc.top}, {rc.right, rc.bottom}};

    ScreenToClient(g_hwnd, &pts[0]);
    ScreenToClient(g_hwnd, &pts[1]);
    const RECT frame = {pts[0].x - 1, pts[0].y - 1, pts[1].x + 1, pts[1].y + 1};
    FrameSolidRect(dc, frame, kMainLine);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    LRESULT frameResult = 0;
    if (SkipSystemFramePaint(msg, frameResult)) return frameResult;
    switch (msg) {
    case WM_CREATE:
        g_hwnd = hwnd;
        g_callbackHwnd.store(hwnd, std::memory_order_release);
        DisableSystemWindowFrame(hwnd);
        CreateControls(hwnd, reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(hwnd, GWLP_HINSTANCE)));
        SetTimer(hwnd, ID_TIMER_UPDATE, TIMER_PERIOD_MS, nullptr);
        return 0;

    case WM_SIZE:
        if (wParam != SIZE_MINIMIZED) {
            LayoutControls(LOWORD(lParam), HIWORD(lParam));
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;

    case WM_ACTIVATE:
        if (LOWORD(wParam) != WA_INACTIVE)
            ReleaseWindowTreeCapture(g_waveWnd);
        break;

    case WM_NCCALCSIZE:
        if (wParam) return 0;
        break;

    case WM_GETMINMAXINFO: {
        MINMAXINFO* mmi = reinterpret_cast<MINMAXINFO*>(lParam);

        const int minW = (std::max)(S(590), BottomRowWidth() + kMargin);
        mmi->ptMinTrackSize.x = minW;
        mmi->ptMinTrackSize.y = kCaptionH + S(470);
        return 0;
    }

    case WM_PAINT: {
        PAINTSTRUCT ps = {};
        HDC dc = BeginPaint(hwnd, &ps);
        RECT rc = {};
        GetClientRect(hwnd, &rc);
        DrawListFrame(dc);
        DrawGrip(dc, rc);
        DrawCustomCaption(hwnd, dc, rc);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_LBUTTONDOWN:
        if (HandleCaptionMouseDown(hwnd, lParam)) return 0;
        break;

    case WM_CTLCOLORSTATIC: {
        HDC dc = reinterpret_cast<HDC>(wParam);
        SetBkColor(dc, kMainBack);
        SetTextColor(dc, (reinterpret_cast<HWND>(lParam) == g_lblTime) ? kMainDimText : kMainText);
        return reinterpret_cast<LRESULT>(MainBackBrush());
    }

    case WM_CTLCOLORLISTBOX: {
        HDC dc = reinterpret_cast<HDC>(wParam);
        SetBkColor(dc, kMainListBack);
        SetTextColor(dc, kMainText);
        return reinterpret_cast<LRESULT>(MainListBrush());
    }

    case WM_NCHITTEST: {
        const LRESULT hit = CustomCaptionHitTest(hwnd, lParam, true);
        if (hit == HTCLIENT) {
            POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            ScreenToClient(hwnd, &pt);
            RECT rc = {};
            GetClientRect(hwnd, &rc);
            if (pt.x >= rc.right - kGripSize && pt.y >= rc.bottom - kGripSize)
                return HTBOTTOMRIGHT;
        }
        return hit;
    }

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case ID_BTN_ADD:    AddFilesDialog(); return 0;
        case ID_BTN_REMOVE: RemoveSelected(); return 0;
        case ID_BTN_CLEAR:  ClearPlaylist(); return 0;
        case ID_BTN_SAMPLER: ToggleSamplerWindow(); return 0;
        case ID_BTN_METER:   ToggleMeterWindow(); return 0;
        case ID_BTN_SETTINGS: ToggleSettingsWindow(); return 0;
        case ID_BTN_PLAYLIST1:
        case ID_BTN_PLAYLIST2:
        case ID_BTN_PLAYLIST3:
            SwitchPlaylist(LOWORD(wParam) - ID_BTN_PLAYLIST1);
            return 0;
        case ID_BTN_PLAY:   TogglePlayPause(); return 0;
        case ID_BTN_STOP:   StopPlayback(); return 0;
        case ID_BTN_PREV:   StepTrack(-1); return 0;
        case ID_BTN_NEXT:   StepTrack(+1); return 0;
        case ID_CHK_NEXT:
            g_autoNext = !g_autoNext;
            InvalidateRect(g_chkNext, nullptr, FALSE);
            return 0;
        case ID_CHK_DELAY:
            g_delayOn = !g_delayOn;
            InvalidateRect(g_chkDelay, nullptr, FALSE);
            return 0;
        case ID_PLAYLIST:
            if (HIWORD(wParam) == LBN_DBLCLK) {
                const int sel = static_cast<int>(SendMessageW(g_list, LB_GETCURSEL, 0, 0));
                if (sel != LB_ERR) RequestPlayIndex(g_activePlaylist, sel);
            } else if (HIWORD(wParam) == LBN_SELCHANGE) {

                if (!g_channel && !g_pendingStart) {
                    const int sel = static_cast<int>(SendMessageW(g_list, LB_GETCURSEL, 0, 0));
                    if (sel != LB_ERR &&
                        sel != g_playlists[g_activePlaylist].currentIndex) {
                        SetReadyTrack(g_activePlaylist, sel);
                    }
                }
                UpdateRemoveButtonState();
            }
            return 0;
        }
        break;

    case WM_APP_PAD_STOP:
        HandlePadStopMessage(static_cast<int>(wParam),
                             static_cast<ULONG_PTR>(lParam));
        return 0;

    case WM_MEASUREITEM: {
        LPMEASUREITEMSTRUCT mis = reinterpret_cast<LPMEASUREITEMSTRUCT>(lParam);
        if (mis->CtlType == ODT_LISTBOX && mis->CtlID == ID_PLAYLIST) {
            mis->itemHeight = static_cast<UINT>(ListItemHeight());
            return TRUE;
        }
        if (mis->CtlType == ODT_MENU && g_playlistMenuOpen) {
            MeasurePlaylistMenuItem(mis);
            return TRUE;
        }
        break;
    }

    case WM_DRAWITEM: {
        LPDRAWITEMSTRUCT dis = reinterpret_cast<LPDRAWITEMSTRUCT>(lParam);
        if (dis->CtlType == ODT_MENU && g_playlistMenuOpen) {
            DrawPlaylistMenuItem(dis);
            return TRUE;
        }
        if (dis->CtlType == ODT_LISTBOX && dis->CtlID == ID_PLAYLIST) {
            DrawPlaylistItem(dis);
            return TRUE;
        }
        if (dis->CtlType == ODT_BUTTON &&
            (dis->CtlID == ID_BTN_PLAY || dis->CtlID == ID_BTN_STOP ||
             dis->CtlID == ID_BTN_PREV || dis->CtlID == ID_BTN_NEXT ||
             dis->CtlID == ID_BTN_ADD  || dis->CtlID == ID_BTN_REMOVE ||
             dis->CtlID == ID_BTN_SETTINGS)) {
            DrawIconButton(dis);
            return TRUE;
        }
        if (dis->CtlType == ODT_BUTTON &&
            (dis->CtlID == ID_CHK_NEXT || dis->CtlID == ID_CHK_DELAY)) {
            DrawToggleButton(dis);
            return TRUE;
        }
        if (dis->CtlType == ODT_BUTTON && dis->CtlID == ID_BTN_CLEAR) {
            DrawTextButton(dis, FALSE, L"Clear");
            return TRUE;
        }
        if (dis->CtlType == ODT_BUTTON && dis->CtlID == ID_BTN_SAMPLER) {
            DrawTextButton(dis, g_smpGlow, L"Sampler");
            return TRUE;
        }
        if (dis->CtlType == ODT_BUTTON && dis->CtlID == ID_BTN_METER) {

            const BOOL dimOverAuto = (g_autoVolume != FALSE) && (g_dimOn != FALSE);
            DrawTextButton(dis, g_autoVolume, L"Level", dimOverAuto);
            return TRUE;
        }
        if (dis->CtlType == ODT_BUTTON && dis->CtlID >= ID_BTN_PLAYLIST1 &&
            dis->CtlID <= ID_BTN_PLAYLIST3) {
            const int playlist = static_cast<int>(dis->CtlID) - ID_BTN_PLAYLIST1;
            const wchar_t* text = (playlist == 0) ? L"1" : (playlist == 1) ? L"2" : L"3";
            const BOOL visible = (g_activePlaylist == playlist);

            const BOOL sounding = (g_playPlaylist == playlist) && PlaylistSoundActive();
            DrawTextButton(dis, visible || sounding, text, !visible && sounding);
            return TRUE;
        }
        break;
    }

    case WM_HSCROLL: {
        const HWND source = reinterpret_cast<HWND>(lParam);
        const int code = LOWORD(wParam);
        if (source == g_seek) {
            if (code == TB_THUMBTRACK) {
                g_dragging = TRUE;
                UpdateTimeLabel();
            } else {
                g_dragging = FALSE;
                SeekToSlider();
                UpdateTimeLabel();
            }
        } else if (source == g_vol) {

            SetVolume(static_cast<int>(SendMessageW(g_vol, TBM_GETPOS, 0, 0)), FALSE, TRUE);
        }
        return 0;
    }

    case WM_TIMER:
        if (wParam == ID_TIMER_UPDATE) {
            if (g_pendingStart) {
                if (--g_pendingTicks <= 0) CompletePendingStart();
                else UpdateTimeLabel();
            } else if (g_channel) {
                const DWORD state = ChannelState(g_channel);
                if (state == BASS_ACTIVE_STOPPED) {
                    if (!g_idle) {

                        const int next = AutoNextEnabled() ? AutoNextIndex() : -1;
                        if (next >= 0) {

                            RequestPlayIndex(g_playPlaylist, next);
                        } else {
                            g_idle = TRUE;
                            g_seekPending = FALSE;
                            SendMessageW(g_seek, TBM_SETPOS, TRUE, SLIDER_MAX);
                            UpdateTimeLabel();
                        }
                    }
                } else {
                    g_idle = FALSE;
                    UpdateSeekBar();
                    UpdateTimeLabel();
                }
            }
            UpdateRemoveButtonState();
            RefreshPlayButton(TRUE);
            SyncPlaylistButtons();
            ApplyKeepAwake();

            BOOL padPlaying = FALSE;
            for (int i = 0; i < kPadCount; ++i) {
                if (g_pads[i].stream &&
                    ChannelState(g_pads[i].stream) == BASS_ACTIVE_PLAYING) {
                    padPlaying = TRUE;
                    break;
                }
            }
            if (padPlaying != g_smpGlow) {
                g_smpGlow = padPlaying;
                if (g_btnSampler) InvalidateRect(g_btnSampler, nullptr, FALSE);
            }

            PlaylistState& visible = g_playlists[g_activePlaylist];
            if (visible.probeIndex < visible.tracks.size()) {
                Track& track = visible.tracks[visible.probeIndex];
                const int row = static_cast<int>(visible.probeIndex);
                BOOL changed = FALSE;
                if (track.missing) {
                    if (FileExists(track.path)) {
                        track.missing = FALSE;
                        track.length = ProbeLength(track.path);
                        changed = TRUE;
                    }
                } else if (!(track.length > 0.0)) {
                    track.length = ProbeLength(track.path);
                    changed = TRUE;
                }
                ++visible.probeIndex;
                if (changed) InvalidatePlaylistRow(row);
            }

            StepLoudnessProbe();
        }
        return 0;

    case WM_DESTROY:
        g_callbackHwnd.store(nullptr, std::memory_order_release);
        KillTimer(hwnd, ID_TIMER_UPDATE);
        ReleaseKeepAwake();
        if (g_settingsWnd) DestroyWindow(g_settingsWnd);
        if (g_smpWnd) DestroyWindow(g_smpWnd);
        if (g_meterWnd) DestroyWindow(g_meterWnd);
        if (g_waveWnd) DestroyWindow(g_waveWnd);
        SaveSettings();
        SaveSampler();
        SaveLoudnessDb();
        CloseChannel();
        CancelLoudnessProbe();
        FreeAudioOutput();
        if (g_font) DeleteObject(g_font);
        if (g_listFont) DeleteObject(g_listFont);
        if (g_labelFont) DeleteObject(g_labelFont);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

}

int RunApplication(HINSTANCE inst, PWSTR, int showCmd) {
    SetProcessDPIAware();
    LoadSettings();
    RebuildUiMetrics();
    RefreshTextButtonWidths();

    INITCOMMONCONTROLSEX icc = {};
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_BAR_CLASSES;
    InitCommonControlsEx(&icc);

    if (!InitAudioOutput()) {
        wchar_t msg[200];
        swprintf_s(msg, L"Could not initialize audio output.\nError code: %lu\n\n"
                        L"Make sure the x64 versions of bass.dll, bassloud.dll,\n"
                        L"bassmix.dll, basswasapi.dll, and bassasio.dll\n"
                        L"are located next to the .exe.",
            BASS_ErrorGetCode());
        MessageBoxW(nullptr, msg, L"Error", MB_ICONERROR | MB_OK);
        return 1;
    }

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = MainBackBrush();
    wc.lpszClassName = L"BassPlayerWindow";
    wc.hIcon = PlayerIcon(GetSystemMetrics(SM_CXICON));
    wc.hIconSm = PlayerIcon(GetSystemMetrics(SM_CXSMICON));
    RegisterClassExW(&wc);

    RECT rc = {0, 0, S(600), kCaptionH + S(600)};
    const DWORD style = WS_POPUP | WS_THICKFRAME | WS_MINIMIZEBOX | WS_CLIPCHILDREN;

    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"DSplayer",
        style, CW_USEDEFAULT, CW_USEDEFAULT, rc.right - rc.left, rc.bottom - rc.top,
        nullptr, nullptr, inst, nullptr);
    if (!hwnd) {
        FreeAudioOutput();
        return 1;
    }
    g_hwnd = hwnd;
    SetIdleTitle();

    ShowWindow(hwnd, showCmd);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {

        HWND dialogWnd = hwnd;
        if (g_smpWnd && IsWindow(g_smpWnd)) {
            HWND root = msg.hwnd ? GetAncestor(msg.hwnd, GA_ROOT) : nullptr;
            if (root == g_smpWnd) dialogWnd = g_smpWnd;
        }
        if (g_waveWnd && IsWindow(g_waveWnd)) {
            HWND root = msg.hwnd ? GetAncestor(msg.hwnd, GA_ROOT) : nullptr;
            if (root == g_waveWnd) dialogWnd = g_waveWnd;
        }
        if (!IsDialogMessageW(dialogWnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    return static_cast<int>(msg.wParam);
}

int ReportUnhandledException() {
    ReleaseKeepAwake();
    CancelLoudnessProbe();
    g_waveExportCancel.store(true, std::memory_order_relaxed);
    if (g_waveExportThread.joinable()) g_waveExportThread.join();
    FreeAudioOutput();
    MessageBoxW(nullptr, L"A critical application error occurred.\n"
                         L"The application will close.",
                L"Error", MB_ICONERROR | MB_OK);
    return 1;
}

int WINAPI wWinMain(_In_ HINSTANCE inst, _In_opt_ HINSTANCE previousInstance,
                    _In_ PWSTR commandLine, _In_ int showCmd) {
    (void)previousInstance;
    try {
        return RunApplication(inst, commandLine, showCmd);
    } catch (const std::exception&) {
        return ReportUnhandledException();
    } catch (...) {
        return ReportUnhandledException();
    }
}
