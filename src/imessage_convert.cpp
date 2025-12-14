// iMessage importer sort of copying the android style except you need sql zzzz
// - Discovers chats (with GUIDs + participants).
// - Exports ONE chosen chat in Instagram-style JSON chunks so the existing
//   analyzer can consume it just like Instagram/Discord exports.

#include "imessage_convert.hpp"

#include <string>
#include <vector>
#include <set>
#include <stdexcept>
#include <filesystem>
#include <sstream>
#include <fstream>
#include <algorithm>
#include <iostream>

#include "json.hpp"
#include "sqlite3.h"

namespace fs = std::filesystem;
using json   = nlohmann::json;

// ---------------------------
// Shared "Instagram-style" message shape
// (mirrors discord_convert.cpp)
// ---------------------------
struct InstaMessage
{
    std::string sender_name;
    long long   timestamp_ms = 0;
    std::string content;
};

// ---------------------------
// RAII wrappers for sqlite3
// ---------------------------
struct SqliteDb
{
    sqlite3* db = nullptr;

    explicit SqliteDb(const std::string& path)
    {
        if (sqlite3_open_v2(path.c_str(), &db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK)
        {
            std::string msg = "Failed to open SQLite DB: ";
            msg += path;
            throw std::runtime_error(msg);
        }
    }

    ~SqliteDb()
    {
        if (db)
        {
            sqlite3_close(db);
        }
    }
};

struct SqliteStmt
{
    sqlite3_stmt* stmt = nullptr;

    SqliteStmt(sqlite3* db, const char* sql)
    {
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
        {
            std::string msg = "Failed to prepare SQL: ";
            msg += sqlite3_errmsg(db);
            throw std::runtime_error(msg);
        }
    }

    ~SqliteStmt()
    {
        if (stmt)
        {
            sqlite3_finalize(stmt);
        }
    }
};

// ---------------------------
// Apple time → Unix ms
// ---------------------------
static long long appleTimeToUnixMs(long long raw)
{
    // Apple stores timestamps as seconds (old) or nanoseconds (new)
    // since 2001-01-01 00:00:00 UTC.
    constexpr long long APPLE_TO_UNIX_OFFSET_SEC = 978307200LL;

    double secondsSince2001;
    if (raw > 1000000000000LL)
    {
        // Looks like nanoseconds
        secondsSince2001 = static_cast<double>(raw) / 1'000'000'000.0;
    }
    else
    {
        // Treat as seconds
        secondsSince2001 = static_cast<double>(raw);
    }

    double unixSeconds = secondsSince2001 + static_cast<double>(APPLE_TO_UNIX_OFFSET_SEC);
    if (unixSeconds < 0) unixSeconds = 0;

    long long unixMs = static_cast<long long>(unixSeconds * 1000.0);
    return unixMs < 0 ? 0 : unixMs;
}

// ---------------------------
// Backup root / DB resolution
// ---------------------------

// Find chat.db or sms.db within an iOS backup root using Manifest.db.
static std::string findImessageDbInBackup(const std::string& backupRoot)
{
    fs::path root(backupRoot);
    fs::path manifestPath = root / "Manifest.db";

    if (!fs::exists(manifestPath))
    {
        throw std::runtime_error("Manifest.db not found in backup root: " + backupRoot);
    }

    SqliteDb db(manifestPath.string());

    const char* SQL = R"SQL(
        SELECT fileID
        FROM Files
        WHERE domain = 'HomeDomain'
          AND (relativePath = 'Library/SMS/sms.db'
               OR relativePath = 'Library/Messages/chat.db')
        LIMIT 1
    )SQL";

    SqliteStmt stmt(db.db, SQL);
    int rc = sqlite3_step(stmt.stmt);
    if (rc != SQLITE_ROW)
    {
        throw std::runtime_error("Could not find sms.db/chat.db entry in Manifest.db");
    }

    const unsigned char* fileid_c = sqlite3_column_text(stmt.stmt, 0);
    if (!fileid_c)
    {
        throw std::runtime_error("fileID is NULL in Manifest.db");
    }

    std::string fileID = reinterpret_cast<const char*>(fileid_c);
    if (fileID.size() < 2)
    {
        throw std::runtime_error("fileID too short in Manifest.db");
    }

    fs::path dbPath = root / fileID.substr(0, 2) / fileID;
    if (!fs::exists(dbPath))
    {
        throw std::runtime_error("Resolved iMessage DB path does not exist: " + dbPath.string());
    }

    return dbPath.string();
}

// Given either a backup root or a direct chat.db path, return the resolved DB path.
static std::string resolveDbPath(const std::string& backupRootOrDbPath)
{
    fs::path p(backupRootOrDbPath);
    if (fs::is_directory(p))
    {
        // iOS backup root
        return findImessageDbInBackup(backupRootOrDbPath);
    }
    else
    {
        // Direct chat.db / sms.db
        if (!fs::exists(p))
        {
            throw std::runtime_error("Database file does not exist: " + backupRootOrDbPath);
        }
        return backupRootOrDbPath;
    }
}

// ---------------------------
// Chat discovery
// ---------------------------

// Fill a vector with basic info on every chat (conversation) in the DB.
static void discoverChatsFromDb(
    const std::string& dbPath,
    std::vector<ImessageChatInfo>& outChats)
{
    SqliteDb db(dbPath);

    // 1) Get all chats with display names & GUIDs.
    const char* SQL_CHATS = R"SQL(
        SELECT
            ROWID,
            guid,
            COALESCE(display_name, chat_identifier, guid) AS disp
        FROM chat
        ORDER BY ROWID
    )SQL";

    SqliteStmt stmtChats(db.db, SQL_CHATS);

    struct ChatRow
    {
        long long rowid = 0;
        std::string guid;
        std::string display;
    };

    std::vector<ChatRow> chatRows;
    while (true)
    {
        int rc = sqlite3_step(stmtChats.stmt);
        if (rc == SQLITE_DONE) break;
        if (rc != SQLITE_ROW)
        {
            std::string msg = "SQLite step error while reading chats: ";
            msg += sqlite3_errmsg(db.db);
            throw std::runtime_error(msg);
        }

        ChatRow cr;
        cr.rowid = sqlite3_column_int64(stmtChats.stmt, 0);

        const unsigned char* guid_c = sqlite3_column_text(stmtChats.stmt, 1);
        const unsigned char* disp_c = sqlite3_column_text(stmtChats.stmt, 2);

        cr.guid    = guid_c ? reinterpret_cast<const char*>(guid_c) : "";
        cr.display = disp_c ? reinterpret_cast<const char*>(disp_c) : cr.guid;

        if (!cr.guid.empty())
        {
            chatRows.push_back(std::move(cr));
        }
    }

    // 2) For each chat, find its participants via chat_handle_join + handle.id.
    const char* SQL_PARTICIPANTS = R"SQL(
        SELECT handle.id
        FROM chat_handle_join
        JOIN handle ON handle.ROWID = chat_handle_join.handle_id
        WHERE chat_handle_join.chat_id = ?
    )SQL";

    SqliteStmt stmtParts(db.db, SQL_PARTICIPANTS);

    outChats.clear();
    outChats.reserve(chatRows.size());

    for (const auto& cr : chatRows)
    {
        ImessageChatInfo info;
        info.guid         = cr.guid;
        info.displayName  = cr.display;
        info.isGroup      = false;
        info.participants = {};

        // Bind chat_id = cr.rowid
        sqlite3_reset(stmtParts.stmt);
        sqlite3_clear_bindings(stmtParts.stmt);
        sqlite3_bind_int64(stmtParts.stmt, 1, cr.rowid);

        std::set<std::string> partSet;

        while (true)
        {
            int rc = sqlite3_step(stmtParts.stmt);
            if (rc == SQLITE_DONE) break;
            if (rc != SQLITE_ROW)
            {
                std::string msg = "SQLite step error while reading chat participants: ";
                msg += sqlite3_errmsg(db.db);
                throw std::runtime_error(msg);
            }

            const unsigned char* id_c = sqlite3_column_text(stmtParts.stmt, 0);
            if (id_c)
            {
                partSet.insert(reinterpret_cast<const char*>(id_c));
            }
        }

        info.participants.assign(partSet.begin(), partSet.end());
        info.isGroup = (info.participants.size() > 1);

        outChats.push_back(std::move(info));
    }
}

bool GetImessageChats(
    const std::string& backupRootOrDbPath,
    std::vector<ImessageChatInfo>& outChats,
    std::string& errorOut)
{
    try
    {
        std::string dbPath = resolveDbPath(backupRootOrDbPath);
        discoverChatsFromDb(dbPath, outChats);
        errorOut.clear();
        return true;
    }
    catch (const std::exception& ex)
    {
        errorOut = ex.what();
        outChats.clear();
        return false;
    }
}

// ---------------------------
// Per-chat export
// ---------------------------

// Load messages for a single chat GUID from the DB into InstaMessage array + participants set.
static void loadMessagesForChat(
    const std::string& dbPath,
    const std::string& chatGuid,
    std::vector<InstaMessage>& outMessages,
    std::set<std::string>&     participants)
{
    SqliteDb db(dbPath);

    // 1) Look up the chat's ROWID by GUID.
    const char* SQL_FIND_CHAT = R"SQL(
        SELECT ROWID
        FROM chat
        WHERE guid = ?
        LIMIT 1
    )SQL";

    SqliteStmt stmtFindChat(db.db, SQL_FIND_CHAT);
    sqlite3_bind_text(stmtFindChat.stmt, 1, chatGuid.c_str(), -1, SQLITE_TRANSIENT);

    int rc = sqlite3_step(stmtFindChat.stmt);
    if (rc != SQLITE_ROW)
    {
        throw std::runtime_error("Chat GUID not found in chat table: " + chatGuid);
    }

    long long chatRowId = sqlite3_column_int64(stmtFindChat.stmt, 0);

    // 2) Query all messages for that chat.
    const char* SQL_MESSAGES = R"SQL(
        SELECT
            message.is_from_me,
            message.date,
            message.text,
            handle.id
        FROM chat_message_join
        JOIN message ON message.ROWID = chat_message_join.message_id
        LEFT JOIN handle ON handle.ROWID = message.handle_id
        WHERE chat_message_join.chat_id = ?
        ORDER BY message.date
    )SQL";

    SqliteStmt stmtMsgs(db.db, SQL_MESSAGES);
    sqlite3_bind_int64(stmtMsgs.stmt, 1, chatRowId);

    while (true)
    {
        rc = sqlite3_step(stmtMsgs.stmt);
        if (rc == SQLITE_DONE) break;
        if (rc != SQLITE_ROW)
        {
            std::string msg = "SQLite step error while reading messages: ";
            msg += sqlite3_errmsg(db.db);
            throw std::runtime_error(msg);
        }

        int isFromMeInt = sqlite3_column_int(stmtMsgs.stmt, 0);
        long long rawDate = sqlite3_column_int64(stmtMsgs.stmt, 1);
        const unsigned char* text_c   = sqlite3_column_text(stmtMsgs.stmt, 2);
        const unsigned char* handle_c = sqlite3_column_text(stmtMsgs.stmt, 3);

        std::string text     = text_c   ? reinterpret_cast<const char*>(text_c)   : "";
        std::string handleId = handle_c ? reinterpret_cast<const char*>(handle_c) : "";
        bool isFromMe        = (isFromMeInt != 0);

        if (text.empty())
        {
            // Skip system/reaction messages with no text.
            continue;
        }

        InstaMessage msgOut;

        // For now, treat "me" as a fixed name "Me".
        // If you later map this to the actual Instagram-style profile name,
        // just change this string.
        if (isFromMe)
        {
            msgOut.sender_name = "Me";
        }
        else
        {
            msgOut.sender_name = handleId.empty() ? "Unknown" : handleId;
        }

        participants.insert(msgOut.sender_name);

        msgOut.timestamp_ms = appleTimeToUnixMs(rawDate);
        msgOut.content      = text;

        outMessages.push_back(std::move(msgOut));
    }
}

// Write Instagram-style JSON chunks into outFolder.
static bool writeInstagramStyleJson(
    const std::vector<InstaMessage>& allMessages,
    const std::set<std::string>&     participants,
    const std::string&               outFolder,
    std::string&                     errorOut)
{
    try
    {
        fs::path outDir = fs::u8path(outFolder);
        if (!fs::exists(outDir))
        {
            fs::create_directories(outDir);
        }

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
            out["participants"]         = participantsJson;
            out["messages"]             = json::array();
            out["title"]                = "";  // optional: fill in later if you want
            out["is_still_participant"] = true;
            out["thread_type"]          = "Regular";
            out["thread_path"]          = "imessage/converted";
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

            std::ostringstream oss;
            oss << "message_" << (c + 1) << ".json";
            fs::path outPath = outDir / fs::u8path(oss.str());

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

bool ConvertImessageChatToInstagramFolder(
    const std::string& backupRootOrDbPath,
    const std::string& chatGuid,
    const std::string& outFolder,
    std::string&       errorOut)
{
    try
    {
        if (chatGuid.empty())
        {
            errorOut = "chatGuid is empty.";
            return false;
        }

        std::string dbPath = resolveDbPath(backupRootOrDbPath);

        std::vector<InstaMessage> allMessages;
        std::set<std::string>     participants;

        loadMessagesForChat(dbPath, chatGuid, allMessages, participants);

        if (allMessages.empty())
        {
            errorOut = "Selected chat has no text messages.";
            return false;
        }

        // Ensure chronological order (just in case).
        std::sort(
            allMessages.begin(),
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
