// sound.h: what the tire inflation system sounds like.
//   Air let out: while the tires deflate, for as long as that takes.
//   Air going in: while they fill.
//     Both are the author's own recordings, of air let out of a tire and of a tire being filled, carried in the mod's
//     file (assets\air_out.wav and assets\air_in.wav through src\sounds.rc). The start plays as it was recorded, the
//     steady part then goes round, and as the change is done the recorded stop is played.
//   The compressor: it runs when tire_pressure.cpp says so, which keeps the air tank the fillings draw from. So not
//     at every filling, and on for a while after one.
//   The compressor stopping: the air it lets go as it stops, once.
// tire_pressure.cpp says on every pass what should be heard (SoundSet); a thread here plays it.
//
// Where the four sounds come from, by ini AirOutSound, AirInSound, CompressorSound and CompressorStopSound:
//   (nothing) the mod's own choice (kSoundOwn): the recording in the mod's file, or a sample among the game's own,
//             read out of the player's shared_sound.pak as the panel's font is read out of the game folder (a sound
//             made by code where that sample cannot be read)
//   none      this one is not played
//   a name    a WAV file of that name in the game's Bin folder (or with its full path), else the sample of that name
//             in shared_sound.pak
// The game's samples are WAV files with the ending .pcm (Microsoft ADPCM, one channel, 44.1 or 22 kHz) and lie in the
// pak as they are, not compressed. A WAV file that marks a loop (a "smpl" chunk) plays up to the loop once, then goes
// round in it, and plays what follows the loop once as the sound stops.
//
// Played through XAudio2 2.9: the xaudio2_9redist.dll the game brings and uses itself, or Windows' own xaudio2_9.dll.
// The engine only exists while a sound plays and for a few seconds after. In between the mod holds no audio device,
// so a default device that changes (a TV switched off) needs no handling. Without XAudio2 or without a device there
// is no sound and nothing else changes.
#pragma once
#include <windows.h>
#include <objbase.h>
#include <xaudio2.h>
#include <math.h>
#include <stdint.h>
#include <string.h>
#include <string>
#include <vector>

void PanelLog(const wchar_t *fmt, ...); // a line in TirePressure.log (tire_pressure.cpp)

enum { kVoiceHiss, kVoiceAir, kVoiceCompressor, kVoicePurge, kVoiceCount };

// Samples of one channel in up to three parts: a start that is played once (up to loopFrom), the part that goes round
// and round while the sound lasts, and an ending that is played once as it stops (from loopTo on, where there is one).
struct SoundClip
{
    std::vector<float> x;
    uint32_t rate = 0;
    float pitch = 1.0f;   // how fast it plays where the shape asks for 1
    size_t loopFrom = 0;
    size_t loopTo = 0;    // 0: no ending, the loop runs to the last sample
    bool marked = false;  // the file itself said where its loop is
};

// One past the last sample of the part that goes round.
inline size_t SoundLoopEnd(const SoundClip &c) { return c.loopTo && c.loopTo < c.x.size() ? c.loopTo : c.x.size(); }

static const float kSoundRms = 0.1f;       // every sound is brought to this loudness (RMS)...
static const float kSoundPeak = 0.6f;      // ...and down again where its peaks would pass this
static const float kSoundMostPitch = 4.0f; // the fastest a voice can be asked to play
static const float kSoundHandOver = 0.04f; // seconds a loop takes to go when its recorded ending takes over (the ending's first 40 ms come in over it)
static const uint64_t kSoundMostBytes = 64ull << 20;

// What each sound is in a pressure change: the seconds its loudness takes to come up once it is wanted and to go once
// it is not, how loud it is at SoundVolume=100 (against kSoundRms), and whether it is played once (the others go
// round while wanted). The last three can fall together at a filling: their levels times kSoundPeak sum to less
// than 1.
struct SoundVoiceKind { const wchar_t *what, *key; float attack, release, level; bool once; };
static const SoundVoiceKind kSoundVoices[kVoiceCount] = {
    { L"air let out", L"AirOutSound", 0.005f, 0.12f, 0.56f, false },   // the starts of these two are in the
    { L"air going in", L"AirInSound", 0.005f, 0.12f, 0.44f, false },   // recordings: no fading in
    { L"compressor", L"CompressorSound", 0.5f, 0.7f, 0.8f, false },
    { L"compressor stopping", L"CompressorStopSound", 0.0f, 0.0f, 0.4f, true } };

// The mod's own choice per sound: a recording in the mod's file (the name in src\sounds.rc), or a sample of the game's
// with the high pass (Hz, 0 = none) and the pitch it is played with; with neither, or where the sample cannot be
// read, it is made by code (SoundMake...).
static const struct { const wchar_t *carried, *sample; float highPass, pitch; } kSoundOwn[kVoiceCount] = {
    { L"AIROUT", nullptr, 0.0f, 1.0f },                                         // air let out: the recording
    { L"AIRIN", nullptr, 0.0f, 1.0f },                                          // air going in: the recording
    { nullptr, L"[sound]\\actors\\actor_lamp_generator_loop.pcm", 0.0f, 1.0f }, // the light tower's generator
    { nullptr, L"[sound]\\trucks\\common\\truck_brake_release_rnd_set\\truck_brake_release_rnd__2.pcm", 0.0f, 1.0f } }; // a truck letting air go

// A WAV file (PCM of 8 to 32 bits, 32-bit float, or Microsoft ADPCM as the game's samples are) as one channel of
// floats. why: what was wrong with it.
inline bool SoundDecode(const std::vector<uint8_t> &file, SoundClip &out, std::wstring &why)
{
    const uint8_t *f = file.data();
    const size_t n = file.size();
    auto u16 = [&](size_t o) { return (uint32_t)(f[o] | f[o + 1] << 8); };
    auto u32 = [&](size_t o) { return (uint32_t)f[o] | (uint32_t)f[o + 1] << 8 | (uint32_t)f[o + 2] << 16 | (uint32_t)f[o + 3] << 24; };
    auto s16 = [&](size_t o) { return (int)(int16_t)u16(o); };
    if (n < 12 || memcmp(f, "RIFF", 4) || memcmp(f + 8, "WAVE", 4)) { why = L"it is not a WAV file"; return false; }
    size_t fmt = 0, fmtSize = 0, data = 0, dataSize = 0, loopFirst = 0, loopLast = 0;
    bool marked = false;
    for (size_t p = 12; p + 8 <= n;)
    {
        const size_t size = u32(p + 4), room = n - p - 8;
        if (!memcmp(f + p, "fmt ", 4)) { fmt = p + 8; fmtSize = min(size, room); }
        else if (!memcmp(f + p, "data", 4)) { data = p + 8; dataSize = min(size, room); }
        else if (!memcmp(f + p, "smpl", 4) && min(size, room) >= 60 && u32(p + 8 + 28) >= 1)
        {
            // a sampler's chunk: after 36 bytes its loops, each with its first and last sample at +8 and +12
            loopFirst = u32(p + 8 + 36 + 8);
            loopLast = u32(p + 8 + 36 + 12);
            marked = true;
        }
        p += 8 + size + (size & 1);
    }
    if (!fmt || fmtSize < 16 || !data || !dataSize) { why = L"it has no format or no samples"; return false; }
    uint32_t tag = u16(fmt);
    const uint32_t channels = u16(fmt + 2), rate = u32(fmt + 4), align = u16(fmt + 12), bits = u16(fmt + 14);
    if (tag == 0xFFFE && fmtSize >= 26) tag = u16(fmt + 24); // the extensible format: its sub format starts with the tag
    if (!channels || channels > 8 || rate < 4000 || rate > 192000 || !align) { why = L"its format is not one the mod reads"; return false; }
    const size_t most = (size_t)rate * 120; // two minutes are plenty for a loop
    std::vector<float> x;
    if (tag == 1 || tag == 3)
    {
        const uint32_t bytes = bits / 8;
        if (bits % 8 || bytes < 1 || bytes > 4 || (tag == 3 && bytes != 4) || align < bytes * channels) { why = L"its format is not one the mod reads"; return false; }
        const size_t frames = min(dataSize / align, most);
        x.resize(frames);
        for (size_t i = 0; i < frames; i++)
        {
            float sum = 0.0f;
            for (uint32_t c = 0; c < channels; c++)
            {
                const size_t o = data + i * align + (size_t)c * bytes;
                float v = 0.0f;
                if (tag == 3) memcpy(&v, f + o, 4);
                else if (bytes == 1) v = (float)((int)f[o] - 128) / 128.0f;
                else if (bytes == 2) v = (float)s16(o) / 32768.0f;
                else if (bytes == 3) v = (float)(int)(f[o] | f[o + 1] << 8 | (int)(int8_t)f[o + 2] * 65536) / 8388608.0f;
                else v = (float)(int32_t)u32(o) / 2147483648.0f;
                sum += isfinite(v) ? max(-4.0f, min(4.0f, v)) : 0.0f;
            }
            x[i] = sum / (float)channels;
        }
    }
    else if (tag == 2)
    {
        // Microsoft ADPCM. A block starts, for each channel, with the number of a predictor, a step and two samples;
        // after that every sample is four bits: the difference to what the predictor says, in steps. The shifts are
        // those of Microsoft's own codec: they round down, below zero too.
        static const int kCoef[7][2] = { { 256, 0 }, { 512, -256 }, { 0, 0 }, { 192, 64 }, { 240, 0 }, { 460, -208 }, { 392, -232 } };
        static const int kAdapt[16] = { 230, 230, 230, 230, 307, 409, 512, 614, 768, 614, 512, 409, 307, 230, 230, 230 };
        if (channels > 2 || align < 7 * channels + 1) { why = L"its format is not one the mod reads"; return false; }
        const float scale = 1.0f / (32768.0f * (float)channels);
        for (size_t b = 0; b < dataSize / align && x.size() < most; b++)
        {
            size_t p = data + b * align;
            int pred[2] = {}, step[2] = {}, s1[2] = {}, s2[2] = {};
            for (uint32_t c = 0; c < channels; c++, p++) pred[c] = f[p] < 7 ? f[p] : 6;
            for (uint32_t c = 0; c < channels; c++) { step[c] = s16(p); p += 2; }
            for (uint32_t c = 0; c < channels; c++) { s1[c] = s16(p); p += 2; }
            for (uint32_t c = 0; c < channels; c++) { s2[c] = s16(p); p += 2; }
            x.push_back((float)(s2[0] + s2[1]) * scale);
            x.push_back((float)(s1[0] + s1[1]) * scale);
            const size_t end = data + (b + 1) * align;
            uint32_t c = 0;
            int frame = 0;
            for (; p < end; p++)
                for (int half = 0; half < 2; half++)
                {
                    const int nibble = half ? f[p] & 15 : f[p] >> 4;
                    int v = ((s1[c] * kCoef[pred[c]][0] + s2[c] * kCoef[pred[c]][1]) >> 8) + (nibble >= 8 ? nibble - 16 : nibble) * step[c];
                    v = max(-32768, min(32767, v));
                    s2[c] = s1[c];
                    s1[c] = v;
                    step[c] = max(16, (kAdapt[nibble] * step[c]) >> 8);
                    frame += v;
                    if (++c == channels) { x.push_back((float)frame * scale); frame = 0; c = 0; }
                }
        }
    }
    else { why = L"its format is not one the mod reads (PCM, float and Microsoft ADPCM are)"; return false; }
    if (x.size() < rate / 10) { why = L"it is shorter than a tenth of a second"; return false; }
    // a marked loop of a tenth of a second or more: what lies before it is played once as the sound begins, what
    // follows it once as the sound stops (less than a twentieth of a second there is no ending and is dropped)
    if (marked && loopLast >= loopFirst && loopLast < x.size() && loopLast + 1 - loopFirst >= rate / 10)
    {
        if (x.size() - (loopLast + 1) >= rate / 20) out.loopTo = loopLast + 1;
        else x.resize(loopLast + 1);
        out.loopFrom = loopFirst;
        out.marked = true;
    }
    out.x.swap(x);
    out.rate = rate;
    return true;
}

inline bool SoundFile(const std::wstring &path, std::vector<uint8_t> &out, std::wstring &why)
{
    const HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) { why = L"the file could not be opened"; return false; }
    LARGE_INTEGER size = {};
    DWORD got = 0;
    bool ok = GetFileSizeEx(h, &size) && size.QuadPart > 0 && (uint64_t)size.QuadPart <= kSoundMostBytes;
    if (ok)
    {
        out.resize((size_t)size.QuadPart);
        ok = ReadFile(h, out.data(), (DWORD)out.size(), &got, nullptr) && got == out.size();
    }
    CloseHandle(h);
    if (!ok) why = L"the file could not be read, or is larger than 64 MB";
    return ok;
}

// One file out of a pak of the game by its name. A pak is a zip, here also in its 64-bit form (shared_sound.pak has
// 5.8 GB). Only files that lie in it as they are get read, which the game's samples do.
inline bool PakRead(const std::wstring &pak, const std::wstring &name, std::vector<uint8_t> &out, std::wstring &why)
{
    const HANDLE h = CreateFileW(pak.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) { why = L"the game's shared_sound.pak could not be opened"; return false; }
    auto readAt = [&](uint64_t at, void *to, size_t size) {
        LARGE_INTEGER p;
        p.QuadPart = (LONGLONG)at;
        DWORD got = 0;
        return SetFilePointerEx(h, p, nullptr, FILE_BEGIN) && ReadFile(h, to, (DWORD)size, &got, nullptr) && got == size;
    };
    auto u16 = [](const uint8_t *p) { return (uint32_t)(p[0] | p[1] << 8); };
    auto u32 = [](const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; };
    auto u64 = [&](const uint8_t *p) { return (uint64_t)u32(p) | (uint64_t)u32(p + 4) << 32; };
    auto letter = [](wchar_t c) { return c >= L'A' && c <= L'Z' ? (wchar_t)(c + 32) : c == L'/' ? L'\\' : c; };
    bool ok = false;
    why = L"the pak is not a zip file as the game's paks are";
    do
    {
        LARGE_INTEGER fileSize = {};
        if (!GetFileSizeEx(h, &fileSize) || fileSize.QuadPart < 22) break;
        const uint64_t size = (uint64_t)fileSize.QuadPart;
        // the end record lies in the last 64 KB (a comment may follow it)
        std::vector<uint8_t> tail((size_t)min(size, (uint64_t)(22 + 65535)));
        if (!readAt(size - tail.size(), tail.data(), tail.size())) break;
        size_t end = tail.size();
        for (size_t i = tail.size() - 22 + 1; i-- > 0;)
            if (u32(&tail[i]) == 0x06054b50) { end = i; break; }
        if (end == tail.size()) break;
        uint64_t dirSize = u32(&tail[end + 12]), dirAt = u32(&tail[end + 16]);
        if (dirSize == 0xFFFFFFFF || dirAt == 0xFFFFFFFF || u16(&tail[end + 10]) == 0xFFFF)
        {
            // the 64-bit form: a locator in front of the end record says where the 64-bit end record is
            uint8_t locator[20], record[56];
            const uint64_t endAt = size - tail.size() + end;
            if (endAt < 20 || !readAt(endAt - 20, locator, 20) || u32(locator) != 0x07064b50) break;
            if (!readAt(u64(locator + 8), record, 56) || u32(record) != 0x06064b50) break;
            dirSize = u64(record + 40);
            dirAt = u64(record + 48);
        }
        if (dirSize < 46 || dirSize > kSoundMostBytes || dirAt > size || dirSize > size - dirAt) break;
        std::vector<uint8_t> dir((size_t)dirSize);
        if (!readAt(dirAt, dir.data(), dir.size())) break;
        why = L"the pak has no file of that name";
        for (size_t p = 0; p + 46 <= dir.size() && u32(&dir[p]) == 0x02014b50;)
        {
            const size_t nameLen = u16(&dir[p + 28]), extraLen = u16(&dir[p + 30]), commentLen = u16(&dir[p + 32]);
            if (p + 46 + nameLen + extraLen > dir.size()) break;
            bool same = nameLen == name.size();
            for (size_t i = 0; same && i < nameLen; i++) same = letter(dir[p + 46 + i]) == letter(name[i]);
            if (!same) { p += 46 + nameLen + extraLen + commentLen; continue; }
            uint64_t packed = u32(&dir[p + 20]), plain = u32(&dir[p + 24]), at = u32(&dir[p + 42]);
            // the 64-bit extra field holds, in this order, those of the three that read 0xFFFFFFFF above
            for (size_t x = p + 46 + nameLen, xe = x + extraLen; x + 4 <= xe;)
            {
                const size_t len = u16(&dir[x + 2]);
                if (u16(&dir[x]) == 1)
                {
                    size_t q = x + 4;
                    const size_t qe = min(xe, q + len);
                    if (plain == 0xFFFFFFFF && q + 8 <= qe) { plain = u64(&dir[q]); q += 8; }
                    if (packed == 0xFFFFFFFF && q + 8 <= qe) { packed = u64(&dir[q]); q += 8; }
                    if (at == 0xFFFFFFFF && q + 8 <= qe) at = u64(&dir[q]);
                }
                x += 4 + len;
            }
            uint8_t local[30];
            if (u16(&dir[p + 10]) != 0 || packed != plain) why = L"it is compressed in the pak, and only files that lie there as they are get read";
            else if (!plain || plain > kSoundMostBytes) why = L"it is empty or larger than 64 MB";
            else if (!readAt(at, local, 30) || u32(local) != 0x04034b50) why = L"the pak could not be read where the file should be";
            else
            {
                out.resize((size_t)plain);
                ok = readAt(at + 30 + u16(local + 26) + u16(local + 28), out.data(), out.size());
                if (!ok) why = L"the pak could not be read where the file should be";
            }
            break;
        }
    } while (false);
    CloseHandle(h);
    return ok;
}

struct SoundBiquad { double b0, b1, b2, a1, a2; };
enum { kSoundHighPass, kSoundBandPass };

inline SoundBiquad SoundFilter(int kind, double hz, double q, double rate)
{
    const double w = 2.0 * 3.14159265358979323846 * hz / rate, c = cos(w), a = sin(w) / (2.0 * q), a0 = 1.0 + a;
    if (kind == kSoundHighPass) return { (1.0 + c) / 2.0 / a0, -(1.0 + c) / a0, (1.0 + c) / 2.0 / a0, -2.0 * c / a0, (1.0 - a) / a0 };
    return { a / a0, 0.0, -a / a0, -2.0 * c / a0, (1.0 - a) / a0 }; // band pass, as loud as the input at its centre
}

// A loop through a filter so that its end still joins its start: once round to settle the filter, then again for the
// result.
inline std::vector<float> SoundLoopFilter(const std::vector<float> &x, const SoundBiquad &k)
{
    std::vector<float> y(x.size());
    double x1 = 0.0, x2 = 0.0, y1 = 0.0, y2 = 0.0;
    for (int pass = 0; pass < 2; pass++)
        for (size_t i = 0; i < x.size(); i++)
        {
            const double v = k.b0 * x[i] + k.b1 * x1 + k.b2 * x2 - k.a1 * y1 - k.a2 * y2;
            x2 = x1;
            x1 = x[i];
            y2 = y1;
            y1 = v;
            y[i] = (float)v;
        }
    return y;
}

inline float SoundRms(const std::vector<float> &x)
{
    double sum = 0.0;
    for (const float v : x) sum += (double)v * v;
    return x.empty() ? 0.0f : (float)sqrt(sum / (double)x.size());
}

inline void SoundToRms(std::vector<float> &x, float rms)
{
    const float now = SoundRms(x), k = rms / (now > 1e-9f ? now : 1e-9f);
    for (float &v : x) v *= k;
}

inline void SoundAdd(std::vector<float> &to, const std::vector<float> &x, float k)
{
    for (size_t i = 0; i < to.size() && i < x.size(); i++) to[i] += x[i] * k;
}

// Every sound at the same loudness, whatever it was recorded at, and one with high peaks a little quieter. What counts
// is the part that goes round; of a sound played once, its loudest tenth of a second.
inline void SoundLevel(SoundClip &c, bool once)
{
    const size_t n = c.x.size(), end = once ? n : SoundLoopEnd(c), span = once ? min(n, (size_t)c.rate / 10) : end - c.loopFrom;
    if (!n || !span) return;
    double sum = 0.0, most = 0.0;
    for (size_t i = once ? 0 : c.loopFrom; i < end; i++)
    {
        sum += (double)c.x[i] * c.x[i];
        if (once && i >= span) sum -= (double)c.x[i - span] * c.x[i - span];
        if (!once || i + 1 >= span) most = max(most, sum);
    }
    float k = kSoundRms / max(1e-9f, (float)sqrt(most / (double)span)), peak = 0.0f;
    for (const float v : c.x) peak = max(peak, fabsf(v));
    if (peak * k > kSoundPeak) k = kSoundPeak / peak;
    for (float &v : c.x) v *= k;
}

// A file of the player's is seldom made to be played round and round: its last 60 ms are folded into its first, so
// that the end joins the start.
inline void SoundJoin(SoundClip &c)
{
    const size_t n = c.x.size(), fold = min(n / 4, (size_t)((float)c.rate * 0.06f));
    if (fold < 8) return;
    for (size_t i = 0; i < fold; i++)
    {
        const float a = (float)i / (float)fold * 1.5707963f;
        c.x[i] = c.x[i] * sinf(a) + c.x[n - fold + i] * cosf(a);
    }
    c.x.resize(n - fold);
}

// The same numbers on every start: -1 .. 1.
struct SoundNoise
{
    uint32_t s;
    float operator()()
    {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        return (float)((double)s / 4294967296.0 * 2.0 - 1.0);
    }
};

// Air through a valve, made by code: noise between 1 and 8 kHz for the most part, in the balance of the air sound
// Expeditions plays, with a little flutter. Four seconds that join.
inline void SoundMakeHiss(SoundClip &c)
{
    const double rate = 44100.0;
    const size_t n = 44100 * 4;
    SoundNoise noise = { 0x51f15e };
    std::vector<float> x(n);
    for (float &v : x) v = noise();
    std::vector<float> y = SoundLoopFilter(x, SoundFilter(kSoundBandPass, 4500.0, 0.6, rate));
    SoundAdd(y, SoundLoopFilter(x, SoundFilter(kSoundBandPass, 1600.0, 0.7, rate)), 1.6f);
    y = SoundLoopFilter(y, SoundFilter(kSoundHighPass, 500.0, 0.707, rate));
    // the flutter: whole turns of a few slow waves, so that the loop joins
    const double turn = 2.0 * 3.14159265358979323846;
    for (size_t i = 0; i < n; i++)
    {
        const double t = (double)i / (double)n;
        y[i] *= (float)(1.0 + 0.05 * sin(turn * 7.0 * t) + 0.04 * sin(turn * 23.0 * t + 1.0) + 0.03 * sin(turn * 61.0 * t + 2.0));
    }
    c.x.swap(y);
    c.rate = 44100;
    c.pitch = 1.0f;
}

// A small piston compressor, made by code: 47 strokes a second, each a short knock that rings in the housing, with
// the air it draws, a rattle and the motor's hum. Three seconds that join.
inline void SoundMakeCompressor(SoundClip &c)
{
    const double rate = 44100.0, strokeHz = 47.0, turn = 2.0 * 3.14159265358979323846;
    const int strokes = 141; // three seconds of them
    const size_t n = (size_t)floor(rate * strokes / strokeHz + 0.5);
    const double period = (double)n / strokes, up = rate * 0.0013, down = rate * 0.0030;
    SoundNoise noise = { 0xc0ffee };
    std::vector<float> knocks(n, 0.0f), air(n);
    for (int s = 0; s < strokes; s++)
    {
        // no two strokes quite alike, in time or in strength
        const double at = ((double)s + 0.012 * noise()) * period;
        const double strength = 1.0 + 0.08 * noise();
        for (int k = 0; (double)k < up + down; k++)
        {
            const long long i = (((long long)floor(at + k + 0.5) % (long long)n) + (long long)n) % (long long)n;
            knocks[(size_t)i] += (float)((double)k < up ? strength * 0.5 * (1.0 - cos(turn * k / up)) : -0.45 * strength * 0.5 * (1.0 - cos(turn * (k - up) / down)));
        }
    }
    for (size_t i = 0; i < n; i++)
    {
        const double phase = fmod((double)i / period, 1.0);
        air[i] = (float)(noise() * (0.25 + 0.75 * exp(-phase * 5.0)));
    }
    static const struct { double hz, q; float share; } rings[6] = { { 47.0 * 2.0, 4.0, 0.30f }, { 47.0 * 4.1, 5.0, 0.75f }, { 47.0 * 6.6, 6.0, 0.95f },
                                                                    { 640.0, 7.0, 1.0f }, { 1250.0, 6.0, 0.7f }, { 2300.0, 5.0, 0.35f } };
    std::vector<float> y(n, 0.0f);
    for (const auto &r : rings) SoundAdd(y, SoundLoopFilter(knocks, SoundFilter(kSoundBandPass, r.hz, r.q, rate)), r.share);
    SoundToRms(y, 0.1f);
    std::vector<float> chuff = SoundLoopFilter(air, SoundFilter(kSoundBandPass, 2200.0, 0.8, rate)), rattle = SoundLoopFilter(air, SoundFilter(kSoundBandPass, 700.0, 1.0, rate));
    SoundToRms(chuff, 0.1f);
    SoundToRms(rattle, 0.1f);
    SoundAdd(y, chuff, 0.22f);
    SoundAdd(y, rattle, 0.12f);
    for (size_t i = 0; i < n; i++)
    {
        const double t = (double)i / rate;
        y[i] += (float)(0.012 * sin(turn * strokeHz * 2.0 * t) + 0.02 * sin(turn * strokeHz * 7.0 * t) * (1.0 + 0.3 * sin(turn * strokeHz * t)));
    }
    c.x.swap(y);
    c.rate = 44100;
    c.pitch = 1.0f;
}

// The air a compressor lets go as it stops, made by code where the game's sample cannot be read: the hiss, a second
// of it, dying away.
inline void SoundMakePuff(SoundClip &c)
{
    SoundMakeHiss(c);
    c.x.resize(44100);
    for (size_t i = 0; i < c.x.size(); i++)
    {
        const float t = (float)i / 44100.0f;
        c.x[i] *= min(1.0f, t / 0.02f) * expf(-t / 0.2f);
    }
}

// Loudness and pitch of a sound that goes round, at one moment. env: 0..1, up over its attack and down over its
// release; p: how far it has come (the air let out: how empty the tire is, 0 at Normal and 1 at Low; the air going
// in: the pressure change, 0..1; the compressor: how full its tank is, 0..1).
inline void SoundShapeAt(int voice, float env, float p, float &gain, float &pitch)
{
    if (voice == kVoiceHiss)
    {
        // air let out: weaker as the tire empties, 9 dB from Normal down to Low as in the recording (there the
        // loudness fell by about 1 dB a second and the tone stayed where it was). p: how empty, 0 at Normal, 1 at
        // Low, below 0 above Normal.
        gain = env * expf(-1.036f * p);
        pitch = 1.0f;
    }
    else if (voice == kVoiceAir)
    {
        // air into the tires: as recorded at first, then weaker and a little higher as the tires fill (in the
        // recording the weaker of two bursts has its tone a tenth higher)
        gain = env * (1.0f - 0.30f * p);
        pitch = 1.0f + 0.08f * p;
    }
    else if (voice == kVoiceCompressor)
    {
        // a compressor: it runs up, works a little harder as the tank fills, and runs down
        gain = powf(env, 0.8f);
        pitch = (0.5f + 0.5f * (1.0f - (1.0f - env) * (1.0f - env))) * (1.0f - 0.07f * p);
    }
    else
    {
        gain = 1.0f;
        pitch = 1.0f;
    }
}

// What the mod's thread wants to hear: set on every pass, read by the sound thread.
struct SoundWant
{
    float volume;                // 0..1
    bool on[kVoiceCount];        // the sounds that go round: wanted now
    float progress[kVoiceCount]; // and how far each has come, 0..1 (SoundShapeAt)
    LONG shots[kVoiceCount];     // a sound played once: how often it has been asked for so far
};
static SRWLOCK g_soundLock = SRWLOCK_INIT;
static SoundWant g_soundWant = {};
static std::wstring g_soundNames[kVoiceCount]; // the ini's names
static bool g_soundNamesNew = false;
static std::wstring g_soundDir, g_soundPak;
static HANDLE g_soundWake = nullptr;
static volatile LONG g_soundOpens = 0, g_soundPlayedMs[kVoiceCount] = {}, g_soundStarts[kVoiceCount] = {}, g_soundEndings[kVoiceCount] = {}; // for the test

// XAudio2 says through this when its device is gone for good; the sound thread then lets go of the engine.
struct SoundEvents : IXAudio2EngineCallback
{
    volatile LONG failed = 0;
    void STDMETHODCALLTYPE OnProcessingPassStart() override {}
    void STDMETHODCALLTYPE OnProcessingPassEnd() override {}
    void STDMETHODCALLTYPE OnCriticalError(HRESULT) override { InterlockedExchange(&failed, 1); }
};
static SoundEvents g_soundEvents;

struct SoundEngine
{
    IXAudio2 *xaudio = nullptr;
    IXAudio2MasteringVoice *master = nullptr;
    // ending: a second voice for a clip's ending, where it has one; ended: that ending has been set off;
    // endingUntil: when it will be over (GetTickCount64)
    struct Voice { IXAudio2SourceVoice *voice, *ending; bool playing, ended; float env, progress, volume; ULONGLONG endingUntil; } voices[kVoiceCount] = {};
};

inline void SoundClose(SoundEngine &e)
{
    for (SoundEngine::Voice &v : e.voices)
    {
        if (v.voice) v.voice->DestroyVoice();
        if (v.ending) v.ending->DestroyVoice();
        v = {};
    }
    if (e.master) e.master->DestroyVoice();
    if (e.xaudio)
    {
        e.xaudio->UnregisterForCallbacks(&g_soundEvents);
        e.xaudio->Release();
    }
    e.master = nullptr;
    e.xaudio = nullptr;
}

// A clip into a voice's queue: what lies before its loop once, then the loop for good; a sound played once has no
// loop.
inline HRESULT SoundQueue(IXAudio2SourceVoice *voice, const SoundClip &clip, bool once)
{
    const size_t loopEnd = SoundLoopEnd(clip);
    XAUDIO2_BUFFER b = {};
    b.Flags = XAUDIO2_END_OF_STREAM;
    b.AudioBytes = (UINT32)((once ? clip.x.size() : loopEnd) * sizeof(float));
    b.pAudioData = (const BYTE *)clip.x.data();
    if (once) return voice->SubmitSourceBuffer(&b);
    b.LoopCount = XAUDIO2_LOOP_INFINITE;
    if (clip.loopFrom)
    {
        b.LoopBegin = (UINT32)clip.loopFrom;
        b.LoopLength = (UINT32)(loopEnd - clip.loopFrom);
        if (SUCCEEDED(voice->SubmitSourceBuffer(&b))) return S_OK;
        b.LoopBegin = b.LoopLength = 0; // refused: all of it round and round
    }
    return voice->SubmitSourceBuffer(&b);
}

// A clip's ending into a voice's queue, to be played once.
inline HRESULT SoundQueueEnding(IXAudio2SourceVoice *voice, const SoundClip &clip)
{
    const size_t loopEnd = SoundLoopEnd(clip);
    XAUDIO2_BUFFER b = {};
    b.Flags = XAUDIO2_END_OF_STREAM;
    b.AudioBytes = (UINT32)((clip.x.size() - loopEnd) * sizeof(float));
    b.pAudioData = (const BYTE *)(clip.x.data() + loopEnd);
    return voice->SubmitSourceBuffer(&b);
}

// An engine on the default device with one voice per sound. A sound that simply goes round is queued for good here
// and later only started and stopped, so it goes on where it was; the others are queued each time they begin.
inline bool SoundOpen(SoundEngine &e, const SoundClip *clips, HRESULT &hr, const wchar_t *&from)
{
    typedef HRESULT(__stdcall *Create)(IXAudio2 **, UINT32, XAUDIO2_PROCESSOR);
    from = L"the game's xaudio2_9redist.dll";
    HMODULE dll = GetModuleHandleW(L"xaudio2_9redist.dll");
    if (!dll) dll = LoadLibraryW((g_soundDir + L"xaudio2_9redist.dll").c_str());
    if (!dll)
    {
        from = L"Windows' xaudio2_9.dll";
        dll = LoadLibraryExW(L"xaudio2_9.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    }
    const Create create = dll ? (Create)(void *)GetProcAddress(dll, "XAudio2Create") : nullptr;
    if (!create) { hr = HRESULT_FROM_WIN32(ERROR_MOD_NOT_FOUND); return false; }
    hr = create(&e.xaudio, 0, XAUDIO2_DEFAULT_PROCESSOR);
    if (FAILED(hr) || !e.xaudio) { e.xaudio = nullptr; return false; }
    InterlockedExchange(&g_soundEvents.failed, 0);
    e.xaudio->RegisterForCallbacks(&g_soundEvents);
    hr = e.xaudio->CreateMasteringVoice(&e.master, XAUDIO2_DEFAULT_CHANNELS, XAUDIO2_DEFAULT_SAMPLERATE, 0, nullptr, nullptr, AudioCategory_GameEffects);
    for (int k = 0; SUCCEEDED(hr) && k < kVoiceCount; k++)
    {
        if (clips[k].x.empty()) continue;
        WAVEFORMATEX format = {};
        format.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
        format.nChannels = 1;
        format.nSamplesPerSec = clips[k].rate;
        format.wBitsPerSample = 32;
        format.nBlockAlign = 4;
        format.nAvgBytesPerSec = clips[k].rate * 4;
        hr = e.xaudio->CreateSourceVoice(&e.voices[k].voice, &format, 0, kSoundMostPitch);
        if (FAILED(hr)) break;
        e.voices[k].voice->SetVolume(0.0f);
        if (!kSoundVoices[k].once && !clips[k].loopFrom) hr = SoundQueue(e.voices[k].voice, clips[k], false);
        if (SUCCEEDED(hr) && !kSoundVoices[k].once && SoundLoopEnd(clips[k]) < clips[k].x.size())
            hr = e.xaudio->CreateSourceVoice(&e.voices[k].ending, &format, 0, kSoundMostPitch);
    }
    if (FAILED(hr)) { SoundClose(e); return false; }
    return true;
}

// A sound the mod's own file carries (src\sounds.rc), by its name there.
inline bool SoundCarried(const wchar_t *name, std::vector<uint8_t> &out)
{
    HMODULE self = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)&g_soundLock, &self)) return false;
    const HRSRC found = FindResourceW(self, name, (LPCWSTR)RT_RCDATA);
    const HGLOBAL loaded = found ? LoadResource(self, found) : nullptr;
    const uint8_t *bytes = loaded ? (const uint8_t *)LockResource(loaded) : nullptr;
    const DWORD size = found ? SizeofResource(self, found) : 0;
    if (!bytes || !size) return false;
    out.assign(bytes, bytes + size);
    return true;
}

// One sound, by the ini's name for it (the top of this file has the rules).
inline void SoundLoad(int voice, const std::wstring &name, SoundClip &clip)
{
    const wchar_t *const what = kSoundVoices[voice].what;
    const bool once = kSoundVoices[voice].once;
    clip = SoundClip{};
    if (!_wcsicmp(name.c_str(), L"none")) { PanelLog(L"sound: %s = none", what); return; }
    std::vector<uint8_t> file;
    std::wstring why;
    bool got = false;
    if (!name.empty())
    {
        const bool full = name.size() > 2 && (name[1] == L':' || (name[0] == L'\\' && name[1] == L'\\'));
        const std::wstring path = full ? name : g_soundDir + name;
        if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES)
        {
            got = SoundFile(path, file, why) && SoundDecode(file, clip, why);
            if (got && !once && !clip.marked) SoundJoin(clip);
        }
        else got = PakRead(g_soundPak, name, file, why) && SoundDecode(file, clip, why);
        if (got) PanelLog(L"sound: %s = %s (%.1f s, %u Hz)", what, name.c_str(), (double)clip.x.size() / clip.rate, clip.rate);
        else PanelLog(L"sound: %s: \"%s\" is not used (%s)", what, name.c_str(), why.c_str());
    }
    const auto &own = kSoundOwn[voice];
    if (!got && own.carried)
    {
        got = SoundCarried(own.carried, file) && SoundDecode(file, clip, why);
        if (got) PanelLog(L"sound: %s = the mod's own recording (%u Hz: a start of %.2f s, %.2f s that go round, an ending of %.2f s)", what, clip.rate, (double)clip.loopFrom / clip.rate,
                          (double)(SoundLoopEnd(clip) - clip.loopFrom) / clip.rate, (double)(clip.x.size() - SoundLoopEnd(clip)) / clip.rate);
        else PanelLog(L"sound: %s: the mod's own recording could not be read", what);
    }
    if (!got && own.sample)
    {
        got = PakRead(g_soundPak, own.sample, file, why) && SoundDecode(file, clip, why);
        if (got && own.highPass > 0.0f)
        {
            const SoundBiquad k = SoundFilter(kSoundHighPass, own.highPass, 0.707, clip.rate);
            clip.x = SoundLoopFilter(SoundLoopFilter(clip.x, k), k);
        }
        if (got) clip.pitch = own.pitch;
        if (got) PanelLog(L"sound: %s = the game's own %s (%.1f s, %u Hz)", what, own.sample, (double)clip.x.size() / clip.rate, clip.rate);
        else PanelLog(L"sound: %s: the game's own %s was not read (%s)", what, own.sample, why.c_str());
    }
    if (!got && !own.carried)
    {
        clip = SoundClip{};
        if (voice == kVoiceHiss) SoundMakeHiss(clip);
        else if (voice == kVoiceCompressor) SoundMakeCompressor(clip);
        else SoundMakePuff(clip);
        got = true;
        PanelLog(L"sound: %s = made by the mod's code", what);
    }
    if (got) SoundLevel(clip, once);
    else clip = SoundClip{};
}

static DWORD WINAPI SoundThread(void *)
{
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    SoundClip clips[kVoiceCount];
    SoundEngine e;
    LARGE_INTEGER perSecond, last;
    QueryPerformanceFrequency(&perSecond);
    QueryPerformanceCounter(&last);
    ULONGLONG quietSince = 0, againAt = 0;
    HRESULT lastError = S_OK;
    LONG shotsHeard[kVoiceCount] = {};
    bool said = false;
    for (;;)
    {
        // with no engine there is nothing to do until a sound is wanted or the names change
        WaitForSingleObject(g_soundWake, e.xaudio ? 10 : INFINITE);
        std::wstring names[kVoiceCount];
        AcquireSRWLockExclusive(&g_soundLock);
        const SoundWant want = g_soundWant;
        const bool namesNew = g_soundNamesNew;
        g_soundNamesNew = false;
        if (namesNew) for (int k = 0; k < kVoiceCount; k++) names[k] = g_soundNames[k];
        ReleaseSRWLockExclusive(&g_soundLock);
        if (namesNew)
        {
            SoundClose(e); // the voices play out of the clips' memory
            for (int k = 0; k < kVoiceCount; k++) SoundLoad(k, names[k], clips[k]);
        }
        const ULONGLONG ms = GetTickCount64();
        bool on[kVoiceCount] = {}, shot[kVoiceCount] = {}, wanted = false;
        for (int k = 0; k < kVoiceCount; k++)
        {
            const bool can = want.volume > 0.0f && !clips[k].x.empty();
            if (kSoundVoices[k].once)
            {
                // asked for since the last look: played now or never
                shot[k] = can && want.shots[k] != shotsHeard[k];
                shotsHeard[k] = want.shots[k];
            }
            else on[k] = can && want.on[k];
            wanted = wanted || on[k] || shot[k];
        }
        if (!e.xaudio)
        {
            if (!wanted || ms < againAt) continue;
            HRESULT hr = S_OK;
            const wchar_t *from = L"";
            if (!SoundOpen(e, clips, hr, from))
            {
                againAt = ms + 5000;
                if (hr != lastError) PanelLog(L"sound: none, as XAudio2 did not start (%08lx from %s); tried again at the next change", (unsigned long)hr, from);
                lastError = hr;
                continue;
            }
            if (!said) PanelLog(L"sound: played through %s", from);
            said = true;
            lastError = S_OK;
            quietSince = 0;
            InterlockedIncrement(&g_soundOpens);
            QueryPerformanceCounter(&last);
        }
        if (InterlockedExchange(&g_soundEvents.failed, 0))
        {
            // the device went away under the engine: let go of it, the next change starts a new one
            SoundClose(e);
            againAt = ms + 2000;
            continue;
        }
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        const float dt = min(0.1f, (float)((double)(now.QuadPart - last.QuadPart) / (double)perSecond.QuadPart));
        last = now;
        bool any = false;
        for (int k = 0; k < kVoiceCount; k++)
        {
            SoundEngine::Voice &v = e.voices[k];
            if (!v.voice) continue;
            const SoundVoiceKind &kind = kSoundVoices[k];
            if (kind.once)
            {
                if (shot[k])
                {
                    v.voice->Stop(0);
                    v.voice->FlushSourceBuffers();
                    v.voice->SetVolume(kind.level * want.volume);
                    v.voice->SetFrequencyRatio(clips[k].pitch);
                    v.playing = SUCCEEDED(SoundQueue(v.voice, clips[k], true)) && SUCCEEDED(v.voice->Start(0));
                    InterlockedIncrement(&g_soundStarts[k]);
                }
                else if (v.playing)
                {
                    XAUDIO2_VOICE_STATE state = {};
                    v.voice->GetState(&state, XAUDIO2_VOICE_NOSAMPLESPLAYED);
                    v.playing = state.BuffersQueued != 0;
                }
                any = any || v.playing;
                continue;
            }
            if (ms < v.endingUntil) any = true; // its ending still sounds
            if (on[k])
            {
                if (!v.playing)
                {
                    v.playing = true;
                    v.env = 0.0f;
                    v.voice->SetVolume(0.0f);
                    if (clips[k].loopFrom)
                    {
                        // from its start each time; whatever the last time left in the queue goes first
                        v.voice->FlushSourceBuffers();
                        SoundQueue(v.voice, clips[k], false);
                    }
                    v.voice->Start(0);
                    InterlockedIncrement(&g_soundStarts[k]);
                }
                v.ended = false;
                v.env = min(1.0f, v.env + dt / kind.attack);
                v.progress = want.progress[k];
                v.volume = want.volume;
            }
            else if (v.playing)
            {
                if (v.ending && !v.ended)
                {
                    // its ending: once, as loud and as high as the loop is now, while the loop gets out of its way
                    v.ended = true;
                    float gain = 0.0f, pitch = 1.0f;
                    SoundShapeAt(k, v.env, v.progress, gain, pitch);
                    pitch = min(kSoundMostPitch, max(0.05f, pitch * clips[k].pitch));
                    v.ending->Stop(0);
                    v.ending->FlushSourceBuffers();
                    v.ending->SetVolume(gain * kind.level * v.volume);
                    v.ending->SetFrequencyRatio(pitch);
                    if (SUCCEEDED(SoundQueueEnding(v.ending, clips[k])) && SUCCEEDED(v.ending->Start(0)))
                        v.endingUntil = ms + 100 + (ULONGLONG)((double)(clips[k].x.size() - SoundLoopEnd(clips[k])) * 1000.0 / clips[k].rate / pitch);
                    InterlockedIncrement(&g_soundEndings[k]);
                    any = true;
                }
                v.env = max(0.0f, v.env - dt / (v.ending ? kSoundHandOver : kind.release));
            }
            if (!v.playing) continue;
            float gain = 0.0f, pitch = 1.0f;
            SoundShapeAt(k, v.env, v.progress, gain, pitch);
            v.voice->SetVolume(gain * kind.level * v.volume);
            v.voice->SetFrequencyRatio(min(kSoundMostPitch, max(0.05f, pitch * clips[k].pitch)));
            if (!on[k] && v.env <= 0.0f)
            {
                v.voice->Stop(0);
                if (clips[k].loopFrom) v.voice->FlushSourceBuffers(); // the next time begins at its start again
                v.playing = false;
                continue;
            }
            any = true;
            InterlockedExchangeAdd(&g_soundPlayedMs[k], (LONG)(dt * 1000.0f + 0.5f));
        }
        if (any) quietSince = 0;
        else if (!quietSince) quietSince = ms;
        else if (ms - quietSince > 3000) SoundClose(e);
    }
}

// The ini's names for the sounds, one per voice (kSoundVoices has the keys); the sounds are made or read again on the
// sound thread.
inline void SoundNames(const std::wstring *names)
{
    AcquireSRWLockExclusive(&g_soundLock);
    bool same = true;
    for (int k = 0; k < kVoiceCount; k++)
    {
        same = same && g_soundNames[k] == names[k];
        g_soundNames[k] = names[k];
    }
    if (!same) g_soundNamesNew = true;
    ReleaseSRWLockExclusive(&g_soundLock);
    if (!same && g_soundWake) SetEvent(g_soundWake);
}

// Starts the sound thread, which makes or reads the sounds at once. bin: the game's Bin folder, with its last
// backslash; pak: the game's sound pak, when it is not where it lies from the Bin folder.
inline bool SoundStart(const std::wstring &bin, const std::wstring &pak = L"")
{
    if (g_soundWake) return true;
    g_soundDir = bin;
    g_soundPak = pak.empty() ? bin + L"..\\..\\preload\\paks\\client\\shared_sound.pak" : pak;
    AcquireSRWLockExclusive(&g_soundLock);
    g_soundNamesNew = true;
    ReleaseSRWLockExclusive(&g_soundLock);
    g_soundWake = CreateEventW(nullptr, FALSE, TRUE, nullptr);
    const HANDLE t = g_soundWake ? CreateThread(nullptr, 0, SoundThread, nullptr, 0, nullptr) : nullptr;
    if (!t)
    {
        if (g_soundWake) CloseHandle(g_soundWake);
        g_soundWake = nullptr;
        return false;
    }
    CloseHandle(t);
    return true;
}

// Called on every pass of the mod's thread with what should be heard now.
inline void SoundSet(const SoundWant &want)
{
    bool wake = false;
    AcquireSRWLockExclusive(&g_soundLock);
    for (int k = 0; k < kVoiceCount; k++) wake = wake || want.on[k] || want.shots[k] != g_soundWant.shots[k];
    g_soundWant = want;
    ReleaseSRWLockExclusive(&g_soundLock);
    if (wake && want.volume > 0.0f && g_soundWake) SetEvent(g_soundWake);
}
