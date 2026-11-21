# CrossPoint To-do fork

This is a personal downstream fork of
[crosspoint-reader/crosspoint-reader](https://github.com/crosspoint-reader/crosspoint-reader).
It adds an on-device **To-do** list (see `src/activities/todo/` and
`src/util/TodoFile.*`) that reads and edits `/todo.md` on the SD-card root.

The feature is kept as commits on top of a **stable CrossPoint release**; it is
not intended for upstream. CrossPoint's `SCOPE.md` excludes interactive apps,
so this fork stays downstream.

This file is the fork's agent-facing guide. CrossPoint's `AGENTS.md` (and its
`CLAUDE.md` symlink) are intentionally left **pristine** so they do not conflict
on rebase; all fork-specific context lives here.

## Remotes

- `upstream` — `crosspoint-reader/crosspoint-reader` (read-only; source of stable releases)
- `origin` — this fork (push target)

## Branch model

- `master` is a **linear feature branch on top of a stable CrossPoint release**;
  the feature is the commits above the release. CrossPoint's `master` and the
  stable tag `2.0.0` are the same commit, so `master` sits directly on the
  source repo's `master`.
- Do not develop on CrossPoint's `develop`; rebase the feature onto the stable
  line only. A linear history keeps each update a single `git rebase`.

## Finding the latest stable release

CrossPoint's stable line is `upstream/master`, and releases are tags. Both point
at the same commit at release time:

```sh
git fetch upstream --tags
git describe --tags upstream/master          # e.g. 2.0.0
git tag --sort=-creatordate | grep -v rc | head -1   # newest non-rc tag
```

Base the feature on either `upstream/master` or the newest non-`rc` tag.

## Update the feature to a new stable release

`master` is **protected**: direct pushes are rejected and every change must land
through a pull request that passes CI. Do the update on a branch:

```sh
git fetch upstream --tags
git switch -c update/<new-stable-tag> master
git rebase upstream/master                   # or: git rebase <new-stable-tag>
git submodule update --init --recursive      # the release pins a new freeink-sdk
./bin/clang-format-fix -g
pio run -t unit-tests
pio run -e default
git push -u origin update/<new-stable-tag>
# open a PR into master; merge once CI's "Test Status" is green
```

After the merge, **Fork Release** builds the bin and publishes it (see below).
Because each update starts a new branch, no force-push is needed.

### Conflict guide

The feature edits these files, so a rebase stops on any that CrossPoint changed.
Resolve toward CrossPoint's new structure, then re-apply the To-do hunks:

- `src/activities/home/HomeActivity.{h,cpp}` — Home menu entry, `menuItemToIndex`/
  `indexToMenuItem`, the menu band, and the touch grid.
- `src/activities/ActivityManager.{h,cpp}` — `goToTodo`, `HomeMenuItem::TODO`,
  and the `goHome` name map.
- `src/components/themes/BaseTheme.{h,cpp}`, `themes/lyra/LyraTheme.{h,cpp}`,
  `themes/roundedraff/RoundedRaffTheme.{h,cpp}` — the shared
  `fitMenuRowHeight`/`getMenuRowTopInset` helper and the three
  `drawButtonMenu` bodies.
- `src/components/CoverGridHomeUi.{h,cpp}` — the cover-grid tab ring.
- `lib/I18n/translations/english.yaml` — the `STR_TODO_*` keys.
- `USER_GUIDE.md` — documentation (a CrossPoint file the feature edits).

New files rarely conflict: `src/util/TodoFile.{h,cpp}`,
`src/activities/todo/*`, `src/components/icons/todo.h`, `test/todo_file/*`.

A quick pre-check of what actually changed in the touched files:

```sh
git diff <old-tag> upstream/master -- \
  src/activities/home/HomeActivity.cpp src/activities/ActivityManager.cpp \
  src/components/themes src/components/CoverGridHomeUi.cpp
```

## CI, branch protection, and releases

`master` is protected: no direct pushes, a pull request is required, and the
`Test Status` check must pass before merging. Configure this in the GitHub
repository settings (see the checklist in the fork's notes).

- `.github/workflows/ci.yml` — CrossPoint's own CI runs on every pull request
  (clang-format, cppcheck, host unit tests, and a build for every board). Its
  `Test Status` job is the required branch-protection check. This fork patches
  its cppcheck dependency step to install PCRE1, which newer Ubuntu runners no
  longer package (`libpcre.so.3` is needed by pioarduino's cppcheck binary), and
  to fall back to the Ubuntu archive when `libpcre3` is unavailable. Expect a
  conflict there when CrossPoint updates `ci.yml`; once they fix it upstream,
  take their version.
- `.github/workflows/fork-release.yml` — every push to `master` (a merged PR):
  builds every supported board (`x3-x4`, `sticky`, `x4pro`, `x4c`, `papermono`,
  `metalio_eink4`, `eego_a4`, `picco`) and publishes
  `todopoint-<tag>-<device>.bin` and `todopoint-<tag>-<device>.factory.bin` for
  each as a GitHub Release tagged `todo-<crosspoint-tag>`, where
  `<crosspoint-tag>` is `git describe --tags --abbrev=0 --exclude 'todo-*'`.
  Re-runs replace the assets on the existing release (`--clobber`).

## Build the bin

```sh
pio run -e default
```

Images land in `.pio/build/default/`:

- `firmware.bin` — app image, flash at `0x10000`
- `firmware.factory.bin` — merged image, flash at `0x0`

Flash with `pio run -e default --target upload`, or:

```sh
esptool.py --chip esp32c3 write_flash 0x0 .pio/build/default/firmware.factory.bin
```

## Requirements

- pioarduino Core `6.1.19` (upstream PlatformIO fails on this platform's
  `tool-scons` pin). See `docs/contributing/getting-started.md`.
- Python 3.13.
- `clang-format` 21+ for `./bin/clang-format-fix`.

## Checks

```sh
./bin/clang-format-fix -g
pio run -t unit-tests
pio run -e default
pio check -e default
```
