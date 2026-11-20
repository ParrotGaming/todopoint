#include "TodoActivity.h"

#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <esp_heap_caps.h>

#include <utility>

#include "activities/util/ConfirmationActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;

namespace {
constexpr const char* kTodoPath = "/todo.md";
// Staging file for the whole-file rewrite; swapped in with Storage.replaceFile.
constexpr const char* kTodoTmpPath = "/todo.md.tmp";
// Upper bound on one task's text. Keeps the keyboard and the edit transforms
// well under the whole-file cap; existing longer tasks can be shortened.
constexpr size_t kMaxTaskChars = 1024;
// Confirm held this long opens the row action menu.
constexpr unsigned long kMenuLongPressMs = 600;
}  // namespace

void TodoActivity::onEnter() {
  UiListActivity::onEnter();
  doc.clear();
  loadState = todo::LoadState::Ok;

  if (Storage.exists(kTodoPath)) {
    // A staging file left beside a valid list is stale.
    Storage.remove(kTodoTmpPath);
  } else if (Storage.exists(kTodoTmpPath)) {
    // Recover a staging file left by an interrupted swap (power loss between
    // remove and rename) rather than losing the edit.
    if (!Storage.rename(kTodoTmpPath, kTodoPath)) {
      LOG_ERR("TODO", "could not recover %s", kTodoTmpPath);
    }
  }
  if (!Storage.exists(kTodoPath)) {
    // Create an empty file so the user can start adding tasks on-device.
    if (!writeRaw(std::string())) {
      LOG_ERR("TODO", "could not create %s", kTodoPath);
      loadState = todo::LoadState::Missing;
      requestUpdate();
      return;
    }
  }

  std::string raw;
  if (Storage.readFileToString("TODO", kTodoPath, todo::kMaxFileBytes, raw)) {
    // Never pass doc.buffer itself: parse() moves its argument into out.buffer.
    // The largest free block lets parse refuse an item reserve that would
    // abort() under -fno-exceptions; the item vector can be larger than the
    // 16 KB file buffer.
    const size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    if (todo::parse(std::move(raw), doc, largest) == todo::ParseResult::NoMemory) {
      LOG_ERR("TODO", "not enough heap for /todo.md task items");
      loadState = todo::LoadState::ReadError;
    }
  } else {
    // readFileToString returns a single false for missing, empty, oversized,
    // directory, and short-read files; probe the size to tell them apart. A
    // zero-byte file classifies as Empty (a valid empty list).
    HalFile file = Storage.open(kTodoPath, O_RDONLY);
    if (file) {
      loadState = file.isDirectory() ? todo::LoadState::ReadError : todo::classifyReadFailure(file.fileSize());
      file.close();
    } else {
      loadState = todo::LoadState::ReadError;
    }
  }
  requestUpdate();
}

void TodoActivity::onExit() {
  optionPopup.dismiss();
  doc.clear();
  UiListActivity::onExit();
}

int TodoActivity::listCount() const { return static_cast<int>(doc.items.size()); }

void TodoActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  screen.setContentMarginFromScreen(fui::Insets{
      static_cast<int16_t>(safe.y + metrics.topPadding + metrics.headerHeight),
      static_cast<int16_t>(renderer.getScreenWidth() - (safe.x + safe.width)),
      static_cast<int16_t>(renderer.getScreenHeight() - (safe.y + safe.height)), static_cast<int16_t>(safe.x)});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  switch (loadState) {
    case todo::LoadState::Missing:
      screen.centeredText(tr(STR_TODO_MISSING), screen.theme().bodyText);
      return;
    case todo::LoadState::TooLarge:
      screen.centeredText(tr(STR_TODO_TOO_LARGE), screen.theme().bodyText);
      return;
    case todo::LoadState::ReadError:
      screen.centeredText(tr(STR_TODO_READ_FAILED), screen.theme().bodyText);
      return;
    case todo::LoadState::Ok:
    case todo::LoadState::Empty:
      break;
  }
  if (doc.items.empty()) {
    screen.centeredText(tr(STR_TODO_EMPTY), screen.theme().bodyText);
    return;
  }

  fui::ListProps props;
  props.rowProvider = &TodoActivity::provideRow;
  props.rowProviderCtx = this;
  // Required even with a rowProvider: the provider supplies rows, not the size.
  props.count = static_cast<uint16_t>(listCount());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch | fui::InputLongPress;  // long press opens the menu
  props.labelText = screen.theme().bodyText;
  props.labelText.maxLines = 16;  // default is 1; wraps grow the row
  // Without this a wrapped label with a trailing checkbox is capped to 3/5 of
  // the content width, wasting the space beside the checkbox.
  props.balanceWrappedLabelWithValue = false;
  syncListViewport(screen, props);  // sets nav/selection/viewport and 28px toggles
  screen.list(props);
}

void TodoActivity::provideRow(void* ctx, const uint16_t index, fui::ListItem& item) {
  auto* self = static_cast<TodoActivity*>(ctx);
  if (index >= self->doc.items.size()) return;
  const auto& task = self->doc.items[index];
  // A textless task (`- [ ]`) stores textOffset == 0; render it as empty rather
  // than pointing at the start of the file.
  item.label = task.textOffset ? self->doc.buffer.data() + task.textOffset : "";
  item.toggle = true;
  item.toggleChecked = task.done;
  item.actionValue = static_cast<int16_t>(index);
}

void TodoActivity::activateIndex(const int index) {
  if (index < 0 || index >= static_cast<int>(doc.items.size())) return;
  const bool wasDone = doc.items[index].done;
  if (writeMark(doc.items[index].markOffset, wasDone)) {
    todo::setDone(doc, static_cast<size_t>(index), !wasDone);
    requestUpdate();
  } else {
    LOG_ERR("TODO", "toggle failed at offset %u", static_cast<unsigned>(doc.items[index].markOffset));
  }
}

bool TodoActivity::writeMark(const uint32_t offset, const bool wasDone) {
  HalFile file = Storage.open(kTodoPath, O_RDWR);
  if (!file) {
    LOG_ERR("TODO", "open for write failed");
    return false;
  }
  if (!file.seek(offset)) {
    LOG_ERR("TODO", "seek failed at %u", static_cast<unsigned>(offset));
    return false;
  }
  // Re-read the marker and abort if the file changed since load: a stale offset
  // must not corrupt a different task's checkbox. The byte must be a marker and
  // its checked state must still match what the parser recorded.
  const int current = file.read();
  const bool currentDone = current == 'x' || current == 'X';
  if ((current != ' ' && !currentDone) || currentDone != wasDone) {
    LOG_ERR("TODO", "marker changed at %u (got %d)", static_cast<unsigned>(offset), current);
    return false;
  }
  if (!file.seek(offset)) {
    LOG_ERR("TODO", "seek back failed at %u", static_cast<unsigned>(offset));
    return false;
  }
  const uint8_t byte = wasDone ? ' ' : 'X';
  if (file.write(&byte, 1) != 1) {
    LOG_ERR("TODO", "write failed at %u", static_cast<unsigned>(offset));
    return false;
  }
  file.flush();
  if (!file.close()) {
    LOG_ERR("TODO", "close failed at %u", static_cast<unsigned>(offset));
    return false;
  }
  return true;
}

void TodoActivity::onRowLongPress(const int index) {
  nav.selected = index;
  showActions();
}

bool TodoActivity::handleCustomInput() {
  if (optionPopup.handleInput(mappedInput, [this] { requestUpdate(); })) return true;

  // Empty list: there is no row to long-press, so Confirm (or a tap) adds the
  // first task directly.
  if (listAvailable() && doc.items.empty()) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      startAdd(-1);
      return true;
    }
    int x = 0;
    int y = 0;
    if (mappedInput.wasScreenTapped(x, y)) {
      startAdd(-1);
      return true;
    }
  }
  return false;
}

bool TodoActivity::handleButtons() {
  if (mappedInput.wasLongPressed(MappedInputManager::Button::Confirm, kMenuLongPressMs)) {
    app.clearTapFlash();
    showActions();
    return true;
  }
  return UiListActivity::handleButtons();
}

void TodoActivity::render(RenderLock&& lock) {
  if (optionPopup.processRender(renderer, mappedInput)) return;
  UiListActivity::render(std::move(lock));
}

void TodoActivity::showActions() {
  if (optionPopup.isActive() || !listAvailable()) return;
  if (doc.items.empty()) {
    startAdd(-1);
    return;
  }
  const int index = nav.selected;
  if (index < 0 || index >= static_cast<int>(doc.items.size())) return;
  const auto& task = doc.items[index];
  const char* headline = task.textOffset ? doc.buffer.data() + task.textOffset : "";

  const char* options[] = {tr(STR_TODO_ADD), tr(STR_TODO_RENAME), tr(STR_TODO_DELETE)};
  RenderLock lock(*this);
  optionPopup.show("", headline, options, 3, 0, [this, index](const int choice) {
    if (choice == 0) {
      startAdd(index);
    } else if (choice == 1) {
      startRename(index);
    } else if (choice == 2) {
      startDelete(index);
    }
  });
  lock.unlock();
  requestUpdate();
}

void TodoActivity::startAdd(const int afterIndex) {
  auto keyboard = makeUniqueNoThrow<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_TODO_NEW_TASK), std::string(),
                                                           kMaxTaskChars, InputType::Text);
  if (!keyboard) {
    LOG_ERR("TODO", "OOM: add keyboard");
    return;
  }
  startActivityForResult(std::move(keyboard), [this, afterIndex](const ActivityResult& result) {
    if (result.isCancelled) return;
    applyInsert(afterIndex, std::get<KeyboardResult>(result.data).text);
  });
}

void TodoActivity::startRename(const int index) {
  if (index < 0 || index >= static_cast<int>(doc.items.size())) return;
  const auto& task = doc.items[index];
  const std::string initial =
      task.textOffset ? std::string(doc.buffer.data() + task.textOffset, task.textLen) : std::string();
  auto keyboard = makeUniqueNoThrow<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_TODO_RENAME), initial,
                                                           kMaxTaskChars, InputType::Text);
  if (!keyboard) {
    LOG_ERR("TODO", "OOM: rename keyboard");
    return;
  }
  startActivityForResult(std::move(keyboard), [this, index](const ActivityResult& result) {
    if (result.isCancelled) return;
    applyRename(index, std::get<KeyboardResult>(result.data).text);
  });
}

void TodoActivity::startDelete(const int index) {
  if (index < 0 || index >= static_cast<int>(doc.items.size())) return;
  const auto& task = doc.items[index];
  const std::string body =
      task.textOffset ? std::string(doc.buffer.data() + task.textOffset, task.textLen) : std::string();
  auto confirmation = makeUniqueNoThrow<ConfirmationActivity>(renderer, mappedInput, tr(STR_TODO_DELETE_CONFIRM), body);
  if (!confirmation) {
    LOG_ERR("TODO", "OOM: delete confirmation");
    return;
  }
  startActivityForResult(std::move(confirmation), [this, index](const ActivityResult& result) {
    if (result.isCancelled) return;
    applyDelete(index);
  });
}

bool TodoActivity::applyInsert(const int afterIndex, const std::string& text) {
  std::string raw;
  if (!readRaw(raw)) {
    LOG_ERR("TODO", "read before add failed");
    return false;
  }
  if (!hasHeapForEdit(raw.size(), text.size())) {
    LOG_ERR("TODO", "not enough heap to add");
    return false;
  }
  const size_t index = afterIndex < 0 ? SIZE_MAX : static_cast<size_t>(afterIndex);
  if (!todo::insertTaskAfter(raw, index, text)) {
    LOG_ERR("TODO", "add failed (cap or stale list)");
    resyncDocument();
    return false;
  }
  if (!writeRaw(raw)) {
    LOG_ERR("TODO", "write after add failed");
    resyncDocument();
    return false;
  }
  const int select = afterIndex < 0 ? static_cast<int>(doc.items.size()) : afterIndex + 1;
  reloadAfterEdit(std::move(raw), select);
  return true;
}

bool TodoActivity::applyRename(const int index, const std::string& text) {
  std::string raw;
  if (!readRaw(raw)) {
    LOG_ERR("TODO", "read before rename failed");
    return false;
  }
  if (!hasHeapForEdit(raw.size(), text.size())) {
    LOG_ERR("TODO", "not enough heap to rename");
    return false;
  }
  if (!todo::renameTask(raw, static_cast<size_t>(index), text)) {
    LOG_ERR("TODO", "rename failed (cap or stale list)");
    resyncDocument();
    return false;
  }
  if (!writeRaw(raw)) {
    LOG_ERR("TODO", "write after rename failed");
    resyncDocument();
    return false;
  }
  reloadAfterEdit(std::move(raw), index);
  return true;
}

bool TodoActivity::applyDelete(const int index) {
  std::string raw;
  if (!readRaw(raw)) {
    LOG_ERR("TODO", "read before delete failed");
    return false;
  }
  if (!todo::deleteTask(raw, static_cast<size_t>(index))) {
    LOG_ERR("TODO", "delete failed (stale list)");
    resyncDocument();
    return false;
  }
  if (!writeRaw(raw)) {
    LOG_ERR("TODO", "write after delete failed");
    resyncDocument();
    return false;
  }
  reloadAfterEdit(std::move(raw), index);
  return true;
}

bool TodoActivity::readRaw(std::string& out) {
  out.clear();
  if (Storage.readFileToString("TODO", kTodoPath, todo::kMaxFileBytes, out)) return true;
  // An empty file is a valid edit base; readFileToString rejects size 0.
  HalFile file = Storage.open(kTodoPath, O_RDONLY);
  if (!file) return false;
  const bool empty = !file.isDirectory() && file.fileSize() == 0;
  file.close();
  return empty;
}

bool TodoActivity::writeRaw(const std::string& content) {
  // Write a temp file and swap it in: a truncate-in-place rewrite would destroy
  // the list on a failed or interrupted write. replaceFile removes the old file
  // and renames the temp over it.
  HalFile file = Storage.open(kTodoTmpPath, O_WRONLY | O_CREAT | O_TRUNC);
  if (!file) {
    LOG_ERR("TODO", "open temp for rewrite failed");
    return false;
  }
  if (!content.empty() &&
      file.write(reinterpret_cast<const uint8_t*>(content.data()), content.size()) != content.size()) {
    LOG_ERR("TODO", "short write");
    file.close();
    Storage.remove(kTodoTmpPath);
    return false;
  }
  file.flush();
  if (!file.close()) {
    LOG_ERR("TODO", "close after rewrite failed");
    Storage.remove(kTodoTmpPath);
    return false;
  }
  if (!Storage.replaceFile(kTodoTmpPath, kTodoPath)) {
    // Leave the complete temp in place; resyncDocument re-reads the original.
    LOG_ERR("TODO", "replace failed");
    return false;
  }
  return true;
}

bool TodoActivity::hasHeapForEdit(const size_t rawSize, const size_t textSize) const {
  // std::string growth can double the buffer, so require room for a second
  // copy of the file plus the new text before the transform mutates it (a
  // failed allocation aborts under -fno-exceptions).
  return heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) >= 2 * (rawSize + textSize) + 512;
}

void TodoActivity::resyncDocument() {
  std::string raw;
  if (readRaw(raw)) {
    reloadAfterEdit(std::move(raw), nav.selected);
    return;
  }
  {
    RenderLock lock(*this);
    doc.clear();
    loadState = todo::LoadState::ReadError;
  }
  requestUpdate();
}

void TodoActivity::reloadAfterEdit(std::string&& raw, const int selectIndex) {
  int select = selectIndex;
  {
    // Replacing doc.buffer while the render task reads it needs the render lock.
    RenderLock lock(*this);
    // Free the old buffers first so the largest-block probe sees real headroom.
    doc.clear();
    const size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    const todo::ParseResult result = todo::parse(std::move(raw), doc, largest);
    loadState = result == todo::ParseResult::NoMemory ? todo::LoadState::ReadError : todo::LoadState::Ok;
    if (select < 0 || select >= static_cast<int>(doc.items.size())) select = static_cast<int>(doc.items.size()) - 1;
    if (select < 0) select = 0;
  }
  moveSelectionTo(select);
}

const char* TodoActivity::headerTitle() const { return tr(STR_TODO_TITLE); }
