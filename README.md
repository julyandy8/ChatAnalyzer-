# ChatAnalyzer-
📊 ChatAnalyzer – Cross-Platform Chat Analytics (C++17)

ChatAnalyzer is a standalone Windows application written entirely in modern C++17 that performs deep analysis of exported chat histories from major messaging platforms.
It transforms raw message archives into a unified internal format, computes rich conversation statistics, and renders interactive charts and summaries inside a custom-built Win32 GUI.

The project is self-contained, has no runtime dependencies, and ships with its own parsers, sentiment engines, and visualization code.

🚀 Supported Export Formats

ChatAnalyzer currently supports:

Instagram (native JSON export)

Parses the standard Meta JSON structure used for direct messages.

Discord (Discrub export → converter)

A custom converter transforms Discrub’s export folder into an Instagram-style JSON dataset, allowing Discord chats to be analyzed using the same logic pipeline.

WhatsApp (_chat.txt export → converter)

WhatsApp’s plaintext export is parsed and normalized into the same internal JSON format, including:

Timestamp parsing (12-hour with AM/PM)

Unicode cleanup (LTR marks, special spacing)

Multi-line message joining

System message filtering (media omitted, calls, deleted messages, encryption notices)

All platforms ultimately map into a unified structure:

{
  "participants": [...],
  "messages": [
    {
      "sender_name": "...",
      "timestamp_ms": 1510117204000,
      "content": "...",
      "is_geoblocked_for_viewer": false,
      "is_unsent_image_by_messenger_kid_parent": false
    }
  ],
  "title": "...",
  "thread_path": "...",
  "is_still_participant": true
}


This lets the entire analytics pipeline operate identically across sources.

🧠 Core Features & Analytics
Message Statistics

Total messages

Per-user message counts

Double-text / triple-text sequences

Longest messages (with smart truncation)

Average / median message length

Word frequency (with noise filtering: media omitted, calls, deleted messages, etc.)

Conversation Dynamics

Response-time estimation using timestamp deltas

Distribution of replies under different time thresholds

Per-user monthly averages

Sentiment Analysis

ChatAnalyzer includes two independent sentiment engines, implemented from scratch:

VADER Sentiment (compound score)

Uses vader_lexicon.txt

Computes per-message compound polarity

Aggregated into monthly emotional intensity per user

Graph automatically clamps negative values at zero for readability

NRC Emotion Lexicon

Joy, Sadness, Anger, Fear, Trust, Anticipation, Disgust, Surprise

Positive vs. Negative categories

Top contributing words displayed after each category

Behavioral Trends

Hour-vs-Weekday heatmap

Monthly message volume

Monthly average message length

Monthly romantic-phrase detection

Per-user timeline graphing with color-coded legend

Full Visualization Layer

A custom Win32 rendering canvas (not third-party UI) draws:

Multi-line graphs

Axis scaling

Dynamic color palettes

Scrollable chart surface

Label placement and collision avoidance

Custom heatmap renderer

All charts update automatically after each analysis pass.

🏗️ Internal Architecture
1. Data Conversion Layer

discord_convert.cpp

whatsapp_convert.cpp

Both convert platform-specific exports into normalized Instagram-style message files.
Raw string parsing, timestamp handling, Unicode cleanup, line continuation, and system message filtering occur here.

2. Analytics Engine (Count_Messages.cpp)

This is the heart of the project. It handles:

Time-indexing messages

Aggregating by month / weekday / hour

Per-user breakdowns

Sentiment scoring

Word counting

Response timing

Romantic keyword detection

Building global + per-user datasets used by the GUI

All analytics output is stored in global vectors such as:

g_monthlyCountPoints
g_userMonthlyEmotionSeries
g_heatmapCounts[7][24]
...


These are consumed by the GUI’s visual layer.

3. Sentiment Modules

vader_sentiment.cpp/.hpp

nrc_emotion.cpp/.hpp

Both lexicons are loaded from the executable’s directory:

vader_lexicon.txt
nrc_emotion_lexicon.txt


Each message is tokenized, normalized, and scored according to its lexicon.

4. GUI Layer (gui_main.cpp)

A fully custom Win32 window:

Tab controls (Summary / Visuals)

Styled RichEdit output with color-coded text sections

A drawing canvas for interactive charts

File pickers & folder pickers

Platform conversion buttons (Discord / WhatsApp)

Background analysis threads

Smooth scrolling chart viewport

Layout engine for buttons, tabs, and dynamic resizing

All GUI code avoids external frameworks to keep the build minimal.

🔧 Building From Source

Requires:

GCC / MinGW-w64

C++17 support

Build command:

g++ -std=c++17 gui_main.cpp Count_Messages.cpp vader_sentiment.cpp nrc_emotion.cpp discord_convert.cpp whatsapp_convert.cpp -o ChatAnalyzer.exe -municode -mwindows -lcomdlg32 -lole32 -lcomctl32


Produces a single portable ChatAnalyzer.exe with no dependencies.

Your resulting folders should look something like this ideally:
ChatAnalyzer(Application)
Count_Messages(C++)
discord_convert(C++)
gui_main(c++)
json.hpp(c++)
nrc_emotion(c++)
nrc_emotion.hpp(C++)
nrc_emotion_lexicon(Text file)
vader_lexicon(text file)
vader_sentiment(C++)
vader_sentiment.hpp(c++)
whatsapp_convert(C++)

Note: The lexicons must be in the same folder as the Application.
End users do not need a compiler or libraries.
They only need to provide their exported chat data.

🔮 Future Improvements

More platform converters (iMessage, Telegram, Messenger)
Exportable graphs (PNG, SVG)
Embed fonts and themes
Add a plugin-based analytics system
Abstract the GUI layer for cross-platform builds

📝 License & Attribution

VADER lexicon: MIT Licensed — original source by C.J. Hutto

NRC Emotion Lexicon: Research dataset by Saif Mohammad & Peter Turney

JSON parsing: json.hpp by nlohmann (MIT)

ChatAnalyzer’s C++ source code is licensed under MIT unless otherwise specified.

❤️ Contributing

Pull requests are welcome — improvements to converters, visualization, sentiment algorithms, or UX are all great places to start.
