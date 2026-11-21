---
title: Create Audio Book
category: tools
order: 75
summary: read the text of a document aloud into an audio file
keywords: audio book, audiobook, speech, text to speech, voice, read aloud, wav, sound, listen, tts
related: /tools/convert.audiobook, /help/tools
---
Create Audio Book reads the text of the document with a voice of your system and saves the speech as a WAV file. You choose what is read and in which order; the document is not changed.

### Step by step

1. The left panel shows the text stream: every paragraph, heading and other item of the text, in the order it will be read. A green bar means the item is read, red means it is skipped, orange that you changed its text.
2. Click an item in the list or on the page; Ctrl adds more items to the selection. Delete skips the selected items, Shift+Delete makes them read again. Alt+Up and Alt+Down move them.
3. The right panel shows the text of the selected item. Edit it to fix how it is read – an abbreviation, a number, a name. Restore brings the original text back.
4. Choose the voice in the toolbar, and the speed and the volume in the right panel. Play Sample lets you hear them.
5. Press Create Audio Book. When the audio is ready, choose where the file goes.

### Choosing the items

The menu **Select** selects items by a rectangle drawn on the page, by a text they contain (upper and lower case matter), by a regular expression or by a list of pages. The chips **All**, **Skipped** and **Modified** filter the list.

### Reading order

| Order | What it does |
| --- | --- |
| Auto | The structure, when the document is tagged; otherwise the layout |
| Layout | Blocks of text as they are placed on the page |
| Content stream | The order in which the text is written in the file |
| Structure | Follows the tags of a tagged document |

A new order reads the text stream again. Your skipped items, edited texts and moved items are lost, so the tool asks first. **Rebuild** does the same with the current order. Both can be undone. Closing the tool with changed items (skipped or read items, edited texts, a changed order) asks first, also when you close the tab or the window or open another tool.

### Tagged documents

The tags of a tagged document tell more than the text:

- An **actual text** replaces the text it belongs to – for example a word drawn letter by letter, or a formula. It is read instead of that text.
- An **expanded form** of an abbreviation is read instead of the abbreviation.
- A picture with an **alternative description** and no text is read by its description.
- A **language** of a part of the text picks a voice of that language, when your system has one and the chosen voice does not speak it; otherwise the chosen voice reads it.
- **Artifacts** – running heads, page numbers and other decoration marked as such – are skipped. Mark them as read if you want them.

Page numbers and titles of the structure are skipped too, until you mark them as read.

### Voices and platforms

The tool uses the voices of the system. On Windows they are in Settings › Time & language › Speech. On other systems the export of audio books is not supported yet. Play Sample uses the voice, the speed and the volume of the tool; it does not change the settings of Read aloud.

### The file

The audio book is a WAV file: uncompressed 16 bit audio, which every player can play. It is large – an hour of speech takes about 115 MB – and a WAV file can hold at most 4 GB; for a longer book choose fewer pages. Between two items there is a pause of 0.3 s, between two pages 0.6 s. Nothing is written until the audio is ready; Stop ends the run and keeps nothing.

The estimated length and size in the right panel come from the number of characters and the speed; the real length depends on the voice.

### Command line

PdfTool creates the same audio book:

```
pdftool audio-book-voices
pdftool audio-book --voice-lang-code en --voice-rate 0.2 --output book.wav input.pdf
pdftool audio-book --text-analysis-alg structure --say-struct-act-text --overwrite input.pdf
```

`--overwrite` replaces an existing file; without it an existing file is never replaced.
