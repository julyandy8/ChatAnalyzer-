// whatsapp_convert.cpp
// Convert exported WhatsApp text chats into Instagram-style
// message_X.json files that the analyzer can consume.

#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <set>
#include <filesystem>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <algorithm>
#include <cctype>

#include "json.hpp"

namespace fs = std::filesystem;
using json = nlohmann::json;

// Simple representation of an Instagram-style message.
// (Identical layout to discord_convert.cpp)
struct InstaMessage
{
    std::string sender_name;
    long long   timestamp_ms = 0;
    std::string content;
};

// -------------------------------------------------------------
// Generic string helpers
// -------------------------------------------------------------

static inline void rtrimInPlace(std::string& s)
{
    while (!s.empty() && (s.back() == '\r' || s.back() == '\n'))
        s.pop_back();
}

static std::string trimCopy(const std::string& s)
{
    std::size_t start = 0;
    std::size_t end   = s.size();

    while (start < end &&
           std::isspace(static_cast<unsigned char>(s[start])))
        ++start;
    while (end > start &&
           std::isspace(static_cast<unsigned char>(s[end - 1])))
        --end;

    return s.substr(start, end - start);
}

static std::string toLowerCopy(const std::string& s)
{
    std::string out = s;
    for (char& ch : out)
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return out;
}

// Strip leading left-to-right mark (U+200E) if present.
static std::string stripBidiMarks(const std::string& s)
{
    std::string out;
    out.reserve(s.size());

    const unsigned char* p = reinterpret_cast<const unsigned char*>(s.data());
    std::size_t i = 0;
    bool skippingPrefixMarks = true;

    while (i < s.size())
    {
        unsigned char c = p[i];

        if (skippingPrefixMarks && c == 0xE2)
        {
            // U+200E encoded as E2 80 8E in UTF-8.
            if (i + 2 < s.size() && p[i+1] == 0x80 && p[i+2] == 0x8E)
            {
                i += 3;
                continue;
            }
        }

        skippingPrefixMarks = false;
        out.push_back(static_cast<char>(c));
        ++i;
    }

    return out;
}

// -------------------------------------------------------------
// Date helpers (same idea as in discord_convert.cpp)
// -------------------------------------------------------------

// Convert a UTC calendar date to days since Unix epoch (1970-01-01).
// Implementation based on Howard Hinnant's days_from_civil.
static long long daysFromCivil(int y, unsigned m, unsigned d)
{
    y -= m <= 2;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);          // [0, 399]
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1; // [0, 365]
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;         // [0, 146096]
    return static_cast<long long>(era * 146097 + static_cast<int>(doe) - 719468);
}

// Parse WhatsApp timestamp into milliseconds since epoch.
//
// Typical examples from your export (_chat.txt):
//   "11/7/17, 9:20:04 PM"
//   "11/8/17, 6:47:25 AM"
//
// We assume US-style month/day/year and 12-hour clock with AM/PM.
// If parsing fails, we return 0 and let the analyzer treat it as unknown.
static long long parseWhatsAppTimestampMs(const std::string& tsRaw)
{
    if (tsRaw.empty())
        return 0;

    // Replace any non-ASCII bytes with spaces so we don't have to
    // reason about narrow no-break spaces, bidi marks, etc.
    std::string s;
    s.reserve(tsRaw.size());
    for (unsigned char ch : tsRaw)
    {
        if (ch < 0x80)
            s.push_back(static_cast<char>(ch));
        else
            s.push_back(' ');
    }

    // Example now looks like: "11/7/17, 9:20:04  PM"
    std::size_t commaPos = s.find(',');
    if (commaPos == std::string::npos)
        return 0;

    std::string datePart = trimCopy(s.substr(0, commaPos));
    std::string timePart = trimCopy(s.substr(commaPos + 1));

    // Parse date: mm/dd/yy
    int month = 0, day = 0, year2 = 0;
    char sep1 = 0, sep2 = 0;
    {
        std::istringstream ds(datePart);
        ds >> month >> sep1 >> day >> sep2 >> year2;
        if (!ds || sep1 != '/' || sep2 != '/')
            return 0;
    }

    int year = (year2 >= 70 ? 1900 + year2 : 2000 + year2);

    // timePart: "9:20:04 PM" (maybe with double spaces)
    int hour = 0, minute = 0, second = 0;
    std::string ampm;

    {
        std::istringstream ts(timePart);
        std::string clockStr;
        ts >> clockStr >> ampm;

        if (clockStr.empty() || ampm.empty())
        {
            // Fallback: last two characters are AM/PM, the rest is time.
            if (timePart.size() >= 2)
            {
                ampm = timePart.substr(timePart.size() - 2);
                clockStr = trimCopy(timePart.substr(0, timePart.size() - 2));
            }
        }

        char c1 = 0, c2 = 0;
        std::istringstream cs(clockStr);
        cs >> hour >> c1 >> minute >> c2 >> second;
        if (!cs || c1 != ':' || c2 != ':')
            return 0;
    }

    std::string ampmLow = toLowerCopy(ampm);
    bool isPM = (ampmLow == "pm");
    bool isAM = (ampmLow == "am");
    if (!isPM && !isAM)
        return 0;

    // Convert to 24-hour clock
    if (isPM && hour < 12) hour += 12;
    if (isAM && hour == 12) hour = 0;

    long long days = daysFromCivil(year,
                                   static_cast<unsigned>(month),
                                   static_cast<unsigned>(day));
    long long seconds = days * 86400LL
                      + hour * 3600LL
                      + minute * 60LL
                      + second;
    return seconds * 1000LL;
}

// -------------------------------------------------------------
// Content filtering: skip WhatsApp system / media messages
// -------------------------------------------------------------
//
// This is where we implement your "DO NOT COUNT these" rule:
//   - "image omitted", "video omitted", "sticker omitted/ommitted", "<media omitted>"
//   - "Voice Call", "Missed Voice Call", "Missed Voicee call", "Video call"...
//   - "This message was deleted"
//   - The WhatsApp end-to-end-encryption banner

static bool isWhatsAppSystemOrOmitted(const std::string& rawContent)
{
    std::string s = stripBidiMarks(rawContent);
    s = trimCopy(s);
    std::string low = toLowerCopy(s);

    if (low.empty())
        return true;

    // Media / attachment placeholders
    static const char* MEDIA_PLACEHOLDERS[] = {
        "<media omitted>",
        "image omitted",
        "video omitted",
        "audio omitted",
        "document omitted",
        "sticker omitted",
        "sticker ommitted", // common typo
        "gif omitted"
    };

    for (const char* p : MEDIA_PLACEHOLDERS)
    {
        if (low == p)
            return true;
    }

    // Call events
    static const char* CALL_EVENTS[] = {
        "voice call",
        "missed voice call",
        "missed voicee call", // typo from your prompt
        "video call",
        "missed video call"
    };

    for (const char* p : CALL_EVENTS)
    {
        if (low == p)
            return true;
    }

    // Deleted messages
    if (low == "this message was deleted")
        return true;

    // End-to-end encryption notice.
    if (low.find("messages and calls are") == 0 &&
        low.find("end-to-end encrypted") != std::string::npos)
    {
        return true;
    }

    return false;
}

// -------------------------------------------------------------
// Line parsing
// -------------------------------------------------------------

// Try to parse a single WhatsApp line that starts a new message.
// Returns true on success and fills sender / timestamp / content.
// Lines that are continuations of previous messages return false.
static bool parseWhatsAppHeaderLine(const std::string& rawLine,
                                    long long&        outTimestampMs,
                                    std::string&      outSender,
                                    std::string&      outContent)
{
    std::string line = rawLine;
    rtrimInPlace(line);

    // Strip leading bidi marks before checking for '['
    line = stripBidiMarks(line);

    if (line.empty() || line[0] != '[')
        return false;

    std::size_t closeBracket = line.find(']');
    if (closeBracket == std::string::npos)
        return false;

    // Extract timestamp portion between '[' and ']'
    std::string ts = line.substr(1, closeBracket - 1);

    // After "] " we expect "Name: message..."
    std::size_t afterBracket = closeBracket + 1;
    if (afterBracket < line.size() && line[afterBracket] == ' ')
        ++afterBracket;

    if (afterBracket >= line.size())
        return false;

    std::string rest = line.substr(afterBracket);

    std::size_t colonPos = rest.find(':');
    if (colonPos == std::string::npos)
    {
        // No sender delimiter; treat as system message with unknown sender.
        outSender   = "Unknown";
        outContent  = trimCopy(rest);
    }
    else
    {
        outSender  = trimCopy(rest.substr(0, colonPos));
        std::size_t msgStart = colonPos + 1;
        if (msgStart < rest.size() && rest[msgStart] == ' ')
            ++msgStart;
        outContent = (msgStart < rest.size())
            ? rest.substr(msgStart)
            : std::string();
    }

    outTimestampMs = parseWhatsAppTimestampMs(ts);
    return true;
}

// -------------------------------------------------------------
// Core processing
// -------------------------------------------------------------

static void processWhatsAppChatFile(
    const std::string&   filename,
    std::vector<InstaMessage>& outMessages,
    std::set<std::string>&     participants)
{
    std::ifstream in(filename);
    if (!in)
    {
        throw std::runtime_error("Could not open WhatsApp chat file: " + filename);
    }

    std::string line;
    bool hasCurrent = false;
    InstaMessage current{};

    while (std::getline(in, line))
    {
        long long   ts      = 0;
        std::string sender;
        std::string content;

        if (parseWhatsAppHeaderLine(line, ts, sender, content))
        {
            // Flush previous message (if any)
            if (hasCurrent)
            {
                if (!isWhatsAppSystemOrOmitted(current.content))
                {
                    outMessages.push_back(current);
                }
            }

            current.sender_name = sender;
            current.timestamp_ms = ts;
            current.content      = content;
            hasCurrent = true;

            participants.insert(sender);
        }
        else
        {
            // Continuation of previous message (multi-line message).
            if (hasCurrent)
            {
                rtrimInPlace(line);
                if (!line.empty())
                {
                    if (!current.content.empty())
                        current.content.push_back('\n');
                    current.content += line;
                }
            }
        }
    }

    // Flush last message
    if (hasCurrent)
    {
        if (!isWhatsAppSystemOrOmitted(current.content))
        {
            outMessages.push_back(current);
        }
    }
}

// -------------------------------------------------------------
// Public conversion API (similar to ConvertDiscordToInstagramFolder)
// -------------------------------------------------------------

bool ConvertWhatsAppToInstagramFolder(
    const std::string& inputPathStr,
    const std::string& outputPathStr,
    const std::string& chatTitle,
    std::string&       errorOut
)
{
    try
    {
        fs::path inputPath = fs::u8path(inputPathStr);
        fs::path outputDir = fs::u8path(outputPathStr);

        if (inputPathStr.empty())
        {
            errorOut = "Input path is empty.";
            return false;
        }

        if (outputPathStr.empty())
        {
            errorOut = "Output path is empty.";
            return false;
        }

        if (!fs::exists(inputPath) || !fs::is_regular_file(inputPath))
        {
            errorOut = "Input path must be a WhatsApp text export (.txt file).";
            return false;
        }

        fs::create_directories(outputDir);

        std::vector<InstaMessage> allMessages;
        std::set<std::string>     participants;

        processWhatsAppChatFile(inputPath.string(), allMessages, participants);

        if (allMessages.empty())
        {
            errorOut = "No messages found in WhatsApp chat file (after filtering).";
            return false;
        }

        // Sort messages chronologically.
        std::sort(allMessages.begin(), allMessages.end(),
                  [](const InstaMessage& a, const InstaMessage& b)
                  {
                      return a.timestamp_ms < b.timestamp_ms;
                  });

        // Build participants array.
        json participantsJson = json::array();
        for (const auto& name : participants)
        {
            json p;
            p["name"] = name;
            participantsJson.push_back(p);
        }

        const std::size_t CHUNK_SIZE = 5000;
        std::size_t total  = allMessages.size();
        std::size_t chunks = (total + CHUNK_SIZE - 1) / CHUNK_SIZE;
        if (chunks == 0) chunks = 1;

        for (std::size_t c = 0; c < chunks; ++c)
        {
            std::size_t start = c * CHUNK_SIZE;
            std::size_t end   = std::min(start + CHUNK_SIZE, total);

            json out;
            out["participants"] = participantsJson;

            json msgs = json::array();
            for (std::size_t i = start; i < end; ++i)
            {
                const auto& im = allMessages[i];
                json m;
                m["sender_name"]  = im.sender_name;
                m["timestamp_ms"] = im.timestamp_ms;
                m["content"]      = im.content;
                m["is_geoblocked_for_viewer"]                = false;
                m["is_unsent_image_by_messenger_kid_parent"] = false;
                msgs.push_back(std::move(m));
            }

            out["messages"]             = std::move(msgs);
            out["title"]                = chatTitle;
            out["is_still_participant"] = true;
            out["thread_path"]          = "whatsapp/converted";
            out["magic_words"]          = json::array();

            std::size_t fileIndex = c + 1;
            std::ostringstream ossName;
            ossName << "message_" << fileIndex << ".json";
            std::string fileName = ossName.str();

            fs::path outFilePath = outputDir / fs::u8path(fileName);

            std::ofstream ofs(outFilePath.string(), std::ios::binary);
            if (!ofs)
            {
                errorOut = "Failed to open output file: " + outFilePath.string();
                return false;
            }
            ofs << out.dump(2);
        }

        errorOut.clear();
        return true;
    }
    catch (const std::exception& ex)
    {
        errorOut = ex.what();
        return false;
    }
}
