# ZED Text Editor

A tiny native text editor for the ZX Spectrum Next.

## Installing the editor
The editor is a DOT command, so the best way to install it is to copy the ZED binary file to the DOT folder in the root of your Next OS SD card.

## Starting the editor
Once installed you can launch the editor in one of two ways.

1. Launch without a file
Starts a new document and prompt for a file name when you either save or exit and choose to save the file.
```
.zed
```

2. Launch with a filename passed at the command line
If the file exists the file will be loaded into the editor, otherwise a new document will be created with the file name defaulted to the name passed at the command line.
```
.zed readme.txt
```

## Editing files
Once launched you can immediately start editing files. Use the cursor/arrow keys to move around as you would expect.

### Paging through large documents
In the status/info section at the bottom of the editor screen you will see highlighted `EXT` which defaults to `Off`. This is the extended keyboard mode. To enable extended keyboard mode, press the `EXTEND MODE` button on the Next/N-GO keyboard, `CAPS SHIFT + SYMBOL SHIFT` on a rubber key Speccy or press `Ctrl+Shift` on an external keyboard, which will toggle `EXT` to `On`. In extended mode, the Up/Down arrow keys will now move up and down one page at a time. To get out of extended mode, you can press the approriate `EXTEND MODE` key combination again.

### Switching to Command mode
Command mode provides access to the functions in the status/info section. Pressing `Edit` on the Next/N-GO keyboard, `CAPS SHIFT + 1` on the rubber key Speccy or the `Escape` key on an external keyboard will toggle command mode. In the status/info section, you will always see what mode pressing `Escape` will take you to, at startup you are in Edit mode, so the `ESC` indicator shows `Command` which is the mode you will move to when pressing `ESC`, once in command mode, the `ESC` indicator will show `Edit` as being the mode that you will switch back to.

Once in command mode, the remainder of the action keys, indicated by the `↑` prefix will be higlighted to show they are now available. To activate any one of the actions, press the `SYMBOL SHIFT` key together with the letter for the corresponding action you want to activate.

**NOTE** - you cannot edit the file while in command mode. The navigation is active while in command mode.

### Cut/Copy/Paste
The editor supports selecting sections of text and either copying or cutting the text. 

To mark/select a section of text to cut or copy, switch into command mode and press `SYMBOL SHIFT+M` (`↑M`) to set a marker/anchor for the selection. Once set, as you move the cursor the text between the marker and the cursor will be selected.

The marker can be cleared by pressing the hot key combination again.

Once selected, you can copy the select by pressing `↑C` to copy the selected text to the copy buffer. Once copied, you can then navigate to a new location in the file and prest `↑V` to paste the copied text.

Cutting text using `↑X` will copy the selected text to the copy buffer and remove it from the document. As with copy, you can then paste the text that was removed using the `↑V` paste feature.

**NOTE** the copy buffer is limited to 2K, the current implementation will move the marker to ensure that there is never more than 2K worth of text between the marker and the cursor. This approach might change, but for now this ensures that you will never cut text and then be left with only a subset of that in the copy buffer.

### Other commands

|Key|Description|
|---|-----------|
| `↑S` Save | Saves the current document, it will prompt for a filename defaulting to the current name if it has been set previously. This is an opportunity to change the filename (ie. Save As) or just press enter to stick with the existing file name.
| `↑F` Find | Search for text in the document. The search starts at the current cursor position and will wrap arround to the start of the document. 
| `↑G` Goto | Goto/Jump directly to a line number in the document. 
| `↑Q` Quit | Quit the editor. If the document has been modified, you will be prompted to confirm if you want to save the document before exiting the editor.
