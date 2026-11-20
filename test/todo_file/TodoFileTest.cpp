#include <gtest/gtest.h>

#include <string>

#include "TodoFile.h"

namespace {

using todo::Document;
using todo::Item;
using todo::ParseResult;

// Parses `text` and returns the document (with the buffer kept alive).
Document parseText(const std::string& text) {
  Document doc;
  todo::parse(std::string(text), doc);
  return doc;
}

std::string label(const Document& doc, const size_t index) {
  const Item& item = doc.items[index];
  if (item.textOffset == 0) return {};
  return std::string(doc.buffer.data() + item.textOffset, item.textLen);
}

}  // namespace

TEST(TodoFile, StandardForms) {
  Document doc = parseText("- [ ] open\n- [x] done\n- [X] doneUpper\n");
  ASSERT_EQ(doc.items.size(), 3u);
  EXPECT_FALSE(doc.items[0].done);
  EXPECT_TRUE(doc.items[1].done);
  EXPECT_TRUE(doc.items[2].done);
  EXPECT_EQ(label(doc, 0), "open");
  EXPECT_EQ(label(doc, 1), "done");
  EXPECT_EQ(label(doc, 2), "doneUpper");
}

TEST(TodoFile, NoSpaceForms) {
  Document doc = parseText("-[ ] open\n-[x] done\n");
  ASSERT_EQ(doc.items.size(), 2u);
  EXPECT_FALSE(doc.items[0].done);
  EXPECT_TRUE(doc.items[1].done);
  EXPECT_EQ(label(doc, 0), "open");
  EXPECT_EQ(label(doc, 1), "done");
}

TEST(TodoFile, IndentedAndMixedWhitespace) {
  Document doc = parseText("  - [ ] two spaces\n\t- [x]\ttab separated\n \t- [X] mixed\n");
  ASSERT_EQ(doc.items.size(), 3u);
  EXPECT_EQ(label(doc, 0), "two spaces");
  EXPECT_EQ(label(doc, 1), "tab separated");
  EXPECT_TRUE(doc.items[2].done);
  EXPECT_EQ(label(doc, 2), "mixed");
}

TEST(TodoFile, CrlfAndFinalLineWithoutNewline) {
  Document doc = parseText("- [ ] first\r\n- [x] second");
  ASSERT_EQ(doc.items.size(), 2u);
  EXPECT_EQ(label(doc, 0), "first");
  EXPECT_EQ(label(doc, 1), "second");
  // The CR must not leak into the label.
  EXPECT_EQ(label(doc, 0).find('\r'), std::string::npos);
  EXPECT_EQ(label(doc, 1).find('\r'), std::string::npos);
}

TEST(TodoFile, Utf8BomIsSkipped) {
  Document doc = parseText("\xEF\xBB\xBF- [ ] bom\n");
  ASSERT_EQ(doc.items.size(), 1u);
  EXPECT_EQ(label(doc, 0), "bom");
  // The marker offset is past the three BOM bytes.
  EXPECT_EQ(doc.items[0].markOffset, 6u);
}

TEST(TodoFile, IgnoredLines) {
  Document doc = parseText(
      "# Heading\n"
      "plain text\n"
      "* [ ] different bullet\n"
      "+ [ ] plus bullet\n"
      "1. [ ] ordered list\n"
      "- not a task\n"
      "-[ ]no-space-text\n"
      "---\n"
      "```\n"
      "- [ ] real\n"
      "```\n");
  ASSERT_EQ(doc.items.size(), 1u);
  EXPECT_EQ(label(doc, 0), "real");
}

TEST(TodoFile, ExactMarkOffsets) {
  // Offsets are absolute into the buffer; each line's start is cumulative.
  Document doc = parseText("- [ ] abc\n-[x] de\n  - [X] fg\n\t- [ ]\thi\n");
  ASSERT_EQ(doc.items.size(), 4u);
  EXPECT_EQ(doc.items[0].markOffset, 3u);
  EXPECT_EQ(doc.items[0].textOffset, 6u);
  EXPECT_EQ(doc.items[0].textLen, 3u);
  EXPECT_EQ(doc.items[1].markOffset, 12u);
  EXPECT_EQ(doc.items[1].textOffset, 15u);
  EXPECT_EQ(doc.items[2].markOffset, 23u);
  EXPECT_EQ(doc.items[2].textOffset, 26u);
  EXPECT_EQ(doc.items[3].markOffset, 33u);
  EXPECT_EQ(doc.items[3].textOffset, 36u);
}

TEST(TodoFile, SetDoneChangesOnlyTheMarkerByte) {
  Document doc = parseText("- [ ] keep every other byte\r\n- [x] second\n");
  ASSERT_EQ(doc.items.size(), 2u);
  const std::string before = doc.buffer;
  todo::setDone(doc, 0, true);
  ASSERT_EQ(doc.buffer.size(), before.size());
  for (size_t i = 0; i < before.size(); ++i) {
    if (i == doc.items[0].markOffset) {
      EXPECT_EQ(doc.buffer[i], 'X') << "at " << i;
    } else {
      EXPECT_EQ(doc.buffer[i], before[i]) << "at " << i;
    }
  }
  EXPECT_TRUE(doc.items[0].done);

  todo::setDone(doc, 0, false);
  EXPECT_EQ(doc.buffer[doc.items[0].markOffset], ' ');
  EXPECT_FALSE(doc.items[0].done);
}

TEST(TodoFile, RecheckingLowercaseNormalizesToUppercase) {
  Document doc = parseText("- [x] lower\n");
  ASSERT_EQ(doc.items.size(), 1u);
  todo::setDone(doc, 0, true);
  EXPECT_EQ(doc.buffer[doc.items[0].markOffset], 'X');
}

TEST(TodoFile, EmptyBufferIsEmptyResult) {
  Document doc = parseText("");
  EXPECT_TRUE(doc.items.empty());
}

TEST(TodoFile, NoTaskLinesIsEmptyResult) {
  Document doc = parseText("# Only headings\n\nsome prose\n");
  EXPECT_EQ(todo::parse(std::string("# Only headings\n"), doc), ParseResult::Empty);
  EXPECT_TRUE(doc.items.empty());
}

TEST(TodoFile, TextlessTaskHasNoTextPointer) {
  Document doc = parseText("- [ ]\n- [ ]   \n");
  ASSERT_EQ(doc.items.size(), 2u);
  EXPECT_EQ(doc.items[0].textOffset, 0u);
  EXPECT_EQ(doc.items[0].textLen, 0u);
  EXPECT_EQ(doc.items[1].textOffset, 0u);
  EXPECT_EQ(label(doc, 0), "");
  EXPECT_EQ(label(doc, 1), "");
}

TEST(TodoFile, ClassifyReadFailure) {
  EXPECT_EQ(todo::classifyReadFailure(0), todo::LoadState::Empty);
  EXPECT_EQ(todo::classifyReadFailure(100), todo::LoadState::ReadError);
  EXPECT_EQ(todo::classifyReadFailure(todo::kMaxFileBytes), todo::LoadState::ReadError);
  EXPECT_EQ(todo::classifyReadFailure(todo::kMaxFileBytes + 1), todo::LoadState::TooLarge);
}

TEST(TodoFile, TextIsNulTerminatedAndExcludesCr) {
  Document doc = parseText("- [ ] hello world\r\n- [x] tail\n");
  ASSERT_EQ(doc.items.size(), 2u);
  const Item& first = doc.items[0];
  EXPECT_EQ(doc.buffer[first.textOffset + first.textLen], '\0');
  EXPECT_EQ(label(doc, 0), "hello world");
}

TEST(TodoFile, ManyTasksReserveExactly) {
  std::string text;
  constexpr int kCount = 1000;
  for (int i = 0; i < kCount; ++i) {
    text += "- [ ] task " + std::to_string(i) + "\n";
  }
  Document doc = parseText(text);
  ASSERT_EQ(doc.items.size(), static_cast<size_t>(kCount));
  EXPECT_EQ(doc.items.capacity(), static_cast<size_t>(kCount));
  EXPECT_EQ(label(doc, 999), "task 999");
}

TEST(TodoFile, NoMemoryWhenItemsCannotBeReserved) {
  std::string text;
  for (int i = 0; i < 100; ++i) text += "- [ ] task\n";
  Document doc;
  // 100 items need ~1200 bytes plus headroom; a 16-byte largest block cannot
  // satisfy the reserve, so parse refuses instead of aborting.
  EXPECT_EQ(todo::parse(std::string(text), doc, 16), todo::ParseResult::NoMemory);
  EXPECT_TRUE(doc.items.empty());
  EXPECT_TRUE(doc.buffer.empty());

  // The same input parses when the caller reports enough room.
  EXPECT_EQ(todo::parse(std::string(text), doc, 64 * 1024), todo::ParseResult::Ok);
  EXPECT_EQ(doc.items.size(), 100u);
}

TEST(TodoFile, InsertAfterTask) {
  std::string file = "- [ ] one\n- [x] two\n";
  EXPECT_TRUE(todo::insertTaskAfter(file, 0, "new"));
  EXPECT_EQ(file, "- [ ] one\n- [ ] new\n- [x] two\n");
}

TEST(TodoFile, InsertAfterLastTask) {
  std::string file = "- [ ] one\n- [x] two\n";
  EXPECT_TRUE(todo::insertTaskAfter(file, 1, "new"));
  EXPECT_EQ(file, "- [ ] one\n- [x] two\n- [ ] new\n");
}

TEST(TodoFile, InsertIntoEmptyFile) {
  std::string file;
  EXPECT_TRUE(todo::insertTaskAfter(file, 0, "first"));
  EXPECT_EQ(file, "- [ ] first\n");
}

TEST(TodoFile, InsertKeepsNoTrailingNewlineStyle) {
  std::string file = "- [ ] one\n- [x] two";
  EXPECT_TRUE(todo::insertTaskAfter(file, 1, "new"));
  EXPECT_EQ(file, "- [ ] one\n- [x] two\n- [ ] new");
}

TEST(TodoFile, InsertUsesCrlfStyle) {
  std::string file = "- [ ] one\r\n- [x] two\r\n";
  EXPECT_TRUE(todo::insertTaskAfter(file, 0, "new"));
  EXPECT_EQ(file, "- [ ] one\r\n- [ ] new\r\n- [x] two\r\n");
}

TEST(TodoFile, InsertTextlessTask) {
  std::string file = "- [ ] one\n";
  EXPECT_TRUE(todo::insertTaskAfter(file, 0, ""));
  EXPECT_EQ(file, "- [ ] one\n- [ ]\n");
}

TEST(TodoFile, RenameKeepsPrefixAndState) {
  std::string file = "  -[x]\told text\n";
  EXPECT_TRUE(todo::renameTask(file, 0, "new text"));
  EXPECT_EQ(file, "  -[x]\tnew text\n");
}

TEST(TodoFile, RenameTextlessTaskAddsSeparator) {
  std::string file = "- [ ]\n";
  EXPECT_TRUE(todo::renameTask(file, 0, "added"));
  EXPECT_EQ(file, "- [ ] added\n");
}

TEST(TodoFile, RenameToEmptyLeavesTextlessTask) {
  std::string file = "- [ ] old\n";
  EXPECT_TRUE(todo::renameTask(file, 0, ""));
  EXPECT_EQ(file, "- [ ]\n");
}

TEST(TodoFile, RenamePreservesCrlfAndOtherLines) {
  std::string file = "- [ ] a\r\n- [x] b\r\n# heading\r\n";
  EXPECT_TRUE(todo::renameTask(file, 1, "renamed"));
  EXPECT_EQ(file, "- [ ] a\r\n- [x] renamed\r\n# heading\r\n");
}

TEST(TodoFile, DeleteTaskRemovesWholeLine) {
  std::string file = "- [ ] a\n- [x] b\n- [ ] c\n";
  EXPECT_TRUE(todo::deleteTask(file, 1));
  EXPECT_EQ(file, "- [ ] a\n- [ ] c\n");
}

TEST(TodoFile, DeleteOnlyTaskLeavesEmptyFile) {
  std::string file = "- [ ] only\n";
  EXPECT_TRUE(todo::deleteTask(file, 0));
  EXPECT_TRUE(file.empty());
}

TEST(TodoFile, DeleteLastLineWithoutTerminator) {
  std::string file = "- [ ] a\n- [ ] b";
  EXPECT_TRUE(todo::deleteTask(file, 1));
  EXPECT_EQ(file, "- [ ] a\n");
}

TEST(TodoFile, TransformsRejectOutOfRange) {
  std::string file = "- [ ] a\n";
  EXPECT_FALSE(todo::renameTask(file, 5, "x"));
  EXPECT_FALSE(todo::deleteTask(file, 5));
  EXPECT_EQ(file, "- [ ] a\n");
}

TEST(TodoFile, InsertRejectsWhenOverCap) {
  std::string file;
  while (file.size() + 8 <= todo::kMaxFileBytes) file += "- [ ]\n";
  const size_t before = file.size();
  EXPECT_FALSE(todo::insertTaskAfter(file, 0, std::string(100, 'x')));
  EXPECT_EQ(file.size(), before);
}

TEST(TodoFile, InsertIntoUnterminatedCrlfFileUsesCrlf) {
  std::string file = "- [ ] one\r\n- [ ] two";
  EXPECT_TRUE(todo::insertTaskAfter(file, 1, "new"));
  EXPECT_EQ(file, "- [ ] one\r\n- [ ] two\r\n- [ ] new");
}

TEST(TodoFile, RenameTextlessPreservesTrailingWhitespace) {
  std::string file = "- [ ]   \n";
  EXPECT_TRUE(todo::renameTask(file, 0, "added"));
  EXPECT_EQ(file, "- [ ]   added\n");
}
