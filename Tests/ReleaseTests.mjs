import assert from "node:assert/strict";
import { test } from "node:test";
import { mkdtemp, mkdir, writeFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { createHash } from "node:crypto";
import { spawnSync } from "node:child_process";
import { analyzeCommits } from "@semantic-release/commit-analyzer";
import { generateNotes } from "@semantic-release/release-notes-generator";
import config from "../release.config.cjs";
import { platforms, verifyRelease } from "../scripts/verify-release.mjs";

// Exercise the asset resolver of the locked GitHub plugin without publishing.
const { default: globAssets } = await import(new URL("lib/glob-assets.js", import.meta.resolve("@semantic-release/github")));

const analyzer = config.plugins.find(([name]) => name === "@semantic-release/commit-analyzer")[1];
const notesOptions = config.plugins.find(([name]) => name === "@semantic-release/release-notes-generator")[1];
const logger = { log() {} };

for (const [message, expected] of [
  ["fix: improve low-note detection", "patch"],
  ["perf: reduce analysis CPU usage", "patch"],
  ["feat: add manual frequency reference", "minor"],
  ["feat!: change tuning parameter format", "major"],
  ["fix: migrate parameters\n\nBREAKING CHANGE: existing sessions need migration", "major"],
  ["docs: explain installation", null],
  ["ci: configure VST3 releases", null],
]) {
  test(`release classification: ${message.split("\n")[0]}`, async () => {
    const result = await analyzeCommits(analyzer, {
      cwd: process.cwd(), logger, commits: [{ hash: "abc1234", message }],
    });
    assert.equal(result, expected);
  });
}

test("release notes describe the plugin changes", async () => {
  const notes = await generateNotes(notesOptions, {
    cwd: process.cwd(), logger,
    options: { repositoryUrl: "https://github.com/example/zazza.git" },
    commits: [{ hash: "0123456789012345678901234567890123456789", message: "feat: add manual frequency reference" }],
    lastRelease: { version: "1.0.0", gitTag: "v1.0.0" },
    nextRelease: { version: "1.1.0", gitTag: "v1.1.0" },
  });
  assert.match(notes, /1\.1\.0/);
  assert.match(notes, /add manual frequency reference/);
});

test("GitHub plugin resolves all native archives and checksums", async () => {
  const cwd = await mkdtemp(join(tmpdir(), "zazza-release-assets-"));
  await mkdir(join(cwd, "dist"));
  const expected = [];
  for (const platform of platforms) {
    const zip = `dist/Zazza-1.2.3-${platform}.zip`;
    await writeFile(join(cwd, zip), "archive fixture");
    await writeFile(join(cwd, `${zip}.sha256`), "checksum fixture");
    expected.push(zip, `${zip}.sha256`);
  }
  await writeFile(join(cwd, "dist/unrelated.txt"), "not a release asset");
  const githubOptions = config.plugins.find(([name]) => name === "@semantic-release/github")[1];
  const assets = await globAssets({ cwd }, githubOptions.assets);
  assert.deepEqual(assets.map((asset) => asset.path).sort(), expected.sort());
});

async function packages(change = () => {}) {
  const directory = await mkdtemp(join(tmpdir(), "zazza-native-packages-"));
  for (const platform of platforms) {
    const stem = `Zazza-1.2.3-${platform}`;
    const content = Buffer.from(`native package fixture: ${platform}`);
    const sha256 = createHash("sha256").update(content).digest("hex");
    const metadata = { version: "1.2.3", platform, commit: "tested-commit", archive: `${stem}.zip`, sha256,
      formats: ["VST3", "Standalone"], pluginval: { passed: true, strictness: 5, gui: true } };
    change(metadata);
    await writeFile(join(directory, `${stem}.zip`), content);
    await writeFile(join(directory, `${stem}.zip.sha256`), `${sha256}  ${stem}.zip\n`);
    await writeFile(join(directory, `${stem}.json`), JSON.stringify(metadata));
  }
  return directory;
}

test("publication accepts three packages built from the exact version and commit", async () => {
  await verifyRelease("1.2.3", await packages(), "tested-commit");
});
test("publication refuses a different commit", async () => {
  await assert.rejects(verifyRelease("1.2.3", await packages(), "untested-commit"), /mismatch/);
});
test("publication refuses a changed embedded version declaration", async () => {
  await assert.rejects(verifyRelease("1.2.3", await packages((m) => { m.version = "1.2.2"; }), "tested-commit"), /mismatch/);
});
test("publication refuses missing Standalone in a package", async () => {
  await assert.rejects(verifyRelease("1.2.3", await packages((m) => { m.formats = ["VST3"]; }), "tested-commit"), /format/);
});
test("publication refuses corrupted bytes", async () => {
  const directory = await packages();
  await writeFile(join(directory, "Zazza-1.2.3-windows-x64.zip"), "corrupted");
  await assert.rejects(verifyRelease("1.2.3", directory, "tested-commit"), /Checksum/);
});
test("publication refuses absent builds", async () => {
  const directory = await mkdtemp(join(tmpdir(), "zazza-missing-platform-"));
  await assert.rejects(verifyRelease("1.2.3", directory, "tested-commit"), /ENOENT/);
});
test("publication refuses stale extra archives", async () => {
  const directory = await packages();
  await writeFile(join(directory, "Zazza-1.2.2-macOS-arm64.zip"), "old package");
  await assert.rejects(verifyRelease("1.2.3", directory, "tested-commit"), /Unexpected archive/);
});
test("publication refuses stale extra checksums", async () => {
  const directory = await packages();
  await writeFile(join(directory, "Zazza-1.2.2-windows-x64.zip.sha256"), "old checksum");
  await assert.rejects(verifyRelease("1.2.3", directory, "tested-commit"), /Unexpected archive/);
});
for (const event of ["pull_request", "workflow_dispatch"]) {
  test(`${event} creates only a preview version plan`, async () => {
    const directory = await mkdtemp(join(tmpdir(), "zazza-plan-"));
    const output = join(directory, "output");
    const result = spawnSync(process.execPath, ["scripts/plan-release.mjs"], { encoding: "utf8",
      env: { ...process.env, GITHUB_EVENT_NAME: event, GITHUB_REF: "refs/heads/main", GITHUB_OUTPUT: output } });
    assert.equal(result.status, 0, result.stderr);
    const { readFile } = await import("node:fs/promises");
    assert.equal(await readFile(output, "utf8"), "version=0.0.0\npublish=false\n");
  });
}

test("publication refuses unvalidated plugins", async () => {
  await assert.rejects(verifyRelease("1.2.3", await packages((m) => { m.pluginval = null; }), "tested-commit"), /pluginval/);
});

const { planRelease } = await import("../scripts/plan-release.mjs");
const mainPush = { GITHUB_EVENT_NAME: "push", GITHUB_REF: "refs/heads/main" };
test("main push plans the exact version without loading publishing plugins", async () => {
  const plan = await planRelease(mainPush, async (options) => {
    assert.equal(options.dryRun, true);
    assert.deepEqual(options.plugins.map(([name]) => name), ["@semantic-release/commit-analyzer", "@semantic-release/release-notes-generator"]);
    return { nextRelease: { version: "1.2.3" } };
  });
  assert.deepEqual(plan, { version: "1.2.3", publish: true });
});
test("no releasable commits produce only preview builds", async () => {
  assert.deepEqual(await planRelease(mainPush, async () => false), { version: "0.0.0", publish: false });
});
test("other branches and tags cannot plan publication", async () => {
  for (const ref of ["refs/heads/master", "refs/tags/v1.2.3"]) {
    assert.deepEqual(await planRelease({ ...mainPush, GITHUB_REF: ref }, () => { throw new Error("must not call release"); }), { version: "0.0.0", publish: false });
  }
});
test("invalid planned versions fail instead of entering workflow outputs", async () => {
  await assert.rejects(planRelease(mainPush, async () => ({ nextRelease: { version: "1.2.3\npublish=true" } })), /Invalid planned version/);
});
