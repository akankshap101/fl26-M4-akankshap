# M4 Design Note

Due Wed Oct 28, 11:59 pm. See specification Section 7.

## 1. Components and threads:

For the threads I plan to stick to having the MainWindow, widgets and workspace on the GUI thread. I will also create a new BuildController class which will manage a thread. The start(), poll() and cancel() functions will run on this GUI thread. The run_build() function will run on the worker thread and build a different ProcessingCore from a workspace copy. A QTimer ensures the future is checked without any blocking. The GUI thread will update the widgets and connections will use the member-function slots.

## 2. Corpus States:

For my states I will be using 5 main states: Empty, Dirty, Building, Current and Failed. When a document is added the corpus becomes Dirty and clears out the old output. When a build is started the code enters the Building state. A successful build becomes Current when the workspace hasn't changed; if it has changed the build is discarded. A failure displays an error and the code enters the Failed state. Reset clears all the data and the code enters the Empty state. The workspace version number is increased when a document is added or when Reset is pressed. Reset will not reset the version number to zero. Search, Term Inspector and Context will only use the corpus when it is Current. Query edits and any document selections will not change versions.

## 3. Risks and examples:

R1: A slower build could cause the window to freeze. To fix this the build work as well as the document delay will run on the worker thread. When a job is active I will disable Build Corpus while also keeping the other controls usable.

R2: An old build could finish after a workspace has been changed. If this happens I will compare the saved version of the workspace with the current version before accepting the finished corpus. I will only accept it if the versions match and the build was not cancelled.

R3: A worker could end up throwing an exception. This would cause an error to be returned through the future. When this happens the GUI will display the error, clear the job and allow another build.

R4: A Reset or Exit could occur during a build. If this happens I will request a cancellation using an atomic flag. The worker will check the cancellation flag during chunking loops and short delay intervals. After Exit is confirmed the GUI will still keep polling until the worker has finished, then it joins the thread and closes. Cancelled jobs will also be collected and their threads joined.

Walkthrough:

a. First a build starts with a version (4). The user then adds a document which makes the next version (5) and clears the old outputs. The build then finishes with version 4. I then discard the finished corpus and leave the corpus Dirty.

b. A build starts with a version (8). Reset then changes the version to 9 and a cancellation is requested. The worker ends up finishing before noticing that a cancellation was requested. But when checked it is found that the versions do not match so the workspace stays Empty.

## 4. Ownership:

The worker first creates a core in a std::unique_ptr and moves it through a promise/future using set_value_at_thread_exit(). The GUI then collects it and joins the finished thread. MainWindow then owns the accepted core until it is invalidated, replaced or the application shuts down. Any rejected cores are destroyed. Any data that is shared and mutable is limited to the atomic cancellation flag and the promise/future state. The GUI writes the cancellation flag and the worker reads it. The worker fills the promise/future state and the GUI checks and collects it. These standard-library mechanisms make these accesses safe. The workspace copy and core are not accessed by both threads at once.

## 5. Alternative:

One alternative I considered was rebuilding the same ProcessingCore used for searching. This would allow the GUI to read unfinished data and would require synchronization. Building a separate ProcessingCore is better because it makes it easier to keep old/incomplete data out of the GUI.