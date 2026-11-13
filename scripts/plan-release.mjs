import { appendFile } from "node:fs/promises";
import { resolve } from "node:path";
import { pathToFileURL } from "node:url";
import semanticRelease from "semantic-release";
import config from "../release.config.cjs";

export async function planRelease(env = process.env, release = semanticRelease) {
  if (env.GITHUB_EVENT_NAME !== "push" || env.GITHUB_REF !== "refs/heads/main")
    return { version: "0.0.0", publish: false };
  // Dry-run omits prepare/publish; only the analyzer and notes generator are loaded.
  const result = await release({ ...config, dryRun: true, plugins: config.plugins.slice(0, 2) });
  if (!result) return { version: "0.0.0", publish: false };
  const version = result.nextRelease.version;
  if (!/^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)$/.test(version))
    throw new Error("Invalid planned version");
  return { version, publish: true };
}

if (process.argv[1] && import.meta.url === pathToFileURL(resolve(process.argv[1])).href) {
  if (!process.env.GITHUB_OUTPUT) throw new Error("Run this planner inside GitHub Actions");
  const { version, publish } = await planRelease();
  await appendFile(process.env.GITHUB_OUTPUT, `version=${version}\npublish=${publish}\n`);
}
