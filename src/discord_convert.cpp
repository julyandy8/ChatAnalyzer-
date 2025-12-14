// discord_convert.cpp
// Convert Discrub-style Discord JSON exports into Instagram-style
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

#include "json.hpp"

namespace fs = std::filesystem;
using json = nlohmann::json;

// Simple representation of an Instagram-style message.
struct InstaMessage
{
    std::string sender_name;
    long long   timestamp_ms = 0;
    std::string content;
};

// Read a whole file into a string.
static std::string readFileToString(const std::string& filename)
{
    std::ifstream in(filename);
    if (!in)
    {
        throw std::runtime_error("Could not open file: " + filename);
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

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

// Parse Discord / ISO8601-like timestamp into milliseconds since epoch.
// Example input: "2023-06-23T01:48:40.585000+00:00"
static long long parseDiscordTimestampMs(const std::string& ts)
{
    if (ts.empty())
        return 0;

    // Keep only "YYYY-MM-DDTHH:MM:SS" and ignore fractional seconds/timezone.
    std::string base = ts;
    std::size_t pos = base.find_first_of(".+Z");
    if (pos != std::string::npos)
    {
        base = base.substr(0, pos);
    }

    std::tm tm{};
    std::istringstream iss(base);
    iss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");
    if (iss.fail())
    {
        // If parsing fails, treat as missing timestamp.
        return 0;
    }

    int year      = tm.tm_year + 1900;
    unsigned mon  = static_cast<unsigned>(tm.tm_mon + 1);
    unsigned day  = static_cast<unsigned>(tm.tm_mday);
    int hour      = tm.tm_hour;
    int min       = tm.tm_min;
    int sec       = tm.tm_sec;

    long long days = daysFromCivil(year, mon, day);
    long long seconds = days * 86400LL
                      + hour * 3600LL
                      + min  * 60LL
                      + sec;
    return seconds * 1000LL;
}

// Process a single Discord JSON page (file) and append InstaMessage objects.
// Also collects the set of participant names.
static void processDiscordPage(
    const std::string& filename,
    std::vector<InstaMessage>& outMessages,
    std::set<std::string>&     participants)
{
    try
    {
        std::string contents = readFileToString(filename);
        if (contents.empty())
            return;

        json j = json::parse(contents);

        // Accept either a top-level array of messages or an object with "messages".
        json messagesJson;
        if (j.is_array())
        {
            messagesJson = j;
        }
        else if (j.is_object() && j.contains("messages") && j["messages"].is_array())
        {
            messagesJson = j["messages"];
        }
        else
        {
            std::cerr << "Warning: " << filename
                      << " is not a recognized Discord JSON structure (skipping).\n";
            return;
        }

        for (const auto& msg : messagesJson)
        {
            if (!msg.is_object())
                continue;

            // ---------------- Sender name ----------------
            std::string senderName;

            // Discrub often exposes "userName" on each message.
            if (msg.contains("userName") && msg["userName"].is_string())
            {
                senderName = msg["userName"].get<std::string>();
            }
            // Otherwise, fall back to "author" object, checking global_name then username.
            else if (msg.contains("author") && msg["author"].is_object())
            {
                const auto& a = msg["author"];
                if (a.contains("global_name") && a["global_name"].is_string() &&
                    !a["global_name"].get<std::string>().empty())
                {
                    senderName = a["global_name"].get<std::string>();
                }
                else if (a.contains("username") && a["username"].is_string())
                {
                    senderName = a["username"].get<std::string>();
                }
            }

            if (senderName.empty())
            {
                senderName = "Unknown";
            }

            participants.insert(senderName);

            // ---------------- Timestamp ----------------
            long long timestampMs = 0;
            if (msg.contains("timestamp") && msg["timestamp"].is_string())
            {
                timestampMs = parseDiscordTimestampMs(
                    msg["timestamp"].get<std::string>());
            }

            // ---------------- Content ----------------
            std::string content;
            if (msg.contains("content") && msg["content"].is_string())
            {
                content = msg["content"].get<std::string>();
            }

            bool hasAttachment = false;
            if (msg.contains("attachments") && msg["attachments"].is_array())
            {
                if (!msg["attachments"].empty())
                {
                    hasAttachment = true;
                }
            }
            if (content.empty() && hasAttachment)
            {
                content = "[Attachment]";
            }

            InstaMessage im;
            im.sender_name = senderName;
            im.timestamp_ms = timestampMs;
            im.content      = content;

            outMessages.push_back(std::move(im));
        }
    }
    catch (const std::exception& ex)
    {
        std::cerr << "Error processing Discord JSON page '" << filename
                  << "': " << ex.what() << "\n";
    }
}

// Public function used by the GUI.
bool ConvertDiscordToInstagramFolder(
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

        fs::create_directories(outputDir);

        std::vector<InstaMessage> allMessages;
        std::set<std::string>     participants;

        if (fs::is_regular_file(inputPath))
        {
            processDiscordPage(inputPath.string(), allMessages, participants);
        }
        else if (fs::is_directory(inputPath))
        {
            for (const auto& entry : fs::directory_iterator(inputPath))
            {
                if (entry.is_regular_file() &&
                    entry.path().extension() == ".json")
                {
                    std::cout << "Processing Discord JSON: "
                              << entry.path().string() << "\n";
                    processDiscordPage(entry.path().string(),
                                       allMessages,
                                       participants);
                }
            }
        }
        else
        {
            errorOut = "Input path is neither a file nor a directory: " +
                       inputPathStr;
            return false;
        }

        if (allMessages.empty())
        {
            errorOut = "No messages found in Discord JSON.";
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
            out["thread_path"]          = "discord/converted";
            out["magic_words"]          = json::array();

            std::size_t fileIndex = c + 1;
            std::ostringstream ossName;
            ossName << "message_" << fileIndex << ".json";
            std::string fileName = ossName.str();

            fs::path outFilePath = outputDir / fs::u8path(fileName);

            std::ofstream ofs(outFilePath.string(), std::ios::binary);
            if (!ofs)
            {
                errorOut = "Failed to open output file: " +
                           outFilePath.string();
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
