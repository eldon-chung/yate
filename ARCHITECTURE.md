# Architecture

This document details some of the overall organisation of the program, explains some of the design choices. 
The text editor program is broken up into (roughly) 3 major components:
1. The [text buffer](https://github.com/eldon-chung/yate/blob/master/text_buffer.h), which defines how the text is represented for efficient(-ish) access and manipulation.
2. The [Program component](https://github.com/eldon-chung/yate/blob/master/Program.h), which defines the overall control flow of the program.
3. The [view component](https://github.com/eldon-chung/yate/blob/master/view.h), which wraps around the [notcurses library](https://notcurses.com/), and has other logic on how rendering should be done.

Before we go into details about each component, perhaps showing the general control flow of what happens during a keypress would help to illustrate how each component interacts.
![overall-keypress-control-flow](https://github.com/eldon-chung/yate/assets/18584068/b61ca61b-5de9-419f-a8c2-90b9e19e5b65)

So after the initial setup, the main loop `run_event_loop()` located in [Program.h](https://github.com/eldon-chung/yate/blob/master/Program.h) gets an event from the event_queue (which may contain other events). 
https://github.com/eldon-chung/yate/blob/25bb6693e47ef26835bfef3b95b7b7376a5886a2/Program.h#L1327-L1336
The event_queue, among other things, will call upon notcurses directly to obtain a keypress event. https://github.com/eldon-chung/yate/blob/25bb6693e47ef26835bfef3b95b7b7376a5886a2/EventQueue.h#L82-L95
Then, the main loop will use dynamic dispatch to get a TextState to handle the keypress (we'll explain why there is dynamic dispatch later on, for now just take it that
the active state of the stack is a `TextState`). https://github.com/eldon-chung/yate/blob/25bb6693e47ef26835bfef3b95b7b7376a5886a2/Program.h#L1345 
`TextState`'s method called `handle_input` at contains essentially all the logic needed to manipulate the text cursor and text_buffer as needed.
https://github.com/eldon-chung/yate/blob/25bb6693e47ef26835bfef3b95b7b7376a5886a2/Program.h#L720  

After that's done, assuming `text_state` is still the active state in the next iteration of `run_event_loop()`, `trigger_render()` is called. You can see it as Line 1333 on one of the previous code snippets.
That method defines what exactly `text_state` how to use `View`'s methods to update the screen. This this case, there are a few things it needs to do. https://github.com/eldon-chung/yate/blob/25bb6693e47ef26835bfef3b95b7b7376a5886a2/Program.h#L697-L718
It needs to tell view to focus on the text plane (this means showing the cursor) then telling it to render the text (it does so by calling the `render()` method on the `TextPlane` object that it has a pointer to). 
Finally, we want to update the "status bar" with the position of our cursor, and whether we're in parser mode, so the last lines prepare some status text before sending that off to View for rendering.

Now we're in `View`, or rather, `TextPlane`, in particular. Its render method is basically:
https://github.com/eldon-chung/yate/blob/25bb6693e47ef26835bfef3b95b7b7376a5886a2/view.h#L377-L386
Each line of which has logic defined on how exactly it needs to access information (such as relevant portions of text, or cursor position), does other logic like computing which are the relevant line numbers to show, 
before calling the notcurses library.

After returning back up the call chain, we'll be back in the main event loop and ready for another keypress!


## The TextBuffer
The [text buffer](https://github.com/eldon-chung/yate/blob/master/text_buffer.h) is essentially a data structure that stores text, that allows for various methods of text insertion, deletion, and lookup by lines. 
It also defines a parser callback function for the treesitter library to call when we need to re-parse the text on every update.
https://github.com/eldon-chung/yate/blob/25bb6693e47ef26835bfef3b95b7b7376a5886a2/text_buffer.h#L864-L866
Side note: It's not exactly the most efficient data structure right now. But that might change in the future. A [piece tree](https://code.visualstudio.com/blogs/2018/03/23/text-buffer-reimplementation#_piece-tree)
would be interesting to implement as well. But my biggest concern was getting everything else up and working (and properly designed in the first place).

## The View
The [View](https://github.com/eldon-chung/yate/blob/master/View.h) essentially contains methods to either create new view elements (text_planes) and more importantly, 
stores all the view elements themselves, such as the `TextPlane`s and `CommandPromptPlane`s and manages them as a resource (creates them and destroys them using the notcurses library) as needed.
As of right now, `View` in and of itself does not really do much besides that. But in the future, when we want to support multi-pane text viewing, `View` will probably play a much bigger role. Perhaps
more interestingly right now, are its two sub-components: `TextPlane` and `BottomPane`. 

### TextPlane
[`TextPlane`](https://github.com/eldon-chung/yate/blob/25bb6693e47ef26835bfef3b95b7b7376a5886a2/view.h#L321) exposes a bunch of methods for a `TextState` to call to render its text on the corresponding notcurses plane `nc_plane` that it owns.
It obtains all the data about the text state it needs to render through [`TextPlaneModel`](https://github.com/eldon-chung/yate/blob/25bb6693e47ef26835bfef3b95b7b7376a5886a2/view.h#L263-L267).
https://github.com/eldon-chung/yate/blob/25bb6693e47ef26835bfef3b95b7b7376a5886a2/view.h#L263-L267

### BottomPane
One thing we haven't talked about is what happens when the user is prompted to enter the name of a file they wish to open, for example. The bottom of the screen needs to show what the user has input, and the position of the cursor.
It accesses the state of the command buffer through (similarly) the [`BottomPlaneModel`](https://github.com/eldon-chung/yate/blob/25bb6693e47ef26835bfef3b95b7b7376a5886a2/view.h#L231-L235).

In it also holds a pointer to the notcurses plane for statuses, which others can access to place whatever they want, via [`render_status`](https://github.com/eldon-chung/yate/blob/25bb6693e47ef26835bfef3b95b7b7376a5886a2/view.h#L847)
https://github.com/eldon-chung/yate/blob/25bb6693e47ef26835bfef3b95b7b7376a5886a2/view.h#L847

## The ProgramStates
This one is probably the most involved portion. [Program.h](https://github.com/eldon-chung/yate/blob/master/Program.h) itself contains a few classes. 
Namely: [`TextState`](https://github.com/eldon-chung/yate/blob/25bb6693e47ef26835bfef3b95b7b7376a5886a2/Program.h#L629), 
[`PromptState`](https://github.com/eldon-chung/yate/blob/25bb6693e47ef26835bfef3b95b7b7376a5886a2/Program.h#L196),
[`FileSaverState`](https://github.com/eldon-chung/yate/blob/25bb6693e47ef26835bfef3b95b7b7376a5886a2/Program.h#L336),
[`FileOpenerState`](https://github.com/eldon-chung/yate/blob/25bb6693e47ef26835bfef3b95b7b7376a5886a2/Program.h#L504). 
Each of these classes all inherit from a base class [`ProgramState`](https://github.com/eldon-chung/yate/blob/25bb6693e47ef26835bfef3b95b7b7376a5886a2/Program.h#L30).
The base class (and therefore by extension, all the other inheriting classes) expose methods for how they handle messages, and keypresses. They also expose methods that define anything that needs to be done
upon starting entering them as a state for the first time (via a `setup()` method), and also what cleanup should be done after we exit the state (via an `exit()` method).
Lastly, it also defines a [`StateStack`](https://github.com/eldon-chung/yate/blob/25bb6693e47ef26835bfef3b95b7b7376a5886a2/Program.h#L1256) that exposes some methods for maintaining the states of the program.


Nota Bene: The reason we are using this is mostly to keep program control flow (in particular, how key presses should be handled) defined together with each contexts (for example, text editing vs prompting the user for a file name).
So this way, each `ProgramState` is free to define how key presses (and events) should be handled (again, via the `handle_key_press()` and `handle_message()` methods). And we can use dynamic dispatch to conceptually do a `match` or a `switch` 
on the program state first before doing another `match` or `switch` on the keypress or event to decide what happens next. Each `ProgramState` must also tell the main event loop what sort of transitions need to happen. 
E.g. we were in `TextState` but the user has pressed the keybind for saving files. Then we need to push an instance of `FileSaverState` onto `StateStack`. And that's what you see here:
https://github.com/eldon-chung/yate/blob/25bb6693e47ef26835bfef3b95b7b7376a5886a2/Program.h#L1345-L1355
If you're curious about this, there's a [chapter by Bob Nystrom in his book Game Programming Patterns on this topic](https://gameprogrammingpatterns.com/state.html).


