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

**NOTE:** The copy buffer is limited to 2K. The marker will adjust automatically to prevent exceeding this limit.

### Other commands

|Key|Description|
|---|-----------|
| `↑S` Save | Saves the current document |
| `↑F` Find | Searches for text starting at the cursor position and wraps around to the document's beginning.|
| `↑G` Goto | Moves the cursor directly to specific line number.| 
| `↑Q` Quit | Exits the editor. You will be prompted to save if the document has unsaved changes.|