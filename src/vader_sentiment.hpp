#pragma once

#include <string>
#include <unordered_map>
#include <vector>

// Lightweight VADER-style sentiment analyzer.
// Scores are similar to the original Python implementation.
class VaderSentiment
{
public:
    VaderSentiment() = default;

    // Load lexicon from a VADER-style file:
    // word <whitespace> score
    bool loadLexicon(const std::string& filepath);

    // Convenience: return only the compound score in [-1, 1].
    double compoundScore(const std::string& text) const;

    // Compute sentiment scores:
    //   neg, neu, pos ∈ [0, 1], roughly summing to 1
    //   compound      ∈ [-1, 1]
    void polarityScores(const std::string& text,
                        double& neg,
                        double& neu,
                        double& pos,
                        double& compound) const;

private:
    // Lexicon entries are stored in lowercase.
    std::unordered_map<std::string, double> lexicon_;

    // Text helpers
    static std::string toLower(const std::string& s);
    static bool isUpper(const std::string& s);
    static bool allCapDifferential(const std::vector<std::string>& words);
    static std::vector<std::string> tokenizeWordsAndEmoticons(const std::string& text);
    static bool isNegated(const std::vector<std::string>& words, bool includeNt = true);

    // Map summed score into [-1, 1].
    static double normalizeScore(double score, double alpha = 15.0);

    // Booster/dampener effect for words such as "very", "barely", etc.
    static double scalarIncDec(const std::string& word,
                               double valence,
                               bool isCapDiff);

    // Sentence-level punctuation emphasis.
    static double amplifyExclamation(const std::string& text);
    static double amplifyQuestion(const std::string& text);

    // Split sentiments into positive, negative, and neutral tallies.
    static void siftSentimentScores(const std::vector<double>& sentiments,
                                    double& posSum,
                                    double& negSum,
                                    int& neuCount);

    // Adjust sentiments around contrastive conjunctions (e.g., "but").
    static void butCheck(const std::vector<std::string>& words,
                         std::vector<double>& sentiments);

    // Handle "least" phrases ("least happy", "at least", etc.).
    static double leastCheck(double valence,
                             const std::vector<std::string>& words,
                             std::size_t i);

    // Apply negation rules within a small window behind the current token.
    static double negationCheck(double valence,
                                const std::vector<std::string>& words,
                                int start_i,
                                std::size_t i);

    // Override valence for special multi-word expressions.
    static double specialIdiomsCheck(double valence,
                                     const std::vector<std::string>& words,
                                     std::size_t i);

    // Compute valence for a single token.
    void sentimentValence(double& valence,
                          const std::vector<std::string>& words,
                          std::size_t i,
                          std::vector<double>& sentiments) const;

    // Booster/dampener table and negation list.
    static const std::unordered_map<std::string, double>& boosterDict();
    static const std::vector<std::string>& negationWords();
};
