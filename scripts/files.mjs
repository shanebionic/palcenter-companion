import { readdir, readFile } from "node:fs/promises";
import path from "node:path";

const ignoredDirectories = new Set([".git", "node_modules", "coverage", "dist"]);
const textExtensions = new Set([".json", ".md", ".mjs", ".yaml", ".yml"]);
const textFileNames = new Set(["LICENSE", ".gitignore", ".prettierignore"]);

export async function listTextFiles(directory = ".") {
  const entries = await readdir(directory, { withFileTypes: true });
  const files = [];

  for (const entry of entries) {
    const filePath = path.join(directory, entry.name);
    if (entry.isDirectory()) {
      if (!ignoredDirectories.has(entry.name)) {
        files.push(...(await listTextFiles(filePath)));
      }
      continue;
    }

    if (textExtensions.has(path.extname(entry.name)) || textFileNames.has(entry.name)) {
      files.push(filePath);
    }
  }

  return files.sort();
}

export async function readText(filePath) {
  return readFile(filePath, "utf8");
}
