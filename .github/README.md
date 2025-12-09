# CI and Release Workflows

Current workflows and triggers:

- `dev-smoke-test.yml` — Trigger: push to `dev`. Runs full unit tests, builds only `esp32s3`, uploads firmware/littlefs/merged artifacts. (Duplicate of `dev-push-light.yml` if both are kept.)
- `dev-push-light.yml` — Trigger: push to `dev`. Same as above: tests + single `esp32s3` build with artifacts.
- `build-and-CI.yml` — Trigger: pull_request to `dev` (or manual dispatch). Runs tests, then matrix-builds all boards from `tools/build-targets.yml`, uploads artifacts per board.
- `release.yml` — Triggers: pull_request to `build` or to `main` when head branch is `dev` (smoke only); push tags `v*` (publishes if tag is on `main`); manual `workflow_dispatch` (publishes). PR runs build/tests for all boards but does not publish. Tag pushes on `main` and manual runs publish; manual uses `version_override` (default `0.0.0-dev`).
- `run-tests.yml` — Manual tests with filter/quick/skip flags; uploads test binaries on failure. No firmware builds.

Branch intent:
- Do work on `dev`; PRs from `dev` to `build` confirm stability.
- PRs from `build` to `main` run the release smoke; publishing happens by tagging the merge commit on `main` or by manual Release workflow dispatch.

## How to publish a release (manual)
1) Go to **Actions → Release Firmware → Run workflow**.  
2) Set `version_override` to the release version (e.g., `1.2.3`); leave blank to use `0.0.0-dev`.  
3) Run the workflow. It will test and build all boards, then create a GitHub release with artifacts and checksums.

## How to publish via tag (main only)
1) Merge `build` into `main` (after smoke tests).  
2) Tag the merge commit: `git tag vX.Y.Z && git push origin vX.Y.Z`.  
3) The tag push on `main` will run the release workflow, build all boards, and publish a GitHub release. Tags not on `main` are skipped.

## Smoke testing the release workflow
- PRs to `build`, or PRs to `main` with head branch `dev`, run the full release pipeline (tests + all-board builds) but skip publishing. Use this to validate before running a manual release.

## Removing a bad release
- Delete via GitHub Releases UI, or via CLI: `gh release delete <tag> -y` and `git push origin :refs/tags/<tag>`.

## Maintaining the build matrix
- Board list comes from `tools/build-targets.yml`. Add/remove envs there to affect PR CI and release builds.
