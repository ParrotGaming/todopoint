#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Parser and raw-file editor transforms for the on-device to-do list
// (`/todo.md`). No HAL dependency, so it stays unit-testable on the host. See
// USER_GUIDE.md for the file-format contract.
namespace todo {

// Whole-file cap. The activity checks it before parsing; a larger file is
// reported as too-large, not a read error.
constexpr size_t kMaxFileBytes = 16 * 1024;

// One task line. Offsets are absolute byte positions into Document::buffer.
struct Item {
  uint32_t markOffset;  // absolute offset of the space/X inside the brackets
  uint32_t textOffset;  // absolute offset of the first text byte, or 0
  uint16_t textLen;     // bytes after the checkbox, without CR/LF
  bool done;
};

struct Document {
  std::string buffer;  // whole file, with task text NUL-terminated in place
  std::vector<Item> items;

  // Releases the buffer and item storage.
  void clear();
};

enum class ParseResult { Ok, Empty, NoMemory };

// Why the activity is not showing a task list. Missing is decided before any
// read; the rest classify a failed whole-file read.
enum class LoadState { Ok, Missing, Empty, TooLarge, ReadError };

// Classifies a failed whole-file read from the probed file size: a zero-byte
// file is Empty, one above the cap is TooLarge, anything else is a read error.
LoadState classifyReadFailure(size_t size);

// `buffer` is moved in. Returns Empty for a valid file with no tasks.
// Call with a local buffer, never with `out.buffer` itself (self-move).
// `largestFreeBlock` lets the caller pass the heap's largest contiguous block
// so parse can refuse an item reserve it cannot satisfy: std::vector::reserve
// aborts on a failed allocation under -fno-exceptions, and the item vector can
// be larger than the file buffer (worst case ~2.7k items * 12 B). The default
// skips the check (host tests). On NoMemory `out` is left empty.
ParseResult parse(std::string&& buffer, Document& out, size_t largestFreeBlock = SIZE_MAX);

// Patch the in-memory marker only; caller writes the byte to SD separately.
void setDone(Document& doc, size_t index, bool done);

// Raw-file edits for the on-device editor. Unlike Document::buffer, `file` is
// the file's bytes with no NULs inserted. Each returns false when the target
// task is not found or the edit would push the file past kMaxFileBytes. Every
// byte outside the edited line(s) is preserved, including line endings.

// Insert `text` as a new unchecked task on the line after task `itemIndex`.
// An out-of-range index appends at end of file (the first task in an empty
// file). The new line matches the surrounding line-ending style.
bool insertTaskAfter(std::string& file, size_t itemIndex, const std::string& text);

// Replace the text after the checkbox of task `itemIndex`, preserving the
// indentation, bullet, checkbox state, and separator. An empty `text` leaves a
// textless task.
bool renameTask(std::string& file, size_t itemIndex, const std::string& text);

// Remove the whole line of task `itemIndex`.
bool deleteTask(std::string& file, size_t itemIndex);

}  // namespace todo
