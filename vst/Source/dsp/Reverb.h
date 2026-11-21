#pragma once
// ============================================================================
//  Algorithmic reverb: an 8-line feedback delay network (Householder mixing)
//  with input diffusion, per-line damping and a pre-delay. Four room types set
//  the delay pattern and diffusion. Everything is preallocated for 192 kHz.
// ============================================================================
#include <vector>
#include "Common.h"

namespace resonique
{
class Reverb
{
public:
    static constexpr int L = 8;

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        const int maxLine = (int) (0.2 * fs) + 8;
        for (auto& d : lines) { d.assign ((size_t) maxLine, 0.0f); }
        pre.assign ((size_t) (0.12 * fs) + 8, 0.0f);
        for (auto& a : apL) a.assign ((size_t) (0.012 * fs) + 8, 0.0f);
        for (auto& a : apR) a.assign ((size_t) (0.012 * fs) + 8, 0.0f);
        reset();
        setType (2);
        setSize (0.5);
        setDamping (0.5);
        setPredelay (12.0);
    }

    void reset()
    {
        for (auto& d : lines) std::fill (d.begin(), d.end(), 0.0f);
        std::fill (pre.begin(), pre.end(), 0.0f);
        for (auto& a : apL) std::fill (a.begin(), a.end(), 0.0f);
        for (auto& a : apR) std::fill (a.begin(), a.end(), 0.0f);
        for (auto& z : lp) z = 0;
        writePos = 0; preWrite = 0; apPos = 0;
    }

    void setType (int t)
    {
        type = clampT (t, 0, 3);
        updateLengths();
    }
    void setSize (double s) { size = clampT (s, 0.0, 1.0); updateLengths(); }
    void setDamping (double d)
    {
        damping = clampT (d, 0.0, 1.0);
        const double hz = 1800.0 + (1.0 - damping) * 14000.0;
        lpCoef = (float) (1.0 - std::exp (-kTwoPi * std::min (hz, fs * 0.45) / fs));
    }
    void setPredelay (double ms) { preLen = clampT ((int) (ms * 1e-3 * fs), 1, (int) pre.size() - 2); }

    // Stereo in -> stereo out (wet only)
    void processChunk (int n, const float* inL, const float* inR, float* outL, float* outR)
    {
        for (int i = 0; i < n; ++i)
        {
            // predelay on the mono-ish send
            const float in = 0.5f * (inL[i] + inR[i]);
            pre[(size_t) preWrite] = in;
            int rd = preWrite - preLen; if (rd < 0) rd += (int) pre.size();
            float x = pre[(size_t) rd];
            preWrite = (preWrite + 1) % (int) pre.size();
            // input diffusion: two allpasses per channel
            float xl = x + 0.3f * inL[i], xr = x + 0.3f * inR[i];
            xl = allpass (apL[0], apLen[0], xl); xl = allpass (apL[1], apLen[1], xl);
            xr = allpass (apR[0], apLen[2], xr); xr = allpass (apR[1], apLen[3], xr);
            apPos = (apPos + 1) % (int) apL[0].size();

            // read the delay lines
            float r[L];
            for (int k = 0; k < L; ++k)
            {
                int pos = writePos - len[k]; if (pos < 0) pos += (int) lines[(size_t) k].size();
                r[k] = lines[(size_t) k][(size_t) pos];
            }
            // Householder: y = r - (2/L) * sum(r)
            float sum = 0; for (int k = 0; k < L; ++k) sum += r[k];
            const float s2 = sum * (2.0f / L);
            float l = 0, rr = 0;
            for (int k = 0; k < L; ++k)
            {
                float y = r[k] - s2;
                // damping + feedback, then write with the input
                lp[k] += lpCoef * (y - lp[k]);
                y = lp[k] * fb[k];
                const float inj = (k & 1) ? xr : xl;
                lines[(size_t) k][(size_t) writePos] = flushDenormal (y + inj * 0.5f);
                if (k < L / 2) l += r[k] * outGain[k]; else rr += r[k] * outGain[k];
            }
            writePos = (writePos + 1) % (int) lines[0].size();
            outL[i] = l;
            outR[i] = rr;
        }
    }

private:
    inline float allpass (std::vector<float>& buf, int length, float x)
    {
        int rd = apPos - length; if (rd < 0) rd += (int) buf.size();
        const float d = buf[(size_t) rd];
        const float y = -0.6f * x + d;
        buf[(size_t) apPos] = x + 0.6f * y;
        return y;
    }

    void updateLengths()
    {
        // base delay patterns (ms) per type, scaled by size
        static const double base[4][L] = {
            { 11.3, 14.9, 19.7, 23.1, 27.9, 31.7, 37.1, 41.3 },      // room
            { 17.3, 21.1, 27.7, 33.9, 39.1, 45.7, 53.3, 59.9 },      // chamber
            { 29.3, 37.1, 43.9, 53.3, 61.7, 71.3, 83.9, 97.1 },      // hall
            { 23.1, 31.7, 41.3, 47.9, 59.3, 67.1, 79.7, 89.9 },      // stage
        };
        static const double rt60s[4] = { 0.9, 1.6, 2.8, 2.1 };
        const double scale = 0.55 + 1.1 * size;
        const double rt60 = rt60s[type] * (0.4 + 1.6 * size);
        for (int k = 0; k < L; ++k)
        {
            const double ms = base[type][k] * scale;
            len[k] = clampT ((int) (ms * 1e-3 * fs), 4, (int) lines[0].size() - 2);
            fb[k] = (float) std::pow (0.001, (double) len[k] / (rt60 * fs));
            outGain[k] = (float) (0.35 * ((k % 3 == 0) ? -1.0 : 1.0));
        }
        const double apBase[4] = { 4.3, 6.1, 7.9, 9.7 };
        for (int k = 0; k < 4; ++k) apLen[k] = clampT ((int) (apBase[k] * 1e-3 * fs), 2, (int) apL[0].size() - 2);
    }

    double fs = 48000, size = 0.5, damping = 0.5;
    int type = 2;
    std::vector<float> lines[L];
    std::vector<float> pre, apL[2], apR[2];
    int len[L] {}, apLen[4] {};
    float fb[L] {}, outGain[L] {}, lp[L] {};
    float lpCoef = 0.3f;
    int writePos = 0, preWrite = 0, preLen = 1, apPos = 0;
};
} // namespace resonique
