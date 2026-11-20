#pragma once

#include <cstdint>
#include <string>

#include "activities/UiListActivity.h"
#include "components/OptionPopup.h"
#include "util/TodoFile.h"

// Reads `/todo.md` from the SD-card root, lists its Markdown task lines, and
// edits them on-device: Confirm/tap toggles the trailing checkbox by patching
// one byte, and a long press opens Add / Rename / Delete (the keyboard supplies
// text). Creates an empty `/todo.md` when the file is missing. The list rows are
// provided virtually (todo::Document) rather than materialized as ListItems.
class TodoActivity final : public UiListActivity {
 public:
  TodoActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : UiListActivity("Todo", renderer, mappedInput, /*wantsTouchLongPress=*/true) {}

  void onEnter() override;
  void onExit() override;

 private:
  todo::Document doc;
  todo::LoadState loadState = todo::LoadState::Ok;
  OptionPopup optionPopup;

  int listCount() const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void onRowLongPress(int index) override;
  bool handleCustomInput() override;
  bool handleButtons() override;
  void render(RenderLock&& lock) override;
  const char* headerTitle() const override;

  static void provideRow(void* ctx, uint16_t index, freeink::ui::ListItem& item);

  // Two write strategies share /todo.md:
  //  - Toggle patches one validated marker byte in place (writeMark); a stale
  //    offset can never touch another task.
  //  - Add/Rename/Delete rewrite the whole file through a temp file + rename
  //    (writeRaw); on a failed edit the document is re-read so stale offsets are
  //    never used against a file that changed.
  // Toggle path: validate the on-disk marker, then patch one byte.
  bool writeMark(uint32_t offset, bool wasDone);

  // True when the screen holds a valid (possibly empty) task list; Empty and Ok
  // both mean "a list", the other states mean "no list".
  bool listAvailable() const { return loadState == todo::LoadState::Ok || loadState == todo::LoadState::Empty; }

  // Editor path. `afterIndex < 0` appends.
  void showActions();
  void startAdd(int afterIndex);
  void startRename(int index);
  void startDelete(int index);
  bool applyInsert(int afterIndex, const std::string& text);
  bool applyRename(int index, const std::string& text);
  bool applyDelete(int index);

  bool readRaw(std::string& out);
  bool writeRaw(const std::string& content);
  // The edit transforms grow `raw` in place; std::string may double its buffer,
  // so a second copy of the file plus the new text must fit or the allocation
  // aborts (-fno-exceptions).
  bool hasHeapForEdit(size_t rawSize, size_t textSize) const;
  // Re-reads /todo.md and re-parses it, used after a failed rewrite so stale
  // offsets are never used against a file that changed.
  void resyncDocument();
  // Re-parses `raw` into `doc` under the render lock and selects `selectIndex`
  // (clamped) in the new list.
  void reloadAfterEdit(std::string&& raw, int selectIndex);
};
