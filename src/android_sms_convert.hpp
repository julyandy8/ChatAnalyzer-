// android_sms_convert.hpp
#pragma once

#include <string>

// xmlPath:
//   - Path to the SMS Backup & Restore XML file (e.g. sms-20251211110655.xml).
//
// targetAddressOrName:
//   - If non-empty, only messages where 'address' OR 'contact_name'
//     matches this string exactly will be included.
//   - Example: "+16465230288" or "Mom".
//
// outFolder:
//   - Destination folder where message_#.json files will be written.
//
// errorOut:
//   - On failure, filled with a human-readable error message.
//
// Returns true on success, false on error.
bool ConvertAndroidSmsXmlToInstagramFolder(
    const std::string& xmlPath,
    const std::string& targetAddressOrName,
    const std::string& outFolder,
    std::string&       errorOut
);
