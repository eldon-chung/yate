# Yet Another Text Editor 

This is yet another text editor that I've been working on. 

Comes flush with basic cut/copy/paste operations, file opening/saving, and syntax highlighting for C++ (powered by the [tree-sitter library](https://tree-sitter.github.io/tree-sitter/), and the [tree-sitter C++ parser](https://github.com/tree-sitter/tree-sitter-cpp)). 

https://github.com/eldon-chung/yate/assets/18584068/5737ec73-5495-4fd6-8209-bde7973a908a

I plan to include syntax highlighting for other languages (since they're already supported) when I come around to doing that.

## Some keybinds:
  * Quit: `ctrl + W` 
  * Save: `ctrl + O` 
  * Read: `ctrl + R` 
  * Quit Prompt: `ctrl + Q` 
  * Quit: `ctrl + W` 
  * Cut: `ctrl + X`  
  * Copy: `ctrl + C`  
  * Paste: `ctrl + G` (`ctrl + V` has issues for now)  
  * Parse: `ctrl + P` (invokes the C++ parser) 

## Code Structure Rough Overview
You can find an exposition on roughly how the code is structured, and some details into each component here: [ARCHITECTURE.md](ARCHITECTURE.md).

## To build the project:
You'll need to have `make` and the [`libtree-sitter`](https://tree-sitter.github.io/tree-sitter/) package installed. I'm using version 0.20.3-1 on Ubuntu for my builds. The `Makefile` should take care of the rest. 
Run `make yate` (or just `make`) to build the executable as `yate`. There's also `make debug` which builds it with `-g` for running it with stuff like `gdb`.

You'll also need the [`notcurses`](https://github.com/dankamongmen/notcurses) package installed.  

## Planned Features:
* Lots and lots of nice cursor movement and basic editing keybinds are missing for now. Including
  1. ~~`ctrl + left/right` should move the cursor by word boundaries rather than single characters.~~ Done!
     1. ~~`ctrl + shift + left/right` should do the same thing but with selections.~~ Also done!
  3. `ctrl + up/down` should scroll up or down by one line.
  4. `ctrl + Enter` should add a newline to the current line, and move the cursor down to that line.
  5. `ctrl + shift + Enter` should add a newline to the previous line, and move the cursor up to that line.
  6. ~~`alt + up/down` should swap the current line with the one above/below it.~~ Done!
    
* Keybinds for manipulating text like cutting/copying/pasting entire lines when the cursor is not in selection mode.

* Search (both normal and regular expression) is missing. Will implement those soon. 
* Undo/Redo
* Multicursor
* Multiple text panes
* Configurable syntax highlighting and colour theming for the editor itself
* Text editing over SSH
* LSP support

