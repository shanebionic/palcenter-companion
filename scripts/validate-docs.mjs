import { access } from "node:fs/promises";
import path from "node:path";
import { listTextFiles, readText } from "./files.mjs";

const errors = [];
const markdownFiles = (await listTextFiles()).filter((filePath) => filePath.endsWith(".md"));
const linkPattern = /\[[^\]]+\]\(([^)]+)\)/gu;

for (const filePath of markdownFiles) {
  const content = await readText(filePath);
  for (const match of content.matchAll(linkPattern)) {
    const destination = match[1].trim();
    if (
      destination.startsWith("http://") ||
      destination.startsWith("https://") ||
      destination.startsWith("mailto:") ||
      destination.startsWith("#")
    ) {
      continue;
    }

    const withoutFragment = destination.split("#", 1)[0];
    const resolvedPath = path.resolve(path.dirname(filePath), decodeURIComponent(withoutFragment));
    try {
      await access(resolvedPath);
    } catch {
      errors.push(`${filePath}: broken local link ${destination}`);
    }
  }
}

if (errors.length > 0) {
  console.error(errors.join("\n"));
  process.exitCode = 1;
} else {
  console.log(`Documentation links validated across ${markdownFiles.length} files.`);
}
