#include "vader_sentiment.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <sstream>

// Constants taken from original VADER.
static constexpr double B_INCR   = 0.293;
static constexpr double B_DECR   = -0.293;
static constexpr double C_INCR   = 0.733;
static constexpr double N_SCALAR = -0.74;


// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

std::string VaderSentiment::toLower(const std::string& s)
{
    std::string out = s;
    for (char& ch : out)
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return out;
}

bool VaderSentiment::isUpper(const std::string& s)
{
    bool hasAlpha = false;

    for (char ch : s)
    {
        if (std::isalpha(static_cast<unsigned char>(ch)))
        {
            hasAlpha = true;
            if (!std::isupper(static_cast<unsigned char>(ch)))
                return false;
        }
    }
    return hasAlpha;
}

// Strip outer punctuation but try to preserve short tokens (emoticons, etc.).
static std::string stripPuncIfWord(const std::string& token)
{
    std::size_t start = 0;
    std::size_t end   = token.size();

    while (start < end &&
           std::ispunct(static_cast<unsigned char>(token[start])))
        ++start;

    while (end > start &&
           std::ispunct(static_cast<unsigned char>(token[end - 1])))
        --end;

    std::string stripped = token.substr(start, end - start);

    if (stripped.size() <= 2)
        return token;

    return stripped;
}

std::vector<std::string> VaderSentiment::tokenizeWordsAndEmoticons(const std::string& text)
{
    std::vector<std::string> tokens;
    std::istringstream iss(text);
    std::string token;

    while (iss >> token)
    {
        std::string t = stripPuncIfWord(token);
        tokens.push_back(t);
    }
    return tokens;
}

bool VaderSentiment::allCapDifferential(const std::vector<std::string>& words)
{
    int allCaps = 0;
    for (const auto& w : words)
    {
        if (isUpper(w))
            ++allCaps;
    }

    int capDiff = static_cast<int>(words.size()) - allCaps;
    return (capDiff > 0 && capDiff < static_cast<int>(words.size()));
}

const std::vector<std::string>& VaderSentiment::negationWords()
{
    static const std::vector<std::string> NEGATE = {
        "aint","arent","cannot","cant","couldnt","darent","didnt","doesnt",
        "ain't","aren't","can't","couldn't","daren't","didn't","doesn't",
        "dont","hadnt","hasnt","havent","isnt","mightnt","mustnt","neither",
        "don't","hadn't","hasn't","haven't","isn't","mightn't","mustn't",
        "neednt","needn't","never","none","nope","nor","not","nothing","nowhere",
        "oughtnt","shant","shouldnt","uhuh","wasnt","werent",
        "oughtn't","shan't","shouldn't","uh-uh","wasn't","weren't",
        "without","wont","wouldnt","won't","wouldn't","rarely","seldom","despite"
    };
    return NEGATE;
}

bool VaderSentiment::isNegated(const std::vector<std::string>& words,
                               bool includeNt)
{
    std::vector<std::string> lower;
    lower.reserve(words.size());
    for (const auto& w : words)
        lower.push_back(toLower(w));

    const auto& negs = negationWords();
    for (const auto& n : negs)
    {
        if (std::find(lower.begin(), lower.end(), n) != lower.end())
            return true;
    }

    if (includeNt)
    {
        for (const auto& w : lower)
        {
            if (w.find("n't") != std::string::npos)
                return true;
        }
    }

    return false;
}

double VaderSentiment::normalizeScore(double score, double alpha)
{
    double norm = score / std::sqrt(score * score + alpha);
    if (norm < -1.0) return -1.0;
    if (norm >  1.0) return  1.0;
    return norm;
}

const std::unordered_map<std::string, double>& VaderSentiment::boosterDict()
{
    static const std::unordered_map<std::string, double> BOOSTERS = {
        // boosters
        {"absolutely", B_INCR}, {"amazingly", B_INCR}, {"awfully", B_INCR},
        {"completely", B_INCR}, {"decidedly", B_INCR}, {"deeply", B_INCR},
        {"enormously", B_INCR}, {"entirely", B_INCR}, {"especially", B_INCR},
        {"extremely", B_INCR}, {"fabulously", B_INCR}, {"highly", B_INCR},
        {"incredibly", B_INCR}, {"intensely", B_INCR}, {"really", B_INCR},
        {"remarkably", B_INCR}, {"so", B_INCR}, {"thoroughly", B_INCR},
        {"totally", B_INCR}, {"tremendously", B_INCR}, {"uber", B_INCR},
        {"unbelievably", B_INCR}, {"utterly", B_INCR}, {"very", B_INCR},

        // dampeners
        {"almost", B_DECR}, {"barely", B_DECR}, {"hardly", B_DECR},
        {"just enough", B_DECR}, {"kind of", B_DECR}, {"kinda", B_DECR},
        {"less", B_DECR}, {"little", B_DECR}, {"marginally", B_DECR},
        {"occasionally", B_DECR}, {"partly", B_DECR}, {"scarcely", B_DECR},
        {"slightly", B_DECR}, {"somewhat", B_DECR}, {"sort of", B_DECR}
    };
    return BOOSTERS;
}

double VaderSentiment::scalarIncDec(const std::string& word,
                                    double valence,
                                    bool isCapDiff)
{
    double scalar = 0.0;
    std::string lower = toLower(word);

    const auto& boosters = boosterDict();
    auto it = boosters.find(lower);
    if (it != boosters.end())
    {
        scalar = it->second;

        if (valence < 0.0)
            scalar *= -1.0;

        if (isUpper(word) && isCapDiff)
        {
            if (valence > 0.0) scalar += C_INCR;
            else               scalar -= C_INCR;
        }
    }

    return scalar;
}

double VaderSentiment::amplifyExclamation(const std::string& text)
{
    int count = 0;
    for (char ch : text)
        if (ch == '!') ++count;

    if (count > 4) count = 4;
    return count * 0.292;
}

double VaderSentiment::amplifyQuestion(const std::string& text)
{
    int count = 0;
    for (char ch : text)
        if (ch == '?') ++count;

    if (count > 1)
    {
        if (count <= 3)
            return count * 0.18;
        return 0.96;
    }
    return 0.0;
}

void VaderSentiment::siftSentimentScores(const std::vector<double>& sentiments,
                                         double& posSum,
                                         double& negSum,
                                         int& neuCount)
{
    posSum   = 0.0;
    negSum   = 0.0;
    neuCount = 0;

    for (double s : sentiments)
    {
        if (s > 0.0)
            posSum += (s + 1.0);
        else if (s < 0.0)
            negSum += (s - 1.0);
        else
            ++neuCount;
    }
}

void VaderSentiment::butCheck(const std::vector<std::string>& words,
                              std::vector<double>& sentiments)
{
    std::vector<std::string> lower;
    lower.reserve(words.size());
    for (const auto& w : words)
        lower.push_back(toLower(w));

    auto it = std::find(lower.begin(), lower.end(), "but");
    if (it == lower.end())
        return;

    std::size_t idx = static_cast<std::size_t>(it - lower.begin());

    for (std::size_t i = 0; i < sentiments.size(); ++i)
    {
        if (i < idx)
            sentiments[i] *= 0.5;
        else if (i > idx)
            sentiments[i] *= 1.5;
    }
}

// Matches original VADER: "least happy" is negated,
// "at least happy" and "very least happy" are not.
double VaderSentiment::leastCheck(double valence,
                                  const std::vector<std::string>& words,
                                  std::size_t i)
{
    if (i == 0) return valence;

    std::vector<std::string> lower;
    lower.reserve(words.size());
    for (const auto& w : words)
        lower.push_back(toLower(w));

    if (lower[i - 1] == "least")
    {
        if (i > 1)
        {
            if (lower[i - 2] != "at" && lower[i - 2] != "very")
                valence *= N_SCALAR;
        }
        else
        {
            valence *= N_SCALAR;
        }
    }

    return valence;
}

double VaderSentiment::negationCheck(double valence,
                                     const std::vector<std::string>& words,
                                     int start_i,
                                     std::size_t i)
{
    std::vector<std::string> lower;
    lower.reserve(words.size());
    for (const auto& w : words)
        lower.push_back(toLower(w));

    if (start_i == 0)
    {
        if (i > 0 && isNegated({ lower[i - 1] }))
            valence *= N_SCALAR;
    }
    else if (start_i == 1)
    {
        if (i > 1 &&
            lower[i - 2] == "never" &&
            (lower[i - 1] == "so" || lower[i - 1] == "this"))
        {
            valence *= 1.25;
        }
        else if (i > 1 &&
                 lower[i - 2] == "without" &&
                 lower[i - 1] == "doubt")
        {
            // treated as intensifier
        }
        else if (i > 1 && isNegated({ lower[i - 2] }))
        {
            valence *= N_SCALAR;
        }
    }
    else if (start_i == 2)
    {
        if (i > 2 &&
            lower[i - 3] == "never" &&
            (lower[i - 2] == "so" || lower[i - 2] == "this" ||
             lower[i - 1] == "so" || lower[i - 1] == "this"))
        {
            valence *= 1.25;
        }
        else if (i > 2 &&
                 lower[i - 3] == "without" &&
                 (lower[i - 2] == "doubt" || lower[i - 1] == "doubt"))
        {
            // treated as intensifier
        }
        else if (i > 2 && isNegated({ lower[i - 3] }))
        {
            valence *= N_SCALAR;
        }
    }

    return valence;
}

double VaderSentiment::specialIdiomsCheck(double valence,
                                          const std::vector<std::string>& words,
                                          std::size_t i)
{
    static const std::unordered_map<std::string, double> SPECIAL_CASES = {
        {"the shit",       3.0},
        {"the bomb",       3.0},
        {"bad ass",        1.5},
        {"badass",         1.5},
        {"bus stop",       0.0},
        {"yeah right",    -2.0},
        {"kiss of death", -1.5},
        {"to die for",     3.0},
        {"beating heart",  3.1},
        {"broken heart",  -2.9}
    };

    std::vector<std::string> lower;
    lower.reserve(words.size());
    for (const auto& w : words)
        lower.push_back(toLower(w));

    auto findSeq = [&](const std::string& seq) -> double {
        auto it = SPECIAL_CASES.find(seq);
        if (it != SPECIAL_CASES.end())
            return it->second;
        return 0.0;
    };

    if (i >= 1)
    {
        std::string onezero = lower[i - 1] + " " + lower[i];
        double v = findSeq(onezero);
        if (v != 0.0) return v;
    }
    if (i >= 2)
    {
        std::string twoonezero =
            lower[i - 2] + " " + lower[i - 1] + " " + lower[i];
        double v = findSeq(twoonezero);
        if (v != 0.0) return v;

        std::string twoone = lower[i - 2] + " " + lower[i - 1];
        v = findSeq(twoone);
        if (v != 0.0) return v;
    }
    if (i >= 3)
    {
        std::string threetwoone =
            lower[i - 3] + " " + lower[i - 2] + " " + lower[i - 1];
        double v = findSeq(threetwoone);
        if (v != 0.0) return v;

        std::string threetwo = lower[i - 3] + " " + lower[i - 2];
        v = findSeq(threetwo);
        if (v != 0.0) return v;
    }

    if (i + 1 < lower.size())
    {
        std::string zeroone = lower[i] + " " + lower[i + 1];
        double v = findSeq(zeroone);
        if (v != 0.0) return v;
    }
    if (i + 2 < lower.size())
    {
        std::string zeroonetwo =
            lower[i] + " " + lower[i + 1] + " " + lower[i + 2];
        double v = findSeq(zeroonetwo);
        if (v != 0.0) return v;
    }

    // Booster bigrams behind the current token.
    const auto& boosters = boosterDict();
    if (i >= 2)
    {
        std::string twoone = lower[i - 2] + " " + lower[i - 1];
        auto it = boosters.find(twoone);
        if (it != boosters.end())
            valence += it->second;
    }

    return valence;
}


// ---------------------------------------------------------------------------
// Core scoring
// ---------------------------------------------------------------------------

void VaderSentiment::sentimentValence(double& valence,
                                      const std::vector<std::string>& words,
                                      std::size_t i,
                                      std::vector<double>& sentiments) const
{
    bool isCapDiff = allCapDifferential(words);
    const std::string& item = words[i];
    std::string lowerItem   = toLower(item);

    auto itLex = lexicon_.find(lowerItem);
    if (itLex == lexicon_.end())
    {
        sentiments.push_back(0.0);
        return;
    }

    valence = itLex->second;

    if (lowerItem == "no" && i + 1 < words.size())
    {
        std::string nextLower = toLower(words[i + 1]);
        if (lexicon_.find(nextLower) != lexicon_.end())
            valence = 0.0;
    }

    if (i > 0 && toLower(words[i - 1]) == "no")
        valence = itLex->second * N_SCALAR;
    else if (i > 1 && toLower(words[i - 2]) == "no")
        valence = itLex->second * N_SCALAR;
    else if (i > 2 &&
             toLower(words[i - 3]) == "no" &&
             (toLower(words[i - 1]) == "or" ||
              toLower(words[i - 1]) == "nor"))
        valence = itLex->second * N_SCALAR;

    if (isUpper(item) && isCapDiff)
    {
        if (valence > 0.0) valence += C_INCR;
        else               valence -= C_INCR;
    }

    for (int start_i = 0; start_i < 3; ++start_i)
    {
        if (i > static_cast<std::size_t>(start_i))
        {
            std::size_t backIdx = i - (start_i + 1);
            std::string prev     = words[backIdx];
            std::string prevLower = toLower(prev);

            if (lexicon_.find(prevLower) == lexicon_.end())
            {
                double s = scalarIncDec(prev, valence, isCapDiff);
                if (start_i == 1 && s != 0.0) s *= 0.95;
                if (start_i == 2 && s != 0.0) s *= 0.90;

                valence += s;
                valence  = negationCheck(valence, words, start_i, i);

                if (start_i == 2)
                    valence = specialIdiomsCheck(valence, words, i);
            }
        }
    }

    valence = leastCheck(valence, words, i);
    sentiments.push_back(valence);
}

void VaderSentiment::polarityScores(const std::string& text,
                                    double& neg,
                                    double& neu,
                                    double& pos,
                                    double& compound) const
{
    if (lexicon_.empty())
    {
        neg = neu = pos = compound = 0.0;
        return;
    }

    std::vector<std::string> words = tokenizeWordsAndEmoticons(text);
    if (words.empty())
    {
        neg = neu = pos = compound = 0.0;
        return;
    }

    std::vector<double> sentiments;
    sentiments.reserve(words.size());

    const auto& boosters = boosterDict();

    for (std::size_t i = 0; i < words.size(); ++i)
    {
        std::string lower = toLower(words[i]);

        if (boosters.find(lower) != boosters.end())
        {
            sentiments.push_back(0.0);
            continue;
        }

        if (i < words.size() - 1 &&
            lower == "kind" &&
            toLower(words[i + 1]) == "of")
        {
            sentiments.push_back(0.0);
            continue;
        }

        double valence = 0.0;
        sentimentValence(valence, words, i, sentiments);
    }

    butCheck(words, sentiments);

    double sumS = 0.0;
    for (double s : sentiments) sumS += s;

    double punctEmph = amplifyExclamation(text) + amplifyQuestion(text);
    if (sumS > 0.0)      sumS += punctEmph;
    else if (sumS < 0.0) sumS -= punctEmph;

    compound = normalizeScore(sumS);

    double posSum, negSum;
    int    neuCount;
    siftSentimentScores(sentiments, posSum, negSum, neuCount);

    if (posSum > std::fabs(negSum))
        posSum += punctEmph;
    else if (posSum < std::fabs(negSum))
        negSum -= punctEmph;

    double total = posSum + std::fabs(negSum) + neuCount;
    if (total == 0.0)
    {
        neg = neu = pos = compound = 0.0;
        return;
    }

    pos = std::fabs(posSum / total);
    neg = std::fabs(negSum / total);
    neu = std::fabs(static_cast<double>(neuCount) / total);

    auto round3 = [](double x) { return std::round(x * 1000.0) / 1000.0; };
    auto round4 = [](double x) { return std::round(x * 10000.0) / 10000.0; };

    pos      = round3(pos);
    neg      = round3(neg);
    neu      = round3(neu);
    compound = round4(compound);
}

double VaderSentiment::compoundScore(const std::string& text) const
{
    double neg, neu, pos, compound;
    polarityScores(text, neg, neu, pos, compound);
    return compound;
}


// ---------------------------------------------------------------------------
// Lexicon loading
// ---------------------------------------------------------------------------

bool VaderSentiment::loadLexicon(const std::string& filepath)
{
    lexicon_.clear();

    std::ifstream in(filepath);
    if (!in)
        return false;

    std::string line;
    while (std::getline(in, line))
    {
        if (line.empty())
            continue;

        std::istringstream iss(line);
        std::string word;
        double      score;

        if (!(iss >> word >> score))
            continue;

        lexicon_[toLower(word)] = score;
    }

    return !lexicon_.empty();
}
