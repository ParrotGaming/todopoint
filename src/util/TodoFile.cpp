#include "TodoFile.h"

namespace todo {

namespace {

bool isSpaceTab(const char c) { return c == ' ' || c == '\t'; }

// Index of the first line, past a UTF-8 BOM when present.
size_t firstLineStart(const std::string& buf) {
  if (buf.size() >= 3 && static_cast<unsigned char>(buf[0]) == 0xEF && static_cast<unsigned char>(buf[1]) == 0xBB &&
      static_cast<unsigned char>(buf[2]) == 0xBF) {
    return 3;
  }
  return 0;
}

// Exclusive end of the line content starting at `start`, excluding a trailing
// CR. `next` is set past the LF terminator (or to size for the final line).
size_t lineEnd(const std::string& buf, const size_t start, size_t& next) {
  const size_t nl = buf.find('\n', start);
  if (nl == std::string::npos) {
    next = buf.size();
    return buf.size();
  }
  next = nl + 1;
  if (nl > start && buf[nl - 1] == '\r') return nl - 1;
  return nl;
}

// Matches the task grammar on [start, end) and fills the geometry fields.
// Mirrors ^[ \t]*-[ \t]*\[[ xX]\]([ \t]+.*)?$ without std::regex.
bool matchTask(const std::string& buf, const size_t start, const size_t end, Item& out) {
  size_t i = start;
  while (i < end && isSpaceTab(buf[i])) ++i;
  if (i >= end || buf[i] != '-') return false;
  ++i;
  while (i < end && isSpaceTab(buf[i])) ++i;
  if (i >= end || buf[i] != '[') return false;
  ++i;
  if (i >= end) return false;
  const char marker = buf[i];
  if (marker != ' ' && marker != 'x' && marker != 'X') return false;
  out.markOffset = static_cast<uint32_t>(i);
  out.done = marker != ' ';
  ++i;
  if (i >= end || buf[i] != ']') return false;
  ++i;

  out.textOffset = 0;
  out.textLen = 0;
  if (i < end) {
    // Any text after the checkbox must be separated by whitespace; `-[ ]text`
    // is ordinary text, not a task.
    if (!isSpaceTab(buf[i])) return false;
    while (i < end && isSpaceTab(buf[i])) ++i;
    if (i < end) {
      out.textOffset = static_cast<uint32_t>(i);
      out.textLen = static_cast<uint16_t>(end - i);
    }
  }
  return true;
}

// Cheap count of task lines, used to reserve the item vector exactly.
size_t countTasks(const std::string& buf) {
  size_t count = 0;
  for (size_t start = firstLineStart(buf); start < buf.size();) {
    size_t next = 0;
    const size_t end = lineEnd(buf, start, next);
    Item probe{};
    if (matchTask(buf, start, end, probe)) ++count;
    start = next;
  }
  return count;
}

// Keep a little headroom above the exact reserve, mirroring the read guard in
// HalStorage::readFileToString.
constexpr size_t kReserveMargin = 512;

// Geometry of one raw file line, used by the editor transforms.
struct LineSpan {
  size_t start = 0;       // first byte of the line
  size_t contentEnd = 0;  // first byte of the terminator (or size)
  size_t next = 0;        // past the terminator (or size)
  bool hasTerminator = false;
  bool crlf = false;
};

// Finds the `itemIndex`-th task line and reports its span and geometry.
bool findTaskLine(const std::string& buf, const size_t itemIndex, LineSpan& span, Item& item) {
  size_t ordinal = 0;
  for (size_t start = firstLineStart(buf); start < buf.size();) {
    size_t next = 0;
    const size_t contentEnd = lineEnd(buf, start, next);
    Item probe{};
    if (matchTask(buf, start, contentEnd, probe)) {
      if (ordinal == itemIndex) {
        span.start = start;
        span.contentEnd = contentEnd;
        span.next = next;
        span.hasTerminator = next > contentEnd;
        span.crlf = next >= start + 2 && buf[next - 1] == '\n' && buf[next - 2] == '\r';
        item = probe;
        return true;
      }
      ++ordinal;
    }
    start = next;
  }
  return false;
}

// Line-ending style to use for a new line: CRLF when the file uses it.
std::string detectEol(const std::string& buf) {
  const size_t nl = buf.find('\n');
  if (nl != std::string::npos && nl > 0 && buf[nl - 1] == '\r') return "\r\n";
  return "\n";
}

}  // namespace

void Document::clear() {
  std::string().swap(buffer);
  std::vector<Item>().swap(items);
}

LoadState classifyReadFailure(const size_t size) {
  if (size == 0) return LoadState::Empty;
  if (size > kMaxFileBytes) return LoadState::TooLarge;
  return LoadState::ReadError;
}

ParseResult parse(std::string&& buffer, Document& out, const size_t largestFreeBlock) {
  out.clear();
  out.buffer = std::move(buffer);

  const std::string& buf = out.buffer;
  const size_t pos = firstLineStart(buf);

  // Reserve the item vector exactly so the fill pass cannot reallocate. A
  // failed reserve aborts under -fno-exceptions, so refuse when the caller's
  // largest free block cannot hold the items; the item vector can be larger
  // than the file buffer.
  const size_t count = countTasks(buf);
  if (count > 0) {
    const size_t need = count * sizeof(Item);
    if (largestFreeBlock != SIZE_MAX && largestFreeBlock < need + kReserveMargin) {
      out.clear();
      return ParseResult::NoMemory;
    }
    out.items.reserve(count);
  }

  for (size_t start = pos; start < buf.size();) {
    size_t next = 0;
    const size_t end = lineEnd(buf, start, next);
    Item item{};
    if (matchTask(buf, start, end, item)) {
      // NUL-terminate the task text in place. `end` is the CR, the LF, or (last
      // line) the string's own terminator, which must not be overwritten.
      if (item.textLen > 0 && end < out.buffer.size()) out.buffer[end] = '\0';
      out.items.push_back(item);
    }
    start = next;
  }

  return out.items.empty() ? ParseResult::Empty : ParseResult::Ok;
}

void setDone(Document& doc, const size_t index, const bool done) {
  if (index >= doc.items.size()) return;
  Item& item = doc.items[index];
  item.done = done;
  if (item.markOffset < doc.buffer.size()) doc.buffer[item.markOffset] = done ? 'X' : ' ';
}

bool insertTaskAfter(std::string& file, const size_t itemIndex, const std::string& text) {
  const std::string newTask = text.empty() ? std::string("- [ ]") : "- [ ] " + text;
  LineSpan span;
  Item item{};
  if (!findTaskLine(file, itemIndex, span, item)) {
    // No such task: append. An empty file gets a fresh first task.
    std::string addition;
    if (file.empty()) {
      addition = newTask + "\n";
    } else if (file.back() == '\n') {
      addition = newTask + detectEol(file);
    } else {
      addition = detectEol(file) + newTask;
    }
    if (file.size() + addition.size() > kMaxFileBytes) return false;
    file += addition;
    return true;
  }
  // A last line with no terminator has no CR to inspect, so fall back to the
  // file's overall style. Keep the no-trailing-newline style by leaving the new
  // line unterminated.
  const std::string eol = span.hasTerminator ? (span.crlf ? "\r\n" : "\n") : detectEol(file);
  const std::string addition = span.hasTerminator ? newTask + eol : eol + newTask;
  if (file.size() + addition.size() > kMaxFileBytes) return false;
  file.insert(span.next, addition);
  return true;
}

bool renameTask(std::string& file, const size_t itemIndex, const std::string& text) {
  LineSpan span;
  Item item{};
  if (!findTaskLine(file, itemIndex, span, item)) return false;
  const size_t afterBracket = item.markOffset + 2;  // marker byte, then ']'
  std::string newLine = file.substr(span.start, afterBracket - span.start);
  if (!text.empty()) {
    // Preserve the author's separator (including trailing whitespace on a
    // textless task); add one when the line had none so the result is valid.
    std::string separator;
    if (item.textLen > 0) {
      separator = file.substr(afterBracket, item.textOffset - afterBracket);
    } else if (span.contentEnd > afterBracket) {
      separator = file.substr(afterBracket, span.contentEnd - afterBracket);
    } else {
      separator = " ";
    }
    newLine += separator;
    newLine += text;
  }
  newLine += file.substr(span.contentEnd, span.next - span.contentEnd);
  const size_t oldLen = span.next - span.start;
  if (file.size() - oldLen + newLine.size() > kMaxFileBytes) return false;
  file.replace(span.start, oldLen, newLine);
  return true;
}

bool deleteTask(std::string& file, const size_t itemIndex) {
  LineSpan span;
  Item item{};
  if (!findTaskLine(file, itemIndex, span, item)) return false;
  file.erase(span.start, span.next - span.start);
  return true;
}

}  // namespace todo
