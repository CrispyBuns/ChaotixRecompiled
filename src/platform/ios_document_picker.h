#pragma once
// iOS only: the system's document picker (the Files app's browser), which
// stands in on the setup page for the file dialog SDL 3 lacks there.
#include <string>

struct SDL_Window;

namespace chaotix {

// Shows the picker over the window and waits until a file is picked or the
// picker is cancelled. The file is copied into the app's temporary folder, so
// the path returned can be read like any other; empty when cancelled.
// start_dir is where the picker opens (the app's Documents), or empty.
std::string ios_pick_document(SDL_Window* window, const std::string& start_dir);

} // namespace chaotix
