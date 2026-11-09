# M4 Design Record

## 1. Threads, ownership and shared data

For the threads MainWindow owns the live Workspace accepted ProcessingCore, BuildController,
IpcServer and polling timer. Widgets, and workspace edits and processing queries running
on the GUI thread. Qt parents own widgets, the cores and strategies use unique_ptr.
 BuildController::start() copies the workspace and starts one  std::thread.
Its worker builds a separate core and publishes it through
 promise::set_value_at_thread_exit(). The GUI timer calls  poll(), checks readiness
without waiting, joins the finished worker and collects the result. Rejected cores
are destroyed and accepted cores move into MainWindow. The controller outlives the
accepted core's reference to its cancellation flag.
For the shared data: BuildController::cancellation_ : GUI writes, worker reads it is atomic.
Promise/future shared state: worker publishes once, GUI consumes once using standard-library synchronization. The future and thread handles are GUI-only.
IpcServer::stopping_ : GUI writes, receiver reads it is atomic.
IpcServer::{path_,on_add_,listening_fd_} : initialized before thread launch,then used by the receiver until join; it also closes/clears the descriptor.Restart and destruction occur after joining. Queued Qt invocations transferowning path copies to the GUI, which checks  shutting_down_ . Destruction stopsthe receiver before window members disappear.
Other mutable objects are confined to one thread thw live state/widgets to GUI,snapshot/unfinished core to build worker, socket buffers/client descriptor to receiver.

## 2. Current builds, failure and shutdown

Add and Reset increment  workspace_version_ ; Reset never restarts the counter.
 MainWindow::poll_build()  accepts only a successful, noncancelled result whose
version matches. Add/Reset invalidate outputs immediately and request cancellation.
 building_  prevents overlapping builds;  corpus_current_  gates processing.
Errors cross the future as text, display Failed and permit retry. Reset never
waits. Confirmed  closeEvent()  stops IPC, requests cancellation and keeps polling
until the build is collected, then closes. Destruction also cancels/joins.

## 3. Changes from the design note

Compared with the current  DESIGN_NOTE.md , worker code is a lambda inside
 start(), rather than a separate  run_build(). BuildChunker  adds cancellation
and delay without changing backend headers. Checks occur around each document
and in 10 ms delay slices, not inside the chunker's internal loop or final indexing.
Building the live searchable core would require synchronizing reads/writes;
using a separate core avoids exposing incomplete results.

## 4. CR1 impact and message format

 IpcServer::{start,serve,stop} ,  MainWindow::start_ipc_server ,
 add_document_file , CLI parsing and  tools/aiws_send.cpp  implement Remote Add.
Existing retrieval/widgets do not need socket logic. The receiver validates file
readability and queues GUI addition; its reply does not wait for GUI processing.
The file must remain available until the GUI processes the addition.

Each Unix stream connection carries one request: four bytes encoding unsigned
big-endian length L, then L UTF-8 absolute-path bytes. L is 1..1,048,576; NUL and
relative paths are rejected. Spaces/newlines are payload bytes. The sender resolves
relative FILE against its own directory. Reply is one ASCII byte:  0  (0x30) for
accepted or  1  (0x31) for rejected. Sender exit codes are 0/1; communication errors
return 2. Connect has a 2 s deadline; sending/reply share 5 s. Receiver accumulates
partial input with a 5 s deadline. Stop joins the receiver and removes its socket.

## 5. Fault switches and test evidence

Both switches are in  main_window.cpp.AIWS_FAULT_ACCEPT_STALE bypasses
Add/Reset cancellation and completion validation.The Add, Reset and Remote Add
stale-result tests catch it. AIWS_FAULT_LOSE_FAILURE  drops failure handling while
leaving Building and the build_failure_is_reported catches it. Tests/CMake do not name
the macros. In the tests all eleven normal test cases passed and both fault builds failed within 60 s.

## 6. AI-use disclosure

OpenAI Codex helped review requirements, integrate and edit
background-build/IPC handling, preserve backend compatibility, help with test creation.

## Pair programming log

Fill these fields from sessions that actually happened:

- Partner's name: Kendra Opong
- Session on my code: 11/8/2026, 7:00, est
- Work on my code: Added a test where one client sends an incomplete request and disconnects. A second client then sends a complete request. The test checks that the server still responds and that exactly one document appears.
- Roles and swap time on my code: I drove and Kendra navigated at 7:00. Kendra drove and I navigated at 7:15.
- Session on partner's code: 11/8/2026, 7:30, est
- Work on partner's code: Added a test in which aiws_send submits a document whose path contains spaces, a UTF‑8 character, and a newline. The test then verifies if the request succeeds and that the document is added to the workspace.
- Roles and swap time on partner's code: Kendra drove and I navigated at 7:30 and I drove and Kendra navigated at 7:45. 
- One contribution I made as navigator: I suggested validating both the sender's exit code as well as the final document count to confirm that the path was processed correctly. I also made sure that it followed E1 and E2 required behavior.
- One contribution my partner made as navigator: checked the document count as well as the sender’s exit code, so we could verify that only the complete request added a document
- What interaction worked best: I think me as the driver and kendra as the navigator worked best because we were able to develop a testcase and run the testcase on my section and we were able to write the test case in kendra's code as well. 