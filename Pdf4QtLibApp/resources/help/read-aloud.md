---
title: Read aloud
category: open-and-view
order: 48
summary: Let PDF4QT read the text of a document aloud with a voice of your system, and the settings of the voice.
keywords: speech, read aloud, text to speech, tts, voice, voice synthesizer, speech engine, language, locale, rate, speed, pitch, volume, play sample, accessibility, listen
related: /settings/speech, /help/read-documents, /help/settings, /help/keyboard-shortcuts
---
PDF4QT can read the text of a document aloud with a voice of your operating system. Open the panel **Speech** in the sidebar of the document.

## Read a document

1. Go to the page, where the reading should start.
2. Click **Play**. The text is read block after block, page after page, to the end of the document.
3. **Pause** stops the reading for a moment, **Resume** continues it; **Stop** ends it.

The card *Now reading · page …* shows the text being read; the words already read are darker. With **Follow the reading** (the button at the right) the document shows the page being read.

The command **Read aloud** starts and stops the reading without the panel. It has no shortcut by default – give it one in [Settings › Shortcuts](/settings/shortcuts).

## The voice

The panel has **Language**, **Voice**, **Rate**, **Pitch** and **Volume**. They are the same settings as [Settings › Speech](/settings/speech): a change in the panel applies to all documents and it is kept for the next start.

## Settings › Speech

| Setting | What it does | Default |
| --- | --- | --- |
| [Voice synthesizer](/settings/speech#voice-synthesizer) | The speech engine of your system, when it offers more of them – for example Windows (WinRT) and Windows (SAPI 5) on Windows, Speech Dispatcher on Linux | Automatic |
| [Locale](/settings/speech#locale) | The language and the region of the voice (called **Language** in the panel) | Automatic |
| [Voice](/settings/speech#voice) | One of the voices of the chosen language, with its gender and age | Automatic |
| [Rate](/settings/speech#rate) | Slower to faster, from −1 to 1; 0 is the normal speed | 0 |
| [Pitch](/settings/speech#pitch) | Lower to higher, from −1 to 1; 0 is the normal pitch | 0 |
| [Volume](/settings/speech#volume) | From silent (0) to full volume (1) | 1 |

**Automatic** uses the default engine, language and voice of your system. Each engine has its own languages and voices, so a new synthesizer or a new language may offer other voices; when you change the language, the voice goes back to Automatic. A voice, which is no longer installed, is shown as *(not available)*.

**Play sample** under *Preview* says a short sentence with the current voice and settings; click **Stop** to end it.

> [!NOTE] Voices of the system
> The voices come from the speech engine of your operating system (Windows, Speech Dispatcher on Linux, macOS). More voices can be installed in the settings of your system. When no engine is available, the panel says so and the reading cannot start.
