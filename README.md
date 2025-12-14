# 📊 ChatAnalyzer – Cross-Platform Chat Analytics (C++17)

ChatAnalyzer is a standalone Windows application written in modern C++17 that analyzes exported conversations exported chat histories from multiple messaging platforms and presents detailed statistics, sentiment analysis, and visual timelines — all without requiring any technical setup
It converts chat exports into a unified format, computes detailed conversation metrics, and provides non-interactive visual charts — all in a fast(?), self-contained Windows application.

---

# 🚀 Quick Start 
1. Download the files from the **Releases** section or the `dist/` folder:
   - `ChatAnalyzer.exe`
   - `nrc_emotion_lexicon.txt`
   - `vader_lexicon.txt`
2. Place all files in the **same folder**
3. Double-click `ChatAnalyzer.exe`
4. Use the buttons at the top to import or convert your chat data
5. Click **Run Analysis**

That’s it. Depending on what platform your initial chats are on - you'll have to a) ask the platform for the data b) use one of the tools listed to get the data or c) for Imsg and Android -->get ur unencrypted backups and convert them using the tool.

# 💬 Supported Platforms
Currently supports analysis of exported conversations from:
- **Instagram** (Meta data export, JSON)
- **WhatsApp** (exported `_chat.txt`)
- **Discord** (JSON exports via tools such as Discrub)
- **Android SMS** (SMS Backup & Restore XML)
- **iMessage** (iOS backups or direct `chat.db`)

All formats are converted into a unified internal structure before analysis.
## 🧮 Message Statistics
- Total messages sent  
- Per-user activity  
- Longest messages (smartly abbreviated preview)  
- Average message length  
- Most frequently used words (noise filtered out)  
- Double-text & triple-text patterns  
## ⏱ Conversation Dynamics
- Average response time between users
- Distribution of fast replies (<1 min, <5 min, <30 min, etc.)  
- Monthly average response times per user
## ❤️ Romantic / Expressive Metrics
- Detection of romantic or affectionate messages
- Monthly romantic message counts
- Per-user romantic message trends
## Behavioral Trends
- Hour-by-weekday message heatmap  
- Monthly message volume  
- Monthly average message length   
- Multi-user comparative line graphs  
----


# Sentiment Analysis
ChatAnalyzer uses two independent, research-backed lexicons:
**VADER's Sentiment analysis** is the process of evaluating text to determine its emotional tone — positive, negative, or neutral.
**NRC's Emotion analysis goes** deeper by categorizing text into discrete emotional states like joy, anger, fear, trust, and more.

## 🔹 VADER Sentiment Analysis  
A rule-based sentiment model optimized for **social media language** and short conversational text.  
It generates:

- Positive score  
- Negative score  
- Neutral score  
- A **compound** score (overall emotional intensity)

**VADER is ideal for:** slang, emojis, emphasis, exaggeration, short chat messages.

📖 Official VADER Paper:  
https://github.com/cjhutto/vaderSentiment

## 🔹 NRC Emotion Lexicon  
A curated dataset mapping thousands of English words to **eight core emotions**:

- Joy  
- Sadness  
- Anger  
- Fear  
- Trust  
- Disgust  
- Surprise  
- Anticipation  

Plus two broader sentiments: **Positive** and **Negative**.
Results are aggregated per user and displayed in a clear, readable format.
**NRC excels at:** fine-grained emotional classification and long-term emotional trend analysis.

📖 NRC Lexicon Page:  
https://saifmohammad.com/WebPages/NRC-Emotion-Lexicon.htm

---

# Examples of Conversion Techniques
## *Discord (Discrub export → converter)*
A converter transforms Discrub’s exported folder into the unified JSON schema.  
Handles:
- Timestamp normalization  
- Sender mapping  
- Multi-line reconstruction  
- JSON output compatible with the analytics engine  

## **WhatsApp (`_chat.txt` export → converter)**
The plaintext export is normalized into the unified schema with:
- 12-hour timestamps (AM/PM)
- Unicode cleanup (LTR marks, bidi characters, smart quotes)
- Multi-line message joining
- Sender extraction
- System-message filtering

---

# 🧱 Unified Data Structure

All platforms map into the following JSON format:

```json
{
  "participants": [{ "name": "User1" }, { "name": "User2" }],
  "messages": [
    {
      "sender_name": "User1",
      "timestamp_ms": 1510117204000,
      "content": "Hello!"
    }
  ],
  "title": "Chat Title",
  "thread_path": "converted",
  "is_still_participant": true
}
```
## 🛠 Technical Overview

- **Language:** C++17
- **GUI:** Native Win32 API
- **Data Storage:** SQLite (read-only)
- **JSON Parsing:** nlohmann/json
- **Sentiment Models:** VADER, NRC Emotion Lexicon
- **Build Style:** Fully static, offline-capable executable

### Stop words
The following words won't be counted towards for the 'top 10 words' statistic as they add too many useless words. Feel free to edit it in the Count_messages.cpp:
It's basically the top 100 most used words in english + filler words. 
```json
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
```
### Romantic Phrases
Below are SOME Examples of words which are counted towards the romantic phrases stat. Feel free to edit it in the Count_messages.cpp as it's impossible to generalize romance for every chat. Careful that you don't include any phrases that are already included by others. For Example "Love you" and "I Love you" would take the message "Hey, I think I love you alot." and increment the counter by 2. 
```json
// affection / love
    "love you",
    "love u",
    "miss you",
    "miss u",
    // too many false positives "want you",
    // too many false positives "need you",
    "crave you",
 

    // attraction / desire
    "want you so bad",
    "want you bad",
    "need you bad",
    "craving you"
    "obsessed with you",
    "crazy about you",

    // emotional intimacy
    "you mean so much",
    "you mean everything",
    "my love",
    "my heart",

    // closeness / belonging
    "feel safe with you"
    "home with you",
    "you feel like home",


    // commitment / emotional weight
    "you complete me",
    "we belong together",
```



