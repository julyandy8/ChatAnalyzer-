#pragma once

#include <string>
#include <vector>

// Basic info about an iMessage chat (conversation) discovered in chat.db.
struct ImessageChatInfo
{
    // The internal GUID Apple uses for the chat (e.g. "iMessage;+;chat123...").
    // This is what you'll pass into ConvertImessageChatToInstagramFolder.
    std::string guid;

    // Display name Apple shows for the chat (group name, or sometimes a phone/email).
    std::string displayName;

    // True if this chat appears to have more than 2 participants.
    bool isGroup = false;

    // Phone numbers / emails of the participants (best-effort; may be empty for you/"Me").
    std::vector<std::string> participants;
};

// Discover all chats in an iMessage database or iOS backup.
// backupRootOrDbPath:
//   - If directory: treat as iOS backup root (has Manifest.db).
//   - If file:      treat as direct chat.db / sms.db.
// On success returns true and fills outChats; on failure returns false and sets errorOut.
bool GetImessageChats(
    const std::string& backupRootOrDbPath,
    std::vector<ImessageChatInfo>& outChats,
    std::string& errorOut
);

// Export a single chat (chosen by GUID) into an Instagram-style folder that
// Count_Messages.cpp can consume.
//
// backupRootOrDbPath:
//   - Same rule as above.
//
// chatGuid:
//   - One of the guid values returned by GetImessageChats.
//
// outFolder:
//   - Folder to create/write Instagram-style message_#.json files into.
//
bool ConvertImessageChatToInstagramFolder(
    const std::string& backupRootOrDbPath,
    const std::string& chatGuid,
    const std::string& outFolder,
    std::string&       errorOut
);
