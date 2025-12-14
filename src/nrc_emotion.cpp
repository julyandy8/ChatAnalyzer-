#include "nrc_emotion.hpp"

#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <cctype>

const char* NrcEmotionLexicon::CATEGORY_NAMES[DIMENSIONS] = {
    "anger",
    "anticipation",
    "disgust",
    "fear",
    "joy",
    "sadness",
    "surprise",
    "trust",
    "negative",
    "positive"
};

static std::string toLowerAscii(std::string s)
{
    for (char& c : s)
        c = (char)std::tolower((unsigned char)c);
    return s;
}

bool NrcEmotionLexicon::loadFromFile(const std::string& path)
{
    m_wordToMaskIndex.clear();
    m_masks.clear();

    std::ifstream in(path);
    if (!in)
    {
        std::cerr << "NrcEmotionLexicon: failed to open file: " << path << "\n";
        return false;
    }

    std::string line;
    long long assigned = 0;

    while (std::getline(in, line))
    {
        if (line.empty())
            continue;

        // Expect: word \t category \t 0|1
        std::istringstream iss(line);
        std::string word, cat, flagStr;

        if (!std::getline(iss, word, '\t')) continue;
        if (!std::getline(iss, cat,  '\t')) continue;
        if (!std::getline(iss, flagStr))    continue;

        word = toLowerAscii(word);
        cat  = toLowerAscii(cat);

        int flag = 0;
        try { flag = std::stoi(flagStr); } catch (...) { flag = 0; }
        if (flag != 1) continue; // only keep positive associations

        // Map category -> index
        int catIndex = -1;
        for (int i = 0; i < DIMENSIONS; ++i)
        {
            if (cat == CATEGORY_NAMES[i])
            {
                catIndex = i;
                break;
            }
        }
        if (catIndex < 0)
            continue;

        auto it = m_wordToMaskIndex.find(word);
        if (it == m_wordToMaskIndex.end())
        {
            // new word: allocate 10 slots
            int idx = (int)(m_masks.size() / DIMENSIONS);
            m_wordToMaskIndex[word] = idx;
            m_masks.resize(m_masks.size() + DIMENSIONS, 0);
            it = m_wordToMaskIndex.find(word);
        }

        int base = it->second * DIMENSIONS;
        m_masks[base + catIndex] = 1;
        ++assigned;
    }

    if (m_wordToMaskIndex.empty())
    {
        std::cerr << "NrcEmotionLexicon: loaded 0 entries from: " << path << "\n";
        return false;
    }

    // Optional: you can print assigned if you want debug noise
    // std::cerr << "NrcEmotionLexicon: loaded " << m_wordToMaskIndex.size()
    //           << " words (" << assigned << " associations)\n";

    return true;
}

void NrcEmotionLexicon::scoreWords(const std::vector<std::string>& words, Scores& outScores) const
{
    for (const std::string& raw : words)
    {
        if (raw.empty()) continue;

        std::string w = toLowerAscii(raw);

        auto it = m_wordToMaskIndex.find(w);
        if (it == m_wordToMaskIndex.end())
            continue;

        int base = it->second * DIMENSIONS;
        for (int i = 0; i < DIMENSIONS; ++i)
        {
            if (m_masks[base + i])
                outScores.values[i] += 1.0;
        }
    }
}
