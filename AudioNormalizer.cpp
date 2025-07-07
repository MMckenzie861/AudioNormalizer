#include <algorithm>
#include <iostream>
#include <fstream>
#include <vector>
#include <filesystem>
#include <string>
#include <limits>
#include <cmath>
#include <cstring>
#include <map>

namespace fs = std::filesystem;
using namespace std;

#pragma pack(push, 1)
struct RIFFHeader
{
    char chunkID[4];       // "RIFF"
    uint32_t chunkSize;
    char format[4];        // "WAVE"
};

struct ChunkHeader
{
    char id[4];
    uint32_t size;
};

struct FmtSubchunk
{
    uint16_t audioFormat;
    uint16_t numChannels;
    uint32_t sampleRate;
    uint32_t byteRate;
    uint16_t blockAlign;
    uint16_t bitsPerSample;
};
#pragma pack(pop)

bool matchTag(const char* id, const char* tag)
{
    return memcmp(id, tag, 4) == 0;
}

double computeMaxAmplitude(const char* data, uint32_t size, const FmtSubchunk* fmt)
{
    double maxAmp = 0.0;

    if (fmt->audioFormat != 1)
    {
        cerr << "Unsupported audio format (only PCM supported)." << endl;
        return 0;
    }

    uint16_t bps = fmt->bitsPerSample;
    uint16_t channels = fmt->numChannels;

    const char* ptr = data;
    const char* end = data + size;

    while (ptr < end)
    {
        for (int ch = 0; ch < channels && ptr < end; ++ch)
        {
            double sample = 0;

            if (bps <= 8)
            {
                uint8_t val = *reinterpret_cast<const uint8_t*>(ptr);
                sample = std::abs((int)val - 128) / 128.0;
                ptr += 1;
            }
            else if (bps <= 16)
            {
                int16_t val = *reinterpret_cast<const int16_t*>(ptr);
                sample = std::abs(val) / 32768.0;
                ptr += 2;
            }
            else if (bps <= 24)
            {
                int32_t val = ptr[0] | (ptr[1] << 8) | (ptr[2] << 16);
                if (val & 0x800000) val |= 0xFF000000; // sign extend
                sample = std::abs(val) / 8388608.0;
                ptr += 3;
            }
            else if (bps <= 32)
            {
                int32_t val = *reinterpret_cast<const int32_t*>(ptr);
                sample = std::abs(val) / static_cast<double>(INT32_MAX);
                ptr += 4;
            }
            else
            {
                cerr << "Unsupported bit depth: " << bps << endl;
                return 0;
            }

            if (sample > maxAmp)
            {
                maxAmp = sample;
            }
        }
    }

    return maxAmp;
}

double processWavFile(const fs::path& path, double& outMaxAmp) {
    ifstream infile(path, ios::binary);
    if (!infile)
    {
        return 0;
    }

    vector buffer((istreambuf_iterator<char>(infile)), istreambuf_iterator<char>());
    const char* ptr = buffer.data();
    const char* end = ptr + buffer.size();

    if (end - ptr < sizeof(RIFFHeader))
    {
        return 0;
    }

    const auto* riff = reinterpret_cast<const RIFFHeader*>(ptr);

    if (!matchTag(riff->chunkID, "RIFF") || !matchTag(riff->format, "WAVE"))
    {
        return 0;
    }

    ptr += sizeof(RIFFHeader);

    const FmtSubchunk* fmt = nullptr;
    const char* dataPtr = nullptr;
    uint32_t dataSize = 0;

    while (ptr + sizeof(ChunkHeader) <= end)
    {
        const auto* chunk = reinterpret_cast<const ChunkHeader*>(ptr);
        const char* chunkData = ptr + sizeof(ChunkHeader);

        if (chunkData + chunk->size > end)
        {
            break;
        }

        if (matchTag(chunk->id, "fmt "))
        {
            fmt = reinterpret_cast<const FmtSubchunk*>(chunkData);
        }
        else if (matchTag(chunk->id, "data"))
        {
            dataPtr = chunkData;
            dataSize = chunk->size;
            break;
        }

        ptr = chunkData + chunk->size;
        if (chunk->size % 2 != 0) ptr += 1;
    }

    if (!fmt || !dataPtr) return 0;

    outMaxAmp = computeMaxAmplitude(dataPtr, dataSize, fmt);
    return outMaxAmp;
}

bool normalizeWav(const fs::path& inputPath, const fs::path& outputPath, double gainFactor) {
    ifstream infile(inputPath, ios::binary);
    if (!infile) return false;

    vector<char> buffer((istreambuf_iterator<char>(infile)), istreambuf_iterator<char>());
    char* ptr = buffer.data();
    char* end = ptr + buffer.size();

    if (end - ptr < sizeof(RIFFHeader)) {
        return false;
    }

    auto* riff = reinterpret_cast<RIFFHeader*>(ptr);

    if (!matchTag(riff->chunkID, "RIFF") || !matchTag(riff->format, "WAVE")) {
        cerr << "File is not a proper Wav file, missing tag" << endl;
        return false;
    }
    ptr += sizeof(RIFFHeader);

    FmtSubchunk* fmt = nullptr;
    char* dataPtr = nullptr;
    uint32_t dataSize = 0;

    while (ptr + sizeof(ChunkHeader) <= end) {
        auto* chunk = reinterpret_cast<ChunkHeader*>(ptr);
        char* chunkData = ptr + sizeof(ChunkHeader);

        if (chunkData + chunk->size > end) break;

        if (matchTag(chunk->id, "fmt ")) {
            fmt = reinterpret_cast<FmtSubchunk*>(chunkData);
        } else if (matchTag(chunk->id, "data")) {
            dataPtr = chunkData;
            dataSize = chunk->size;
            break;
        }

        ptr = chunkData + chunk->size;
        if (chunk->size % 2 != 0) ptr += 1;
    }

    if (!fmt || !dataPtr) return false;

    // Apply normalization in-place
    uint16_t bps = fmt->bitsPerSample;
    uint16_t channels = fmt->numChannels;
    char* audioPtr = dataPtr;
    char* audioEnd = dataPtr + dataSize;

    while (audioPtr < audioEnd)
    {
        for (int ch = 0; ch < channels && audioPtr < audioEnd; ++ch)
        {
            if (bps <= 8)
            {
                uint8_t& val = *reinterpret_cast<uint8_t*>(audioPtr);
                int s = static_cast<int>(val) - 128;
                s = static_cast<int>(round(s * gainFactor));
                s = std::clamp(s, -128, 127);
                val = static_cast<uint8_t>(s + 128);
                audioPtr += 1;
            }
            else if (bps <= 16)
            {
                int16_t& val = *reinterpret_cast<int16_t*>(audioPtr);
                int s = static_cast<int>(round(val * gainFactor));
                val = std::clamp(s, -32768, 32767);
                audioPtr += 2;
            }
            else if (bps <= 24)
            {
                int32_t val = audioPtr[0] | (audioPtr[1] << 8) | (audioPtr[2] << 16);
                if (val & 0x800000) val |= 0xFF000000;
                val = static_cast<int>(round(val * gainFactor));
                val = std::clamp(val, -8388608, 8388607);
                audioPtr[0] = val & 0xFF;
                audioPtr[1] = (val >> 8) & 0xFF;
                audioPtr[2] = (val >> 16) & 0xFF;
                audioPtr += 3;
            }
            else if (bps <= 32)
            {
                int32_t& val = *reinterpret_cast<int32_t*>(audioPtr);
                int64_t s = static_cast<int64_t>(round(val * gainFactor));
                s = std::clamp(s, static_cast<int64_t>(INT32_MIN), static_cast<int64_t>(INT32_MAX));
                val = static_cast<int32_t>(s);
                audioPtr += 4;
            }
        }
    }

    ofstream outfile(outputPath, ios::binary);
    outfile.write(buffer.data(), buffer.size());
    return true;
}

int main(int argc, char* argv[])
{
    fs::path dir;

    if (argc <= 1)
    {
        cerr << "Usage: Pass in a directory path" << endl;
    }
    else if (argc >= 2)
    {
        dir = argv[1];
    }

    double globalMax = 0;
    string loudestFile;
    map<fs::path, double> fileAmplitudes;

    for (const auto& entry : fs::directory_iterator(dir))
    {
        if (entry.path().extension() == ".wav")
        {
            ifstream infile(entry.path(), ios::binary);
            vector<char> buffer((istreambuf_iterator<char>(infile)), istreambuf_iterator<char>());
            const char* ptr = buffer.data();
            const char* end = ptr + buffer.size();

            ptr += sizeof(RIFFHeader);
            const FmtSubchunk* fmt = nullptr;
            const char* dataPtr = nullptr;
            uint32_t dataSize = 0;

            while (ptr + sizeof(ChunkHeader) <= end)
            {
                const auto* chunk = reinterpret_cast<const ChunkHeader*>(ptr);
                const char* chunkData = ptr + sizeof(ChunkHeader);

                if (chunkData + chunk->size > end)
                {
                    break;
                }

                if (matchTag(chunk->id, "fmt "))
                {
                    fmt = reinterpret_cast<const FmtSubchunk*>(chunkData);
                }
                else if (matchTag(chunk->id, "data"))
                {
                    dataPtr = chunkData;
                    dataSize = chunk->size;
                    break;
                }
                else
                {
                    cerr << "Unknown chunk: " << chunk->id << endl;
                }

                ptr = chunkData + chunk->size;
                if (chunk->size % 2 != 0)
                {
                    ptr += 1;
                }
            }

            if (fmt && dataPtr)
            {
                double amp = computeMaxAmplitude(dataPtr, dataSize, fmt);
                fileAmplitudes[entry.path()] = amp;
                if (amp > globalMax)
                {
                    globalMax = amp;
                    loudestFile = entry.path().filename().string();
                }
            }
        }
    }

    cout << "\nLoudest File: " << loudestFile << "\nPeak Amplitude: " << globalMax << endl;

    for (const auto& [path, amp] : fileAmplitudes)
    {
        if (amp == 0 || amp == globalMax) continue;
        double gain = globalMax / amp;
        fs::path outPath = path.parent_path() / ("normalized_" + path.filename().string());

        cout << "Normalizing " << path.filename() << " -> " << outPath.filename()
             << " (gain: " << gain << ")" << endl;

        normalizeWav(path, outPath, gain);
    }

    return 0;
}
