#include <iostream>
#include <fstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <filesystem>
#include <algorithm>
#include <cctype>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <map>
#include <ctime>
#include <shobjidl.h> 
#include <objbase.h>    

#include "json.hpp"
#include "vader_sentiment.hpp"
#include "nrc_emotion.hpp"

#ifdef _WIN32
#include <windows.h>
#endif

using json = nlohmann::json;
namespace fs = std::filesystem;

// -------------------------------------------------------------
// Shared structs & globals (for gui)
// -------------------------------------------------------------
struct MonthlyEmotionPoint {
    int year;
    int month;
    double avgCompound;
};

struct MonthlyCountPoint {
    int year;
    int month;
    long long totalMessages;
};

struct MonthlyResponsePoint {
    int year;
    int month;
    double avgMinutes;
};

struct MonthlyRomanticPoint {
    int year;
    int month;
    long long romanticMessages;
};

struct MonthlyAvgLengthPoint {
    int year;
    int month;
    double avgWords;
};

// Per-user series for charts
struct UserMonthlyCountPoint {
    int year;
    int month;
    long long totalMessages;
};

struct UserMonthlyEmotionPoint {
    int year;
    int month;
    double avgCompound;
};

struct UserMonthlyResponsePoint {
    int year;
    int month;
    double avgMinutes;
};

struct UserMonthlyRomanticPoint {
    int year;
    int month;
    long long romanticMessages;
};

struct UserMonthlyAvgLengthPoint {
    int year;
    int month;
    double avgWords;
};

int  g_heatmapCounts[7][24] = { 0 };
bool g_heatmapReady = false;

std::vector<MonthlyEmotionPoint>      g_monthlyEmotionPoints;
std::vector<MonthlyCountPoint>        g_monthlyCountPoints;
std::vector<MonthlyResponsePoint>     g_monthlyResponsePoints;
std::vector<MonthlyRomanticPoint>     g_monthlyRomanticPoints;
std::vector<MonthlyAvgLengthPoint>    g_monthlyAvgLengthPoints;

std::vector<std::string> g_chartUserNames;

std::vector<std::vector<UserMonthlyCountPoint>>      g_userMonthlyCountSeries;
std::vector<std::vector<UserMonthlyEmotionPoint>>    g_userMonthlyEmotionSeries;
std::vector<std::vector<UserMonthlyResponsePoint>>   g_userMonthlyResponseSeries;
std::vector<std::vector<UserMonthlyRomanticPoint>>   g_userMonthlyRomanticSeries;
std::vector<std::vector<UserMonthlyAvgLengthPoint>>  g_userMonthlyAvgLengthSeries;

// Internal accumulators
struct MonthlyAggregate {
    double      sumCompound = 0.0;
    long long   count       = 0;
};

struct MonthlyLengthAgg {
    long long sumWords = 0;
    long long msgCount = 0;
};

struct MonthlyResponseAgg {
    long long sumMs  = 0;
    long long count  = 0;
};

// Monthly total message counts (for "Messages per Month" chart)
static std::map<std::pair<int,int>, MonthlyAggregate> g_monthlyAggregates;
static std::map<std::pair<int,int>, long long>        g_monthlyMessageCounts;

// Per-user monthly totals and emotion aggregates
static std::map<std::string, std::map<std::pair<int,int>, long long>>      g_perUserMonthlyMessageCounts;
static std::map<std::string, std::map<std::pair<int,int>, MonthlyAggregate>> g_perUserMonthlyEmotion;

// New monthly accumulators
static std::map<std::pair<int,int>, MonthlyResponseAgg>                               g_monthlyResponseAgg;
static std::map<std::string, std::map<std::pair<int,int>, MonthlyResponseAgg>>       g_perUserMonthlyResponseAgg;

static std::map<std::pair<int,int>, long long>                                       g_monthlyRomanticCounts;
static std::map<std::string, std::map<std::pair<int,int>, long long>>               g_perUserMonthlyRomanticCounts;

static std::map<std::pair<int,int>, MonthlyLengthAgg>                                g_monthlyLengthAgg;
static std::map<std::string, std::map<std::pair<int,int>, MonthlyLengthAgg>>        g_perUserMonthlyLengthAgg;

// -------------------------------------------------------------
// NRC helper: category names
// -------------------------------------------------------------

static constexpr int NRC_DIM = NrcEmotionLexicon::DIMENSIONS;
static const char* NRC_CATEGORY_NAMES[NRC_DIM] = {
    "anger", "anticipation", "disgust", "fear", "joy",
    "sadness", "surprise", "trust", "negative", "positive"
};

// -------------------------------------------------------------
// Helper structs
// -------------------------------------------------------------
struct UserStats {
    long long totalMessages = 0;
    long long totalWords    = 0;
    long long wordMessages  = 0;
    std::vector<long long> messageWordLengths;

    std::unordered_map<std::string, long long> wordFrequency;

    long long romanticMessages     = 0;
    long long conversationsStarted = 0;

    long long totalResponseTimeMs  = 0;
    long long responseCount        = 0;

    long long doubleTextRuns       = 0;
    long long tripleTextRuns       = 0;
    long long yappingRuns          = 0; 

    long long reactionsSent        = 0;

    double vaderPosSum       = 0.0;
    double vaderNegSum       = 0.0;
    double vaderNeuSum       = 0.0;
    double vaderCompoundSum  = 0.0;
    long long vaderSamples   = 0;

    double nrcEmotionSums[NRC_DIM] = {};
    long long nrcTaggedTokens = 0;

    long long    longestMessageWords   = 0;
    std::string  longestMessageContent;
};

struct Message {
    std::string sender;
    long long   timestampMs = 0;
    std::string content;
};

static std::string TrimLower(std::string s)
{
    auto isSpace = [](unsigned char c) { return std::isspace(c) != 0; };

    while (!s.empty() && isSpace((unsigned char)s.front())) s.erase(s.begin());
    while (!s.empty() && isSpace((unsigned char)s.back()))  s.pop_back();

    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

static bool IsSystemPlaceholderMessage(const std::string& content)
{
    std::string t = TrimLower(content);

    // Keep strict to not delete accidentally important stuff
    return (t == "sent an attachment." ||
            t == "sent an attachment"  ||
            t == "liked a message."    ||
            t == "liked a message");
}


// -------------------------------------------------------------
// Stop words
// -------------------------------------------------------------
const std::unordered_set<std::string> STOP_WORDS = {
      "a","about","after","again","against","all","also","am","an","and","any",
    "are","as","at","be","because","been","before","being","below","between",
    "both","but","by","can","come","could","did","do","does","doing","down",
    "during","each","even","ever","every","few","for","from","further","get",
    "go","goes","got","had","has","have","having","he","her","here","hers",
    "herself","him","himself","his","how","i","if","in","into","is","it",
    "its","itself","just","know","let","like","made","make","makes","many",
    "may","me","might","more","most","much","must","my","myself","new","no",
    "nor","not","now","of","off","on","once","one","only","or","other","our",
    "ours","ourselves","out","over","own","perhaps","put","said","same",
    "say","says","see","seen","she","should","since","so","some","still",
    "such","take","taken","than","that","the","their","theirs","them",
    "themselves","then","there","these","they","thing","things","think",
    "this","those","through","to","too","under","until","up","use","used",
    "using","very","want","was","we","well","were","what","when","where",
    "which","while","who","whom","why","will","with","within","without",
    "would","yeah","yep","yes","yet","you","your","yours","yourself",
    "yourselves",

    // Chat-specific fillers, reactions, and low-information words
    "alright","anyway","aww","bc","bet","brb","bro","bruh","btw","cool","cuz",
    "dude","eh","fine","gonna","hah","haha","hahaha","hehe","hey","hi",
    "hmm","idc","idk","idek","im","jk","k","kk","lmao","lmfao","lol","loll",
    "lolol","man","maybe","nah","nice","now","ok","okay","omg","oof","oop",
    "pls","plz","pretty","prob","probably","really","right","rn","sure",
    "thanks","thank","thx","true","uh","uhh","ugh","um","well","whoa",
    "wow","wtf","yall","yup","ur",

    // Export noise (to avoid polluting analytics)
    "attachment","attachments","message","messages","reacted","sent","still"
};

// -------------------------------------------------------------
// Junk tokens
// -------------------------------------------------------------
const std::unordered_set<std::string> JUNK_TOKENS = {
    "don","t","ll","ve","re","im","id","ill","youre","youd",
    "attachment","attachments","sent","send"
};

// -------------------------------------------------------------
// Helpers
// -------------------------------------------------------------
std::string formatWithCommas(long long value) {
    bool negative = value < 0;
    unsigned long long v = static_cast<unsigned long long>(negative ? -value : value);
    std::string s = std::to_string(v);
    int insertPos = static_cast<int>(s.size()) - 3;
    while (insertPos > 0) {
        s.insert(static_cast<std::string::size_type>(insertPos), ",");
        insertPos -= 3;
    }
    if (negative) s.insert(s.begin(), '-');
    return s;
}


static fs::path GetExecutableDir()
{
#ifdef _WIN32
    char buf[MAX_PATH];
    DWORD len = GetModuleFileNameA(nullptr, buf, MAX_PATH);
    if (len == 0 || len == MAX_PATH)
        return fs::current_path();
    fs::path exePath(buf);
    return exePath.parent_path();
#else
    return fs::current_path();
#endif
}



static std::string PadNumberSuffix(const std::string& number,
                                   const std::string& suffix,
                                   int numberWidth)
{
    std::ostringstream oss;
    oss << std::right << std::setw(numberWidth) << number;
    if (!suffix.empty())
        oss << " " << suffix;
    return oss.str();
}

static std::string FormatFixed(double v, int precision)
{
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(precision) << v;
    return oss.str();
}

std::string abbreviateContent(const std::string& input, std::size_t maxLen) {
    std::string s = input;
    for (char& c : s) {
        if (c == '\n' || c == '\r') c = ' ';
    }
    if (s.size() <= maxLen) return s;
    if (maxLen <= 3) return std::string(maxLen, '.');
    return s.substr(0, maxLen - 3) + "...";
}

std::string toLower(const std::string& input) {
    std::string result = input;
    for (char &ch : result)
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return result;
}

void replaceAll(std::string& s, const std::string& from, const std::string& to) {
    if (from.empty()) return;
    std::size_t startPos = 0;
    while ((startPos = s.find(from, startPos)) != std::string::npos) {
        s.replace(startPos, from.length(), to);
        startPos += to.length();
    }
}

std::string normalizeContractions(const std::string& input) {
    std::string s = toLower(input);
    // normalize apostrophes
    replaceAll(s, "’", "'");
    replaceAll(s, "‘", "'");
    replaceAll(s, "",  "'");

    // negatives
    replaceAll(s, "don't",   "do not");
    replaceAll(s, "doesn't", "does not");
    replaceAll(s, "didn't",  "did not");
    replaceAll(s, "can't",   "can not");
    replaceAll(s, "cannot",  "can not");
    replaceAll(s, "won't",   "will not");
    replaceAll(s, "wouldn't","would not");
    replaceAll(s, "shouldn't","should not");
    replaceAll(s, "couldn't","could not");
    replaceAll(s, "isn't",   "is not");
    replaceAll(s, "aren't",  "are not");
    replaceAll(s, "wasn't",  "was not");
    replaceAll(s, "weren't", "were not");
    replaceAll(s, "ain't",   "is not");

    replaceAll(s, "i'm",   "i am");
    replaceAll(s, "im ",  "i am ");
    replaceAll(s, "you're","you are");
    replaceAll(s, "youre","you are");
    replaceAll(s, "we're","we are");
    replaceAll(s, "they're","they are");
    replaceAll(s, "it's","it is");
    replaceAll(s, "thats","that is");
    replaceAll(s, "that's","that is");
    replaceAll(s, "there's","there is");
    replaceAll(s, "what's","what is");
    replaceAll(s, "who's","who is");
    replaceAll(s, "let's","let us");

    replaceAll(s, "i've","i have");
    replaceAll(s, "you've","you have");
    replaceAll(s, "we've","we have");
    replaceAll(s, "they've","they have");

    replaceAll(s, "i'd","i would");
    replaceAll(s, "you'd","you would");
    replaceAll(s, "he'd","he would");
    replaceAll(s, "she'd","she would");
    replaceAll(s, "they'd","they would");
    replaceAll(s, "we'd","we would");

    replaceAll(s, "i'll","i will");
    replaceAll(s, "you'll","you will");
    replaceAll(s, "he'll","he will");
    replaceAll(s, "she'll","she will");
    replaceAll(s, "they'll","they will");
    replaceAll(s, "we'll","we will");

    replaceAll(s, "would've","would have");
    replaceAll(s, "could've","could have");
    replaceAll(s, "should've","should have");

    return s;
}

std::vector<std::string> extractWordsLower(const std::string& text) {
    std::vector<std::string> words;
    std::string current;
    for (char ch : text) {
        unsigned char uch = static_cast<unsigned char>(ch);
        if (std::isalnum(uch))
            current.push_back(static_cast<char>(std::tolower(uch)));
        else {
            if (!current.empty()) {
                words.push_back(current);
                current.clear();
            }
        }
    }
    if (!current.empty())
        words.push_back(current);

    std::vector<std::string> filtered;
    filtered.reserve(words.size());
    for (const std::string& w : words) {
        if (w == "i") {
            filtered.push_back(w);
            continue;
        }
        if (w.size() == 1) continue;
        if (w == "m" || w == "s" || w == "t" || w == "d" || w == "ll" || w == "re" || w == "ve")
            continue;
        if (JUNK_TOKENS.find(w) != JUNK_TOKENS.end())
            continue;
        filtered.push_back(w);
    }
    return filtered;
}

std::string readFileToString(const std::string& filename) {
    std::ifstream in(filename);
    if (!in)
        throw std::runtime_error("Could not open file: " + filename);
    return std::string(
        (std::istreambuf_iterator<char>(in)),
        std::istreambuf_iterator<char>()
    );
}

// -------------------------------------------------------------
// Process a single chat JSON file
// -------------------------------------------------------------
void processJsonFile(
    const std::string& filename,
    std::unordered_map<std::string, UserStats>& userStats,
    std::vector<Message>& allMessages,
    const std::vector<std::string>& romanticPhrasesLower,
    std::unordered_set<std::string>& nameWordsStop,
    const VaderSentiment& analyzer,
    const NrcEmotionLexicon& nrcLexicon
) {
    json j = json::parse(readFileToString(filename));

    // Collect participant name tokens (so we can drop them from "top words")
    if (j.contains("participants") && j["participants"].is_array()) {
        for (const auto& p : j["participants"]) {
            if (p.contains("name") && p["name"].is_string()) {
                std::string name    = p["name"];
                std::string lowered = toLower(name);
                std::vector<std::string> nameTokens = extractWordsLower(lowered);
                for (const std::string& w : nameTokens)
                    nameWordsStop.insert(w);
            }
        }
    }

    if (!j.contains("messages") || !j["messages"].is_array()) {
        std::cerr << "Warning: '" << fs::path(filename).filename().string()
                  << "' does not contain a valid 'messages' array.\n";
        return;
    }

    for (const auto& msg : j["messages"]) {
        if (!msg.contains("sender_name") || !msg["sender_name"].is_string())
            continue;

        std::string sender = msg["sender_name"];

        // Skip Meta AI entirely
        if (sender == "Meta AI")
            continue;

        long long timestampMs = 0;
        if (msg.contains("timestamp_ms") && msg["timestamp_ms"].is_number_integer())
            timestampMs = msg["timestamp_ms"].get<long long>();

        std::string content;
        if (msg.contains("content") && msg["content"].is_string())
            content = msg["content"];

        if (!content.empty()) {
            std::string lowerContent = toLower(content);
            if (IsSystemPlaceholderMessage(content)) continue;
        }

        // --- Time breakdown for heatmap / monthly sentiment / monthly volume ----
        std::tm localTm{};
        bool haveLocalTm = false;
        if (timestampMs > 0) {
            std::time_t t = static_cast<std::time_t>(timestampMs / 1000);
#ifdef _WIN32
            if (localtime_s(&localTm, &t) == 0)
                haveLocalTm = true;
#else
            std::tm* ptm = std::localtime(&t);
            if (ptm) {
                localTm = *ptm;
                haveLocalTm = true;
            }
#endif
            if (haveLocalTm) {
                int hour = localTm.tm_hour; // 0..23
                int wday = localTm.tm_wday; // 0=Sunday
                int row  = (wday + 6) % 7;  // 0=Mon ... 6=Sun
                if (row >= 0 && row < 7 && hour >= 0 && hour < 24) {
                    g_heatmapCounts[row][hour]++;
                    g_heatmapReady = true;
                }

                // Monthly total messages (count every message w/ timestamp)
                int year  = localTm.tm_year + 1900;
                int month = localTm.tm_mon + 1;
                auto key  = std::make_pair(year, month);
                g_monthlyMessageCounts[key]++;
                g_perUserMonthlyMessageCounts[sender][key]++;
            }
        }

        // Add to global timeline
        Message m;
        m.sender      = sender;
        m.timestampMs = timestampMs;
        m.content     = content;
        allMessages.push_back(m);

        UserStats& stats = userStats[sender];
        stats.totalMessages++;

        // Reactions
        if (msg.contains("reactions") && msg["reactions"].is_array()) {
            for (const auto& r : msg["reactions"]) {
                if (r.contains("actor") && r["actor"].is_string()) {
                    std::string actor = r["actor"];
                    if (actor == "Meta AI") continue;
                    UserStats& aStats = userStats[actor];
                    aStats.reactionsSent++;
                }
            }
        }

        if (!content.empty()) {
            std::string normalized = normalizeContractions(content);
            std::vector<std::string> words = extractWordsLower(normalized);
            long long wordCount = static_cast<long long>(words.size());

            if (wordCount > 0) {
                stats.totalWords += wordCount;
                stats.wordMessages += 1;
                stats.messageWordLengths.push_back(wordCount);

                // Monthly average length aggregates (global + per user)
                if (haveLocalTm) {
                    int year  = localTm.tm_year + 1900;
                    int month = localTm.tm_mon + 1;
                    auto key  = std::make_pair(year, month);

                    MonthlyLengthAgg& agg = g_monthlyLengthAgg[key];
                    agg.sumWords += wordCount;
                    agg.msgCount += 1;

                    MonthlyLengthAgg& uAgg = g_perUserMonthlyLengthAgg[sender][key];
                    uAgg.sumWords += wordCount;
                    uAgg.msgCount += 1;
                }

                for (const std::string& w : words)
                    stats.wordFrequency[w]++;

                if (wordCount > stats.longestMessageWords) {
                    stats.longestMessageWords   = wordCount;
                    stats.longestMessageContent = content;
                }

                // NRC
                NrcEmotionLexicon::Scores nrcScores;
                nrcLexicon.scoreWords(words, nrcScores);
                double tokenTaggedHere = 0.0;
                for (int i = 0; i < NRC_DIM; ++i) {
                    stats.nrcEmotionSums[i] += nrcScores.values[i];
                    tokenTaggedHere += nrcScores.values[i];
                }
                stats.nrcTaggedTokens += static_cast<long long>(tokenTaggedHere);
            }

            // Romantic phrases
            if (!romanticPhrasesLower.empty()) {
                std::string contentLower = toLower(content);
                bool foundRomantic = false;
                for (const std::string& phraseLower : romanticPhrasesLower) {
                    if (!phraseLower.empty() &&
                        contentLower.find(phraseLower) != std::string::npos) {
                        foundRomantic = true;
                        break;
                    }
                }
                if (foundRomantic) {
                    stats.romanticMessages++;
                    if (haveLocalTm) {
                        int year  = localTm.tm_year + 1900;
                        int month = localTm.tm_mon + 1;
                        auto key  = std::make_pair(year, month);
                        g_monthlyRomanticCounts[key]++;
                        g_perUserMonthlyRomanticCounts[sender][key]++;
                    }
                }
            }

            // VADER
            double neg, neu, pos, compound;
            analyzer.polarityScores(content, neg, neu, pos, compound);
            stats.vaderPosSum      += pos;
            stats.vaderNegSum      += neg;
            stats.vaderNeuSum      += neu;
            stats.vaderCompoundSum += compound;
            stats.vaderSamples++;

            // Monthly sentiment aggregate (only if given time)
            if (haveLocalTm) {
                int year  = localTm.tm_year + 1900;
                int month = localTm.tm_mon + 1;
                auto key  = std::make_pair(year, month);

                // Global aggregate
                MonthlyAggregate& agg = g_monthlyAggregates[key];
                agg.sumCompound += compound;
                agg.count       += 1;

                // Per-user aggregate
                MonthlyAggregate& userAgg = g_perUserMonthlyEmotion[sender][key];
                userAgg.sumCompound += compound;
                userAgg.count       += 1;
            }
        }
    }

    std::cout << "Processed: " << fs::path(filename).filename().string() << "\n";
}

// -------------------------------------------------------------
// Timeline analysis (conversation gaps, response times, runs)
// -------------------------------------------------------------
void analyzeTimeline(
    const std::vector<Message>& allMessagesIn,
    std::unordered_map<std::string, UserStats>& userStats
) {
    if (allMessagesIn.size() < 2)
        return;

    std::vector<Message> messages = allMessagesIn;
    std::sort(messages.begin(), messages.end(),
        [](const Message& a, const Message& b) {
            return a.timestampMs < b.timestampMs;
        });

    const long long CONVERSATION_GAP_MS = 6LL * 60LL * 60LL * 1000LL;

    std::string prevSender = messages[0].sender;
    long long   prevTime   = messages[0].timestampMs;
    long long   currentRunLen = 1;

    for (std::size_t i = 1; i < messages.size(); ++i) {
        const Message& msg = messages[i];
        long long gap = msg.timestampMs - prevTime;

        if (gap >= CONVERSATION_GAP_MS)
            userStats[msg.sender].conversationsStarted++;

        if (msg.sender != prevSender) {
            // response time per user (overall)
            UserStats& replyStats = userStats[msg.sender];
            replyStats.totalResponseTimeMs += gap;
            replyStats.responseCount += 1;

            // monthly response time aggregates (global + per user)
            if (msg.timestampMs > 0) {
                std::time_t t = static_cast<std::time_t>(msg.timestampMs / 1000);
                std::tm localTm{};
#ifdef _WIN32
                if (localtime_s(&localTm, &t) == 0) {
#else
                std::tm* ptm = std::localtime(&t);
                if (ptm) {
                    localTm = *ptm;
#endif
                    int year  = localTm.tm_year + 1900;
                    int month = localTm.tm_mon + 1;
                    auto key  = std::make_pair(year, month);

                    MonthlyResponseAgg& gAgg = g_monthlyResponseAgg[key];
                    gAgg.sumMs += gap;
                    gAgg.count += 1;

                    MonthlyResponseAgg& uAgg =
                        g_perUserMonthlyResponseAgg[msg.sender][key];
                    uAgg.sumMs += gap;
                    uAgg.count += 1;
#ifdef _WIN32
                }
#else
                }
#endif
            }
        }

        if (msg.sender == prevSender) {
            currentRunLen++;
        } else {
            if (currentRunLen == 2)
                userStats[prevSender].doubleTextRuns++;
            else if (currentRunLen == 3)
                userStats[prevSender].tripleTextRuns++;
            else if (currentRunLen >= 4)
                userStats[prevSender].yappingRuns++;
            currentRunLen = 1;
        }

        prevSender = msg.sender;
        prevTime   = msg.timestampMs;
    }

    // last run code
    if (currentRunLen == 2)
        userStats[prevSender].doubleTextRuns++;
    else if (currentRunLen == 3)
        userStats[prevSender].tripleTextRuns++;
    else if (currentRunLen >= 4)
        userStats[prevSender].yappingRuns++;
}

// -------------------------------------------------------------
// Core analysis function used by GUI & console
// -------------------------------------------------------------
std::string runAnalysisToString(const std::string& inputPathStr) {
    // Reset global analytics every run
    std::fill(&g_heatmapCounts[0][0], &g_heatmapCounts[0][0] + 7 * 24, 0);
    g_heatmapReady = false;

    g_monthlyEmotionPoints.clear();
    g_monthlyAggregates.clear();

    g_monthlyMessageCounts.clear();
    g_monthlyCountPoints.clear();

    g_monthlyResponseAgg.clear();
    g_monthlyResponsePoints.clear();

    g_monthlyRomanticCounts.clear();
    g_monthlyRomanticPoints.clear();

    g_monthlyLengthAgg.clear();
    g_monthlyAvgLengthPoints.clear();

    g_chartUserNames.clear();

    g_userMonthlyCountSeries.clear();
    g_userMonthlyEmotionSeries.clear();
    g_userMonthlyResponseSeries.clear();
    g_userMonthlyRomanticSeries.clear();
    g_userMonthlyAvgLengthSeries.clear();

    g_perUserMonthlyMessageCounts.clear();
    g_perUserMonthlyEmotion.clear();
    g_perUserMonthlyResponseAgg.clear();
    g_perUserMonthlyRomanticCounts.clear();
    g_perUserMonthlyLengthAgg.clear();

    // Load sentiment resources
    fs::path exeDir = GetExecutableDir();

    VaderSentiment analyzer;
    {
        fs::path vaderPath = exeDir / "vader_lexicon.txt";
        if (!analyzer.loadLexicon(vaderPath.string()))
        {
            // dev fallback (if running from a different working dir)
            if (!analyzer.loadLexicon("vader_lexicon.txt"))
                throw std::runtime_error("Failed to load vader_lexicon.txt");
        }
    }

    NrcEmotionLexicon nrc;
    {
        fs::path nrcPath = exeDir / "nrc_emotion_lexicon.txt";
        if (!nrc.loadFromFile(nrcPath.string()))
        {
            if (!nrc.loadFromFile("nrc_emotion_lexicon.txt"))
                throw std::runtime_error("Failed to load nrc_emotion_lexicon.txt");
        }
    }


    fs::path inputPath = inputPathStr;

    std::unordered_map<std::string, UserStats> userStats;
    std::vector<Message> allMessages;
    std::unordered_set<std::string> nameWordsStop;

    std::vector<std::string> romanticPhrasesLower = {
    // affection / love
    "love you",
    "love u",
    "miss you",
    "miss u",
    // too many false positives "want you",
    // too many false positives "need you",
    "crave you",
    "adore you",
    "care about you",
    "thinking of you",
    "thinking about you",

    // attraction / desire
    "want you so bad",
    "want you bad",
    "need you bad",
    "craving you",
    "dying for you",
    "hungry for you",
    "thirsty for you",
    "obsessed with you",
    "crazy about you",
    "mad about you",
    "hooked on you",

    // touching / closeness
    "miss your touch",
    "love your touch",
    "love your body",
    "miss your body",
    "want your body",
    "need your body",
    "love your kisses",
    "love your kiss",
    "miss your kiss",
    "love your hugs",
    "miss your hugs",

    // emotional intimacy
    "you mean so much",
    "you mean everything",
    "my love",
    "my heart",
    "my world",
    "my everything",
    "my person",
    "my soulmate",
    "my favorite person",
    "my whole world",

    // closeness / belonging
    "feel safe with you",
    "safe with you",
    "home with you",
    "you feel like home",
    "belong with you",
    "belong to you",
    "meant for you",
    "meant for us",
    "meant to be",

    // affectionate nicknames / pet names
    "babe",
    "baby",
    "sweetheart",
    "sweetie",
    "cutie",
    "beautiful",
    "gorgeous",
    "handsome",
    "sexy",
    "darling",
    "honey",
    // longing / distance
    "wish you were here",
    "wish u were here",
    "want you here",
    "need you here",
    "miss being with you",
    "miss time with you",
    "miss your voice",
    "miss your smile",
    "miss your face",

    // passion / sexual energy
    "turn me on",
    "hot for you",
    "want you naked",
    "want your lips",
    "love your lips",
    "love your neck",
    "love your skin",
    "need your touch",

    // flirting / teasing
    "cant resist you",
    "you tempt me",
    "you tease me",
    "you make me weak",
    "you make me melt",
    "you melt me",
    "you ruin me",
    "you own me",

    // commitment / emotional weight
    "you complete me",
    "we belong together",
    };

    if (fs::is_regular_file(inputPath)) {
        processJsonFile(
            inputPath.string(),
            userStats,
            allMessages,
            romanticPhrasesLower,
            nameWordsStop,
            analyzer,
            nrc
        );
    } else if (fs::is_directory(inputPath)) {
        for (const auto& entry : fs::directory_iterator(inputPath)) {
            if (entry.path().extension() == ".json") {
                processJsonFile(
                    entry.path().string(),
                    userStats,
                    allMessages,
                    romanticPhrasesLower,
                    nameWordsStop,
                    analyzer,
                    nrc
                );
            }
        }
    } else {
        throw std::runtime_error("Invalid path: " + inputPathStr);
    }

    analyzeTimeline(allMessages, userStats);

    // Finalize monthly emotion points from aggregates
    g_monthlyEmotionPoints.clear();
    for (const auto& kv : g_monthlyAggregates) {
        const auto& key = kv.first;
        const auto& agg = kv.second;
        if (agg.count <= 0) continue;
        MonthlyEmotionPoint p;
        p.year        = key.first;
        p.month       = key.second;
        p.avgCompound = agg.sumCompound / static_cast<double>(agg.count);
        g_monthlyEmotionPoints.push_back(p);
    }
    std::sort(
        g_monthlyEmotionPoints.begin(),
        g_monthlyEmotionPoints.end(),
        [](const MonthlyEmotionPoint& a, const MonthlyEmotionPoint& b) {
            if (a.year != b.year) return a.year < b.year;
            return a.month < b.month;
        }
    );

    // Finalize monthly total message counts
    g_monthlyCountPoints.clear();
    for (const auto& kv : g_monthlyMessageCounts) {
        MonthlyCountPoint p;
        p.year          = kv.first.first;
        p.month         = kv.first.second;
        p.totalMessages = kv.second;
        g_monthlyCountPoints.push_back(p);
    }
    std::sort(
        g_monthlyCountPoints.begin(),
        g_monthlyCountPoints.end(),
        [](const MonthlyCountPoint& a, const MonthlyCountPoint& b) {
            if (a.year != b.year) return a.year < b.year;
            return a.month < b.month;
        }
    );

    // Finalize monthly average response time (global)
    g_monthlyResponsePoints.clear();
    for (const auto& kv : g_monthlyResponseAgg) {
        const auto& key = kv.first;
        const auto& agg = kv.second;
        if (agg.count <= 0) continue;
        MonthlyResponsePoint p;
        p.year       = key.first;
        p.month      = key.second;
        double avgMs = static_cast<double>(agg.sumMs) / static_cast<double>(agg.count);
        p.avgMinutes = (avgMs / 1000.0) / 60.0;
        g_monthlyResponsePoints.push_back(p);
    }
    std::sort(
        g_monthlyResponsePoints.begin(),
        g_monthlyResponsePoints.end(),
        [](const MonthlyResponsePoint& a, const MonthlyResponsePoint& b) {
            if (a.year != b.year) return a.year < b.year;
            return a.month < b.month;
        }
    );

    // Finalize monthly romantic counts (global)
    g_monthlyRomanticPoints.clear();
    for (const auto& kv : g_monthlyRomanticCounts) {
        MonthlyRomanticPoint p;
        p.year             = kv.first.first;
        p.month            = kv.first.second;
        p.romanticMessages = kv.second;
        g_monthlyRomanticPoints.push_back(p);
    }
    std::sort(
        g_monthlyRomanticPoints.begin(),
        g_monthlyRomanticPoints.end(),
        [](const MonthlyRomanticPoint& a, const MonthlyRomanticPoint& b) {
            if (a.year != b.year) return a.year < b.year;
            return a.month < b.month;
        }
    );

    // Finalize monthly average message length (global)
    g_monthlyAvgLengthPoints.clear();
    for (const auto& kv : g_monthlyLengthAgg) {
        const auto& key = kv.first;
        const auto& agg = kv.second;
        if (agg.msgCount <= 0) continue;
        MonthlyAvgLengthPoint p;
        p.year     = key.first;
        p.month    = key.second;
        p.avgWords = static_cast<double>(agg.sumWords) /
                     static_cast<double>(agg.msgCount);
        g_monthlyAvgLengthPoints.push_back(p);
    }
    std::sort(
        g_monthlyAvgLengthPoints.begin(),
        g_monthlyAvgLengthPoints.end(),
        [](const MonthlyAvgLengthPoint& a, const MonthlyAvgLengthPoint& b) {
            if (a.year != b.year) return a.year < b.year;
            return a.month < b.month;
        }
    );

    // ---------------- Build report string ----------------
    std::ostringstream out;

    const int LABEL_WIDTH = 40;
    const int COL_WIDTH   = 26;
    const int NUM_WIDTH = 12;

    // Helper: comparative row
    auto printRow = [&](const std::string& label,
                    const std::vector<std::string>& values) {
    out << " " << std::left << std::setw(LABEL_WIDTH) << (label + ":");
    for (const auto& v : values)
        out << " " << std::right << std::setw(COL_WIDTH) << v;
    out << "\n";
    };


    // Helper: single-user stat
    auto printSingle = [&](const std::string& label,
                           const std::string& value) {
        out << " " << std::left << std::setw(LABEL_WIDTH)
            << (label + ":") << " " << value << "\n";
    };

    // Sorted list of user names
    std::vector<std::string> userNames;
    userNames.reserve(userStats.size());
    for (const auto& p : userStats)
        userNames.push_back(p.first);
    std::sort(userNames.begin(), userNames.end());

    // Build per-user chart series (counts, emotion, response, romantic, avg length)
    g_chartUserNames.clear();
    g_userMonthlyCountSeries.clear();
    g_userMonthlyEmotionSeries.clear();
    g_userMonthlyResponseSeries.clear();
    g_userMonthlyRomanticSeries.clear();
    g_userMonthlyAvgLengthSeries.clear();

    for (const auto& name : userNames) {
        // --- never treat any synthetic "Total" user as a chart series --- (Got Bugs - Might be able to delete now) 
        if (name == "Total (all users)" || name == "Total" || name == "All users")
            continue;

        g_chartUserNames.push_back(name);

        std::vector<UserMonthlyCountPoint>     countSeries;
        std::vector<UserMonthlyEmotionPoint>   emoSeries;
        std::vector<UserMonthlyResponsePoint>  respSeries;
        std::vector<UserMonthlyRomanticPoint>  romanticSeries;
        std::vector<UserMonthlyAvgLengthPoint> lenSeries;

        // Per-user counts
        auto itCounts = g_perUserMonthlyMessageCounts.find(name);
        if (itCounts != g_perUserMonthlyMessageCounts.end()) {
            for (const auto& kv : itCounts->second) {
                UserMonthlyCountPoint p;
                p.year          = kv.first.first;
                p.month         = kv.first.second;
                p.totalMessages = kv.second;
                countSeries.push_back(p);
            }
            std::sort(
                countSeries.begin(), countSeries.end(),
                [](const UserMonthlyCountPoint& a, const UserMonthlyCountPoint& b) {
                    if (a.year != b.year) return a.year < b.year;
                    return a.month < b.month;
                }
            );
        }

        // Per-user emotion
        auto itEmo = g_perUserMonthlyEmotion.find(name);
        if (itEmo != g_perUserMonthlyEmotion.end()) {
            for (const auto& kv : itEmo->second) {
                const MonthlyAggregate& agg = kv.second;
                if (agg.count <= 0) continue;
                UserMonthlyEmotionPoint p;
                p.year        = kv.first.first;
                p.month       = kv.first.second;
                p.avgCompound = agg.sumCompound /
                                static_cast<double>(agg.count);
                emoSeries.push_back(p);
            }
            std::sort(
                emoSeries.begin(), emoSeries.end(),
                [](const UserMonthlyEmotionPoint& a, const UserMonthlyEmotionPoint& b) {
                    if (a.year != b.year) return a.year < b.year;
                    return a.month < b.month;
                }
            );
        }

        // Per-user response time
        auto itResp = g_perUserMonthlyResponseAgg.find(name);
        if (itResp != g_perUserMonthlyResponseAgg.end()) {
            for (const auto& kv : itResp->second) {
                const MonthlyResponseAgg& agg = kv.second;
                if (agg.count <= 0) continue;
                UserMonthlyResponsePoint p;
                p.year       = kv.first.first;
                p.month      = kv.first.second;
                double avgMs = static_cast<double>(agg.sumMs) /
                               static_cast<double>(agg.count);
                p.avgMinutes = (avgMs / 1000.0) / 60.0;
                respSeries.push_back(p);
            }
            std::sort(
                respSeries.begin(), respSeries.end(),
                [](const UserMonthlyResponsePoint& a, const UserMonthlyResponsePoint& b) {
                    if (a.year != b.year) return a.year < b.year;
                    return a.month < b.month;
                }
            );
        }

        // Per-user romantic counts
        auto itRom = g_perUserMonthlyRomanticCounts.find(name);
        if (itRom != g_perUserMonthlyRomanticCounts.end()) {
            for (const auto& kv : itRom->second) {
                UserMonthlyRomanticPoint p;
                p.year             = kv.first.first;
                p.month            = kv.first.second;
                p.romanticMessages = kv.second;
                romanticSeries.push_back(p);
            }
            std::sort(
                romanticSeries.begin(), romanticSeries.end(),
                [](const UserMonthlyRomanticPoint& a, const UserMonthlyRomanticPoint& b) {
                    if (a.year != b.year) return a.year < b.year;
                    return a.month < b.month;
                }
            );
        }

        // Per-user average message length
        auto itLen = g_perUserMonthlyLengthAgg.find(name);
        if (itLen != g_perUserMonthlyLengthAgg.end()) {
            for (const auto& kv : itLen->second) {
                const MonthlyLengthAgg& agg = kv.second;
                if (agg.msgCount <= 0) continue;
                UserMonthlyAvgLengthPoint p;
                p.year     = kv.first.first;
                p.month    = kv.first.second;
                p.avgWords = static_cast<double>(agg.sumWords) /
                             static_cast<double>(agg.msgCount);
                lenSeries.push_back(p);
            }
            std::sort(
                lenSeries.begin(), lenSeries.end(),
                [](const UserMonthlyAvgLengthPoint& a, const UserMonthlyAvgLengthPoint& b) {
                    if (a.year != b.year) return a.year < b.year;
                    return a.month < b.month;
                }
            );
        }

        g_userMonthlyCountSeries.push_back(std::move(countSeries));
        g_userMonthlyEmotionSeries.push_back(std::move(emoSeries));
        g_userMonthlyResponseSeries.push_back(std::move(respSeries));
        g_userMonthlyRomanticSeries.push_back(std::move(romanticSeries));
        g_userMonthlyAvgLengthSeries.push_back(std::move(lenSeries));
    }
    out << "=== Message Stats ===\n\n";
    if (userNames.empty()) {
        out << "No messages found.\n";
        return out.str();
    }

    // -----------------------------------------------------
    // General comparative section
    // -----------------------------------------------------
    out << "[General Message Data & Conversation Dynamics]\n";

    // Header row with user names
    out << " " << std::left << std::setw(LABEL_WIDTH) << "";
    for (const auto& name : userNames)
        out << " " << std::left << std::setw(COL_WIDTH) << name;
    out << "\n";

    // Total messages
    {
        std::vector<std::string> vals;
        for (const auto& name : userNames) {
            const UserStats& s = userStats[name];
            vals.push_back(PadNumberSuffix(formatWithCommas(s.totalMessages), "messages", NUM_WIDTH));
        }
        printRow("Total messages", vals);
    }

    // Average message length
    {
        std::vector<std::string> vals;
        for (const auto& name : userNames) {
            const UserStats& s = userStats[name];
            double avgWords = 0.0;
            if (s.wordMessages > 0) {
                avgWords = static_cast<double>(s.totalWords) /
                           static_cast<double>(s.wordMessages);
            }
            std::string num = FormatFixed(avgWords, 2);
            vals.push_back(PadNumberSuffix(num, "words", NUM_WIDTH));
        }
        printRow("Average message length", vals);
    }

    // Romantic messages
    {
        std::vector<std::string> vals;
        for (const auto& name : userNames) {
            const UserStats& s = userStats[name];
            vals.push_back(PadNumberSuffix(formatWithCommas(s.romanticMessages), "messages", NUM_WIDTH));
        }
        printRow("Romantic messages", vals);
    }

    // Conversations started
    {
        std::vector<std::string> vals;
        for (const auto& name : userNames) {
            const UserStats& s = userStats[name];
            vals.push_back(PadNumberSuffix(formatWithCommas(s.conversationsStarted), "conversations", NUM_WIDTH));
        }
        printRow("Conversations started (>= 6h)", vals);
    }

    // Average response time
    {
        std::vector<std::string> vals;
        for (const auto& name : userNames) {
            const UserStats& s = userStats[name];
            if (s.responseCount > 0) {
                double avgMs      = static_cast<double>(s.totalResponseTimeMs) /
                                    static_cast<double>(s.responseCount);
                double avgSeconds = avgMs / 1000.0;
                double avgMinutes = avgSeconds / 60.0;
                std::ostringstream tmp;
                tmp << std::fixed << std::setprecision(2)
                    << avgMinutes << " min ("
                    << std::setprecision(2) << avgSeconds << " s)";
                vals.push_back(tmp.str());
            } else {
                vals.push_back("N/A");
            }
        }
        printRow("Average response time", vals);
    }

    // Double / triple / yapping runs
    {
        std::vector<std::string> vals;
        for (const auto& name : userNames) {
            const UserStats& s = userStats[name];
            vals.push_back(PadNumberSuffix(formatWithCommas(s.doubleTextRuns), "occurrences", NUM_WIDTH));
        }
        printRow("Double-text runs (==2 in a row)", vals);
    }
    {
        std::vector<std::string> vals;
        for (const auto& name : userNames) {
            const UserStats& s = userStats[name];
            vals.push_back(PadNumberSuffix(formatWithCommas(s.tripleTextRuns), "occurrences", NUM_WIDTH));
        }
        printRow("Triple-text runs (==3 in a row)", vals);
    }
    {
        std::vector<std::string> vals;
        for (const auto& name : userNames) {
            const UserStats& s = userStats[name];
            vals.push_back(PadNumberSuffix(formatWithCommas(s.yappingRuns), "occurrences", NUM_WIDTH));
        }
        printRow("Yapping runs (>=4 in a row)", vals);
    }

    out << "\n";

    // -----------------------------------------------------
    // Reactions comparative section
    // -----------------------------------------------------
    out << "[Reactions]\n";
    out << " " << std::left << std::setw(LABEL_WIDTH) << "";
    for (const auto& name : userNames)
        out << " " << std::left << std::setw(COL_WIDTH) << name;
    out << "\n";

    {
        std::vector<std::string> vals;
        for (const auto& name : userNames) {
            const UserStats& s = userStats[name];
            vals.push_back(PadNumberSuffix(formatWithCommas(s.reactionsSent), "reactions", NUM_WIDTH));
        }
        printRow("Reactions sent", vals);
    }
    out << "\n";

    // -----------------------------------------------------
    // VADER comparative section
    // -----------------------------------------------------
    out << "[VADER Sentiment Analysis]\n";
    out << " "
        << "VADER scores how positive, negative, or neutral each message feels "
        << "using a sentiment lexicon. These values are averages across all of "
        << "your messages.\n\n";

    out << " " << std::left << std::setw(LABEL_WIDTH) << "";
    for (const auto& name : userNames)
        out << " " << std::left << std::setw(COL_WIDTH) << name;
    out << "\n";

    // % positive
    {
        std::vector<std::string> vals;
        for (const auto& name : userNames) {
            const UserStats& s = userStats[name];
            if (s.vaderSamples > 0) {
                double v = static_cast<double>(s.vaderPosSum) / static_cast<double>(s.vaderSamples);
                std::ostringstream tmp;
                tmp << std::fixed << std::setprecision(3) << v;
                vals.push_back(tmp.str());
            } else {
                vals.push_back("N/A");
            }
        }
        printRow("% Positive messages", vals);
    }

    // % negative
    {
        std::vector<std::string> vals;
        for (const auto& name : userNames) {
            const UserStats& s = userStats[name];
            if (s.vaderSamples > 0) {
                double v = static_cast<double>(s.vaderNegSum) / static_cast<double>(s.vaderSamples);
                std::ostringstream tmp;
                tmp << std::fixed << std::setprecision(3) << v;
                vals.push_back(tmp.str());
            } else {
                vals.push_back("N/A");
            }
        }
        printRow("% Negative messages", vals);
    }

    // % neutral
    {
        std::vector<std::string> vals;
        for (const auto& name : userNames) {
            const UserStats& s = userStats[name];
            if (s.vaderSamples > 0) {
                double v = static_cast<double>(s.vaderNeuSum) / static_cast<double>(s.vaderSamples);
                std::ostringstream tmp;
                tmp << std::fixed << std::setprecision(3) << v;
                vals.push_back(tmp.str());
            } else {
                vals.push_back("N/A");
            }
        }
        printRow("% Neutral messages", vals);
    }

    // avg compound
    {
        std::vector<std::string> vals;
        for (const auto& name : userNames) {
            const UserStats& s = userStats[name];
            if (s.vaderSamples > 0) {
                double v = static_cast<double>(s.vaderCompoundSum) / static_cast<double>(s.vaderSamples);
                std::ostringstream tmp;
                tmp << std::fixed << std::setprecision(4) << v;
                vals.push_back(tmp.str());
            } else {
                vals.push_back("N/A");
            }
        }
        printRow("Average compound score", vals);
    }

    out << "\n";

    // -----------------------------------------------------
    // NRC comparative section
    // -----------------------------------------------------
    out << "[NRC Emotion Profile]\n";
    out << " "
        << "The NRC Emotion Lexicon measures how often your words align with "
        << "each emotion (0–1 scale). Higher values mean that emotion shows up "
        << "more in your language.\n\n";

    out << " " << std::left << std::setw(LABEL_WIDTH) << "";
    for (const auto& name : userNames)
        out << " " << std::left << std::setw(COL_WIDTH) << name;
    out << "\n";

    for (int dim = 0; dim < NRC_DIM; ++dim) {
        std::vector<std::string> vals;
        for (const auto& name : userNames) {
            const UserStats& s = userStats[name];
            if (s.nrcTaggedTokens > 0) {
                double denom = static_cast<double>(s.nrcTaggedTokens);
                double value = s.nrcEmotionSums[dim] / denom;
                std::ostringstream tmp;
                tmp << std::fixed << std::setprecision(3) << value;
                vals.push_back(tmp.str());
            } else {
                vals.push_back("N/A");
            }
        }
        printRow(NRC_CATEGORY_NAMES[dim], vals);
    }

    out << "\n";

    // -----------------------------------------------------
    // Word usage & longest messages (per user) – LAST section
    // -----------------------------------------------------
    out << "[Word Usage & Longest Messages]\n";
    for (const auto& name : userNames) {
        const UserStats& stats = userStats[name];

        out << "User: " << name << "\n";

        if (stats.longestMessageWords > 0) {
            std::ostringstream tmp;
            tmp << formatWithCommas(stats.longestMessageWords) << " words"
                << " | Preview: "
                << abbreviateContent(stats.longestMessageContent, 80);
            printSingle("Longest message", tmp.str());
        } else {
            printSingle("Longest message", "(no textual messages)");
        }

        // Top 10 words
        std::vector<std::pair<std::string,long long>> wordsVec;
        wordsVec.reserve(stats.wordFrequency.size());
        for (const auto& wc : stats.wordFrequency) {
            const std::string& word  = wc.first;
            long long          count = wc.second;

            if (STOP_WORDS.find(word) != STOP_WORDS.end()) continue;
            if (JUNK_TOKENS.find(word) != JUNK_TOKENS.end()) continue;
            if (word.size() < 3) continue;
            if (nameWordsStop.find(word) != nameWordsStop.end()) continue;

            wordsVec.emplace_back(word, count);
        }
        std::sort(
            wordsVec.begin(), wordsVec.end(),
            [](const auto& a, const auto& b) {
                return a.second > b.second;
            }
        );

        if (!wordsVec.empty()) {
            std::ostringstream tmp;
            int printed = 0;
            for (const auto& p2 : wordsVec) {
                if (printed > 0) tmp << ", ";
                tmp << p2.first << ": " << formatWithCommas(p2.second);
                printed++;
                if (printed >= 10 ||
                    printed >= static_cast<int>(wordsVec.size()))
                    break;
            }
            printSingle("Top 10 most used words", tmp.str());
        } else {
            printSingle("Top 10 most used words", "(no words recorded)");
        }

        out << "\n";
    }

    return out.str();
}

// -------------------------------------------------------------
// Console entry point (unchanged)
// -------------------------------------------------------------
int console_main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <file_or_directory>\n";
        return 1;
    }

    try {
        std::string report = runAnalysisToString(argv[1]);
        std::cout << report;
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << "\n";
        return 1;
    }
}

int main(int argc, char* argv[]) {
    return console_main(argc, argv);
}
