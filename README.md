# 📊 ChatAnalyzer – Cross-Platform Chat Analytics (C++17)

ChatAnalyzer is a standalone Windows application written in modern C++17 that analyzes exported conversations from **Instagram**, **WhatsApp**, and **Discord**.  
It converts chat exports into a unified format, computes detailed conversation metrics, and provides non-interactive visual charts — all in a fast(?), self-contained Windows application.

---

## 📈 What Insights Does ChatAnalyzer Provide?
---
## **Message Statistics & Conversation Dynamics**
- Total messages sent  
- Per-user activity  
- Longest messages (smartly abbreviated preview)  
- Average message length  
- Most frequently used words (noise filtered out)  
- Double-text & triple-text patterns  
- Average response time between messages  
- Distribution of fast replies (<1 min, <5 min, <30 min, etc.)  
- Monthly activity trends for each user
## ** Sentiment Analysis Results **
- Per-message **VADER polarity scores**  
- Monthly emotional intensity (with negative clamping for readability)  
- NRC emotion categories with top contributing words
## ** Behavioral & Temporal Trends**
- Hour-by-weekday message heatmap  
- Monthly message volume  
- Monthly average message length  
- Monthly romantic-phrase frequency  
- Multi-user comparative line graphs  

---
---
### **Sentiment Analysis**
ChatAnalyzer uses two independent, research-backed lexicons:
**VADER's Sentiment analysis** is the process of evaluating text to determine its emotional tone — positive, negative, or neutral.
**NRC's Emotion analysis goes** deeper by categorizing text into discrete emotional states like joy, anger, fear, trust, and more.

### 🔹 VADER Sentiment Analysis  
A rule-based sentiment model optimized for **social media language** and short conversational text.  
It generates:

- Positive score  
- Negative score  
- Neutral score  
- A **compound** score (overall emotional intensity)

**VADER is ideal for:** slang, emojis, emphasis, exaggeration, short chat messages.

📖 Official VADER Paper:  
https://github.com/cjhutto/vaderSentiment

### 🔹 NRC Emotion Lexicon  
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

**NRC excels at:** fine-grained emotional classification and long-term emotional trend analysis.

📖 NRC Lexicon Page:  
https://saifmohammad.com/WebPages/NRC-Emotion-Lexicon.htm

---

## ✅ How to Download & Run

1. Download the latest **ChatAnalyzer.zip** from the GitHub **Releases** section.  
2. Extract the folder anywhere.  
3. Make sure these files stay together: ChatAnalyzer.exe, vader_lexicon.txt, nrc_emotion_lexicon.txt
4. Run `ChatAnalyzer.exe`.  
5. Export your chat from Instagram / WhatsApp / Discord and load it into the app.
---

## 🚀 Supported Export Formats

### **Instagram (JSON export)**
Parses Meta’s DM export, including:
- Participants  
- Message bodies  
- Reactions  
- Timestamps  

### **Discord (Discrub export → converter)**
A converter transforms Discrub’s exported folder into the unified JSON schema.  
Handles:
- Timestamp normalization  
- Sender mapping  
- Multi-line reconstruction  
- JSON output compatible with the analytics engine  

### **WhatsApp (`_chat.txt` export → converter)**
The plaintext export is normalized into the unified schema with:
- 12-hour timestamps (AM/PM)
- Unicode cleanup (LTR marks, bidi characters, smart quotes)
- Multi-line message joining
- Sender extraction
- System-message filtering

---

## 🧱 Unified Data Structure

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

## Stop words
The following words won't be counted towards for the 'top 10 words' statistic as they add too many useless words. Feel free to edit it in the Count_messages.cpp:
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
## Romantic Phrases
The following words are counted towards the romantic phrases stat. Feel free to edit it in the Count_messages.cpp as it's impossible to generalize romance for every chat. Careful that you don't include any phrases that are already included by others. For Example "Love you" and "I Love you" would take the message "Hey, I think I love you alot." and increment the counter by 2. 
```json
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
```



