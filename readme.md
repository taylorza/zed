# ZED Text Editor

A native text editor for the ZX Spectrum Next.

---

## Installing the editor
The editor is a DOT command, the recommended installation method is to copy the ZED binary file to the DOT folder located in the root of your Next OS SD card.

The latest binary can be downloaded from [itch.io](https://taylorza.itch.io/zed)

---

## Starting the editor
Once installed you can launch the editor in one of two ways.

### 1. Launch without a file
Starts a new document and prompt for a file name when you either save or exit and choose to save the file.
```bash
.zed
```

### 2. Launch with a filename passed at the Command Line

If the specified file exists, it will be loaded into the editor. Otherwise, a new document will be created with the filename prefilled.

```bash
.zed readme.txt
```

To open a file with the cursor positioned at a specific line and column, use the `+` prefix when specifying the location.
For example, to start the editor on line 16, run:

```bash
.zed +16 readme.txt
```

You can also specify a column within the line:

```bash
.zed +16,7 readme.txt
```

---

## Editing files

After launching, you can immediately start editing. Use the cursor/arrow keys for navigation as expected.

### Editor keys

Standard navigation

|Key|Action|
|---|------|
|`⇦`|Move left one character|
|`⇨`|Move right one character|
|`⇧`|Move up one line|
|`⇩`|Move down one line|

Extended navigation

|Key|Action|
|---|------|
|`SYMBOL SHIFT` + `⇦`|Move left one word|
|`SYMBOL SHIFT` + `⇨`|Move right one word|
|`SYMBOL SHIFT` + `⇧`|Page up|
|`SYMBOL SHIFT` + `⇩`|Page down|

* Note: Due to the way the keyboard works, you can also press `EXTEND MODE` and the arrow keys to achieve the above actions.

### Cut/Copy and Paste
The editor allows text selection for cutting or copying.

#### Marking/Selecting Text:
1. Press `EXTEND MODE`+`M` (`↑M`) to set a marker/anchor.
2. Move the cursor to select text between the marker and the cursor position.
3. To clear the marker, press the `↑M` key combination again.

#### Copying Text:
* Once text is selected, press `↑C` to copy it to the buffer.
* Navigate to a new location and press `↑V` to paste.

#### Cutting Text:
* Press `↑X` to cut (copy to buffer and remove from the document).
* Navigate to a new location and press `↑V` to paste.

#### Cutting Lines:
* Press `↑K` to cut the current line (copy to buffer and remove from the document).
* Navigate to a new location and press `↑V` to paste.

**NOTE:** The copy buffer is limited to 8K. The start/anchor will adjust automatically to prevent exceeding this limit.

### Other commands

|Key|Description|
|---|-----------|
| `↑S` Save | Saves the current document |
| `↑F` Find | Searches for text starting at the cursor position and wraps around to the document's beginning.|
| `↑G` Goto | Moves the cursor directly to specific line number.| 
| `↑Q` Quit | Exits the editor. You will be prompted to save if the document has unsaved changes.|
| `↑EDIT` | Toggle insert/overwrite mode.|
| `↑0` | Enter codepoint input mode to insert characters by their hexadecimal codepoint (20-ff).|
| `GRAPH` | Toggle graphics character mode.|
| `INV VIDEO` | Toggle inverse video mode for graphics entry.|
| `↑2` | Save file and reload settings.|

### Codepoint Input Mode
When in codepoint input mode, indicated by the caret changing to a different pattern, you can enter characters by their hexadecimal codepoints.
1. Press `↑0` to enter codepoint input mode. 
2. Type two hexadecimal digits (0-9, A-F) to specify the character codepoint.
3. The character will be inserted at the cursor position.
4. Press `ESC` to exit codepoint input mode without inserting a character.

## Settings
The editor settings are stored in a configuration file named `zed.cfg` located in C:/SYS/ directory on your Next OS SD card.
You can edit this file directly to change settings such as caret blink rate, key repeat delay, and beep sound, color scheme etc.

All available settings:

```
background=<number>         # Background color (default 0x02)
foreground=<number>         # Foreground color (default 0xfc)
highlight=<number>          # Highlight Background color (default 0x0b)

caret_default=<number>      # Caret default color (0-255) (default 0xcf)
caret_caps=<number>         # Caret color when caps lock is on (0-255) (default 0xff)
caret_graphics=<number>     # Caret color in graphics mode (0-255) (default 0x1c)

repeat_delay=<number>       # Key repeat delay (1-255) higher is longer delay (default 0x14)
repeat_rate=<number>        # Key repeat rate (1-255) higher is slower repeat (default 0x02)
blink_rate=<number>         # Caret blink rate (1-255) higher is slower (default 0x0f)
key_beep_cycles=<number>    # Keyboard beep sound (0-off) (default 0x00)
key_beep_period=<number>    # Keyboard beep half period per cycle (1-65535) (default 0x8c)

font=<filename>             ; Font file to load (Default is built-in font)
```

Colors are 3-3-2 bit RGB format 
All numbers can be entered as decimal values, hex values (0x00-0xFF) or binary values (0b00000000-0b11111111).
Any setting not specified in the config file will use the default value.
