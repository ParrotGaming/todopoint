# CrossPoint To-do fork

This is a personal downstream fork of
[crosspoint-reader/crosspoint-reader](https://github.com/crosspoint-reader/crosspoint-reader).
It adds an on-device **To-do** list (see `src/activities/todo/` and
`src/util/TodoFile.*`) that reads and edits `/todo.md` on the SD-card root. The
user-facing behavior is documented in `USER_GUIDE.md` (section 3.1.1).

- Fork: `git@github.com:ParrotGaming/todopoint.git` (`origin`)
- Upstream: `git@github.com:crosspoint-reader/crosspoint-reader.git` (`upstream`)

The feature is kept as commits on top of a **stable CrossPoint release**; it is
not intended for upstream. CrossPoint's `SCOPE.md` excludes interactive apps, so
this fork stays downstream.

This file is the fork's agent-facing guide. CrossPoint's `AGENTS.md` (and its
`CLAUDE.md` symlink) are intentionally left **pristine** so they do not conflict
on rebase; all fork-specific context lives here.

## First-time setup (fresh clone)

A fresh clone has only `origin`. Add `upstream` and the submodule before doing
anything else:

```sh
git remote add upstream git@github.com:crosspoint-reader/crosspoint-reader.git
git fetch upstream --tags
git submodule update --init --recursive
```

Install the toolchain per [Requirements](#requirements) before the first build.

## Branch model

- `master` is a **linear feature branch on top of a stable CrossPoint release**;
  the feature is the commits above the release (`git log <tag>..master`).
  CrossPoint's `master` and the stable tag `2.0.0` are the same commit, so
  `master` sits directly on the source repo's `master`.
- Do not develop on CrossPoint's `develop`; rebase the feature onto the stable
  line only. A linear history keeps each update a single `git rebase`.

## Finding the latest stable release

CrossPoint's stable line is its `master` branch, and releases are tags cut from
it (they point at the same commit at release time):

```sh
git fetch upstream --tags
git tag --sort=-creatordate | grep -v rc | head -1   # newest non-rc tag, e.g. 2.0.0
git describe --tags upstream/master                  # the tag upstream/master sits on
```

Prefer the **newest stable tag** as the rebase target. `upstream/master` equals
it at release time, but if CrossPoint commits to `master` between releases,
rebasing onto `master` would pull in unreleased changes and the release tag
(below) would name an older commit than the build.

## Update the feature to a new stable release

`master` is **protected**: direct pushes are rejected and every change must land
through a pull request that passes CI and a required review. Do the update on a
branch:

```sh
git fetch upstream --tags
tag="$(git tag --sort=-creatordate | grep -v rc | head -1)"   # e.g. 2.1.0
git switch -c "update/${tag}" master
git rebase "${tag}"
git submodule update --init --recursive      # the release pins a new freeink-sdk
./bin/clang-format-fix -g
pio run -t unit-tests
pio run -e default
git push -u origin "update/${tag}"
# open a PR into master; merge once CI's "Test Status" is green and it is approved
```

After the merge, **Fork Release** builds every board and replaces the release
assets (see below). Because each update starts a new branch, no force-push is
needed.

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
- `.github/workflows/ci.yml` — the fork's PCRE1 patch to the cppcheck step (see
  [CI, branch protection, and releases](#ci-branch-protection-and-releases)).
  Once CrossPoint fixes that step upstream, take their version.

New files rarely conflict: `src/util/TodoFile.{h,cpp}`,
`src/activities/todo/*`, `src/components/icons/todo.h`, `test/todo_file/*`,
`FORK.md`.

A quick pre-check of what actually changed in the touched files:

```sh
git diff <old-tag> "${tag}" -- \
  src/activities/home/HomeActivity.cpp src/activities/ActivityManager.cpp \
  src/components/themes src/components/CoverGridHomeUi.cpp .github/workflows/ci.yml
```

## CI, branch protection, and releases

`master` is protected: no direct pushes, a pull request is required, and it must
pass CI **and** a review before merging.

- `.github/workflows/ci.yml` — CrossPoint's own CI runs on every pull request
  (clang-format, cppcheck, host unit tests, and a build for every board). Its
  `Test Status` job is the required branch-protection check. This fork patches
  its cppcheck dependency step to install PCRE1, which newer Ubuntu runners no
  longer package (`libpcre.so.3` is needed by pioarduino's cppcheck binary), and
  to fall back to the Ubuntu archive when `libpcre3` is unavailable.
- `.github/workflows/fork-release.yml` — every push to `master` (a merged PR):
  builds every supported board (`x3-x4`, `sticky`, `x4pro`, `x4c`, `papermono`,
  `metalio_eink4`, `eego_a4`, `picco`) and publishes
  `todopoint-<tag>-<device>.bin` and `todopoint-<tag>-<device>.factory.bin` for
  each as a GitHub Release tagged `todo-<crosspoint-tag>`, where
  `<crosspoint-tag>` is `git describe --tags --abbrev=0 --exclude 'todo-*'`.
  Re-runs replace the assets on the existing release (`--clobber`).

### GitHub protection setup (once)

1. **Settings → Actions → General → Workflow permissions → Read and write
   permissions.** Without this, `fork-release.yml` cannot create the tag/release.
2. **Settings → Branches → Add branch protection rule**, pattern `master`:
   - Require a pull request before merging (required approvals: `1`)
   - Require review from Code Owners (`.github/CODEOWNERS`)
   - Require status checks to pass → **`Test Status`**, and require branches to
     be up to date
   - Allow force pushes / deletions: **off**
   - "Do not allow bypassing the above settings": optional
3. **Approval caveat:** GitHub does not let you approve your own PR. With one
   required approval, a solo owner cannot merge without a second approver — add
   a second collaborator and list them in `.github/CODEOWNERS`, or leave the
   bypass option off so the owner can override. This is the intended tradeoff:
   nothing lands without an approval.

## Build the bin

```sh
pio run -e default                 # the C3 X3/X4 target
```

Images land in `.pio/build/default/`:

- `firmware.bin` — app image, flash at `0x10000`
- `firmware.factory.bin` — merged image, flash at `0x0`

Flash with `pio run -e default --target upload`, or:

```sh
esptool.py --chip esp32c3 write_flash 0x0 .pio/build/default/firmware.factory.bin
```

Other targets use their own env (`pio run -e eego_a4`, `pio run -e sticky`,
etc.); the release workflow builds the `*-gh_release` variants of all of them.

## Requirements

- **pioarduino Core `6.1.19`** (upstream PlatformIO 6.2.0 fails on this
  platform's `tool-scons` pin). Install it, then pin the nested platform penv
  that `custom_sdkconfig` envs (`default`, `sticky`) use, or the link fails with
  `No module named 'SCons.Tool.FortranCommon'`:

  ```sh
  python3 -m venv .venv && . .venv/bin/activate
  python -m pip install --upgrade 'https://github.com/pioarduino/platformio-core/archive/refs/tags/v6.1.19.zip'
  python -m pip install uv
  pio pkg install -e default
  uv pip install --python ~/.platformio/penv/bin/python 'pioarduino==6.1.19'
  ```

  Full setup: `docs/contributing/getting-started.md`.
- **Python 3.13**.
- **clang-format 21+** for `./bin/clang-format-fix`.

## Checks

```sh
./bin/clang-format-fix -g
pio run -t unit-tests
pio run -e default
pio check -e default
```

After an update, confirm the feature still works on device (open To-do, toggle,
add/rename/delete); a green build does not prove behavior.
