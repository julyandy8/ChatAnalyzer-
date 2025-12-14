
// Convert Android "SMS Backup & Restore" XML exports into Instagram-style JSON
// Notes:
// - We parse the XML file line-by-line and extract <sms .../> blocks.
// - We still keep messages in memory to sort chronologically before writing output.
// 

#include "android_sms_convert.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "json.hpp"

namespace fs = std::filesystem;
using json = nlohmann::json;

struct InstaMessage
{
    std::string sender_name;
    long long   timestamp_ms = 0;
    std::string content;
};

// -----------------------------------------------------------------------------
// Small helpers
// -----------------------------------------------------------------------------

static bool extractXmlAttribute(const std::string& src,
                                const std::string& attrName,
                                std::string&       out)
{
    const std::string pattern = attrName + "=\"";
    std::size_t pos = src.find(pattern);
    if (pos == std::string::npos)
        return false;

    pos += pattern.size();
    std::size_t end = src.find('"', pos);
    if (end == std::string::npos)
        return false;

    out.assign(src.begin() + static_cast<std::ptrdiff_t>(pos),
               src.begin() + static_cast<std::ptrdiff_t>(end));
    return true;
}

static bool isNullOrEmpty(const std::string& s)
{
    return s.empty() || s == "null";
}

static int hexValue(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
}

// Decodes XML escapes used by SMS Backup & Restore
// including numeric entities like &#10; and &#xA;.
static std::string xmlUnescape(const std::string& s)
{
    std::string out;
    out.reserve(s.size());

    for (std::size_t i = 0; i < s.size(); ++i)
    {
        if (s[i] != '&')
        {
            out.push_back(s[i]);
            continue;
        }

        // Look for the next ';'
        std::size_t semi = s.find(';', i + 1);
        if (semi == std::string::npos)
        {
            out.push_back(s[i]);
            continue;
        }

        const std::string token = s.substr(i + 1, semi - (i + 1));

        if (token == "amp")      { out.push_back('&'); i = semi; continue; }
        if (token == "lt")       { out.push_back('<'); i = semi; continue; }
        if (token == "gt")       { out.push_back('>'); i = semi; continue; }
        if (token == "quot")     { out.push_back('"'); i = semi; continue; }
        if (token == "apos")     { out.push_back('\''); i = semi; continue; }

        // Numeric: &#123; or &#x1A;
        if (!token.empty() && token[0] == '#')
        {
            long long codePoint = -1;

            if (token.size() >= 2 && (token[1] == 'x' || token[1] == 'X'))
            {
                // Hex
                long long value = 0;
                bool ok = true;
                for (std::size_t k = 2; k < token.size(); ++k)
                {
                    int hv = hexValue(token[k]);
                    if (hv < 0) { ok = false; break; }
                    value = (value * 16) + hv;
                }
                if (ok) codePoint = value;
            }
            else
            {
                // Decimal
                long long value = 0;
                bool ok = true;
                for (std::size_t k = 1; k < token.size(); ++k)
                {
                    if (!std::isdigit(static_cast<unsigned char>(token[k])))
                    {
                        ok = false;
                        break;
                    }
                    value = (value * 10) + (token[k] - '0');
                }
                if (ok) codePoint = value;
            }

            // Basic UTF-8 encode for BMP + a bit beyond (enough for typical SMS).
            if (codePoint >= 0 && codePoint <= 0x10FFFF)
            {
                if (codePoint <= 0x7F)
                {
                    out.push_back(static_cast<char>(codePoint));
                }
                else if (codePoint <= 0x7FF)
                {
                    out.push_back(static_cast<char>(0xC0 | ((codePoint >> 6) & 0x1F)));
                    out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
                }
                else if (codePoint <= 0xFFFF)
                {
                    out.push_back(static_cast<char>(0xE0 | ((codePoint >> 12) & 0x0F)));
                    out.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
                    out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
                }
                else
                {
                    out.push_back(static_cast<char>(0xF0 | ((codePoint >> 18) & 0x07)));
                    out.push_back(static_cast<char>(0x80 | ((codePoint >> 12) & 0x3F)));
                    out.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
                    out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
                }

                i = semi;
                continue;
            }
        }

        // Unknown entity; keep original text
        out.append(s, i, (semi - i + 1));
        i = semi;
    }

    return out;
}

// -----------------------------------------------------------------------------
// Write messages + participants to Instagram-style JSON chunks
// -----------------------------------------------------------------------------

static bool writeInstagramStyleJson(const std::vector<InstaMessage>& allMessages,
                                   const std::set<std::string>&     participants,
                                   const std::string&               outFolder,
                                   std::string&                     errorOut)
{
    try
    {
        fs::path outDir = fs::u8path(outFolder);
        if (!fs::exists(outDir))
            fs::create_directories(outDir);

        json participantsJson = json::array();
        for (const auto& name : participants)
        {
            json p;
            p["name"] = name;
            participantsJson.push_back(std::move(p));
        }

        const std::size_t CHUNK_SIZE = 5000;
        const std::size_t total      = allMessages.size();
        std::size_t chunks           = (total + CHUNK_SIZE - 1) / CHUNK_SIZE;
        if (chunks == 0) chunks = 1;

        for (std::size_t c = 0; c < chunks; ++c)
        {
            const std::size_t start = c * CHUNK_SIZE;
            const std::size_t end   = std::min(start + CHUNK_SIZE, total);

            json out;
            out["participants"]         = participantsJson;
            out["messages"]             = json::array();
            out["title"]                = "";
            out["is_still_participant"] = true;
            out["thread_type"]          = "Regular";
            out["thread_path"]          = "android_sms/converted";
            out["magic_words"]          = json::array();

            for (std::size_t i = start; i < end; ++i)
            {
                const auto& im = allMessages[i];
                json m;
                m["sender_name"]  = im.sender_name;
                m["timestamp_ms"] = im.timestamp_ms;
                m["content"]      = im.content;
                out["messages"].push_back(std::move(m));
            }

            std::ostringstream filename;
            filename << "message_" << (c + 1) << ".json";
            fs::path outPath = outDir / fs::u8path(filename.str());

            std::ofstream ofs(outPath, std::ios::binary);
            if (!ofs)
            {
                errorOut = "Failed to open output file: " + outPath.string();
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

// -----------------------------------------------------------------------------
// Core conversion
// -----------------------------------------------------------------------------

bool ConvertAndroidSmsXmlToInstagramFolder(const std::string& xmlPath,
                                          const std::string& targetAddressOrName,
                                          const std::string& outFolder,
                                          std::string&       errorOut)
{
    try
    {
        fs::path inPath = fs::u8path(xmlPath);
        if (!fs::exists(inPath))
        {
            errorOut = "Android SMS XML file does not exist: " + xmlPath;
            return false;
        }

        std::ifstream in(inPath, std::ios::binary);
        if (!in)
        {
            errorOut = "Failed to open Android SMS XML file: " + xmlPath;
            return false;
        }

        std::vector<InstaMessage> allMessages;
        std::set<std::string>     participants;
        participants.insert("Me");

        const bool hasFilter = !targetAddressOrName.empty();

        std::string line;
        bool        inSms = false;
        std::string smsChunk;

        while (std::getline(in, line))
        {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();

            if (!inSms)
            {
                std::size_t pos = line.find("<sms ");
                if (pos == std::string::npos)
                    continue;

                inSms = true;
                smsChunk.clear();
                smsChunk.append(line.substr(pos));
                smsChunk.push_back('\n');

                if (smsChunk.find("/>") != std::string::npos)
                    inSms = false;
                else
                    continue;
            }
            else
            {
                smsChunk.append(line);
                smsChunk.push_back('\n');

                if (line.find("/>") == std::string::npos)
                    continue;

                inSms = false;
            }

            std::string address;
            std::string contactName;
            std::string dateStr;
            std::string typeStr;
            std::string body;

            extractXmlAttribute(smsChunk, "address",      address);
            extractXmlAttribute(smsChunk, "contact_name", contactName);
            extractXmlAttribute(smsChunk, "date",         dateStr);
            extractXmlAttribute(smsChunk, "type",         typeStr);
            extractXmlAttribute(smsChunk, "body",         body);

            // XML-escaped attributes are common in SMS exports.
            address     = xmlUnescape(address);
            contactName = xmlUnescape(contactName);
            body        = xmlUnescape(body);

            if (isNullOrEmpty(body))
                continue;

            if (hasFilter)
            {
                // Exact match behavior (your original intent).
                if (address != targetAddressOrName &&
                    contactName != targetAddressOrName)
                {
                    continue;
                }
            }

            std::string remoteName;
            if (!isNullOrEmpty(contactName) && contactName != "(Unknown)")
                remoteName = contactName;
            else
                remoteName = address.empty() ? "(Unknown)" : address;

            participants.insert(remoteName);

            if (isNullOrEmpty(dateStr))
                continue;

            long long tsMs = 0;
            try
            {
                tsMs = std::stoll(dateStr);
            }
            catch (...)
            {
                continue;
            }

            InstaMessage im;
            if (typeStr == "2")
                im.sender_name = "Me";       // outgoing
            else
                im.sender_name = remoteName; // incoming 

            im.timestamp_ms = tsMs;
            im.content      = body;

            allMessages.push_back(std::move(im));
        }

        if (allMessages.empty())
        {
            if (hasFilter)
            {
                errorOut = "No SMS messages found matching address/contact: \"" +
                           targetAddressOrName + "\".";
            }
            else
            {
                errorOut = "No SMS messages were found in the XML file.";
            }
            return false;
        }

        std::sort(allMessages.begin(),
                  allMessages.end(),
                  [](const InstaMessage& a, const InstaMessage& b)
                  {
                      return a.timestamp_ms < b.timestamp_ms;
                  });

        return writeInstagramStyleJson(allMessages, participants, outFolder, errorOut);
    }
    catch (const std::exception& ex)
    {
        errorOut = ex.what();
        return false;
    }
}
