# ZED Text Editor

A native text editor for the ZX Spectrum Next.

---

## Installing the editor
The editor is a DOT command, the recommended installation method is to copy the ZED binary file to the DOT folder located in the root of your Next OS SD card.

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

---

## Editing files

After launching, you can immediately start editing. Use the cursor/arrow keys for navigation as expected.

### Paging through large documents
The extended keyboard mode enables you to page through the document rather than moving up and down one line at a time.

* The status/info section at the bottom displays the `EXT` indicator, which defaults to Off.

* To enable extended keyboard mode:
  * Press `EXTEND MODE` on the Next/N-GO keyboard.
  * Press `CAPS SHIFT`+`SYMBOL SHIFT` on a rubber key Speccy.
  * Press `Ctrl`+`Shift` on an external keyboard.

* When `EXT` is set to `On`, the Up/Down arrow keys will move up or down one page at a time.

* To return to normal mode, press the appropriate `EXTEND MODE` key combination again.

### Switching to Command mode

Command mode gives access to the functions displayed in the status/info section.

* To toggle command mode:
  * Press `Edit` on the Next/N-GO keyboard.
  * Press `CAPS SHIFT`+`1` on a rubber key Speccy.
  * Press `Escape` on an external keyboard.

In the status/info section, you’ll see the current mode indicated. For example:

* In Edit mode, the `ESC` indicator shows `Edit`.
* In Command mode, the `ESC` indicator switches to `Command`.

Once in Command mode:

* Keys with the `↑` prefix will become active and highlighted.
* Activate functions by pressing the `SYMBOL SHIFT` key along with the letter of the desired action.

**NOTE:** Editing is disabled while in Command mode, but navigation remains active.

### Cut/Copy and Paste
The editor allows text selection for cutting or copying.

#### Marking/Selecting Text:
1. Switch to Command mode.
2. Press `SYMBOL SHIFT`+`M` (`↑M`) to set a marker/anchor.
3. Move the cursor to select text between the marker and the cursor position.
4. To clear the marker, press the `↑M` key combination again.

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