#pragma once

#include <string>
#include <unordered_map>
#include <vector>

class NrcEmotionLexicon
{
public:
    static constexpr int DIMENSIONS = 10;

    // Fixed order used throughout the app and output.
    // 0..9: anger, anticipation, disgust, fear, joy, sadness, surprise, trust, negative, positive
    static const char* CATEGORY_NAMES[DIMENSIONS];

    struct Scores
    {
        double values[DIMENSIONS]{};

        void clear()
        {
            for (double& v : values) v = 0.0;
        }

        double& operator[](int i) { return values[i]; }
        const double& operator[](int i) const { return values[i]; }
    };

    // Loads the NRC lexicon file.
    // Supports standard NRC format lines like:
    //   word<TAB>emotion<TAB>0|1
    bool loadFromFile(const std::string& path);

    // Scores a token list (already split into words).
    // Adds counts into outScores.
    void scoreWords(const std::vector<std::string>& words, Scores& outScores) const;

private:
    // word -> [10] flags/counts (0 or 1 per category in the lexicon)
    std::unordered_map<std::string, int> m_wordToMaskIndex;
    std::vector<int> m_masks; // flattened DIMENSIONS-sized blocks
};
